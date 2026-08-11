#include "ember/bytecode/bytecode.hpp"
#include "ember/frontend/lexer.hpp"
#include "ember/frontend/parser.hpp"
#include "ember/ir/bytecode_lowerer.hpp"
#include "ember/ir/optimization.hpp"
#include "ember/ir/verifier.hpp"
#include "ember/jit/baseline_compiler.hpp"
#include "ember/jit/platform.hpp"
#include "ember/runtime/native_code.hpp"
#include "ember/semantic/analyzer.hpp"

#include "test_harness.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#if EMBER_HAS_WIN64_JIT
#include <intrin.h>
#endif

namespace ember::runtime::test {
class NativeCodeHandleAccess {
public:
    [[nodiscard]] static void* entryAddress(const NativeCodeHandle& handle) noexcept {
        return handle.entryAddress();
    }
};
} // namespace ember::runtime::test

namespace {
#if EMBER_HAS_WIN64_JIT
struct BridgeProbe {
    bool called{};
    bool stackAligned{};
    bool argumentsValid{};
    bool expectsVoidResult{};
    bool failCall{};
};

std::uint64_t probeBridge(ember::jit::NativeFrame* caller,
                          std::uint64_t callee,
                          const std::uint64_t* arguments,
                          std::uint64_t argumentCount,
                          std::uint64_t* result) {
    auto* probe = static_cast<BridgeProbe*>(caller->callContext);
    probe->called = true;
    probe->stackAligned =
        (reinterpret_cast<std::uintptr_t>(_AddressOfReturnAddress()) & 0xFU) == 8U;
    probe->argumentsValid = callee == 1 && argumentCount == 1 && arguments != nullptr &&
                            arguments[0] == 41U &&
                            (probe->expectsVoidResult ? result == nullptr : result != nullptr);
    if (probe->failCall) {
        caller->errorCode = static_cast<std::uint64_t>(ember::jit::NativeFrameError::invalidCall);
        return static_cast<std::uint64_t>(ember::jit::NativeFrameError::invalidCall);
    }
    if (!probe->expectsVoidResult)
        *result = 42U;
    return static_cast<std::uint64_t>(ember::jit::NativeFrameError::none);
}

[[nodiscard]] std::optional<ember::jit::x64::MachineCode> makeCanaryCaller(void* target) {
    using ember::jit::x64::Condition;
    using ember::jit::x64::Emitter;
    using ember::jit::x64::Register;

    constexpr std::int64_t rbxCanary = 0x112233445566778LL;
    constexpr std::int64_t r12Canary = 0x223344556677889LL;
    constexpr std::int64_t r13Canary = 0x33445566778899ALL;
    constexpr std::int64_t r14Canary = 0x445566778899AABLL;
    Emitter emitter;
    const auto failure = emitter.createLabel();
    const auto done = emitter.createLabel();
    if (!emitter.push(Register::rbx) || !emitter.push(Register::r12) ||
        !emitter.push(Register::r13) || !emitter.push(Register::r14) ||
        !emitter.subtractStackPointer(40) || !emitter.moveImmediate64(Register::rbx, rbxCanary) ||
        !emitter.moveImmediate64(Register::r12, r12Canary) ||
        !emitter.moveImmediate64(Register::r13, r13Canary) ||
        !emitter.moveImmediate64(Register::r14, r14Canary) ||
        !emitter.moveImmediate64(
            Register::rax,
            static_cast<std::int64_t>(reinterpret_cast<std::intptr_t>(target))) ||
        !emitter.call(Register::rax) || !emitter.moveImmediate64(Register::r8, rbxCanary) ||
        !emitter.compare(Register::rbx, Register::r8) ||
        !emitter.jump(Condition::notEqual, failure) ||
        !emitter.moveImmediate64(Register::r8, r12Canary) ||
        !emitter.compare(Register::r12, Register::r8) ||
        !emitter.jump(Condition::notEqual, failure) ||
        !emitter.moveImmediate64(Register::r8, r13Canary) ||
        !emitter.compare(Register::r13, Register::r8) ||
        !emitter.jump(Condition::notEqual, failure) ||
        !emitter.moveImmediate64(Register::r8, r14Canary) ||
        !emitter.compare(Register::r14, Register::r8) ||
        !emitter.jump(Condition::notEqual, failure) || !emitter.moveImmediate64(Register::rax, 1) ||
        !emitter.jump(done) || !emitter.bind(failure) ||
        !emitter.moveImmediate64(Register::rax, 0) || !emitter.bind(done) ||
        !emitter.addStackPointer(40) || !emitter.pop(Register::r14) ||
        !emitter.pop(Register::r13) || !emitter.pop(Register::r12) || !emitter.pop(Register::rbx) ||
        !emitter.returnFromFunction())
        return std::nullopt;
    auto finalized = emitter.finalize();
    return std::move(finalized.code);
}
#endif

[[nodiscard]] auto lower(std::string sourceText) -> std::optional<ember::ir::VerifiedFunction> {
    ember::support::SourceText source{ember::support::SourceId{92},
                                      "baseline.ember",
                                      std::move(sourceText)};
    const auto lexed = ember::frontend::Lexer{}.lex(source);
    if (!lexed.diagnostics.empty())
        return std::nullopt;
    const auto parsed = ember::frontend::Parser{}.parse(source, lexed.tokens);
    if (!parsed.program || !parsed.diagnostics.empty())
        return std::nullopt;
    const auto typed = ember::semantic::SemanticAnalyzer{}.analyze(*parsed.program, source);
    if (!typed.program || !typed.diagnostics.empty())
        return std::nullopt;
    auto compiled = ember::bytecode::Compiler{}.compile(*typed.program);
    if (!compiled.program)
        return std::nullopt;
    auto verified = ember::bytecode::Verifier{}.verify(std::move(*compiled.program));
    if (!verified.program)
        return std::nullopt;
    auto lowered = ember::ir::Lowerer{}.lower(*verified.program, 0);
    return std::move(lowered.function);
}
} // namespace

EMBER_TEST("baseline compiler executes frame-based i64 arithmetic") {
    auto function = lower("fn add(left: i64, right: i64) -> i64 { return left + right; }");
    tests.expect(function.has_value(), "binary i64 function lowers to verified IR");
    if (!function)
        return;

    auto compiled = ember::jit::x64::BaselineCompiler{}.compile(*function);
    tests.expect(compiled.code.has_value() &&
                     compiled.error == ember::jit::x64::BaselineCompileError::none,
                 "supported binary arithmetic produces finalized machine code");
    if (!compiled.code)
        return;

#if EMBER_HAS_WIN64_JIT
    std::vector<std::uint64_t> spills(compiled.frameRequirements.spillCount);
    auto native = ember::runtime::NativeCodeHandle::publishWordFrame(std::move(*compiled.code));
    tests.expect(native.handle.has_value(), "baseline machine code is publishable on Win64");
    if (native.handle) {
        std::vector<std::uint64_t> locals{std::bit_cast<std::uint64_t>(std::int64_t{-17}), 25U};
        ember::runtime::NativeFrame frame{.locals = locals.data(),
                                          .localCount = locals.size(),
                                          .spills = spills.data(),
                                          .spillCount = spills.size()};
        tests.expect(native.handle->invokeI64Frame(frame) == 8,
                     "baseline machine code preserves i64 addition semantics");
    }
#endif
}

EMBER_TEST("baseline compiler supports flexible frame parameters and guarded division") {
    auto unary = lower("fn identity(value: i64) -> i64 { return value; }");
    tests.expect(unary.has_value(), "one-argument i64 function lowers to verified IR");
    if (unary) {
        const auto compiled = ember::jit::x64::BaselineCompiler{}.compile(*unary);
        tests.expect(compiled.code.has_value() &&
                         compiled.error == ember::jit::x64::BaselineCompileError::none,
                     "frame ABI admits a one-argument i64 function");
    }

    auto division = lower("fn quotient(left: i64, right: i64) -> i64 { return left / right; }");
    tests.expect(division.has_value(), "binary division lowers to verified IR");
    if (division) {
        const auto compiled = ember::jit::x64::BaselineCompiler{}.compile(*division);
        tests.expect(compiled.code.has_value() &&
                         compiled.error == ember::jit::x64::BaselineCompileError::none,
                     "division emits an explicit runtime-error guard");
    }

    auto remainder = lower("fn remainder(left: i64, right: i64) -> i64 { return left % right; }");
    tests.expect(remainder.has_value(), "binary remainder lowers to verified IR");
    if (!remainder)
        return;
    auto remainderCode = ember::jit::x64::BaselineCompiler{}.compile(*remainder);
    tests.expect(remainderCode.code.has_value(), "remainder emits guarded machine code");
#if EMBER_HAS_WIN64_JIT
    if (remainderCode.code) {
        std::vector<std::uint64_t> spills(remainderCode.frameRequirements.spillCount);
        auto native =
            ember::runtime::NativeCodeHandle::publishWordFrame(std::move(*remainderCode.code));
        tests.expect(native.handle.has_value(), "guarded remainder machine code is publishable");
        if (native.handle) {
            std::vector<std::uint64_t> locals{std::bit_cast<std::uint64_t>(std::int64_t{-17}), 5U};
            ember::runtime::NativeFrame frame{.locals = locals.data(),
                                              .localCount = locals.size(),
                                              .spills = spills.data(),
                                              .spillCount = spills.size()};
            tests.expect(native.handle->invokeI64Frame(frame) == -2 && frame.errorCode == 0,
                         "native remainder uses RDX while preserving the frame error state");
        }
    }
#endif
}

EMBER_TEST("baseline optimization pipeline reduces code size for a constant CFG") {
    auto function = lower("fn main() -> i64 { if 2 + 3 == 20 { return 99; } return 5; }");
    tests.expect(function.has_value(), "constant-CFG fixture lowers to verified IR");
    if (!function)
        return;

    const auto optimized = ember::ir::OptimizationPipeline{}.run(*function);
    tests.expect(optimized.function.has_value(), "constant-CFG optimization re-verifies IR");
    if (!optimized.function)
        return;

    const auto unoptimizedCode = ember::jit::x64::BaselineCompiler{}.compile(*function);
    const auto optimizedCode = ember::jit::x64::BaselineCompiler{}.compile(*optimized.function);
    tests.expect(unoptimizedCode.code.has_value() && optimizedCode.code.has_value() &&
                     optimizedCode.code->bytes().size() < unoptimizedCode.code->bytes().size(),
                 "constant folding, CFG pruning, and DCE reduce finalized baseline code size");
}

EMBER_TEST("baseline compiler rejects verifier-valid noncanonical preheaders") {
    using ember::ir::BasicBlock;
    using ember::ir::Function;
    using ember::ir::Instruction;
    using ember::ir::Terminator;
    using ember::semantic::Type;

    auto verified = ember::ir::Verifier{}.verify(
        Function{.id = 71,
                 .signature = {.parameterTypes = {Type::i64}, .returnType = Type::i64},
                 .localTypes = {Type::i64},
                 .valueTypes = {Type::i64, Type::i64, Type::i64},
                 .blocks = {{.id = 0,
                             .instructions = {Instruction::parameter(0, 0),
                                              Instruction::negateI64(1, 0),
                                              Instruction::storeLocal(0, 1)},
                             .terminator = Terminator::branch(1)},
                            {.id = 1,
                             .instructions = {Instruction::loadLocal(2, 0)},
                             .terminator = Terminator::returnValue(2)}}});
    tests.expect(verified.function.has_value(),
                 "regression IR remains valid under the general verifier");
    if (!verified.function)
        return;
    const auto compiled = ember::jit::x64::BaselineCompiler{}.compile(*verified.function);
    tests.expect(!compiled.code &&
                     compiled.error == ember::jit::x64::BaselineCompileError::unsupportedFunction,
                 "backend fails closed instead of silently skipping preheader semantics");
}

EMBER_TEST("baseline compiler uses a fixed aligned Win64 call frame") {
    auto function = lower("fn identity(value: i64) -> i64 { return value; }");
    tests.expect(function.has_value(), "ABI fixture lowers to verified IR");
    if (!function)
        return;

    const auto compiled = ember::jit::x64::BaselineCompiler{}.compile(*function);
    tests.expect(compiled.code.has_value(), "ABI fixture emits machine code");
    if (!compiled.code)
        return;

    constexpr std::array<std::byte, 14> expectedPrologue{std::byte{0x53},
                                                         std::byte{0x41},
                                                         std::byte{0x54},
                                                         std::byte{0x41},
                                                         std::byte{0x55},
                                                         std::byte{0x41},
                                                         std::byte{0x56},
                                                         std::byte{0x48},
                                                         std::byte{0x81},
                                                         std::byte{0xEC},
                                                         std::byte{0x28},
                                                         std::byte{0x00},
                                                         std::byte{0x00},
                                                         std::byte{0x00}};
    const auto bytes = compiled.code->bytes();
    tests.expect(
        bytes.size() >= expectedPrologue.size() &&
            std::ranges::equal(bytes.first(expectedPrologue.size()), expectedPrologue),
        "native prologue saves RBX/R12-R14 and reserves exactly 40 aligned call-area bytes");
    tests.expect(compiled.frameRequirements.spillCount == function->function().valueTypes.size() &&
                     compiled.frameRequirements.callArgumentCapacity == 0,
                 "runtime-owned spills contain only materialized virtual-register values");
}

EMBER_TEST("baseline ABI preserves nonvolatile registers across bridge and error paths") {
#if EMBER_HAS_WIN64_JIT
    const auto runUnderCanaryCaller = [](ember::jit::x64::BaselineCompileResult compiled,
                                         ember::jit::NativeFrame& frame) -> bool {
        if (!compiled.code)
            return false;
        auto target = ember::runtime::NativeCodeHandle::publishWordFrame(std::move(*compiled.code));
        if (!target.handle)
            return false;
        auto callerCode = makeCanaryCaller(
            ember::runtime::test::NativeCodeHandleAccess::entryAddress(*target.handle));
        if (!callerCode)
            return false;
        auto caller = ember::runtime::NativeCodeHandle::publishWordFrame(std::move(*callerCode));
        return caller.handle && caller.handle->invokeI64Frame(frame) == 1;
    };

    auto caller = lower("fn caller() -> i64 { return callee(41); } "
                        "fn callee(value: i64) -> i64 { return value; }");
    tests.expect(caller.has_value(), "bridge ABI fixture lowers to verified IR");
    if (!caller)
        return;
    auto bridgeCode = ember::jit::x64::BaselineCompiler{}.compile(*caller);
    std::vector<std::uint64_t> bridgeSpills(bridgeCode.frameRequirements.spillCount);
    std::vector<std::uint64_t> bridgeArguments(bridgeCode.frameRequirements.callArgumentCapacity);
    BridgeProbe normalProbe;
    ember::jit::NativeFrame normalFrame{.spills = bridgeSpills.data(),
                                        .spillCount = bridgeSpills.size(),
                                        .callArguments = bridgeArguments.data(),
                                        .callArgumentCapacity = bridgeArguments.size(),
                                        .callContext = &normalProbe,
                                        .callBridge = &probeBridge};
    tests.expect(runUnderCanaryCaller(std::move(bridgeCode), normalFrame) && normalProbe.called &&
                     normalProbe.stackAligned && normalProbe.argumentsValid &&
                     normalFrame.errorCode == 0,
                 "bridge call has aligned RSP, uses the fifth argument, and preserves RBX/R12-R14");

    auto voidCaller = lower("fn caller() -> i64 { sink(41); return 42; } "
                            "fn sink(value: i64) -> void { return; }");
    tests.expect(voidCaller.has_value(), "void bridge ABI fixture lowers to verified IR");
    if (!voidCaller)
        return;
    auto voidBridgeCode = ember::jit::x64::BaselineCompiler{}.compile(*voidCaller);
    std::vector<std::uint64_t> voidSpills(voidBridgeCode.frameRequirements.spillCount);
    std::vector<std::uint64_t> voidArguments(voidBridgeCode.frameRequirements.callArgumentCapacity);
    BridgeProbe voidProbe{.expectsVoidResult = true};
    ember::jit::NativeFrame voidFrame{.spills = voidSpills.data(),
                                      .spillCount = voidSpills.size(),
                                      .callArguments = voidArguments.data(),
                                      .callArgumentCapacity = voidArguments.size(),
                                      .callContext = &voidProbe,
                                      .callBridge = &probeBridge};
    tests.expect(
        runUnderCanaryCaller(std::move(voidBridgeCode), voidFrame) && voidProbe.called &&
            voidProbe.stackAligned && voidProbe.argumentsValid && voidFrame.errorCode == 0,
        "void bridge calls pass nullptr as the fifth argument and materialize no result word");

    auto callFailure = lower("fn caller() -> i64 { return callee(41); } "
                             "fn callee(value: i64) -> i64 { return value; }");
    if (!callFailure) {
        tests.expect(false, "call-error ABI fixture lowers to verified IR");
        return;
    }
    auto callFailureCode = ember::jit::x64::BaselineCompiler{}.compile(*callFailure);
    std::vector<std::uint64_t> callFailureSpills(callFailureCode.frameRequirements.spillCount);
    std::vector<std::uint64_t> callFailureArguments(
        callFailureCode.frameRequirements.callArgumentCapacity);
    BridgeProbe failureProbe{.failCall = true};
    ember::jit::NativeFrame failureFrame{.spills = callFailureSpills.data(),
                                         .spillCount = callFailureSpills.size(),
                                         .callArguments = callFailureArguments.data(),
                                         .callArgumentCapacity = callFailureArguments.size(),
                                         .callContext = &failureProbe,
                                         .callBridge = &probeBridge};
    tests.expect(runUnderCanaryCaller(std::move(callFailureCode), failureFrame) &&
                     failureProbe.called && failureProbe.stackAligned &&
                     failureFrame.errorCode ==
                         static_cast<std::uint64_t>(ember::jit::NativeFrameError::invalidCall),
                 "call-error epilogue restores nonvolatile registers after a bridge failure");

    auto division = lower("fn quotient(left: i64, right: i64) -> i64 { return left / right; }");
    if (!division) {
        tests.expect(false, "division-error ABI fixture lowers to verified IR");
        return;
    }
    auto divisionCode = ember::jit::x64::BaselineCompiler{}.compile(*division);
    std::vector<std::uint64_t> divisionLocals{
        std::bit_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::min()),
        std::bit_cast<std::uint64_t>(std::int64_t{-1})};
    std::vector<std::uint64_t> divisionSpills(divisionCode.frameRequirements.spillCount);
    ember::jit::NativeFrame divisionFrame{.locals = divisionLocals.data(),
                                          .localCount = divisionLocals.size(),
                                          .spills = divisionSpills.data(),
                                          .spillCount = divisionSpills.size()};
    tests.expect(
        runUnderCanaryCaller(std::move(divisionCode), divisionFrame) &&
            divisionFrame.errorCode ==
                static_cast<std::uint64_t>(ember::jit::NativeFrameError::invalidI64Division),
        "division-error epilogue restores nonvolatile registers without executing IDIV");
#else
    tests.expect(true, "Win64 ABI probe is not applicable on this platform");
#endif
}
