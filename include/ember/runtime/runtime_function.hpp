#pragma once

#include "ember/bytecode/bytecode.hpp"

#include <cstdint>
#include <memory>
#include <utility>

namespace ember::runtime
{
class RuntimeDispatcher;
enum class ExecutionTier
{
    virtualMachine,
    native
};

// Defined together with the native ABI and executable-memory owner. Until
// then, its incompleteness prevents callers from constructing a fake target.
class NativeCodeHandle;

struct ProfilingCounters
{
    std::uint64_t invocationCount{};
    bool isHot{};
};

class RuntimeFunction
{
  public:
    explicit RuntimeFunction(bytecode::Function bytecode) : bytecode_(std::move(bytecode)) {}

    [[nodiscard]] const bytecode::Function &bytecode() const noexcept { return bytecode_; }
    [[nodiscard]] semantic::FunctionId id() const noexcept { return bytecode_.id; }
    [[nodiscard]] ExecutionTier tier() const noexcept { return tier_; }
    [[nodiscard]] const ProfilingCounters &profiling() const noexcept { return profiling_; }

  private:
    // The dispatcher is the sole owner of profiling mutation and tier
    // selection. This prevents callers from bypassing the common call path.
    [[nodiscard]] bool recordInvocation(std::uint64_t hotThreshold) noexcept;
    [[nodiscard]] ExecutionTier selectExecutionTier(bool jitEnabled) const noexcept;

    bytecode::Function bytecode_;
    ExecutionTier tier_{ExecutionTier::virtualMachine};
    ProfilingCounters profiling_{};
    std::shared_ptr<const NativeCodeHandle> nativeCode_;

    friend class RuntimeDispatcher;
};
} // namespace ember::runtime
