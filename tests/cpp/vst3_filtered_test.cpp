#include "tests/cpp/vst3_test_support.hpp"

#include <cmath>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <utility>

#if !defined(RHODES_FILTERED_VST3)
#error This integration suite must compile every adapter source with RHODES_FILTERED_VST3=1
#endif

namespace {
using namespace rhodes;
using namespace rhodes::vst3;
using namespace rhodes::vst3::test_host;
static_assert(kFilteredAudio && kStateModel == 2);
static_assert(std::is_same_v<HostEventProcessor, BasicEventProcessor<AudioEngine>>);

std::size_t rendered_samples = 0;

// The adapter oracle calls AudioEngine directly. It does not reuse the adapter's
// event dispatcher, NoteTracker, interpolation implementation or conversion API.
// FIR implementation correctness has its own direct-convolution core tests.
struct ScalarModel {
    AudioEngine engine;
    InstrumentState state{};
    std::uint64_t cursor = 0;
    std::map<std::pair<sdk::int16, sdk::int32>, sdk::int16> owners;

    explicit ScalarModel(double rate) { require(engine.prepare(rate), "oracle prepare"); }
    void recall(InstrumentState recalled) {
        engine.reset();
        owners.clear();
        state = recalled;
        cursor = 0;
        engine.set_sustain(state.sustain);
    }
    void note(const vst::Event& event) {
        const bool on = event.type == vst::Event::kNoteOnEvent;
        const auto pitch = on ? event.noteOn.pitch : event.noteOff.pitch;
        const auto identity = std::pair{on ? event.noteOn.channel : event.noteOff.channel,
                                       on ? event.noteOn.noteId : event.noteOff.noteId};
        if (on && event.noteOn.velocity != 0.0F) {
            owners[identity] = pitch;
            require(engine.note_on(static_cast<std::size_t>(pitch - 28),
                                   static_cast<double>(event.noteOn.velocity)), "oracle strike");
        } else {
            const auto found = owners.find(identity);
            if (found == owners.end()) { return; }
            require(found->second == pitch, "oracle ownership fixture");
            owners.erase(found);
            if (std::none_of(owners.begin(), owners.end(),
                    [pitch](const auto& owner) { return owner.second == pitch; })) {
                require(engine.note_off(static_cast<std::size_t>(pitch - 28)), "oracle release");
            }
        }
    }
};

using Points = std::map<sdk::int32, long double>;

long double gain_at(const Points& points, float initial, sdk::int32 frame) {
    if (points.empty()) { return initial; }
    const auto right = points.lower_bound(frame);
    if (right == points.end()) { return points.rbegin()->second; }
    if (right->first == frame) { return right->second; }
    const auto left = right == points.begin() ?
        std::pair<sdk::int32, long double>{-1, initial} :
        std::pair<sdk::int32, long double>{std::prev(right)->first, std::prev(right)->second};
    return (left.second * static_cast<long double>(right->first - frame) +
            right->second * static_cast<long double>(frame - left.first)) /
           static_cast<long double>(right->first - left.first);
}

template<class Sample>
struct Fixture {
    ProcessorHost<Sample, 128> host;
    ScalarModel model;
    ParameterQueue gain, pedal;
    ParameterChanges parameters;
    EventList events;

    explicit Fixture(double rate = 48000.0) : host(rate), model(rate) {
        pedal.id = kSustainId;
        parameters.count = 2;
        parameters.values = {&pedal, &gain};
        host.data.inputParameterChanges = &parameters;
        host.data.inputEvents = &events;
    }
    void clear() {
        gain.count = pedal.count = events.count = 0;
        gain.failed_index = pedal.failed_index = events.failed_index = -1;
    }
    void accepted(sdk::int32 frames) {
        Points gains, pedals;
        for (sdk::int32 i = 0; i < gain.count; ++i) {
            const auto& point = gain.values[static_cast<std::size_t>(i)];
            gains[point.offset] = static_cast<float>(point.value);
        }
        for (sdk::int32 i = 0; i < pedal.count; ++i) {
            const auto& point = pedal.values[static_cast<std::size_t>(i)];
            pedals[point.offset] = point.value >= 0.5 ? 1.0L : 0.0L;
        }
        const auto initial_gain = model.state.monitoring_gain;
        host.data.numSamples = frames;
        host.fill(Sample{91.25});
        require(host.processor.process(host.data) == sdk::kResultOk, "valid filtered process rejected");
        bool nonzero = false;
        if (frames == 0) {
            if (!gains.empty()) { model.state.monitoring_gain = static_cast<float>(gains.rbegin()->second); }
            if (!pedals.empty()) {
                model.state.sustain = pedals.rbegin()->second != 0;
                model.engine.set_sustain(model.state.sustain);
            }
        }
        for (sdk::int32 frame = 0; frame < frames; ++frame) {
            const auto pedal_point = pedals.find(frame);
            if (pedal_point != pedals.end()) {
                model.state.sustain = pedal_point->second != 0;
                model.engine.set_sustain(model.state.sustain);
            }
            for (sdk::int32 i = 0; i < events.count; ++i) {
                const auto& event = events.values[static_cast<std::size_t>(i)];
                if (event.sampleOffset == frame) { model.note(event); }
            }
            double volts = 0;
            require(model.engine.process(std::span<double>(&volts, 1)), "oracle process failed");
            const auto normalized = gain_at(gains, initial_gain, frame);
            const auto expected = static_cast<Sample>(32.0L * normalized * volts);
            const auto at = static_cast<std::size_t>(frame);
            const double tolerance = (std::is_same_v<Sample, float> ? 4.0 : 64.0) *
                std::numeric_limits<Sample>::epsilon() * std::max(1e-8, std::abs(static_cast<double>(expected)));
            require(std::isfinite(host.left[at]) &&
                    std::abs(static_cast<double>(host.left[at]) - expected) <= tolerance,
                    "filtered output differs from scalar event/control oracle");
            require(host.left[at] == host.right[at], "filtered stereo differs");
            if (normalized == 0) { require(host.left[at] == Sample{0}, "zero gain is not silent"); }
            nonzero = nonzero || host.left[at] != Sample{0};
            model.state.monitoring_gain = static_cast<float>(normalized);
            ++rendered_samples;
        }
        model.cursor += static_cast<std::uint64_t>(frames);
        for (std::size_t at = static_cast<std::size_t>(frames); at < host.left.size(); ++at) {
            require(host.left[at] == Sample{91.25} && host.right[at] == Sample{91.25},
                    "filtered process wrote outside declared frames");
        }
        require(host.output.silenceFlags == (nonzero ? 0U : 3U), "filtered silence flags");
        require(host.processor.current_state() == model.state, "filtered live controls differ");
        require(host.processor.published_state() == model.state, "filtered published controls differ");
        require(host.processor.engine_processor().frame_cursor() == model.cursor, "filtered cursor differs");
        require(host.processor.note_tracker().active_count() == model.owners.size(), "filtered ownership differs");
        same_mechanics(host.processor.engine_processor().engine(), model.engine);
    }
    void warm() {
        events.count = 1;
        events.values[0] = note_on(2, 17);
        accepted(23);
        clear();
        require(host.processor.engine_processor().engine().mechanical_energy_joules() > 0,
                "history fixture did not excite mechanics");
    }
};

template<class Sample>
void scalar_at_rate(double rate) {
    auto fixture = std::make_unique<Fixture<Sample>>(rate);
    fixture->gain.count = 4;
    fixture->gain.values[0] = {11, 0.9};
    fixture->gain.values[1] = {11, 0.25}; // duplicate endpoint: last wins
    fixture->gain.values[2] = {63, 0.75};
    fixture->gain.values[3] = {127, 0.125};
    fixture->pedal.count = 2;
    fixture->pedal.values[0] = {37, 0.5};
    fixture->pedal.values[1] = {111, 0.499999};
    fixture->events.count = 8;
    fixture->events.values[0] = note_on(7, 1, 68, 0.75F);
    fixture->events.values[1] = note_on(31, 2, 60, 0.5F);
    fixture->events.values[2] = note_off(37, 1);
    fixture->events.values[3] = note_on(51, 3, 68, 0.375F);
    fixture->events.values[4] = note_on(77, 4, 68, 0.625F, 2);
    fixture->events.values[5] = note_off(88, 3);
    fixture->events.values[6] = note_off(111, 4, 68, 2);
    fixture->events.values[7] = note_off(123, 2, 60);
    fixture->accepted(128);
    fixture->clear();
    fixture->gain.count = 2;
    fixture->gain.values[0] = {0, 0.0};
    fixture->gain.values[1] = {88, 0.333333333};
    fixture->events.count = 2;
    fixture->events.values[0] = note_on(3, 5, 81, 0.25F);
    fixture->events.values[1] = note_off(80, 5, 81);
    fixture->accepted(89);
    fixture->clear();
    fixture->accepted(128);
}

void scalar_output() {
    for (double rate : {32000.0, 48000.0, 88200.0, 96000.0, 192000.0}) {
        scalar_at_rate<float>(rate);
        scalar_at_rate<double>(rate);
    }
}

void rate_contract() {
    RhodesProcessor unconfigured;
    require(unconfigured.getLatencySamples() == 48, "latency before setup");
    for (const auto& [rate, factor] : std::array<std::pair<double, std::size_t>, 8>{{
             {32000, 4}, {44100, 4}, {48000, 4}, {std::nextafter(48000.0, 49000.0), 2},
             {88200, 2}, {96000, 2}, {std::nextafter(96000.0, 97000.0), 1}, {192000, 1}}}) {
        auto host = std::make_unique<ProcessorHost<double>>(rate);
        const auto& engine = host->processor.engine_processor().engine();
        require(host->processor.getLatencySamples() == 48 && engine.latency_frames() == 48,
                "filtered reported latency is not 48");
        require(engine.sample_rate_hz() == rate && engine.oversampling_factor() == factor &&
                engine.internal_sample_rate_hz() == rate * static_cast<double>(factor),
                "filtered host/internal rate contract");
    }
}

void state_contract() {
    for (bool pending : {false, true}) {
        auto fixture = std::make_unique<Fixture<double>>();
        fixture->warm();
        const InstrumentState recalled{0.625F, true};
        MemoryStream good;
        good.load(recalled, 3, 2);
        good.transfer_limit = 1;
        if (pending) {
            require(fixture->host.processor.setState(&good) == sdk::kResultOk, "model-2 processor recall");
        }
        ProcessorSnapshot before(fixture->host.processor);
        MemoryStream wrong;
        wrong.load({0.125F, false}, 0, 1);
        require(fixture->host.processor.setState(&wrong) != sdk::kResultOk,
                "filtered processor accepted direct model-1 state");
        before.expect_unchanged(fixture->host.processor);
        MemoryStream saved;
        saved.transfer_limit = 1;
        require(fixture->host.processor.getState(&saved) == sdk::kResultOk, "filtered processor save");
        require(saved.extent == 16 && saved.bytes[6] == 2 && saved.bytes[7] == 0,
                "filtered processor did not serialize model tag 2");
        saved.position = 0;
        const auto expected = pending ? recalled : fixture->model.state;
        require(read_state_stream(&saved, 2) == expected, "model-2 processor roundtrip");
        saved.position = 0;
        require(!read_state_stream(&saved, 1), "direct reader accepted filtered state");

        ControllerHost controller;
        saved.position = 0;
        require(controller.controller.setComponentState(&saved) == sdk::kResultOk,
                "filtered controller component-state recall");
        MemoryStream controller_input;
        controller_input.load({0.375F, true}, 0, 2);
        require(controller.controller.setState(&controller_input) == sdk::kResultOk,
                "filtered controller own-state recall");
        for (bool component : {false, true}) {
            wrong.position = 0;
            require((component ? controller.controller.setComponentState(&wrong) :
                                 controller.controller.setState(&wrong)) != sdk::kResultOk,
                    "filtered controller accepted model-1 state");
            require(controller.controller.getParamNormalized(kGainId) == 0.375 &&
                    controller.controller.getParamNormalized(kSustainId) == 1.0,
                    "cross-model state changed controller controls");
        }
        MemoryStream controller_saved;
        controller_saved.transfer_limit = 1;
        require(controller.controller.getState(&controller_saved) == sdk::kResultOk,
                "filtered controller save");
        require(controller_saved.bytes[6] == 2 && controller_saved.bytes[7] == 0,
                "filtered controller saved wrong model");
        controller_saved.position = 0;
        require(read_state_stream(&controller_saved, 2) == InstrumentState{0.375F, true},
                "filtered controller model-2 roundtrip");

        if (pending) { fixture->model.recall(recalled); }
        fixture->events.count = 1;
        fixture->events.values[0] = note_on(3, 22, 77, 0.5F);
        fixture->accepted(128); // A pending valid recall survives cross-model rejection.
        fixture->clear();
        fixture->accepted(128); // Includes the entire surviving FIR history.
    }
}

template<class Sample>
void reset_at_rate(double rate) {
    for (unsigned action = 0; action < 4; ++action) {
        auto fixture = std::make_unique<Fixture<Sample>>(rate);
        fixture->warm();
        auto& processor = fixture->host.processor;
        const InstrumentState state = fixture->model.state;
        if (action == 0) { require(processor.setProcessing(false) == sdk::kResultOk, "stop reset"); }
        if (action == 1) { require(processor.setActive(false) == sdk::kResultOk, "inactive reset"); }
        if (action == 2) {
            vst::ProcessSetup setup{vst::kRealtime, ProcessorHost<Sample>::sample_size, 128, rate};
            require(processor.setupProcessing(setup) == sdk::kResultOk, "setup reset");
        }
        if (action == 3) {
            MemoryStream recalled;
            recalled.load(state, 0, 2);
            require(processor.setState(&recalled) == sdk::kResultOk, "recall reset setup");
        }
        fixture->model.recall(state);
        fixture->accepted(0); // Recalls apply on flush, without consuming FIR samples.
        fixture->accepted(128);
        fixture->host.expect_silence(128); // Any retained FIR tail would violate this.
        fixture->events.count = 1;
        fixture->events.values[0] = note_on(0, 8, 64, 0.5F);
        fixture->accepted(128); // Reset phase matches a freshly prepared engine.
    }
    auto fixture = std::make_unique<Fixture<Sample>>(rate);
    fixture->warm();
    ProcessorSnapshot before(fixture->host.processor);
    fixture->accepted(0);
    fixture->accepted(0);
    before.expect_unchanged(fixture->host.processor);
    fixture->gain.count = fixture->pedal.count = 1;
    fixture->gain.values[0] = {0, 0.75};
    fixture->pedal.values[0] = {0, 1.0};
    fixture->accepted(0);
    fixture->clear();
    fixture->events.count = 1;
    fixture->events.values[0] = note_off(0, 17);
    fixture->accepted(128); // Flush control changes occur without advancing history.
}

void reset_and_flush() {
    for (double rate : {48000.0, 96000.0, 192000.0}) {
        reset_at_rate<float>(rate);
        reset_at_rate<double>(rate);
    }
}

template<class Sample>
void rejection_at_rate(double rate) {
    for (unsigned fault = 0; fault < 8; ++fault) {
        auto fixture = std::make_unique<Fixture<Sample>>(rate);
        fixture->warm();
        auto& host = fixture->host;
        ProcessorSnapshot before(host.processor);
        if (fault < 4) {
            vst::ProcessSetup setup{vst::kRealtime, ProcessorHost<Sample>::sample_size, 128, rate};
            if (fault == 0) { setup.sampleRate = 31999; }
            if (fault == 1) { setup.sampleRate = std::numeric_limits<double>::quiet_NaN(); }
            if (fault == 2) { setup.maxSamplesPerBlock = 0; }
            if (fault == 3) { setup.symbolicSampleSize = 42; }
            require(host.processor.setupProcessing(setup) != sdk::kResultOk, "invalid setup accepted");
        } else {
            fixture->gain.count = 2;
            fixture->gain.values[0] = {0, 0.25};
            fixture->gain.values[1] = {31, 0.75};
            fixture->events.count = 1;
            fixture->events.values[0] = note_on(2, 30, 72, 0.5F);
            if (fault == 4) { fixture->gain.values[1].value = std::numeric_limits<double>::quiet_NaN(); }
            if (fault == 5) { fixture->events.values[0].sampleOffset = 32; }
            if (fault == 6) { fixture->events.failed_index = 0; }
            if (fault == 7) { host.data.numInputs = 1; }
            host.data.numSamples = 32;
            host.fill(Sample{91.25});
            require(host.processor.process(host.data) != sdk::kResultOk, "invalid supported block accepted");
            host.expect_silence(32);
            require(host.left[32] == Sample{91.25} && host.right[32] == Sample{91.25},
                    "rejection silenced beyond declared block");
        }
        before.expect_unchanged(host.processor);
        fixture->clear();
        host.data.numInputs = 0;
        fixture->accepted(128);
        fixture->accepted(128); // Check far more than the 96-frame FIR support.
    }
}

void rejected_calls() {
    for (double rate : {48000.0, 96000.0, 192000.0}) {
        rejection_at_rate<float>(rate);
        rejection_at_rate<double>(rate);
    }
}

using Selector = std::pair<std::string_view, void (*)()>;
constexpr std::array<Selector, 5> selectors{{
    {"scalar_output", scalar_output}, {"rate_contract", rate_contract},
    {"state_contract", state_contract}, {"reset_and_flush", reset_and_flush},
    {"rejected_calls", rejected_calls}
}};
} // namespace

int main(int argc, char** argv) {
    if (argc != 2) { std::cerr << "Usage: rhodes_vst3_filtered_tests <selector>\n"; return 2; }
    for (const auto& [name, run] : selectors) {
        if (name != argv[1]) { continue; }
        try {
            run();
            std::cout << "PASS " << name << " (" << rendered_samples << " scalar-checked samples)\n";
            return 0;
        } catch (const std::exception& error) {
            std::cerr << "FAIL " << name << ": " << error.what() << '\n';
            return 1;
        }
    }
    std::cerr << "Unknown filtered selector: " << argv[1] << '\n';
    return 2;
}
