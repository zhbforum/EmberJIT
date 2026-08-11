#include "ember/jit/code_buffer.hpp"
#include "ember/jit/platform.hpp"

#include <atomic>
#include <cstring>
#include <utility>

#if EMBER_HAS_WIN64_JIT
#include <windows.h>
#endif

namespace ember::jit
{
namespace
{
#if EMBER_HAS_WIN64_JIT
std::atomic_size_t liveExecutableAllocationCount{};

void releaseExecutableAllocation(void *memory) noexcept
{
    if (::VirtualFree(memory, 0, MEM_RELEASE) != 0)
        static_cast<void>(liveExecutableAllocationCount.fetch_sub(1, std::memory_order_relaxed));
}
#endif
} // namespace

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
    return create(code, TestHooks{});
}

CodeBufferCreateResult CodeBuffer::create(std::span<const std::byte> code, TestHooks testHooks) noexcept
{
    if (code.empty())
        return {.buffer = std::nullopt, .error = CodeBufferError::emptyCode};

#if EMBER_HAS_WIN64_JIT
    void *const memory = ::VirtualAlloc(nullptr, code.size(), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (memory == nullptr)
        return {.buffer = std::nullopt, .error = CodeBufferError::allocationFailed};
    static_cast<void>(liveExecutableAllocationCount.fetch_add(1, std::memory_order_relaxed));
    if (testHooks.observedStage != nullptr)
        *testHooks.observedStage = TestStage::executableAllocated;
    if (testHooks.failpoint == TestFailpoint::afterAllocation)
    {
        releaseExecutableAllocation(memory);
        return {.buffer = std::nullopt, .error = CodeBufferError::testFailure};
    }

    std::memcpy(memory, code.data(), code.size());
    if (testHooks.observedStage != nullptr)
        *testHooks.observedStage = TestStage::executableWritten;
    if (testHooks.failpoint == TestFailpoint::afterWrite)
    {
        releaseExecutableAllocation(memory);
        return {.buffer = std::nullopt, .error = CodeBufferError::testFailure};
    }

    DWORD previousProtection{};
    if (::VirtualProtect(memory, code.size(), PAGE_EXECUTE_READ, &previousProtection) == 0)
    {
        releaseExecutableAllocation(memory);
        return {.buffer = std::nullopt, .error = CodeBufferError::protectionFailed};
    }
    if (testHooks.observedStage != nullptr)
        *testHooks.observedStage = TestStage::executableProtected;
    if (testHooks.failpoint == TestFailpoint::afterProtection)
    {
        releaseExecutableAllocation(memory);
        return {.buffer = std::nullopt, .error = CodeBufferError::testFailure};
    }

    if (::FlushInstructionCache(::GetCurrentProcess(), memory, code.size()) == 0)
    {
        releaseExecutableAllocation(memory);
        return {.buffer = std::nullopt, .error = CodeBufferError::cacheFlushFailed};
    }
    return {.buffer = CodeBuffer{memory, code.size()}, .error = CodeBufferError::none};
#else
    static_cast<void>(testHooks);
    return {.buffer = std::nullopt, .error = CodeBufferError::unsupportedTarget};
#endif
}

std::size_t CodeBuffer::liveExecutableAllocationCountForTesting() noexcept
{
#if EMBER_HAS_WIN64_JIT
    return liveExecutableAllocationCount.load(std::memory_order_relaxed);
#else
    return 0;
#endif
}

void CodeBuffer::reset() noexcept
{
#if EMBER_HAS_WIN64_JIT
    if (memory_ != nullptr)
        releaseExecutableAllocation(memory_);
#endif
    memory_ = nullptr;
    size_ = 0;
}
} // namespace ember::jit
