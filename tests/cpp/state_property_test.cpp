#include "rhodes/instrument_state.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace {

using Bytes = std::array<std::uint8_t, 16>;
constexpr std::array<std::uint8_t, 8> header{0x52, 0x48, 0x53, 0x54, 1, 0, 1, 0};
constexpr std::uint32_t negative_zero = 0x80000000U;
constexpr std::uint32_t one = 0x3f800000U;
std::uint64_t checks = 0;

// These checks remain active in Release/NDEBUG builds. Expected domains use
// the ordered IEEE binary32 representation, not the implementation's floating
// comparisons or its private byte/packing helpers.
void require(bool condition, std::string_view message) {
    ++checks;
    if (!condition) { throw std::runtime_error(std::string{message}); }
}

void require_word(bool condition, std::string_view message, std::uint32_t word) {
    ++checks;
    if (!condition) {
        throw std::runtime_error(std::string{message} + " (binary32 word " +
                                 std::to_string(word) + ")");
    }
}

constexpr bool expected_gain(std::uint32_t word) noexcept {
    // Positive binary32 encodings are monotonically ordered. 0..1 includes
    // all positive subnormals; only negative zero is valid on the sign-bit side.
    return word <= one || word == negative_zero;
}

constexpr bool expected_pedal(std::uint32_t word) noexcept {
    return word == 0U || word == negative_zero || word == one;
}

constexpr std::uint32_t canonical_gain(std::uint32_t word) noexcept {
    return word == negative_zero ? 0U : word;
}

// Manual little-endian construction uses arithmetic base-256 digits; it never
// calls the codec's byte helpers, encoder, or native-memory serialization.
Bytes packet(std::uint32_t gain, std::uint32_t pedal) {
    Bytes bytes{};
    std::copy(header.begin(), header.end(), bytes.begin());
    for (std::size_t digit = 0; digit < 4; ++digit) {
        bytes[8 + digit] = static_cast<std::uint8_t>(gain % 256U);
        bytes[12 + digit] = static_cast<std::uint8_t>(pedal % 256U);
        gain /= 256U;
        pedal /= 256U;
    }
    return bytes;
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
    // Fixed unsigned arithmetic makes this sequence independent of a standard
    // library's random-distribution implementation. Repeated words are allowed.
    std::uint32_t generator = 0x5a17c0deU;
    for (std::size_t index = 0; index < 16384; ++index) {
        generator ^= generator << 13U;
        generator ^= generator >> 17U;
        generator ^= generator << 5U;
        visit(generator);
    }
    return count;
}

void gain_bit_domain() {
    constexpr Bytes literal{
        0x52, 0x48, 0x53, 0x54, 1, 0, 1, 0,
        0xab, 0xaa, 0xaa, 0x3e, 0, 0, 0x80, 0x3f};
    require(packet(0x3eaaaaabU, one) == literal, "manual wire fixture is not little endian");
    const auto count = patterns([](std::uint32_t word) {
        const bool accepted = expected_gain(word);
        const float gain = std::bit_cast<float>(word);
        for (const bool sustain : {false, true}) {
            const auto raw = packet(word, sustain ? one : 0U);
            const auto decoded = rhodes::decode_instrument_state(raw);
            require_word(decoded.has_value() == accepted, "gain decode domain mismatch", word);
            const auto encoded = rhodes::encode_instrument_state({gain, sustain});
            require_word(encoded.has_value() == accepted, "gain encode domain mismatch", word);
            if (!accepted) { continue; }
            const auto canonical = packet(canonical_gain(word), sustain ? one : 0U);
            require_word(std::bit_cast<std::uint32_t>(decoded->monitoring_gain) == canonical_gain(word),
                         "gain decode changed payload or failed zero canonicalization", word);
            require_word(decoded->sustain == sustain, "gain payload changed sustain", word);
            require_word(*encoded == canonical, "encoder differs from manual wire bytes", word);
            require_word(rhodes::encode_instrument_state(*decoded) == canonical,
                         "decoded state did not encode canonically", word);
            const auto again = rhodes::decode_instrument_state(canonical);
            require_word(again.has_value() &&
                         std::bit_cast<std::uint32_t>(again->monitoring_gain) == canonical_gain(word) &&
                         again->sustain == sustain, "canonical round trip changed state", word);
        }
    });
    require(count == 19474, "gain pattern count changed unintentionally");
}

void pedal_bit_domain() {
    const auto count = patterns([](std::uint32_t word) {
        const auto decoded = rhodes::decode_instrument_state(packet(0x3eaaaaabU, word));
        require_word(decoded.has_value() == expected_pedal(word), "pedal decode domain mismatch", word);
        if (!decoded) { return; }
        require_word(std::bit_cast<std::uint32_t>(decoded->monitoring_gain) == 0x3eaaaaabU,
                     "pedal payload changed gain", word);
        require_word(decoded->sustain == (word == one), "pedal decoded incorrectly", word);
        require_word(rhodes::encode_instrument_state(*decoded) ==
                     packet(0x3eaaaaabU, word == one ? one : 0U),
                     "pedal did not round trip to canonical exact zero/one", word);
    });
    require(count == 19474, "pedal pattern count changed unintentionally");
    const auto both_negative_zero = rhodes::decode_instrument_state(packet(negative_zero, negative_zero));
    require(both_negative_zero.has_value() &&
            rhodes::encode_instrument_state(*both_negative_zero) == packet(0U, 0U),
            "simultaneous negative zeros are not canonical");
}

void header_lengths() {
    const auto valid = packet(0x3eaaaaabU, one);
    require(!rhodes::decode_instrument_state(std::span<const std::uint8_t>{}),
            "empty null span was accepted");
    std::size_t mutated = 0;
    for (std::size_t at = 0; at < header.size(); ++at) {
        for (unsigned int value = 0; value < 256U; ++value) {
            if (value == header[at]) { continue; }
            auto changed = valid;
            changed[at] = static_cast<std::uint8_t>(value);
            require(!rhodes::decode_instrument_state(changed),
                    "altered magic/version/model byte was accepted");
            ++mutated;
        }
    }
    require(mutated == 2040, "header mutation count changed unintentionally");
    for (std::size_t alignment = 0; alignment < 8; ++alignment) {
        std::array<std::uint8_t, 72> storage{};
        storage.fill(0xa5U);
        std::copy(valid.begin(), valid.end(), storage.begin() + static_cast<std::ptrdiff_t>(alignment));
        const auto before = storage;
        for (std::size_t length = 0; length <= 64; ++length) {
            const auto decoded = rhodes::decode_instrument_state(
                std::span<const std::uint8_t>{storage.data() + alignment, length});
            require(decoded.has_value() == (length == 16), "unaligned length contract mismatch");
            if (decoded) {
                require(std::bit_cast<std::uint32_t>(decoded->monitoring_gain) == 0x3eaaaaabU &&
                        decoded->sustain, "unaligned exact-length payload changed");
            }
        }
        require(storage == before, "decoder changed caller storage");
    }
}

void invalid_recall() {
    rhodes::StateMailbox mailbox;
    const rhodes::InstrumentState baseline{0.375F, true};
    require(mailbox.request_recall(baseline), "valid baseline recall rejected");
    auto token = mailbox.begin_block();
    require(token.apply_recall && mailbox.end_block(token, baseline), "baseline installation failed");
    std::size_t invalid = 0;
    patterns([&](std::uint32_t word) {
        if (expected_gain(word)) { return; }
        const bool pending = (++invalid % 2U) == 0U;
        if (pending) { require(mailbox.request_recall(baseline), "pending baseline rejected"); }
        const rhodes::InstrumentState bad{std::bit_cast<float>(word), false};
        require_word(!mailbox.request_recall(bad), "invalid gain entered mailbox", word);
        require_word(mailbox.snapshot() == baseline, "invalid recall changed coherent snapshot", word);
        token = mailbox.begin_block();
        require_word(token.may_publish && token.apply_recall == pending,
                     "invalid recall changed pending status", word);
        if (pending) { require(token.recalled == baseline, "invalid recall replaced pending state"); }
        require_word(!mailbox.end_block(token, bad), "invalid audio publication accepted", word);
        require_word(mailbox.snapshot() == baseline, "invalid publication changed snapshot", word);
        require(mailbox.end_block(token, baseline), "rejected publication damaged valid token");
        require(!mailbox.begin_block().apply_recall, "invalid request left a phantom recall");
    });
    require(invalid > 14000, "invalid recall corpus unexpectedly small");
    require(mailbox.request_recall({std::bit_cast<float>(negative_zero), false}),
            "negative-zero recall rejected");
    require(std::bit_cast<std::uint32_t>(mailbox.snapshot().monitoring_gain) == 0U,
            "mailbox failed negative-zero canonicalization");
    token = mailbox.begin_block();
    require(token.apply_recall && std::bit_cast<std::uint32_t>(token.recalled.monitoring_gain) == 0U &&
            !token.recalled.sustain, "negative-zero recall consumed incorrectly");
}

using Test = void (*)();
constexpr std::array<std::pair<std::string_view, Test>, 4> tests{{
    {"gain_bit_domain", gain_bit_domain}, {"pedal_bit_domain", pedal_bit_domain},
    {"header_lengths", header_lengths}, {"invalid_recall", invalid_recall}
}};

} // namespace

int main(int argc, char** argv) {
    const std::string_view selected = argc > 1 ? std::string_view{argv[1]} : "all";
    bool found = false;
    try {
        for (const auto& [name, test] : tests) {
            if (selected != "all" && selected != name) { continue; }
            found = true;
            checks = 0;
            test();
            std::cout << "PASS " << name << " (" << checks << " always-active checks)\n";
        }
        require(found, "unknown state property selector");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << selected << ": " << error.what() << '\n';
        return 1;
    }
}
