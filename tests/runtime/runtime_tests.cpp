#include "ember/bytecode/bytecode.hpp"
#include "ember/runtime/vm.hpp"

#include "test_harness.hpp"

#include <cstdint>
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
} // namespace
