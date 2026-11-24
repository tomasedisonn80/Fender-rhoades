#include "controller.hpp"
#include "state_stream.hpp"
#if defined(RHODES_HAS_PHOTO_EDITOR)
#include "photo_editor.hpp"
#endif

#include "rhodes/control_value.hpp"
#include "rhodes/control_text.hpp"

#include "pluginterfaces/vst/ivstmidicontrollers.h"
#include "pluginterfaces/vst/ivstmessage.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string_view>

namespace rhodes::vst3 {
namespace {

constexpr std::size_t kTextCapacity = 128;

constexpr std::array<Steinberg::Vst::ParamID, 8> kPersistentIds{
    kGainId, kSustainId, kExpressionId, kBassId, kTrebleId,
    kIntensityId, kSpeedId, kVibratoId};

[[nodiscard]] bool panel_continuous_parameter(Steinberg::Vst::ParamID tag) noexcept {
    return tag == kBassId || tag == kTrebleId || tag == kIntensityId || tag == kSpeedId;
}

[[nodiscard]] bool known_parameter(Steinberg::Vst::ParamID tag) noexcept {
    return tag == kGainId || tag == kSustainId || tag == kExpressionId ||
           panel_continuous_parameter(tag) || tag == kVibratoId ||
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

[[nodiscard]] std::string_view panel_suffix(Steinberg::Vst::ParamID tag) noexcept {
    return tag == kIntensityId ? "x" : tag == kSpeedId ? "Hz" : "dB";
}

// New panel text uses physical units for EQ and speed. Intensity is a 0..1x
// factor (equivalent to 0..100%), keeping every number inside the existing
// portable decimal helper's bounded domain. Suffixes are optional and exact.
// Affine physical display has binary64 precision; persistence retains the full
// canonical binary32 normalized value independently of display rounding.
[[nodiscard]] std::optional<double> parse_panel_decimal(
    Steinberg::Vst::ParamID tag, std::string_view text) noexcept {
    text = trim_ascii(text);
    const auto suffix = panel_suffix(tag);
    if (text.ends_with(suffix)) {
        text.remove_suffix(suffix.size());
        text = trim_ascii(text);
    }
    const auto parsed = control_text::parse_decimal(text);
    if (!parsed) { return std::nullopt; }
    const auto bits = std::bit_cast<std::uint64_t>(*parsed);
    double normalized = *parsed;
    if (tag == kBassId || tag == kTrebleId) {
        if ((bits & 0x7fffffffffffffffULL) > 0x4028000000000000ULL) {
            return std::nullopt;
        }
        normalized = (*parsed + 12.0) / 24.0;
    } else if (tag == kSpeedId) {
        if (bits < 0x3fe0000000000000ULL || bits > 0x4028000000000000ULL) {
            return std::nullopt;
        }
        normalized = (*parsed - 0.5) / 11.5;
    } else if (!control::valid_normalized(*parsed)) {
        return std::nullopt;
    }
    const auto canonical = control::quantize(normalized);
    return canonical ? control::widen(*canonical) : std::nullopt;
}

[[nodiscard]] std::optional<std::size_t> format_panel_decimal(
    Steinberg::Vst::ParamID tag, double value,
    std::array<char, kTextCapacity>& text) noexcept {
    const auto canonical = *control::widen(*control::quantize(value));
    const auto plain = tag == kBassId || tag == kTrebleId ? canonical * 24.0 - 12.0 :
                       tag == kSpeedId ? canonical * 11.5 + 0.5 : canonical;
    const auto count = control_text::format_decimal(plain, text);
    if (!count) { return std::nullopt; }
    auto length = *count;
    const auto suffix = panel_suffix(tag);
    if (length + 1U + suffix.size() >= text.size()) { return std::nullopt; }
    text[length++] = ' ';
    for (const auto character : suffix) { text[length++] = character; }
    return length;
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

Steinberg::IPlugView* PLUGIN_API RhodesController::createView(Steinberg::FIDString name) {
#if defined(RHODES_HAS_PHOTO_EDITOR)
    if (initialized_ && name != nullptr && std::strcmp(name, Steinberg::Vst::ViewType::kEditor) == 0) {
        return create_photo_editor(this);
    }
#else
    (void)name;
#endif
    return nullptr;
}

RhodesController::AuditionOwner RhodesController::registerAuditionOwner() {
    if (!initialized_ || next_audition_owner_ == 0) { return 0; }
    for (auto& state : audition_owners_) {
        if (state.owner == 0) {
            // Unsigned wrap reaches the reserved zero sentinel and permanently
            // refuses further registrations, rather than reusing a stale token.
            state = {next_audition_owner_++, -1};
            return state.owner;
        }
    }
    return 0;
}

RhodesController::AuditionState* RhodesController::findAuditionOwner(
    AuditionOwner owner) noexcept {
    if (owner == 0) { return nullptr; }
    for (auto& state : audition_owners_) {
        if (state.owner == owner) { return &state; }
    }
    return nullptr;
}

int RhodesController::heldAuditionKey(AuditionOwner owner) const noexcept {
    if (owner != 0) {
        for (const auto& state : audition_owners_) {
            if (state.owner == owner) { return state.key; }
        }
    }
    return -1;
}

bool RhodesController::auditionKeyHasOwner(int key) const noexcept {
    return std::any_of(audition_owners_.begin(), audition_owners_.end(),
                       [key](const auto& state) { return state.owner != 0 && state.key == key; });
}

bool RhodesController::unregisterAuditionOwner(AuditionOwner owner) {
    auto* state = findAuditionOwner(owner);
    if (state == nullptr) { return true; }
    const int key = state->key;
    *state = {};
    if (audition_release_pending_) { return retryAuditionRelease(); }
    if (key < 0 || auditionKeyHasOwner(key)) { return true; }
    if (sendAuditionNote(key, false, 0.0F)) { return true; }
    (void)releaseAuditionNotes();
    return false;
}

bool RhodesController::auditionNote(AuditionOwner owner, int key, bool down, float velocity) {
    auto* state = findAuditionOwner(owner);
    if (!initialized_ || state == nullptr || key < 0 || key >= 73 ||
        !control::valid_normalized(velocity)) {
        return false;
    }
    // The processor treats either signed note-on zero as a release. Apply
    // that meaning before ownership aggregation so it cannot release a key
    // still held by a sibling view, or manufacture an owner for a silent down.
    down = down && !control::is_zero(velocity);
    if (!down) { velocity = 0.0F; }
    if (audition_release_pending_) {
        // Even successful recovery cancels this attempt. Do not append a fresh
        // down to the same call that sent the out-of-band emergency release.
        (void)retryAuditionRelease();
        return false;
    }
    if (!down) {
        if (state->key != key) { return true; }
        state->key = -1;
        if (auditionKeyHasOwner(key)) { return true; }
    } else {
        if (state->key >= 0 && state->key != key) {
            if (!auditionNote(owner, state->key, false, 0.0F)) { return false; }
            state = findAuditionOwner(owner);
            if (state == nullptr) { return false; }
        }
        state->key = key;
    }
    if (sendAuditionNote(key, down, velocity)) { return true; }
    // A rejected send can represent a full processor queue. V1's existing
    // emergency contract cancels every UI owner, so mirror that state here.
    (void)releaseAuditionNotes();
    return false;
}

bool RhodesController::sendAuditionNote(int key, bool down, float velocity) {
    auto message = Steinberg::owned(allocateMessage());
    if (!message) { return false; }
    message->setMessageID("RhodesPhotoNoteV1");
    auto* attributes = message->getAttributes();
    if (attributes == nullptr ||
        attributes->setInt("key", key) != Steinberg::kResultOk ||
        attributes->setInt("down", down ? 1 : 0) != Steinberg::kResultOk ||
        attributes->setFloat("velocity", *control::widen(velocity)) != Steinberg::kResultOk) {
        return false;
    }
    return sendMessage(message) == Steinberg::kResultOk;
}

bool RhodesController::releaseAuditionNotes() {
    for (auto& state : audition_owners_) { state.key = -1; }
    audition_release_pending_ = true;
    return retryAuditionRelease();
}

bool RhodesController::retryAuditionRelease() {
    if (!audition_release_pending_) { return true; }
    if (!initialized_) { return false; }
    auto message = Steinberg::owned(allocateMessage());
    if (!message) { return false; }
    message->setMessageID("RhodesPhotoReleaseV1");
    if (sendMessage(message) != Steinberg::kResultOk) { return false; }
    audition_release_pending_ = false;
    return true;
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
    // Keep every legacy parameter index stable, including all sixteen commands.
    parameters.addParameter(new Steinberg::Vst::RangeParameter(
        STR16("Bass"), kBassId, STR16("dB"), -12.0, 12.0, 0.0));
    parameters.addParameter(new Steinberg::Vst::RangeParameter(
        STR16("Treble"), kTrebleId, STR16("dB"), -12.0, 12.0, 0.0));
    parameters.addParameter(new Steinberg::Vst::RangeParameter(
        STR16("Intensity"), kIntensityId, STR16("x"), 0.0, 1.0, 0.0));
    parameters.addParameter(new Steinberg::Vst::RangeParameter(
        STR16("Speed"), kSpeedId, STR16("Hz"), 0.5, 12.0, 6.25));
    auto* vibrato = new Steinberg::Vst::StringListParameter(STR16("Vibrato"), kVibratoId);
    vibrato->appendString(STR16("Off"));
    vibrato->appendString(STR16("On"));
    parameters.addParameter(vibrato);
    initialized_ = true;
    return Steinberg::kResultOk;
}

Steinberg::tresult PLUGIN_API RhodesController::terminate() {
    // Also clean up if a host terminates with an outstanding view token. Idle
    // owners send nothing; held notes still use ordered last-owner note-ups.
    for (const auto& state : audition_owners_) {
        if (state.owner != 0) { (void)unregisterAuditionOwner(state.owner); }
    }
    (void)retryAuditionRelease();
    const auto result = EditController::terminate();
    if (result == Steinberg::kResultOk) { initialized_ = false; }
    return result;
}

Steinberg::tresult PLUGIN_API RhodesController::setComponentState(Steinberg::IBStream* stream) {
    const auto decoded = read_plugin_state_stream(stream, kStateModel);
    if (!decoded) { return Steinberg::kResultFalse; }
    for (const auto tag : kPersistentIds) {
        if (getParameterObject(tag) == nullptr) { return Steinberg::kResultFalse; }
    }
    // Decode validates the complete configuration before any UI-thread parameter
    // mutation. A legacy packet supplies neutral panel defaults in the same recall.
    const std::array<double, kPersistentIds.size()> values{
        *control::widen(decoded->instrument.monitoring_gain),
        decoded->instrument.sustain ? 1.0 : 0.0,
        *control::widen(decoded->instrument.expression),
        *control::widen(decoded->panel.bass), *control::widen(decoded->panel.treble),
        *control::widen(decoded->panel.intensity), *control::widen(decoded->panel.speed),
        decoded->panel.vibrato ? 1.0 : 0.0};
    for (std::size_t index = 0; index < kPersistentIds.size(); ++index) {
        (void)setParamNormalized(kPersistentIds[index], values[index]);
    }
    return Steinberg::kResultOk;
}

Steinberg::tresult PLUGIN_API RhodesController::setState(Steinberg::IBStream* state) {
    return setComponentState(state);
}

Steinberg::tresult PLUGIN_API RhodesController::getState(Steinberg::IBStream* stream) {
    for (const auto tag : kPersistentIds) {
        if (getParameterObject(tag) == nullptr) { return Steinberg::kResultFalse; }
    }
    const auto gain = control::quantize(getParamNormalized(kGainId));
    const auto expression = control::quantize(getParamNormalized(kExpressionId));
    const auto bass = control::quantize(getParamNormalized(kBassId));
    const auto treble = control::quantize(getParamNormalized(kTrebleId));
    const auto intensity = control::quantize(getParamNormalized(kIntensityId));
    const auto speed = control::quantize(getParamNormalized(kSpeedId));
    const auto sustain = getParamNormalized(kSustainId);
    const auto vibrato = getParamNormalized(kVibratoId);
    if (!gain || !expression || !bass || !treble || !intensity || !speed ||
        !control::is_boolean(sustain) || !control::is_boolean(vibrato)) {
        return Steinberg::kResultFalse;
    }
    const PluginState state{{*gain, sustain >= 0.5, *expression},
                            {*bass, *treble, *intensity, *speed, vibrato >= 0.5}};
    return write_plugin_state_stream(stream, state, kStateModel) ?
        Steinberg::kResultOk : Steinberg::kResultFalse;
}

Steinberg::tresult PLUGIN_API RhodesController::setParamNormalized(
    Steinberg::Vst::ParamID tag, Steinberg::Vst::ParamValue value) {
    if (!initialized_ || !known_parameter(tag) || !control::valid_normalized(value)) {
        return Steinberg::kInvalidArgument;
    }
    // Match the SDK's rejection without recording an unaccepted mutation.
    if (getParameterObject(tag) == nullptr) { return Steinberg::kResultFalse; }
    const double canonical = (tag == kGainId || tag == kExpressionId ||
                              panel_continuous_parameter(tag)) ?
        *control::widen(*control::quantize(value)) :
        (tag == kSustainId || tag == kVibratoId) ? (value >= 0.5 ? 1.0 : 0.0) :
        control::is_zero(value) ? 0.0 : value;
    // Advance before setNormalized can synchronously notify SDK observers. A
    // nested host write must remain distinguishable even if it has equal value.
    if (tag < parameter_revisions_.size()) { ++parameter_revisions_[tag]; }
    return EditController::setParamNormalized(tag, canonical);
}

std::uint64_t RhodesController::parameterRevision(Steinberg::Vst::ParamID tag) const noexcept {
    return tag < parameter_revisions_.size() ? parameter_revisions_[tag] : 0;
}

Steinberg::tresult PLUGIN_API RhodesController::getParamStringByValue(
    Steinberg::Vst::ParamID tag, Steinberg::Vst::ParamValue value,
    Steinberg::Vst::String128 string) {
    if (!initialized_ || !known_parameter(tag) || getParameterObject(tag) == nullptr ||
        string == nullptr || !control::valid_normalized(value)) {
        return Steinberg::kInvalidArgument;
    }
    Steinberg::Vst::String128 result{};
    if (tag == kSustainId || tag == kVibratoId) {
        write_ascii(value >= 0.5 ? "On" : "Off", result);
    } else if (panel_continuous_parameter(tag)) {
        std::array<char, kTextCapacity> ascii{};
        const auto count = format_panel_decimal(tag, value, ascii);
        if (!count) { return Steinberg::kResultFalse; }
        write_ascii({ascii.data(), *count}, result);
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
    if (tag == kSustainId || tag == kVibratoId) {
        if (*text == "On") { result = 1.0; }
        else if (*text != "Off") { return Steinberg::kResultFalse; }
    } else if (panel_continuous_parameter(tag)) {
        const auto parsed = parse_panel_decimal(tag, *text);
        if (!parsed) { return Steinberg::kResultFalse; }
        result = *parsed;
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
