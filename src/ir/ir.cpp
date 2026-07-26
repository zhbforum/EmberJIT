#include "ember/ir/ir.hpp"

#include <utility>

namespace ember::ir
{
Instruction Instruction::parameter(ValueId result, LocalId local) noexcept
{
    return {.opcode = Opcode::parameter, .result = result, .local = local, .arguments = {}};
}

Instruction Instruction::constantI64(ValueId result, std::int64_t value) noexcept
{
    return {.opcode = Opcode::constantI64, .result = result, .constant = value, .arguments = {}};
}

Instruction Instruction::loadLocal(ValueId result, LocalId local) noexcept
{
    return {.opcode = Opcode::loadLocal, .result = result, .local = local, .arguments = {}};
}

Instruction Instruction::storeLocal(LocalId local, ValueId value) noexcept
{
    return {.opcode = Opcode::storeLocal, .input = value, .local = local, .arguments = {}};
}

Instruction Instruction::negateI64(ValueId result, ValueId input) noexcept
{
    return {.opcode = Opcode::negateI64, .result = result, .input = input, .arguments = {}};
}

Instruction Instruction::binaryI64(Opcode opcode, ValueId result, ValueId left,
                                   ValueId right) noexcept
{
    return {.opcode = opcode, .result = result, .left = left, .right = right, .arguments = {}};
}

Instruction Instruction::callI64(ValueId result, semantic::FunctionId callee,
                                 std::vector<ValueId> arguments)
{
    return {.opcode = Opcode::callI64,
            .result = result,
            .callee = callee,
            .arguments = std::move(arguments)};
}

Terminator Terminator::branch(BlockId target) noexcept
{
    return {.kind = TerminatorKind::branch, .trueTarget = target};
}

Terminator Terminator::branchIfFalse(ValueId condition, BlockId falseTarget,
                                     BlockId trueTarget) noexcept
{
    return {.kind = TerminatorKind::branchIfFalse,
            .condition = condition,
            .trueTarget = trueTarget,
            .falseTarget = falseTarget};
}

Terminator Terminator::returnValue(ValueId value) noexcept
{
    return {.kind = TerminatorKind::returnValue, .value = value};
}

Terminator Terminator::returnVoid() noexcept { return {.kind = TerminatorKind::returnVoid}; }
} // namespace ember::ir
