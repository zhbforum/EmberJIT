#pragma once

#include <cstddef>
#include <optional>
#include <span>

namespace ember::runtime
{
class NativeCodeHandle;
}

namespace ember::jit
{
enum class CodeBufferError
{
    none,
    emptyCode,
    unsupportedTarget,
    allocationFailed,
    protectionFailed,
    cacheFlushFailed,
    testFailure,
};

class CodeBuffer;

struct CodeBufferCreateResult;

// Owns one published code allocation. Construction is the only write path:
// pages are allocated RW, populated, changed to RX, and then instruction-cache
// flushed before a CodeBuffer becomes observable.
class CodeBuffer
{
  public:
    CodeBuffer(const CodeBuffer &) = delete;
    auto operator=(const CodeBuffer &) -> CodeBuffer & = delete;
    CodeBuffer(CodeBuffer &&other) noexcept;
    auto operator=(CodeBuffer &&other) noexcept -> CodeBuffer &;
    ~CodeBuffer();

    [[nodiscard]] std::size_t size() const noexcept { return size_; }

  private:
    enum class TestFailpoint
    {
        none,
        afterAllocation,
        afterWrite,
        afterProtection,
    };

    enum class TestStage
    {
        none,
        executableAllocated,
        executableWritten,
        executableProtected,
    };

    struct TestHooks
    {
        TestFailpoint failpoint{TestFailpoint::none};
        TestStage *observedStage{};
    };

    CodeBuffer(void *memory, std::size_t size) noexcept : memory_(memory), size_(size) {}
    [[nodiscard]] static CodeBufferCreateResult create(std::span<const std::byte> code) noexcept;
    [[nodiscard]] static CodeBufferCreateResult
    create(std::span<const std::byte> code, TestHooks testHooks) noexcept;
    [[nodiscard]] static std::size_t liveExecutableAllocationCountForTesting() noexcept;
    void reset() noexcept;

    void *memory_{};
    std::size_t size_{};

    friend class ::ember::runtime::NativeCodeHandle;
};

struct CodeBufferCreateResult
{
    std::optional<CodeBuffer> buffer;
    CodeBufferError error{CodeBufferError::none};
};
} // namespace ember::jit
