#include "ember/integration/builtin_registration.hpp"

#include <type_traits>

static_assert(std::is_function_v<decltype(ember::integration::registerBuiltins)>);
