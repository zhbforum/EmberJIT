#include "ember/runtime/native_value.hpp"

#include <type_traits>

static_assert(std::is_function_v<decltype(ember::runtime::encodeNativeValueWord)>);
static_assert(std::is_function_v<decltype(ember::runtime::decodeNativeValueWord)>);
