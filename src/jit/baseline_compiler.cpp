#include "ember/jit/baseline_compiler.hpp"
#include "ember/jit/native_abi.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

namespace ember::jit::x64
{
namespace
{
[[nodiscard]] BaselineCompileResult unsupported()
{
    return {.code = std::nullopt, .error = BaselineCompileError::unsupportedFunction};
}

[[nodiscard]] bool isI64(semantic::Type type) noexcept { return type == semantic::Type::i64; }

[[nodiscard]] std::optional<std::int32_t> byteOffset(std::size_t slot)
{
    if (slot > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max() / 8))
        return std::nullopt;
    return static_cast<std::int32_t>(slot * 8);
}

constexpr std::size_t maximumNativeFrameSlots = 65'536;

[[nodiscard]] bool isCanonicalPreheader(const ir::Function &function, const ir::BasicBlock &preheader)
{
    const auto parameterCount = function.signature.parameterTypes.size();
    if (preheader.instructions.size() != parameterCount * 2U)
        return false;
    for (std::size_t index{}; index < parameterCount; ++index)
    {
        const auto &parameter = preheader.instructions[index];
        const auto &store = preheader.instructions[parameterCount + index];
        if (parameter.opcode != ir::Opcode::parameter || parameter.result != index ||
            parameter.local != index || parameter.input != ir::noValue ||
            parameter.left != ir::noValue || parameter.right != ir::noValue ||
            parameter.constant != 0 || parameter.callee != ir::noFunction ||
            !parameter.arguments.empty() || store.opcode != ir::Opcode::storeLocal ||
            store.result != ir::noValue || store.input != index || store.left != ir::noValue ||
            store.right != ir::noValue || store.local != index || store.constant != 0 ||
            store.callee != ir::noFunction || !store.arguments.empty())
            return false;
    }
    return true;
}

[[nodiscard]] bool emitBinary(Emitter &emitter, ir::Opcode opcode)
{
    switch (opcode)
    {
    case ir::Opcode::addI64:
        return emitter.add(Register::rax, Register::r8);
    case ir::Opcode::subI64:
        return emitter.subtract(Register::rax, Register::r8);
    case ir::Opcode::mulI64:
        return emitter.multiply(Register::rax, Register::r8);
    default:
        return false;
    }
}

[[nodiscard]] std::optional<Condition> conditionFor(ir::Opcode opcode)
{
    switch (opcode)
    {
    case ir::Opcode::equalI64:
        return Condition::equal;
    case ir::Opcode::notEqualI64:
        return Condition::notEqual;
    case ir::Opcode::lessI64:
        return Condition::less;
    case ir::Opcode::lessEqualI64:
        return Condition::lessEqual;
    case ir::Opcode::greaterI64:
        return Condition::greater;
    case ir::Opcode::greaterEqualI64:
        return Condition::greaterEqual;
    default:
        return std::nullopt;
    }
}

[[nodiscard]] bool isBoolean(semantic::Type type) noexcept { return type == semantic::Type::boolean; }
} // namespace

BaselineCompileResult BaselineCompiler::compile(const ir::VerifiedFunction &verified) const
{
    if (options_.forceFailureForTesting)
        return {.code = std::nullopt, .error = BaselineCompileError::emissionFailed};
    const auto &function = verified.function();
    if (!isI64(function.signature.returnType) || function.blocks.size() < 2 ||
        !std::ranges::all_of(function.signature.parameterTypes, isI64) ||
        !std::ranges::all_of(function.localTypes, isI64) ||
        !std::ranges::all_of(function.valueTypes,
                             [](semantic::Type type) { return isI64(type) || isBoolean(type); }))
        return unsupported();

    const auto &preheader = function.blocks[0];
    if (preheader.id != 0 || preheader.terminator.kind != ir::TerminatorKind::branch ||
        preheader.terminator.trueTarget != 1 || !isCanonicalPreheader(function, preheader))
        return unsupported();

    std::size_t maximumCallArgumentCount{};
    for (const auto &block : function.blocks)
        for (const auto &instruction : block.instructions)
            if (instruction.opcode == ir::Opcode::callI64)
                maximumCallArgumentCount = std::max(maximumCallArgumentCount,
                                                    instruction.arguments.size());
    if (function.valueTypes.size() > maximumNativeFrameSlots ||
        maximumCallArgumentCount > maximumNativeFrameSlots)
        return unsupported();
    const auto valueOffset = [](std::size_t value) { return byteOffset(value); };
    const auto localOffset = [](std::size_t local) { return byteOffset(local); };

    Emitter emitter;
    constexpr std::uint32_t callAreaBytes = 40;
    if (!emitter.push(Register::rbx) || !emitter.push(Register::r12) ||
        !emitter.push(Register::r13) || !emitter.push(Register::r14) ||
        !emitter.subtractStackPointer(callAreaBytes) || !emitter.move(Register::r12, Register::rcx) ||
        !emitter.load(Register::rbx, Register::r12, nativeFrameLocalsOffset) ||
        !emitter.load(Register::r13, Register::r12, nativeFrameSpillsOffset) ||
        !emitter.load(Register::r14, Register::r12, nativeFrameCallArgumentsOffset))
        return {.code = std::nullopt, .error = BaselineCompileError::emissionFailed};

    std::vector<Label> labels;
    labels.reserve(function.blocks.size());
    for (std::size_t index = 0; index < function.blocks.size(); ++index)
        labels.push_back(emitter.createLabel());
    const auto divisionFailure = emitter.createLabel();
    const auto callFailure = emitter.createLabel();
    // Parameter locals already reside in NativeFrame. The canonical preheader
    // only materializes and stores those same values, so its native form is a
    // single branch while retaining a bound label for verifier-proven CFGs.
    if (!emitter.bind(labels[0]) || !emitter.jump(labels[1]))
        return {.code = std::nullopt, .error = BaselineCompileError::emissionFailed};

    const auto emitReturn = [&emitter, &valueOffset](ir::ValueId value) -> bool
    {
        const auto returnOffset = valueOffset(value);
        return returnOffset && emitter.load(Register::rax, Register::r13, *returnOffset) &&
               emitter.addStackPointer(callAreaBytes) && emitter.pop(Register::r14) &&
               emitter.pop(Register::r13) && emitter.pop(Register::r12) && emitter.pop(Register::rbx) &&
               emitter.returnFromFunction();
    };

    for (std::size_t blockIndex = 1; blockIndex < function.blocks.size(); ++blockIndex)
    {
        const auto &block = function.blocks[blockIndex];
        if (block.id != blockIndex || !emitter.bind(labels[blockIndex]))
            return {.code = std::nullopt, .error = BaselineCompileError::emissionFailed};

        for (const auto &instruction : block.instructions)
        {
            switch (instruction.opcode)
            {
            case ir::Opcode::constantI64:
            {
                const auto resultOffset = valueOffset(instruction.result);
                if (!resultOffset)
                    return unsupported();
                if (!emitter.moveImmediate64(Register::rax, instruction.constant) ||
                    !emitter.store(Register::r13, *resultOffset, Register::rax))
                    return {.code = std::nullopt, .error = BaselineCompileError::emissionFailed};
                break;
            }
            case ir::Opcode::loadLocal:
            {
                const auto resultOffset = valueOffset(instruction.result);
                const auto slotOffset = localOffset(instruction.local);
                if (!resultOffset || !slotOffset || !emitter.load(Register::rax, Register::rbx, *slotOffset) ||
                    !emitter.store(Register::r13, *resultOffset, Register::rax))
                    return {.code = std::nullopt, .error = BaselineCompileError::emissionFailed};
                break;
            }
            case ir::Opcode::storeLocal:
            {
                const auto inputOffset = valueOffset(instruction.input);
                const auto slotOffset = localOffset(instruction.local);
                if (!inputOffset || !slotOffset || !emitter.load(Register::rax, Register::r13, *inputOffset) ||
                    !emitter.store(Register::rbx, *slotOffset, Register::rax))
                    return {.code = std::nullopt, .error = BaselineCompileError::emissionFailed};
                break;
            }
            case ir::Opcode::negateI64:
            {
                const auto resultOffset = valueOffset(instruction.result);
                const auto inputOffset = valueOffset(instruction.input);
                if (!resultOffset || !inputOffset || !emitter.load(Register::rax, Register::r13, *inputOffset) ||
                    !emitter.negate(Register::rax) || !emitter.store(Register::r13, *resultOffset, Register::rax))
                    return {.code = std::nullopt, .error = BaselineCompileError::emissionFailed};
                break;
            }
            case ir::Opcode::addI64:
            case ir::Opcode::subI64:
            case ir::Opcode::mulI64:
            {
                const auto resultOffset = valueOffset(instruction.result);
                const auto leftOffset = valueOffset(instruction.left);
                const auto rightOffset = valueOffset(instruction.right);
                if (!resultOffset || !leftOffset || !rightOffset || !emitter.load(Register::rax, Register::r13, *leftOffset) ||
                    !emitter.load(Register::r8, Register::r13, *rightOffset) ||
                    !emitBinary(emitter, instruction.opcode) ||
                    !emitter.store(Register::r13, *resultOffset, Register::rax))
                    return {.code = std::nullopt, .error = BaselineCompileError::emissionFailed};
                break;
            }
            case ir::Opcode::divI64:
            case ir::Opcode::remI64:
            {
                const auto resultOffset = valueOffset(instruction.result);
                const auto leftOffset = valueOffset(instruction.left);
                const auto rightOffset = valueOffset(instruction.right);
                const auto safeDivision = emitter.createLabel();
                if (!resultOffset || !leftOffset || !rightOffset ||
                    !emitter.load(Register::rax, Register::r13, *leftOffset) ||
                    !emitter.load(Register::r8, Register::r13, *rightOffset) ||
                    !emitter.test(Register::r8, Register::r8) ||
                    !emitter.jump(Condition::equal, divisionFailure) ||
                    !emitter.moveImmediate64(Register::r9, std::numeric_limits<std::int64_t>::min()) ||
                    !emitter.compare(Register::rax, Register::r9) ||
                    !emitter.jump(Condition::notEqual, safeDivision) ||
                    !emitter.moveImmediate64(Register::r9, -1) ||
                    !emitter.compare(Register::r8, Register::r9) ||
                    !emitter.jump(Condition::equal, divisionFailure) || !emitter.bind(safeDivision) ||
                    !emitter.signExtendRaxIntoRdx() || !emitter.signedDivide(Register::r8) ||
                    !emitter.store(Register::r13, *resultOffset,
                                   instruction.opcode == ir::Opcode::divI64 ? Register::rax
                                                                            : Register::rdx))
                    return {.code = std::nullopt, .error = BaselineCompileError::emissionFailed};
                break;
            }
            case ir::Opcode::equalI64:
            case ir::Opcode::notEqualI64:
            case ir::Opcode::lessI64:
            case ir::Opcode::lessEqualI64:
            case ir::Opcode::greaterI64:
            case ir::Opcode::greaterEqualI64:
            {
                const auto resultOffset = valueOffset(instruction.result);
                const auto leftOffset = valueOffset(instruction.left);
                const auto rightOffset = valueOffset(instruction.right);
                const auto condition = conditionFor(instruction.opcode);
                if (!resultOffset || !leftOffset || !rightOffset || !condition ||
                    !emitter.load(Register::rax, Register::r13, *leftOffset) ||
                    !emitter.load(Register::r8, Register::r13, *rightOffset) ||
                    !emitter.compare(Register::rax, Register::r8) ||
                    !emitter.moveImmediate64(Register::rax, 0) || !emitter.set(*condition, Register::rax) ||
                    !emitter.store(Register::r13, *resultOffset, Register::rax))
                    return {.code = std::nullopt, .error = BaselineCompileError::emissionFailed};
                break;
            }
            case ir::Opcode::callI64:
            {
                const auto resultOffset = valueOffset(instruction.result);
                if (!resultOffset || instruction.arguments.size() > maximumCallArgumentCount ||
                    instruction.arguments.size() >
                        static_cast<std::size_t>(std::numeric_limits<std::int64_t>::max()))
                    return unsupported();
                for (std::size_t index{}; index < instruction.arguments.size(); ++index)
                {
                    const auto argumentOffset = valueOffset(instruction.arguments[index]);
                    const auto scratchOffset = static_cast<std::int32_t>(index * 8U);
                    if (!argumentOffset || !emitter.load(Register::rax, Register::r13, *argumentOffset) ||
                        !emitter.store(Register::r14, scratchOffset, Register::rax))
                        return {.code = std::nullopt, .error = BaselineCompileError::emissionFailed};
                }
                if (!emitter.move(Register::rcx, Register::r12) ||
                    !emitter.moveImmediate64(Register::rdx,
                                              static_cast<std::int64_t>(instruction.callee)) ||
                    !emitter.move(Register::r8, Register::r14) ||
                    !emitter.moveImmediate64(Register::r9,
                                              static_cast<std::int64_t>(instruction.arguments.size())) ||
                    !emitter.loadEffectiveAddress(Register::rax, Register::r13, *resultOffset) ||
                    !emitter.store(Register::rsp, 32, Register::rax) ||
                    !emitter.load(Register::rax, Register::r12, nativeFrameCallBridgeOffset) ||
                    !emitter.call(Register::rax) || !emitter.test(Register::rax, Register::rax) ||
                    !emitter.jump(Condition::notEqual, callFailure))
                    return {.code = std::nullopt, .error = BaselineCompileError::emissionFailed};
                break;
            }
            default:
                return unsupported();
            }
        }

        switch (block.terminator.kind)
        {
        case ir::TerminatorKind::branch:
            if (block.terminator.trueTarget >= labels.size() || !emitter.jump(labels[block.terminator.trueTarget]))
                return {.code = std::nullopt, .error = BaselineCompileError::emissionFailed};
            break;
        case ir::TerminatorKind::branchIfFalse:
        {
            const auto conditionOffset = valueOffset(block.terminator.condition);
            if (!conditionOffset || block.terminator.falseTarget >= labels.size() ||
                block.terminator.trueTarget >= labels.size() ||
                !emitter.load(Register::rax, Register::r13, *conditionOffset) ||
                !emitter.test(Register::rax, Register::rax) ||
                !emitter.jump(Condition::equal, labels[block.terminator.falseTarget]) ||
                !emitter.jump(labels[block.terminator.trueTarget]))
                return {.code = std::nullopt, .error = BaselineCompileError::emissionFailed};
            break;
        }
        case ir::TerminatorKind::returnValue:
            if (!emitReturn(block.terminator.value))
                return {.code = std::nullopt, .error = BaselineCompileError::emissionFailed};
            break;
        default:
            return unsupported();
        }
    }
    if (!emitter.bind(divisionFailure) ||
        !emitter.moveImmediate64(Register::rax,
                                 static_cast<std::int64_t>(NativeFrameError::invalidI64Division)) ||
        !emitter.store(Register::r12, nativeFrameErrorCodeOffset, Register::rax) ||
        !emitter.moveImmediate64(Register::rax, 0) || !emitter.addStackPointer(callAreaBytes) ||
        !emitter.pop(Register::r14) || !emitter.pop(Register::r13) ||
        !emitter.pop(Register::r12) || !emitter.pop(Register::rbx) || !emitter.returnFromFunction() ||
        !emitter.bind(callFailure) || !emitter.moveImmediate64(Register::rax, 0) ||
        !emitter.addStackPointer(callAreaBytes) || !emitter.pop(Register::r14) ||
        !emitter.pop(Register::r13) || !emitter.pop(Register::r12) ||
        !emitter.pop(Register::rbx) || !emitter.returnFromFunction())
        return {.code = std::nullopt, .error = BaselineCompileError::emissionFailed};
    auto code = emitter.finalize();
    if (!code.code)
        return {.code = std::nullopt, .error = BaselineCompileError::emissionFailed};
    return {.code = std::move(code.code),
            .error = BaselineCompileError::none,
            .frameRequirements = {.spillCount = function.valueTypes.size(),
                                  .callArgumentCapacity = maximumCallArgumentCount}};
}
} // namespace ember::jit::x64
