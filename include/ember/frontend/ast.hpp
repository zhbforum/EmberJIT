#pragma once

#include "ember/frontend/token.hpp"

#include <memory>
#include <variant>
#include <vector>

namespace ember::frontend {

enum class TypeName {
    i64,
    f64,
    boolean,
    voidType,
};

enum class UnaryOperator {
    plus,
    minus,
};

enum class BinaryOperator {
    add,
    subtract,
    multiply,
    divide,
    remainder,
    equal,
    notEqual,
    less,
    lessEqual,
    greater,
    greaterEqual,
};

struct Expression;
struct Statement;
struct Block;

using ExpressionPtr = std::unique_ptr<Expression>;
using StatementPtr = std::unique_ptr<Statement>;
using BlockPtr = std::unique_ptr<Block>;

struct IdentifierExpression {
    support::SourceSpan nameSpan;
};

struct LiteralExpression {
    TokenKind kind;
    support::SourceSpan literalSpan;
};

struct UnaryExpression {
    UnaryOperator operation;
    support::SourceSpan operatorSpan;
    ExpressionPtr operand;
};

struct BinaryExpression {
    BinaryOperator operation;
    support::SourceSpan operatorSpan;
    ExpressionPtr left;
    ExpressionPtr right;
};

struct CallExpression {
    ExpressionPtr callee;
    std::vector<ExpressionPtr> arguments;
};

struct ParenthesizedExpression {
    ExpressionPtr expression;
};

struct Expression {
    support::SourceSpan span;
    std::variant<IdentifierExpression,
                 LiteralExpression,
                 UnaryExpression,
                 BinaryExpression,
                 CallExpression,
                 ParenthesizedExpression>
        node;
};

struct LetStatement {
    support::SourceSpan nameSpan;
    support::SourceSpan typeSpan;
    TypeName type;
    ExpressionPtr initializer;
};

struct AssignmentStatement {
    support::SourceSpan targetSpan;
    ExpressionPtr value;
};

struct ReturnStatement {
    ExpressionPtr value;
};

struct IfStatement {
    ExpressionPtr condition;
    BlockPtr thenBlock;
    StatementPtr elseBranch;
};

struct WhileStatement {
    ExpressionPtr condition;
    BlockPtr body;
};

struct ExpressionStatement {
    ExpressionPtr expression;
};

struct Statement {
    support::SourceSpan span;
    std::variant<LetStatement,
                 AssignmentStatement,
                 ReturnStatement,
                 IfStatement,
                 WhileStatement,
                 ExpressionStatement,
                 BlockPtr>
        node;
};

struct Block {
    support::SourceSpan span;
    std::vector<Statement> statements;
};

struct Parameter {
    support::SourceSpan span;
    support::SourceSpan nameSpan;
    support::SourceSpan typeSpan;
    TypeName type;
};

struct FunctionDeclaration {
    support::SourceSpan span;
    support::SourceSpan nameSpan;
    std::vector<Parameter> parameters;
    support::SourceSpan returnTypeSpan;
    TypeName returnType;
    Block body;
};

struct Program {
    support::SourceSpan span;
    std::vector<FunctionDeclaration> functions;
};

} // namespace ember::frontend
