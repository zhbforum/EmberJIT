#include "ember/runtime/native_code.hpp"

#include <bit>
#include <cassert>
#include <utility>

namespace ember::runtime {
NativeCodeCreateResult
NativeCodeHandle::publishI64Binary(jit::x64::MachineCode machineCode) noexcept {
    auto code = jit::CodeBuffer::create(machineCode.bytes());
    if (!code.buffer)
        return {.handle = std::nullopt, .error = code.error};
    return {.handle = NativeCodeHandle{std::move(*code.buffer)},
            .error = jit::CodeBufferError::none};
}

NativeCodeCreateResult
NativeCodeHandle::publishWordFrame(jit::x64::MachineCode machineCode) noexcept {
    return publishWordFrameWithFailpointForTesting(std::move(machineCode),
                                                   PublicationFailpointForTesting::none,
                                                   nullptr);
}

NativeCodeCreateResult NativeCodeHandle::publishWordFrameWithFailpointForTesting(
    jit::x64::MachineCode machineCode,
    PublicationFailpointForTesting failpoint,
    PublicationStageForTesting* observedStage) noexcept {
    jit::CodeBuffer::TestFailpoint codeBufferFailpoint{jit::CodeBuffer::TestFailpoint::none};
    switch (failpoint) {
    case PublicationFailpointForTesting::none:
        break;
    case PublicationFailpointForTesting::afterExecutableAllocation:
        codeBufferFailpoint = jit::CodeBuffer::TestFailpoint::afterAllocation;
        break;
    case PublicationFailpointForTesting::afterExecutableWrite:
        codeBufferFailpoint = jit::CodeBuffer::TestFailpoint::afterWrite;
        break;
    case PublicationFailpointForTesting::afterExecutableProtection:
        codeBufferFailpoint = jit::CodeBuffer::TestFailpoint::afterProtection;
        break;
    }

    jit::CodeBuffer::TestStage codeBufferStage{jit::CodeBuffer::TestStage::none};
    auto code = jit::CodeBuffer::create(
        machineCode.bytes(),
        {.failpoint = codeBufferFailpoint, .observedStage = &codeBufferStage});
    if (observedStage != nullptr) {
        switch (codeBufferStage) {
        case jit::CodeBuffer::TestStage::none:
            *observedStage = PublicationStageForTesting::none;
            break;
        case jit::CodeBuffer::TestStage::executableAllocated:
            *observedStage = PublicationStageForTesting::executableAllocated;
            break;
        case jit::CodeBuffer::TestStage::executableWritten:
            *observedStage = PublicationStageForTesting::executableWritten;
            break;
        case jit::CodeBuffer::TestStage::executableProtected:
            *observedStage = PublicationStageForTesting::executableProtected;
            break;
        }
    }
    if (!code.buffer)
        return {.handle = std::nullopt, .error = code.error};
    return {.handle = NativeCodeHandle{std::move(*code.buffer)},
            .error = jit::CodeBufferError::none};
}

std::size_t NativeCodeHandle::liveExecutableAllocationCountForTesting() noexcept {
    return jit::CodeBuffer::liveExecutableAllocationCountForTesting();
}

std::int64_t NativeCodeHandle::invokeI64Binary(std::int64_t left, std::int64_t right) const {
    assert(valid());
    // The trusted publication contract fixes the signature. On Windows x64
    // the first two i64 arguments are RCX/RDX and the i64 result is RAX. This
    // is the sole object-pointer-to-function-pointer conversion in the project.
    using EntryPoint = std::int64_t (*)(std::int64_t, std::int64_t);
    const auto function = reinterpret_cast<EntryPoint>(entryAddress());
    return function(left, right);
}

std::uint64_t NativeCodeHandle::invokeWordFrame(NativeFrame& frame) const {
    assert(valid());
    assert(frame.localCount == 0 || frame.locals != nullptr);
    using EntryPoint = std::uint64_t (*)(NativeFrame*);
    const auto function = reinterpret_cast<EntryPoint>(entryAddress());
    return function(&frame);
}

std::int64_t NativeCodeHandle::invokeI64Frame(NativeFrame& frame) const {
    return std::bit_cast<std::int64_t>(invokeWordFrame(frame));
}
} // namespace ember::runtime
