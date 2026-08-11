#pragma once

#include "ember/bytecode/bytecode.hpp"

#include <cstdint>
#include <optional>

namespace ember::runtime {
// Converts only between a verified Ember value and the raw word used at the
// native ABI boundary. `bool` decoding rejects every non-canonical word.
[[nodiscard]] std::optional<std::uint64_t> encodeNativeValueWord(const bytecode::Value& value,
                                                                 semantic::Type type) noexcept;
[[nodiscard]] std::optional<bytecode::Value> decodeNativeValueWord(std::uint64_t word,
                                                                   semantic::Type type) noexcept;
} // namespace ember::runtime
