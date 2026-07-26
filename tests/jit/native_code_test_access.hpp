#pragma once

#include "ember/runtime/native_code.hpp"

namespace ember::runtime::test
{
class NativeCodeHandleAccess
{
  public:
    [[nodiscard]] static const void *entryAddress(const NativeCodeHandle &handle) noexcept
    {
        return handle.entryAddress();
    }
};
} // namespace ember::runtime::test
