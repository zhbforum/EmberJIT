#include "ember/runtime/runtime_function.hpp"

#include "ember/ir/bytecode_lowerer.hpp"
#include "ember/jit/baseline_compiler.hpp"
#include "ember/runtime/native_code.hpp"

#include <memory>

#include <limits>

namespace ember::runtime
{
bool RuntimeFunction::recordInvocation(std::uint64_t hotThreshold) noexcept
{
    if (profiling_.invocationCount != std::numeric_limits<std::uint64_t>::max())
        ++profiling_.invocationCount;

    if (hotThreshold == 0 || profiling_.isHot || profiling_.invocationCount < hotThreshold)
        return false;

    profiling_.isHot = true;
    return true;
}

ExecutionTier RuntimeFunction::selectExecutionTier(bool jitEnabled) const noexcept
{
    return jitEnabled && tier_ == ExecutionTier::native && nativeCode_
               ? ExecutionTier::native
               : ExecutionTier::virtualMachine;
}

bool RuntimeFunction::compileBaselineNative(bool forceFailureForTesting)
{
    if (nativeCode_ || !nativeSource_)
        return nativeCode_ != nullptr;

    auto lowered = ir::Lowerer{}.lower(*nativeSource_, bytecode_.id);
    if (!lowered.function)
        return false;
    auto compiled = jit::x64::BaselineCompiler{
        {.forceFailureForTesting = forceFailureForTesting}}
                        .compile(*lowered.function);
    if (!compiled.code)
        return false;
    auto published = NativeCodeHandle::publishI64Frame(std::move(*compiled.code));
    if (!published.handle)
        return false;

    auto handle = std::make_shared<const NativeCodeHandle>(std::move(*published.handle));
    nativeFrameRequirements_ = compiled.frameRequirements;
    nativeCode_ = std::move(handle);
    nativeSource_.reset();
    tier_ = ExecutionTier::native;
    return true;
}

NativeInvocation RuntimeFunction::invokeNativeI64Frame(std::vector<std::int64_t> &locals,
                                                        void *callContext,
                                                        jit::NativeCallBridge callBridge) const
{
    std::vector<std::int64_t> spills(nativeFrameRequirements_.spillCount);
    std::vector<std::int64_t> callArguments(nativeFrameRequirements_.callArgumentCapacity);
    NativeFrame frame{.locals = locals.data(),
                      .localCount = locals.size(),
                      .spills = spills.data(),
                      .spillCount = spills.size(),
                      .callArguments = callArguments.data(),
                      .callArgumentCapacity = callArguments.size(),
                      .callContext = callContext,
                      .callBridge = callBridge};
    return {.value = nativeCode_->invokeI64Frame(frame),
            .error = static_cast<NativeFrameError>(frame.errorCode)};
}
} // namespace ember::runtime
