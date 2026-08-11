#include "ember/jit/emitter.hpp"
#include "ember/jit/platform.hpp"
#include "jit/emitter_test_access.hpp"
#include "jit/native_code_test_access.hpp"

#include "test_harness.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <ranges>
#include <utility>
#include <vector>

#if EMBER_HAS_WIN64_JIT
#include <windows.h>
#endif

namespace
{
[[nodiscard]] auto isEqual(std::span<const std::byte> actual,
                           std::span<const std::byte> expected) -> bool
{
    return std::ranges::equal(actual, expected);
}
} // namespace

EMBER_TEST("x64 emitter checks rel32 and code-size boundaries without allocation")
{
    using ember::jit::x64::test::EmitterAccess;

    constexpr auto minimumRel32 =
        static_cast<std::int64_t>((std::numeric_limits<std::int32_t>::min)());
    constexpr auto maximumRel32 =
        static_cast<std::int64_t>((std::numeric_limits<std::int32_t>::max)());

    const auto minimum = EmitterAccess::checkedRel32(minimumRel32);
    tests.expect(minimum && *minimum == (std::numeric_limits<std::int32_t>::min)(),
                 "INT32_MIN rel32 displacement is accepted");
    const auto maximum = EmitterAccess::checkedRel32(maximumRel32);
    tests.expect(maximum && *maximum == (std::numeric_limits<std::int32_t>::max)(),
                 "INT32_MAX rel32 displacement is accepted");
    tests.expect(!EmitterAccess::checkedRel32(minimumRel32 - 1),
                 "rel32 displacement below INT32_MIN is rejected");
    tests.expect(!EmitterAccess::checkedRel32(maximumRel32 + 1),
                 "rel32 displacement above INT32_MAX is rejected");
    const auto zero = EmitterAccess::checkedRel32(0);
    tests.expect(zero && *zero == 0, "zero rel32 displacement is accepted");

    const auto maximumCodeSize = EmitterAccess::maximumCodeSize();
    tests.expect(EmitterAccess::canAppend(maximumCodeSize, 0),
                 "code-size boundary accepts a zero-byte label binding");
    tests.expect(EmitterAccess::canAppend(maximumCodeSize - 1, 1),
                 "code-size boundary accepts an instruction ending at the maximum");
    tests.expect(!EmitterAccess::canAppend(maximumCodeSize, 1),
                 "code-size boundary rejects an instruction past the maximum");
    tests.expect(!EmitterAccess::canAppend(maximumCodeSize - 1, 2),
                 "code-size boundary rejects a two-byte overflow");
    tests.expect(!EmitterAccess::canAppend(0, maximumCodeSize + 1),
                 "instruction larger than the maximum is rejected");
}

EMBER_TEST("x64 emitter encodes arithmetic with golden bytes")
{
    ember::jit::x64::Emitter emitter;
    tests.expect(emitter.move(ember::jit::x64::Register::rax, ember::jit::x64::Register::rcx) &&
                     emitter.add(ember::jit::x64::Register::rax, ember::jit::x64::Register::rdx) &&
                     emitter.returnFromFunction(),
                 "arithmetic instructions encode");

    auto emitted = emitter.finalize();
    constexpr std::array expected{std::byte{0x48}, std::byte{0x89}, std::byte{0xC8},
                                  std::byte{0x48}, std::byte{0x01}, std::byte{0xD0},
                                  std::byte{0xC3}};
    tests.expect(emitted.error == ember::jit::x64::EmitError::none && emitted.code.has_value(),
                 "arithmetic emission finalizes");
    if (emitted.code)
    {
        tests.expect(isEqual(emitted.code->bytes(), expected), "arithmetic bytes are stable");
        tests.expect(emitted.code->listing() == "0000: 48 89 C8 48 01 D0 C3",
                     "listing contains stable hexadecimal bytes");
    }
}

EMBER_TEST("x64 emitter resolves forward rel32 branch fixups")
{
    ember::jit::x64::Emitter emitter;
    const auto returnRight = emitter.createLabel();
    tests.expect(emitter.move(ember::jit::x64::Register::rax, ember::jit::x64::Register::rcx) &&
                     emitter.compare(ember::jit::x64::Register::rcx, ember::jit::x64::Register::rdx) &&
                     emitter.jump(ember::jit::x64::Condition::lessEqual, returnRight) &&
                     emitter.move(ember::jit::x64::Register::rax, ember::jit::x64::Register::rdx) &&
                     emitter.bind(returnRight) && emitter.returnFromFunction(),
                 "branch instructions and label encode");

    auto emitted = emitter.finalize();
    constexpr std::array expected{std::byte{0x48}, std::byte{0x89}, std::byte{0xC8},
                                  std::byte{0x48}, std::byte{0x39}, std::byte{0xD1},
                                  std::byte{0x0F}, std::byte{0x8E}, std::byte{0x03},
                                  std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
                                  std::byte{0x48}, std::byte{0x89}, std::byte{0xD0},
                                  std::byte{0xC3}};
    tests.expect(emitted.error == ember::jit::x64::EmitError::none && emitted.code.has_value(),
                 "branch emission finalizes");
    if (emitted.code)
        tests.expect(isEqual(emitted.code->bytes(), expected), "forward rel32 displacement is stable");
}

EMBER_TEST("x64 emitter encodes backward rel32 branch fixups")
{
    ember::jit::x64::Emitter emitter;
    const auto loop = emitter.createLabel();
    tests.expect(emitter.bind(loop) &&
                     emitter.test(ember::jit::x64::Register::rcx, ember::jit::x64::Register::rcx) &&
                     emitter.jump(ember::jit::x64::Condition::notEqual, loop) &&
                     emitter.returnFromFunction(),
                 "backward branch instructions and label encode");

    auto emitted = emitter.finalize();
    constexpr std::array expected{std::byte{0x48}, std::byte{0x85}, std::byte{0xC9},
                                  std::byte{0x0F}, std::byte{0x85}, std::byte{0xF7},
                                  std::byte{0xFF}, std::byte{0xFF}, std::byte{0xFF},
                                  std::byte{0xC3}};
    tests.expect(emitted.error == ember::jit::x64::EmitError::none && emitted.code.has_value(),
                 "backward branch emission finalizes");
    if (emitted.code)
        tests.expect(isEqual(emitted.code->bytes(), expected),
                     "backward rel32 displacement uses two's-complement encoding");
}

EMBER_TEST("x64 emitter resolves unconditional rel32 branch fixups")
{
    ember::jit::x64::Emitter emitter;
    const auto target = emitter.createLabel();
    tests.expect(emitter.jump(target) && emitter.bind(target) && emitter.returnFromFunction(),
                 "unconditional branch and target encode");

    auto emitted = emitter.finalize();
    constexpr std::array expected{std::byte{0xE9}, std::byte{0x00}, std::byte{0x00},
                                  std::byte{0x00}, std::byte{0x00}, std::byte{0xC3}};
    tests.expect(emitted.error == ember::jit::x64::EmitError::none && emitted.code.has_value(),
                 "unconditional branch finalizes");
    if (emitted.code)
        tests.expect(isEqual(emitted.code->bytes(), expected),
                     "unconditional rel32 displacement is stable");
}

EMBER_TEST("x64 emitter encodes the remaining integer leaf subset")
{
    ember::jit::x64::Emitter emitter;
    tests.expect(emitter.moveImmediate64(ember::jit::x64::Register::r8, -1) &&
                     emitter.subtract(ember::jit::x64::Register::rax, ember::jit::x64::Register::r8) &&
                     emitter.multiply(ember::jit::x64::Register::r9, ember::jit::x64::Register::r10) &&
                     emitter.negate(ember::jit::x64::Register::r11) &&
                     emitter.test(ember::jit::x64::Register::r8, ember::jit::x64::Register::r9) &&
                     emitter.signExtendRaxIntoRdx() &&
                     emitter.signedDivide(ember::jit::x64::Register::r10) &&
                     emitter.returnFromFunction(),
                 "extended integer leaf instructions encode");

    auto emitted = emitter.finalize();
    constexpr std::array expected{
        std::byte{0x49}, std::byte{0xB8}, std::byte{0xFF}, std::byte{0xFF}, std::byte{0xFF},
        std::byte{0xFF}, std::byte{0xFF}, std::byte{0xFF}, std::byte{0xFF}, std::byte{0xFF},
        std::byte{0x4C}, std::byte{0x29}, std::byte{0xC0}, std::byte{0x4D}, std::byte{0x0F},
        std::byte{0xAF}, std::byte{0xCA}, std::byte{0x49}, std::byte{0xF7}, std::byte{0xDB},
        std::byte{0x4D}, std::byte{0x85}, std::byte{0xC8}, std::byte{0x48}, std::byte{0x99},
        std::byte{0x49}, std::byte{0xF7}, std::byte{0xFA}, std::byte{0xC3}};
    tests.expect(emitted.error == ember::jit::x64::EmitError::none && emitted.code.has_value(),
                 "extended integer emission finalizes");
    if (emitted.code)
    {
        tests.expect(isEqual(emitted.code->bytes(), expected), "extended integer bytes are stable");
        tests.expect(emitted.code->listing() ==
                         "0000: 49 B8 FF FF FF FF FF FF FF FF 4C 29 C0 4D 0F AF\n"
                         "0010: CA 49 F7 DB 4D 85 C8 48 99 49 F7 FA C3",
                     "multi-line listing has stable offsets and no trailing newline");
    }
}

EMBER_TEST("x64 emitter uses a REX prefix for every low-byte setcc register")
{
    ember::jit::x64::Emitter emitter;
    tests.expect(emitter.set(ember::jit::x64::Condition::equal, ember::jit::x64::Register::rsp) &&
                     emitter.set(ember::jit::x64::Condition::notEqual, ember::jit::x64::Register::r8) &&
                     emitter.returnFromFunction(),
                 "setcc accepts SPL and R8B destinations");
    const auto emitted = emitter.finalize();
    constexpr std::array expected{std::byte{0x40}, std::byte{0x0F}, std::byte{0x94},
                                  std::byte{0xC4}, std::byte{0x41}, std::byte{0x0F},
                                  std::byte{0x95}, std::byte{0xC0}, std::byte{0xC3}};
    tests.expect(emitted.code.has_value() && emitted.error == ember::jit::x64::EmitError::none,
                 "setcc low-byte encodings finalize");
    if (emitted.code)
        tests.expect(isEqual(emitted.code->bytes(), expected),
                     "SPL uses neutral REX and R8B uses extended REX");
}

EMBER_TEST("x64 emitter rejects unbound labels")
{
    ember::jit::x64::Emitter emitter;
    const auto target = emitter.createLabel();
    tests.expect(emitter.jump(target), "branch placeholder is emitted");

    const auto emitted = emitter.finalize();
    tests.expect(!emitted.code && emitted.error == ember::jit::x64::EmitError::unboundLabel,
                 "unbound label fails closed");
}

EMBER_TEST("x64 emitter rejects duplicate label bindings and remains fail-sticky")
{
    ember::jit::x64::Emitter emitter;
    const auto label = emitter.createLabel();
    tests.expect(emitter.bind(label), "first label binding succeeds");
    tests.expect(!emitter.bind(label), "duplicate label binding fails");
    static_cast<void>(emitter.createLabel());
    tests.expect(!emitter.returnFromFunction(), "emission remains blocked after the first error");

    const auto emitted = emitter.finalize();
    tests.expect(!emitted.code && emitted.error == ember::jit::x64::EmitError::duplicateLabel,
                 "duplicate label remains the terminal error");
}

EMBER_TEST("x64 emitter rejects labels owned by another emitter")
{
    ember::jit::x64::Emitter first;
    ember::jit::x64::Emitter second;
    const auto foreign = first.createLabel();
    static_cast<void>(second.createLabel());

    tests.expect(!second.jump(foreign), "foreign label is rejected before a branch is emitted");
    tests.expect(!second.returnFromFunction(), "foreign-label failure is sticky");
    const auto emitted = second.finalize();
    tests.expect(!emitted.code && emitted.error == ember::jit::x64::EmitError::invalidLabel,
                 "foreign label cannot alter another emitter's control flow");
}

EMBER_TEST("x64 emitter rejects labels that outlive an emitter storage generation")
{
    std::optional<ember::jit::x64::Emitter> slot;
    slot.emplace();
    const auto stale = slot->createLabel();
    slot.reset();

    slot.emplace();
    static_cast<void>(slot->createLabel());
    tests.expect(!slot->jump(stale), "stale label is rejected after storage reuse");
    const auto emitted = slot->finalize();
    tests.expect(!emitted.code && emitted.error == ember::jit::x64::EmitError::invalidLabel,
                 "stale label cannot target a new emitter generation");
}

EMBER_TEST("x64 emitter rejects implicit division registers")
{
    ember::jit::x64::Emitter rdxEmitter;
    tests.expect(!rdxEmitter.signedDivide(ember::jit::x64::Register::rdx),
                 "RDX cannot be both dividend high half and divisor");
    tests.expect(!rdxEmitter.returnFromFunction(), "division-operand failure is sticky");

    const auto emitted = rdxEmitter.finalize();
    tests.expect(!emitted.code && emitted.error == ember::jit::x64::EmitError::invalidDivisionOperand,
                 "invalid implicit division operand is reported");

    ember::jit::x64::Emitter raxEmitter;
    tests.expect(!raxEmitter.signedDivide(ember::jit::x64::Register::rax),
                 "RAX cannot be both quotient destination and divisor");
    const auto raxEmitted = raxEmitter.finalize();
    tests.expect(!raxEmitted.code &&
                     raxEmitted.error == ember::jit::x64::EmitError::invalidDivisionOperand,
                 "RAX division operand is reported independently");
}

EMBER_TEST("x64 emitter rejects emission and repeated finalization after publication")
{
    ember::jit::x64::Emitter emitter;
    tests.expect(emitter.returnFromFunction(), "return instruction encodes before finalization");
    const auto first = emitter.finalize();
    tests.expect(first.code.has_value(), "first finalization publishes machine code");
    tests.expect(!emitter.returnFromFunction(), "emission after finalization fails");
    const auto second = emitter.finalize();
    tests.expect(!second.code && second.error == ember::jit::x64::EmitError::finalized,
                 "second finalization fails deterministically");
}

EMBER_TEST("native code handle rejects an empty publication")
{
    ember::jit::x64::Emitter emitter;
    auto emitted = emitter.finalize();
    tests.expect(emitted.code.has_value(), "empty emitter can produce an empty finalized byte sequence");
    if (!emitted.code)
        return;

    const auto native = ember::runtime::NativeCodeHandle::publishI64Binary(std::move(*emitted.code));
    tests.expect(!native.handle && native.error == ember::jit::CodeBufferError::emptyCode,
                 "empty byte sequence cannot become executable memory");
}

EMBER_TEST("x64 emitter encodes stack-frame memory and indirect-call primitives")
{
    ember::jit::x64::Emitter emitter;
    tests.expect(emitter.push(ember::jit::x64::Register::rbx) &&
                     emitter.subtractStackPointer(32) &&
                     emitter.load(ember::jit::x64::Register::rax,
                                  ember::jit::x64::Register::rsp, 16) &&
                     emitter.loadEffectiveAddress(ember::jit::x64::Register::r8,
                                                  ember::jit::x64::Register::rsp, 40) &&
                     emitter.store(ember::jit::x64::Register::rsp, 24,
                                   ember::jit::x64::Register::rax) &&
                     emitter.addStackPointer(32) && emitter.pop(ember::jit::x64::Register::rbx) &&
                     emitter.returnFromFunction(),
                 "frame and memory primitives encode");
    const auto emitted = emitter.finalize();
    tests.expect(emitted.code.has_value() && emitted.error == ember::jit::x64::EmitError::none,
                 "frame primitive encoding finalizes");
    if (emitted.code)
    {
        constexpr std::array expectedLea{std::byte{0x4C}, std::byte{0x8D}, std::byte{0x84},
                                         std::byte{0x24}, std::byte{0x28}, std::byte{0x00},
                                         std::byte{0x00}, std::byte{0x00}};
        const auto bytes = emitted.code->bytes();
        tests.expect(bytes.size() >= expectedLea.size() + 16 &&
                         isEqual(bytes.subspan(16, expectedLea.size()), expectedLea),
                     "LEA uses the checked RSP SIB encoding needed for call arguments");
    }
}

#if EMBER_HAS_WIN64_JIT
EMBER_TEST("native code handle executes Win64 i64 arithmetic from RX memory")
{
    ember::jit::x64::Emitter emitter;
    tests.expect(emitter.move(ember::jit::x64::Register::rax, ember::jit::x64::Register::rcx) &&
                     emitter.add(ember::jit::x64::Register::rax, ember::jit::x64::Register::rdx) &&
                     emitter.returnFromFunction(),
                 "native arithmetic instructions encode");
    auto emitted = emitter.finalize();
    tests.expect(emitted.code.has_value(), "native arithmetic code finalizes");
    if (!emitted.code)
        return;

    const auto codeSize = emitted.code->bytes().size();
    auto native = ember::runtime::NativeCodeHandle::publishI64Binary(std::move(*emitted.code));
    tests.expect(native.handle.has_value() && native.error == ember::jit::CodeBufferError::none,
                 "machine code is published");
    if (!native.handle)
        return;

    auto moved = std::move(*native.handle);
    tests.expect(!native.handle->valid(), "moved-from native handle is invalid");
    tests.expect(moved.valid() && moved.codeSize() == codeSize,
                 "moved native handle retains the published code");

    MEMORY_BASIC_INFORMATION memoryInfo{};
    const auto queryResult = ::VirtualQuery(
        ember::runtime::test::NativeCodeHandleAccess::entryAddress(moved), &memoryInfo,
        sizeof(memoryInfo));
    tests.expect(queryResult == sizeof(memoryInfo), "published code has a queryable virtual-memory region");
    if (queryResult == sizeof(memoryInfo))
        tests.expect(memoryInfo.Protect == PAGE_EXECUTE_READ,
                     "published machine code has final RX protection");

    tests.expect(moved.invokeI64Binary(19, -7) == 12,
                 "generated i64 function returns the arithmetic result");
}

EMBER_TEST("native code handle executes both generated branch paths")
{
    ember::jit::x64::Emitter emitter;
    const auto useLeft = emitter.createLabel();
    tests.expect(emitter.move(ember::jit::x64::Register::rax, ember::jit::x64::Register::rdx) &&
                     emitter.compare(ember::jit::x64::Register::rcx, ember::jit::x64::Register::rdx) &&
                     emitter.jump(ember::jit::x64::Condition::lessEqual, useLeft) &&
                     emitter.returnFromFunction() && emitter.bind(useLeft) &&
                     emitter.move(ember::jit::x64::Register::rax, ember::jit::x64::Register::rcx) &&
                     emitter.returnFromFunction(),
                 "branch function instructions encode");
    auto emitted = emitter.finalize();
    tests.expect(emitted.code.has_value(), "branch function finalizes");
    if (!emitted.code)
        return;

    auto native = ember::runtime::NativeCodeHandle::publishI64Binary(std::move(*emitted.code));
    tests.expect(native.handle.has_value(), "branch code is published");
    if (!native.handle)
        return;

    tests.expect(native.handle->invokeI64Binary(-4, 9) == -4,
                 "generated branch takes the signed less-than path");
    tests.expect(native.handle->invokeI64Binary(12, 3) == 3,
                 "generated branch takes the fall-through path");
    tests.expect(native.handle->invokeI64Binary(5, 5) == 5,
                 "generated branch handles equality");
}

EMBER_TEST("native code handle executes ordered SSE2 f64 comparisons")
{
    ember::jit::x64::Emitter emitter;
    tests.expect(emitter.moveToXmm(ember::jit::x64::XmmRegister::xmm0,
                                   ember::jit::x64::Register::rcx) &&
                     emitter.moveToXmm(ember::jit::x64::XmmRegister::xmm1,
                                       ember::jit::x64::Register::rdx) &&
                     emitter.compareDouble(ember::jit::x64::XmmRegister::xmm0,
                                           ember::jit::x64::XmmRegister::xmm1) &&
                     emitter.moveImmediate64(ember::jit::x64::Register::rax, 0) &&
                     emitter.set(ember::jit::x64::Condition::below,
                                 ember::jit::x64::Register::rax) &&
                     emitter.returnFromFunction(),
                 "SSE2 comparison instructions encode");
    auto emitted = emitter.finalize();
    tests.expect(emitted.code.has_value(), "SSE2 comparison code finalizes");
    if (!emitted.code)
        return;
    auto native = ember::runtime::NativeCodeHandle::publishI64Binary(std::move(*emitted.code));
    tests.expect(native.handle.has_value(), "SSE2 comparison code is published");
    if (!native.handle)
        return;
    const auto negative = std::bit_cast<std::int64_t>(-2.5);
    const auto zero = std::bit_cast<std::int64_t>(0.0);
    tests.expect(native.handle->invokeI64Binary(negative, zero) == 1,
                 "UCOMISD plus SETB recognises an ordered less-than relation");
}

EMBER_TEST("native code handle preserves f64 bits through XMM MOVQ")
{
    ember::jit::x64::Emitter emitter;
    tests.expect(emitter.moveToXmm(ember::jit::x64::XmmRegister::xmm0,
                                   ember::jit::x64::Register::rcx) &&
                     emitter.moveFromXmm(ember::jit::x64::Register::rax,
                                         ember::jit::x64::XmmRegister::xmm0) &&
                     emitter.returnFromFunction(),
                 "XMM MOVQ instructions encode");
    auto emitted = emitter.finalize();
    tests.expect(emitted.code.has_value(), "XMM MOVQ code finalizes");
    if (!emitted.code)
        return;
    auto native = ember::runtime::NativeCodeHandle::publishI64Binary(std::move(*emitted.code));
    tests.expect(native.handle.has_value(), "XMM MOVQ code is published");
    if (!native.handle)
        return;
    const auto input = std::bit_cast<std::int64_t>(-2.5);
    tests.expect(native.handle->invokeI64Binary(input, 0) == input,
                 "MOVQ preserves the f64 bit pattern across the XMM boundary");
}

EMBER_TEST("native frame adapter executes generated local-slot access")
{
    ember::jit::x64::Emitter emitter;
    tests.expect(emitter.load(ember::jit::x64::Register::rax, ember::jit::x64::Register::rcx, 0) &&
                     emitter.load(ember::jit::x64::Register::rax, ember::jit::x64::Register::rax, 0) &&
                     emitter.returnFromFunction(),
                 "native frame local access encodes");
    auto emitted = emitter.finalize();
    tests.expect(emitted.code.has_value(), "native frame access finalizes");
    if (!emitted.code)
        return;
    auto native = ember::runtime::NativeCodeHandle::publishWordFrame(std::move(*emitted.code));
    tests.expect(native.handle.has_value(), "native frame code is published");
    if (!native.handle)
        return;
    std::vector<std::uint64_t> locals{73U};
    ember::runtime::NativeFrame frame{.locals = locals.data(), .localCount = locals.size()};
    tests.expect(native.handle->invokeI64Frame(frame) == 73,
                 "native frame adapter returns the requested local slot");
}
#else
EMBER_TEST("native code publication reports unsupported target")
{
    ember::jit::x64::Emitter emitter;
    tests.expect(emitter.returnFromFunction(), "return instruction encodes for publication test");
    auto emitted = emitter.finalize();
    tests.expect(emitted.code.has_value(), "publication test code finalizes");
    if (!emitted.code)
        return;

    const auto native = ember::runtime::NativeCodeHandle::publishI64Binary(std::move(*emitted.code));
    tests.expect(!native.handle && native.error == ember::jit::CodeBufferError::unsupportedTarget,
                 "native publication is unavailable outside Win64");
}
#endif
