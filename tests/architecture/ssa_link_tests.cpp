#include "ember/ssa/dump.hpp"
#include "ember/ssa/verifier.hpp"

#include "test_harness.hpp"

EMBER_TEST("SSA target verifies and dumps a function without semantic adapters") {
    using ember::core::Type;
    using ember::ssa::Instruction;
    using ember::ssa::Terminator;

    auto verified = ember::ssa::Verifier{}.verify(
        {.id = 0,
         .signature = {.parameterTypes = {}, .returnType = Type::i64},
         .valueTypes = {Type::i64},
         .blocks = {{.id = 0,
                     .parameters = {},
                     .instructions = {Instruction::constantI64(0, 42)},
                     .terminator = Terminator::returnValue(0)}}});
    tests.expect(verified.function.has_value() && verified.diagnostics.empty(),
                 "the standalone SSA consumer verifies a constant return");
    if (!verified.function)
        return;
    tests.expect(!ember::ssa::dump(*verified.function).empty(),
                 "the standalone SSA consumer can dump the verified function");
}
