#include "ember/frontend/ast_printer.hpp"
#include "ember/frontend/lexer.hpp"
#include "ember/frontend/parser.hpp"
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

void printUsage(std::string_view executable)
{
    std::cerr << "usage: " << executable << " <dump-tokens|dump-ast|dump-typed-ast> <file>\n";
}

} // namespace

int main(int argumentCount, char *arguments[])
{
    if (argumentCount == 1)
    {
        std::cout << "EmberJIT 0.1.0\n";
        return 0;
    }

    if (argumentCount == 3)
    {
        const std::string_view command{arguments[1]};
        if (command != "dump-tokens" && command != "dump-ast" && command != "dump-typed-ast")
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
    }

    printUsage(arguments[0]);
    return 2;
}
