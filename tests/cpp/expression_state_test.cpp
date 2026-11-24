#include "rhodes/instrument_state.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cfenv>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>

#if defined(__SSE__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 1)
#define RHODES_EXPRESSION_TEST_MXCSR 1
#include <xmmintrin.h>
#if defined(_MSC_VER)
#include <immintrin.h>
#endif
#else
#define RHODES_EXPRESSION_TEST_MXCSR 0
#endif

#if defined(_MSC_VER)
#define RHODES_EXPRESSION_TEST_NOINLINE __declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
#define RHODES_EXPRESSION_TEST_NOINLINE __attribute__((noinline))
#else
#define RHODES_EXPRESSION_TEST_NOINLINE
#endif

namespace {
using LegacyBytes = std::array<std::uint8_t, 16>;
using ExpressionBytes = std::array<std::uint8_t, 20>;
using rhodes::InstrumentState;
using rhodes::StateMailbox;
constexpr std::uint32_t one = 0x3f800000U;
constexpr std::uint32_t negative_zero = 0x80000000U;
constexpr std::array<std::uint16_t, 3> models{1, 2, 3};
std::uint64_t checks = 0;

static_assert(rhodes::instrument_state_bytes == 16);
static_assert(rhodes::instrument_state_v2_bytes == 20);
static_assert(std::is_same_v<rhodes::EncodedInstrumentState, LegacyBytes>);
static_assert(std::is_same_v<rhodes::EncodedInstrumentStateV2, ExpressionBytes>);

// Assertions remain active in Release. Expected domains, bytes and mailbox words
// never use state_detail helpers or floating-point equality.
void require(bool condition, std::string_view message) {
    ++checks;
    if (!condition) { throw std::runtime_error(std::string{message}); }
}

constexpr bool expected_normalized(std::uint32_t word) noexcept {
    const bool negative = (word >> 31U) != 0U;
    const auto exponent = (word >> 23U) & 255U;
    const auto fraction = word & 0x7fffffU;
    if (negative) { return exponent == 0U && fraction == 0U; }
    return exponent < 127U || (exponent == 127U && fraction == 0U);
}

constexpr std::uint32_t canonical(std::uint32_t word) noexcept {
    return word == negative_zero ? 0U : word;
}

InstrumentState state(std::uint32_t gain, bool sustain, std::uint32_t expression) {
    return {std::bit_cast<float>(gain), sustain, std::bit_cast<float>(expression)};
}

void expect_state(InstrumentState actual, std::uint32_t gain, bool sustain,
                  std::uint32_t expression) {
    require(std::bit_cast<std::uint32_t>(actual.monitoring_gain) == gain,
            "monitoring gain payload changed");
    require(actual.sustain == sustain, "sustain changed");
    require(std::bit_cast<std::uint32_t>(actual.expression) == expression,
            "expression payload changed");
}

// Arithmetic base-256 digits supply an independent little-endian wire oracle.
void digits(std::span<std::uint8_t> bytes, std::size_t at, std::uint32_t word) {
    for (std::size_t digit = 0; digit < 4; ++digit) {
        bytes[at + digit] = static_cast<std::uint8_t>(word % 256U);
        word /= 256U;
    }
}

LegacyBytes legacy_packet(std::uint16_t model, std::uint32_t gain,
                          std::uint32_t sustain) {
    LegacyBytes bytes{0x52, 0x48, 0x53, 0x54, 1, 0,
        static_cast<std::uint8_t>(model % 256U), static_cast<std::uint8_t>(model / 256U)};
    digits(bytes, 8, gain);
    digits(bytes, 12, sustain);
    return bytes;
}

ExpressionBytes packet(std::uint16_t model, std::uint32_t gain,
                       std::uint32_t sustain, std::uint32_t expression) {
    ExpressionBytes bytes{0x52, 0x48, 0x53, 0x54, 2, 0,
        static_cast<std::uint8_t>(model % 256U), static_cast<std::uint8_t>(model / 256U)};
    digits(bytes, 8, gain);
    digits(bytes, 12, sustain);
    digits(bytes, 16, expression);
    return bytes;
}

constexpr std::array<LegacyBytes, 3> legacy_goldens{{
    {0x52, 0x48, 0x53, 0x54, 1, 0, 1, 0, 0, 0, 0, 0x3f, 0, 0, 0x80, 0x3f},
    {0x52, 0x48, 0x53, 0x54, 1, 0, 2, 0, 0, 0, 0, 0x3f, 0, 0, 0x80, 0x3f},
    {0x52, 0x48, 0x53, 0x54, 1, 0, 3, 0, 0, 0, 0, 0x3f, 0, 0, 0x80, 0x3f}}};
constexpr std::array<ExpressionBytes, 3> expression_goldens{{
    {0x52, 0x48, 0x53, 0x54, 2, 0, 1, 0, 0, 0, 0, 0x3f, 0, 0, 0x80, 0x3f,
     0, 0, 0x80, 0x3e},
    {0x52, 0x48, 0x53, 0x54, 2, 0, 2, 0, 0, 0, 0, 0x3f, 0, 0, 0x80, 0x3f,
     0, 0, 0x80, 0x3e},
    {0x52, 0x48, 0x53, 0x54, 2, 0, 3, 0, 0, 0, 0, 0x3f, 0, 0, 0x80, 0x3f,
     0, 0, 0x80, 0x3e}}};

void defaults_and_goldens() {
    expect_state({}, 0x3f000000U, false, one);
    expect_state({0.25F, true}, 0x3e800000U, true, one);
    require(rhodes::encode_instrument_state({0.5F, true}) == legacy_goldens[0],
            "default model or legacy aggregate bytes changed");
    require(rhodes::encode_instrument_state_v2({0.5F, true, 0.25F}) == expression_goldens[0],
            "default v2 model changed");
    for (std::size_t index = 0; index < models.size(); ++index) {
        const auto model = models[index];
        require(legacy_packet(model, 0x3f000000U, one) == legacy_goldens[index],
                "independent v1 oracle differs from literal fixture");
        require(packet(model, 0x3f000000U, one, 0x3e800000U) == expression_goldens[index],
                "independent v2 oracle differs from literal fixture");
        require(rhodes::encode_instrument_state({0.5F, true, 1.0F}, model) == legacy_goldens[index],
                "explicit unity failed exact v1 compatibility");
        const auto legacy = rhodes::decode_instrument_state(legacy_goldens[index], model);
        require(legacy.has_value(), "literal v1 rejected");
        expect_state(*legacy, 0x3f000000U, true, one);
        require(rhodes::encode_instrument_state_v2({0.5F, true, 0.25F}, model) ==
                expression_goldens[index], "literal v2 encoding differs");
        const auto expression = rhodes::decode_instrument_state(expression_goldens[index], model);
        require(expression.has_value(), "literal v2 rejected");
        expect_state(*expression, 0x3f000000U, true, 0x3e800000U);
        require(!rhodes::encode_instrument_state(*expression, model),
                "legacy export silently discarded nonunity expression");
        const auto v2_unity = rhodes::decode_instrument_state(
            packet(model, 0x3f000000U, one, one), model);
        require(v2_unity.has_value() &&
                rhodes::encode_instrument_state(*v2_unity, model) == legacy_goldens[index],
                "v2 unity did not retain canonical v1 export");
        const auto zeros = rhodes::decode_instrument_state(
            packet(model, negative_zero, negative_zero, negative_zero), model);
        require(zeros.has_value(), "signed-zero fields rejected");
        expect_state(*zeros, 0U, false, 0U);
        require(rhodes::encode_instrument_state_v2(*zeros, model) == packet(model, 0U, 0U, 0U),
                "signed zeros did not re-encode canonically");
        require(!rhodes::encode_instrument_state(*zeros, model), "zero expression lost in v1 export");
    }
    constexpr ExpressionBytes subnormal_golden{
        0x52, 0x48, 0x53, 0x54, 2, 0, 1, 0, 0, 0, 0, 0x3f, 0, 0, 0x80, 0x3f,
        1, 0, 0, 0};
    require(rhodes::encode_instrument_state_v2(state(0x3f000000U, true, 1U)) == subnormal_golden,
            "smallest subnormal expression bytes changed");
}

template <class Function>
std::size_t patterns(Function function) {
    std::size_t count = 0;
    const auto visit = [&](std::uint32_t word) { function(word); ++count; };
    constexpr std::array<std::uint32_t, 18> edges{
        0U, 1U, 2U, 0x007fffffU, 0x00800000U, 0x00800001U,
        0x3f7ffffeU, 0x3f7fffffU, one, 0x3f800001U, 0x3f800002U,
        0x7f7fffffU, 0x7f800000U, 0x7f800001U, 0x7fbfffffU,
        0x7fc00000U, 0x7fffffffU, negative_zero};
    for (const auto word : edges) { visit(word); }
    constexpr std::array<std::uint32_t, 6> fractions{
        0U, 1U, 0x001fffffU, 0x003fffffU, 0x007ffffeU, 0x007fffffU};
    for (std::uint32_t exponent = 0; exponent < 256U; ++exponent) {
        for (const auto fraction : fractions) {
            for (const auto sign : {0U, negative_zero}) {
                visit(sign | (exponent << 23U) | fraction);
            }
        }
    }
    std::uint32_t generator = 0x5a17c0deU;
    for (std::size_t index = 0; index < 16384; ++index) {
        generator ^= generator << 13U;
        generator ^= generator >> 17U;
        generator ^= generator << 5U;
        visit(generator);
    }
    return count;
}

void codec_domains() {
    const auto count = patterns([](std::uint32_t word) {
        const bool allowed = expected_normalized(word);
        for (const auto model : models) {
            for (const bool sustain : {false, true}) {
                const auto candidate = state(0x3eaaaaabU, sustain, word);
                require(rhodes::valid_instrument_state(candidate) == allowed,
                        "expression validation domain differs");
                const auto encoded = rhodes::encode_instrument_state_v2(candidate, model);
                require(encoded.has_value() == allowed, "expression v2 encoder domain differs");
                require(rhodes::encode_instrument_state(candidate, model).has_value() == (word == one),
                        "legacy encoder accepted expression other than exact unity");
                const auto input = packet(model, 0x3eaaaaabU, sustain ? one : 0U, word);
                const auto before = input;
                const auto decoded = rhodes::decode_instrument_state(input, model);
                require(input == before, "decoder modified input expression bytes");
                require(decoded.has_value() == allowed, "expression decoder domain differs");
                if (allowed) {
                    const auto wanted = packet(model, 0x3eaaaaabU, sustain ? one : 0U, canonical(word));
                    require(*encoded == wanted, "v2 encoder changed valid expression bits");
                    expect_state(*decoded, 0x3eaaaaabU, sustain, canonical(word));
                    require(rhodes::encode_instrument_state_v2(*decoded, model) == wanted,
                            "decoded expression did not round trip exactly");
                }
            }
            const auto gain_state = state(word, true, 1U);
            require(rhodes::valid_instrument_state(gain_state) == allowed,
                    "v2 validation omitted gain");
            require(rhodes::encode_instrument_state_v2(gain_state, model).has_value() == allowed,
                    "v2 encoder gain domain differs");
            const auto gain = rhodes::decode_instrument_state(packet(model, word, one, 1U), model);
            require(gain.has_value() == allowed, "v2 decoder gain domain differs");
            if (gain) { expect_state(*gain, canonical(word), true, 1U); }
            const bool pedal_allowed = word == 0U || word == negative_zero || word == one;
            const auto pedal = rhodes::decode_instrument_state(packet(model, 1U, word, 1U), model);
            require(pedal.has_value() == pedal_allowed, "v2 sustain domain differs");
            if (pedal) { expect_state(*pedal, 1U, word == one, 1U); }
        }
    });
    require(count == 19474, "independent binary32 corpus size changed");
}

void malformed_packets() {
    require(!rhodes::decode_instrument_state(std::span<const std::uint8_t>{}),
            "null empty input accepted");
    for (const auto model : models) {
        for (const std::uint8_t version : {std::uint8_t{1}, std::uint8_t{2}}) {
            std::array<std::uint8_t, 40> storage{};
            const auto source = packet(model, 0x3f000000U, one, one);
            std::copy(source.begin(), source.end(), storage.begin());
            storage[4] = version;
            const std::size_t correct_size = version == 1 ? 16U : 20U;
            for (std::size_t length = 0; length <= storage.size(); ++length) {
                const auto before = storage;
                const auto decoded = rhodes::decode_instrument_state(
                    std::span<const std::uint8_t>{storage}.first(length), model);
                require(decoded.has_value() == (length == correct_size),
                        "version/length mismatch accepted or correct packet rejected");
                require(storage == before, "length rejection changed caller bytes");
            }
            for (std::size_t at = 0; at < 8; ++at) {
                for (unsigned byte = 0; byte < 256U; ++byte) {
                    if (byte == storage[at]) { continue; }
                    auto malformed = storage;
                    malformed[at] = static_cast<std::uint8_t>(byte);
                    const auto before = malformed;
                    require(!rhodes::decode_instrument_state(
                        std::span<const std::uint8_t>{malformed}.first(correct_size), model),
                        "mutated magic/version/model accepted");
                    require(malformed == before, "header rejection changed caller bytes");
                }
            }
            // A byte-span codec must not rely on the alignment of host storage.
            std::array<std::uint8_t, 26> unaligned{};
            unaligned.fill(0xa5U);
            std::copy_n(storage.begin(), correct_size, unaligned.begin() + 1);
            const auto before = unaligned;
            const auto decoded = rhodes::decode_instrument_state(
                std::span<const std::uint8_t>{unaligned}.subspan(1, correct_size), model);
            require(decoded.has_value(), "unaligned valid packet rejected");
            expect_state(*decoded, 0x3f000000U, true, one);
            require(unaligned == before, "unaligned decode changed payload or guards");
        }
        // Exhaust both version bytes together, including nonzero high bytes.
        for (std::uint32_t version = 0; version <= 65535U; ++version) {
            auto v1 = legacy_packet(model, 0x3f000000U, one);
            auto v2 = packet(model, 0x3f000000U, one, one);
            v1[4] = v2[4] = static_cast<std::uint8_t>(version % 256U);
            v1[5] = v2[5] = static_cast<std::uint8_t>(version / 256U);
            require(rhodes::decode_instrument_state(v1, model).has_value() == (version == 1U),
                    "16-byte wire version domain differs");
            require(rhodes::decode_instrument_state(v2, model).has_value() == (version == 2U),
                    "20-byte wire version domain differs");
        }
    }
}

void model_isolation() {
    for (const auto model : models) {
        for (const auto other : models) {
            if (other == model) { continue; }
            require(!rhodes::decode_instrument_state(legacy_packet(model, 0x3f000000U, one), other),
                    "cross-model legacy packet accepted");
            require(!rhodes::decode_instrument_state(packet(model, 0x3f000000U, one, 1U), other),
                    "cross-model expression packet accepted");
        }
    }
    std::size_t unsupported = 0;
    for (std::uint32_t word = 0; word <= std::numeric_limits<std::uint16_t>::max(); ++word) {
        if (word == 1U || word == 2U || word == 3U) { continue; }
        ++unsupported;
        const auto model = static_cast<std::uint16_t>(word);
        require(!rhodes::encode_instrument_state({}, model), "unknown legacy encoder model accepted");
        require(!rhodes::encode_instrument_state_v2({}, model), "unknown v2 encoder model accepted");
        const auto bytes = packet(model, 0x3f000000U, one, one);
        require(!rhodes::decode_instrument_state(bytes, model), "matching unknown v2 model accepted");
        for (const auto known : models) {
            require(!rhodes::decode_instrument_state(bytes, known), "unknown v2 wire model accepted");
            require(!rhodes::decode_instrument_state(expression_goldens[known - 1U], model),
                    "unknown requested v2 model accepted");
        }
    }
    require(unsupported == 65533U, "unknown model domain was not exhausted");
}

void mailbox_words() {
    struct Fixture {
        std::uint32_t gain;
        bool sustain;
        std::uint32_t expression;
        std::uint64_t clear_token;
    };
    constexpr std::array<Fixture, 10> fixtures{{
        {0x3f000000U, false, one, 0xfe0000003f000000ULL},
        {1U, true, 0x007fffffU, 0x01fffffd00000001ULL},
        {0U, false, 1U, 0x0000000400000000ULL},
        {one, true, 0U, 0x000000013f800000ULL},
        {0U, false, 0x1fffffffU, 0x7ffffffc00000000ULL},
        {0U, false, 0x20000000U, 0x8000000000000000ULL},
        {one, true, one, 0xfe0000013f800000ULL},
        {0x00800000U, true, 0x3f7fffffU, 0xfdfffffd00800000ULL},
        {0x007fffffU, false, 0x00800000U, 0x02000000007fffffULL},
        {negative_zero, true, negative_zero, 0x0000000100000000ULL}}};
    StateMailbox initial;
    expect_state(initial.snapshot(), 0x3f000000U, false, one);
    const auto initial_token = initial.begin_block();
    require(initial_token.publication_token == 0xfe0000003f000000ULL &&
            !initial_token.apply_recall && initial_token.may_publish,
            "default mailbox token differs from literal word");
    expect_state(initial_token.recalled, 0x3f000000U, false, one);
    for (const auto& fixture : fixtures) {
        StateMailbox mailbox;
        const auto input = state(fixture.gain, fixture.sustain, fixture.expression);
        const auto check = [&](InstrumentState actual) {
            expect_state(actual, canonical(fixture.gain), fixture.sustain, canonical(fixture.expression));
        };
        require(mailbox.request_recall(input), "literal mailbox recall rejected");
        check(mailbox.snapshot());
        const auto first = mailbox.begin_block();
        require(first.apply_recall && first.may_publish, "literal mailbox recall missing");
        require(first.publication_token == fixture.clear_token, "mailbox packing differs from literal word");
        check(first.recalled);
        mailbox.abort_block(first);
        const auto retry = mailbox.begin_block();
        require(retry.apply_recall && retry.publication_token == fixture.clear_token,
                "abort did not restore complete pending triple");
        check(retry.recalled);
        require(mailbox.end_block(retry, input), "literal mailbox publication rejected");
        check(mailbox.snapshot());
        const auto finished = mailbox.begin_block();
        require(!finished.apply_recall && finished.publication_token == fixture.clear_token,
                "publication retained pending flag or changed expression");
    }
}

void mailbox_transactions() {
    const auto baseline = state(0x3f000000U, true, 1U);
    const auto replacement = state(0x3f000000U, true, 0x007fffffU);
    const auto publication = state(0x3e800000U, false, 0x3f000000U);
    constexpr std::array<std::uint32_t, 11> invalid{
        0x80000001U, 0x807fffffU, 0xbf000000U, 0x3f800001U, 0x40000000U,
        0x7f7fffffU, 0x7f800000U, 0xff800000U, 0x7f800001U, 0x7fc12345U, 0xff812345U};
    for (const auto bits : invalid) {
        StateMailbox mailbox;
        require(mailbox.request_recall(baseline), "pending baseline failed");
        require(!mailbox.request_recall(state(0U, false, bits)), "invalid expression recall accepted");
        expect_state(mailbox.snapshot(), 0x3f000000U, true, 1U);
        const auto token = mailbox.begin_block();
        require(token.apply_recall, "invalid recall consumed existing pending state");
        expect_state(token.recalled, 0x3f000000U, true, 1U);
        require(!mailbox.end_block(token, state(0U, false, bits)),
                "invalid expression publication accepted");
        expect_state(mailbox.snapshot(), 0x3f000000U, true, 1U);
        require(mailbox.end_block(token, publication), "invalid publication damaged usable token");
        expect_state(mailbox.snapshot(), 0x3e800000U, false, 0x3f000000U);
    }
    // Deterministic single-owner interleavings: only expression differs, or a
    // later request is identical. Both must defeat an older completion/abort.
    for (const bool identical : {false, true}) {
        for (const bool abort : {false, true}) {
            StateMailbox mailbox;
            require(mailbox.request_recall(baseline), "ordered baseline failed");
            const auto old = mailbox.begin_block();
            require(old.apply_recall, "ordered baseline missing");
            require(mailbox.request_recall(identical ? baseline : replacement), "newer recall failed");
            if (abort) { mailbox.abort_block(old); }
            else { require(!mailbox.end_block(old, publication), "stale publication replaced recall"); }
            const auto wanted = identical ? 1U : 0x007fffffU;
            expect_state(mailbox.snapshot(), 0x3f000000U, true, wanted);
            const auto newer = mailbox.begin_block();
            require(newer.apply_recall, "newer recall lost pending flag");
            expect_state(newer.recalled, 0x3f000000U, true, wanted);
            require(mailbox.end_block(newer, newer.recalled), "newer completion failed");
            require(!mailbox.begin_block().apply_recall, "newer recall consumed twice");
        }
    }
    for (const bool consume_first : {false, true}) {
        StateMailbox mailbox;
        require(mailbox.request_recall(baseline), "ABA baseline failed");
        const auto old = consume_first ? mailbox.begin_block() : StateMailbox::BlockToken{};
        require(mailbox.request_recall(replacement) && mailbox.request_recall(baseline),
                "ABA coalesced writes failed");
        if (consume_first) {
            require(!mailbox.end_block(old, publication), "post-consume ABA lost later request");
            mailbox.abort_block(old);
        }
        const auto latest = mailbox.begin_block();
        require(latest.apply_recall, "ABA final request missing");
        expect_state(latest.recalled, 0x3f000000U, true, 1U);
        require(mailbox.end_block(latest, latest.recalled), "ABA final completion failed");
        require(!mailbox.begin_block().apply_recall, "ABA final request consumed twice");
    }
}

RHODES_EXPRESSION_TEST_NOINLINE bool validate(InstrumentState value) {
    return rhodes::valid_instrument_state(value);
}
RHODES_EXPRESSION_TEST_NOINLINE auto encode(InstrumentState value) {
    return rhodes::encode_instrument_state_v2(value);
}
RHODES_EXPRESSION_TEST_NOINLINE auto decode(const ExpressionBytes& bytes) {
    return rhodes::decode_instrument_state(bytes);
}
RHODES_EXPRESSION_TEST_NOINLINE bool recall(StateMailbox& mailbox, InstrumentState value) {
    return mailbox.request_recall(value);
}
RHODES_EXPRESSION_TEST_NOINLINE bool publish(StateMailbox& mailbox,
                                            const StateMailbox::BlockToken& token,
                                            InstrumentState value) {
    return mailbox.end_block(token, value);
}

void floating_point_probe(std::uint32_t source) {
    volatile std::uint32_t runtime_bits = source;
    const std::uint32_t bits = runtime_bits;
    const bool allowed = expected_normalized(bits);
    const auto input = state(1U, true, bits);
    require(validate(input) == allowed, "FP mode changed expression validation");
    const auto encoded = encode(input);
    require(encoded.has_value() == allowed, "FP mode changed expression encoding domain");
    if (encoded) { require(*encoded == packet(1, 1U, one, canonical(bits)), "FP mode changed bytes"); }
    const auto raw = packet(1, 1U, one, bits);
    const auto decoded = decode(raw);
    require(decoded.has_value() == allowed, "FP mode changed expression decoding domain");
    if (decoded) { expect_state(*decoded, 1U, true, canonical(bits)); }
    StateMailbox mailbox;
    const auto baseline = state(0x007fffffU, false, 2U);
    require(recall(mailbox, baseline), "FP baseline recall failed");
    require(recall(mailbox, input) == allowed, "FP recall domain changed");
    const auto gain = allowed ? 1U : 0x007fffffU;
    const auto expression = allowed ? canonical(bits) : 2U;
    expect_state(mailbox.snapshot(), gain, allowed, expression);
    const auto first = mailbox.begin_block();
    require(first.apply_recall && first.may_publish, "FP pending recall missing");
    expect_state(first.recalled, gain, allowed, expression);
    mailbox.abort_block(first);
    const auto retry = mailbox.begin_block();
    require(retry.apply_recall && retry.publication_token == first.publication_token,
            "FP abort changed full token");
    expect_state(retry.recalled, gain, allowed, expression);
    require(publish(mailbox, retry, input) == allowed, "FP publication domain changed");
    expect_state(mailbox.snapshot(), gain, allowed, expression);
    if (!allowed) { require(publish(mailbox, retry, baseline), "FP rejection damaged token"); }
    require(!mailbox.begin_block().apply_recall, "FP recall consumed twice");
}

struct RestoreEnvironment {
    std::fenv_t original{};
#if RHODES_EXPRESSION_TEST_MXCSR
    unsigned original_mxcsr = _mm_getcsr();
#endif
    RestoreEnvironment() { require(std::fegetenv(&original) == 0, "cannot capture FP environment"); }
    ~RestoreEnvironment() {
        (void)std::fesetenv(&original);
#if RHODES_EXPRESSION_TEST_MXCSR
        _mm_setcsr(original_mxcsr);
#endif
    }
};

#if RHODES_EXPRESSION_TEST_MXCSR
unsigned supported_mxcsr() {
    alignas(16) std::array<unsigned char, 512> saved{};
#if defined(_MSC_VER)
    _fxsave(saved.data());
#else
    asm volatile("fxsave %0" : "=m"(saved));
#endif
    std::uint32_t mask = 0;
    std::memcpy(&mask, saved.data() + 28, sizeof(mask));
    return mask;
}
#endif

void floating_point_modes() {
    constexpr unsigned ftz = 1U << 15U;
    constexpr unsigned daz = 1U << 6U;
    constexpr std::array<unsigned, 4> flush_modes{0U, ftz, daz, ftz | daz};
    constexpr std::array<int, 4> rounding_modes{FE_TONEAREST, FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO};
    constexpr std::array<std::uint32_t, 24> words{
        0U, negative_zero, 1U, 2U, 0x00012345U, 0x007fffffU, 0x00800000U,
        0x1fffffffU, 0x20000000U, 0x3f000000U, 0x3f7fffffU, one, 0x3f800001U,
        0x40000000U, 0x80000001U, 0x807fffffU, 0x80800000U, 0xbf000000U,
        0x7f800000U, 0xff800000U, 0x7f800001U, 0x7fc12345U, 0xff800001U, 0xffc12345U};
    std::size_t executed = 0;
    std::size_t skipped = 0;
    const int original_rounding = std::fegetround();
    const int original_flags = std::fetestexcept(FE_ALL_EXCEPT);
#if RHODES_EXPRESSION_TEST_MXCSR
    const unsigned original_mxcsr = _mm_getcsr();
    const unsigned supported = supported_mxcsr();
#endif
    for (const auto rounding : rounding_modes) {
        for (const auto flush : flush_modes) {
            RestoreEnvironment restore;
            if (std::fesetround(rounding) != 0) { ++skipped; continue; }
#if RHODES_EXPRESSION_TEST_MXCSR
            if ((flush & ~supported) != 0U) { ++skipped; continue; }
            constexpr unsigned status = 0x3fU;
            const auto requested = (_mm_getcsr() & ~(ftz | daz | status)) | flush | 0x1f80U;
            _mm_setcsr(requested);
            require(_mm_getcsr() == requested, "requested MXCSR mode unavailable");
#else
            if (flush != 0U) { ++skipped; continue; }
#endif
            require(std::feclearexcept(FE_ALL_EXCEPT) == 0, "cannot clear FP flags");
            // Test clean flags and existing sticky flags; state work must retain
            // both without raising an invalid exception for signaling NaNs.
            for (const bool sticky : {false, true}) {
                if (sticky) {
                    require(std::feraiseexcept(FE_INEXACT) == 0, "cannot seed sticky FP flag");
                }
                const auto flags_before = std::fetestexcept(FE_ALL_EXCEPT);
#if RHODES_EXPRESSION_TEST_MXCSR
                const auto mxcsr_before = _mm_getcsr();
#endif
                for (const auto word : words) { floating_point_probe(word); }
                mailbox_words();
                require(std::fegetround() == rounding, "state work changed FP rounding");
                require(std::fetestexcept(FE_ALL_EXCEPT) == flags_before, "state work changed FP flags");
#if RHODES_EXPRESSION_TEST_MXCSR
                require(_mm_getcsr() == mxcsr_before, "state work changed MXCSR controls/status");
#endif
            }
            ++executed;
        }
    }
    require(executed != 0U, "no FP mode executed");
    require(std::fegetround() == original_rounding &&
            std::fetestexcept(FE_ALL_EXCEPT) == original_flags, "FP environment was not restored");
#if RHODES_EXPRESSION_TEST_MXCSR
    require(_mm_getcsr() == original_mxcsr, "original MXCSR was not restored");
#endif
    std::cout << "FP modes: " << executed << " executed, " << skipped << " skipped\n";
}
} // namespace

int main(int argc, char** argv) {
    const std::string_view selected = argc > 1 ? argv[1] : "all";
    try {
        bool found = false;
        const auto run = [&](std::string_view name, auto test) {
            if (selected != "all" && selected != name) { return; }
            found = true;
            checks = 0;
            test();
            std::cout << "PASS " << name << " (" << checks << " always-active checks)\n";
        };
        run("defaults_and_goldens", defaults_and_goldens);
        run("codec_domains", codec_domains);
        run("malformed_packets", malformed_packets);
        run("model_isolation", model_isolation);
        run("mailbox_words", mailbox_words);
        run("mailbox_transactions", mailbox_transactions);
        run("floating_point_modes", floating_point_modes);
        require(found, "unknown expression state selector");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << selected << ": " << error.what() << '\n';
        return 1;
    }
}
