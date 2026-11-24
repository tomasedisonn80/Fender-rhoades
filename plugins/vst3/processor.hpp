#pragma once

#include "plugin_ids.hpp"
#include "rhodes/event_processor.hpp"
#include "rhodes/audio_engine.hpp"
#include "rhodes/quadrature_engine.hpp"
#include "rhodes/front_panel_effects.hpp"
#include "rhodes/plugin_state.hpp"
#include "rhodes/note_tracker.hpp"
#include "rhodes/editor_audition.hpp"
#include "public.sdk/source/vst/vstaudioeffect.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace rhodes::vst3 {

#if defined(RHODES_QUADRATURE_VST3)
using HostEventProcessor = BasicEventProcessor<QuadratureEngine>;
static_assert(QuadratureEngine::latency_frames() == kLatencyFrames);
#elif defined(RHODES_FILTERED_VST3)
using HostEventProcessor = BasicEventProcessor<AudioEngine>;
static_assert(AudioEngine::latency_frames() == kLatencyFrames);
#else
using HostEventProcessor = EventProcessor;
#endif

// This adapter intentionally uses the synthetic impulse ResearchEngine. The
// offline contact reference is not part of its real-time signal path.
class RhodesProcessor final : public Steinberg::Vst::AudioEffect {
public:
    RhodesProcessor();
    static Steinberg::FUnknown* createInstance(void* context);

    Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown* context) override;
    Steinberg::tresult PLUGIN_API terminate() override;
    Steinberg::tresult PLUGIN_API setBusArrangements(
        Steinberg::Vst::SpeakerArrangement* inputs, Steinberg::int32 numIns,
        Steinberg::Vst::SpeakerArrangement* outputs, Steinberg::int32 numOuts) override;
    Steinberg::tresult PLUGIN_API canProcessSampleSize(Steinberg::int32 sampleSize) override;
    Steinberg::tresult PLUGIN_API setupProcessing(Steinberg::Vst::ProcessSetup& setup) override;
    Steinberg::tresult PLUGIN_API setActive(Steinberg::TBool state) override;
    Steinberg::tresult PLUGIN_API setProcessing(Steinberg::TBool state) override;
    Steinberg::tresult PLUGIN_API process(Steinberg::Vst::ProcessData& data) override;
    Steinberg::uint32 PLUGIN_API getTailSamples() override;
    Steinberg::uint32 PLUGIN_API getLatencySamples() override;
    Steinberg::tresult PLUGIN_API setState(Steinberg::IBStream* state) override;
    Steinberg::tresult PLUGIN_API getState(Steinberg::IBStream* state) override;
    Steinberg::tresult PLUGIN_API notify(Steinberg::Vst::IMessage* message) override;

    // Read-only diagnostics for a suspended processor/test host. Only the audio
    // owner may inspect these while processing; published_state() is thread safe.
    [[nodiscard]] const HostEventProcessor& engine_processor() const noexcept { return engine_; }
    [[nodiscard]] const NoteTracker& note_tracker() const noexcept { return notes_; }
    [[nodiscard]] bool ui_key_held(std::size_t key) const noexcept {
        return key < key_count && ui_held_[key];
    }
    [[nodiscard]] InstrumentState current_state() const noexcept { return live_state_; }
    [[nodiscard]] FrontPanelState current_panel_state() const noexcept { return live_panel_; }
    [[nodiscard]] InstrumentState published_state() const noexcept {
        return mailbox_.snapshot().instrument;
    }
    [[nodiscard]] bool requires_reset() const noexcept {
        return conversion_fault_ || engine_.requires_reset() || panel_effects_.requires_reset();
    }

private:
    struct ParameterPoint {
        Steinberg::int32 offset = 0;
        float value = 0.0F;
    };
    struct PanelParameterScratch {
        std::array<std::array<ParameterPoint, kMaximumHostFrames>, 5> points{};
        std::array<std::size_t, 5> counts{};
    };
    struct HostNote {
        Steinberg::int32 offset = 0;
        MidiNote note{};
    };
    struct ChannelCommand {
        Steinberg::int32 offset = 0;
        Steinberg::int16 channel = 0;
    };
    struct PhysicalNote {
        Steinberg::int32 offset = 0;
        NoteChange change{};
    };

    [[nodiscard]] bool validate_buffers(const Steinberg::Vst::ProcessData& data) noexcept;
    [[nodiscard]] bool collect_parameters(const Steinberg::Vst::ProcessData& data) noexcept;
    [[nodiscard]] bool collect_notes(const Steinberg::Vst::ProcessData& data) noexcept;
    [[nodiscard]] bool translate_notes(NoteTracker& candidate) noexcept;
    [[nodiscard]] bool validate_dispatch_bound() const noexcept;
    [[nodiscard]] bool apply_editor_audition(
        const EditorAuditionQueue::Snapshot& snapshot) noexcept;
    [[nodiscard]] bool commit_editor_audition(
        const EditorAuditionQueue::Snapshot& snapshot) noexcept;
    [[nodiscard]] PluginState audio_owner_state() const noexcept {
        return {live_state_, live_panel_};
    }
    void reset_audio_owner() noexcept;
    void install_recall(PluginState state) noexcept;
    [[nodiscard]] Steinberg::tresult render(Steinberg::Vst::ProcessData& data,
                                           const PluginStateMailbox::BlockToken& token) noexcept;
    static void silence_outputs(Steinberg::Vst::ProcessData& data) noexcept;

    HostEventProcessor engine_{};
    NoteTracker notes_{};
    // SDK notify is a serialized UI-thread producer; it never touches the
    // engine or these audio-owned held flags. Processing is the sole consumer;
    // lifecycle callbacks assume that ownership only while audio is stopped.
    EditorAuditionQueue audition_queue_{};
    std::array<bool, key_count> ui_held_{};
    std::uint64_t applied_ui_release_epoch_ = 0;
    FrontPanelEffects panel_effects_{};
    PluginStateMailbox mailbox_{};
    InstrumentState live_state_{};
    FrontPanelState live_panel_{};
    bool initialized_ = false;
    bool configured_ = false;
    bool conversion_fault_ = false;

    // Instance-owned fixed scratch. Staging input never changes live ownership,
    // mechanics, configuration or the mailbox, even on a late invalid point.
    std::array<ParameterPoint, kMaximumHostFrames> gain_points_{};
    std::array<ParameterPoint, kMaximumHostFrames> expression_points_{};
    std::array<ParameterPoint, kMaximumHostFrames> sustain_points_{};
    // Allocate the five new fixed queues once, without enlarging stack-resident
    // processor instances by another 320 KiB on hosts with small thread stacks.
    std::unique_ptr<PanelParameterScratch> panel_parameters_;
    std::array<ChannelCommand, kMaximumChannelCommands> channel_commands_{};
    std::array<HostNote, maximum_block_events> host_notes_{};
    // Every release consumes an initially held key or a preceding note-on.
    // At most 256 input-note actions plus 73+256 releases can be produced.
    std::array<PhysicalNote, 2 * maximum_block_events + key_count> physical_notes_{};
    std::array<EngineEvent, maximum_block_events> dispatch_{};
    std::size_t gain_count_ = 0;
    std::size_t expression_count_ = 0;
    std::size_t sustain_count_ = 0;
    std::size_t channel_command_count_ = 0;
    std::size_t host_note_count_ = 0;
    std::size_t physical_note_count_ = 0;
};

} // namespace rhodes::vst3
