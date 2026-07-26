#pragma once

#include "ember/jit/code_buffer.hpp"
#include "ember/jit/emitter.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>

namespace ember::runtime
{
namespace test
{
class NativeCodeHandleAccess;
}

struct NativeCodeCreateResult;

// Owns trusted, emitter-produced executable code with the sole native
// signature supported by this milestone. It is also the only place where an
// executable address is cast to a C++ function pointer.
class NativeCodeHandle
{
  public:
    NativeCodeHandle(const NativeCodeHandle &) = delete;
    auto operator=(const NativeCodeHandle &) -> NativeCodeHandle & = delete;
    NativeCodeHandle(NativeCodeHandle &&) noexcept = default;
    auto operator=(NativeCodeHandle &&) noexcept -> NativeCodeHandle & = default;
    ~NativeCodeHandle() = default;

    // `machineCode` must implement Win64 i64(i64, i64). MachineCode proves
    // only that the bytes were finalized by this emitter; ABI and control-flow
    // correctness remain the trusted lowering/emitter boundary.
    [[nodiscard]] static NativeCodeCreateResult
    publishI64Binary(jit::x64::MachineCode machineCode) noexcept;
    [[nodiscard]] bool valid() const noexcept { return code_.size() != 0; }
    // Requires valid(); debug builds assert this precondition.
    [[nodiscard]] std::int64_t invokeI64Binary(std::int64_t left, std::int64_t right) const;
    [[nodiscard]] std::size_t codeSize() const noexcept { return code_.size(); }

  private:
    explicit NativeCodeHandle(jit::CodeBuffer code) noexcept : code_(std::move(code)) {}
    [[nodiscard]] void *entryAddress() const noexcept { return code_.memory_; }

    jit::CodeBuffer code_;

    friend class test::NativeCodeHandleAccess;
};

struct NativeCodeCreateResult
{
    std::optional<NativeCodeHandle> handle;
    jit::CodeBufferError error{jit::CodeBufferError::none};
};
} // namespace ember::runtime
