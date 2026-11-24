#pragma once

#include <bit>
#include <climits>
#include <cstdint>
#include <limits>
#include <optional>

// Clang's default exception behavior permits replacing bit-level zero tests
// with floating comparisons. That can raise FE_INVALID on a signaling NaN,
// despite the source using only integers. Preserve the public no-FP-effects
// contract in every inline caller, and restore the includer's policy below.
#if defined(__clang__)
#pragma float_control(precise, on, push)
#pragma float_control(except, on)
#endif

namespace rhodes::control {

static_assert(CHAR_BIT == 8 && sizeof(float) == 4 && sizeof(double) == 8);
static_assert(std::numeric_limits<float>::is_iec559 &&
              std::numeric_limits<float>::radix == 2 &&
              std::numeric_limits<float>::digits == 24 &&
              std::numeric_limits<float>::max_exponent == 128);
static_assert(std::numeric_limits<double>::is_iec559 &&
              std::numeric_limits<double>::radix == 2 &&
              std::numeric_limits<double>::digits == 53 &&
              std::numeric_limits<double>::max_exponent == 1024);

// Control domains and stored configuration must not depend on the host's
// denormals-are-zero, flush-to-zero, or rounding controls. Audio arithmetic
// remains native floating point; these helpers operate only on control values.
[[nodiscard]] constexpr bool valid_normalized(float value) noexcept {
    const auto bits = std::bit_cast<std::uint32_t>(value);
    return bits <= 0x3f800000U || bits == 0x80000000U;
}

[[nodiscard]] constexpr bool valid_normalized(double value) noexcept {
    const auto bits = std::bit_cast<std::uint64_t>(value);
    return bits <= 0x3ff0000000000000ULL || bits == 0x8000000000000000ULL;
}

[[nodiscard]] constexpr bool is_zero(float value) noexcept {
    return (std::bit_cast<std::uint32_t>(value) & 0x7fffffffU) == 0U;
}

[[nodiscard]] constexpr bool is_zero(double value) noexcept {
    return (std::bit_cast<std::uint64_t>(value) & 0x7fffffffffffffffULL) == 0ULL;
}

[[nodiscard]] constexpr bool is_boolean(double value) noexcept {
    return is_zero(value) || std::bit_cast<std::uint64_t>(value) == 0x3ff0000000000000ULL;
}

namespace detail {
// Calls use shifts 29..53, so neither mask construction nor the tie test can
// shift by the word width. The source significand is at most 53 bits wide.
[[nodiscard]] constexpr std::uint64_t round_shift(std::uint64_t value,
                                                 unsigned shift) noexcept {
    const auto whole = value >> shift;
    const auto remainder = value & ((std::uint64_t{1} << shift) - 1U);
    const auto half = std::uint64_t{1} << (shift - 1U);
    return whole + static_cast<std::uint64_t>(remainder > half ||
                                              (remainder == half && (whole & 1U) != 0U));
}
} // namespace detail

// Quantize a normalized binary64 value to binary32, round-to-nearest with ties
// to an even significand. The subnormal range, zero tie, and carry into the
// smallest normal value use the same integer rounding rule. Invalid input has
// no result; both signed zeros produce the canonical positive zero.
[[nodiscard]] constexpr std::optional<float> quantize(double value) noexcept {
    if (!valid_normalized(value)) { return std::nullopt; }
    const auto bits = std::bit_cast<std::uint64_t>(value);
    if (is_zero(value)) { return std::bit_cast<float>(std::uint32_t{0}); }
    const auto exponent = static_cast<int>((bits >> 52U) & 0x7ffU) - 1023;
    if (exponent < -150) { return std::bit_cast<float>(std::uint32_t{0}); }
    const auto significand = (bits & 0x000fffffffffffffULL) | (std::uint64_t{1} << 52U);
    std::uint32_t result = 0;
    if (exponent < -126) {
        result = static_cast<std::uint32_t>(detail::round_shift(
            significand, static_cast<unsigned>(-exponent - 97)));
    } else {
        auto rounded = detail::round_shift(significand, 29U);
        auto target_exponent = static_cast<std::uint32_t>(exponent + 127);
        if (rounded == (std::uint64_t{1} << 24U)) {
            rounded >>= 1U;
            ++target_exponent;
        }
        result = (target_exponent << 23U) |
                 (static_cast<std::uint32_t>(rounded) & 0x007fffffU);
    }
    return std::bit_cast<float>(result);
}

// Every normalized binary32 value is exactly representable in binary64,
// including positive subnormals. Construct its bits without a float-to-double
// instruction, which may treat a source subnormal as zero under DAZ.
[[nodiscard]] constexpr std::optional<double> widen(float value) noexcept {
    if (!valid_normalized(value)) { return std::nullopt; }
    const auto bits = std::bit_cast<std::uint32_t>(value);
    if (is_zero(value)) { return std::bit_cast<double>(std::uint64_t{0}); }
    const auto exponent = bits >> 23U;
    const auto fraction = bits & 0x007fffffU;
    std::uint64_t result = 0;
    if (exponent != 0U) {
        result = (static_cast<std::uint64_t>(exponent + 896U) << 52U) |
                 (static_cast<std::uint64_t>(fraction) << 29U);
    } else {
        const unsigned leading = 31U - static_cast<unsigned>(std::countl_zero(fraction));
        result = (static_cast<std::uint64_t>(leading + 874U) << 52U) |
                 ((static_cast<std::uint64_t>(fraction) - (std::uint64_t{1} << leading))
                  << (52U - leading));
    }
    return std::bit_cast<double>(result);
}

} // namespace rhodes::control

#if defined(__clang__)
#pragma float_control(pop)
#endif
