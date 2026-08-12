#pragma once

#include "ember/ssa/verifier.hpp"

#include <string>

namespace ember::ssa {
[[nodiscard]] std::string dump(const VerifiedSsaFunction& function);
} // namespace ember::ssa
