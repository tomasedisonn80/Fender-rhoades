#include "controller.hpp"
#include "state_stream.hpp"

#include "rhodes/control_value.hpp"
#include "rhodes/control_text.hpp"

#include "pluginterfaces/vst/ivstmidicontrollers.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <optional>
#include <string_view>

namespace rhodes::vst3 {
namespace {

constexpr std::size_t kTextCapacity = 128;

[[nodiscard]] bool known_parameter(Steinberg::Vst::ParamID tag) noexcept {
    return tag == kGainId || tag == kSustainId || tag == kExpressionId ||
           all_notes_off_channel(tag) >= 0;
}

[[nodiscard]] bool ascii_whitespace(char value) noexcept {
    return value == ' ' || value == '\t' || value == '\n' || value == '\r' ||
           value == '\f' || value == '\v';
}

[[nodiscard]] std::string_view trim_ascii(std::string_view text) noexcept {
    while (!text.empty() && ascii_whitespace(text.front())) { text.remove_prefix(1); }
    while (!text.empty() && ascii_whitespace(text.back())) { text.remove_suffix(1); }
    return text;
}

// The public SDK input is a pointer, not a length-bearing string. Require its
// terminator inside the same 128-code-unit bound used for host display strings.
[[nodiscard]] std::optional<std::string_view> read_ascii(
    const Steinberg::Vst::TChar* source, std::array<char, kTextCapacity>& destination) noexcept {
    if (source == nullptr) { return std::nullopt; }
    for (std::size_t index = 0; index < destination.size(); ++index) {
        const auto unit = static_cast<std::uint16_t>(source[index]);
        if (unit == 0U) { return std::string_view{destination.data(), index}; }
        if (unit > 0x7fU) { return std::nullopt; }
        destination[index] = static_cast<char>(unit);
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<double> parse_decimal(
    std::string_view text, bool allow_factor_suffix, std::uint64_t maximum_bits) noexcept {
    text = trim_ascii(text);
    if (allow_factor_suffix && !text.empty() && text.back() == 'x') {
        text.remove_suffix(1);
        text = trim_ascii(text);
    }
    const auto parsed = control_text::parse_decimal(text);
    if (!parsed) { return std::nullopt; }
    // The portable integer converter rounds to binary64 before this parameter
    // domain check. Classify bits so DAZ cannot hide a negative subnormal.
    const auto bits = std::bit_cast<std::uint64_t>(*parsed);
    if (bits > maximum_bits && bits != 0x8000000000000000ULL) { return std::nullopt; }
    return control::is_zero(*parsed) ? 0.0 : *parsed;
}

// All callers supply bounded, already validated ASCII. Stage public output in
// a local String128 so every failing conversion preserves the caller's buffer.
void write_ascii(std::string_view source, Steinberg::Vst::String128 destination) noexcept {
    for (std::size_t index = 0; index < source.size(); ++index) {
        destination[index] = static_cast<Steinberg::Vst::TChar>(source[index]);
    }
    destination[source.size()] = 0;
}

void channel_title(std::string_view prefix, unsigned channel,
                   Steinberg::Vst::String128 destination) noexcept {
    write_ascii(prefix, destination);
    auto index = prefix.size();
    if (channel >= 10U) {
        destination[index++] = static_cast<Steinberg::Vst::TChar>('0' + channel / 10U);
    }
    destination[index++] = static_cast<Steinberg::Vst::TChar>('0' + channel % 10U);
    destination[index] = 0;
}

class CommandParameter final : public Steinberg::Vst::Parameter {
public:
    CommandParameter(const Steinberg::Vst::TChar* title, Steinberg::Vst::ParamID tag,
                     const Steinberg::Vst::TChar* short_title)
        : Parameter(title, tag, STR16(""), 0.0, 0,
                    Steinberg::Vst::ParameterInfo::kNoFlags,
                    Steinberg::Vst::kRootUnitId, short_title) {}

    bool setNormalized(Steinberg::Vst::ParamValue value) override {
        if (!control::valid_normalized(value)) { return false; }
        const double canonical = control::is_zero(value) ? 0.0 : value;
        // The SDK's floating equality can discard a positive subnormal command
        // value under DAZ. Retain the full binary64 value for numeric round trips.
        if (std::bit_cast<std::uint64_t>(canonical) ==
            std::bit_cast<std::uint64_t>(valueNormalized)) { return false; }
        valueNormalized = canonical;
        changed();
        return true;
    }
};

} // namespace

Steinberg::FUnknown* RhodesController::createInstance(void* /*context*/) {
    return static_cast<Steinberg::Vst::IEditController*>(new RhodesController);
}

Steinberg::tresult PLUGIN_API RhodesController::initialize(Steinberg::FUnknown* context) {
    // The SDK's host-context guard does not detect a prior null-context init.
    // Reject before adding parameters or replacing their current values.
    if (initialized_) { return Steinberg::kResultFalse; }
    const auto result = EditController::initialize(context);
    if (result != Steinberg::kResultOk) { return result; }
    parameters.addParameter(new Steinberg::Vst::RangeParameter(
        STR16("Monitoring Gain"), kGainId, STR16("x"), 0.0, 32.0, 16.0));
    auto* sustain = new Steinberg::Vst::StringListParameter(STR16("Sustain"), kSustainId);
    sustain->appendString(STR16("Off"));
    sustain->appendString(STR16("On"));
    parameters.addParameter(sustain);
    parameters.addParameter(new Steinberg::Vst::RangeParameter(
        STR16("Expression"), kExpressionId, STR16("x"), 0.0, 1.0, 1.0, 0,
        Steinberg::Vst::ParameterInfo::kCanAutomate));
    for (std::size_t channel = 0; channel < kMidiChannelCount; ++channel) {
        Steinberg::Vst::String128 title{}, short_title{};
        channel_title("All Notes Off Ch ", static_cast<unsigned>(channel) + 1U, title);
        channel_title("ANO Ch", static_cast<unsigned>(channel) + 1U, short_title);
        parameters.addParameter(new CommandParameter(
            title, kAllNotesOffBaseId + static_cast<Steinberg::Vst::ParamID>(channel), short_title));
    }
    initialized_ = true;
    return Steinberg::kResultOk;
}

Steinberg::tresult PLUGIN_API RhodesController::terminate() {
    const auto result = EditController::terminate();
    if (result == Steinberg::kResultOk) { initialized_ = false; }
    return result;
}

Steinberg::tresult PLUGIN_API RhodesController::setComponentState(Steinberg::IBStream* stream) {
    const auto decoded = read_state_stream(stream, kStateModel);
    if (!decoded || getParameterObject(kGainId) == nullptr ||
        getParameterObject(kSustainId) == nullptr ||
        getParameterObject(kExpressionId) == nullptr) {
        return Steinberg::kResultFalse;
    }
    const auto gain = control::widen(decoded->monitoring_gain);
    const auto expression = control::widen(decoded->expression);
    if (!gain || !expression) { return Steinberg::kResultFalse; }
    // All three parameters and the entire chunk have been checked before any
    // parameter changes, so malformed recall leaves the controller unchanged.
    (void)setParamNormalized(kGainId, *gain);
    (void)setParamNormalized(kSustainId, decoded->sustain ? 1.0 : 0.0);
    (void)setParamNormalized(kExpressionId, *expression);
    return Steinberg::kResultOk;
}

Steinberg::tresult PLUGIN_API RhodesController::setState(Steinberg::IBStream* state) {
    return setComponentState(state);
}

Steinberg::tresult PLUGIN_API RhodesController::getState(Steinberg::IBStream* stream) {
    if (getParameterObject(kGainId) == nullptr || getParameterObject(kSustainId) == nullptr ||
        getParameterObject(kExpressionId) == nullptr) {
        return Steinberg::kResultFalse;
    }
    const auto gain = control::quantize(getParamNormalized(kGainId));
    const auto expression = control::quantize(getParamNormalized(kExpressionId));
    if (!gain || !expression) { return Steinberg::kResultFalse; }
    const InstrumentState state{*gain, getParamNormalized(kSustainId) >= 0.5, *expression};
    return write_state_stream(stream, state, kStateModel) ? Steinberg::kResultOk : Steinberg::kResultFalse;
}

Steinberg::tresult PLUGIN_API RhodesController::setParamNormalized(
    Steinberg::Vst::ParamID tag, Steinberg::Vst::ParamValue value) {
    if (!initialized_ || !known_parameter(tag) || !control::valid_normalized(value)) {
        return Steinberg::kInvalidArgument;
    }
    const double canonical = (tag == kGainId || tag == kExpressionId) ?
        *control::widen(*control::quantize(value)) :
        tag == kSustainId ? (value >= 0.5 ? 1.0 : 0.0) :
        control::is_zero(value) ? 0.0 : value;
    return EditController::setParamNormalized(tag, canonical);
}

Steinberg::tresult PLUGIN_API RhodesController::getParamStringByValue(
    Steinberg::Vst::ParamID tag, Steinberg::Vst::ParamValue value,
    Steinberg::Vst::String128 string) {
    if (!initialized_ || !known_parameter(tag) || getParameterObject(tag) == nullptr ||
        string == nullptr || !control::valid_normalized(value)) {
        return Steinberg::kInvalidArgument;
    }
    Steinberg::Vst::String128 result{};
    if (tag == kSustainId) {
        write_ascii(value >= 0.5 ? "On" : "Off", result);
    } else {
        // Gain and expression text represent the canonical stored binary32
        // configuration; command text preserves the complete binary64 value.
        // Every nonzero widened binary32 is a normal double, so this power-of-two
        // gain scaling is exact and insensitive to host rounding/flush controls.
        const double canonical = (tag == kGainId || tag == kExpressionId) ?
            *control::widen(*control::quantize(value)) :
            control::is_zero(value) ? 0.0 : value;
        const double plain = tag == kGainId ? canonical * 32.0 : canonical;
        std::array<char, kTextCapacity> ascii{};
        const auto count = control_text::format_decimal(plain, ascii);
        if (!count) { return Steinberg::kResultFalse; }
        write_ascii({ascii.data(), *count}, result);
    }
    std::copy_n(result, kTextCapacity, string);
    return Steinberg::kResultOk;
}

Steinberg::tresult PLUGIN_API RhodesController::getParamValueByString(
    Steinberg::Vst::ParamID tag, Steinberg::Vst::TChar* string,
    Steinberg::Vst::ParamValue& value) {
    if (!initialized_ || !known_parameter(tag) || getParameterObject(tag) == nullptr ||
        string == nullptr) {
        return Steinberg::kInvalidArgument;
    }
    std::array<char, kTextCapacity> ascii{};
    const auto text = read_ascii(string, ascii);
    if (!text) { return Steinberg::kResultFalse; }
    double result = 0.0;
    if (tag == kSustainId) {
        if (*text == "On") { result = 1.0; }
        else if (*text != "Off") { return Steinberg::kResultFalse; }
    } else {
        const auto parsed = parse_decimal(*text, tag == kGainId || tag == kExpressionId,
            tag == kGainId ? 0x4040000000000000ULL : 0x3ff0000000000000ULL);
        if (!parsed) { return Steinberg::kResultFalse; }
        result = *parsed;
        if (tag == kGainId) {
            // A plain gain <= 2^-145 normalizes to <= half the least binary32
            // subnormal and quantizes to zero. Avoid unnecessary binary64
            // underflow/DAZ effects before the existing integer quantizer.
            result = std::bit_cast<std::uint64_t>(result) <= 0x36e0000000000000ULL ?
                0.0 : *control::widen(*control::quantize(result / 32.0));
        } else if (tag == kExpressionId) {
            result = *control::widen(*control::quantize(result));
        }
    }
    value = result;
    return Steinberg::kResultOk;
}

Steinberg::tresult PLUGIN_API RhodesController::getMidiControllerAssignment(
    Steinberg::int32 busIndex, Steinberg::int16 channel,
    Steinberg::Vst::CtrlNumber midiControllerNumber, Steinberg::Vst::ParamID& tag) {
    if (busIndex == 0 && channel >= 0 && channel < static_cast<Steinberg::int16>(kMidiChannelCount)) {
        if (midiControllerNumber == Steinberg::Vst::kCtrlExpression) {
            tag = kExpressionId;
            return Steinberg::kResultTrue;
        }
        if (midiControllerNumber == Steinberg::Vst::kCtrlSustainOnOff) {
            tag = kSustainId;
            return Steinberg::kResultTrue;
        }
        if (midiControllerNumber == Steinberg::Vst::kCtrlAllNotesOff) {
            tag = kAllNotesOffBaseId + static_cast<Steinberg::Vst::ParamID>(channel);
            return Steinberg::kResultTrue;
        }
    }
    return Steinberg::kResultFalse;
}

Steinberg::tresult PLUGIN_API RhodesController::queryInterface(const char* interface_id, void** obj) {
    if (interface_id == nullptr || obj == nullptr) { return Steinberg::kInvalidArgument; }
    QUERY_INTERFACE(interface_id, obj, Steinberg::Vst::IMidiMapping::iid, Steinberg::Vst::IMidiMapping)
    return EditController::queryInterface(interface_id, obj);
}

} // namespace rhodes::vst3
