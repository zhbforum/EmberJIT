#include "ember/runtime/vm.hpp"

#include "ember/bytecode/builtins.hpp"

#include <bit>
#include <chrono>
#include <iostream>
#include <limits>
#include <unordered_map>

namespace ember::runtime
{
namespace
{
constexpr std::size_t maximumFrameCount = 4096;

struct Frame
{
    const bytecode::Function *function;
    std::size_t pc{};
    // Values below this boundary belong to suspended caller expressions.
    std::size_t stackBase{};
    std::vector<bytecode::Value> locals;
};

[[nodiscard]] ExecutionResult fail(std::string code, std::string message)
{
    return {.value = std::nullopt, .error = RuntimeError{std::move(code), std::move(message)}};
}

[[nodiscard]] auto valueMatches(bytecode::Value const &value, semantic::Type type) -> bool
{
    return (type == semantic::Type::i64 && std::holds_alternative<std::int64_t>(value)) ||
           (type == semantic::Type::f64 && std::holds_alternative<double>(value)) ||
           (type == semantic::Type::boolean && std::holds_alternative<bool>(value));
}

[[nodiscard]] auto wrapAdd(std::int64_t left, std::int64_t right) -> std::int64_t
{
    return std::bit_cast<std::int64_t>(static_cast<std::uint64_t>(left) +
                                       static_cast<std::uint64_t>(right));
}
[[nodiscard]] auto wrapSub(std::int64_t left, std::int64_t right) -> std::int64_t
{
    return std::bit_cast<std::int64_t>(static_cast<std::uint64_t>(left) -
                                       static_cast<std::uint64_t>(right));
}
[[nodiscard]] auto wrapMul(std::int64_t left, std::int64_t right) -> std::int64_t
{
    return std::bit_cast<std::int64_t>(static_cast<std::uint64_t>(left) *
                                       static_cast<std::uint64_t>(right));
}
} // namespace

VirtualMachine VirtualMachine::create(bytecode::VerifiedProgram verifiedProgram)
{
    return VirtualMachine{std::move(verifiedProgram).takeProgram()};
}

ExecutionResult VirtualMachine::execute(semantic::FunctionId entry,
                                        const std::vector<bytecode::Value> &arguments) const
{
    std::unordered_map<semantic::FunctionId, const bytecode::Function *> functions;
    for (const auto &function : program_.functions)
        functions.emplace(function.id, &function);

    const auto createFrame = [&](semantic::FunctionId id,
                                 const std::vector<bytecode::Value> &arguments,
                                 std::size_t stackBase) -> std::optional<Frame>
    {
        const auto found = functions.find(id);
        if (found == functions.end() || found->second->kind != semantic::FunctionKind::user)
            return std::nullopt;
        const auto &function = *found->second;
        if (arguments.size() != function.signature.parameterTypes.size())
            return std::nullopt;
        for (std::size_t index = 0; index < arguments.size(); ++index)
            if (!valueMatches(arguments[index], function.signature.parameterTypes[index]))
                return std::nullopt;
        Frame frame{.function = &function,
                    .pc = 0,
                    .stackBase = stackBase,
                    .locals = std::vector<bytecode::Value>(function.localCount)};
        for (std::size_t index = 0; index < arguments.size(); ++index)
            frame.locals[index] = arguments[index];
        return frame;
    };

    const auto initialFrame = createFrame(entry, arguments, 0);
    if (!initialFrame)
        return fail("R5002", "invalid entry function or arguments");

    std::vector<Frame> frames;
    frames.push_back(*initialFrame);
    std::vector<bytecode::Value> stack;
    const auto popValue = [&stack]() -> bytecode::Value
    {
        auto value = std::move(stack.back());
        stack.pop_back();
        return value;
    };

    while (!frames.empty())
    {
        auto &frame = frames.back();
        const auto &function = *frame.function;
        if (frame.pc >= function.code.size())
            return fail("R5005", "function fell through");

        const auto &instruction = function.code[frame.pc];
        using enum bytecode::Opcode;
        switch (instruction.opcode)
        {
        case constant:
            stack.push_back(*instruction.value);
            ++frame.pc;
            break;
        case load:
            stack.push_back(frame.locals[instruction.operand]);
            ++frame.pc;
            break;
        case store:
            frame.locals[instruction.operand] = popValue();
            ++frame.pc;
            break;
        case pop:
            (void)popValue();
            ++frame.pc;
            break;
        case negateI64:
            stack.push_back(wrapSub(0, std::get<std::int64_t>(popValue())));
            ++frame.pc;
            break;
        case negateF64:
            stack.push_back(-std::get<double>(popValue()));
            ++frame.pc;
            break;
        case addI64:
        case subI64:
        case mulI64:
        {
            const auto right = std::get<std::int64_t>(popValue());
            const auto left = std::get<std::int64_t>(popValue());
            stack.push_back(instruction.opcode == addI64   ? wrapAdd(left, right)
                            : instruction.opcode == subI64 ? wrapSub(left, right)
                                                           : wrapMul(left, right));
            ++frame.pc;
            break;
        }
        case divI64:
        case remI64:
        {
            const auto right = std::get<std::int64_t>(popValue());
            const auto left = std::get<std::int64_t>(popValue());
            if (right == 0 || (left == std::numeric_limits<std::int64_t>::min() && right == -1))
                return fail("R5004", "invalid i64 division");
            stack.push_back(instruction.opcode == divI64 ? left / right : left % right);
            ++frame.pc;
            break;
        }
        case addF64:
        case subF64:
        case mulF64:
        case divF64:
        {
            const auto right = std::get<double>(popValue());
            const auto left = std::get<double>(popValue());
            stack.push_back(instruction.opcode == addF64   ? left + right
                            : instruction.opcode == subF64 ? left - right
                            : instruction.opcode == mulF64 ? left * right
                                                           : left / right);
            ++frame.pc;
            break;
        }
        case equalI64:
        case notEqualI64:
        case lessI64:
        case lessEqualI64:
        case greaterI64:
        case greaterEqualI64:
        {
            const auto right = std::get<std::int64_t>(popValue());
            const auto left = std::get<std::int64_t>(popValue());
            stack.push_back(instruction.opcode == equalI64       ? left == right
                            : instruction.opcode == notEqualI64  ? left != right
                            : instruction.opcode == lessI64      ? left < right
                            : instruction.opcode == lessEqualI64 ? left <= right
                            : instruction.opcode == greaterI64   ? left > right
                                                                 : left >= right);
            ++frame.pc;
            break;
        }
        case equalF64:
        case notEqualF64:
        case lessF64:
        case lessEqualF64:
        case greaterF64:
        case greaterEqualF64:
        {
            const auto right = std::get<double>(popValue());
            const auto left = std::get<double>(popValue());
            stack.push_back(instruction.opcode == equalF64       ? left == right
                            : instruction.opcode == notEqualF64  ? left != right
                            : instruction.opcode == lessF64      ? left < right
                            : instruction.opcode == lessEqualF64 ? left <= right
                            : instruction.opcode == greaterF64   ? left > right
                                                                 : left >= right);
            ++frame.pc;
            break;
        }
        case equalBool:
        case notEqualBool:
        {
            const auto right = std::get<bool>(popValue());
            const auto left = std::get<bool>(popValue());
            stack.push_back(instruction.opcode == equalBool ? left == right : left != right);
            ++frame.pc;
            break;
        }
        case jump:
            frame.pc = instruction.operand;
            break;
        case jumpIfFalse:
            if (!std::get<bool>(popValue()))
                frame.pc = instruction.operand;
            else
                ++frame.pc;
            break;
        case call:
        {
            const auto &callee = *functions.at(instruction.operand);
            std::vector<bytecode::Value> callArguments(callee.signature.parameterTypes.size());
            for (std::size_t index = callArguments.size(); index > 0; --index)
                callArguments[index - 1] = popValue();

            // Advance the caller before pushing the callee so execution resumes
            // at the instruction following the call after the callee returns.
            ++frame.pc;
            if (callee.kind == semantic::FunctionKind::user)
            {
                if (frames.size() >= maximumFrameCount)
                    return fail("R5006", "VM frame limit exceeded");
                const auto calleeFrame = createFrame(callee.id, callArguments, stack.size());
                if (!calleeFrame)
                    return fail("R5002", "invalid call arguments");
                frames.push_back(*calleeFrame);
                break;
            }
            const auto *builtin = bytecode::findBuiltin(callee.id);
            if (builtin == nullptr)
                return fail("R5003", "unsupported host function");
            switch (builtin->kind)
            {
            case bytecode::BuiltinKind::printI64:
                std::cout << std::get<std::int64_t>(callArguments[0]) << '\n';
                break;
            case bytecode::BuiltinKind::printF64:
                std::cout << std::get<double>(callArguments[0]) << '\n';
                break;
            case bytecode::BuiltinKind::clockMs:
                stack.push_back(static_cast<std::int64_t>(
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now().time_since_epoch())
                        .count()));
                break;
            }
            break;
        }
        case returnValue:
        {
            auto value = popValue();
            // A returning frame must leave no operands above its caller-owned stack segment.
            if (stack.size() != frame.stackBase)
                return fail("R5007", "invalid frame stack state");
            frames.pop_back();
            if (frames.empty())
                return {.value = std::move(value), .error = std::nullopt};
            stack.push_back(std::move(value));
            break;
        }
        case returnVoid:
            if (stack.size() != frame.stackBase)
                return fail("R5007", "invalid frame stack state");
            frames.pop_back();
            if (frames.empty())
                return {};
            break;
        }
    }
    return fail("R5005", "VM frame stack became empty without return");
}
} // namespace ember::runtime
