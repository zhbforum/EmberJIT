#pragma once

#include "ember/ir/ir.hpp"
#include "ember/support/diagnostic.hpp"

#include <optional>
#include <utility>
#include <vector>

namespace ember::bytecode
{
class VerifiedProgram;
}

namespace ember::ir
{
struct CallTarget
{
    semantic::FunctionId id;
    semantic::FunctionKind kind;
    semantic::FunctionSignature signature;
};

// Immutable call-target metadata copied exclusively from verified bytecode.
// A VerifiedFunction retains it so every later verifier pass checks calls
// against the same trusted signatures.
class CallTargetTable
{
  public:
    [[nodiscard]] static CallTargetTable fromVerifiedProgram(const bytecode::VerifiedProgram &program);
    [[nodiscard]] const CallTarget *find(semantic::FunctionId id) const noexcept;

  private:
    explicit CallTargetTable(std::vector<CallTarget> targets) : targets_(std::move(targets)) {}

    std::vector<CallTarget> targets_;
    friend class Verifier;
};

class VerifiedFunction
{
  public:
    VerifiedFunction(const VerifiedFunction &) = delete;
    auto operator=(const VerifiedFunction &) -> VerifiedFunction & = delete;
    VerifiedFunction(VerifiedFunction &&) noexcept = default;
    auto operator=(VerifiedFunction &&) noexcept -> VerifiedFunction & = default;

    [[nodiscard]] const Function &function() const noexcept { return function_; }
    [[nodiscard]] const CallTargetTable &callTargets() const noexcept { return callTargets_; }

  private:
    explicit VerifiedFunction(Function function, CallTargetTable callTargets)
        : function_(std::move(function)), callTargets_(std::move(callTargets))
    {
    }

    Function function_;
    CallTargetTable callTargets_;
    friend class Verifier;
};

struct VerifyResult
{
    std::optional<VerifiedFunction> function;
    std::vector<support::Diagnostic> diagnostics;
};

class Verifier
{
  public:
    // Validates canonical instruction encodings, block-local def-use, CFG
    // reachability and targets, terminators, and local initialization.
    [[nodiscard]] VerifyResult verify(Function function, const CallTargetTable &callTargets) const;
    // Call-free unit IR can use this overload. Any call is rejected because it
    // has no trusted target table.
    [[nodiscard]] VerifyResult verify(Function function) const;
};
} // namespace ember::ir
