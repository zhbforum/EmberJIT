#pragma once

#include "ember/ir/verifier.hpp"

#include <string>

namespace ember::ir {
[[nodiscard]] std::string dump(const VerifiedFunction& function);
} // namespace ember::ir
