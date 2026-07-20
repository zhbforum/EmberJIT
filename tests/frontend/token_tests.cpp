#include "ember/frontend/token.hpp"

#include "test_harness.hpp"

#include <array>
#include <type_traits>

namespace
{

using ember::frontend::Token;
using ember::frontend::TokenKind;
using ember::frontend::tokenKindName;
using ember::support::SourceId;
using ember::support::SourceSpan;

static_assert(std::is_trivially_copyable_v<Token>);

EMBER_TEST("token kind names are stable")
{
    struct TokenKindCase
    {
        TokenKind kind;
        std::string_view expectedName;
    };

    constexpr auto cases = std::array{
        TokenKindCase{TokenKind::endOfFile, "end_of_file"},
        TokenKindCase{TokenKind::identifier, "identifier"},
        TokenKindCase{TokenKind::integerLiteral, "integer_literal"},
        TokenKindCase{TokenKind::float64Literal, "float64_literal"},
        TokenKindCase{TokenKind::float32Literal, "float32_literal"},
        TokenKindCase{TokenKind::keywordFn, "fn"},
        TokenKindCase{TokenKind::keywordLet, "let"},
        TokenKindCase{TokenKind::keywordReturn, "return"},
        TokenKindCase{TokenKind::keywordIf, "if"},
        TokenKindCase{TokenKind::keywordElse, "else"},
        TokenKindCase{TokenKind::keywordWhile, "while"},
        TokenKindCase{TokenKind::keywordTrue, "true"},
        TokenKindCase{TokenKind::keywordFalse, "false"},
        TokenKindCase{TokenKind::keywordI64, "i64"},
        TokenKindCase{TokenKind::keywordF64, "f64"},
        TokenKindCase{TokenKind::keywordF32, "f32"},
        TokenKindCase{TokenKind::keywordBool, "bool"},
        TokenKindCase{TokenKind::keywordVoid, "void"},
        TokenKindCase{TokenKind::keywordExtern, "extern"},
        TokenKindCase{TokenKind::plus, "+"},
        TokenKindCase{TokenKind::minus, "-"},
        TokenKindCase{TokenKind::star, "*"},
        TokenKindCase{TokenKind::slash, "/"},
        TokenKindCase{TokenKind::percent, "%"},
        TokenKindCase{TokenKind::equal, "="},
        TokenKindCase{TokenKind::equalEqual, "=="},
        TokenKindCase{TokenKind::bangEqual, "!="},
        TokenKindCase{TokenKind::less, "<"},
        TokenKindCase{TokenKind::lessEqual, "<="},
        TokenKindCase{TokenKind::greater, ">"},
        TokenKindCase{TokenKind::greaterEqual, ">="},
        TokenKindCase{TokenKind::leftParen, "("},
        TokenKindCase{TokenKind::rightParen, ")"},
        TokenKindCase{TokenKind::leftBrace, "{"},
        TokenKindCase{TokenKind::rightBrace, "}"},
        TokenKindCase{TokenKind::comma, ","},
        TokenKindCase{TokenKind::colon, ":"},
        TokenKindCase{TokenKind::semicolon, ";"},
        TokenKindCase{TokenKind::arrow, "->"},
    };

    for (const auto &testCase : cases)
    {
        tests.expect(tokenKindName(testCase.kind) == testCase.expectedName,
                     "token kind has its documented stable name");
    }
}

EMBER_TEST("token preserves kind and source span")
{
    constexpr Token token{
        .kind = TokenKind::keywordReturn,
        .span = SourceSpan{.source = SourceId{21}, .begin = 8, .end = 14},
    };

    tests.expect(token.kind == TokenKind::keywordReturn,
                 "token preserves its kind");
    tests.expect(token.span ==
                     SourceSpan{.source = SourceId{21}, .begin = 8, .end = 14},
                 "token preserves its source span");
}

} // namespace
