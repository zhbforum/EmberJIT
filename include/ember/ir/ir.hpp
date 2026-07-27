#pragma once

#include "ember/semantic/typed_ast.hpp"

#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

namespace ember::ir
{
using ValueId = std::uint32_t;
using BlockId = std::uint32_t;
using LocalId = std::uint32_t;

inline constexpr ValueId noValue = std::numeric_limits<ValueId>::max();
inline constexpr BlockId noBlock = std::numeric_limits<BlockId>::max();
inline constexpr LocalId noLocal = std::numeric_limits<LocalId>::max();
inline constexpr semantic::FunctionId noFunction = std::numeric_limits<semantic::FunctionId>::max();

enum class Opcode : std::uint8_t
{
    parameter,
    constantI64,
    loadLocal,
    storeLocal,
    negateI64,
    addI64,
    subI64,
    mulI64,
    divI64,
    remI64,
    equalI64,
    notEqualI64,
    lessI64,
    lessEqualI64,
    greaterI64,
    greaterEqualI64,
    callI64,
};

struct Instruction
{
    Opcode opcode;
    ValueId result{noValue};
    ValueId input{noValue};
    ValueId left{noValue};
    ValueId right{noValue};
    LocalId local{noLocal};
    std::int64_t constant{};
    semantic::FunctionId callee{noFunction};
    std::vector<ValueId> arguments;

    [[nodiscard]] static Instruction parameter(ValueId result, LocalId local) noexcept;
    [[nodiscard]] static Instruction constantI64(ValueId result, std::int64_t value) noexcept;
    [[nodiscard]] static Instruction loadLocal(ValueId result, LocalId local) noexcept;
    [[nodiscard]] static Instruction storeLocal(LocalId local, ValueId value) noexcept;
    [[nodiscard]] static Instruction negateI64(ValueId result, ValueId input) noexcept;
    [[nodiscard]] static Instruction binaryI64(Opcode opcode, ValueId result, ValueId left,
                                               ValueId right) noexcept;
    [[nodiscard]] static Instruction callI64(ValueId result, semantic::FunctionId callee,
                                             std::vector<ValueId> arguments);
};

// Visits every virtual-register use encoded by one instruction. Shared by
// optimization analyses and operand rewriting; the overloads intentionally
// expose mutable references for rewriting and const references for analysis.
template <typename InstructionType, typename Callback>
void forEachUseImpl(InstructionType &instruction, Callback &&callback)
{
    switch (instruction.opcode)
    {
    case Opcode::storeLocal:
    case Opcode::negateI64:
        callback(instruction.input);
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
        callback(instruction.left);
        callback(instruction.right);
        break;
    case Opcode::callI64:
        for (auto &argument : instruction.arguments)
            callback(argument);
        break;
    case Opcode::parameter:
    case Opcode::constantI64:
    case Opcode::loadLocal:
        break;
    }
}

template <typename Callback>
void forEachUse(Instruction &instruction, Callback &&callback)
{
    forEachUseImpl(instruction, std::forward<Callback>(callback));
}

template <typename Callback>
void forEachUse(const Instruction &instruction, Callback &&callback)
{
    forEachUseImpl(instruction, std::forward<Callback>(callback));
}

enum class TerminatorKind : std::uint8_t
{
    invalid,
    branch,
    branchIfFalse,
    returnValue,
    returnVoid,
};

struct Terminator
{
    TerminatorKind kind{TerminatorKind::invalid};
    ValueId condition{noValue};
    ValueId value{noValue};
    BlockId trueTarget{noBlock};
    BlockId falseTarget{noBlock};

    [[nodiscard]] static Terminator branch(BlockId target) noexcept;
    [[nodiscard]] static Terminator branchIfFalse(ValueId condition, BlockId falseTarget,
                                                   BlockId trueTarget) noexcept;
    [[nodiscard]] static Terminator returnValue(ValueId value) noexcept;
    [[nodiscard]] static Terminator returnVoid() noexcept;
};

struct BasicBlock
{
    BlockId id;
    std::vector<Instruction> instructions;
    Terminator terminator{};
};

// `valueTypes[index]` is the type of virtual register `v<index>`. Void is a
// function return type only and is never a virtual-register or local type.
struct Function
{
    semantic::FunctionId id;
    semantic::FunctionSignature signature;
    std::vector<semantic::Type> localTypes;
    std::vector<semantic::Type> valueTypes;
    std::vector<BasicBlock> blocks;
};
} // namespace ember::ir
