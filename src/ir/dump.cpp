#include "ember/ir/dump.hpp"

#include <sstream>
#include <string_view>

namespace ember::ir
{
namespace
{
[[nodiscard]] std::string_view opcodeName(Opcode opcode)
{
    switch (opcode)
    {
    case Opcode::parameter:
        return "param";
    case Opcode::constantI64:
        return "const.i64";
    case Opcode::loadLocal:
        return "load";
    case Opcode::storeLocal:
        return "store";
    case Opcode::negateI64:
        return "neg.i64";
    case Opcode::addI64:
        return "add.i64";
    case Opcode::subI64:
        return "sub.i64";
    case Opcode::mulI64:
        return "mul.i64";
    case Opcode::divI64:
        return "div.i64";
    case Opcode::remI64:
        return "rem.i64";
    case Opcode::equalI64:
        return "eq.i64";
    case Opcode::notEqualI64:
        return "ne.i64";
    case Opcode::lessI64:
        return "lt.i64";
    case Opcode::lessEqualI64:
        return "le.i64";
    case Opcode::greaterI64:
        return "gt.i64";
    case Opcode::greaterEqualI64:
        return "ge.i64";
    }
    return "<invalid>";
}
} // namespace

std::string dump(const VerifiedFunction &verified)
{
    const auto &function = verified.function();
    std::ostringstream output;
    output << "fn #" << function.id << " (";
    for (std::size_t index = 0; index < function.signature.parameterTypes.size(); ++index)
    {
        if (index != 0)
            output << ", ";
        output << semantic::typeName(function.signature.parameterTypes[index]);
    }
    output << ") -> " << semantic::typeName(function.signature.returnType) << '\n';
    for (std::size_t local = 0; local < function.localTypes.size(); ++local)
        output << "  local %" << local << ": " << semantic::typeName(function.localTypes[local]) << '\n';
    for (const auto &block : function.blocks)
    {
        output << "block b" << block.id << ":\n";
        for (const auto &instruction : block.instructions)
        {
            output << "  ";
            if (instruction.result != noValue)
                output << "v" << instruction.result << ':'
                       << semantic::typeName(function.valueTypes[instruction.result]) << " = ";
            output << opcodeName(instruction.opcode);
            switch (instruction.opcode)
            {
            case Opcode::parameter:
            case Opcode::loadLocal:
                output << " %" << instruction.local;
                break;
            case Opcode::storeLocal:
                output << " %" << instruction.local << ", v" << instruction.input;
                break;
            case Opcode::constantI64:
                output << ' ' << instruction.constant;
                break;
            case Opcode::negateI64:
                output << " v" << instruction.input;
                break;
            case Opcode::addI64:
            case Opcode::subI64:
            case Opcode::mulI64:
            case Opcode::divI64:
            case Opcode::remI64:
            case Opcode::equalI64:
            case Opcode::notEqualI64:
            case Opcode::lessI64:
            case Opcode::lessEqualI64:
            case Opcode::greaterI64:
            case Opcode::greaterEqualI64:
                output << " v" << instruction.left << ", v" << instruction.right;
                break;
            }
            output << '\n';
        }
        switch (block.terminator.kind)
        {
        case TerminatorKind::branch:
            output << "  branch b" << block.terminator.trueTarget << '\n';
            break;
        case TerminatorKind::branchIfFalse:
            output << "  branch_if_false v" << block.terminator.condition << ", b"
                   << block.terminator.falseTarget << ", b" << block.terminator.trueTarget << '\n';
            break;
        case TerminatorKind::returnValue:
            output << "  return v" << block.terminator.value << '\n';
            break;
        case TerminatorKind::returnVoid:
            output << "  return_void\n";
            break;
        case TerminatorKind::invalid:
        default:
            output << "  <invalid-terminator>\n";
            break;
        }
    }
    return output.str();
}
} // namespace ember::ir
