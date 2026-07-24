#include "ember/bytecode/bytecode.hpp"
#include "ember/runtime/vm.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <optional>
#include <utility>

namespace
{
constexpr std::size_t warmupIterations = 10'000;
constexpr std::size_t sampleIterations = 100'000;
constexpr std::size_t sampleCount = 9;

[[nodiscard]] ember::bytecode::VerifiedProgram makeProgram()
{
    ember::bytecode::Program program{
        .functions = {{.id = 0,
                       .kind = ember::semantic::FunctionKind::user,
                       .signature = {.parameterTypes = {},
                                     .returnType = ember::semantic::Type::voidType},
                       .localCount = 0,
                       .localTypes = {},
                       .code = {{.opcode = ember::bytecode::Opcode::returnVoid,
                                 .operand = 0,
                                 .value = std::nullopt}}}}};
    auto verified = ember::bytecode::Verifier{}.verify(std::move(program));
    if (!verified.program)
        std::abort();
    return std::move(*verified.program);
}

void execute(ember::runtime::VirtualMachine &vm, std::size_t count)
{
    for (std::size_t iteration = 0; iteration < count; ++iteration)
    {
        const auto report = vm.execute(0);
        if (report.result.error)
            std::abort();
    }
}

[[nodiscard]] double medianNanosecondsPerCall(ember::runtime::RuntimeOptions options)
{
    auto vm = ember::runtime::VirtualMachine::create(makeProgram(), options);
    execute(vm, warmupIterations);

    std::array<double, sampleCount> samples{};
    for (auto &sample : samples)
    {
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

int main()
{
    const auto disabled = medianNanosecondsPerCall(
        {.hotThreshold = 0, .jitEnabled = true, .profilingEnabled = false});
    const auto thresholdZero = medianNanosecondsPerCall(
        {.hotThreshold = 0, .jitEnabled = true, .profilingEnabled = true});
    const auto alreadyHot = medianNanosecondsPerCall(
        {.hotThreshold = 1, .jitEnabled = true, .profilingEnabled = true});

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "runtime dispatch benchmark (median of " << sampleCount << " samples, "
              << sampleIterations << " calls/sample)\n";
    std::cout << "profiling disabled: " << disabled << " ns/call\n";
    std::cout << "profiling enabled, threshold 0: " << thresholdZero << " ns/call ("
              << ((thresholdZero / disabled) - 1.0) * 100.0 << "% vs disabled)\n";
    std::cout << "profiling enabled, already hot: " << alreadyHot << " ns/call ("
              << ((alreadyHot / disabled) - 1.0) * 100.0 << "% vs disabled)\n";
    return 0;
}
