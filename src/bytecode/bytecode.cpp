#include "ember/bytecode/bytecode.hpp"

#include "ember/bytecode/builtins.hpp"

#include <algorithm>
#include <chrono>
#include <concepts>
#include <iomanip>
#include <iostream>
#include <queue>
#include <sstream>
#include <type_traits>
#include <unordered_map>
#include <utility>

namespace ember::bytecode {
void nativePrintI64(std::int64_t value) noexcept {
    std::cout << value << '\n';
}

void nativePrintF64(double value) noexcept {
    std::cout << value << '\n';
}

std::int64_t nativeClockMs() noexcept {
    return static_cast<std::int64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                         std::chrono::steady_clock::now().time_since_epoch())
                                         .count());
}

namespace {
[[nodiscard]] bool hasType(const Value& value, semantic::Type type) noexcept {
    return (type == semantic::Type::i64 && std::holds_alternative<std::int64_t>(value)) ||
           (type == semantic::Type::f64 && std::holds_alternative<double>(value)) ||
           (type == semantic::Type::boolean && std::holds_alternative<bool>(value));
}
} // namespace

BuiltinInvocation invokeBuiltin(const BuiltinDescriptor& builtin,
                                std::span<const Value> arguments) noexcept {
    if (arguments.size() != builtin.signature.parameterTypes.size())
        return {};
    for (std::size_t index{}; index < arguments.size(); ++index)
        if (!hasType(arguments[index], builtin.signature.parameterTypes[index]))
            return {};

    switch (builtin.kind) {
    case BuiltinKind::printI64:
        if (builtin.nativeAbi != NativeBuiltinAbi::i64ToVoid)
            return {};
        nativePrintI64(std::get<std::int64_t>(arguments[0]));
        return {.succeeded = true, .value = std::nullopt};
    case BuiltinKind::printF64:
        if (builtin.nativeAbi != NativeBuiltinAbi::f64ToVoid)
            return {};
        nativePrintF64(std::get<double>(arguments[0]));
        return {.succeeded = true, .value = std::nullopt};
    case BuiltinKind::clockMs:
        if (builtin.nativeAbi != NativeBuiltinAbi::voidToI64)
            return {};
        return {.succeeded = true, .value = Value{nativeClockMs()}};
    }
    return {};
}

std::uintptr_t nativeBuiltinEntry(const BuiltinDescriptor& builtin) noexcept {
    switch (builtin.kind) {
    case BuiltinKind::printI64:
        if (builtin.nativeAbi != NativeBuiltinAbi::i64ToVoid)
            return 0;
        return reinterpret_cast<std::uintptr_t>(&nativePrintI64);
    case BuiltinKind::printF64:
        if (builtin.nativeAbi != NativeBuiltinAbi::f64ToVoid)
            return 0;
        return reinterpret_cast<std::uintptr_t>(&nativePrintF64);
    case BuiltinKind::clockMs:
        if (builtin.nativeAbi != NativeBuiltinAbi::voidToI64)
            return 0;
        return reinterpret_cast<std::uintptr_t>(&nativeClockMs);
    }
    return 0;
}

namespace {
[[nodiscard]] support::Diagnostic error(std::string message) {
    return {.stage = support::DiagnosticStage::bytecode,
            .severity = support::DiagnosticSeverity::error,
            .code = "E4001",
            .message = std::move(message),
            .primarySpan = {}};
}
[[nodiscard]] auto valueType(const Value& value) -> semantic::Type {
    if (std::holds_alternative<std::int64_t>(value))
        return semantic::Type::i64;
    if (std::holds_alternative<double>(value))
        return semantic::Type::f64;
    return semantic::Type::boolean;
}
[[nodiscard]] auto binaryOpcode(frontend::BinaryOperator op, semantic::Type type) -> Opcode {
    using enum frontend::BinaryOperator;
    const bool f = type == semantic::Type::f64;
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
                 : (type == semantic::Type::boolean ? Opcode::equalBool : Opcode::equalI64);
    case notEqual:
        return f ? Opcode::notEqualF64
                 : (type == semantic::Type::boolean ? Opcode::notEqualBool : Opcode::notEqualI64);
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
                    .kind = semantic::FunctionKind::user,
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
        emit(function_.signature.returnType == semantic::Type::voidType ? Opcode::returnVoid
                                                                        : Opcode::returnValue);
        function_.localCount = static_cast<std::uint32_t>(slots_.size());
        return std::move(function_);
    }

private:
    void emit(Opcode op, std::uint32_t operand = 0, std::optional<Value> value = std::nullopt) {
        function_.code.push_back({op, operand, std::move(value)});
    }
    [[nodiscard]] std::uint32_t slot(semantic::SymbolId symbol,
                                     semantic::Type type = semantic::Type::voidType) {
        const auto [it, inserted] =
            slots_.try_emplace(symbol, static_cast<std::uint32_t>(slots_.size()));
        if (inserted)
            function_.localTypes.push_back(type);
        return it->second;
    }
    void expression(const semantic::TypedExpression& expr) {
        std::visit([this, &expr](const auto& node) { expressionNode(node, expr.type); }, expr.node);
    }
    void expressionNode(const semantic::TypedIdentifierExpression& node, semantic::Type) {
        emit(Opcode::load, slot(node.symbol));
    }
    void expressionNode(const semantic::TypedLiteralExpression& node, semantic::Type) {
        emit(Opcode::constant, 0, node.value);
    }
    void expressionNode(const semantic::TypedParenthesizedExpression& node, semantic::Type) {
        expression(*node.expression);
    }
    void expressionNode(const semantic::TypedUnaryExpression& node, semantic::Type type) {
        expression(*node.operand);
        switch (node.operation) {
        case frontend::UnaryOperator::plus:
            break;
        case frontend::UnaryOperator::minus:
            emit(type == semantic::Type::f64 ? Opcode::negateF64 : Opcode::negateI64);
            break;
        }
    }
    void expressionNode(const semantic::TypedBinaryExpression& node, semantic::Type) {
        expression(*node.left);
        expression(*node.right);
        emit(binaryOpcode(node.operation, node.left->type));
    }
    void expressionNode(const semantic::TypedCallExpression& node, semantic::Type) {
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
        } else
            emit(Opcode::returnVoid);
    }
    void statementNode(const semantic::TypedExpressionStatement& node) {
        expression(*node.expression);
        if (node.expression->type != semantic::Type::voidType)
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
        for (const auto& item : body.statements)
            statement(item);
    }
    Function function_;
    std::unordered_map<semantic::SymbolId, std::uint32_t> slots_;
};
} // namespace

CompileResult Compiler::compile(const semantic::TypedProgram& program) const {
    Program result;
    result.functions.reserve(program.functions.size());
    for (const auto& function : program.functions)
        if (function.kind == semantic::FunctionKind::host)
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

VerifyResult Verifier::verify(Program program) const {
    std::vector<support::Diagnostic> diagnostics;
    const auto validType = [](semantic::Type type) {
        return type == semantic::Type::i64 || type == semantic::Type::f64 ||
               type == semantic::Type::boolean || type == semantic::Type::voidType;
    };
    const auto validSignature = [&validType](const semantic::FunctionSignature& signature) {
        if (!validType(signature.returnType))
            return false;
        for (const auto type : signature.parameterTypes)
            if (!validType(type) || type == semantic::Type::voidType)
                return false;
        return true;
    };
    std::unordered_map<semantic::FunctionId, const Function*> functions;
    for (const auto& function : program.functions) {
        if (!functions.try_emplace(function.id, &function).second)
            diagnostics.push_back(error("duplicate function id"));
        if (!validSignature(function.signature))
            diagnostics.push_back(error("function has an invalid signature type"));
        if (function.kind == semantic::FunctionKind::host) {
            const auto* builtin = findBuiltin(function.id);
            if (builtin == nullptr ||
                builtin->signature.parameterTypes != function.signature.parameterTypes ||
                builtin->signature.returnType != function.signature.returnType ||
                function.localCount != 0 || !function.localTypes.empty() || !function.code.empty())
                diagnostics.push_back(error("host function does not match the builtin registry"));
        } else if (function.kind != semantic::FunctionKind::user)
            diagnostics.push_back(error("function has an invalid kind"));
    }

    const auto instructionError =
        [&](const Function& function, std::size_t pc, std::string message) {
            diagnostics.push_back(error("function #" + std::to_string(function.id) + ", pc " +
                                        std::to_string(pc) + ": " + std::move(message)));
        };
    const auto canonical = [](const Instruction& instruction, bool needsValue, bool needsOperand) {
        return instruction.value.has_value() == needsValue &&
               (needsOperand || instruction.operand == 0);
    };

    for (const auto& function : program.functions) {
        if (function.kind != semantic::FunctionKind::user)
            continue;
        if (function.code.empty() || function.localTypes.size() != function.localCount ||
            function.localCount < function.signature.parameterTypes.size()) {
            diagnostics.push_back(error("function has invalid local layout"));
            continue;
        }
        for (std::size_t slot = 0; slot < function.localTypes.size(); ++slot) {
            if (!validType(function.localTypes[slot]) ||
                function.localTypes[slot] == semantic::Type::voidType ||
                (slot < function.signature.parameterTypes.size() &&
                 function.localTypes[slot] != function.signature.parameterTypes[slot]))
                diagnostics.push_back(error("function has invalid parameter/local type layout"));
        }
        for (std::size_t pc = 0; pc < function.code.size(); ++pc) {
            const auto& instruction = function.code[pc];
            bool valid = true;
            switch (instruction.opcode) {
            case Opcode::constant:
                valid = canonical(instruction, true, false);
                break;
            case Opcode::load:
            case Opcode::store:
                valid = canonical(instruction, false, true) &&
                        instruction.operand < function.localCount;
                break;
            case Opcode::jump:
            case Opcode::jumpIfFalse:
                valid = canonical(instruction, false, true) &&
                        instruction.operand < function.code.size();
                break;
            case Opcode::call:
                valid =
                    canonical(instruction, false, true) && functions.contains(instruction.operand);
                break;
            case Opcode::pop:
            case Opcode::negateI64:
            case Opcode::negateF64:
            case Opcode::addI64:
            case Opcode::subI64:
            case Opcode::mulI64:
            case Opcode::divI64:
            case Opcode::remI64:
            case Opcode::addF64:
            case Opcode::subF64:
            case Opcode::mulF64:
            case Opcode::divF64:
            case Opcode::equalI64:
            case Opcode::equalF64:
            case Opcode::equalBool:
            case Opcode::notEqualI64:
            case Opcode::notEqualF64:
            case Opcode::notEqualBool:
            case Opcode::lessI64:
            case Opcode::lessEqualI64:
            case Opcode::greaterI64:
            case Opcode::greaterEqualI64:
            case Opcode::lessF64:
            case Opcode::lessEqualF64:
            case Opcode::greaterF64:
            case Opcode::greaterEqualF64:
            case Opcode::returnValue:
            case Opcode::returnVoid:
                valid = canonical(instruction, false, false);
                break;
            default:
                valid = false;
                break;
            }
            if (!valid)
                instructionError(function, pc, "non-canonical or invalid instruction");
        }
    }
    if (!diagnostics.empty())
        return {.program = std::nullopt, .diagnostics = std::move(diagnostics)};

    struct State {
        std::vector<semantic::Type> stack;
        std::vector<bool> initialized;
    };
    for (const auto& function : program.functions) {
        if (function.kind != semantic::FunctionKind::user)
            continue;
        std::vector<std::optional<State>> incoming(function.code.size());
        State entry{.stack = {}, .initialized = std::vector<bool>(function.localCount, false)};
        std::fill_n(entry.initialized.begin(), function.signature.parameterTypes.size(), true);
        incoming[0] = entry;
        std::queue<std::size_t> work;
        work.push(0);
        while (!work.empty()) {
            const auto pc = work.front();
            work.pop();
            State state = *incoming[pc];
            const auto& instruction = function.code[pc];
            const auto diagnosticCount = diagnostics.size();
            const auto pop = [&](semantic::Type type) {
                if (state.stack.empty() || state.stack.back() != type) {
                    instructionError(function, pc, "stack type mismatch");
                    return false;
                }
                state.stack.pop_back();
                return true;
            };
            const auto binary = [&](semantic::Type input, semantic::Type output) {
                if (state.stack.size() < 2 || state.stack.back() != input ||
                    state.stack[state.stack.size() - 2] != input) {
                    instructionError(function, pc, "stack type mismatch");
                    return false;
                }
                state.stack.pop_back();
                state.stack.back() = output;
                return true;
            };
            bool valid = true;
            std::vector<std::size_t> successors;
            switch (instruction.opcode) {
            case Opcode::constant:
                state.stack.push_back(valueType(*instruction.value));
                break;
            case Opcode::load:
                valid = state.initialized[instruction.operand];
                if (valid)
                    state.stack.push_back(function.localTypes[instruction.operand]);
                else
                    instructionError(function, pc, "load of uninitialized local");
                break;
            case Opcode::store:
                valid = pop(function.localTypes[instruction.operand]);
                if (valid)
                    state.initialized[instruction.operand] = true;
                break;
            case Opcode::pop:
                valid = !state.stack.empty();
                if (valid)
                    state.stack.pop_back();
                else
                    instructionError(function, pc, "stack underflow");
                break;
            case Opcode::negateI64:
                valid = pop(semantic::Type::i64);
                if (valid)
                    state.stack.push_back(semantic::Type::i64);
                break;
            case Opcode::negateF64:
                valid = pop(semantic::Type::f64);
                if (valid)
                    state.stack.push_back(semantic::Type::f64);
                break;
            case Opcode::addI64:
            case Opcode::subI64:
            case Opcode::mulI64:
            case Opcode::divI64:
            case Opcode::remI64:
                valid = binary(semantic::Type::i64, semantic::Type::i64);
                break;
            case Opcode::addF64:
            case Opcode::subF64:
            case Opcode::mulF64:
            case Opcode::divF64:
                valid = binary(semantic::Type::f64, semantic::Type::f64);
                break;
            case Opcode::equalI64:
            case Opcode::notEqualI64:
            case Opcode::lessI64:
            case Opcode::lessEqualI64:
            case Opcode::greaterI64:
            case Opcode::greaterEqualI64:
                valid = binary(semantic::Type::i64, semantic::Type::boolean);
                break;
            case Opcode::equalF64:
            case Opcode::notEqualF64:
            case Opcode::lessF64:
            case Opcode::lessEqualF64:
            case Opcode::greaterF64:
            case Opcode::greaterEqualF64:
                valid = binary(semantic::Type::f64, semantic::Type::boolean);
                break;
            case Opcode::equalBool:
            case Opcode::notEqualBool:
                valid = binary(semantic::Type::boolean, semantic::Type::boolean);
                break;
            case Opcode::jump:
                successors.push_back(instruction.operand);
                break;
            case Opcode::jumpIfFalse:
                valid = pop(semantic::Type::boolean);
                successors.push_back(instruction.operand);
                break;
            case Opcode::call: {
                const auto& signature = functions.at(instruction.operand)->signature;
                for (auto it = signature.parameterTypes.rbegin();
                     it != signature.parameterTypes.rend();
                     ++it)
                    valid = pop(*it) && valid;
                if (valid && signature.returnType != semantic::Type::voidType)
                    state.stack.push_back(signature.returnType);
                break;
            }
            case Opcode::returnValue:
                if (function.signature.returnType == semantic::Type::voidType) {
                    instructionError(function, pc, "void function cannot return a value");
                    valid = false;
                    break;
                }
                valid = pop(function.signature.returnType);
                if (valid && !state.stack.empty()) {
                    instructionError(function, pc, "return leaves extra values on the stack");
                    valid = false;
                }
                break;
            case Opcode::returnVoid:
                if (function.signature.returnType != semantic::Type::voidType) {
                    instructionError(function, pc, "non-void function must return a value");
                    valid = false;
                    break;
                }
                if (!state.stack.empty()) {
                    instructionError(function, pc, "void return requires an empty stack");
                    valid = false;
                }
                break;
            default:
                valid = false;
                break;
            }
            if (!valid) {
                if (diagnostics.size() == diagnosticCount)
                    instructionError(function, pc, "invalid instruction state");
                continue;
            }
            if (instruction.opcode != Opcode::jump && instruction.opcode != Opcode::returnValue &&
                instruction.opcode != Opcode::returnVoid) {
                if (pc + 1 == function.code.size()) {
                    instructionError(function, pc, "reachable fall-through");
                    continue;
                }
                successors.push_back(pc + 1);
            }
            for (const auto target : successors) {
                if (!incoming[target]) {
                    incoming[target] = state;
                    work.push(target);
                    continue;
                }
                auto& current = *incoming[target];
                if (current.stack != state.stack) {
                    instructionError(function, target, "incompatible stack at control-flow merge");
                    continue;
                }
                auto merged = current.initialized;
                for (std::size_t slot = 0; slot < merged.size(); ++slot)
                    merged[slot] = merged[slot] && state.initialized[slot];
                if (merged != current.initialized) {
                    current.initialized = std::move(merged);
                    work.push(target);
                }
            }
        }
    }
    if (!diagnostics.empty())
        return {.program = std::nullopt, .diagnostics = std::move(diagnostics)};
    return {.program = VerifiedProgram{std::move(program)}, .diagnostics = {}};
}

std::string dump(const VerifiedProgram& verified) {
    const auto& program = verified.program();
    const auto opcodeName = [](Opcode opcode) -> std::string_view {
        switch (opcode) {
        case Opcode::constant:
            return "const";
        case Opcode::load:
            return "load";
        case Opcode::store:
            return "store";
        case Opcode::pop:
            return "pop";
        case Opcode::negateI64:
            return "neg.i64";
        case Opcode::negateF64:
            return "neg.f64";
        case Opcode::addI64:
            return "add.i64";
        case Opcode::subI64:
            return "sub.i64";
        case Opcode::mulI64:
            return "mul.i64";
        case Opcode::divI64:
            return "div.i64";
        case Opcode::remI64:
            return "rem.i64";
        case Opcode::addF64:
            return "add.f64";
        case Opcode::subF64:
            return "sub.f64";
        case Opcode::mulF64:
            return "mul.f64";
        case Opcode::divF64:
            return "div.f64";
        case Opcode::equalI64:
            return "eq.i64";
        case Opcode::equalF64:
            return "eq.f64";
        case Opcode::equalBool:
            return "eq.bool";
        case Opcode::notEqualI64:
            return "ne.i64";
        case Opcode::notEqualF64:
            return "ne.f64";
        case Opcode::notEqualBool:
            return "ne.bool";
        case Opcode::lessI64:
            return "lt.i64";
        case Opcode::lessEqualI64:
            return "le.i64";
        case Opcode::greaterI64:
            return "gt.i64";
        case Opcode::greaterEqualI64:
            return "ge.i64";
        case Opcode::lessF64:
            return "lt.f64";
        case Opcode::lessEqualF64:
            return "le.f64";
        case Opcode::greaterF64:
            return "gt.f64";
        case Opcode::greaterEqualF64:
            return "ge.f64";
        case Opcode::jump:
            return "jump";
        case Opcode::jumpIfFalse:
            return "jump_false";
        case Opcode::call:
            return "call";
        case Opcode::returnValue:
            return "return";
        case Opcode::returnVoid:
            return "return_void";
        }
        return "<invalid>";
    };
    const auto printConstant = [](std::ostream& output, const Value& value) {
        std::visit(
            [&output](const auto& item) {
                using Item = std::decay_t<decltype(item)>;
                if constexpr (std::same_as<Item, std::int64_t>)
                    output << "const.i64 " << item;
                else if constexpr (std::same_as<Item, double>)
                    output << "const.f64 " << std::hexfloat << item << std::defaultfloat;
                else
                    output << "const.bool " << (item ? "true" : "false");
            },
            value);
    };
    std::ostringstream output;
    for (const auto& function : program.functions) {
        output << (function.kind == semantic::FunctionKind::host ? "host" : "fn") << " #"
               << function.id << " (";
        for (std::size_t index = 0; index < function.signature.parameterTypes.size(); ++index) {
            if (index != 0)
                output << ", ";
            output << semantic::typeName(function.signature.parameterTypes[index]);
        }
        output << ") -> " << semantic::typeName(function.signature.returnType) << '\n';
        if (function.kind == semantic::FunctionKind::user)
            for (std::size_t slot = 0; slot < function.localTypes.size(); ++slot)
                output << "  local %" << slot << ": "
                       << semantic::typeName(function.localTypes[slot]) << '\n';
        for (std::size_t i = 0; i < function.code.size(); ++i) {
            const auto& instruction = function.code[i];
            output << "  " << std::setw(4) << std::setfill('0') << i << "  ";
            output << std::setfill(' ');
            if (instruction.value)
                printConstant(output, *instruction.value);
            else {
                output << opcodeName(instruction.opcode);
                if (instruction.opcode == Opcode::load || instruction.opcode == Opcode::store)
                    output << " %" << instruction.operand;
                else if (instruction.opcode == Opcode::jump ||
                         instruction.opcode == Opcode::jumpIfFalse)
                    output << ' ' << std::setw(4) << std::setfill('0') << instruction.operand;
                else if (instruction.opcode == Opcode::call)
                    output << " #" << instruction.operand;
            }
            output << std::setfill(' ') << '\n';
        }
    }
    return output.str();
}
} // namespace ember::bytecode
