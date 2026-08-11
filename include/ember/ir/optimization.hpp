#pragma once

#include "ember/ir/verifier.hpp"

#include <optional>
#include <vector>

namespace ember::ir {
enum class OptimizationPass {
    constantFolding,
    constantPropagation,
    cfgSimplification,
    deadCodeElimination,
};

[[nodiscard]] const char* optimizationPassName(OptimizationPass pass) noexcept;

struct OptimizationResult {
    std::optional<VerifiedFunction> function;
    std::optional<OptimizationPass> failedPass;
    std::vector<support::Diagnostic> diagnostics;
};

// Every pass accepts only verifier-checked IR and returns verifier-checked IR.
// A failed postcondition is reported through VerifyResult. The pipeline adds
// the pass identity so callers can report an actionable failure and preserve
// their existing VM fallback path.
class ConstantFoldingPass {
public:
    [[nodiscard]] VerifyResult run(const VerifiedFunction& input) const;
};

class ConstantPropagationPass {
public:
    [[nodiscard]] VerifyResult run(const VerifiedFunction& input) const;
};

class DeadCodeEliminationPass {
public:
    [[nodiscard]] VerifyResult run(const VerifiedFunction& input) const;
};

class CfgSimplificationPass {
public:
    [[nodiscard]] VerifyResult run(const VerifiedFunction& input) const;
};

// The default baseline pipeline is deliberately small and ordered so that
// control-flow simplification exposes dead pure instructions to DCE.
class OptimizationPipeline {
public:
    [[nodiscard]] OptimizationResult run(const VerifiedFunction& input) const;
};
} // namespace ember::ir
