#pragma once

#include "ember/semantic/typed_ast.hpp"

#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

namespace ember::ssa {
using ValueId = std::uint32_t;
using BlockId = std::uint32_t;
using ParameterIndex = std::uint32_t;

inline constexpr ValueId noValue = std::numeric_limits<ValueId>::max();
inline constexpr BlockId noBlock = std::numeric_limits<BlockId>::max();
inline constexpr ParameterIndex noParameter = std::numeric_limits<ParameterIndex>::max();
inline constexpr semantic::FunctionId noFunction = std::numeric_limits<semantic::FunctionId>::max();

enum class Opcode : std::uint8_t {
    invalid,
    parameter,
    constantI64,
    constantF64,
    constantBool,
    negateI64,
    negateF64,
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
    addF64,
    subF64,
    mulF64,
    divF64,
    equalF64,
    notEqualF64,
    lessF64,
    lessEqualF64,
    greaterF64,
    greaterEqualF64,
    equalBool,
    notEqualBool,
    callI64,
    callValue,
    callVoid,
};

struct Instruction {
    Opcode opcode{Opcode::invalid};
    ValueId result{noValue};
    ValueId input{noValue};
    ValueId left{noValue};
    ValueId right{noValue};
    ParameterIndex parameterIndex{noParameter};
    std::int64_t constant{};
    semantic::FunctionId callee{noFunction};
    semantic::FunctionKind calleeKind{semantic::FunctionKind::user};
    std::vector<ValueId> arguments;

    [[nodiscard]] static Instruction parameter(ValueId result, ParameterIndex parameter) noexcept;
    [[nodiscard]] static Instruction constantI64(ValueId result, std::int64_t value) noexcept;
    [[nodiscard]] static Instruction constantF64(ValueId result, double value) noexcept;
    [[nodiscard]] static Instruction constantBool(ValueId result, bool value) noexcept;
    [[nodiscard]] static Instruction negateI64(ValueId result, ValueId input) noexcept;
    [[nodiscard]] static Instruction negateF64(ValueId result, ValueId input) noexcept;
    [[nodiscard]] static Instruction
    binary(Opcode opcode, ValueId result, ValueId left, ValueId right) noexcept;
    [[nodiscard]] static Instruction
    callI64(ValueId result,
            semantic::FunctionId callee,
            std::vector<ValueId> arguments,
            semantic::FunctionKind calleeKind = semantic::FunctionKind::user);
    [[nodiscard]] static Instruction
    callValue(ValueId result,
              semantic::FunctionId callee,
              std::vector<ValueId> arguments,
              semantic::FunctionKind calleeKind = semantic::FunctionKind::user);
    [[nodiscard]] static Instruction
    callVoid(semantic::FunctionId callee,
             std::vector<ValueId> arguments,
             semantic::FunctionKind calleeKind = semantic::FunctionKind::user);
};

struct BlockParameter {
    ValueId value{noValue};
    semantic::Type type{semantic::Type::voidType};
};

struct Edge {
    BlockId target{noBlock};
    std::vector<ValueId> arguments;
};

enum class TerminatorKind : std::uint8_t {
    invalid,
    branch,
    branchIfFalse,
    returnValue,
    returnVoid,
};

struct Terminator {
    TerminatorKind kind{TerminatorKind::invalid};
    ValueId condition{noValue};
    ValueId value{noValue};
    Edge trueEdge;
    Edge falseEdge;

    [[nodiscard]] static Terminator branch(BlockId target, std::vector<ValueId> arguments = {});
    [[nodiscard]] static Terminator branchIfFalse(ValueId condition, Edge falseEdge, Edge trueEdge);
    [[nodiscard]] static Terminator returnValue(ValueId value) noexcept;
    [[nodiscard]] static Terminator returnVoid() noexcept;
};

struct BasicBlock {
    BlockId id{noBlock};
    std::vector<BlockParameter> parameters;
    std::vector<Instruction> instructions;
    Terminator terminator{};
};

// `valueTypes[index]` is the type of SSA value `v<index>`. Void is a function
// return type only and is never an SSA value or block-parameter type.
struct Function {
    semantic::FunctionId id{noFunction};
    semantic::FunctionSignature signature;
    std::vector<semantic::Type> valueTypes;
    std::vector<BasicBlock> blocks;
};
} // namespace ember::ssa
