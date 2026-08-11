#include "ember/bytecode/builtins.hpp"
#include "ember/bytecode/bytecode.hpp"
#include "ember/frontend/lexer.hpp"
#include "ember/frontend/parser.hpp"
#include "ember/runtime/vm.hpp"
#include "ember/semantic/analyzer.hpp"

#include "test_harness.hpp"

#include <algorithm>
#include <string>

namespace {
struct CompiledProgram {
    ember::bytecode::CompileResult compilation;
    ember::semantic::FunctionId mainId{};
};

[[nodiscard]] auto compile(std::string code,
                           bool registerRuntimeBuiltins = false) -> CompiledProgram {
    ember::support::SourceText source{ember::support::SourceId{90}, "vm.ember", std::move(code)};
    const auto tokens = ember::frontend::Lexer{}.lex(source);
    if (!tokens.diagnostics.empty())
        return CompiledProgram{};
    const auto ast = ember::frontend::Parser{}.parse(source, tokens.tokens);
    if (!ast.diagnostics.empty() || !ast.program)
        return CompiledProgram{};

    ember::semantic::HostFunctionRegistry hosts;
    if (registerRuntimeBuiltins && !ember::bytecode::registerBuiltins(hosts))
        return CompiledProgram{};
    const auto typed = ember::semantic::SemanticAnalyzer{}.analyze(*ast.program, source, hosts);
    if (!typed.diagnostics.empty() || !typed.program)
        return CompiledProgram{};

    const auto main = std::find_if(
        typed.program->functions.begin(),
        typed.program->functions.end(),
        [](const auto& function) {
            return function.kind == ember::semantic::FunctionKind::user && function.name == "main";
        });
    if (main == typed.program->functions.end())
        return CompiledProgram{};

    return {.compilation = ember::bytecode::Compiler{}.compile(*typed.program), .mainId = main->id};
}
EMBER_TEST("bytecode lowers loops and VM executes i64 arithmetic") {
    auto compiled = compile("fn main() -> i64 { let n: i64 = 5; let r: i64 = 1; while n > 1 { "
                            "r = r * n; n = n - 1; } return r; }");
    tests.expect(compiled.compilation.program.has_value(), "typed program lowers");
    if (!compiled.compilation.program)
        return;
    auto verified = ember::bytecode::Verifier{}.verify(std::move(*compiled.compilation.program));
    tests.expect(verified.program.has_value(), "compiler output verifies");
    if (!verified.program)
        return;
    auto vm = ember::runtime::VirtualMachine::create(std::move(*verified.program));
    const auto result = vm.execute(compiled.mainId);
    tests.expect(result.result.value.has_value() &&
                     std::get<std::int64_t>(*result.result.value) == 120,
                 "factorial executes");
}
EMBER_TEST("VM uses explicit frames for recursive calls") {
    auto compiled = compile(
        "fn fact(n: i64) -> i64 { if n == 0 { return 1; } else { return n * fact(n - 1); } } "
        "fn main() -> i64 { return fact(8); }");
    if (!compiled.compilation.program) {
        tests.expect(false, "recursive typed program lowers");
        return;
    }
    auto verified = ember::bytecode::Verifier{}.verify(std::move(*compiled.compilation.program));
    tests.expect(verified.program.has_value(), "recursive bytecode verifies");
    if (!verified.program)
        return;
    auto vm = ember::runtime::VirtualMachine::create(std::move(*verified.program));
    const auto result = vm.execute(compiled.mainId);
    tests.expect(result.result.value.has_value() &&
                     std::get<std::int64_t>(*result.result.value) == 40320,
                 "recursive function executes through VM frames");
}
EMBER_TEST("verifier rejects malformed jump before VM construction") {
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
EMBER_TEST("bytecode dump uses stable symbolic instructions and constants") {
    auto compiled = compile("fn main() -> i64 { return 42; }");
    if (!compiled.compilation.program) {
        tests.expect(false, "golden typed program lowers");
        return;
    }
    auto verified = ember::bytecode::Verifier{}.verify(std::move(*compiled.compilation.program));
    if (!verified.program) {
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
EMBER_TEST("VM reports integer division runtime errors") {
    auto compiled = compile("fn main() -> i64 { return 1 / 0; }");
    if (!compiled.compilation.program) {
        tests.expect(false, "division typed program lowers");
        return;
    }
    auto verified = ember::bytecode::Verifier{}.verify(std::move(*compiled.compilation.program));
    if (!verified.program) {
        tests.expect(false, "division bytecode verifies");
        return;
    }
    auto vm = ember::runtime::VirtualMachine::create(std::move(*verified.program));
    const auto result = vm.execute(compiled.mainId);
    tests.expect(result.result.error.has_value() && result.result.error->code == "R5004",
                 "division by zero is a runtime error");

    auto overflowCompiled = compile("fn main() -> i64 { return -9223372036854775808 / -1; }");
    if (!overflowCompiled.compilation.program) {
        tests.expect(false, "INT64_MIN division program lowers");
        return;
    }
    auto overflowVerified =
        ember::bytecode::Verifier{}.verify(std::move(*overflowCompiled.compilation.program));
    if (!overflowVerified.program) {
        tests.expect(false, "INT64_MIN division bytecode verifies");
        return;
    }
    auto overflowVm = ember::runtime::VirtualMachine::create(std::move(*overflowVerified.program));
    const auto overflowResult = overflowVm.execute(overflowCompiled.mainId);
    tests.expect(overflowResult.result.error.has_value() &&
                     overflowResult.result.error->code == "R5004",
                 "INT64_MIN divided by minus one is a runtime error");
}

EMBER_TEST("VM executes f64, bool, and void functions") {
    auto compiled =
        compile("fn half(value: f64) -> f64 { if value > 0.0 { return -value / 2.0; } "
                "else { return value; } } "
                "fn observe(value: bool) -> void { if value { return; } return; } "
                "fn main() -> i64 { let result: f64 = half(3.5); observe(result == -1.75); "
                "if result == -1.75 { return 1; } else { return 0; } }");
    if (!compiled.compilation.program) {
        tests.expect(false, "typed program with f64, bool, and void lowers");
        return;
    }
    auto verified = ember::bytecode::Verifier{}.verify(std::move(*compiled.compilation.program));
    if (!verified.program) {
        tests.expect(false, "typed program with f64, bool, and void verifies");
        return;
    }
    auto vm = ember::runtime::VirtualMachine::create(std::move(*verified.program));
    const auto result = vm.execute(compiled.mainId);
    tests.expect(result.result.value.has_value() &&
                     std::holds_alternative<std::int64_t>(*result.result.value) &&
                     std::get<std::int64_t>(*result.result.value) == 1 &&
                     !result.result.error.has_value(),
                 "f64 arithmetic and boolean branch produce the expected result");
}

EMBER_TEST("VM executes registered clock builtin") {
    auto compiled = compile("fn main() -> i64 { return clock_ms(); }", true);
    if (!compiled.compilation.program) {
        tests.expect(false, "program using clock builtin lowers");
        return;
    }
    auto verified = ember::bytecode::Verifier{}.verify(std::move(*compiled.compilation.program));
    if (!verified.program) {
        tests.expect(false, "program using clock builtin verifies");
        return;
    }
    auto vm = ember::runtime::VirtualMachine::create(std::move(*verified.program));
    const auto result = vm.execute(compiled.mainId);
    tests.expect(result.result.value.has_value() &&
                     std::holds_alternative<std::int64_t>(*result.result.value) &&
                     !result.result.error.has_value(),
                 "clock builtin returns an i64 value");
}

EMBER_TEST("VM validates its public entry-point arguments") {
    auto compiled = compile("fn main(value: i64) -> i64 { return value; }");
    if (!compiled.compilation.program) {
        tests.expect(false, "entry-argument program lowers");
        return;
    }
    auto verified = ember::bytecode::Verifier{}.verify(std::move(*compiled.compilation.program));
    if (!verified.program) {
        tests.expect(false, "entry-argument program verifies");
        return;
    }
    auto vm = ember::runtime::VirtualMachine::create(std::move(*verified.program));
    const auto result = vm.execute(compiled.mainId, {true});
    tests.expect(result.result.error.has_value() && result.result.error->code == "R5002",
                 "invalid entry arguments are a runtime error");
}

EMBER_TEST("verifier preserves unary and boolean opcode semantics") {
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
    tests.expect(unaryResult.result.value.has_value() &&
                     std::get<std::int64_t>(*unaryResult.result.value) == -1,
                 "i64 unary negation executes");

    auto boolean = compile("fn main() -> bool { return true == false; }");
    if (!boolean.compilation.program) {
        tests.expect(false, "boolean typed program lowers");
        return;
    }
    auto booleanVerified =
        ember::bytecode::Verifier{}.verify(std::move(*boolean.compilation.program));
    tests.expect(booleanVerified.program.has_value(), "bool equality verifies as bool opcode");
}

EMBER_TEST("verifier validates binary operand and result types") {
    using ember::bytecode::Instruction;
    using ember::bytecode::Opcode;
    using ember::bytecode::Program;
    using ember::bytecode::Value;
    using ember::semantic::FunctionKind;
    using ember::semantic::Type;

    const auto makeFunction = [](Type returnType, std::vector<Instruction> code) {
        return Program{.functions = {{.id = 0,
                                      .kind = FunctionKind::user,
                                      .signature = {.parameterTypes = {}, .returnType = returnType},
                                      .localCount = 0,
                                      .localTypes = {},
                                      .code = std::move(code)}}};
    };
    const auto verify = [&makeFunction](Type returnType, std::vector<Instruction> code) {
        return ember::bytecode::Verifier{}.verify(makeFunction(returnType, std::move(code)));
    };
    const auto constantI64 = [](std::int64_t value) {
        return Instruction{
            .opcode = Opcode::constant,
            .operand = 0,
            .value = Value{value},
        };
    };
    const auto constantBool = [](bool value) {
        return Instruction{
            .opcode = Opcode::constant,
            .operand = 0,
            .value = Value{value},
        };
    };
    const auto operation = [](Opcode opcode) {
        return Instruction{
            .opcode = opcode,
            .operand = 0,
            .value = std::nullopt,
        };
    };

    const auto oneOperand =
        verify(Type::voidType,
               {constantI64(1), operation(Opcode::addI64), operation(Opcode::returnVoid)});
    tests.expect(!oneOperand.program.has_value(), "add.i64 rejects one operand instead of two");

    const auto mismatchedOperands = verify(Type::voidType,
                                           {constantBool(true),
                                            constantI64(1),
                                            operation(Opcode::addI64),
                                            operation(Opcode::returnVoid)});
    tests.expect(!mismatchedOperands.program.has_value(),
                 "add.i64 rejects a mismatched second operand");

    const auto addition = verify(Type::i64,
                                 {constantI64(1),
                                  constantI64(2),
                                  operation(Opcode::addI64),
                                  operation(Opcode::returnValue)});
    tests.expect(addition.program.has_value(), "add.i64 produces an i64 result");

    const auto comparison = verify(Type::boolean,
                                   {constantI64(1),
                                    constantI64(2),
                                    operation(Opcode::lessI64),
                                    operation(Opcode::returnValue)});
    tests.expect(comparison.program.has_value(), "less.i64 produces a bool result");
}

EMBER_TEST("verifier rejects unsafe local layouts and malformed opcodes") {
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

EMBER_TEST("verifier rejects malformed merges, calls, and unreachable instructions") {
    using ember::bytecode::Instruction;
    using ember::bytecode::Opcode;
    using ember::bytecode::Program;
    using ember::semantic::FunctionKind;
    using ember::semantic::Type;

    const auto makeFunction = [](std::vector<Instruction> code) {
        return Program{
            .functions = {{.id = 0,
                           .kind = FunctionKind::user,
                           .signature = {.parameterTypes = {}, .returnType = Type::voidType},
                           .localCount = 0,
                           .localTypes = {},
                           .code = std::move(code)}}};
    };

    const auto incompatibleMerge = ember::bytecode::Verifier{}.verify(makeFunction(
        {{.opcode = Opcode::constant, .operand = 0, .value = ember::bytecode::Value{true}},
         {.opcode = Opcode::jumpIfFalse, .operand = 4, .value = std::nullopt},
         {.opcode = Opcode::constant,
          .operand = 0,
          .value = ember::bytecode::Value{std::int64_t{1}}},
         {.opcode = Opcode::jump, .operand = 5, .value = std::nullopt},
         {.opcode = Opcode::jump, .operand = 5, .value = std::nullopt},
         {.opcode = Opcode::returnVoid, .operand = 0, .value = std::nullopt}}));
    tests.expect(!incompatibleMerge.program.has_value(),
                 "incompatible stack states at a merge are rejected");

    Program malformedCall{
        .functions = {
            {.id = 0,
             .kind = FunctionKind::user,
             .signature = {.parameterTypes = {}, .returnType = Type::voidType},
             .localCount = 0,
             .localTypes = {},
             .code =
                 {{.opcode = Opcode::constant, .operand = 0, .value = ember::bytecode::Value{true}},
                  {.opcode = Opcode::call, .operand = 1, .value = std::nullopt},
                  {.opcode = Opcode::returnVoid, .operand = 0, .value = std::nullopt}}},
            {.id = 1,
             .kind = FunctionKind::user,
             .signature = {.parameterTypes = {Type::i64}, .returnType = Type::voidType},
             .localCount = 1,
             .localTypes = {Type::i64},
             .code = {{.opcode = Opcode::returnVoid, .operand = 0, .value = std::nullopt}}},
        }};
    const auto invalidCall = ember::bytecode::Verifier{}.verify(std::move(malformedCall));
    tests.expect(!invalidCall.program.has_value(),
                 "call arguments with the wrong type are rejected");

    const auto invalidUnreachable = ember::bytecode::Verifier{}.verify(
        makeFunction({{.opcode = Opcode::jump, .operand = 2, .value = std::nullopt},
                      {.opcode = Opcode::load, .operand = 0, .value = std::nullopt},
                      {.opcode = Opcode::returnVoid, .operand = 0, .value = std::nullopt}}));
    tests.expect(!invalidUnreachable.program.has_value(),
                 "unreachable instructions must still be well-formed");
}

EMBER_TEST("verifier rejects invalid returns, kinds, types, and host payloads") {
    const auto makeFunction = [](ember::semantic::Type returnType,
                                 std::vector<ember::bytecode::Instruction> code) {
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

EMBER_TEST("VM enforces frame limit and executes a jump to instruction zero") {
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
    if (!recursiveVerified.program) {
        tests.expect(false, "recursive frame-limit bytecode verifies");
        return;
    }
    auto recursiveVm =
        ember::runtime::VirtualMachine::create(std::move(*recursiveVerified.program));
    const auto recursiveResult = recursiveVm.execute(0);
    tests.expect(recursiveResult.result.error.has_value() &&
                     recursiveResult.result.error->code == "R5006",
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
    if (!targetZeroVerified.program) {
        tests.expect(false, "target-zero bytecode verifies");
        return;
    }
    auto targetZeroVm =
        ember::runtime::VirtualMachine::create(std::move(*targetZeroVerified.program));
    const auto targetZeroResult = targetZeroVm.execute(0, {std::int64_t{2}});
    tests.expect(targetZeroResult.result.value.has_value() &&
                     std::get<std::int64_t>(*targetZeroResult.result.value) == 0,
                 "backward jump to instruction zero executes");
}
} // namespace
