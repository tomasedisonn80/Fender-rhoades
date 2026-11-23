#pragma once

#include <array>
#include <atomic>
#include <bit>
#include <climits>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>

namespace rhodes {

static_assert(CHAR_BIT == 8 && sizeof(float) == 4 && std::numeric_limits<float>::is_iec559);
static_assert(std::atomic<std::uint64_t>::is_always_lock_free,
              "The experimental host adapter requires lock-free 64-bit atomics");

struct InstrumentState {
    float monitoring_gain = 0.5F; // Digital gain = 32 * this value, not calibration.
    bool sustain = false;
    friend bool operator==(const InstrumentState&, const InstrumentState&) = default;
};

inline constexpr std::size_t instrument_state_bytes = 16;
using EncodedInstrumentState = std::array<std::uint8_t, instrument_state_bytes>;

[[nodiscard]] inline bool valid_instrument_state(InstrumentState state) noexcept {
    return std::isfinite(state.monitoring_gain) && state.monitoring_gain >= 0.0F &&
           state.monitoring_gain <= 1.0F;
}

namespace state_detail {
inline std::uint32_t read_u32(std::span<const std::uint8_t> bytes, std::size_t at) noexcept {
    return static_cast<std::uint32_t>(bytes[at]) |
           (static_cast<std::uint32_t>(bytes[at + 1]) << 8U) |
           (static_cast<std::uint32_t>(bytes[at + 2]) << 16U) |
           (static_cast<std::uint32_t>(bytes[at + 3]) << 24U);
}
inline void write_u32(EncodedInstrumentState& bytes, std::size_t at, std::uint32_t word) noexcept {
    for (std::size_t i = 0; i < 4; ++i) {
        bytes[at + i] = static_cast<std::uint8_t>(word >> (8U * i));
    }
}
inline std::uint32_t gain_bits(float gain) noexcept {
    return std::bit_cast<std::uint32_t>(gain == 0.0F ? 0.0F : gain);
}
inline std::uint64_t pack(InstrumentState state) noexcept {
    return static_cast<std::uint64_t>(gain_bits(state.monitoring_gain)) |
           (static_cast<std::uint64_t>(state.sustain) << 32U);
}
inline InstrumentState unpack(std::uint64_t packed) noexcept {
    return {std::bit_cast<float>(static_cast<std::uint32_t>(packed)),
            ((packed >> 32U) & 1U) != 0};
}
} // namespace state_detail

[[nodiscard]] inline std::optional<EncodedInstrumentState>
encode_instrument_state(InstrumentState state) noexcept {
    if (!valid_instrument_state(state)) { return std::nullopt; }
    EncodedInstrumentState bytes{'R', 'H', 'S', 'T', 1, 0, 1, 0};
    state_detail::write_u32(bytes, 8, state_detail::gain_bits(state.monitoring_gain));
    state_detail::write_u32(bytes, 12, std::bit_cast<std::uint32_t>(state.sustain ? 1.0F : 0.0F));
    return bytes;
}

[[nodiscard]] inline std::optional<InstrumentState>
decode_instrument_state(std::span<const std::uint8_t> bytes) noexcept {
    if (bytes.size() != instrument_state_bytes || bytes[0] != 'R' || bytes[1] != 'H' ||
        bytes[2] != 'S' || bytes[3] != 'T' || bytes[4] != 1 || bytes[5] != 0 ||
        bytes[6] != 1 || bytes[7] != 0) {
        return std::nullopt;
    }
    const float gain = std::bit_cast<float>(state_detail::read_u32(bytes, 8));
    const float pedal = std::bit_cast<float>(state_detail::read_u32(bytes, 12));
    InstrumentState state{gain == 0.0F ? 0.0F : gain, pedal == 1.0F};
    if (!valid_instrument_state(state) || (pedal != 0.0F && pedal != 1.0F)) {
        return std::nullopt;
    }
    return state;
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
