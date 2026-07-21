#include "ember/semantic/typed_ast_printer.hpp"

#include <sstream>
#include <variant>

namespace ember::semantic
{
namespace
{
class Printer
{
  public:
    explicit Printer(const support::SourceText &source) : source_(source)
    {
    }
    [[nodiscard]] auto print(const TypedProgram &program) -> std::string
    {
        line("TypedProgram", program.span);
        ++indent_;
        for (const auto &function : program.declarations)
            functionLine(function);
        return output_.str();
    }

  private:
    void line(std::string_view name, support::SourceSpan span)
    {
        output_ << std::string(indent_ * 2, ' ') << name << " [" << span.begin << ", " << span.end
                << ")\n";
    }
    [[nodiscard]] auto text(support::SourceSpan span) const -> std::string_view
    {
        return source_.slice(span).value_or("<invalid-span>");
    }
    void functionLine(const TypedFunctionDeclaration &function)
    {
        output_ << std::string(indent_ * 2, ' ') << "Function " << function.name << " #"
                << function.id << " -> " << typeName(function.signature.returnType) << " ["
                << function.span.begin << ", " << function.span.end << ")\n";
        ++indent_;
        for (const auto &parameter : function.parameters)
            output_ << std::string(indent_ * 2, ' ') << "Parameter " << text(parameter.nameSpan)
                    << " #" << parameter.symbol << ": " << typeName(parameter.type) << " ["
                    << parameter.span.begin << ", " << parameter.span.end << ")\n";
        block(function.body);
        --indent_;
    }
    void block(const TypedBlock &value)
    {
        line("Block", value.span);
        ++indent_;
        for (const auto &statement : value.statements)
            statementLine(statement);
        --indent_;
    }
    void statementLine(const TypedStatement &statement)
    {
        std::visit([this, &statement](const auto &node) { statementNode(node, statement.span); },
                   statement.node);
    }
    void statementNode(const TypedLetStatement &node, support::SourceSpan span)
    {
        output_ << std::string(indent_ * 2, ' ') << "Let " << text(node.nameSpan) << " #"
                << node.symbol << ": " << typeName(node.type) << " [" << span.begin << ", "
                << span.end << ")\n";
        ++indent_;
        expression(*node.initializer);
        --indent_;
    }
    void statementNode(const TypedAssignmentStatement &node, support::SourceSpan span)
    {
        output_ << std::string(indent_ * 2, ' ') << "Assignment " << text(node.targetSpan) << " #"
                << node.target << ": " << typeName(node.targetType) << " [" << span.begin << ", "
                << span.end << ")\n";
        ++indent_;
        expression(*node.value);
        --indent_;
    }
    void statementNode(const TypedReturnStatement &node, support::SourceSpan span)
    {
        line("Return", span);
        if (node.value != nullptr)
        {
            ++indent_;
            expression(*node.value);
            --indent_;
        }
    }
    void statementNode(const TypedIfStatement &node, support::SourceSpan span)
    {
        line("If", span);
        ++indent_;
        line("Condition", node.condition->span);
        ++indent_;
        expression(*node.condition);
        --indent_;
        line("Then", node.thenBlock->span);
        ++indent_;
        block(*node.thenBlock);
        --indent_;
        if (node.elseBranch != nullptr)
        {
            line("Else", node.elseBranch->span);
            ++indent_;
            statementLine(*node.elseBranch);
            --indent_;
        }
        --indent_;
    }
    void statementNode(const TypedWhileStatement &node, support::SourceSpan span)
    {
        line("While", span);
        ++indent_;
        expression(*node.condition);
        block(*node.body);
        --indent_;
    }
    void statementNode(const TypedExpressionStatement &node, support::SourceSpan span)
    {
        line("ExpressionStatement", span);
        ++indent_;
        expression(*node.expression);
        --indent_;
    }
    void statementNode(const TypedBlockPtr &node, support::SourceSpan)
    {
        block(*node);
    }
    void expression(const TypedExpression &value)
    {
        std::visit([this, &value](const auto &node) { expressionNode(node, value); }, value.node);
    }
    void expressionNode(const TypedIdentifierExpression &node, const TypedExpression &value)
    {
        output_ << std::string(indent_ * 2, ' ') << "Identifier " << text(node.nameSpan) << " #"
                << node.symbol << ": " << typeName(value.type) << " [" << value.span.begin << ", "
                << value.span.end << ")\n";
    }
    void expressionNode(const TypedLiteralExpression &node, const TypedExpression &value)
    {
        output_ << std::string(indent_ * 2, ' ') << "Literal " << text(node.literalSpan) << ": "
                << typeName(value.type) << " [" << value.span.begin << ", " << value.span.end
                << ")\n";
    }
    void expressionNode(const TypedUnaryExpression &node, const TypedExpression &value)
    {
        output_ << std::string(indent_ * 2, ' ') << "Unary "
                << (node.operation == frontend::UnaryOperator::minus ? "minus" : "plus") << ": "
                << typeName(value.type) << " [" << value.span.begin << ", " << value.span.end
                << ")\n";
        ++indent_;
        expression(*node.operand);
        --indent_;
    }
    void expressionNode(const TypedBinaryExpression &node, const TypedExpression &value)
    {
        output_ << std::string(indent_ * 2, ' ') << "Binary "
                << frontend::tokenKindName(binaryToken(node.operation)) << ": "
                << typeName(value.type) << " [" << value.span.begin << ", " << value.span.end
                << ")\n";
        ++indent_;
        expression(*node.left);
        expression(*node.right);
        --indent_;
    }
    void expressionNode(const TypedCallExpression &node, const TypedExpression &value)
    {
        output_ << std::string(indent_ * 2, ' ') << "Call #" << node.callee << ": "
                << typeName(value.type) << " [" << value.span.begin << ", " << value.span.end
                << ")\n";
        ++indent_;
        for (const auto &argument : node.arguments)
            expression(*argument);
        --indent_;
    }
    void expressionNode(const TypedParenthesizedExpression &node, const TypedExpression &value)
    {
        output_ << std::string(indent_ * 2, ' ') << "Parenthesized: " << typeName(value.type)
                << " [" << value.span.begin << ", " << value.span.end << ")\n";
        ++indent_;
        expression(*node.expression);
        --indent_;
    }
    [[nodiscard]] static auto binaryToken(frontend::BinaryOperator operation) noexcept
        -> frontend::TokenKind
    {
        switch (operation)
        {
        case frontend::BinaryOperator::add:
            return frontend::TokenKind::plus;
        case frontend::BinaryOperator::subtract:
            return frontend::TokenKind::minus;
        case frontend::BinaryOperator::multiply:
            return frontend::TokenKind::star;
        case frontend::BinaryOperator::divide:
            return frontend::TokenKind::slash;
        case frontend::BinaryOperator::remainder:
            return frontend::TokenKind::percent;
        case frontend::BinaryOperator::equal:
            return frontend::TokenKind::equalEqual;
        case frontend::BinaryOperator::notEqual:
            return frontend::TokenKind::bangEqual;
        case frontend::BinaryOperator::less:
            return frontend::TokenKind::less;
        case frontend::BinaryOperator::lessEqual:
            return frontend::TokenKind::lessEqual;
        case frontend::BinaryOperator::greater:
            return frontend::TokenKind::greater;
        case frontend::BinaryOperator::greaterEqual:
            return frontend::TokenKind::greaterEqual;
        }
        return frontend::TokenKind::endOfFile;
    }
    const support::SourceText &source_;
    std::ostringstream output_;
    std::size_t indent_{};
};
} // namespace
auto TypedAstPrinter::print(const TypedProgram &program, const support::SourceText &source) const
    -> std::string
{
    return Printer{source}.print(program);
}
} // namespace ember::semantic
