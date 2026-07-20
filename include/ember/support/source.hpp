#pragma once

#include "ember/support/diagnostic.hpp"
#include "ember/support/source_location.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ember::support {

class SourceText {
public:
    SourceText(
        SourceId id,
        std::string name,
        std::string contents);

    [[nodiscard]] SourceId id() const noexcept;
    [[nodiscard]] std::string_view name() const noexcept;
    [[nodiscard]] std::string_view contents() const noexcept;
    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] std::optional<Diagnostic> validateEncoding() const;
    [[nodiscard]] bool isSourceBoundary(std::size_t offset) const noexcept;
    [[nodiscard]] std::optional<std::string_view> slice(SourceSpan span) const noexcept;
    [[nodiscard]] std::optional<std::string_view> rawSlice(SourceSpan span) const noexcept;
    [[nodiscard]] std::optional<SourceLocation> locationAt(std::size_t offset) const noexcept;

private:
    SourceId id_ {};
    std::string name_;
    std::string contents_;
    std::vector<std::size_t> lineStarts_;
    std::optional<std::size_t> firstInvalidUtf8Offset_;
    std::optional<Diagnostic> encodingDiagnostic_;
};

} // namespace ember::support
