#pragma once

#include "ember/core/type.hpp"

#include <cstdint>
#include <limits>
#include <vector>

namespace ember::core {

using FunctionId = std::uint32_t;
inline constexpr FunctionId noFunction = std::numeric_limits<FunctionId>::max();

enum class FunctionKind { user, host };

struct FunctionSignature {
    std::vector<Type> parameterTypes;
    Type returnType;
};

// Describes one callable Ember function independently of the representation
// that carries the trusted metadata.
struct CallTarget {
    FunctionId id;
    FunctionKind kind;
    FunctionSignature signature;
};

} // namespace ember::core
