#include "ember/ir/dump.hpp"
#include "ember/ir/optimization.hpp"
#include "ember/ir/verifier.hpp"

#include "test_harness.hpp"

EMBER_TEST("IR target verifies, optimizes and dumps a function without bytecode lowering") {
    using ember::core::Type;
    using ember::ir::Instruction;
    using ember::ir::Terminator;

    auto verified =
        ember::ir::Verifier{}.verify({.id = 0,
                                      .signature = {.parameterTypes = {}, .returnType = Type::i64},
                                      .localTypes = {},
                                      .valueTypes = {Type::i64},
                                      .blocks = {{.id = 0,
                                                  .instructions = {Instruction::constantI64(0, 42)},
                                                  .terminator = Terminator::returnValue(0)}}});
    tests.expect(verified.function.has_value() && verified.diagnostics.empty(),
                 "the standalone IR consumer verifies a constant return");
    if (!verified.function)
        return;
    auto optimized = ember::ir::OptimizationPipeline{}.run(*verified.function);
    tests.expect(optimized.function.has_value() && optimized.diagnostics.empty(),
                 "the standalone IR consumer can run optimization passes");
    if (!optimized.function)
        return;
    tests.expect(!ember::ir::dump(*optimized.function).empty(),
                 "the standalone IR consumer can dump the optimized function");
}
