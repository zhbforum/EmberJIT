#include "ember/semantic/typed_ast.hpp"
#include "ember/ssa/dump.hpp"
#include "ember/ssa/ssa.hpp"
#include "ember/ssa/verifier.hpp"

#include "test_harness.hpp"

#include <bit>
#include <cstdint>
#include <utility>
#include <vector>

namespace {
using ember::semantic::Type;
using ember::ssa::BlockParameter;
using ember::ssa::CallTarget;
using ember::ssa::CallTargetTable;
using ember::ssa::Function;
using ember::ssa::Instruction;
using ember::ssa::Opcode;
using ember::ssa::Terminator;

[[nodiscard]] Function validLoopFunction() {
    return {
        .id = 41,
        .signature = {.parameterTypes = {Type::i64}, .returnType = Type::i64},
        .valueTypes = {Type::i64,
                       Type::i64,
                       Type::i64,
                       Type::boolean,
                       Type::i64,
                       Type::i64,
                       Type::i64,
                       Type::i64},
        .blocks = {{.id = 0,
                    .parameters = {},
                    .instructions = {Instruction::parameter(0, 0), Instruction::constantI64(1, 0)},
                    .terminator = Terminator::branch(1, {1})},
                   {.id = 1,
                    .parameters = {{.value = 2, .type = Type::i64}},
                    .instructions = {Instruction::binary(Opcode::lessI64, 3, 2, 0)},
                    .terminator = Terminator::branchIfFalse(3,
                                                            {.target = 3, .arguments = {2}},
                                                            {.target = 2, .arguments = {2}})},
                   {.id = 2,
                    .parameters = {{.value = 4, .type = Type::i64}},
                    .instructions = {Instruction::constantI64(5, 1),
                                     Instruction::binary(Opcode::addI64, 6, 4, 5)},
                    .terminator = Terminator::branch(1, {6})},
                   {.id = 3,
                    .parameters = {{.value = 7, .type = Type::i64}},
                    .instructions = {},
                    .terminator = Terminator::returnValue(7)}}};
}

[[nodiscard]] Function edgeTypeMismatchFunction() {
    return {.id = 42,
            .signature = {.parameterTypes = {Type::i64}, .returnType = Type::i64},
            .valueTypes = {Type::i64, Type::boolean, Type::i64},
            .blocks = {
                {.id = 0,
                 .parameters = {},
                 .instructions = {Instruction::parameter(0, 0), Instruction::constantBool(1, true)},
                 .terminator = Terminator::branch(1, {1})},
                {.id = 1,
                 .parameters = {{.value = 2, .type = Type::i64}},
                 .instructions = {},
                 .terminator = Terminator::returnValue(2)}}};
}

[[nodiscard]] bool rejected(Function function) {
    return !ember::ssa::Verifier{}.verify(std::move(function)).function.has_value();
}

[[nodiscard]] bool rejected(Function function, CallTargetTable callTargets) {
    return !ember::ssa::Verifier{}
                .verify(std::move(function), std::move(callTargets))
                .function.has_value();
}

[[nodiscard]] Function nonLayoutOrderedFunction() {
    return {.id = 43,
            .signature = {.parameterTypes = {}, .returnType = Type::i64},
            .valueTypes = {Type::i64, Type::i64},
            .blocks = {
                {.id = 0,
                 .parameters = {},
                 .instructions = {Instruction::constantI64(1, 1), Instruction::constantI64(0, 0)},
                 .terminator = Terminator::returnValue(0)}}};
}

[[nodiscard]] Function f64DumpFunction() {
    return {.id = 44,
            .signature = {.parameterTypes = {}, .returnType = Type::f64},
            .valueTypes = {Type::f64, Type::f64, Type::f64},
            .blocks = {{.id = 0,
                        .parameters = {},
                        .instructions =
                            {
                                Instruction::constantF64(
                                    0,
                                    std::bit_cast<double>(std::uint64_t{0x8000000000000000})),
                                Instruction::constantF64(
                                    1,
                                    std::bit_cast<double>(std::uint64_t{0x3fb999999999999a})),
                                Instruction::constantF64(
                                    2,
                                    std::bit_cast<double>(std::uint64_t{0x7ff8000000000042})),
                            },
                        .terminator = Terminator::returnValue(2)}}};
}

[[nodiscard]] CallTargetTable validCallTargets() {
    return CallTargetTable{std::vector<CallTarget>{
        {.id = 100,
         .kind = ember::semantic::FunctionKind::user,
         .signature = {.parameterTypes = {Type::i64}, .returnType = Type::i64}},
        {.id = 101,
         .kind = ember::semantic::FunctionKind::host,
         .signature = {.parameterTypes = {Type::i64}, .returnType = Type::f64}},
        {.id = 102,
         .kind = ember::semantic::FunctionKind::user,
         .signature = {.parameterTypes = {Type::f64}, .returnType = Type::voidType}},
    }};
}

[[nodiscard]] Function validCallFunction() {
    return {
        .id = 45,
        .signature = {.parameterTypes = {Type::i64}, .returnType = Type::f64},
        .valueTypes = {Type::i64, Type::boolean, Type::i64, Type::f64},
        .blocks = {{.id = 0,
                    .parameters = {},
                    .instructions =
                        {Instruction::parameter(0, 0),
                         Instruction::constantBool(1, false),
                         Instruction::callI64(2, 100, {0}),
                         Instruction::callValue(3, 101, {0}, ember::semantic::FunctionKind::host),
                         Instruction::callVoid(102, {3})},
                    .terminator = Terminator::returnValue(3)}}};
}

[[nodiscard]] Function valueCallResultFunction() {
    return {
        .id = 46,
        .signature = {.parameterTypes = {}, .returnType = Type::i64},
        .valueTypes = {Type::i64},
        .blocks = {{.id = 0,
                    .parameters = {},
                    .instructions =
                        {Instruction::callValue(0, 101, {}, ember::semantic::FunctionKind::host)},
                    .terminator = Terminator::returnValue(0)}}};
}

[[nodiscard]] Function voidCallFunction() {
    return {.id = 47,
            .signature = {.parameterTypes = {}, .returnType = Type::voidType},
            .valueTypes = {},
            .blocks = {{.id = 0,
                        .parameters = {},
                        .instructions = {Instruction::callVoid(100, {})},
                        .terminator = Terminator::returnVoid()}}};
}

[[nodiscard]] Function typedOperandsFunction() {
    return {
        .id = 48,
        .signature = {.parameterTypes = {}, .returnType = Type::i64},
        .valueTypes = {Type::i64, Type::f64, Type::boolean, Type::i64, Type::f64, Type::boolean},
        .blocks = {{.id = 0,
                    .parameters = {},
                    .instructions = {Instruction::constantI64(0, 1),
                                     Instruction::constantF64(1, 1.0),
                                     Instruction::constantBool(2, true),
                                     Instruction::binary(Opcode::addI64, 3, 0, 0),
                                     Instruction::binary(Opcode::addF64, 4, 1, 1),
                                     Instruction::binary(Opcode::equalBool, 5, 2, 2)},
                    .terminator = Terminator::returnValue(3)}}};
}

[[nodiscard]] Function selfTargetFunction() {
    return {.id = 49,
            .signature = {.parameterTypes = {}, .returnType = Type::i64},
            .valueTypes = {Type::i64},
            .blocks = {{.id = 0,
                        .parameters = {},
                        .instructions = {Instruction::constantI64(0, 0)},
                        .terminator = Terminator::returnValue(0)}}};
}

[[nodiscard]] Function validRecursiveSelfCallFunction() {
    return {.id = 50,
            .signature = {.parameterTypes = {Type::i64}, .returnType = Type::i64},
            .valueTypes = {Type::i64, Type::i64},
            .blocks = {
                {.id = 0,
                 .parameters = {},
                 .instructions = {Instruction::parameter(0, 0), Instruction::callI64(1, 50, {0})},
                 .terminator = Terminator::returnValue(1)}}};
}
} // namespace

EMBER_TEST("SSA verifier accepts typed block parameters, branches, and loops") {
    const auto verified = ember::ssa::Verifier{}.verify(validLoopFunction());
    tests.expect(verified.function.has_value(), "well-formed SSA loop verifies");
    if (!verified.function)
        return;

    const auto firstDump = ember::ssa::dump(*verified.function);
    const auto secondDump = ember::ssa::dump(*verified.function);
    tests.expect(firstDump == secondDump, "verified SSA dump is deterministic");
    tests.expect(firstDump == "fn #41 (i64) -> i64\n"
                              "block b0:\n"
                              "  v0:i64 = param 0\n"
                              "  v1:i64 = const.i64 0\n"
                              "  branch b1(v1)\n"
                              "block b1(v2:i64):\n"
                              "  v3:bool = lt.i64 v2, v0\n"
                              "  branch_if_false v3, b3(v2), b2(v2)\n"
                              "block b2(v4:i64):\n"
                              "  v5:i64 = const.i64 1\n"
                              "  v6:i64 = add.i64 v4, v5\n"
                              "  branch b1(v6)\n"
                              "block b3(v7:i64):\n"
                              "  return v7\n",
                 "SSA dump prints block parameters and edge arguments deterministically");
}

EMBER_TEST("SSA verifier accepts dense value ids independent of layout definition order") {
    const auto verified = ember::ssa::Verifier{}.verify(nonLayoutOrderedFunction());
    tests.expect(verified.function.has_value(),
                 "dense SSA values need not be defined in textual ValueId order");
}

EMBER_TEST("SSA dump preserves every f64 bit exactly") {
    const auto verified = ember::ssa::Verifier{}.verify(f64DumpFunction());
    tests.expect(verified.function.has_value(), "bit-pattern f64 constants verify");
    if (!verified.function)
        return;

    tests.expect(ember::ssa::dump(*verified.function) == "fn #44 () -> f64\n"
                                                         "block b0:\n"
                                                         "  v0:f64 = const.f64 0x8000000000000000\n"
                                                         "  v1:f64 = const.f64 0x3fb999999999999a\n"
                                                         "  v2:f64 = const.f64 0x7ff8000000000042\n"
                                                         "  return v2\n",
                 "f64 dump uses deterministic IEEE-754 bit patterns");
}

EMBER_TEST("SSA verifier rejects invalid edge arguments and invalid CFG references") {
    auto arityMismatch = validLoopFunction();
    arityMismatch.blocks[0].terminator = Terminator::branch(1);
    tests.expect(rejected(std::move(arityMismatch)), "edge argument count mismatch is rejected");

    tests.expect(rejected(edgeTypeMismatchFunction()), "edge argument type mismatch is rejected");

    auto invalidTarget = validLoopFunction();
    invalidTarget.blocks[0].terminator = Terminator::branch(9, {1});
    tests.expect(rejected(std::move(invalidTarget)), "invalid CFG target is rejected");
}

EMBER_TEST("SSA verifier rejects invalid definitions, uses, and dominance") {
    auto duplicateValue = validLoopFunction();
    duplicateValue.blocks[1].parameters[0] = BlockParameter{.value = 1, .type = Type::i64};
    tests.expect(rejected(std::move(duplicateValue)), "duplicate SSA value definition is rejected");

    auto undefinedUse = validLoopFunction();
    undefinedUse.blocks[2].instructions[1] = Instruction::binary(Opcode::addI64, 6, 4, 99);
    tests.expect(rejected(std::move(undefinedUse)), "undefined SSA value use is rejected");

    auto nonDominatingUse = validLoopFunction();
    nonDominatingUse.blocks[2].instructions[1] = Instruction::binary(Opcode::addI64, 6, 4, 7);
    tests.expect(rejected(std::move(nonDominatingUse)), "non-dominating SSA value use is rejected");
}

EMBER_TEST("SSA verifier validates calls against a validated target table") {
    const auto accepted = ember::ssa::Verifier{}.verify(validCallFunction(), validCallTargets());
    tests.expect(accepted.function.has_value(), "well-typed user and host calls verify");

    auto missingTarget = validCallFunction();
    missingTarget.blocks[0].instructions[2].callee = 999;
    tests.expect(rejected(std::move(missingTarget), validCallTargets()),
                 "call target must be present in the target table");

    auto wrongKind = validCallFunction();
    wrongKind.blocks[0].instructions[3].calleeKind = ember::semantic::FunctionKind::user;
    tests.expect(rejected(std::move(wrongKind), validCallTargets()),
                 "call kind must match the target table");

    auto wrongArity = validCallFunction();
    wrongArity.blocks[0].instructions[3].arguments = {};
    tests.expect(rejected(std::move(wrongArity), validCallTargets()),
                 "call arity must match the target signature");

    auto wrongArgumentType = validCallFunction();
    wrongArgumentType.blocks[0].instructions[3].arguments = {1};
    tests.expect(rejected(std::move(wrongArgumentType), validCallTargets()),
                 "call arguments must match the target signature");

    tests.expect(rejected(valueCallResultFunction(),
                          CallTargetTable{std::vector<CallTarget>{
                              {.id = 101,
                               .kind = ember::semantic::FunctionKind::host,
                               .signature = {.parameterTypes = {}, .returnType = Type::f64}},
                          }}),
                 "call result type must match the target signature");

    tests.expect(rejected(valueCallResultFunction(),
                          CallTargetTable{std::vector<CallTarget>{
                              {.id = 101,
                               .kind = ember::semantic::FunctionKind::host,
                               .signature = {.parameterTypes = {}, .returnType = Type::voidType}},
                          }}),
                 "value calls cannot target void functions");

    tests.expect(rejected(voidCallFunction(),
                          CallTargetTable{std::vector<CallTarget>{
                              {.id = 100,
                               .kind = ember::semantic::FunctionKind::user,
                               .signature = {.parameterTypes = {}, .returnType = Type::i64}},
                          }}),
                 "void calls cannot target value functions");
}

EMBER_TEST("SSA verifier rejects malformed trusted call target tables") {
    tests.expect(rejected(validLoopFunction(),
                          CallTargetTable{std::vector<CallTarget>{
                              {.id = 100,
                               .kind = ember::semantic::FunctionKind::user,
                               .signature = {.parameterTypes = {}, .returnType = Type::i64}},
                              {.id = 100,
                               .kind = ember::semantic::FunctionKind::host,
                               .signature = {.parameterTypes = {}, .returnType = Type::f64}},
                          }}),
                 "duplicate target ids are rejected");

    tests.expect(rejected(validLoopFunction(),
                          CallTargetTable{std::vector<CallTarget>{
                              {.id = ember::ssa::noFunction,
                               .kind = ember::semantic::FunctionKind::user,
                               .signature = {.parameterTypes = {}, .returnType = Type::i64}},
                          }}),
                 "noFunction is not a valid target id");

    tests.expect(
        rejected(validLoopFunction(),
                 CallTargetTable{std::vector<CallTarget>{
                     {.id = 100,
                      .kind = ember::semantic::FunctionKind::user,
                      .signature = {.parameterTypes = {Type::voidType}, .returnType = Type::i64}},
                 }}),
        "target signatures cannot use void parameters");
}

EMBER_TEST("SSA verifier validates self call target metadata") {
    tests.expect(rejected(selfTargetFunction(),
                          CallTargetTable{std::vector<CallTarget>{
                              {.id = 49,
                               .kind = ember::semantic::FunctionKind::user,
                               .signature = {.parameterTypes = {}, .returnType = Type::f64}},
                          }}),
                 "self call target signatures must match the SSA function signature");

    tests.expect(rejected(selfTargetFunction(),
                          CallTargetTable{std::vector<CallTarget>{
                              {.id = 49,
                               .kind = ember::semantic::FunctionKind::host,
                               .signature = {.parameterTypes = {}, .returnType = Type::i64}},
                          }}),
                 "self call targets must describe a user function");
}

EMBER_TEST("SSA verifier accepts a recursive self call with matching metadata") {
    const auto verified = ember::ssa::Verifier{}.verify(
        validRecursiveSelfCallFunction(),
        CallTargetTable{std::vector<CallTarget>{
            {.id = 50,
             .kind = ember::semantic::FunctionKind::user,
             .signature = {.parameterTypes = {Type::i64}, .returnType = Type::i64}},
        }});
    tests.expect(verified.function.has_value(),
                 "matching self call metadata permits recursive calls");
}

EMBER_TEST("SSA verifier rejects each structural invariant group") {
    auto invalidFunctionId = validLoopFunction();
    invalidFunctionId.id = ember::ssa::noFunction;
    tests.expect(rejected(std::move(invalidFunctionId)),
                 "noFunction is not a valid SSA function id");

    auto useBeforeDefinition = nonLayoutOrderedFunction();
    useBeforeDefinition.blocks[0].instructions = {Instruction::constantI64(1, 1),
                                                  Instruction::binary(Opcode::addI64, 0, 1, 0)};
    tests.expect(rejected(std::move(useBeforeDefinition)),
                 "same-block uses before their definition are rejected");

    auto voidBlockParameter = validLoopFunction();
    voidBlockParameter.blocks[1].parameters[0].type = Type::voidType;
    tests.expect(rejected(std::move(voidBlockParameter)), "void block parameters are rejected");

    auto invalidBlockParameter = validLoopFunction();
    invalidBlockParameter.blocks[1].parameters[0].value = ember::ssa::noValue;
    tests.expect(rejected(std::move(invalidBlockParameter)),
                 "invalid block parameter values are rejected");

    auto wrongResultType = validLoopFunction();
    wrongResultType.valueTypes[6] = Type::boolean;
    tests.expect(rejected(std::move(wrongResultType)),
                 "instruction result types must match their opcode");

    auto wrongI64Operand = typedOperandsFunction();
    wrongI64Operand.blocks[0].instructions[3].left = 1;
    tests.expect(rejected(std::move(wrongI64Operand)), "i64 operands must be i64 values");

    auto wrongF64Operand = typedOperandsFunction();
    wrongF64Operand.blocks[0].instructions[4].left = 2;
    tests.expect(rejected(std::move(wrongF64Operand)), "f64 operands must be f64 values");

    auto wrongBoolOperand = typedOperandsFunction();
    wrongBoolOperand.blocks[0].instructions[5].left = 0;
    tests.expect(rejected(std::move(wrongBoolOperand)), "bool operands must be bool values");

    auto parameterAfterPrefix = validLoopFunction();
    parameterAfterPrefix.blocks[0].instructions.push_back(Instruction::parameter(1, 1));
    tests.expect(rejected(std::move(parameterAfterPrefix)),
                 "function parameters are restricted to the entry prefix");

    auto unreachable = validLoopFunction();
    unreachable.blocks.push_back(
        {.id = 4, .parameters = {}, .instructions = {}, .terminator = Terminator::returnValue(0)});
    tests.expect(rejected(std::move(unreachable)), "unreachable blocks are rejected");

    auto entryPredecessor = validLoopFunction();
    entryPredecessor.blocks[3].terminator = Terminator::branch(0);
    tests.expect(rejected(std::move(entryPredecessor)), "the entry block cannot have predecessors");

    auto wrongReturnKind = validLoopFunction();
    wrongReturnKind.blocks[3].terminator = Terminator::returnVoid();
    tests.expect(rejected(std::move(wrongReturnKind)),
                 "return_void must agree with the function signature");

    auto wrongReturnType = validLoopFunction();
    wrongReturnType.blocks[3].terminator = Terminator::returnValue(3);
    tests.expect(rejected(std::move(wrongReturnType)),
                 "return values must agree with the function signature");

    auto nonCanonicalUnusedField = validLoopFunction();
    nonCanonicalUnusedField.blocks[0].instructions[1].input = 0;
    tests.expect(rejected(std::move(nonCanonicalUnusedField)),
                 "unused instruction fields must use the canonical encoding");
}

EMBER_TEST("SSA verifier returns a capability only after deterministic successful verification") {
    const auto accepted = ember::ssa::Verifier{}.verify(validLoopFunction());
    tests.expect(accepted.function.has_value() && accepted.diagnostics.empty(),
                 "successful verification returns VerifiedSsaFunction without diagnostics");

    const auto firstRejected = ember::ssa::Verifier{}.verify(edgeTypeMismatchFunction());
    const auto secondRejected = ember::ssa::Verifier{}.verify(edgeTypeMismatchFunction());
    tests.expect(!firstRejected.function.has_value() && !firstRejected.diagnostics.empty(),
                 "failed verification does not return a capability");
    tests.expect(
        !secondRejected.function.has_value() && !secondRejected.diagnostics.empty() &&
            firstRejected.diagnostics.front().code == secondRejected.diagnostics.front().code &&
            firstRejected.diagnostics.front().message == secondRejected.diagnostics.front().message,
        "failed verification reports deterministic diagnostics");
}
