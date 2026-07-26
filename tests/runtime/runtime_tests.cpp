#include "ember/bytecode/bytecode.hpp"
#include "ember/jit/platform.hpp"
#include "ember/runtime/vm.hpp"

#include "test_harness.hpp"

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
    const auto *entry = jitVm.function(0);
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
    tests.expect(callee != nullptr && callee->tier() == ember::runtime::ExecutionTier::native,
                 "successful Win64 publication selects the native tier on the threshold call");
    tests.expect(entry != nullptr && entry->tier() == ember::runtime::ExecutionTier::native,
                 "a native caller crosses the trusted bridge to its native callee");
#else
    tests.expect(callee != nullptr && callee->tier() == ember::runtime::ExecutionTier::virtualMachine,
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
    const auto *loop = jitVm.function(1);
    tests.expect(!jitReport.result.error && !vmReport.result.error &&
                     jitReport.result.value == vmReport.result.value &&
                     jitReport.result.value == ember::bytecode::Value{std::int64_t{97}},
                 "native locals, comparisons and loop branches preserve VM semantics");
#if EMBER_HAS_WIN64_JIT
    tests.expect(loop != nullptr && loop->tier() == ember::runtime::ExecutionTier::native,
                 "the hot loop publishes only after complete native compilation");
#endif
}

EMBER_TEST("native recursive calls preserve the VM result")
{
    const auto makeProgram = []
    {
        return Program{.functions = {
                           {.id = 0,
                            .kind = FunctionKind::user,
                            .signature = {.parameterTypes = {Type::i64}, .returnType = Type::i64},
                            .localCount = 1,
                            .localTypes = {Type::i64},
                            .code = {{.opcode = Opcode::load, .operand = 0, .value = std::nullopt},
                                     {.opcode = Opcode::constant,
                                      .operand = 0,
                                      .value = ember::bytecode::Value{std::int64_t{1}}},
                                     {.opcode = Opcode::lessEqualI64,
                                      .operand = 0,
                                      .value = std::nullopt},
                                     {.opcode = Opcode::jumpIfFalse,
                                      .operand = 6,
                                      .value = std::nullopt},
                                     {.opcode = Opcode::load, .operand = 0, .value = std::nullopt},
                                     {.opcode = Opcode::returnValue,
                                      .operand = 0,
                                      .value = std::nullopt},
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
                                     {.opcode = Opcode::returnValue,
                                      .operand = 0,
                                      .value = std::nullopt}}},
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
    const auto *recursive = jitVm.function(0);
    tests.expect(!jitReport.result.error && !vmReport.result.error &&
                     jitReport.result.value == vmReport.result.value &&
                     jitReport.result.value == ember::bytecode::Value{std::int64_t{8}},
                 "native recursive bridge calls match the VM result");
#if EMBER_HAS_WIN64_JIT
    tests.expect(recursive != nullptr && recursive->tier() == ember::runtime::ExecutionTier::native,
                 "recursive i64 function remains published in the native tier");
#endif
}

EMBER_TEST("shared dynamic frame budget bounds mixed native and VM recursion")
{
    const auto makeProgram = []
    {
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
                     jitVm.function(1)->tier() == ember::runtime::ExecutionTier::virtualMachine,
                 "frame-limit test exercises native to VM to native recursion");
#endif
}

EMBER_TEST("native caller dispatches an unsupported i64 callee through the VM fallback")
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
    const auto *caller = jitVm.function(0);
    const auto *callee = jitVm.function(1);
    tests.expect(!jitReport.result.error && !vmReport.result.error &&
                     jitReport.result.value == vmReport.result.value &&
                     jitReport.result.value == ember::bytecode::Value{std::int64_t{42}},
                 "native caller receives the same result from the VM-only callee");
#if EMBER_HAS_WIN64_JIT
    tests.expect(caller != nullptr && caller->tier() == ember::runtime::ExecutionTier::native &&
                     callee != nullptr && callee->tier() == ember::runtime::ExecutionTier::virtualMachine,
                 "bridge preserves mixed native/VM tier selection");
#endif
}

EMBER_TEST("zero-argument native bridge calls are accepted")
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
                 "zero-argument native-to-VM call preserves the VM result");
#if EMBER_HAS_WIN64_JIT
    tests.expect(jitVm.function(0)->tier() == ember::runtime::ExecutionTier::native &&
                     jitVm.function(1)->tier() == ember::runtime::ExecutionTier::virtualMachine,
                 "zero-argument bridge keeps an unsupported callee in VM tier");
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
                     function->tier() == ember::runtime::ExecutionTier::virtualMachine,
                 "injected failure cannot publish a partial entry point or change the VM result");
}
} // namespace
