#include "ember/runtime/native_value.hpp"

#include <bit>

namespace ember::runtime {
std::optional<std::uint64_t> encodeNativeValueWord(const core::Value& value,
                                                   core::Type type) noexcept {
    if (type == core::Type::i64 && std::holds_alternative<std::int64_t>(value))
        return std::bit_cast<std::uint64_t>(std::get<std::int64_t>(value));
    if (type == core::Type::f64 && std::holds_alternative<double>(value))
        return std::bit_cast<std::uint64_t>(std::get<double>(value));
    if (type == core::Type::boolean && std::holds_alternative<bool>(value))
        return std::get<bool>(value) ? 1U : 0U;
    return std::nullopt;
}

std::optional<core::Value> decodeNativeValueWord(std::uint64_t word, core::Type type) noexcept {
    if (type == core::Type::i64)
        return core::Value{std::bit_cast<std::int64_t>(word)};
    if (type == core::Type::f64)
        return core::Value{std::bit_cast<double>(word)};
    if (type == core::Type::boolean) {
        if (word > 1U)
            return std::nullopt;
        return core::Value{word != 0U};
    }
    return std::nullopt;
}
} // namespace ember::runtime
