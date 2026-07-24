#pragma once

#include "ember/ir/ir.hpp"
#include "ember/support/diagnostic.hpp"

#include <optional>
#include <utility>
#include <vector>

namespace ember::ir
{
class VerifiedFunction
{
  public:
    VerifiedFunction(const VerifiedFunction &) = delete;
    auto operator=(const VerifiedFunction &) -> VerifiedFunction & = delete;
    VerifiedFunction(VerifiedFunction &&) noexcept = default;
    auto operator=(VerifiedFunction &&) noexcept -> VerifiedFunction & = default;

    [[nodiscard]] const Function &function() const noexcept { return function_; }

  private:
    explicit VerifiedFunction(Function function) : function_(std::move(function)) {}

    Function function_;
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
    [[nodiscard]] VerifyResult verify(Function function) const;
};
} // namespace ember::ir
