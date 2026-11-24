#include "rhodes/control_text.hpp"
#include "../fixtures/control_text_vectors.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cfenv>
#include <clocale>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <optional>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#if defined(__clang__)
#pragma STDC FENV_ACCESS ON
#endif

#if defined(__SSE__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 1)
#define RHODES_TEXT_TEST_MXCSR 1
#include <xmmintrin.h>
#if defined(_MSC_VER)
#include <immintrin.h>
#endif
#else
#define RHODES_TEXT_TEST_MXCSR 0
#endif

#if (defined(__i386__) || defined(__x86_64__)) && (defined(__GNUC__) || defined(__clang__))
#define RHODES_TEXT_TEST_X87 1
#else
#define RHODES_TEXT_TEST_X87 0
#endif

#if defined(__aarch64__) && (defined(__GNUC__) || defined(__clang__))
#define RHODES_TEXT_TEST_FPCR 1
#else
#define RHODES_TEXT_TEST_FPCR 0
#endif

#if defined(_MSC_VER)
#define RHODES_TEXT_TEST_NOINLINE __declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
#define RHODES_TEXT_TEST_NOINLINE __attribute__((noinline))
#else
#define RHODES_TEXT_TEST_NOINLINE
#endif

namespace {
namespace text = rhodes::control_text;
namespace oracle = rhodes::control_text::test_vectors;

// Public bounds, independent of the implementation's integer representation.
constexpr std::size_t input_limit = 127;
constexpr std::size_t output_limit = 24;
constexpr std::uint64_t sign_mask = 0x8000000000000000ULL;
constexpr std::uint64_t fraction_mask = 0x000fffffffffffffULL;
constexpr std::uint64_t magnitude_limit = 0x4040000000000000ULL;
constexpr std::uint64_t one_bits = 0x3ff0000000000000ULL;
constexpr char guard = '#';
constexpr std::size_t guard_size = 4;
std::size_t checks = 0;
std::size_t conversions = 0;
std::string_view current_case = "initialization";
std::uint64_t current_bits = 0;

void require(bool condition, std::string_view message) {
    ++checks;
    if (!condition) {
        std::ostringstream error;
        error << current_case << " [bits=0x" << std::hex << current_bits << "]: " << message;
        throw std::runtime_error(error.str());
    }
}

#if RHODES_TEXT_TEST_X87
unsigned short x87_controls() noexcept {
    unsigned short result = 0;
    asm volatile("fnstcw %0" : "=m"(result));
    return result;
}
#endif

#if RHODES_TEXT_TEST_FPCR
std::uint64_t fpcr_controls() noexcept {
    std::uint64_t result = 0;
    asm volatile("mrs %0, fpcr" : "=r"(result));
    return result;
}

void set_fpcr_controls(std::uint64_t value) noexcept {
    asm volatile("msr fpcr, %0" : : "r"(value) : "memory");
}
#endif

// Status flags are deliberately excluded: the host contract preserves controls,
// while making no promise about parser-library sticky exception flags.
struct Controls {
    int rounding = std::fegetround();
#if RHODES_TEXT_TEST_MXCSR
    unsigned mxcsr = _mm_getcsr() & ~0x3fU;
#endif
#if RHODES_TEXT_TEST_X87
    unsigned short x87 = x87_controls();
#endif
#if RHODES_TEXT_TEST_FPCR
    std::uint64_t fpcr = fpcr_controls();
#endif

    void expect_unchanged() const {
        require(std::fegetround() == rounding, "conversion changed rounding control");
#if RHODES_TEXT_TEST_MXCSR
        require((_mm_getcsr() & ~0x3fU) == mxcsr, "conversion changed MXCSR controls");
#endif
#if RHODES_TEXT_TEST_X87
        require(x87_controls() == x87, "conversion changed x87 controls");
#endif
#if RHODES_TEXT_TEST_FPCR
        require(fpcr_controls() == fpcr, "conversion changed ARM FPCR controls");
#endif
    }
};

struct RestoreEnvironment {
    std::fenv_t saved{};
#if RHODES_TEXT_TEST_MXCSR
    unsigned mxcsr = _mm_getcsr();
#endif
#if RHODES_TEXT_TEST_FPCR
    std::uint64_t fpcr = fpcr_controls();
#endif

    RestoreEnvironment() {
        require(std::fegetenv(&saved) == 0, "cannot save floating-point environment");
    }
    ~RestoreEnvironment() {
        (void)std::fesetenv(&saved);
#if RHODES_TEXT_TEST_MXCSR
        _mm_setcsr(mxcsr);
#endif
#if RHODES_TEXT_TEST_FPCR
        set_fpcr_controls(fpcr);
#endif
    }
};

// Keep a runtime call boundary so optimized Release tests exercise the selected
// host environment even for the fixed edge encodings and decimal fixtures.
RHODES_TEXT_TEST_NOINLINE std::optional<double> native_parse(std::string_view input) {
    return text::parse_decimal(input);
}

RHODES_TEXT_TEST_NOINLINE std::optional<std::size_t> native_format(
    double value, std::span<char> destination) {
    return text::format_decimal(value, destination);
}

std::optional<double> parse(std::string_view input) {
    const Controls before;
    const auto result = native_parse(input);
    before.expect_unchanged();
    ++conversions;
    return result;
}

std::optional<std::size_t> format(std::uint64_t encoding, std::span<char> destination) {
    const Controls before;
    const auto result = native_format(std::bit_cast<double>(encoding), destination);
    before.expect_unchanged();
    ++conversions;
    return result;
}

void expect_parse(std::string_view input, std::uint64_t expected) {
    current_bits = expected;
    const auto actual = parse(input);
    require(actual.has_value(), "valid complete decimal was rejected");
    require(std::bit_cast<std::uint64_t>(*actual) == expected,
            "parsed binary64 bits differ from the independent expectation");
}

void expect_rejected(std::string_view input) {
    current_bits = 0;
    require(!parse(input), "invalid or unrepresentable decimal was accepted");
}

void oracle_cases() {
    std::size_t accepted = 0;
    std::size_t rejected = 0;
    for (const auto& vector : oracle::vectors) {
        current_case = vector.id;
        current_bits = vector.bits;
        const auto actual = parse(vector.input);
        require(actual.has_value() == vector.accepted, "acceptance differs from rational oracle");
        if (vector.accepted) {
            require(std::bit_cast<std::uint64_t>(*actual) == vector.bits,
                    "binary64 bits differ from rational oracle");
            ++accepted;
        } else {
            ++rejected;
        }
    }
    require(accepted != 0 && rejected != 0, "oracle must cover acceptance and rejection");
}

void bound_cases() {
    current_case = "empty views";
    expect_rejected({});
    expect_rejected(std::string_view{static_cast<const char*>(nullptr), 0});

    current_case = "non-NUL-terminated input at every permitted length";
    std::array<char, input_limit> raw{};
    raw.fill('0');
    for (std::size_t length = 1; length <= raw.size(); ++length) {
        expect_parse(std::string_view{raw.data(), length}, 0);
        raw[length - 1] = '1';
        expect_parse(std::string_view{raw.data(), length}, one_bits);
        raw[length - 1] = '0';
    }
    raw.front() = '-';
    expect_parse(std::string_view{raw.data(), raw.size()}, sign_mask);
    raw.back() = '1';
    expect_parse(std::string_view{raw.data(), raw.size()}, sign_mask | one_bits);
    raw.front() = '+';
    expect_parse(std::string_view{raw.data(), raw.size()}, one_bits);

    current_case = "127-byte decimal point and exponent bounds";
    raw.fill('0');
    raw.front() = '1';
    raw[1] = '.';
    expect_parse(std::string_view{raw.data(), raw.size()}, one_bits);
    raw.fill('9');
    raw[0] = '-';
    raw[1] = '0';
    raw[2] = 'e';
    expect_parse(std::string_view{raw.data(), raw.size()}, sign_mask);
    raw[1] = '1';
    expect_rejected(std::string_view{raw.data(), raw.size()});
    raw[3] = '-';
    expect_rejected(std::string_view{raw.data(), raw.size()});
    raw[1] = '0';
    expect_parse(std::string_view{raw.data(), raw.size()}, sign_mask);

    current_case = "length is a hard bound including decimal zero";
    std::array<char, 4096> oversized{};
    oversized.fill('0');
    for (const auto length : {input_limit + 1U, input_limit + 2U, oversized.size()}) {
        expect_rejected(std::string_view{oversized.data(), length});
    }
    oversized[input_limit - 1] = '1';
    oversized[input_limit] = 'x';
    expect_parse(std::string_view{oversized.data(), input_limit}, one_bits);
    expect_rejected(std::string_view{oversized.data(), input_limit + 1});

    current_case = "all one-byte values and embedded non-grammar bytes";
    constexpr std::array<std::uint64_t, 10> digit_bits{
        0, 0x3ff0000000000000ULL, 0x4000000000000000ULL, 0x4008000000000000ULL,
        0x4010000000000000ULL, 0x4014000000000000ULL, 0x4018000000000000ULL,
        0x401c000000000000ULL, 0x4020000000000000ULL, 0x4022000000000000ULL};
    constexpr std::string_view grammar_bytes = "0123456789.+-eE";
    constexpr std::string_view decimal = "1.25e+0";
    for (unsigned byte = 0; byte <= 255U; ++byte) {
        const auto character = std::bit_cast<char>(static_cast<unsigned char>(byte));
        const std::array<char, 1> single{character};
        const std::string_view view{single.data(), single.size()};
        if (byte >= static_cast<unsigned>('0') && byte <= static_cast<unsigned>('9')) {
            expect_parse(view, digit_bits[byte - static_cast<unsigned>('0')]);
        } else {
            expect_rejected(view);
        }
        if (grammar_bytes.find(character) != std::string_view::npos) { continue; }
        for (std::size_t position = 0; position <= decimal.size(); ++position) {
            std::array<char, decimal.size() + 1> inserted{};
            std::copy_n(decimal.begin(), position, inserted.begin());
            inserted[position] = character;
            std::copy(decimal.begin() + static_cast<std::ptrdiff_t>(position), decimal.end(),
                      inserted.begin() + static_cast<std::ptrdiff_t>(position + 1));
            expect_rejected(std::string_view{inserted.data(), inserted.size()});
        }
    }

    current_case = "bounded views consume embedded NUL and trailing syntax";
    const std::array<char, 5> embedded_nul{'1', '.', '5', '\0', '2'};
    expect_parse(std::string_view{embedded_nul.data(), 3}, 0x3ff8000000000000ULL);
    expect_rejected(std::string_view{embedded_nul.data(), 4});
    expect_rejected(std::string_view{embedded_nul.data(), embedded_nul.size()});
    constexpr std::array invalid{
        "+", "-", ".", "+.", "-.", ".e0", "1e", "1e+", "1e-", "1e+-0",
        "1e--0", "1e++0", "1.2.3", "1e0.0", "1e0e0", "++1", "--1", "-+1",
        " 1", "1 ", "\t1", "1\r", "1\n", "1x", "1 x", "1,0", "0x1p0", "nan",
        "NaN", "inf", "Infinity", "16junk", "1e100000", "-1e100000", "1e-100000"};
    for (const auto* input : invalid) { expect_rejected(input); }
}

struct Formatted {
    std::array<char, output_limit> bytes{};
    std::size_t length = 0;
    std::string_view view() const { return {bytes.data(), length}; }
};

void display_grammar(std::string_view displayed) {
    require(!displayed.empty() && displayed.size() <= output_limit,
            "display exceeded its static output bound");
    if (displayed.front() == '-') { displayed.remove_prefix(1); }
    require(!displayed.empty() && displayed.front() >= '0' && displayed.front() <= '9',
            "display must start with an ASCII digit after its optional minus sign");
    const auto exponent_start = displayed.find('e');
    const auto mantissa = displayed.substr(0, exponent_start);
    const auto point = mantissa.find('.');
    const auto whole_length = point == std::string_view::npos ? mantissa.size() : point;
    require(whole_length == 1 || mantissa.front() != '0', "display has leading integer zeros");
    std::size_t significant_digits = 0;
    bool nonzero = false;
    for (std::size_t index = 0; index < mantissa.size(); ++index) {
        if (index == point) { continue; }
        const auto character = mantissa[index];
        require(character >= '0' && character <= '9', "display contains nondecimal mantissa bytes");
        nonzero = nonzero || character != '0';
        if (nonzero) { ++significant_digits; }
    }
    require(significant_digits <= 17, "display contains more than 17 significant digits");
    if (point != std::string_view::npos) {
        require(point != 0 && point + 1 < mantissa.size(), "display has an empty decimal component");
        require(mantissa.back() != '0', "display did not strip trailing fractional zeros");
    }
    if (exponent_start != std::string_view::npos) {
        require(whole_length == 1 && mantissa.front() != '0', "scientific mantissa is not normalized");
        const auto exponent = displayed.substr(exponent_start + 1);
        require(exponent.size() >= 2 && exponent.size() <= 4 && exponent.front() == '-',
                "scientific exponent has invalid sign or length");
        require(exponent[1] >= '1' && exponent[1] <= '9', "scientific exponent has leading zeros");
        unsigned magnitude = 0;
        for (std::size_t index = 1; index < exponent.size(); ++index) {
            require(exponent[index] >= '0' && exponent[index] <= '9', "exponent is not ASCII decimal");
            magnitude = magnitude * 10U + static_cast<unsigned>(exponent[index] - '0');
        }
        require(magnitude >= 5 && magnitude <= 324, "scientific exponent is outside display policy");
    } else if (nonzero && mantissa.front() == '0') {
        require(point == 1 && mantissa.find_first_of("123456789") <= 5,
                "small fixed decimal should use scientific notation");
    }
}

template <std::size_t Size>
void expect_guarded_output(const std::array<char, Size>& storage, std::string_view expected) {
    require(guard_size + expected.size() <= storage.size(), "invalid guarded-output test fixture");
    for (std::size_t index = 0; index < storage.size(); ++index) {
        const auto wanted = index >= guard_size && index < guard_size + expected.size()
            ? expected[index - guard_size] : guard;
        require(storage[index] == wanted, "format changed guard, unused capacity, or output bytes");
    }
}

Formatted check_format(std::uint64_t encoding) {
    current_bits = encoding;
    std::array<char, output_limit + guard_size * 2> storage{};
    storage.fill(guard);
    const auto result = format(encoding, std::span<char>{storage}.subspan(guard_size, output_limit));
    require(result.has_value(), "supported binary64 value could not be formatted in 24 bytes");
    require(*result != 0 && *result <= output_limit, "format returned an invalid output length");
    const std::string_view displayed{storage.data() + guard_size, *result};
    display_grammar(displayed);
    expect_guarded_output(storage, displayed);
    const auto recovered = parse(displayed);
    const auto expected = (encoding & ~sign_mask) == 0 ? 0 : encoding;
    require(recovered.has_value(), "formatter produced text that the parser rejected");
    require(std::bit_cast<std::uint64_t>(*recovered) == expected,
            "format/parse round trip changed binary64 bits or failed to canonicalize signed zero");
    Formatted answer;
    answer.length = *result;
    std::copy_n(displayed.begin(), answer.length, answer.bytes.begin());
    return answer;
}

void check_capacities(std::uint64_t encoding, std::string_view expected) {
    current_bits = encoding;
    for (std::size_t capacity = 0; capacity < expected.size(); ++capacity) {
        std::array<char, output_limit + guard_size * 2> storage{};
        storage.fill(guard);
        const auto before = storage;
        const auto result = format(encoding, std::span<char>{storage}.subspan(guard_size, capacity));
        require(!result, "format accepted insufficient output capacity");
        require(storage == before, "failed format modified destination or neighboring bytes");
    }
    for (const auto capacity : {expected.size(), output_limit, output_limit + guard_size}) {
        std::array<char, output_limit + guard_size * 3> storage{};
        storage.fill(guard);
        const auto result = format(encoding, std::span<char>{storage}.subspan(guard_size, capacity));
        require(result && *result == expected.size(), "format rejected sufficient output capacity");
        expect_guarded_output(storage, expected);
    }
    require(!format(encoding, {}), "format accepted a null empty destination");
}

void invalid_format_cases() {
    current_case = "finite out-of-range and nonfinite formatting leaves all bytes unchanged";
    constexpr std::array<std::uint64_t, 15> invalid{
        magnitude_limit + 1U, magnitude_limit + 2U, 0x4040800000000000ULL,
        0x4050000000000000ULL, 0x40f0000000000000ULL, 0x7fe0000000000000ULL,
        0x7fefffffffffffffULL, 0x7ff0000000000000ULL, 0x7ff0000000000001ULL,
        0x7ff0000000000042ULL, 0x7ff4000000000000ULL, 0x7ff8000000000000ULL,
        0x7ff8000000000042ULL, 0x7ffffffffffffffeULL, 0x7fffffffffffffffULL};
    for (const auto magnitude : invalid) {
        for (const auto sign : {std::uint64_t{0}, sign_mask}) {
            current_bits = sign | magnitude;
            for (const auto capacity : {std::size_t{0}, std::size_t{1}, output_limit,
                                        output_limit + guard_size}) {
                std::array<char, output_limit + guard_size * 3> storage{};
                storage.fill(guard);
                const auto before = storage;
                require(!format(current_bits, std::span<char>{storage}.subspan(guard_size, capacity)),
                        "format accepted out-of-range, infinity, or NaN encoding");
                require(storage == before, "invalid format modified destination or guards");
            }
            require(!format(current_bits, {}), "invalid format accepted empty destination");
        }
    }
}

void formatting_cases() {
    struct Expected { std::uint64_t encoding; std::string_view displayed; };
    constexpr std::array<Expected, 16> cases{{
        {0, "0"}, {sign_mask, "0"}, {one_bits, "1"}, {sign_mask | one_bits, "-1"},
        {magnitude_limit, "32"}, {sign_mask | magnitude_limit, "-32"},
        {0x3fe0000000000000ULL, "0.5"}, {0xbfe0000000000000ULL, "-0.5"},
        {0x3fb0000000000000ULL, "0.0625"}, {0x3f20000000000000ULL, "0.0001220703125"},
        {0x3ef0000000000000ULL, "1.52587890625e-5"},
        {one_bits + 1U, "1.0000000000000002"},
        {magnitude_limit - 1U, "31.999999999999996"},
        {1, "4.9406564584124654e-324"}, {sign_mask | 1U, "-4.9406564584124654e-324"},
        {0x0010000000000000ULL, "2.2250738585072014e-308"}}};
    current_case = "specified display policy and exact output capacity";
    bool reached_bound = false;
    for (const auto& fixture : cases) {
        const auto displayed = check_format(fixture.encoding);
        require(displayed.view() == fixture.displayed, "display differs from declared formatting policy");
        check_capacities(fixture.encoding, fixture.displayed);
        reached_bound = reached_bound || displayed.length == output_limit;
    }
    require(reached_bound, "formatting cases failed to exercise the 24-byte bound");
    invalid_format_cases();
}

std::uint64_t random_bits(std::uint64_t& state) {
    state ^= state << 13U;
    state ^= state >> 7U;
    state ^= state << 17U;
    return state;
}

void roundtrip_cases() {
    current_case = "both signs across every supported normal exponent and fraction edges";
    constexpr std::array<std::uint64_t, 6> fractions{
        0, 1, 0x0007ffffffffffffULL, 0x0008000000000000ULL, fraction_mask - 1U, fraction_mask};
    for (std::uint64_t exponent = 0; exponent < 1028; ++exponent) {
        for (const auto fraction : fractions) {
            const auto magnitude = (exponent << 52U) | fraction;
            (void)check_format(magnitude);
            (void)check_format(sign_mask | magnitude);
        }
    }
    (void)check_format(magnitude_limit);
    (void)check_format(sign_mask | magnitude_limit);

    current_case = "neighbors of every subnormal power of two";
    for (unsigned leading = 0; leading < 52; ++leading) {
        const auto power = std::uint64_t{1} << leading;
        for (const auto magnitude : {power - 1U, power, power + 1U}) {
            (void)check_format(magnitude);
            (void)check_format(sign_mask | magnitude);
        }
    }

    current_case = "deterministic additional binary64 mantissas";
    std::uint64_t generator = 0x029d9a7e6b154c83ULL;
    for (std::size_t index = 0; index < 1024; ++index) {
        const auto magnitude = random_bits(generator) % (magnitude_limit + 1U);
        (void)check_format(magnitude);
        (void)check_format(sign_mask | magnitude);
    }

    for (const auto& vector : oracle::vectors) {
        if (!vector.accepted) { continue; }
        current_case = vector.id;
        (void)check_format(vector.bits);
    }
}

#if RHODES_TEXT_TEST_MXCSR
unsigned supported_mxcsr() {
    alignas(16) std::array<unsigned char, 512> saved{};
#if defined(_MSC_VER)
    _fxsave(saved.data());
#else
    asm volatile("fxsave %0" : "=m"(saved));
#endif
    std::uint32_t mask = 0;
    std::memcpy(&mask, saved.data() + 28, sizeof(mask));
    return mask == 0U ? 0xffbfU : mask;
}
#endif

void environment_cases() {
    std::array<Formatted, std::size(oracle::vectors)> baseline{};
    current_case = "environment-independent formatting baseline";
    for (std::size_t index = 0; index < std::size(oracle::vectors); ++index) {
        if (oracle::vectors[index].accepted) { baseline[index] = check_format(oracle::vectors[index].bits); }
    }
    std::size_t environments = 0;
    constexpr std::array roundings{FE_TONEAREST, FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO};
#if RHODES_TEXT_TEST_MXCSR
    constexpr unsigned flush_output = 1U << 15U;
    constexpr unsigned flush_input = 1U << 6U;
    constexpr std::array<unsigned, 4> flushes{0, flush_output, flush_input, flush_output | flush_input};
    const auto supported = supported_mxcsr();
#elif RHODES_TEXT_TEST_FPCR
    constexpr std::uint64_t flush_output = std::uint64_t{1} << 24U;
    constexpr std::uint64_t flush_input = std::uint64_t{1} << 19U; // Optional half-precision FZ16.
    constexpr std::array<std::uint64_t, 4> flushes{
        0, flush_output, flush_input, flush_output | flush_input};
#else
    constexpr std::array<unsigned, 1> flushes{0};
#endif
    for (const auto rounding : roundings) {
        for (const auto flush : flushes) {
#if RHODES_TEXT_TEST_MXCSR
            if ((flush & ~supported) != 0U) { continue; }
#endif
            const Controls caller;
            {
                RestoreEnvironment restore;
                if (std::fesetround(rounding) != 0) {
                    std::cout << "SKIP unsupported rounding mode " << rounding << '\n';
                    continue;
                }
#if RHODES_TEXT_TEST_MXCSR
                const auto requested = (_mm_getcsr() & ~(flush_output | flush_input)) | flush;
                _mm_setcsr(requested);
                require(_mm_getcsr() == requested, "requested MXCSR controls did not take effect");
#elif RHODES_TEXT_TEST_FPCR
                const auto requested = (fpcr_controls() & ~(flush_output | flush_input)) | flush;
                set_fpcr_controls(requested);
                if (fpcr_controls() != requested) {
                    std::cout << "SKIP unsupported ARM FPCR flush mode " << flush << '\n';
                    continue;
                }
#endif
                try {
                    oracle_cases();
                    for (std::size_t index = 0; index < std::size(oracle::vectors); ++index) {
                        const auto& vector = oracle::vectors[index];
                        if (!vector.accepted) { continue; }
                        current_case = vector.id;
                        require(check_format(vector.bits).view() == baseline[index].view(),
                                "formatting depends on host rounding or flush controls");
                    }
                    invalid_format_cases();
                } catch (const std::exception& error) {
                    throw std::runtime_error("rounding=" + std::to_string(rounding) +
                        " flush=" + std::to_string(flush) + " " + error.what());
                }
                ++environments;
            }
            caller.expect_unchanged();
        }
    }
    require(environments != 0, "no supported floating-point environment was exercised");
    std::cout << "checked " << environments << " supported rounding/flush combinations\n";
}

void locale_cases() {
    current_case = "locale-independent decimal syntax";
    const auto* current = std::setlocale(LC_NUMERIC, nullptr);
    require(current != nullptr, "cannot read numeric locale");
    struct RestoreLocale {
        std::string saved;
        ~RestoreLocale() { (void)std::setlocale(LC_NUMERIC, saved.c_str()); }
    } restore{current};
    bool comma_locale = false;
    for (const auto* candidate : {"C", "de_DE.UTF-8", "fr_FR.UTF-8", "de_DE.utf8", "fr_FR.utf8",
                                  "German_Germany.1252", "French_France.1252"}) {
        if (std::setlocale(LC_NUMERIC, candidate) == nullptr) { continue; }
        oracle_cases();
        current_case = "locale-independent decimal display";
        expect_parse("1.5", 0x3ff8000000000000ULL);
        expect_rejected("1,5");
        require(check_format(0x3ff8000000000000ULL).view() == "1.5", "locale changed decimal point");
        if (std::string_view{std::localeconv()->decimal_point} == ",") {
            comma_locale = true;
            std::cout << "checked comma-decimal locale " << candidate << '\n';
            break;
        }
    }
    if (!comma_locale) { std::cout << "SKIP comma-decimal locale: no installed candidate\n"; }
}

constexpr bool fixture_bounds() {
    for (const auto& vector : oracle::vectors) {
        if (vector.id.empty() || vector.input.size() > input_limit) { return false; }
        for (const auto character : vector.input) {
            if (static_cast<unsigned char>(character) > 127U) { return false; }
        }
        if (vector.accepted && (vector.bits & ~sign_mask) > magnitude_limit) { return false; }
    }
    return true;
}

static_assert(std::size(oracle::vectors) != 0 && fixture_bounds());
static_assert(std::string_view{"-4.9406564584124654e-324"}.size() == output_limit);
static_assert(std::is_same_v<decltype(text::parse_decimal(std::string_view{})), std::optional<double>>);
static_assert(std::is_same_v<decltype(text::format_decimal(
                  std::declval<double>(), std::span<char>{})), std::optional<std::size_t>>);
static_assert(noexcept(text::parse_decimal(std::string_view{})));
static_assert(noexcept(text::format_decimal(std::declval<double>(), std::span<char>{})));
} // namespace

int main(int argc, char** argv) {
    const std::string_view selected = argc > 1 ? argv[1] : "all";
    try {
        RestoreEnvironment restore;
        require(std::fesetenv(FE_DFL_ENV) == 0, "cannot select default floating-point environment");
        bool found = false;
        const auto run = [&](std::string_view name, auto test) {
            if (selected != "all" && selected != name) { return; }
            current_case = name;
            current_bits = 0;
            checks = 0;
            conversions = 0;
            test();
            found = true;
            std::cout << "PASS " << name << " (" << conversions << " conversions, "
                      << checks << " always-active checks)\n";
        };
        run("oracle", oracle_cases);
        run("bounds", bound_cases);
        run("formatting", formatting_cases);
        run("roundtrip", roundtrip_cases);
        run("environments", environment_cases);
        run("locale", locale_cases);
        require(found, "unknown control-text selector");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << selected << ": " << error.what() << '\n';
        return 1;
    }
}
