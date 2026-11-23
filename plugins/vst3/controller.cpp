#include "controller.hpp"
#include "state_stream.hpp"

#include "pluginterfaces/vst/ivstmidicontrollers.h"

#include <cmath>

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
    const auto decoded = read_state_stream(stream);
    if (!decoded || getParameterObject(kGainId) == nullptr ||
        getParameterObject(kSustainId) == nullptr) {
        return Steinberg::kResultFalse;
    }
    // Both parameters and the entire chunk have been checked before either
    // parameter changes, so malformed recall leaves the controller unchanged.
    (void)setParamNormalized(kGainId, decoded->monitoring_gain);
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
    const InstrumentState state{static_cast<float>(getParamNormalized(kGainId)),
                                getParamNormalized(kSustainId) >= 0.5};
    return write_state_stream(stream, state) ? Steinberg::kResultOk : Steinberg::kResultFalse;
}

Steinberg::tresult PLUGIN_API RhodesController::setParamNormalized(
    Steinberg::Vst::ParamID tag, Steinberg::Vst::ParamValue value) {
    if ((tag != kGainId && tag != kSustainId) || !std::isfinite(value) ||
        value < 0.0 || value > 1.0) {
        return Steinberg::kInvalidArgument;
    }
    const double canonical = tag == kSustainId ? (value >= 0.5 ? 1.0 : 0.0) :
        static_cast<double>(value == 0.0 ? 0.0F : static_cast<float>(value));
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

Steinberg::tresult PLUGIN_API RhodesController::queryInterface(const char* iid, void** obj) {
    if (iid == nullptr || obj == nullptr) { return Steinberg::kInvalidArgument; }
    QUERY_INTERFACE(iid, obj, Steinberg::Vst::IMidiMapping::iid, Steinberg::Vst::IMidiMapping)
    return EditController::queryInterface(iid, obj);
}

} // namespace rhodes::vst3
