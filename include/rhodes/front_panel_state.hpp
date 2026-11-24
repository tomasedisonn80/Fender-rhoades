#pragma once

#include <bit>
#include <climits>
#include <cstdint>
#include <limits>

namespace rhodes {

static_assert(CHAR_BIT == 8 && sizeof(float) == 4 && std::numeric_limits<float>::is_iec559 &&
              std::numeric_limits<float>::radix == 2 && std::numeric_limits<float>::digits == 24);

// Normalized synthetic controls; these are not calibrated circuit parameters.
struct FrontPanelState {
    float bass = 0.5F;
    float treble = 0.5F;
    float intensity = 0.0F;
    float speed = 0.5F;
    bool vibrato = false;
    friend bool operator==(const FrontPanelState&, const FrontPanelState&) = default;
};

namespace front_panel_detail {
// Test binary32 encodings, not floating comparisons: DAZ/FTZ must not make a
// negative subnormal valid or discard a positive subnormal configuration.
inline constexpr bool valid_control_bits(std::uint32_t bits) noexcept {
    return bits <= 0x3f800000U || bits == 0x80000000U;
}
inline constexpr std::uint32_t canonical_control_bits(std::uint32_t bits) noexcept {
    return (bits & 0x7fffffffU) == 0U ? 0U : bits;
}
inline std::uint32_t control_bits(float value) noexcept {
    return canonical_control_bits(std::bit_cast<std::uint32_t>(value));
}
} // namespace front_panel_detail

[[nodiscard]] inline bool valid_front_panel_state(FrontPanelState state) noexcept {
    return front_panel_detail::valid_control_bits(std::bit_cast<std::uint32_t>(state.bass)) &&
           front_panel_detail::valid_control_bits(std::bit_cast<std::uint32_t>(state.treble)) &&
           front_panel_detail::valid_control_bits(std::bit_cast<std::uint32_t>(state.intensity)) &&
           front_panel_detail::valid_control_bits(std::bit_cast<std::uint32_t>(state.speed));
}

// Exact persisted defaults, with either zero sign accepted for intensity.
// Inactive but nondefault vibrato settings still need to survive state saving.
[[nodiscard]] inline bool neutral_front_panel_state(FrontPanelState state) noexcept {
    return front_panel_detail::control_bits(state.bass) == 0x3f000000U &&
           front_panel_detail::control_bits(state.treble) == 0x3f000000U &&
           front_panel_detail::control_bits(state.intensity) == 0U &&
           front_panel_detail::control_bits(state.speed) == 0x3f000000U &&
           !state.vibrato;
}

} // namespace rhodes
