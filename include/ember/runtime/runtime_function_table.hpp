#pragma once

#include "ember/runtime/runtime_function.hpp"

#include <cstddef>
#include <unordered_map>
#include <vector>

namespace ember::runtime
{
class RuntimeDispatcher;

// Owns the storage and the ID index as one invariant. Its construction
// boundary accepts only verifier-produced bytecode with unique function IDs.
class RuntimeFunctionTable
{
  public:
    RuntimeFunctionTable(bytecode::VerifiedProgram verifiedProgram, bool retainNativeSource);

    [[nodiscard]] const RuntimeFunction *find(semantic::FunctionId id) const noexcept;

  private:
    [[nodiscard]] RuntimeFunction *findMutable(semantic::FunctionId id) noexcept;

    std::vector<RuntimeFunction> functions_;
    std::unordered_map<semantic::FunctionId, std::size_t> index_;

    friend class RuntimeDispatcher;
};
} // namespace ember::runtime
