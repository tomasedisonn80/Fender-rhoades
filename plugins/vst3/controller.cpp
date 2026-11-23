#include "controller.hpp"
#include "state_stream.hpp"

#include "rhodes/control_value.hpp"

#include "pluginterfaces/vst/ivstmidicontrollers.h"

namespace rhodes::vst3 {

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
        getParameterObject(kSustainId) == nullptr) {
        return Steinberg::kResultFalse;
    }
    const auto gain = control::widen(decoded->monitoring_gain);
    if (!gain) { return Steinberg::kResultFalse; }
    // Both parameters and the entire chunk have been checked before either
    // parameter changes, so malformed recall leaves the controller unchanged.
    (void)setParamNormalized(kGainId, *gain);
    (void)setParamNormalized(kSustainId, decoded->sustain ? 1.0 : 0.0);
    return Steinberg::kResultOk;
}

Steinberg::tresult PLUGIN_API RhodesController::setState(Steinberg::IBStream* state) {
    return setComponentState(state);
}

Steinberg::tresult PLUGIN_API RhodesController::getState(Steinberg::IBStream* stream) {
    if (getParameterObject(kGainId) == nullptr || getParameterObject(kSustainId) == nullptr) {
        return Steinberg::kResultFalse;
    }
    const auto gain = control::quantize(getParamNormalized(kGainId));
    if (!gain) { return Steinberg::kResultFalse; }
    const InstrumentState state{*gain, getParamNormalized(kSustainId) >= 0.5};
    return write_state_stream(stream, state, kStateModel) ? Steinberg::kResultOk : Steinberg::kResultFalse;
}

Steinberg::tresult PLUGIN_API RhodesController::setParamNormalized(
    Steinberg::Vst::ParamID tag, Steinberg::Vst::ParamValue value) {
    if ((tag != kGainId && tag != kSustainId) || !control::valid_normalized(value)) {
        return Steinberg::kInvalidArgument;
    }
    const double canonical = tag == kSustainId ? (value >= 0.5 ? 1.0 : 0.0) :
        *control::widen(*control::quantize(value));
    return EditController::setParamNormalized(tag, canonical);
}

Steinberg::tresult PLUGIN_API RhodesController::getMidiControllerAssignment(
    Steinberg::int32 busIndex, Steinberg::int16 channel,
    Steinberg::Vst::CtrlNumber midiControllerNumber, Steinberg::Vst::ParamID& tag) {
    if (busIndex == 0 && channel >= 0 && channel < 16 &&
        midiControllerNumber == Steinberg::Vst::kCtrlSustainOnOff) {
        tag = kSustainId;
        return Steinberg::kResultTrue;
    }
    return Steinberg::kResultFalse;
}

Steinberg::tresult PLUGIN_API RhodesController::queryInterface(const char* interface_id, void** obj) {
    if (interface_id == nullptr || obj == nullptr) { return Steinberg::kInvalidArgument; }
    QUERY_INTERFACE(interface_id, obj, Steinberg::Vst::IMidiMapping::iid, Steinberg::Vst::IMidiMapping)
    return EditController::queryInterface(interface_id, obj);
}

} // namespace rhodes::vst3
