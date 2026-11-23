#include "tests/cpp/vst3_test_support.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace {
using namespace rhodes;
using namespace rhodes::vst3;
using namespace rhodes::vst3::test_host;

static_assert(kMaximumHostFrames == 8192, "Review the declared host-bound contract");
constexpr std::size_t capacity = 8193;
constexpr sdk::int32 configured_frames = 64;
constexpr sdk::uint64 untouched_flags = 0x5a5a5a5a5a5a5a5aULL;
std::size_t checks = 0;
std::size_t cases = 0;
std::size_t process_calls = 0;

void check(bool condition, std::string_view message) {
    ++checks;
    if (!condition) { throw std::runtime_error(std::string{message}); }
}

sdk::tresult invoke(RhodesProcessor& processor, vst::ProcessData& data) {
    ++process_calls;
    return processor.process(data);
}

template <class Sample>
struct GuardedBuffers {
    static constexpr sdk::int32 sample_size = std::is_same_v<Sample, float> ?
        vst::kSample32 : vst::kSample64;
    std::array<std::array<Sample, capacity + 2>, 3> storage{};
    std::array<Sample*, 3> channels{};
    // Both declared buses have real, bounded storage. They may alias in the
    // rejected two-output-bus case; no fictitious or invalid pointer is used.
    std::array<vst::AudioBusBuffers, 2> buses{};

    GuardedBuffers() { restore(); }

    void restore() {
        for (std::size_t channel = 0; channel < channels.size(); ++channel) {
            channels[channel] = storage[channel].data() + 1;
        }
        for (auto& bus : buses) {
            bus = {};
            bus.numChannels = 2;
            bus.silenceFlags = untouched_flags;
            if constexpr (std::is_same_v<Sample, float>) {
                bus.channelBuffers32 = channels.data();
            } else {
                bus.channelBuffers64 = channels.data();
            }
        }
        fill();
    }

    void fill() {
        for (auto& channel : storage) {
            channel.fill(Sample{7});
            channel.front() = Sample{-123};
            channel.back() = Sample{123};
        }
        for (auto& bus : buses) { bus.silenceFlags = untouched_flags; }
    }

    void guards_and_tail(std::size_t frames) const {
        check(frames <= capacity, "invalid test buffer extent");
        for (const auto& channel : storage) {
            check(channel.front() == Sample{-123} && channel.back() == Sample{123},
                  "processing touched a caller-storage guard");
            check(std::all_of(channel.begin() + static_cast<std::ptrdiff_t>(frames + 1),
                              channel.end() - 1,
                              [](Sample value) { return value == Sample{7}; }),
                  "processing wrote past the caller-declared frame count");
        }
    }

    void cleared(std::size_t frames, unsigned mask) const {
        guards_and_tail(frames);
        for (std::size_t channel = 0; channel < storage.size(); ++channel) {
            const Sample expected = (mask & (1U << channel)) != 0U ? Sample{0} : Sample{7};
            check(std::all_of(storage[channel].begin() + 1,
                              storage[channel].begin() + static_cast<std::ptrdiff_t>(frames + 1),
                              [expected](Sample value) { return value == expected; }),
                  "rejection cleared the wrong caller-owned sample region");
        }
    }

    void unchanged() const { cleared(0, 0); }

    void matches(const GuardedBuffers& reference, std::size_t frames, unsigned mask = 3) const {
        guards_and_tail(frames);
        bool nonzero = false;
        for (std::size_t channel = 0; channel < storage.size(); ++channel) {
            bool equal = true;
            for (std::size_t frame = 1; frame <= frames; ++frame) {
                const auto expected = (mask & (1U << channel)) != 0U ?
                    reference.storage[channel][frame] : Sample{7};
                equal = equal && std::isfinite(storage[channel][frame]) &&
                    storage[channel][frame] == expected;
                if (channel < 2) { nonzero = nonzero || reference.storage[channel][frame] != Sample{0}; }
            }
            check(equal, "rendered samples or omitted-channel storage differ from the control host");
        }
        // An inactive bus with no supplied sample pointers has no output data
        // whose silence flags a consumer can use. Do not prescribe that hint.
        if (mask != 0U) {
            check(buses[0].silenceFlags == (nonzero ? 0U : 3U), "incorrect rendered silence flags");
        }
    }
};

template <class Sample>
struct Host {
    RhodesProcessor processor;
    GuardedBuffers<Sample> buffers;
    vst::ProcessData data{};
    sdk::int32 setup_mode = vst::kRealtime;

    explicit Host(bool configured = true, sdk::int32 mode = vst::kRealtime) : setup_mode(mode) {
        check(processor.initialize(nullptr) == sdk::kResultOk, "initialize buffer-test host");
        check(processor.activateBus(vst::kAudio, vst::kOutput, 0, true) == sdk::kResultOk,
              "activate buffer-test output");
        if (configured) { setup(); }
        restore();
    }
    ~Host() { (void)processor.terminate(); }
    Host(const Host&) = delete;
    Host& operator=(const Host&) = delete;

    void setup() {
        vst::ProcessSetup requested{setup_mode, GuardedBuffers<Sample>::sample_size,
                                    configured_frames, 48000.0};
        check(processor.setupProcessing(requested) == sdk::kResultOk, "configure buffer-test host");
        check(processor.setActive(true) == sdk::kResultOk, "activate buffer-test component");
        check(processor.setProcessing(true) == sdk::kResultOk, "start buffer-test processor");
    }

    void restore(sdk::int32 frames = 16) {
        buffers.restore();
        data = {};
        data.processMode = setup_mode;
        data.symbolicSampleSize = GuardedBuffers<Sample>::sample_size;
        data.numSamples = frames;
        data.numOutputs = 1;
        data.outputs = buffers.buses.data();
    }
};

EncodedInstrumentState saved_state(RhodesProcessor& processor) {
    MemoryStream stream;
    check(processor.getState(&stream) == sdk::kResultOk, "save real processor state");
    check(stream.extent == 16 && stream.position == 16, "saved state has wrong extent");
    EncodedInstrumentState bytes{};
    std::copy_n(stream.bytes.begin(), bytes.size(), bytes.begin());
    return bytes;
}

template <class Sample>
void compare_state(Host<Sample>& actual, Host<Sample>& reference) {
    check(saved_state(actual.processor) == saved_state(reference.processor),
          "serialized configuration differs from the independent control instance");
    check(actual.processor.engine_processor().frame_cursor() ==
              reference.processor.engine_processor().frame_cursor(), "control-instance cursor differs");
    check(actual.processor.note_tracker().active_count() ==
              reference.processor.note_tracker().active_count(), "control-instance ownership differs");
}

template <class Sample>
void pair_block(Host<Sample>& actual, Host<Sample>& reference, EventList& events, sdk::int32 frames) {
    actual.restore(frames);
    reference.restore(frames);
    actual.data.inputEvents = &events;
    reference.data.inputEvents = &events;
    check(invoke(actual.processor, actual.data) == sdk::kResultOk, "actual continuation failed");
    check(invoke(reference.processor, reference.data) == sdk::kResultOk, "control continuation failed");
    actual.buffers.matches(reference.buffers, static_cast<std::size_t>(frames));
    reference.buffers.guards_and_tail(static_cast<std::size_t>(frames));
    compare_state(actual, reference);
}

template <class Sample>
void prime(Host<Sample>& actual, Host<Sample>& reference) {
    EventList events;
    events.count = 2;
    events.values[0] = note_on(0, 101, 60, 0.875F, 0);
    events.values[1] = note_on(5, 202, 67, 0.5F, 1);
    pair_block(actual, reference, events, 16);
    check(std::any_of(reference.buffers.storage[0].begin() + 1,
                      reference.buffers.storage[0].begin() + 17,
                      [](Sample value) { return value != Sample{0}; }), "priming fixture was silent");
}

template <class Sample>
void continue_pair(Host<Sample>& actual, Host<Sample>& reference) {
    EventList events;
    events.count = 3;
    events.values[0] = note_off(0, 101, 60, 0);
    events.values[1] = note_on(3, 303, 84, 0.625F, 3);
    events.values[2] = note_off(15, 202, 67, 1);
    pair_block(actual, reference, events, 17);
    events.count = 2;
    events.values[0] = note_on(0, 101, 60, 0.25F, 0);
    events.values[1] = note_off(30, 303, 84, 3);
    pair_block(actual, reference, events, 31);
    events.count = 0;
    pair_block(actual, reference, events, configured_frames);
}

template <class Sample>
void queue_recall(Host<Sample>& actual, Host<Sample>& reference) {
    MemoryStream left, right;
    left.load({0.25F, true});
    right.load({0.25F, true});
    check(actual.processor.setState(&left) == sdk::kResultOk &&
              reference.processor.setState(&right) == sdk::kResultOk, "queue matching configuration recalls");
}

template <class Sample>
void inactive_cases() {
    for (unsigned mask = 0; mask < 4; ++mask) {
        ++cases;
        auto actual = std::make_unique<Host<Sample>>();
        auto reference = std::make_unique<Host<Sample>>();
        prime(*actual, *reference);
        actual->restore(32);
        reference->restore(32);
        check(actual->processor.activateBus(vst::kAudio, vst::kOutput, 0, false) == sdk::kResultOk,
              "deactivate output bus");
        for (unsigned channel = 0; channel < 2; ++channel) {
            if ((mask & (1U << channel)) == 0U) { actual->buffers.channels[channel] = nullptr; }
        }
        // Real-time and prefetch processing may share one non-offline setup.
        if ((mask & 1U) != 0U) { actual->data.processMode = vst::kPrefetch; }
        EventList events;
        events.count = 2;
        events.values[0] = note_off(7, 101, 60, 0);
        events.values[1] = note_on(19, 404, 77, 0.5F, 4);
        actual->data.inputEvents = &events;
        reference->data.inputEvents = &events;
        check(invoke(actual->processor, actual->data) == sdk::kResultOk, "inactive buffer mask rejected");
        check(invoke(reference->processor, reference->data) == sdk::kResultOk, "inactive control render failed");
        actual->buffers.matches(reference->buffers, 32, mask);
        compare_state(*actual, *reference);
        check(actual->processor.activateBus(vst::kAudio, vst::kOutput, 0, true) == sdk::kResultOk,
              "reactivate output bus");
        continue_pair(*actual, *reference);
    }
}

template <class Sample>
void flush_cases() {
    for (bool configured : {false, true}) {
        for (sdk::int32 format : {vst::kSample32, vst::kSample64}) {
            for (sdk::int32 mode : {vst::kRealtime, vst::kPrefetch, vst::kOffline}) {
                for (int shape = 0; shape < 4; ++shape) {
                    ++cases;
                    auto actual = std::make_unique<Host<Sample>>(configured);
                    auto reference = std::make_unique<Host<Sample>>(configured);
                    if (configured) { prime(*actual, *reference); }
                    actual->restore(0);
                    reference->restore(0);
                    const auto cursor = actual->processor.engine_processor().frame_cursor();
                    const auto mechanics = actual->processor.engine_processor().engine();
                    ParameterQueue gain, sustain;
                    gain.count = 1;
                    gain.values[0] = {0, 0.3125};
                    sustain.id = kSustainId;
                    sustain.count = 1;
                    sustain.values[0] = {0, 0.5};
                    ParameterChanges changes;
                    changes.count = 2;
                    changes.values = {&gain, &sustain};
                    actual->data.inputParameterChanges = &changes;
                    reference->data.inputParameterChanges = &changes;
                    actual->data.processMode = mode;
                    actual->data.symbolicSampleSize = format;
                    if (shape == 0) { actual->data.numOutputs = 0; actual->data.outputs = nullptr; }
                    if (shape == 1) { actual->buffers.buses[0].numChannels = 0; }
                    if (shape == 1 || shape == 2) { actual->buffers.buses[0].channelBuffers32 = nullptr; }
                    // No buffers are read even when a flush format differs
                    // from both the configured format and supplied pointers.
                    reference->data.numOutputs = 0;
                    reference->data.outputs = nullptr;
                    check(invoke(actual->processor, actual->data) == sdk::kResultOk, "flush cross-product rejected");
                    check(invoke(reference->processor, reference->data) == sdk::kResultOk, "canonical control flush failed");
                    actual->buffers.unchanged();
                    check(actual->processor.engine_processor().frame_cursor() == cursor, "flush advanced audio time");
                    same_mechanics(actual->processor.engine_processor().engine(), mechanics);
                    const auto packet = saved_state(actual->processor);
                    constexpr EncodedInstrumentState expected{
                        'R', 'H', 'S', 'T', 1, 0, 1, 0, 0, 0, 0xa0, 0x3e, 0, 0, 0x80, 0x3f};
                    check(packet == expected, "flush serialized the wrong gain or sustain");
                    if (shape != 0) {
                        check(actual->buffers.buses[0].silenceFlags == (shape == 1 ? 0U : 3U),
                              "flush silence flags do not match its channel declaration");
                    }
                    compare_state(*actual, *reference);
                    if (!configured) { actual->setup(); reference->setup(); }
                    continue_pair(*actual, *reference);
                }
            }
        }
    }
}

enum class BadBuffer {
    negative_frames, setup_limit, global_limit, negative_inputs, extra_input,
    negative_outputs, missing_output, extra_output, null_output_array,
    negative_channels, zero_channels, mono_channels, extra_channel,
    null_channel_array, null_left, null_right, null_both,
    negative_mode, invalid_mode, offline_mismatch, invalid_format, format_mismatch
};
constexpr std::array bad_buffers{
    BadBuffer::negative_frames, BadBuffer::setup_limit, BadBuffer::global_limit,
    BadBuffer::negative_inputs, BadBuffer::extra_input, BadBuffer::negative_outputs,
    BadBuffer::missing_output, BadBuffer::extra_output, BadBuffer::null_output_array,
    BadBuffer::negative_channels, BadBuffer::zero_channels, BadBuffer::mono_channels,
    BadBuffer::extra_channel, BadBuffer::null_channel_array, BadBuffer::null_left,
    BadBuffer::null_right, BadBuffer::null_both, BadBuffer::negative_mode,
    BadBuffer::invalid_mode, BadBuffer::offline_mismatch, BadBuffer::invalid_format,
    BadBuffer::format_mismatch};

template <class Sample>
void reject_buffer(Host<Sample>& actual, BadBuffer scenario) {
    actual.restore();
    auto& data = actual.data;
    auto& bus = actual.buffers.buses[0];
    std::size_t cleared_frames = 16;
    unsigned cleared_mask = 3;
    sdk::uint64 expected_flags = 3;
    switch (scenario) {
    case BadBuffer::negative_frames: data.numSamples = -1; cleared_frames = 0; break;
    case BadBuffer::setup_limit: data.numSamples = 65; cleared_frames = 65; break;
    case BadBuffer::global_limit: data.numSamples = 8193; cleared_frames = 8193; break;
    case BadBuffer::negative_inputs: data.numInputs = -1; break;
    case BadBuffer::extra_input: data.numInputs = 1; break;
    case BadBuffer::negative_outputs: data.numOutputs = -1; cleared_mask = 0; expected_flags = untouched_flags; break;
    case BadBuffer::missing_output: data.numOutputs = 0; cleared_mask = 0; expected_flags = untouched_flags; break;
    case BadBuffer::extra_output: data.numOutputs = 2; break;
    case BadBuffer::null_output_array: data.outputs = nullptr; cleared_mask = 0; expected_flags = untouched_flags; break;
    case BadBuffer::negative_channels: bus.numChannels = -1; cleared_mask = 0; expected_flags = untouched_flags; break;
    case BadBuffer::zero_channels: bus.numChannels = 0; cleared_mask = 0; expected_flags = 0; break;
    case BadBuffer::mono_channels: bus.numChannels = 1; cleared_mask = 1; expected_flags = 1; break;
    case BadBuffer::extra_channel: bus.numChannels = 3; cleared_mask = 7; expected_flags = 7; break;
    case BadBuffer::null_channel_array:
        if constexpr (std::is_same_v<Sample, float>) { bus.channelBuffers32 = nullptr; }
        else { bus.channelBuffers64 = nullptr; }
        cleared_mask = 0;
        break;
    case BadBuffer::null_left: actual.buffers.channels[0] = nullptr; cleared_mask = 2; break;
    case BadBuffer::null_right: actual.buffers.channels[1] = nullptr; cleared_mask = 1; break;
    case BadBuffer::null_both: actual.buffers.channels[0] = nullptr; actual.buffers.channels[1] = nullptr; cleared_mask = 0; break;
    case BadBuffer::negative_mode: data.processMode = -1; break;
    case BadBuffer::invalid_mode: data.processMode = 99; break;
    case BadBuffer::offline_mismatch: data.processMode = actual.setup_mode == vst::kOffline ? vst::kRealtime : vst::kOffline; break;
    case BadBuffer::invalid_format: data.symbolicSampleSize = 99; cleared_mask = 0; break;
    case BadBuffer::format_mismatch: {
        using Other = std::conditional_t<std::is_same_v<Sample, float>, double, float>;
        auto alternate = std::make_unique<GuardedBuffers<Other>>();
        auto alternate_data = data;
        alternate_data.symbolicSampleSize = GuardedBuffers<Other>::sample_size;
        alternate_data.outputs = alternate->buses.data();
        check(invoke(actual.processor, alternate_data) != sdk::kResultOk, "configured sample-format mismatch accepted");
        alternate->cleared(16, 3);
        check(alternate->buses[0].silenceFlags == 3, "mismatched-format output flags wrong");
        actual.buffers.unchanged();
        return;
    }
    }
    check(invoke(actual.processor, data) != sdk::kResultOk, "malformed ProcessData accepted");
    actual.buffers.cleared(cleared_frames, cleared_mask);
    check(bus.silenceFlags == expected_flags, "rejected output has wrong silence flags");
    if (scenario == BadBuffer::extra_output) {
        check(actual.buffers.buses[1].silenceFlags == 3, "second rejected bus flags wrong");
    }
}

template <class Sample>
void rejected_buffer_cases() {
    for (sdk::int32 mode : {vst::kRealtime, vst::kOffline}) {
        for (bool pending : {false, true}) {
            for (const auto scenario : bad_buffers) {
                ++cases;
                auto actual = std::make_unique<Host<Sample>>(true, mode);
                auto reference = std::make_unique<Host<Sample>>(true, mode);
                prime(*actual, *reference);
                if (pending) { queue_recall(*actual, *reference); }
                const ProcessorSnapshot before{actual->processor};
                reject_buffer(*actual, scenario);
                before.expect_unchanged(actual->processor);
                compare_state(*actual, *reference);
                continue_pair(*actual, *reference);
            }
        }
    }
}

template <class Sample>
void setup_cases() {
    for (sdk::int32 mode : {vst::kRealtime, vst::kOffline}) {
        for (bool pending : {false, true}) {
            for (int scenario = 0; scenario < 19; ++scenario) {
                ++cases;
                auto actual = std::make_unique<Host<Sample>>(true, mode);
                auto reference = std::make_unique<Host<Sample>>(true, mode);
                prime(*actual, *reference);
                if (pending) { queue_recall(*actual, *reference); }
                const ProcessorSnapshot before{actual->processor};
                vst::ProcessSetup request{mode, GuardedBuffers<Sample>::sample_size,
                                          configured_frames, 48000.0};
                std::array<vst::SpeakerArrangement, 2> outputs{
                    vst::SpeakerArr::kStereo, vst::SpeakerArr::kStereo};
                sdk::tresult result = sdk::kResultOk;
                switch (scenario) {
                case 0: request.sampleRate = std::nextafter(32000.0, 0.0); break;
                case 1: request.sampleRate = std::nextafter(192000.0, 200000.0); break;
                case 2: request.sampleRate = std::numeric_limits<double>::quiet_NaN(); break;
                case 3: request.sampleRate = std::numeric_limits<double>::infinity(); break;
                case 4: request.maxSamplesPerBlock = -1; break;
                case 5: request.maxSamplesPerBlock = 0; break;
                case 6: request.maxSamplesPerBlock = 8193; break;
                case 7: request.symbolicSampleSize = 99; break;
                case 8: request.processMode = -1; break;
                case 9: request.processMode = 99; break;
                case 10:
                    request.sampleRate = 96000.0;
                    request.symbolicSampleSize = GuardedBuffers<Sample>::sample_size == vst::kSample32 ? vst::kSample64 : vst::kSample32;
                    request.maxSamplesPerBlock = 8193;
                    request.processMode = mode == vst::kOffline ? vst::kRealtime : vst::kOffline;
                    break;
                case 11: result = actual->processor.setBusArrangements(nullptr, -1, outputs.data(), 1); break;
                case 12: result = actual->processor.setBusArrangements(outputs.data(), 1, outputs.data(), 1); break;
                case 13: result = actual->processor.setBusArrangements(nullptr, 0, outputs.data(), -1); break;
                case 14: result = actual->processor.setBusArrangements(nullptr, 0, outputs.data(), 0); break;
                case 15: result = actual->processor.setBusArrangements(nullptr, 0, outputs.data(), 2); break;
                case 16: result = actual->processor.setBusArrangements(nullptr, 0, nullptr, 1); break;
                case 17: outputs[0] = vst::SpeakerArr::kMono; result = actual->processor.setBusArrangements(nullptr, 0, outputs.data(), 1); break;
                case 18: outputs[0] = vst::SpeakerArr::kEmpty; result = actual->processor.setBusArrangements(nullptr, 0, outputs.data(), 1); break;
                default: throw std::runtime_error("invalid setup test case");
                }
                if (scenario < 11) { result = actual->processor.setupProcessing(request); }
                check(result != sdk::kResultOk, "invalid setup or arrangement accepted");
                before.expect_unchanged(actual->processor);
                vst::SpeakerArrangement observed = vst::SpeakerArr::kEmpty;
                check(actual->processor.getBusArrangement(vst::kOutput, 0, observed) == sdk::kResultOk &&
                          observed == vst::SpeakerArr::kStereo, "rejected configuration changed stereo arrangement");
                compare_state(*actual, *reference);
                continue_pair(*actual, *reference);
                // The retained max-block setting is tested at its adjacent
                // rejection boundary, after real continuation at exactly 64.
                const ProcessorSnapshot continued{actual->processor};
                reject_buffer(*actual, BadBuffer::setup_limit);
                continued.expect_unchanged(actual->processor);
            }
        }
    }
}

void inactive_buffers() { inactive_cases<float>(); inactive_cases<double>(); }
void flush_matrix() { flush_cases<float>(); flush_cases<double>(); }
void rejected_buffers() { rejected_buffer_cases<float>(); rejected_buffer_cases<double>(); }
void setup_rollback() { setup_cases<float>(); setup_cases<double>(); }

using Fixture = std::pair<std::string_view, void (*)()>;
constexpr std::array<Fixture, 4> fixtures{{
    {"inactive_buffers", inactive_buffers}, {"flush_matrix", flush_matrix},
    {"rejected_buffers", rejected_buffers}, {"setup_rollback", setup_rollback}}};
} // namespace

int main(int argc, char** argv) {
    const std::string_view selected = argc > 1 ? argv[1] : "all";
    try {
        bool found = false;
        for (const auto& [name, run] : fixtures) {
            if (selected != "all" && selected != name) { continue; }
            found = true;
            checks = 0;
            cases = 0;
            process_calls = 0;
            run();
            std::cout << "PASS " << name << " (" << cases << " cases, " << process_calls
                      << " actual processor calls, " << checks << " explicit checks)\n";
        }
        check(found, "unknown VST3 buffer-contract selector");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << selected << ": " << error.what() << '\n';
        return 1;
    }
}
