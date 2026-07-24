#pragma once

#include "ember/bytecode/bytecode.hpp"
#include "ember/ir/verifier.hpp"

#include <optional>
#include <vector>

namespace ember::ir
{
enum class LoweringFailure
{
    unsupported,
    invalidInput,
    internalInvariant,
};

struct LoweringResult
{
    std::optional<VerifiedFunction> function;
    std::optional<LoweringFailure> failure;
    std::vector<support::Diagnostic> diagnostics;
};

class Lowerer
{
  public:
    // Lowers one verified user function. The first native subset accepts i64
    // parameters, locals, return values, arithmetic and comparisons; a
    // comparison bool may only feed a branch.
    [[nodiscard]] LoweringResult lower(const bytecode::VerifiedProgram &program,
                                       semantic::FunctionId functionId) const;
};
} // namespace ember::ir
