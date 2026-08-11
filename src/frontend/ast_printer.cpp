#include "ember/frontend/ast_printer.hpp"

#include <sstream>
#include <string_view>
#include <variant>

namespace ember::frontend {
namespace {

[[nodiscard]] auto typeName(TypeName type) noexcept -> std::string_view {
    switch (type) {
    case TypeName::i64:
        return "i64";
    case TypeName::f64:
        return "f64";
    case TypeName::boolean:
        return "bool";
    case TypeName::voidType:
        return "void";
    }
    return "<invalid-type>";
}

[[nodiscard]] auto unaryName(UnaryOperator operation) noexcept -> std::string_view {
    return operation == UnaryOperator::plus ? "+" : "-";
}

[[nodiscard]] auto binaryName(BinaryOperator operation) noexcept -> std::string_view {
    switch (operation) {
    case BinaryOperator::add:
        return "+";
    case BinaryOperator::subtract:
        return "-";
    case BinaryOperator::multiply:
        return "*";
    case BinaryOperator::divide:
        return "/";
    case BinaryOperator::remainder:
        return "%";
    case BinaryOperator::equal:
        return "==";
    case BinaryOperator::notEqual:
        return "!=";
    case BinaryOperator::less:
        return "<";
    case BinaryOperator::lessEqual:
        return "<=";
    case BinaryOperator::greater:
        return ">";
    case BinaryOperator::greaterEqual:
        return ">=";
    }
    return "<invalid-operator>";
}

class Printer {
public:
    explicit Printer(const support::SourceText& source)
        : source_(source) {
    }

    [[nodiscard]] auto print(const Program& program) -> std::string {
        line("Program", program.span);
        ++indent_;
        for (const auto& function : program.functions) {
            printFunction(function);
        }
        return output_.str();
    }

private:
    void line(std::string_view name, support::SourceSpan span) {
        output_ << std::string(indent_ * 2, ' ') << name << " [" << span.begin << ", " << span.end
                << ")\n";
    }

    [[nodiscard]] auto text(support::SourceSpan span) const -> std::string_view {
        return source_.slice(span).value_or("<invalid-span>");
    }

    void printFunction(const FunctionDeclaration& function) {
        output_ << std::string(indent_ * 2, ' ') << "Function " << text(function.nameSpan) << " -> "
                << typeName(function.returnType) << " [" << function.span.begin << ", "
                << function.span.end << ")\n";
        ++indent_;
        for (const auto& parameter : function.parameters) {
            output_ << std::string(indent_ * 2, ' ') << "Parameter " << text(parameter.nameSpan)
                    << ": " << typeName(parameter.type) << " [" << parameter.span.begin << ", "
                    << parameter.span.end << ")\n";
        }
        printBlock(function.body);
        --indent_;
    }

    void printBlock(const Block& block) {
        line("Block", block.span);
        ++indent_;
        for (const auto& statement : block.statements) {
            printStatement(statement);
        }
        --indent_;
    }

    void printStatement(const Statement& statement) {
        std::visit(
            [this, &statement](const auto& node) { printStatementNode(node, statement.span); },
            statement.node);
    }

    void printStatementNode(const LetStatement& node, support::SourceSpan span) {
        output_ << std::string(indent_ * 2, ' ') << "Let " << text(node.nameSpan) << ": "
                << typeName(node.type) << " [" << span.begin << ", " << span.end << ")\n";
        ++indent_;
        printExpression(*node.initializer);
        --indent_;
    }

    void printStatementNode(const AssignmentStatement& node, support::SourceSpan span) {
        output_ << std::string(indent_ * 2, ' ') << "Assignment " << text(node.targetSpan) << " ["
                << span.begin << ", " << span.end << ")\n";
        ++indent_;
        printExpression(*node.value);
        --indent_;
    }

    void printStatementNode(const ReturnStatement& node, support::SourceSpan span) {
        line("Return", span);
        if (node.value != nullptr) {
            ++indent_;
            printExpression(*node.value);
            --indent_;
        }
    }

    void printStatementNode(const IfStatement& node, support::SourceSpan span) {
        line("If", span);
        ++indent_;
        line("Condition", node.condition->span);
        ++indent_;
        printExpression(*node.condition);
        --indent_;
        printBlock(*node.thenBlock);
        if (node.elseBranch != nullptr) {
            line("Else", node.elseBranch->span);
            ++indent_;
            printStatement(*node.elseBranch);
            --indent_;
        }
        --indent_;
    }

    void printStatementNode(const WhileStatement& node, support::SourceSpan span) {
        line("While", span);
        ++indent_;
        line("Condition", node.condition->span);
        ++indent_;
        printExpression(*node.condition);
        --indent_;
        printBlock(*node.body);
        --indent_;
    }

    void printStatementNode(const ExpressionStatement& node, support::SourceSpan span) {
        line("ExpressionStatement", span);
        ++indent_;
        printExpression(*node.expression);
        --indent_;
    }

    void printStatementNode(const BlockPtr& node, support::SourceSpan) {
        printBlock(*node);
    }

    void printExpression(const Expression& expression) {
        std::visit(
            [this, &expression](const auto& node) { printExpressionNode(node, expression.span); },
            expression.node);
    }

    void printExpressionNode(const IdentifierExpression& node, support::SourceSpan span) {
        output_ << std::string(indent_ * 2, ' ') << "Identifier " << text(node.nameSpan) << " ["
                << span.begin << ", " << span.end << ")\n";
    }

    void printExpressionNode(const LiteralExpression& node, support::SourceSpan span) {
        output_ << std::string(indent_ * 2, ' ') << "Literal " << text(node.literalSpan) << " ["
                << span.begin << ", " << span.end << ")\n";
    }

    void printExpressionNode(const UnaryExpression& node, support::SourceSpan span) {
        output_ << std::string(indent_ * 2, ' ') << "Unary " << unaryName(node.operation) << " ["
                << span.begin << ", " << span.end << ")\n";
        ++indent_;
        printExpression(*node.operand);
        --indent_;
    }

    void printExpressionNode(const BinaryExpression& node, support::SourceSpan span) {
        output_ << std::string(indent_ * 2, ' ') << "Binary " << binaryName(node.operation) << " ["
                << span.begin << ", " << span.end << ")\n";
        ++indent_;
        printExpression(*node.left);
        printExpression(*node.right);
        --indent_;
    }

    void printExpressionNode(const CallExpression& node, support::SourceSpan span) {
        line("Call", span);
        ++indent_;
        printExpression(*node.callee);
        for (const auto& argument : node.arguments) {
            printExpression(*argument);
        }
        --indent_;
    }

    void printExpressionNode(const ParenthesizedExpression& node, support::SourceSpan span) {
        line("Parenthesized", span);
        ++indent_;
        printExpression(*node.expression);
        --indent_;
    }

    const support::SourceText& source_;
    std::ostringstream output_;
    std::size_t indent_{};
};

} // namespace

auto AstPrinter::print(const Program& program,
                       const support::SourceText& source) const -> std::string {
    return Printer{source}.print(program);
}

} // namespace ember::frontend
