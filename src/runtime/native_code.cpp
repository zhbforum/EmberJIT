#include "ember/runtime/native_code.hpp"

#include <cassert>
#include <bit>
#include <utility>

namespace ember::runtime
{
NativeCodeCreateResult
NativeCodeHandle::publishI64Binary(jit::x64::MachineCode machineCode) noexcept
{
    auto code = jit::CodeBuffer::create(machineCode.bytes());
    if (!code.buffer)
        return {.handle = std::nullopt, .error = code.error};
    return {.handle = NativeCodeHandle{std::move(*code.buffer)}, .error = jit::CodeBufferError::none};
}

NativeCodeCreateResult
NativeCodeHandle::publishWordFrame(jit::x64::MachineCode machineCode) noexcept
{
    auto code = jit::CodeBuffer::create(machineCode.bytes());
    if (!code.buffer)
        return {.handle = std::nullopt, .error = code.error};
    return {.handle = NativeCodeHandle{std::move(*code.buffer)}, .error = jit::CodeBufferError::none};
}

std::int64_t NativeCodeHandle::invokeI64Binary(std::int64_t left, std::int64_t right) const
{
    assert(valid());
    // The trusted publication contract fixes the signature. On Windows x64
    // the first two i64 arguments are RCX/RDX and the i64 result is RAX. This
    // is the sole object-pointer-to-function-pointer conversion in the project.
    using EntryPoint = std::int64_t (*)(std::int64_t, std::int64_t);
    const auto function = reinterpret_cast<EntryPoint>(entryAddress());
    return function(left, right);
}

std::uint64_t NativeCodeHandle::invokeWordFrame(NativeFrame &frame) const
{
    assert(valid());
    assert(frame.localCount == 0 || frame.locals != nullptr);
    using EntryPoint = std::uint64_t (*)(NativeFrame *);
    const auto function = reinterpret_cast<EntryPoint>(entryAddress());
    return function(&frame);
}

std::int64_t NativeCodeHandle::invokeI64Frame(NativeFrame &frame) const
{
    return std::bit_cast<std::int64_t>(invokeWordFrame(frame));
}
} // namespace ember::runtime
