#pragma once

#include "ember/semantic/typed_ast.hpp"
#include "ember/support/diagnostic.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace ember::bytecode
{
using Value = std::variant<std::int64_t, double, bool>;

enum class Opcode : std::uint8_t
{
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

struct Instruction
{
    Opcode opcode;
    std::uint32_t operand{}; // local slot, instruction target, or function id
    std::optional<Value> value;
};
struct Function
{
    semantic::FunctionId id;
    semantic::FunctionKind kind{semantic::FunctionKind::user};
    semantic::FunctionSignature signature;
    std::uint32_t localCount{};
    std::vector<semantic::Type> localTypes;
    std::vector<Instruction> code;
};
struct Program
{
    std::vector<Function> functions;
};
struct CompileResult
{
    std::optional<Program> program;
    std::vector<support::Diagnostic> diagnostics;
};
class VerifiedProgram
{
  public:
    VerifiedProgram(const VerifiedProgram &) = delete;
    auto operator=(const VerifiedProgram &) -> VerifiedProgram & = delete;
    VerifiedProgram(VerifiedProgram &&) noexcept = default;
    auto operator=(VerifiedProgram &&) noexcept -> VerifiedProgram & = default;

    [[nodiscard]] const Program &program() const noexcept { return program_; }
    [[nodiscard]] Program takeProgram() && noexcept { return std::move(program_); }

  private:
    explicit VerifiedProgram(Program program) : program_(std::move(program)) {}

    Program program_;
    friend class Verifier;
};
struct VerifyResult
{
    std::optional<VerifiedProgram> program;
    std::vector<support::Diagnostic> diagnostics;
};

class Compiler
{
  public:
    [[nodiscard]] CompileResult compile(const semantic::TypedProgram &program) const;
};
class Verifier
{
  public:
    [[nodiscard]] VerifyResult verify(Program program) const;
};
[[nodiscard]] std::string dump(const VerifiedProgram &program);
} // namespace ember::bytecode
