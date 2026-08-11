#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ember::jit::x64 {
// The encoder exposes the GPRs needed by the baseline stack machine. Lowering
// is responsible for saving every non-volatile register it uses and for Win64
// shadow space/alignment before any generated call.
enum class Register : std::uint8_t {
    rax = 0,
    rcx = 1,
    rdx = 2,
    rbx = 3,
    rsp = 4,
    rbp = 5,
    rsi = 6,
    rdi = 7,
    r8 = 8,
    r9 = 9,
    r10 = 10,
    r11 = 11,
    r12 = 12,
    r13 = 13,
    r14 = 14,
    r15 = 15,
};

enum class XmmRegister : std::uint8_t {
    xmm0 = 0,
    xmm1 = 1,
};

enum class Condition : std::uint8_t {
    equal = 0x4,
    notEqual = 0x5,
    less = 0xC,
    lessEqual = 0xE,
    greater = 0xF,
    greaterEqual = 0xD,
    below = 0x2,
    belowEqual = 0x6,
    above = 0x7,
    aboveEqual = 0x3,
    parity = 0xA,
    notParity = 0xB,
};

enum class EmitError {
    none,
    finalized,
    invalidLabel,
    duplicateLabel,
    unboundLabel,
    codeTooLarge,
    invalidDivisionOperand,
};

[[nodiscard]] std::string_view emitErrorName(EmitError error) noexcept;

class Emitter;
class EmitterIdentity;
namespace test {
class EmitterAccess;
}

class Label {
public:
    Label() = delete;

private:
    Label(std::uint32_t id, std::weak_ptr<const EmitterIdentity> owner) noexcept
        : id_(id),
          owner_(std::move(owner)) {
    }

    std::uint32_t id_;
    std::weak_ptr<const EmitterIdentity> owner_;
    friend class Emitter;
};

class MachineCode {
public:
    MachineCode(const MachineCode&) = delete;
    auto operator=(const MachineCode&) -> MachineCode& = delete;
    MachineCode(MachineCode&&) noexcept = default;
    auto operator=(MachineCode&&) noexcept -> MachineCode& = default;

    [[nodiscard]] std::span<const std::byte> bytes() const noexcept {
        return bytes_;
    }
    [[nodiscard]] std::string listing() const;

private:
    explicit MachineCode(std::vector<std::byte> bytes)
        : bytes_(std::move(bytes)) {
    }

    std::vector<std::byte> bytes_;
    friend class Emitter;
};

struct EmissionResult {
    std::optional<MachineCode> code;
    EmitError error{EmitError::none};
};

// Encodes the small leaf-function subset required before baseline lowering.
// Every relative branch is a rel32 fixup; finalize() resolves all labels and
// returns no code on any validation failure. Allocation exceptions propagate;
// callers must discard an emitter after any exception.
class Emitter {
public:
    Emitter();
    ~Emitter();
    Emitter(const Emitter&) = delete;
    auto operator=(const Emitter&) -> Emitter& = delete;
    Emitter(Emitter&&) = delete;
    auto operator=(Emitter&&) -> Emitter& = delete;

    [[nodiscard]] Label createLabel();
    [[nodiscard]] bool bind(const Label& label);

    [[nodiscard]] bool move(Register destination, Register source);
    [[nodiscard]] bool moveImmediate64(Register destination, std::int64_t value);
    [[nodiscard]] bool add(Register destination, Register source);
    [[nodiscard]] bool subtract(Register destination, Register source);
    [[nodiscard]] bool multiply(Register destination, Register source);
    [[nodiscard]] bool bitwiseXor(Register destination, Register source);
    [[nodiscard]] bool negate(Register value);
    [[nodiscard]] bool compare(Register left, Register right);
    [[nodiscard]] bool test(Register left, Register right);
    // Stores 0 or 1 in the low byte of `destination`; the caller must clear
    // its upper bits first when a full-width boolean is required.
    [[nodiscard]] bool set(Condition condition, Register destination);
    [[nodiscard]] bool push(Register value);
    [[nodiscard]] bool pop(Register value);
    [[nodiscard]] bool subtractStackPointer(std::uint32_t bytes);
    [[nodiscard]] bool addStackPointer(std::uint32_t bytes);
    [[nodiscard]] bool load(Register destination, Register base, std::int32_t displacement);
    [[nodiscard]] bool
    loadEffectiveAddress(Register destination, Register base, std::int32_t displacement);
    [[nodiscard]] bool store(Register base, std::int32_t displacement, Register source);
    [[nodiscard]] bool moveToXmm(XmmRegister destination, Register source);
    [[nodiscard]] bool moveFromXmm(Register destination, XmmRegister source);
    [[nodiscard]] bool addDouble(XmmRegister destination, XmmRegister source);
    [[nodiscard]] bool subtractDouble(XmmRegister destination, XmmRegister source);
    [[nodiscard]] bool multiplyDouble(XmmRegister destination, XmmRegister source);
    [[nodiscard]] bool divideDouble(XmmRegister destination, XmmRegister source);
    [[nodiscard]] bool compareDouble(XmmRegister left, XmmRegister right);
    [[nodiscard]] bool call(Register target);
    // Encodes CQO: sign-extends RAX into the implicit RDX:RAX dividend.
    [[nodiscard]] bool signExtendRaxIntoRdx();
    // Encodes IDIV. It reads RDX:RAX, writes quotient/remainder to RAX/RDX,
    // and may trap for zero divisor or INT64_MIN / -1. RAX and RDX are
    // rejected as explicit divisors because they are implicit operands.
    [[nodiscard]] bool signedDivide(Register divisor);
    [[nodiscard]] bool jump(const Label& target);
    [[nodiscard]] bool jump(Condition condition, const Label& target);
    [[nodiscard]] bool returnFromFunction();

    [[nodiscard]] EmissionResult finalize();

private:
    struct Fixup {
        std::uint32_t label;
        std::size_t displacementOffset;
    };

    [[nodiscard]] static std::optional<std::int32_t> checkedRel32(std::int64_t delta) noexcept;
    [[nodiscard]] static std::size_t maximumCodeSize() noexcept;
    [[nodiscard]] static bool canAppend(std::size_t currentSize,
                                        std::size_t instructionSize) noexcept;
    [[nodiscard]] bool beginInstruction(std::size_t size);
    [[nodiscard]] bool valid(const Label& label) const noexcept;
    void fail(EmitError error) noexcept;
    void appendByte(std::uint8_t value);
    void appendUInt32(std::uint32_t value);
    void appendUInt64(std::uint64_t value);
    void appendRexW(Register reg, Register rm);
    void appendModRm(Register reg, Register rm);
    void appendModRm(std::uint8_t regField, Register rm);

    std::vector<std::byte> bytes_;
    std::vector<std::optional<std::size_t>> labels_;
    std::vector<Fixup> fixups_;
    std::shared_ptr<const EmitterIdentity> identity_;
    EmitError error_{EmitError::none};
    bool finalized_{};

    friend class test::EmitterAccess;
};
} // namespace ember::jit::x64
