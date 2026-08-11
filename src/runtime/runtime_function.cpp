#include "ember/runtime/runtime_function.hpp"

#include "ember/ir/bytecode_lowerer.hpp"
#include "ember/ir/optimization.hpp"
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

bool RuntimeFunction::compileBaselineNative(NativeCompilationTestOptions testOptions,
                                            bool disableOptimizationForTesting)
{
    if (nativeCode_ || !nativeSource_)
        return nativeCode_ != nullptr;

    const auto failpoint = testOptions.failpoint;
    auto lowered = ir::Lowerer{}.lower(*nativeSource_, bytecode_.id);
    if (!lowered.function)
        return false;
    nativeCompilationStageForTesting_ = NativeCompilationStage::lowered;
    if (failpoint == NativeCompilationFailpoint::afterLowering)
        return false;

    if (!disableOptimizationForTesting)
    {
        auto optimized = ir::OptimizationPipeline{}.run(*lowered.function);
        if (!optimized.function)
            return false;
        lowered.function = std::move(optimized.function);
    }
    auto compiled = jit::x64::BaselineCompiler{
        {.forceFailureForTesting = testOptions.forceCompilerFailure}}
                        .compile(*lowered.function);
    if (!compiled.code)
    {
        if (testOptions.forceCompilerFailure &&
            compiled.error == jit::x64::BaselineCompileError::emissionFailed)
        {
            nativeCompilationStageForTesting_ = NativeCompilationStage::compilerFailure;
        }
        return false;
    }
    nativeCompilationStageForTesting_ = NativeCompilationStage::emitted;
    if (failpoint == NativeCompilationFailpoint::afterEmission)
        return false;

    auto publicationFailpoint = NativeCodeHandle::PublicationFailpointForTesting::none;
    switch (failpoint)
    {
    case NativeCompilationFailpoint::none:
    case NativeCompilationFailpoint::afterLowering:
    case NativeCompilationFailpoint::afterEmission:
        break;
    case NativeCompilationFailpoint::afterExecutableAllocation:
        publicationFailpoint = NativeCodeHandle::PublicationFailpointForTesting::afterExecutableAllocation;
        break;
    case NativeCompilationFailpoint::afterExecutableWrite:
        publicationFailpoint = NativeCodeHandle::PublicationFailpointForTesting::afterExecutableWrite;
        break;
    case NativeCompilationFailpoint::afterExecutableProtection:
        publicationFailpoint = NativeCodeHandle::PublicationFailpointForTesting::afterExecutableProtection;
        break;
    }
    auto publicationStage = NativeCodeHandle::PublicationStageForTesting::none;
    auto published = NativeCodeHandle::publishWordFrameWithFailpointForTesting(
        std::move(*compiled.code), publicationFailpoint, &publicationStage);
    if (!published.handle)
    {
        switch (publicationStage)
        {
        case NativeCodeHandle::PublicationStageForTesting::none:
            break;
        case NativeCodeHandle::PublicationStageForTesting::executableAllocated:
            nativeCompilationStageForTesting_ = NativeCompilationStage::executableAllocated;
            break;
        case NativeCodeHandle::PublicationStageForTesting::executableWritten:
            nativeCompilationStageForTesting_ = NativeCompilationStage::executableWritten;
            break;
        case NativeCodeHandle::PublicationStageForTesting::executableProtected:
            nativeCompilationStageForTesting_ = NativeCompilationStage::executableProtected;
            break;
        }
        return false;
    }

    auto handle = std::make_shared<const NativeCodeHandle>(std::move(*published.handle));
    nativeFrameRequirements_ = compiled.frameRequirements;
    nativeCode_ = std::move(handle);
    nativeSource_.reset();
    tier_ = ExecutionTier::native;
    nativeCompilationStageForTesting_ = NativeCompilationStage::published;
    return true;
}

NativeInvocation RuntimeFunction::invokeNativeWordFrame(std::vector<std::uint64_t> &locals,
                                                         void *callContext,
                                                         jit::NativeCallBridge callBridge) const
{
    std::vector<std::uint64_t> spills(nativeFrameRequirements_.spillCount);
    std::vector<std::uint64_t> callArguments(nativeFrameRequirements_.callArgumentCapacity);
    NativeFrame frame{.locals = locals.data(),
                      .localCount = locals.size(),
                      .spills = spills.data(),
                      .spillCount = spills.size(),
                      .callArguments = callArguments.data(),
                      .callArgumentCapacity = callArguments.size(),
                      .callContext = callContext,
                      .callBridge = callBridge};
    return {.value = nativeCode_->invokeWordFrame(frame),
            .error = static_cast<NativeFrameError>(frame.errorCode)};
}
} // namespace ember::runtime
