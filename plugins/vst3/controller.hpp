#pragma once

#include "plugin_ids.hpp"
#include "public.sdk/source/vst/vsteditcontroller.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace rhodes::vst3 {

// Host controls, MIDI mapping and an optional native photograph editor.
class RhodesController final : public Steinberg::Vst::EditController,
                               public Steinberg::Vst::IMidiMapping {
public:
    static Steinberg::FUnknown* createInstance(void* context);

    Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown* context) override;
    Steinberg::tresult PLUGIN_API terminate() override;
    Steinberg::tresult PLUGIN_API setComponentState(Steinberg::IBStream* state) override;
    Steinberg::tresult PLUGIN_API setState(Steinberg::IBStream* state) override;
    Steinberg::tresult PLUGIN_API getState(Steinberg::IBStream* state) override;
    Steinberg::IPlugView* PLUGIN_API createView(Steinberg::FIDString name) override;
    // Serialized UI-thread ownership. Tokens never alias a closed view, and
    // only the last holder of a key sends its ordered V1 note-up message.
    using AuditionOwner = std::uint64_t;
    static constexpr std::size_t kMaximumAuditionOwners = 32;
    [[nodiscard]] AuditionOwner registerAuditionOwner();
    [[nodiscard]] bool unregisterAuditionOwner(AuditionOwner owner);
    [[nodiscard]] int heldAuditionKey(AuditionOwner owner) const noexcept;
    [[nodiscard]] bool auditionNote(AuditionOwner owner, int key, bool down, float velocity);
    // Global release is reserved for failed transport. A failed emergency
    // remains pending; idle/teardown retries it before any new note is sent.
    [[nodiscard]] bool releaseAuditionNotes();
    [[nodiscard]] bool retryAuditionRelease();
    Steinberg::tresult PLUGIN_API setParamNormalized(Steinberg::Vst::ParamID tag,
                                                    Steinberg::Vst::ParamValue value) override;
    Steinberg::tresult PLUGIN_API getParamStringByValue(
        Steinberg::Vst::ParamID tag, Steinberg::Vst::ParamValue value,
        Steinberg::Vst::String128 string) override;
    Steinberg::tresult PLUGIN_API getParamValueByString(
        Steinberg::Vst::ParamID tag, Steinberg::Vst::TChar* string,
        Steinberg::Vst::ParamValue& value) override;
    Steinberg::tresult PLUGIN_API getMidiControllerAssignment(
        Steinberg::int32 busIndex, Steinberg::int16 channel,
        Steinberg::Vst::CtrlNumber midiControllerNumber,
        Steinberg::Vst::ParamID& tag) override;

    DELEGATE_REFCOUNT(Steinberg::Vst::EditController)
    Steinberg::tresult PLUGIN_API queryInterface(const char* interface_id, void** obj) override;

private:
    struct AuditionState {
        AuditionOwner owner = 0;
        int key = -1;
    };
    [[nodiscard]] AuditionState* findAuditionOwner(AuditionOwner owner) noexcept;
    [[nodiscard]] bool auditionKeyHasOwner(int key) const noexcept;
    [[nodiscard]] bool sendAuditionNote(int key, bool down, float velocity);
    // Allows the regression fixture to reach the otherwise impractical uint64
    // exhaustion boundary without adding a host-facing or editor-facing API.
    friend struct AuditionOwnershipTestAccess;
    std::array<AuditionState, kMaximumAuditionOwners> audition_owners_{};
    AuditionOwner next_audition_owner_ = 1;
    bool audition_release_pending_ = false;
    bool initialized_ = false;
};

} // namespace rhodes::vst3
