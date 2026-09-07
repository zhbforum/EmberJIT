#include "ember/core/value.hpp"

#include <type_traits>

static_assert(std::is_object_v<ember::core::Value>);
