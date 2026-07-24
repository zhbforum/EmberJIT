#include "ember/runtime/runtime_function.hpp"

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
} // namespace ember::runtime
