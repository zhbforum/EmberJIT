#pragma once

#include "ember/bytecode/bytecode.hpp"
#include "ember/runtime/runtime_dispatcher.hpp"

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

struct ExecutionReport
{
    ExecutionResult result;
    std::vector<HotFunctionEvent> hotEvents;
};

// The constructor is deliberately private: execution is possible only after
// verification.
// The VM trusts verified bytecode and is not a security sandbox.
class VirtualMachine
{
  public:
    [[nodiscard]] static VirtualMachine create(bytecode::VerifiedProgram verifiedProgram,
                                               RuntimeOptions options = {});
    [[nodiscard]] ExecutionReport execute(semantic::FunctionId entry,
                                           const std::vector<bytecode::Value> &arguments = {});
    [[nodiscard]] const RuntimeFunction *function(semantic::FunctionId id) const noexcept;

  private:
    explicit VirtualMachine(bytecode::VerifiedProgram verifiedProgram, RuntimeOptions options);

    RuntimeOptions options_;
    RuntimeFunctionTable functions_;
};
} // namespace ember::runtime
