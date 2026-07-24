#include "ember/bytecode/builtins.hpp"
#include "ember/bytecode/bytecode.hpp"
#include "ember/frontend/ast_printer.hpp"
#include "ember/frontend/lexer.hpp"
#include "ember/frontend/parser.hpp"
#include "ember/ir/bytecode_lowerer.hpp"
#include "ember/ir/dump.hpp"
#include "ember/runtime/vm.hpp"
#include "ember/semantic/analyzer.hpp"
#include "ember/semantic/typed_ast_printer.hpp"

#include "ember/support/diagnostic.hpp"
#include "ember/support/source.hpp"

#include <charconv>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace
{
struct RunOptions
{
    std::uint64_t hotThreshold{1000};
    bool traceJit{};
    bool jitEnabled{true};
};

[[nodiscard]] auto diagnosticSeverityName(ember::support::DiagnosticSeverity severity) noexcept
    -> std::string_view
{
    using ember::support::DiagnosticSeverity;

    switch (severity)
    {
    case DiagnosticSeverity::error:
        return "error";
    case DiagnosticSeverity::warning:
        return "warning";
    case DiagnosticSeverity::note:
        return "note";
    }

    return "diagnostic";
}

[[nodiscard]] bool readFile(std::string_view path, std::string &contents)
{
    std::ifstream input{std::string{path}, std::ios::binary};
    if (!input.is_open())
    {
        return false;
    }

    contents.assign(std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{});
    return !input.bad();
}

void printDiagnostic(std::string_view path, const ember::support::SourceText &source,
                     const ember::support::Diagnostic &diagnostic)
{
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

void printEntryPointDiagnostic(std::string_view path, const ember::support::SourceText &source,
                               ember::support::SourceSpan span, std::string message)
{
    printDiagnostic(path, source,
                    {.stage = ember::support::DiagnosticStage::runtime,
                     .severity = ember::support::DiagnosticSeverity::error,
                     .code = "E5001",
                     .message = std::move(message),
                     .primarySpan = span});
}

[[nodiscard]] int dumpTokens(std::string_view path, std::string contents)
{
    const ember::support::SourceText source{
        ember::support::SourceId{1},
        std::string{path},
        std::move(contents),
    };
    const auto result = ember::frontend::Lexer{}.lex(source);

    if (!result.diagnostics.empty())
    {
        for (const auto &diagnostic : result.diagnostics)
        {
            printDiagnostic(path, source, diagnostic);
        }
        return 1;
    }

    for (const auto &token : result.tokens)
    {
        std::cout << ember::frontend::tokenKindName(token.kind) << " [" << token.span.begin << ", "
                  << token.span.end << ")\n";
    }
    return 0;
}

[[nodiscard]] int dumpAst(std::string_view path, std::string contents)
{
    const ember::support::SourceText source{
        ember::support::SourceId{1},
        std::string{path},
        std::move(contents),
    };
    const auto lexResult = ember::frontend::Lexer{}.lex(source);
    if (!lexResult.diagnostics.empty())
    {
        for (const auto &diagnostic : lexResult.diagnostics)
        {
            printDiagnostic(path, source, diagnostic);
        }
        return 1;
    }

    const auto parseResult = ember::frontend::Parser{}.parse(source, lexResult.tokens);
    if (!parseResult.diagnostics.empty())
    {
        for (const auto &diagnostic : parseResult.diagnostics)
        {
            printDiagnostic(path, source, diagnostic);
        }
        return 1;
    }

    std::cout << ember::frontend::AstPrinter{}.print(*parseResult.program, source);
    return 0;
}

[[nodiscard]] int dumpTypedAst(std::string_view path, std::string contents)
{
    const ember::support::SourceText source{ember::support::SourceId{1}, std::string{path},
                                            std::move(contents)};
    const auto lexResult = ember::frontend::Lexer{}.lex(source);
    if (!lexResult.diagnostics.empty())
    {
        for (const auto &diagnostic : lexResult.diagnostics)
            printDiagnostic(path, source, diagnostic);
        return 1;
    }
    const auto parseResult = ember::frontend::Parser{}.parse(source, lexResult.tokens);
    if (!parseResult.diagnostics.empty())
    {
        for (const auto &diagnostic : parseResult.diagnostics)
            printDiagnostic(path, source, diagnostic);
        return 1;
    }
    const auto semanticResult =
        ember::semantic::SemanticAnalyzer{}.analyze(*parseResult.program, source);
    if (!semanticResult.diagnostics.empty())
    {
        for (const auto &diagnostic : semanticResult.diagnostics)
            printDiagnostic(path, source, diagnostic);
        return 1;
    }
    std::cout << ember::semantic::TypedAstPrinter{}.print(*semanticResult.program, source);
    return 0;
}

[[nodiscard]] auto analyzeForRuntime(std::string_view path, std::string contents,
                                     ember::semantic::AnalysisResult &analysis)
    -> std::optional<ember::support::SourceText>
{
    ember::support::SourceText source{ember::support::SourceId{1}, std::string{path},
                                      std::move(contents)};
    const auto lexed = ember::frontend::Lexer{}.lex(source);
    if (!lexed.diagnostics.empty())
    {
        for (const auto &d : lexed.diagnostics)
            printDiagnostic(path, source, d);
        return std::nullopt;
    }
    const auto parsed = ember::frontend::Parser{}.parse(source, lexed.tokens);
    if (!parsed.diagnostics.empty())
    {
        for (const auto &d : parsed.diagnostics)
            printDiagnostic(path, source, d);
        return std::nullopt;
    }
    ember::semantic::HostFunctionRegistry hosts;
    if (!ember::bytecode::registerBuiltins(hosts))
    {
        std::cerr << "error: unable to initialize runtime built-ins\n";
        return std::nullopt;
    }
    analysis = ember::semantic::SemanticAnalyzer{}.analyze(*parsed.program, source, hosts);
    if (!analysis.diagnostics.empty())
    {
        for (const auto &d : analysis.diagnostics)
            printDiagnostic(path, source, d);
        return std::nullopt;
    }
    return source;
}

[[nodiscard]] int dumpBytecode(std::string_view path, std::string contents)
{
    ember::semantic::AnalysisResult analysis;
    if (!analyzeForRuntime(path, std::move(contents), analysis))
        return 1;
    auto compiled = ember::bytecode::Compiler{}.compile(*analysis.program);
    if (!compiled.program)
    {
        for (const auto &diagnostic : compiled.diagnostics)
            std::cerr << "bytecode error: " << diagnostic.message << '\n';
        return 1;
    }
    auto checked = ember::bytecode::Verifier{}.verify(std::move(*compiled.program));
    if (!checked.program)
    {
        for (const auto &diagnostic : checked.diagnostics)
            std::cerr << "bytecode error: " << diagnostic.message << '\n';
        return 1;
    }
    std::cout << ember::bytecode::dump(*checked.program);
    return 0;
}

[[nodiscard]] int dumpIr(std::string_view path, std::string contents)
{
    ember::semantic::AnalysisResult analysis;
    if (!analyzeForRuntime(path, std::move(contents), analysis))
        return 1;
    auto compiled = ember::bytecode::Compiler{}.compile(*analysis.program);
    if (!compiled.program)
    {
        for (const auto &diagnostic : compiled.diagnostics)
            std::cerr << "bytecode error: " << diagnostic.message << '\n';
        return 1;
    }
    auto checked = ember::bytecode::Verifier{}.verify(std::move(*compiled.program));
    if (!checked.program)
    {
        for (const auto &diagnostic : checked.diagnostics)
            std::cerr << "bytecode error: " << diagnostic.message << '\n';
        return 1;
    }

    struct FunctionLowering
    {
        ember::semantic::FunctionId id;
        ember::ir::LoweringResult result;
    };
    std::vector<FunctionLowering> loweredFunctions;
    for (const auto &function : checked.program->program().functions)
    {
        if (function.kind != ember::semantic::FunctionKind::user)
            continue;
        loweredFunctions.push_back(
            {.id = function.id, .result = ember::ir::Lowerer{}.lower(*checked.program, function.id)});
    }
    for (const auto &lowered : loweredFunctions)
    {
        if (!lowered.result.function &&
            lowered.result.failure != ember::ir::LoweringFailure::unsupported)
        {
            if (lowered.result.diagnostics.empty())
            {
                std::cerr << "IR lowering error: failed without a diagnostic\n";
                return 1;
            }
            for (const auto &diagnostic : lowered.result.diagnostics)
                std::cerr << "IR lowering error[" << diagnostic.code << "]: "
                          << diagnostic.message << '\n';
            return 1;
        }
    }
    for (const auto &lowered : loweredFunctions)
    {
        if (lowered.result.function)
        {
            std::cout << ember::ir::dump(*lowered.result.function);
            continue;
        }
        const auto message = lowered.result.diagnostics.empty()
                                 ? "unsupported native i64 form"
                                 : lowered.result.diagnostics.front().message;
        std::cout << "fn #" << lowered.id << ": not lowered (" << message << ")\n";
    }
    return 0;
}

[[nodiscard]] int runVm(std::string_view path, std::string contents, const RunOptions &options)
{
    ember::semantic::AnalysisResult analysis;
    const auto source = analyzeForRuntime(path, std::move(contents), analysis);
    if (!source)
        return 1;

    const auto entry = std::find_if(
        analysis.program->declarations.begin(), analysis.program->declarations.end(),
        [](const auto &function) { return function.name == "main"; });
    if (entry == analysis.program->declarations.end())
    {
        printEntryPointDiagnostic(path, *source,
                                  {.source = source->id(),
                                   .begin = source->size(),
                                   .end = source->size()},
                                  "missing user entry function 'main'");
        return 1;
    }
    if (!entry->signature.parameterTypes.empty())
    {
        printEntryPointDiagnostic(path, *source, entry->nameSpan,
                                  "entry function 'main' must not accept parameters");
        return 1;
    }
    if (entry->signature.returnType != ember::semantic::Type::voidType &&
        entry->signature.returnType != ember::semantic::Type::i64)
    {
        printEntryPointDiagnostic(path, *source, entry->nameSpan,
                                  "entry function 'main' must return void or i64");
        return 1;
    }

    auto compiled = ember::bytecode::Compiler{}.compile(*analysis.program);
    if (!compiled.program)
    {
        for (const auto &diagnostic : compiled.diagnostics)
            std::cerr << "bytecode error: " << diagnostic.message << '\n';
        return 1;
    }
    auto checked = ember::bytecode::Verifier{}.verify(std::move(*compiled.program));
    if (!checked.program)
    {
        for (const auto &diagnostic : checked.diagnostics)
            std::cerr << "bytecode error: " << diagnostic.message << '\n';
        return 1;
    }
    ember::runtime::RuntimeOptions runtimeOptions{
        .hotThreshold = options.hotThreshold,
        .jitEnabled = options.jitEnabled,
        .profilingEnabled = true,
    };
    auto vm = ember::runtime::VirtualMachine::create(std::move(*checked.program),
                                                      std::move(runtimeOptions));
    const auto report = vm.execute(entry->id);
    if (options.traceJit)
    {
        std::unordered_map<ember::semantic::FunctionId, std::string> functionNames;
        functionNames.reserve(analysis.program->declarations.size());
        for (const auto &function : analysis.program->declarations)
            functionNames.emplace(function.id, function.name);

        for (const auto &event : report.hotEvents)
        {
            const auto name = functionNames.find(event.functionId);
            std::cerr << "[jit] function "
                      << (name == functionNames.end() ? "<unknown>" : name->second)
                      << " became hot after " << event.invocationCount << " calls\n";
        }
    }
    if (report.result.error)
    {
        std::cerr << "runtime error[" << report.result.error->code << "]: "
                  << report.result.error->message
                  << '\n';
        return 1;
    }
    return 0;
}

void printUsage(std::string_view executable)
{
    std::cerr << "usage: " << executable
              << " <dump-tokens|dump-ast|dump-typed-ast|dump-bytecode|dump-ir> <file>\n"
              << "       " << executable
              << " run [--no-jit] [--jit-threshold=<non-negative-integer>] [--trace-jit] <file>\n";
}

[[nodiscard]] bool parseHotThreshold(std::string_view text, std::uint64_t &threshold)
{
    std::uint64_t parsed{};
    const auto [position, error] = std::from_chars(text.data(), text.data() + text.size(), parsed);
    if (error != std::errc{} || position != text.data() + text.size())
        return false;
    threshold = parsed;
    return true;
}

[[nodiscard]] std::optional<RunOptions> parseRunOptions(int argumentCount, char *arguments[],
                                                         std::string_view &path)
{
    RunOptions options;
    bool thresholdSpecified{};
    bool traceSpecified{};
    bool noJitSpecified{};

    for (int index = 2; index < argumentCount; ++index)
    {
        const std::string_view argument{arguments[index]};
        if (argument == "--no-jit")
        {
            if (noJitSpecified)
                return std::nullopt;
            noJitSpecified = true;
            options.jitEnabled = false;
            continue;
        }
        if (argument == "--trace-jit")
        {
            if (traceSpecified)
                return std::nullopt;
            traceSpecified = true;
            options.traceJit = true;
            continue;
        }
        constexpr std::string_view thresholdPrefix{"--jit-threshold="};
        if (argument.starts_with(thresholdPrefix))
        {
            if (thresholdSpecified ||
                !parseHotThreshold(argument.substr(thresholdPrefix.size()), options.hotThreshold))
            {
                return std::nullopt;
            }
            thresholdSpecified = true;
            continue;
        }
        if (argument.starts_with("--") || !path.empty())
            return std::nullopt;
        path = argument;
    }

    return path.empty() ? std::nullopt : std::optional<RunOptions>{options};
}

} // namespace

int main(int argumentCount, char *arguments[])
{
    if (argumentCount == 1)
    {
        std::cout << "EmberJIT 0.1.0\n";
        return 0;
    }

    if (argumentCount >= 3 && std::string_view{arguments[1]} == "run")
    {
        std::string_view path;
        const auto options = parseRunOptions(argumentCount, arguments, path);
        if (!options)
        {
            printUsage(arguments[0]);
            return 2;
        }
        std::string contents;
        if (!readFile(path, contents))
        {
            std::cerr << "error: unable to read source file '" << path << "'\n";
            return 1;
        }
        return runVm(path, std::move(contents), *options);
    }
    if (argumentCount == 3)
    {
        const std::string_view command{arguments[1]};
        if (command != "dump-tokens" && command != "dump-ast" && command != "dump-typed-ast" &&
            command != "dump-bytecode" && command != "dump-ir")
        {
            printUsage(arguments[0]);
            return 2;
        }

        std::string contents;
        const std::string_view path{arguments[2]};
        if (!readFile(path, contents))
        {
            std::cerr << "error: unable to read source file '" << path << "'\n";
            return 1;
        }

        if (command == "dump-tokens")
        {
            return dumpTokens(path, std::move(contents));
        }
        if (command == "dump-ast")
        {
            return dumpAst(path, std::move(contents));
        }
        if (command == "dump-typed-ast")
        {
            return dumpTypedAst(path, std::move(contents));
        }
        if (command == "dump-bytecode")
            return dumpBytecode(path, std::move(contents));
        if (command == "dump-ir")
            return dumpIr(path, std::move(contents));
    }

    printUsage(arguments[0]);
    return 2;
}
