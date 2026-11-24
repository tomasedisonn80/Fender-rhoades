#pragma once

#include "rhodes/front_panel_state.hpp"
#include "rhodes/instrument_state.hpp"

#include <array>
#include <atomic>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>

namespace rhodes {

struct PluginState {
    InstrumentState instrument{};
    FrontPanelState panel{};
    friend bool operator==(const PluginState&, const PluginState&) = default;
};

[[nodiscard]] inline bool valid_plugin_state(PluginState state) noexcept {
    return valid_instrument_state(state.instrument) && valid_front_panel_state(state.panel);
}

inline constexpr std::size_t plugin_state_v3_bytes = 40;
using EncodedPluginStateV3 = std::array<std::uint8_t, plugin_state_v3_bytes>;

// Adaptive encoding owns fixed storage; only the first size bytes belong to
// the packet. No allocation is needed for any of the three wire versions.
struct EncodedPluginState {
    EncodedPluginStateV3 bytes{};
    std::size_t size = 0;

    [[nodiscard]] std::span<std::uint8_t> view() noexcept {
        return std::span{bytes}.first(size);
    }
    [[nodiscard]] std::span<const std::uint8_t> view() const noexcept {
        return std::span{bytes}.first(size);
    }
};

namespace plugin_state_detail {
inline constexpr bool valid_boolean_bits(std::uint32_t bits) noexcept {
    return bits == 0U || bits == 0x80000000U || bits == 0x3f800000U;
}

inline constexpr std::size_t word_count = 8;
using Words = std::array<std::uint32_t, word_count>;

inline Words pack(PluginState state) noexcept {
    return {state_detail::gain_bits(state.instrument.monitoring_gain),
            state.instrument.sustain ? 0x3f800000U : 0U,
            state_detail::gain_bits(state.instrument.expression),
            front_panel_detail::control_bits(state.panel.bass),
            front_panel_detail::control_bits(state.panel.treble),
            front_panel_detail::control_bits(state.panel.intensity),
            front_panel_detail::control_bits(state.panel.speed),
            state.panel.vibrato ? 0x3f800000U : 0U};
}

inline PluginState unpack(const Words& words) noexcept {
    return {{std::bit_cast<float>(state_detail::canonical_gain_bits(words[0])),
             words[1] == 0x3f800000U,
             std::bit_cast<float>(state_detail::canonical_gain_bits(words[2]))},
            {std::bit_cast<float>(front_panel_detail::canonical_control_bits(words[3])),
             std::bit_cast<float>(front_panel_detail::canonical_control_bits(words[4])),
             std::bit_cast<float>(front_panel_detail::canonical_control_bits(words[5])),
             std::bit_cast<float>(front_panel_detail::canonical_control_bits(words[6])),
             words[7] == 0x3f800000U}};
}

inline bool valid_words(const Words& words) noexcept {
    return state_detail::valid_gain_bits(words[0]) && valid_boolean_bits(words[1]) &&
           state_detail::valid_gain_bits(words[2]) &&
           front_panel_detail::valid_control_bits(words[3]) &&
           front_panel_detail::valid_control_bits(words[4]) &&
           front_panel_detail::valid_control_bits(words[5]) &&
           front_panel_detail::valid_control_bits(words[6]) && valid_boolean_bits(words[7]);
}

// Only standalone tests define this friend to seed exhaustion or writer-busy
// cases with no concurrent mailbox users. Production has no reset/test API.
struct MailboxTestAccess;
} // namespace plugin_state_detail

[[nodiscard]] inline std::optional<EncodedPluginStateV3>
encode_plugin_state_v3(PluginState state, std::uint16_t model = 1) noexcept {
    if (!state_detail::valid_model(model) || !valid_plugin_state(state)) {
        return std::nullopt;
    }
    // RHST v3 retains the v2 instrument layout at bytes 8..19, then adds
    // bass, treble, intensity, speed and exact binary32 vibrato at 20..39.
    EncodedPluginStateV3 bytes{'R', 'H', 'S', 'T', 3, 0,
        static_cast<std::uint8_t>(model), static_cast<std::uint8_t>(model >> 8U)};
    const auto words = plugin_state_detail::pack(state);
    for (std::size_t i = 0; i < words.size(); ++i) {
        state_detail::write_u32(bytes, 8 + 4 * i, words[i]);
    }
    return bytes;
}

[[nodiscard]] inline std::optional<EncodedPluginState>
encode_plugin_state(PluginState state, std::uint16_t model = 1) noexcept {
    if (!state_detail::valid_model(model) || !valid_plugin_state(state)) {
        return std::nullopt;
    }
    EncodedPluginState encoded;
    if (neutral_front_panel_state(state.panel)) {
        if (state_detail::gain_bits(state.instrument.expression) == 0x3f800000U) {
            const auto legacy = encode_instrument_state(state.instrument, model);
            for (std::size_t i = 0; i < legacy->size(); ++i) { encoded.bytes[i] = (*legacy)[i]; }
            encoded.size = instrument_state_bytes;
        } else {
            const auto legacy = encode_instrument_state_v2(state.instrument, model);
            for (std::size_t i = 0; i < legacy->size(); ++i) { encoded.bytes[i] = (*legacy)[i]; }
            encoded.size = instrument_state_v2_bytes;
        }
    } else {
        encoded.bytes = *encode_plugin_state_v3(state, model);
        encoded.size = plugin_state_v3_bytes;
    }
    return encoded;
}

[[nodiscard]] inline std::optional<PluginState>
decode_plugin_state(std::span<const std::uint8_t> bytes, std::uint16_t model = 1) noexcept {
    if (bytes.size() == instrument_state_bytes || bytes.size() == instrument_state_v2_bytes) {
        const auto legacy = decode_instrument_state(bytes, model);
        return legacy ? std::optional<PluginState>{PluginState{*legacy, {}}} : std::nullopt;
    }
    if (!state_detail::valid_model(model) || bytes.size() != plugin_state_v3_bytes ||
        bytes[0] != 'R' || bytes[1] != 'H' || bytes[2] != 'S' || bytes[3] != 'T' ||
        bytes[4] != 3 || bytes[5] != 0 || bytes[6] != static_cast<std::uint8_t>(model) ||
        bytes[7] != model >> 8U) {
        return std::nullopt;
    }
    plugin_state_detail::Words words{};
    for (std::size_t i = 0; i < words.size(); ++i) {
        words[i] = state_detail::read_u32(bytes, 8 + 4 * i);
    }
    if (!plugin_state_detail::valid_words(words)) { return std::nullopt; }
    return plugin_state_detail::unpack(words);
}

static_assert(std::atomic<std::uint32_t>::is_always_lock_free &&
              std::atomic<std::uint64_t>::is_always_lock_free,
              "The plug-in state mailbox requires lock-free 32-bit and 64-bit atomics");

// One audio owner, with at most one active block token. Non-audio producers may
// request recalls concurrently. All shared words and the guard are seq_cst:
// an accepted snapshot cannot straddle a writer in that single total order.
// Audio never retries and never holds writer ownership across a method return.
class PluginStateMailbox {
public:
    struct BlockToken {
        std::uint64_t publication_token = 0;
        PluginState recalled{};
        bool apply_recall = false;
        bool may_publish = false;
    };

    // Non-audio API: may retry while another writer owns the fixed payload.
    [[nodiscard]] bool request_recall(PluginState state) noexcept {
        if (!valid_plugin_state(state)) { return false; }
        const auto words = plugin_state_detail::pack(state);
        for (;;) {
            auto observed = guard_.load(std::memory_order_seq_cst);
            if ((observed & busy_flag) != 0) { continue; }
            if (!can_advance(observed)) { return false; }
            if (!guard_.compare_exchange_weak(observed, observed | busy_flag,
                    std::memory_order_seq_cst, std::memory_order_seq_cst)) {
                continue;
            }
            store_words(words);
            guard_.store(next_guard(observed, true), std::memory_order_seq_cst);
            return true;
        }
    }

    // Non-audio API: a snapshot includes pending recalls and never consumes one.
    [[nodiscard]] PluginState snapshot() const noexcept {
        for (;;) {
            const auto before = guard_.load(std::memory_order_seq_cst);
            if ((before & busy_flag) != 0) { continue; }
            const auto words = load_words();
            if (before == guard_.load(std::memory_order_seq_cst)) {
                return plugin_state_detail::unpack(words);
            }
        }
    }

    [[nodiscard]] BlockToken begin_block() noexcept {
        auto observed = guard_.load(std::memory_order_seq_cst);
        if ((observed & busy_flag) != 0 || !can_advance(observed)) { return {}; }
        if (!guard_.compare_exchange_strong(observed, observed | busy_flag,
                std::memory_order_seq_cst, std::memory_order_seq_cst)) {
            return {};
        }
        const auto words = load_words();
        const bool pending = (observed & pending_flag) != 0;
        const auto published = next_guard(observed, pending);
        guard_.store(published, std::memory_order_seq_cst);
        return {published, plugin_state_detail::unpack(words), pending, true};
    }

    [[nodiscard]] bool end_block(const BlockToken& token, PluginState state) noexcept {
        if (!token.may_publish || (token.publication_token & busy_flag) != 0 ||
            !can_advance(token.publication_token) || !valid_plugin_state(state)) {
            return false;
        }
        const auto words = plugin_state_detail::pack(state);
        auto expected = token.publication_token;
        if (!guard_.compare_exchange_strong(expected, expected | busy_flag,
                std::memory_order_seq_cst, std::memory_order_seq_cst)) {
            return false;
        }
        store_words(words);
        guard_.store(next_guard(token.publication_token, false), std::memory_order_seq_cst);
        return true;
    }

    // Begin does not consume pending: rejection needs no restoration write.
    void abort_block(const BlockToken&) noexcept {}

private:
    friend struct plugin_state_detail::MailboxTestAccess;
    static constexpr std::uint64_t busy_flag = 1U;
    static constexpr std::uint64_t pending_flag = 2U;
    static constexpr std::uint64_t revision_step = 4U;
    static constexpr std::uint64_t revision_mask =
        std::numeric_limits<std::uint64_t>::max() & ~(busy_flag | pending_flag);

    static constexpr bool can_advance(std::uint64_t guard) noexcept {
        return (guard & revision_mask) != revision_mask;
    }

    // Call only while owning a non-exhausted guard. The masked revision never
    // wraps; exhaustion leaves the last complete snapshot available forever.
    static constexpr std::uint64_t next_guard(std::uint64_t guard, bool pending) noexcept {
        return ((guard & revision_mask) + revision_step) | (pending ? pending_flag : 0U);
    }

    plugin_state_detail::Words load_words() const noexcept {
        plugin_state_detail::Words words{};
        for (std::size_t i = 0; i < words.size(); ++i) {
            words[i] = words_[i].load(std::memory_order_seq_cst);
        }
        return words;
    }

    void store_words(const plugin_state_detail::Words& words) noexcept {
        for (std::size_t i = 0; i < words.size(); ++i) {
            words_[i].store(words[i], std::memory_order_seq_cst);
        }
    }

    std::array<std::atomic<std::uint32_t>, plugin_state_detail::word_count> words_{
        0x3f000000U, 0U, 0x3f800000U, 0x3f000000U, 0x3f000000U, 0U, 0x3f000000U, 0U};
    // bit 0: temporary writer ownership; bit 1: pending; bits 2..63: revision.
    std::atomic<std::uint64_t> guard_{0};
};

} // namespace rhodes
