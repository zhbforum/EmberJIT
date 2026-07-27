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
    // Test-only fault injection. It proves failed compilation leaves the VM
    // entry point intact; no command-line option exposes this behavior.
    bool forceNativeCompilationFailureForTesting{};
    // Test-only seam for VM/unoptimized-native/optimized-native differential
    // tests. Production dispatch always enables the optimization pipeline.
    bool disableOptimizationForTesting{};
    // Test-only fault injection for the native call bridge. VM-only execution
    // never enters this bridge, so this isolates native error propagation.
    bool forceNativeCallFailureForTesting{};
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
