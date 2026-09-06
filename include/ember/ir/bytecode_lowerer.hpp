#pragma once

#include "ember/bytecode/bytecode.hpp"
#include "ember/ir/verifier.hpp"

#include <optional>
#include <vector>

namespace ember::ir {
enum class LoweringFailure {
    unsupported,
    invalidInput,
    internalInvariant,
};

struct LoweringResult {
    std::optional<VerifiedFunction> function;
    std::optional<LoweringFailure> failure;
    std::vector<support::Diagnostic> diagnostics;
};

class Lowerer {
public:
    // Copies trusted function metadata from verified bytecode for IR call
    // verification. The resulting table remains an IR capability.
    [[nodiscard]] static CallTargetTable
    callTargetsFromVerifiedProgram(const bytecode::VerifiedProgram& program);

    // Lowers one verified user function to the full native v0.1 subset:
    // i64, f64 and bool values plus void returns. Operand stacks must still
    // be empty at CFG edges because this non-SSA IR has no edge arguments.
    [[nodiscard]] LoweringResult lower(const bytecode::VerifiedProgram& program,
                                       core::FunctionId functionId) const;
};
} // namespace ember::ir
