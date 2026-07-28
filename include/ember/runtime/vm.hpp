#pragma once

#include "ember/bytecode/bytecode.hpp"
#include "ember/jit/native_abi.hpp"
#include "ember/runtime/runtime_dispatcher.hpp"

#include <cstddef>
#include <cstdint>
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
    struct NativeCallState
    {
        VirtualMachine *machine{};
        std::vector<HotFunctionEvent> *events{};
        std::size_t dynamicFrameCount{};
        std::size_t nativeBridgeDepth{};
        bool forceNativeCallFailureForTesting{};
    };

    explicit VirtualMachine(bytecode::VerifiedProgram verifiedProgram, RuntimeOptions options);
    [[nodiscard]] ExecutionResult executeInternal(semantic::FunctionId entry,
                                                  const std::vector<bytecode::Value> &arguments,
                                                  NativeCallState &state, bool forceVm = false);
    [[nodiscard]] static std::uint64_t nativeCallBridge(jit::NativeFrame *caller,
                                                         std::uint64_t callee,
                                                         const std::uint64_t *arguments,
                                                         std::uint64_t argumentCount,
                                                         std::uint64_t *result) noexcept;

    RuntimeOptions options_;
    RuntimeFunctionTable functions_;
};
} // namespace ember::runtime
