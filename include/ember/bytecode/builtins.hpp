#pragma once

#include "ember/semantic/analyzer.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace ember::bytecode
{
enum class BuiltinKind : std::uint32_t
{
    printI64 = 0,
    printF64 = 1,
    clockMs = 2,
};

struct BuiltinDescriptor
{
    BuiltinKind kind;
    std::string_view name;
    semantic::FunctionSignature signature;

    [[nodiscard]] constexpr semantic::FunctionId id() const noexcept
    {
        return static_cast<semantic::FunctionId>(kind);
    }
};

[[nodiscard]] inline const auto &builtins()
{
    static const std::array descriptors{
        BuiltinDescriptor{
            BuiltinKind::printI64, "print_i64", {{semantic::Type::i64}, semantic::Type::voidType}},
        BuiltinDescriptor{
            BuiltinKind::printF64, "print_f64", {{semantic::Type::f64}, semantic::Type::voidType}},
        BuiltinDescriptor{BuiltinKind::clockMs, "clock_ms", {{}, semantic::Type::i64}},
    };
    return descriptors;
}

[[nodiscard]] inline const BuiltinDescriptor *findBuiltin(semantic::FunctionId id)
{
    for (const auto &builtin : builtins())
        if (builtin.id() == id)
            return &builtin;
    return nullptr;
}

[[nodiscard]] inline bool registerBuiltins(semantic::HostFunctionRegistry &registry)
{
    if (!registry.functions().empty())
        return false;
    for (std::size_t index = 0; index < builtins().size(); ++index)
    {
        const auto &builtin = builtins()[index];
        if (builtin.id() != index)
            return false;
        if (!registry.add({.name = std::string{builtin.name}, .signature = builtin.signature}))
            return false;
    }
    return true;
}
} // namespace ember::bytecode
