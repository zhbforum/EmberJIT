#pragma once

#include "ember/core/function.hpp"
#include "ember/core/value.hpp"
#include "ember/support/diagnostic.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace ember::bytecode {
enum class Opcode : std::uint8_t {
    constant,
    load,
    store,
    pop,
    negateI64,
    negateF64,
    addI64,
    subI64,
    mulI64,
    divI64,
    remI64,
    addF64,
    subF64,
    mulF64,
    divF64,
    equalI64,
    equalF64,
    equalBool,
    notEqualI64,
    notEqualF64,
    notEqualBool,
    lessI64,
    lessEqualI64,
    greaterI64,
    greaterEqualI64,
    lessF64,
    lessEqualF64,
    greaterF64,
    greaterEqualF64,
    jump,
    jumpIfFalse,
    call,
    returnValue,
    returnVoid
};

struct Instruction {
    Opcode opcode;
    std::uint32_t operand{}; // local slot, instruction target, or function id
    std::optional<core::Value> value;
};
struct Function {
    core::FunctionId id;
    core::FunctionKind kind{core::FunctionKind::user};
    core::FunctionSignature signature;
    std::uint32_t localCount{};
    std::vector<core::Type> localTypes;
    std::vector<Instruction> code;
};
struct Program {
    std::vector<Function> functions;
};
class VerifiedProgram {
public:
    VerifiedProgram(const VerifiedProgram&) = delete;
    auto operator=(const VerifiedProgram&) -> VerifiedProgram& = delete;
    VerifiedProgram(VerifiedProgram&&) noexcept = default;
    auto operator=(VerifiedProgram&&) noexcept -> VerifiedProgram& = default;

    [[nodiscard]] const Program& program() const noexcept {
        return program_;
    }
    [[nodiscard]] Program takeProgram() && noexcept {
        return std::move(program_);
    }

private:
    explicit VerifiedProgram(Program program)
        : program_(std::move(program)) {
    }

    Program program_;
    friend class Verifier;
};
struct VerifyResult {
    std::optional<VerifiedProgram> program;
    std::vector<support::Diagnostic> diagnostics;
};

class Verifier {
public:
    [[nodiscard]] VerifyResult verify(Program program) const;
};
[[nodiscard]] std::string dump(const VerifiedProgram& program);
} // namespace ember::bytecode
