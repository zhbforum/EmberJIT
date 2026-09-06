#include "ember/core/type.hpp"

#include <type_traits>

static_assert(std::is_enum_v<ember::core::Type>);
