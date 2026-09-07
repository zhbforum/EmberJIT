#include "ember/bytecode/lowering.hpp"

#include <type_traits>

static_assert(std::is_aggregate_v<ember::bytecode::CompileResult>);
