#include "ember/frontend/ast_printer.hpp"
#include "ember/frontend/lexer.hpp"
#include "ember/frontend/parser.hpp"

#include "test_harness.hpp"

#include <array>
#include <string>
#include <string_view>
#include <variant>

namespace
{

using ember::frontend::AstPrinter;
using ember::frontend::BinaryExpression;
using ember::frontend::BinaryOperator;
using ember::frontend::Lexer;
using ember::frontend::LetStatement;
using ember::frontend::Parser;
using ember::frontend::ParenthesizedExpression;
using ember::frontend::ReturnStatement;
using ember::frontend::UnaryExpression;
using ember::frontend::UnaryOperator;
using ember::support::SourceId;
using ember::support::SourceSpan;
using ember::support::SourceText;

EMBER_TEST("parser builds function declarations and expression precedence")
{
    const SourceText source{SourceId{41}, "test.ember",
                            "fn main() -> i64 { let value: i64 = 1 + 2 * 3; return value; }"};
    const auto lexResult = Lexer{}.lex(source);
    const auto result = Parser{}.parse(source, lexResult.tokens);

    tests.expect(lexResult.diagnostics.empty(), "source lexes successfully");
    tests.expect(result.diagnostics.empty(), "program parses successfully");
    tests.expect(result.program != nullptr, "successful parse returns a program");
    if (result.program == nullptr)
    {
        return;
    }

    tests.expect(result.program->functions.size() == 1,
                 "program has one function declaration");
    if (result.program->functions.size() != 1)
    {
        return;
    }
    const auto &function = result.program->functions.front();
    tests.expect(function.body.statements.size() == 2,
                 "function body has declaration and return");
    if (function.body.statements.empty())
    {
        return;
    }

    const auto *declaration = std::get_if<LetStatement>(&function.body.statements.front().node);
    tests.expect(declaration != nullptr, "first statement is a declaration");
    if (declaration == nullptr)
    {
        return;
    }
    const auto *addition = std::get_if<BinaryExpression>(&declaration->initializer->node);
    tests.expect(addition != nullptr && addition->operation == BinaryOperator::add,
                 "addition is the outer expression");
    if (addition == nullptr)
    {
        return;
    }
    const auto *multiplication = std::get_if<BinaryExpression>(&addition->right->node);
    tests.expect(multiplication != nullptr && multiplication->operation == BinaryOperator::multiply,
                 "multiplication binds tighter than addition");

    const auto dump = AstPrinter{}.print(*result.program, source);
    tests.expect(dump.starts_with("Program [0, 62)\n  Function main -> i64 [0, 62)\n"),
                 "AST dump starts with stable program and function nodes");
}

EMBER_TEST("parser returns one diagnostic without partial AST")
{
    const SourceText source{SourceId{42}, "invalid.ember",
                            "fn main() -> i64 { return 1 }"};
    const auto lexResult = Lexer{}.lex(source);
    const auto result = Parser{}.parse(source, lexResult.tokens);

    tests.expect(lexResult.diagnostics.empty(), "invalid grammar still lexes");
    tests.expect(result.program == nullptr, "failed parse has no partial program");
    tests.expect(result.diagnostics.size() == 1, "failed parse has one diagnostic");
    if (result.diagnostics.size() == 1)
    {
        tests.expect(result.diagnostics.front().code == "E2002",
                     "missing semicolon has expected-token diagnostic");
    }
}

EMBER_TEST("parser preserves parenthesized and operator spans")
{
    const std::string text{"fn main() -> i64 { return -(1 + 2) * 3; }"};
    const SourceText source{SourceId{43}, "spans.ember", text};
    const auto lexResult = Lexer{}.lex(source);
    const auto result = Parser{}.parse(source, lexResult.tokens);

    tests.expect(result.diagnostics.empty(), "parenthesized expression parses");
    if (result.program == nullptr || result.program->functions.size() != 1)
    {
        return;
    }
    const auto &body = result.program->functions.front().body;
    tests.expect(body.statements.size() == 1,
                 "function body contains the return statement");
    if (body.statements.size() != 1)
    {
        return;
    }
    const auto &statement = body.statements.front();
    const auto *returned = std::get_if<ReturnStatement>(&statement.node);
    tests.expect(returned != nullptr && returned->value != nullptr,
                 "return contains an expression");
    if (returned == nullptr || returned->value == nullptr)
    {
        return;
    }
    const auto *multiply = std::get_if<BinaryExpression>(&returned->value->node);
    tests.expect(multiply != nullptr && multiply->operation == BinaryOperator::multiply,
                 "multiplication is the outer expression");
    if (multiply == nullptr)
    {
        return;
    }
    const auto multiplyOffset = text.find('*');
    tests.expect(multiply->operatorSpan ==
                     SourceSpan{.source = SourceId{43},
                                .begin = multiplyOffset,
                                .end = multiplyOffset + 1},
                 "binary expression preserves its operator span");
    const auto *unary = std::get_if<UnaryExpression>(&multiply->left->node);
    tests.expect(unary != nullptr && unary->operation == UnaryOperator::minus,
                 "unary minus is preserved");
    if (unary == nullptr)
    {
        return;
    }
    const auto *parenthesized =
        std::get_if<ParenthesizedExpression>(&unary->operand->node);
    tests.expect(parenthesized != nullptr, "parentheses have a dedicated AST node");
    if (parenthesized == nullptr)
    {
        return;
    }
    const auto openOffset = text.find('(' , text.find("return"));
    const auto closeOffset = text.find(')', openOffset);
    tests.expect(parenthesized->expression->span ==
                     SourceSpan{.source = SourceId{43},
                                .begin = openOffset + 1,
                                .end = closeOffset},
                 "inner expression excludes delimiters");
    const auto *inner =
        std::get_if<BinaryExpression>(&parenthesized->expression->node);
    tests.expect(inner != nullptr &&
                     inner->operatorSpan ==
                         SourceSpan{.source = SourceId{43},
                                    .begin = openOffset + 3,
                                    .end = openOffset + 4},
                 "inner binary expression preserves its operator span");
    tests.expect(unary->operand->span ==
                     SourceSpan{.source = SourceId{43},
                                .begin = openOffset,
                                .end = closeOffset + 1},
                 "parenthesized node includes both delimiters");
}

EMBER_TEST("parser accepts v0.1 statement forms")
{
    const SourceText source{
        SourceId{44}, "forms.ember",
        "fn helper(a: i64, b: f64, c: bool) -> void { return; }\n"
        "fn main() -> i64 { let x: i64 = helper(1, 2.0, true); x = x + 1; "
        "if x == 2 { while x < 4 { x = x + 1; } } else if x != 0 { "
        "helper(x, 2.0, false); } else { { helper(x, 2.0, true); } } "
        "return x; }"};
    const auto lexResult = Lexer{}.lex(source);
    const auto result = Parser{}.parse(source, lexResult.tokens);

    tests.expect(lexResult.diagnostics.empty(), "all statement forms lex");
    tests.expect(result.diagnostics.empty(), "all statement forms parse");
    tests.expect(result.program != nullptr && result.program->functions.size() == 2,
                 "multiple functions and parameters are retained");
}

EMBER_TEST("parser reports representative syntax errors")
{
    struct InvalidProgram
    {
        std::string_view text;
        std::string_view code;
    };
    constexpr auto cases = std::array{
        InvalidProgram{"fn main() -> i64 {", "E2002"},
        InvalidProgram{"fn main( -> i64 {}", "E2002"},
        InvalidProgram{"fn main() -> i64 { foo(1; }", "E2002"},
        InvalidProgram{"fn main() -> missing {}", "E2001"},
        InvalidProgram{"let value: i64 = 1;", "E2002"},
    };

    for (const auto &testCase : cases)
    {
        const SourceText source{SourceId{45}, "invalid.ember", std::string{testCase.text}};
        const auto lexResult = Lexer{}.lex(source);
        const auto result = Parser{}.parse(source, lexResult.tokens);
        tests.expect(lexResult.diagnostics.empty(), "syntax error input lexes");
        tests.expect(result.program == nullptr, "syntax error has no partial AST");
        tests.expect(result.diagnostics.size() == 1, "syntax error has one diagnostic");
        if (result.diagnostics.size() == 1)
        {
            tests.expect(result.diagnostics.front().code == testCase.code,
                         "syntax error uses its documented code");
        }
    }
}

EMBER_TEST("parser diagnostics preserve the unexpected token span")
{
    const SourceText source{SourceId{46}, "invalid.ember",
                            "fn main() -> missing {}"};
    const auto lexResult = Lexer{}.lex(source);
    const auto result = Parser{}.parse(source, lexResult.tokens);

    tests.expect(result.diagnostics.size() == 1, "invalid type has one diagnostic");
    if (result.diagnostics.size() != 1)
    {
        return;
    }
    const auto &diagnostic = result.diagnostics.front();
    tests.expect(diagnostic.code == "E2001", "invalid type is an unexpected token");
    tests.expect(diagnostic.primarySpan ==
                     SourceSpan{.source = SourceId{46}, .begin = 13, .end = 20},
                 "diagnostic highlights only the unexpected type token");
}

} // namespace
