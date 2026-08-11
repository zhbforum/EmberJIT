#pragma once

#include "ember/jit/emitter.hpp"

namespace ember::jit::x64::test {
class EmitterAccess {
public:
    [[nodiscard]] static std::optional<std::int32_t> checkedRel32(std::int64_t delta) noexcept {
        return Emitter::checkedRel32(delta);
    }

    [[nodiscard]] static std::size_t maximumCodeSize() noexcept {
        return Emitter::maximumCodeSize();
    }

    [[nodiscard]] static bool canAppend(std::size_t currentSize,
                                        std::size_t instructionSize) noexcept {
        return Emitter::canAppend(currentSize, instructionSize);
    }
};
} // namespace ember::jit::x64::test
