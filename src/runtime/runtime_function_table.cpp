#include "ember/runtime/runtime_function_table.hpp"

#include <cassert>
#include <exception>
#include <utility>

namespace ember::runtime
{
RuntimeFunctionTable::RuntimeFunctionTable(bytecode::VerifiedProgram verifiedProgram)
{
    auto program = std::move(verifiedProgram).takeProgram();
    functions_.reserve(program.functions.size());
    for (auto &function : program.functions)
        functions_.emplace_back(std::move(function));

    index_.reserve(functions_.size());
    for (std::size_t index = 0; index < functions_.size(); ++index)
    {
        const bool inserted = index_.emplace(functions_[index].id(), index).second;
        assert(inserted);
        if (!inserted)
            std::terminate();
    }
}

const RuntimeFunction *RuntimeFunctionTable::find(semantic::FunctionId id) const noexcept
{
    const auto found = index_.find(id);
    return found == index_.end() ? nullptr : &functions_[found->second];
}

RuntimeFunction *RuntimeFunctionTable::findMutable(semantic::FunctionId id) noexcept
{
    const auto found = index_.find(id);
    return found == index_.end() ? nullptr : &functions_[found->second];
}
} // namespace ember::runtime
