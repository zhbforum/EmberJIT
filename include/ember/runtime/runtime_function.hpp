#pragma once

#include "ember/bytecode/bytecode.hpp"
#include "ember/jit/baseline_compiler.hpp"
#include "ember/runtime/native_code.hpp"

#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

namespace ember::runtime {
class RuntimeDispatcher;
class VirtualMachine;
namespace test {
class RuntimeFunctionAccess;
}

enum class NativeCompilationFailpoint {
    none,
    afterLowering,
    afterEmission,
    afterExecutableAllocation,
    afterExecutableWrite,
    afterExecutableProtection,
};

// Test-only observation of the furthest completed native compilation stage.
enum class NativeCompilationStage {
    none,
    lowered,
    compilerFailure,
    emitted,
    executableAllocated,
    executableWritten,
    executableProtected,
    published,
};

enum class ExecutionTier { virtualMachine, native };

// Defined together with the native ABI and executable-memory owner. Until
// then, its incompleteness prevents callers from constructing a fake target.
struct NativeInvocation {
    std::uint64_t value{};
    NativeFrameError error{NativeFrameError::none};
};

struct ProfilingCounters {
    std::uint64_t invocationCount{};
    bool isHot{};
};

class RuntimeFunction {
public:
    RuntimeFunction(bytecode::Function bytecode,
                    std::shared_ptr<const bytecode::VerifiedProgram> nativeSource)
        : bytecode_(std::move(bytecode)),
          nativeSource_(std::move(nativeSource)) {
    }

    [[nodiscard]] const bytecode::Function& bytecode() const noexcept {
        return bytecode_;
    }
    [[nodiscard]] semantic::FunctionId id() const noexcept {
        return bytecode_.id;
    }
    [[nodiscard]] ExecutionTier tier() const noexcept {
        return tier_;
    }
    [[nodiscard]] const ProfilingCounters& profiling() const noexcept {
        return profiling_;
    }

private:
    struct NativeCompilationTestOptions {
        bool forceCompilerFailure{};
        NativeCompilationFailpoint failpoint{NativeCompilationFailpoint::none};
    };

    // The dispatcher is the sole owner of profiling mutation and tier
    // selection. This prevents callers from bypassing the common call path.
    [[nodiscard]] bool recordInvocation(std::uint64_t hotThreshold) noexcept;
    [[nodiscard]] ExecutionTier selectExecutionTier(bool jitEnabled) const noexcept;
    [[nodiscard]] bool compileBaselineNative(NativeCompilationTestOptions testOptions,
                                             bool disableOptimizationForTesting = false);
    [[nodiscard]] NativeInvocation invokeNativeWordFrame(std::vector<std::uint64_t>& locals,
                                                         void* callContext,
                                                         jit::NativeCallBridge callBridge) const;

    bytecode::Function bytecode_;
    ExecutionTier tier_{ExecutionTier::virtualMachine};
    ProfilingCounters profiling_{};
    // Retained immutable verifier output is lowered only on the hot path.
    // This keeps --no-jit and never-invoked functions entirely VM-only.
    std::shared_ptr<const bytecode::VerifiedProgram> nativeSource_;
    std::shared_ptr<const NativeCodeHandle> nativeCode_;
    jit::x64::NativeFrameRequirements nativeFrameRequirements_{};
    NativeCompilationStage nativeCompilationStageForTesting_{NativeCompilationStage::none};

    friend class RuntimeDispatcher;
    friend class VirtualMachine;
    friend class test::RuntimeFunctionAccess;
};
} // namespace ember::runtime
