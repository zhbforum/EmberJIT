#include "ember/bytecode/bytecode.hpp"
#include "ember/frontend/lexer.hpp"
#include "ember/frontend/parser.hpp"
#include "ember/ir/bytecode_lowerer.hpp"
#include "ember/ir/dump.hpp"
#include "ember/ir/optimization.hpp"
#include "ember/ir/verifier.hpp"
#include "ember/semantic/analyzer.hpp"
#include "test_harness.hpp"

#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace
{
using ember::ir::BasicBlock;
using ember::ir::BlockId;
using ember::ir::Function;
using ember::ir::Instruction;
using ember::ir::Opcode;
using ember::ir::Terminator;
using ember::ir::TerminatorKind;
using ember::semantic::Type;

[[nodiscard]] auto compileAndVerify(std::string sourceText)
    -> std::optional<ember::bytecode::VerifiedProgram>
{
    ember::support::SourceText source{ember::support::SourceId{91}, "ir.ember", std::move(sourceText)};
    const auto tokens = ember::frontend::Lexer{}.lex(source);
    if (!tokens.diagnostics.empty())
        return std::nullopt;
    const auto parsed = ember::frontend::Parser{}.parse(source, tokens.tokens);
    if (!parsed.program || !parsed.diagnostics.empty())
        return std::nullopt;
    const auto typed = ember::semantic::SemanticAnalyzer{}.analyze(*parsed.program, source);
    if (!typed.program || !typed.diagnostics.empty())
        return std::nullopt;
    auto compiled = ember::bytecode::Compiler{}.compile(*typed.program);
    if (!compiled.program)
        return std::nullopt;
    auto verified = ember::bytecode::Verifier{}.verify(std::move(*compiled.program));
    return std::move(verified.program);
}

[[nodiscard]] Function baseI64Function(std::vector<BasicBlock> blocks,
                                       std::vector<Type> valueTypes,
                                       std::vector<Type> localTypes = {})
{
    return {.id = 0,
            .signature = {.parameterTypes = {}, .returnType = Type::i64},
            .localTypes = std::move(localTypes),
            .valueTypes = std::move(valueTypes),
            .blocks = std::move(blocks)};
}

[[nodiscard]] bool rejects(Function function)
{
    return !ember::ir::Verifier{}.verify(std::move(function)).function.has_value();
}

EMBER_TEST("IR lowering creates explicit CFG blocks and virtual registers")
{
    auto bytecode = compileAndVerify(
        "fn main() -> i64 { let n: i64 = 2; while n > 0 { n = n - 1; } return n; }");
    if (!bytecode)
    {
        tests.expect(false, "i64 loop bytecode verifies");
        return;
    }
    const auto lowered = ember::ir::Lowerer{}.lower(*bytecode, 0);
    tests.expect(lowered.function.has_value(), "i64 loop lowers to verified IR");
    if (!lowered.function)
        return;
    const auto &function = lowered.function->function();
    tests.expect(function.blocks.size() == 5,
                 "loop lowering adds a preheader before entry, condition, body, and exit");
    tests.expect(function.valueTypes.size() == 8, "each stack value becomes a typed virtual register");
    const auto dump = ember::ir::dump(*lowered.function);
    tests.expect(dump.find("block b1:\n") != std::string::npos &&
                     dump.find("gt.i64") != std::string::npos &&
                     dump.find("branch_if_false") != std::string::npos &&
                     dump.find("branch b2\n") != std::string::npos,
                 "IR dump exposes loop CFG and control-flow merge");
}

EMBER_TEST("IR lowering uses a one-time preheader for parameter initialization")
{
    auto bytecode = compileAndVerify(
        "fn countdown(n: i64) -> i64 { while n > 0 { n = n - 1; } return n; }");
    if (!bytecode)
    {
        tests.expect(false, "parameterized loop bytecode verifies");
        return;
    }
    const auto lowered = ember::ir::Lowerer{}.lower(*bytecode, 0);
    tests.expect(lowered.function.has_value(), "parameterized loop lowers to verified IR");
    if (!lowered.function)
        return;

    const auto &blocks = lowered.function->function().blocks;
    const auto &preheader = blocks.front();
    tests.expect(preheader.instructions.size() == 2 &&
                     preheader.instructions[0].opcode == Opcode::parameter &&
                     preheader.instructions[1].opcode == Opcode::storeLocal &&
                     preheader.terminator.kind == TerminatorKind::branch &&
                     preheader.terminator.trueTarget == 1,
                 "entry preheader initializes parameters and enters bytecode block b1 once");

    bool backedgeToBytecodeEntry{};
    bool hasEntryPredecessor{};
    for (const auto &block : blocks)
    {
        const auto &terminator = block.terminator;
        if (terminator.kind == TerminatorKind::branch)
        {
            backedgeToBytecodeEntry = backedgeToBytecodeEntry ||
                                      (block.id != 0 && terminator.trueTarget == 1);
            hasEntryPredecessor = hasEntryPredecessor || terminator.trueTarget == 0;
        }
        else if (terminator.kind == TerminatorKind::branchIfFalse)
        {
            hasEntryPredecessor = hasEntryPredecessor || terminator.falseTarget == 0 ||
                                   terminator.trueTarget == 0;
        }
    }
    tests.expect(backedgeToBytecodeEntry, "loop backedge targets bytecode entry rather than preheader");
    tests.expect(!hasEntryPredecessor, "no CFG terminator targets the one-time preheader");
}

EMBER_TEST("IR verifier rejects non-local virtual-register uses")
{
    const auto laterDefinition = baseI64Function(
        {{.id = 0,
          .instructions = {Instruction::constantI64(0, 0), Instruction::constantI64(1, 1),
                           Instruction::binaryI64(Opcode::lessI64, 2, 0, 1)},
          .terminator = Terminator::branchIfFalse(2, 2, 1)},
         {.id = 1, .instructions = {}, .terminator = Terminator::returnValue(3)},
         {.id = 2, .instructions = {}, .terminator = Terminator::branch(3)},
         {.id = 3,
          .instructions = {Instruction::constantI64(3, 1)},
          .terminator = Terminator::returnValue(3)}},
        {Type::i64, Type::i64, Type::boolean, Type::i64});
    tests.expect(rejects(laterDefinition),
                 "terminator cannot use a value defined in a later reachable block");

    const auto diamond = baseI64Function(
        {{.id = 0,
          .instructions = {Instruction::constantI64(0, 0), Instruction::constantI64(1, 1),
                           Instruction::binaryI64(Opcode::lessI64, 2, 0, 1)},
          .terminator = Terminator::branchIfFalse(2, 2, 1)},
         {.id = 1,
          .instructions = {Instruction::constantI64(3, 42)},
          .terminator = Terminator::branch(3)},
         {.id = 2, .instructions = {}, .terminator = Terminator::branch(3)},
         {.id = 3,
          .instructions = {Instruction::binaryI64(Opcode::addI64, 4, 3, 0)},
          .terminator = Terminator::returnValue(4)}},
        {Type::i64, Type::i64, Type::boolean, Type::i64, Type::i64});
    tests.expect(rejects(diamond), "diamond merge cannot read a value defined on one predecessor");
}

EMBER_TEST("IR verifier validates every terminator and its canonical operands")
{
    const auto malformedDeadBlock = baseI64Function(
        {{.id = 0,
          .instructions = {Instruction::constantI64(0, 1)},
          .terminator = Terminator::returnValue(0)},
         {.id = 1,
          .instructions = {},
          .terminator = {.kind = TerminatorKind::branch, .trueTarget = 999}}},
        {Type::i64});
    tests.expect(rejects(malformedDeadBlock), "unreachable blocks still receive structural validation");

    const auto invalidReturnOperands = baseI64Function(
        {{.id = 0,
          .instructions = {Instruction::constantI64(0, 1)},
          .terminator = {.kind = TerminatorKind::returnValue, .condition = 0, .value = 0}}},
        {Type::i64});
    tests.expect(rejects(invalidReturnOperands), "return rejects unused condition operands");

    const auto missingTerminator = baseI64Function(
        {{.id = 0, .instructions = {Instruction::constantI64(0, 1)}, .terminator = {}}}, {Type::i64});
    tests.expect(rejects(missingTerminator), "default-initialized terminator is invalid");

    const auto validDeadBlock = baseI64Function(
        {{.id = 0,
          .instructions = {Instruction::constantI64(0, 1)},
          .terminator = Terminator::returnValue(0)},
         {.id = 1,
          .instructions = {Instruction::constantI64(1, 2)},
          .terminator = Terminator::returnValue(1)}},
        {Type::i64, Type::i64});
    tests.expect(rejects(validDeadBlock), "structurally valid but unreachable IR block is rejected");

    const auto entryBackedge = baseI64Function(
        {{.id = 0,
          .instructions = {Instruction::constantI64(0, 1)},
          .terminator = Terminator::branch(1)},
         {.id = 1,
          .instructions = {Instruction::constantI64(1, 2)},
          .terminator = Terminator::branch(0)}},
        {Type::i64, Type::i64});
    tests.expect(rejects(entryBackedge), "entry block predecessor is rejected directly by the verifier");
}

EMBER_TEST("IR verifier rejects malformed instruction encodings and definitions")
{
    const auto nonCanonical = baseI64Function(
        {{.id = 0,
          .instructions = {{.opcode = Opcode::constantI64,
                            .result = 0,
                            .input = 7,
                            .constant = 1,
                            .arguments = {}}},
          .terminator = Terminator::returnValue(0)}},
        {Type::i64});
    tests.expect(rejects(nonCanonical), "constant rejects non-canonical unused operands");

    const auto nonDense = baseI64Function(
        {{.id = 0,
          .instructions = {Instruction::constantI64(1, 1)},
          .terminator = Terminator::returnValue(1)}},
        {Type::i64, Type::i64});
    tests.expect(rejects(nonDense), "virtual-register definitions must be dense and unique");

    const auto duplicateDefinition = baseI64Function(
        {{.id = 0,
          .instructions = {Instruction::constantI64(0, 1), Instruction::constantI64(0, 2)},
          .terminator = Terminator::returnValue(0)}},
        {Type::i64});
    tests.expect(rejects(duplicateDefinition), "duplicate virtual-register definitions are rejected");

    const auto parameterOutsideEntry = Function{
        .id = 0,
        .signature = {.parameterTypes = {Type::i64}, .returnType = Type::i64},
        .localTypes = {Type::i64},
        .valueTypes = {Type::i64, Type::i64},
        .blocks = {{.id = 0, .instructions = {}, .terminator = Terminator::branch(1)},
                   {.id = 1,
                    .instructions = {Instruction::parameter(0, 0), Instruction::storeLocal(0, 0),
                                     Instruction::loadLocal(1, 0)},
                    .terminator = Terminator::returnValue(1)}}};
    tests.expect(rejects(parameterOutsideEntry), "parameters are only valid in the entry prefix");
}

EMBER_TEST("IR verifier checks return types and local initialization merges")
{
    auto wrongReturn = baseI64Function(
        {{.id = 0,
          .instructions = {Instruction::constantI64(0, 1)},
          .terminator = Terminator::returnValue(0)}},
        {Type::i64});
    wrongReturn.signature.returnType = Type::f64;
    tests.expect(rejects(wrongReturn), "return value type must match the function signature");

    const auto partialInitialization = baseI64Function(
        {{.id = 0,
          .instructions = {Instruction::constantI64(0, 0), Instruction::constantI64(1, 1),
                           Instruction::binaryI64(Opcode::lessI64, 2, 0, 1)},
          .terminator = Terminator::branchIfFalse(2, 2, 1)},
         {.id = 1,
          .instructions = {Instruction::constantI64(3, 7), Instruction::storeLocal(0, 3)},
          .terminator = Terminator::branch(3)},
         {.id = 2, .instructions = {}, .terminator = Terminator::branch(3)},
         {.id = 3,
          .instructions = {Instruction::loadLocal(4, 0)},
          .terminator = Terminator::returnValue(4)}},
        {Type::i64, Type::i64, Type::boolean, Type::i64, Type::i64}, {Type::i64});
    tests.expect(rejects(partialInitialization), "local initialized on one merge edge is not definite");

    const auto completeInitialization = baseI64Function(
        {{.id = 0,
          .instructions = {Instruction::constantI64(0, 0), Instruction::constantI64(1, 1),
                           Instruction::binaryI64(Opcode::lessI64, 2, 0, 1)},
          .terminator = Terminator::branchIfFalse(2, 2, 1)},
         {.id = 1,
          .instructions = {Instruction::constantI64(3, 7), Instruction::storeLocal(0, 3)},
          .terminator = Terminator::branch(3)},
         {.id = 2,
          .instructions = {Instruction::constantI64(4, 8), Instruction::storeLocal(0, 4)},
          .terminator = Terminator::branch(3)},
         {.id = 3,
          .instructions = {Instruction::loadLocal(5, 0)},
          .terminator = Terminator::returnValue(5)}},
        {Type::i64, Type::i64, Type::boolean, Type::i64, Type::i64, Type::i64}, {Type::i64});
    tests.expect(!rejects(completeInitialization), "local initialized on every merge edge verifies");
}

EMBER_TEST("IR verifier reaches a stable local-initialization fixed point through loops")
{
    const auto loop = baseI64Function(
        {{.id = 0,
          .instructions = {Instruction::constantI64(0, 0), Instruction::storeLocal(0, 0)},
          .terminator = Terminator::branch(1)},
         {.id = 1,
          .instructions = {Instruction::loadLocal(1, 0), Instruction::constantI64(2, 3),
                           Instruction::binaryI64(Opcode::lessI64, 3, 1, 2)},
          .terminator = Terminator::branchIfFalse(3, 3, 2)},
         {.id = 2,
          .instructions = {Instruction::loadLocal(4, 0), Instruction::constantI64(5, 1),
                           Instruction::binaryI64(Opcode::addI64, 6, 4, 5),
                           Instruction::storeLocal(0, 6)},
          .terminator = Terminator::branch(1)},
         {.id = 3,
          .instructions = {Instruction::loadLocal(7, 0)},
          .terminator = Terminator::returnValue(7)}},
        {Type::i64, Type::i64, Type::i64, Type::boolean, Type::i64, Type::i64, Type::i64,
         Type::i64},
        {Type::i64});
    tests.expect(!rejects(loop), "loop dataflow converges with an initialized local");
}

EMBER_TEST("IR verifier accepts the declared f64 bool and void type model")
{
    const auto verifyIdentity = [](Type type)
    {
        return ember::ir::Verifier{}.verify(
            {.id = 0,
             .signature = {.parameterTypes = {type}, .returnType = type},
             .localTypes = {type},
             .valueTypes = {type, type},
             .blocks = {{.id = 0,
                         .instructions = {Instruction::parameter(0, 0), Instruction::storeLocal(0, 0),
                                          Instruction::loadLocal(1, 0)},
                         .terminator = Terminator::returnValue(1)}}})
            .function.has_value();
    };
    tests.expect(verifyIdentity(Type::f64), "f64 parameter local and return model verifies");
    tests.expect(verifyIdentity(Type::boolean), "bool parameter local and return model verifies");
    tests.expect(ember::ir::Verifier{}
                     .verify({.id = 0,
                              .signature = {.parameterTypes = {}, .returnType = Type::voidType},
                              .localTypes = {},
                              .valueTypes = {},
                              .blocks = {{.id = 0,
                                          .instructions = {},
                                          .terminator = Terminator::returnVoid()}}})
                     .function.has_value(),
                 "void return type model verifies without a runtime value");
}

EMBER_TEST("IR lowering fail-closes unsupported forms with precise diagnostics")
{
    auto callProgram = compileAndVerify(
        "fn id(value: i64) -> i64 { return value; } fn main() -> i64 { return id(1); }");
    if (!callProgram)
    {
        tests.expect(false, "call program bytecode verifies");
        return;
    }
    const auto callLowered = ember::ir::Lowerer{}.lower(*callProgram, 1);
    tests.expect(callLowered.function.has_value() && !callLowered.failure &&
                     callLowered.function->function().blocks.size() == 2 &&
                     callLowered.function->function().blocks[1].instructions.size() == 2 &&
                     callLowered.function->function().blocks[1].instructions[1].opcode ==
                         ember::ir::Opcode::callI64,
                 "i64 user call lowers to a verifier-checked native bridge instruction");

    auto boolProgram = compileAndVerify(
        "fn main() -> i64 { if true { return 1; } else { return 0; } }");
    if (!boolProgram)
    {
        tests.expect(false, "bool-condition bytecode verifies");
        return;
    }
    const auto boolLowered = ember::ir::Lowerer{}.lower(*boolProgram, 0);
    tests.expect(!boolLowered.function &&
                     boolLowered.failure == ember::ir::LoweringFailure::unsupported,
                 "bool literal condition is outside the initial native subset");

    auto boolLocalProgram = compileAndVerify(
        "fn main() -> i64 { let value: bool = true; if value { return 1; } else { return 0; } }");
    auto f64LocalProgram = compileAndVerify(
        "fn main() -> i64 { let value: f64 = 1.0; return 1; }");
    tests.expect(boolLocalProgram.has_value() &&
                     ember::ir::Lowerer{}.lower(*boolLocalProgram, 0).failure ==
                         ember::ir::LoweringFailure::unsupported,
                 "first-class bool local is rejected");
    tests.expect(f64LocalProgram.has_value() &&
                     ember::ir::Lowerer{}.lower(*f64LocalProgram, 0).failure ==
                         ember::ir::LoweringFailure::unsupported,
                 "f64 local in an i64 function is rejected");

    auto stackEdgeProgram = ember::bytecode::Verifier{}.verify(
        {.functions = {{.id = 0,
                        .kind = ember::semantic::FunctionKind::user,
                        .signature = {.parameterTypes = {}, .returnType = Type::i64},
                        .localCount = 0,
                        .localTypes = {},
                        .code = {{.opcode = ember::bytecode::Opcode::constant,
                                  .operand = 0,
                                  .value = ember::bytecode::Value{std::int64_t{1}}},
                                 {.opcode = ember::bytecode::Opcode::jump,
                                  .operand = 2,
                                  .value = std::nullopt},
                                 {.opcode = ember::bytecode::Opcode::returnValue,
                                  .operand = 0,
                                  .value = std::nullopt}}}}});
    tests.expect(stackEdgeProgram.program.has_value(), "non-empty stack edge is valid bytecode");
    if (stackEdgeProgram.program)
    {
        const auto stackEdgeLowered = ember::ir::Lowerer{}.lower(*stackEdgeProgram.program, 0);
        tests.expect(!stackEdgeLowered.function &&
                         stackEdgeLowered.failure == ember::ir::LoweringFailure::unsupported &&
                         !stackEdgeLowered.diagnostics.empty() &&
                         stackEdgeLowered.diagnostics.front().message.find("pc 1") != std::string::npos,
                     "non-empty stack edge is unsupported rather than an internal lowering failure");
    }

    const auto invalidId = ember::ir::Lowerer{}.lower(*callProgram, 999);
    tests.expect(!invalidId.function && invalidId.failure == ember::ir::LoweringFailure::invalidInput,
                 "missing function id is classified as invalid lowerer input");
}

EMBER_TEST("IR constant folding preserves wrapping i64 semantics and re-verifies its output")
{
    auto verified = ember::ir::Verifier{}.verify(baseI64Function(
        {{.id = 0,
          .instructions = {Instruction::constantI64(0, std::numeric_limits<std::int64_t>::max()),
                           Instruction::constantI64(1, 1),
                           Instruction::binaryI64(Opcode::addI64, 2, 0, 1),
                           Instruction::negateI64(3, 2)},
          .terminator = Terminator::returnValue(3)}},
        {Type::i64, Type::i64, Type::i64, Type::i64}));
    tests.expect(verified.function.has_value(), "constant-folding fixture verifies");
    if (!verified.function)
        return;

    const auto folded = ember::ir::ConstantFoldingPass{}.run(*verified.function);
    tests.expect(folded.function.has_value(), "constant folding postcondition passes the IR verifier");
    if (!folded.function)
        return;
    const auto &instructions = folded.function->function().blocks[0].instructions;
    tests.expect(instructions[2].opcode == Opcode::constantI64 &&
                     instructions[2].constant == std::numeric_limits<std::int64_t>::min() &&
                     instructions[3].opcode == Opcode::constantI64 &&
                     instructions[3].constant == std::numeric_limits<std::int64_t>::min(),
                 "constant folding uses defined two's-complement wraparound");

    auto divisionFixture = ember::ir::Verifier{}.verify(baseI64Function(
        {{.id = 0,
          .instructions = {Instruction::constantI64(0, 1), Instruction::constantI64(1, 0),
                           Instruction::binaryI64(Opcode::divI64, 2, 0, 1)},
          .terminator = Terminator::returnValue(2)}},
        {Type::i64, Type::i64, Type::i64}));
    tests.expect(divisionFixture.function.has_value(), "division folding fixture verifies");
    if (!divisionFixture.function)
        return;
    const auto divisionFolded = ember::ir::ConstantFoldingPass{}.run(*divisionFixture.function);
    tests.expect(divisionFolded.function.has_value() &&
                     divisionFolded.function->function().blocks[0].instructions[2].opcode == Opcode::divI64,
                 "constant folding retains division so its runtime error remains observable");

    auto remainderFixture = ember::ir::Verifier{}.verify(baseI64Function(
        {{.id = 0,
          .instructions = {Instruction::constantI64(0, 1), Instruction::constantI64(1, 0),
                           Instruction::binaryI64(Opcode::remI64, 2, 0, 1)},
          .terminator = Terminator::returnValue(2)}},
        {Type::i64, Type::i64, Type::i64}));
    tests.expect(remainderFixture.function.has_value(), "remainder folding fixture verifies");
    if (!remainderFixture.function)
        return;
    const auto remainderFolded = ember::ir::ConstantFoldingPass{}.run(*remainderFixture.function);
    tests.expect(remainderFolded.function.has_value() &&
                     remainderFolded.function->function().blocks[0].instructions[2].opcode == Opcode::remI64,
                 "constant folding retains remainder so its runtime error remains observable");

    auto comparisonFixture = ember::ir::Verifier{}.verify(baseI64Function(
        {{.id = 0,
          .instructions = {Instruction::constantI64(0, 2), Instruction::constantI64(1, 3),
                           Instruction::binaryI64(Opcode::lessI64, 2, 0, 1),
                           Instruction::constantI64(3, 0)},
          .terminator = Terminator::returnValue(3)}},
        {Type::i64, Type::i64, Type::boolean, Type::i64}));
    tests.expect(comparisonFixture.function.has_value(), "standalone comparison fixture verifies");
    if (!comparisonFixture.function)
        return;
    const auto comparisonFolded = ember::ir::ConstantFoldingPass{}.run(*comparisonFixture.function);
    tests.expect(comparisonFolded.function.has_value() &&
                     comparisonFolded.function->function().blocks[0].instructions[2].opcode == Opcode::lessI64,
                 "constant folding leaves comparisons to CFG simplification's terminator contract");
}

EMBER_TEST("IR propagation and DCE retain required effects while removing dead pure values")
{
    auto propagationFixture = ember::ir::Verifier{}.verify(baseI64Function(
        {{.id = 0,
          .instructions = {Instruction::constantI64(0, 7), Instruction::storeLocal(0, 0),
                           Instruction::loadLocal(1, 0)},
          .terminator = Terminator::returnValue(1)}},
        {Type::i64, Type::i64}, {Type::i64}));
    tests.expect(propagationFixture.function.has_value(), "constant-propagation fixture verifies");
    if (!propagationFixture.function)
        return;
    const auto propagated = ember::ir::ConstantPropagationPass{}.run(*propagationFixture.function);
    tests.expect(propagated.function.has_value() &&
                     propagated.function->function().blocks[0].instructions[2].opcode == Opcode::constantI64,
                 "local constant propagation replaces a same-block load and re-verifies IR");

    auto overwriteFixture = ember::ir::Verifier{}.verify(baseI64Function(
        {{.id = 0,
          .instructions = {Instruction::constantI64(0, 0), Instruction::storeLocal(0, 0),
                           Instruction::constantI64(1, 7), Instruction::storeLocal(0, 1),
                           Instruction::constantI64(2, 9), Instruction::storeLocal(1, 2),
                           Instruction::loadLocal(3, 0), Instruction::loadLocal(4, 1)},
          .terminator = Terminator::returnValue(3)}},
        {Type::i64, Type::i64, Type::i64, Type::i64, Type::i64}, {Type::i64, Type::i64}));
    tests.expect(overwriteFixture.function.has_value(), "multi-local propagation fixture verifies");
    if (!overwriteFixture.function)
        return;
    const auto overwritten = ember::ir::ConstantPropagationPass{}.run(*overwriteFixture.function);
    tests.expect(overwritten.function.has_value() &&
                     overwritten.function->function().blocks[0].instructions[6].constant == 7 &&
                     overwritten.function->function().blocks[0].instructions[7].constant == 9,
                 "propagation handles zero, overwrites, and independent locals");

    auto invalidationFixture = ember::ir::Verifier{}.verify(
        {.id = 0,
         .signature = {.parameterTypes = {Type::i64}, .returnType = Type::i64},
         .localTypes = {Type::i64},
         .valueTypes = {Type::i64, Type::i64, Type::i64},
         .blocks = {{.id = 0,
                     .instructions = {Instruction::parameter(0, 0), Instruction::constantI64(1, 7),
                                      Instruction::storeLocal(0, 1), Instruction::storeLocal(0, 0),
                                      Instruction::loadLocal(2, 0)},
                     .terminator = Terminator::returnValue(2)}}});
    tests.expect(invalidationFixture.function.has_value(), "nonconstant-store propagation fixture verifies");
    if (!invalidationFixture.function)
        return;
    const auto invalidated = ember::ir::ConstantPropagationPass{}.run(*invalidationFixture.function);
    tests.expect(invalidated.function.has_value() &&
                     invalidated.function->function().blocks[0].instructions[4].opcode == Opcode::loadLocal,
                 "a nonconstant store invalidates a previous local constant fact");

    auto edgeFixture = ember::ir::Verifier{}.verify(baseI64Function(
        {{.id = 0,
          .instructions = {Instruction::constantI64(0, 7), Instruction::storeLocal(0, 0)},
          .terminator = Terminator::branch(1)},
         {.id = 1,
          .instructions = {Instruction::loadLocal(1, 0)},
          .terminator = Terminator::returnValue(1)}},
        {Type::i64, Type::i64}, {Type::i64}));
    tests.expect(edgeFixture.function.has_value(), "cross-edge propagation fixture verifies");
    if (!edgeFixture.function)
        return;
    const auto edgePropagated = ember::ir::ConstantPropagationPass{}.run(*edgeFixture.function);
    tests.expect(edgePropagated.function.has_value() &&
                     edgePropagated.function->function().blocks[1].instructions[0].opcode == Opcode::loadLocal,
                 "constant propagation does not cross CFG edges");

    auto refoldingFixture = ember::ir::Verifier{}.verify(baseI64Function(
        {{.id = 0,
          .instructions = {Instruction::constantI64(0, 2), Instruction::storeLocal(0, 0),
                           Instruction::loadLocal(1, 0), Instruction::constantI64(2, 3),
                           Instruction::binaryI64(Opcode::addI64, 3, 1, 2)},
          .terminator = Terminator::returnValue(3)}},
        {Type::i64, Type::i64, Type::i64, Type::i64}, {Type::i64}));
    tests.expect(refoldingFixture.function.has_value(), "post-propagation folding fixture verifies");
    if (!refoldingFixture.function)
        return;
    const auto refolded = ember::ir::OptimizationPipeline{}.run(*refoldingFixture.function);
    tests.expect(refolded.function.has_value() &&
                     ember::ir::dump(*refolded.function).find("const.i64 5") != std::string::npos,
                 "pipeline folds arithmetic newly exposed by constant propagation");

    auto dceFixture = ember::ir::Verifier{}.verify(baseI64Function(
        {{.id = 0,
          .instructions = {Instruction::constantI64(0, 1), Instruction::constantI64(1, 42)},
          .terminator = Terminator::returnValue(1)}},
        {Type::i64, Type::i64}));
    tests.expect(dceFixture.function.has_value(), "DCE fixture verifies");
    if (!dceFixture.function)
        return;
    const auto eliminated = ember::ir::DeadCodeEliminationPass{}.run(*dceFixture.function);
    tests.expect(eliminated.function.has_value() && eliminated.function->function().valueTypes.size() == 1 &&
                     eliminated.function->function().blocks[0].instructions.size() == 1 &&
                     eliminated.function->function().blocks[0].terminator.value == 0,
                 "DCE removes only dead pure values and compacts virtual-register ids");

    auto effectFixture = ember::ir::Verifier{}.verify(baseI64Function(
        {{.id = 0,
          .instructions = {Instruction::constantI64(0, 1), Instruction::constantI64(1, 0),
                           Instruction::binaryI64(Opcode::divI64, 2, 0, 1),
                           Instruction::binaryI64(Opcode::remI64, 3, 0, 1),
                           Instruction::callI64(4, 7, {0}), Instruction::constantI64(5, 42)},
          .terminator = Terminator::returnValue(5)}},
        {Type::i64, Type::i64, Type::i64, Type::i64, Type::i64, Type::i64}));
    tests.expect(effectFixture.function.has_value(), "DCE observable-effect fixture verifies");
    if (!effectFixture.function)
        return;
    const auto effectsRetained = ember::ir::DeadCodeEliminationPass{}.run(*effectFixture.function);
    tests.expect(effectsRetained.function.has_value() &&
                     effectsRetained.function->function().blocks[0].instructions.size() == 6,
                 "DCE retains dead-result division, remainder, and calls because they are observable");

    auto remapFixture = ember::ir::Verifier{}.verify(baseI64Function(
        {{.id = 0,
          .instructions = {Instruction::constantI64(0, 99), Instruction::constantI64(1, 7),
                           Instruction::constantI64(2, 8), Instruction::binaryI64(Opcode::addI64, 3, 1, 2),
                           Instruction::storeLocal(0, 3), Instruction::loadLocal(4, 0),
                           Instruction::callI64(5, 7, {4, 1}), Instruction::constantI64(6, 42)},
          .terminator = Terminator::returnValue(6)}},
        {Type::i64, Type::i64, Type::i64, Type::i64, Type::i64, Type::i64, Type::i64}, {Type::i64}));
    tests.expect(remapFixture.function.has_value(), "DCE remapping fixture verifies");
    if (!remapFixture.function)
        return;
    const auto remapped = ember::ir::DeadCodeEliminationPass{}.run(*remapFixture.function);
    tests.expect(remapped.function.has_value() && remapped.function->function().valueTypes.size() == 6 &&
                     remapped.function->function().blocks[0].instructions[5].arguments ==
                         std::vector<ember::ir::ValueId>{3, 0},
                 "DCE remaps binary inputs, stores, loads, and call arguments after deletion");
    if (!remapped.function)
        return;
    const auto eliminatedAgain = ember::ir::DeadCodeEliminationPass{}.run(*remapped.function);
    tests.expect(eliminatedAgain.function.has_value() &&
                     ember::ir::dump(*eliminatedAgain.function) == ember::ir::dump(*remapped.function),
                 "DCE is idempotent");
}

EMBER_TEST("IR default optimization pipeline simplifies constant CFG and exposes the result in dumps")
{
    auto verified = ember::ir::Verifier{}.verify(baseI64Function(
        {{.id = 0,
          .instructions = {Instruction::constantI64(0, 2), Instruction::constantI64(1, 3),
                           Instruction::binaryI64(Opcode::addI64, 2, 0, 1),
                           Instruction::constantI64(3, 4),
                           Instruction::binaryI64(Opcode::mulI64, 4, 2, 3),
                           Instruction::constantI64(5, 0),
                           Instruction::binaryI64(Opcode::equalI64, 6, 4, 5),
                           Instruction::storeLocal(0, 4)},
          .terminator = Terminator::branchIfFalse(6, 2, 1)},
         {.id = 1,
          .instructions = {Instruction::constantI64(7, 99)},
          .terminator = Terminator::returnValue(7)},
         {.id = 2,
          .instructions = {Instruction::loadLocal(8, 0)},
          .terminator = Terminator::returnValue(8)}},
        {Type::i64, Type::i64, Type::i64, Type::i64, Type::i64, Type::i64, Type::boolean,
         Type::i64, Type::i64},
        {Type::i64}));
    tests.expect(verified.function.has_value(), "constant-CFG fixture verifies");
    if (!verified.function)
        return;

    const auto optimized = ember::ir::OptimizationPipeline{}.run(*verified.function);
    tests.expect(optimized.function.has_value(), "every default pass preserves the verifier contract");
    if (!optimized.function)
        return;
    const auto &function = optimized.function->function();
    const auto dump = ember::ir::dump(*optimized.function);
    tests.expect(function.blocks.size() == 2 && function.blocks[0].terminator.kind == TerminatorKind::branch &&
                     function.blocks[0].terminator.trueTarget == 1 &&
                     dump.find("const.i64 20") != std::string::npos &&
                     dump.find("add.i64") == std::string::npos &&
                     dump.find("branch_if_false") == std::string::npos,
                 "pipeline folds constants, removes the dead successor, and dumps optimized CFG");
    const auto optimizedAgain = ember::ir::OptimizationPipeline{}.run(*optimized.function);
    tests.expect(optimizedAgain.function.has_value() &&
                     ember::ir::dump(*optimizedAgain.function) == ember::ir::dump(*optimized.function),
                 "default optimization pipeline is idempotent");
}

EMBER_TEST("CFG simplification evaluates every i64 comparison only in branch conditions")
{
    const auto outcome = [](Opcode opcode, std::int64_t left, std::int64_t right)
        -> std::optional<std::int64_t>
    {
        auto verified = ember::ir::Verifier{}.verify(baseI64Function(
            {{.id = 0,
              .instructions = {Instruction::constantI64(0, left), Instruction::constantI64(1, right),
                               Instruction::binaryI64(opcode, 2, 0, 1)},
              .terminator = Terminator::branchIfFalse(2, 2, 1)},
             {.id = 1,
              .instructions = {Instruction::constantI64(3, 11)},
              .terminator = Terminator::returnValue(3)},
             {.id = 2,
              .instructions = {Instruction::constantI64(4, 22)},
              .terminator = Terminator::returnValue(4)}},
            {Type::i64, Type::i64, Type::boolean, Type::i64, Type::i64}));
        if (!verified.function)
            return std::nullopt;
        auto simplified = ember::ir::CfgSimplificationPass{}.run(*verified.function);
        if (!simplified.function || simplified.function->function().blocks.size() != 2)
            return std::nullopt;
        return simplified.function->function().blocks[1].instructions[0].constant;
    };

    tests.expect(outcome(Opcode::equalI64, 4, 4) == 11 && outcome(Opcode::equalI64, 4, 5) == 22 &&
                     outcome(Opcode::notEqualI64, 4, 4) == 22 && outcome(Opcode::notEqualI64, 4, 5) == 11 &&
                     outcome(Opcode::lessI64, 4, 5) == 11 && outcome(Opcode::lessI64, 5, 4) == 22 &&
                     outcome(Opcode::lessEqualI64, 4, 4) == 11 && outcome(Opcode::lessEqualI64, 5, 4) == 22 &&
                     outcome(Opcode::greaterI64, 5, 4) == 11 && outcome(Opcode::greaterI64, 4, 5) == 22 &&
                     outcome(Opcode::greaterEqualI64, 4, 4) == 11 &&
                     outcome(Opcode::greaterEqualI64, 4, 5) == 22,
                 "CFG simplification chooses the correct successor for true and false comparisons");

    auto sameTarget = ember::ir::Verifier{}.verify(
        {.id = 0,
         .signature = {.parameterTypes = {Type::i64}, .returnType = Type::i64},
         .localTypes = {Type::i64},
         .valueTypes = {Type::i64, Type::i64, Type::boolean, Type::i64},
         .blocks = {{.id = 0,
                     .instructions = {Instruction::parameter(0, 0), Instruction::constantI64(1, 0),
                                      Instruction::binaryI64(Opcode::equalI64, 2, 0, 1)},
                     .terminator = Terminator::branchIfFalse(2, 1, 1)},
                    {.id = 1,
                     .instructions = {Instruction::constantI64(3, 7)},
                     .terminator = Terminator::returnValue(3)}}});
    tests.expect(sameTarget.function.has_value(), "same-target CFG fixture verifies");
    if (!sameTarget.function)
        return;
    const auto sameTargetSimplified = ember::ir::CfgSimplificationPass{}.run(*sameTarget.function);
    tests.expect(sameTargetSimplified.function.has_value() &&
                     sameTargetSimplified.function->function().blocks[0].terminator.kind ==
                         TerminatorKind::branch,
                 "CFG simplification removes a conditional branch with identical targets");
}
} // namespace
