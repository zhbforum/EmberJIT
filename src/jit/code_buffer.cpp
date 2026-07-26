#include "ember/jit/code_buffer.hpp"
#include "ember/jit/platform.hpp"

#include <cstring>
#include <utility>

#if EMBER_HAS_WIN64_JIT
#include <windows.h>
#endif

namespace ember::jit
{
CodeBuffer::CodeBuffer(CodeBuffer &&other) noexcept
    : memory_(std::exchange(other.memory_, nullptr)), size_(std::exchange(other.size_, 0))
{
}

auto CodeBuffer::operator=(CodeBuffer &&other) noexcept -> CodeBuffer &
{
    if (this != &other)
    {
        reset();
        memory_ = std::exchange(other.memory_, nullptr);
        size_ = std::exchange(other.size_, 0);
    }
    return *this;
}

CodeBuffer::~CodeBuffer()
{
    reset();
}

CodeBufferCreateResult CodeBuffer::create(std::span<const std::byte> code) noexcept
{
    if (code.empty())
        return {.buffer = std::nullopt, .error = CodeBufferError::emptyCode};

#if EMBER_HAS_WIN64_JIT
    void *const memory = ::VirtualAlloc(nullptr, code.size(), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (memory == nullptr)
        return {.buffer = std::nullopt, .error = CodeBufferError::allocationFailed};

    std::memcpy(memory, code.data(), code.size());
    DWORD previousProtection{};
    if (::VirtualProtect(memory, code.size(), PAGE_EXECUTE_READ, &previousProtection) == 0)
    {
        static_cast<void>(::VirtualFree(memory, 0, MEM_RELEASE));
        return {.buffer = std::nullopt, .error = CodeBufferError::protectionFailed};
    }
    if (::FlushInstructionCache(::GetCurrentProcess(), memory, code.size()) == 0)
    {
        static_cast<void>(::VirtualFree(memory, 0, MEM_RELEASE));
        return {.buffer = std::nullopt, .error = CodeBufferError::cacheFlushFailed};
    }
    return {.buffer = CodeBuffer{memory, code.size()}, .error = CodeBufferError::none};
#else
    return {.buffer = std::nullopt, .error = CodeBufferError::unsupportedTarget};
#endif
}

void CodeBuffer::reset() noexcept
{
#if EMBER_HAS_WIN64_JIT
    if (memory_ != nullptr)
        static_cast<void>(::VirtualFree(memory_, 0, MEM_RELEASE));
#endif
    memory_ = nullptr;
    size_ = 0;
}
} // namespace ember::jit
