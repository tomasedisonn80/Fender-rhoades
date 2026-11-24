#include "tests/cpp/vst3_test_support.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cfenv>
#include <clocale>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

#if defined(__SSE__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 1)
#define RHODES_TEXT_TEST_MXCSR 1
#include <xmmintrin.h>
#if defined(_MSC_VER)
#include <immintrin.h>
#endif
#else
#define RHODES_TEXT_TEST_MXCSR 0
#endif

namespace {
namespace sdk = Steinberg;
namespace vst = Steinberg::Vst;
namespace plugin = rhodes::vst3;
namespace host = rhodes::vst3::test_host;
using Text = std::array<vst::TChar, 128>;
using Packet = std::array<std::uint8_t, 16>;

#if defined(RHODES_QUADRATURE_VST3)
static_assert(plugin::kStateModel == 3 && plugin::kQuadratureAudio);
#elif defined(RHODES_FILTERED_VST3)
static_assert(plugin::kStateModel == 2 && plugin::kFilteredAudio);
#else
static_assert(plugin::kStateModel == 1 && !plugin::kFilteredAudio);
#endif

constexpr std::uint64_t output_sentinel = 0x7ff80000000012abULL;
constexpr unsigned ftz = 1U << 15U;
constexpr unsigned daz = 1U << 6U;
std::size_t checks = 0;
std::size_t conversions = 0;
std::size_t environments = 0;
std::string current_case;

void check(bool condition, std::string_view message) {
    ++checks;
    if (!condition) {
        throw std::runtime_error(current_case + ": " + std::string{message});
    }
}

std::uint64_t bits(double value) { return std::bit_cast<std::uint64_t>(value); }
double value(std::uint64_t encoding) { return std::bit_cast<double>(encoding); }

constexpr auto parameter_ids() {
    std::array<vst::ParamID, 18> ids{plugin::kGainId, plugin::kSustainId};
    for (std::size_t channel = 0; channel < 16; ++channel) {
        ids[channel + 2] = plugin::kAllNotesOffBaseId + static_cast<vst::ParamID>(channel);
    }
    return ids;
}
constexpr auto ids = parameter_ids();

Text text(std::string_view ascii) {
    check(ascii.size() < 128, "invalid text fixture length");
    Text result{};
    for (std::size_t index = 0; index < ascii.size(); ++index) {
        result[index] = static_cast<vst::TChar>(ascii[index]);
    }
    return result;
}

std::string ascii(const vst::TChar* source) {
    std::string result;
    for (std::size_t index = 0; index < 128; ++index) {
        if (source[index] == 0) { return result; }
        check(static_cast<std::uint16_t>(source[index]) <= 127,
              "formatting produced non-ASCII text");
        result += static_cast<char>(source[index]);
    }
    check(false, "formatting did not terminate within 128 code units");
    return {};
}

// Sticky exception flags may change in the standard library. Rounding, flush
// controls and exception masks belong to the caller and must remain unchanged.
struct Controls {
    int rounding = std::fegetround();
#if RHODES_TEXT_TEST_MXCSR
    unsigned mxcsr = _mm_getcsr() & ~0x3fU;
#endif
    void expect_unchanged() const {
        check(std::fegetround() == rounding, "text conversion changed rounding mode");
#if RHODES_TEXT_TEST_MXCSR
        check((_mm_getcsr() & ~0x3fU) == mxcsr,
              "text conversion changed MXCSR control bits");
#endif
    }
};

// Expected packets are constructed independently of the production state codec.
Packet packet(std::uint32_t gain, bool sustain) {
    Packet result{'R', 'H', 'S', 'T', 1, 0,
        static_cast<std::uint8_t>(plugin::kStateModel), 0};
    for (std::size_t index = 0; index < 4; ++index) {
        result[index + 8] = static_cast<std::uint8_t>((gain >> (index * 8U)) & 255U);
    }
    if (sustain) { result[14] = 0x80U; result[15] = 0x3fU; }
    return result;
}

Packet save(vst::IEditController& controller) {
    host::MemoryStream stream;
    check(controller.getState(&stream) == sdk::kResultOk, "configuration save failed");
    check(stream.extent == 16 && stream.position == 16,
          "text conversion changed the 16-byte configuration format");
    Packet result{};
    std::copy_n(stream.bytes.begin(), result.size(), result.begin());
    return result;
}

struct Snapshot {
    Packet configuration;
    std::array<std::uint64_t, ids.size()> normalized{};

    explicit Snapshot(vst::IEditController& controller) : configuration(save(controller)) {
        for (std::size_t index = 0; index < ids.size(); ++index) {
            normalized[index] = bits(controller.getParamNormalized(ids[index]));
        }
    }
    void expect_unchanged(vst::IEditController& controller) const {
        check(save(controller) == configuration, "text conversion changed saved configuration");
        for (std::size_t index = 0; index < ids.size(); ++index) {
            check(bits(controller.getParamNormalized(ids[index])) == normalized[index],
                  "text conversion changed a gain, sustain or command parameter");
        }
    }
};

void seed(vst::IEditController& controller) {
    check(controller.getParameterCount() == 18, "controller parameter count differs");
    check(controller.setParamNormalized(plugin::kGainId, 0.125) == sdk::kResultOk &&
          controller.setParamNormalized(plugin::kSustainId, 1.0) == sdk::kResultOk,
          "configuration seed failed");
    for (std::size_t index = 2; index < ids.size(); ++index) {
        // Exact binary fractions give every command a nondefault retained value.
        const double command = static_cast<double>(index - 1) / 32.0;
        check(controller.setParamNormalized(ids[index], command) == sdk::kResultOk,
              "command seed failed");
    }
    check(save(controller) == packet(0x3e000000U, true), "seed packet differs");
}

void accept(vst::IEditController& controller, vst::ParamID id, vst::TChar* input,
            std::uint64_t expected) {
    const Snapshot before(controller);
    double output = value(output_sentinel);
    const Controls controls;
    ++conversions;
    const auto result = controller.getParamValueByString(id, input, output);
    controls.expect_unchanged();
    check(result == sdk::kResultOk, "valid text rejected");
    check(bits(output) == expected, "parsed normalized binary64 bits differ");
    before.expect_unchanged(controller);
}

void accept(vst::IEditController& controller, vst::ParamID id,
            std::string_view input, std::uint64_t expected) {
    current_case = "parse id=" + std::to_string(id) + " text=" + std::string{input};
    auto source = text(input);
    const auto original = source;
    accept(controller, id, source.data(), expected);
    check(source == original, "parsing modified the input buffer");
}

void reject(vst::IEditController& controller, vst::ParamID id, vst::TChar* input,
            bool initialized = true) {
    double output = value(output_sentinel);
    const Controls controls;
    ++conversions;
    if (initialized) {
        const Snapshot before(controller);
        const auto result = controller.getParamValueByString(id, input, output);
        controls.expect_unchanged();
        check(result != sdk::kResultOk, "invalid text accepted");
        check(bits(output) == output_sentinel, "rejected parse changed caller output");
        before.expect_unchanged(controller);
    } else {
        check(controller.getParamValueByString(id, input, output) != sdk::kResultOk,
              "inactive controller accepted text");
        controls.expect_unchanged();
        check(bits(output) == output_sentinel, "inactive parse changed caller output");
    }
}

void reject(vst::IEditController& controller, vst::ParamID id, std::string_view input) {
    current_case = "reject parse id=" + std::to_string(id) + " text=" + std::string{input};
    auto source = text(input);
    const auto original = source;
    reject(controller, id, source.data());
    check(source == original, "rejected parse modified its input buffer");
}

struct GuardedText {
    vst::TChar before = static_cast<vst::TChar>(0x5a5a);
    Text content{};
    vst::TChar after = static_cast<vst::TChar>(0x6b6b);
    GuardedText() {
        for (std::size_t index = 0; index < content.size(); ++index) {
            content[index] = static_cast<vst::TChar>(0x5000U + index);
        }
    }
    void expect_guards() const {
        check(before == static_cast<vst::TChar>(0x5a5a) &&
              after == static_cast<vst::TChar>(0x6b6b), "formatting overwrote caller guards");
    }
};

void format(vst::IEditController& controller, vst::ParamID id, std::uint64_t input,
            std::uint64_t expected, std::string_view exact = {}) {
    current_case = "format id=" + std::to_string(id) + " bits=" + std::to_string(input);
    const Snapshot before(controller);
    GuardedText output;
    const Controls controls;
    ++conversions;
    const auto result = controller.getParamStringByValue(id, value(input), output.content.data());
    controls.expect_unchanged();
    check(result == sdk::kResultOk, "valid normalized formatting rejected");
    output.expect_guards();
    const auto rendered = ascii(output.content.data());
    check(!rendered.empty(), "formatter returned empty text");
    if (id == plugin::kSustainId) {
        check(rendered == exact, "formatted sustain label differs");
    }
    if (id != plugin::kSustainId) {
        check(rendered.find_first_not_of("0123456789.eE+-") == std::string::npos,
              "numeric formatter emitted a suffix, label or locale-dependent punctuation");
    }
    before.expect_unchanged(controller);
    const auto source = output.content;
    accept(controller, id, output.content.data(), expected);
    check(output.content == source, "round-trip parsing changed formatted input");
}

void reject_format(vst::IEditController& controller, vst::ParamID id,
                   std::uint64_t input, bool initialized = true) {
    current_case = "reject format id=" + std::to_string(id) + " bits=" + std::to_string(input);
    GuardedText output;
    const auto original = output.content;
    const Controls controls;
    ++conversions;
    if (initialized) {
        const Snapshot before(controller);
        check(controller.getParamStringByValue(id, value(input), output.content.data()) !=
              sdk::kResultOk, "invalid formatting input accepted");
        controls.expect_unchanged();
        before.expect_unchanged(controller);
    } else {
        check(controller.getParamStringByValue(id, value(input), output.content.data()) !=
              sdk::kResultOk, "inactive controller formatted text");
        controls.expect_unchanged();
    }
    output.expect_guards();
    check(output.content == original, "rejected formatting changed caller output");
}

struct ParseCase { std::string_view input; std::uint64_t normalized; };

void valid_parse_cases(vst::IEditController& controller) {
    // Literal oracles encode the declared binary64 parse followed by nearest-even
    // binary32 gain storage. They do not call the production control helpers.
    constexpr ParseCase gain_cases[]{
        {"0", 0}, {"-0", 0}, {"+0.0", 0}, {"-0e+8x", 0},
        {"16", 0x3fe0000000000000ULL}, {"32", 0x3ff0000000000000ULL},
        {"16x", 0x3fe0000000000000ULL}, {"16 x", 0x3fe0000000000000ULL},
        {" \t\r\n\f\v+1.6e+1 \tx\v ", 0x3fe0000000000000ULL},
        {".5", 0x3f90000000000000ULL}, {"1.", 0x3fa0000000000000ULL},
        {"00032.0000", 0x3ff0000000000000ULL}, {"3.2E1", 0x3ff0000000000000ULL},
        {"32.000000000000000001", 0x3ff0000000000000ULL},
        {"31.999999999999999999", 0x3ff0000000000000ULL},
        {"0.1", 0x3f699999a0000000ULL},
        {"16.00000095367431640625", 0x3fe0000000000000ULL},
        {"16.00000095367432", 0x3fe0000020000000ULL},
        {"16.00000286102294921875", 0x3fe0000040000000ULL},
        {"31.99999904632568359375", 0x3ff0000000000000ULL},
        {"3.2e-39", 0x37a16c2000000000ULL},
        {"2.2420775429197073134779673332638658e-44", 0},
        {"4.4841550858394146269559346665277316e-44", 0x36a0000000000000ULL},
        {"6.7262326287591219404339019997915974e-44", 0x36b0000000000000ULL},
        {"1e-308", 0}, {"5e-324", 0}
    };
    for (const auto& item : gain_cases) {
        accept(controller, plugin::kGainId, item.input, item.normalized);
    }
    constexpr ParseCase command_cases[]{
        {"0", 0}, {"-0", 0}, {"+0.0", 0}, {"-0e+8", 0},
        {"1", 0x3ff0000000000000ULL}, {"1.", 0x3ff0000000000000ULL},
        {"0001.000", 0x3ff0000000000000ULL},
        {"1.000000000000000001", 0x3ff0000000000000ULL},
        {".5", 0x3fe0000000000000ULL}, {"5E-1", 0x3fe0000000000000ULL},
        {" \t\r\n\f\v+1.25e-1\v ", 0x3fc0000000000000ULL},
        {"0.1", 0x3fb999999999999aULL},
        {"0.5000000298023223876953125", 0x3fe0000010000000ULL},
        {"0.9999999999999999", 0x3fefffffffffffffULL},
        {"1e-40", 0x37a16c262777579cULL},
        {"2.2250738585072014e-308", 0x0010000000000000ULL},
        {"2.225073858507201e-308", 0x000fffffffffffffULL},
        {"1e-308", 0x000730d67819e8d2ULL},
        {"5e-324", 0x0000000000000001ULL}, {"1e-323", 0x0000000000000002ULL}
    };
    for (std::size_t index = 2; index < ids.size(); ++index) {
        for (const auto& item : command_cases) {
            accept(controller, ids[index], item.input, item.normalized);
        }
    }
    accept(controller, plugin::kSustainId, "Off", 0);
    accept(controller, plugin::kSustainId, "On", 0x3ff0000000000000ULL);
}

void invalid_parse_cases(vst::IEditController& controller) {
    constexpr std::array malformed{
        "", " ", "\t\n\r\f\v", ".", "+", "-", "+.", "-.", "--0", "++0", "+-0",
        "nan", "NaN", "NAN", "nan(1)", "+nan", "inf", "-inf", "infinity", "+Infinity",
        "0x0", "0x1p-1", "0X10", "1,0", "1_0", "1'0", "16junk", "0junk", "1e",
        "1e+", "1e-", "e1", "1e1e1", "1..0", "1 0", "+ 1", "1 e0", "1\t.0",
        "1e309", "-1e309", "1e99999", "1e-99999", "1e-324", "-1e-324",
        "-0.1", "-1e-40", "-5e-324", "-2.225073858507201e-308",
        "1\x01", "\x07" "1", "1\x1f", "1\x7f", "Off", "On"
    };
    for (const auto id : ids) {
        if (id == plugin::kSustainId) { continue; }
        for (const auto* input : malformed) { reject(controller, id, input); }
        for (const auto* input : {"x", "1X", "1xx", "1 x x", "1x junk", "1x0"}) {
            reject(controller, id, input);
        }
    }
    for (const auto* input : {"32.00000000000001", "32.000001", "32.1", "33", "1e2"}) {
        reject(controller, plugin::kGainId, input);
    }
    for (std::size_t index = 2; index < ids.size(); ++index) {
        for (const auto* input : {"1.0000000000000002", "1.000001", "1.1", "2", "16",
                                  "0x", "0 x", "1x", "0.5 x", "5e-1x"}) {
            reject(controller, ids[index], input);
        }
    }
    for (const auto* input : {"", "off", "OFF", "on", "ON", "On ", " Off", "0", "1",
                              "0.5", "2", "nan", "Onx", "Offjunk", "\tOn"}) {
        reject(controller, plugin::kSustainId, input);
    }
}

void bounded_and_unicode_cases(vst::IEditController& controller) {
    for (const auto id : ids) {
        current_case = "bounded/UTF-16 id=" + std::to_string(id);
        reject(controller, id, nullptr);
        Text unterminated{};
        unterminated.fill(static_cast<vst::TChar>('0'));
        const auto original = unterminated;
        reject(controller, id, unterminated.data());
        check(unterminated == original, "unterminated input changed");
        std::array<vst::TChar, 129> late_terminator{};
        std::fill_n(late_terminator.begin(), 128, static_cast<vst::TChar>('0'));
        const auto late_original = late_terminator;
        reject(controller, id, late_terminator.data());
        check(late_terminator == late_original, "late-terminated input changed");
        for (const auto unit : {0x0080U, 0x00a0U, 0x0661U, 0x2212U, 0xff11U,
                                0xd800U, 0xdc00U, 0xffffU}) {
            auto input = text(id == plugin::kSustainId ? "On" : "1");
            input[id == plugin::kSustainId ? 2U : 1U] = static_cast<vst::TChar>(unit);
            const auto unicode_original = input;
            reject(controller, id, input.data());
            check(input == unicode_original, "UTF-16 rejection changed input");
        }
        if (id == plugin::kSustainId) { continue; }
        auto last_terminator = original;
        last_terminator[126] = static_cast<vst::TChar>('1');
        last_terminator[127] = 0;
        accept(controller, id, last_terminator.data(), id == plugin::kGainId ?
               0x3fa0000000000000ULL : 0x3ff0000000000000ULL);
    }
    auto embedded_nul = text("16");
    embedded_nul[3] = static_cast<vst::TChar>(0xffff);
    current_case = "first NUL ends the input";
    accept(controller, plugin::kGainId, embedded_nul.data(), 0x3fe0000000000000ULL);
}

void valid_format_cases(vst::IEditController& controller) {
    struct FormatCase {
        std::uint64_t input;
        std::uint64_t normalized;
    };
    constexpr FormatCase gain_cases[]{
        {0, 0}, {0x8000000000000000ULL, 0},
        {0x3fd0000000000000ULL, 0x3fd0000000000000ULL},
        {0x3fe0000000000000ULL, 0x3fe0000000000000ULL},
        {0x3ff0000000000000ULL, 0x3ff0000000000000ULL},
        {0x3fb999999999999aULL, 0x3fb99999a0000000ULL},
        {0x3fe0000010000000ULL, 0x3fe0000000000000ULL},
        {0x3fe0000030000000ULL, 0x3fe0000040000000ULL},
        {0x3fefffffffffffffULL, 0x3ff0000000000000ULL},
        {0x37a16c262777579cULL, 0x37a16c2000000000ULL},
        {0x36a0000000000000ULL, 0x36a0000000000000ULL},
        {0x3690000000000000ULL, 0},
        {0x3690000000000001ULL, 0x36a0000000000000ULL},
        {0x36a8000000000000ULL, 0x36b0000000000000ULL},
        {0x0000000000000001ULL, 0}
    };
    for (const auto& item : gain_cases) {
        format(controller, plugin::kGainId, item.input, item.normalized);
    }
    constexpr FormatCase command_cases[]{
        {0, 0}, {0x8000000000000000ULL, 0},
        {0x3ff0000000000000ULL, 0x3ff0000000000000ULL},
        {0x3fe0000000000000ULL, 0x3fe0000000000000ULL},
        {0x3fb999999999999aULL, 0x3fb999999999999aULL},
        {0x3fe0000010000000ULL, 0x3fe0000010000000ULL},
        {0x3fefffffffffffffULL, 0x3fefffffffffffffULL},
        {0x37a16c262777579cULL, 0x37a16c262777579cULL},
        {0x0010000000000000ULL, 0x0010000000000000ULL},
        {0x000fffffffffffffULL, 0x000fffffffffffffULL},
        {0x000730d67819e8d2ULL, 0x000730d67819e8d2ULL},
        {0x000123456789abcdULL, 0x000123456789abcdULL},
        {0x0000000000000001ULL, 0x0000000000000001ULL},
        {0x0000000000000002ULL, 0x0000000000000002ULL}
    };
    for (std::size_t index = 2; index < ids.size(); ++index) {
        for (const auto& item : command_cases) {
            format(controller, ids[index], item.input, item.normalized);
        }
    }
    for (const auto input : {0ULL, 0x8000000000000000ULL, 1ULL, 0x3fdfffffffffffffULL}) {
        format(controller, plugin::kSustainId, input, 0, "Off");
    }
    for (const auto input : {0x3fe0000000000000ULL, 0x3fefffffffffffffULL,
                             0x3ff0000000000000ULL}) {
        format(controller, plugin::kSustainId, input, 0x3ff0000000000000ULL, "On");
    }
    vst::ParameterInfo info{};
    check(controller.getParameterInfo(0, info) == sdk::kResultOk &&
          info.id == plugin::kGainId && ascii(info.units) == "x",
          "gain unit metadata lost x");
}

void invalid_format_and_id_cases(vst::IEditController& controller) {
    constexpr std::array invalid{
        0xbff0000000000000ULL, 0xbfb999999999999aULL, 0x8000000000000001ULL,
        0x800fffffffffffffULL, 0x3ff0000000000001ULL, 0x4000000000000000ULL,
        0x7fefffffffffffffULL, 0x7ff0000000000000ULL, 0xfff0000000000000ULL,
        0x7ff8000000000000ULL, 0xfff8000000000123ULL, 0x7ff0000000000001ULL
    };
    for (const auto id : ids) {
        for (const auto encoding : invalid) { reject_format(controller, id, encoding); }
        current_case = "null format output id=" + std::to_string(id);
        const Snapshot before(controller);
        const Controls controls;
        ++conversions;
        check(controller.getParamStringByValue(id, 0.5, nullptr) != sdk::kResultOk,
              "null formatting output accepted");
        controls.expect_unchanged();
        before.expect_unchanged(controller);
    }
    for (const auto id : {vst::ParamID{2}, vst::ParamID{0x0fff}, vst::ParamID{0x1010},
                          vst::ParamID{0xffffffffU}}) {
        reject(controller, id, "0.5");
        reject_format(controller, id, 0x3fe0000000000000ULL);
    }
}

void recall_cases(vst::IEditController& controller) {
    current_case = "valid recall after conversion failures";
    const Snapshot original(controller);
    for (const bool component : {false, true}) {
        const auto expected = component ? packet(0x3d800000U, true) : packet(0x3f400000U, false);
        host::MemoryStream input;
        std::copy(expected.begin(), expected.end(), input.bytes.begin());
        input.extent = 16;
        input.transfer_limit = 1;
        const auto result = component ? controller.setComponentState(&input) : controller.setState(&input);
        check(result == sdk::kResultOk, "final valid recall failed");
        check(save(controller) == expected, "final recall packet changed or acquired command state");
        check(bits(controller.getParamNormalized(plugin::kGainId)) ==
              (component ? 0x3fb0000000000000ULL : 0x3fe8000000000000ULL) &&
              bits(controller.getParamNormalized(plugin::kSustainId)) ==
              (component ? 0x3ff0000000000000ULL : 0ULL), "recalled controls differ");
        for (std::size_t index = 2; index < ids.size(); ++index) {
            check(bits(controller.getParamNormalized(ids[index])) == original.normalized[index],
                  "configuration recall reset a transient command value");
        }
    }
}

void inactive_cases(vst::IEditController& controller) {
    check(controller.getParameterCount() == 0, "inactive controller retained parameters");
    auto source = text("0.5");
    const auto original = source;
    for (const auto id : ids) {
        current_case = "inactive text id=" + std::to_string(id);
        reject(controller, id, source.data(), false);
        reject(controller, id, nullptr, false);
        reject_format(controller, id, 0x3fe0000000000000ULL, false);
        check(controller.getParamStringByValue(id, 0.5, nullptr) != sdk::kResultOk,
              "inactive controller accepted null output");
    }
    check(source == original, "inactive parse changed input");
    host::MemoryStream stream;
    stream.bytes.fill(0xa5U);
    const auto original_stream = stream.bytes;
    check(controller.getState(&stream) != sdk::kResultOk && stream.bytes == original_stream &&
          stream.extent == 0 && stream.position == 0,
          "inactive conversions created or wrote saved configuration");
    check(controller.getParameterCount() == 0, "inactive text conversion created parameters");
}

void lifecycle_cases() {
    plugin::RhodesController implementation;
    vst::IEditController& controller = implementation;
    current_case = "uninitialized controller";
    inactive_cases(controller);
    check(controller.initialize(nullptr) == sdk::kResultOk, "lifecycle initialize failed");
    seed(controller);
    check(controller.terminate() == sdk::kResultOk, "lifecycle terminate failed");
    current_case = "terminated controller";
    inactive_cases(controller);
    check(controller.initialize(nullptr) == sdk::kResultOk, "lifecycle reinitialize failed");
    seed(controller);
    accept(controller, plugin::kGainId, "16 x", 0x3fe0000000000000ULL);
    format(controller, plugin::kSustainId, 0x3fe0000000000000ULL, 0x3ff0000000000000ULL, "On");
    recall_cases(controller);
    check(controller.terminate() == sdk::kResultOk, "final lifecycle terminate failed");
}

void all_cases() {
    host::ControllerHost instance;
    vst::IEditController& controller = instance.controller;
    seed(controller);
    valid_parse_cases(controller);
    invalid_parse_cases(controller);
    bounded_and_unicode_cases(controller);
    valid_format_cases(controller);
    invalid_format_and_id_cases(controller);
    recall_cases(controller);
    lifecycle_cases();
}

struct RestoreEnvironment {
    std::fenv_t original{};
#if RHODES_TEXT_TEST_MXCSR
    unsigned mxcsr = _mm_getcsr();
#endif
    RestoreEnvironment() { check(std::fegetenv(&original) == 0, "cannot save floating environment"); }
    ~RestoreEnvironment() {
        (void)std::fesetenv(&original);
#if RHODES_TEXT_TEST_MXCSR
        _mm_setcsr(mxcsr);
#endif
    }
};

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
    constexpr std::array roundings{FE_TONEAREST, FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO};
    for (const auto rounding : roundings) {
        for (const auto flush : {0U, ftz, daz, ftz | daz}) {
            RestoreEnvironment restore;
#if RHODES_TEXT_TEST_MXCSR
            if ((flush & ~supported_mxcsr()) != 0U) { continue; }
#else
            if (flush != 0U) { continue; }
#endif
            if (std::fesetround(rounding) != 0) { continue; }
#if RHODES_TEXT_TEST_MXCSR
            _mm_setcsr((_mm_getcsr() & ~(ftz | daz | 0x3fU)) | flush);
#endif
            ++environments;
            try {
                all_cases();
            } catch (const std::exception& error) {
                throw std::runtime_error("rounding=" + std::to_string(rounding) +
                    " flush=" + std::to_string(flush) + " " + error.what());
            }
        }
    }
    check(environments != 0, "no supported floating-point environment ran");
}

void locale_cases() {
    const auto* current = std::setlocale(LC_NUMERIC, nullptr);
    check(current != nullptr, "cannot inspect numeric locale");
    const std::string saved{current};
    struct RestoreLocale {
        std::string name;
        ~RestoreLocale() { (void)std::setlocale(LC_NUMERIC, name.c_str()); }
    } restore{saved};
    for (const auto* candidate : {"de_DE.UTF-8", "fr_FR.UTF-8", "de_DE.utf8",
                                  "fr_FR.utf8", "German_Germany.1252", "French_France.1252"}) {
        if (std::setlocale(LC_NUMERIC, candidate) == nullptr ||
            std::string_view{std::localeconv()->decimal_point} != ",") { continue; }
        host::ControllerHost instance;
        vst::IEditController& controller = instance.controller;
        seed(controller);
        accept(controller, plugin::kGainId, "0.1", 0x3f699999a0000000ULL);
        reject(controller, plugin::kGainId, "0,1");
        format(controller, plugin::kGainId, 0x3fa0000000000000ULL, 0x3fa0000000000000ULL);
        accept(controller, ids[2], "0.1", 0x3fb999999999999aULL);
        reject(controller, ids[2], "0,1");
        format(controller, ids[2], 0x3fb999999999999aULL, 0x3fb999999999999aULL);
        std::cout << "PASS comma-decimal locale " << candidate << '\n';
        return;
    }
    std::cout << "SKIP comma-decimal locale: no installed candidate\n";
}
} // namespace

int main() {
    try {
        environment_cases();
        locale_cases();
        std::cout << "PASS controller text model=" << plugin::kStateModel << " ("
                  << environments << " floating-point environments, " << conversions
                  << " conversions, " << checks << " always-active checks)\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL controller text model=" << plugin::kStateModel << ": "
                  << error.what() << '\n';
        return 1;
    }
}
