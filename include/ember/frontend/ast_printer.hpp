#pragma once

#include "ember/frontend/ast.hpp"
#include "ember/support/source.hpp"

#include <string>

namespace ember::frontend {

class AstPrinter {
public:
    [[nodiscard]] std::string print(const Program& program,
                                    const support::SourceText& source) const;
};

} // namespace ember::frontend
