#include "ember/frontend/lexer.hpp"

#include "test_harness.hpp"

#include <array>
#include <string>
#include <string_view>

namespace
{

using ember::frontend::Lexer;
using ember::frontend::LexResult;
using ember::frontend::TokenKind;
using ember::support::SourceId;
using ember::support::SourceSpan;
using ember::support::SourceText;

[[nodiscard]] auto lex(std::string_view text) -> LexResult
{
    const SourceText source{SourceId{31}, "test.ember", std::string{text}};
    return Lexer{}.lex(source);
}

EMBER_TEST("lexer recognizes punctuation and maximal munch operators")
{
    const auto result = lex("+ - * / % = == != < <= > >= ( ) { } , : ; ->");
    constexpr auto expectedKinds = std::array{
        TokenKind::plus,       TokenKind::minus,      TokenKind::star,
        TokenKind::slash,      TokenKind::percent,    TokenKind::equal,
        TokenKind::equalEqual, TokenKind::bangEqual,  TokenKind::less,
        TokenKind::lessEqual,  TokenKind::greater,    TokenKind::greaterEqual,
        TokenKind::leftParen,  TokenKind::rightParen, TokenKind::leftBrace,
        TokenKind::rightBrace, TokenKind::comma,      TokenKind::colon,
        TokenKind::semicolon,  TokenKind::arrow,      TokenKind::endOfFile,
    };

    tests.expect(result.diagnostics.empty(), "punctuation has no diagnostics");
    tests.expect(result.tokens.size() == expectedKinds.size(),
                 "punctuation emits every expected token");
    if (result.tokens.size() != expectedKinds.size())
    {
        return;
    }

    for (std::size_t index = 0; index < expectedKinds.size(); ++index)
    {
        tests.expect(result.tokens[index].kind == expectedKinds[index],
                     "operator uses maximal munch token kind");
    }
}

EMBER_TEST("single-character operators consume exactly one byte")
{
    const auto result = lex("=-/<>");
    constexpr auto expectedKinds = std::array{
        TokenKind::equal, TokenKind::minus,   TokenKind::slash,
        TokenKind::less,  TokenKind::greater, TokenKind::endOfFile,
    };

    tests.expect(result.diagnostics.empty(),
                 "single-character operators have no diagnostics");
    tests.expect(result.tokens.size() == expectedKinds.size(),
                 "single-character operators emit expected tokens");
    if (result.tokens.size() != expectedKinds.size())
    {
        return;
    }

    for (std::size_t index = 0; index < expectedKinds.size(); ++index)
    {
        tests.expect(result.tokens[index].kind == expectedKinds[index],
                     "single-character operator has expected kind");
        const auto end = index + 1 < result.tokens.size() ? index + 1 : index;
        tests.expect(
            result.tokens[index].span ==
                SourceSpan{.source = SourceId{31}, .begin = index, .end = end},
            "single-character operator has expected span");
    }
}

EMBER_TEST("lexer skips whitespace and line comments")
{
    const auto result = lex("\t// comment\r\n->// tail");

    tests.expect(result.diagnostics.empty(),
                 "comments and whitespace have no diagnostics");
    tests.expect(result.tokens.size() == 2,
                 "comment input emits arrow and EOF");
    if (result.tokens.size() != 2)
    {
        return;
    }

    tests.expect(result.tokens[0].kind == TokenKind::arrow,
                 "arrow remains after CRLF comment");
    tests.expect(result.tokens[0].span ==
                     SourceSpan{.source = SourceId{31}, .begin = 13, .end = 15},
                 "arrow span follows CRLF exactly");
    tests.expect(result.tokens[1].kind == TokenKind::endOfFile,
                 "EOF token is emitted after comment");
    tests.expect(result.tokens[1].span ==
                     SourceSpan{.source = SourceId{31}, .begin = 22, .end = 22},
                 "EOF span is empty at the end of input");
}

EMBER_TEST("lexer emits EOF for empty input")
{
    const auto result = lex("");

    tests.expect(result.diagnostics.empty(), "empty input has no diagnostics");
    tests.expect(result.tokens.size() == 1, "empty input emits one token");
    if (result.tokens.size() != 1)
    {
        return;
    }

    tests.expect(result.tokens.front().kind == TokenKind::endOfFile,
                 "empty input token is EOF");
    tests.expect(result.tokens.front().span ==
                     SourceSpan{.source = SourceId{31}, .begin = 0, .end = 0},
                 "empty input EOF span is empty");
}

EMBER_TEST("lexer recognizes every keyword")
{
    const auto result = lex(
        "fn let return if else while true false i64 f64 f32 bool void extern");
    constexpr auto expectedKinds = std::array{
        TokenKind::keywordFn,     TokenKind::keywordLet,
        TokenKind::keywordReturn, TokenKind::keywordIf,
        TokenKind::keywordElse,   TokenKind::keywordWhile,
        TokenKind::keywordTrue,   TokenKind::keywordFalse,
        TokenKind::keywordI64,    TokenKind::keywordF64,
        TokenKind::keywordF32,    TokenKind::keywordBool,
        TokenKind::keywordVoid,   TokenKind::keywordExtern,
        TokenKind::endOfFile,
    };

    tests.expect(result.diagnostics.empty(), "keywords have no diagnostics");
    tests.expect(result.tokens.size() == expectedKinds.size(),
                 "every keyword emits one token");
    if (result.tokens.size() != expectedKinds.size())
    {
        return;
    }

    for (std::size_t index = 0; index < expectedKinds.size(); ++index)
    {
        tests.expect(result.tokens[index].kind == expectedKinds[index],
                     "keyword uses the corresponding token kind");
    }
}

EMBER_TEST("lexer recognizes ASCII identifiers")
{
    const auto result = lex("fnx i64value _ true_value alpha9 _2");

    tests.expect(result.diagnostics.empty(), "identifiers have no diagnostics");
    tests.expect(result.tokens.size() == 7,
                 "identifiers emit six tokens and EOF");
    if (result.tokens.size() != 7)
    {
        return;
    }

    for (std::size_t index = 0; index < result.tokens.size() - 1; ++index)
    {
        tests.expect(result.tokens[index].kind == TokenKind::identifier,
                     "keyword-like identifier remains an identifier");
    }
    tests.expect(result.tokens.back().kind == TokenKind::endOfFile,
                 "identifier input ends with EOF");
    tests.expect(result.tokens[3].span ==
                     SourceSpan{.source = SourceId{31}, .begin = 15, .end = 25},
                 "identifier span covers its complete ASCII lexeme");
}

EMBER_TEST("lexer recognizes numeric literals")
{
    const auto result = lex("0 42 19.99 0.5f");
    constexpr auto expectedKinds = std::array{
        TokenKind::integerLiteral, TokenKind::integerLiteral,
        TokenKind::float64Literal, TokenKind::float32Literal,
        TokenKind::endOfFile,
    };

    tests.expect(result.diagnostics.empty(),
                 "numeric literals have no diagnostics");
    tests.expect(result.tokens.size() == expectedKinds.size(),
                 "numeric literals emit expected tokens");
    if (result.tokens.size() != expectedKinds.size())
    {
        return;
    }

    for (std::size_t index = 0; index < expectedKinds.size(); ++index)
    {
        tests.expect(result.tokens[index].kind == expectedKinds[index],
                     "numeric literal uses the expected token kind");
    }
    tests.expect(result.tokens[3].span ==
                     SourceSpan{.source = SourceId{31}, .begin = 11, .end = 15},
                 "float32 span includes its suffix");
}

EMBER_TEST("lexer rejects invalid numeric literals")
{
    struct InvalidLiteralCase
    {
        std::string_view text;
        std::string_view expectedCode;
    };

    constexpr auto cases = std::array{
        InvalidLiteralCase{"01", "E1005"},
        InvalidLiteralCase{"1.", "E1004"},
        InvalidLiteralCase{"1e3", "E1004"},
        InvalidLiteralCase{"0x10", "E1004"},
        InvalidLiteralCase{"1_000", "E1004"},
        InvalidLiteralCase{"3f", "E1004"},
    };

    for (const auto &testCase : cases)
    {
        const auto result = lex(testCase.text);
        tests.expect(result.diagnostics.size() == 1,
                     "invalid literal has one diagnostic");
        if (result.diagnostics.size() != 1)
        {
            continue;
        }

        const auto &diagnostic = result.diagnostics.front();
        tests.expect(diagnostic.code == testCase.expectedCode,
                     "invalid literal has the expected code");
        tests.expect(diagnostic.primarySpan ==
                         SourceSpan{.source = SourceId{31},
                                    .begin = 0,
                                    .end = testCase.text.size()},
                     "invalid literal span covers the full candidate");
    }

    const auto dotResult = lex(".5");
    tests.expect(dotResult.diagnostics.size() == 1,
                 "literal without integer part has one diagnostic");
    if (dotResult.diagnostics.size() == 1)
    {
        tests.expect(dotResult.diagnostics.front().code == "E1003",
                     "literal without integer part is an unexpected dot");
        tests.expect(
            dotResult.diagnostics.front().primarySpan ==
                SourceSpan{.source = SourceId{31}, .begin = 0, .end = 1},
            "unexpected dot span has one byte");
    }
}

EMBER_TEST("lexer treats signs as separate tokens")
{
    const auto result = lex("-1 +1");
    constexpr auto expectedKinds = std::array{
        TokenKind::minus,          TokenKind::integerLiteral, TokenKind::plus,
        TokenKind::integerLiteral, TokenKind::endOfFile,
    };

    tests.expect(result.diagnostics.empty(),
                 "signed forms have no diagnostics");
    tests.expect(result.tokens.size() == expectedKinds.size(),
                 "signs and integers emit separate tokens");
    if (result.tokens.size() != expectedKinds.size())
    {
        return;
    }

    for (std::size_t index = 0; index < expectedKinds.size(); ++index)
    {
        tests.expect(result.tokens[index].kind == expectedKinds[index],
                     "signs are not part of integer literals");
    }
}

EMBER_TEST("lexer rejects unsupported characters")
{
    const auto asciiResult = lex("@");
    tests.expect(asciiResult.diagnostics.size() == 1,
                 "unexpected ASCII character has one diagnostic");
    if (asciiResult.diagnostics.size() == 1)
    {
        tests.expect(asciiResult.diagnostics.front().code == "E1003",
                     "unexpected ASCII uses E1003");
        tests.expect(
            asciiResult.diagnostics.front().primarySpan ==
                SourceSpan{.source = SourceId{31}, .begin = 0, .end = 1},
            "unexpected ASCII span is one byte");
    }

    const auto unicodeResult = lex("\xCE\xB1");
    tests.expect(unicodeResult.diagnostics.size() == 1,
                 "unsupported Unicode character has one diagnostic");
    if (unicodeResult.diagnostics.size() == 1)
    {
        tests.expect(unicodeResult.diagnostics.front().code == "E1003",
                     "unsupported Unicode uses E1003");
        tests.expect(
            unicodeResult.diagnostics.front().primarySpan ==
                SourceSpan{.source = SourceId{31}, .begin = 0, .end = 2},
            "unsupported Unicode span covers its complete UTF-8 scalar");
    }
}

EMBER_TEST("lexer forwards source encoding diagnostics")
{
    const SourceText source{SourceId{32}, "invalid.ember",
                            "\xC2"
                            "A"};
    const auto result = Lexer{}.lex(source);

    tests.expect(result.tokens.empty(), "encoding error emits no lexer tokens");
    tests.expect(result.diagnostics.size() == 1,
                 "encoding error emits one diagnostic");
    if (result.diagnostics.size() == 1)
    {
        tests.expect(result.diagnostics.front().code == "E1002",
                     "encoding error preserves E1002");
        tests.expect(
            result.diagnostics.front().primarySpan ==
                SourceSpan{.source = SourceId{32}, .begin = 0, .end = 2},
            "encoding error preserves source span");
    }
}

} // namespace
