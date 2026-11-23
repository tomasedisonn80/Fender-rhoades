#include "rhodes/instrument_state.hpp"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

#if defined(__SSE__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 1)
#define RHODES_TEST_MXCSR 1
#include <xmmintrin.h>
#if defined(_MSC_VER)
#include <immintrin.h>
#endif
#else
#define RHODES_TEST_MXCSR 0
#endif

#if defined(_MSC_VER)
#define RHODES_TEST_NOINLINE __declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
#define RHODES_TEST_NOINLINE __attribute__((noinline))
#else
#define RHODES_TEST_NOINLINE
#endif

namespace {
using Bytes = std::array<std::uint8_t, 16>;
using rhodes::InstrumentState;
using rhodes::StateMailbox;
constexpr std::uint32_t one = 0x3f800000U;
constexpr std::uint32_t negative_zero = 0x80000000U;
constexpr unsigned ftz = 1U << 15U;
constexpr unsigned daz = 1U << 6U;
std::size_t checks = 0;

// Assertions stay active in Release. No test-oracle comparison uses floating
// arithmetic: DAZ must not make a subnormal appear equal to zero in the oracle.
void require(bool condition, std::string_view message) {
    ++checks;
    if (!condition) { throw std::runtime_error(std::string{message}); }
}

Bytes packet(std::uint32_t gain, std::uint32_t pedal) {
    Bytes bytes{'R', 'H', 'S', 'T', 1, 0, 1, 0};
    for (std::size_t digit = 0; digit < 4; ++digit) {
        bytes[8 + digit] = static_cast<std::uint8_t>(gain % 256U);
        bytes[12 + digit] = static_cast<std::uint8_t>(pedal % 256U);
        gain /= 256U;
        pedal /= 256U;
    }
    return bytes;
}

bool expected_gain(std::uint32_t bits) {
    const bool sign = (bits >> 31U) != 0U;
    const auto exponent = (bits >> 23U) & 0xffU;
    const auto fraction = bits & 0x7fffffU;
    if (sign) { return exponent == 0U && fraction == 0U; }
    return exponent < 127U || (exponent == 127U && fraction == 0U);
}

std::uint32_t canonical(std::uint32_t bits) {
    return bits == negative_zero ? 0U : bits;
}

void expect_state(InstrumentState state, std::uint32_t gain, bool sustain) {
    require(std::bit_cast<std::uint32_t>(state.monitoring_gain) == gain,
            "configuration gain bits changed under floating-point controls");
    require(state.sustain == sustain, "configuration sustain changed");
}

// Runtime inputs and noinline boundaries prevent the compiler from evaluating
// the original floating-comparison implementation before the control change.
RHODES_TEST_NOINLINE bool validate(InstrumentState state) {
    return rhodes::valid_instrument_state(state);
}
RHODES_TEST_NOINLINE auto encode(InstrumentState state) {
    return rhodes::encode_instrument_state(state);
}
RHODES_TEST_NOINLINE auto decode(const Bytes& bytes) {
    return rhodes::decode_instrument_state(bytes);
}
RHODES_TEST_NOINLINE bool recall(StateMailbox& mailbox, InstrumentState state) {
    return mailbox.request_recall(state);
}
RHODES_TEST_NOINLINE bool publish(StateMailbox& mailbox,
                                const StateMailbox::BlockToken& token,
                                InstrumentState state) {
    return mailbox.end_block(token, state);
}

void probe(std::uint32_t source_bits) {
    volatile std::uint32_t runtime_bits = source_bits;
    const std::uint32_t bits = runtime_bits;
    const bool allowed = expected_gain(bits);
    const auto wanted = canonical(bits);
    const InstrumentState state{std::bit_cast<float>(bits), true};
    require(validate(state) == allowed, "gain validation depends on FP mode");
    const auto encoded = encode(state);
    require(encoded.has_value() == allowed, "gain encoding domain changed");
    if (encoded) { require(*encoded == packet(wanted, one), "gain encoding changed bits"); }
    const auto input = packet(bits, one);
    const auto before = input;
    const auto decoded = decode(input);
    require(input == before, "decoder altered caller bytes");
    require(decoded.has_value() == allowed, "gain decoding domain changed");
    if (decoded) {
        expect_state(*decoded, wanted, true);
        require(encode(*decoded) == packet(wanted, one), "canonical round trip changed bits");
    }

    // Invalid UI requests must preserve an already pending recall. Valid
    // subnormal requests must survive snapshot, consume, abort, and publication.
    StateMailbox mailbox;
    const InstrumentState baseline{0.75F, false};
    require(recall(mailbox, baseline), "baseline recall failed");
    require(recall(mailbox, state) == allowed, "mailbox recall domain changed");
    const auto retained_bits = allowed ? wanted : 0x3f400000U;
    const bool retained_sustain = allowed;
    expect_state(mailbox.snapshot(), retained_bits, retained_sustain);
    const auto token = mailbox.begin_block();
    require(token.apply_recall && token.may_publish, "pending recall disappeared");
    expect_state(token.recalled, retained_bits, retained_sustain);
    mailbox.abort_block(token);
    const auto retry = mailbox.begin_block();
    require(retry.apply_recall, "abort did not restore pending recall");
    expect_state(retry.recalled, retained_bits, retained_sustain);
    require(publish(mailbox, retry, state) == allowed, "mailbox publication domain changed");
    expect_state(mailbox.snapshot(), retained_bits, retained_sustain);
    if (!allowed) {
        require(publish(mailbox, retry, baseline), "rejected publication damaged its token");
        expect_state(mailbox.snapshot(), 0x3f400000U, false);
    }
    require(!mailbox.begin_block().apply_recall, "consumed recall remained pending");

    const bool pedal_allowed = bits == 0U || bits == negative_zero || bits == one;
    // Keep a named input across the noinline boundary, as for the gain probe.
    // This also makes preservation of the caller's pedal bytes observable.
    const auto pedal_input = packet(0x3f000000U, bits);
    const auto pedal_before = pedal_input;
    const auto pedal = decode(pedal_input);
    require(pedal_input == pedal_before, "pedal decoder altered caller bytes");
    require(pedal.has_value() == pedal_allowed, "pedal must be exact encoded zero or one");
    if (pedal) {
        expect_state(*pedal, 0x3f000000U, bits == one);
        require(encode(*pedal) == packet(0x3f000000U, bits == one ? one : 0U),
                "pedal did not canonicalize only signed zero");
    }
}

void protocol_cases() {
    constexpr std::array<std::uint32_t, 22> edges{
        0U, negative_zero, 1U, 2U, 0x00012345U, 0x007fffffU,
        0x80000001U, 0x80000002U, 0x80012345U, 0x807fffffU,
        0x00800000U, 0x80800000U, 0x3f000000U, 0x3f7fffffU,
        one, 0x3f800001U, 0x7f800000U, 0xff800000U,
        0x7fc12345U, 0x7f812345U, 0xffc12345U, 0xff812345U};
    for (const auto bits : edges) { probe(bits); }
    std::uint32_t generator = 0x50da2f17U;
    for (std::size_t index = 0; index < 128; ++index) {
        generator ^= generator << 13U;
        generator ^= generator >> 17U;
        generator ^= generator << 5U;
        probe(generator);
        const auto subnormal = (generator & 0x7fffffU) | 1U;
        probe(subnormal);
        probe(subnormal | negative_zero);
    }
}

#if RHODES_TEST_MXCSR
constexpr unsigned status_flags = 0x3fU;
struct RestoreControls {
    unsigned original = _mm_getcsr();
    ~RestoreControls() { _mm_setcsr(original); }
};

unsigned supported_controls() {
    alignas(16) std::array<unsigned char, 512> saved{};
#if defined(_MSC_VER)
    _fxsave(saved.data());
#else
    asm volatile("fxsave %0" : "=m"(saved));
#endif
    std::uint32_t mask = 0;
    std::memcpy(&mask, saved.data() + 28, sizeof(mask));
    // An absent mask does not justify enabling an unreported control bit.
    return mask;
}
#endif

bool run_mode(std::string_view name, unsigned controls) {
#if RHODES_TEST_MXCSR
    const auto original = _mm_getcsr();
    const auto supported = supported_controls();
    if ((controls & ~supported) != 0U) {
        std::cout << "SKIP " << name << ": requested MXCSR bits are not reported supported\n";
        return false;
    }
    {
        RestoreControls restore;
        // Mask exceptions for malformed signaling-NaN probes. Every original
        // control/status bit is restored even if an always-active check throws.
        const auto requested = (original & ~(ftz | daz | status_flags)) | controls | 0x1f80U;
        _mm_setcsr(requested);
        require(_mm_getcsr() == requested, "MXCSR control write did not take effect");
        protocol_cases();
        require((_mm_getcsr() & status_flags) == 0U,
                "state protocol raised an FP exception/status flag");
    }
    require(_mm_getcsr() == original, "MXCSR was not restored exactly");
#else
    if (controls != 0U) {
        std::cout << "SKIP " << name << ": x86 MXCSR controls unavailable\n";
        return false;
    }
    // The portable baseline still checks bytes under the native environment;
    // it makes no claim that this architecture's flush controls were changed.
    protocol_cases();
#endif
    std::cout << "PASS " << name << " (" << checks << " always-active checks)\n";
    return true;
}
} // namespace

int main(int argc, char** argv) {
    const std::string_view selected = argc > 1 ? argv[1] : "all";
    try {
        constexpr std::array<std::string_view, 4> names{"baseline", "ftz", "daz", "ftz_daz"};
        constexpr std::array<unsigned, 4> controls{0U, ftz, daz, ftz | daz};
        bool found = false;
        bool ran = false;
        for (std::size_t index = 0; index < names.size(); ++index) {
            if (selected != "all" && selected != names[index]) { continue; }
            found = true;
            checks = 0;
            ran = run_mode(names[index], controls[index]) || ran;
        }
        require(found, "unknown floating-point environment selector");
        return ran ? 0 : 77; // CTest may mark unsupported platform/mode as skipped.
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << selected << ": " << error.what() << '\n';
        return 1;
    }
}
