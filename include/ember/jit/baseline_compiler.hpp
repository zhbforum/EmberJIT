#pragma once

#include "ember/ir/verifier.hpp"
#include "ember/jit/emitter.hpp"

#include <optional>

namespace ember::jit::x64
{
enum class BaselineCompileError
{
    none,
    unsupportedFunction,
    emissionFailed,
};

struct NativeFrameRequirements
{
    std::size_t spillCount{};
    std::size_t callArgumentCapacity{};
};

struct BaselineCompileResult
{
    std::optional<MachineCode> code;
    BaselineCompileError error{BaselineCompileError::none};
    NativeFrameRequirements frameRequirements{};
};

struct BaselineCompilerOptions
{
    // Test seam for proving that a failed compilation cannot publish an entry
    // point. It is not wired to the user-facing CLI.
    bool forceFailureForTesting{};
};

// Compiles verified v0.1 IR (i64, f64, bool and void) to the frame-based
// baseline ABI. Forms outside the implemented structural contract return no
// code so dispatch retains VM execution; publication is all-or-nothing.
class BaselineCompiler
{
  public:
    explicit BaselineCompiler(BaselineCompilerOptions options = {}) : options_(options) {}
    [[nodiscard]] BaselineCompileResult compile(const ir::VerifiedFunction &function) const;

  private:
    BaselineCompilerOptions options_;
};
} // namespace ember::jit::x64
