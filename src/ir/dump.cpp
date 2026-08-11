#include "ember/ir/dump.hpp"

#include <bit>
#include <sstream>
#include <string_view>

namespace ember::ir {
namespace {
[[nodiscard]] std::string_view opcodeName(Opcode opcode) {
    switch (opcode) {
    case Opcode::parameter:
        return "param";
    case Opcode::constantI64:
        return "const.i64";
    case Opcode::constantF64:
        return "const.f64";
    case Opcode::constantBool:
        return "const.bool";
    case Opcode::loadLocal:
        return "load";
    case Opcode::storeLocal:
        return "store";
    case Opcode::negateI64:
        return "neg.i64";
    case Opcode::negateF64:
        return "neg.f64";
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
    case Opcode::addF64:
        return "add.f64";
    case Opcode::subF64:
        return "sub.f64";
    case Opcode::mulF64:
        return "mul.f64";
    case Opcode::divF64:
        return "div.f64";
    case Opcode::equalF64:
        return "eq.f64";
    case Opcode::notEqualF64:
        return "ne.f64";
    case Opcode::lessF64:
        return "lt.f64";
    case Opcode::lessEqualF64:
        return "le.f64";
    case Opcode::greaterF64:
        return "gt.f64";
    case Opcode::greaterEqualF64:
        return "ge.f64";
    case Opcode::equalBool:
        return "eq.bool";
    case Opcode::notEqualBool:
        return "ne.bool";
    case Opcode::callI64:
        return "call.i64";
    case Opcode::callValue:
        return "call";
    case Opcode::callVoid:
        return "call.void";
    }
    return "<invalid>";
}
} // namespace

std::string dump(const VerifiedFunction& verified) {
    const auto& function = verified.function();
    std::ostringstream output;
    output << "fn #" << function.id << " (";
    for (std::size_t index = 0; index < function.signature.parameterTypes.size(); ++index) {
        if (index != 0)
            output << ", ";
        output << semantic::typeName(function.signature.parameterTypes[index]);
    }
    output << ") -> " << semantic::typeName(function.signature.returnType) << '\n';
    for (std::size_t local = 0; local < function.localTypes.size(); ++local)
        output << "  local %" << local << ": " << semantic::typeName(function.localTypes[local])
               << '\n';
    for (const auto& block : function.blocks) {
        output << "block b" << block.id << ":\n";
        for (const auto& instruction : block.instructions) {
            output << "  ";
            if (instruction.result != noValue)
                output << "v" << instruction.result << ':'
                       << semantic::typeName(function.valueTypes[instruction.result]) << " = ";
            output << opcodeName(instruction.opcode);
            switch (instruction.opcode) {
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
            case Opcode::constantF64:
                output << ' ' << std::bit_cast<double>(instruction.constant);
                break;
            case Opcode::constantBool:
                output << (instruction.constant == 0 ? " false" : " true");
                break;
            case Opcode::negateI64:
            case Opcode::negateF64:
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
            case Opcode::addF64:
            case Opcode::subF64:
            case Opcode::mulF64:
            case Opcode::divF64:
            case Opcode::equalF64:
            case Opcode::notEqualF64:
            case Opcode::lessF64:
            case Opcode::lessEqualF64:
            case Opcode::greaterF64:
            case Opcode::greaterEqualF64:
            case Opcode::equalBool:
            case Opcode::notEqualBool:
                output << " v" << instruction.left << ", v" << instruction.right;
                break;
            case Opcode::callI64:
            case Opcode::callValue:
            case Opcode::callVoid:
                output << " #" << instruction.callee << "(";
                for (std::size_t index{}; index < instruction.arguments.size(); ++index) {
                    if (index != 0)
                        output << ", ";
                    output << "v" << instruction.arguments[index];
                }
                output << ')';
                break;
            }
            output << '\n';
        }
        switch (block.terminator.kind) {
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
