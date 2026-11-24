#include "vst3_test_support.hpp"

#include <bit>
#include <iostream>
#include <limits>

namespace {
namespace sdk = Steinberg;
namespace vst = Steinberg::Vst;
namespace plugin = rhodes::vst3;
namespace host = plugin::test_host;
using Text = std::array<vst::TChar, 128>;

// Public-contract literals keep a production ID/codec change from silently
// changing the oracle. No production encoder or text helper builds expectations.
#if defined(RHODES_QUADRATURE_VST3)
constexpr std::uint8_t model = 3;
#elif defined(RHODES_FILTERED_VST3)
constexpr std::uint8_t model = 2;
#else
constexpr std::uint8_t model = 1;
#endif
constexpr std::array<vst::ParamID, 24> parameter_ids{
    0, 1, 2, 0x1000, 0x1001, 0x1002, 0x1003, 0x1004, 0x1005,
    0x1006, 0x1007, 0x1008, 0x1009, 0x100a, 0x100b, 0x100c,
    0x100d, 0x100e, 0x100f, 3, 4, 5, 6, 7};
constexpr std::array<vst::ParamID, 5> panel_ids{3, 4, 5, 6, 7};
constexpr std::array<vst::ParamID, 4> continuous_ids{3, 4, 5, 6};
constexpr std::uint64_t output_sentinel = 0x7ff80000000012abULL;
std::size_t checks = 0;

void check(bool condition, std::string_view message) {
    ++checks;
    host::require(condition, message);
}

std::uint64_t bits(double value) { return std::bit_cast<std::uint64_t>(value); }

struct Packet {
    std::array<std::uint8_t, 40> bytes{};
    std::size_t size = 0;
    friend bool operator==(const Packet&, const Packet&) = default;
};

constexpr Packet default_packet{{
    'R', 'H', 'S', 'T', 1, 0, model, 0,
    0, 0, 0, 0x3f, 0, 0, 0, 0}, 16};
constexpr Packet legacy_v1{{
    'R', 'H', 'S', 'T', 1, 0, model, 0,
    0, 0, 0x80, 0x3e, 0, 0, 0x80, 0x3f}, 16};
constexpr Packet legacy_v2{{
    'R', 'H', 'S', 'T', 2, 0, model, 0,
    0, 0, 0x80, 0x3e, 0, 0, 0x80, 0x3f, 0, 0, 0x40, 0x3f}, 20};
// Gain=.25, sustain=on, expression=.75, bass=.125, treble=.875,
// intensity=.5, speed=.25, vibrato=on, in little-endian binary32 words.
constexpr Packet panel_packet{{
    'R', 'H', 'S', 'T', 3, 0, model, 0,
    0, 0, 0x80, 0x3e, 0, 0, 0x80, 0x3f, 0, 0, 0x40, 0x3f,
    0, 0, 0, 0x3e, 0, 0, 0x60, 0x3f, 0, 0, 0, 0x3f,
    0, 0, 0x80, 0x3e, 0, 0, 0x80, 0x3f}, 40};
// Every field differs from panel_packet, so late invalid fields reveal partial
// recall of otherwise valid earlier fields rather than merely reapplying them.
constexpr Packet other_panel_packet{{
    'R', 'H', 'S', 'T', 3, 0, model, 0,
    0, 0, 0x40, 0x3f, 0, 0, 0, 0, 0, 0, 0x80, 0x3e,
    0, 0, 0x60, 0x3f, 0, 0, 0, 0x3e, 0, 0, 0x40, 0x3f,
    0, 0, 0x20, 0x3f, 0, 0, 0, 0}, 40};

void load(host::MemoryStream& stream, const Packet& packet, std::size_t prefix = 0) {
    check(packet.size <= packet.bytes.size() && prefix + packet.size <= stream.bytes.size(),
          "invalid stream fixture bounds");
    stream.bytes.fill(0xa5U);
    std::copy_n(packet.bytes.begin(), packet.size,
                stream.bytes.begin() + static_cast<std::ptrdiff_t>(prefix));
    stream.position = static_cast<sdk::int64>(prefix);
    stream.extent = static_cast<sdk::int64>(prefix + packet.size);
}

Packet save(vst::IEditController& controller) {
    host::MemoryStream output;
    check(controller.getState(&output) == sdk::kResultOk, "controller save failed");
    check(output.extent == 16 || output.extent == 20 || output.extent == 40,
          "saved packet has unsupported extent");
    check(output.position == output.extent && output.write_calls <= 40,
          "saved packet transfer is incomplete or unbounded");
    Packet result{};
    result.size = static_cast<std::size_t>(output.extent);
    std::copy_n(output.bytes.begin(), result.size, result.bytes.begin());
    return result;
}

void replace_word(Packet& packet, std::size_t word, std::uint32_t value) {
    check(word < 8, "invalid wire word fixture");
    for (std::size_t byte = 0; byte < 4; ++byte) {
        packet.bytes[8 + 4 * word + byte] = static_cast<std::uint8_t>(value >> (8 * byte));
    }
}

void expect_word(const Packet& packet, vst::ParamID id, std::uint32_t expected) {
    check(packet.size == 40, "nonneutral panel was not saved as v3");
    for (std::size_t byte = 0; byte < 4; ++byte) {
        check(packet.bytes[8 + 4 * id + byte] ==
              static_cast<std::uint8_t>(expected >> (8 * byte)),
              "normalized control differs from literal saved binary32 word");
    }
}

struct Instance {
    plugin::RhodesController implementation;
    vst::IEditController& controller = implementation;
    Instance() { check(controller.initialize(nullptr) == sdk::kResultOk, "initialize failed"); }
    ~Instance() { (void)controller.terminate(); }
};

struct Snapshot {
    std::array<std::uint64_t, parameter_ids.size()> values{};
    Packet configuration;
    explicit Snapshot(vst::IEditController& controller) : configuration(save(controller)) {
        for (std::size_t index = 0; index < parameter_ids.size(); ++index) {
            values[index] = bits(controller.getParamNormalized(parameter_ids[index]));
        }
    }
    void expect_other_parameters(vst::IEditController& controller, vst::ParamID changed) const {
        for (std::size_t index = 0; index < parameter_ids.size(); ++index) {
            if (parameter_ids[index] == changed) { continue; }
            check(bits(controller.getParamNormalized(parameter_ids[index])) == values[index],
                  "operation changed an unrelated persistent control or command");
        }
    }
    void expect_unchanged(vst::IEditController& controller) const {
        expect_other_parameters(controller, std::numeric_limits<vst::ParamID>::max());
        check(save(controller) == configuration, "operation changed saved configuration");
    }
    void expect_commands(vst::IEditController& controller) const {
        for (std::size_t index = 3; index < 19; ++index) {
            check(bits(controller.getParamNormalized(parameter_ids[index])) == values[index],
                  "configuration recall changed a transient command");
        }
    }
};

sdk::tresult recall(vst::IEditController& controller, bool component, sdk::IBStream* stream) {
    return component ? controller.setComponentState(stream) : controller.setState(stream);
}

void recall_packet(vst::IEditController& controller, const Packet& packet, bool component = false) {
    host::MemoryStream input;
    load(input, packet, 3);
    input.transfer_limit = 1;
    check(recall(controller, component, &input) == sdk::kResultOk, "valid recall failed");
    check(input.position == static_cast<sdk::int64>(packet.size + 3) &&
          input.read_calls == packet.size, "one-byte recall did not consume exactly its packet");
    check(save(controller) == packet, "recalled state differs from literal byte oracle");
}

Text text(std::string_view value) {
    check(value.size() < 128, "text fixture exceeds public bound");
    Text result{};
    for (std::size_t index = 0; index < value.size(); ++index) {
        result[index] = static_cast<vst::TChar>(value[index]);
    }
    return result;
}

std::string ascii(const vst::TChar* value) {
    std::string result;
    for (std::size_t index = 0; index < 128; ++index) {
        if (value[index] == 0) { return result; }
        check(static_cast<std::uint16_t>(value[index]) <= 0x7fU, "non-ASCII display output");
        result += static_cast<char>(value[index]);
    }
    throw std::runtime_error("unterminated public display string");
}

void metadata_cases() {
    Instance instance;
    auto& controller = instance.controller;
    check(controller.getParameterCount() == 24 && plugin::kControllerParameterCount == 24,
          "public parameter count differs");
    check(plugin::kStateModel == model && plugin::kBassId == 3 && plugin::kTrebleId == 4 &&
          plugin::kIntensityId == 5 && plugin::kSpeedId == 6 && plugin::kVibratoId == 7,
          "model or persistent panel IDs changed");
    for (std::size_t index = 0; index < parameter_ids.size(); ++index) {
        vst::ParameterInfo info{};
        check(controller.getParameterInfo(static_cast<sdk::int32>(index), info) == sdk::kResultOk &&
              info.id == parameter_ids[index], "legacy enumeration order or appended panel ID differs");
    }
    struct Metadata {
        const char* title;
        const char* units;
        double initial;
        double low;
        double middle;
        double high;
    };
    constexpr std::array metadata{
        Metadata{"Bass", "dB", 0.5, -12.0, 0.0, 12.0},
        Metadata{"Treble", "dB", 0.5, -12.0, 0.0, 12.0},
        Metadata{"Intensity", "x", 0.0, 0.0, 0.5, 1.0},
        Metadata{"Speed", "Hz", 0.5, 0.5, 6.25, 12.0},
        Metadata{"Vibrato", "", 0.0, 0.0, 1.0, 1.0}};
    const Snapshot before(controller);
    for (std::size_t index = 0; index < metadata.size(); ++index) {
        const auto& expected = metadata[index];
        const auto id = panel_ids[index];
        vst::ParameterInfo info{};
        check(controller.getParameterInfo(static_cast<sdk::int32>(19 + index), info) == sdk::kResultOk,
              "panel metadata unavailable");
        check(ascii(info.title) == expected.title && ascii(info.units) == expected.units,
              "panel title or physical unit differs");
        const auto flags = vst::ParameterInfo::kCanAutomate |
            (id == 7 ? vst::ParameterInfo::kIsList : 0);
        check(info.flags == flags && info.stepCount == (id == 7 ? 1 : 0) &&
              info.unitId == vst::kRootUnitId, "panel automation, steps or unit ID differs");
        check(bits(info.defaultNormalizedValue) == bits(expected.initial) &&
              bits(controller.getParamNormalized(id)) == bits(expected.initial),
              "panel metadata/live defaults differ");
        check(controller.normalizedParamToPlain(id, 0.0) == expected.low &&
              controller.normalizedParamToPlain(id, 1.0) == expected.high &&
              controller.plainParamToNormalized(id, expected.low) == 0.0 &&
              controller.plainParamToNormalized(id, expected.high) == 1.0,
              "panel plain/normalized endpoints differ");
        if (id != 7) {
            check(controller.normalizedParamToPlain(id, 0.5) == expected.middle &&
                  controller.plainParamToNormalized(id, expected.middle) == 0.5,
                  "continuous panel midpoint differs");
        }
    }
    check(save(controller) == default_packet, "neutral defaults changed literal legacy v1 export");
    before.expect_unchanged(controller);
}

constexpr std::array<std::uint64_t, 8> invalid_values{
    0xbff0000000000000ULL, 0x8000000000000001ULL, 0x3ff0000000000001ULL,
    0x7ff0000000000000ULL, 0xfff0000000000000ULL, 0x7ff0000000000001ULL,
    0x7ff80000000012abULL, 0xfff80000000012abULL};

void value_cases() {
    Instance instance;
    auto& controller = instance.controller;
    recall_packet(controller, panel_packet);
    struct NumericCase { std::uint64_t input; std::uint64_t stored; std::uint32_t wire; };
    constexpr std::array cases{
        NumericCase{0, 0, 0}, NumericCase{0x8000000000000000ULL, 0, 0},
        NumericCase{0x0000000000000001ULL, 0, 0},
        NumericCase{0x36a0000000000000ULL, 0x36a0000000000000ULL, 0x00000001U},
        NumericCase{0x3810000000000000ULL, 0x3810000000000000ULL, 0x00800000U},
        NumericCase{0x3fb999999999999aULL, 0x3fb99999a0000000ULL, 0x3dcccccdU},
        NumericCase{0x3fe0000010000000ULL, 0x3fe0000000000000ULL, 0x3f000000U},
        NumericCase{0x3fe0000010000001ULL, 0x3fe0000020000000ULL, 0x3f000001U},
        NumericCase{0x3fefffffffffffffULL, 0x3ff0000000000000ULL, 0x3f800000U},
        NumericCase{0x3ff0000000000000ULL, 0x3ff0000000000000ULL, 0x3f800000U}};
    for (const auto id : continuous_ids) {
        for (const auto& item : cases) {
            check(controller.setParamNormalized(id, 0.625) == sdk::kResultOk, "value seed failed");
            const Snapshot before(controller);
            check(controller.setParamNormalized(id, std::bit_cast<double>(item.input)) == sdk::kResultOk &&
                  bits(controller.getParamNormalized(id)) == item.stored,
                  "continuous normalized input did not reach canonical binary32 value");
            expect_word(save(controller), id, item.wire);
            before.expect_other_parameters(controller, id);
        }
    }
    for (const auto id : panel_ids) {
        for (const auto invalid : invalid_values) {
            const Snapshot before(controller);
            check(controller.setParamNormalized(id, std::bit_cast<double>(invalid)) ==
                  sdk::kInvalidArgument, "invalid normalized panel value was accepted");
            before.expect_unchanged(controller);
        }
    }
    for (const auto& item : std::array{
             std::pair{0x8000000000000000ULL, 0.0}, std::pair{0x0000000000000001ULL, 0.0},
             std::pair{0x3fdfffffffffffffULL, 0.0}, std::pair{0x3fe0000000000000ULL, 1.0},
             std::pair{0x3fe0000000000001ULL, 1.0}, std::pair{0x3ff0000000000000ULL, 1.0}}) {
        check(controller.setParamNormalized(7, std::bit_cast<double>(item.first)) == sdk::kResultOk &&
              bits(controller.getParamNormalized(7)) == bits(item.second),
              "Vibrato threshold or zero canonicalization differs");
        expect_word(save(controller), 7, item.second == 0.0 ? 0U : 0x3f800000U);
    }
    for (const auto id : {vst::ParamID{8}, vst::ParamID{0xfff}, vst::ParamID{0x1010},
                          std::numeric_limits<vst::ParamID>::max()}) {
        const Snapshot before(controller);
        check(controller.setParamNormalized(id, 0.5) == sdk::kInvalidArgument,
              "undeclared parameter ID was accepted");
        before.expect_unchanged(controller);
    }
}

void reject_text(vst::IEditController& controller, vst::ParamID id, Text input) {
    const auto original = input;
    double output = std::bit_cast<double>(output_sentinel);
    check(controller.getParamValueByString(id, input.data(), output) != sdk::kResultOk &&
          bits(output) == output_sentinel && input == original,
          "rejected text changed its input or caller output");
}

void text_cases() {
    Instance instance;
    auto& controller = instance.controller;
    recall_packet(controller, panel_packet);
    const Snapshot before(controller);
    struct TextCase { vst::ParamID id; const char* display; double normalized; };
    constexpr std::array cases{
        TextCase{3, "-12 dB", 0.0}, TextCase{3, "-6 dB", 0.25}, TextCase{3, "0 dB", 0.5},
        TextCase{3, "12 dB", 1.0}, TextCase{4, "-12 dB", 0.0}, TextCase{4, "0 dB", 0.5},
        TextCase{4, "6 dB", 0.75}, TextCase{4, "12 dB", 1.0},
        TextCase{5, "0 x", 0.0}, TextCase{5, "0.5 x", 0.5}, TextCase{5, "1 x", 1.0},
        TextCase{6, "0.5 Hz", 0.0}, TextCase{6, "3.375 Hz", 0.25},
        TextCase{6, "6.25 Hz", 0.5}, TextCase{6, "12 Hz", 1.0},
        TextCase{7, "Off", 0.0}, TextCase{7, "On", 1.0}};
    for (const auto& item : cases) {
        Text formatted{};
        check(controller.getParamStringByValue(item.id, item.normalized, formatted.data()) ==
              sdk::kResultOk && ascii(formatted.data()) == item.display,
              "literal physical-unit display differs");
        auto input = text(item.display);
        const auto original = input;
        double output = std::bit_cast<double>(output_sentinel);
        check(controller.getParamValueByString(item.id, input.data(), output) == sdk::kResultOk &&
              bits(output) == bits(item.normalized) && input == original,
              "literal physical-unit input differs");
    }
    for (const auto& item : std::array{
             TextCase{3, "\t -6dB\r\n", 0.25}, TextCase{4, "+6", 0.75},
             TextCase{5, " .5x ", 0.5}, TextCase{6, " 3.375 ", 0.25}}) {
        auto input = text(item.display);
        double output = std::bit_cast<double>(output_sentinel);
        check(controller.getParamValueByString(item.id, input.data(), output) == sdk::kResultOk &&
              bits(output) == bits(item.normalized), "optional exact suffix/ASCII whitespace differs");
    }
    // Ordinary canonical binary32 values must round-trip through physical text.
    // Affine EQ/Hz text cannot distinguish every normalized subnormal near an
    // endpoint; value_cases separately verifies those stored bits without such a claim.
    constexpr std::array<std::uint64_t, 8> roundtrip_values{
        0, 0x3fb99999a0000000ULL, 0x3fc0000000000000ULL, 0x3fd0000000000000ULL,
        0x3fe0000000000000ULL, 0x3fe0000020000000ULL, 0x3fec000000000000ULL,
        0x3ff0000000000000ULL};
    for (const auto id : continuous_ids) {
        for (const auto expected : roundtrip_values) {
            Text formatted{};
            check(controller.getParamStringByValue(id, std::bit_cast<double>(expected),
                                                  formatted.data()) == sdk::kResultOk,
                  "canonical panel formatting failed");
            double output = std::bit_cast<double>(output_sentinel);
            check(controller.getParamValueByString(id, formatted.data(), output) == sdk::kResultOk &&
                  bits(output) == expected, "canonical panel text did not round-trip exactly");
        }
    }
    for (const auto id : panel_ids) {
        for (const auto* invalid : {"", "nan", "inf", "0x1p0", "0,5", "1junk", "1e99999",
                                    "1e-99999", "1e", "1 0", "1 Hz Hz", "1 X", "1 DB"}) {
            reject_text(controller, id, text(invalid));
        }
        Text unterminated{};
        unterminated.fill('1');
        reject_text(controller, id, unterminated);
        auto unicode = text("1 ");
        unicode[1] = 0x2212;
        reject_text(controller, id, unicode);
        double output = std::bit_cast<double>(output_sentinel);
        check(controller.getParamValueByString(id, nullptr, output) == sdk::kInvalidArgument &&
              bits(output) == output_sentinel, "null text changed caller output");
        check(controller.getParamStringByValue(id, 0.5, nullptr) == sdk::kInvalidArgument,
              "null display buffer was accepted");
        for (const auto invalid : invalid_values) {
            Text formatted{};
            formatted.fill('Q');
            const auto old = formatted;
            check(controller.getParamStringByValue(id, std::bit_cast<double>(invalid), formatted.data()) ==
                  sdk::kInvalidArgument && formatted == old, "invalid format modified caller buffer");
        }
    }
    for (const auto& item : std::array{
             TextCase{3, "-12.0001dB", 0}, TextCase{3, "12.0001 dB", 0},
             TextCase{4, "-12.0001dB", 0}, TextCase{4, "12.0001 dB", 0},
             TextCase{3, "0 db", 0}, TextCase{4, "0 DB", 0},
             TextCase{5, "1.001x", 0}, TextCase{5, "-5e-324", 0}, TextCase{5, "50%", 0},
             TextCase{6, "0.499 Hz", 0}, TextCase{6, "12.01 Hz", 0}, TextCase{6, "1 hz", 0},
             TextCase{7, "on", 0}, TextCase{7, "On ", 0}, TextCase{7, " Off", 0},
             TextCase{7, "1", 0}, TextCase{7, "0", 0}}) {
        reject_text(controller, item.id, text(item.display));
    }
    Text boundary{};
    boundary.fill(' ');
    boundary[126] = '1';
    boundary[127] = 0;
    double output = -1.0;
    check(controller.getParamValueByString(5, boundary.data(), output) == sdk::kResultOk && output == 1.0,
          "valid terminator at public text bound was rejected");
    before.expect_unchanged(controller);
}

void stream_cases() {
    const rhodes::PluginState intended{{0.25F, true, 0.75F},
                                       {0.125F, 0.875F, 0.5F, 0.25F, true}};
    for (const sdk::int32 chunk : {1, 2, 3, 7, 16, 40}) {
        host::MemoryStream input;
        load(input, panel_packet, 3);
        input.transfer_limit = chunk;
        const auto original = input.bytes;
        const auto decoded = plugin::read_plugin_state_stream(&input, model);
        const auto calls = (40U + static_cast<unsigned>(chunk) - 1U) / static_cast<unsigned>(chunk);
        check(decoded.has_value() && *decoded == intended, "literal v3 wire fields decoded incorrectly");
        check(input.position == 43 && input.read_calls == calls && input.bytes == original,
              "v3 reader changed input or violated its positive short-transfer bound");
        host::MemoryStream output;
        output.transfer_limit = chunk;
        output.position = 3;
        output.extent = 64;
        output.bytes.fill(0xa5U);
        check(plugin::write_plugin_state_stream(&output, intended, model), "v3 short write failed");
        check(output.position == 43 && output.extent == 64 && output.write_calls == calls,
              "v3 writer changed host-owned extent or violated transfer bound");
        check(output.bytes == original, "v3 writer differs from literal bytes or changed caller guards");
        input.position = 3;
        input.read_calls = 0;
        check(!plugin::read_state_stream(&input, model) && input.read_calls == 0,
              "legacy reader silently consumed extended panel state");
    }
    for (sdk::int64 size = 0; size <= 64; ++size) {
        if (size == 16 || size == 20 || size == 40) { continue; }
        host::MemoryStream input;
        load(input, panel_packet);
        input.extent = size;
        check(!plugin::read_plugin_state_stream(&input, model) && input.read_calls == 0,
              "unsupported extent reached payload read");
    }
    for (const auto fault : {host::TransferFault::result_error, host::TransferFault::zero_progress,
                            host::TransferFault::negative_count, host::TransferFault::overreported_count}) {
        for (const std::size_t after : {1U, 20U, 40U}) {
            host::MemoryStream input;
            load(input, panel_packet);
            input.transfer_limit = 1;
            input.read_fault = fault;
            input.read_fault_at = after;
            check(!plugin::read_plugin_state_stream(&input, model) && input.read_calls == after,
                  "invalid short read was accepted or retried");
            host::MemoryStream output;
            output.transfer_limit = 1;
            output.write_fault = fault;
            output.write_fault_at = after;
            check(!plugin::write_plugin_state_stream(&output, intended, model) &&
                  output.write_calls == after, "invalid short write was accepted or retried");
        }
    }
    for (const auto flag : {&host::MemoryStream::fail_tell, &host::MemoryStream::fail_seek_end,
                            &host::MemoryStream::fail_seek_set,
                            &host::MemoryStream::wrong_restored_position}) {
        host::MemoryStream input;
        load(input, panel_packet);
        input.*flag = true;
        check(!plugin::read_plugin_state_stream(&input, model) && input.read_calls == 0,
              "failed stream positioning reached payload read");
    }
    check(!plugin::read_plugin_state_stream(nullptr, model) &&
          !plugin::write_plugin_state_stream(nullptr, intended, model), "null stream was accepted");
    for (const auto invalid_model : {std::uint16_t{0}, std::uint16_t{4}, std::uint16_t{65535}}) {
        host::MemoryStream stream;
        load(stream, panel_packet);
        check(!plugin::read_plugin_state_stream(&stream, invalid_model) && stream.read_calls == 0,
              "invalid model read touched payload");
        check(!plugin::write_plugin_state_stream(&stream, intended, invalid_model) && stream.write_calls == 0,
              "invalid model write touched payload");
    }
    for (std::uint16_t other_model = 1; other_model <= 3; ++other_model) {
        if (other_model == model) { continue; }
        host::MemoryStream input;
        load(input, panel_packet);
        check(!plugin::read_plugin_state_stream(&input, other_model), "cross-model v3 recall was accepted");
    }
    for (const auto field : {&rhodes::FrontPanelState::bass, &rhodes::FrontPanelState::treble,
                             &rhodes::FrontPanelState::intensity, &rhodes::FrontPanelState::speed}) {
        auto invalid = intended;
        invalid.panel.*field = std::numeric_limits<float>::infinity();
        host::MemoryStream output;
        output.bytes.fill(0xa5U);
        const auto original = output.bytes;
        check(!plugin::write_plugin_state_stream(&output, invalid, model) &&
              output.write_calls == 0 && output.bytes == original,
              "invalid panel field caused a partial write");
    }
    host::MemoryStream untruncated;
    load(untruncated, panel_packet);
    check(plugin::write_plugin_state_stream(&untruncated, {}, model) && untruncated.extent == 40,
          "adaptive legacy writer resized host-owned destination");
    untruncated.position = 0;
    check(!plugin::read_plugin_state_stream(&untruncated, model),
          "legacy header with stale v3 trailing bytes was accepted");
}

void recall_cases() {
    Instance instance;
    auto& controller = instance.controller;
    for (std::size_t channel = 0; channel < 16; ++channel) {
        check(controller.setParamNormalized(0x1000U + static_cast<vst::ParamID>(channel),
              static_cast<double>(channel + 1) / 32.0) == sdk::kResultOk, "command seed failed");
    }
    for (const bool component : {false, true}) {
        recall_packet(controller, panel_packet, component);
        const Snapshot original(controller);
        constexpr std::array<double, 8> expected{0.25, 1.0, 0.75, 0.125, 0.875, 0.5, 0.25, 1.0};
        for (std::size_t id = 0; id < expected.size(); ++id) {
            check(bits(controller.getParamNormalized(static_cast<vst::ParamID>(id))) == bits(expected[id]),
                  "valid recall did not apply all eight independent controls");
        }
        const auto rejected = [&](const Packet& malformed) {
            host::MemoryStream input;
            load(input, malformed);
            input.transfer_limit = 1;
            check(recall(controller, component, &input) != sdk::kResultOk && input.read_calls <= 40,
                  "malformed recall was accepted or exceeded its transfer bound");
            original.expect_unchanged(controller);
        };
        for (std::size_t location = 0; location < 8; ++location) {
            auto malformed = other_panel_packet;
            malformed.bytes[location] = 0xffU;
            rejected(malformed);
        }
        for (std::size_t word = 0; word < 8; ++word) {
            for (const auto invalid : {0x80000001U, 0x3f800001U, 0x7f800000U,
                                       0xff800000U, 0x7f800001U, 0x7fc012abU}) {
                auto malformed = other_panel_packet;
                replace_word(malformed, word, invalid);
                rejected(malformed);
            }
        }
        for (const std::size_t word : {1U, 7U}) {
            auto malformed = other_panel_packet;
            replace_word(malformed, word, 0x3f000000U);
            rejected(malformed);
        }
        for (std::size_t size = 0; size < 40; ++size) {
            auto malformed = other_panel_packet;
            malformed.size = size;
            rejected(malformed);
        }
        for (const auto fault : {host::TransferFault::result_error, host::TransferFault::zero_progress,
                                host::TransferFault::negative_count, host::TransferFault::overreported_count}) {
            host::MemoryStream input;
            load(input, other_panel_packet);
            input.transfer_limit = 1;
            input.read_fault = fault;
            input.read_fault_at = 40;
            check(recall(controller, component, &input) != sdk::kResultOk && input.read_calls == 40,
                  "late stream failure was accepted or retried");
            original.expect_unchanged(controller);
        }
        check(recall(controller, component, nullptr) != sdk::kResultOk, "null recall succeeded");
        original.expect_unchanged(controller);
        for (const auto& legacy : {legacy_v2, legacy_v1}) {
            recall_packet(controller, panel_packet, component);
            recall_packet(controller, legacy, component);
            constexpr std::array<double, 5> neutral{0.5, 0.5, 0.0, 0.5, 0.0};
            for (std::size_t index = 0; index < panel_ids.size(); ++index) {
                check(bits(controller.getParamNormalized(panel_ids[index])) == bits(neutral[index]),
                      "legacy recall failed to reset a previously nonneutral panel field");
            }
            check(controller.getParamNormalized(2) == (legacy.size == 16 ? 1.0 : 0.75),
                  "legacy expression default or recalled value differs");
            original.expect_commands(controller);
        }
    }
}

void instance_cases() {
    Instance first;
    Instance second;
    recall_packet(first.controller, panel_packet);
    recall_packet(second.controller, other_panel_packet, true);
    const Snapshot second_before(second.controller);
    for (const auto id : panel_ids) {
        check(first.controller.setParamNormalized(id, id == 7 ? 0.0 : 0.375) == sdk::kResultOk,
              "first instance edit failed");
        second_before.expect_unchanged(second.controller);
    }
    const Snapshot first_before(first.controller);
    check(first.controller.initialize(nullptr) != sdk::kResultOk, "duplicate initialization succeeded");
    first_before.expect_unchanged(first.controller);
    second_before.expect_unchanged(second.controller);
    auto malformed = panel_packet;
    replace_word(malformed, 7, 0x7fc012abU);
    host::MemoryStream input;
    load(input, malformed);
    check(second.controller.setState(&input) != sdk::kResultOk, "malformed second-instance recall succeeded");
    first_before.expect_unchanged(first.controller);
    second_before.expect_unchanged(second.controller);
    recall_packet(first.controller, legacy_v1);
    second_before.expect_unchanged(second.controller);
    check(first.controller.terminate() == sdk::kResultOk, "first instance terminate failed");
    for (const auto id : panel_ids) {
        check(first.controller.setParamNormalized(id, 0.75) == sdk::kInvalidArgument,
              "terminated instance accepted a panel edit");
    }
    host::MemoryStream output;
    check(first.controller.getState(&output) != sdk::kResultOk && output.write_calls == 0,
          "terminated instance saved stale panel state");
    second_before.expect_unchanged(second.controller);
    check(first.controller.initialize(nullptr) == sdk::kResultOk, "first instance reinitialize failed");
    check(first.controller.getParameterCount() == 24 && save(first.controller) == default_packet,
          "reinitialized instance retained old panel configuration or duplicate parameters");
    second_before.expect_unchanged(second.controller);
    recall_packet(first.controller, panel_packet, true);
    second_before.expect_unchanged(second.controller);
}
} // namespace

int main(int argc, char** argv) {
    const std::string_view selected = argc > 1 ? argv[1] : "all";
    try {
        check(argc <= 2, "expected at most one selector");
        struct Test { std::string_view name; void (*run)(); };
        constexpr std::array tests{
            Test{"metadata", metadata_cases}, Test{"values", value_cases}, Test{"text", text_cases},
            Test{"stream", stream_cases}, Test{"recall", recall_cases}, Test{"instances", instance_cases}};
        bool found = false;
        for (const auto& test : tests) {
            if (selected != "all" && selected != test.name) { continue; }
            found = true;
            checks = 0;
            test.run();
            std::cout << "PASS panel controller model=" << static_cast<unsigned>(model) << ' '
                      << test.name << " (" << checks << " always-active checks)\n";
        }
        check(found, "unknown selector");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL panel controller model=" << static_cast<unsigned>(model) << ' '
                  << selected << ": " << error.what() << '\n';
        return 1;
    }
}
