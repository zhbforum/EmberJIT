#pragma once

#include <cstdint>
#include <variant>

namespace ember::core {

// Represents a materialized non-void Ember value.
using Value = std::variant<std::int64_t, double, bool>;

} // namespace ember::core
