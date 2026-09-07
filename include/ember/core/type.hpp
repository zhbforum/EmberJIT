#pragma once

#include <string_view>

namespace ember::core {

enum class Type { i64, f64, boolean, voidType };

[[nodiscard]] constexpr auto typeName(Type type) noexcept -> std::string_view {
    switch (type) {
    case Type::i64:
        return "i64";
    case Type::f64:
        return "f64";
    case Type::boolean:
        return "bool";
    case Type::voidType:
        return "void";
    }
    return "<invalid-type>";
}

} // namespace ember::core
