#pragma once

#include "ember/bytecode/bytecode.hpp"
#include "ember/semantic/analyzer.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace ember::bytecode {
enum class BuiltinKind : std::uint32_t {
    printI64 = 0,
    printF64 = 1,
    clockMs = 2,
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
    std::optional<Value> value;
};

struct BuiltinDescriptor {
    BuiltinKind kind;
    std::string_view name;
    semantic::FunctionSignature signature;
    NativeBuiltinAbi nativeAbi;

    [[nodiscard]] constexpr semantic::FunctionId id() const noexcept {
        return static_cast<semantic::FunctionId>(kind);
    }
};

[[nodiscard]] inline const auto& builtins() {
    static const std::array descriptors{
        BuiltinDescriptor{BuiltinKind::printI64,
                          "print_i64",
                          {{semantic::Type::i64}, semantic::Type::voidType},
                          NativeBuiltinAbi::i64ToVoid},
        BuiltinDescriptor{BuiltinKind::printF64,
                          "print_f64",
                          {{semantic::Type::f64}, semantic::Type::voidType},
                          NativeBuiltinAbi::f64ToVoid},
        BuiltinDescriptor{BuiltinKind::clockMs,
                          "clock_ms",
                          {{}, semantic::Type::i64},
                          NativeBuiltinAbi::voidToI64},
    };
    return descriptors;
}

[[nodiscard]] inline const BuiltinDescriptor* findBuiltin(semantic::FunctionId id) {
    for (const auto& builtin : builtins())
        if (builtin.id() == id)
            return &builtin;
    return nullptr;
}

[[nodiscard]] inline bool registerBuiltins(semantic::HostFunctionRegistry& registry) {
    if (!registry.functions().empty())
        return false;
    for (std::size_t index = 0; index < builtins().size(); ++index) {
        const auto& builtin = builtins()[index];
        if (builtin.id() != index)
            return false;
        if (!registry.add({.name = std::string{builtin.name}, .signature = builtin.signature}))
            return false;
    }
    return true;
}

// The shared VM/bridge dispatch path validates the descriptor signature and
// keeps host effects in one place. A void builtin has `value == nullopt`.
[[nodiscard]] BuiltinInvocation invokeBuiltin(const BuiltinDescriptor& builtin,
                                              std::span<const Value> arguments) noexcept;
[[nodiscard]] std::uintptr_t nativeBuiltinEntry(const BuiltinDescriptor& builtin) noexcept;
} // namespace ember::bytecode
