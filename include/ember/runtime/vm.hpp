#pragma once

#include "ember/bytecode/bytecode.hpp"

#include <optional>
#include <string>
#include <vector>

namespace ember::runtime
{
struct RuntimeError
{
    std::string code;
    std::string message;
};
struct ExecutionResult
{
    std::optional<bytecode::Value> value;
    std::optional<RuntimeError> error;
};

// The constructor is deliberately private: execution is possible only after
// verification.
// The VM trusts verified bytecode and is not a security sandbox.
class VirtualMachine
{
  public:
    [[nodiscard]] static VirtualMachine create(bytecode::VerifiedProgram verifiedProgram);
    [[nodiscard]] ExecutionResult execute(semantic::FunctionId entry,
                                          const std::vector<bytecode::Value> &arguments = {}) const;

  private:
    explicit VirtualMachine(bytecode::Program program) : program_(std::move(program)) {}
    bytecode::Program program_;
};
} // namespace ember::runtime
