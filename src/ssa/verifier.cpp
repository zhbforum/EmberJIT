#include "ember/ssa/verifier.hpp"

#include <algorithm>
#include <deque>
#include <limits>
#include <string>
#include <string_view>

namespace ember::ssa {
namespace {
struct DefinitionLocation {
    BlockId block;
    std::size_t position;
};

[[nodiscard]] support::Diagnostic error(std::string message) {
    return {.stage = support::DiagnosticStage::jit,
            .severity = support::DiagnosticSeverity::error,
            .code = "E6101",
            .message = std::move(message),
            .primarySpan = {}};
}

[[nodiscard]] bool validType(semantic::Type type) noexcept {
    return type == semantic::Type::i64 || type == semantic::Type::f64 ||
           type == semantic::Type::boolean || type == semantic::Type::voidType;
}

[[nodiscard]] bool validValueType(semantic::Type type) noexcept {
    return validType(type) && type != semantic::Type::voidType;
}

[[nodiscard]] bool isI64Comparison(Opcode opcode) noexcept {
    return opcode == Opcode::equalI64 || opcode == Opcode::notEqualI64 ||
           opcode == Opcode::lessI64 || opcode == Opcode::lessEqualI64 ||
           opcode == Opcode::greaterI64 || opcode == Opcode::greaterEqualI64;
}

[[nodiscard]] bool isI64Binary(Opcode opcode) noexcept {
    return opcode == Opcode::addI64 || opcode == Opcode::subI64 || opcode == Opcode::mulI64 ||
           opcode == Opcode::divI64 || opcode == Opcode::remI64 || isI64Comparison(opcode);
}

[[nodiscard]] bool isF64Comparison(Opcode opcode) noexcept {
    return opcode == Opcode::equalF64 || opcode == Opcode::notEqualF64 ||
           opcode == Opcode::lessF64 || opcode == Opcode::lessEqualF64 ||
           opcode == Opcode::greaterF64 || opcode == Opcode::greaterEqualF64;
}

[[nodiscard]] bool isF64Binary(Opcode opcode) noexcept {
    return opcode == Opcode::addF64 || opcode == Opcode::subF64 || opcode == Opcode::mulF64 ||
           opcode == Opcode::divF64 || isF64Comparison(opcode);
}

[[nodiscard]] bool isBoolBinary(Opcode opcode) noexcept {
    return opcode == Opcode::equalBool || opcode == Opcode::notEqualBool;
}
} // namespace

const CallTarget* CallTargetTable::find(semantic::FunctionId id) const noexcept {
    const auto found = std::find_if(targets_.begin(),
                                    targets_.end(),
                                    [id](const CallTarget& target) { return target.id == id; });
    return found == targets_.end() ? nullptr : &*found;
}

VerifyResult Verifier::verify(Function function, CallTargetTable callTargets) const {
    std::vector<support::Diagnostic> diagnostics;
    const auto report = [&diagnostics, id = function.id](std::string message) {
        diagnostics.push_back(error("function #" + std::to_string(id) + ": " + std::move(message)));
    };
    const auto validSignature = [](const semantic::FunctionSignature& signature) {
        return validType(signature.returnType) &&
               std::ranges::all_of(signature.parameterTypes,
                                   [](semantic::Type type) { return validValueType(type); });
    };

    for (std::size_t index{}; index < callTargets.targets_.size(); ++index) {
        const auto& target = callTargets.targets_[index];
        if (target.id == noFunction)
            report("trusted call target table contains noFunction");
        if (target.kind != semantic::FunctionKind::user &&
            target.kind != semantic::FunctionKind::host)
            report("trusted call target table contains an invalid function kind");
        if (!validSignature(target.signature))
            report("trusted call target table contains an invalid signature");
        if (target.id == function.id &&
            (target.kind != semantic::FunctionKind::user ||
             target.signature.returnType != function.signature.returnType ||
             target.signature.parameterTypes != function.signature.parameterTypes))
            report("self call target metadata does not match the user function signature");
        for (std::size_t previous{}; previous < index; ++previous)
            if (callTargets.targets_[previous].id == target.id)
                report("trusted call target table contains a duplicate function id");
    }

    const auto maximumBlockCount = static_cast<std::size_t>(std::numeric_limits<BlockId>::max());
    const auto maximumValueCount = static_cast<std::size_t>(std::numeric_limits<ValueId>::max());
    const auto maximumParameterCount =
        static_cast<std::size_t>(std::numeric_limits<ParameterIndex>::max());
    if (function.id == noFunction)
        report("has an invalid function id");
    if (!validSignature(function.signature) || function.blocks.empty())
        report("has an invalid signature or no entry block");
    if (function.blocks.size() > maximumBlockCount ||
        function.valueTypes.size() > maximumValueCount ||
        function.signature.parameterTypes.size() > maximumParameterCount)
        report("exceeds the representable block, value, or parameter range");
    if (!std::ranges::all_of(function.valueTypes,
                             [](semantic::Type type) { return validValueType(type); }))
        report("has an invalid SSA value type");
    if (!diagnostics.empty())
        return {.function = std::nullopt, .diagnostics = std::move(diagnostics)};

    for (std::size_t index{}; index < function.blocks.size(); ++index)
        if (function.blocks[index].id != static_cast<BlockId>(index))
            report("block ids must be dense and match block order");
    if (!function.blocks[0].parameters.empty())
        report("entry block must not declare block parameters");
    if (!diagnostics.empty())
        return {.function = std::nullopt, .diagnostics = std::move(diagnostics)};

    const auto validBlock = [&function](BlockId block) {
        return block != noBlock && static_cast<std::size_t>(block) < function.blocks.size();
    };
    const auto validValue = [&function](ValueId value) {
        return value != noValue && static_cast<std::size_t>(value) < function.valueTypes.size();
    };
    const auto hasNoCallMetadata = [](const Instruction& instruction) {
        return instruction.callee == noFunction &&
               instruction.calleeKind == semantic::FunctionKind::user &&
               instruction.arguments.empty();
    };
    const auto hasNoValueOperands = [](const Instruction& instruction) {
        return instruction.input == noValue && instruction.left == noValue &&
               instruction.right == noValue;
    };
    const auto hasNoEdge = [](const Edge& edge) {
        return edge.target == noBlock && edge.arguments.empty();
    };

    std::vector<std::optional<DefinitionLocation>> definitions(function.valueTypes.size());
    const auto define = [&](ValueId value,
                            semantic::Type type,
                            BlockId block,
                            std::size_t position,
                            std::string_view role) {
        if (!validValue(value)) {
            report(std::string{role} + " defines an invalid SSA value");
            return;
        }
        if (definitions[value]) {
            report(std::string{role} + " defines an SSA value more than once");
            return;
        }
        if (function.valueTypes[value] != type)
            report(std::string{role} + " defines an SSA value with the wrong type");
        definitions[value] = DefinitionLocation{.block = block, .position = position};
    };

    std::size_t parameterCount{};
    for (const auto& block : function.blocks) {
        for (const auto& parameter : block.parameters) {
            if (!validValueType(parameter.type))
                report("block parameter has an invalid type");
            define(parameter.value, parameter.type, block.id, 0, "block parameter");
        }

        bool parameterPrefix = block.id == 0;
        for (std::size_t index{}; index < block.instructions.size(); ++index) {
            const auto& instruction = block.instructions[index];
            const auto position = index + 1;
            switch (instruction.opcode) {
            case Opcode::parameter:
                if (!parameterPrefix ||
                    parameterCount >= function.signature.parameterTypes.size() ||
                    instruction.parameterIndex != static_cast<ParameterIndex>(parameterCount) ||
                    !hasNoValueOperands(instruction) || instruction.constant != 0 ||
                    !hasNoCallMetadata(instruction))
                    report("function parameter has a non-canonical encoding or placement");
                define(instruction.result,
                       parameterCount < function.signature.parameterTypes.size()
                           ? function.signature.parameterTypes[parameterCount]
                           : semantic::Type::i64,
                       block.id,
                       position,
                       "function parameter");
                ++parameterCount;
                break;
            case Opcode::constantI64:
                if (instruction.parameterIndex != noParameter || !hasNoValueOperands(instruction) ||
                    !hasNoCallMetadata(instruction))
                    report("i64 constant has non-canonical unused operands");
                define(instruction.result, semantic::Type::i64, block.id, position, "constant");
                parameterPrefix = false;
                break;
            case Opcode::constantF64:
                if (instruction.parameterIndex != noParameter || !hasNoValueOperands(instruction) ||
                    !hasNoCallMetadata(instruction))
                    report("f64 constant has non-canonical unused operands");
                define(instruction.result, semantic::Type::f64, block.id, position, "constant");
                parameterPrefix = false;
                break;
            case Opcode::constantBool:
                if (instruction.parameterIndex != noParameter || !hasNoValueOperands(instruction) ||
                    !hasNoCallMetadata(instruction) ||
                    (instruction.constant != 0 && instruction.constant != 1))
                    report("boolean constant has a non-canonical encoding");
                define(instruction.result, semantic::Type::boolean, block.id, position, "constant");
                parameterPrefix = false;
                break;
            case Opcode::negateI64:
                if (instruction.parameterIndex != noParameter || instruction.left != noValue ||
                    instruction.right != noValue || instruction.constant != 0 ||
                    !hasNoCallMetadata(instruction))
                    report("i64 negation has non-canonical unused operands");
                define(instruction.result, semantic::Type::i64, block.id, position, "negation");
                parameterPrefix = false;
                break;
            case Opcode::negateF64:
                if (instruction.parameterIndex != noParameter || instruction.left != noValue ||
                    instruction.right != noValue || instruction.constant != 0 ||
                    !hasNoCallMetadata(instruction))
                    report("f64 negation has non-canonical unused operands");
                define(instruction.result, semantic::Type::f64, block.id, position, "negation");
                parameterPrefix = false;
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
                if (instruction.parameterIndex != noParameter || instruction.input != noValue ||
                    instruction.constant != 0 || !hasNoCallMetadata(instruction))
                    report("i64 binary instruction has non-canonical unused operands");
                define(instruction.result,
                       isI64Comparison(instruction.opcode) ? semantic::Type::boolean
                                                           : semantic::Type::i64,
                       block.id,
                       position,
                       "binary instruction");
                parameterPrefix = false;
                break;
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
                if (instruction.parameterIndex != noParameter || instruction.input != noValue ||
                    instruction.constant != 0 || !hasNoCallMetadata(instruction))
                    report("binary instruction has non-canonical unused operands");
                define(instruction.result,
                       isF64Comparison(instruction.opcode) || isBoolBinary(instruction.opcode)
                           ? semantic::Type::boolean
                           : semantic::Type::f64,
                       block.id,
                       position,
                       "binary instruction");
                parameterPrefix = false;
                break;
            case Opcode::callI64:
                if (instruction.parameterIndex != noParameter || !hasNoValueOperands(instruction) ||
                    instruction.constant != 0 || instruction.callee == noFunction ||
                    (instruction.calleeKind != semantic::FunctionKind::user &&
                     instruction.calleeKind != semantic::FunctionKind::host))
                    report("i64 call has a non-canonical encoding");
                define(instruction.result, semantic::Type::i64, block.id, position, "call");
                parameterPrefix = false;
                break;
            case Opcode::callValue:
                if (instruction.parameterIndex != noParameter || !hasNoValueOperands(instruction) ||
                    instruction.constant != 0 || instruction.callee == noFunction ||
                    (instruction.calleeKind != semantic::FunctionKind::user &&
                     instruction.calleeKind != semantic::FunctionKind::host))
                    report("value call has a non-canonical encoding");
                if (!validValue(instruction.result))
                    report("value call must define a valid non-void SSA value");
                else
                    define(instruction.result,
                           function.valueTypes[instruction.result],
                           block.id,
                           position,
                           "call");
                parameterPrefix = false;
                break;
            case Opcode::callVoid:
                if (instruction.parameterIndex != noParameter || instruction.result != noValue ||
                    !hasNoValueOperands(instruction) || instruction.constant != 0 ||
                    instruction.callee == noFunction ||
                    (instruction.calleeKind != semantic::FunctionKind::user &&
                     instruction.calleeKind != semantic::FunctionKind::host))
                    report("void call has a non-canonical encoding");
                parameterPrefix = false;
                break;
            case Opcode::invalid:
            default:
                report("has an invalid instruction opcode");
                parameterPrefix = false;
                break;
            }
        }

        const auto& terminator = block.terminator;
        switch (terminator.kind) {
        case TerminatorKind::branch:
            if (terminator.condition != noValue || terminator.value != noValue ||
                !hasNoEdge(terminator.falseEdge) || !validBlock(terminator.trueEdge.target))
                report("branch has non-canonical operands or an invalid target");
            break;
        case TerminatorKind::branchIfFalse:
            if (terminator.condition == noValue || terminator.value != noValue ||
                !validBlock(terminator.falseEdge.target) || !validBlock(terminator.trueEdge.target))
                report("conditional branch has non-canonical operands or invalid targets");
            break;
        case TerminatorKind::returnValue:
            if (function.signature.returnType == semantic::Type::voidType ||
                terminator.condition != noValue || terminator.value == noValue ||
                !hasNoEdge(terminator.trueEdge) || !hasNoEdge(terminator.falseEdge))
                report("return value is incompatible with the signature or non-canonical operands");
            break;
        case TerminatorKind::returnVoid:
            if (function.signature.returnType != semantic::Type::voidType ||
                terminator.condition != noValue || terminator.value != noValue ||
                !hasNoEdge(terminator.trueEdge) || !hasNoEdge(terminator.falseEdge))
                report("return_void is incompatible with the signature or non-canonical operands");
            break;
        case TerminatorKind::invalid:
        default:
            report("has an invalid terminator kind");
            break;
        }
    }
    if (parameterCount != function.signature.parameterTypes.size())
        report("entry block does not define every function parameter");
    if (!std::ranges::all_of(definitions, [](const std::optional<DefinitionLocation>& definition) {
            return definition.has_value();
        }))
        report("every dense SSA value must have exactly one definition");
    if (!diagnostics.empty())
        return {.function = std::nullopt, .diagnostics = std::move(diagnostics)};

    std::vector<std::vector<BlockId>> predecessors(function.blocks.size());
    for (const auto& block : function.blocks) {
        if (block.terminator.kind == TerminatorKind::branch)
            predecessors[block.terminator.trueEdge.target].push_back(block.id);
        else if (block.terminator.kind == TerminatorKind::branchIfFalse) {
            predecessors[block.terminator.falseEdge.target].push_back(block.id);
            predecessors[block.terminator.trueEdge.target].push_back(block.id);
        }
    }
    if (!predecessors[0].empty())
        report("entry block must not have predecessors");
    if (!diagnostics.empty())
        return {.function = std::nullopt, .diagnostics = std::move(diagnostics)};

    std::vector<bool> reachable(function.blocks.size(), false);
    std::deque<BlockId> pending;
    reachable[0] = true;
    pending.push_back(0);
    while (!pending.empty()) {
        const auto blockId = pending.front();
        pending.pop_front();
        const auto visit = [&pending, &reachable](BlockId successor) {
            if (!reachable[successor]) {
                reachable[successor] = true;
                pending.push_back(successor);
            }
        };
        const auto& terminator = function.blocks[blockId].terminator;
        if (terminator.kind == TerminatorKind::branch)
            visit(terminator.trueEdge.target);
        else if (terminator.kind == TerminatorKind::branchIfFalse) {
            visit(terminator.falseEdge.target);
            visit(terminator.trueEdge.target);
        }
    }
    for (const bool blockReachable : reachable)
        if (!blockReachable)
            report("contains an unreachable basic block");
    if (!diagnostics.empty())
        return {.function = std::nullopt, .diagnostics = std::move(diagnostics)};

    std::vector<std::vector<bool>> dominates(function.blocks.size(),
                                             std::vector<bool>(function.blocks.size(), true));
    std::fill(dominates[0].begin(), dominates[0].end(), false);
    dominates[0][0] = true;
    bool changed = true;
    while (changed) {
        changed = false;
        for (std::size_t block{}; block < function.blocks.size(); ++block) {
            if (block == 0)
                continue;
            std::vector<bool> next(function.blocks.size(), true);
            for (const auto predecessor : predecessors[block])
                for (std::size_t candidate{}; candidate < next.size(); ++candidate)
                    next[candidate] = next[candidate] && dominates[predecessor][candidate];
            next[block] = true;
            if (next != dominates[block]) {
                dominates[block] = std::move(next);
                changed = true;
            }
        }
    }

    const auto validateUse = [&](ValueId value,
                                 semantic::Type type,
                                 BlockId block,
                                 std::size_t position,
                                 std::string_view role) {
        if (!validValue(value) || !definitions[value]) {
            report(std::string{role} + " uses an undefined SSA value");
            return;
        }
        if (function.valueTypes[value] != type)
            report(std::string{role} + " has an SSA value with the wrong type");
        const auto definition = *definitions[value];
        if (definition.block == block) {
            if (definition.position >= position)
                report(std::string{role} + " uses an SSA value before its definition");
        } else if (!dominates[block][definition.block]) {
            report(std::string{role} + " uses an SSA value that does not dominate the use");
        }
    };

    const auto validateCall =
        [&](const Instruction& instruction, BlockId block, std::size_t position) {
            const auto* target = callTargets.find(instruction.callee);
            if (target == nullptr) {
                report("call target is absent from the trusted target table");
                return;
            }
            if (target->kind != instruction.calleeKind)
                report("call kind does not match the trusted target");
            if (instruction.arguments.size() != target->signature.parameterTypes.size())
                report("call argument count does not match the trusted target signature");
            const auto checkedCount =
                std::min(instruction.arguments.size(), target->signature.parameterTypes.size());
            for (std::size_t index{}; index < checkedCount; ++index)
                validateUse(instruction.arguments[index],
                            target->signature.parameterTypes[index],
                            block,
                            position,
                            "call argument");

            if (instruction.opcode == Opcode::callVoid) {
                if (target->signature.returnType != semantic::Type::voidType)
                    report("void call targets a non-void function");
                return;
            }

            if (target->signature.returnType == semantic::Type::voidType)
                report("value call targets a void function");
            if (!validValue(instruction.result) ||
                function.valueTypes[instruction.result] != target->signature.returnType)
                report("call result type does not match the trusted target signature");
            if (instruction.opcode == Opcode::callI64) {
                if (target->signature.returnType != semantic::Type::i64)
                    report("call_i64 targets a non-i64 function");
                if (!std::ranges::all_of(target->signature.parameterTypes, [](semantic::Type type) {
                        return type == semantic::Type::i64;
                    }))
                    report("call_i64 target has a non-i64 parameter");
            }
        };

    for (const auto& block : function.blocks) {
        for (std::size_t index{}; index < block.instructions.size(); ++index) {
            const auto& instruction = block.instructions[index];
            const auto position = index + 1;
            if (instruction.opcode == Opcode::negateI64)
                validateUse(instruction.input, semantic::Type::i64, block.id, position, "negation");
            else if (instruction.opcode == Opcode::negateF64)
                validateUse(instruction.input, semantic::Type::f64, block.id, position, "negation");
            else if (isI64Binary(instruction.opcode)) {
                validateUse(instruction.left,
                            semantic::Type::i64,
                            block.id,
                            position,
                            "binary instruction");
                validateUse(instruction.right,
                            semantic::Type::i64,
                            block.id,
                            position,
                            "binary instruction");
            } else if (isF64Binary(instruction.opcode)) {
                validateUse(instruction.left,
                            semantic::Type::f64,
                            block.id,
                            position,
                            "binary instruction");
                validateUse(instruction.right,
                            semantic::Type::f64,
                            block.id,
                            position,
                            "binary instruction");
            } else if (isBoolBinary(instruction.opcode)) {
                validateUse(instruction.left,
                            semantic::Type::boolean,
                            block.id,
                            position,
                            "binary instruction");
                validateUse(instruction.right,
                            semantic::Type::boolean,
                            block.id,
                            position,
                            "binary instruction");
            } else if (instruction.opcode == Opcode::callI64 ||
                       instruction.opcode == Opcode::callValue ||
                       instruction.opcode == Opcode::callVoid)
                validateCall(instruction, block.id, position);
        }

        const auto terminatorPosition = block.instructions.size() + 1;
        const auto validateEdge = [&](const Edge& edge) {
            const auto& parameters = function.blocks[edge.target].parameters;
            if (edge.arguments.size() != parameters.size())
                report("edge argument count does not match target block parameters");
            const auto checkedCount = std::min(edge.arguments.size(), parameters.size());
            for (std::size_t index{}; index < checkedCount; ++index)
                validateUse(edge.arguments[index],
                            parameters[index].type,
                            block.id,
                            terminatorPosition,
                            "edge argument");
        };
        switch (block.terminator.kind) {
        case TerminatorKind::branch:
            validateEdge(block.terminator.trueEdge);
            break;
        case TerminatorKind::branchIfFalse:
            validateUse(block.terminator.condition,
                        semantic::Type::boolean,
                        block.id,
                        terminatorPosition,
                        "conditional branch");
            validateEdge(block.terminator.falseEdge);
            validateEdge(block.terminator.trueEdge);
            break;
        case TerminatorKind::returnValue:
            validateUse(block.terminator.value,
                        function.signature.returnType,
                        block.id,
                        terminatorPosition,
                        "return");
            break;
        case TerminatorKind::returnVoid:
        case TerminatorKind::invalid:
        default:
            break;
        }
    }
    if (!diagnostics.empty())
        return {.function = std::nullopt, .diagnostics = std::move(diagnostics)};
    return {.function = VerifiedSsaFunction{std::move(function), std::move(callTargets)},
            .diagnostics = {}};
}

VerifyResult Verifier::verify(Function function) const {
    return verify(std::move(function), CallTargetTable{std::vector<CallTarget>{}});
}
} // namespace ember::ssa
