#pragma once

#include "ember/ssa/ssa.hpp"
#include "ember/support/diagnostic.hpp"

#include <optional>
#include <utility>
#include <vector>

namespace ember::ssa {
struct CallTarget {
    semantic::FunctionId id{noFunction};
    semantic::FunctionKind kind{semantic::FunctionKind::user};
    semantic::FunctionSignature signature;
};

class CallTargetTable {
public:
    // The table is caller-provided metadata. Verifier::verify validates its
    // ids, kinds, and signatures before retaining it in a verified function.
    explicit CallTargetTable(std::vector<CallTarget> targets)
        : targets_(std::move(targets)) {
    }

    [[nodiscard]] const CallTarget* find(semantic::FunctionId id) const noexcept;

private:
    std::vector<CallTarget> targets_;

    friend class Verifier;
};

class VerifiedSsaFunction {
public:
    VerifiedSsaFunction(const VerifiedSsaFunction&) = delete;
    auto operator=(const VerifiedSsaFunction&) -> VerifiedSsaFunction& = delete;
    VerifiedSsaFunction(VerifiedSsaFunction&&) noexcept = default;
    auto operator=(VerifiedSsaFunction&&) -> VerifiedSsaFunction& = default;

    [[nodiscard]] const Function& function() const noexcept {
        return function_;
    }
    [[nodiscard]] const CallTargetTable& callTargets() const noexcept {
        return callTargets_;
    }

private:
    explicit VerifiedSsaFunction(Function function, CallTargetTable callTargets)
        : function_(std::move(function)),
          callTargets_(std::move(callTargets)) {
    }

    Function function_;
    CallTargetTable callTargets_;

    friend class Verifier;
};

struct VerifyResult {
    std::optional<VerifiedSsaFunction> function;
    std::vector<support::Diagnostic> diagnostics;
};

class Verifier {
public:
    [[nodiscard]] VerifyResult verify(Function function, CallTargetTable callTargets) const;
    // Call-free unit SSA can use this overload. Any call is rejected because it
    // has no trusted target table.
    [[nodiscard]] VerifyResult verify(Function function) const;
};
} // namespace ember::ssa
