#pragma once

#include "ember/semantic/typed_ast.hpp"
#include "ember/support/source.hpp"

#include <string>

namespace ember::semantic
{
class TypedAstPrinter
{
  public:
    [[nodiscard]] std::string print(const TypedProgram &program,
                                    const support::SourceText &source) const;
};
} // namespace ember::semantic
