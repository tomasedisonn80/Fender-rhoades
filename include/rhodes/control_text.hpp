#pragma once

#include <array>
#include <bit>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string_view>

// Keep bit classification as integer operations even when an includer uses
// Clang's default floating exception policy. This changes no runtime controls.
#if defined(__clang__)
#pragma float_control(precise, on, push)
#pragma float_control(except, on)
#endif

namespace rhodes::control_text {

static_assert(CHAR_BIT == 8 && sizeof(double) == 8);
static_assert(std::numeric_limits<double>::is_iec559 &&
              std::numeric_limits<double>::radix == 2 &&
              std::numeric_limits<double>::digits == 53 &&
              std::numeric_limits<double>::max_exponent == 1024);

namespace detail {

constexpr std::uint64_t sign_mask = 0x8000000000000000ULL;
constexpr std::uint64_t fraction_mask = 0x000fffffffffffffULL;
constexpr std::uint64_t maximum_magnitude = 0x4040000000000000ULL; // 32

// UI-only arithmetic, independent of locale and the floating-point environment.
// 80 base-2^32 limbs hold 2560 bits. Formatting needs at most 2547 bits for
// a 53-bit significand times 5^1074 (767 decimal digits). After decimal-order
// rejection, parsing needs at most 1496 bits: 127 digits shifted by 1074, or
// a denominator no larger than 10^450. Every capacity check is defensive.
struct Integer {
    std::array<std::uint32_t, 80> limbs{};
    std::size_t used = 0;

    explicit Integer(std::uint64_t value = 0) noexcept {
        if (value != 0U) {
            limbs[0] = static_cast<std::uint32_t>(value);
            limbs[1] = static_cast<std::uint32_t>(value >> 32U);
            used = limbs[1] == 0U ? 1U : 2U;
        }
    }

    [[nodiscard]] unsigned bits() const noexcept {
        if (used == 0U) { return 0; }
        return static_cast<unsigned>((used - 1U) * 32U) +
               32U - static_cast<unsigned>(std::countl_zero(limbs[used - 1U]));
    }

    [[nodiscard]] int compare(const Integer& other) const noexcept {
        if (used != other.used) { return used < other.used ? -1 : 1; }
        for (auto index = used; index != 0U; --index) {
            if (limbs[index - 1U] != other.limbs[index - 1U]) {
                return limbs[index - 1U] < other.limbs[index - 1U] ? -1 : 1;
            }
        }
        return 0;
    }

    [[nodiscard]] bool multiply_add(std::uint32_t multiplier,
                                    std::uint32_t addition = 0) noexcept {
        std::uint64_t carry = addition;
        for (std::size_t index = 0; index < used; ++index) {
            const auto product = static_cast<std::uint64_t>(limbs[index]) *
                                 multiplier + carry;
            limbs[index] = static_cast<std::uint32_t>(product);
            carry = product >> 32U;
        }
        if (carry != 0U) {
            if (used == limbs.size()) { return false; }
            limbs[used++] = static_cast<std::uint32_t>(carry);
        }
        return true;
    }

    [[nodiscard]] bool shift_left(unsigned shift) noexcept {
        if (used == 0U || shift == 0U) { return true; }
        const auto width = bits();
        if (shift > limbs.size() * 32U - width) { return false; }
        const auto old_used = used;
        const auto words = static_cast<std::size_t>(shift / 32U);
        const auto part = shift % 32U;
        used = static_cast<std::size_t>((width + shift + 31U) / 32U);
        for (auto index = used; index != 0U; --index) {
            const auto destination = index - 1U;
            std::uint32_t value = 0;
            if (destination >= words) {
                const auto source = destination - words;
                if (source < old_used) { value = limbs[source] << part; }
                if (part != 0U && source != 0U && source - 1U < old_used) {
                    value |= limbs[source - 1U] >> (32U - part);
                }
            }
            limbs[destination] = value;
        }
        return true;
    }

    void shift_right_one() noexcept {
        std::uint32_t carry = 0;
        for (auto index = used; index != 0U; --index) {
            const auto value = limbs[index - 1U];
            limbs[index - 1U] = (value >> 1U) | (carry << 31U);
            carry = value & 1U;
        }
        if (used != 0U && limbs[used - 1U] == 0U) { --used; }
    }

    // The caller has established *this >= other.
    void subtract(const Integer& other) noexcept {
        std::uint64_t borrow = 0;
        for (std::size_t index = 0; index < used; ++index) {
            const auto subtrahend = index < other.used ? other.limbs[index] : 0U;
            const auto difference = static_cast<std::uint64_t>(limbs[index]) -
                                    subtrahend - borrow;
            limbs[index] = static_cast<std::uint32_t>(difference);
            borrow = difference >> 63U;
        }
        while (used != 0U && limbs[used - 1U] == 0U) { --used; }
    }

    [[nodiscard]] std::uint32_t divide(std::uint32_t divisor) noexcept {
        std::uint64_t remainder = 0;
        for (auto index = used; index != 0U; --index) {
            const auto dividend = (remainder << 32U) | limbs[index - 1U];
            limbs[index - 1U] = static_cast<std::uint32_t>(dividend / divisor);
            remainder = dividend % divisor;
        }
        while (used != 0U && limbs[used - 1U] == 0U) { --used; }
        return static_cast<std::uint32_t>(remainder);
    }
};

[[nodiscard]] constexpr bool digit(char value) noexcept {
    return value >= '0' && value <= '9';
}

// The destination has room for all digits of value; no terminator is written.
[[nodiscard]] inline std::size_t write_unsigned(std::uint64_t value,
                                               char* destination) noexcept {
    std::array<char, 20> reversed{};
    std::size_t length = 0;
    do {
        reversed[length++] = static_cast<char>('0' + value % 10U);
        value /= 10U;
    } while (value != 0U);
    for (std::size_t index = 0; index < length; ++index) {
        destination[index] = reversed[length - index - 1U];
    }
    return length;
}

// Conversion has a fixed 86-chunk / 767-digit bound for every supported value.
[[nodiscard]] inline std::size_t decimal_digits(Integer value,
                                               std::array<char, 768>& digits) noexcept {
    std::array<std::uint32_t, 86> chunks{};
    std::size_t count = 0;
    while (value.used != 0U) {
        if (count == chunks.size()) { return 0; }
        chunks[count++] = value.divide(1000000000U);
    }
    if (count == 0U) { return 0; }
    auto length = write_unsigned(chunks[count - 1U], digits.data());
    for (auto index = count - 1U; index != 0U; --index) {
        if (length + 9U > digits.size()) { return 0; }
        auto chunk = chunks[index - 1U];
        for (std::size_t position = 9; position != 0U; --position) {
            digits[length + position - 1U] = static_cast<char>('0' + chunk % 10U);
            chunk /= 10U;
        }
        length += 9U;
    }
    return length;
}

} // namespace detail

// Parse a complete signed ASCII decimal of at most 127 bytes. The controller
// owns surrounding whitespace/unit handling; neither is accepted here. The
// grammar permits .5, 1., and signed e/E exponents. Values are rounded directly
// to nearest-even binary64, independently of host rounding/flush controls.
// Reject invalid syntax, rounded magnitude > 32, and nonzero input rounding to
// zero. Decimal zero is accepted even with a huge exponent and retains its sign.
// The bound applies to the rounded binary64 value, so 32.000000000000000001 is
// accepted. No floating arithmetic, allocation, locale or runtime FP controls.
[[nodiscard]] inline std::optional<double> parse_decimal(std::string_view text) noexcept {
    if (text.empty() || text.size() > 127U) { return std::nullopt; }
    std::size_t position = 0;
    std::uint64_t sign = 0;
    if (text[position] == '+' || text[position] == '-') {
        sign = text[position] == '-' ? detail::sign_mask : 0U;
        ++position;
    }
    detail::Integer numerator;
    bool have_digit = false;
    bool fraction = false;
    int fractional_digits = 0;
    int significant_digits = 0;
    while (position < text.size()) {
        const auto current = text[position];
        if (detail::digit(current)) {
            have_digit = true;
            const auto value = static_cast<std::uint32_t>(current - '0');
            if (!numerator.multiply_add(10U, value)) { return std::nullopt; }
            if (fraction) { ++fractional_digits; }
            if (value != 0U || significant_digits != 0) { ++significant_digits; }
            ++position;
        } else if (current == '.' && !fraction) {
            fraction = true;
            ++position;
        } else {
            break;
        }
    }
    if (!have_digit) { return std::nullopt; }
    int exponent = 0;
    if (position < text.size() && (text[position] == 'e' || text[position] == 'E')) {
        ++position;
        bool negative_exponent = false;
        if (position < text.size() && (text[position] == '+' || text[position] == '-')) {
            negative_exponent = text[position] == '-';
            ++position;
        }
        const auto beginning = position;
        while (position < text.size() && detail::digit(text[position])) {
            // Saturation is safely beyond every representable bounded input.
            if (exponent < 10000) {
                exponent = exponent * 10 + static_cast<int>(text[position] - '0');
                if (exponent > 10000) { exponent = 10000; }
            }
            ++position;
        }
        if (position == beginning) { return std::nullopt; }
        if (negative_exponent) { exponent = -exponent; }
    }
    if (position != text.size()) { return std::nullopt; }
    if (numerator.used == 0U) { return std::bit_cast<double>(sign); }
    const auto power = exponent - fractional_digits;
    const auto decimal_order = significant_digits + power - 1;
    if (decimal_order < -324 || decimal_order > 1) { return std::nullopt; }

    detail::Integer denominator{1};
    for (int index = 0; index < power; ++index) {
        if (!numerator.multiply_add(10U)) { return std::nullopt; }
    }
    for (int index = 0; index < -power; ++index) {
        if (!denominator.multiply_add(10U)) { return std::nullopt; }
    }

    // Determine floor(log2(numerator / denominator)) by integer alignment.
    auto binary_exponent = static_cast<int>(numerator.bits()) -
                           static_cast<int>(denominator.bits());
    if (binary_exponent >= 0) {
        auto aligned = denominator;
        if (!aligned.shift_left(static_cast<unsigned>(binary_exponent))) {
            return std::nullopt;
        }
        if (numerator.compare(aligned) < 0) { --binary_exponent; }
    } else {
        auto aligned = numerator;
        if (!aligned.shift_left(static_cast<unsigned>(-binary_exponent))) {
            return std::nullopt;
        }
        if (aligned.compare(denominator) < 0) { --binary_exponent; }
    }
    const auto unit_exponent = binary_exponent < -1022 ? -1074 : binary_exponent - 52;
    if (!numerator.shift_left(static_cast<unsigned>(-unit_exponent))) {
        return std::nullopt;
    }

    // The scaled quotient has at most 53 bits. Binary long division leaves the
    // exact remainder, so there is no approximation or double-rounding step.
    std::uint64_t significand = 0;
    if (numerator.compare(denominator) >= 0) {
        const auto shift = numerator.bits() - denominator.bits();
        if (shift > 53U) { return std::nullopt; }
        auto divisor = denominator;
        if (!divisor.shift_left(shift)) { return std::nullopt; }
        for (int index = static_cast<int>(shift); index >= 0; --index) {
            if (numerator.compare(divisor) >= 0) {
                numerator.subtract(divisor);
                significand |= std::uint64_t{1} << static_cast<unsigned>(index);
            }
            divisor.shift_right_one();
        }
    }
    if (!numerator.shift_left(1U)) { return std::nullopt; }
    const auto halfway = numerator.compare(denominator);
    if (halfway > 0 || (halfway == 0 && (significand & 1U) != 0U)) { ++significand; }
    if (significand == 0U) { return std::nullopt; }

    std::uint64_t magnitude = significand;
    if (binary_exponent >= -1022) {
        if (significand == (std::uint64_t{1} << 53U)) {
            significand >>= 1U;
            ++binary_exponent;
        }
        magnitude = (static_cast<std::uint64_t>(binary_exponent + 1023) << 52U) |
                    (significand & detail::fraction_mask);
    }
    if (magnitude > detail::maximum_magnitude) { return std::nullopt; }
    return std::bit_cast<double>(sign | magnitude);
}

// Format finite binary64 magnitude <= 32, with at most 17 significant decimal
// digits, enough for a nearest-even binary64 round trip. This is intentionally
// not a shortest-decimal algorithm. Strip trailing zeros and use fixed notation
// for decimal exponents -4..1; both signed zeros display as "0". On success,
// return the number of bytes written, without a NUL terminator. On any failure
// leave the entire destination unchanged. 24 bytes suffice for every result.
[[nodiscard]] inline std::optional<std::size_t> format_decimal(
    double value, std::span<char> destination) noexcept {
    const auto encoding = std::bit_cast<std::uint64_t>(value);
    const auto magnitude = encoding & ~detail::sign_mask;
    if (magnitude > detail::maximum_magnitude) { return std::nullopt; }
    std::array<char, 24> output{};
    std::size_t length = 0;
    if (magnitude == 0U) {
        output[length++] = '0';
    } else {
        if ((encoding & detail::sign_mask) != 0U) { output[length++] = '-'; }
        const auto binary_exponent = static_cast<int>(magnitude >> 52U);
        auto significand = magnitude & detail::fraction_mask;
        auto power = 1074;
        if (binary_exponent != 0) {
            significand |= std::uint64_t{1} << 52U;
            power = 1075 - binary_exponent;
        }
        const auto zeros = static_cast<int>(std::countr_zero(significand));
        const auto removable = zeros < power ? zeros : power;
        significand >>= static_cast<unsigned>(removable);
        power -= removable;
        detail::Integer coefficient{significand};
        for (int index = 0; index < power; ++index) {
            if (!coefficient.multiply_add(5U)) { return std::nullopt; }
        }
        std::array<char, 768> exact{};
        const auto exact_length = detail::decimal_digits(coefficient, exact);
        if (exact_length == 0U) { return std::nullopt; }
        auto decimal_exponent = static_cast<int>(exact_length) - power - 1;
        auto precision = exact_length < 17U ? exact_length : 17U;
        std::uint64_t rounded = 0;
        for (std::size_t index = 0; index < precision; ++index) {
            rounded = rounded * 10U + static_cast<unsigned>(exact[index] - '0');
        }
        if (exact_length > precision) {
            bool remainder = false;
            for (auto index = precision + 1U; index < exact_length; ++index) {
                remainder = remainder || exact[index] != '0';
            }
            const auto next = exact[precision];
            if (next > '5' || (next == '5' && (remainder || (rounded & 1U) != 0U))) {
                ++rounded;
            }
        }
        std::array<char, 20> digits{};
        auto count = detail::write_unsigned(rounded, digits.data());
        decimal_exponent += static_cast<int>(count) - static_cast<int>(precision);
        while (count > 1U && digits[count - 1U] == '0') { --count; }
        if (decimal_exponent >= 0) {
            const auto whole = static_cast<std::size_t>(decimal_exponent + 1);
            for (std::size_t index = 0; index < whole || index < count; ++index) {
                if (index == whole) { output[length++] = '.'; }
                output[length++] = index < count ? digits[index] : '0';
            }
        } else if (decimal_exponent >= -4) {
            output[length++] = '0';
            output[length++] = '.';
            for (int index = -1; index > decimal_exponent; --index) {
                output[length++] = '0';
            }
            for (std::size_t index = 0; index < count; ++index) {
                output[length++] = digits[index];
            }
        } else {
            output[length++] = digits[0];
            if (count > 1U) { output[length++] = '.'; }
            for (std::size_t index = 1; index < count; ++index) {
                output[length++] = digits[index];
            }
            output[length++] = 'e';
            output[length++] = '-';
            length += detail::write_unsigned(static_cast<std::uint64_t>(-decimal_exponent),
                                             output.data() + length);
        }
    }
    if (destination.size() < length) { return std::nullopt; }
    for (std::size_t index = 0; index < length; ++index) {
        destination[index] = output[index];
    }
    return length;
}

} // namespace rhodes::control_text

#if defined(__clang__)
#pragma float_control(pop)
#endif
