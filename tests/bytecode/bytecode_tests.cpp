#include "ember/bytecode/bytecode.hpp"
#include "ember/frontend/lexer.hpp"
#include "ember/frontend/parser.hpp"
#include "ember/runtime/vm.hpp"
#include "ember/semantic/analyzer.hpp"
#include "test_harness.hpp"

#include <string>

namespace
{
[[nodiscard]] auto compile(std::string code)
{
    ember::support::SourceText source{ember::support::SourceId{90}, "vm.ember", std::move(code)};
    const auto tokens = ember::frontend::Lexer{}.lex(source);
    if (!tokens.diagnostics.empty())
        return ember::bytecode::CompileResult{};
    const auto ast = ember::frontend::Parser{}.parse(source, tokens.tokens);
    if (!ast.diagnostics.empty() || !ast.program)
        return ember::bytecode::CompileResult{};
    const auto typed = ember::semantic::SemanticAnalyzer{}.analyze(*ast.program, source);
    if (!typed.diagnostics.empty() || !typed.program)
        return ember::bytecode::CompileResult{};
    return ember::bytecode::Compiler{}.compile(*typed.program);
}
EMBER_TEST("bytecode lowers loops and VM executes i64 arithmetic")
{
    auto compiled = compile("fn main() -> i64 { let n: i64 = 5; let r: i64 = 1; while n > 1 { "
                            "r = r * n; n = n - 1; } return r; }");
    tests.expect(compiled.program.has_value(), "typed program lowers");
    if (!compiled.program)
        return;
    auto verified = ember::bytecode::Verifier{}.verify(std::move(*compiled.program));
    tests.expect(verified.program.has_value(), "compiler output verifies");
    if (!verified.program)
        return;
    auto vm = ember::runtime::VirtualMachine::create(std::move(*verified.program));
    const auto result = vm.execute(0);
    tests.expect(result.value.has_value() && std::get<std::int64_t>(*result.value) == 120,
                 "factorial executes");
}
EMBER_TEST("VM uses explicit frames for recursive calls")
{
    auto compiled = compile(
        "fn fact(n: i64) -> i64 { if n == 0 { return 1; } else { return n * fact(n - 1); } } "
        "fn main() -> i64 { return fact(8); }");
    if (!compiled.program)
    {
        tests.expect(false, "recursive typed program lowers");
        return;
    }
    auto verified = ember::bytecode::Verifier{}.verify(std::move(*compiled.program));
    tests.expect(verified.program.has_value(), "recursive bytecode verifies");
    if (!verified.program)
        return;
    auto vm = ember::runtime::VirtualMachine::create(std::move(*verified.program));
    const auto result = vm.execute(1);
    tests.expect(result.value.has_value() && std::get<std::int64_t>(*result.value) == 40320,
                 "recursive function executes through VM frames");
}
EMBER_TEST("verifier rejects malformed jump before VM construction")
{
    ember::bytecode::Program program{
        .functions = {
            {.id = 0,
             .kind = ember::semantic::FunctionKind::user,
             .signature = {.parameterTypes = {}, .returnType = ember::semantic::Type::voidType},
             .localCount = 0,
             .localTypes = {},
             .code = {
                 {.opcode = ember::bytecode::Opcode::jump, .operand = 9, .value = std::nullopt}}}}};
    const auto verified = ember::bytecode::Verifier{}.verify(program);
    tests.expect(!verified.program.has_value() && !verified.diagnostics.empty(),
                 "bad jump is rejected");
}
EMBER_TEST("bytecode dump uses stable symbolic instructions and constants")
{
    auto compiled = compile("fn main() -> i64 { return 42; }");
    if (!compiled.program)
    {
        tests.expect(false, "golden typed program lowers");
        return;
    }
    auto verified = ember::bytecode::Verifier{}.verify(std::move(*compiled.program));
    if (!verified.program)
    {
        tests.expect(false, "golden bytecode verifies");
        return;
    }
    const auto text = ember::bytecode::dump(*verified.program);
    tests.expect(text == "fn #0 () -> i64\n"
                         "  0000  const.i64 42\n"
                         "  0001  return\n"
                         "  0002  return\n",
                 "dump is a stable golden representation");
}
EMBER_TEST("VM reports integer division runtime error")
{
    auto compiled = compile("fn main() -> i64 { return 1 / 0; }");
    if (!compiled.program)
    {
        tests.expect(false, "division typed program lowers");
        return;
    }
    auto verified = ember::bytecode::Verifier{}.verify(std::move(*compiled.program));
    if (!verified.program)
    {
        tests.expect(false, "division bytecode verifies");
        return;
    }
    auto vm = ember::runtime::VirtualMachine::create(std::move(*verified.program));
    const auto result = vm.execute(0);
    tests.expect(result.error.has_value() && result.error->code == "R5004",
                 "division by zero is a runtime error");
}

EMBER_TEST("verifier preserves unary and boolean opcode semantics")
{
    ember::bytecode::Program unary{
        .functions = {
            {.id = 0,
             .kind = ember::semantic::FunctionKind::user,
             .signature = {.parameterTypes = {}, .returnType = ember::semantic::Type::i64},
             .localCount = 0,
             .localTypes = {},
             .code = {{.opcode = ember::bytecode::Opcode::constant,
                       .operand = 0,
                       .value = ember::bytecode::Value{std::int64_t{1}}},
                      {.opcode = ember::bytecode::Opcode::negateI64,
                       .operand = 0,
                       .value = std::nullopt},
                      {.opcode = ember::bytecode::Opcode::returnValue,
                       .operand = 0,
                       .value = std::nullopt}}}}};
    auto unaryVerified = ember::bytecode::Verifier{}.verify(std::move(unary));
    tests.expect(unaryVerified.program.has_value(), "i64 unary negation verifies");
    if (!unaryVerified.program)
        return;
    auto unaryVm = ember::runtime::VirtualMachine::create(std::move(*unaryVerified.program));
    const auto unaryResult = unaryVm.execute(0);
    tests.expect(unaryResult.value.has_value() && std::get<std::int64_t>(*unaryResult.value) == -1,
                 "i64 unary negation executes");

    auto boolean = compile("fn main() -> bool { return true == false; }");
    if (!boolean.program)
    {
        tests.expect(false, "boolean typed program lowers");
        return;
    }
    auto booleanVerified = ember::bytecode::Verifier{}.verify(std::move(*boolean.program));
    tests.expect(booleanVerified.program.has_value(), "bool equality verifies as bool opcode");
}

EMBER_TEST("verifier rejects unsafe local layouts and malformed opcodes")
{
    const ember::bytecode::Function unsafeLocal{
        .id = 0,
        .kind = ember::semantic::FunctionKind::user,
        .signature = {.parameterTypes = {ember::semantic::Type::i64},
                      .returnType = ember::semantic::Type::voidType},
        .localCount = 0,
        .localTypes = {},
        .code = {{.opcode = ember::bytecode::Opcode::returnVoid,
                  .operand = 0,
                  .value = std::nullopt}},
    };
    const auto unsafeResult = ember::bytecode::Verifier{}.verify({.functions = {unsafeLocal}});
    tests.expect(!unsafeResult.program.has_value(), "parameter without local slot is rejected");

    const ember::bytecode::Function unknownOpcode{
        .id = 0,
        .kind = ember::semantic::FunctionKind::user,
        .signature = {.parameterTypes = {}, .returnType = ember::semantic::Type::voidType},
        .localCount = 0,
        .localTypes = {},
        .code = {{.opcode = static_cast<ember::bytecode::Opcode>(255),
                  .operand = 0,
                  .value = std::nullopt}},
    };
    const auto unknownResult = ember::bytecode::Verifier{}.verify({.functions = {unknownOpcode}});
    tests.expect(!unknownResult.program.has_value(), "unknown opcode is rejected");

    const ember::bytecode::Function uninitializedLoad{
        .id = 0,
        .kind = ember::semantic::FunctionKind::user,
        .signature = {.parameterTypes = {}, .returnType = ember::semantic::Type::i64},
        .localCount = 1,
        .localTypes = {ember::semantic::Type::i64},
        .code = {{.opcode = ember::bytecode::Opcode::load, .operand = 0, .value = std::nullopt},
                 {.opcode = ember::bytecode::Opcode::returnValue,
                  .operand = 0,
                  .value = std::nullopt}},
    };
    const auto uninitializedResult =
        ember::bytecode::Verifier{}.verify({.functions = {uninitializedLoad}});
    tests.expect(!uninitializedResult.program.has_value(), "uninitialized local load is rejected");
}

EMBER_TEST("verifier rejects invalid returns, kinds, types, and host payloads")
{
    const auto makeFunction =
        [](ember::semantic::Type returnType, std::vector<ember::bytecode::Instruction> code)
    {
        return ember::bytecode::Program{
            .functions = {{.id = 0,
                           .kind = ember::semantic::FunctionKind::user,
                           .signature = {.parameterTypes = {}, .returnType = returnType},
                           .localCount = 0,
                           .localTypes = {},
                           .code = std::move(code)}}};
    };
    tests.expect(!ember::bytecode::Verifier{}
                      .verify(makeFunction(ember::semantic::Type::voidType,
                                           {{.opcode = ember::bytecode::Opcode::returnValue,
                                             .operand = 0,
                                             .value = std::nullopt}}))
                      .program.has_value(),
                 "void returnValue is rejected");
    tests.expect(!ember::bytecode::Verifier{}
                      .verify(makeFunction(ember::semantic::Type::i64,
                                           {{.opcode = ember::bytecode::Opcode::returnVoid,
                                             .operand = 0,
                                             .value = std::nullopt}}))
                      .program.has_value(),
                 "non-void returnVoid is rejected");
    tests.expect(!ember::bytecode::Verifier{}
                      .verify(makeFunction(ember::semantic::Type::i64,
                                           {{.opcode = ember::bytecode::Opcode::constant,
                                             .operand = 0,
                                             .value = ember::bytecode::Value{std::int64_t{1}}},
                                            {.opcode = ember::bytecode::Opcode::constant,
                                             .operand = 0,
                                             .value = ember::bytecode::Value{std::int64_t{2}}},
                                            {.opcode = ember::bytecode::Opcode::returnValue,
                                             .operand = 0,
                                             .value = std::nullopt}}))
                      .program.has_value(),
                 "return with residual stack values is rejected");

    auto invalidKind = makeFunction(
        ember::semantic::Type::voidType,
        {{.opcode = ember::bytecode::Opcode::returnVoid, .operand = 0, .value = std::nullopt}});
    invalidKind.functions.front().kind = static_cast<ember::semantic::FunctionKind>(255);
    tests.expect(!ember::bytecode::Verifier{}.verify(std::move(invalidKind)).program.has_value(),
                 "invalid function kind is rejected");

    auto invalidType = makeFunction(
        ember::semantic::Type::voidType,
        {{.opcode = ember::bytecode::Opcode::returnVoid, .operand = 0, .value = std::nullopt}});
    invalidType.functions.front().signature.returnType = static_cast<ember::semantic::Type>(255);
    tests.expect(!ember::bytecode::Verifier{}.verify(std::move(invalidType)).program.has_value(),
                 "invalid semantic type is rejected");

    ember::bytecode::Program malformedHost{
        .functions = {{.id = 0,
                       .kind = ember::semantic::FunctionKind::host,
                       .signature = {.parameterTypes = {ember::semantic::Type::i64},
                                     .returnType = ember::semantic::Type::voidType},
                       .localCount = 1,
                       .localTypes = {ember::semantic::Type::i64},
                       .code = {{.opcode = ember::bytecode::Opcode::returnVoid,
                                 .operand = 0,
                                 .value = std::nullopt}}}}};
    tests.expect(!ember::bytecode::Verifier{}.verify(std::move(malformedHost)).program.has_value(),
                 "host payload is rejected");
}

EMBER_TEST("VM enforces frame limit and executes a jump to instruction zero")
{
    ember::bytecode::Program recursion{
        .functions = {
            {.id = 0,
             .kind = ember::semantic::FunctionKind::user,
             .signature = {.parameterTypes = {}, .returnType = ember::semantic::Type::i64},
             .localCount = 0,
             .localTypes = {},
             .code = {
                 {.opcode = ember::bytecode::Opcode::call, .operand = 0, .value = std::nullopt},
                 {.opcode = ember::bytecode::Opcode::returnValue,
                  .operand = 0,
                  .value = std::nullopt}}}}};
    auto recursiveVerified = ember::bytecode::Verifier{}.verify(std::move(recursion));
    if (!recursiveVerified.program)
    {
        tests.expect(false, "recursive frame-limit bytecode verifies");
        return;
    }
    auto recursiveVm =
        ember::runtime::VirtualMachine::create(std::move(*recursiveVerified.program));
    const auto recursiveResult = recursiveVm.execute(0);
    tests.expect(recursiveResult.error.has_value() && recursiveResult.error->code == "R5006",
                 "frame limit is a defined runtime error");

    ember::bytecode::Program targetZero{
        .functions = {
            {.id = 0,
             .kind = ember::semantic::FunctionKind::user,
             .signature = {.parameterTypes = {ember::semantic::Type::i64},
                           .returnType = ember::semantic::Type::i64},
             .localCount = 1,
             .localTypes = {ember::semantic::Type::i64},
             .code = {
                 {.opcode = ember::bytecode::Opcode::load, .operand = 0, .value = std::nullopt},
                 {.opcode = ember::bytecode::Opcode::constant,
                  .operand = 0,
                  .value = ember::bytecode::Value{std::int64_t{0}}},
                 {.opcode = ember::bytecode::Opcode::greaterI64,
                  .operand = 0,
                  .value = std::nullopt},
                 {.opcode = ember::bytecode::Opcode::jumpIfFalse,
                  .operand = 9,
                  .value = std::nullopt},
                 {.opcode = ember::bytecode::Opcode::load, .operand = 0, .value = std::nullopt},
                 {.opcode = ember::bytecode::Opcode::constant,
                  .operand = 0,
                  .value = ember::bytecode::Value{std::int64_t{1}}},
                 {.opcode = ember::bytecode::Opcode::subI64, .operand = 0, .value = std::nullopt},
                 {.opcode = ember::bytecode::Opcode::store, .operand = 0, .value = std::nullopt},
                 {.opcode = ember::bytecode::Opcode::jump, .operand = 0, .value = std::nullopt},
                 {.opcode = ember::bytecode::Opcode::load, .operand = 0, .value = std::nullopt},
                 {.opcode = ember::bytecode::Opcode::returnValue,
                  .operand = 0,
                  .value = std::nullopt}}}}};
    auto targetZeroVerified = ember::bytecode::Verifier{}.verify(std::move(targetZero));
    if (!targetZeroVerified.program)
    {
        tests.expect(false, "target-zero bytecode verifies");
        return;
    }
    auto targetZeroVm =
        ember::runtime::VirtualMachine::create(std::move(*targetZeroVerified.program));
    const auto targetZeroResult = targetZeroVm.execute(0, {std::int64_t{2}});
    tests.expect(targetZeroResult.value.has_value() &&
                     std::get<std::int64_t>(*targetZeroResult.value) == 0,
                 "backward jump to instruction zero executes");
}
} // namespace
