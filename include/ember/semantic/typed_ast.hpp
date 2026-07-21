#pragma once

#include "ember/frontend/ast.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace ember::semantic
{

enum class Type
{
    i64,
    f64,
    boolean,
    voidType
};
using SymbolId = std::uint32_t;
using FunctionId = std::uint32_t;
enum class FunctionKind
{
    user,
    host
};

[[nodiscard]] constexpr auto typeName(Type type) noexcept -> std::string_view
{
    switch (type)
    {
    case Type::i64:
        return "i64";
    case Type::f64:
        return "f64";
    case Type::boolean:
        return "bool";
    case Type::voidType:
        return "void";
    }
    return "<invalid-type>";
}

struct TypedExpression;
struct TypedStatement;
struct TypedBlock;
using TypedExpressionPtr = std::unique_ptr<TypedExpression>;
using TypedStatementPtr = std::unique_ptr<TypedStatement>;
using TypedBlockPtr = std::unique_ptr<TypedBlock>;

struct TypedIdentifierExpression
{
    support::SourceSpan nameSpan;
    SymbolId symbol;
};
using LiteralValue = std::variant<std::int64_t, double, bool>;
struct TypedLiteralExpression
{
    support::SourceSpan literalSpan;
    LiteralValue value;
};
struct TypedUnaryExpression
{
    frontend::UnaryOperator operation;
    support::SourceSpan operatorSpan;
    TypedExpressionPtr operand;
};
struct TypedBinaryExpression
{
    frontend::BinaryOperator operation;
    support::SourceSpan operatorSpan;
    TypedExpressionPtr left;
    TypedExpressionPtr right;
};
struct TypedCallExpression
{
    support::SourceSpan calleeSpan;
    FunctionId callee;
    std::vector<TypedExpressionPtr> arguments;
};
struct TypedParenthesizedExpression
{
    TypedExpressionPtr expression;
};

struct TypedExpression
{
    support::SourceSpan span;
    Type type;
    std::variant<TypedIdentifierExpression, TypedLiteralExpression, TypedUnaryExpression,
                 TypedBinaryExpression, TypedCallExpression, TypedParenthesizedExpression>
        node;
};

struct TypedLetStatement
{
    support::SourceSpan nameSpan;
    SymbolId symbol;
    Type type;
    TypedExpressionPtr initializer;
};
struct TypedAssignmentStatement
{
    support::SourceSpan targetSpan;
    SymbolId target;
    Type targetType;
    TypedExpressionPtr value;
};
struct TypedReturnStatement
{
    TypedExpressionPtr value;
};
struct TypedIfStatement
{
    TypedExpressionPtr condition;
    TypedBlockPtr thenBlock;
    TypedStatementPtr elseBranch;
};
struct TypedWhileStatement
{
    TypedExpressionPtr condition;
    TypedBlockPtr body;
};
struct TypedExpressionStatement
{
    TypedExpressionPtr expression;
};

struct TypedStatement
{
    support::SourceSpan span;
    std::variant<TypedLetStatement, TypedAssignmentStatement, TypedReturnStatement,
                 TypedIfStatement, TypedWhileStatement, TypedExpressionStatement, TypedBlockPtr>
        node;
};

struct TypedBlock
{
    support::SourceSpan span;
    std::vector<TypedStatement> statements;
};
struct TypedParameter
{
    support::SourceSpan span;
    support::SourceSpan nameSpan;
    SymbolId symbol;
    Type type;
};
struct FunctionSignature
{
    std::vector<Type> parameterTypes;
    Type returnType;
};
struct ResolvedFunction
{
    FunctionId id;
    FunctionKind kind;
    std::string name;
    FunctionSignature signature;
};
struct TypedFunctionDeclaration
{
    support::SourceSpan span;
    support::SourceSpan nameSpan;
    FunctionId id;
    std::string name;
    FunctionSignature signature;
    std::vector<TypedParameter> parameters;
    TypedBlock body;
};
struct TypedProgram
{
    support::SourceSpan span;
    std::vector<ResolvedFunction> functions;
    std::vector<TypedFunctionDeclaration> declarations;
};

} // namespace ember::semantic
