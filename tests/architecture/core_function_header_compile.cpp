#include "ember/core/function.hpp"

#include <type_traits>

static_assert(std::is_aggregate_v<ember::core::FunctionSignature>);
