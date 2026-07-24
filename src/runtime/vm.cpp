#include "ember/runtime/vm.hpp"

#include "ember/bytecode/builtins.hpp"

#include <bit>
#include <chrono>
#include <iostream>
#include <limits>
#include <utility>

namespace ember::runtime
{
namespace
{
constexpr std::size_t maximumFrameCount = 4096;

struct Frame
{
    const RuntimeFunction *function;
    std::size_t pc{};
    // Values below this boundary belong to suspended caller expressions.
    std::size_t stackBase{};
    std::vector<bytecode::Value> locals;
};

[[nodiscard]] ExecutionResult fail(std::string code, std::string message)
{
    return {.value = std::nullopt, .error = RuntimeError{std::move(code), std::move(message)}};
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

VirtualMachine::VirtualMachine(bytecode::VerifiedProgram verifiedProgram, RuntimeOptions options)
    : options_(std::move(options)), functions_(std::move(verifiedProgram)) {}

VirtualMachine VirtualMachine::create(bytecode::VerifiedProgram verifiedProgram, RuntimeOptions options)
{
    return VirtualMachine{std::move(verifiedProgram), std::move(options)};
}

const RuntimeFunction *VirtualMachine::function(semantic::FunctionId id) const noexcept
{
    return functions_.find(id);
}

ExecutionReport VirtualMachine::execute(semantic::FunctionId entry,
                                        const std::vector<bytecode::Value> &arguments)
{
    std::vector<HotFunctionEvent> hotEvents;
    RuntimeDispatcher dispatcher{functions_, options_, hotEvents};
    const auto failExecution = [&hotEvents](std::string code, std::string message) -> ExecutionReport
    {
        return {.result = fail(std::move(code), std::move(message)),
                .hotEvents = std::move(hotEvents)};
    };
    const auto createVmFrame = [](const RuntimeFunction *function,
                                  const std::vector<bytecode::Value> &callArguments,
                                  std::size_t stackBase) -> Frame
    {
        Frame frame{.function = function,
                    .pc = 0,
                    .stackBase = stackBase,
                    .locals = std::vector<bytecode::Value>(function->bytecode().localCount)};
        for (std::size_t index = 0; index < callArguments.size(); ++index)
            frame.locals[index] = callArguments[index];
        return frame;
    };

    const auto initialDispatch = dispatcher.dispatch(entry, arguments);
    if (!initialDispatch)
        return failExecution("R5002", "invalid entry function or arguments");
    if (initialDispatch->tier != ExecutionTier::virtualMachine)
        return failExecution("R5008", "native execution is unavailable");

    std::vector<Frame> frames;
    frames.push_back(createVmFrame(initialDispatch->function, arguments, 0));
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
        const auto &function = frame.function->bytecode();
        if (frame.pc >= function.code.size())
            return failExecution("R5005", "function fell through");

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
                return failExecution("R5004", "invalid i64 division");
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
            const auto *callee = functions_.find(instruction.operand);
            if (callee == nullptr)
                return failExecution("R5003", "unsupported function");
            const auto &calleeBytecode = callee->bytecode();
            std::vector<bytecode::Value> callArguments(calleeBytecode.signature.parameterTypes.size());
            for (std::size_t index = callArguments.size(); index > 0; --index)
                callArguments[index - 1] = popValue();

            // Advance the caller before pushing the callee so execution resumes
            // at the instruction following the call after the callee returns.
            ++frame.pc;
            if (calleeBytecode.kind == semantic::FunctionKind::user)
            {
                if (frames.size() >= maximumFrameCount)
                    return failExecution("R5006", "VM frame limit exceeded");
                const auto calleeDispatch = dispatcher.dispatch(callee->id(), callArguments);
                if (!calleeDispatch)
                    return failExecution("R5002", "invalid call arguments");
                if (calleeDispatch->tier != ExecutionTier::virtualMachine)
                    return failExecution("R5008", "native execution is unavailable");
                frames.push_back(createVmFrame(calleeDispatch->function, callArguments, stack.size()));
                break;
            }
            const auto *builtin = bytecode::findBuiltin(callee->id());
            if (builtin == nullptr)
                return failExecution("R5003", "unsupported host function");
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
                return failExecution("R5007", "invalid frame stack state");
            frames.pop_back();
            if (frames.empty())
                return {.result = {.value = std::move(value), .error = std::nullopt},
                        .hotEvents = std::move(hotEvents)};
            stack.push_back(std::move(value));
            break;
        }
        case returnVoid:
            if (stack.size() != frame.stackBase)
                return failExecution("R5007", "invalid frame stack state");
            frames.pop_back();
            if (frames.empty())
                return {.result = {}, .hotEvents = std::move(hotEvents)};
            break;
        }
    }
    return failExecution("R5005", "VM frame stack became empty without return");
}
} // namespace ember::runtime
