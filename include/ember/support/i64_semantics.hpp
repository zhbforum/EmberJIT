#pragma once

#include <bit>
#include <cstdint>

namespace ember::support::i64 {
[[nodiscard]] constexpr std::int64_t fromBits(std::uint64_t value) noexcept {
    return std::bit_cast<std::int64_t>(value);
}

[[nodiscard]] constexpr std::uint64_t toBits(std::int64_t value) noexcept {
    return std::bit_cast<std::uint64_t>(value);
}

[[nodiscard]] constexpr std::int64_t add(std::int64_t left, std::int64_t right) noexcept {
    return fromBits(toBits(left) + toBits(right));
}

[[nodiscard]] constexpr std::int64_t subtract(std::int64_t left, std::int64_t right) noexcept {
    return fromBits(toBits(left) - toBits(right));
}

[[nodiscard]] constexpr std::int64_t multiply(std::int64_t left, std::int64_t right) noexcept {
    return fromBits(toBits(left) * toBits(right));
}

[[nodiscard]] constexpr std::int64_t negate(std::int64_t value) noexcept {
    return fromBits(std::uint64_t{} - toBits(value));
}
} // namespace ember::support::i64
