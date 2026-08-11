#include "ember/frontend/lexer.hpp"
#include "ember/frontend/parser.hpp"
#include "ember/semantic/analyzer.hpp"
#include "ember/semantic/typed_ast_printer.hpp"

#include "test_harness.hpp"

#include <array>
#include <limits>
#include <string>
#include <string_view>
#include <variant>

namespace {
using ember::frontend::Lexer;
using ember::frontend::Parser;
using ember::semantic::HostFunction;
using ember::semantic::HostFunctionRegistry;
using ember::semantic::SemanticAnalyzer;
using ember::semantic::Type;
using ember::support::SourceId;
using ember::support::SourceText;

[[nodiscard]] auto analyze(std::string_view code) {
    const SourceText source{SourceId{80}, "semantic.ember", std::string{code}};
    const auto lexed = Lexer{}.lex(source);
    const auto parsed = Parser{}.parse(source, lexed.tokens);
    return std::pair{source, SemanticAnalyzer{}.analyze(*parsed.program, source)};
}

EMBER_TEST("semantic analyzer builds typed AST for scopes and recursive calls") {
    const auto [source, result] = analyze(
        "fn fact(n: i64) -> i64 { if n == 0 { return 1; } else { let next: i64 = n - 1; return n * "
        "fact(next); } }\n"
        "fn main() -> i64 { let value: i64 = fact(3); { let value: i64 = 2; } return value; }");
    tests.expect(result.diagnostics.empty(), "well typed recursive program succeeds");
    tests.expect(result.program != nullptr && result.program->functions.size() == 2,
                 "success returns typed functions");
    if (result.program != nullptr) {
        const auto dump = ember::semantic::TypedAstPrinter{}.print(*result.program, source);
        tests.expect(dump.starts_with("TypedProgram"), "typed dump has stable root");
        tests.expect(dump.find("Call #0: i64") != std::string::npos,
                     "typed dump annotates resolved call and type");
    }
}

EMBER_TEST("semantic analyzer rejects representative invalid programs") {
    struct Case {
        std::string_view text;
        std::string_view code;
    };
    constexpr std::array cases{
        Case{"fn main() -> i64 { return unknown; }", "E3004"},
        Case{"fn main() -> i64 { let a: i64 = 1; let a: i64 = 2; return a; }", "E3003"},
        Case{"fn main() -> i64 { let a: i64 = 1; a = true; return a; }", "E3005"},
        Case{"fn main() -> void { return 1; }", "E3006"},
        Case{"fn main() -> i64 { if 1 { return 1; } else { return 2; } }", "E3007"},
        Case{"fn main() -> i64 { return 1 + true; }", "E3008"},
        Case{"fn main() -> i64 { return missing(); }", "E3010"},
        Case{"fn main() -> i64 { let a: void = 1; return 1; }", "E3002"},
        Case{"fn main() -> i64 { let a: i64 = 1; }", "E3012"},
        Case{"fn a() -> i64 { return 1; } fn a() -> i64 { return 2; }", "E3001"},
        Case{"fn main(a: i64, a: i64) -> i64 { return a; }", "E3003"},
        Case{"fn main() -> i64 { return 9223372036854775808; }", "E3011"},
        Case{"fn main() -> i64 { return -9223372036854775809; }", "E3011"},
    };
    for (const auto& testCase : cases) {
        const auto [source, result] = analyze(testCase.text);
        (void)source;
        tests.expect(result.program == nullptr, "semantic failure returns no typed AST");
        tests.expect(result.diagnostics.size() == 1 &&
                         result.diagnostics.front().code == testCase.code,
                     "semantic failure has documented code");
    }
}

EMBER_TEST("semantic analyzer preserves resolved symbols and function kinds") {
    const SourceText source{
        SourceId{82},
        "resolved.ember",
        "fn main(a: i64) -> i64 { let value: i64 = a; value = a; return value; }"};
    const auto lexed = Lexer{}.lex(source);
    const auto parsed = Parser{}.parse(source, lexed.tokens);
    const auto result = SemanticAnalyzer{}.analyze(*parsed.program, source);
    tests.expect(result.diagnostics.empty() && result.program != nullptr,
                 "resolved-symbol program succeeds");
    if (result.program == nullptr)
        return;
    tests.expect(result.program->functions.size() == 1 &&
                     result.program->functions.front().kind == ember::semantic::FunctionKind::user,
                 "typed program records user function symbol");
    const auto& body = result.program->declarations.front().body;
    const auto* declaration =
        std::get_if<ember::semantic::TypedLetStatement>(&body.statements[0].node);
    const auto* assignment =
        std::get_if<ember::semantic::TypedAssignmentStatement>(&body.statements[1].node);
    tests.expect(declaration != nullptr && assignment != nullptr &&
                     declaration->symbol == assignment->target,
                 "assignment retains resolved local symbol");
}

EMBER_TEST("semantic analyzer accepts i64 boundaries and return before unreachable tail") {
    const auto [source, result] =
        analyze("fn min() -> i64 { return -9223372036854775808; } fn max() -> i64 { return "
                "9223372036854775807; } fn main() -> i64 { return 1; 2; }");
    (void)source;
    tests.expect(result.diagnostics.empty() && result.program != nullptr,
                 "i64 boundaries and unreachable tail type check");
}

EMBER_TEST("typed AST materializes INT64_MIN as an i64 literal") {
    const auto [source, result] = analyze("fn main() -> i64 { return -9223372036854775808; }");
    (void)source;
    tests.expect(result.diagnostics.empty() && result.program != nullptr,
                 "INT64_MIN program succeeds");
    if (result.program == nullptr)
        return;
    const auto* returned = std::get_if<ember::semantic::TypedReturnStatement>(
        &result.program->declarations.front().body.statements.front().node);
    const auto* literal =
        returned == nullptr
            ? nullptr
            : std::get_if<ember::semantic::TypedLiteralExpression>(&returned->value->node);
    const auto* value = literal == nullptr ? nullptr : std::get_if<std::int64_t>(&literal->value);
    tests.expect(value != nullptr && *value == std::numeric_limits<std::int64_t>::min(),
                 "typed AST stores INT64_MIN as a representable semantic value");
}

EMBER_TEST("semantic analyzer accepts registered host function signatures") {
    const SourceText source{SourceId{81},
                            "host.ember",
                            "fn main() -> void { print_i64(1); return; }"};
    const auto lexed = Lexer{}.lex(source);
    const auto parsed = Parser{}.parse(source, lexed.tokens);
    HostFunctionRegistry hosts;
    const bool registered = hosts.add(
        HostFunction{.name = "print_i64",
                     .signature = {.parameterTypes = {Type::i64}, .returnType = Type::voidType}});
    const auto result = SemanticAnalyzer{}.analyze(*parsed.program, source, hosts);
    tests.expect(registered, "host registry accepts unique function");
    tests.expect(result.diagnostics.empty() && result.program != nullptr,
                 "registered host call type checks");
    if (result.program == nullptr)
        return;
    const auto* statement = std::get_if<ember::semantic::TypedExpressionStatement>(
        &result.program->declarations.front().body.statements.front().node);
    const auto* call =
        statement == nullptr
            ? nullptr
            : std::get_if<ember::semantic::TypedCallExpression>(&statement->expression->node);
    tests.expect(call != nullptr && result.program->functions.at(call->callee).kind ==
                                        ember::semantic::FunctionKind::host,
                 "host call retains resolved host function ID");
}

EMBER_TEST("semantic analyzer handles mutual recursion and host registry failures") {
    const auto [source, result] = analyze(
        "fn even(n: i64) -> bool { if n == 0 { return true; } else { return odd(n - 1); } } fn "
        "odd(n: i64) -> bool { if n == 0 { return false; } else { return even(n - 1); } }");
    (void)source;
    tests.expect(result.diagnostics.empty() && result.program != nullptr,
                 "mutual recursion succeeds");

    HostFunctionRegistry hosts;
    const bool first = hosts.add(
        HostFunction{.name = "host",
                     .signature = {.parameterTypes = {Type::i64}, .returnType = Type::voidType}});
    const bool duplicate =
        hosts.add(HostFunction{.name = "host",
                               .signature = {.parameterTypes = {}, .returnType = Type::voidType}});
    tests.expect(first && !duplicate, "host registry rejects duplicate names");

    const SourceText collisionSource{SourceId{83},
                                     "collision.ember",
                                     "fn host() -> void { return; }"};
    const auto collisionLexed = Lexer{}.lex(collisionSource);
    const auto collisionParsed = Parser{}.parse(collisionSource, collisionLexed.tokens);
    const auto collision =
        SemanticAnalyzer{}.analyze(*collisionParsed.program, collisionSource, hosts);
    tests.expect(collision.diagnostics.size() == 1 && collision.diagnostics.front().code == "E3001",
                 "user and host names share a namespace");

    HostFunctionRegistry invalidHosts;
    const bool invalidRegistered = invalidHosts.add(HostFunction{
        .name = "invalid",
        .signature = {.parameterTypes = {Type::voidType}, .returnType = Type::voidType}});
    const SourceText invalidSource{SourceId{84},
                                   "invalid_host.ember",
                                   "fn main() -> void { return; }"};
    const auto invalidLexed = Lexer{}.lex(invalidSource);
    const auto invalidParsed = Parser{}.parse(invalidSource, invalidLexed.tokens);
    const auto invalid =
        SemanticAnalyzer{}.analyze(*invalidParsed.program, invalidSource, invalidHosts);
    tests.expect(invalidRegistered && invalid.diagnostics.size() == 1 &&
                     invalid.diagnostics.front().code == "E3002",
                 "void host parameter is rejected by analyzer");
}
} // namespace
