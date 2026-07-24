#pragma once

#include "ember/runtime/runtime_function_table.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace ember::runtime
{
struct HotFunctionEvent
{
    semantic::FunctionId functionId;
    std::uint64_t invocationCount;
};

struct RuntimeOptions
{
    // Zero records invocations but disables hot-state transitions.
    std::uint64_t hotThreshold{1000};
    // --no-jit disables compilation and native dispatch, not profiling.
    bool jitEnabled{true};
    // Benchmark-only baseline; the CLI always leaves profiling enabled.
    bool profilingEnabled{true};
};

struct DispatchDecision
{
    const RuntimeFunction *function;
    ExecutionTier tier;
    bool becameHot;
};

// This is the common call contract for VM and future native callers. It does
// not create VM frames and never invokes user-provided code.
class RuntimeDispatcher
{
  public:
    RuntimeDispatcher(RuntimeFunctionTable &functions, RuntimeOptions options,
                      std::vector<HotFunctionEvent> &events)
        : functions_(functions), options_(options), events_(events)
    {
    }

    [[nodiscard]] std::optional<DispatchDecision>
    dispatch(semantic::FunctionId functionId, std::span<const bytecode::Value> arguments);

  private:
    RuntimeFunctionTable &functions_;
    RuntimeOptions options_;
    std::vector<HotFunctionEvent> &events_;
};
} // namespace ember::runtime
