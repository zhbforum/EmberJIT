#include "ember/bytecode/bytecode.hpp"

#include "test_harness.hpp"

#include <cstdint>

EMBER_TEST("bytecode target verifies and dumps a program without semantic adapters") {
    using ember::bytecode::Opcode;
    using ember::core::Type;

    auto verified = ember::bytecode::Verifier{}.verify(
        {.functions = {
             {.id = 0,
              .kind = ember::core::FunctionKind::user,
              .signature = {.parameterTypes = {}, .returnType = Type::i64},
              .localCount = 0,
              .localTypes = {},
              .code = {{Opcode::constant, 0, std::int64_t{42}}, {Opcode::returnValue, 0, {}}}}}});
    tests.expect(verified.program.has_value() && verified.diagnostics.empty(),
                 "the standalone bytecode consumer verifies a constant return");
    if (!verified.program)
        return;
    tests.expect(!ember::bytecode::dump(*verified.program).empty(),
                 "the standalone bytecode consumer can dump verified bytecode");
}
