#include "ember/jit/baseline_compiler.hpp"

#include "test_harness.hpp"

EMBER_TEST("JIT target compiles verified IR without runtime integration") {
    using ember::core::Type;
    using ember::ir::Instruction;
    using ember::ir::Terminator;

    auto verified = ember::ir::Verifier{}.verify(
        {.id = 0,
         .signature = {.parameterTypes = {}, .returnType = Type::i64},
         .localTypes = {},
         .valueTypes = {Type::i64},
         .blocks = {{.id = 0, .instructions = {}, .terminator = Terminator::branch(1)},
                    {.id = 1,
                     .instructions = {Instruction::constantI64(0, 42)},
                     .terminator = Terminator::returnValue(0)}}});
    tests.expect(verified.function.has_value() && verified.diagnostics.empty(),
                 "the standalone JIT consumer can verify its input IR");
    if (!verified.function)
        return;
    const auto compiled = ember::jit::x64::BaselineCompiler{}.compile(*verified.function);
    tests.expect(compiled.code.has_value() &&
                     compiled.error == ember::jit::x64::BaselineCompileError::none,
                 "the standalone JIT consumer can compile a constant return");
    if (!compiled.code)
        return;
    tests.expect(!compiled.code->listing().empty(),
                 "the standalone JIT consumer can inspect the generated code");
}
