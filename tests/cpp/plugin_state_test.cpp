// Portable retained codec, floating-environment and transactional mailbox contract.
// Literal wire bytes and bit-field validity oracles are independent of pack/unpack.
#include "rhodes/plugin_state.hpp"

#include <algorithm>
#include <array>
#include <barrier>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>

#if defined(__SSE__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 1)
#define RHODES_PLUGIN_STATE_TEST_MXCSR 1
#include <xmmintrin.h>
#if defined(_MSC_VER)
#include <immintrin.h>
#endif
#else
#define RHODES_PLUGIN_STATE_TEST_MXCSR 0
#endif

#if defined(_MSC_VER)
#define RHODES_PLUGIN_STATE_TEST_NOINLINE __declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
#define RHODES_PLUGIN_STATE_TEST_NOINLINE __attribute__((noinline))
#else
#define RHODES_PLUGIN_STATE_TEST_NOINLINE
#endif

namespace rhodes::plugin_state_detail {
struct MailboxTestAccess {
    static void seed(PluginStateMailbox& mailbox, std::uint64_t guard) {
        mailbox.guard_.store(guard, std::memory_order_seq_cst);
    }
    static std::uint64_t guard(const PluginStateMailbox& mailbox) {
        return mailbox.guard_.load(std::memory_order_seq_cst);
    }
};
}

namespace {
using rhodes::PluginState;
using rhodes::PluginStateMailbox;
using Access = rhodes::plugin_state_detail::MailboxTestAccess;
using Words = std::array<std::uint32_t, 8>;
using Packet = std::array<std::uint8_t, 40>;
std::size_t checks = 0;

constexpr PluginState a{{0.125F, true, 0.75F}, {0.25F, 0.875F, 0.0625F, 0.625F, true}};
constexpr PluginState b{{0.875F, false, 0.125F}, {0.75F, 0.25F, 0.875F, 0.0625F, false}};
constexpr PluginState c{{0.0625F, true, 0.25F}, {0.875F, 0.625F, 0.75F, 0.125F, false}};
constexpr PluginState d{{0.75F, false, 0.875F}, {0.625F, 0.0625F, 0.125F, 0.25F, true}};
constexpr Packet golden{
    0x52, 0x48, 0x53, 0x54, 3, 0, 1, 0,
    0, 0, 0, 0x3e,       0, 0, 0x80, 0x3f,
    0, 0, 0x40, 0x3f,    0, 0, 0x80, 0x3e,
    0, 0, 0x60, 0x3f,    0, 0, 0x80, 0x3d,
    0, 0, 0x20, 0x3f,    0, 0, 0x80, 0x3f};
constexpr std::array<std::size_t, 6> float_words{0, 2, 3, 4, 5, 6};

void require(bool condition, std::string_view message) {
    ++checks;
    if (!condition) { throw std::runtime_error(std::string{message}); }
}

Words signature(PluginState state) {
    return {std::bit_cast<std::uint32_t>(state.instrument.monitoring_gain),
            state.instrument.sustain ? 0x3f800000U : 0U,
            std::bit_cast<std::uint32_t>(state.instrument.expression),
            std::bit_cast<std::uint32_t>(state.panel.bass),
            std::bit_cast<std::uint32_t>(state.panel.treble),
            std::bit_cast<std::uint32_t>(state.panel.intensity),
            std::bit_cast<std::uint32_t>(state.panel.speed),
            state.panel.vibrato ? 0x3f800000U : 0U};
}

bool same(PluginState left, PluginState right) { return signature(left) == signature(right); }

void put_word(Packet& packet, std::size_t word, std::uint32_t value) {
    for (std::size_t digit = 0; digit < 4; ++digit) {
        packet[8 + 4 * word + digit] = static_cast<std::uint8_t>(value % 256U);
        value /= 256U;
    }
}

PluginState with_control(PluginState state, std::size_t field, std::uint32_t bits) {
    const auto value = std::bit_cast<float>(bits);
    switch (field) {
    case 0: state.instrument.monitoring_gain = value; break;
    case 1: state.instrument.expression = value; break;
    case 2: state.panel.bass = value; break;
    case 3: state.panel.treble = value; break;
    case 4: state.panel.intensity = value; break;
    case 5: state.panel.speed = value; break;
    default: throw std::runtime_error("invalid test field");
    }
    return state;
}

// Independent sign/exponent oracle, with no floating comparison.
bool normalized(std::uint32_t bits) {
    const auto sign = bits >> 31U;
    const auto exponent = (bits >> 23U) & 255U;
    const auto fraction = bits & 0x7fffffU;
    return sign != 0 ? exponent == 0 && fraction == 0 :
        exponent < 127 || (exponent == 127 && fraction == 0);
}

void codec_contract() {
    require(rhodes::valid_front_panel_state({}) && rhodes::neutral_front_panel_state({}),
            "default panel helpers");
    for (const auto model : std::array<std::uint16_t, 3>{1, 2, 3}) {
        auto expected = golden;
        expected[6] = static_cast<std::uint8_t>(model);
        require(rhodes::encode_plugin_state_v3(a, model) == expected, "literal v3 bytes");
        const auto decoded = rhodes::decode_plugin_state(expected, model);
        require(decoded && same(*decoded, a), "literal v3 decode");
        const auto encoded = rhodes::encode_plugin_state(a, model);
        require(encoded && encoded->size == 40 && encoded->bytes == expected, "adaptive v3");

        PluginState legacy{{0.125F, true, 1.0F}, {}};
        auto short_state = rhodes::encode_plugin_state(legacy, model);
        const auto v1 = rhodes::encode_instrument_state(legacy.instrument, model);
        require(short_state && short_state->size == 16 &&
                std::equal(short_state->view().begin(), short_state->view().end(), v1->begin()),
                "adaptive v1 identity");
        auto restored = rhodes::decode_plugin_state(*v1, model);
        require(restored && same(*restored, legacy), "legacy v1 neutral panel");
        legacy.instrument.expression = 0.75F;
        short_state = rhodes::encode_plugin_state(legacy, model);
        const auto v2 = rhodes::encode_instrument_state_v2(legacy.instrument, model);
        require(short_state && short_state->size == 20 &&
                std::equal(short_state->view().begin(), short_state->view().end(), v2->begin()),
                "adaptive v2 identity");
        restored = rhodes::decode_plugin_state(*v2, model);
        require(restored && same(*restored, legacy), "legacy v2 neutral panel");
        require(!rhodes::decode_plugin_state(expected, model == 3 ? 1 : model + 1),
                "model mismatch rejected");
    }
    for (const auto model : std::array<std::uint16_t, 3>{0, 4, 65535}) {
        require(!rhodes::encode_plugin_state(a, model) &&
                !rhodes::encode_plugin_state_v3(a, model) &&
                !rhodes::decode_plugin_state(golden, model), "unsupported model rejected");
    }
    for (std::size_t field = 2; field < 6; ++field) {
        const auto changed = with_control({}, field, 0x3e800000U);
        const auto encoded = rhodes::encode_plugin_state(changed);
        require(!rhodes::neutral_front_panel_state(changed.panel) && encoded && encoded->size == 40,
                "inactive nondefault panel field must persist");
    }
    PluginState switched;
    switched.panel.vibrato = true;
    require(rhodes::encode_plugin_state(switched)->size == 40, "vibrato switch persists");
    for (std::size_t byte = 0; byte < 8; ++byte) {
        auto malformed = golden;
        malformed[byte] ^= 0x80U;
        require(!rhodes::decode_plugin_state(malformed), "malformed header rejected");
    }
    std::array<std::uint8_t, 64> storage{};
    std::copy(golden.begin(), golden.end(), storage.begin());
    for (std::size_t size = 0; size <= storage.size(); ++size) {
        require(rhodes::decode_plugin_state(std::span{storage}.first(size)).has_value() == (size == 40),
                "exact v3 length required");
    }
    for (const auto version : std::array<std::uint8_t, 5>{0, 1, 2, 4, 255}) {
        auto malformed = golden;
        malformed[4] = version;
        require(!rhodes::decode_plugin_state(malformed), "version/length disagreement rejected");
    }
}

RHODES_PLUGIN_STATE_TEST_NOINLINE bool validate(PluginState state) { return rhodes::valid_plugin_state(state); }
RHODES_PLUGIN_STATE_TEST_NOINLINE auto encode(PluginState state) { return rhodes::encode_plugin_state_v3(state); }
RHODES_PLUGIN_STATE_TEST_NOINLINE auto decode(const Packet& packet) { return rhodes::decode_plugin_state(packet); }
RHODES_PLUGIN_STATE_TEST_NOINLINE bool recall(PluginStateMailbox& box, PluginState state) {
    return box.request_recall(state);
}
RHODES_PLUGIN_STATE_TEST_NOINLINE bool publish(PluginStateMailbox& box,
                             const PluginStateMailbox::BlockToken& token, PluginState state) {
    return box.end_block(token, state);
}

void bit_probe(std::uint32_t source_bits) {
    volatile std::uint32_t runtime_bits = source_bits;
    const auto bits = runtime_bits;
    const bool allowed = normalized(bits);
    const auto wanted = bits == 0x80000000U ? 0U : bits;
    for (std::size_t field = 0; field < float_words.size(); ++field) {
        const auto state = with_control(a, field, bits);
        require(validate(state) == allowed, "full state validation domain");
        auto input = golden;
        put_word(input, float_words[field], bits);
        const auto unchanged = input;
        auto expected = golden;
        put_word(expected, float_words[field], wanted);
        const auto decoded = decode(input);
        require(input == unchanged && decoded.has_value() == allowed, "decode domain or mutation");
        const auto encoded = encode(state);
        require(encoded.has_value() == allowed, "encode domain");
        const auto adaptive = rhodes::encode_plugin_state(state);
        require(adaptive.has_value() == allowed, "adaptive validation domain");
        if (allowed) {
            require(encoded && *encoded == expected && encode(*decoded) == expected,
                    "canonical exact v3 round trip");
            require(signature(*decoded)[float_words[field]] == wanted, "subnormal/zero bit preservation");
        }
        PluginStateMailbox mailbox;
        require(recall(mailbox, b), "pending baseline setup");
        require(recall(mailbox, state) == allowed, "recall domain");
        auto retained = signature(allowed ? state : b);
        if (allowed) { retained[float_words[field]] = wanted; }
        require(signature(mailbox.snapshot()) == retained, "recall snapshot bits");
        const auto token = mailbox.begin_block();
        require(token.may_publish && token.apply_recall && signature(token.recalled) == retained,
                "complete pending recall");
        const auto before_abort = Access::guard(mailbox);
        mailbox.abort_block(token);
        require(Access::guard(mailbox) == before_abort, "abort is a no-op");
        const auto again = mailbox.begin_block();
        require(again.apply_recall && again.publication_token > token.publication_token,
                "begin retains pending and advances revision");
        require(publish(mailbox, again, state) == allowed, "publication domain");
        if (!allowed) { require(publish(mailbox, again, b), "invalid end preserves token"); }
        require(signature(mailbox.snapshot()) == retained && !mailbox.begin_block().apply_recall,
                "successful end clears pending with intact state");
    }
    const bool boolean = bits == 0 || bits == 0x80000000U || bits == 0x3f800000U;
    for (std::size_t word : {1U, 7U}) {
        auto input = golden;
        put_word(input, word, bits);
        const auto decoded = decode(input);
        require(decoded.has_value() == boolean, "boolean wire encoding must be exact");
        if (boolean) {
            require(signature(*decoded)[word] == (bits == 0x3f800000U ? bits : 0U),
                    "boolean signed zero canonicalization");
        }
    }
}

void fp_cases() {
    constexpr std::array<std::uint32_t, 24> edges{
        0U, 0x80000000U, 1U, 2U, 0x00012345U, 0x007fffffU,
        0x80000001U, 0x80000002U, 0x80012345U, 0x807fffffU,
        0x00800000U, 0x80800000U, 0x3e800000U, 0x3f000000U,
        0x3f7fffffU, 0x3f800000U, 0x3f800001U, 0x40000000U,
        0x7f800000U, 0xff800000U, 0x7fc12345U, 0x7f812345U,
        0xffc12345U, 0xff812345U};
    for (auto bits : edges) { bit_probe(bits); }
    std::uint32_t generator = 0x50da2f17U;
    for (std::size_t i = 0; i < 64; ++i) {
        generator ^= generator << 13U;
        generator ^= generator >> 17U;
        generator ^= generator << 5U;
        bit_probe(generator);
        bit_probe((generator & 0x7fffffU) | 1U);
        bit_probe(generator | 0x80000000U);
    }
    const auto negative_zero = with_control({}, 4, 0x80000000U);
    require(rhodes::neutral_front_panel_state(negative_zero.panel) &&
            rhodes::encode_plugin_state(negative_zero)->size == 16, "zero sign retains v1");
    const auto subnormal = with_control({}, 4, 1U);
    require(!rhodes::neutral_front_panel_state(subnormal.panel) &&
            rhodes::encode_plugin_state(subnormal)->size == 40, "DAZ must not select v1");
}

void floating_modes() {
#if RHODES_PLUGIN_STATE_TEST_MXCSR
    alignas(16) std::array<unsigned char, 512> saved{};
#if defined(_MSC_VER)
    _fxsave(saved.data());
#else
    asm volatile("fxsave %0" : "=m"(saved));
#endif
    std::uint32_t mask = 0;
    std::memcpy(&mask, saved.data() + 28, sizeof(mask));
    const auto original = _mm_getcsr();
    struct Restore { unsigned value; ~Restore() { _mm_setcsr(value); } } restore{original};
    for (unsigned controls : {0U, 1U << 15U, 1U << 6U, (1U << 15U) | (1U << 6U)}) {
        if ((controls & ~mask) != 0) { std::cout << "SKIP MXCSR " << controls << '\n'; continue; }
        const auto requested = (original & ~((1U << 15U) | (1U << 6U) | 0x3fU)) |
                               controls | 0x1f80U;
        _mm_setcsr(requested);
        require(_mm_getcsr() == requested, "requested MXCSR controls applied");
        fp_cases();
        require((_mm_getcsr() & 0x3fU) == 0, "state operations raised FP flags");
        _mm_setcsr(original);
        std::cout << "PASS FP mode " << controls << '\n';
    }
#else
    fp_cases();
    std::cout << "PASS native FP mode; FTZ/DAZ controls unavailable\n";
#endif
}

void transaction_contract() {
    PluginStateMailbox mailbox;
    require(same(mailbox.snapshot(), {}), "default complete snapshot");
    const auto first = mailbox.begin_block();
    require(first.may_publish && !first.apply_recall && first.publication_token == 4,
            "initial begin advances revision");
    require(mailbox.end_block(first, a), "initial publication");
    require(!mailbox.end_block(first, b), "duplicate token cannot publish");
    for (bool identical : {false, true}) {
        require(mailbox.request_recall(a), "ordered setup");
        const auto old = mailbox.begin_block();
        require(mailbox.request_recall(identical ? a : b), "newer recall");
        require(!mailbox.end_block(old, c), "stale end cannot overwrite identical recall");
        mailbox.abort_block(old);
        const auto latest = mailbox.begin_block();
        require(latest.apply_recall && same(latest.recalled, identical ? a : b), "new recall pending");
        require(mailbox.end_block(latest, latest.recalled), "new recall completes");
    }
    require(mailbox.request_recall(a), "ABA setup");
    const auto old = mailbox.begin_block();
    require(mailbox.request_recall(b) && mailbox.request_recall(a), "ABA recalls");
    require(!mailbox.end_block(old, d), "A-B-A cannot revive old token");
    const auto latest = mailbox.begin_block();
    require(latest.apply_recall && same(latest.recalled, a), "ABA pending selected");
    require(mailbox.end_block(latest, a), "ABA completion");
    const auto token = mailbox.begin_block();
    Access::seed(mailbox, token.publication_token | 1U);
    const auto busy = mailbox.begin_block();
    require(!busy.may_publish && !busy.apply_recall && !mailbox.end_block(token, b),
            "audio defers immediately from busy guard");
    require(Access::guard(mailbox) == (token.publication_token | 1U), "busy failure mutates nothing");
    Access::seed(mailbox, token.publication_token + 4U);
    require(same(mailbox.snapshot(), a), "busy rejection preserves payload");
    require(!mailbox.end_block({}, b), "default token rejected");
}

void exhaustion_contract() {
    constexpr auto maximum = ~std::uint64_t{3};
    {
        PluginStateMailbox mailbox;
        Access::seed(mailbox, maximum - 4U);
        require(mailbox.request_recall(a) && Access::guard(mailbox) == (maximum | 2U),
                "last recall reaches maximum revision");
        require(!mailbox.request_recall(b) && !mailbox.begin_block().may_publish &&
                same(mailbox.snapshot(), a) && Access::guard(mailbox) == (maximum | 2U),
                "exhausted recall and begin fail closed");
    }
    {
        PluginStateMailbox mailbox;
        require(mailbox.request_recall(a), "exhaustion pending setup");
        Access::seed(mailbox, (maximum - 8U) | 2U);
        const auto token = mailbox.begin_block();
        require(token.may_publish && token.apply_recall &&
                token.publication_token == ((maximum - 4U) | 2U), "penultimate begin");
        require(mailbox.end_block(token, b) && Access::guard(mailbox) == maximum,
                "last end clears pending without wrapping");
        require(!mailbox.end_block(token, c) && !mailbox.request_recall(c) &&
                !mailbox.begin_block().may_publish && same(mailbox.snapshot(), b),
                "exhausted publication preserves final payload");
    }
    {
        PluginStateMailbox mailbox;
        require(mailbox.request_recall(a), "last begin pending setup");
        Access::seed(mailbox, (maximum - 4U) | 2U);
        const auto token = mailbox.begin_block();
        require(token.may_publish && token.apply_recall && token.publication_token == (maximum | 2U),
                "last begin reaches maximum revision");
        require(!mailbox.end_block(token, b), "end cannot wrap revision");
        mailbox.abort_block(token);
        require(Access::guard(mailbox) == (maximum | 2U) && same(mailbox.snapshot(), a),
                "exhaustion and abort retain pending payload");
    }
}

bool coherent(PluginState state) {
    for (const auto& expected : std::array{PluginState{}, a, b, c, d}) {
        if (same(state, expected)) { return true; }
    }
    return false;
}

void concurrent_contract() {
    PluginStateMailbox mailbox;
    std::barrier start{5};
    constexpr std::size_t iterations = 50000;
    std::array<std::size_t, 4> errors{};
    std::size_t published = 0;
    std::size_t deferred = 0;
    std::jthread writer_one([&] {
        start.arrive_and_wait();
        for (std::size_t i = 0; i < iterations; ++i) {
            if (!mailbox.request_recall(i % 2 == 0 ? a : b)) { ++errors[0]; }
        }
    });
    std::jthread writer_two([&] {
        start.arrive_and_wait();
        for (std::size_t i = 0; i < iterations; ++i) {
            if (!mailbox.request_recall(i % 2 == 0 ? c : d)) { ++errors[1]; }
        }
    });
    std::jthread audio([&] {
        start.arrive_and_wait();
        for (std::size_t i = 0; i < iterations; ++i) {
            const auto token = mailbox.begin_block();
            if (!token.may_publish) { ++deferred; }
            else if (!coherent(token.recalled)) { ++errors[2]; }
            if (i % 7 == 0) { mailbox.abort_block(token); }
            else if (mailbox.end_block(token, i % 2 == 0 ? a : d)) { ++published; }
        }
    });
    std::jthread observer([&] {
        start.arrive_and_wait();
        for (std::size_t i = 0; i < iterations; ++i) {
            if (!coherent(mailbox.snapshot())) { ++errors[3]; }
        }
    });
    start.arrive_and_wait();
    writer_one.join(); writer_two.join(); audio.join(); observer.join();
    require(errors == std::array<std::size_t, 4>{}, "torn state or rejected producer in stress");
    require(mailbox.request_recall(c), "final recall after stress");
    const auto old = mailbox.begin_block();
    std::barrier phases{2};
    bool rejected = false;
    std::jthread stale_audio([&] {
        phases.arrive_and_wait();
        phases.arrive_and_wait();
        rejected = !mailbox.end_block(old, a);
    });
    phases.arrive_and_wait();
    const bool committed = mailbox.request_recall(c);
    phases.arrive_and_wait();
    stale_audio.join();
    require(committed && rejected, "identical concurrent recall rejects stale end");
    const auto final = mailbox.begin_block();
    require(final.apply_recall && same(final.recalled, c) && mailbox.end_block(final, c) &&
            !mailbox.begin_block().apply_recall, "final recall consumed once");
    std::cout << "PASS concurrency: 2 producers x " << iterations << ", audio published="
              << published << ", deferred=" << deferred << ", snapshots=" << iterations << '\n';
}
}

int main(int argc, char** argv) {
    try {
        if (argc > 2) { throw std::runtime_error("expected at most one fixture name"); }
        const std::string_view selected = argc > 1 ? argv[1] : "all";
        bool found = false;
        const auto run = [&](std::string_view name, auto test) {
            if (selected == "all" || selected == name) {
                test();
                found = true;
                std::cout << "PASS " << name << '\n';
            }
        };
        run("codec", codec_contract);
        run("floating_modes", floating_modes);
        run("transactions", transaction_contract);
        run("exhaustion", exhaustion_contract);
        run("concurrency", concurrent_contract);
        if (!found) { throw std::runtime_error("unknown fixture: " + std::string{selected}); }
        std::cout << "PASS total checks=" << checks << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}
