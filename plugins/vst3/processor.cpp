#include "processor.hpp"
#include "state_stream.hpp"

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

bool valid_normalized(double value) noexcept {
    return std::isfinite(value) && value >= 0.0 && value <= 1.0;
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
    // Validate before invoking the SDK helper, whose default implementation can
    // partially change bus arrangements before returning a failure.
    if (numIns != 0 || numOuts != 1 || outputs == nullptr ||
        outputs[0] != vst::SpeakerArr::kStereo || !initialized_) {
        return Steinberg::kResultFalse;
    }
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

Steinberg::tresult PLUGIN_API RhodesProcessor::setState(Steinberg::IBStream* stream) {
    const auto decoded = read_state_stream(stream);
    return decoded && mailbox_.request_recall(*decoded) ?
        Steinberg::kResultOk : Steinberg::kResultFalse;
}

Steinberg::tresult PLUGIN_API RhodesProcessor::getState(Steinberg::IBStream* stream) {
    return write_state_stream(stream, mailbox_.snapshot()) ?
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
    if (data.numSamples == 0) {
        // Hosts may omit all buffers or retain a zero-channel/stereo bus while
        // flushing parameters. No sample pointer is accessed during a flush.
        return data.numOutputs == 0 || data.outputs[0].numChannels == 0 ||
               data.outputs[0].numChannels == 2;
    }
    if (!configured_ || data.numSamples > processSetup.maxSamplesPerBlock ||
        data.symbolicSampleSize != processSetup.symbolicSampleSize ||
        ((data.processMode == vst::kOffline) != (processSetup.processMode == vst::kOffline)) ||
        data.numOutputs != 1 || data.outputs[0].numChannels != 2) {
        return false;
    }
    const auto* bus = getAudioOutput(0);
    const bool active = bus != nullptr && bus->isActive();
    if (data.symbolicSampleSize == vst::kSample32) {
        const auto buffers = data.outputs[0].channelBuffers32;
        return buffers != nullptr && (!active || (buffers[0] != nullptr && buffers[1] != nullptr));
    }
    const auto buffers = data.outputs[0].channelBuffers64;
    return buffers != nullptr && (!active || (buffers[0] != nullptr && buffers[1] != nullptr));
}

bool RhodesProcessor::collect_parameters(const vst::ProcessData& data) noexcept {
    gain_count_ = 0;
    sustain_count_ = 0;
    if (data.inputParameterChanges == nullptr) { return true; }
    const auto queue_count = data.inputParameterChanges->getParameterCount();
    if (queue_count < 0 || queue_count > 2) { return false; }
    std::array<bool, 2> seen{};
    for (Steinberg::int32 queue_index = 0; queue_index < queue_count; ++queue_index) {
        auto* queue = data.inputParameterChanges->getParameterData(queue_index);
        if (queue == nullptr) { return false; }
        const auto id = queue->getParameterId();
        if ((id != kGainId && id != kSustainId) || seen[id]) { return false; }
        seen[id] = true;
        const auto count = queue->getPointCount();
        if (count < 0 || count > static_cast<Steinberg::int32>(kMaximumHostFrames)) {
            return false;
        }
        auto& points = id == kGainId ? gain_points_ : sustain_points_;
        auto& distinct_count = id == kGainId ? gain_count_ : sustain_count_;
        Steinberg::int32 previous_offset = -1;
        for (Steinberg::int32 index = 0; index < count; ++index) {
            Steinberg::int32 offset = -1;
            vst::ParamValue value = std::numeric_limits<double>::quiet_NaN();
            if (queue->getPoint(index, offset, value) != Steinberg::kResultOk ||
                !valid_offset(offset, data.numSamples) || offset < previous_offset ||
                !valid_normalized(value)) {
                return false;
            }
            const float canonical = id == kSustainId ? (value >= 0.5 ? 1.0F : 0.0F) :
                (value == 0.0 ? 0.0F : static_cast<float>(value));
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
        float tuning = 0.0F;
        if (event.type == vst::Event::kNoteOnEvent) {
            note = {true, event.noteOn.channel, event.noteOn.pitch,
                    event.noteOn.noteId, event.noteOn.velocity};
            tuning = event.noteOn.tuning;
        } else if (event.type == vst::Event::kNoteOffEvent) {
            note = {false, event.noteOff.channel, event.noteOff.pitch,
                    event.noteOff.noteId, event.noteOff.velocity};
            tuning = event.noteOff.tuning;
        } else {
            // In particular, LegacyMIDICCOutEvent is an output-only SDK type.
            // Unsupported input event kinds are rejected, never guessed at.
            return false;
        }
        if (note.channel < 0 || note.channel > 15 || note.pitch < 0 || note.pitch > 127 ||
            !valid_normalized(note.velocity) || !std::isfinite(tuning)) {
            return false;
        }
        // Finite tuning/PPQ/length metadata does not change this fixed synthetic
        // temperament. Only channel, pitch, ownership and velocity are used.
        host_notes_[host_note_count_++] = {event.sampleOffset, note};
        previous_offset = event.sampleOffset;
    }
    return true;
}

bool RhodesProcessor::translate_notes(NoteTracker& candidate) noexcept {
    physical_note_count_ = 0;
    for (std::size_t index = 0; index < host_note_count_; ++index) {
        const auto& input = host_notes_[index];
        const auto result = candidate.apply(input.note);
        if (!result.succeeded()) { return false; }
        if (result.change) {
            if (physical_note_count_ == physical_notes_.size()) { return false; }
            physical_notes_[physical_note_count_++] = {input.offset, *result.change};
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
    if (data.numOutputs <= 0 || data.outputs == nullptr) { return; }
    // The host owns buffer storage and its declared dimensions. For rejected
    // oversized buffers, clearing necessarily scales with the supplied size.
    for (Steinberg::int32 bus_index = 0; bus_index < data.numOutputs; ++bus_index) {
        auto& bus = data.outputs[bus_index];
        if (bus.numChannels < 0) { continue; }
        bus.silenceFlags = bus.numChannels >= 64 ? std::numeric_limits<Steinberg::uint64>::max() :
            ((Steinberg::uint64{1} << static_cast<unsigned>(bus.numChannels)) - 1U);
        if (data.numSamples <= 0) { continue; }
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
        if (sustain_count_ > 0) {
            live_state_.sustain = sustain_points_[sustain_count_ - 1].value == 1.0F;
            (void)engine_.set_sustain(live_state_.sustain);
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
    auto& output = data.outputs[0];
    std::size_t gain_index = 0;
    std::size_t pedal_index = 0;
    std::size_t note_index = 0;
    Steinberg::int32 left_offset = -1;
    double left_gain = live_state_.monitoring_gain;
    bool any_nonzero = false;
    for (Steinberg::int32 frame = 0; frame < data.numSamples; ++frame) {
        double gain = left_gain;
        if (gain_index < gain_count_) {
            const auto& point = gain_points_[gain_index];
            if (point.offset == frame) {
                gain = point.value;
                left_gain = gain;
                left_offset = frame;
                ++gain_index;
            } else {
                const double fraction = static_cast<double>(frame - left_offset) /
                                        static_cast<double>(point.offset - left_offset);
                gain = left_gain + (static_cast<double>(point.value) - left_gain) * fraction;
            }
        }
        live_state_.monitoring_gain = gain == 0.0 ? 0.0F : static_cast<float>(gain);

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
        const double converted = (32.0 * gain) * volts;
        if (!result.succeeded() || !std::isfinite(converted)) {
            conversion_fault_ = true;
            (void)mailbox_.end_block(token, live_state_);
            output.silenceFlags = any_nonzero ? 0 : 3;
            return Steinberg::kResultFalse;
        }
        if (data.symbolicSampleSize == vst::kSample32) {
            const float sample = static_cast<float>(converted);
            if (!std::isfinite(sample)) {
                conversion_fault_ = true;
                (void)mailbox_.end_block(token, live_state_);
                output.silenceFlags = any_nonzero ? 0 : 3;
                return Steinberg::kResultFalse;
            }
            for (std::size_t channel = 0; channel < 2; ++channel) {
                if (output.channelBuffers32[channel] != nullptr) {
                    output.channelBuffers32[channel][frame] = sample;
                }
            }
            any_nonzero = any_nonzero || sample != 0.0F;
        } else {
            for (std::size_t channel = 0; channel < 2; ++channel) {
                if (output.channelBuffers64[channel] != nullptr) {
                    output.channelBuffers64[channel][frame] = converted;
                }
            }
            any_nonzero = any_nonzero || converted != 0.0;
        }
    }
    output.silenceFlags = any_nonzero ? 0 : 3;
    (void)mailbox_.end_block(token, live_state_);
    return Steinberg::kResultOk;
}

} // namespace rhodes::vst3
