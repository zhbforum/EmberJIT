#include "ember/bytecode/bytecode.hpp"
#include "ember/jit/platform.hpp"
#include "ember/runtime/native_value.hpp"
#include "ember/runtime/vm.hpp"

#include "jit/native_code_test_access.hpp"
#include "runtime/runtime_function_test_access.hpp"
#include "test_harness.hpp"

#include <array>
#include <bit>
#include <cstdint>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

namespace
{
using ember::bytecode::Function;
using ember::bytecode::Instruction;
using ember::bytecode::Opcode;
using ember::bytecode::Program;
using ember::semantic::FunctionId;
using ember::semantic::FunctionKind;
using ember::semantic::Type;
using ember::test::TestContext;

[[nodiscard]] Function voidFunction(FunctionId id, std::vector<Instruction> code)
{
    return {.id = id,
            .kind = FunctionKind::user,
            .signature = {.parameterTypes = {}, .returnType = Type::voidType},
            .localCount = 0,
            .localTypes = {},
            .code = std::move(code)};
}

[[nodiscard]] std::optional<ember::bytecode::VerifiedProgram> verify(Program program)
{
    auto result = ember::bytecode::Verifier{}.verify(std::move(program));
    return std::move(result.program);
}

EMBER_TEST("native value decoder rejects non-canonical boolean words")
{
    tests.expect(!ember::runtime::decodeNativeValueWord(2U, Type::boolean).has_value() &&
                     ember::runtime::decodeNativeValueWord(0U, Type::boolean) ==
                         ember::bytecode::Value{false} &&
                     ember::runtime::decodeNativeValueWord(1U, Type::boolean) ==
                         ember::bytecode::Value{true},
                 "runtime native boundary accepts boolean words only in canonical 0/1 form");
}

EMBER_TEST("dispatch events are execution-local and emitted once across repeated execution")
{
    auto verified = verify({.functions = {
                             voidFunction(0,
                                          {{.opcode = Opcode::call, .operand = 1, .value = std::nullopt},
                                           {.opcode = Opcode::call, .operand = 1, .value = std::nullopt},
                                           {.opcode = Opcode::returnVoid,
                                            .operand = 0,
                                            .value = std::nullopt}}),
                             voidFunction(1, {{.opcode = Opcode::returnVoid,
                                               .operand = 0,
                                               .value = std::nullopt}}),
                         }});
    if (!verified)
    {
        tests.expect(false, "dispatch test bytecode verifies");
        return;
    }

    auto vm = ember::runtime::VirtualMachine::create(
        std::move(*verified),
        {.hotThreshold = 2, .jitEnabled = true, .profilingEnabled = true});
    const auto first = vm.execute(0);
    tests.expect(!first.result.error && first.hotEvents.size() == 1 &&
                     first.hotEvents.front().functionId == 1 &&
                     first.hotEvents.front().invocationCount == 2,
                 "callee becomes hot on its second dynamic call");

    const auto second = vm.execute(0);
    tests.expect(!second.result.error && second.hotEvents.size() == 1 &&
                     second.hotEvents.front().functionId == 0 &&
                     second.hotEvents.front().invocationCount == 2,
                 "entry transition belongs to the second execution only");
    const auto *entry = vm.function(0);
    const auto *callee = vm.function(1);
    tests.expect(entry != nullptr && entry->profiling().invocationCount == 2 &&
                     entry->profiling().isHot && callee != nullptr &&
                     callee->profiling().invocationCount == 4 && callee->profiling().isHot,
                 "all user calls share the dispatcher and retain independent counters");
}

EMBER_TEST("a recursive call can become hot without changing the active VM chain")
{
    auto verified = verify({.functions = {
                             voidFunction(0,
                                          {{.opcode = Opcode::constant,
                                            .operand = 0,
                                            .value = ember::bytecode::Value{std::int64_t{3}}},
                                           {.opcode = Opcode::call, .operand = 1, .value = std::nullopt},
                                           {.opcode = Opcode::returnVoid,
                                            .operand = 0,
                                            .value = std::nullopt}}),
                             {.id = 1,
                              .kind = FunctionKind::user,
                              .signature = {.parameterTypes = {Type::i64},
                                            .returnType = Type::voidType},
                              .localCount = 1,
                              .localTypes = {Type::i64},
                              .code = {{.opcode = Opcode::load, .operand = 0, .value = std::nullopt},
                                       {.opcode = Opcode::constant,
                                        .operand = 0,
                                        .value = ember::bytecode::Value{std::int64_t{0}}},
                                       {.opcode = Opcode::greaterI64,
                                        .operand = 0,
                                        .value = std::nullopt},
                                       {.opcode = Opcode::jumpIfFalse,
                                        .operand = 8,
                                        .value = std::nullopt},
                                       {.opcode = Opcode::load, .operand = 0, .value = std::nullopt},
                                       {.opcode = Opcode::constant,
                                        .operand = 0,
                                        .value = ember::bytecode::Value{std::int64_t{1}}},
                                       {.opcode = Opcode::subI64,
                                        .operand = 0,
                                        .value = std::nullopt},
                                       {.opcode = Opcode::call, .operand = 1, .value = std::nullopt},
                                       {.opcode = Opcode::returnVoid,
                                        .operand = 0,
                                        .value = std::nullopt}}},
                         }});
    if (!verified)
    {
        tests.expect(false, "recursive dispatch bytecode verifies");
        return;
    }

    auto vm = ember::runtime::VirtualMachine::create(
        std::move(*verified),
        {.hotThreshold = 3, .jitEnabled = true, .profilingEnabled = true});
    const auto report = vm.execute(0);
    tests.expect(!report.result.error && report.hotEvents.size() == 1 &&
                     report.hotEvents.front().functionId == 1 &&
                     report.hotEvents.front().invocationCount == 3,
                 "recursive function becomes hot on its third active invocation");
    const auto *recursive = vm.function(1);
    tests.expect(recursive != nullptr && recursive->profiling().invocationCount == 4 &&
                     recursive->profiling().isHot,
                 "all recursive frames complete in the VM after the transition");
}

EMBER_TEST("host calls are not profiled and --no-jit preserves profiling")
{
    auto verified = verify({.functions = {
                             {.id = 2,
                              .kind = FunctionKind::host,
                              .signature = {.parameterTypes = {}, .returnType = Type::i64},
                              .localCount = 0,
                              .localTypes = {},
                              .code = {}},
                             voidFunction(3,
                                          {{.opcode = Opcode::call, .operand = 2, .value = std::nullopt},
                                           {.opcode = Opcode::pop, .operand = 0, .value = std::nullopt},
                                           {.opcode = Opcode::returnVoid,
                                            .operand = 0,
                                            .value = std::nullopt}}),
                         }});
    if (!verified)
    {
        tests.expect(false, "host dispatch bytecode verifies");
        return;
    }

    auto vm = ember::runtime::VirtualMachine::create(
        std::move(*verified),
        {.hotThreshold = 1, .jitEnabled = false, .profilingEnabled = true});
    const auto report = vm.execute(3);
    const auto *host = vm.function(2);
    tests.expect(!report.result.error && report.hotEvents.size() == 1 &&
                     report.hotEvents.front().functionId == 3 && host != nullptr &&
                     host->profiling().invocationCount == 0,
                 "--no-jit does not disable profiling and host calls do not enter it");
}

EMBER_TEST("native v0.1 values preserve f64 edge semantics, bool results, and void returns")
{
    const auto makeProgram = []
    {
        return Program{.functions = {
                           {.id = 3,
                            .kind = FunctionKind::user,
                            .signature = {.parameterTypes = {Type::f64}, .returnType = Type::f64},
                            .localCount = 1,
                            .localTypes = {Type::f64},
                            .code = {{.opcode = Opcode::load, .operand = 0, .value = std::nullopt},
                                     {.opcode = Opcode::returnValue,
                                      .operand = 0,
                                      .value = std::nullopt}}},
                           {.id = 4,
                            .kind = FunctionKind::user,
                            .signature = {.parameterTypes = {Type::f64, Type::f64},
                                          .returnType = Type::boolean},
                            .localCount = 2,
                            .localTypes = {Type::f64, Type::f64},
                            .code = {{.opcode = Opcode::load, .operand = 0, .value = std::nullopt},
                                     {.opcode = Opcode::load, .operand = 1, .value = std::nullopt},
                                     {.opcode = Opcode::equalF64, .operand = 0, .value = std::nullopt},
                                     {.opcode = Opcode::returnValue,
                                      .operand = 0,
                                      .value = std::nullopt}}},
                           {.id = 5,
                            .kind = FunctionKind::user,
                            .signature = {.parameterTypes = {Type::f64, Type::f64},
                                          .returnType = Type::boolean},
                            .localCount = 2,
                            .localTypes = {Type::f64, Type::f64},
                            .code = {{.opcode = Opcode::load, .operand = 0, .value = std::nullopt},
                                     {.opcode = Opcode::load, .operand = 1, .value = std::nullopt},
                                     {.opcode = Opcode::lessF64, .operand = 0, .value = std::nullopt},
                                     {.opcode = Opcode::returnValue,
                                      .operand = 0,
                                      .value = std::nullopt}}},
                           voidFunction(6,
                                        {{.opcode = Opcode::constant,
                                          .operand = 0,
                                          .value = ember::bytecode::Value{1.0}},
                                         {.opcode = Opcode::negateF64,
                                          .operand = 0,
                                          .value = std::nullopt},
                                         {.opcode = Opcode::pop,
                                          .operand = 0,
                                          .value = std::nullopt},
                                         {.opcode = Opcode::returnVoid,
                                          .operand = 0,
                                          .value = std::nullopt}}),
                           {.id = 7,
                            .kind = FunctionKind::user,
                            .signature = {.parameterTypes = {Type::f64, Type::f64},
                                          .returnType = Type::f64},
                            .localCount = 2,
                            .localTypes = {Type::f64, Type::f64},
                            .code = {{.opcode = Opcode::load, .operand = 0, .value = std::nullopt},
                                     {.opcode = Opcode::load, .operand = 1, .value = std::nullopt},
                                     {.opcode = Opcode::addF64, .operand = 0, .value = std::nullopt},
                                     {.opcode = Opcode::load, .operand = 0, .value = std::nullopt},
                                     {.opcode = Opcode::load, .operand = 1, .value = std::nullopt},
                                     {.opcode = Opcode::subF64, .operand = 0, .value = std::nullopt},
                                     {.opcode = Opcode::addF64, .operand = 0, .value = std::nullopt},
                                     {.opcode = Opcode::load, .operand = 0, .value = std::nullopt},
                                     {.opcode = Opcode::load, .operand = 1, .value = std::nullopt},
                                     {.opcode = Opcode::mulF64, .operand = 0, .value = std::nullopt},
                                     {.opcode = Opcode::addF64, .operand = 0, .value = std::nullopt},
                                     {.opcode = Opcode::load, .operand = 0, .value = std::nullopt},
                                     {.opcode = Opcode::load, .operand = 1, .value = std::nullopt},
                                     {.opcode = Opcode::divF64, .operand = 0, .value = std::nullopt},
                                     {.opcode = Opcode::subF64, .operand = 0, .value = std::nullopt},
                                     {.opcode = Opcode::returnValue,
                                      .operand = 0,
                                      .value = std::nullopt}}},
                       }};
    };

    auto jitProgram = verify(makeProgram());
    auto vmProgram = verify(makeProgram());
    if (!jitProgram || !vmProgram)
    {
        tests.expect(false, "native v0.1 value corpus verifies");
        return;
    }
    auto jitVm = ember::runtime::VirtualMachine::create(
        std::move(*jitProgram), {.hotThreshold = 1, .jitEnabled = true, .profilingEnabled = true});
    auto vm = ember::runtime::VirtualMachine::create(
        std::move(*vmProgram), {.hotThreshold = 1, .jitEnabled = false, .profilingEnabled = true});
    const auto negativeZero = -0.0;
    const auto nan = std::numeric_limits<double>::quiet_NaN();
    const auto jitZero = jitVm.execute(3, {negativeZero});
    const auto vmZero = vm.execute(3, {negativeZero});
    const auto jitNanEqual = jitVm.execute(4, {nan, nan});
    const auto vmNanEqual = vm.execute(4, {nan, nan});
    const auto jitSignedZeroEqual = jitVm.execute(4, {negativeZero, 0.0});
    const auto vmSignedZeroEqual = vm.execute(4, {negativeZero, 0.0});
    const auto jitNanLess = jitVm.execute(5, {nan, 0.0});
    const auto vmNanLess = vm.execute(5, {nan, 0.0});
    const auto jitOrderedLess = jitVm.execute(5, {-1.0, 0.0});
    const auto vmOrderedLess = vm.execute(5, {-1.0, 0.0});
    const auto jitVoid = jitVm.execute(6);
    const auto vmVoid = vm.execute(6);
    const auto jitArithmetic = jitVm.execute(7, {6.0, 2.0});
    const auto vmArithmetic = vm.execute(7, {6.0, 2.0});

    tests.expect(!jitZero.result.error && !vmZero.result.error && jitZero.result.value &&
                     vmZero.result.value &&
                     std::bit_cast<std::uint64_t>(std::get<double>(*jitZero.result.value)) ==
                         std::bit_cast<std::uint64_t>(std::get<double>(*vmZero.result.value)),
                 "native f64 return preserves the signed-zero bit pattern");
    tests.expect(!jitNanEqual.result.error && !vmNanEqual.result.error &&
                     jitNanEqual.result.value == ember::bytecode::Value{false} &&
                     jitNanEqual.result.value == vmNanEqual.result.value &&
                     jitSignedZeroEqual.result.value == ember::bytecode::Value{true} &&
                     jitSignedZeroEqual.result.value == vmSignedZeroEqual.result.value,
                 "native equality preserves NaN and signed-zero semantics");
    tests.expect(!jitNanLess.result.error && !vmNanLess.result.error &&
                     jitNanLess.result.value == ember::bytecode::Value{false} &&
                     jitNanLess.result.value == vmNanLess.result.value &&
                     jitOrderedLess.result.value == ember::bytecode::Value{true} &&
                     jitOrderedLess.result.value == vmOrderedLess.result.value,
                 "native relational comparisons reject unordered operands and preserve ordered results");
    tests.expect(!jitVoid.result.error && !vmVoid.result.error && !jitVoid.result.value &&
                     !vmVoid.result.value,
                 "native void return does not materialize a runtime value");
    tests.expect(!jitArithmetic.result.error && !vmArithmetic.result.error &&
                     jitArithmetic.result.value == ember::bytecode::Value{21.0} &&
                     jitArithmetic.result.value == vmArithmetic.result.value,
                 "native SSE2 add, sub, mul, and div match the VM result");
#if EMBER_HAS_WIN64_JIT
    tests.expect(jitVm.function(3)->tier() == ember::runtime::ExecutionTier::native &&
                     jitVm.function(4)->tier() == ember::runtime::ExecutionTier::native &&
                     jitVm.function(5)->tier() == ember::runtime::ExecutionTier::native &&
                     jitVm.function(6)->tier() == ember::runtime::ExecutionTier::native &&
                     jitVm.function(7)->tier() == ember::runtime::ExecutionTier::native,
                 "v0.1 f64, bool, and void corpus publishes every user function natively");
#endif
}

EMBER_TEST("native floating and boolean operations match the VM across edge-case matrices")
{
    const auto floatExpected = [](Opcode opcode, double left, double right)
    {
        switch (opcode)
        {
        case Opcode::equalF64:
            return left == right;
        case Opcode::notEqualF64:
            return left != right;
        case Opcode::lessF64:
            return left < right;
        case Opcode::lessEqualF64:
            return left <= right;
        case Opcode::greaterF64:
            return left > right;
        case Opcode::greaterEqualF64:
            return left >= right;
        default:
            return false;
        }
    };
    const auto makeFloatProgram = [](Opcode opcode)
    {
        return Program{.functions = {{.id = 0,
                                      .kind = FunctionKind::user,
                                      .signature = {.parameterTypes = {Type::f64, Type::f64},
                                                    .returnType = Type::boolean},
                                      .localCount = 2,
                                      .localTypes = {Type::f64, Type::f64},
                                      .code = {{.opcode = Opcode::load, .operand = 0, .value = std::nullopt},
                                               {.opcode = Opcode::load, .operand = 1, .value = std::nullopt},
                                               {.opcode = opcode, .operand = 0, .value = std::nullopt},
                                               {.opcode = Opcode::returnValue,
                                                .operand = 0,
                                               .value = std::nullopt}}}}};
    };
    const auto floatingOperationName = [](Opcode opcode) -> const char *
    {
        switch (opcode)
        {
        case Opcode::equalF64:
            return "equal.f64";
        case Opcode::notEqualF64:
            return "not_equal.f64";
        case Opcode::lessF64:
            return "less.f64";
        case Opcode::lessEqualF64:
            return "less_equal.f64";
        case Opcode::greaterF64:
            return "greater.f64";
        case Opcode::greaterEqualF64:
            return "greater_equal.f64";
        default:
            return "unknown.f64";
        }
    };
    const auto nan = std::numeric_limits<double>::quiet_NaN();
    const std::array cases{std::pair{1.0, 1.0}, std::pair{-1.0, 0.0}, std::pair{1.0, 0.0},
                           std::pair{-0.0, 0.0}, std::pair{nan, 0.0}, std::pair{0.0, nan}};
    const std::array operations{Opcode::equalF64, Opcode::notEqualF64, Opcode::lessF64,
                                Opcode::lessEqualF64, Opcode::greaterF64, Opcode::greaterEqualF64};
    bool floatingMatrixMatches = true;
    for (const auto operation : operations)
    {
        auto jitProgram = verify(makeFloatProgram(operation));
        auto vmProgram = verify(makeFloatProgram(operation));
        if (!jitProgram || !vmProgram)
        {
            tests.expect(false, floatingOperationName(operation));
            floatingMatrixMatches = false;
            continue;
        }
        auto jitVm = ember::runtime::VirtualMachine::create(
            std::move(*jitProgram), {.hotThreshold = 1, .jitEnabled = true, .profilingEnabled = true});
        auto vm = ember::runtime::VirtualMachine::create(
            std::move(*vmProgram), {.hotThreshold = 1, .jitEnabled = false, .profilingEnabled = true});
        bool operationMatches = true;
        for (const auto &[left, right] : cases)
        {
            const auto jitReport = jitVm.execute(0, {left, right});
            const auto vmReport = vm.execute(0, {left, right});
            const auto expected = ember::bytecode::Value{floatExpected(operation, left, right)};
            operationMatches = operationMatches && !jitReport.result.error && !vmReport.result.error &&
                               jitReport.result.value == expected && vmReport.result.value == expected;
        }
#if EMBER_HAS_WIN64_JIT
        operationMatches = operationMatches &&
                           jitVm.function(0)->tier() == ember::runtime::ExecutionTier::native;
#endif
        floatingMatrixMatches = floatingMatrixMatches && operationMatches;
        tests.expect(operationMatches, floatingOperationName(operation));
    }
    tests.expect(floatingMatrixMatches, "all floating comparisons execute");

    const auto boolExpected = [](Opcode opcode, bool left, bool right)
    { return opcode == Opcode::equalBool ? left == right : left != right; };
    const auto makeBoolProgram = [](Opcode opcode)
    {
        return Program{.functions = {{.id = 0,
                                      .kind = FunctionKind::user,
                                      .signature = {.parameterTypes = {Type::boolean, Type::boolean},
                                                    .returnType = Type::boolean},
                                      .localCount = 2,
                                      .localTypes = {Type::boolean, Type::boolean},
                                      .code = {{.opcode = Opcode::load, .operand = 0, .value = std::nullopt},
                                               {.opcode = Opcode::load, .operand = 1, .value = std::nullopt},
                                               {.opcode = opcode, .operand = 0, .value = std::nullopt},
                                               {.opcode = Opcode::returnValue,
                                                .operand = 0,
                                                .value = std::nullopt}}}}};
    };
    const std::array boolCases{std::pair{false, false}, std::pair{true, true},
                               std::pair{true, false}};
    for (const auto operation : {Opcode::equalBool, Opcode::notEqualBool})
    {
        auto jitProgram = verify(makeBoolProgram(operation));
        auto vmProgram = verify(makeBoolProgram(operation));
        if (!jitProgram || !vmProgram)
        {
            tests.expect(false, "boolean comparison corpus verifies");
            continue;
        }
        auto jitVm = ember::runtime::VirtualMachine::create(
            std::move(*jitProgram), {.hotThreshold = 1, .jitEnabled = true, .profilingEnabled = true});
        auto vm = ember::runtime::VirtualMachine::create(
            std::move(*vmProgram), {.hotThreshold = 1, .jitEnabled = false, .profilingEnabled = true});
        bool operationMatches = true;
        for (const auto &[left, right] : boolCases)
        {
            const auto jitReport = jitVm.execute(0, {left, right});
            const auto vmReport = vm.execute(0, {left, right});
            const auto expected = ember::bytecode::Value{boolExpected(operation, left, right)};
            operationMatches = operationMatches && !jitReport.result.error && !vmReport.result.error &&
                               jitReport.result.value == expected && vmReport.result.value == expected;
        }
#if EMBER_HAS_WIN64_JIT
        operationMatches = operationMatches &&
                           jitVm.function(0)->tier() == ember::runtime::ExecutionTier::native;
#endif
        tests.expect(operationMatches, "native boolean equality operations match the VM");
    }

    const auto makeNegateProgram = []
    {
        return Program{.functions = {{.id = 0,
                                      .kind = FunctionKind::user,
                                      .signature = {.parameterTypes = {Type::f64}, .returnType = Type::f64},
                                      .localCount = 1,
                                      .localTypes = {Type::f64},
                                      .code = {{.opcode = Opcode::load, .operand = 0, .value = std::nullopt},
                                               {.opcode = Opcode::negateF64, .operand = 0, .value = std::nullopt},
                                               {.opcode = Opcode::returnValue,
                                                .operand = 0,
                                                .value = std::nullopt}}}}};
    };
    auto negateJitProgram = verify(makeNegateProgram());
    auto negateVmProgram = verify(makeNegateProgram());
    if (!negateJitProgram || !negateVmProgram)
    {
        tests.expect(false, "f64 negation corpus verifies");
        return;
    }
    auto negateJitVm = ember::runtime::VirtualMachine::create(
        std::move(*negateJitProgram), {.hotThreshold = 1, .jitEnabled = true, .profilingEnabled = true});
    auto negateVm = ember::runtime::VirtualMachine::create(
        std::move(*negateVmProgram), {.hotThreshold = 1, .jitEnabled = false, .profilingEnabled = true});
    bool negateMatches = true;
    for (const auto value : std::array{0.0, -0.0, nan})
    {
        const auto jitReport = negateJitVm.execute(0, {value});
        const auto vmReport = negateVm.execute(0, {value});
        const auto expectedWord = std::bit_cast<std::uint64_t>(value) ^ (std::uint64_t{1} << 63U);
        negateMatches = negateMatches && !jitReport.result.error && !vmReport.result.error &&
                        jitReport.result.value && vmReport.result.value &&
                        std::bit_cast<std::uint64_t>(std::get<double>(*jitReport.result.value)) == expectedWord &&
                        std::bit_cast<std::uint64_t>(std::get<double>(*vmReport.result.value)) == expectedWord;
    }
#if EMBER_HAS_WIN64_JIT
    negateMatches = negateMatches &&
                    negateJitVm.function(0)->tier() == ember::runtime::ExecutionTier::native;
#endif
    tests.expect(negateMatches, "native f64 negation preserves zero and NaN payload bits while flipping the sign");
}

EMBER_TEST("native user call_void passes no result word through the runtime bridge")
{
    const auto makeProgram = []
    {
        return Program{.functions = {
                           {.id = 0,
                            .kind = FunctionKind::user,
                            .signature = {.parameterTypes = {}, .returnType = Type::i64},
                            .localCount = 0,
                            .localTypes = {},
                            .code = {{.opcode = Opcode::constant,
                                      .operand = 0,
                                      .value = ember::bytecode::Value{1.25}},
                                     {.opcode = Opcode::call, .operand = 1, .value = std::nullopt},
                                     {.opcode = Opcode::constant,
                                      .operand = 0,
                                      .value = ember::bytecode::Value{std::int64_t{42}}},
                                     {.opcode = Opcode::returnValue,
                                      .operand = 0,
                                      .value = std::nullopt}}},
                           {.id = 1,
                            .kind = FunctionKind::user,
                            .signature = {.parameterTypes = {Type::f64}, .returnType = Type::voidType},
                            .localCount = 1,
                            .localTypes = {Type::f64},
                            .code = {{.opcode = Opcode::load, .operand = 0, .value = std::nullopt},
                                     {.opcode = Opcode::negateF64, .operand = 0, .value = std::nullopt},
                                     {.opcode = Opcode::pop, .operand = 0, .value = std::nullopt},
                                     {.opcode = Opcode::returnVoid,
                                      .operand = 0,
                                      .value = std::nullopt}}},
                       }};
    };
    auto jitProgram = verify(makeProgram());
    auto vmProgram = verify(makeProgram());
    if (!jitProgram || !vmProgram)
    {
        tests.expect(false, "user call_void corpus verifies");
        return;
    }
    auto jitVm = ember::runtime::VirtualMachine::create(
        std::move(*jitProgram), {.hotThreshold = 1, .jitEnabled = true, .profilingEnabled = true});
    auto vm = ember::runtime::VirtualMachine::create(
        std::move(*vmProgram), {.hotThreshold = 1, .jitEnabled = false, .profilingEnabled = true});
    const auto jitReport = jitVm.execute(0);
    const auto vmReport = vm.execute(0);
    tests.expect(!jitReport.result.error && !vmReport.result.error &&
                     jitReport.result.value == ember::bytecode::Value{std::int64_t{42}} &&
                     jitReport.result.value == vmReport.result.value,
                 "native caller resumes after a void callee without consuming a fabricated value");
#if EMBER_HAS_WIN64_JIT
    tests.expect(jitVm.function(0)->tier() == ember::runtime::ExecutionTier::native &&
                     jitVm.function(1)->tier() == ember::runtime::ExecutionTier::native,
                 "native user-to-user call_void executes through the typed bridge");
#endif
}

EMBER_TEST("native host builtins use direct typed Win64 calls instead of the user-call bridge")
{
    auto verified = verify({.functions = {
                               {.id = 2,
                                .kind = FunctionKind::host,
                                .signature = {.parameterTypes = {}, .returnType = Type::i64},
                                .localCount = 0,
                                .localTypes = {},
                                .code = {}},
                               {.id = 3,
                                .kind = FunctionKind::user,
                                .signature = {.parameterTypes = {}, .returnType = Type::i64},
                                .localCount = 0,
                                .localTypes = {},
                                .code = {{.opcode = Opcode::call,
                                          .operand = 2,
                                          .value = std::nullopt},
                                         {.opcode = Opcode::returnValue,
                                          .operand = 0,
                                          .value = std::nullopt}}},
                           }});
    if (!verified)
    {
        tests.expect(false, "direct native host-call fixture verifies");
        return;
    }
    auto vm = ember::runtime::VirtualMachine::create(
        std::move(*verified),
        {.hotThreshold = 1,
         .jitEnabled = true,
         .profilingEnabled = true,
         .forceNativeCallFailureForTesting = true});
    const auto report = vm.execute(3);
    tests.expect(!report.result.error && report.result.value &&
                     std::holds_alternative<std::int64_t>(*report.result.value),
                 "clock_ms remains callable after injected user-call bridge failure");
#if EMBER_HAS_WIN64_JIT
    tests.expect(vm.function(3)->tier() == ember::runtime::ExecutionTier::native,
                 "registered host builtin is emitted as a direct native call");
#endif
}

EMBER_TEST("copied VMs retain independent function indexes after the source is destroyed")
{
    std::optional<ember::runtime::VirtualMachine> copy;
    {
        auto verified = verify({.functions = {
                                 voidFunction(0, {{.opcode = Opcode::returnVoid,
                                                   .operand = 0,
                                                   .value = std::nullopt}}),
                             }});
        if (!verified)
        {
            tests.expect(false, "copy test bytecode verifies");
            return;
        }
        auto original = ember::runtime::VirtualMachine::create(
            std::move(*verified),
            {.hotThreshold = 2, .jitEnabled = true, .profilingEnabled = true});
        copy.emplace(original);
        const auto originalReport = original.execute(0);
        tests.expect(!originalReport.result.error && original.function(0) != nullptr &&
                         original.function(0)->profiling().invocationCount == 1,
                     "original VM keeps its own profiling state");
    }

    const auto copyReport = copy->execute(0);
    tests.expect(!copyReport.result.error && copy->function(0) != nullptr &&
                     copy->function(0)->profiling().invocationCount == 1,
                 "copied VM remains valid and does not reference destroyed storage");

    auto moved = std::move(*copy);
    const auto movedReport = moved.execute(0);
    tests.expect(!movedReport.result.error && moved.function(0) != nullptr &&
                     moved.function(0)->profiling().invocationCount == 2,
                 "moved VM keeps an index into its moved function storage");
}

EMBER_TEST("profiling baseline and hot tracking preserve execution results")
{
    const auto makeProgram = []
    {
        return Program{
            .functions = {{.id = 0,
                           .kind = FunctionKind::user,
                           .signature = {.parameterTypes = {}, .returnType = Type::i64},
                           .localCount = 0,
                           .localTypes = {},
                           .code = {{.opcode = Opcode::constant,
                                     .operand = 0,
                                     .value = ember::bytecode::Value{std::int64_t{42}}},
                                    {.opcode = Opcode::returnValue,
                                     .operand = 0,
                                     .value = std::nullopt}}}}};
    };
    auto unprofiled = verify(makeProgram());
    auto thresholdZero = verify(makeProgram());
    auto hot = verify(makeProgram());
    if (!unprofiled || !thresholdZero || !hot)
    {
        tests.expect(false, "profiling-equivalence bytecode verifies");
        return;
    }

    auto unprofiledVm = ember::runtime::VirtualMachine::create(
        std::move(*unprofiled),
        {.hotThreshold = 0, .jitEnabled = true, .profilingEnabled = false});
    auto thresholdZeroVm = ember::runtime::VirtualMachine::create(
        std::move(*thresholdZero),
        {.hotThreshold = 0, .jitEnabled = true, .profilingEnabled = true});
    auto hotVm = ember::runtime::VirtualMachine::create(
        std::move(*hot),
        {.hotThreshold = 1, .jitEnabled = true, .profilingEnabled = true});
    const auto unprofiledReport = unprofiledVm.execute(0);
    const auto thresholdZeroReport = thresholdZeroVm.execute(0);
    const auto hotReport = hotVm.execute(0);

    tests.expect(unprofiledReport.result.value == thresholdZeroReport.result.value &&
                     thresholdZeroReport.result.value == hotReport.result.value &&
                     !unprofiledReport.result.error && !thresholdZeroReport.result.error &&
                     !hotReport.result.error && unprofiledReport.hotEvents.empty() &&
                     thresholdZeroReport.hotEvents.empty() && hotReport.hotEvents.size() == 1,
                 "profiling changes only counters and events, not the execution result");
    tests.expect(unprofiledVm.function(0)->profiling().invocationCount == 0 &&
                     thresholdZeroVm.function(0)->profiling().invocationCount == 1 &&
                     hotVm.function(0)->profiling().invocationCount == 1,
                 "profiling-disabled execution provides a counter-free baseline");
}

EMBER_TEST("hot binary i64 callee publishes atomically and matches the VM")
{
    const auto makeProgram = []
    {
        return Program{.functions = {
                           {.id = 0,
                            .kind = FunctionKind::user,
                            .signature = {.parameterTypes = {}, .returnType = Type::i64},
                            .localCount = 0,
                            .localTypes = {},
                            .code = {{.opcode = Opcode::constant,
                                      .operand = 0,
                                      .value = ember::bytecode::Value{std::int64_t{19}}},
                                     {.opcode = Opcode::constant,
                                      .operand = 0,
                                      .value = ember::bytecode::Value{std::int64_t{23}}},
                                     {.opcode = Opcode::call, .operand = 1, .value = std::nullopt},
                                     {.opcode = Opcode::returnValue,
                                      .operand = 0,
                                      .value = std::nullopt}}},
                           {.id = 1,
                            .kind = FunctionKind::user,
                            .signature = {.parameterTypes = {Type::i64, Type::i64},
                                          .returnType = Type::i64},
                            .localCount = 2,
                            .localTypes = {Type::i64, Type::i64},
                            .code = {{.opcode = Opcode::load, .operand = 0, .value = std::nullopt},
                                     {.opcode = Opcode::load, .operand = 1, .value = std::nullopt},
                                     {.opcode = Opcode::addI64,
                                      .operand = 0,
                                      .value = std::nullopt},
                                     {.opcode = Opcode::returnValue,
                                      .operand = 0,
                                      .value = std::nullopt}}},
                       }};
    };

    auto jitProgram = verify(makeProgram());
    auto vmProgram = verify(makeProgram());
    if (!jitProgram || !vmProgram)
    {
        tests.expect(false, "binary-callee programs verify");
        return;
    }

    auto jitVm = ember::runtime::VirtualMachine::create(
        std::move(*jitProgram), {.hotThreshold = 1, .jitEnabled = true, .profilingEnabled = true});
    auto vm = ember::runtime::VirtualMachine::create(
        std::move(*vmProgram), {.hotThreshold = 1, .jitEnabled = false, .profilingEnabled = true});
    const auto jitReport = jitVm.execute(0);
    const auto vmReport = vm.execute(0);
    const auto *callee = jitVm.function(1);
    const auto *vmCallee = vm.function(1);

    tests.expect(!jitReport.result.error && !vmReport.result.error &&
                     jitReport.result.value == vmReport.result.value &&
                     jitReport.result.value == ember::bytecode::Value{std::int64_t{42}},
                 "native threshold call has the same result as VM execution");
    tests.expect(callee != nullptr && callee->profiling().isHot,
                 "binary callee becomes hot before native-target selection");
    tests.expect(vmCallee != nullptr && vmCallee->profiling().isHot &&
                     vmCallee->tier() == ember::runtime::ExecutionTier::virtualMachine,
                 "disabled JIT preserves profiling but prohibits publication and native dispatch");
#if EMBER_HAS_WIN64_JIT
    const auto *entry = jitVm.function(0);
    tests.expect(callee != nullptr && callee->tier() == ember::runtime::ExecutionTier::native,
                 "successful Win64 publication selects the native tier on the threshold call");
    tests.expect(entry != nullptr && entry->tier() == ember::runtime::ExecutionTier::native,
                 "a native caller crosses the trusted bridge to its native callee");
#else
    tests.expect(callee != nullptr &&
                     callee->tier() == ember::runtime::ExecutionTier::virtualMachine,
                 "unsupported native platforms retain VM execution");
#endif
}

EMBER_TEST("hot i64 loop with locals and comparisons matches the VM")
{
    const auto makeProgram = []
    {
        return Program{.functions = {
                           {.id = 0,
                            .kind = FunctionKind::user,
                            .signature = {.parameterTypes = {}, .returnType = Type::i64},
                            .localCount = 0,
                            .localTypes = {},
                            .code = {{.opcode = Opcode::constant,
                                      .operand = 0,
                                      .value = ember::bytecode::Value{std::int64_t{97}}},
                                     {.opcode = Opcode::call, .operand = 1, .value = std::nullopt},
                                     {.opcode = Opcode::returnValue,
                                      .operand = 0,
                                      .value = std::nullopt}}},
                           {.id = 1,
                            .kind = FunctionKind::user,
                            .signature = {.parameterTypes = {Type::i64}, .returnType = Type::i64},
                            .localCount = 2,
                            .localTypes = {Type::i64, Type::i64},
                            .code = {{.opcode = Opcode::constant,
                                      .operand = 0,
                                      .value = ember::bytecode::Value{std::int64_t{0}}},
                                     {.opcode = Opcode::store, .operand = 1, .value = std::nullopt},
                                     {.opcode = Opcode::load, .operand = 1, .value = std::nullopt},
                                     {.opcode = Opcode::load, .operand = 0, .value = std::nullopt},
                                     {.opcode = Opcode::lessI64, .operand = 0, .value = std::nullopt},
                                     {.opcode = Opcode::jumpIfFalse,
                                      .operand = 11,
                                      .value = std::nullopt},
                                     {.opcode = Opcode::load, .operand = 1, .value = std::nullopt},
                                     {.opcode = Opcode::constant,
                                      .operand = 0,
                                      .value = ember::bytecode::Value{std::int64_t{1}}},
                                     {.opcode = Opcode::addI64, .operand = 0, .value = std::nullopt},
                                     {.opcode = Opcode::store, .operand = 1, .value = std::nullopt},
                                     {.opcode = Opcode::jump, .operand = 2, .value = std::nullopt},
                                     {.opcode = Opcode::load, .operand = 1, .value = std::nullopt},
                                     {.opcode = Opcode::returnValue,
                                      .operand = 0,
                                      .value = std::nullopt}}},
                       }};
    };
    auto jitProgram = verify(makeProgram());
    auto vmProgram = verify(makeProgram());
    if (!jitProgram || !vmProgram)
    {
        tests.expect(false, "loop comparison program verifies");
        return;
    }

    auto jitVm = ember::runtime::VirtualMachine::create(
        std::move(*jitProgram), {.hotThreshold = 1, .jitEnabled = true, .profilingEnabled = true});
    auto vm = ember::runtime::VirtualMachine::create(
        std::move(*vmProgram), {.hotThreshold = 1, .jitEnabled = false, .profilingEnabled = true});
    const auto jitReport = jitVm.execute(0);
    const auto vmReport = vm.execute(0);
    tests.expect(!jitReport.result.error && !vmReport.result.error &&
                     jitReport.result.value == vmReport.result.value &&
                     jitReport.result.value == ember::bytecode::Value{std::int64_t{97}},
                 "native locals, comparisons and loop branches preserve VM semantics");
#if EMBER_HAS_WIN64_JIT
    const auto *loop = jitVm.function(1);
    tests.expect(loop != nullptr && loop->tier() == ember::runtime::ExecutionTier::native,
                 "the hot loop publishes only after complete native compilation");
#endif
}

EMBER_TEST("native recursive calls preserve the VM result") {
    const auto makeProgram = [] {
        return Program{
            .functions = {
                {.id = 0,
                 .kind = FunctionKind::user,
                 .signature = {.parameterTypes = {Type::i64}, .returnType = Type::i64},
                 .localCount = 1,
                 .localTypes = {Type::i64},
                 .code = {{.opcode = Opcode::load, .operand = 0, .value = std::nullopt},
                          {.opcode = Opcode::constant,
                           .operand = 0,
                           .value = ember::bytecode::Value{std::int64_t{1}}},
                          {.opcode = Opcode::lessEqualI64, .operand = 0, .value = std::nullopt},
                          {.opcode = Opcode::jumpIfFalse, .operand = 6, .value = std::nullopt},
                          {.opcode = Opcode::load, .operand = 0, .value = std::nullopt},
                          {.opcode = Opcode::returnValue, .operand = 0, .value = std::nullopt},
                          {.opcode = Opcode::load, .operand = 0, .value = std::nullopt},
                          {.opcode = Opcode::constant,
                           .operand = 0,
                           .value = ember::bytecode::Value{std::int64_t{1}}},
                          {.opcode = Opcode::subI64, .operand = 0, .value = std::nullopt},
                          {.opcode = Opcode::call, .operand = 0, .value = std::nullopt},
                          {.opcode = Opcode::load, .operand = 0, .value = std::nullopt},
                          {.opcode = Opcode::constant,
                           .operand = 0,
                           .value = ember::bytecode::Value{std::int64_t{2}}},
                          {.opcode = Opcode::subI64, .operand = 0, .value = std::nullopt},
                          {.opcode = Opcode::call, .operand = 0, .value = std::nullopt},
                          {.opcode = Opcode::addI64, .operand = 0, .value = std::nullopt},
                          {.opcode = Opcode::returnValue, .operand = 0, .value = std::nullopt}}},
            }};
    };
    auto jitProgram = verify(makeProgram());
    auto vmProgram = verify(makeProgram());
    if (!jitProgram || !vmProgram)
    {
        tests.expect(false, "recursive i64 program verifies");
        return;
    }
    auto jitVm = ember::runtime::VirtualMachine::create(
        std::move(*jitProgram), {.hotThreshold = 1, .jitEnabled = true, .profilingEnabled = true});
    auto vm = ember::runtime::VirtualMachine::create(
        std::move(*vmProgram), {.hotThreshold = 1, .jitEnabled = false, .profilingEnabled = true});
    const auto jitReport = jitVm.execute(0, {std::int64_t{6}});
    const auto vmReport = vm.execute(0, {std::int64_t{6}});
    tests.expect(!jitReport.result.error && !vmReport.result.error &&
                     jitReport.result.value == vmReport.result.value &&
                     jitReport.result.value == ember::bytecode::Value{std::int64_t{8}},
                 "native recursive bridge calls match the VM result");
#if EMBER_HAS_WIN64_JIT
    const auto *recursive = jitVm.function(0);
    tests.expect(recursive != nullptr && recursive->tier() == ember::runtime::ExecutionTier::native,
                 "recursive i64 function remains published in the native tier");
#endif
}

EMBER_TEST("shared dynamic frame budget bounds mixed native and VM recursion") {
    const auto makeProgram = [] {
        return Program{.functions = {
                           {.id = 0,
                            .kind = FunctionKind::user,
                            .signature = {.parameterTypes = {Type::i64}, .returnType = Type::i64},
                            .localCount = 1,
                            .localTypes = {Type::i64},
                            .code = {{.opcode = Opcode::load, .operand = 0, .value = std::nullopt},
                                     {.opcode = Opcode::constant,
                                      .operand = 0,
                                      .value = ember::bytecode::Value{std::int64_t{0}}},
                                     {.opcode = Opcode::greaterI64, .operand = 0, .value = std::nullopt},
                                     {.opcode = Opcode::jumpIfFalse,
                                      .operand = 9,
                                      .value = std::nullopt},
                                     {.opcode = Opcode::load, .operand = 0, .value = std::nullopt},
                                     {.opcode = Opcode::constant,
                                      .operand = 0,
                                      .value = ember::bytecode::Value{std::int64_t{1}}},
                                     {.opcode = Opcode::subI64, .operand = 0, .value = std::nullopt},
                                     {.opcode = Opcode::call, .operand = 1, .value = std::nullopt},
                                     {.opcode = Opcode::returnValue,
                                      .operand = 0,
                                      .value = std::nullopt},
                                     {.opcode = Opcode::constant,
                                      .operand = 0,
                                      .value = ember::bytecode::Value{std::int64_t{0}}},
                                     {.opcode = Opcode::returnValue,
                                      .operand = 0,
                                      .value = std::nullopt}}},
                           {.id = 1,
                            .kind = FunctionKind::user,
                            .signature = {.parameterTypes = {Type::i64}, .returnType = Type::i64},
                            .localCount = 2,
                            .localTypes = {Type::i64, Type::boolean},
                            .code = {{.opcode = Opcode::constant,
                                      .operand = 0,
                                      .value = ember::bytecode::Value{true}},
                                     {.opcode = Opcode::store, .operand = 1, .value = std::nullopt},
                                     {.opcode = Opcode::load, .operand = 0, .value = std::nullopt},
                                     {.opcode = Opcode::call, .operand = 0, .value = std::nullopt},
                                     {.opcode = Opcode::returnValue,
                                      .operand = 0,
                                      .value = std::nullopt}}},
                       }};
    };
    auto jitProgram = verify(makeProgram());
    auto vmProgram = verify(makeProgram());
    if (!jitProgram || !vmProgram)
    {
        tests.expect(false, "frame-budget recursion program verifies");
        return;
    }
    auto jitVm = ember::runtime::VirtualMachine::create(
        std::move(*jitProgram), {.hotThreshold = 1, .jitEnabled = true, .profilingEnabled = true});
    auto vm = ember::runtime::VirtualMachine::create(
        std::move(*vmProgram), {.hotThreshold = 1, .jitEnabled = false, .profilingEnabled = true});
    const auto jitAtLimit = jitVm.execute(0, {std::int64_t{2047}});
    const auto vmAtLimit = vm.execute(0, {std::int64_t{2047}});
    const auto jitOverLimit = jitVm.execute(0, {std::int64_t{2048}});
    const auto vmOverLimit = vm.execute(0, {std::int64_t{2048}});
    tests.expect(!jitAtLimit.result.error && !vmAtLimit.result.error &&
                     jitAtLimit.result.value == ember::bytecode::Value{std::int64_t{0}} &&
                     jitAtLimit.result.value == vmAtLimit.result.value,
                 "mixed-tier recursion succeeds at the shared frame-budget boundary");
    tests.expect(jitOverLimit.result.error && vmOverLimit.result.error &&
                     jitOverLimit.result.error->code == "R5006" &&
                     jitOverLimit.result.error->code == vmOverLimit.result.error->code,
                 "VM-to-native calls reserve the same shared frame-budget slot as VM calls");
#if EMBER_HAS_WIN64_JIT
    tests.expect(jitVm.function(0)->tier() == ember::runtime::ExecutionTier::native &&
                     jitVm.function(1)->tier() == ember::runtime::ExecutionTier::native,
                 "frame-limit test exercises fully native recursion with the shared frame budget");
#endif
}

EMBER_TEST("native caller dispatches a bool-local callee through the typed bridge")
{
    const auto makeProgram = []
    {
        return Program{.functions = {
                           {.id = 0,
                            .kind = FunctionKind::user,
                            .signature = {.parameterTypes = {}, .returnType = Type::i64},
                            .localCount = 0,
                            .localTypes = {},
                            .code = {{.opcode = Opcode::constant,
                                      .operand = 0,
                                      .value = ember::bytecode::Value{std::int64_t{41}}},
                                     {.opcode = Opcode::call, .operand = 1, .value = std::nullopt},
                                     {.opcode = Opcode::returnValue,
                                      .operand = 0,
                                      .value = std::nullopt}}},
                           {.id = 1,
                            .kind = FunctionKind::user,
                            .signature = {.parameterTypes = {Type::i64}, .returnType = Type::i64},
                            .localCount = 1,
                            .localTypes = {Type::i64},
                            .code = {{.opcode = Opcode::load, .operand = 0, .value = std::nullopt},
                                     {.opcode = Opcode::constant,
                                      .operand = 0,
                                      .value = ember::bytecode::Value{true}},
                                     {.opcode = Opcode::jumpIfFalse,
                                      .operand = 5,
                                      .value = std::nullopt},
                                     {.opcode = Opcode::constant,
                                      .operand = 0,
                                      .value = ember::bytecode::Value{std::int64_t{1}}},
                                     {.opcode = Opcode::addI64, .operand = 0, .value = std::nullopt},
                                     {.opcode = Opcode::returnValue,
                                      .operand = 0,
                                      .value = std::nullopt}}},
                       }};
    };
    auto jitProgram = verify(makeProgram());
    auto vmProgram = verify(makeProgram());
    if (!jitProgram || !vmProgram)
    {
        tests.expect(false, "mixed-tier fallback program verifies");
        return;
    }
    auto jitVm = ember::runtime::VirtualMachine::create(
        std::move(*jitProgram), {.hotThreshold = 1, .jitEnabled = true, .profilingEnabled = true});
    auto vm = ember::runtime::VirtualMachine::create(
        std::move(*vmProgram), {.hotThreshold = 1, .jitEnabled = false, .profilingEnabled = true});
    const auto jitReport = jitVm.execute(0);
    const auto vmReport = vm.execute(0);
    tests.expect(!jitReport.result.error && !vmReport.result.error &&
                     jitReport.result.value == vmReport.result.value &&
                     jitReport.result.value == ember::bytecode::Value{std::int64_t{42}},
                 "native caller receives the same result from the VM-only callee");
#if EMBER_HAS_WIN64_JIT
    const auto *caller = jitVm.function(0);
    const auto *callee = jitVm.function(1);
    tests.expect(caller != nullptr && caller->tier() == ember::runtime::ExecutionTier::native &&
                     callee != nullptr &&
                     callee->tier() == ember::runtime::ExecutionTier::virtualMachine,
                 "bridge preserves mixed native/VM tier selection");
#endif
}

EMBER_TEST("zero-argument native bridge calls are accepted") {
    const auto makeProgram = [] {
        return Program{.functions = {
                           {.id = 0,
                            .kind = FunctionKind::user,
                            .signature = {.parameterTypes = {}, .returnType = Type::i64},
                            .localCount = 0,
                            .localTypes = {},
                            .code = {{.opcode = Opcode::call, .operand = 1, .value = std::nullopt},
                                     {.opcode = Opcode::returnValue,
                                      .operand = 0,
                                      .value = std::nullopt}}},
                           {.id = 1,
                            .kind = FunctionKind::user,
                            .signature = {.parameterTypes = {}, .returnType = Type::i64},
                            .localCount = 0,
                            .localTypes = {},
                            .code = {{.opcode = Opcode::constant,
                                      .operand = 0,
                                      .value = ember::bytecode::Value{std::int64_t{42}}},
                                     {.opcode = Opcode::returnValue,
                                      .operand = 0,
                                      .value = std::nullopt}}},
                       }};
    };
    auto jitProgram = verify(makeProgram());
    auto vmProgram = verify(makeProgram());
    if (!jitProgram || !vmProgram)
    {
        tests.expect(false, "zero-argument call program verifies");
        return;
    }
    auto jitVm = ember::runtime::VirtualMachine::create(
        std::move(*jitProgram), {.hotThreshold = 1, .jitEnabled = true, .profilingEnabled = true});
    auto vm = ember::runtime::VirtualMachine::create(
        std::move(*vmProgram), {.hotThreshold = 1, .jitEnabled = false, .profilingEnabled = true});
    const auto jitReport = jitVm.execute(0);
    const auto vmReport = vm.execute(0);
    tests.expect(!jitReport.result.error && !vmReport.result.error &&
                     jitReport.result.value == vmReport.result.value &&
                     jitReport.result.value == ember::bytecode::Value{std::int64_t{42}},
                 "zero-argument native call matches VM execution");
#if EMBER_HAS_WIN64_JIT
    tests.expect(jitVm.function(0)->tier() == ember::runtime::ExecutionTier::native &&
                     jitVm.function(1)->tier() == ember::runtime::ExecutionTier::native,
                 "zero-argument caller and callee both publish natively");
#endif
}

EMBER_TEST("zero-argument native callers fall back to VM callees")
{
    const auto makeProgram = []
    {
        return Program{.functions = {
                           {.id = 0,
                            .kind = FunctionKind::user,
                            .signature = {.parameterTypes = {}, .returnType = Type::i64},
                            .localCount = 0,
                            .localTypes = {},
                            .code = {{.opcode = Opcode::call, .operand = 1, .value = std::nullopt},
                                     {.opcode = Opcode::returnValue,
                                      .operand = 0,
                                      .value = std::nullopt}}},
                           {.id = 1,
                            .kind = FunctionKind::user,
                            .signature = {.parameterTypes = {}, .returnType = Type::i64},
                            .localCount = 1,
                            .localTypes = {Type::boolean},
                            .code = {{.opcode = Opcode::constant,
                                      .operand = 0,
                                      .value = ember::bytecode::Value{true}},
                                     {.opcode = Opcode::store, .operand = 0, .value = std::nullopt},
                                     {.opcode = Opcode::load, .operand = 0, .value = std::nullopt},
                                     {.opcode = Opcode::jumpIfFalse,
                                      .operand = 6,
                                      .value = std::nullopt},
                                     {.opcode = Opcode::constant,
                                      .operand = 0,
                                      .value = ember::bytecode::Value{std::int64_t{42}}},
                                     {.opcode = Opcode::returnValue,
                                      .operand = 0,
                                      .value = std::nullopt},
                                     {.opcode = Opcode::constant,
                                      .operand = 0,
                                      .value = ember::bytecode::Value{std::int64_t{0}}},
                                     {.opcode = Opcode::returnValue,
                                      .operand = 0,
                                      .value = std::nullopt}}},
                       }};
    };
    auto jitProgram = verify(makeProgram());
    auto vmProgram = verify(makeProgram());
    if (!jitProgram || !vmProgram)
    {
        tests.expect(false, "zero-argument mixed-tier program verifies");
        return;
    }
    auto jitVm = ember::runtime::VirtualMachine::create(
        std::move(*jitProgram), {.hotThreshold = 1, .jitEnabled = true, .profilingEnabled = true});
    auto vm = ember::runtime::VirtualMachine::create(
        std::move(*vmProgram), {.hotThreshold = 1, .jitEnabled = false, .profilingEnabled = true});
    const auto jitReport = jitVm.execute(0);
    const auto vmReport = vm.execute(0);
    tests.expect(!jitReport.result.error && !vmReport.result.error &&
                     jitReport.result.value == vmReport.result.value &&
                     jitReport.result.value == ember::bytecode::Value{std::int64_t{42}},
                 "zero-argument native typed call preserves the VM result");
#if EMBER_HAS_WIN64_JIT
    tests.expect(jitVm.function(0)->tier() == ember::runtime::ExecutionTier::native &&
                     jitVm.function(1)->tier() == ember::runtime::ExecutionTier::native,
                 "zero-argument bridge publishes the bool-local callee natively");
#endif
}

EMBER_TEST("guarded division publishes natively and preserves the VM result")
{
    auto verified = verify({.functions = {
                               {.id = 0,
                                .kind = FunctionKind::user,
                                .signature = {.parameterTypes = {Type::i64, Type::i64},
                                              .returnType = Type::i64},
                                .localCount = 2,
                                .localTypes = {Type::i64, Type::i64},
                                .code = {{.opcode = Opcode::load,
                                          .operand = 0,
                                          .value = std::nullopt},
                                         {.opcode = Opcode::load,
                                          .operand = 1,
                                          .value = std::nullopt},
                                         {.opcode = Opcode::divI64,
                                          .operand = 0,
                                          .value = std::nullopt},
                                         {.opcode = Opcode::returnValue,
                                          .operand = 0,
                                          .value = std::nullopt}}},
                           }});
    if (!verified)
    {
        tests.expect(false, "division program verifies");
        return;
    }

    auto vm = ember::runtime::VirtualMachine::create(
        std::move(*verified), {.hotThreshold = 1, .jitEnabled = true, .profilingEnabled = true});
    const auto report = vm.execute(0, {std::int64_t{12}, std::int64_t{3}});
    const auto *function = vm.function(0);
    tests.expect(!report.result.error && report.result.value == ember::bytecode::Value{std::int64_t{4}},
                 "guarded native division retains the VM result");
    tests.expect(function != nullptr && function->profiling().isHot
#if EMBER_HAS_WIN64_JIT
                     && function->tier() == ember::runtime::ExecutionTier::native
#else
                     && function->tier() == ember::runtime::ExecutionTier::virtualMachine
#endif
                 ,
                 "division publishes only with an explicit native runtime-error guard");
}

EMBER_TEST("native division guards preserve VM runtime errors")
{
    const auto makeProgram = [](Opcode operation)
    {
        return Program{.functions = {
                           {.id = 0,
                            .kind = FunctionKind::user,
                            .signature = {.parameterTypes = {Type::i64, Type::i64},
                                          .returnType = Type::i64},
                            .localCount = 2,
                            .localTypes = {Type::i64, Type::i64},
                            .code = {{.opcode = Opcode::load, .operand = 0, .value = std::nullopt},
                                     {.opcode = Opcode::load, .operand = 1, .value = std::nullopt},
                                     {.opcode = operation, .operand = 0, .value = std::nullopt},
                                     {.opcode = Opcode::returnValue,
                                      .operand = 0,
                                      .value = std::nullopt}}},
                       }};
    };
    const auto checkError = [&makeProgram](Opcode operation, std::int64_t left, std::int64_t right)
    {
        auto jitProgram = verify(makeProgram(operation));
        auto vmProgram = verify(makeProgram(operation));
        if (!jitProgram || !vmProgram)
            return false;
        auto jitVm = ember::runtime::VirtualMachine::create(
            std::move(*jitProgram), {.hotThreshold = 1, .jitEnabled = true, .profilingEnabled = true});
        auto vm = ember::runtime::VirtualMachine::create(
            std::move(*vmProgram), {.hotThreshold = 1, .jitEnabled = false, .profilingEnabled = true});
        const auto jitReport = jitVm.execute(0, {left, right});
        const auto vmReport = vm.execute(0, {left, right});
        const auto *function = jitVm.function(0);
        return jitReport.result.error && vmReport.result.error &&
               jitReport.result.error->code == vmReport.result.error->code && function != nullptr
#if EMBER_HAS_WIN64_JIT
               && function->tier() == ember::runtime::ExecutionTier::native
#else
               && function->tier() == ember::runtime::ExecutionTier::virtualMachine
#endif
            ;
    };
    tests.expect(checkError(Opcode::divI64, 1, 0) &&
                     checkError(Opcode::divI64, std::numeric_limits<std::int64_t>::min(), -1) &&
                     checkError(Opcode::remI64, std::numeric_limits<std::int64_t>::min(), -1),
                 "division and remainder guards preserve zero and INT64_MIN/-1 VM errors");
}

EMBER_TEST("native i64 wrap arithmetic and signed comparisons match the VM")
{
    const auto runBinary = [](Opcode operation, std::int64_t left, std::int64_t right,
                              std::int64_t expected) -> bool
    {
        const bool comparison = operation == Opcode::equalI64 || operation == Opcode::notEqualI64 ||
                                operation == Opcode::lessI64 || operation == Opcode::lessEqualI64 ||
                                operation == Opcode::greaterI64 || operation == Opcode::greaterEqualI64;
        const auto makeProgram = [operation, comparison]
        {
            std::vector<Instruction> code{{.opcode = Opcode::load, .operand = 0, .value = std::nullopt},
                                          {.opcode = Opcode::load, .operand = 1, .value = std::nullopt},
                                          {.opcode = operation, .operand = 0, .value = std::nullopt}};
            if (comparison)
            {
                code.push_back({.opcode = Opcode::jumpIfFalse, .operand = 6, .value = std::nullopt});
                code.push_back({.opcode = Opcode::constant,
                                .operand = 0,
                                .value = ember::bytecode::Value{std::int64_t{1}}});
                code.push_back({.opcode = Opcode::returnValue, .operand = 0, .value = std::nullopt});
                code.push_back({.opcode = Opcode::constant,
                                .operand = 0,
                                .value = ember::bytecode::Value{std::int64_t{0}}});
            }
            code.push_back({.opcode = Opcode::returnValue, .operand = 0, .value = std::nullopt});
            return Program{.functions = {{.id = 0,
                                          .kind = FunctionKind::user,
                                          .signature = {.parameterTypes = {Type::i64, Type::i64},
                                                        .returnType = Type::i64},
                                          .localCount = 2,
                                          .localTypes = {Type::i64, Type::i64},
                                          .code = std::move(code)}}};
        };
        auto jitProgram = verify(makeProgram());
        auto vmProgram = verify(makeProgram());
        if (!jitProgram || !vmProgram)
            return false;
        auto jitVm = ember::runtime::VirtualMachine::create(
            std::move(*jitProgram), {.hotThreshold = 1, .jitEnabled = true, .profilingEnabled = true});
        auto vm = ember::runtime::VirtualMachine::create(
            std::move(*vmProgram), {.hotThreshold = 1, .jitEnabled = false, .profilingEnabled = true});
        const auto jitReport = jitVm.execute(0, {left, right});
        const auto vmReport = vm.execute(0, {left, right});
        const auto *function = jitVm.function(0);
        const bool tierIsExpected = function != nullptr
#if EMBER_HAS_WIN64_JIT
                                    && function->tier() == ember::runtime::ExecutionTier::native
#endif
            ;
        return !jitReport.result.error && !vmReport.result.error &&
               jitReport.result.value == ember::bytecode::Value{expected} &&
               jitReport.result.value == vmReport.result.value && tierIsExpected;
    };

    const auto runNegate = [](std::int64_t input, std::int64_t expected) -> bool
    {
        const auto makeProgram = []
        {
            return Program{.functions = {{.id = 0,
                                          .kind = FunctionKind::user,
                                          .signature = {.parameterTypes = {Type::i64}, .returnType = Type::i64},
                                          .localCount = 1,
                                          .localTypes = {Type::i64},
                                          .code = {{.opcode = Opcode::load,
                                                    .operand = 0,
                                                    .value = std::nullopt},
                                                   {.opcode = Opcode::negateI64,
                                                    .operand = 0,
                                                    .value = std::nullopt},
                                                   {.opcode = Opcode::returnValue,
                                                    .operand = 0,
                                                    .value = std::nullopt}}}}};
        };
        auto jitProgram = verify(makeProgram());
        auto vmProgram = verify(makeProgram());
        if (!jitProgram || !vmProgram)
            return false;
        auto jitVm = ember::runtime::VirtualMachine::create(
            std::move(*jitProgram), {.hotThreshold = 1, .jitEnabled = true, .profilingEnabled = true});
        auto vm = ember::runtime::VirtualMachine::create(
            std::move(*vmProgram), {.hotThreshold = 1, .jitEnabled = false, .profilingEnabled = true});
        const auto jitReport = jitVm.execute(0, {input});
        const auto vmReport = vm.execute(0, {input});
        return !jitReport.result.error && !vmReport.result.error &&
               jitReport.result.value == ember::bytecode::Value{expected} &&
               jitReport.result.value == vmReport.result.value;
    };

    const auto minimum = std::numeric_limits<std::int64_t>::min();
    const auto maximum = std::numeric_limits<std::int64_t>::max();
    tests.expect(runBinary(Opcode::addI64, maximum, 1, minimum) &&
                     runBinary(Opcode::subI64, minimum, 1, maximum) &&
                     runBinary(Opcode::mulI64, minimum, -1, minimum) &&
                     runNegate(minimum, minimum) &&
                     runBinary(Opcode::equalI64, -1, -1, 1) &&
                     runBinary(Opcode::notEqualI64, -1, 0, 1) &&
                     runBinary(Opcode::lessI64, minimum, -1, 1) &&
                     runBinary(Opcode::lessEqualI64, -1, -1, 1) &&
                     runBinary(Opcode::greaterI64, 0, minimum, 1) &&
                     runBinary(Opcode::greaterEqualI64, maximum, maximum, 1),
                 "wrapping i64 arithmetic and all signed comparison lowerings match the VM");
}

EMBER_TEST("optimized native constant CFG matches the unoptimized VM")
{
    const auto makeProgram = []
    {
        return Program{.functions = {
                           {.id = 0,
                            .kind = FunctionKind::user,
                            .signature = {.parameterTypes = {}, .returnType = Type::i64},
                            .localCount = 1,
                            .localTypes = {Type::i64},
                            .code = {{.opcode = Opcode::constant,
                                      .operand = 0,
                                      .value = ember::bytecode::Value{std::int64_t{2}}},
                                     {.opcode = Opcode::constant,
                                      .operand = 0,
                                      .value = ember::bytecode::Value{std::int64_t{3}}},
                                     {.opcode = Opcode::addI64, .operand = 0, .value = std::nullopt},
                                     {.opcode = Opcode::store, .operand = 0, .value = std::nullopt},
                                     {.opcode = Opcode::load, .operand = 0, .value = std::nullopt},
                                     {.opcode = Opcode::constant,
                                      .operand = 0,
                                      .value = ember::bytecode::Value{std::int64_t{20}}},
                                     {.opcode = Opcode::equalI64, .operand = 0, .value = std::nullopt},
                                     {.opcode = Opcode::jumpIfFalse, .operand = 10, .value = std::nullopt},
                                     {.opcode = Opcode::constant,
                                      .operand = 0,
                                      .value = ember::bytecode::Value{std::int64_t{99}}},
                                     {.opcode = Opcode::returnValue,
                                      .operand = 0,
                                      .value = std::nullopt},
                                     {.opcode = Opcode::load, .operand = 0, .value = std::nullopt},
                                     {.opcode = Opcode::returnValue,
                                      .operand = 0,
                                      .value = std::nullopt}}},
                       }};
    };

    auto jitProgram = verify(makeProgram());
    auto unoptimizedProgram = verify(makeProgram());
    auto vmProgram = verify(makeProgram());
    if (!jitProgram || !unoptimizedProgram || !vmProgram)
    {
        tests.expect(false, "optimized-CFG bytecode verifies");
        return;
    }
    auto jitVm = ember::runtime::VirtualMachine::create(
        std::move(*jitProgram), {.hotThreshold = 1, .jitEnabled = true, .profilingEnabled = true});
    auto unoptimizedVm = ember::runtime::VirtualMachine::create(
        std::move(*unoptimizedProgram),
        {.hotThreshold = 1,
         .jitEnabled = true,
         .profilingEnabled = true,
         .disableOptimizationForTesting = true});
    auto vm = ember::runtime::VirtualMachine::create(
        std::move(*vmProgram), {.hotThreshold = 1, .jitEnabled = false, .profilingEnabled = true});
    const auto jitReport = jitVm.execute(0);
    const auto unoptimizedReport = unoptimizedVm.execute(0);
    const auto vmReport = vm.execute(0);
    const auto *function = jitVm.function(0);
    const auto *unoptimizedFunction = unoptimizedVm.function(0);
    tests.expect(!jitReport.result.error && !unoptimizedReport.result.error && !vmReport.result.error &&
                     jitReport.result.value == ember::bytecode::Value{std::int64_t{5}} &&
                     jitReport.result.value == unoptimizedReport.result.value &&
                     jitReport.result.value == vmReport.result.value && function != nullptr &&
                     unoptimizedFunction != nullptr
#if EMBER_HAS_WIN64_JIT
                     && function->tier() == ember::runtime::ExecutionTier::native &&
                     unoptimizedFunction->tier() == ember::runtime::ExecutionTier::native
#else
                     && function->tier() == ember::runtime::ExecutionTier::virtualMachine &&
                     unoptimizedFunction->tier() == ember::runtime::ExecutionTier::virtualMachine
#endif
                 ,
                 "optimized and unoptimized native CFGs preserve the VM result");
}

EMBER_TEST("optimized and unoptimized native code preserve VM runtime errors")
{
    const auto checkI64Error = [](Opcode operation, std::int64_t left, std::int64_t right)
    {
        const auto makeProgram = [operation]
        {
            return Program{.functions = {
                               {.id = 0,
                                .kind = FunctionKind::user,
                                .signature = {.parameterTypes = {Type::i64, Type::i64},
                                              .returnType = Type::i64},
                                .localCount = 2,
                                .localTypes = {Type::i64, Type::i64},
                                .code = {{.opcode = Opcode::load, .operand = 0, .value = std::nullopt},
                                         {.opcode = Opcode::load, .operand = 1, .value = std::nullopt},
                                         {.opcode = operation, .operand = 0, .value = std::nullopt},
                                         {.opcode = Opcode::returnValue,
                                          .operand = 0,
                                          .value = std::nullopt}}},
                           }};
        };
        auto optimizedProgram = verify(makeProgram());
        auto unoptimizedProgram = verify(makeProgram());
        auto vmProgram = verify(makeProgram());
        if (!optimizedProgram || !unoptimizedProgram || !vmProgram)
            return false;
        auto optimizedVm = ember::runtime::VirtualMachine::create(
            std::move(*optimizedProgram), {.hotThreshold = 1, .jitEnabled = true, .profilingEnabled = true});
        auto unoptimizedVm = ember::runtime::VirtualMachine::create(
            std::move(*unoptimizedProgram),
            {.hotThreshold = 1,
             .jitEnabled = true,
             .profilingEnabled = true,
             .disableOptimizationForTesting = true});
        auto vm = ember::runtime::VirtualMachine::create(
            std::move(*vmProgram), {.hotThreshold = 1, .jitEnabled = false, .profilingEnabled = true});
        const auto optimized = optimizedVm.execute(0, {left, right});
        const auto unoptimized = unoptimizedVm.execute(0, {left, right});
        const auto interpreted = vm.execute(0, {left, right});
        return optimized.result.error && unoptimized.result.error && interpreted.result.error &&
               optimized.result.error->code == interpreted.result.error->code &&
               unoptimized.result.error->code == interpreted.result.error->code;
    };

    const auto recursiveFrameLimit = []
    {
        const auto makeProgram = []
        {
            return Program{.functions = {
                               {.id = 0,
                                .kind = FunctionKind::user,
                                .signature = {.parameterTypes = {}, .returnType = Type::i64},
                                .localCount = 0,
                                .localTypes = {},
                                .code = {{.opcode = Opcode::call, .operand = 0, .value = std::nullopt},
                                         {.opcode = Opcode::returnValue,
                                          .operand = 0,
                                          .value = std::nullopt}}},
                           }};
        };
        auto optimizedProgram = verify(makeProgram());
        auto unoptimizedProgram = verify(makeProgram());
        auto vmProgram = verify(makeProgram());
        if (!optimizedProgram || !unoptimizedProgram || !vmProgram)
            return false;
        auto optimizedVm = ember::runtime::VirtualMachine::create(
            std::move(*optimizedProgram), {.hotThreshold = 1, .jitEnabled = true, .profilingEnabled = true});
        auto unoptimizedVm = ember::runtime::VirtualMachine::create(
            std::move(*unoptimizedProgram),
            {.hotThreshold = 1,
             .jitEnabled = true,
             .profilingEnabled = true,
             .disableOptimizationForTesting = true});
        auto vm = ember::runtime::VirtualMachine::create(
            std::move(*vmProgram), {.hotThreshold = 1, .jitEnabled = false, .profilingEnabled = true});
        const auto optimized = optimizedVm.execute(0);
        const auto unoptimized = unoptimizedVm.execute(0);
        const auto interpreted = vm.execute(0);
        return optimized.result.error && unoptimized.result.error && interpreted.result.error &&
               optimized.result.error->code == "R5006" &&
               unoptimized.result.error->code == interpreted.result.error->code &&
               optimized.result.error->code == interpreted.result.error->code;
    };

    tests.expect(checkI64Error(Opcode::divI64, 1, 0) &&
                     checkI64Error(Opcode::divI64, std::numeric_limits<std::int64_t>::min(), -1) &&
                     checkI64Error(Opcode::remI64, 1, 0) &&
                     checkI64Error(Opcode::remI64, std::numeric_limits<std::int64_t>::min(), -1) &&
                     recursiveFrameLimit(),
                 "VM and both execution modes preserve runtime-error codes");
}

#if EMBER_HAS_WIN64_JIT
EMBER_TEST("optimized and unoptimized native code preserve bridge failures")
{
    const auto makeProgram = []
    {
        return Program{.functions = {
                           {.id = 0,
                            .kind = FunctionKind::user,
                            .signature = {.parameterTypes = {}, .returnType = Type::i64},
                            .localCount = 0,
                            .localTypes = {},
                            .code = {{.opcode = Opcode::call, .operand = 1, .value = std::nullopt},
                                     {.opcode = Opcode::returnValue,
                                      .operand = 0,
                                      .value = std::nullopt}}},
                           {.id = 1,
                            .kind = FunctionKind::user,
                            .signature = {.parameterTypes = {}, .returnType = Type::i64},
                            .localCount = 0,
                            .localTypes = {},
                            .code = {{.opcode = Opcode::constant,
                                      .operand = 0,
                                      .value = ember::bytecode::Value{std::int64_t{7}}},
                                     {.opcode = Opcode::returnValue,
                                      .operand = 0,
                                      .value = std::nullopt}}},
                       }};
    };
    auto optimizedProgram = verify(makeProgram());
    auto unoptimizedProgram = verify(makeProgram());
    if (!optimizedProgram || !unoptimizedProgram)
    {
        tests.expect(false, "native bridge-failure fixture verifies");
        return;
    }
    const ember::runtime::RuntimeOptions optimizedOptions{
        .hotThreshold = 1,
        .jitEnabled = true,
        .profilingEnabled = true,
        .forceNativeCallFailureForTesting = true};
    const ember::runtime::RuntimeOptions unoptimizedOptions{
        .hotThreshold = 1,
        .jitEnabled = true,
        .profilingEnabled = true,
        .disableOptimizationForTesting = true,
        .forceNativeCallFailureForTesting = true};
    auto optimizedVm = ember::runtime::VirtualMachine::create(std::move(*optimizedProgram), optimizedOptions);
    auto unoptimizedVm =
        ember::runtime::VirtualMachine::create(std::move(*unoptimizedProgram), unoptimizedOptions);
    const auto optimized = optimizedVm.execute(0);
    const auto unoptimized = unoptimizedVm.execute(0);
    tests.expect(optimized.result.error && unoptimized.result.error &&
                     optimized.result.error->code == "R5003" &&
                     optimized.result.error->code == unoptimized.result.error->code,
                 "optimized and unoptimized native calls preserve bridge-failure code R5003");
}
#endif

EMBER_TEST("injected native compilation failure retains VM dispatch and result")
{
    auto verified = verify({.functions = {
                               {.id = 0,
                                .kind = FunctionKind::user,
                                .signature = {.parameterTypes = {}, .returnType = Type::i64},
                                .localCount = 0,
                                .localTypes = {},
                                .code = {{.opcode = Opcode::constant,
                                          .operand = 0,
                                          .value = ember::bytecode::Value{std::int64_t{42}}},
                                         {.opcode = Opcode::returnValue,
                                          .operand = 0,
                                          .value = std::nullopt}}},
                           }});
    if (!verified)
    {
        tests.expect(false, "fault-injection program verifies");
        return;
    }
    auto vm = ember::runtime::VirtualMachine::create(
        std::move(*verified),
        {.hotThreshold = 1,
         .jitEnabled = true,
         .profilingEnabled = true,
         .forceNativeCompilationFailureForTesting = true});
    const auto report = vm.execute(0);
    const auto *function = vm.function(0);
    tests.expect(!report.result.error && report.result.value == ember::bytecode::Value{std::int64_t{42}} &&
                     function != nullptr && function->profiling().isHot &&
                     function->tier() == ember::runtime::ExecutionTier::virtualMachine &&
                     !ember::runtime::test::RuntimeFunctionAccess::hasNativeEntry(*function) &&
                     ember::runtime::test::RuntimeFunctionAccess::nativeCompilationStage(*function) ==
                         ember::runtime::NativeCompilationStage::compilerFailure,
                 "injected failure cannot publish a partial entry point or change the VM result");
}

EMBER_TEST("native lifecycle failpoints retain VM dispatch and release executable memory")
{
#if EMBER_HAS_WIN64_JIT
    const auto makeProgram = [] {
        return Program{.functions = {
                           {.id = 0,
                            .kind = FunctionKind::user,
                            .signature = {.parameterTypes = {}, .returnType = Type::i64},
                            .localCount = 0,
                            .localTypes = {},
                            .code = {{.opcode = Opcode::constant,
                                      .operand = 0,
                                      .value = ember::bytecode::Value{std::int64_t{42}}},
                                     {.opcode = Opcode::returnValue,
                                      .operand = 0,
                                      .value = std::nullopt}}},
                       }};
    };
    const auto expected = ember::bytecode::Value{std::int64_t{42}};
    tests.expect(ember::runtime::test::NativeCodeHandleAccess::liveExecutableAllocationCount() == 0,
                 "lifecycle test starts without a live executable allocation");

    {
        auto verified = verify(makeProgram());
        if (!verified)
        {
            tests.expect(false, "native lifecycle control program verifies");
            return;
        }
        auto vm = ember::runtime::VirtualMachine::create(
            std::move(*verified), {.hotThreshold = 1, .jitEnabled = true, .profilingEnabled = true});
        const auto report = vm.execute(0);
        const auto *function = vm.function(0);
        tests.expect(!report.result.error && report.result.value == expected && function != nullptr &&
                         function->tier() == ember::runtime::ExecutionTier::native &&
                         ember::runtime::test::RuntimeFunctionAccess::hasNativeEntry(*function) &&
                         ember::runtime::test::RuntimeFunctionAccess::nativeCompilationStage(*function) ==
                             ember::runtime::NativeCompilationStage::published,
                     "control program reaches a published native entry");
        tests.expect(ember::runtime::test::NativeCodeHandleAccess::liveExecutableAllocationCount() == 1,
                     "published control entry owns one executable allocation");
    }
    tests.expect(ember::runtime::test::NativeCodeHandleAccess::liveExecutableAllocationCount() == 0,
                 "destroyed VM releases the published executable allocation");

    struct FailpointCase
    {
        ember::runtime::NativeCompilationFailpoint failpoint;
        ember::runtime::NativeCompilationStage expectedStage;
    };
    constexpr std::array failpoints{
        FailpointCase{ember::runtime::NativeCompilationFailpoint::afterLowering,
                      ember::runtime::NativeCompilationStage::lowered},
        FailpointCase{ember::runtime::NativeCompilationFailpoint::afterEmission,
                      ember::runtime::NativeCompilationStage::emitted},
        FailpointCase{ember::runtime::NativeCompilationFailpoint::afterExecutableAllocation,
                      ember::runtime::NativeCompilationStage::executableAllocated},
        FailpointCase{ember::runtime::NativeCompilationFailpoint::afterExecutableWrite,
                      ember::runtime::NativeCompilationStage::executableWritten},
        FailpointCase{ember::runtime::NativeCompilationFailpoint::afterExecutableProtection,
                      ember::runtime::NativeCompilationStage::executableProtected},
    };
    for (const auto scenario : failpoints)
    {
        auto verified = verify(makeProgram());
        if (!verified)
        {
            tests.expect(false, "native lifecycle failpoint program verifies");
            return;
        }
        auto vm = ember::runtime::VirtualMachine::create(
            std::move(*verified),
            {.hotThreshold = 1,
             .jitEnabled = true,
             .profilingEnabled = true,
             .nativeCompilationFailpointForTesting = scenario.failpoint});
        const auto first = vm.execute(0);
        const auto second = vm.execute(0);
        const auto *function = vm.function(0);
        tests.expect(!first.result.error && first.result.value == expected && !second.result.error &&
                         second.result.value == expected && function != nullptr &&
                         function->tier() == ember::runtime::ExecutionTier::virtualMachine &&
                         !ember::runtime::test::RuntimeFunctionAccess::hasNativeEntry(*function) &&
                         ember::runtime::test::RuntimeFunctionAccess::nativeCompilationStage(*function) ==
                             scenario.expectedStage,
                     "lifecycle failpoint keeps the entry on the VM path across repeated execution");
        tests.expect(ember::runtime::test::NativeCodeHandleAccess::liveExecutableAllocationCount() == 0,
                     "lifecycle failpoint leaves no temporary executable allocation");
    }
#else
    tests.expect(true, "native lifecycle failpoints require the Win64 JIT target");
#endif
}
} // namespace
