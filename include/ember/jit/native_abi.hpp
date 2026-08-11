#pragma once

#include <cstddef>
#include <cstdint>

namespace ember::jit {
struct NativeFrame;
// `arguments` and every NativeFrame value array contain raw v0.1 value words.
// `result` is null exactly for a verified void callee; all other callees must
// receive writable storage for one result word.
using NativeCallBridge = std::uint64_t (*)(NativeFrame* caller,
                                           std::uint64_t callee,
                                           const std::uint64_t* arguments,
                                           std::uint64_t argumentCount,
                                           std::uint64_t* result);

// Deliberately POD: native code addresses these fields by the checked offsets
// below. The caller owns raw-word storage referenced by `locals` for one call.
struct NativeFrame {
    std::uint64_t* locals{};
    std::size_t localCount{};
    std::uint64_t* spills{};
    std::size_t spillCount{};
    std::uint64_t* callArguments{};
    std::size_t callArgumentCapacity{};
    std::uint64_t errorCode{};
    void* callContext{};
    NativeCallBridge callBridge{};
};

enum class NativeFrameError : std::uint64_t {
    none = 0,
    invalidI64Division = 1,
    invalidCall = 2,
    frameLimitExceeded = 3,
};

inline constexpr auto nativeFrameLocalsOffset =
    static_cast<std::int32_t>(offsetof(NativeFrame, locals));
inline constexpr auto nativeFrameErrorCodeOffset =
    static_cast<std::int32_t>(offsetof(NativeFrame, errorCode));
inline constexpr auto nativeFrameSpillsOffset =
    static_cast<std::int32_t>(offsetof(NativeFrame, spills));
inline constexpr auto nativeFrameCallArgumentsOffset =
    static_cast<std::int32_t>(offsetof(NativeFrame, callArguments));
inline constexpr auto nativeFrameCallContextOffset =
    static_cast<std::int32_t>(offsetof(NativeFrame, callContext));
inline constexpr auto nativeFrameCallBridgeOffset =
    static_cast<std::int32_t>(offsetof(NativeFrame, callBridge));
static_assert(nativeFrameLocalsOffset == 0);
static_assert(nativeFrameSpillsOffset == 16);
static_assert(nativeFrameCallArgumentsOffset == 32);
static_assert(nativeFrameErrorCodeOffset == 48);
static_assert(nativeFrameCallContextOffset == 56);
static_assert(nativeFrameCallBridgeOffset == 64);
} // namespace ember::jit
