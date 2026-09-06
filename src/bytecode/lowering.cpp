#include "ember/bytecode/lowering.hpp"

#include "ember/semantic/typed_ast.hpp"

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <utility>
#include <variant>

namespace ember::bytecode {
namespace {

[[nodiscard]] auto binaryOpcode(frontend::BinaryOperator op, core::Type type) -> Opcode {
    using enum frontend::BinaryOperator;
    const bool f = type == core::Type::f64;
    switch (op) {
    case add:
        return f ? Opcode::addF64 : Opcode::addI64;
    case subtract:
        return f ? Opcode::subF64 : Opcode::subI64;
    case multiply:
        return f ? Opcode::mulF64 : Opcode::mulI64;
    case divide:
        return f ? Opcode::divF64 : Opcode::divI64;
    case remainder:
        return Opcode::remI64;
    case equal:
        return f ? Opcode::equalF64
                 : (type == core::Type::boolean ? Opcode::equalBool : Opcode::equalI64);
    case notEqual:
        return f ? Opcode::notEqualF64
                 : (type == core::Type::boolean ? Opcode::notEqualBool : Opcode::notEqualI64);
    case less:
        return f ? Opcode::lessF64 : Opcode::lessI64;
    case lessEqual:
        return f ? Opcode::lessEqualF64 : Opcode::lessEqualI64;
    case greater:
        return f ? Opcode::greaterF64 : Opcode::greaterI64;
    case greaterEqual:
        return f ? Opcode::greaterEqualF64 : Opcode::greaterEqualI64;
    }
    std::unreachable();
}

class FunctionCompiler {
public:
    explicit FunctionCompiler(const semantic::TypedFunctionDeclaration& declaration)
        : function_{.id = declaration.id,
                    .kind = core::FunctionKind::user,
                    .signature = declaration.signature,
                    .localCount = 0,
                    .localTypes = {},
                    .code = {}} {
        for (const auto& parameter : declaration.parameters)
            (void)slot(parameter.symbol, parameter.type);
    }

    [[nodiscard]] Function compile(const semantic::TypedBlock& body) {
        block(body);
        // The final terminator gives every compiler-emitted jump a concrete target. For a
        // non-void function it is unreachable by the semantic analyzer's return-path proof.
        emit(function_.signature.returnType == core::Type::voidType ? Opcode::returnVoid
                                                                    : Opcode::returnValue);
        function_.localCount = static_cast<std::uint32_t>(slots_.size());
        return std::move(function_);
    }

private:
    void
    emit(Opcode op, std::uint32_t operand = 0, std::optional<core::Value> value = std::nullopt) {
        function_.code.push_back({op, operand, std::move(value)});
    }

    [[nodiscard]] std::uint32_t slot(semantic::SymbolId symbol,
                                     core::Type type = core::Type::voidType) {
        const auto [it, inserted] =
            slots_.try_emplace(symbol, static_cast<std::uint32_t>(slots_.size()));
        if (inserted)
            function_.localTypes.push_back(type);
        return it->second;
    }

    void expression(const semantic::TypedExpression& expression) {
        std::visit([this, &expression](const auto& node) { expressionNode(node, expression.type); },
                   expression.node);
    }

    void expressionNode(const semantic::TypedIdentifierExpression& node, core::Type) {
        emit(Opcode::load, slot(node.symbol));
    }

    void expressionNode(const semantic::TypedLiteralExpression& node, core::Type) {
        emit(Opcode::constant, 0, node.value);
    }

    void expressionNode(const semantic::TypedParenthesizedExpression& node, core::Type) {
        expression(*node.expression);
    }

    void expressionNode(const semantic::TypedUnaryExpression& node, core::Type type) {
        expression(*node.operand);
        switch (node.operation) {
        case frontend::UnaryOperator::plus:
            break;
        case frontend::UnaryOperator::minus:
            emit(type == core::Type::f64 ? Opcode::negateF64 : Opcode::negateI64);
            break;
        }
    }

    void expressionNode(const semantic::TypedBinaryExpression& node, core::Type) {
        expression(*node.left);
        expression(*node.right);
        emit(binaryOpcode(node.operation, node.left->type));
    }

    void expressionNode(const semantic::TypedCallExpression& node, core::Type) {
        for (const auto& argument : node.arguments)
            expression(*argument);
        emit(Opcode::call, node.callee);
    }

    void statement(const semantic::TypedStatement& statement) {
        std::visit([this](const auto& node) { statementNode(node); }, statement.node);
    }

    void statementNode(const semantic::TypedLetStatement& node) {
        expression(*node.initializer);
        emit(Opcode::store, slot(node.symbol, node.type));
    }

    void statementNode(const semantic::TypedAssignmentStatement& node) {
        expression(*node.value);
        emit(Opcode::store, slot(node.target, node.targetType));
    }

    void statementNode(const semantic::TypedReturnStatement& node) {
        if (node.value) {
            expression(*node.value);
            emit(Opcode::returnValue);
        } else {
            emit(Opcode::returnVoid);
        }
    }

    void statementNode(const semantic::TypedExpressionStatement& node) {
        expression(*node.expression);
        if (node.expression->type != core::Type::voidType)
            emit(Opcode::pop);
    }

    void statementNode(const semantic::TypedBlockPtr& node) {
        block(*node);
    }

    void statementNode(const semantic::TypedIfStatement& node) {
        expression(*node.condition);
        const auto falseJump = static_cast<std::uint32_t>(function_.code.size());
        emit(Opcode::jumpIfFalse);
        block(*node.thenBlock);
        const auto endJump = static_cast<std::uint32_t>(function_.code.size());
        emit(Opcode::jump);
        function_.code[falseJump].operand = static_cast<std::uint32_t>(function_.code.size());
        if (node.elseBranch)
            statement(*node.elseBranch);
        function_.code[endJump].operand = static_cast<std::uint32_t>(function_.code.size());
    }

    void statementNode(const semantic::TypedWhileStatement& node) {
        const auto start = static_cast<std::uint32_t>(function_.code.size());
        expression(*node.condition);
        const auto exit = static_cast<std::uint32_t>(function_.code.size());
        emit(Opcode::jumpIfFalse);
        block(*node.body);
        emit(Opcode::jump, start);
        function_.code[exit].operand = static_cast<std::uint32_t>(function_.code.size());
    }

    void block(const semantic::TypedBlock& body) {
        for (const auto& statement : body.statements)
            this->statement(statement);
    }

    Function function_;
    std::unordered_map<semantic::SymbolId, std::uint32_t> slots_;
};

} // namespace

CompileResult Compiler::compile(const semantic::TypedProgram& program) const {
    Program result;
    result.functions.reserve(program.functions.size());
    for (const auto& function : program.functions)
        if (function.kind == core::FunctionKind::host)
            result.functions.push_back({.id = function.id,
                                        .kind = function.kind,
                                        .signature = function.signature,
                                        .localCount = 0,
                                        .localTypes = {},
                                        .code = {}});
    for (const auto& declaration : program.declarations)
        result.functions.push_back(FunctionCompiler{declaration}.compile(declaration.body));
    return {.program = std::move(result), .diagnostics = {}};
}

} // namespace ember::bytecode
