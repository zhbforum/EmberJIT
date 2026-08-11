#include "ember/bytecode/builtins.hpp"
#include "ember/bytecode/bytecode.hpp"
#include "ember/frontend/ast_printer.hpp"
#include "ember/frontend/lexer.hpp"
#include "ember/frontend/parser.hpp"
#include "ember/ir/bytecode_lowerer.hpp"
#include "ember/ir/dump.hpp"
#include "ember/ir/optimization.hpp"
#include "ember/jit/baseline_compiler.hpp"
#include "ember/runtime/vm.hpp"
#include "ember/semantic/analyzer.hpp"
#include "ember/semantic/typed_ast_printer.hpp"
#include "ember/support/diagnostic.hpp"
#include "ember/support/source.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {
struct RunOptions {
    std::uint64_t hotThreshold{1000};
    bool traceJit{};
    bool jitEnabled{true};
};

struct BenchmarkOptions {
    std::uint64_t iterations{10};
};

struct PreparedRuntimeProgram {
    ember::support::SourceText source;
    ember::semantic::AnalysisResult analysis;
    ember::bytecode::VerifiedProgram verifiedProgram;
};

struct RunOptionsParseResult {
    std::optional<RunOptions> options;
    std::string_view path;
    std::string error;
};

struct BenchmarkOptionsParseResult {
    std::optional<BenchmarkOptions> options;
    std::string_view path;
    std::string error;
};

constexpr std::uint64_t maximumBenchmarkIterations{100'000};

[[nodiscard]] auto
diagnosticSeverityName(ember::support::DiagnosticSeverity severity) noexcept -> std::string_view {
    using ember::support::DiagnosticSeverity;

    switch (severity) {
    case DiagnosticSeverity::error:
        return "error";
    case DiagnosticSeverity::warning:
        return "warning";
    case DiagnosticSeverity::note:
        return "note";
    }

    return "diagnostic";
}

[[nodiscard]] bool readFile(std::string_view path, std::string& contents) {
    std::ifstream input{std::string{path}, std::ios::binary};
    if (!input.is_open()) {
        return false;
    }

    contents.assign(std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{});
    return !input.bad();
}

void printDiagnostic(std::string_view path,
                     const ember::support::SourceText& source,
                     const ember::support::Diagnostic& diagnostic) {
    const auto location = source.locationAt(diagnostic.primarySpan.begin)
                              .value_or(ember::support::SourceLocation{
                                  .offset = diagnostic.primarySpan.begin,
                                  .line = 1,
                                  .column = 1,
                              });

    std::cerr << path << ':' << location.line << ':' << location.column << ": "
              << diagnosticSeverityName(diagnostic.severity) << '[' << diagnostic.code
              << "]: " << diagnostic.message << '\n';
}

void printEntryPointDiagnostic(std::string_view path,
                               const ember::support::SourceText& source,
                               ember::support::SourceSpan span,
                               std::string message) {
    printDiagnostic(path,
                    source,
                    {.stage = ember::support::DiagnosticStage::runtime,
                     .severity = ember::support::DiagnosticSeverity::error,
                     .code = "E5001",
                     .message = std::move(message),
                     .primarySpan = span});
}

void printOptimizationFailure(const ember::ir::OptimizationResult& result) {
    const auto pass =
        result.failedPass ? ember::ir::optimizationPassName(*result.failedPass) : "unknown";
    std::cerr << "IR optimization error[" << pass << "]\n";
    for (const auto& diagnostic : result.diagnostics)
        std::cerr << "  " << diagnostic.code << ": " << diagnostic.message << '\n';
}

[[nodiscard]] int dumpTokens(std::string_view path, std::string contents) {
    const ember::support::SourceText source{
        ember::support::SourceId{1},
        std::string{path},
        std::move(contents),
    };
    const auto result = ember::frontend::Lexer{}.lex(source);

    if (!result.diagnostics.empty()) {
        for (const auto& diagnostic : result.diagnostics) {
            printDiagnostic(path, source, diagnostic);
        }
        return 1;
    }

    for (const auto& token : result.tokens) {
        std::cout << ember::frontend::tokenKindName(token.kind) << " [" << token.span.begin << ", "
                  << token.span.end << ")\n";
    }
    return 0;
}

[[nodiscard]] int dumpAst(std::string_view path, std::string contents) {
    const ember::support::SourceText source{
        ember::support::SourceId{1},
        std::string{path},
        std::move(contents),
    };
    const auto lexResult = ember::frontend::Lexer{}.lex(source);
    if (!lexResult.diagnostics.empty()) {
        for (const auto& diagnostic : lexResult.diagnostics) {
            printDiagnostic(path, source, diagnostic);
        }
        return 1;
    }

    const auto parseResult = ember::frontend::Parser{}.parse(source, lexResult.tokens);
    if (!parseResult.diagnostics.empty()) {
        for (const auto& diagnostic : parseResult.diagnostics) {
            printDiagnostic(path, source, diagnostic);
        }
        return 1;
    }

    std::cout << ember::frontend::AstPrinter{}.print(*parseResult.program, source);
    return 0;
}

[[nodiscard]] int dumpTypedAst(std::string_view path, std::string contents) {
    const ember::support::SourceText source{ember::support::SourceId{1},
                                            std::string{path},
                                            std::move(contents)};
    const auto lexResult = ember::frontend::Lexer{}.lex(source);
    if (!lexResult.diagnostics.empty()) {
        for (const auto& diagnostic : lexResult.diagnostics)
            printDiagnostic(path, source, diagnostic);
        return 1;
    }
    const auto parseResult = ember::frontend::Parser{}.parse(source, lexResult.tokens);
    if (!parseResult.diagnostics.empty()) {
        for (const auto& diagnostic : parseResult.diagnostics)
            printDiagnostic(path, source, diagnostic);
        return 1;
    }
    const auto semanticResult =
        ember::semantic::SemanticAnalyzer{}.analyze(*parseResult.program, source);
    if (!semanticResult.diagnostics.empty()) {
        for (const auto& diagnostic : semanticResult.diagnostics)
            printDiagnostic(path, source, diagnostic);
        return 1;
    }
    std::cout << ember::semantic::TypedAstPrinter{}.print(*semanticResult.program, source);
    return 0;
}

[[nodiscard]] auto analyzeForRuntime(std::string_view path,
                                     std::string contents,
                                     ember::semantic::AnalysisResult& analysis)
    -> std::optional<ember::support::SourceText> {
    ember::support::SourceText source{ember::support::SourceId{1},
                                      std::string{path},
                                      std::move(contents)};
    const auto lexed = ember::frontend::Lexer{}.lex(source);
    if (!lexed.diagnostics.empty()) {
        for (const auto& d : lexed.diagnostics)
            printDiagnostic(path, source, d);
        return std::nullopt;
    }
    const auto parsed = ember::frontend::Parser{}.parse(source, lexed.tokens);
    if (!parsed.diagnostics.empty()) {
        for (const auto& d : parsed.diagnostics)
            printDiagnostic(path, source, d);
        return std::nullopt;
    }
    ember::semantic::HostFunctionRegistry hosts;
    if (!ember::bytecode::registerBuiltins(hosts)) {
        std::cerr << "error: unable to initialize runtime built-ins\n";
        return std::nullopt;
    }
    analysis = ember::semantic::SemanticAnalyzer{}.analyze(*parsed.program, source, hosts);
    if (!analysis.diagnostics.empty()) {
        for (const auto& d : analysis.diagnostics)
            printDiagnostic(path, source, d);
        return std::nullopt;
    }
    return source;
}

[[nodiscard]] auto prepareRuntimeProgram(std::string_view path, std::string contents)
    -> std::optional<PreparedRuntimeProgram> {
    ember::semantic::AnalysisResult analysis;
    auto source = analyzeForRuntime(path, std::move(contents), analysis);
    if (!source)
        return std::nullopt;
    if (!analysis.program) {
        std::cerr << "internal error: semantic analysis completed without a typed program\n";
        return std::nullopt;
    }

    auto compiled = ember::bytecode::Compiler{}.compile(*analysis.program);
    if (!compiled.program) {
        for (const auto& diagnostic : compiled.diagnostics)
            std::cerr << "bytecode error: " << diagnostic.message << '\n';
        return std::nullopt;
    }
    auto checked = ember::bytecode::Verifier{}.verify(std::move(*compiled.program));
    if (!checked.program) {
        for (const auto& diagnostic : checked.diagnostics)
            std::cerr << "bytecode error: " << diagnostic.message << '\n';
        return std::nullopt;
    }

    return PreparedRuntimeProgram{
        .source = std::move(*source),
        .analysis = std::move(analysis),
        .verifiedProgram = std::move(*checked.program),
    };
}

[[nodiscard]] auto findCliEntryPoint(std::string_view path, const PreparedRuntimeProgram& program)
    -> std::optional<ember::semantic::FunctionId> {
    const auto entry = std::find_if(program.analysis.program->declarations.begin(),
                                    program.analysis.program->declarations.end(),
                                    [](const auto& function) { return function.name == "main"; });
    if (entry == program.analysis.program->declarations.end()) {
        printEntryPointDiagnostic(path,
                                  program.source,
                                  {.source = program.source.id(),
                                   .begin = program.source.size(),
                                   .end = program.source.size()},
                                  "missing user entry function 'main'");
        return std::nullopt;
    }
    if (!entry->signature.parameterTypes.empty()) {
        printEntryPointDiagnostic(path,
                                  program.source,
                                  entry->nameSpan,
                                  "entry function 'main' must not accept parameters");
        return std::nullopt;
    }
    if (entry->signature.returnType != ember::semantic::Type::voidType &&
        entry->signature.returnType != ember::semantic::Type::i64) {
        printEntryPointDiagnostic(path,
                                  program.source,
                                  entry->nameSpan,
                                  "entry function 'main' must return void or i64");
        return std::nullopt;
    }
    return entry->id;
}

[[nodiscard]] int dumpBytecode(std::string_view path, std::string contents) {
    ember::semantic::AnalysisResult analysis;
    if (!analyzeForRuntime(path, std::move(contents), analysis))
        return 1;
    auto compiled = ember::bytecode::Compiler{}.compile(*analysis.program);
    if (!compiled.program) {
        for (const auto& diagnostic : compiled.diagnostics)
            std::cerr << "bytecode error: " << diagnostic.message << '\n';
        return 1;
    }
    auto checked = ember::bytecode::Verifier{}.verify(std::move(*compiled.program));
    if (!checked.program) {
        for (const auto& diagnostic : checked.diagnostics)
            std::cerr << "bytecode error: " << diagnostic.message << '\n';
        return 1;
    }
    std::cout << ember::bytecode::dump(*checked.program);
    return 0;
}

[[nodiscard]] int dumpIr(std::string_view path, std::string contents) {
    ember::semantic::AnalysisResult analysis;
    if (!analyzeForRuntime(path, std::move(contents), analysis))
        return 1;
    auto compiled = ember::bytecode::Compiler{}.compile(*analysis.program);
    if (!compiled.program) {
        for (const auto& diagnostic : compiled.diagnostics)
            std::cerr << "bytecode error: " << diagnostic.message << '\n';
        return 1;
    }
    auto checked = ember::bytecode::Verifier{}.verify(std::move(*compiled.program));
    if (!checked.program) {
        for (const auto& diagnostic : checked.diagnostics)
            std::cerr << "bytecode error: " << diagnostic.message << '\n';
        return 1;
    }

    struct FunctionLowering {
        ember::semantic::FunctionId id;
        ember::ir::LoweringResult result;
    };
    std::vector<FunctionLowering> loweredFunctions;
    for (const auto& function : checked.program->program().functions) {
        if (function.kind != ember::semantic::FunctionKind::user)
            continue;
        loweredFunctions.push_back(
            {.id = function.id,
             .result = ember::ir::Lowerer{}.lower(*checked.program, function.id)});
    }
    for (const auto& lowered : loweredFunctions) {
        if (!lowered.result.function &&
            lowered.result.failure != ember::ir::LoweringFailure::unsupported) {
            if (lowered.result.diagnostics.empty()) {
                std::cerr << "IR lowering error: failed without a diagnostic\n";
                return 1;
            }
            for (const auto& diagnostic : lowered.result.diagnostics)
                std::cerr << "IR lowering error[" << diagnostic.code << "]: " << diagnostic.message
                          << '\n';
            return 1;
        }
    }
    for (const auto& lowered : loweredFunctions) {
        if (lowered.result.function) {
            const auto optimized = ember::ir::OptimizationPipeline{}.run(*lowered.result.function);
            if (!optimized.function) {
                printOptimizationFailure(optimized);
                return 1;
            }
            std::cout << ember::ir::dump(*optimized.function);
            continue;
        }
        const auto message = lowered.result.diagnostics.empty()
                                 ? "unsupported native form"
                                 : lowered.result.diagnostics.front().message;
        std::cout << "fn #" << lowered.id << ": not lowered (" << message << ")\n";
    }
    return 0;
}

[[nodiscard]] int dumpAsm(std::string_view path, std::string contents) {
    ember::semantic::AnalysisResult analysis;
    if (!analyzeForRuntime(path, std::move(contents), analysis))
        return 1;
    auto compiled = ember::bytecode::Compiler{}.compile(*analysis.program);
    if (!compiled.program) {
        for (const auto& diagnostic : compiled.diagnostics)
            std::cerr << "bytecode error: " << diagnostic.message << '\n';
        return 1;
    }
    auto checked = ember::bytecode::Verifier{}.verify(std::move(*compiled.program));
    if (!checked.program) {
        for (const auto& diagnostic : checked.diagnostics)
            std::cerr << "bytecode error: " << diagnostic.message << '\n';
        return 1;
    }
    for (const auto& function : checked.program->program().functions) {
        if (function.kind != ember::semantic::FunctionKind::user)
            continue;
        auto lowered = ember::ir::Lowerer{}.lower(*checked.program, function.id);
        if (!lowered.function) {
            const auto message =
                lowered.diagnostics.empty() ? "not lowerable" : lowered.diagnostics.front().message;
            std::cout << "fn #" << function.id << ": no native code (" << message << ")\n";
            continue;
        }
        auto optimized = ember::ir::OptimizationPipeline{}.run(*lowered.function);
        if (!optimized.function) {
            const auto pass = optimized.failedPass
                                  ? ember::ir::optimizationPassName(*optimized.failedPass)
                                  : "unknown";
            std::cout << "fn #" << function.id << ": no native code (IR optimization failed in "
                      << pass << ")\n";
            continue;
        }
        auto native = ember::jit::x64::BaselineCompiler{}.compile(*optimized.function);
        if (!native.code) {
            std::cout << "fn #" << function.id
                      << ": no native code (baseline subset unsupported)\n";
            continue;
        }
        std::cout << "fn #" << function.id << ":\n" << native.code->listing() << '\n';
    }
    return 0;
}

void printJitTrace(const ember::semantic::AnalysisResult& analysis,
                   const ember::runtime::ExecutionReport& report) {
    std::unordered_map<ember::semantic::FunctionId, std::string> functionNames;
    functionNames.reserve(analysis.program->declarations.size());
    for (const auto& function : analysis.program->declarations)
        functionNames.emplace(function.id, function.name);

    for (const auto& event : report.hotEvents) {
        const auto name = functionNames.find(event.functionId);
        std::cerr << "[jit] function " << (name == functionNames.end() ? "<unknown>" : name->second)
                  << " became hot after " << event.invocationCount << " calls\n";
    }
}

[[nodiscard]] int runVm(PreparedRuntimeProgram program,
                        ember::semantic::FunctionId entry,
                        const RunOptions& options) {
    const ember::runtime::RuntimeOptions runtimeOptions{
        .hotThreshold = options.hotThreshold,
        .jitEnabled = options.jitEnabled,
        .profilingEnabled = true,
    };
    auto vm =
        ember::runtime::VirtualMachine::create(std::move(program.verifiedProgram), runtimeOptions);
    const auto report = vm.execute(entry);
    if (options.traceJit)
        printJitTrace(program.analysis, report);
    if (report.result.error) {
        std::cerr << "runtime error[" << report.result.error->code
                  << "]: " << report.result.error->message << '\n';
        return 1;
    }
    return 0;
}

[[nodiscard]] auto cloneVerifiedProgram(const ember::bytecode::VerifiedProgram& program)
    -> std::optional<ember::bytecode::VerifiedProgram> {
    auto checked = ember::bytecode::Verifier{}.verify(program.program());
    if (!checked.program) {
        std::cerr << "internal error: unable to clone verified bytecode for benchmark\n";
        return std::nullopt;
    }
    return std::move(*checked.program);
}

[[nodiscard]] bool executeBenchmarkIteration(ember::runtime::VirtualMachine& vm,
                                             ember::semantic::FunctionId entry) {
    const auto report = vm.execute(entry);
    if (!report.result.error)
        return true;

    std::cerr << "runtime error[" << report.result.error->code
              << "]: " << report.result.error->message << '\n';
    return false;
}

class DiscardingStreamBuffer final : public std::streambuf {
protected:
    auto overflow(int_type character) -> int_type override {
        return traits_type::not_eof(character);
    }

    auto xsputn(const char*, std::streamsize count) -> std::streamsize override {
        return count;
    }
};

class ScopedStandardOutputSilencer {
public:
    ScopedStandardOutputSilencer()
        : previous_(std::cout.rdbuf(&sink_)) {
    }

    ScopedStandardOutputSilencer(const ScopedStandardOutputSilencer&) = delete;
    auto operator=(const ScopedStandardOutputSilencer&) -> ScopedStandardOutputSilencer& = delete;

    ~ScopedStandardOutputSilencer() {
        std::cout.rdbuf(previous_);
    }

private:
    DiscardingStreamBuffer sink_;
    std::streambuf* previous_;
};

[[nodiscard]] auto
measureVirtualMachine(const ember::bytecode::VerifiedProgram& program,
                      ember::semantic::FunctionId entry,
                      std::uint64_t iterations) -> std::optional<std::chrono::nanoseconds> {
    auto clone = cloneVerifiedProgram(program);
    if (!clone)
        return std::nullopt;
    const ember::runtime::RuntimeOptions options{
        .hotThreshold = 0,
        .jitEnabled = false,
        .profilingEnabled = false,
    };
    auto vm = ember::runtime::VirtualMachine::create(std::move(*clone), options);
    const auto started = std::chrono::steady_clock::now();
    for (std::uint64_t iteration{}; iteration < iterations; ++iteration) {
        if (!executeBenchmarkIteration(vm, entry))
            return std::nullopt;
    }
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() -
                                                                started);
}

[[nodiscard]] auto
measureColdJit(const ember::bytecode::VerifiedProgram& program,
               ember::semantic::FunctionId entry,
               std::uint64_t iterations) -> std::optional<std::chrono::nanoseconds> {
    std::chrono::nanoseconds elapsed{};
    const ember::runtime::RuntimeOptions options{
        .hotThreshold = 1,
        .jitEnabled = true,
        .profilingEnabled = true,
    };
    for (std::uint64_t iteration{}; iteration < iterations; ++iteration) {
        auto clone = cloneVerifiedProgram(program);
        if (!clone)
            return std::nullopt;
        auto vm = ember::runtime::VirtualMachine::create(std::move(*clone), options);
        const auto started = std::chrono::steady_clock::now();
        if (!executeBenchmarkIteration(vm, entry))
            return std::nullopt;
        elapsed += std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - started);
    }
    return elapsed;
}

[[nodiscard]] auto
measureWarmedJit(ember::runtime::VirtualMachine& vm,
                 ember::semantic::FunctionId entry,
                 std::uint64_t iterations) -> std::optional<std::chrono::nanoseconds> {
    const auto started = std::chrono::steady_clock::now();
    for (std::uint64_t iteration{}; iteration < iterations; ++iteration) {
        if (!executeBenchmarkIteration(vm, entry))
            return std::nullopt;
    }
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() -
                                                                started);
}

void printBenchmarkMeasurement(std::string_view label,
                               std::chrono::nanoseconds elapsed,
                               std::uint64_t iterations) {
    const auto milliseconds = std::chrono::duration<double, std::milli>{elapsed}.count();
    const auto millisecondsPerRun = milliseconds / static_cast<double>(iterations);
    std::cout << label << ": " << milliseconds << " ms total (" << millisecondsPerRun
              << " ms/run)\n";
}

[[nodiscard]] int runBenchmark(const PreparedRuntimeProgram& program,
                               ember::semantic::FunctionId entry,
                               const BenchmarkOptions& options) {
    const ember::runtime::RuntimeOptions jitOptions{
        .hotThreshold = 1,
        .jitEnabled = true,
        .profilingEnabled = true,
    };
    auto warmClone = cloneVerifiedProgram(program.verifiedProgram);
    if (!warmClone)
        return 1;
    auto warmedVm = ember::runtime::VirtualMachine::create(std::move(*warmClone), jitOptions);

    std::optional<std::chrono::nanoseconds> vmElapsed;
    std::optional<std::chrono::nanoseconds> coldJitElapsed;
    std::optional<std::chrono::nanoseconds> warmedJitElapsed;
    {
        // Existing built-ins use std::cout. Suppress their output so the command's
        // table remains a stable measurement result rather than N repeated runs.
        ScopedStandardOutputSilencer silenceProgramOutput;
        if (!executeBenchmarkIteration(warmedVm, entry))
            return 1;
        const auto* entryFunction = warmedVm.function(entry);
        if (entryFunction == nullptr ||
            entryFunction->tier() != ember::runtime::ExecutionTier::native) {
            std::cerr << "error: benchmark requires a native entry function; "
                         "the baseline JIT is unavailable for this program or platform\n";
            return 1;
        }

        vmElapsed = measureVirtualMachine(program.verifiedProgram, entry, options.iterations);
        coldJitElapsed = measureColdJit(program.verifiedProgram, entry, options.iterations);
        warmedJitElapsed = measureWarmedJit(warmedVm, entry, options.iterations);
    }
    if (!vmElapsed || !coldJitElapsed || !warmedJitElapsed)
        return 1;

    std::cout << std::fixed << std::setprecision(3);
    std::cout << "benchmark: " << program.source.name() << '\n';
    std::cout << "iterations: " << options.iterations << '\n';
    printBenchmarkMeasurement("VM", *vmElapsed, options.iterations);
    printBenchmarkMeasurement("cold JIT (includes native compilation)",
                              *coldJitElapsed,
                              options.iterations);
    printBenchmarkMeasurement("warmed JIT", *warmedJitElapsed, options.iterations);
    std::cout << "note: program stdout is suppressed; these are reference measurements for this "
                 "machine\n";
    return 0;
}

[[nodiscard]] bool isDumpCommand(std::string_view command) noexcept {
    return command == "dump-tokens" || command == "dump-ast" || command == "dump-typed-ast" ||
           command == "dump-bytecode" || command == "dump-ir" || command == "dump-asm";
}

void printGeneralUsage(std::ostream& output, std::string_view executable) {
    output << "usage: " << executable << " <command> [options] <file>\n"
           << "\n"
           << "commands:\n"
           << "  run                 execute a program\n"
           << "  benchmark           measure VM, cold JIT, and warmed JIT execution\n"
           << "  dump-tokens         print lexer tokens\n"
           << "  dump-ast            print the syntax tree\n"
           << "  dump-typed-ast      print the typed syntax tree\n"
           << "  dump-bytecode       print verified bytecode\n"
           << "  dump-ir             print optimized IR\n"
           << "  dump-asm            print baseline native-code listings\n"
           << "\n"
           << "global options:\n"
           << "  --help, help        show help\n"
           << "  --version           show the version\n"
           << "\n"
           << "Run '" << executable << " <command> --help' for command-specific usage.\n";
}

void printRunUsage(std::ostream& output, std::string_view executable) {
    output << "usage: " << executable
           << " run [--no-jit] [--jit-threshold=<non-negative-integer>] [--trace-jit] <file>\n"
           << "\n"
           << "  --no-jit                         run only in the VM\n"
           << "  --jit-threshold=<non-negative-integer>  calls before a function becomes hot\n"
           << "  --trace-jit                      print hot-function transitions to stderr\n";
}

void printBenchmarkUsage(std::ostream& output, std::string_view executable) {
    output << "usage: " << executable << " benchmark [--iterations=<1-100000>] <file>\n"
           << "\n"
           << "Measures VM execution, cold JIT execution including native compilation, and warmed "
              "JIT\n"
           << "execution. Program stdout is suppressed while measuring.\n"
           << "\n"
           << "  --iterations=<1-100000>  measurements per execution mode (default: 10)\n";
}

void printDumpUsage(std::ostream& output, std::string_view executable, std::string_view command) {
    output << "usage: " << executable << ' ' << command << " <file>\n";
}

[[nodiscard]] bool
printCommandHelp(std::ostream& output, std::string_view executable, std::string_view command) {
    if (command == "run") {
        printRunUsage(output, executable);
        return true;
    }
    if (command == "benchmark") {
        printBenchmarkUsage(output, executable);
        return true;
    }
    if (isDumpCommand(command)) {
        printDumpUsage(output, executable, command);
        return true;
    }
    return false;
}

void printUsageError(std::string_view executable, std::string_view message) {
    std::cerr << "error: " << message << "\n\n";
    printGeneralUsage(std::cerr, executable);
}

[[nodiscard]] std::string unknownOptionError(std::string_view option, std::string_view command) {
    return "unknown option '" + std::string{option} + "' for " + std::string{command};
}

[[nodiscard]] bool parseHotThreshold(std::string_view text, std::uint64_t& threshold) {
    std::uint64_t parsed{};
    const auto [position, error] = std::from_chars(text.data(), text.data() + text.size(), parsed);
    if (error != std::errc{} || position != text.data() + text.size())
        return false;
    threshold = parsed;
    return true;
}

[[nodiscard]] RunOptionsParseResult parseRunOptions(int argumentCount, char* arguments[]) {
    RunOptions options;
    bool thresholdSpecified{};
    bool traceSpecified{};
    bool noJitSpecified{};
    std::string_view path;

    for (int index = 2; index < argumentCount; ++index) {
        const std::string_view argument{arguments[index]};
        if (argument == "--no-jit") {
            if (noJitSpecified)
                return {.options = std::nullopt,
                        .path = {},
                        .error = "--no-jit may be specified only once"};
            noJitSpecified = true;
            options.jitEnabled = false;
            continue;
        }
        if (argument == "--trace-jit") {
            if (traceSpecified)
                return {.options = std::nullopt,
                        .path = {},
                        .error = "--trace-jit may be specified only once"};
            traceSpecified = true;
            options.traceJit = true;
            continue;
        }
        constexpr std::string_view thresholdPrefix{"--jit-threshold="};
        if (argument.starts_with(thresholdPrefix)) {
            if (thresholdSpecified ||
                !parseHotThreshold(argument.substr(thresholdPrefix.size()), options.hotThreshold)) {
                return {.options = std::nullopt,
                        .path = {},
                        .error = "--jit-threshold must be a non-negative integer and may be "
                                 "specified only once"};
            }
            thresholdSpecified = true;
            continue;
        }
        if (argument.starts_with("-")) {
            return {.options = std::nullopt,
                    .path = {},
                    .error = unknownOptionError(argument, "run")};
        }
        if (!path.empty()) {
            return {.options = std::nullopt,
                    .path = {},
                    .error = "run accepts exactly one source file"};
        }
        path = argument;
    }

    if (path.empty()) {
        return {.options = std::nullopt,
                .path = {},
                .error = "run requires exactly one source file"};
    }
    return {.options = options, .path = path, .error = {}};
}

[[nodiscard]] BenchmarkOptionsParseResult parseBenchmarkOptions(int argumentCount,
                                                                char* arguments[]) {
    BenchmarkOptions options;
    bool iterationsSpecified{};
    std::string_view path;

    for (int index = 2; index < argumentCount; ++index) {
        const std::string_view argument{arguments[index]};
        constexpr std::string_view iterationsPrefix{"--iterations="};
        if (argument.starts_with(iterationsPrefix)) {
            if (iterationsSpecified ||
                !parseHotThreshold(argument.substr(iterationsPrefix.size()), options.iterations) ||
                options.iterations == 0 || options.iterations > maximumBenchmarkIterations) {
                return {.options = std::nullopt,
                        .path = {},
                        .error = "--iterations must be an integer from 1 through 100000 and may be "
                                 "specified only once"};
            }
            iterationsSpecified = true;
            continue;
        }
        if (argument.starts_with("-")) {
            return {.options = std::nullopt,
                    .path = {},
                    .error = unknownOptionError(argument, "benchmark")};
        }
        if (!path.empty()) {
            return {.options = std::nullopt,
                    .path = {},
                    .error = "benchmark accepts exactly one source file"};
        }
        path = argument;
    }

    if (path.empty()) {
        return {.options = std::nullopt,
                .path = {},
                .error = "benchmark requires exactly one source file"};
    }
    return {.options = options, .path = path, .error = {}};
}

} // namespace

int main(int argumentCount, char* arguments[]) {
    if (argumentCount == 1) {
        printGeneralUsage(std::cout, arguments[0]);
        return 0;
    }

    const std::string_view command{arguments[1]};
    if (command == "--version") {
        if (argumentCount != 2) {
            printUsageError(arguments[0], "--version does not accept arguments");
            return 2;
        }
        std::cout << "EmberJIT 0.1.0\n";
        return 0;
    }
    if (command == "--help" || command == "help") {
        if (argumentCount == 2) {
            printGeneralUsage(std::cout, arguments[0]);
            return 0;
        }
        if (argumentCount == 3 && command == "help" &&
            printCommandHelp(std::cout, arguments[0], arguments[2])) {
            return 0;
        }
        printUsageError(arguments[0], "help accepts at most one known command");
        return 2;
    }
    if (argumentCount == 3 && std::string_view{arguments[2]} == "--help") {
        if (printCommandHelp(std::cout, arguments[0], command))
            return 0;
        printUsageError(arguments[0], "unknown command");
        return 2;
    }

    if (command == "run") {
        const auto parsed = parseRunOptions(argumentCount, arguments);
        if (!parsed.options) {
            printUsageError(arguments[0], parsed.error);
            return 2;
        }
        std::string contents;
        if (!readFile(parsed.path, contents)) {
            std::cerr << "error: unable to read source file '" << parsed.path << "'\n";
            return 1;
        }
        auto program = prepareRuntimeProgram(parsed.path, std::move(contents));
        if (!program)
            return 1;
        const auto entry = findCliEntryPoint(parsed.path, *program);
        if (!entry)
            return 1;
        return runVm(std::move(*program), *entry, *parsed.options);
    }
    if (command == "benchmark") {
        const auto parsed = parseBenchmarkOptions(argumentCount, arguments);
        if (!parsed.options) {
            printUsageError(arguments[0], parsed.error);
            return 2;
        }
        std::string contents;
        if (!readFile(parsed.path, contents)) {
            std::cerr << "error: unable to read source file '" << parsed.path << "'\n";
            return 1;
        }
        const auto program = prepareRuntimeProgram(parsed.path, std::move(contents));
        if (!program)
            return 1;
        const auto entry = findCliEntryPoint(parsed.path, *program);
        if (!entry)
            return 1;
        return runBenchmark(*program, *entry, *parsed.options);
    }
    if (isDumpCommand(command)) {
        if (argumentCount != 3) {
            printUsageError(arguments[0], "inspection commands require exactly one source file");
            return 2;
        }

        std::string contents;
        const std::string_view path{arguments[2]};
        if (path.starts_with("-")) {
            printUsageError(arguments[0], unknownOptionError(path, command));
            return 2;
        }
        if (!readFile(path, contents)) {
            std::cerr << "error: unable to read source file '" << path << "'\n";
            return 1;
        }

        if (command == "dump-tokens") {
            return dumpTokens(path, std::move(contents));
        }
        if (command == "dump-ast") {
            return dumpAst(path, std::move(contents));
        }
        if (command == "dump-typed-ast") {
            return dumpTypedAst(path, std::move(contents));
        }
        if (command == "dump-bytecode")
            return dumpBytecode(path, std::move(contents));
        if (command == "dump-ir")
            return dumpIr(path, std::move(contents));
        if (command == "dump-asm")
            return dumpAsm(path, std::move(contents));
    }

    printUsageError(arguments[0], "unknown command");
    return 2;
}
