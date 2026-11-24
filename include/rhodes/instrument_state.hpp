#pragma once

#include <array>
#include <atomic>
#include <bit>
#include <climits>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>

namespace rhodes {

static_assert(CHAR_BIT == 8 && sizeof(float) == 4 && std::numeric_limits<float>::is_iec559 &&
              std::numeric_limits<float>::radix == 2 && std::numeric_limits<float>::digits == 24);
static_assert(std::atomic<std::uint64_t>::is_always_lock_free,
              "The experimental host adapter requires lock-free 64-bit atomics");

struct InstrumentState {
    float monitoring_gain = 0.5F; // Digital gain = 32 * this value, not calibration.
    bool sustain = false;
    float expression = 1.0F; // Global digital output multiplier, independent of gain.
    friend bool operator==(const InstrumentState&, const InstrumentState&) = default;
};

// Keep the legacy format and API explicit: exporting it must not lose expression.
inline constexpr std::size_t instrument_state_bytes = 16;
using EncodedInstrumentState = std::array<std::uint8_t, instrument_state_bytes>;
inline constexpr std::size_t instrument_state_v2_bytes = 20;
using EncodedInstrumentStateV2 = std::array<std::uint8_t, instrument_state_v2_bytes>;

namespace state_detail {
inline constexpr bool valid_model(std::uint16_t model) noexcept {
    return model == 1 || model == 2 || model == 3;
}
// Positive binary32 encodings are ordered from +0 through +1. The only
// permitted sign-bit encoding is -0. Integer tests keep the wire protocol
// independent of a host's denormals-are-zero/flush-to-zero arithmetic mode.
inline constexpr bool valid_gain_bits(std::uint32_t bits) noexcept {
    return bits <= 0x3f800000U || bits == 0x80000000U;
}
inline constexpr std::uint32_t canonical_gain_bits(std::uint32_t bits) noexcept {
    return (bits & 0x7fffffffU) == 0U ? 0U : bits;
}
inline std::uint32_t read_u32(std::span<const std::uint8_t> bytes, std::size_t at) noexcept {
    return static_cast<std::uint32_t>(bytes[at]) |
           (static_cast<std::uint32_t>(bytes[at + 1]) << 8U) |
           (static_cast<std::uint32_t>(bytes[at + 2]) << 16U) |
           (static_cast<std::uint32_t>(bytes[at + 3]) << 24U);
}
inline void write_u32(std::span<std::uint8_t> bytes, std::size_t at, std::uint32_t word) noexcept {
    for (std::size_t i = 0; i < 4; ++i) {
        bytes[at + i] = static_cast<std::uint8_t>(word >> (8U * i));
    }
}
inline std::uint32_t gain_bits(float gain) noexcept {
    return canonical_gain_bits(std::bit_cast<std::uint32_t>(gain));
}
inline std::uint64_t pack(InstrumentState state) noexcept {
    // Valid normalized binary32 expression uses at most thirty bits. Cast before
    // shifting: unity reaches bit 63. Bit 33 remains reserved for pending recall.
    return static_cast<std::uint64_t>(gain_bits(state.monitoring_gain)) |
           (static_cast<std::uint64_t>(state.sustain) << 32U) |
           (static_cast<std::uint64_t>(gain_bits(state.expression)) << 34U);
}
inline InstrumentState unpack(std::uint64_t packed) noexcept {
    return {std::bit_cast<float>(static_cast<std::uint32_t>(packed)),
            ((packed >> 32U) & 1U) != 0,
            std::bit_cast<float>(static_cast<std::uint32_t>(packed >> 34U))};
}
} // namespace state_detail

[[nodiscard]] inline bool valid_instrument_state(InstrumentState state) noexcept {
    return state_detail::valid_gain_bits(std::bit_cast<std::uint32_t>(state.monitoring_gain)) &&
           state_detail::valid_gain_bits(std::bit_cast<std::uint32_t>(state.expression));
}

[[nodiscard]] inline std::optional<EncodedInstrumentState>
encode_instrument_state(InstrumentState state, std::uint16_t model = 1) noexcept {
    if (!state_detail::valid_model(model) || !valid_instrument_state(state) ||
        state_detail::gain_bits(state.expression) != 0x3f800000U) {
        return std::nullopt;
    }
    // RHST wire version 1 is shared by the direct (1), filtered (2), and
    // quadrature (3) instruments. The selected immutable model occupies
    // little-endian bytes 6–7.
    EncodedInstrumentState bytes{'R', 'H', 'S', 'T', 1, 0,
        static_cast<std::uint8_t>(model), static_cast<std::uint8_t>(model >> 8U)};
    state_detail::write_u32(bytes, 8, state_detail::gain_bits(state.monitoring_gain));
    state_detail::write_u32(bytes, 12, std::bit_cast<std::uint32_t>(state.sustain ? 1.0F : 0.0F));
    return bytes;
}

[[nodiscard]] inline std::optional<EncodedInstrumentStateV2>
encode_instrument_state_v2(InstrumentState state, std::uint16_t model = 1) noexcept {
    if (!state_detail::valid_model(model) || !valid_instrument_state(state)) {
        return std::nullopt;
    }
    EncodedInstrumentStateV2 bytes{'R', 'H', 'S', 'T', 2, 0,
        static_cast<std::uint8_t>(model), static_cast<std::uint8_t>(model >> 8U)};
    state_detail::write_u32(bytes, 8, state_detail::gain_bits(state.monitoring_gain));
    state_detail::write_u32(bytes, 12, std::bit_cast<std::uint32_t>(state.sustain ? 1.0F : 0.0F));
    state_detail::write_u32(bytes, 16, state_detail::gain_bits(state.expression));
    return bytes;
}

[[nodiscard]] inline std::optional<InstrumentState>
decode_instrument_state(std::span<const std::uint8_t> bytes,
                        std::uint16_t model = 1) noexcept {
    if (!state_detail::valid_model(model) ||
        (bytes.size() != instrument_state_bytes && bytes.size() != instrument_state_v2_bytes) ||
        bytes[0] != 'R' || bytes[1] != 'H' ||
        bytes[2] != 'S' || bytes[3] != 'T' || bytes[5] != 0 ||
        !((bytes[4] == 1 && bytes.size() == instrument_state_bytes) ||
          (bytes[4] == 2 && bytes.size() == instrument_state_v2_bytes)) ||
        bytes[6] != static_cast<std::uint8_t>(model) || bytes[7] != model >> 8U) {
        return std::nullopt;
    }
    const auto gain = state_detail::read_u32(bytes, 8);
    const auto pedal = state_detail::read_u32(bytes, 12);
    const auto expression = bytes[4] == 2 ? state_detail::read_u32(bytes, 16) : 0x3f800000U;
    if (!state_detail::valid_gain_bits(gain) || !state_detail::valid_gain_bits(expression) ||
        (pedal != 0U && pedal != 0x80000000U && pedal != 0x3f800000U)) {
        return std::nullopt;
    }
    return InstrumentState{std::bit_cast<float>(state_detail::canonical_gain_bits(gain)),
                           pedal == 0x3f800000U,
                           std::bit_cast<float>(state_detail::canonical_gain_bits(expression))};
}

// UI writes configuration recalls; one audio owner consumes at block boundaries.
// No streams or mechanics live here. A recall is last-writer-wins before consume.
class StateMailbox {
public:
    struct BlockToken {
        std::uint64_t publication_token = 0;
        InstrumentState recalled{};
        bool apply_recall = false;
        bool may_publish = false;
    };

    [[nodiscard]] bool request_recall(InstrumentState state) noexcept {
        if (!valid_instrument_state(state)) { return false; }
        packed_.store(state_detail::pack(state) | pending_flag, std::memory_order_release);
        return true;
    }

    [[nodiscard]] InstrumentState snapshot() const noexcept {
        return state_detail::unpack(packed_.load(std::memory_order_acquire));
    }

    [[nodiscard]] BlockToken begin_block() noexcept {
        std::uint64_t observed = packed_.load(std::memory_order_acquire);
        if ((observed & pending_flag) == 0) {
            return {observed, {}, false, true};
        }
        const std::uint64_t cleared = observed & ~pending_flag;
        const InstrumentState state = state_detail::unpack(observed);
        if (!packed_.compare_exchange_strong(observed, cleared,
                std::memory_order_acq_rel, std::memory_order_acquire)) {
            return {}; // A newer UI write wins; defer without an audio retry loop.
        }
        return {cleared, state, true, true};
    }

    [[nodiscard]] bool end_block(const BlockToken& token, InstrumentState state) noexcept {
        if (!token.may_publish || !valid_instrument_state(state)) { return false; }
        std::uint64_t expected = token.publication_token;
        return packed_.compare_exchange_strong(expected, state_detail::pack(state),
            std::memory_order_release, std::memory_order_relaxed);
    }

    // Restore an unconsumed recall after ownership preflight rejects, provided
    // a newer UI write has not replaced it. No mechanics may be mutated first.
    void abort_block(const BlockToken& token) noexcept {
        if (!token.may_publish || !token.apply_recall) { return; }
        std::uint64_t expected = token.publication_token;
        (void)packed_.compare_exchange_strong(expected, token.publication_token | pending_flag,
            std::memory_order_release, std::memory_order_relaxed);
    }

private:
    static constexpr std::uint64_t pending_flag = std::uint64_t{1} << 33U;
    std::atomic<std::uint64_t> packed_{state_detail::pack({})};
};

} // namespace rhodes
