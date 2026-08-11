#include "ember/bytecode/bytecode.hpp"
#include "ember/jit/platform.hpp"
#include "ember/runtime/vm.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <optional>
#include <utility>

namespace {
constexpr std::size_t warmupIterations = 2'000;
constexpr std::size_t sampleIterations = 20'000;
constexpr std::size_t sampleCount = 9;
constexpr std::int64_t loopIterations = 32;

[[nodiscard]] ember::bytecode::VerifiedProgram makeProgram() {
    ember::bytecode::Program program{
        .functions = {
            {.id = 0,
             .kind = ember::semantic::FunctionKind::user,
             .signature = {.parameterTypes = {ember::semantic::Type::i64},
                           .returnType = ember::semantic::Type::i64},
             .localCount = 2,
             .localTypes = {ember::semantic::Type::i64, ember::semantic::Type::i64},
             .code = {
                 {.opcode = ember::bytecode::Opcode::constant,
                  .operand = 0,
                  .value = ember::bytecode::Value{std::int64_t{0}}},
                 {.opcode = ember::bytecode::Opcode::store, .operand = 1, .value = std::nullopt},
                 {.opcode = ember::bytecode::Opcode::load, .operand = 1, .value = std::nullopt},
                 {.opcode = ember::bytecode::Opcode::load, .operand = 0, .value = std::nullopt},
                 {.opcode = ember::bytecode::Opcode::lessI64, .operand = 0, .value = std::nullopt},
                 {.opcode = ember::bytecode::Opcode::jumpIfFalse,
                  .operand = 11,
                  .value = std::nullopt},
                 {.opcode = ember::bytecode::Opcode::load, .operand = 1, .value = std::nullopt},
                 {.opcode = ember::bytecode::Opcode::constant,
                  .operand = 0,
                  .value = ember::bytecode::Value{std::int64_t{1}}},
                 {.opcode = ember::bytecode::Opcode::addI64, .operand = 0, .value = std::nullopt},
                 {.opcode = ember::bytecode::Opcode::store, .operand = 1, .value = std::nullopt},
                 {.opcode = ember::bytecode::Opcode::jump, .operand = 2, .value = std::nullopt},
                 {.opcode = ember::bytecode::Opcode::load, .operand = 1, .value = std::nullopt},
                 {.opcode = ember::bytecode::Opcode::returnValue,
                  .operand = 0,
                  .value = std::nullopt}}}}};
    auto verified = ember::bytecode::Verifier{}.verify(std::move(program));
    if (!verified.program)
        std::abort();
    return std::move(*verified.program);
}

void execute(ember::runtime::VirtualMachine& vm, std::size_t count) {
    for (std::size_t iteration = 0; iteration < count; ++iteration) {
        const auto report = vm.execute(0, {loopIterations});
        if (report.result.error || report.result.value != ember::bytecode::Value{loopIterations})
            std::abort();
    }
}

[[nodiscard]] double medianNanosecondsPerCall(ember::runtime::RuntimeOptions options,
                                              bool expectNative) {
    auto vm = ember::runtime::VirtualMachine::create(makeProgram(), options);
    execute(vm, warmupIterations);
#if EMBER_HAS_WIN64_JIT
    if (expectNative && vm.function(0)->tier() != ember::runtime::ExecutionTier::native)
        std::abort();
#else
    static_cast<void>(expectNative);
#endif

    std::array<double, sampleCount> samples{};
    for (auto& sample : samples) {
        const auto start = std::chrono::steady_clock::now();
        execute(vm, sampleIterations);
        const auto elapsed = std::chrono::steady_clock::now() - start;
        sample = static_cast<double>(
                     std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count()) /
                 static_cast<double>(sampleIterations);
    }
    std::ranges::sort(samples);
    return samples[sampleCount / 2];
}
} // namespace

int main() {
#if !EMBER_HAS_WIN64_JIT
    std::cout << "runtime dispatch benchmark unavailable: baseline native tier requires Win64\n";
    return 0;
#else
    const auto vm =
        medianNanosecondsPerCall({.hotThreshold = 1, .jitEnabled = false, .profilingEnabled = true},
                                 false);
    const auto jit =
        medianNanosecondsPerCall({.hotThreshold = 1, .jitEnabled = true, .profilingEnabled = true},
                                 true);

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "runtime dispatch benchmark (median of " << sampleCount << " samples, "
              << sampleIterations << " calls/sample)\n";
    std::cout << "hot_loop iterations/call: " << loopIterations << '\n';
    std::cout << "VM: " << vm << " ns/call\n";
    std::cout << "warmed baseline JIT: " << jit << " ns/call\n";
    std::cout << "speedup: " << vm / jit << "x; latency reduction: " << (1.0 - jit / vm) * 100.0
              << "%\n";
    return 0;
#endif
}
