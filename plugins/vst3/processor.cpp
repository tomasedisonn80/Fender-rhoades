#include "processor.hpp"
#include "state_stream.hpp"

#include "rhodes/control_value.hpp"

#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstmessage.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>
#include <span>

namespace rhodes::vst3 {
namespace {

namespace vst = Steinberg::Vst;

bool valid_mode(Steinberg::int32 mode) noexcept {
    return mode == vst::kRealtime || mode == vst::kPrefetch || mode == vst::kOffline;
}

bool valid_offset(Steinberg::int32 offset, Steinberg::int32 frames) noexcept {
    return offset >= 0 && (frames == 0 ? offset == 0 : offset < frames);
}

double mono_sample(StereoSample sample) noexcept {
    // The exact neutral path preserves signed zero and subnormal encodings.
    // Halving before summing also avoids overflow for finite unequal channels.
    if (std::bit_cast<std::uint64_t>(sample.left) ==
        std::bit_cast<std::uint64_t>(sample.right)) {
        return sample.left;
    }
    return 0.5 * sample.left + 0.5 * sample.right;
}

} // namespace

RhodesProcessor::RhodesProcessor()
    : panel_parameters_(std::make_unique<PanelParameterScratch>()) {
    setControllerClass(kControllerUID);
}

Steinberg::FUnknown* RhodesProcessor::createInstance(void* /*context*/) {
    return static_cast<vst::IAudioProcessor*>(new RhodesProcessor);
}

Steinberg::tresult PLUGIN_API RhodesProcessor::initialize(Steinberg::FUnknown* context) {
    if (initialized_) { return Steinberg::kResultFalse; }
    const auto result = AudioEffect::initialize(context);
    if (result != Steinberg::kResultOk) { return result; }
    addEventInput(STR16("MIDI In"), 16);
    addAudioOutput(STR16("Stereo Out"), vst::SpeakerArr::kStereo);
    initialized_ = true;
    return Steinberg::kResultOk;
}

Steinberg::tresult PLUGIN_API RhodesProcessor::terminate() {
    reset_audio_owner();
    configured_ = false;
    initialized_ = false;
    return AudioEffect::terminate();
}

Steinberg::tresult PLUGIN_API RhodesProcessor::setBusArrangements(
    vst::SpeakerArrangement* /*inputs*/, Steinberg::int32 numIns,
    vst::SpeakerArrangement* outputs, Steinberg::int32 numOuts) {
    // Validate before changing the SDK bus. The default implementation can
    // partially change bus arrangements before returning a failure.
    if (!initialized_ || numIns != 0 || numOuts != 1 || outputs == nullptr ||
        (outputs[0] != vst::SpeakerArr::kMono && outputs[0] != vst::SpeakerArr::kStereo)) {
        return Steinberg::kResultFalse;
    }
    auto* bus = getAudioOutput(0);
    if (bus == nullptr) { return Steinberg::kResultFalse; }
    // Hosts negotiate while suspended. Use the SDK bus as the single source of
    // layout metadata; changing the arrangement retains its activation state.
    bus->setName(outputs[0] == vst::SpeakerArr::kMono ? STR16("Mono Out") : STR16("Stereo Out"));
    bus->setArrangement(outputs[0]);
    return Steinberg::kResultTrue;
}

Steinberg::tresult PLUGIN_API RhodesProcessor::canProcessSampleSize(Steinberg::int32 sampleSize) {
    return sampleSize == vst::kSample32 || sampleSize == vst::kSample64 ?
        Steinberg::kResultTrue : Steinberg::kResultFalse;
}

Steinberg::tresult PLUGIN_API RhodesProcessor::setupProcessing(vst::ProcessSetup& setup) {
    if (!initialized_ || !valid_mode(setup.processMode) ||
        canProcessSampleSize(setup.symbolicSampleSize) != Steinberg::kResultTrue ||
        setup.maxSamplesPerBlock <= 0 ||
        setup.maxSamplesPerBlock > static_cast<Steinberg::int32>(kMaximumHostFrames) ||
        !std::isfinite(setup.sampleRate) || setup.sampleRate < minimum_sample_rate_hz ||
        setup.sampleRate > maximum_sample_rate_hz) {
        return Steinberg::kResultFalse;
    }
    // ResearchEngine::prepare is atomic on rejection. The base class's default
    // setupProcessing mutates some fields before validating sample size, so use
    // the fully checked setup directly only after both preparations succeed.
    // Prepare the small effect copy first so rejection cannot alter live DSP.
    auto prepared_effects = panel_effects_;
    if (!prepared_effects.prepare(setup.sampleRate) || !engine_.prepare(setup.sampleRate)) {
        return Steinberg::kResultFalse;
    }
    panel_effects_ = prepared_effects;
    processSetup = setup;
    configured_ = true;
    reset_audio_owner();
    return Steinberg::kResultOk;
}

Steinberg::tresult PLUGIN_API RhodesProcessor::setActive(Steinberg::TBool state) {
    reset_audio_owner();
    return AudioEffect::setActive(state);
}

Steinberg::tresult PLUGIN_API RhodesProcessor::setProcessing(Steinberg::TBool /*state*/) {
    // Host lifecycle transitions synchronize with processing. Reset is explicit
    // and works even after a core or output-conversion arithmetic failure.
    reset_audio_owner();
    return Steinberg::kResultOk;
}

Steinberg::uint32 PLUGIN_API RhodesProcessor::getTailSamples() {
    // The synthetic resonators decay asymptotically; no arbitrary tail cutoff.
    return vst::kInfiniteTail;
}

Steinberg::uint32 PLUGIN_API RhodesProcessor::getLatencySamples() {
    return kLatencyFrames;
}

Steinberg::tresult PLUGIN_API RhodesProcessor::setState(Steinberg::IBStream* stream) {
    const auto decoded = read_plugin_state_stream(stream, kStateModel);
    return decoded && mailbox_.request_recall(*decoded) ?
        Steinberg::kResultOk : Steinberg::kResultFalse;
}

Steinberg::tresult PLUGIN_API RhodesProcessor::getState(Steinberg::IBStream* stream) {
    return write_plugin_state_stream(stream, mailbox_.snapshot(), kStateModel) ?
        Steinberg::kResultOk : Steinberg::kResultFalse;
}

Steinberg::tresult PLUGIN_API RhodesProcessor::notify(vst::IMessage* message) {
    // IConnectionPoint::notify is a UI-thread SDK call. This is exactly one
    // serialized producer, not a general concurrent/reentrant message endpoint.
    // Validate all typed attributes before publishing anything; no engine,
    // ownership or configuration operation belongs on this thread.
    if (!initialized_ || message == nullptr) { return Steinberg::kResultFalse; }
    const auto* id = message->getMessageID();
    if (id == nullptr) { return Steinberg::kResultFalse; }
    if (std::strcmp(id, "RhodesPhotoReleaseV1") == 0) {
        audition_queue_.request_release_all();
        return Steinberg::kResultOk;
    }
    if (std::strcmp(id, "RhodesPhotoNoteV1") != 0) { return Steinberg::kResultFalse; }
    auto* attributes = message->getAttributes();
    Steinberg::int64 key = -1;
    Steinberg::int64 down = -1;
    double velocity = 0.0;
    if (attributes == nullptr ||
        attributes->getInt("key", key) != Steinberg::kResultOk ||
        attributes->getInt("down", down) != Steinberg::kResultOk ||
        attributes->getFloat("velocity", velocity) != Steinberg::kResultOk ||
        key < 0 || key >= static_cast<Steinberg::int64>(key_count) ||
        (down != 0 && down != 1)) {
        return Steinberg::kResultFalse;
    }
    // Integer-bit validation/rounding rejects negative subnormals and all NaNs
    // independently of the host's DAZ, FTZ and floating-point rounding modes.
    const auto canonical_velocity = control::quantize(velocity);
    if (!canonical_velocity) { return Steinberg::kResultFalse; }
    return audition_queue_.enqueue(
        {static_cast<int>(key), down == 1, *canonical_velocity}) ?
        Steinberg::kResultOk : Steinberg::kResultFalse;
}

void RhodesProcessor::install_recall(PluginState state) noexcept {
    engine_.reset();
    notes_.reset();
    ui_held_.fill(false);
    // A successful host recall/lifecycle reset discards one published prefix.
    // A concurrent UI publication after that capture stays queued. Keep the
    // applied epoch: an unacknowledged release conservatively wins over the
    // next captured batch. Neither producer cursor nor epoch is reset here.
    audition_queue_.discard();
    conversion_fault_ = false;
    live_state_ = state.instrument;
    live_panel_ = state.panel;
    panel_effects_.reset(live_panel_);
    // Reset has just cleared the engine's fault latch; this cannot fail.
    (void)engine_.set_sustain(live_state_.sustain);
}

void RhodesProcessor::reset_audio_owner() noexcept {
    const auto token = mailbox_.begin_block();
    install_recall(token.apply_recall ? token.recalled : audio_owner_state());
    (void)mailbox_.end_block(token, audio_owner_state());
}

bool RhodesProcessor::apply_editor_audition(
    const EditorAuditionQueue::Snapshot& snapshot) noexcept {
    const auto release_ui_key = [&](std::size_t key) noexcept {
        if (!ui_held_[key]) { return true; }
        // Consult ownership at the block boundary, before notes_ is replaced
        // by the fully translated host candidate (which includes future events).
        if (notes_.held_count(key) == 0 && !engine_.release_key(key)) { return false; }
        ui_held_[key] = false;
        return true;
    };
    for (std::size_t index = 0; index < snapshot.count; ++index) {
        const auto& command = snapshot.commands[index];
        const auto key = static_cast<std::size_t>(command.key);
        // Match note-on-zero semantics without passing a physical release
        // through the strike port when an external MIDI owner still holds it.
        if (command.down && !control::is_zero(command.velocity)) {
            if (!engine_.press_key(key, *control::widen(command.velocity))) { return false; }
            ui_held_[key] = true;
        } else if (!release_ui_key(key)) {
            return false;
        }
    }
    // Release follows the entire captured prefix, including any racing downs.
    // Overflow/focus loss/close releases only UI ownership, never shared pedal
    // state or MIDI holders. Work is bounded by 256 commands plus 73 releases.
    if (snapshot.release_epoch != applied_ui_release_epoch_) {
        for (std::size_t key = 0; key < key_count; ++key) {
            if (!release_ui_key(key)) { return false; }
        }
    }
    return true;
}

bool RhodesProcessor::commit_editor_audition(
    const EditorAuditionQueue::Snapshot& snapshot) noexcept {
    // Called only after this accepted block/flush succeeded. A failed host
    // preflight never peeks, applies or commits. Arithmetic faults retain the
    // prefix and epoch until an explicit reset discards it; no replay is possible
    // while the engine's fault fence is set.
    if (!audition_queue_.commit(snapshot)) {
        // Impossible under the single-consumer contract; never replay a prefix
        // whose physical actions already happened if that contract is violated.
        conversion_fault_ = true;
        return false;
    }
    applied_ui_release_epoch_ = snapshot.release_epoch;
    return true;
}

bool RhodesProcessor::validate_buffers(const vst::ProcessData& data) noexcept {
    if (!initialized_ || data.numSamples < 0 ||
        data.numSamples > static_cast<Steinberg::int32>(kMaximumHostFrames) ||
        data.numInputs != 0 || data.numOutputs < 0 || data.numOutputs > 1 ||
        !valid_mode(data.processMode) ||
        (data.symbolicSampleSize != vst::kSample32 && data.symbolicSampleSize != vst::kSample64)) {
        return false;
    }
    if (data.numOutputs == 1 && data.outputs == nullptr) { return false; }
    const auto* bus = getAudioOutput(0);
    if (bus == nullptr) { return false; }
    const auto arrangement = bus->getArrangement();
    if (arrangement != vst::SpeakerArr::kMono && arrangement != vst::SpeakerArr::kStereo) {
        return false;
    }
    const Steinberg::int32 channels = arrangement == vst::SpeakerArr::kMono ? 1 : 2;
    if (data.numSamples == 0) {
        // Hosts may omit all buffers or retain a zero-channel/negotiated-width
        // bus while flushing parameters. Never inspect its sample pointers.
        return data.numOutputs == 0 || data.outputs[0].numChannels == 0 ||
               data.outputs[0].numChannels == channels;
    }
    if (!configured_ || data.numSamples > processSetup.maxSamplesPerBlock ||
        data.symbolicSampleSize != processSetup.symbolicSampleSize ||
        ((data.processMode == vst::kOffline) != (processSetup.processMode == vst::kOffline))) {
        return false;
    }
    const bool active = bus->isActive();
    // The SDK permits omission of trailing inactive buses. Do not even form a
    // reference to outputs[0] when the sole output has been omitted.
    if (data.numOutputs == 0) { return !active; }
    if (data.outputs[0].numChannels != channels) { return false; }
    const auto valid_channels = [active, channels](const auto buffers) noexcept {
        if (buffers == nullptr) { return false; }
        if (active) {
            for (Steinberg::int32 channel = 0; channel < channels; ++channel) {
                if (buffers[channel] == nullptr) { return false; }
            }
        }
        return true;
    };
    if (data.symbolicSampleSize == vst::kSample32) {
        return valid_channels(data.outputs[0].channelBuffers32);
    }
    return valid_channels(data.outputs[0].channelBuffers64);
}

bool RhodesProcessor::collect_parameters(const vst::ProcessData& data) noexcept {
    gain_count_ = 0;
    expression_count_ = 0;
    sustain_count_ = 0;
    panel_parameters_->counts.fill(0);
    channel_command_count_ = 0;
    if (data.inputParameterChanges == nullptr) { return true; }
    const auto queue_count = data.inputParameterChanges->getParameterCount();
    if (queue_count < 0 ||
        queue_count > static_cast<Steinberg::int32>(kControllerParameterCount)) { return false; }
    std::array<bool, kControllerParameterCount> seen{};
    for (Steinberg::int32 queue_index = 0; queue_index < queue_count; ++queue_index) {
        auto* queue = data.inputParameterChanges->getParameterData(queue_index);
        if (queue == nullptr) { return false; }
        const auto id = queue->getParameterId();
        const auto channel = all_notes_off_channel(id);
        const bool panel_parameter = id >= kBassId && id <= kVibratoId;
        if (id != kGainId && id != kSustainId && id != kExpressionId &&
            !panel_parameter && channel < 0) { return false; }
        const auto panel_index = panel_parameter ? static_cast<std::size_t>(id - kBassId) : 0;
        // Keep the original nineteen queue slots stable, including all sixteen
        // channel commands; the five panel controls occupy slots 19 through 23.
        const auto parameter_index = channel >= 0 ? 3 + static_cast<std::size_t>(channel) :
            panel_parameter ? 19 + panel_index : static_cast<std::size_t>(id);
        if (seen[parameter_index]) { return false; }
        seen[parameter_index] = true;
        const auto count = queue->getPointCount();
        const auto point_limit = channel >= 0 ? kMaximumChannelCommands : kMaximumHostFrames;
        if (count < 0 || count > static_cast<Steinberg::int32>(point_limit) ||
            (channel >= 0 && static_cast<std::size_t>(count) >
                kMaximumChannelCommands - channel_command_count_)) {
            return false;
        }
        auto& points = panel_parameter ? panel_parameters_->points[panel_index] :
                       id == kGainId ? gain_points_ :
                       id == kExpressionId ? expression_points_ : sustain_points_;
        auto& distinct_count = panel_parameter ? panel_parameters_->counts[panel_index] :
                               id == kGainId ? gain_count_ :
                               id == kExpressionId ? expression_count_ : sustain_count_;
        Steinberg::int32 previous_offset = -1;
        for (Steinberg::int32 index = 0; index < count; ++index) {
            Steinberg::int32 offset = -1;
            vst::ParamValue value = std::numeric_limits<double>::quiet_NaN();
            if (queue->getPoint(index, offset, value) != Steinberg::kResultOk ||
                !valid_offset(offset, data.numSamples) || offset < previous_offset ||
                !control::valid_normalized(value)) {
                return false;
            }
            if (channel >= 0) {
                // Values are validated but never interpolated or edge-detected.
                // Every delivered CC123 point, including zero, is a command.
                channel_commands_[channel_command_count_++] =
                    {offset, static_cast<Steinberg::int16>(channel)};
                previous_offset = offset;
                continue;
            }
            const float canonical = id == kSustainId || id == kVibratoId ?
                (value >= 0.5 ? 1.0F : 0.0F) : *control::quantize(value);
            // A duplicate offset replaces the complete prior point, including
            // the endpoint used by interpolation from an earlier sample.
            if (distinct_count > 0 && offset == previous_offset) {
                points[distinct_count - 1].value = canonical;
            } else {
                points[distinct_count++] = {offset, canonical};
            }
            previous_offset = offset;
        }
    }
    // Fixed-bound stable insertion sort; no allocation, recursion or host calls.
    // Equal-offset channel order must not depend on the host's queue order.
    for (std::size_t index = 1; index < channel_command_count_; ++index) {
        const auto command = channel_commands_[index];
        auto position = index;
        while (position > 0) {
            const auto& previous = channel_commands_[position - 1];
            if (previous.offset < command.offset ||
                (previous.offset == command.offset && previous.channel <= command.channel)) { break; }
            channel_commands_[position] = previous;
            --position;
        }
        channel_commands_[position] = command;
    }
    return true;
}

bool RhodesProcessor::collect_notes(const vst::ProcessData& data) noexcept {
    host_note_count_ = 0;
    if (data.inputEvents == nullptr) { return true; }
    const auto count = data.inputEvents->getEventCount();
    if (count < 0 || count > static_cast<Steinberg::int32>(maximum_block_events) ||
        (data.numSamples == 0 && count != 0)) {
        return false;
    }
    Steinberg::int32 previous_offset = -1;
    for (Steinberg::int32 index = 0; index < count; ++index) {
        vst::Event event{};
        if (data.inputEvents->getEvent(index, event) != Steinberg::kResultOk ||
            event.busIndex != 0 || !valid_offset(event.sampleOffset, data.numSamples) ||
            event.sampleOffset < previous_offset || !std::isfinite(event.ppqPosition)) {
            return false;
        }
        MidiNote note;
        float raw_velocity = 0.0F;
        float tuning = 0.0F;
        if (event.type == vst::Event::kNoteOnEvent) {
            note = {true, event.noteOn.channel, event.noteOn.pitch,
                    event.noteOn.noteId, 0.0};
            raw_velocity = event.noteOn.velocity;
            tuning = event.noteOn.tuning;
        } else if (event.type == vst::Event::kNoteOffEvent) {
            note = {false, event.noteOff.channel, event.noteOff.pitch,
                    event.noteOff.noteId, 0.0};
            raw_velocity = event.noteOff.velocity;
            tuning = event.noteOff.tuning;
        } else if (event.type == vst::Event::kPolyPressureEvent) {
            // This synthetic model has no pressure response. Validate and
            // ignore the typed event without discarding accompanying releases.
            // All signed note IDs are legal; no ownership lookup is needed.
            if (event.polyPressure.channel < 0 || event.polyPressure.channel > 15 ||
                event.polyPressure.pitch < 0 || event.polyPressure.pitch > 127 ||
                !control::valid_normalized(event.polyPressure.pressure)) {
                return false;
            }
            previous_offset = event.sampleOffset;
            continue;
        } else {
            // In particular, LegacyMIDICCOutEvent is an output-only SDK type.
            // Unsupported input event kinds are rejected, never guessed at.
            return false;
        }
        // Validate the SDK's binary32 value before any widening instruction:
        // DAZ would otherwise erase the sign and magnitude of tiny velocities.
        const auto velocity = control::widen(raw_velocity);
        if (note.channel < 0 || note.channel > 15 || note.pitch < 0 || note.pitch > 127 ||
            !velocity || !std::isfinite(tuning)) {
            return false;
        }
        note.velocity = *velocity;
        // Finite tuning/PPQ/length metadata does not change this fixed synthetic
        // temperament. Only channel, pitch, ownership and velocity are used.
        host_notes_[host_note_count_++] = {event.sampleOffset, note};
        previous_offset = event.sampleOffset;
    }
    return true;
}

bool RhodesProcessor::translate_notes(NoteTracker& candidate) noexcept {
    physical_note_count_ = 0;
    std::size_t note_index = 0;
    std::size_t command_index = 0;
    const auto append = [&](Steinberg::int32 offset, NoteChange change) noexcept {
        if (physical_note_count_ == physical_notes_.size()) { return false; }
        physical_notes_[physical_note_count_++] = {offset, change};
        return true;
    };
    while (note_index < host_note_count_ || command_index < channel_command_count_) {
        const auto note_offset = note_index < host_note_count_ ? host_notes_[note_index].offset :
            std::numeric_limits<Steinberg::int32>::max();
        const auto command_offset = command_index < channel_command_count_ ?
            channel_commands_[command_index].offset : std::numeric_limits<Steinberg::int32>::max();
        const auto offset = std::min(note_offset, command_offset);
        // Channel commands precede notes at ties. Sustain is dispatched before
        // these physical actions by render(), and does not affect ownership.
        while (command_index < channel_command_count_ &&
               channel_commands_[command_index].offset == offset) {
            const auto result = candidate.release_channel(channel_commands_[command_index++].channel);
            if (!result.succeeded()) { return false; }
            for (std::size_t key = 0; key < result.count; ++key) {
                if (!append(offset, {EventType::note_off, result.keys[key], 0.0})) { return false; }
            }
        }
        while (note_index < host_note_count_ && host_notes_[note_index].offset == offset) {
            const auto result = candidate.apply(host_notes_[note_index++].note);
            if (!result.succeeded() || (result.change && !append(offset, *result.change))) { return false; }
        }
    }
    return validate_dispatch_bound();
}

bool RhodesProcessor::validate_dispatch_bound() const noexcept {
    std::size_t note_index = 0;
    std::size_t pedal_index = 0;
    while (note_index < physical_note_count_ || pedal_index < sustain_count_) {
        const auto note_offset = note_index < physical_note_count_ ?
            physical_notes_[note_index].offset : std::numeric_limits<Steinberg::int32>::max();
        const auto pedal_offset = pedal_index < sustain_count_ ?
            sustain_points_[pedal_index].offset : std::numeric_limits<Steinberg::int32>::max();
        const auto offset = std::min(note_offset, pedal_offset);
        std::size_t count = 0;
        if (pedal_index < sustain_count_ && pedal_offset == offset) { ++pedal_index; ++count; }
        while (note_index < physical_note_count_ && physical_notes_[note_index].offset == offset) {
            ++note_index;
            ++count;
        }
        if (count > maximum_block_events) { return false; }
    }
    return true;
}

void RhodesProcessor::silence_outputs(vst::ProcessData& data) noexcept {
    // Rejection cleanup has its own fixed envelope. Unsupported dimensions or
    // formats leave both samples and flags untouched, without traversing host
    // counts or claiming that a partially cleared output is silent.
    if (data.numSamples < 0 ||
        data.numSamples > static_cast<Steinberg::int32>(kMaximumHostFrames) ||
        data.numOutputs != 1 || data.outputs == nullptr ||
        (data.symbolicSampleSize != vst::kSample32 &&
         data.symbolicSampleSize != vst::kSample64)) {
        return;
    }
    auto& bus = data.outputs[0];
    if (data.numSamples == 0) {
        // Cleanup recognizes empty, mono and stereo buses, independently of
        // the negotiated layout. Flushes never inspect sample pointers.
        if (bus.numChannels == 0) { bus.silenceFlags = 0; }
        else if (bus.numChannels == 1) { bus.silenceFlags = 1; }
        else if (bus.numChannels == 2) { bus.silenceFlags = 3; }
        return;
    }
    if (bus.numChannels != 1 && bus.numChannels != 2) { return; }
    bus.silenceFlags = bus.numChannels == 1 ? 1 : 3;
    if (data.symbolicSampleSize == vst::kSample32 && bus.channelBuffers32 != nullptr) {
        for (Steinberg::int32 channel = 0; channel < bus.numChannels; ++channel) {
            if (bus.channelBuffers32[channel] != nullptr) {
                std::fill_n(bus.channelBuffers32[channel], data.numSamples, 0.0F);
            }
        }
    } else if (data.symbolicSampleSize == vst::kSample64 && bus.channelBuffers64 != nullptr) {
        for (Steinberg::int32 channel = 0; channel < bus.numChannels; ++channel) {
            if (bus.channelBuffers64[channel] != nullptr) {
                std::fill_n(bus.channelBuffers64[channel], data.numSamples, 0.0);
            }
        }
    }
}

Steinberg::tresult PLUGIN_API RhodesProcessor::process(vst::ProcessData& data) {
    const auto reject = [&]() noexcept {
        silence_outputs(data);
        return Steinberg::kResultFalse;
    };
    // No mailbox consumption or audio-state mutation until the complete host
    // syntax has been copied and validated, including the final queue point.
    if (!validate_buffers(data) || !collect_parameters(data) || !collect_notes(data)) {
        return reject();
    }

    const auto token = mailbox_.begin_block();
    NoteTracker candidate = token.apply_recall ? NoteTracker{} : notes_;
    const auto cursor = token.apply_recall ? std::uint64_t{0} : engine_.frame_cursor();
    if (!translate_notes(candidate) ||
        static_cast<std::uint64_t>(data.numSamples) >
            std::numeric_limits<std::uint64_t>::max() - cursor ||
        (!token.apply_recall && requires_reset())) {
        mailbox_.abort_block(token);
        return reject();
    }

    if (token.apply_recall) { install_recall(token.recalled); }
    // Full input/ownership preflight passed. An arithmetic fault after this
    // accepted transaction requires explicit reset, and never rolls back audio.
    const auto audition = audition_queue_.peek();
    const bool audition_applied = apply_editor_audition(audition);
    notes_ = candidate;
    if (!audition_applied) {
        silence_outputs(data);
        (void)mailbox_.end_block(token, audio_owner_state());
        return Steinberg::kResultFalse;
    }
    if (data.numSamples == 0) {
        if (gain_count_ > 0) { live_state_.monitoring_gain = gain_points_[gain_count_ - 1].value; }
        if (expression_count_ > 0) {
            live_state_.expression = expression_points_[expression_count_ - 1].value;
        }
        if (sustain_count_ > 0) {
            live_state_.sustain = sustain_points_[sustain_count_ - 1].value == 1.0F;
            (void)engine_.set_sustain(live_state_.sustain);
        }
        const std::array<float*, 4> panel_controls{
            &live_panel_.bass, &live_panel_.treble, &live_panel_.intensity, &live_panel_.speed};
        for (std::size_t index = 0; index < panel_controls.size(); ++index) {
            const auto count = panel_parameters_->counts[index];
            if (count > 0) {
                *panel_controls[index] = panel_parameters_->points[index][count - 1].value;
            }
        }
        if (panel_parameters_->counts[4] > 0) {
            live_panel_.vibrato = panel_parameters_->points[4]
                [panel_parameters_->counts[4] - 1].value == 1.0F;
        }
        for (std::size_t index = 0; index < physical_note_count_; ++index) {
            // Nonempty input event lists are rejected for zero-frame calls,
            // so these actions can only be preflighted CC123 releases.
            const auto key = physical_notes_[index].change.key;
            if (!ui_held_[key] && !engine_.release_key(key)) {
                silence_outputs(data);
                (void)mailbox_.end_block(token, audio_owner_state());
                return Steinberg::kResultFalse;
            }
        }
        silence_outputs(data);
        (void)mailbox_.end_block(token, audio_owner_state());
        // UI strikes/releases also work in a zero-frame flush, preserving the
        // cursor without clearing mechanical/filter/effect histories.
        return commit_editor_audition(audition) ? Steinberg::kResultOk : Steinberg::kResultFalse;
    }
    const auto result = render(data, token);
    if (result == Steinberg::kResultOk && !commit_editor_audition(audition)) {
        silence_outputs(data);
        return Steinberg::kResultFalse;
    }
    return result;
}

Steinberg::tresult RhodesProcessor::render(vst::ProcessData& data,
                                          const PluginStateMailbox::BlockToken& token) noexcept {
    silence_outputs(data);
    auto* output = data.numOutputs == 1 ? &data.outputs[0] : nullptr;
    const Steinberg::int32 channels = output != nullptr ? output->numChannels : 0;
    const Steinberg::uint64 silent_mask = channels == 1 ? 1 : channels == 2 ? 3 : 0;
    // The negotiated layout also governs conversion when an inactive output
    // is omitted. validate_buffers() has already established this bus exists.
    const bool mono = getAudioOutput(0)->getArrangement() == vst::SpeakerArr::kMono;
    std::size_t gain_index = 0;
    std::size_t expression_index = 0;
    std::size_t pedal_index = 0;
    std::size_t note_index = 0;
    std::array<std::size_t, 4> panel_indices{};
    std::size_t vibrato_index = 0;
    Steinberg::int32 gain_left_offset = -1;
    Steinberg::int32 expression_left_offset = -1;
    std::array<Steinberg::int32, 4> panel_left_offsets{-1, -1, -1, -1};
    // Recall and parameter preflight guarantee normalized binary32 storage.
    // Anchor both curves in the audio owner's state after the selected recall;
    // a mailbox snapshot may already contain a newer, unapplied UI recall.
    double left_gain = *control::widen(live_state_.monitoring_gain);
    double left_expression = *control::widen(live_state_.expression);
    std::array<double, 4> left_panel{
        *control::widen(live_panel_.bass), *control::widen(live_panel_.treble),
        *control::widen(live_panel_.intensity), *control::widen(live_panel_.speed)};
    bool any_nonzero = false;
    const auto fault = [&]() noexcept {
        conversion_fault_ = true;
        (void)mailbox_.end_block(token, audio_owner_state());
        if (output != nullptr) { output->silenceFlags = any_nonzero ? 0 : silent_mask; }
        return Steinberg::kResultFalse;
    };
    for (Steinberg::int32 frame = 0; frame < data.numSamples; ++frame) {
        double gain = left_gain;
        if (gain_index < gain_count_) {
            const auto& point = gain_points_[gain_index];
            if (point.offset == frame) {
                gain = *control::widen(point.value);
                left_gain = gain;
                gain_left_offset = frame;
                ++gain_index;
            } else {
                const double fraction = static_cast<double>(frame - gain_left_offset) /
                                        static_cast<double>(point.offset - gain_left_offset);
                gain = left_gain + (*control::widen(point.value) - left_gain) * fraction;
            }
        }
        double expression = left_expression;
        if (expression_index < expression_count_) {
            const auto& point = expression_points_[expression_index];
            if (point.offset == frame) {
                expression = *control::widen(point.value);
                left_expression = expression;
                expression_left_offset = frame;
                ++expression_index;
            } else {
                const double fraction = static_cast<double>(frame - expression_left_offset) /
                                        static_cast<double>(point.offset - expression_left_offset);
                expression = left_expression +
                    (*control::widen(point.value) - left_expression) * fraction;
            }
        }
        const auto stored_gain = control::quantize(gain);
        const auto stored_expression = control::quantize(expression);
        if (!stored_gain || !stored_expression) { return fault(); }
        std::array<float, 4> stored_panel{};
        for (std::size_t index = 0; index < stored_panel.size(); ++index) {
            double value = left_panel[index];
            auto& next = panel_indices[index];
            if (next < panel_parameters_->counts[index]) {
                const auto& point = panel_parameters_->points[index][next];
                if (point.offset == frame) {
                    value = *control::widen(point.value);
                    left_panel[index] = value;
                    panel_left_offsets[index] = frame;
                    ++next;
                } else {
                    const double fraction = static_cast<double>(frame - panel_left_offsets[index]) /
                        static_cast<double>(point.offset - panel_left_offsets[index]);
                    value = left_panel[index] +
                        (*control::widen(point.value) - left_panel[index]) * fraction;
                }
            }
            const auto canonical = control::quantize(value);
            if (!canonical) { return fault(); }
            stored_panel[index] = *canonical;
        }
        // Commit all valid continuous controls before DSP, retaining the existing
        // accepted-fault policy for the failing frame's configuration.
        live_state_.monitoring_gain = *stored_gain;
        live_state_.expression = *stored_expression;
        live_panel_ = {stored_panel[0], stored_panel[1], stored_panel[2], stored_panel[3],
                       live_panel_.vibrato};
        if (vibrato_index < panel_parameters_->counts[4] &&
            panel_parameters_->points[4][vibrato_index].offset == frame) {
            live_panel_.vibrato = panel_parameters_->points[4][vibrato_index++].value == 1.0F;
        }

        std::size_t dispatch_count = 0;
        if (pedal_index < sustain_count_ && sustain_points_[pedal_index].offset == frame) {
            live_state_.sustain = sustain_points_[pedal_index++].value == 1.0F;
            dispatch_[dispatch_count++] = {0, 0, EventType::sustain, 0,
                                          live_state_.sustain ? 1.0 : 0.0};
        }
        while (note_index < physical_note_count_ && physical_notes_[note_index].offset == frame) {
            const auto& change = physical_notes_[note_index++].change;
            // MIDI still owns and retriggers its normal notes. Only its final
            // physical release waits while the independent photo key is held.
            if (change.type == EventType::note_off && ui_held_[change.key]) { continue; }
            dispatch_[dispatch_count] = {0, static_cast<std::uint32_t>(dispatch_count),
                                         change.type, change.key, change.value};
            ++dispatch_count;
        }

        double volts = 0.0;
        const auto result = engine_.process(std::span<double>(&volts, 1),
            std::span<const EngineEvent>(dispatch_.data(), dispatch_count));
        const double monitored = (32.0 * gain) * volts;
        if (!result.succeeded() || !std::isfinite(monitored)) { return fault(); }
        auto effected = panel_effects_.process(monitored, live_panel_);
        if (!std::isfinite(effected.left) || !std::isfinite(effected.right)) { return fault(); }
        if (mono) {
            const double folded = mono_sample(effected);
            effected = {folded, folded};
        }
        // Unity preserves the legacy arithmetic path exactly. Attenuation may
        // bring a finite double into float range, but never hides an engine or
        // monitoring fault. Conversion still runs when no output was supplied.
        const double converted_left = expression == 1.0 ? effected.left : effected.left * expression;
        const double converted_right = expression == 1.0 ? effected.right : effected.right * expression;
        if (!std::isfinite(converted_left) || !std::isfinite(converted_right)) { return fault(); }
        if (data.symbolicSampleSize == vst::kSample32) {
            const float left = static_cast<float>(converted_left);
            const float right = static_cast<float>(converted_right);
            if (!std::isfinite(left) || !std::isfinite(right)) { return fault(); }
            for (Steinberg::int32 channel = 0; channel < channels; ++channel) {
                if (output->channelBuffers32[channel] != nullptr) {
                    output->channelBuffers32[channel][frame] = channel == 0 ? left : right;
                }
            }
            any_nonzero = any_nonzero || left != 0.0F || right != 0.0F;
        } else {
            for (Steinberg::int32 channel = 0; channel < channels; ++channel) {
                if (output->channelBuffers64[channel] != nullptr) {
                    output->channelBuffers64[channel][frame] =
                        channel == 0 ? converted_left : converted_right;
                }
            }
            any_nonzero = any_nonzero || converted_left != 0.0 || converted_right != 0.0;
        }
    }
    if (output != nullptr) { output->silenceFlags = any_nonzero ? 0 : silent_mask; }
    (void)mailbox_.end_block(token, audio_owner_state());
    return Steinberg::kResultOk;
}

} // namespace rhodes::vst3
