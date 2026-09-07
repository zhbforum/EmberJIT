#include "ember/integration/builtin_registration.hpp"

#include "ember/bytecode/builtins.hpp"
#include "ember/semantic/analyzer.hpp"

#include <string>

namespace ember::integration {

bool registerBuiltins(semantic::HostFunctionRegistry& registry) {
    if (!registry.functions().empty())
        return false;

    for (const auto& builtin : bytecode::builtins())
        if (!registry.add({.id = builtin.id,
                           .name = std::string{builtin.name},
                           .signature = builtin.signature}))
            return false;

    return true;
}

} // namespace ember::integration
