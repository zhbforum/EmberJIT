#include "ember/frontend/parser.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <utility>

namespace ember::frontend {
namespace {

[[nodiscard]] auto joinSpans(support::SourceSpan first,
                             support::SourceSpan last) noexcept -> support::SourceSpan {
    return support::SourceSpan{
        .source = first.source,
        .begin = first.begin,
        .end = last.end,
    };
}

[[nodiscard]] auto makeDiagnostic(support::SourceId source,
                                  std::size_t begin,
                                  std::size_t end,
                                  std::string code,
                                  std::string message) -> support::Diagnostic {
    return support::Diagnostic{
        .stage = support::DiagnosticStage::parser,
        .severity = support::DiagnosticSeverity::error,
        .code = std::move(code),
        .message = std::move(message),
        .primarySpan =
            support::SourceSpan{
                .source = source,
                .begin = begin,
                .end = end,
            },
    };
}

class ParserState {
public:
    ParserState(const support::SourceText& source, std::span<const Token> tokens)
        : source_(source),
          tokens_(tokens) {
    }

    [[nodiscard]] auto parse() -> ParseResult {
        if (tokens_.empty() || tokens_.back().kind != TokenKind::endOfFile) {
            failAtEnd("E2002", "expected end-of-file token");
            return finish();
        }

        auto program = std::make_unique<Program>();
        program->span = support::SourceSpan{
            .source = source_.id(),
            .begin = 0,
            .end = source_.size(),
        };

        while (!at(TokenKind::endOfFile)) {
            auto function = parseFunction();
            if (!function.has_value()) {
                return finish();
            }
            program->functions.push_back(std::move(*function));
        }

        return ParseResult{.program = std::move(program), .diagnostics = {}};
    }

private:
    [[nodiscard]] auto finish() -> ParseResult {
        return ParseResult{.program = nullptr, .diagnostics = std::move(diagnostics_)};
    }

    [[nodiscard]] auto current() const -> const Token& {
        return tokens_[index_];
    }

    [[nodiscard]] auto at(TokenKind kind) const -> bool {
        return current().kind == kind;
    }

    [[nodiscard]] auto peek(TokenKind kind) const -> bool {
        return index_ + 1 < tokens_.size() && tokens_[index_ + 1].kind == kind;
    }

    auto advance() -> const Token& {
        const auto& token = current();
        if (!at(TokenKind::endOfFile)) {
            ++index_;
        }
        return token;
    }

    [[nodiscard]] auto consume(TokenKind kind, std::string message) -> const Token* {
        if (!at(kind)) {
            fail("E2002", std::move(message));
            return nullptr;
        }
        return &advance();
    }

    void fail(std::string code, std::string message) {
        if (!diagnostics_.empty()) {
            return;
        }
        const auto& token = current();
        diagnostics_.push_back(makeDiagnostic(source_.id(),
                                              token.span.begin,
                                              token.span.end,
                                              std::move(code),
                                              std::move(message)));
    }

    void failAtEnd(std::string code, std::string message) {
        diagnostics_.push_back(makeDiagnostic(source_.id(),
                                              source_.size(),
                                              source_.size(),
                                              std::move(code),
                                              std::move(message)));
    }

    [[nodiscard]] auto parseFunction() -> std::optional<FunctionDeclaration> {
        const auto* keyword = consume(TokenKind::keywordFn, "expected 'fn'");
        const auto* name = consume(TokenKind::identifier, "expected function name");
        if (keyword == nullptr || name == nullptr ||
            consume(TokenKind::leftParen, "expected '(' after function name") == nullptr) {
            return std::nullopt;
        }

        std::vector<Parameter> parameters;
        if (!at(TokenKind::rightParen)) {
            while (true) {
                auto parameter = parseParameter();
                if (!parameter.has_value()) {
                    return std::nullopt;
                }
                parameters.push_back(std::move(*parameter));
                if (!at(TokenKind::comma)) {
                    break;
                }
                advance();
            }
        }

        if (consume(TokenKind::rightParen, "expected ')' after parameters") == nullptr ||
            consume(TokenKind::arrow, "expected '->' before return type") == nullptr) {
            return std::nullopt;
        }
        const auto returnType = parseType();
        auto body = parseBlock();
        if (!returnType.has_value() || body == nullptr) {
            return std::nullopt;
        }

        return FunctionDeclaration{
            .span = joinSpans(keyword->span, body->span),
            .nameSpan = name->span,
            .parameters = std::move(parameters),
            .returnTypeSpan = returnType->second,
            .returnType = returnType->first,
            .body = std::move(*body),
        };
    }

    [[nodiscard]] auto parseParameter() -> std::optional<Parameter> {
        const auto* name = consume(TokenKind::identifier, "expected parameter name");
        if (name == nullptr ||
            consume(TokenKind::colon, "expected ':' after parameter") == nullptr) {
            return std::nullopt;
        }
        const auto type = parseType();
        if (!type.has_value()) {
            return std::nullopt;
        }
        return Parameter{.span = joinSpans(name->span, type->second),
                         .nameSpan = name->span,
                         .typeSpan = type->second,
                         .type = type->first};
    }

    [[nodiscard]] auto parseType() -> std::optional<std::pair<TypeName, support::SourceSpan>> {
        const auto token = current();
        TypeName type{};
        switch (token.kind) {
        case TokenKind::keywordI64:
            type = TypeName::i64;
            break;
        case TokenKind::keywordF64:
            type = TypeName::f64;
            break;
        case TokenKind::keywordBool:
            type = TypeName::boolean;
            break;
        case TokenKind::keywordVoid:
            type = TypeName::voidType;
            break;
        default:
            fail("E2001", "expected type name");
            return std::nullopt;
        }
        advance();
        return std::pair{type, token.span};
    }

    [[nodiscard]] auto parseBlock() -> BlockPtr {
        const auto* open = consume(TokenKind::leftBrace, "expected '{'");
        if (open == nullptr) {
            return nullptr;
        }
        auto block = std::make_unique<Block>();
        while (!at(TokenKind::rightBrace)) {
            if (at(TokenKind::endOfFile)) {
                fail("E2002", "expected '}' before end of file");
                return nullptr;
            }
            auto statement = parseStatement();
            if (!statement.has_value()) {
                return nullptr;
            }
            block->statements.push_back(std::move(*statement));
        }
        const auto& close = advance();
        block->span = joinSpans(open->span, close.span);
        return block;
    }

    [[nodiscard]] auto parseStatement() -> std::optional<Statement> {
        switch (current().kind) {
        case TokenKind::keywordLet:
            return parseLet();
        case TokenKind::keywordReturn:
            return parseReturn();
        case TokenKind::keywordIf:
            return parseIf();
        case TokenKind::keywordWhile:
            return parseWhile();
        case TokenKind::leftBrace:
            return parseNestedBlock();
        case TokenKind::identifier:
            if (peek(TokenKind::equal)) {
                return parseAssignment();
            }
            break;
        default:
            break;
        }

        auto expression = parseExpression();
        if (expression == nullptr) {
            return std::nullopt;
        }
        const auto* semicolon = consume(TokenKind::semicolon, "expected ';' after expression");
        if (semicolon == nullptr) {
            return std::nullopt;
        }
        const auto span = joinSpans(expression->span, semicolon->span);
        return Statement{.span = span,
                         .node = ExpressionStatement{.expression = std::move(expression)}};
    }

    [[nodiscard]] auto parseNestedBlock() -> std::optional<Statement> {
        auto block = parseBlock();
        if (block == nullptr) {
            return std::nullopt;
        }
        const auto span = block->span;
        return Statement{.span = span, .node = std::move(block)};
    }

    [[nodiscard]] auto parseLet() -> std::optional<Statement> {
        const auto& keyword = advance();
        const auto* name = consume(TokenKind::identifier, "expected variable name");
        if (name == nullptr ||
            consume(TokenKind::colon, "expected ':' after variable name") == nullptr) {
            return std::nullopt;
        }
        const auto type = parseType();
        if (!type.has_value() ||
            consume(TokenKind::equal, "expected '=' after variable type") == nullptr) {
            return std::nullopt;
        }
        auto initializer = parseExpression();
        const auto* semicolon = consume(TokenKind::semicolon, "expected ';' after declaration");
        if (initializer == nullptr || semicolon == nullptr) {
            return std::nullopt;
        }
        return Statement{.span = joinSpans(keyword.span, semicolon->span),
                         .node = LetStatement{.nameSpan = name->span,
                                              .typeSpan = type->second,
                                              .type = type->first,
                                              .initializer = std::move(initializer)}};
    }

    [[nodiscard]] auto parseAssignment() -> std::optional<Statement> {
        const auto& target = advance();
        advance();
        auto value = parseExpression();
        const auto* semicolon = consume(TokenKind::semicolon, "expected ';' after assignment");
        if (value == nullptr || semicolon == nullptr) {
            return std::nullopt;
        }
        return Statement{
            .span = joinSpans(target.span, semicolon->span),
            .node = AssignmentStatement{.targetSpan = target.span, .value = std::move(value)}};
    }

    [[nodiscard]] auto parseReturn() -> std::optional<Statement> {
        const auto& keyword = advance();
        ExpressionPtr value;
        if (!at(TokenKind::semicolon)) {
            auto expression = parseExpression();
            if (expression == nullptr) {
                return std::nullopt;
            }
            value = std::move(expression);
        }
        const auto* semicolon = consume(TokenKind::semicolon, "expected ';' after return");
        if (semicolon == nullptr) {
            return std::nullopt;
        }
        return Statement{.span = joinSpans(keyword.span, semicolon->span),
                         .node = ReturnStatement{.value = std::move(value)}};
    }

    [[nodiscard]] auto parseIf() -> std::optional<Statement> {
        const auto& keyword = advance();
        auto condition = parseExpression();
        auto thenBlock = parseBlock();
        if (condition == nullptr || thenBlock == nullptr) {
            return std::nullopt;
        }
        StatementPtr elseBranch;
        support::SourceSpan end = thenBlock->span;
        if (at(TokenKind::keywordElse)) {
            advance();
            auto statement = at(TokenKind::keywordIf) ? parseIf() : parseNestedBlock();
            if (!statement.has_value()) {
                return std::nullopt;
            }
            end = statement->span;
            elseBranch = std::make_unique<Statement>(std::move(*statement));
        }
        return Statement{.span = joinSpans(keyword.span, end),
                         .node = IfStatement{.condition = std::move(condition),
                                             .thenBlock = std::move(thenBlock),
                                             .elseBranch = std::move(elseBranch)}};
    }

    [[nodiscard]] auto parseWhile() -> std::optional<Statement> {
        const auto& keyword = advance();
        auto condition = parseExpression();
        auto body = parseBlock();
        if (condition == nullptr || body == nullptr) {
            return std::nullopt;
        }
        return Statement{
            .span = joinSpans(keyword.span, body->span),
            .node = WhileStatement{.condition = std::move(condition), .body = std::move(body)}};
    }

    [[nodiscard]] auto parseExpression() -> ExpressionPtr {
        return parseEquality();
    }

    [[nodiscard]] auto parseEquality() -> ExpressionPtr {
        auto expression = parseComparison();
        while (expression != nullptr && (at(TokenKind::equalEqual) || at(TokenKind::bangEqual))) {
            const auto& operatorToken = advance();
            const auto operation = operatorToken.kind == TokenKind::equalEqual
                                       ? BinaryOperator::equal
                                       : BinaryOperator::notEqual;
            auto right = parseComparison();
            if (right == nullptr) {
                return nullptr;
            }
            expression =
                binary(operation, operatorToken.span, std::move(expression), std::move(right));
        }
        return expression;
    }

    [[nodiscard]] auto parseComparison() -> ExpressionPtr {
        auto expression = parseTerm();
        while (expression != nullptr && (at(TokenKind::less) || at(TokenKind::lessEqual) ||
                                         at(TokenKind::greater) || at(TokenKind::greaterEqual))) {
            const auto& operatorToken = advance();
            const auto kind = operatorToken.kind;
            const auto operation = kind == TokenKind::less        ? BinaryOperator::less
                                   : kind == TokenKind::lessEqual ? BinaryOperator::lessEqual
                                   : kind == TokenKind::greater   ? BinaryOperator::greater
                                                                  : BinaryOperator::greaterEqual;
            auto right = parseTerm();
            if (right == nullptr) {
                return nullptr;
            }
            expression =
                binary(operation, operatorToken.span, std::move(expression), std::move(right));
        }
        return expression;
    }

    [[nodiscard]] auto parseTerm() -> ExpressionPtr {
        auto expression = parseFactor();
        while (expression != nullptr && (at(TokenKind::plus) || at(TokenKind::minus))) {
            const auto& operatorToken = advance();
            const auto operation = operatorToken.kind == TokenKind::plus ? BinaryOperator::add
                                                                         : BinaryOperator::subtract;
            auto right = parseFactor();
            if (right == nullptr) {
                return nullptr;
            }
            expression =
                binary(operation, operatorToken.span, std::move(expression), std::move(right));
        }
        return expression;
    }

    [[nodiscard]] auto parseFactor() -> ExpressionPtr {
        auto expression = parseUnary();
        while (expression != nullptr &&
               (at(TokenKind::star) || at(TokenKind::slash) || at(TokenKind::percent))) {
            const auto& operatorToken = advance();
            const auto kind = operatorToken.kind;
            const auto operation = kind == TokenKind::star    ? BinaryOperator::multiply
                                   : kind == TokenKind::slash ? BinaryOperator::divide
                                                              : BinaryOperator::remainder;
            auto right = parseUnary();
            if (right == nullptr) {
                return nullptr;
            }
            expression =
                binary(operation, operatorToken.span, std::move(expression), std::move(right));
        }
        return expression;
    }

    [[nodiscard]] auto parseUnary() -> ExpressionPtr {
        if (at(TokenKind::plus) || at(TokenKind::minus)) {
            const auto& operatorToken = advance();
            auto operand = parseUnary();
            if (operand == nullptr) {
                return nullptr;
            }
            const auto operation =
                operatorToken.kind == TokenKind::plus ? UnaryOperator::plus : UnaryOperator::minus;
            return std::make_unique<Expression>(Expression{
                .span = joinSpans(operatorToken.span, operand->span),
                .node = UnaryExpression{.operation = operation,
                                        .operatorSpan = operatorToken.span,
                                        .operand = std::move(operand)},
            });
        }
        return parseCall();
    }

    [[nodiscard]] auto parseCall() -> ExpressionPtr {
        auto expression = parsePrimary();
        while (expression != nullptr && at(TokenKind::leftParen)) {
            advance();
            std::vector<ExpressionPtr> arguments;
            if (!at(TokenKind::rightParen)) {
                while (true) {
                    auto argument = parseExpression();
                    if (argument == nullptr) {
                        return nullptr;
                    }
                    arguments.push_back(std::move(argument));
                    if (!at(TokenKind::comma)) {
                        break;
                    }
                    advance();
                }
            }
            const auto* close = consume(TokenKind::rightParen, "expected ')' after call arguments");
            if (close == nullptr) {
                return nullptr;
            }
            expression = std::make_unique<Expression>(Expression{
                .span = joinSpans(expression->span, close->span),
                .node = CallExpression{.callee = std::move(expression),
                                       .arguments = std::move(arguments)},
            });
        }
        return expression;
    }

    [[nodiscard]] auto parsePrimary() -> ExpressionPtr {
        const auto& token = current();
        if (token.kind == TokenKind::identifier) {
            advance();
            return std::make_unique<Expression>(Expression{
                .span = token.span,
                .node = IdentifierExpression{.nameSpan = token.span},
            });
        }
        if (token.kind == TokenKind::integerLiteral || token.kind == TokenKind::float64Literal ||
            token.kind == TokenKind::keywordTrue || token.kind == TokenKind::keywordFalse) {
            advance();
            return std::make_unique<Expression>(Expression{
                .span = token.span,
                .node = LiteralExpression{.kind = token.kind, .literalSpan = token.span},
            });
        }
        if (at(TokenKind::leftParen)) {
            const auto& open = advance();
            auto expression = parseExpression();
            const auto* close = consume(TokenKind::rightParen, "expected ')' after expression");
            if (expression == nullptr || close == nullptr) {
                return nullptr;
            }
            return std::make_unique<Expression>(Expression{
                .span = joinSpans(open.span, close->span),
                .node = ParenthesizedExpression{.expression = std::move(expression)},
            });
        }
        fail("E2001", "expected expression");
        return nullptr;
    }

    [[nodiscard]] static auto binary(BinaryOperator operation,
                                     support::SourceSpan operatorSpan,
                                     ExpressionPtr left,
                                     ExpressionPtr right) -> ExpressionPtr {
        const auto span = joinSpans(left->span, right->span);
        return std::make_unique<Expression>(Expression{
            .span = span,
            .node = BinaryExpression{.operation = operation,
                                     .operatorSpan = operatorSpan,
                                     .left = std::move(left),
                                     .right = std::move(right)},
        });
    }

    const support::SourceText& source_;
    std::span<const Token> tokens_;
    std::size_t index_{};
    std::vector<support::Diagnostic> diagnostics_;
};

} // namespace

auto Parser::parse(const support::SourceText& source,
                   std::span<const Token> tokens) const -> ParseResult {
    return ParserState{source, tokens}.parse();
}

} // namespace ember::frontend
