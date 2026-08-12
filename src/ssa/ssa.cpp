#include "ember/ssa/ssa.hpp"

#include <bit>
#include <utility>

namespace ember::ssa {
Instruction Instruction::parameter(ValueId result, ParameterIndex parameterIndex) noexcept {
    return {.opcode = Opcode::parameter,
            .result = result,
            .parameterIndex = parameterIndex,
            .arguments = {}};
}

Instruction Instruction::constantI64(ValueId result, std::int64_t value) noexcept {
    return {.opcode = Opcode::constantI64, .result = result, .constant = value, .arguments = {}};
}

Instruction Instruction::constantF64(ValueId result, double value) noexcept {
    return {.opcode = Opcode::constantF64,
            .result = result,
            .constant = std::bit_cast<std::int64_t>(value),
            .arguments = {}};
}

Instruction Instruction::constantBool(ValueId result, bool value) noexcept {
    return {.opcode = Opcode::constantBool,
            .result = result,
            .constant = value ? 1 : 0,
            .arguments = {}};
}

Instruction Instruction::negateI64(ValueId result, ValueId input) noexcept {
    return {.opcode = Opcode::negateI64, .result = result, .input = input, .arguments = {}};
}

Instruction Instruction::negateF64(ValueId result, ValueId input) noexcept {
    return {.opcode = Opcode::negateF64, .result = result, .input = input, .arguments = {}};
}

Instruction
Instruction::binary(Opcode opcode, ValueId result, ValueId left, ValueId right) noexcept {
    return {.opcode = opcode, .result = result, .left = left, .right = right, .arguments = {}};
}

Instruction Instruction::callI64(ValueId result,
                                 semantic::FunctionId callee,
                                 std::vector<ValueId> arguments,
                                 semantic::FunctionKind calleeKind) {
    return {.opcode = Opcode::callI64,
            .result = result,
            .callee = callee,
            .calleeKind = calleeKind,
            .arguments = std::move(arguments)};
}

Instruction Instruction::callValue(ValueId result,
                                   semantic::FunctionId callee,
                                   std::vector<ValueId> arguments,
                                   semantic::FunctionKind calleeKind) {
    return {.opcode = Opcode::callValue,
            .result = result,
            .callee = callee,
            .calleeKind = calleeKind,
            .arguments = std::move(arguments)};
}

Instruction Instruction::callVoid(semantic::FunctionId callee,
                                  std::vector<ValueId> arguments,
                                  semantic::FunctionKind calleeKind) {
    return {.opcode = Opcode::callVoid,
            .callee = callee,
            .calleeKind = calleeKind,
            .arguments = std::move(arguments)};
}

Terminator Terminator::branch(BlockId target, std::vector<ValueId> arguments) {
    return {.kind = TerminatorKind::branch,
            .condition = noValue,
            .value = noValue,
            .trueEdge = {.target = target, .arguments = std::move(arguments)},
            .falseEdge = {}};
}

Terminator Terminator::branchIfFalse(ValueId condition, Edge falseEdge, Edge trueEdge) {
    return {.kind = TerminatorKind::branchIfFalse,
            .condition = condition,
            .trueEdge = std::move(trueEdge),
            .falseEdge = std::move(falseEdge)};
}

Terminator Terminator::returnValue(ValueId value) noexcept {
    return {.kind = TerminatorKind::returnValue,
            .condition = noValue,
            .value = value,
            .trueEdge = {},
            .falseEdge = {}};
}

Terminator Terminator::returnVoid() noexcept {
    return {.kind = TerminatorKind::returnVoid,
            .condition = noValue,
            .value = noValue,
            .trueEdge = {},
            .falseEdge = {}};
}
} // namespace ember::ssa
