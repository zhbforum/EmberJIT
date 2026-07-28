#include "ember/runtime/native_value.hpp"

#include <bit>

namespace ember::runtime
{
std::optional<std::uint64_t> encodeNativeValueWord(const bytecode::Value &value,
                                                    semantic::Type type) noexcept
{
    if (type == semantic::Type::i64 && std::holds_alternative<std::int64_t>(value))
        return std::bit_cast<std::uint64_t>(std::get<std::int64_t>(value));
    if (type == semantic::Type::f64 && std::holds_alternative<double>(value))
        return std::bit_cast<std::uint64_t>(std::get<double>(value));
    if (type == semantic::Type::boolean && std::holds_alternative<bool>(value))
        return std::get<bool>(value) ? 1U : 0U;
    return std::nullopt;
}

std::optional<bytecode::Value> decodeNativeValueWord(std::uint64_t word,
                                                      semantic::Type type) noexcept
{
    if (type == semantic::Type::i64)
        return bytecode::Value{std::bit_cast<std::int64_t>(word)};
    if (type == semantic::Type::f64)
        return bytecode::Value{std::bit_cast<double>(word)};
    if (type == semantic::Type::boolean)
    {
        if (word > 1U)
            return std::nullopt;
        return bytecode::Value{word != 0U};
    }
    return std::nullopt;
}
} // namespace ember::runtime
