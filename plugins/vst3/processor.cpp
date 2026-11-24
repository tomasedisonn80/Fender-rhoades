#include "processor.hpp"
#include "state_stream.hpp"

#include "rhodes/control_value.hpp"

#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"

#include <algorithm>
#include <cmath>
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

} // namespace

RhodesProcessor::RhodesProcessor() {
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
    // the fully checked setup directly only after core preparation succeeds.
    if (!engine_.prepare(setup.sampleRate)) { return Steinberg::kResultFalse; }
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
    const auto decoded = read_state_stream(stream, kStateModel);
    return decoded && mailbox_.request_recall(*decoded) ?
        Steinberg::kResultOk : Steinberg::kResultFalse;
}

Steinberg::tresult PLUGIN_API RhodesProcessor::getState(Steinberg::IBStream* stream) {
    return write_state_stream(stream, mailbox_.snapshot(), kStateModel) ?
        Steinberg::kResultOk : Steinberg::kResultFalse;
}

void RhodesProcessor::install_recall(InstrumentState state) noexcept {
    engine_.reset();
    notes_.reset();
    conversion_fault_ = false;
    live_state_ = state;
    // Reset has just cleared the engine's fault latch; this cannot fail.
    (void)engine_.set_sustain(live_state_.sustain);
}

void RhodesProcessor::reset_audio_owner() noexcept {
    const auto token = mailbox_.begin_block();
    install_recall(token.apply_recall ? token.recalled : live_state_);
    (void)mailbox_.end_block(token, live_state_);
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
        if (id != kGainId && id != kSustainId && id != kExpressionId && channel < 0) { return false; }
        const auto parameter_index = channel >= 0 ? 3 + static_cast<std::size_t>(channel) :
                                                   static_cast<std::size_t>(id);
        if (seen[parameter_index]) { return false; }
        seen[parameter_index] = true;
        const auto count = queue->getPointCount();
        const auto point_limit = channel >= 0 ? kMaximumChannelCommands : kMaximumHostFrames;
        if (count < 0 || count > static_cast<Steinberg::int32>(point_limit) ||
            (channel >= 0 && static_cast<std::size_t>(count) >
                kMaximumChannelCommands - channel_command_count_)) {
            return false;
        }
        auto& points = id == kGainId ? gain_points_ :
                       id == kExpressionId ? expression_points_ : sustain_points_;
        auto& distinct_count = id == kGainId ? gain_count_ :
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
            const float canonical = id == kSustainId ? (value >= 0.5 ? 1.0F : 0.0F) :
                *control::quantize(value);
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
    notes_ = candidate;
    if (data.numSamples == 0) {
        if (gain_count_ > 0) { live_state_.monitoring_gain = gain_points_[gain_count_ - 1].value; }
        if (expression_count_ > 0) {
            live_state_.expression = expression_points_[expression_count_ - 1].value;
        }
        if (sustain_count_ > 0) {
            live_state_.sustain = sustain_points_[sustain_count_ - 1].value == 1.0F;
            (void)engine_.set_sustain(live_state_.sustain);
        }
        for (std::size_t index = 0; index < physical_note_count_; ++index) {
            // Nonempty input event lists are rejected for zero-frame calls,
            // so these actions can only be preflighted CC123 releases.
            if (!engine_.release_key(physical_notes_[index].change.key)) {
                silence_outputs(data);
                (void)mailbox_.end_block(token, live_state_);
                return Steinberg::kResultFalse;
            }
        }
        silence_outputs(data);
        (void)mailbox_.end_block(token, live_state_);
        return Steinberg::kResultOk;
    }
    return render(data, token);
}

Steinberg::tresult RhodesProcessor::render(vst::ProcessData& data,
                                          const StateMailbox::BlockToken& token) noexcept {
    silence_outputs(data);
    auto* output = data.numOutputs == 1 ? &data.outputs[0] : nullptr;
    const Steinberg::int32 channels = output != nullptr ? output->numChannels : 0;
    const Steinberg::uint64 silent_mask = channels == 1 ? 1 : channels == 2 ? 3 : 0;
    std::size_t gain_index = 0;
    std::size_t expression_index = 0;
    std::size_t pedal_index = 0;
    std::size_t note_index = 0;
    Steinberg::int32 gain_left_offset = -1;
    Steinberg::int32 expression_left_offset = -1;
    // Recall and parameter preflight guarantee normalized binary32 storage.
    // Anchor both curves in the audio owner's state after the selected recall;
    // a mailbox snapshot may already contain a newer, unapplied UI recall.
    double left_gain = *control::widen(live_state_.monitoring_gain);
    double left_expression = *control::widen(live_state_.expression);
    bool any_nonzero = false;
    const auto fault = [&]() noexcept {
        conversion_fault_ = true;
        (void)mailbox_.end_block(token, live_state_);
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
        // Commit the valid pair together before DSP, retaining the existing
        // accepted-fault policy for the failing frame's configuration.
        live_state_.monitoring_gain = *stored_gain;
        live_state_.expression = *stored_expression;

        std::size_t dispatch_count = 0;
        if (pedal_index < sustain_count_ && sustain_points_[pedal_index].offset == frame) {
            live_state_.sustain = sustain_points_[pedal_index++].value == 1.0F;
            dispatch_[dispatch_count++] = {0, 0, EventType::sustain, 0,
                                          live_state_.sustain ? 1.0 : 0.0};
        }
        while (note_index < physical_note_count_ && physical_notes_[note_index].offset == frame) {
            const auto& change = physical_notes_[note_index++].change;
            dispatch_[dispatch_count] = {0, static_cast<std::uint32_t>(dispatch_count),
                                         change.type, change.key, change.value};
            ++dispatch_count;
        }

        double volts = 0.0;
        const auto result = engine_.process(std::span<double>(&volts, 1),
            std::span<const EngineEvent>(dispatch_.data(), dispatch_count));
        const double monitored = (32.0 * gain) * volts;
        if (!result.succeeded() || !std::isfinite(monitored)) { return fault(); }
        // Unity preserves the legacy arithmetic path exactly. Attenuation may
        // bring a finite double into float range, but never hides an engine or
        // monitoring fault. Conversion still runs when no output was supplied.
        const double converted = expression == 1.0 ? monitored : monitored * expression;
        if (!std::isfinite(converted)) { return fault(); }
        if (data.symbolicSampleSize == vst::kSample32) {
            const float sample = static_cast<float>(converted);
            if (!std::isfinite(sample)) { return fault(); }
            for (Steinberg::int32 channel = 0; channel < channels; ++channel) {
                if (output->channelBuffers32[channel] != nullptr) {
                    output->channelBuffers32[channel][frame] = sample;
                }
            }
            any_nonzero = any_nonzero || sample != 0.0F;
        } else {
            for (Steinberg::int32 channel = 0; channel < channels; ++channel) {
                if (output->channelBuffers64[channel] != nullptr) {
                    output->channelBuffers64[channel][frame] = converted;
                }
            }
            any_nonzero = any_nonzero || converted != 0.0;
        }
    }
    if (output != nullptr) { output->silenceFlags = any_nonzero ? 0 : silent_mask; }
    (void)mailbox_.end_block(token, live_state_);
    return Steinberg::kResultOk;
}

} // namespace rhodes::vst3
