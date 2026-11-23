#pragma once

#include "plugin_ids.hpp"
#include "public.sdk/source/vst/vsteditcontroller.h"

namespace rhodes::vst3 {

// Headless controller: ordinary host parameter controls and CC64 mapping only.
class RhodesController final : public Steinberg::Vst::EditController,
                               public Steinberg::Vst::IMidiMapping {
public:
    static Steinberg::FUnknown* createInstance(void* context);

    Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown* context) override;
    Steinberg::tresult PLUGIN_API terminate() override;
    Steinberg::tresult PLUGIN_API setComponentState(Steinberg::IBStream* state) override;
    Steinberg::tresult PLUGIN_API setState(Steinberg::IBStream* state) override;
    Steinberg::tresult PLUGIN_API getState(Steinberg::IBStream* state) override;
    Steinberg::tresult PLUGIN_API setParamNormalized(Steinberg::Vst::ParamID tag,
                                                    Steinberg::Vst::ParamValue value) override;
    Steinberg::tresult PLUGIN_API getMidiControllerAssignment(
        Steinberg::int32 busIndex, Steinberg::int16 channel,
        Steinberg::Vst::CtrlNumber midiControllerNumber,
        Steinberg::Vst::ParamID& tag) override;

    DELEGATE_REFCOUNT(Steinberg::Vst::EditController)
    Steinberg::tresult PLUGIN_API queryInterface(const char* interface_id, void** obj) override;

private:
    bool initialized_ = false;
};

} // namespace rhodes::vst3
