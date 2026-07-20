#pragma once

#include "ember/frontend/token.hpp"
#include "ember/support/diagnostic.hpp"
#include "ember/support/source.hpp"

#include <vector>

namespace ember::frontend
{

struct LexResult
{
    std::vector<Token> tokens;
    std::vector<support::Diagnostic> diagnostics;
};

class Lexer
{
  public:
    [[nodiscard]] LexResult lex(const support::SourceText &source) const;
};

} // namespace ember::frontend
