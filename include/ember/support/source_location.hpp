#pragma once

#include <cstddef>
#include <cstdint>

namespace ember::support {

struct SourceId {
    std::uint32_t value {};

    friend constexpr bool operator==(SourceId, SourceId) = default;
};

struct SourceLocation {
    std::size_t offset {};
    std::size_t line {1};
    std::size_t column {1};

    friend constexpr bool operator==(SourceLocation, SourceLocation) = default;
};

struct SourceSpan {
    SourceId source {};
    std::size_t begin {};
    std::size_t end {};

    [[nodiscard]] constexpr bool empty() const noexcept
    {
        return begin == end;
    }

    friend constexpr bool operator==(SourceSpan, SourceSpan) = default;
};

} // namespace ember::support
