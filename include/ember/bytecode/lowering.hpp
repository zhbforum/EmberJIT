#pragma once

#include "ember/bytecode/bytecode.hpp"
#include "ember/support/diagnostic.hpp"

#include <optional>
#include <vector>

namespace ember::semantic {
struct TypedProgram;
}

namespace ember::bytecode {

struct CompileResult {
    std::optional<Program> program;
    std::vector<support::Diagnostic> diagnostics;
};

// Lowers a fully resolved typed program into bytecode. This adapter belongs
// above the bytecode representation and verifier boundary.
class Compiler {
public:
    // Precondition: program was returned successfully by SemanticAnalyzer.
    [[nodiscard]] CompileResult compile(const semantic::TypedProgram& program) const;
};

} // namespace ember::bytecode
