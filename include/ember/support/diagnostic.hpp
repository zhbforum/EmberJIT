#pragma once

#include "ember/support/source_location.hpp"

#include <string>

namespace ember::support {

enum class DiagnosticStage {
    lexer,
    parser,
    semantic,
    bytecode,
    jit,
    runtime,
};

enum class DiagnosticSeverity {
    error,
    warning,
    note,
};

struct Diagnostic {
    DiagnosticStage stage {DiagnosticStage::lexer};
    DiagnosticSeverity severity {DiagnosticSeverity::error};
    std::string code;
    std::string message;
    SourceSpan primarySpan {};
};

} // namespace ember::support
