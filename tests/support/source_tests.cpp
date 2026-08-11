#include "ember/support/diagnostic.hpp"
#include "ember/support/source.hpp"

#include "test_harness.hpp"

#include <array>
#include <string>
#include <string_view>

namespace {
using ember::test::TestContext;

using ember::support::DiagnosticSeverity;
using ember::support::DiagnosticStage;
using ember::support::SourceId;
using ember::support::SourceLocation;
using ember::support::SourceSpan;
using ember::support::SourceText;

EMBER_TEST("valid UTF-8 and locations") {
    const SourceText source{SourceId{7}, "valid.ember", "a\xCE\xB1\r\nb"};

    tests.expect(!source.validateEncoding().has_value(), "valid UTF-8 source is accepted");
    tests.expect(source.id() == SourceId{7}, "source id is preserved");
    tests.expect(source.name() == "valid.ember", "source name is preserved");
    tests.expect(source.locationAt(0) == SourceLocation{.offset = 0, .line = 1, .column = 1},
                 "first byte has line 1, column 1");
    tests.expect(source.locationAt(1) == SourceLocation{.offset = 1, .line = 1, .column = 2},
                 "Unicode scalar starts at the next column");
    tests.expect(source.locationAt(3) == SourceLocation{.offset = 3, .line = 1, .column = 3},
                 "column advances once for a multi-byte scalar");
    tests.expect(source.locationAt(5) == SourceLocation{.offset = 5, .line = 2, .column = 1},
                 "CRLF advances to one new line");
    tests.expect(!source.locationAt(2).has_value(), "middle of a UTF-8 scalar has no location");
    tests.expect(!source.locationAt(4).has_value(), "middle of CRLF has no location");
}

EMBER_TEST("slice") {
    const SourceText source{SourceId{3}, "slice.ember", "h\xCE\xB1llo"};

    tests.expect(source.slice(SourceSpan{.source = SourceId{3}, .begin = 1, .end = 3}) ==
                     "\xCE\xB1",
                 "slice returns the requested lexeme");
    tests.expect(!source.slice(SourceSpan{.source = SourceId{4}, .begin = 1, .end = 3}).has_value(),
                 "slice rejects a span from another source");
    tests.expect(!source.slice(SourceSpan{.source = SourceId{3}, .begin = 4, .end = 7}).has_value(),
                 "slice rejects an out-of-range span");
    tests.expect(!source.slice(SourceSpan{.source = SourceId{3}, .begin = 3, .end = 1}).has_value(),
                 "slice rejects a reversed span");
    tests.expect(!source.slice(SourceSpan{.source = SourceId{3}, .begin = 2, .end = 3}).has_value(),
                 "slice rejects a span through a UTF-8 scalar");
    tests.expect(
        source.slice(
            SourceSpan{.source = SourceId{3}, .begin = source.size(), .end = source.size()}) == "",
        "slice allows a zero-width EOF span");
}

EMBER_TEST("UTF-8 BOM diagnostic") {
    const SourceText source{SourceId{11},
                            "bom.ember",
                            "\xEF\xBB\xBF"
                            "fn main() {}"};
    const auto diagnostic = source.validateEncoding();
    tests.expect(diagnostic.has_value(), "UTF-8 BOM is rejected");
    tests.expect(source.name() == "bom.ember", "source name survives a BOM diagnostic");
    tests.expect(source.contents().starts_with("\xEF\xBB\xBF"),
                 "source bytes survive a BOM diagnostic");
    if (!diagnostic.has_value()) {
        return;
    }

    tests.expect(diagnostic->stage == DiagnosticStage::lexer, "BOM diagnostic stage is lexer");
    tests.expect(diagnostic->severity == DiagnosticSeverity::error,
                 "BOM diagnostic severity is error");
    tests.expect(diagnostic->code == "E1001", "BOM diagnostic code is stable");
    tests.expect(diagnostic->primarySpan ==
                     SourceSpan{.source = SourceId{11}, .begin = 0, .end = 3},
                 "BOM diagnostic covers all three BOM bytes");
}

EMBER_TEST("malformed UTF-8 diagnostic") {
    const SourceText source{SourceId{12},
                            "invalid.ember",
                            "\xC2"
                            "A"};
    const auto diagnostic = source.validateEncoding();
    tests.expect(diagnostic.has_value(), "invalid UTF-8 is rejected");
    tests.expect(source.name() == "invalid.ember", "source name survives an UTF-8 diagnostic");
    if (!diagnostic.has_value()) {
        return;
    }

    tests.expect(diagnostic->code == "E1002", "invalid UTF-8 diagnostic code is stable");
    tests.expect(diagnostic->primarySpan ==
                     SourceSpan{.source = SourceId{12}, .begin = 0, .end = 2},
                 "invalid UTF-8 span contains the bytes needed to detect the error");
}

EMBER_TEST("line endings and Unicode boundaries") {
    const SourceText empty{SourceId{13}, "empty.ember", ""};
    tests.expect(empty.locationAt(0) == SourceLocation{.offset = 0, .line = 1, .column = 1},
                 "empty source has an EOF location");
    tests.expect(!empty.locationAt(1).has_value(), "offset after EOF has no location");

    const SourceText lineEndings{SourceId{14}, "lines.ember", "a\nb\rc"};
    tests.expect(lineEndings.locationAt(2) == SourceLocation{.offset = 2, .line = 2, .column = 1},
                 "LF starts the next line");
    tests.expect(lineEndings.locationAt(4) == SourceLocation{.offset = 4, .line = 3, .column = 1},
                 "CR starts the next line");

    const SourceText tabAndEmoji{SourceId{15}, "unicode.ember", "\t\xF0\x9F\x98\x80"};
    tests.expect(tabAndEmoji.locationAt(1) == SourceLocation{.offset = 1, .line = 1, .column = 2},
                 "tab advances one logical column");
    tests.expect(tabAndEmoji.locationAt(5) == SourceLocation{.offset = 5, .line = 1, .column = 3},
                 "four-byte scalar advances one logical column");
    tests.expect(!tabAndEmoji.isSourceBoundary(3),
                 "middle of a four-byte scalar is not a source boundary");
}

EMBER_TEST("invalid UTF-8 cases") {
    struct InvalidUtf8Case {
        std::string_view name;
        std::string bytes;
        SourceSpan expectedSpan;
    };

    const auto cases = std::array{
        InvalidUtf8Case{"truncated sequence",
                        "\xE2\x82",
                        SourceSpan{.source = SourceId{16}, .begin = 0, .end = 2}},
        InvalidUtf8Case{"stray continuation",
                        "\x80",
                        SourceSpan{.source = SourceId{16}, .begin = 0, .end = 1}},
        InvalidUtf8Case{"overlong sequence",
                        "\xC0\xAF",
                        SourceSpan{.source = SourceId{16}, .begin = 0, .end = 1}},
        InvalidUtf8Case{"UTF-16 surrogate",
                        "\xED\xA0\x80",
                        SourceSpan{.source = SourceId{16}, .begin = 0, .end = 2}},
        InvalidUtf8Case{"code point above U+10FFFF",
                        "\xF4\x90\x80\x80",
                        SourceSpan{.source = SourceId{16}, .begin = 0, .end = 2}},
    };

    for (const auto& testCase : cases) {
        const SourceText source{SourceId{16}, std::string{testCase.name}, testCase.bytes};
        const auto diagnostic = source.validateEncoding();
        tests.expect(diagnostic.has_value(), "invalid UTF-8 case produces a diagnostic");
        if (!diagnostic.has_value()) {
            continue;
        }

        tests.expect(diagnostic->code == "E1002", "invalid UTF-8 case uses E1002");
        tests.expect(diagnostic->primarySpan == testCase.expectedSpan,
                     "invalid UTF-8 case has expected span");
    }
}

EMBER_TEST("invalid source boundaries") {
    const SourceText source{SourceId{17}, "invalid.ember", "\x80"};
    const auto diagnostic = source.validateEncoding();
    tests.expect(diagnostic.has_value(), "invalid source has a diagnostic");
    if (!diagnostic.has_value()) {
        return;
    }

    tests.expect(source.locationAt(diagnostic->primarySpan.begin) ==
                     SourceLocation{.offset = 0, .line = 1, .column = 1},
                 "location of invalid sequence is available");
    tests.expect(!source.isSourceBoundary(source.size()),
                 "EOF after malformed UTF-8 is not a text boundary");
    tests.expect(!source.slice(diagnostic->primarySpan).has_value(),
                 "text slice rejects malformed UTF-8");
    tests.expect(source.rawSlice(diagnostic->primarySpan) == std::string_view{"\x80", 1},
                 "raw slice exposes diagnostic bytes");
}

EMBER_TEST("BOM before malformed UTF-8") {
    const SourceText source{SourceId{18},
                            "bom-invalid.ember",
                            "\xEF\xBB\xBF"
                            "\xC2"
                            "A"};
    const auto diagnostic = source.validateEncoding();
    tests.expect(diagnostic.has_value(), "source has an encoding diagnostic");
    if (!diagnostic.has_value()) {
        return;
    }

    tests.expect(diagnostic->code == "E1001", "BOM remains the first reported diagnostic");
    tests.expect(source.isSourceBoundary(3), "start of malformed suffix remains a boundary");
    tests.expect(!source.isSourceBoundary(4), "offset inside malformed suffix is not a boundary");
    tests.expect(!source.isSourceBoundary(source.size()),
                 "EOF after malformed suffix is not a text boundary");
    tests.expect(!source.slice(SourceSpan{.source = source.id(), .begin = 0, .end = source.size()})
                      .has_value(),
                 "text slice rejects malformed UTF-8 hidden behind BOM");
    tests.expect(source.rawSlice(SourceSpan{.source = source.id(), .begin = 3, .end = 5}) ==
                     std::string_view{"\xC2"
                                      "A",
                                      2},
                 "raw slice exposes malformed bytes after BOM");
}

} // namespace
