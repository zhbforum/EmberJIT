#include "ember/bytecode/builtins.hpp"
#include "ember/bytecode/bytecode.hpp"
#include "ember/frontend/ast_printer.hpp"
#include "ember/frontend/lexer.hpp"
#include "ember/frontend/parser.hpp"
#include "ember/runtime/vm.hpp"
#include "ember/semantic/analyzer.hpp"
#include "ember/semantic/typed_ast_printer.hpp"

#include "ember/support/diagnostic.hpp"
#include "ember/support/source.hpp"

#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>

namespace
{

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

[[nodiscard]] int runVm(std::string_view path, std::string contents)
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
    auto vm = ember::runtime::VirtualMachine::create(std::move(*checked.program));
    const auto main = std::find_if(
        analysis.program->functions.begin(), analysis.program->functions.end(),
        [](const auto &function)
        {
            return function.name == "main" && function.kind == ember::semantic::FunctionKind::user;
        });
    if (main == analysis.program->functions.end())
    {
        std::cerr << "error: missing main function\n";
        return 1;
    }
    const auto result = vm.execute(main->id);
    if (result.error)
    {
        std::cerr << "runtime error[" << result.error->code << "]: " << result.error->message
                  << '\n';
        return 1;
    }
    return 0;
}

void printUsage(std::string_view executable)
{
    std::cerr << "usage: " << executable
              << " <dump-tokens|dump-ast|dump-typed-ast|dump-bytecode|run --no-jit> <file>\n";
}

} // namespace

int main(int argumentCount, char *arguments[])
{
    if (argumentCount == 1)
    {
        std::cout << "EmberJIT 0.1.0\n";
        return 0;
    }

    if (argumentCount == 4 && std::string_view{arguments[1]} == "run" &&
        std::string_view{arguments[2]} == "--no-jit")
    {
        std::string contents;
        const std::string_view path{arguments[3]};
        if (!readFile(path, contents))
            return 1;
        return runVm(path, std::move(contents));
    }
    if (argumentCount == 3)
    {
        const std::string_view command{arguments[1]};
        if (command != "dump-tokens" && command != "dump-ast" && command != "dump-typed-ast" &&
            command != "dump-bytecode")
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
    }

    printUsage(arguments[0]);
    return 2;
}
