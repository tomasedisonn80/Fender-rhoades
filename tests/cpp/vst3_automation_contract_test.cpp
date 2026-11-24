#include "tests/cpp/vst3_test_support.hpp"

#include <bit>
#include <cfenv>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace {
using namespace rhodes;
using namespace rhodes::vst3;
using namespace rhodes::vst3::test_host;

using ScalarEngine = std::remove_cvref_t<decltype(std::declval<const HostEventProcessor&>().engine())>;
#if defined(RHODES_QUADRATURE_VST3)
static_assert(kQuadratureAudio && !kFilteredAudio && kStateModel == 3);
static_assert(std::is_same_v<ScalarEngine, QuadratureEngine>);
#elif defined(RHODES_FILTERED_VST3)
static_assert(kStateModel == 2);
#else
static_assert(kStateModel == 1);
#endif

std::size_t checks = 0;
std::size_t cases = 0;
std::size_t calls = 0;
std::size_t samples = 0;

void check(bool condition, std::string_view message) {
    ++checks;
    if (!condition) { throw std::runtime_error(std::string(message)); }
}

// This suite runs under native round-to-nearest with ordinary normal controls.
// Native casts are independent of the production integer conversion helpers.
float anchor(double value) {
    return value == 0.0 ? 0.0F : static_cast<float>(value);
}

using Points = std::map<sdk::int32, long double>;

long double curve(const Points& points, float initial, sdk::int32 frame) {
    if (points.empty()) { return initial; }
    const auto right = points.lower_bound(frame);
    if (right == points.end()) { return points.rbegin()->second; }
    if (right->first == frame) { return right->second; }
    const auto left = right == points.begin() ?
        std::pair<sdk::int32, long double>{-1, initial} :
        std::pair<sdk::int32, long double>{std::prev(right)->first, std::prev(right)->second};
    // Barycentric evaluation, not the processor's increment/fraction formula.
    const auto left_weight = static_cast<long double>(right->first - frame);
    const auto right_weight = static_cast<long double>(frame - left.first);
    return (left.second * left_weight + right->second * right_weight) /
           static_cast<long double>(right->first - left.first);
}

struct ScalarModel {
    ScalarEngine engine;
    float gain = 0.5F;
    bool sustain = false;
    std::uint64_t cursor = 0;
    std::map<std::pair<sdk::int16, sdk::int32>, sdk::int16> owners;

    explicit ScalarModel(double rate) { check(engine.prepare(rate), "reference prepare"); }

    void recall(InstrumentState state) {
        engine.reset();
        owners.clear();
        gain = state.monitoring_gain;
        sustain = state.sustain;
        cursor = 0;
        engine.set_sustain(sustain);
    }

    void note(const vst::Event& event) {
        const bool on_type = event.type == vst::Event::kNoteOnEvent;
        const auto channel = on_type ? event.noteOn.channel : event.noteOff.channel;
        const auto id = on_type ? event.noteOn.noteId : event.noteOff.noteId;
        const auto pitch = on_type ? event.noteOn.pitch : event.noteOff.pitch;
        const auto identity = std::pair{channel, id};
        if (on_type && event.noteOn.velocity != 0.0F) {
            const auto found = owners.find(identity);
            check(found == owners.end() || found->second == pitch, "invalid reference identity");
            owners[identity] = pitch;
            check(engine.note_on(static_cast<std::size_t>(pitch - 28),
                                 static_cast<double>(event.noteOn.velocity)), "reference note-on");
        } else {
            const auto found = owners.find(identity);
            if (found == owners.end()) { return; }
            check(found->second == pitch, "reference release pitch");
            owners.erase(found);
            const bool another = std::any_of(owners.begin(), owners.end(),
                [pitch](const auto& owner) { return owner.second == pitch; });
            if (!another) {
                check(engine.note_off(static_cast<std::size_t>(pitch - 28)), "reference note-off");
            }
        }
    }
};

template<class Sample>
struct Fixture {
    ProcessorHost<Sample, 128> host;
    ParameterQueue gain;
    ParameterQueue pedal;
    ParameterChanges parameters;
    EventList events;
    ScalarModel model;

    explicit Fixture(double rate) : host(rate), model(rate) {
        // Preserve the existing two-control scalar oracle at Expression unity.
        check(host.processor.current_state().expression == 1.0F,
              "legacy automation fixture requires unity expression");
        pedal.id = kSustainId;
        wire(false);
        host.data.inputEvents = &events;
    }

    void wire(bool reverse) {
        parameters.count = 2;
        parameters.values = reverse ? std::array<vst::IParamValueQueue*, 2>{&pedal, &gain} :
                                      std::array<vst::IParamValueQueue*, 2>{&gain, &pedal};
        host.data.inputParameterChanges = &parameters;
    }

    void clear() {
        gain.count = 0;
        pedal.count = 0;
        events.count = 0;
        gain.failed_index = -1;
        pedal.failed_index = -1;
        wire(false);
        host.data.inputEvents = &events;
    }
};

template<class Sample>
void accepted(Fixture<Sample>& fixture, sdk::int32 frames) {
    ++cases;
    auto& host = fixture.host;
    auto& model = fixture.model;
    host.data.numSamples = frames;
    const auto initial_gain = model.gain;
    Points gains, pedals;
    std::size_t expected_gain_reads = 0;
    std::size_t expected_pedal_reads = 0;
    if (host.data.inputParameterChanges != nullptr) {
        for (sdk::int32 queue = 0; queue < fixture.parameters.count; ++queue) {
            const auto* pointer = fixture.parameters.values[static_cast<std::size_t>(queue)];
            const auto& input = pointer == &fixture.gain ? fixture.gain : fixture.pedal;
            check(pointer == &fixture.gain || pointer == &fixture.pedal, "unknown reference queue");
            auto& points = input.id == kGainId ? gains : pedals;
            for (sdk::int32 index = 0; index < input.count; ++index) {
                const auto& point = input.values[static_cast<std::size_t>(index)];
                points[point.offset] = input.id == kGainId ? anchor(point.value) :
                                      (point.value >= 0.5 ? 1.0L : 0.0L);
            }
            (input.id == kGainId ? expected_gain_reads : expected_pedal_reads) =
                static_cast<std::size_t>(input.count);
        }
    }
    const auto old_gain_reads = fixture.gain.get_calls;
    const auto old_pedal_reads = fixture.pedal.get_calls;
    const auto old_note_reads = fixture.events.get_calls;
    const auto note_count = host.data.inputEvents == nullptr ? 0 : fixture.events.count;
    host.fill(Sample{91.25});
    ++calls;
    check(host.processor.process(host.data) == sdk::kResultOk, "accepted automation rejected");
    check(fixture.gain.get_calls - old_gain_reads == expected_gain_reads, "gain queue truncated");
    check(fixture.pedal.get_calls - old_pedal_reads == expected_pedal_reads, "pedal queue truncated");
    check(fixture.events.get_calls - old_note_reads == static_cast<std::size_t>(note_count),
          "note list truncated");
    bool nonzero = false;
    if (frames == 0) {
        if (!gains.empty()) { model.gain = static_cast<float>(gains.rbegin()->second); }
        if (!pedals.empty()) {
            model.sustain = pedals.rbegin()->second != 0.0L;
            model.engine.set_sustain(model.sustain);
        }
    }
    for (sdk::int32 frame = 0; frame < frames; ++frame) {
        const auto pedal = pedals.find(frame);
        if (pedal != pedals.end()) {
            model.sustain = pedal->second != 0.0L;
            model.engine.set_sustain(model.sustain);
        }
        // Parameters precede same-sample notes; note list order stays intact.
        for (sdk::int32 index = 0; index < note_count; ++index) {
            const auto& event = fixture.events.values[static_cast<std::size_t>(index)];
            if (event.sampleOffset == frame) { model.note(event); }
        }
        const auto normalized = curve(gains, initial_gain, frame);
        double volts = 0.0;
        check(model.engine.process(std::span<double>{&volts, 1}), "reference sample failed");
        // Monitoring gain scales this host-rate output after any FIR. Notes and
        // controls keep their original host offsets, with no latency shift.
        const auto expected = static_cast<Sample>(32.0L * normalized * static_cast<long double>(volts));
        const auto at = static_cast<std::size_t>(frame);
        const double observed = static_cast<double>(host.left[at]);
        const double wanted = static_cast<double>(expected);
        const double multiplier = std::is_same_v<Sample, float> ? 4.0 : 64.0;
        const double tolerance = multiplier * std::numeric_limits<Sample>::epsilon() *
                                 std::max(1e-8, std::abs(wanted));
        check(std::isfinite(observed) && std::abs(observed - wanted) <= tolerance,
              "output differs from independent barycentric control model");
        check(host.left[at] == host.right[at], "automation stereo mismatch");
        if (normalized == 0.0L) { check(host.left[at] == Sample{0}, "zero gain emitted audio"); }
        nonzero = nonzero || host.left[at] != Sample{0};
        model.gain = anchor(static_cast<double>(normalized));
        ++samples;
    }
    model.cursor += static_cast<std::uint64_t>(frames);
    for (std::size_t frame = static_cast<std::size_t>(frames); frame < host.left.size(); ++frame) {
        check(host.left[frame] == Sample{91.25} && host.right[frame] == Sample{91.25},
              "write outside declared frame count");
    }
    check(host.output.silenceFlags == (nonzero ? 0U : 3U), "automation silence flag mismatch");
    check(host.processor.engine_processor().frame_cursor() == model.cursor, "automation cursor mismatch");
    check(std::bit_cast<std::uint32_t>(host.processor.current_state().monitoring_gain) ==
          std::bit_cast<std::uint32_t>(model.gain), "stored boundary gain differs");
    check(host.processor.current_state().sustain == model.sustain, "stored sustain differs");
    check(host.processor.published_state() == host.processor.current_state(), "configuration unpublished");
    check(host.processor.note_tracker().active_count() == model.owners.size(), "owner count differs");
    same_mechanics(host.processor.engine_processor().engine(), model.engine);
}

std::uint32_t random_word(std::uint32_t& state) {
    state ^= state << 13U;
    state ^= state >> 17U;
    state ^= state << 5U;
    return state;
}

template<class Sample>
void model_at_rate(double rate) {
    for (std::uint32_t seed : {7U, 12345U, 0x6b39f1e5U}) {
        auto fixture = std::make_unique<Fixture<Sample>>(rate);
        auto state = seed;
        for (std::size_t block = 0; block < 32; ++block) {
            fixture->clear();
            fixture->wire(block % 2 == 1);
            const auto frames = static_cast<sdk::int32>(1U + random_word(state) % 97U);
            std::vector<sdk::int32> offsets;
            const auto point_count = random_word(state) % 13U;
            for (std::uint32_t index = 0; index < point_count; ++index) {
                offsets.push_back(static_cast<sdk::int32>(random_word(state) % static_cast<std::uint32_t>(frames)));
            }
            if (block % 3 == 0) { offsets.push_back(frames - 1); }
            if (block % 5 == 0) { offsets.push_back(0); }
            std::sort(offsets.begin(), offsets.end());
            for (const auto offset : offsets) {
                const double value = static_cast<double>(random_word(state) % 1000001U) / 1000000.0;
                fixture->gain.values[static_cast<std::size_t>(fixture->gain.count++)] = {offset, value};
                if ((random_word(state) & 3U) == 0U) {
                    fixture->gain.values[static_cast<std::size_t>(fixture->gain.count++)] =
                        {offset, static_cast<double>(random_word(state) % 8193U) / 8192.0};
                }
            }
            fixture->pedal.count = 2;
            fixture->pedal.values[0] = {0, block % 2 == 0 ? 0.5 : 0.499999};
            fixture->pedal.values[1] = {frames - 1, block % 3 == 0 ? 1.0 : 0.0};
            fixture->events.count = 1;
            fixture->events.values[0] = block % 4 == 3 ? note_off(0, 7) :
                note_on(0, 7, 68, block % 2 == 0 ? 0.25F : 0.75F);
            accepted(*fixture, frames);
        }
    }
}

void automation_model() {
    for (double rate : {32000.0, 48000.0, 96000.0, 192000.0}) {
        model_at_rate<float>(rate);
        model_at_rate<double>(rate);
    }
}

template<class Sample>
void ties_at_rate(double rate) {
    for (bool down : {false, true}) {
        for (unsigned order = 0; order < 4; ++order) {
            std::vector<Sample> first_order;
            for (bool reverse : {false, true}) {
                auto fixture = std::make_unique<Fixture<Sample>>(rate);
                fixture->events.count = 1;
                fixture->events.values[0] = note_on(0, 7);
                accepted(*fixture, 17);
                fixture->clear();
                fixture->wire(reverse);
                fixture->gain.count = 4;
                fixture->gain.values[0] = {0, 0.99};
                fixture->gain.values[1] = {0, 0.187500001};
                fixture->gain.values[2] = {5, 0.01};
                fixture->gain.values[3] = {5, 0.812499999};
                fixture->pedal.count = 3;
                fixture->pedal.values[0] = {0, down ? 0.0 : 1.0};
                fixture->pedal.values[1] = {0, down ? 0.5 : 0.499999};
                fixture->pedal.values[2] = {5, down ? 1.0 : 0.0};
                fixture->events.count = 3;
                if ((order & 1U) == 0U) {
                    fixture->events.values[0] = note_off(0, 7);
                    fixture->events.values[1] = note_on(0, 7, 68, 0.25F);
                } else {
                    fixture->events.values[0] = note_on(0, 7, 68, 0.25F);
                    fixture->events.values[1] = note_off(0, 7);
                }
                fixture->events.values[2] = (order & 2U) == 0U ? note_on(5, 8, 68, 0.5F, 3) :
                                                                          note_off(5, 7);
                accepted(*fixture, 9);
                std::vector<Sample> output(fixture->host.left.begin(), fixture->host.left.begin() + 9);
                fixture->clear();
                fixture->pedal.count = 1;
                fixture->pedal.values[0] = {0, 0.0};
                accepted(*fixture, 23);
                output.insert(output.end(), fixture->host.left.begin(), fixture->host.left.begin() + 23);
                if (!reverse) { first_order = output; }
                else { check(output == first_order, "cross-stream queue order changed audio"); }
            }
        }
    }
}

void automation_ties() {
    for (double rate : {32000.0, 48000.0, 96000.0, 192000.0}) {
        ties_at_rate<float>(rate);
        ties_at_rate<double>(rate);
    }
}

template<class Sample>
void flush_at_rate(double rate) {
    for (unsigned shape = 0; shape < 6; ++shape) {
        for (bool reverse : {false, true}) {
            auto fixture = std::make_unique<Fixture<Sample>>(rate);
            fixture->events.count = 1;
            fixture->events.values[0] = note_on(0, 7);
            accepted(*fixture, 13);
            fixture->clear();
            fixture->wire(reverse);
            fixture->gain.count = 3;
            fixture->gain.values[0] = {0, 0.75};
            fixture->gain.values[1] = {0, 0.25};
            fixture->gain.values[2] = {0, -0.0};
            fixture->pedal.count = 3;
            fixture->pedal.values[0] = {0, 0.0};
            fixture->pedal.values[1] = {0, 0.49};
            fixture->pedal.values[2] = {0, 0.5};
            switch (shape) {
            case 0: fixture->host.data.inputParameterChanges = nullptr; break;
            case 1: fixture->parameters.count = 0; break;
            case 2: fixture->gain.count = 0; fixture->pedal.count = 0; break;
            case 3: fixture->pedal.count = 0; break;
            case 4: fixture->gain.count = 0; break;
            default: break;
            }
            accepted(*fixture, 0);
            accepted(*fixture, 0); // Repeated empty/duplicate flush cannot advance time.
            fixture->clear();
            fixture->gain.count = 1;
            fixture->gain.values[0] = {31, 0.333333333};
            fixture->events.count = 1;
            fixture->events.values[0] = note_off(0, 7);
            accepted(*fixture, 32);
            fixture->clear();
            accepted(*fixture, 1); // Empty queue holds the last exact endpoint.
            fixture->gain.count = 1;
            fixture->gain.values[0] = {0, 0.75000001};
            accepted(*fixture, 1); // Offset-zero anchor takes effect directly.
        }
    }
}

void automation_flush() {
    for (double rate : {32000.0, 48000.0, 96000.0, 192000.0}) {
        flush_at_rate<float>(rate);
        flush_at_rate<double>(rate);
    }
}

template<class Sample>
void rejection_at_rate(double rate) {
    for (bool reverse : {false, true}) {
        for (bool pending : {false, true}) {
            for (unsigned fault = 0; fault < 8; ++fault) {
                auto fixture = std::make_unique<Fixture<Sample>>(rate);
                fixture->events.count = 1;
                fixture->events.values[0] = note_on(0, 7);
                accepted(*fixture, 19);
                fixture->clear();
                const InstrumentState recalled{0.625F, true};
                if (pending) {
                    MemoryStream stream;
                    stream.load(recalled, 0, kStateModel);
                    check(fixture->host.processor.setState(&stream) == sdk::kResultOk, "pending recall rejected");
                }
                auto before = std::make_unique<ProcessorSnapshot>(fixture->host.processor);
                fixture->wire(reverse);
                fixture->gain.count = 5;
                for (sdk::int32 index = 0; index < 5; ++index) {
                    fixture->gain.values[static_cast<std::size_t>(index)] = {index * 7, 0.1 + 0.15 * index};
                }
                fixture->pedal.count = 4;
                for (sdk::int32 index = 0; index < 4; ++index) {
                    fixture->pedal.values[static_cast<std::size_t>(index)] = {index * 9, index % 2 == 0 ? 1.0 : 0.0};
                }
                fixture->events.count = 2;
                fixture->events.values[0] = note_on(2, 8, 60, 0.5F);
                fixture->events.values[1] = note_off(29, 7);
                switch (fault) {
                case 0: fixture->gain.failed_index = 4; break;
                case 1: fixture->pedal.failed_index = 3; break;
                case 2: fixture->gain.values[4].value = std::numeric_limits<double>::quiet_NaN(); break;
                case 3: fixture->pedal.values[3].value = -0.01; break;
                case 4: fixture->gain.values[4].offset = 20; break;
                case 5: fixture->pedal.values[3].offset = 32; break;
                case 6:
                    fixture->gain.values[3] = {28, std::numeric_limits<double>::infinity()};
                    fixture->gain.values[4] = {28, 0.5};
                    break;
                default:
                    fixture->gain.failed_index = 3;
                    fixture->gain.values[3] = {28, 0.25};
                    fixture->gain.values[4] = {28, 0.5};
                    break;
                }
                fixture->host.data.numSamples = 32;
                fixture->host.fill(Sample{91.25});
                ++cases;
                ++calls;
                check(fixture->host.processor.process(fixture->host.data) != sdk::kResultOk,
                      "late malformed queue accepted");
                before->expect_unchanged(fixture->host.processor);
                fixture->host.expect_silence(32);
                for (std::size_t index = 32; index < fixture->host.left.size(); ++index) {
                    check(fixture->host.left[index] == Sample{91.25} && fixture->host.right[index] == Sample{91.25},
                          "rejection cleared beyond declared buffer");
                }
                fixture->clear();
                if (pending) { fixture->model.recall(recalled); }
                fixture->gain.count = 2;
                fixture->gain.values[0] = {15, 0.375};
                fixture->gain.values[1] = {31, 0.75};
                fixture->events.count = 1;
                fixture->events.values[0] = note_on(0, 9, 72, 0.5F);
                accepted(*fixture, 32);
                fixture->clear();
                accepted(*fixture, 17);
            }
        }
    }
}

void automation_rejection() {
    for (double rate : {32000.0, 48000.0, 96000.0, 192000.0}) {
        rejection_at_rate<float>(rate);
        rejection_at_rate<double>(rate);
    }
}
} // namespace

int main(int argc, char** argv) {
    const std::string_view selected = argc == 2 ? argv[1] : "all";
    try {
        check(argc <= 2, "too many arguments");
        check(std::fegetround() == FE_TONEAREST, "suite requires native nearest rounding");
        const std::array<std::pair<std::string_view, void(*)()>, 4> entries{{
            {"automation_model", automation_model}, {"automation_ties", automation_ties},
            {"automation_flush", automation_flush}, {"automation_rejection", automation_rejection}}};
        bool found = false;
        for (const auto& entry : entries) {
            if (selected != "all" && selected != entry.first) { continue; }
            found = true;
            checks = cases = calls = samples = 0;
            entry.second();
            std::cout << "PASS " << entry.first << ": " << cases << " cases, " << calls
                      << " process calls, " << samples << " sample comparisons, " << checks << " explicit checks\n";
        }
        check(found, "unknown selector");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << selected << ": " << error.what() << '\n';
        return 1;
    }
}
