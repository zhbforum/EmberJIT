#include "ember/semantic/analyzer.hpp"

#include <charconv>
#include <cstdint>
#include <limits>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace ember::semantic
{
namespace
{

[[nodiscard]] auto fromAst(frontend::TypeName type) noexcept -> Type
{
    switch (type)
    {
    case frontend::TypeName::i64:
        return Type::i64;
    case frontend::TypeName::f64:
        return Type::f64;
    case frontend::TypeName::boolean:
        return Type::boolean;
    case frontend::TypeName::voidType:
        return Type::voidType;
    }
    return Type::voidType;
}

[[nodiscard]] auto isNumeric(Type type) noexcept -> bool
{
    return type == Type::i64 || type == Type::f64;
}

class Analyzer
{
  public:
    Analyzer(const support::SourceText &source, const HostFunctionRegistry &hosts)
        : source_(source), hosts_(hosts)
    {
    }

    [[nodiscard]] auto run(const frontend::Program &program) -> AnalysisResult
    {
        if (!registerHostFunctions(program.span))
        {
            return finish();
        }
        if (!registerUserFunctions(program))
        {
            return finish();
        }

        auto typed = std::make_unique<TypedProgram>();
        typed->span = program.span;
        typed->functions = resolvedFunctions_;
        for (const auto &function : program.functions)
        {
            auto typedFunction = analyzeFunction(function);
            if (!typedFunction.has_value())
            {
                return finish();
            }
            typed->declarations.push_back(std::move(*typedFunction));
        }
        return AnalysisResult{.program = std::move(typed), .diagnostics = {}};
    }

  private:
    [[nodiscard]] auto registerHostFunctions(support::SourceSpan programSpan) -> bool
    {
        for (const auto &host : hosts_.functions())
        {
            if (!validSignature(host.signature, programSpan, "host function"))
            {
                return false;
            }
            const auto function = ResolvedFunction{.id = nextFunctionId_++,
                                                   .kind = FunctionKind::host,
                                                   .name = host.name,
                                                   .signature = host.signature};
            if (!functions_.emplace(host.name, function).second)
            {
                fail(programSpan, "E3001", "duplicate function name");
                return false;
            }
            resolvedFunctions_.push_back(function);
        }
        return true;
    }

    [[nodiscard]] auto registerUserFunctions(const frontend::Program &program) -> bool
    {
        for (const auto &function : program.functions)
        {
            FunctionSignature signature;
            signature.returnType = fromAst(function.returnType);
            for (const auto &parameter : function.parameters)
            {
                signature.parameterTypes.push_back(fromAst(parameter.type));
            }
            if (!validSignature(signature, function.nameSpan, "function"))
            {
                return false;
            }
            const auto name = text(function.nameSpan);
            const auto functionSymbol = ResolvedFunction{.id = nextFunctionId_++,
                                                         .kind = FunctionKind::user,
                                                         .name = name,
                                                         .signature = std::move(signature)};
            if (!functions_.emplace(name, functionSymbol).second)
            {
                fail(function.nameSpan, "E3001", "duplicate function name '" + name + "'");
                return false;
            }
            resolvedFunctions_.push_back(functionSymbol);
        }
        return true;
    }
    [[nodiscard]] auto finish() -> AnalysisResult
    {
        return {.program = nullptr, .diagnostics = std::move(diagnostics_)};
    }
    [[nodiscard]] auto text(support::SourceSpan span) const -> std::string
    {
        return std::string{source_.slice(span).value_or("<invalid-name>")};
    }
    void fail(support::SourceSpan span, std::string code, std::string message)
    {
        if (diagnostics_.empty())
        {
            diagnostics_.push_back({.stage = support::DiagnosticStage::semantic,
                                    .severity = support::DiagnosticSeverity::error,
                                    .code = std::move(code),
                                    .message = std::move(message),
                                    .primarySpan = span});
        }
    }
    [[nodiscard]] auto validSignature(const FunctionSignature &signature, support::SourceSpan span,
                                      std::string_view subject) -> bool
    {
        for (const auto type : signature.parameterTypes)
        {
            if (type == Type::voidType)
            {
                fail(span, "E3002", std::string{subject} + " parameter cannot have type void");
                return false;
            }
        }
        return true;
    }
    struct Symbol
    {
        SymbolId id;
        Type type;
    };
    struct MinimumI64FoldResult
    {
        bool handled{};
        TypedExpressionPtr expression;
    };
    class ScopeGuard
    {
      public:
        explicit ScopeGuard(Analyzer &analyzer) : analyzer_(analyzer)
        {
            analyzer_.scopes_.emplace_back();
        }

        ~ScopeGuard()
        {
            analyzer_.scopes_.pop_back();
        }

        ScopeGuard(const ScopeGuard &) = delete;
        auto operator=(const ScopeGuard &) -> ScopeGuard & = delete;

      private:
        Analyzer &analyzer_;
    };
    [[nodiscard]] auto declare(support::SourceSpan span, std::string name, Type type)
        -> std::optional<Symbol>
    {
        auto &scope = scopes_.back();
        const auto symbol = Symbol{.id = nextSymbolId_++, .type = type};
        if (!scope.emplace(std::move(name), symbol).second)
        {
            fail(span, "E3003", "duplicate name in this scope");
            return std::nullopt;
        }
        return symbol;
    }
    [[nodiscard]] auto lookup(const std::string &name) const -> std::optional<Symbol>
    {
        for (auto scope = scopes_.rbegin(); scope != scopes_.rend(); ++scope)
        {
            if (const auto found = scope->find(name); found != scope->end())
            {
                return found->second;
            }
        }
        return std::nullopt;
    }
    [[nodiscard]] auto analyzeFunction(const frontend::FunctionDeclaration &function)
        -> std::optional<TypedFunctionDeclaration>
    {
        currentReturnType_ = fromAst(function.returnType);
        scopes_.clear();
        scopes_.emplace_back();
        std::vector<TypedParameter> parameters;
        for (const auto &parameter : function.parameters)
        {
            const auto type = fromAst(parameter.type);
            const auto symbol = declare(parameter.nameSpan, text(parameter.nameSpan), type);
            if (!symbol.has_value())
            {
                return std::nullopt;
            }
            parameters.push_back({.span = parameter.span,
                                  .nameSpan = parameter.nameSpan,
                                  .symbol = symbol->id,
                                  .type = type});
        }
        auto body = analyzeBlockContents(function.body);
        if (body == nullptr)
        {
            return std::nullopt;
        }
        if (currentReturnType_ != Type::voidType && !alwaysReturns(*body))
        {
            fail(function.nameSpan, "E3012", "non-void function does not return on every path");
            return std::nullopt;
        }
        const auto name = text(function.nameSpan);
        const auto &resolved = functions_.at(name);
        return TypedFunctionDeclaration{.span = function.span,
                                        .nameSpan = function.nameSpan,
                                        .id = resolved.id,
                                        .name = name,
                                        .signature = resolved.signature,
                                        .parameters = std::move(parameters),
                                        .body = std::move(*body)};
    }
    [[nodiscard]] auto analyzeNestedBlock(const frontend::Block &block) -> TypedBlockPtr
    {
        const ScopeGuard scope{*this};
        return analyzeBlockContents(block);
    }

    [[nodiscard]] auto analyzeBlockContents(const frontend::Block &block) -> TypedBlockPtr
    {
        TypedBlock result{.span = block.span, .statements = {}};
        for (const auto &statement : block.statements)
        {
            auto typed = analyzeStatement(statement);
            if (!typed.has_value())
            {
                return nullptr;
            }
            result.statements.push_back(std::move(*typed));
        }
        return std::make_unique<TypedBlock>(std::move(result));
    }
    [[nodiscard]] auto analyzeStatement(const frontend::Statement &statement)
        -> std::optional<TypedStatement>
    {
        return std::visit(
            [this, &statement](const auto &node) {
                return analyzeStatementNode(node, statement.span);
            },
            statement.node);
    }
    [[nodiscard]] auto analyzeStatementNode(const frontend::LetStatement &node,
                                            support::SourceSpan span)
        -> std::optional<TypedStatement>
    {
        const auto type = fromAst(node.type);
        if (type == Type::voidType)
        {
            fail(node.typeSpan, "E3002", "local variable cannot have type void");
            return std::nullopt;
        }
        auto initializer = analyzeExpression(*node.initializer);
        if (initializer == nullptr)
        {
            return std::nullopt;
        }
        if (initializer->type != type)
        {
            fail(node.initializer->span, "E3005",
                 "initializer type does not match declaration type");
            return std::nullopt;
        }
        const auto symbol = declare(node.nameSpan, text(node.nameSpan), type);
        if (!symbol.has_value())
        {
            return std::nullopt;
        }
        return TypedStatement{.span = span,
                              .node = TypedLetStatement{.nameSpan = node.nameSpan,
                                                        .symbol = symbol->id,
                                                        .type = type,
                                                        .initializer = std::move(initializer)}};
    }
    [[nodiscard]] auto analyzeStatementNode(const frontend::AssignmentStatement &node,
                                            support::SourceSpan span)
        -> std::optional<TypedStatement>
    {
        const auto name = text(node.targetSpan);
        const auto target = lookup(name);
        if (!target.has_value())
        {
            fail(node.targetSpan, "E3004", "unknown name '" + name + "'");
            return std::nullopt;
        }
        auto value = analyzeExpression(*node.value);
        if (value == nullptr)
        {
            return std::nullopt;
        }
        if (value->type != target->type)
        {
            fail(value->span, "E3005", "assignment type does not match target type");
            return std::nullopt;
        }
        return TypedStatement{.span = span,
                              .node = TypedAssignmentStatement{.targetSpan = node.targetSpan,
                                                               .target = target->id,
                                                               .targetType = target->type,
                                                               .value = std::move(value)}};
    }
    [[nodiscard]] auto analyzeStatementNode(const frontend::ReturnStatement &node,
                                            support::SourceSpan span)
        -> std::optional<TypedStatement>
    {
        if (node.value == nullptr)
        {
            if (currentReturnType_ != Type::voidType)
            {
                fail(span, "E3006", "non-void function must return a value");
                return std::nullopt;
            }
            return TypedStatement{.span = span, .node = TypedReturnStatement{}};
        }
        auto value = analyzeExpression(*node.value);
        if (value == nullptr)
        {
            return std::nullopt;
        }
        if (currentReturnType_ == Type::voidType || value->type != currentReturnType_)
        {
            fail(value->span, "E3006", "return value does not match function return type");
            return std::nullopt;
        }
        return TypedStatement{.span = span,
                              .node = TypedReturnStatement{.value = std::move(value)}};
    }
    [[nodiscard]] auto analyzeStatementNode(const frontend::IfStatement &node,
                                            support::SourceSpan span)
        -> std::optional<TypedStatement>
    {
        auto condition = analyzeExpression(*node.condition);
        if (condition == nullptr)
        {
            return std::nullopt;
        }
        if (condition->type != Type::boolean)
        {
            fail(condition->span, "E3007", "if condition must have type bool");
            return std::nullopt;
        }
        auto thenBlock = analyzeNestedBlock(*node.thenBlock);
        if (thenBlock == nullptr)
        {
            return std::nullopt;
        }
        TypedStatementPtr elseBranch;
        if (node.elseBranch != nullptr)
        {
            auto branch = analyzeStatement(*node.elseBranch);
            if (!branch.has_value())
            {
                return std::nullopt;
            }
            elseBranch = std::make_unique<TypedStatement>(std::move(*branch));
        }
        return TypedStatement{.span = span,
                              .node = TypedIfStatement{.condition = std::move(condition),
                                                       .thenBlock = std::move(thenBlock),
                                                       .elseBranch = std::move(elseBranch)}};
    }
    [[nodiscard]] auto analyzeStatementNode(const frontend::WhileStatement &node,
                                            support::SourceSpan span)
        -> std::optional<TypedStatement>
    {
        auto condition = analyzeExpression(*node.condition);
        if (condition == nullptr)
        {
            return std::nullopt;
        }
        if (condition->type != Type::boolean)
        {
            fail(condition->span, "E3007", "while condition must have type bool");
            return std::nullopt;
        }
        auto body = analyzeNestedBlock(*node.body);
        if (body == nullptr)
        {
            return std::nullopt;
        }
        return TypedStatement{.span = span,
                              .node = TypedWhileStatement{.condition = std::move(condition),
                                                          .body = std::move(body)}};
    }
    [[nodiscard]] auto analyzeStatementNode(const frontend::ExpressionStatement &node,
                                            support::SourceSpan span)
        -> std::optional<TypedStatement>
    {
        auto expression = analyzeExpression(*node.expression);
        if (expression == nullptr)
        {
            return std::nullopt;
        }
        return TypedStatement{
            .span = span, .node = TypedExpressionStatement{.expression = std::move(expression)}};
    }
    [[nodiscard]] auto analyzeStatementNode(const frontend::BlockPtr &node,
                                            support::SourceSpan span)
        -> std::optional<TypedStatement>
    {
        auto block = analyzeNestedBlock(*node);
        if (block == nullptr)
        {
            return std::nullopt;
        }
        return TypedStatement{.span = span, .node = std::move(block)};
    }
    [[nodiscard]] auto analyzeExpression(const frontend::Expression &expression)
        -> TypedExpressionPtr
    {
        return std::visit(
            [this, &expression](const auto &node) {
                return analyzeExpressionNode(node, expression.span);
            },
            expression.node);
    }
    [[nodiscard]] auto makeExpression(support::SourceSpan span, Type type, auto node)
        -> TypedExpressionPtr
    {
        return std::make_unique<TypedExpression>(
            TypedExpression{.span = span, .type = type, .node = std::move(node)});
    }
    [[nodiscard]] auto analyzeExpressionNode(const frontend::IdentifierExpression &node,
                                             support::SourceSpan span) -> TypedExpressionPtr
    {
        const auto name = text(node.nameSpan);
        const auto symbol = lookup(name);
        if (!symbol.has_value())
        {
            fail(node.nameSpan, "E3004", "unknown name '" + name + "'");
            return nullptr;
        }
        return makeExpression(
            span, symbol->type,
            TypedIdentifierExpression{.nameSpan = node.nameSpan, .symbol = symbol->id});
    }
    [[nodiscard]] auto analyzeExpressionNode(const frontend::LiteralExpression &node,
                                             support::SourceSpan span) -> TypedExpressionPtr
    {
        if (node.kind == frontend::TokenKind::integerLiteral)
        {
            return parseIntegerLiteral(node.literalSpan, span);
        }
        if (node.kind == frontend::TokenKind::float64Literal)
        {
            return parseFloatLiteral(node.literalSpan, span);
        }
        return makeExpression(
            span, Type::boolean,
            TypedLiteralExpression{.literalSpan = node.literalSpan,
                                   .value = node.kind == frontend::TokenKind::keywordTrue});
    }
    [[nodiscard]] auto analyzeExpressionNode(const frontend::UnaryExpression &node,
                                             support::SourceSpan span) -> TypedExpressionPtr
    {
        auto foldedMinimum = tryFoldMinimumI64(node, span);
        if (foldedMinimum.handled)
        {
            return std::move(foldedMinimum.expression);
        }
        auto operand = analyzeExpression(*node.operand);
        if (operand == nullptr)
        {
            return nullptr;
        }
        if (!isNumeric(operand->type))
        {
            fail(node.operatorSpan, "E3008", "unary operator requires i64 or f64 operand");
            return nullptr;
        }
        const auto type = operand->type;
        return makeExpression(span, type,
                              TypedUnaryExpression{.operation = node.operation,
                                                   .operatorSpan = node.operatorSpan,
                                                   .operand = std::move(operand)});
    }
    [[nodiscard]] auto analyzeExpressionNode(const frontend::BinaryExpression &node,
                                             support::SourceSpan span) -> TypedExpressionPtr
    {
        auto left = analyzeExpression(*node.left);
        if (left == nullptr)
        {
            return nullptr;
        }
        auto right = analyzeExpression(*node.right);
        if (right == nullptr)
        {
            return nullptr;
        }
        const auto result =
            checkBinaryOperator(node.operation, left->type, right->type, node.operatorSpan);
        if (!result.has_value())
        {
            return nullptr;
        }
        return makeExpression(span, *result,
                              TypedBinaryExpression{.operation = node.operation,
                                                    .operatorSpan = node.operatorSpan,
                                                    .left = std::move(left),
                                                    .right = std::move(right)});
    }
    [[nodiscard]] auto analyzeExpressionNode(const frontend::CallExpression &node,
                                             support::SourceSpan span) -> TypedExpressionPtr
    {
        const auto *callee = std::get_if<frontend::IdentifierExpression>(&node.callee->node);
        if (callee == nullptr)
        {
            fail(node.callee->span, "E3009", "call target must be a function name");
            return nullptr;
        }
        const auto name = text(callee->nameSpan);
        const auto found = functions_.find(name);
        if (found == functions_.end())
        {
            fail(callee->nameSpan, "E3010", "unknown function '" + name + "'");
            return nullptr;
        }
        if (node.arguments.size() != found->second.signature.parameterTypes.size())
        {
            fail(span, "E3010", "call has incorrect argument count");
            return nullptr;
        }
        std::vector<TypedExpressionPtr> arguments;
        for (std::size_t index{}; index < node.arguments.size(); ++index)
        {
            auto argument = analyzeExpression(*node.arguments[index]);
            if (argument == nullptr)
            {
                return nullptr;
            }
            if (argument->type != found->second.signature.parameterTypes[index])
            {
                fail(argument->span, "E3010", "call argument type does not match parameter type");
                return nullptr;
            }
            arguments.push_back(std::move(argument));
        }
        return makeExpression(span, found->second.signature.returnType,
                              TypedCallExpression{.calleeSpan = callee->nameSpan,
                                                  .callee = found->second.id,
                                                  .arguments = std::move(arguments)});
    }
    [[nodiscard]] auto analyzeExpressionNode(const frontend::ParenthesizedExpression &node,
                                             support::SourceSpan span) -> TypedExpressionPtr
    {
        auto inner = analyzeExpression(*node.expression);
        if (inner == nullptr)
        {
            return nullptr;
        }
        const auto type = inner->type;
        return makeExpression(span, type,
                              TypedParenthesizedExpression{.expression = std::move(inner)});
    }

    [[nodiscard]] auto parseIntegerLiteral(support::SourceSpan literalSpan,
                                           support::SourceSpan expressionSpan) -> TypedExpressionPtr
    {
        const auto magnitude = integerLiteralMagnitude(literalSpan);
        constexpr auto maximum =
            static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
        if (!magnitude.has_value())
        {
            return nullptr;
        }
        if (*magnitude > maximum)
        {
            fail(literalSpan, "E3011", "integer literal is outside the i64 range");
            return nullptr;
        }
        return makeExpression(
            expressionSpan, Type::i64,
            TypedLiteralExpression{.literalSpan = literalSpan,
                                   .value = static_cast<std::int64_t>(*magnitude)});
    }

    [[nodiscard]] auto parseFloatLiteral(support::SourceSpan literalSpan,
                                         support::SourceSpan expressionSpan) -> TypedExpressionPtr
    {
        double value{};
        const auto literal = source_.slice(literalSpan).value_or("");
        const auto result = std::from_chars(literal.data(), literal.data() + literal.size(), value);
        if (result.ec != std::errc{} || result.ptr != literal.data() + literal.size())
        {
            fail(literalSpan, "E3011", "floating literal is outside the f64 range");
            return nullptr;
        }
        return makeExpression(expressionSpan, Type::f64,
                              TypedLiteralExpression{.literalSpan = literalSpan, .value = value});
    }

    [[nodiscard]] auto tryFoldMinimumI64(const frontend::UnaryExpression &node,
                                         support::SourceSpan expressionSpan) -> MinimumI64FoldResult
    {
        if (node.operation != frontend::UnaryOperator::minus)
        {
            return {};
        }
        const auto *literal = std::get_if<frontend::LiteralExpression>(&node.operand->node);
        if (literal == nullptr || literal->kind != frontend::TokenKind::integerLiteral)
        {
            return {};
        }

        constexpr auto magnitudeOfI64Min =
            static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) + 1U;
        const auto magnitude = integerLiteralMagnitude(literal->literalSpan);
        if (!magnitude.has_value())
        {
            return {.handled = true, .expression = nullptr};
        }
        if (*magnitude == magnitudeOfI64Min)
        {
            return {.handled = true,
                    .expression = makeExpression(
                        expressionSpan, Type::i64,
                        TypedLiteralExpression{.literalSpan = expressionSpan,
                                               .value = std::numeric_limits<std::int64_t>::min()})};
        }
        if (*magnitude > magnitudeOfI64Min)
        {
            fail(literal->literalSpan, "E3011", "integer literal is outside the i64 range");
            return {.handled = true, .expression = nullptr};
        }
        return {};
    }

    [[nodiscard]] auto checkBinaryOperator(frontend::BinaryOperator operation, Type left,
                                           Type right, support::SourceSpan operatorSpan)
        -> std::optional<Type>
    {
        if (operation == frontend::BinaryOperator::equal ||
            operation == frontend::BinaryOperator::notEqual)
        {
            if (left != right || left == Type::voidType)
            {
                fail(operatorSpan, "E3008", "equality operands must have the same non-void type");
                return std::nullopt;
            }
            return Type::boolean;
        }
        if (operation == frontend::BinaryOperator::less ||
            operation == frontend::BinaryOperator::lessEqual ||
            operation == frontend::BinaryOperator::greater ||
            operation == frontend::BinaryOperator::greaterEqual)
        {
            if (left != right || !isNumeric(left))
            {
                fail(operatorSpan, "E3008", "comparison operands must have the same numeric type");
                return std::nullopt;
            }
            return Type::boolean;
        }
        if (operation == frontend::BinaryOperator::remainder)
        {
            if (left != Type::i64 || right != Type::i64)
            {
                fail(operatorSpan, "E3008", "remainder operands must have type i64");
                return std::nullopt;
            }
            return Type::i64;
        }
        if (left != right || !isNumeric(left))
        {
            fail(operatorSpan, "E3008", "arithmetic operands must have the same numeric type");
            return std::nullopt;
        }
        return left;
    }

    [[nodiscard]] auto integerLiteralMagnitude(support::SourceSpan span)
        -> std::optional<std::uint64_t>
    {
        std::uint64_t value{};
        const auto literal = source_.slice(span).value_or("");
        const auto result = std::from_chars(literal.data(), literal.data() + literal.size(), value);
        if (result.ec == std::errc{} && result.ptr == literal.data() + literal.size())
        {
            return value;
        }
        fail(span, "E3011", "integer literal is outside the i64 range");
        return std::nullopt;
    }
    [[nodiscard]] static auto alwaysReturns(const TypedBlock &block) -> bool
    {
        for (const auto &statement : block.statements)
        {
            if (alwaysReturns(statement))
            {
                return true;
            }
        }
        return false;
    }
    [[nodiscard]] static auto alwaysReturns(const TypedStatement &statement) -> bool
    {
        if (std::holds_alternative<TypedReturnStatement>(statement.node))
        {
            return true;
        }
        if (const auto *branch = std::get_if<TypedIfStatement>(&statement.node))
        {
            return branch->elseBranch != nullptr && alwaysReturns(*branch->thenBlock) &&
                   alwaysReturns(*branch->elseBranch);
        }
        if (const auto *block = std::get_if<TypedBlockPtr>(&statement.node))
        {
            return alwaysReturns(**block);
        }
        return false;
    }

    const support::SourceText &source_;
    const HostFunctionRegistry &hosts_;
    std::unordered_map<std::string, ResolvedFunction> functions_;
    std::vector<ResolvedFunction> resolvedFunctions_;
    std::vector<std::unordered_map<std::string, Symbol>> scopes_;
    SymbolId nextSymbolId_{};
    FunctionId nextFunctionId_{};
    Type currentReturnType_{Type::voidType};
    std::vector<support::Diagnostic> diagnostics_;
};
} // namespace

auto HostFunctionRegistry::add(HostFunction function) -> bool
{
    for (const auto &existing : functions_)
    {
        if (existing.name == function.name)
        {
            return false;
        }
    }
    functions_.push_back(std::move(function));
    return true;
}
auto SemanticAnalyzer::analyze(const frontend::Program &program, const support::SourceText &source,
                               const HostFunctionRegistry &hosts) const -> AnalysisResult
{
    return Analyzer{source, hosts}.run(program);
}
} // namespace ember::semantic
