#include "ember/support/source.hpp"

#include "ember/support/diagnostic.hpp"

#include <algorithm>
#include <iterator>
#include <utility>

namespace ember::support {
namespace {

struct Utf8Error {
    std::size_t begin{};
    std::size_t end{};
};

[[nodiscard]] constexpr auto asByte(char value) noexcept -> std::uint8_t {
    return static_cast<std::uint8_t>(static_cast<unsigned char>(value));
}

[[nodiscard]] constexpr auto isContinuation(std::uint8_t byte) noexcept -> bool {
    return byte >= 0x80U && byte <= 0xBFU;
}

[[nodiscard]] auto validateUtf8(std::string_view text) -> std::optional<Utf8Error> {
    for (std::size_t index = 0; index < text.size();) {
        const auto first = asByte(text[index]);
        if (first <= 0x7FU) {
            ++index;
            continue;
        }

        std::size_t length{};
        std::uint8_t secondMinimum{0x80U};
        std::uint8_t secondMaximum{0xBFU};

        if (first >= 0xC2U && first <= 0xDFU) {
            length = 2;
        } else if (first == 0xE0U) {
            length = 3;
            secondMinimum = 0xA0U;
        } else if ((first >= 0xE1U && first <= 0xECU) || (first >= 0xEEU && first <= 0xEFU)) {
            length = 3;
        } else if (first == 0xEDU) {
            length = 3;
            secondMaximum = 0x9FU;
        } else if (first == 0xF0U) {
            length = 4;
            secondMinimum = 0x90U;
        } else if (first >= 0xF1U && first <= 0xF3U) {
            length = 4;
        } else if (first == 0xF4U) {
            length = 4;
            secondMaximum = 0x8FU;
        } else {
            return Utf8Error{index, index + 1};
        }

        const auto available = text.size() - index;
        if (available < length) {
            return Utf8Error{index, text.size()};
        }

        const auto second = asByte(text[index + 1]);
        if (second < secondMinimum || second > secondMaximum) {
            return Utf8Error{index, index + 2};
        }

        for (std::size_t continuationIndex = 2; continuationIndex < length; ++continuationIndex) {
            if (!isContinuation(asByte(text[index + continuationIndex]))) {
                return Utf8Error{index, index + continuationIndex + 1};
            }
        }

        index += length;
    }

    return std::nullopt;
}

[[nodiscard]] constexpr auto utf8Length(std::uint8_t first) noexcept -> std::size_t {
    if (first <= 0x7FU) {
        return 1;
    }
    if (first <= 0xDFU) {
        return 2;
    }
    if (first <= 0xEFU) {
        return 3;
    }
    return 4;
}

[[nodiscard]] auto makeDiagnostic(SourceId source,
                                  std::size_t begin,
                                  std::size_t end,
                                  std::string code,
                                  std::string message) -> Diagnostic {
    return Diagnostic{
        .stage = DiagnosticStage::lexer,
        .severity = DiagnosticSeverity::error,
        .code = std::move(code),
        .message = std::move(message),
        .primarySpan = SourceSpan{.source = source, .begin = begin, .end = end},
    };
}

[[nodiscard]] auto
findEncodingDiagnostic(SourceId source,
                       std::string_view contents,
                       const std::optional<Utf8Error>& utf8Error) -> std::optional<Diagnostic> {
    constexpr std::string_view utf8Bom{"\xEF\xBB\xBF", 3};
    if (contents.starts_with(utf8Bom)) {
        return makeDiagnostic(source, 0, utf8Bom.size(), "E1001", "UTF-8 BOM is not allowed");
    }

    if (utf8Error.has_value()) {
        return makeDiagnostic(source,
                              utf8Error->begin,
                              utf8Error->end,
                              "E1002",
                              "invalid UTF-8 sequence");
    }

    return std::nullopt;
}

[[nodiscard]] auto collectLineStarts(std::string_view contents) -> std::vector<std::size_t> {
    std::vector<std::size_t> lineStarts{0};
    for (std::size_t index = 0; index < contents.size(); ++index) {
        if (contents[index] == '\r') {
            if (index + 1 < contents.size() && contents[index + 1] == '\n') {
                ++index;
            }
            lineStarts.push_back(index + 1);
        } else if (contents[index] == '\n') {
            lineStarts.push_back(index + 1);
        }
    }

    return lineStarts;
}

} // namespace

SourceText::SourceText(SourceId id, std::string name, std::string contents)
    : id_(id),
      name_(std::move(name)),
      contents_(std::move(contents)),
      lineStarts_(collectLineStarts(contents_)) {
    const auto utf8Error = validateUtf8(contents_);
    if (utf8Error.has_value()) {
        firstInvalidUtf8Offset_ = utf8Error->begin;
    }
    encodingDiagnostic_ = findEncodingDiagnostic(id_, contents_, utf8Error);
}

auto SourceText::id() const noexcept -> SourceId {
    return id_;
}

auto SourceText::name() const noexcept -> std::string_view {
    return name_;
}

auto SourceText::contents() const noexcept -> std::string_view {
    return contents_;
}

auto SourceText::size() const noexcept -> std::size_t {
    return contents_.size();
}

auto SourceText::validateEncoding() const -> std::optional<Diagnostic> {
    return encodingDiagnostic_;
}

auto SourceText::isSourceBoundary(std::size_t offset) const noexcept -> bool {
    if (offset > contents_.size()) {
        return false;
    }

    if (firstInvalidUtf8Offset_.has_value() && offset > *firstInvalidUtf8Offset_) {
        return false;
    }

    const auto lineStart =
        std::prev(std::upper_bound(lineStarts_.begin(), lineStarts_.end(), offset));
    for (std::size_t index = *lineStart; index < offset;) {
        const auto first = asByte(contents_[index]);
        if (first == static_cast<std::uint8_t>('\r')) {
            const auto length =
                index + 1 < contents_.size() && contents_[index + 1] == '\n' ? 2U : 1U;
            if (index + length > offset) {
                return false;
            }
            index += length;
            continue;
        }

        if (first == static_cast<std::uint8_t>('\n')) {
            if (index + 1 > offset) {
                return false;
            }
            ++index;
            continue;
        }

        const auto length = utf8Length(first);
        if (index + length > offset) {
            return false;
        }

        index += length;
    }

    return true;
}

auto SourceText::slice(SourceSpan span) const noexcept -> std::optional<std::string_view> {
    if (firstInvalidUtf8Offset_.has_value() || span.source != id_ || span.begin > span.end ||
        !isSourceBoundary(span.begin) || !isSourceBoundary(span.end)) {
        return std::nullopt;
    }

    return std::string_view{contents_}.substr(span.begin, span.end - span.begin);
}

auto SourceText::rawSlice(SourceSpan span) const noexcept -> std::optional<std::string_view> {
    if (span.source != id_ || span.begin > span.end || span.end > contents_.size()) {
        return std::nullopt;
    }

    return std::string_view{contents_}.substr(span.begin, span.end - span.begin);
}

auto SourceText::locationAt(std::size_t offset) const noexcept -> std::optional<SourceLocation> {
    if (!isSourceBoundary(offset)) {
        return std::nullopt;
    }

    const auto lineStart =
        std::prev(std::upper_bound(lineStarts_.begin(), lineStarts_.end(), offset));
    SourceLocation location{
        .offset = offset,
        .line = static_cast<std::size_t>(std::distance(lineStarts_.begin(), lineStart)) + 1,
        .column = 1,
    };

    for (std::size_t index = *lineStart; index < offset;) {
        const auto first = asByte(contents_[index]);
        const auto length = utf8Length(first);
        index += length;
        ++location.column;
    }

    return location;
}

} // namespace ember::support
