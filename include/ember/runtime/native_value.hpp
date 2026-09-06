#pragma once

#include "ember/core/type.hpp"
#include "ember/core/value.hpp"

#include <cstdint>
#include <optional>

namespace ember::runtime {
// Converts only between a verified Ember value and the raw word used at the
// native ABI boundary. `bool` decoding rejects every non-canonical word.
[[nodiscard]] std::optional<std::uint64_t> encodeNativeValueWord(const core::Value& value,
                                                                 core::Type type) noexcept;
[[nodiscard]] std::optional<core::Value> decodeNativeValueWord(std::uint64_t word,
                                                               core::Type type) noexcept;
} // namespace ember::runtime
