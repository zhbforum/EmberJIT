#include "ember/ir/optimization.hpp"

#include "ember/support/i64_semantics.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

namespace ember::ir
{
namespace
{
struct ConstantFacts
{
    explicit ConstantFacts(std::size_t valueCount) : i64(valueCount), boolean(valueCount) {}

    std::vector<std::optional<std::int64_t>> i64;
    std::vector<std::optional<bool>> boolean;
};

struct LocalConstant
{
    std::size_t epoch{};
    std::optional<std::int64_t> value;
};

[[nodiscard]] bool isArithmetic(Opcode opcode) noexcept
{
    return opcode == Opcode::addI64 || opcode == Opcode::subI64 || opcode == Opcode::mulI64;
}

[[nodiscard]] bool isComparison(Opcode opcode) noexcept
{
    return opcode == Opcode::equalI64 || opcode == Opcode::notEqualI64 ||
           opcode == Opcode::lessI64 || opcode == Opcode::lessEqualI64 ||
           opcode == Opcode::greaterI64 || opcode == Opcode::greaterEqualI64;
}

[[nodiscard]] bool isPure(Opcode opcode) noexcept
{
    return opcode == Opcode::constantI64 || opcode == Opcode::loadLocal ||
           opcode == Opcode::negateI64 || isArithmetic(opcode) || isComparison(opcode);
}

[[nodiscard]] std::optional<std::int64_t>
evaluateI64Arithmetic(Opcode opcode, std::int64_t left, std::int64_t right) noexcept
{
    switch (opcode)
    {
    case Opcode::addI64:
        return support::i64::add(left, right);
    case Opcode::subI64:
        return support::i64::subtract(left, right);
    case Opcode::mulI64:
        return support::i64::multiply(left, right);
    default:
        return std::nullopt;
    }
}

[[nodiscard]] std::optional<bool>
evaluateI64Comparison(Opcode opcode, std::int64_t left, std::int64_t right) noexcept
{
    switch (opcode)
    {
    case Opcode::equalI64:
        return left == right;
    case Opcode::notEqualI64:
        return left != right;
    case Opcode::lessI64:
        return left < right;
    case Opcode::lessEqualI64:
        return left <= right;
    case Opcode::greaterI64:
        return left > right;
    case Opcode::greaterEqualI64:
        return left >= right;
    default:
        return std::nullopt;
    }
}

[[nodiscard]] ConstantFacts collectConstantFacts(const Function &function)
{
    ConstantFacts facts(function.valueTypes.size());
    for (const auto &block : function.blocks)
    {
        for (const auto &instruction : block.instructions)
        {
            if (instruction.opcode == Opcode::constantI64)
            {
                facts.i64[instruction.result] = instruction.constant;
                continue;
            }
            if (instruction.opcode == Opcode::negateI64 && facts.i64[instruction.input])
            {
                facts.i64[instruction.result] = support::i64::negate(*facts.i64[instruction.input]);
                continue;
            }
            if (instruction.left == noValue || instruction.right == noValue ||
                !facts.i64[instruction.left] || !facts.i64[instruction.right])
                continue;
            if (const auto value = evaluateI64Arithmetic(instruction.opcode,
                                                         *facts.i64[instruction.left],
                                                         *facts.i64[instruction.right]))
                facts.i64[instruction.result] = *value;
            else if (const auto comparison = evaluateI64Comparison(instruction.opcode,
                                                                    *facts.i64[instruction.left],
                                                                    *facts.i64[instruction.right]))
                facts.boolean[instruction.result] = *comparison;
        }
    }
    return facts;
}

void remapUse(ValueId &value, const std::vector<ValueId> &newIds)
{
    if (value != noValue)
        value = newIds[value];
}

[[nodiscard]] Function compactValues(Function function)
{
    std::vector<ValueId> newIds(function.valueTypes.size(), noValue);
    std::vector<semantic::Type> valueTypes;
    valueTypes.reserve(function.valueTypes.size());
    for (const auto &block : function.blocks)
    {
        for (const auto &instruction : block.instructions)
        {
            if (instruction.result == noValue)
                continue;
            newIds[instruction.result] = static_cast<ValueId>(valueTypes.size());
            valueTypes.push_back(function.valueTypes[instruction.result]);
        }
    }

    for (auto &block : function.blocks)
    {
        for (auto &instruction : block.instructions)
        {
            remapUse(instruction.result, newIds);
            forEachUse(instruction, [&newIds](ValueId &value) { remapUse(value, newIds); });
        }
        remapUse(block.terminator.condition, newIds);
        remapUse(block.terminator.value, newIds);
    }
    function.valueTypes = std::move(valueTypes);
    return function;
}

[[nodiscard]] VerifyResult verify(Function function)
{
    return Verifier{}.verify(std::move(function));
}

[[nodiscard]] OptimizationResult failed(OptimizationPass pass, VerifyResult result)
{
    return {.function = std::nullopt,
            .failedPass = pass,
            .diagnostics = std::move(result.diagnostics)};
}

[[nodiscard]] OptimizationResult successful(VerifyResult result)
{
    return {.function = std::move(result.function), .failedPass = std::nullopt, .diagnostics = {}};
}
} // namespace

const char *optimizationPassName(OptimizationPass pass) noexcept
{
    switch (pass)
    {
    case OptimizationPass::constantFolding:
        return "constant-folding";
    case OptimizationPass::constantPropagation:
        return "constant-propagation";
    case OptimizationPass::cfgSimplification:
        return "cfg-simplification";
    case OptimizationPass::deadCodeElimination:
        return "dead-code-elimination";
    }
    return "unknown";
}

VerifyResult ConstantFoldingPass::run(const VerifiedFunction &input) const
{
    Function function = input.function();
    const auto facts = collectConstantFacts(function);
    for (auto &block : function.blocks)
    {
        for (auto &instruction : block.instructions)
        {
            if (instruction.opcode == Opcode::negateI64 && facts.i64[instruction.result])
                instruction = Instruction::constantI64(instruction.result, *facts.i64[instruction.result]);
            else if (isArithmetic(instruction.opcode) && facts.i64[instruction.result])
                instruction = Instruction::constantI64(instruction.result, *facts.i64[instruction.result]);
        }
    }
    return verify(std::move(function));
}

VerifyResult ConstantPropagationPass::run(const VerifiedFunction &input) const
{
    Function function = input.function();
    std::vector<std::optional<std::int64_t>> values(function.valueTypes.size());
    std::vector<LocalConstant> locals(function.localTypes.size());
    std::size_t epoch{};
    for (auto &block : function.blocks)
    {
        if (epoch == std::numeric_limits<std::size_t>::max())
        {
            for (auto &local : locals)
                local.epoch = 0;
            epoch = 1;
        }
        else
        {
            ++epoch;
        }
        for (auto &instruction : block.instructions)
        {
            if (instruction.opcode == Opcode::constantI64)
                values[instruction.result] = instruction.constant;
            else if (instruction.opcode == Opcode::loadLocal && locals[instruction.local].epoch == epoch &&
                     locals[instruction.local].value)
            {
                const auto result = instruction.result;
                const auto constant = *locals[instruction.local].value;
                instruction = Instruction::constantI64(result, constant);
                values[result] = constant;
            }
            else if (instruction.opcode == Opcode::storeLocal)
            {
                locals[instruction.local] = {.epoch = epoch, .value = values[instruction.input]};
            }
            else if (instruction.result != noValue)
            {
                values[instruction.result] = std::nullopt;
            }
        }
    }
    return verify(std::move(function));
}

auto CfgSimplificationPass::run(const VerifiedFunction &input) const -> VerifyResult
{
    Function function = input.function();
    const auto facts = collectConstantFacts(function);
    for (auto &block : function.blocks)
    {
        if (block.terminator.kind == TerminatorKind::branchIfFalse &&
            facts.boolean[block.terminator.condition])
        {
            block.terminator = Terminator::branch(*facts.boolean[block.terminator.condition]
                                                      ? block.terminator.trueTarget
                                                      : block.terminator.falseTarget);
        }
        else if (block.terminator.kind == TerminatorKind::branchIfFalse &&
                 block.terminator.falseTarget == block.terminator.trueTarget)
        {
            block.terminator = Terminator::branch(block.terminator.trueTarget);
        }
    }

    std::vector<bool> reachable(function.blocks.size(), false);
    std::deque<BlockId> work;
    reachable[0] = true;
    work.push_back(0);
    while (!work.empty())
    {
        const auto blockId = work.front();
        work.pop_front();
        const auto enqueue = [&reachable, &work](BlockId target)
        {
            if (!reachable[target])
            {
                reachable[target] = true;
                work.push_back(target);
            }
        };
        const auto &terminator = function.blocks[blockId].terminator;
        if (terminator.kind == TerminatorKind::branch)
            enqueue(terminator.trueTarget);
        else if (terminator.kind == TerminatorKind::branchIfFalse)
        {
            enqueue(terminator.falseTarget);
            enqueue(terminator.trueTarget);
        }
    }

    std::vector<BlockId> newIds(function.blocks.size(), noBlock);
    std::vector<BasicBlock> blocks;
    blocks.reserve(function.blocks.size());
    for (std::size_t index{}; index < function.blocks.size(); ++index)
    {
        if (!reachable[index])
            continue;
        newIds[index] = static_cast<BlockId>(blocks.size());
        blocks.push_back(std::move(function.blocks[index]));
    }
    for (std::size_t index{}; index < blocks.size(); ++index)
    {
        auto &block = blocks[index];
        block.id = static_cast<BlockId>(index);
        if (block.terminator.kind == TerminatorKind::branch)
            block.terminator.trueTarget = newIds[block.terminator.trueTarget];
        else if (block.terminator.kind == TerminatorKind::branchIfFalse)
        {
            block.terminator.falseTarget = newIds[block.terminator.falseTarget];
            block.terminator.trueTarget = newIds[block.terminator.trueTarget];
        }
    }
    function.blocks = std::move(blocks);
    return verify(compactValues(std::move(function)));
}

VerifyResult DeadCodeEliminationPass::run(const VerifiedFunction &input) const
{
    Function function = input.function();
    std::vector<bool> live(function.valueTypes.size(), false);
    for (auto &block : function.blocks)
    {
        if (block.terminator.kind == TerminatorKind::branchIfFalse)
            live[block.terminator.condition] = true;
        else if (block.terminator.kind == TerminatorKind::returnValue)
            live[block.terminator.value] = true;

        std::vector<Instruction> kept;
        kept.reserve(block.instructions.size());
        for (auto iterator = block.instructions.rbegin(); iterator != block.instructions.rend(); ++iterator)
        {
            const auto &instruction = *iterator;
            const bool resultLive = instruction.result != noValue && live[instruction.result];
            // Parameters remain even when unused to preserve the canonical
            // entry prefix required by the verifier.
            const bool retain = !isPure(instruction.opcode) || resultLive ||
                                instruction.opcode == Opcode::parameter;
            if (!retain)
                continue;
            if (instruction.result != noValue)
                live[instruction.result] = false;
            forEachUse(instruction, [&live](ValueId value) { live[value] = true; });
            kept.push_back(instruction);
        }
        std::ranges::reverse(kept);
        block.instructions = std::move(kept);
    }
    return verify(compactValues(std::move(function)));
}

OptimizationResult OptimizationPipeline::run(const VerifiedFunction &input) const
{
    auto folded = ConstantFoldingPass{}.run(input);
    if (!folded.function)
        return failed(OptimizationPass::constantFolding, std::move(folded));
    auto propagated = ConstantPropagationPass{}.run(*folded.function);
    if (!propagated.function)
        return failed(OptimizationPass::constantPropagation, std::move(propagated));
    auto refolded = ConstantFoldingPass{}.run(*propagated.function);
    if (!refolded.function)
        return failed(OptimizationPass::constantFolding, std::move(refolded));
    auto simplified = CfgSimplificationPass{}.run(*refolded.function);
    if (!simplified.function)
        return failed(OptimizationPass::cfgSimplification, std::move(simplified));
    auto eliminated = DeadCodeEliminationPass{}.run(*simplified.function);
    if (!eliminated.function)
        return failed(OptimizationPass::deadCodeElimination, std::move(eliminated));
    return successful(std::move(eliminated));
}
} // namespace ember::ir
