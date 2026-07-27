#include "ember/runtime/vm.hpp"

#include "ember/support/i64_semantics.hpp"

#include "ember/bytecode/builtins.hpp"

#include <chrono>
#include <iostream>
#include <limits>
#include <new>
#include <utility>

namespace ember::runtime
{
namespace
{
constexpr std::size_t maximumFrameCount = 4096;
constexpr std::size_t maximumNativeBridgeDepth = 64;

struct Frame
{
    const RuntimeFunction *function;
    std::size_t pc{};
    // Values below this boundary belong to suspended caller expressions.
    std::size_t stackBase{};
    bool ownsDynamicFrame{};
    std::vector<bytecode::Value> locals;
};

[[nodiscard]] ExecutionResult fail(std::string code, std::string message)
{
    return {.value = std::nullopt, .error = RuntimeError{std::move(code), std::move(message)}};
}

} // namespace

VirtualMachine::VirtualMachine(bytecode::VerifiedProgram verifiedProgram, RuntimeOptions options)
    : options_(std::move(options)), functions_(std::move(verifiedProgram), options_.jitEnabled) {}

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
    NativeCallState state{.machine = this,
                          .events = &hotEvents,
                          .dynamicFrameCount = 0,
                          .nativeBridgeDepth = 0,
                          .forceNativeCallFailureForTesting = options_.forceNativeCallFailureForTesting};
    auto result = executeInternal(entry, arguments, state);
    return {.result = std::move(result), .hotEvents = std::move(hotEvents)};
}

ExecutionResult VirtualMachine::executeInternal(semantic::FunctionId entry,
                                                const std::vector<bytecode::Value> &arguments,
                                                NativeCallState &state, bool forceVm)
{
    if (state.dynamicFrameCount >= maximumFrameCount)
        return fail("R5006", "VM frame limit exceeded");
    ++state.dynamicFrameCount;
    struct DynamicFrameGuard
    {
        NativeCallState &state;
        ~DynamicFrameGuard() { --state.dynamicFrameCount; }
    } dynamicFrameGuard{state};

    RuntimeDispatcher dispatcher{functions_, options_, *state.events};
    const auto failExecution = [](std::string code, std::string message) -> ExecutionResult
    {
        return fail(std::move(code), std::move(message));
    };
    const auto createVmFrame = [](const RuntimeFunction *function,
                                  const std::vector<bytecode::Value> &callArguments,
                                  std::size_t stackBase) -> Frame
    {
        Frame frame{.function = function,
                    .pc = 0,
                    .stackBase = stackBase,
                    .ownsDynamicFrame = false,
                    .locals = std::vector<bytecode::Value>(function->bytecode().localCount)};
        for (std::size_t index = 0; index < callArguments.size(); ++index)
            frame.locals[index] = callArguments[index];
        return frame;
    };
    const auto invokeNative = [&state](const RuntimeFunction *function,
                                       std::span<const bytecode::Value> callArguments)
        -> std::optional<NativeInvocation>
    {
        const auto &bytecode = function->bytecode();
        std::vector<std::int64_t> locals(bytecode.localCount);
        for (std::size_t index = 0; index < callArguments.size(); ++index)
        {
            if (!std::holds_alternative<std::int64_t>(callArguments[index]))
                return std::nullopt;
            locals[index] = std::get<std::int64_t>(callArguments[index]);
        }
        return function->invokeNativeI64Frame(locals, &state, &VirtualMachine::nativeCallBridge);
    };

    const auto initialDispatch = dispatcher.dispatch(entry, arguments);
    if (!initialDispatch)
        return failExecution("R5002", "invalid entry function or arguments");
    if (!forceVm && initialDispatch->tier == ExecutionTier::native)
    {
        const auto result = invokeNative(initialDispatch->function, arguments);
        if (!result)
            return failExecution("R5002", "invalid native entry arguments");
        if (result->error == NativeFrameError::invalidI64Division)
            return failExecution("R5004", "invalid i64 division");
        if (result->error == NativeFrameError::frameLimitExceeded)
            return failExecution("R5006", "VM frame limit exceeded");
        if (result->error == NativeFrameError::invalidCall)
            return failExecution("R5003", "native call bridge failed");
        if (result->error != NativeFrameError::none)
            return failExecution("R5005", "invalid native execution status");
        return {.value = result->value, .error = std::nullopt};
    }

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
            stack.push_back(support::i64::negate(std::get<std::int64_t>(popValue())));
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
            stack.push_back(instruction.opcode == addI64   ? support::i64::add(left, right)
                            : instruction.opcode == subI64 ? support::i64::subtract(left, right)
                                                           : support::i64::multiply(left, right));
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
                if (state.dynamicFrameCount >= maximumFrameCount)
                    return failExecution("R5006", "VM frame limit exceeded");
                const auto calleeDispatch = dispatcher.dispatch(callee->id(), callArguments);
                if (!calleeDispatch)
                    return failExecution("R5002", "invalid call arguments");
                if (!forceVm && calleeDispatch->tier == ExecutionTier::native)
                {
                    // Unlike a bridge re-entry, this VM-to-native transition
                    // does not call executeInternal. Reserve its Ember frame
                    // explicitly so every dynamic user invocation consumes
                    // exactly one shared-budget slot.
                    ++state.dynamicFrameCount;
                    DynamicFrameGuard nativeFrameGuard{state};
                    const auto result = invokeNative(calleeDispatch->function, callArguments);
                    if (!result)
                        return failExecution("R5002", "invalid native call arguments");
                    if (result->error == NativeFrameError::invalidI64Division)
                        return failExecution("R5004", "invalid i64 division");
                    if (result->error == NativeFrameError::frameLimitExceeded)
                        return failExecution("R5006", "VM frame limit exceeded");
                    if (result->error == NativeFrameError::invalidCall)
                        return failExecution("R5003", "native call bridge failed");
                    if (result->error != NativeFrameError::none)
                        return failExecution("R5005", "invalid native execution status");
                    stack.push_back(result->value);
                    break;
                }
                ++state.dynamicFrameCount;
                auto calleeFrame = createVmFrame(calleeDispatch->function, callArguments, stack.size());
                calleeFrame.ownsDynamicFrame = true;
                frames.push_back(std::move(calleeFrame));
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
            const bool ownsDynamicFrame = frame.ownsDynamicFrame;
            frames.pop_back();
            if (ownsDynamicFrame)
                --state.dynamicFrameCount;
            if (frames.empty())
                return {.value = std::move(value), .error = std::nullopt};
            stack.push_back(std::move(value));
            break;
        }
        case returnVoid:
            if (stack.size() != frame.stackBase)
                return failExecution("R5007", "invalid frame stack state");
            const bool ownsDynamicFrame = frame.ownsDynamicFrame;
            frames.pop_back();
            if (ownsDynamicFrame)
                --state.dynamicFrameCount;
            if (frames.empty())
                return {};
            break;
        }
    }
    return failExecution("R5005", "VM frame stack became empty without return");
}

std::uint64_t VirtualMachine::nativeCallBridge(jit::NativeFrame *caller, std::uint64_t callee,
                                               const std::int64_t *arguments,
                                               std::uint64_t argumentCount,
                                               std::int64_t *result) noexcept
{
    const auto failBridge = [caller](NativeFrameError error) -> std::uint64_t
    {
        if (caller != nullptr)
            caller->errorCode = static_cast<std::uint64_t>(error);
        return static_cast<std::uint64_t>(error);
    };
    if (caller == nullptr || caller->callContext == nullptr || result == nullptr ||
        callee > std::numeric_limits<semantic::FunctionId>::max() ||
        argumentCount > std::numeric_limits<std::size_t>::max() ||
        (argumentCount != 0 && arguments == nullptr))
        return failBridge(NativeFrameError::invalidCall);

    auto *state = static_cast<NativeCallState *>(caller->callContext);
    if (state->machine == nullptr || state->events == nullptr)
        return failBridge(NativeFrameError::invalidCall);
    if (state->forceNativeCallFailureForTesting)
        return failBridge(NativeFrameError::invalidCall);
    bool depthIncremented{};
    try
    {
        std::vector<bytecode::Value> callArguments;
        callArguments.reserve(static_cast<std::size_t>(argumentCount));
        for (std::size_t index{}; index < static_cast<std::size_t>(argumentCount); ++index)
            callArguments.emplace_back(arguments[index]);

        const bool forceVm = state->nativeBridgeDepth >= maximumNativeBridgeDepth;
        if (!forceVm)
        {
            ++state->nativeBridgeDepth;
            depthIncremented = true;
        }
        const auto invocation = state->machine->executeInternal(
            static_cast<semantic::FunctionId>(callee), callArguments, *state, forceVm);
        if (depthIncremented)
        {
            --state->nativeBridgeDepth;
            depthIncremented = false;
        }
        if (invocation.error)
        {
            const auto error = invocation.error->code == "R5004"     ? NativeFrameError::invalidI64Division
                               : invocation.error->code == "R5006" ? NativeFrameError::frameLimitExceeded
                                                                       : NativeFrameError::invalidCall;
            return failBridge(error);
        }
        if (!invocation.value || !std::holds_alternative<std::int64_t>(*invocation.value))
            return failBridge(NativeFrameError::invalidCall);
        *result = std::get<std::int64_t>(*invocation.value);
        return static_cast<std::uint64_t>(NativeFrameError::none);
    }
    catch (const std::bad_alloc &)
    {
        if (depthIncremented)
            --state->nativeBridgeDepth;
        return failBridge(NativeFrameError::invalidCall);
    }
    catch (...)
    {
        if (depthIncremented)
            --state->nativeBridgeDepth;
        return failBridge(NativeFrameError::invalidCall);
    }
}
} // namespace ember::runtime
