#pragma once

#include "ember/core/function.hpp"
#include "ember/core/type.hpp"
#include "ember/core/value.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace ember::bytecode {
enum class BuiltinKind : std::uint32_t {
    printI64,
    printF64,
    clockMs,
};

enum class NativeBuiltinAbi : std::uint8_t {
    i64ToVoid,
    f64ToVoid,
    voidToI64,
};

// These fixed-signature adapters are the only native entry points exported by
// the trusted builtin registry. They are noexcept because generated code has
// no C++ unwind metadata.
void nativePrintI64(std::int64_t value) noexcept;
void nativePrintF64(double value) noexcept;
[[nodiscard]] std::int64_t nativeClockMs() noexcept;

struct BuiltinInvocation {
    bool succeeded{};
    std::optional<core::Value> value;
};

struct BuiltinDescriptor {
    core::FunctionId id;
    BuiltinKind kind;
    std::string_view name;
    core::FunctionSignature signature;
    NativeBuiltinAbi nativeAbi;
};

[[nodiscard]] inline const auto& builtins() {
    static const std::array descriptors{
        BuiltinDescriptor{core::FunctionId{0},
                          BuiltinKind::printI64,
                          "print_i64",
                          {{core::Type::i64}, core::Type::voidType},
                          NativeBuiltinAbi::i64ToVoid},
        BuiltinDescriptor{core::FunctionId{1},
                          BuiltinKind::printF64,
                          "print_f64",
                          {{core::Type::f64}, core::Type::voidType},
                          NativeBuiltinAbi::f64ToVoid},
        BuiltinDescriptor{core::FunctionId{2},
                          BuiltinKind::clockMs,
                          "clock_ms",
                          {{}, core::Type::i64},
                          NativeBuiltinAbi::voidToI64},
    };
    return descriptors;
}

[[nodiscard]] inline const BuiltinDescriptor* findBuiltin(core::FunctionId id) {
    for (const auto& builtin : builtins())
        if (builtin.id == id)
            return &builtin;
    return nullptr;
}

// The shared VM/bridge dispatch path validates the descriptor signature and
// keeps host effects in one place. A void builtin has `value == nullopt`.
[[nodiscard]] BuiltinInvocation invokeBuiltin(const BuiltinDescriptor& builtin,
                                              std::span<const core::Value> arguments) noexcept;
[[nodiscard]] std::uintptr_t nativeBuiltinEntry(const BuiltinDescriptor& builtin) noexcept;
} // namespace ember::bytecode
