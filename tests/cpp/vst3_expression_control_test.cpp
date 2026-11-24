#include "plugins/vst3/controller.hpp"
#include "pluginterfaces/base/ibstream.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cfenv>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

#if defined(__SSE__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 1)
#define RHODES_EXPRESSION_MXCSR 1
#include <xmmintrin.h>
#if defined(_MSC_VER)
#include <immintrin.h>
#endif
#else
#define RHODES_EXPRESSION_MXCSR 0
#endif

#if (defined(__i386__) || defined(__x86_64__)) && (defined(__GNUC__) || defined(__clang__))
#define RHODES_EXPRESSION_X87 1
#else
#define RHODES_EXPRESSION_X87 0
#endif

#if defined(__aarch64__) && (defined(__GNUC__) || defined(__clang__))
#define RHODES_EXPRESSION_FPCR 1
#else
#define RHODES_EXPRESSION_FPCR 0
#endif

#if defined(_MSC_VER)
#define RHODES_EXPRESSION_NOINLINE __declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
#define RHODES_EXPRESSION_NOINLINE __attribute__((noinline))
#else
#define RHODES_EXPRESSION_NOINLINE
#endif

namespace {
namespace sdk = Steinberg;
namespace vst = Steinberg::Vst;
using Text = std::array<vst::TChar, 128>;

// These are public-contract literals, not aliases of the production ID table.
constexpr vst::ParamID gain_id = 0, sustain_id = 1, expression_id = 2;
constexpr vst::ParamID command_base = 0x1000U;
constexpr std::size_t parameter_count = 19;
#if defined(RHODES_QUADRATURE_VST3)
constexpr std::uint8_t model = 3;
#elif defined(RHODES_FILTERED_VST3)
constexpr std::uint8_t model = 2;
#else
constexpr std::uint8_t model = 1;
#endif
constexpr std::uint64_t unity = 0x3ff0000000000000ULL;
constexpr std::uint64_t output_sentinel = 0x7ff80000000012abULL;
constexpr unsigned ftz = 1U << 15U, daz = 1U << 6U;
std::size_t checks = 0, environments = 0;
std::string current_case;

void check(bool condition, std::string_view message) {
    ++checks;
    if (!condition) {
        throw std::runtime_error(current_case + ": " + std::string{message});
    }
}

static_assert(sizeof(double) == 8 && std::numeric_limits<double>::is_iec559);
std::uint64_t bits(double value) { return std::bit_cast<std::uint64_t>(value); }

// Runtime bit inputs and a separate public-call boundary prevent a compiler's
// constant folding from standing in for execution under the selected FP mode.
RHODES_EXPRESSION_NOINLINE double value(std::uint64_t encoding) {
    volatile std::uint64_t runtime = encoding;
    return std::bit_cast<double>(static_cast<std::uint64_t>(runtime));
}
RHODES_EXPRESSION_NOINLINE sdk::tresult set(vst::IEditController& controller,
                                          vst::ParamID id, double input) {
    return controller.setParamNormalized(id, input);
}

#if RHODES_EXPRESSION_X87
std::uint16_t x87_controls() {
    std::uint16_t result = 0;
    asm volatile("fnstcw %0" : "=m"(result));
    return result;
}
#endif
#if RHODES_EXPRESSION_FPCR
std::uint64_t arm_controls() {
    std::uint64_t result = 0;
    asm volatile("mrs %0, fpcr" : "=r"(result));
    return result;
}
#endif

// Conversion calls may raise sticky status flags. They must preserve the host's
// rounding, exception masks and flush controls; the outer mode guard restores
// the complete saved environment, including sticky flags, after each mode.
struct Controls {
    int rounding = std::fegetround();
#if RHODES_EXPRESSION_MXCSR
    unsigned mxcsr = _mm_getcsr() & ~0x3fU;
#endif
#if RHODES_EXPRESSION_X87
    std::uint16_t x87 = x87_controls();
#endif
#if RHODES_EXPRESSION_FPCR
    std::uint64_t fpcr = arm_controls();
#endif
    void expect_unchanged() const {
        check(std::fegetround() == rounding, "host rounding mode changed");
#if RHODES_EXPRESSION_MXCSR
        check((_mm_getcsr() & ~0x3fU) == mxcsr, "host MXCSR control bits changed");
#endif
#if RHODES_EXPRESSION_X87
        check(x87_controls() == x87, "host x87 control word changed");
#endif
#if RHODES_EXPRESSION_FPCR
        check(arm_controls() == fpcr, "host ARM FPCR changed");
#endif
    }
};

Text text(std::string_view input) {
    check(input.size() < 128, "text fixture exceeds public bound");
    Text result{};
    for (std::size_t index = 0; index < input.size(); ++index) {
        result[index] = static_cast<vst::TChar>(input[index]);
    }
    return result;
}

std::string ascii(const vst::TChar* input) {
    std::string result;
    for (std::size_t index = 0; index < 128; ++index) {
        if (input[index] == 0) { return result; }
        check(static_cast<std::uint16_t>(input[index]) < 128U,
              "display is not bounded ASCII");
        result += static_cast<char>(input[index]);
    }
    check(false, "display is not terminated within String128");
    return {};
}

constexpr auto ids() {
    std::array<vst::ParamID, parameter_count> result{gain_id, sustain_id, expression_id};
    for (std::size_t channel = 0; channel < 16; ++channel) {
        result[channel + 3] = command_base + static_cast<vst::ParamID>(channel);
    }
    return result;
}
constexpr auto parameter_ids = ids();

struct Packet {
    std::array<std::uint8_t, 24> bytes{};
    std::size_t size = 0;
    friend bool operator==(const Packet&, const Packet&) = default;
};

// Independent little-endian fixture construction. This test never calls the
// production codec, normalized-value helpers, parser or formatter as an oracle.
void word(Packet& packet, std::size_t at, std::uint32_t encoding) {
    for (std::size_t byte = 0; byte < 4; ++byte) {
        packet.bytes[at + byte] = static_cast<std::uint8_t>(encoding >> (8U * byte));
    }
}

Packet packet(unsigned version, std::uint32_t gain, std::uint32_t sustain,
              std::uint32_t expression = 0x3f800000U) {
    Packet result{{'R', 'H', 'S', 'T', static_cast<std::uint8_t>(version), 0, model, 0},
                  version == 1U ? 16U : 20U};
    word(result, 8, gain);
    word(result, 12, sustain);
    if (version != 1U) { word(result, 16, expression); }
    return result;
}

struct Stream final : sdk::IBStream {
    enum class Fault { none, error, zero_progress, error_after_byte };
    std::array<std::uint8_t, 64> bytes{};
    std::size_t position = 0, extent = 0, transfer_limit = 64, read_calls = 0;
    std::size_t fail_at = 64;
    Fault fault = Fault::none;

    Stream() = default;
    explicit Stream(const Packet& input) : extent(input.size) {
        std::copy(input.bytes.begin(), input.bytes.end(), bytes.begin());
    }
    sdk::uint32 PLUGIN_API addRef() override { return 1; }
    sdk::uint32 PLUGIN_API release() override { return 1; }
    sdk::tresult PLUGIN_API queryInterface(const sdk::TUID interface_id, void** object) override {
        if (interface_id == nullptr || object == nullptr) { return sdk::kInvalidArgument; }
        *object = nullptr;
        if (sdk::FUnknownPrivate::iidEqual(interface_id, sdk::IBStream::iid) ||
            sdk::FUnknownPrivate::iidEqual(interface_id, sdk::FUnknown::iid)) {
            *object = static_cast<sdk::IBStream*>(this);
            return sdk::kResultOk;
        }
        return sdk::kNoInterface;
    }
    sdk::tresult PLUGIN_API read(void* output, sdk::int32 requested,
                                 sdk::int32* received) override {
        ++read_calls;
        if (received != nullptr) { *received = 0; }
        if (output == nullptr || requested < 0 || position > extent) {
            return sdk::kInvalidArgument;
        }
        if (position >= fail_at && fault != Fault::none && fault != Fault::error_after_byte) {
            return fault == Fault::error ? sdk::kResultFalse : sdk::kResultOk;
        }
        const auto count = std::min({static_cast<std::size_t>(requested),
                                     extent - position, transfer_limit});
        std::memcpy(output, bytes.data() + position, count);
        position += count;
        if (received != nullptr) { *received = static_cast<sdk::int32>(count); }
        return fault == Fault::error_after_byte && position > fail_at ?
            sdk::kResultFalse : sdk::kResultOk;
    }
    sdk::tresult PLUGIN_API write(void* input, sdk::int32 requested,
                                  sdk::int32* written) override {
        if (written != nullptr) { *written = 0; }
        if (input == nullptr || requested < 0 || position > bytes.size()) {
            return sdk::kInvalidArgument;
        }
        const auto count = std::min({static_cast<std::size_t>(requested),
                                     bytes.size() - position, transfer_limit});
        std::memcpy(bytes.data() + position, input, count);
        position += count;
        extent = std::max(extent, position);
        if (written != nullptr) { *written = static_cast<sdk::int32>(count); }
        return sdk::kResultOk;
    }
    sdk::tresult PLUGIN_API seek(sdk::int64 offset, sdk::int32 origin,
                                 sdk::int64* result) override {
        sdk::int64 base = 0;
        if (origin == kIBSeekCur) { base = static_cast<sdk::int64>(position); }
        else if (origin == kIBSeekEnd) { base = static_cast<sdk::int64>(extent); }
        else if (origin != kIBSeekSet) { return sdk::kInvalidArgument; }
        if (offset < -base || offset > static_cast<sdk::int64>(bytes.size()) - base) {
            return sdk::kResultFalse;
        }
        position = static_cast<std::size_t>(base + offset);
        if (result != nullptr) { *result = static_cast<sdk::int64>(position); }
        return sdk::kResultOk;
    }
    sdk::tresult PLUGIN_API tell(sdk::int64* output) override {
        if (output == nullptr) { return sdk::kInvalidArgument; }
        *output = static_cast<sdk::int64>(position);
        return sdk::kResultOk;
    }
};

Packet save(vst::IEditController& controller) {
    Stream output;
    output.bytes.fill(0xa5U);
    const Controls controls;
    check(controller.getState(&output) == sdk::kResultOk, "state save failed");
    controls.expect_unchanged();
    check((output.extent == 16 || output.extent == 20) && output.position == output.extent,
          "adaptive state save has wrong extent");
    check(std::all_of(output.bytes.begin() + static_cast<std::ptrdiff_t>(output.extent),
                      output.bytes.end(), [](auto byte) { return byte == 0xa5U; }),
          "state save wrote beyond its packet");
    Packet result;
    result.size = output.extent;
    std::copy_n(output.bytes.begin(), result.size, result.bytes.begin());
    return result;
}

struct Snapshot {
    Packet configuration;
    std::array<std::uint64_t, parameter_count> parameters{};
    explicit Snapshot(vst::IEditController& controller) : configuration(save(controller)) {
        for (std::size_t index = 0; index < parameter_count; ++index) {
            parameters[index] = bits(controller.getParamNormalized(parameter_ids[index]));
        }
    }
    void expect_parameters(vst::IEditController& controller, bool include_expression = true) const {
        check(controller.getParameterCount() == 19, "parameter count changed");
        for (std::size_t index = 0; index < parameter_count; ++index) {
            if (!include_expression && index == 2) { continue; }
            check(bits(controller.getParamNormalized(parameter_ids[index])) == parameters[index],
                  "persistent or command parameter changed unexpectedly");
        }
    }
    void expect_unchanged(vst::IEditController& controller) const {
        expect_parameters(controller);
        check(save(controller) == configuration, "rejected or read-only call changed saved state");
    }
};

struct Instance {
    rhodes::vst3::RhodesController implementation;
    vst::IEditController& controller = implementation;
    Instance() {
        check(controller.initialize(nullptr) == sdk::kResultOk, "initialize failed");
    }
    ~Instance() { (void)controller.terminate(); }
};

void seed(vst::IEditController& controller) {
    check(set(controller, gain_id, 0.125) == sdk::kResultOk &&
          set(controller, sustain_id, 1.0) == sdk::kResultOk &&
          set(controller, expression_id, 0.25) == sdk::kResultOk, "persistent seed failed");
    constexpr std::array<std::uint64_t, 16> commands{
        0x3fb999999999999aULL, 0x0000000000000001ULL,
        0x3fb8000000000000ULL, 0x3fc0000000000000ULL,
        0x3fc4000000000000ULL, 0x3fc8000000000000ULL,
        0x3fcc000000000000ULL, 0x3fd0000000000000ULL,
        0x3fd2000000000000ULL, 0x3fd4000000000000ULL,
        0x3fd6000000000000ULL, 0x3fd8000000000000ULL,
        0x3fda000000000000ULL, 0x3fdc000000000000ULL,
        0x3fde000000000000ULL, 0x3fe0000000000000ULL};
    for (std::size_t channel = 0; channel < commands.size(); ++channel) {
        const auto id = command_base + static_cast<vst::ParamID>(channel);
        check(set(controller, id, value(commands[channel])) == sdk::kResultOk &&
              bits(controller.getParamNormalized(id)) == commands[channel],
              "binary64 command seed changed precision");
    }
    check(save(controller) == packet(2, 0x3e000000U, 0x3f800000U, 0x3e800000U),
          "seed packet differs");
}

void expect_defaults(vst::IEditController& controller) {
    check(controller.getParameterCount() == 19, "default parameter count differs");
    for (std::size_t index = 0; index < parameter_count; ++index) {
        vst::ParameterInfo info{};
        check(controller.getParameterInfo(static_cast<sdk::int32>(index), info) == sdk::kResultOk,
              "parameter enumeration failed");
        check(info.id == parameter_ids[index], "persistent/command order or ID changed");
        const auto expected = index == 0 ? 0x3fe0000000000000ULL : index == 2 ? unity : 0ULL;
        check(bits(info.defaultNormalizedValue) == expected &&
              bits(controller.getParamNormalized(info.id)) == expected,
              "metadata or live default differs");
        if (index == 2) {
            check(ascii(info.title) == "Expression" && ascii(info.units) == "x",
                  "Expression title/unit differs");
            check(info.stepCount == 0 && info.flags == vst::ParameterInfo::kCanAutomate &&
                  info.unitId == vst::kRootUnitId, "Expression flags, steps or unit ID differ");
        } else if (index >= 3) {
            check(info.stepCount == 0 && info.flags == vst::ParameterInfo::kNoFlags,
                  "command metadata changed");
        }
    }
    check(save(controller) == packet(1, 0x3f000000U, 0), "default v1 bytes changed");
}

void metadata_cases() {
    current_case = "metadata and MIDI mapping";
    Instance instance;
    auto& controller = instance.controller;
    expect_defaults(controller);
    const Snapshot before(controller);
    for (const double input : {0.0, 0.25, 0.5, 1.0}) {
        check(bits(controller.normalizedParamToPlain(expression_id, input)) == bits(input) &&
              bits(controller.plainParamToNormalized(expression_id, input)) == bits(input),
              "Expression plain/normalized range is not the identity");
    }
    check(controller.normalizedParamToPlain(gain_id, 0.5) == 16.0 &&
          controller.plainParamToNormalized(gain_id, 16.0) == 0.5,
          "Monitoring Gain range changed");
    void* object = nullptr;
    check(controller.queryInterface(vst::IMidiMapping::iid, &object) == sdk::kResultOk &&
          object != nullptr, "public IMidiMapping unavailable");
    auto* mapping = static_cast<vst::IMidiMapping*>(object);
    struct ReleaseMapping {
        vst::IMidiMapping* pointer;
        ~ReleaseMapping() { pointer->release(); }
    } release{mapping};
    constexpr vst::ParamID sentinel = 0xdeadbeefU;
    for (sdk::int16 channel = 0; channel < 16; ++channel) {
        for (sdk::int16 cc = 0; cc < 128; ++cc) {
            vst::ParamID assigned = sentinel;
            const auto result = mapping->getMidiControllerAssignment(0, channel, cc, assigned);
            if (cc == 11 || cc == 64 || cc == 123) {
                const auto expected = cc == 11 ? expression_id : cc == 64 ? sustain_id :
                    command_base + static_cast<vst::ParamID>(channel);
                check(result == sdk::kResultTrue && assigned == expected,
                      "CC11/CC64/CC123 mapping differs");
            } else {
                check(result == sdk::kResultFalse && assigned == sentinel,
                      "unsupported CC (including CC43) changed the tag");
            }
        }
    }
    for (const auto bus : {-1, 1, std::numeric_limits<sdk::int32>::min(),
                          std::numeric_limits<sdk::int32>::max()}) {
        for (const auto channel : {sdk::int16{0}, sdk::int16{15}}) {
            for (const auto cc : {sdk::int16{11}, sdk::int16{43}, sdk::int16{64}, sdk::int16{123}}) {
                vst::ParamID assigned = sentinel;
                check(mapping->getMidiControllerAssignment(bus, channel, cc, assigned) ==
                      sdk::kResultFalse && assigned == sentinel, "invalid bus changed mapping tag");
            }
        }
    }
    for (const auto channel : {std::numeric_limits<sdk::int16>::min(), sdk::int16{-1},
                              sdk::int16{16}, std::numeric_limits<sdk::int16>::max()}) {
        for (const auto cc : {sdk::int16{11}, sdk::int16{43}, sdk::int16{64}, sdk::int16{123}}) {
            vst::ParamID assigned = sentinel;
            check(mapping->getMidiControllerAssignment(0, channel, cc, assigned) ==
                  sdk::kResultFalse && assigned == sentinel, "invalid channel changed mapping tag");
        }
    }
    before.expect_unchanged(controller);
}

struct NumericCase {
    std::uint64_t input, normalized;
    std::uint32_t saved;
};
// Literal IEEE oracles: for six binary32 rounding boundaries, the binary64
// predecessor, exact midpoint and successor distinguish nearest-even from all
// directed rounding modes. Expected bits were derived from binary spacings;
// a separate Python struct probe confirmed the constants before execution.
constexpr std::array numeric_cases{
    NumericCase{0, 0, 0},
    NumericCase{0x8000000000000000ULL, 0, 0},
    NumericCase{0x0000000000000001ULL, 0, 0},
    NumericCase{0x000fffffffffffffULL, 0, 0},
    NumericCase{0x0010000000000000ULL, 0, 0},
    NumericCase{0x3fb999999999999aULL, 0x3fb99999a0000000ULL, 0x3dcccccdU},
    NumericCase{0x36a0000000000000ULL, 0x36a0000000000000ULL, 0x00000001U},
    NumericCase{0x36b0000000000000ULL, 0x36b0000000000000ULL, 0x00000002U},
    NumericCase{0x380fffffc0000000ULL, 0x380fffffc0000000ULL, 0x007fffffU},
    NumericCase{0x3810000000000000ULL, 0x3810000000000000ULL, 0x00800000U},
    NumericCase{0x3fefffffe0000000ULL, 0x3fefffffe0000000ULL, 0x3f7fffffU},
    NumericCase{0x3fefffffffffffffULL, unity, 0x3f800000U},
    NumericCase{unity, unity, 0x3f800000U},
    NumericCase{0x368fffffffffffffULL, 0, 0},
    NumericCase{0x3690000000000000ULL, 0, 0},
    NumericCase{0x3690000000000001ULL, 0x36a0000000000000ULL, 0x00000001U},
    NumericCase{0x36a7ffffffffffffULL, 0x36a0000000000000ULL, 0x00000001U},
    NumericCase{0x36a8000000000000ULL, 0x36b0000000000000ULL, 0x00000002U},
    NumericCase{0x36a8000000000001ULL, 0x36b0000000000000ULL, 0x00000002U},
    NumericCase{0x380fffffdfffffffULL, 0x380fffffc0000000ULL, 0x007fffffU},
    NumericCase{0x380fffffe0000000ULL, 0x3810000000000000ULL, 0x00800000U},
    NumericCase{0x380fffffe0000001ULL, 0x3810000000000000ULL, 0x00800000U},
    NumericCase{0x3fe000000fffffffULL, 0x3fe0000000000000ULL, 0x3f000000U},
    NumericCase{0x3fe0000010000000ULL, 0x3fe0000000000000ULL, 0x3f000000U},
    NumericCase{0x3fe0000010000001ULL, 0x3fe0000020000000ULL, 0x3f000001U},
    NumericCase{0x3fe000002fffffffULL, 0x3fe0000020000000ULL, 0x3f000001U},
    NumericCase{0x3fe0000030000000ULL, 0x3fe0000040000000ULL, 0x3f000002U},
    NumericCase{0x3fe0000030000001ULL, 0x3fe0000040000000ULL, 0x3f000002U},
    NumericCase{0x3fefffffefffffffULL, 0x3fefffffe0000000ULL, 0x3f7fffffU},
    NumericCase{0x3feffffff0000000ULL, unity, 0x3f800000U},
    NumericCase{0x3feffffff0000001ULL, unity, 0x3f800000U}};

void accept_parse(vst::IEditController& controller, vst::ParamID id, Text input,
                  std::uint64_t expected) {
    const auto original = input;
    const Snapshot before(controller);
    double output = value(output_sentinel);
    const Controls controls;
    check(controller.getParamValueByString(id, input.data(), output) == sdk::kResultOk,
          "valid text was rejected");
    controls.expect_unchanged();
    check(bits(output) == expected, "parsed canonical bits differ");
    check(input == original, "parse changed caller text");
    before.expect_unchanged(controller);
}

void reject_parse(vst::IEditController& controller, vst::ParamID id, vst::TChar* input) {
    const Snapshot before(controller);
    double output = value(output_sentinel);
    const Controls controls;
    check(controller.getParamValueByString(id, input, output) != sdk::kResultOk,
          "invalid text was accepted");
    controls.expect_unchanged();
    check(bits(output) == output_sentinel, "rejected parse changed caller numeric output");
    before.expect_unchanged(controller);
}

struct GuardedText {
    vst::TChar before = static_cast<vst::TChar>(0x5aa5);
    Text output{};
    vst::TChar after = static_cast<vst::TChar>(0x6bb6);
    GuardedText() { output.fill(static_cast<vst::TChar>(0x1234)); }
    void expect_guards() const {
        check(before == static_cast<vst::TChar>(0x5aa5) &&
              after == static_cast<vst::TChar>(0x6bb6), "format touched String128 guards");
    }
};

void format(vst::IEditController& controller, vst::ParamID id, std::uint64_t input,
            std::uint64_t expected, std::string_view literal = {}) {
    const Snapshot before(controller);
    GuardedText output;
    const Controls controls;
    check(controller.getParamStringByValue(id, value(input), output.output.data()) == sdk::kResultOk,
          "valid numeric display was rejected");
    controls.expect_unchanged();
    output.expect_guards();
    const auto display = ascii(output.output.data());
    check(!display.empty() && display.find('x') == std::string::npos,
          "display contains an embedded factor suffix");
    if (!literal.empty()) { check(display == literal, "independent literal display differs"); }
    // The fixed expected bits, not the formatter's output, are the round-trip oracle.
    accept_parse(controller, id, output.output, expected);
    before.expect_unchanged(controller);
}

void reject_numeric(vst::IEditController& controller, vst::ParamID id, std::uint64_t input) {
    const Snapshot before(controller);
    GuardedText output;
    const auto original = output.output;
    const Controls controls;
    check(set(controller, id, value(input)) != sdk::kResultOk, "invalid numeric set was accepted");
    check(controller.getParamStringByValue(id, value(input), output.output.data()) != sdk::kResultOk,
          "invalid numeric display was accepted");
    controls.expect_unchanged();
    check(output.output == original, "rejected format changed caller String128");
    output.expect_guards();
    before.expect_unchanged(controller);
}

void numeric_cases_for(vst::IEditController& controller) {
    for (const auto& item : numeric_cases) {
        current_case = "numeric Expression bits=" + std::to_string(item.input);
        check(set(controller, expression_id, 0.0) == sdk::kResultOk, "numeric reset failed");
        const Snapshot before(controller);
        const Controls controls;
        check(set(controller, expression_id, value(item.input)) == sdk::kResultOk,
              "valid numeric set failed");
        controls.expect_unchanged();
        check(bits(controller.getParamNormalized(expression_id)) == item.normalized,
              "nearest-even Expression bits differ");
        before.expect_parameters(controller, false);
        check(save(controller) == packet(item.saved == 0x3f800000U ? 1U : 2U,
                                        0x3e000000U, 0x3f800000U, item.saved),
              "numeric Expression saved bits differ");
        format(controller, expression_id, item.input, item.normalized);
    }
    constexpr std::array invalid{
        0x8000000000000001ULL, 0x800fffffffffffffULL, 0x8010000000000000ULL,
        0xb690000000000000ULL, 0xbff0000000000000ULL, 0x3ff0000000000001ULL,
        0x4000000000000000ULL, 0x7fefffffffffffffULL, 0x7ff0000000000000ULL,
        0xfff0000000000000ULL, 0x7ff8000000001234ULL, 0xfff8000000001234ULL,
        0x7ff0000000000001ULL, 0xfff0000000000001ULL};
    for (const auto input : invalid) {
        current_case = "invalid Expression bits=" + std::to_string(input);
        reject_numeric(controller, expression_id, input);
    }
    current_case = "independent literal displays";
    format(controller, expression_id, 0x8000000000000000ULL, 0, "0");
    format(controller, expression_id, unity, unity, "1");
    format(controller, expression_id, 0x3fb999999999999aULL, 0x3fb99999a0000000ULL,
           "0.10000000149011612");
    format(controller, expression_id, 0x36a0000000000000ULL, 0x36a0000000000000ULL,
           "1.4012984643248171e-45");
    format(controller, expression_id, 0x380fffffc0000000ULL, 0x380fffffc0000000ULL,
           "1.1754942106924411e-38");
    format(controller, expression_id, 0x3810000000000000ULL, 0x3810000000000000ULL,
           "1.1754943508222875e-38");
    format(controller, expression_id, 0x3fefffffe0000000ULL, 0x3fefffffe0000000ULL,
           "0.99999994039535522");
    format(controller, command_base, 0x3fb999999999999aULL, 0x3fb999999999999aULL,
           "0.10000000000000001");
    format(controller, command_base + 1U, 1, 1, "4.9406564584124654e-324");
}

void text_cases_for(vst::IEditController& controller) {
    struct ParseCase { std::string_view input; std::uint64_t expected; };
    constexpr std::array valid{
        ParseCase{"0", 0}, ParseCase{"-0", 0}, ParseCase{"+0", 0},
        ParseCase{"-0.000e+99 x", 0}, ParseCase{"1", unity},
        ParseCase{"+1.", unity}, ParseCase{"1.e+0x", unity}, ParseCase{"1E-0 x", unity},
        ParseCase{".5", 0x3fe0000000000000ULL},
        ParseCase{"0.5x", 0x3fe0000000000000ULL},
        ParseCase{"0.5 x", 0x3fe0000000000000ULL},
        ParseCase{"\t\r\n\f\v +5.E-1 \t x \r\n\v\f", 0x3fe0000000000000ULL},
        ParseCase{".1", 0x3fb99999a0000000ULL}, ParseCase{"10e-2", 0x3fb99999a0000000ULL},
        ParseCase{"1.000000000000000001", unity},
        ParseCase{"0.5000000298023223876953125", 0x3fe0000000000000ULL},
        ParseCase{"0.5000000894069671630859375", 0x3fe0000040000000ULL},
        ParseCase{"0.9999999701976776123046875", unity},
        ParseCase{"7.0064923216240854e-46", 0},
        ParseCase{"1.4012984643248171e-45x", 0x36a0000000000000ULL},
        ParseCase{"2.1019476964872256e-45", 0x36b0000000000000ULL},
        ParseCase{"1.1754942807573643e-38", 0x3810000000000000ULL},
        ParseCase{"1e-40", 0x37a16c2000000000ULL},
        ParseCase{"5e-324", 0}, ParseCase{"0e-400", 0}, ParseCase{"-0e-400 x", 0}};
    for (const auto& item : valid) {
        current_case = "parse Expression " + std::string{item.input};
        accept_parse(controller, expression_id, text(item.input), item.expected);
    }
    constexpr std::string_view invalid[]{
        "", " ", "x", "x0.5", ".", "+", "-", "+.", "e1", "1e", "1e+", "1e-",
        "0.5X", "0.5xx", "0.5 x x", "0.5 x junk", "0.5x0", "0.5xy", "0.5d",
        "0.5%", "1,0", "1_0", "0x1p-1", "0x0.8", "0 .5", "0. 5", "1 e-1",
        "1e -1", "1e+ 0", "++0.5", "--0.5", "+-0.5", "1/2", "1.0.0", "nan",
        "NaN", "inf", "-inf", "1e309", "1e-400", "-1e-400", "1e-324", "-1e-324",
        "-5e-324", "-1e-50", "-0.000001",
        "1.0000000000000002", "1.00000001 x", "32 x"};
    for (const auto input : invalid) {
        current_case = "reject Expression " + std::string{input};
        auto source = text(input);
        const auto original = source;
        reject_parse(controller, expression_id, source.data());
        check(source == original, "rejected parse changed source");
    }
    current_case = "bounded/null/Unicode Expression text";
    Text full{};
    full.fill(static_cast<vst::TChar>('0'));
    full[127] = 0;
    accept_parse(controller, expression_id, full, 0);
    full[127] = static_cast<vst::TChar>('0');
    reject_parse(controller, expression_id, full.data());
    for (const auto unit : {0x00a0U, 0x00d7U, 0x2212U, 0xff10U, 0xd800U}) {
        auto source = text("0.5 ");
        source[3] = static_cast<vst::TChar>(unit);
        reject_parse(controller, expression_id, source.data());
    }
    reject_parse(controller, expression_id, nullptr);
    const Snapshot before(controller);
    check(controller.getParamStringByValue(expression_id, 0.5, nullptr) != sdk::kResultOk,
          "null output accepted");
    before.expect_unchanged(controller);
    for (const auto id : {vst::ParamID{3}, vst::ParamID{0x0fff}, vst::ParamID{0x1010},
                          vst::ParamID{0xffffffffU}}) {
        auto source = text("0.5");
        reject_parse(controller, id, source.data());
        reject_numeric(controller, id, 0x3fe0000000000000ULL);
    }
    // Expression's binary32 path must not quantize the sixteen binary64 commands
    // or grant commands the factor suffix accepted by gain and Expression.
    current_case = "command precision and gain independence";
    for (std::size_t channel = 0; channel < 16; ++channel) {
        const auto id = command_base + static_cast<vst::ParamID>(channel);
        accept_parse(controller, id, text("0.1"), 0x3fb999999999999aULL);
        accept_parse(controller, id, text("5e-324"), 1);
        auto source = text("0.5 x");
        reject_parse(controller, id, source.data());
    }
    accept_parse(controller, gain_id, text("16 x"), 0x3fe0000000000000ULL);
}

void conversion_cases() {
    Instance instance;
    seed(instance.controller);
    numeric_cases_for(instance.controller);
    text_cases_for(instance.controller);
}

sdk::tresult recall(vst::IEditController& controller, bool component, sdk::IBStream* stream) {
    return component ? controller.setComponentState(stream) : controller.setState(stream);
}

void expect_commands(vst::IEditController& controller, const Snapshot& previous) {
    for (std::size_t index = 3; index < parameter_count; ++index) {
        check(bits(controller.getParamNormalized(parameter_ids[index])) == previous.parameters[index],
              "recall changed a transient command value");
    }
}

void valid_recall(vst::IEditController& controller, bool component, const Packet& input,
                  const Packet& expected, std::uint64_t gain, std::uint64_t sustain,
                  std::uint64_t expression) {
    seed(controller);
    const Snapshot before(controller);
    Stream stream(input);
    stream.transfer_limit = 1;
    const Controls controls;
    check(recall(controller, component, &stream) == sdk::kResultOk, "valid recall failed");
    controls.expect_unchanged();
    check(stream.position == input.size && stream.read_calls == input.size,
          "one-byte recall did not consume exactly the packet");
    check(bits(controller.getParamNormalized(gain_id)) == gain &&
          bits(controller.getParamNormalized(sustain_id)) == sustain &&
          bits(controller.getParamNormalized(expression_id)) == expression,
          "recall produced wrong persistent control bits");
    expect_commands(controller, before);
    check(save(controller) == expected, "recall canonical save differs");
}

void invalid_recall(vst::IEditController& controller, bool component, Stream& stream) {
    const Snapshot before(controller);
    const Controls controls;
    check(recall(controller, component, &stream) != sdk::kResultOk,
          "malformed or incomplete recall was accepted");
    controls.expect_unchanged();
    before.expect_unchanged(controller);
}

void recall_cases() {
    Instance instance;
    auto& controller = instance.controller;
    for (const bool component : {false, true}) {
        current_case = component ? "setComponentState migration" : "setState migration";
        valid_recall(controller, component, packet(1, 0x3e800000U, 0x3f800000U),
                     packet(1, 0x3e800000U, 0x3f800000U), 0x3fd0000000000000ULL, unity, unity);
        valid_recall(controller, component, packet(2, 0x3f400000U, 0, 0x3e800000U),
                     packet(2, 0x3f400000U, 0, 0x3e800000U), 0x3fe8000000000000ULL, 0,
                     0x3fd0000000000000ULL);
        valid_recall(controller, component, packet(2, 0x3e000000U, 0, 0x3f800000U),
                     packet(1, 0x3e000000U, 0), 0x3fc0000000000000ULL, 0, unity);
        valid_recall(controller, component, packet(1, 0x80000000U, 0x80000000U),
                     packet(1, 0, 0), 0, 0, unity);
        valid_recall(controller, component, packet(2, 0x80000000U, 0x80000000U, 0x80000000U),
                     packet(2, 0, 0, 0), 0, 0, 0);
        constexpr std::array regions{
            NumericCase{0, 0, 0},
            NumericCase{0, 0x36a0000000000000ULL, 0x00000001U},
            NumericCase{0, 0x36b0000000000000ULL, 0x00000002U},
            NumericCase{0, 0x380fffffc0000000ULL, 0x007fffffU},
            NumericCase{0, 0x3810000000000000ULL, 0x00800000U},
            NumericCase{0, 0x3fefffffe0000000ULL, 0x3f7fffffU}};
        for (const auto& item : regions) {
            const auto input = packet(2, 0x3f000000U, 0x3f800000U, item.saved);
            valid_recall(controller, component, input, input, 0x3fe0000000000000ULL,
                         unity, item.normalized);
        }
        // Literal RHST v2 golden pins all byte offsets independently of word().
        const Packet golden{{0x52, 0x48, 0x53, 0x54, 0x02, 0x00, model, 0x00,
                             0x00, 0x00, 0x00, 0x3f, 0x00, 0x00, 0x80, 0x3f,
                             0x01, 0x00, 0x00, 0x00}, 20};
        valid_recall(controller, component, golden, golden, 0x3fe0000000000000ULL,
                     unity, 0x36a0000000000000ULL);
        seed(controller);
        current_case = component ? "setComponentState rejection" : "setState rejection";
        for (const unsigned version : {1U, 2U}) {
            const auto good = packet(version, 0x3f400000U, 0, 0x3f000000U);
            for (const auto extent : {0U, 15U, 16U, 17U, 19U, 20U, 21U}) {
                if (extent == good.size) { continue; }
                auto malformed = good;
                malformed.size = extent;
                Stream stream(malformed);
                invalid_recall(controller, component, stream);
            }
            for (const auto bad_model : {0U, 1U, 2U, 3U, 4U}) {
                if (bad_model == model) { continue; }
                auto malformed = good;
                malformed.bytes[6] = static_cast<std::uint8_t>(bad_model);
                Stream stream(malformed);
                invalid_recall(controller, component, stream);
            }
            for (const auto at : {0U, 1U, 2U, 3U, 5U, 7U}) {
                auto malformed = good;
                malformed.bytes[at] ^= 1U;
                Stream stream(malformed);
                invalid_recall(controller, component, stream);
            }
            for (const auto bad_version : {0U, 3U, 255U}) {
                auto malformed = good;
                malformed.bytes[4] = static_cast<std::uint8_t>(bad_version);
                Stream stream(malformed);
                invalid_recall(controller, component, stream);
            }
            for (const auto bad_gain : {0x80000001U, 0x3f800001U, 0x7f800000U, 0x7fc01234U}) {
                auto malformed = good;
                word(malformed, 8, bad_gain);
                Stream stream(malformed);
                invalid_recall(controller, component, stream);
            }
            auto bad_pedal = good;
            word(bad_pedal, 12, 0x3f000000U);
            Stream pedal_stream(bad_pedal);
            invalid_recall(controller, component, pedal_stream);
            for (const auto fault : {Stream::Fault::error, Stream::Fault::zero_progress,
                                     Stream::Fault::error_after_byte}) {
                Stream stream(good);
                stream.transfer_limit = 1;
                stream.fail_at = good.size - 1;
                stream.fault = fault;
                invalid_recall(controller, component, stream);
            }
        }
        for (const auto bad_expression : {0x80000001U, 0x807fffffU, 0xbf800000U,
                                         0x3f800001U, 0x7f800000U, 0xff800000U,
                                         0x7fc01234U, 0xffc01234U, 0x7f800001U}) {
            Stream stream(packet(2, 0x3f400000U, 0, bad_expression));
            invalid_recall(controller, component, stream);
        }
        for (const auto at : {16U, 17U, 18U, 19U}) {
            Stream stream(packet(2, 0x3f400000U, 0, 0x3f000000U));
            stream.transfer_limit = 1;
            stream.fail_at = at;
            stream.fault = Stream::Fault::error;
            invalid_recall(controller, component, stream);
        }
        const Snapshot before(controller);
        check(recall(controller, component, nullptr) != sdk::kResultOk, "null recall accepted");
        before.expect_unchanged(controller);
    }
}

void inactive_cases(vst::IEditController& controller) {
    check(controller.getParameterCount() == 0, "inactive controller retained parameters");
    auto input = text("0.5 x");
    double output = value(output_sentinel);
    GuardedText display;
    const auto original = display.output;
    const Controls controls;
    check(set(controller, expression_id, 0.5) != sdk::kResultOk,
          "inactive controller accepted Expression set");
    check(controller.getParamValueByString(expression_id, input.data(), output) != sdk::kResultOk &&
          bits(output) == output_sentinel, "inactive parse changed numeric output");
    check(controller.getParamStringByValue(expression_id, 0.5, display.output.data()) !=
          sdk::kResultOk && display.output == original, "inactive format changed text output");
    controls.expect_unchanged();
    display.expect_guards();
    for (const bool component : {false, true}) {
        for (const unsigned version : {1U, 2U}) {
            Stream state(packet(version, 0x3f000000U, 0, 0x3e800000U));
            check(recall(controller, component, &state) != sdk::kResultOk,
                  "inactive controller accepted recall");
        }
    }
    Stream state;
    state.bytes.fill(0xa5U);
    const auto original_bytes = state.bytes;
    check(controller.getState(&state) != sdk::kResultOk && state.bytes == original_bytes &&
          state.position == 0 && state.extent == 0, "inactive save touched caller state");
    check(controller.getParameterCount() == 0, "inactive calls created parameters");
}

void lifecycle_cases() {
    // A host-owned context exercises both null-context initialization and the
    // SDK hostContext branch without relying on a real DAW or internal objects.
    Stream context;
    rhodes::vst3::RhodesController implementation;
    vst::IEditController& controller = implementation;
    current_case = "before initialize";
    inactive_cases(controller);
    for (const bool supplied_context : {false, true, false}) {
        current_case = supplied_context ? "host-context lifecycle" : "null-context lifecycle";
        auto* host_context = supplied_context ? static_cast<sdk::FUnknown*>(&context) : nullptr;
        check(controller.initialize(host_context) == sdk::kResultOk, "reinitialize failed");
        expect_defaults(controller);
        seed(controller);
        const Snapshot before(controller);
        check(controller.initialize(nullptr) != sdk::kResultOk &&
              controller.initialize(&context) != sdk::kResultOk, "duplicate initialize accepted");
        before.expect_unchanged(controller);
        accept_parse(controller, expression_id, text("0.5 x"), 0x3fe0000000000000ULL);
        check(controller.terminate() == sdk::kResultOk, "terminate failed");
        inactive_cases(controller);
    }
}

struct RestoreEnvironment {
    std::fenv_t original{};
#if RHODES_EXPRESSION_MXCSR
    unsigned mxcsr = _mm_getcsr();
#endif
    RestoreEnvironment() {
        check(std::fegetenv(&original) == 0, "cannot save caller floating environment");
    }
    ~RestoreEnvironment() {
        (void)std::fesetenv(&original);
#if RHODES_EXPRESSION_MXCSR
        _mm_setcsr(mxcsr);
#endif
    }
};

#if RHODES_EXPRESSION_MXCSR
unsigned supported_mxcsr() {
    alignas(16) std::array<unsigned char, 512> saved{};
#if defined(_MSC_VER)
    _fxsave(saved.data());
#else
    asm volatile("fxsave %0" : "=m"(saved));
#endif
    std::uint32_t mask = 0;
    std::memcpy(&mask, saved.data() + 28, sizeof(mask));
    // Intel's architectural zero-mask fallback excludes DAZ but permits FTZ.
    return mask == 0U ? 0xffbfU : mask;
}
#endif

bool floating_cases(bool flush_modes) {
    current_case = flush_modes ? "floating flush modes" : "floating rounding modes";
#if !RHODES_EXPRESSION_MXCSR
    if (flush_modes) {
        std::cout << "SKIP fp_flush: x86 MXCSR FTZ/DAZ unavailable; ARM flush modes are not exercised\n";
        return false;
    }
#endif
    const Controls original;
    const auto original_flags = std::fetestexcept(FE_ALL_EXCEPT);
#if RHODES_EXPRESSION_MXCSR
    const auto original_mxcsr = _mm_getcsr();
#endif
    bool ran = false;
    constexpr std::array roundings{FE_TONEAREST, FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO};
    for (const auto rounding : roundings) {
        for (const auto flush : {0U, ftz, daz, ftz | daz}) {
            if (flush_modes != (flush != 0U)) { continue; }
#if RHODES_EXPRESSION_MXCSR
            if ((flush & ~supported_mxcsr()) != 0U) {
                std::cout << "SKIP rounding=" << rounding << " flush=" << flush
                          << ": requested MXCSR bits unavailable\n";
                continue;
            }
#endif
            {
                RestoreEnvironment restore;
                if (std::fesetround(rounding) != 0) {
                    std::cout << "SKIP rounding=" << rounding << ": fenv mode unavailable\n";
                    continue;
                }
#if RHODES_EXPRESSION_MXCSR
                _mm_setcsr((_mm_getcsr() & ~(ftz | daz)) | flush);
                check((_mm_getcsr() & (ftz | daz)) == flush, "flush mode was not installed");
#endif
                check(std::fegetround() == rounding, "rounding mode was not installed");
                ++environments;
                ran = true;
                const Controls requested;
                try {
                    conversion_cases();
                    recall_cases();
                    requested.expect_unchanged();
                } catch (const std::exception& error) {
                    throw std::runtime_error("rounding=" + std::to_string(rounding) +
                        " flush=" + std::to_string(flush) + " " + error.what());
                }
            }
            original.expect_unchanged();
            check(std::fetestexcept(FE_ALL_EXCEPT) == original_flags,
                  "test did not restore caller sticky flags");
#if RHODES_EXPRESSION_MXCSR
            check(_mm_getcsr() == original_mxcsr, "test did not restore complete caller MXCSR");
#endif
        }
    }
    return ran;
}
} // namespace

int main(int argc, char** argv) {
    const std::string_view selected = argc > 1 ? argv[1] : "all";
    try {
        constexpr std::array<std::string_view, 6> selectors{
            "metadata", "conversion", "recall", "lifecycle", "fp_rounding", "fp_flush"};
        bool found = false, ran_any = false;
        for (const auto name : selectors) {
            if (selected != "all" && selected != name) { continue; }
            found = true;
            checks = environments = 0;
            current_case = std::string{name};
            bool ran = true;
            if (name == "metadata") { metadata_cases(); }
            else if (name == "conversion") { conversion_cases(); }
            else if (name == "recall") { recall_cases(); }
            else if (name == "lifecycle") { lifecycle_cases(); }
            else { ran = floating_cases(name == "fp_flush"); }
            ran_any = ran_any || ran;
            if (ran) {
                std::cout << "PASS Expression controller model=" << static_cast<unsigned>(model)
                          << " " << name << " (" << checks << " always-active checks, "
                          << environments << " FP environments)\n";
            }
        }
        check(found, "unknown selector");
        return ran_any ? 0 : 77;
    } catch (const std::exception& error) {
        std::cerr << "FAIL Expression controller model=" << static_cast<unsigned>(model)
                  << " " << selected << ": " << error.what() << '\n';
        return 1;
    }
}
