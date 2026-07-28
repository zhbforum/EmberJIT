#include "ember/ir/verifier.hpp"

#include "ember/bytecode/bytecode.hpp"

#include <algorithm>
#include <deque>
#include <string>
#include <string_view>

namespace ember::ir
{
namespace
{
struct DefinitionLocation
{
    BlockId block;
    std::size_t instruction;
};

[[nodiscard]] support::Diagnostic error(std::string message)
{
    return {.stage = support::DiagnosticStage::jit,
            .severity = support::DiagnosticSeverity::error,
            .code = "E6001",
            .message = std::move(message),
            .primarySpan = {}};
}

[[nodiscard]] bool validType(semantic::Type type) noexcept
{
    return type == semantic::Type::i64 || type == semantic::Type::f64 ||
           type == semantic::Type::boolean || type == semantic::Type::voidType;
}

[[nodiscard]] bool validValueType(semantic::Type type) noexcept
{
    return validType(type) && type != semantic::Type::voidType;
}

[[nodiscard]] bool isComparison(Opcode opcode) noexcept
{
    return opcode == Opcode::equalI64 || opcode == Opcode::notEqualI64 ||
           opcode == Opcode::lessI64 || opcode == Opcode::lessEqualI64 ||
           opcode == Opcode::greaterI64 || opcode == Opcode::greaterEqualI64;
}

[[nodiscard]] bool isF64Comparison(Opcode opcode) noexcept
{
    return opcode == Opcode::equalF64 || opcode == Opcode::notEqualF64 ||
           opcode == Opcode::lessF64 || opcode == Opcode::lessEqualF64 ||
           opcode == Opcode::greaterF64 || opcode == Opcode::greaterEqualF64;
}

[[nodiscard]] bool isF64Binary(Opcode opcode) noexcept
{
    return opcode == Opcode::addF64 || opcode == Opcode::subF64 ||
           opcode == Opcode::mulF64 || opcode == Opcode::divF64 || isF64Comparison(opcode);
}

[[nodiscard]] bool isBoolBinary(Opcode opcode) noexcept
{
    return opcode == Opcode::equalBool || opcode == Opcode::notEqualBool;
}

[[nodiscard]] bool isBinaryI64(Opcode opcode) noexcept
{
    return opcode == Opcode::addI64 || opcode == Opcode::subI64 ||
           opcode == Opcode::mulI64 || opcode == Opcode::divI64 ||
           opcode == Opcode::remI64 || isComparison(opcode);
}
} // namespace

CallTargetTable CallTargetTable::fromVerifiedProgram(const bytecode::VerifiedProgram &program)
{
    std::vector<CallTarget> targets;
    targets.reserve(program.program().functions.size());
    for (const auto &function : program.program().functions)
        targets.push_back(
            {.id = function.id, .kind = function.kind, .signature = function.signature});
    return CallTargetTable{std::move(targets)};
}

const CallTarget *CallTargetTable::find(semantic::FunctionId id) const noexcept
{
    const auto found = std::ranges::find(targets_, id, &CallTarget::id);
    return found == targets_.end() ? nullptr : &*found;
}

VerifyResult Verifier::verify(Function function, const CallTargetTable &callTargets) const
{
    std::vector<support::Diagnostic> diagnostics;
    const auto report = [&diagnostics, id = function.id](std::string message)
    { diagnostics.push_back(error("function #" + std::to_string(id) + ": " + std::move(message))); };
    const auto validSignature = [](const semantic::FunctionSignature &signature)
    {
        return validType(signature.returnType) &&
               std::ranges::all_of(signature.parameterTypes,
                                   [](semantic::Type type) { return validValueType(type); });
    };

    if (!validSignature(function.signature) || function.blocks.empty())
        report("has an invalid signature or no entry block");
    if (!std::ranges::all_of(function.localTypes,
                             [](semantic::Type type) { return validValueType(type); }) ||
        !std::ranges::all_of(function.valueTypes,
                             [](semantic::Type type) { return validValueType(type); }))
        report("has an invalid local or virtual-register type");
    if (function.localTypes.size() < function.signature.parameterTypes.size())
        report("has fewer locals than parameters");
    for (std::size_t index = 0;
         index < std::min(function.localTypes.size(), function.signature.parameterTypes.size()); ++index)
    {
        if (function.localTypes[index] != function.signature.parameterTypes[index])
            report("parameter/local layout does not match the signature");
    }
    for (std::size_t index = 0; index < function.blocks.size(); ++index)
        if (function.blocks[index].id != index)
            report("block ids must be dense and match block order");
    if (!diagnostics.empty())
        return {.function = std::nullopt, .diagnostics = std::move(diagnostics)};

    std::vector<std::optional<DefinitionLocation>> definitions(function.valueTypes.size());
    std::size_t nextValue{};
    std::size_t parameterCount{};
    const auto define = [&](ValueId value, semantic::Type type, BlockId block, std::size_t instruction,
                            std::string_view role)
    {
        if (value != nextValue || value >= function.valueTypes.size())
        {
            report(std::string{role} + " must define the next dense virtual register");
            return;
        }
        if (function.valueTypes[value] != type)
        {
            report(std::string{role} + " defines a virtual register with the wrong type");
            return;
        }
        definitions[value] = DefinitionLocation{.block = block, .instruction = instruction};
        ++nextValue;
    };
    const auto hasNoCallOperands = [](const Instruction &instruction)
    {
        return instruction.callee == noFunction &&
               instruction.calleeKind == semantic::FunctionKind::user && instruction.arguments.empty();
    };
    const auto canonicalNoValues = [&hasNoCallOperands](const Instruction &instruction)
    {
        return instruction.input == noValue && instruction.left == noValue &&
               instruction.right == noValue && hasNoCallOperands(instruction);
    };

    // Phase 1: all blocks receive full structural validation before any
    // reachability-dependent dataflow. This makes malformed dead code fail closed.
    for (const auto &block : function.blocks)
    {
        bool parameterPrefix = block.id == 0;
        for (std::size_t index = 0; index < block.instructions.size(); ++index)
        {
            const auto &instruction = block.instructions[index];
            switch (instruction.opcode)
            {
            case Opcode::parameter:
                if (!parameterPrefix || parameterCount >= function.signature.parameterTypes.size() ||
                    instruction.local != parameterCount || !canonicalNoValues(instruction) ||
                    instruction.constant != 0)
                    report("parameter has a non-canonical encoding or placement");
                define(instruction.result,
                       parameterCount < function.signature.parameterTypes.size()
                           ? function.signature.parameterTypes[parameterCount]
                           : semantic::Type::i64,
                       block.id, index, "parameter");
                ++parameterCount;
                break;
            case Opcode::constantI64:
                if (!canonicalNoValues(instruction) || instruction.local != noLocal)
                    report("constant has non-canonical unused operands");
                define(instruction.result, semantic::Type::i64, block.id, index, "constant");
                parameterPrefix = false;
                break;
            case Opcode::constantF64:
                if (!canonicalNoValues(instruction) || instruction.local != noLocal)
                    report("constant has non-canonical unused operands");
                define(instruction.result, semantic::Type::f64, block.id, index, "constant");
                parameterPrefix = false;
                break;
            case Opcode::constantBool:
                if (!canonicalNoValues(instruction) || instruction.local != noLocal ||
                    (instruction.constant != 0 && instruction.constant != 1))
                    report("boolean constant has a non-canonical encoding");
                define(instruction.result, semantic::Type::boolean, block.id, index, "constant");
                parameterPrefix = false;
                break;
            case Opcode::loadLocal:
                if (!canonicalNoValues(instruction) || instruction.local >= function.localTypes.size() ||
                    instruction.constant != 0)
                    report("load has a non-canonical encoding or invalid local");
                define(instruction.result,
                       instruction.local < function.localTypes.size()
                           ? function.localTypes[instruction.local]
                           : semantic::Type::i64,
                       block.id, index, "load");
                parameterPrefix = false;
                break;
            case Opcode::storeLocal:
                if (instruction.result != noValue || instruction.left != noValue ||
                    instruction.right != noValue || instruction.local >= function.localTypes.size() ||
                    instruction.constant != 0 || !hasNoCallOperands(instruction))
                    report("store has a non-canonical encoding or invalid local");
                parameterPrefix = false;
                break;
            case Opcode::negateI64:
                if (instruction.left != noValue || instruction.right != noValue ||
                    instruction.local != noLocal || instruction.constant != 0 ||
                    !hasNoCallOperands(instruction))
                    report("negation has non-canonical unused operands");
                define(instruction.result, semantic::Type::i64, block.id, index, "negation");
                parameterPrefix = false;
                break;
            case Opcode::negateF64:
                if (instruction.left != noValue || instruction.right != noValue ||
                    instruction.local != noLocal || instruction.constant != 0 ||
                    !hasNoCallOperands(instruction))
                    report("negation has non-canonical unused operands");
                define(instruction.result, semantic::Type::f64, block.id, index, "negation");
                parameterPrefix = false;
                break;
            case Opcode::callI64:
                if (instruction.input != noValue || instruction.left != noValue ||
                    instruction.right != noValue || instruction.local != noLocal ||
                    instruction.constant != 0 || instruction.callee == noFunction ||
                    (instruction.calleeKind != semantic::FunctionKind::user &&
                     instruction.calleeKind != semantic::FunctionKind::host))
                    report("call has a non-canonical encoding");
                define(instruction.result, semantic::Type::i64, block.id, index, "call");
                parameterPrefix = false;
                break;
            case Opcode::callValue:
                if (instruction.input != noValue || instruction.left != noValue ||
                    instruction.right != noValue || instruction.local != noLocal ||
                    instruction.constant != 0 || instruction.callee == noFunction ||
                    (instruction.calleeKind != semantic::FunctionKind::user &&
                     instruction.calleeKind != semantic::FunctionKind::host))
                    report("call has a non-canonical encoding");
                if (instruction.result == noValue || instruction.result >= function.valueTypes.size() ||
                    function.valueTypes[instruction.result] == semantic::Type::voidType)
                    report("value call must define a non-void virtual register");
                else
                    define(instruction.result, function.valueTypes[instruction.result], block.id, index,
                           "call");
                parameterPrefix = false;
                break;
            case Opcode::callVoid:
                if (instruction.result != noValue || instruction.input != noValue ||
                    instruction.left != noValue || instruction.right != noValue ||
                    instruction.local != noLocal || instruction.constant != 0 ||
                    instruction.callee == noFunction ||
                    (instruction.calleeKind != semantic::FunctionKind::user &&
                     instruction.calleeKind != semantic::FunctionKind::host))
                    report("void call has a non-canonical encoding");
                parameterPrefix = false;
                break;
            case Opcode::addI64:
            case Opcode::subI64:
            case Opcode::mulI64:
            case Opcode::divI64:
            case Opcode::remI64:
            case Opcode::equalI64:
            case Opcode::notEqualI64:
            case Opcode::lessI64:
            case Opcode::lessEqualI64:
            case Opcode::greaterI64:
            case Opcode::greaterEqualI64:
                if (instruction.input != noValue || instruction.local != noLocal ||
                    instruction.constant != 0 || !hasNoCallOperands(instruction))
                    report("binary instruction has non-canonical unused operands");
                define(instruction.result, isComparison(instruction.opcode) ? semantic::Type::boolean
                                                                            : semantic::Type::i64,
                       block.id, index, "binary instruction");
                parameterPrefix = false;
                break;
            case Opcode::addF64:
            case Opcode::subF64:
            case Opcode::mulF64:
            case Opcode::divF64:
            case Opcode::equalF64:
            case Opcode::notEqualF64:
            case Opcode::lessF64:
            case Opcode::lessEqualF64:
            case Opcode::greaterF64:
            case Opcode::greaterEqualF64:
            case Opcode::equalBool:
            case Opcode::notEqualBool:
                if (instruction.input != noValue || instruction.local != noLocal ||
                    instruction.constant != 0 || !hasNoCallOperands(instruction))
                    report("binary instruction has non-canonical unused operands");
                define(instruction.result,
                       isF64Comparison(instruction.opcode) || isBoolBinary(instruction.opcode)
                           ? semantic::Type::boolean
                           : semantic::Type::f64,
                       block.id, index, "binary instruction");
                parameterPrefix = false;
                break;
            default:
                report("has an invalid instruction opcode");
                parameterPrefix = false;
                break;
            }
        }

        const auto &terminator = block.terminator;
        switch (terminator.kind)
        {
        case TerminatorKind::branch:
            if (terminator.condition != noValue || terminator.value != noValue ||
                terminator.falseTarget != noBlock || terminator.trueTarget >= function.blocks.size())
                report("branch has non-canonical operands or an invalid target");
            break;
        case TerminatorKind::branchIfFalse:
            if (terminator.condition == noValue || terminator.value != noValue ||
                terminator.trueTarget >= function.blocks.size() ||
                terminator.falseTarget >= function.blocks.size())
                report("conditional branch has non-canonical operands or invalid targets");
            break;
        case TerminatorKind::returnValue:
            if (function.signature.returnType == semantic::Type::voidType ||
                terminator.condition != noValue || terminator.value == noValue ||
                terminator.trueTarget != noBlock || terminator.falseTarget != noBlock)
                report("return value is incompatible with the signature or non-canonical operands");
            break;
        case TerminatorKind::returnVoid:
            if (function.signature.returnType != semantic::Type::voidType ||
                terminator.condition != noValue || terminator.value != noValue ||
                terminator.trueTarget != noBlock || terminator.falseTarget != noBlock)
                report("return_void is incompatible with the signature or non-canonical operands");
            break;
        case TerminatorKind::invalid:
        default:
            report("has an invalid terminator kind");
            break;
        }
    }
    if (parameterCount != function.signature.parameterTypes.size())
        report("entry block does not define every parameter");
    if (nextValue != function.valueTypes.size())
        report("virtual-register definitions are duplicate or non-dense");
    if (!diagnostics.empty())
        return {.function = std::nullopt, .diagnostics = std::move(diagnostics)};

    std::vector<std::size_t> predecessorCounts(function.blocks.size(), 0);
    for (const auto &block : function.blocks)
    {
        if (block.terminator.kind == TerminatorKind::branch)
            ++predecessorCounts[block.terminator.trueTarget];
        else if (block.terminator.kind == TerminatorKind::branchIfFalse)
        {
            ++predecessorCounts[block.terminator.falseTarget];
            ++predecessorCounts[block.terminator.trueTarget];
        }
    }
    if (predecessorCounts[0] != 0)
        report("entry block must not have predecessors");
    if (!diagnostics.empty())
        return {.function = std::nullopt, .diagnostics = std::move(diagnostics)};

    // A verified function is a compact backend input: Lowerer already drops
    // dead bytecode, so dead IR blocks are malformed rather than preserved.
    std::vector<bool> reachable(function.blocks.size(), false);
    std::deque<BlockId> pending;
    reachable[0] = true;
    pending.push_back(0);
    while (!pending.empty())
    {
        const auto blockId = pending.front();
        pending.pop_front();
        const auto &terminator = function.blocks[blockId].terminator;
        const auto visit = [&](BlockId successor)
        {
            if (!reachable[successor])
            {
                reachable[successor] = true;
                pending.push_back(successor);
            }
        };
        if (terminator.kind == TerminatorKind::branch)
            visit(terminator.trueTarget);
        else if (terminator.kind == TerminatorKind::branchIfFalse)
        {
            visit(terminator.falseTarget);
            visit(terminator.trueTarget);
        }
    }
    for (const bool blockReachable : reachable)
        if (!blockReachable)
            report("contains an unreachable basic block");
    if (!diagnostics.empty())
        return {.function = std::nullopt, .diagnostics = std::move(diagnostics)};

    // Phase 2: virtual registers are deliberately block-local in this
    // non-SSA IR. Values that cross a CFG edge must first be stored in a local.
    const auto validateUse = [&](ValueId value, semantic::Type type, BlockId block,
                                 std::size_t useIndex, std::string_view role)
    {
        if (value == noValue || value >= definitions.size() || !definitions[value])
        {
            report(std::string{role} + " uses an undefined virtual register");
            return;
        }
        const auto definition = *definitions[value];
        if (definition.block != block || definition.instruction >= useIndex)
        {
            report(std::string{role} + " uses a virtual register outside its defining block");
            return;
        }
        if (function.valueTypes[value] != type)
            report(std::string{role} + " has an invalid virtual-register type");
    };
    for (const auto &block : function.blocks)
    {
        for (std::size_t index = 0; index < block.instructions.size(); ++index)
        {
            const auto &instruction = block.instructions[index];
            if (instruction.opcode == Opcode::storeLocal)
                validateUse(instruction.input, function.localTypes[instruction.local], block.id, index,
                            "store");
            else if (instruction.opcode == Opcode::negateI64)
                validateUse(instruction.input, semantic::Type::i64, block.id, index, "negation");
            else if (instruction.opcode == Opcode::negateF64)
                validateUse(instruction.input, semantic::Type::f64, block.id, index, "negation");
            else if (instruction.opcode == Opcode::callI64 || instruction.opcode == Opcode::callValue ||
                     instruction.opcode == Opcode::callVoid)
            {
                const auto *target = callTargets.find(instruction.callee);
                if (target == nullptr)
                {
                    report("call target is absent from the trusted target table");
                    continue;
                }
                if (target->kind != instruction.calleeKind)
                    report("call kind does not match the trusted target");
                if (instruction.arguments.size() != target->signature.parameterTypes.size())
                    report("call argument count does not match the trusted target signature");

                const auto checkedArgumentCount =
                    std::min(instruction.arguments.size(), target->signature.parameterTypes.size());
                for (std::size_t argumentIndex{}; argumentIndex < checkedArgumentCount; ++argumentIndex)
                    validateUse(instruction.arguments[argumentIndex],
                                target->signature.parameterTypes[argumentIndex], block.id, index,
                                "call argument");

                if (instruction.opcode == Opcode::callVoid)
                {
                    if (target->signature.returnType != semantic::Type::voidType)
                        report("void call targets a non-void function");
                    continue;
                }

                if (target->signature.returnType == semantic::Type::voidType)
                    report("value call targets a void function");
                if (instruction.result == noValue || instruction.result >= function.valueTypes.size() ||
                    function.valueTypes[instruction.result] != target->signature.returnType)
                    report("call result type does not match the trusted target signature");
                if (instruction.opcode == Opcode::callI64)
                {
                    if (target->signature.returnType != semantic::Type::i64)
                        report("call_i64 targets a non-i64 function");
                    if (!std::ranges::all_of(target->signature.parameterTypes,
                                             [](semantic::Type type)
                                             { return type == semantic::Type::i64; }))
                        report("call_i64 target has a non-i64 parameter");
                }
            }
            else if (isBinaryI64(instruction.opcode))
            {
                validateUse(instruction.left, semantic::Type::i64, block.id, index, "binary instruction");
                validateUse(instruction.right, semantic::Type::i64, block.id, index, "binary instruction");
            }
            else if (isF64Binary(instruction.opcode))
            {
                validateUse(instruction.left, semantic::Type::f64, block.id, index, "binary instruction");
                validateUse(instruction.right, semantic::Type::f64, block.id, index, "binary instruction");
            }
            else if (isBoolBinary(instruction.opcode))
            {
                validateUse(instruction.left, semantic::Type::boolean, block.id, index,
                            "binary instruction");
                validateUse(instruction.right, semantic::Type::boolean, block.id, index,
                            "binary instruction");
            }
        }
        const auto terminatorIndex = block.instructions.size();
        if (block.terminator.kind == TerminatorKind::branchIfFalse)
            validateUse(block.terminator.condition, semantic::Type::boolean, block.id, terminatorIndex,
                        "conditional branch");
        else if (block.terminator.kind == TerminatorKind::returnValue)
            validateUse(block.terminator.value, function.signature.returnType, block.id, terminatorIndex,
                        "return");
    }
    if (!diagnostics.empty())
        return {.function = std::nullopt, .diagnostics = std::move(diagnostics)};

    // Phase 3: must-analysis for definite local initialization. Every block is
    // reachable by the stronger structural contract above.
    struct LocalState
    {
        std::vector<bool> initialized;
    };
    std::vector<std::optional<LocalState>> incoming(function.blocks.size());
    incoming[0] = LocalState{.initialized = std::vector<bool>(function.localTypes.size(), false)};
    pending.push_back(0);
    while (!pending.empty())
    {
        const auto blockId = pending.front();
        pending.pop_front();
        auto state = *incoming[blockId];
        const auto &block = function.blocks[blockId];
        for (const auto &instruction : block.instructions)
        {
            if (instruction.opcode == Opcode::loadLocal && !state.initialized[instruction.local])
                report("load of an uninitialized local");
            else if (instruction.opcode == Opcode::storeLocal)
                state.initialized[instruction.local] = true;
        }
        const auto merge = [&](BlockId successor)
        {
            if (!incoming[successor])
            {
                incoming[successor] = state;
                pending.push_back(successor);
                return;
            }
            auto merged = incoming[successor]->initialized;
            for (std::size_t index = 0; index < merged.size(); ++index)
                merged[index] = merged[index] && state.initialized[index];
            if (merged != incoming[successor]->initialized)
            {
                incoming[successor]->initialized = std::move(merged);
                pending.push_back(successor);
            }
        };
        if (block.terminator.kind == TerminatorKind::branch)
            merge(block.terminator.trueTarget);
        else if (block.terminator.kind == TerminatorKind::branchIfFalse)
        {
            merge(block.terminator.falseTarget);
            merge(block.terminator.trueTarget);
        }
    }
    if (!diagnostics.empty())
        return {.function = std::nullopt, .diagnostics = std::move(diagnostics)};
    return {.function = VerifiedFunction{std::move(function), callTargets}, .diagnostics = {}};
}

VerifyResult Verifier::verify(Function function) const
{
    return verify(std::move(function), CallTargetTable{{}});
}
} // namespace ember::ir
