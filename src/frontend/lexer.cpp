#include "ember/frontend/lexer.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace ember::frontend {
namespace {

[[nodiscard]] constexpr auto asByte(char value) noexcept -> std::uint8_t {
    return static_cast<std::uint8_t>(static_cast<unsigned char>(value));
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

[[nodiscard]] constexpr auto isWhitespace(char value) noexcept -> bool {
    return value == ' ' || value == '\t' || value == '\r' || value == '\n';
}

[[nodiscard]] constexpr auto isAsciiLetter(char value) noexcept -> bool {
    return (value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z');
}

[[nodiscard]] constexpr auto isAsciiDigit(char value) noexcept -> bool {
    return value >= '0' && value <= '9';
}

[[nodiscard]] constexpr auto isIdentifierStart(char value) noexcept -> bool {
    return isAsciiLetter(value) || value == '_';
}

[[nodiscard]] constexpr auto isIdentifierContinue(char value) noexcept -> bool {
    return isIdentifierStart(value) || isAsciiDigit(value);
}

[[nodiscard]] constexpr auto isNumericCandidateCharacter(char value) noexcept -> bool {
    return isAsciiLetter(value) || isAsciiDigit(value) || value == '_' || value == '.';
}

[[nodiscard]] constexpr auto isValidInteger(std::string_view text) noexcept -> bool {
    if (text.empty() || !isAsciiDigit(text.front())) {
        return false;
    }

    if (text.front() == '0' && text.size() != 1) {
        return false;
    }

    for (const auto character : text) {
        if (!isAsciiDigit(character)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] constexpr auto isAllDigits(std::string_view text) noexcept -> bool {
    if (text.empty()) {
        return false;
    }

    for (const auto character : text) {
        if (!isAsciiDigit(character)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] constexpr auto
classifyNumber(std::string_view text) noexcept -> std::optional<TokenKind> {
    const auto decimalPoint = text.find('.');
    if (decimalPoint == std::string_view::npos) {
        if (isValidInteger(text)) {
            return TokenKind::integerLiteral;
        }
        return std::nullopt;
    }

    if (text.find('.', decimalPoint + 1) != std::string_view::npos) {
        return std::nullopt;
    }

    const auto isFloat32 = text.ends_with('f');
    const auto fractionEnd = isFloat32 ? text.size() - 1 : text.size();
    if (decimalPoint + 1 >= fractionEnd) {
        return std::nullopt;
    }

    const auto integerPart = text.substr(0, decimalPoint);
    const auto fractionPart = text.substr(decimalPoint + 1, fractionEnd - decimalPoint - 1);
    if (!isValidInteger(integerPart) || !isAllDigits(fractionPart)) {
        return std::nullopt;
    }

    return isFloat32 ? TokenKind::float32Literal : TokenKind::float64Literal;
}

[[nodiscard]] constexpr auto classifyIdentifier(std::string_view text) noexcept -> TokenKind {
    if (text == "fn") {
        return TokenKind::keywordFn;
    }
    if (text == "let") {
        return TokenKind::keywordLet;
    }
    if (text == "return") {
        return TokenKind::keywordReturn;
    }
    if (text == "if") {
        return TokenKind::keywordIf;
    }
    if (text == "else") {
        return TokenKind::keywordElse;
    }
    if (text == "while") {
        return TokenKind::keywordWhile;
    }
    if (text == "true") {
        return TokenKind::keywordTrue;
    }
    if (text == "false") {
        return TokenKind::keywordFalse;
    }
    if (text == "i64") {
        return TokenKind::keywordI64;
    }
    if (text == "f64") {
        return TokenKind::keywordF64;
    }
    if (text == "f32") {
        return TokenKind::keywordF32;
    }
    if (text == "bool") {
        return TokenKind::keywordBool;
    }
    if (text == "void") {
        return TokenKind::keywordVoid;
    }
    if (text == "extern") {
        return TokenKind::keywordExtern;
    }
    return TokenKind::identifier;
}

[[nodiscard]] auto makeDiagnostic(support::SourceId source,
                                  std::size_t begin,
                                  std::size_t end,
                                  std::string code,
                                  std::string message) -> support::Diagnostic {
    return support::Diagnostic{
        .stage = support::DiagnosticStage::lexer,
        .severity = support::DiagnosticSeverity::error,
        .code = std::move(code),
        .message = std::move(message),
        .primarySpan = support::SourceSpan{.source = source, .begin = begin, .end = end},
    };
}

class Scanner {
public:
    explicit Scanner(const support::SourceText& source)
        : source_(source),
          text_(source.contents()) {
    }

    [[nodiscard]] LexResult scan() {
        while (!atEnd()) {
            const auto begin = offset_;
            const auto character = current();

            if (isWhitespace(character)) {
                ++offset_;
                continue;
            }

            switch (character) {
            case '+':
                emit(TokenKind::plus, begin, ++offset_);
                break;
            case '-':
                if (matchNext('>')) {
                    emit(TokenKind::arrow, begin, offset_);
                } else {
                    emit(TokenKind::minus, begin, ++offset_);
                }
                break;
            case '*':
                emit(TokenKind::star, begin, ++offset_);
                break;
            case '/':
                if (matchNext('/')) {
                    skipComment();
                } else {
                    emit(TokenKind::slash, begin, ++offset_);
                }
                break;
            case '%':
                emit(TokenKind::percent, begin, ++offset_);
                break;
            case '=':
                if (matchNext('=')) {
                    emit(TokenKind::equalEqual, begin, offset_);
                } else {
                    emit(TokenKind::equal, begin, ++offset_);
                }
                break;
            case '!':
                if (matchNext('=')) {
                    emit(TokenKind::bangEqual, begin, offset_);
                    break;
                }
                return error(begin, begin + 1, "unexpected character");
            case '<':
                if (matchNext('=')) {
                    emit(TokenKind::lessEqual, begin, offset_);
                } else {
                    emit(TokenKind::less, begin, ++offset_);
                }
                break;
            case '>':
                if (matchNext('=')) {
                    emit(TokenKind::greaterEqual, begin, offset_);
                } else {
                    emit(TokenKind::greater, begin, ++offset_);
                }
                break;
            case '(':
                emit(TokenKind::leftParen, begin, ++offset_);
                break;
            case ')':
                emit(TokenKind::rightParen, begin, ++offset_);
                break;
            case '{':
                emit(TokenKind::leftBrace, begin, ++offset_);
                break;
            case '}':
                emit(TokenKind::rightBrace, begin, ++offset_);
                break;
            case ',':
                emit(TokenKind::comma, begin, ++offset_);
                break;
            case ':':
                emit(TokenKind::colon, begin, ++offset_);
                break;
            case ';':
                emit(TokenKind::semicolon, begin, ++offset_);
                break;
            default:
                if (isIdentifierStart(character)) {
                    scanIdentifier(begin);
                    break;
                }
                if (isAsciiDigit(character)) {
                    if (!scanNumber(begin)) {
                        return std::move(result_);
                    }
                    break;
                }
                if (asByte(character) >= 0x80U) {
                    return error(begin,
                                 begin + utf8Length(asByte(character)),
                                 "unsupported non-ASCII character");
                }
                return error(begin, begin + 1, "unexpected character");
            }
        }

        emit(TokenKind::endOfFile, offset_, offset_);
        return std::move(result_);
    }

private:
    [[nodiscard]] bool atEnd() const noexcept {
        return offset_ == text_.size();
    }

    [[nodiscard]] char current() const noexcept {
        return text_[offset_];
    }

    bool matchNext(char expected) noexcept {
        if (offset_ + 1 >= text_.size() || text_[offset_ + 1] != expected) {
            return false;
        }

        offset_ += 2;
        return true;
    }

    void skipComment() noexcept {
        while (!atEnd() && current() != '\r' && current() != '\n') {
            ++offset_;
        }
    }

    void scanIdentifier(std::size_t begin) {
        while (!atEnd() && isIdentifierContinue(current())) {
            ++offset_;
        }

        const auto identifier = text_.substr(begin, offset_ - begin);
        emit(classifyIdentifier(identifier), begin, offset_);
    }

    [[nodiscard]] bool scanNumber(std::size_t begin) {
        while (!atEnd() && isNumericCandidateCharacter(current())) {
            ++offset_;
        }

        const auto literal = text_.substr(begin, offset_ - begin);
        if (literal.size() > 1 && literal.front() == '0' && isAsciiDigit(literal[1])) {
            addDiagnostic(begin, offset_, "E1005", "integer literal has a leading zero");
            return false;
        }

        const auto kind = classifyNumber(literal);
        if (!kind.has_value()) {
            addDiagnostic(begin, offset_, "E1004", "invalid numeric literal");
            return false;
        }

        emit(*kind, begin, offset_);
        return true;
    }

    void emit(TokenKind kind, std::size_t begin, std::size_t end) {
        result_.tokens.push_back(Token{
            .kind = kind,
            .span = support::SourceSpan{.source = source_.id(), .begin = begin, .end = end},
        });
    }

    [[nodiscard]] LexResult error(std::size_t begin, std::size_t end, std::string message) {
        addDiagnostic(begin, end, "E1003", std::move(message));
        return std::move(result_);
    }

    void addDiagnostic(std::size_t begin, std::size_t end, std::string code, std::string message) {
        result_.diagnostics.push_back(
            makeDiagnostic(source_.id(), begin, end, std::move(code), std::move(message)));
    }

    const support::SourceText& source_;
    std::string_view text_;
    std::size_t offset_{};
    LexResult result_;
};

} // namespace

auto Lexer::lex(const support::SourceText& source) const -> LexResult {
    if (const auto diagnostic = source.validateEncoding(); diagnostic.has_value()) {
        return LexResult{.tokens = {}, .diagnostics = {*diagnostic}};
    }

    return Scanner{source}.scan();
}

} // namespace ember::frontend
