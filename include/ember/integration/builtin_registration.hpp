#pragma once

namespace ember::semantic {
class HostFunctionRegistry;
}

namespace ember::integration {

// Registers the bytecode builtin descriptors with the semantic host-function
// registry. This is an explicit integration adapter, not bytecode metadata.
[[nodiscard]] bool registerBuiltins(semantic::HostFunctionRegistry& registry);

} // namespace ember::integration
