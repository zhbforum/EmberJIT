#pragma once

#include "ember/core/function.hpp"
#include "ember/core/value.hpp"
#include "ember/frontend/ast.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <variant>
#include <vector>

namespace ember::semantic {

using SymbolId = std::uint32_t;

struct TypedExpression;
struct TypedStatement;
struct TypedBlock;
using TypedExpressionPtr = std::unique_ptr<TypedExpression>;
using TypedStatementPtr = std::unique_ptr<TypedStatement>;
using TypedBlockPtr = std::unique_ptr<TypedBlock>;

struct TypedIdentifierExpression {
    support::SourceSpan nameSpan;
    SymbolId symbol;
};
struct TypedLiteralExpression {
    support::SourceSpan literalSpan;
    core::Value value;
};
struct TypedUnaryExpression {
    frontend::UnaryOperator operation;
    support::SourceSpan operatorSpan;
    TypedExpressionPtr operand;
};
struct TypedBinaryExpression {
    frontend::BinaryOperator operation;
    support::SourceSpan operatorSpan;
    TypedExpressionPtr left;
    TypedExpressionPtr right;
};
struct TypedCallExpression {
    support::SourceSpan calleeSpan;
    core::FunctionId callee;
    std::vector<TypedExpressionPtr> arguments;
};
struct TypedParenthesizedExpression {
    TypedExpressionPtr expression;
};

struct TypedExpression {
    support::SourceSpan span;
    core::Type type;
    std::variant<TypedIdentifierExpression,
                 TypedLiteralExpression,
                 TypedUnaryExpression,
                 TypedBinaryExpression,
                 TypedCallExpression,
                 TypedParenthesizedExpression>
        node;
};

struct TypedLetStatement {
    support::SourceSpan nameSpan;
    SymbolId symbol;
    core::Type type;
    TypedExpressionPtr initializer;
};
struct TypedAssignmentStatement {
    support::SourceSpan targetSpan;
    SymbolId target;
    core::Type targetType;
    TypedExpressionPtr value;
};
struct TypedReturnStatement {
    TypedExpressionPtr value;
};
struct TypedIfStatement {
    TypedExpressionPtr condition;
    TypedBlockPtr thenBlock;
    TypedStatementPtr elseBranch;
};
struct TypedWhileStatement {
    TypedExpressionPtr condition;
    TypedBlockPtr body;
};
struct TypedExpressionStatement {
    TypedExpressionPtr expression;
};

struct TypedStatement {
    support::SourceSpan span;
    std::variant<TypedLetStatement,
                 TypedAssignmentStatement,
                 TypedReturnStatement,
                 TypedIfStatement,
                 TypedWhileStatement,
                 TypedExpressionStatement,
                 TypedBlockPtr>
        node;
};

struct TypedBlock {
    support::SourceSpan span;
    std::vector<TypedStatement> statements;
};
struct TypedParameter {
    support::SourceSpan span;
    support::SourceSpan nameSpan;
    SymbolId symbol;
    core::Type type;
};
struct ResolvedFunction {
    core::FunctionId id;
    core::FunctionKind kind;
    std::string name;
    core::FunctionSignature signature;
};
struct TypedFunctionDeclaration {
    support::SourceSpan span;
    support::SourceSpan nameSpan;
    core::FunctionId id;
    std::string name;
    core::FunctionSignature signature;
    std::vector<TypedParameter> parameters;
    TypedBlock body;
};
struct TypedProgram {
    support::SourceSpan span;
    std::vector<ResolvedFunction> functions;
    std::vector<TypedFunctionDeclaration> declarations;
};

} // namespace ember::semantic
