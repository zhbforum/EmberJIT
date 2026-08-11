#pragma once

#include "ember/frontend/ast.hpp"
#include "ember/support/diagnostic.hpp"
#include "ember/support/source.hpp"

#include <memory>
#include <span>
#include <vector>

namespace ember::frontend {

struct ParseResult {
    std::unique_ptr<Program> program;
    std::vector<support::Diagnostic> diagnostics;
};

class Parser {
public:
    [[nodiscard]] ParseResult parse(const support::SourceText& source,
                                    std::span<const Token> tokens) const;
};

} // namespace ember::frontend
