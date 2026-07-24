#include "ember/ir/bytecode_lowerer.hpp"

#include <algorithm>
#include <deque>
#include <iterator>
#include <string>
#include <utility>
#include <variant>

namespace ember::ir
{
namespace
{
[[nodiscard]] support::Diagnostic loweringError(LoweringFailure failure, std::string message)
{
    const char *code = "E6004";
    if (failure == LoweringFailure::unsupported)
        code = "E6002";
    else if (failure == LoweringFailure::invalidInput)
        code = "E6003";
    return {.stage = support::DiagnosticStage::jit,
            .severity = support::DiagnosticSeverity::error,
            .code = code,
            .message = std::move(message),
            .primarySpan = {}};
}

[[nodiscard]] std::string atPc(semantic::FunctionId id, std::size_t pc, std::string message)
{
    return "function #" + std::to_string(id) + ", pc " + std::to_string(pc) + ": " +
           std::move(message);
}

[[nodiscard]] bool isI64SubsetSignature(const semantic::FunctionSignature &signature)
{
    return signature.returnType == semantic::Type::i64 &&
           std::ranges::all_of(signature.parameterTypes,
                               [](semantic::Type type) { return type == semantic::Type::i64; });
}

[[nodiscard]] std::optional<Opcode> lowerOpcode(bytecode::Opcode opcode)
{
    using BytecodeOpcode = bytecode::Opcode;
    switch (opcode)
    {
    case BytecodeOpcode::negateI64:
        return Opcode::negateI64;
    case BytecodeOpcode::addI64:
        return Opcode::addI64;
    case BytecodeOpcode::subI64:
        return Opcode::subI64;
    case BytecodeOpcode::mulI64:
        return Opcode::mulI64;
    case BytecodeOpcode::divI64:
        return Opcode::divI64;
    case BytecodeOpcode::remI64:
        return Opcode::remI64;
    case BytecodeOpcode::equalI64:
        return Opcode::equalI64;
    case BytecodeOpcode::notEqualI64:
        return Opcode::notEqualI64;
    case BytecodeOpcode::lessI64:
        return Opcode::lessI64;
    case BytecodeOpcode::lessEqualI64:
        return Opcode::lessEqualI64;
    case BytecodeOpcode::greaterI64:
        return Opcode::greaterI64;
    case BytecodeOpcode::greaterEqualI64:
        return Opcode::greaterEqualI64;
    default:
        return std::nullopt;
    }
}

[[nodiscard]] bool isComparison(Opcode opcode) noexcept
{
    return opcode == Opcode::equalI64 || opcode == Opcode::notEqualI64 ||
           opcode == Opcode::lessI64 || opcode == Opcode::lessEqualI64 ||
           opcode == Opcode::greaterI64 || opcode == Opcode::greaterEqualI64;
}
} // namespace

LoweringResult Lowerer::lower(const bytecode::VerifiedProgram &program,
                              semantic::FunctionId functionId) const
{
    const auto &functions = program.program().functions;
    const auto found = std::find_if(functions.begin(), functions.end(),
                                    [functionId](const bytecode::Function &function)
                                    { return function.id == functionId; });
    if (found == functions.end())
    {
        return {.function = std::nullopt,
                .failure = LoweringFailure::invalidInput,
                .diagnostics = {loweringError(LoweringFailure::invalidInput,
                                              "IR target function does not exist")}};
    }
    const auto &source = *found;
    if (source.kind != semantic::FunctionKind::user || !isI64SubsetSignature(source.signature) ||
        !std::ranges::all_of(source.localTypes,
                             [](semantic::Type type) { return type == semantic::Type::i64; }))
    {
        return {.function = std::nullopt,
                .failure = LoweringFailure::unsupported,
                .diagnostics = {loweringError(LoweringFailure::unsupported,
                                              "function #" + std::to_string(source.id) +
                                                  " is outside the native i64 subset")}};
    }
    if (source.code.empty())
    {
        return {.function = std::nullopt,
                .failure = LoweringFailure::internalInvariant,
                .diagnostics = {loweringError(
                    LoweringFailure::internalInvariant,
                    "function #" + std::to_string(source.id) +
                        " has an empty body despite the VerifiedProgram contract")}};
    }

    std::vector<bool> reachable(source.code.size(), false);
    std::deque<std::size_t> reachableWork;
    reachable[0] = true;
    reachableWork.push_back(0);
    while (!reachableWork.empty())
    {
        const auto pc = reachableWork.front();
        reachableWork.pop_front();
        const auto enqueue = [&reachable, &reachableWork](std::size_t target)
        {
            if (!reachable[target])
            {
                reachable[target] = true;
                reachableWork.push_back(target);
            }
        };
        const auto &instruction = source.code[pc];
        if (instruction.opcode == bytecode::Opcode::jump)
            enqueue(instruction.operand);
        else if (instruction.opcode == bytecode::Opcode::jumpIfFalse)
        {
            enqueue(instruction.operand);
            enqueue(pc + 1);
        }
        else if (instruction.opcode != bytecode::Opcode::returnValue &&
                 instruction.opcode != bytecode::Opcode::returnVoid)
            enqueue(pc + 1);
    }

    std::vector<std::size_t> leaders{0};
    for (std::size_t pc = 0; pc < source.code.size(); ++pc)
    {
        if (!reachable[pc])
            continue;
        const auto opcode = source.code[pc].opcode;
        if (opcode == bytecode::Opcode::jump || opcode == bytecode::Opcode::jumpIfFalse)
            leaders.push_back(source.code[pc].operand);
        if ((opcode == bytecode::Opcode::jump || opcode == bytecode::Opcode::jumpIfFalse ||
             opcode == bytecode::Opcode::returnValue || opcode == bytecode::Opcode::returnVoid) &&
            pc + 1 < source.code.size() && reachable[pc + 1])
            leaders.push_back(pc + 1);
    }
    std::ranges::sort(leaders);
    leaders.erase(std::unique(leaders.begin(), leaders.end()), leaders.end());
    std::vector<std::optional<BlockId>> blockAt(source.code.size());
    for (std::size_t index = 0; index < leaders.size(); ++index)
        blockAt[leaders[index]] = static_cast<BlockId>(index + 1);

    Function function{.id = source.id,
                      .signature = source.signature,
                      .localTypes = source.localTypes,
                      .valueTypes = {},
                      .blocks = {}};
    const auto makeValue = [&function](semantic::Type type)
    {
        const auto value = static_cast<ValueId>(function.valueTypes.size());
        function.valueTypes.push_back(type);
        return value;
    };
    const auto fail = [](LoweringFailure failure, std::string message) -> LoweringResult
    {
        return {.function = std::nullopt,
                .failure = failure,
                .diagnostics = {loweringError(failure, std::move(message))}};
    };

    // The preheader runs exactly once. Bytecode pc 0 deliberately starts in
    // b1, so a loop backedge to pc 0 cannot reinitialize parameter locals.
    BasicBlock preheader{.id = 0, .instructions = {}, .terminator = {}};
    std::vector<ValueId> parameterValues;
    parameterValues.reserve(source.signature.parameterTypes.size());
    for (std::size_t parameter = 0; parameter < source.signature.parameterTypes.size(); ++parameter)
    {
        const auto value = makeValue(source.signature.parameterTypes[parameter]);
        parameterValues.push_back(value);
        preheader.instructions.push_back(Instruction::parameter(value, static_cast<LocalId>(parameter)));
    }
    for (std::size_t parameter = 0; parameter < source.signature.parameterTypes.size(); ++parameter)
        preheader.instructions.push_back(
            Instruction::storeLocal(static_cast<LocalId>(parameter), parameterValues[parameter]));
    preheader.terminator = Terminator::branch(1);
    function.blocks.push_back(std::move(preheader));

    for (std::size_t blockIndex = 0; blockIndex < leaders.size(); ++blockIndex)
    {
        const auto begin = leaders[blockIndex];
        const auto end = blockIndex + 1 < leaders.size() ? leaders[blockIndex + 1] : source.code.size();
        auto lastReachable = end;
        while (lastReachable > begin && !reachable[lastReachable - 1])
            --lastReachable;
        if (lastReachable == begin)
            return fail(LoweringFailure::internalInvariant,
                        "function #" + std::to_string(source.id) +
                            " has an empty reachable basic block");

        BasicBlock block{.id = static_cast<BlockId>(blockIndex + 1),
                         .instructions = {},
                         .terminator = {}};
        bool terminated{};
        std::vector<ValueId> stack;
        const auto pop = [&stack](std::size_t) -> std::optional<ValueId>
        {
            if (stack.empty())
                return std::nullopt;
            const auto value = stack.back();
            stack.pop_back();
            return value;
        };
        for (std::size_t pc = begin; pc < end; ++pc)
        {
            if (!reachable[pc])
                continue;
            const auto &instruction = source.code[pc];
            const bool finalInstruction = pc + 1 == lastReachable;
            if ((instruction.opcode == bytecode::Opcode::jump ||
                 instruction.opcode == bytecode::Opcode::jumpIfFalse ||
                 instruction.opcode == bytecode::Opcode::returnValue ||
                 instruction.opcode == bytecode::Opcode::returnVoid) &&
                !finalInstruction)
                return fail(LoweringFailure::internalInvariant,
                            atPc(source.id, pc, "verified bytecode has a non-final terminator"));

            if (instruction.opcode == bytecode::Opcode::constant)
            {
                if (!instruction.value || !std::holds_alternative<std::int64_t>(*instruction.value))
                    return fail(LoweringFailure::unsupported,
                                atPc(source.id, pc, "non-i64 constants are outside the native subset"));
                const auto value = makeValue(semantic::Type::i64);
                block.instructions.push_back(
                    Instruction::constantI64(value, std::get<std::int64_t>(*instruction.value)));
                stack.push_back(value);
                continue;
            }
            if (instruction.opcode == bytecode::Opcode::load)
            {
                const auto value = makeValue(semantic::Type::i64);
                block.instructions.push_back(Instruction::loadLocal(value, instruction.operand));
                stack.push_back(value);
                continue;
            }
            if (instruction.opcode == bytecode::Opcode::store)
            {
                const auto value = pop(pc);
                if (!value)
                    return fail(LoweringFailure::internalInvariant,
                                atPc(source.id, pc, "verified bytecode underflowed the lowering stack"));
                block.instructions.push_back(Instruction::storeLocal(instruction.operand, *value));
                continue;
            }
            if (instruction.opcode == bytecode::Opcode::pop)
            {
                const auto value = pop(pc);
                if (!value)
                    return fail(LoweringFailure::internalInvariant,
                                atPc(source.id, pc, "verified bytecode underflowed the lowering stack"));
                if (function.valueTypes[*value] != semantic::Type::i64)
                    return fail(LoweringFailure::unsupported,
                                atPc(source.id, pc, "first-class non-i64 values are outside the native subset"));
                continue;
            }
            if (const auto lowered = lowerOpcode(instruction.opcode))
            {
                const auto right = pop(pc);
                if (!right)
                    return fail(LoweringFailure::internalInvariant,
                                atPc(source.id, pc, "verified bytecode underflowed the lowering stack"));
                if (*lowered == Opcode::negateI64)
                {
                    const auto result = makeValue(semantic::Type::i64);
                    block.instructions.push_back(Instruction::negateI64(result, *right));
                    stack.push_back(result);
                    continue;
                }
                const auto left = pop(pc);
                if (!left)
                    return fail(LoweringFailure::internalInvariant,
                                atPc(source.id, pc, "verified bytecode underflowed the lowering stack"));
                const auto result = makeValue(isComparison(*lowered) ? semantic::Type::boolean
                                                                       : semantic::Type::i64);
                block.instructions.push_back(Instruction::binaryI64(*lowered, result, *left, *right));
                stack.push_back(result);
                continue;
            }
            if (instruction.opcode == bytecode::Opcode::jump)
            {
                if (!blockAt[instruction.operand])
                    return fail(LoweringFailure::internalInvariant,
                                atPc(source.id, pc, "verified bytecode has an invalid jump target"));
                if (!stack.empty())
                    return fail(LoweringFailure::unsupported,
                                atPc(source.id, pc,
                                     "non-empty operand stack at a CFG edge is outside the native subset"));
                block.terminator = Terminator::branch(*blockAt[instruction.operand]);
                terminated = true;
                continue;
            }
            if (instruction.opcode == bytecode::Opcode::jumpIfFalse)
            {
                const auto condition = pop(pc);
                if (!condition || function.valueTypes[*condition] != semantic::Type::boolean)
                    return fail(LoweringFailure::internalInvariant,
                                atPc(source.id, pc, "verified bytecode has an invalid conditional jump state"));
                if (!blockAt[instruction.operand] || blockIndex + 1 >= leaders.size())
                    return fail(LoweringFailure::internalInvariant,
                                atPc(source.id, pc, "verified bytecode has an invalid conditional jump target"));
                if (!stack.empty())
                    return fail(LoweringFailure::unsupported,
                                atPc(source.id, pc,
                                     "non-empty operand stack at a CFG edge is outside the native subset"));
                block.terminator = Terminator::branchIfFalse(
                    *condition, *blockAt[instruction.operand], static_cast<BlockId>(blockIndex + 2));
                terminated = true;
                continue;
            }
            if (instruction.opcode == bytecode::Opcode::returnValue)
            {
                const auto value = pop(pc);
                if (!value || !stack.empty())
                    return fail(LoweringFailure::internalInvariant,
                                atPc(source.id, pc, "verified bytecode returns with an invalid stack"));
                block.terminator = Terminator::returnValue(*value);
                terminated = true;
                continue;
            }
            return fail(LoweringFailure::unsupported,
                        atPc(source.id, pc, "bytecode opcode is outside the native i64 subset"));
        }
        if (!terminated)
        {
            if (blockIndex + 1 >= leaders.size())
                return fail(LoweringFailure::internalInvariant,
                            "function #" + std::to_string(source.id) +
                                " has an invalid fall-through target");
            if (!stack.empty())
                return fail(LoweringFailure::unsupported,
                            atPc(source.id, lastReachable - 1,
                                 "non-empty operand stack at a CFG edge is outside the native subset"));
            block.terminator = Terminator::branch(static_cast<BlockId>(blockIndex + 2));
        }
        function.blocks.push_back(std::move(block));
    }

    auto verified = Verifier{}.verify(std::move(function));
    if (verified.function)
        return {.function = std::move(verified.function), .failure = std::nullopt, .diagnostics = {}};
    std::vector<support::Diagnostic> diagnostics;
    diagnostics.push_back(loweringError(
        LoweringFailure::internalInvariant,
        "lowering produced IR that violates the verifier contract"));
    diagnostics.insert(diagnostics.end(), std::make_move_iterator(verified.diagnostics.begin()),
                       std::make_move_iterator(verified.diagnostics.end()));
    return {.function = std::nullopt,
            .failure = LoweringFailure::internalInvariant,
            .diagnostics = std::move(diagnostics)};
}
} // namespace ember::ir
