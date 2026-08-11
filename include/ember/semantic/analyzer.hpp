#pragma once

#include "ember/semantic/typed_ast.hpp"
#include "ember/support/diagnostic.hpp"
#include "ember/support/source.hpp"

#include <string>
#include <vector>

namespace ember::semantic {

struct HostFunction {
    std::string name;
    FunctionSignature signature;
};

class HostFunctionRegistry {
public:
    [[nodiscard]] bool add(HostFunction function);
    [[nodiscard]] const std::vector<HostFunction>& functions() const noexcept {
        return functions_;
    }

private:
    std::vector<HostFunction> functions_;
};

struct AnalysisResult {
    std::unique_ptr<TypedProgram> program;
    std::vector<support::Diagnostic> diagnostics;
};

class SemanticAnalyzer {
public:
    [[nodiscard]] AnalysisResult analyze(const frontend::Program& program,
                                         const support::SourceText& source,
                                         const HostFunctionRegistry& hosts = {}) const;
};

} // namespace ember::semantic
