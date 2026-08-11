#pragma once

#include "ember/runtime/runtime_function.hpp"

namespace ember::runtime::test
{
class RuntimeFunctionAccess
{
  public:
    [[nodiscard]] static bool hasNativeEntry(const RuntimeFunction &function) noexcept
    {
        return function.nativeCode_ != nullptr;
    }

    [[nodiscard]] static NativeCompilationStage
    nativeCompilationStage(const RuntimeFunction &function) noexcept
    {
        return function.nativeCompilationStageForTesting_;
    }
};
} // namespace ember::runtime::test
