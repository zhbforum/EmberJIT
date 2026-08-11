#pragma once

#include "ember/support/source_location.hpp"

#include <string_view>

namespace ember::frontend {

enum class TokenKind {
    endOfFile,
    identifier,
    integerLiteral,
    float64Literal,
    float32Literal,

    keywordFn,
    keywordLet,
    keywordReturn,
    keywordIf,
    keywordElse,
    keywordWhile,
    keywordTrue,
    keywordFalse,
    keywordI64,
    keywordF64,
    keywordF32,
    keywordBool,
    keywordVoid,
    keywordExtern,

    plus,
    minus,
    star,
    slash,
    percent,
    equal,
    equalEqual,
    bangEqual,
    less,
    lessEqual,
    greater,
    greaterEqual,

    leftParen,
    rightParen,
    leftBrace,
    rightBrace,
    comma,
    colon,
    semicolon,
    arrow,
};

[[nodiscard]] constexpr auto tokenKindName(TokenKind kind) noexcept -> std::string_view {
    switch (kind) {
    case TokenKind::endOfFile:
        return "end_of_file";
    case TokenKind::identifier:
        return "identifier";
    case TokenKind::integerLiteral:
        return "integer_literal";
    case TokenKind::float64Literal:
        return "float64_literal";
    case TokenKind::float32Literal:
        return "float32_literal";
    case TokenKind::keywordFn:
        return "fn";
    case TokenKind::keywordLet:
        return "let";
    case TokenKind::keywordReturn:
        return "return";
    case TokenKind::keywordIf:
        return "if";
    case TokenKind::keywordElse:
        return "else";
    case TokenKind::keywordWhile:
        return "while";
    case TokenKind::keywordTrue:
        return "true";
    case TokenKind::keywordFalse:
        return "false";
    case TokenKind::keywordI64:
        return "i64";
    case TokenKind::keywordF64:
        return "f64";
    case TokenKind::keywordF32:
        return "f32";
    case TokenKind::keywordBool:
        return "bool";
    case TokenKind::keywordVoid:
        return "void";
    case TokenKind::keywordExtern:
        return "extern";
    case TokenKind::plus:
        return "+";
    case TokenKind::minus:
        return "-";
    case TokenKind::star:
        return "*";
    case TokenKind::slash:
        return "/";
    case TokenKind::percent:
        return "%";
    case TokenKind::equal:
        return "=";
    case TokenKind::equalEqual:
        return "==";
    case TokenKind::bangEqual:
        return "!=";
    case TokenKind::less:
        return "<";
    case TokenKind::lessEqual:
        return "<=";
    case TokenKind::greater:
        return ">";
    case TokenKind::greaterEqual:
        return ">=";
    case TokenKind::leftParen:
        return "(";
    case TokenKind::rightParen:
        return ")";
    case TokenKind::leftBrace:
        return "{";
    case TokenKind::rightBrace:
        return "}";
    case TokenKind::comma:
        return ",";
    case TokenKind::colon:
        return ":";
    case TokenKind::semicolon:
        return ";";
    case TokenKind::arrow:
        return "->";
    }

    return "<invalid-token-kind>";
}

struct Token {
    TokenKind kind{TokenKind::endOfFile};
    support::SourceSpan span{};

    friend constexpr bool operator==(Token, Token) = default;
};

} // namespace ember::frontend
