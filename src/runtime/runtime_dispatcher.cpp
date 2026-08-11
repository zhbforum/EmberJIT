#include "ember/runtime/runtime_dispatcher.hpp"

#include <new>

namespace ember::runtime
{
namespace
{
[[nodiscard]] bool valueMatches(const bytecode::Value &value, semantic::Type type)
{
    return (type == semantic::Type::i64 && std::holds_alternative<std::int64_t>(value)) ||
           (type == semantic::Type::f64 && std::holds_alternative<double>(value)) ||
           (type == semantic::Type::boolean && std::holds_alternative<bool>(value));
}
} // namespace

std::optional<DispatchDecision>
RuntimeDispatcher::dispatch(semantic::FunctionId functionId,
                            std::span<const bytecode::Value> arguments)
{
    auto *function = functions_.findMutable(functionId);
    if (function == nullptr)
        return std::nullopt;

    const auto &bytecode = function->bytecode();
    if (bytecode.kind != semantic::FunctionKind::user ||
        arguments.size() != bytecode.signature.parameterTypes.size())
    {
        return std::nullopt;
    }
    for (std::size_t index = 0; index < arguments.size(); ++index)
        if (!valueMatches(arguments[index], bytecode.signature.parameterTypes[index]))
            return std::nullopt;

    const bool becameHot = options_.profilingEnabled &&
                           function->recordInvocation(options_.hotThreshold);
    if (becameHot)
    {
        events_.push_back(
            {.functionId = function->id(), .invocationCount = function->profiling().invocationCount});
        if (options_.jitEnabled)
        {
            // Native compilation is optional. Allocation failure and every
            // unsupported form preserve the already-verified VM path.
            try
            {
                static_cast<void>(function->compileBaselineNative(
                    {.forceCompilerFailure = options_.forceNativeCompilationFailureForTesting,
                     .failpoint = options_.nativeCompilationFailpointForTesting},
                    options_.disableOptimizationForTesting));
            }
            catch (const std::bad_alloc &)
            {
            }
        }
    }

    return DispatchDecision{
        .function = function,
        .tier = function->selectExecutionTier(options_.jitEnabled),
        .becameHot = becameHot,
    };
}
} // namespace ember::runtime
