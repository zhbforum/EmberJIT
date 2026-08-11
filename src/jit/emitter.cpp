#include "ember/jit/emitter.hpp"

#include <algorithm>
#include <iomanip>
#include <limits>
#include <sstream>
#include <utility>

namespace ember::jit::x64
{
class EmitterIdentity
{
};

namespace
{
constexpr auto maximumRel32CodeSize =
    static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max());

[[nodiscard]] auto registerCode(Register value) noexcept -> std::uint8_t
{
    return static_cast<std::uint8_t>(value);
}

[[nodiscard]] auto xmmCode(XmmRegister value) noexcept -> std::uint8_t
{
    return static_cast<std::uint8_t>(value);
}

[[nodiscard]] auto xmmModRm(XmmRegister reg, XmmRegister rm) noexcept -> std::uint8_t
{
    return static_cast<std::uint8_t>(0xC0U | (static_cast<std::uint32_t>(xmmCode(reg)) << 3U) |
                                     static_cast<std::uint32_t>(xmmCode(rm)));
}
} // namespace

Emitter::Emitter() : identity_(std::make_shared<EmitterIdentity>()) {}

Emitter::~Emitter() = default;

std::string_view emitErrorName(EmitError error) noexcept
{
    switch (error)
    {
    case EmitError::none:
        return "none";
    case EmitError::finalized:
        return "finalized";
    case EmitError::invalidLabel:
        return "invalid label";
    case EmitError::duplicateLabel:
        return "duplicate label";
    case EmitError::unboundLabel:
        return "unbound label";
    case EmitError::codeTooLarge:
        return "code exceeds rel32 addressable range";
    case EmitError::invalidDivisionOperand:
        return "RAX and RDX cannot be IDIV divisors";
    }
    return "unknown emitter error";
}

std::string MachineCode::listing() const
{
    std::ostringstream output;
    output << std::uppercase << std::hex << std::setfill('0');
    for (std::size_t offset = 0; offset < bytes_.size(); offset += 16)
    {
        output << std::setw(4) << offset << ':';
        const auto lineEnd = std::min(offset + 16, bytes_.size());
        for (std::size_t index = offset; index < lineEnd; ++index)
            output << ' ' << std::setw(2)
                   << static_cast<unsigned int>(std::to_integer<std::uint8_t>(bytes_[index]));
        if (lineEnd != bytes_.size())
            output << '\n';
    }
    return output.str();
}

Label Emitter::createLabel()
{
    if (error_ != EmitError::none || finalized_)
    {
        if (finalized_)
            fail(EmitError::finalized);
        return Label{std::numeric_limits<std::uint32_t>::max(), identity_};
    }
    if (labels_.size() >= std::numeric_limits<std::uint32_t>::max())
    {
        fail(EmitError::codeTooLarge);
        return Label{std::numeric_limits<std::uint32_t>::max(), identity_};
    }
    const auto id = static_cast<std::uint32_t>(labels_.size());
    labels_.push_back(std::nullopt);
    return Label{id, identity_};
}

bool Emitter::bind(const Label &label)
{
    if (!beginInstruction(0))
        return false;
    if (!valid(label))
    {
        fail(EmitError::invalidLabel);
        return false;
    }
    if (labels_[label.id_])
    {
        fail(EmitError::duplicateLabel);
        return false;
    }
    labels_[label.id_] = bytes_.size();
    return true;
}

bool Emitter::move(Register destination, Register source)
{
    if (!beginInstruction(3))
        return false;
    appendRexW(source, destination);
    appendByte(0x89);
    appendModRm(source, destination);
    return true;
}

bool Emitter::moveImmediate64(Register destination, std::int64_t value)
{
    if (!beginInstruction(10))
        return false;
    appendRexW(Register::rax, destination);
    appendByte(static_cast<std::uint8_t>(0xB8U + (registerCode(destination) & 0x7U)));
    appendUInt64(static_cast<std::uint64_t>(value));
    return true;
}

bool Emitter::add(Register destination, Register source)
{
    if (!beginInstruction(3))
        return false;
    appendRexW(source, destination);
    appendByte(0x01);
    appendModRm(source, destination);
    return true;
}

bool Emitter::subtract(Register destination, Register source)
{
    if (!beginInstruction(3))
        return false;
    appendRexW(source, destination);
    appendByte(0x29);
    appendModRm(source, destination);
    return true;
}

bool Emitter::multiply(Register destination, Register source)
{
    if (!beginInstruction(4))
        return false;
    appendRexW(destination, source);
    appendByte(0x0F);
    appendByte(0xAF);
    appendModRm(destination, source);
    return true;
}

bool Emitter::bitwiseXor(Register destination, Register source)
{
    if (!beginInstruction(3))
        return false;
    appendRexW(source, destination);
    appendByte(0x31);
    appendModRm(source, destination);
    return true;
}

bool Emitter::negate(Register value)
{
    if (!beginInstruction(3))
        return false;
    appendRexW(Register::rax, value);
    appendByte(0xF7);
    appendModRm(3, value);
    return true;
}

bool Emitter::compare(Register left, Register right)
{
    if (!beginInstruction(3))
        return false;
    appendRexW(right, left);
    appendByte(0x39);
    appendModRm(right, left);
    return true;
}

bool Emitter::test(Register left, Register right)
{
    if (!beginInstruction(3))
        return false;
    appendRexW(right, left);
    appendByte(0x85);
    appendModRm(right, left);
    return true;
}

bool Emitter::set(Condition condition, Register destination)
{
    if (!beginInstruction(4))
        return false;
    const auto code = registerCode(destination);
    if (code >= 8)
        appendByte(0x41);
    else if ((code & 0x7U) >= 4)
        appendByte(0x40);
    appendByte(0x0F);
    appendByte(static_cast<std::uint8_t>(0x90U + static_cast<std::uint8_t>(condition)));
    appendByte(static_cast<std::uint8_t>(0xC0U | (code & 0x7U)));
    return true;
}

bool Emitter::push(Register value)
{
    if (!beginInstruction(2))
        return false;
    if (registerCode(value) >= 8)
        appendByte(0x41);
    appendByte(static_cast<std::uint8_t>(0x50U + (registerCode(value) & 0x7U)));
    return true;
}

bool Emitter::pop(Register value)
{
    if (!beginInstruction(2))
        return false;
    if (registerCode(value) >= 8)
        appendByte(0x41);
    appendByte(static_cast<std::uint8_t>(0x58U + (registerCode(value) & 0x7U)));
    return true;
}

bool Emitter::subtractStackPointer(std::uint32_t bytes)
{
    if (!beginInstruction(7))
        return false;
    appendByte(0x48);
    appendByte(0x81);
    appendByte(0xEC);
    appendUInt32(bytes);
    return true;
}

bool Emitter::addStackPointer(std::uint32_t bytes)
{
    if (!beginInstruction(7))
        return false;
    appendByte(0x48);
    appendByte(0x81);
    appendByte(0xC4);
    appendUInt32(bytes);
    return true;
}

bool Emitter::load(Register destination, Register base, std::int32_t displacement)
{
    if (!beginInstruction(8))
        return false;
    appendRexW(destination, base);
    appendByte(0x8B);
    const auto reg = registerCode(destination);
    const auto rm = registerCode(base);
    appendByte(static_cast<std::uint8_t>(0x80U | ((reg & 0x7U) << 3U) | (rm & 0x7U)));
    if ((rm & 0x7U) == 4)
        appendByte(0x24);
    appendUInt32(static_cast<std::uint32_t>(displacement));
    return true;
}

bool Emitter::loadEffectiveAddress(Register destination, Register base, std::int32_t displacement)
{
    if (!beginInstruction(8))
        return false;
    appendRexW(destination, base);
    appendByte(0x8D);
    const auto reg = registerCode(destination);
    const auto rm = registerCode(base);
    appendByte(static_cast<std::uint8_t>(0x80U | ((reg & 0x7U) << 3U) | (rm & 0x7U)));
    if ((rm & 0x7U) == 4)
        appendByte(0x24);
    appendUInt32(static_cast<std::uint32_t>(displacement));
    return true;
}

bool Emitter::store(Register base, std::int32_t displacement, Register source)
{
    if (!beginInstruction(8))
        return false;
    appendRexW(source, base);
    appendByte(0x89);
    const auto reg = registerCode(source);
    const auto rm = registerCode(base);
    appendByte(static_cast<std::uint8_t>(0x80U | ((reg & 0x7U) << 3U) | (rm & 0x7U)));
    if ((rm & 0x7U) == 4)
        appendByte(0x24);
    appendUInt32(static_cast<std::uint32_t>(displacement));
    return true;
}

bool Emitter::moveToXmm(XmmRegister destination, Register source)
{
    if (!beginInstruction(5))
        return false;
    const auto destinationCode = xmmCode(destination);
    if (destinationCode >= 8)
        return false;
    appendByte(0x66);
    appendRexW(Register::rax, source);
    appendByte(0x0F);
    appendByte(0x6E);
    appendByte(static_cast<std::uint8_t>(
        0xC0U | (static_cast<std::uint32_t>(destinationCode) << 3U) |
        static_cast<std::uint32_t>(registerCode(source) & 0x7U)));
    return true;
}

bool Emitter::moveFromXmm(Register destination, XmmRegister source)
{
    if (!beginInstruction(5))
        return false;
    const auto sourceCode = xmmCode(source);
    if (sourceCode >= 8)
        return false;
    appendByte(0x66);
    appendRexW(Register::rax, destination);
    appendByte(0x0F);
    appendByte(0x7E);
    appendByte(static_cast<std::uint8_t>(
        0xC0U | (static_cast<std::uint32_t>(sourceCode) << 3U) |
        static_cast<std::uint32_t>(registerCode(destination) & 0x7U)));
    return true;
}

bool Emitter::addDouble(XmmRegister destination, XmmRegister source)
{
    if (!beginInstruction(4) || xmmCode(destination) >= 8 || xmmCode(source) >= 8)
        return false;
    appendByte(0xF2);
    appendByte(0x0F);
    appendByte(0x58);
    appendByte(xmmModRm(destination, source));
    return true;
}

bool Emitter::subtractDouble(XmmRegister destination, XmmRegister source)
{
    if (!beginInstruction(4) || xmmCode(destination) >= 8 || xmmCode(source) >= 8)
        return false;
    appendByte(0xF2);
    appendByte(0x0F);
    appendByte(0x5C);
    appendByte(xmmModRm(destination, source));
    return true;
}

bool Emitter::multiplyDouble(XmmRegister destination, XmmRegister source)
{
    if (!beginInstruction(4) || xmmCode(destination) >= 8 || xmmCode(source) >= 8)
        return false;
    appendByte(0xF2);
    appendByte(0x0F);
    appendByte(0x59);
    appendByte(xmmModRm(destination, source));
    return true;
}

bool Emitter::divideDouble(XmmRegister destination, XmmRegister source)
{
    if (!beginInstruction(4) || xmmCode(destination) >= 8 || xmmCode(source) >= 8)
        return false;
    appendByte(0xF2);
    appendByte(0x0F);
    appendByte(0x5E);
    appendByte(xmmModRm(destination, source));
    return true;
}

bool Emitter::compareDouble(XmmRegister left, XmmRegister right)
{
    if (!beginInstruction(4) || xmmCode(left) >= 8 || xmmCode(right) >= 8)
        return false;
    appendByte(0x66);
    appendByte(0x0F);
    appendByte(0x2E);
    appendByte(xmmModRm(left, right));
    return true;
}

bool Emitter::call(Register target)
{
    if (!beginInstruction(3))
        return false;
    appendRexW(Register::rax, target);
    appendByte(0xFF);
    appendModRm(2, target);
    return true;
}

bool Emitter::signExtendRaxIntoRdx()
{
    if (!beginInstruction(2))
        return false;
    appendByte(0x48);
    appendByte(0x99);
    return true;
}

bool Emitter::signedDivide(Register divisor)
{
    if (!beginInstruction(3))
        return false;
    if (divisor == Register::rax || divisor == Register::rdx)
    {
        fail(EmitError::invalidDivisionOperand);
        return false;
    }
    appendRexW(Register::rax, divisor);
    appendByte(0xF7);
    appendModRm(7, divisor);
    return true;
}

bool Emitter::jump(const Label &target)
{
    if (!beginInstruction(5))
        return false;
    if (!valid(target))
    {
        fail(EmitError::invalidLabel);
        return false;
    }
    appendByte(0xE9);
    const auto displacementOffset = bytes_.size();
    appendUInt32(0);
    fixups_.push_back({.label = target.id_, .displacementOffset = displacementOffset});
    return true;
}

bool Emitter::jump(Condition condition, const Label &target)
{
    if (!beginInstruction(6))
        return false;
    if (!valid(target))
    {
        fail(EmitError::invalidLabel);
        return false;
    }
    appendByte(0x0F);
    appendByte(static_cast<std::uint8_t>(0x80U + static_cast<std::uint8_t>(condition)));
    const auto displacementOffset = bytes_.size();
    appendUInt32(0);
    fixups_.push_back({.label = target.id_, .displacementOffset = displacementOffset});
    return true;
}

bool Emitter::returnFromFunction()
{
    if (!beginInstruction(1))
        return false;
    appendByte(0xC3);
    return true;
}

EmissionResult Emitter::finalize()
{
    if (finalized_)
        return {.code = std::nullopt, .error = EmitError::finalized};
    finalized_ = true;
    if (error_ != EmitError::none)
        return {.code = std::nullopt, .error = error_};
    for (const auto &label : labels_)
        if (!label)
        {
            error_ = EmitError::unboundLabel;
            return {.code = std::nullopt, .error = error_};
        }
    for (const auto &fixup : fixups_)
    {
        const auto target = *labels_[fixup.label];
        const auto nextInstruction = fixup.displacementOffset + sizeof(std::uint32_t);
        // beginInstruction() bounds both offsets to maximumCodeSize(), so
        // their signed conversion and subtraction are safe before rel32
        // range validation.
        const auto target64 = static_cast<std::int64_t>(target);
        const auto nextInstruction64 = static_cast<std::int64_t>(nextInstruction);
        const auto displacement = checkedRel32(target64 - nextInstruction64);
        if (!displacement)
        {
            error_ = EmitError::codeTooLarge;
            return {.code = std::nullopt, .error = error_};
        }
        const auto encoded = static_cast<std::uint32_t>(*displacement);
        for (std::size_t byteIndex{}; byteIndex < sizeof(encoded); ++byteIndex)
        {
            const auto shift = static_cast<unsigned int>(byteIndex * 8U);
            bytes_[fixup.displacementOffset + byteIndex] =
                static_cast<std::byte>((encoded >> shift) & 0xFFU);
        }
    }
    return {.code = MachineCode{std::move(bytes_)}, .error = EmitError::none};
}

std::optional<std::int32_t> Emitter::checkedRel32(std::int64_t delta) noexcept
{
    if (delta < std::numeric_limits<std::int32_t>::min() ||
        delta > std::numeric_limits<std::int32_t>::max())
        return std::nullopt;
    return static_cast<std::int32_t>(delta);
}

std::size_t Emitter::maximumCodeSize() noexcept
{
    return maximumRel32CodeSize;
}

bool Emitter::canAppend(std::size_t currentSize, std::size_t instructionSize) noexcept
{
    return instructionSize <= maximumRel32CodeSize &&
           currentSize <= maximumRel32CodeSize - instructionSize;
}

bool Emitter::beginInstruction(std::size_t size)
{
    if (error_ != EmitError::none)
        return false;
    if (finalized_)
    {
        fail(EmitError::finalized);
        return false;
    }
    if (!canAppend(bytes_.size(), size))
    {
        fail(EmitError::codeTooLarge);
        return false;
    }
    return true;
}

bool Emitter::valid(const Label &label) const noexcept
{
    const auto owner = label.owner_.lock();
    return owner && owner == identity_ && label.id_ < labels_.size();
}

void Emitter::fail(EmitError error) noexcept
{
    if (error_ == EmitError::none)
        error_ = error;
}

void Emitter::appendByte(std::uint8_t value)
{
    bytes_.push_back(static_cast<std::byte>(value));
}

void Emitter::appendUInt32(std::uint32_t value)
{
    for (std::size_t byteIndex{}; byteIndex < sizeof(value); ++byteIndex)
    {
        const auto shift = static_cast<unsigned int>(byteIndex * 8U);
        appendByte(static_cast<std::uint8_t>((value >> shift) & 0xFFU));
    }
}

void Emitter::appendUInt64(std::uint64_t value)
{
    for (std::size_t byteIndex{}; byteIndex < sizeof(value); ++byteIndex)
    {
        const auto shift = static_cast<unsigned int>(byteIndex * 8U);
        appendByte(static_cast<std::uint8_t>((value >> shift) & 0xFFU));
    }
}

void Emitter::appendRexW(Register reg, Register rm)
{
    const auto regCode = static_cast<std::uint32_t>(registerCode(reg));
    const auto rmCode = static_cast<std::uint32_t>(registerCode(rm));
    const auto rex = static_cast<std::uint8_t>(0x48U | ((regCode >> 3U) << 2U) | (rmCode >> 3U));
    appendByte(rex);
}

void Emitter::appendModRm(Register reg, Register rm)
{
    appendModRm(registerCode(reg), rm);
}

void Emitter::appendModRm(std::uint8_t regField, Register rm)
{
    const auto regCode = static_cast<std::uint32_t>(regField);
    const auto rmCode = static_cast<std::uint32_t>(registerCode(rm));
    const auto modRm = static_cast<std::uint8_t>(0xC0U | ((regCode & 0x7U) << 3U) | (rmCode & 0x7U));
    appendByte(modRm);
}
} // namespace ember::jit::x64
