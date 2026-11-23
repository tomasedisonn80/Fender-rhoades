#include "rhodes/control_value.hpp"

#include <array>
#include <bit>
#include <cfenv>
#include <cmath>
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
namespace control = rhodes::control;
constexpr std::uint32_t float_one = 0x3f800000U;
constexpr std::uint64_t double_one = 0x3ff0000000000000ULL;
constexpr std::uint64_t double_fraction = 0x000fffffffffffffULL;
constexpr unsigned ftz = 1U << 15U;
constexpr unsigned daz = 1U << 6U;
std::size_t checks = 0;

void require(bool condition, std::string_view message) {
    ++checks;
    if (!condition) { throw std::runtime_error(std::string{message}); }
}

// Native casts are an independent oracle, evaluated only after explicitly
// selecting round-to-nearest and gradual underflow. Volatile inputs and a
// noinline call boundary prevent compile-time conversion from hiding FP modes.
RHODES_TEST_NOINLINE std::uint32_t native_quantize(double value) {
    volatile double source = value;
    volatile float converted = static_cast<float>(source);
    const auto bits = std::bit_cast<std::uint32_t>(static_cast<float>(converted));
    return bits == 0x80000000U ? 0U : bits;
}

RHODES_TEST_NOINLINE std::uint64_t native_widen(float value) {
    volatile float source = value;
    volatile double converted = static_cast<double>(source);
    const auto bits = std::bit_cast<std::uint64_t>(static_cast<double>(converted));
    return bits == 0x8000000000000000ULL ? 0ULL : bits;
}

RHODES_TEST_NOINLINE auto quantize(double value) { return control::quantize(value); }
RHODES_TEST_NOINLINE auto widen(float value) { return control::widen(value); }

struct RestoreEnvironment {
    std::fenv_t original{};
#if RHODES_TEST_MXCSR
    unsigned original_mxcsr = _mm_getcsr();
#endif
    RestoreEnvironment() {
        require(std::fegetenv(&original) == 0, "cannot save floating-point environment");
    }
    ~RestoreEnvironment() {
        (void)std::fesetenv(&original);
#if RHODES_TEST_MXCSR
        _mm_setcsr(original_mxcsr);
#endif
    }
};

void set_default_environment() {
    require(std::fesetenv(FE_DFL_ENV) == 0, "cannot select default floating-point environment");
    require(std::fesetround(FE_TONEAREST) == 0, "cannot select round-to-nearest");
#if RHODES_TEST_MXCSR
    _mm_setcsr(_mm_getcsr() & ~(ftz | daz));
#endif
}

std::uint64_t random_bits(std::uint64_t& state) {
    state ^= state << 13U;
    state ^= state >> 7U;
    state ^= state << 17U;
    return state;
}

// Domain expectations classify sign, exponent, and fraction independently of
// the production ordering predicate. No oracle compares a subnormal to zero.
bool allowed_float(std::uint32_t bits) {
    const auto exponent = (bits >> 23U) & 0xffU;
    const auto fraction = bits & 0x007fffffU;
    if ((bits >> 31U) != 0U) { return exponent == 0U && fraction == 0U; }
    return exponent < 127U || (exponent == 127U && fraction == 0U);
}

bool allowed_double(std::uint64_t bits) {
    const auto exponent = (bits >> 52U) & 0x7ffU;
    const auto fraction = bits & double_fraction;
    if ((bits >> 63U) != 0U) { return exponent == 0U && fraction == 0U; }
    return exponent < 1023U || (exponent == 1023U && fraction == 0U);
}

void check_float_domain(std::uint32_t bits) {
    const float value = std::bit_cast<float>(bits);
    const bool zero = bits == 0U || bits == 0x80000000U;
    require(control::valid_normalized(value) == allowed_float(bits), "float domain mismatch");
    require(control::is_zero(value) == zero, "float zero classification mismatch");
    require(widen(value).has_value() == allowed_float(bits), "widen accepted invalid float");
}

void check_double_domain(std::uint64_t bits) {
    const double value = std::bit_cast<double>(bits);
    const bool zero = bits == 0ULL || bits == 0x8000000000000000ULL;
    require(control::valid_normalized(value) == allowed_double(bits), "double domain mismatch");
    require(control::is_zero(value) == zero, "double zero classification mismatch");
    require(control::is_boolean(value) == (zero || bits == double_one),
            "boolean classification mismatch");
    require(quantize(value).has_value() == allowed_double(bits),
            "quantize accepted invalid double");
}

void domains() {
    require(std::feclearexcept(FE_ALL_EXCEPT) == 0, "cannot clear exception flags");
    constexpr std::array<std::uint32_t, 24> float_edges{
        0U, 0x80000000U, 1U, 2U, 0x007ffffeU, 0x007fffffU,
        0x80000001U, 0x80000002U, 0x807fffffU, 0x00800000U, 0x80800000U,
        0x3effffffU, 0x3f000000U, 0x3f000001U, float_one - 1U, float_one,
        float_one + 1U, 0x7f7fffffU, 0x7f800000U, 0xff800000U,
        0x7fc12345U, 0x7f812345U, 0xffc12345U, 0xff812345U};
    constexpr std::array<std::uint64_t, 24> double_edges{
        0ULL, 0x8000000000000000ULL, 1ULL, 2ULL, 0x000ffffffffffffeULL,
        0x000fffffffffffffULL, 0x8000000000000001ULL, 0x8000000000000002ULL,
        0x800fffffffffffffULL, 0x0010000000000000ULL, 0x8010000000000000ULL,
        0x3fdfffffffffffffULL, 0x3fe0000000000000ULL, 0x3fe0000000000001ULL,
        double_one - 1ULL, double_one, double_one + 1ULL, 0x7fefffffffffffffULL,
        0x7ff0000000000000ULL, 0xfff0000000000000ULL, 0x7ff8123456789abcULL,
        0x7ff0123456789abcULL, 0xfff8123456789abcULL, 0xfff0123456789abcULL};
    for (const auto bits : float_edges) { check_float_domain(bits); }
    for (const auto bits : double_edges) { check_double_domain(bits); }
    std::uint64_t generator = 0x053a600d918b7c21ULL;
    for (std::size_t index = 0; index < 32768; ++index) {
        const auto bits = random_bits(generator);
        check_float_domain(static_cast<std::uint32_t>(bits));
        check_double_domain(bits);
    }
    require(std::fetestexcept(FE_ALL_EXCEPT) == 0,
            "integer domain checks or conversions raised floating-point exceptions");
}

void check_quantize(double input) {
    const auto expected = native_quantize(input);
    const auto result = quantize(input);
    require(result.has_value(), "valid quantization rejected");
    require(std::bit_cast<std::uint32_t>(*result) == expected,
            "quantization differs from independent nearest-even native cast");
}

void check_interval(std::uint32_t lower_bits) {
    const double lower = std::bit_cast<double>(native_widen(std::bit_cast<float>(lower_bits)));
    const double upper = std::bit_cast<double>(native_widen(std::bit_cast<float>(lower_bits + 1U)));
    // Adjacent floats and their midpoint are exactly representable in double,
    // including the zero/subnormal and subnormal/normal boundary intervals.
    const double midpoint = lower + (upper - lower) * 0.5;
    check_quantize(lower);
    check_quantize(upper);
    check_quantize(midpoint);
    check_quantize(std::nextafter(midpoint, lower));
    check_quantize(std::nextafter(midpoint, upper));
    const auto tie = quantize(midpoint);
    const auto expected_even = (lower_bits & 1U) == 0U ? lower_bits : lower_bits + 1U;
    require(tie && std::bit_cast<std::uint32_t>(*tie) == expected_even,
            "midpoint did not choose even binary32 significand");
}

void quantize_cases() {
    check_quantize(0.0);
    check_quantize(-0.0);
    check_quantize(1.0);
    constexpr std::array<std::uint32_t, 15> intervals{
        0U, 1U, 2U, 3U, 0x00012345U, 0x003fffffU, 0x00400000U,
        0x007ffffdU, 0x007ffffeU, 0x007fffffU, 0x00800000U,
        0x3effffffU, 0x3f000000U, float_one - 2U, float_one - 1U};
    for (const auto bits : intervals) { check_interval(bits); }
    for (std::uint32_t exponent = 1; exponent < 127; ++exponent) {
        const auto boundary = exponent << 23U;
        check_interval(boundary - 1U);
        check_interval(boundary);
        check_interval(boundary + 1U);
    }
    // Every binary64 exponent at or below one is covered, especially all float
    // underflow exponents and carries across every float power-of-two boundary.
    std::uint64_t generator = 0x053bc413aed82561ULL;
    for (std::uint64_t exponent = 0; exponent < 1023; ++exponent) {
        check_quantize(std::bit_cast<double>(exponent << 52U));
        check_quantize(std::bit_cast<double>((exponent << 52U) | double_fraction));
        for (std::size_t sample = 0; sample < 64; ++sample) {
            check_quantize(std::bit_cast<double>(
                (exponent << 52U) | (random_bits(generator) & double_fraction)));
        }
    }
    for (std::size_t sample = 0; sample < 16384; ++sample) {
        check_interval(static_cast<std::uint32_t>(random_bits(generator) % float_one));
    }
}

void check_widen(float input) {
    const auto expected = native_widen(input);
    const auto result = widen(input);
    require(result.has_value(), "valid widening rejected");
    require(std::bit_cast<std::uint64_t>(*result) == expected,
            "widening differs from independent exact native cast");
    const auto recovered = quantize(*result);
    const auto source_bits = std::bit_cast<std::uint32_t>(input);
    require(recovered && std::bit_cast<std::uint32_t>(*recovered) ==
            (source_bits == 0x80000000U ? 0U : source_bits),
            "widen then quantize did not recover source float bits");
}

void widen_cases() {
    check_widen(0.0F);
    check_widen(-0.0F);
    check_widen(1.0F);
    constexpr std::array<std::uint32_t, 9> fractions{
        0U, 1U, 2U, 3U, 0x003fffffU, 0x00400000U, 0x00400001U, 0x007ffffeU, 0x007fffffU};
    for (std::uint32_t exponent = 0; exponent < 127; ++exponent) {
        for (const auto fraction : fractions) {
            check_widen(std::bit_cast<float>((exponent << 23U) | fraction));
        }
    }
    for (unsigned leading = 0; leading < 23; ++leading) {
        const auto boundary = std::uint32_t{1} << leading;
        check_widen(std::bit_cast<float>(boundary - 1U));
        check_widen(std::bit_cast<float>(boundary));
        check_widen(std::bit_cast<float>(boundary + 1U));
    }
    std::uint64_t generator = 0x053fab76e321918dULL;
    for (std::size_t sample = 0; sample < 32768; ++sample) {
        const auto bits = random_bits(generator);
        check_widen(std::bit_cast<float>(static_cast<std::uint32_t>(bits % float_one)));
        check_widen(std::bit_cast<float>(static_cast<std::uint32_t>(bits) & 0x007fffffU));
    }
}

#if RHODES_TEST_MXCSR
unsigned supported_controls() {
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

void rounding_modes() {
    struct DoubleProbe { double value; std::uint32_t expected; };
    struct FloatProbe { float value; std::uint64_t expected; };
    std::array<DoubleProbe, 1024> doubles{};
    std::array<FloatProbe, 1024> floats{};
    std::uint64_t generator = 0x0530264971af6ed1ULL;
    for (std::size_t index = 0; index < doubles.size(); ++index) {
        // Alternate arbitrary normalized doubles with exact float midpoints.
        const auto bits = random_bits(generator);
        double value = std::bit_cast<double>(bits % double_one);
        if ((index & 1U) == 0U) {
            const auto lower_bits = static_cast<std::uint32_t>(bits % float_one);
            const double lower = std::bit_cast<double>(native_widen(std::bit_cast<float>(lower_bits)));
            const double upper = std::bit_cast<double>(native_widen(std::bit_cast<float>(lower_bits + 1U)));
            value = lower + (upper - lower) * 0.5;
        }
        doubles[index] = {value, native_quantize(value)};
        const auto float_bits = index < 512 ? static_cast<std::uint32_t>(bits) & 0x007fffffU :
            static_cast<std::uint32_t>(bits % float_one);
        const auto float_value = std::bit_cast<float>(float_bits);
        floats[index] = {float_value, native_widen(float_value)};
    }
    std::size_t modes = 0;
    for (const int rounding : {FE_TONEAREST, FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO}) {
        for (const unsigned flush : {0U, ftz, daz, ftz | daz}) {
#if RHODES_TEST_MXCSR
            if ((flush & ~supported_controls()) != 0U) { continue; }
#else
            if (flush != 0U) { continue; }
#endif
            RestoreEnvironment restore;
            require(std::fesetround(rounding) == 0, "cannot set requested rounding mode");
            require(std::feclearexcept(FE_ALL_EXCEPT) == 0, "cannot clear exception flags");
#if RHODES_TEST_MXCSR
            const auto requested = (_mm_getcsr() & ~(ftz | daz | 0x3fU)) | flush;
            _mm_setcsr(requested);
#endif
            for (const auto& probe : doubles) {
                const auto result = quantize(probe.value);
                require(result && std::bit_cast<std::uint32_t>(*result) == probe.expected,
                        "quantization depends on rounding or flush controls");
            }
            for (const auto& probe : floats) {
                const auto result = widen(probe.value);
                require(result && std::bit_cast<std::uint64_t>(*result) == probe.expected,
                        "widening depends on rounding or flush controls");
            }
            domains();
            require(std::fegetround() == rounding, "helper changed rounding mode");
            require(std::fetestexcept(FE_ALL_EXCEPT) == 0, "helper raised FP exception flags");
#if RHODES_TEST_MXCSR
            require(_mm_getcsr() == requested, "helper changed MXCSR controls or status");
#endif
            ++modes;
        }
    }
    std::cout << "checked " << modes << " supported rounding/flush combinations\n";
}

static_assert(control::valid_normalized(std::bit_cast<double>(std::uint64_t{1})));
static_assert(!control::is_zero(std::bit_cast<double>(std::uint64_t{1})));
static_assert(!control::is_boolean(std::bit_cast<double>(std::uint64_t{1})));
static_assert(std::bit_cast<std::uint32_t>(*control::quantize(0.5)) == 0x3f000000U);
static_assert(std::bit_cast<std::uint64_t>(*control::widen(
              std::bit_cast<float>(std::uint32_t{1}))) == 0x36a0000000000000ULL);
} // namespace

int main(int argc, char** argv) {
    const std::string_view selected = argc > 1 ? argv[1] : "all";
    try {
        RestoreEnvironment restore;
        set_default_environment();
        bool found = false;
        const auto run = [&](std::string_view name, auto test) {
            if (selected == "all" || selected == name) {
                checks = 0;
                test();
                found = true;
                std::cout << "PASS " << name << " (" << checks << " always-active checks)\n";
            }
        };
        run("domains", domains);
        run("quantize", quantize_cases);
        run("widen", widen_cases);
        run("rounding_modes", rounding_modes);
        require(found, "unknown control-value selector");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << selected << ": " << error.what() << '\n';
        return 1;
    }
}
