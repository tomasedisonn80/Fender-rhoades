#include "tests/cpp/vst3_test_support.hpp"

#include <bit>
#include <cfenv>
#include <cmath>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <map>
#include <optional>
#include <utility>

namespace {
using namespace rhodes;
using namespace rhodes::vst3;
using namespace rhodes::vst3::test_host;

constexpr std::size_t capacity = 257;
std::size_t checked_samples = 0;
std::size_t checked_callbacks = 0;

template<class Sample>
using SampleBits = std::conditional_t<std::is_same_v<Sample, float>,
                                      std::uint32_t, std::uint64_t>;

template<class Sample>
bool same_bits(Sample a, Sample b) {
    return std::bit_cast<SampleBits<Sample>>(a) == std::bit_cast<SampleBits<Sample>>(b);
}

// The legacy shared helper intentionally has only two entries. Keep the third
// queue local to this suite, and use raw queues: the SDK hosting helper merges
// equal offsets before the processor can observe the duplicate endpoints.
struct ThreeQueues final : FixedHostObject<vst::IParameterChanges> {
    std::array<vst::IParamValueQueue*, 3> values{};
    sdk::int32 count = 0;

    void set(std::initializer_list<vst::IParamValueQueue*> queues) {
        require(queues.size() <= values.size(), "too many fixture queues");
        values.fill(nullptr);
        std::copy(queues.begin(), queues.end(), values.begin());
        count = static_cast<sdk::int32>(queues.size());
    }
    sdk::int32 PLUGIN_API getParameterCount() override { return count; }
    vst::IParamValueQueue* PLUGIN_API getParameterData(sdk::int32 index) override {
        return index >= 0 && index < count ? values[static_cast<std::size_t>(index)] : nullptr;
    }
    vst::IParamValueQueue* PLUGIN_API addParameterData(const vst::ParamID&,
                                                     sdk::int32&) override {
        return nullptr;
    }
};

void points(ParameterQueue& queue, std::initializer_list<ParameterQueue::Point> supplied) {
    require(supplied.size() <= queue.values.size(), "too many fixture points");
    std::copy(supplied.begin(), supplied.end(), queue.values.begin());
    queue.count = static_cast<sdk::int32>(supplied.size());
}

float canonical(double value) {
    const float result = static_cast<float>(value);
    return result == 0.0F ? 0.0F : result;
}

// Independent oracle: first replace duplicate endpoints in the complete map,
// then use barycentric weights. No production control/interpolation helper is
// used. Dyadic fixtures have exact rational values in both host formats.
struct Curve {
    float initial;
    std::map<sdk::int32, long double> anchors;

    Curve(float previous, const ParameterQueue* queue) : initial(previous) {
        if (queue == nullptr) { return; }
        for (sdk::int32 index = 0; index < queue->count; ++index) {
            const auto& point = queue->values[static_cast<std::size_t>(index)];
            anchors[point.offset] = canonical(point.value);
        }
    }
    double at(sdk::int32 frame) const {
        const auto right = anchors.lower_bound(frame);
        if (right == anchors.end()) {
            return anchors.empty() ? initial : static_cast<double>(anchors.rbegin()->second);
        }
        if (right->first == frame) { return static_cast<double>(right->second); }
        const auto left = right == anchors.begin() ?
            std::pair<sdk::int32, long double>{-1, initial} :
            std::pair<sdk::int32, long double>{std::prev(right)->first, std::prev(right)->second};
        return static_cast<double>(
            (static_cast<long double>(right->first - frame) * left.second +
             static_cast<long double>(frame - left.first) * right->second) /
            static_cast<long double>(right->first - left.first));
    }
    float final(sdk::int32 frames) const {
        if (frames != 0) { return canonical(at(frames - 1)); }
        return anchors.empty() ? initial : canonical(static_cast<double>(anchors.rbegin()->second));
    }
};

std::uint32_t word_at(const MemoryStream& stream, std::size_t offset) {
    std::uint32_t result = 0;
    for (std::size_t index = 0; index != 4; ++index) {
        result |= static_cast<std::uint32_t>(stream.bytes[offset + index]) << (8U * index);
    }
    return result;
}

// Literal state fixture, independent of the production encoders and of the
// shared MemoryStream::load helper, whose contract is legacy v1/unity only.
void load_recall(MemoryStream& stream, InstrumentState state) {
    stream.bytes.fill(0);
    stream.bytes[0] = 'R'; stream.bytes[1] = 'H';
    stream.bytes[2] = 'S'; stream.bytes[3] = 'T';
    const bool unity = same_bits(state.expression, 1.0F);
    stream.bytes[4] = unity ? 1 : 2;
    stream.bytes[6] = static_cast<std::uint8_t>(kStateModel);
    const auto put = [&](std::size_t offset, float value) {
        const auto bits = std::bit_cast<std::uint32_t>(value);
        for (std::size_t index = 0; index != 4; ++index) {
            stream.bytes[offset + index] = static_cast<std::uint8_t>(bits >> (8U * index));
        }
    };
    put(8, state.monitoring_gain);
    put(12, state.sustain ? 1.0F : 0.0F);
    if (!unity) { put(16, state.expression); }
    stream.position = 0;
    stream.extent = unity ? 16 : 20;
}

void expect_state(const RhodesProcessor& processor, InstrumentState expected) {
    for (const auto actual : {processor.current_state(), processor.published_state()}) {
        require(same_bits(actual.monitoring_gain, expected.monitoring_gain),
                "canonical final gain differs");
        require(same_bits(actual.expression, expected.expression),
                "canonical final expression differs");
        require(actual.sustain == expected.sustain, "final sustain differs");
    }
}

void expect_saved_state(RhodesProcessor& processor, InstrumentState expected) {
    MemoryStream stream;
    require(processor.getState(&stream) == sdk::kResultOk, "save accepted configuration");
    const bool unity = same_bits(expected.expression, 1.0F);
    require(stream.extent == (unity ? 16 : 20) && stream.bytes[4] == (unity ? 1 : 2),
            "saved expression chose the wrong version/length");
    require(word_at(stream, 8) == std::bit_cast<std::uint32_t>(expected.monitoring_gain),
            "saved gain bits differ");
    require(word_at(stream, 12) == (expected.sustain ? 0x3f800000U : 0U),
            "saved sustain bits differ");
    require(unity || word_at(stream, 16) == std::bit_cast<std::uint32_t>(expected.expression),
            "saved expression bits differ");
}

template<class Sample>
void inspect_buffer(const ProcessorHost<Sample, capacity>& host, sdk::int32 frames) {
    bool nonzero = false;
    for (std::size_t index = 0; index < static_cast<std::size_t>(frames); ++index) {
        require(std::isfinite(host.left[index]), "nonfinite audio sample");
        require(same_bits(host.left[index], host.right[index]), "stereo bytes differ");
        nonzero = nonzero || host.left[index] != Sample{0};
    }
    for (std::size_t index = static_cast<std::size_t>(frames); index < capacity; ++index) {
        require(same_bits(host.left[index], Sample{91.25}) &&
                same_bits(host.right[index], Sample{91.25}), "write beyond declared frame count");
    }
    require(host.output.silenceFlags == (nonzero ? 0U : 3U), "actual-output silence mask differs");
    require(!host.processor.requires_reset(), "valid scalar fixture latched a fault");
}

template<class Sample>
bool any_audio(const ProcessorHost<Sample, capacity>& host, std::size_t frames) {
    return std::any_of(host.left.begin(), host.left.begin() + static_cast<std::ptrdiff_t>(frames),
                       [](Sample value) { return value != Sample{0}; });
}

enum class Comparison { exact, nondyadic };

template<class Sample>
struct Fixture {
    ProcessorHost<Sample, capacity> target;
    ProcessorHost<double, capacity> monitored;
    std::unique_ptr<ProcessorHost<double, capacity>> unity_gain;
    ParameterQueue gain, expression, pedal;
    ThreeQueues parameters, peer_parameters, unity_parameters;
    EventList events;
    bool null_parameters = false;
    InstrumentState expected{};
    std::optional<InstrumentState> pending;
    std::uint64_t cursor = 0;
    std::map<std::pair<sdk::int16, sdk::int32>, sdk::int16> owners;
    std::size_t narrowing_witnesses = 0;

    explicit Fixture(bool check_gain = false) {
        expression.id = kExpressionId;
        pedal.id = kSustainId;
        if (check_gain) {
            unity_gain = std::make_unique<ProcessorHost<double, capacity>>();
            points(gain, {{0, 1.0}});
            unity_parameters.set({&gain});
            unity_gain->data.numSamples = 0;
            unity_gain->data.inputParameterChanges = &unity_parameters;
            require(unity_gain->processor.process(unity_gain->data) == sdk::kResultOk,
                    "establish unity-gain peer");
            expect_state(unity_gain->processor, {1.0F, false, 1.0F});
        }
        clear();
    }

    void clear() {
        gain.count = expression.count = pedal.count = events.count = 0;
        parameters.set({});
        null_parameters = false;
    }

    bool includes(const ParameterQueue& queue) const {
        if (null_parameters) { return false; }
        for (sdk::int32 index = 0; index < parameters.count; ++index) {
            if (parameters.values[static_cast<std::size_t>(index)] == &queue) { return true; }
        }
        return false;
    }

    void recall(InstrumentState state) {
        MemoryStream actual_stream, peer_stream;
        load_recall(actual_stream, state);
        require(target.processor.setState(&actual_stream) == sdk::kResultOk, "target recall");
        auto peer_state = state;
        peer_state.expression = 1.0F;
        load_recall(peer_stream, peer_state);
        require(monitored.processor.setState(&peer_stream) == sdk::kResultOk, "matched peer recall");
        if (unity_gain) {
            MemoryStream unity_stream;
            peer_state.monitoring_gain = 1.0F;
            load_recall(unity_stream, peer_state);
            require(unity_gain->processor.setState(&unity_stream) == sdk::kResultOk,
                    "unity-gain peer recall");
        }
        pending = state;
    }

    void accepted(sdk::int32 frames, Comparison comparison = Comparison::exact) {
        require(frames >= 0 && static_cast<std::size_t>(frames) < capacity, "fixture frame bound");
        if (pending) {
            expected = *pending;
            pending.reset();
            cursor = 0;
            owners.clear();
        }
        const Curve gains{expected.monitoring_gain, includes(gain) ? &gain : nullptr};
        const Curve expressions{expected.expression, includes(expression) ? &expression : nullptr};
        peer_parameters.set({});
        unity_parameters.set({});
        if (!null_parameters) {
            for (sdk::int32 index = 0; index < parameters.count; ++index) {
                auto* queue = parameters.values[static_cast<std::size_t>(index)];
                if (queue != &expression) {
                    peer_parameters.values[static_cast<std::size_t>(peer_parameters.count++)] = queue;
                }
                if (queue == &pedal) {
                    unity_parameters.values[static_cast<std::size_t>(unity_parameters.count++)] = queue;
                }
            }
        }
        const auto process = [&](auto& host, ThreeQueues& queues) {
            host.fill(91.25);
            host.data.numSamples = frames;
            host.data.inputParameterChanges = null_parameters ? nullptr : &queues;
            host.data.inputEvents = &events;
            require(host.processor.process(host.data) == sdk::kResultOk, "valid expression block rejected");
            inspect_buffer(host, frames);
        };
        process(target, parameters);
        process(monitored, peer_parameters);
        if (unity_gain) { process(*unity_gain, unity_parameters); }

        for (sdk::int32 frame = 0; frame < frames; ++frame) {
            const auto index = static_cast<std::size_t>(frame);
            double monitoring = monitored.left[index];
            if (unity_gain) {
                const double volts = unity_gain->left[index] / 32.0;
                require(volts == 0.0 || std::isnormal(volts), "gain oracle left the normal sample domain");
                require(same_bits(32.0 * volts, unity_gain->left[index]),
                        "unity peer division was not exact");
                monitoring = (32.0 * gains.at(frame)) * volts;
                require(same_bits(monitored.left[index], monitoring),
                        "gain differs from independent curve and legacy multiplication order");
            }
            const double factor = expressions.at(frame);
            const double scaled = factor == 1.0 ? monitoring : monitoring * factor;
            const Sample wanted = static_cast<Sample>(scaled);
            const Sample actual = target.left[index];
            // Muting requires numerical zero; only the exact-unity path has
            // a legacy byte-preservation requirement for a signed audio zero.
            bool equal = factor == 0.0 ? actual == Sample{0} : same_bits(actual, wanted);
            if (comparison == Comparison::nondyadic) {
                if (wanted == Sample{0}) { equal = actual == Sample{0}; }
                else if constexpr (std::is_same_v<Sample, float>) {
                    equal = actual >= std::nextafter(wanted, -std::numeric_limits<float>::infinity()) &&
                            actual <= std::nextafter(wanted, std::numeric_limits<float>::infinity());
                } else {
                    const double bound = 16.0 * std::numeric_limits<double>::epsilon() * std::abs(monitoring);
                    equal = std::abs(actual - wanted) <= bound;
                }
                const Sample narrowed = static_cast<Sample>(monitoring * static_cast<double>(canonical(factor)));
                if (monitoring != 0.0 && !same_bits(narrowed, wanted)) { ++narrowing_witnesses; }
            }
            if (!equal) {
                throw std::runtime_error("expression scalar mismatch at frame " + std::to_string(frame) +
                    " (actual bits " + std::to_string(std::bit_cast<SampleBits<Sample>>(actual)) +
                    ", reference bits " + std::to_string(std::bit_cast<SampleBits<Sample>>(wanted)) + ")");
            }
            if (factor == 0.0) { require(actual == Sample{0}, "zero expression emitted nonzero audio"); }
        }

        expected.monitoring_gain = gains.final(frames);
        expected.expression = expressions.final(frames);
        if (includes(pedal) && pedal.count != 0) {
            expected.sustain = pedal.values[static_cast<std::size_t>(pedal.count - 1)].value >= 0.5;
        }
        for (sdk::int32 index = 0; index < events.count; ++index) {
            const auto& event = events.values[static_cast<std::size_t>(index)];
            if (event.type == vst::Event::kNoteOnEvent && event.noteOn.velocity != 0.0F) {
                owners[{event.noteOn.channel, event.noteOn.noteId}] = event.noteOn.pitch;
            } else if (event.type == vst::Event::kNoteOffEvent) {
                owners.erase({event.noteOff.channel, event.noteOff.noteId});
            } else {
                require(false, "unexpected event in scalar fixture");
            }
        }
        cursor += static_cast<std::uint64_t>(frames);
        const auto inspect_owner = [&](const RhodesProcessor& processor) {
            require(processor.engine_processor().frame_cursor() == cursor, "audio cursor differs");
            require(processor.note_tracker().active_count() == owners.size(), "active ownership differs");
            for (std::size_t key = 0; key < key_count; ++key) {
                const auto pitch = static_cast<sdk::int16>(key + 28);
                const auto count = std::count_if(owners.begin(), owners.end(),
                    [pitch](const auto& owner) { return owner.second == pitch; });
                require(processor.note_tracker().held_count(key) == static_cast<std::size_t>(count),
                        "per-key ownership differs");
            }
        };
        inspect_owner(target.processor);
        inspect_owner(monitored.processor);
        expect_state(target.processor, expected);
        expect_saved_state(target.processor, expected);
        auto peer_state = expected;
        peer_state.expression = 1.0F;
        expect_state(monitored.processor, peer_state);
        same_mechanics(target.processor.engine_processor().engine(), monitored.processor.engine_processor().engine());
        if (unity_gain) {
            inspect_owner(unity_gain->processor);
            peer_state.monitoring_gain = 1.0F;
            expect_state(unity_gain->processor, peer_state);
            same_mechanics(target.processor.engine_processor().engine(), unity_gain->processor.engine_processor().engine());
        }
        checked_samples += static_cast<std::size_t>(frames);
        ++checked_callbacks;
    }

    void warm() {
        clear();
        events.count = 1;
        events.values[0] = note_on(0, 10, 68, 0.75F);
        accepted(256);
        require(any_audio(target, 256), "warm-up is not audibly discriminating");
        clear();
    }

    void same_future_filter_audio() const {
        using Engine = std::remove_cvref_t<decltype(target.processor.engine_processor().engine())>;
        const auto actual = std::make_unique<Engine>(target.processor.engine_processor().engine());
        const auto peer = std::make_unique<Engine>(monitored.processor.engine_processor().engine());
        std::array<double, 129> actual_output{}, peer_output{};
        require(actual->process(actual_output) && peer->process(peer_output), "filter continuation failed");
        for (std::size_t index = 0; index < actual_output.size(); ++index) {
            require(same_bits(actual_output[index], peer_output[index]), "expression changed future filter audio");
        }
    }
};

template<class Sample>
void default_queues_for() {
    for (float initial : {1.0F, 0.25F}) {
        for (int shape = 0; shape != 4; ++shape) {
            auto fixture = std::make_unique<Fixture<Sample>>();
            auto& test = *fixture;
            test.warm();
            if (initial != 1.0F) {
                points(test.expression, {{0, initial}});
                test.parameters.set({&test.expression});
                test.accepted(0);
                test.clear();
            }
            if (shape == 0) { test.null_parameters = true; }
            if (shape == 2) {
                points(test.gain, {{63, 0.75}});
                test.parameters.set({&test.gain});
            }
            if (shape == 3) { test.parameters.set({&test.expression}); }
            test.accepted(64);
            require(same_bits(test.target.processor.current_state().expression, initial),
                    "absent/empty expression queue changed its held value");
            require(any_audio(test.target, 64), "queue-shape fixture is silent");
        }
    }
}

template<class Sample>
void anchor_duplicates_for() {
    auto fixture = std::make_unique<Fixture<Sample>>();
    auto& test = *fixture;
    test.warm();
    points(test.expression, {{3, 0.0}, {3, 0.5}, {7, 0.0}, {7, 0.25}});
    const Curve reference{1.0F, &test.expression};
    constexpr std::array<double, 8> expected{0.875, 0.75, 0.625, 0.5, 0.4375, 0.375, 0.3125, 0.25};
    for (std::size_t index = 0; index < expected.size(); ++index) {
        require(reference.at(static_cast<sdk::int32>(index)) == expected[index], "anchor fixture drift");
    }
    test.parameters.set({&test.expression});
    test.accepted(8);
    require(test.expression.get_calls == 4, "raw duplicate endpoints were not delivered");
    test.clear();
    test.null_parameters = true;
    test.accepted(1);
    for (double endpoint : {0.0, 1.0}) {
        test.clear();
        points(test.expression, {{0, endpoint}});
        test.parameters.set({&test.expression});
        test.accepted(1);
    }
    test.clear();
    test.accepted(64);
    require(any_audio(test.target, 64), "unity endpoint did not resume held audio");
}

template<class Sample>
void combined_curves_for() {
    for (bool reversed : {false, true}) {
        auto fixture = std::make_unique<Fixture<Sample>>(true);
        auto& test = *fixture;
        test.warm();
        points(test.gain, {{0, 0.25}});
        points(test.expression, {{0, 1.0}});
        test.parameters.set({&test.gain, &test.expression});
        test.accepted(0);
        test.clear();
        points(test.gain, {{3, 0.75}, {11, 0.25}});
        points(test.expression, {{7, 0.5}, {15, 1.0}});
        const Curve gains{0.25F, &test.gain}, expressions{1.0F, &test.expression};
        constexpr std::array<sdk::int32, 5> positions{0, 3, 7, 11, 15};
        constexpr std::array<double, 5> products{0.3515625, 0.5625, 0.25, 0.1875, 0.25};
        for (std::size_t index = 0; index < positions.size(); ++index) {
            require(gains.at(positions[index]) * expressions.at(positions[index]) == products[index],
                    "independent-curve fixture drift");
        }
        if (reversed) { test.parameters.set({&test.expression, &test.gain}); }
        else { test.parameters.set({&test.gain, &test.expression}); }
        test.accepted(16);
        require(any_audio(test.target, 16), "combined-control fixture is silent");
        test.clear();
        test.accepted(64);
    }
}

template<class Sample>
void nondyadic_curve_for() {
    auto fixture = std::make_unique<Fixture<Sample>>();
    auto& test = *fixture;
    test.warm();
    require(std::bit_cast<std::uint32_t>(canonical(0.1)) == 0x3dcccccdU &&
            std::bit_cast<std::uint32_t>(canonical(0.9)) == 0x3f666666U,
            "non-dyadic canonical anchors differ");
    points(test.expression, {{0, 0.1}});
    test.parameters.set({&test.expression});
    test.accepted(0);
    test.clear();
    points(test.expression, {{6, 0.9}});
    test.parameters.set({&test.expression});
    test.accepted(7, Comparison::nondyadic);
    require(test.narrowing_witnesses != 0, "non-dyadic fixture cannot distinguish premature float controls");
    test.clear();
    test.accepted(64);
}

template<class Sample>
void muted_resume_for() {
    auto fixture = std::make_unique<Fixture<Sample>>();
    auto& test = *fixture;
    test.warm();
    const double before_energy = test.target.processor.engine_processor().engine().mechanical_energy_joules();
    points(test.expression, {{0, 0.0}});
    test.parameters.set({&test.expression});
    test.accepted(32);
    require(test.target.processor.note_tracker().active_count() == 1, "mute discarded held ownership");
    require(test.target.processor.engine_processor().engine().mechanical_energy_joules() != before_energy,
            "mute froze mechanical evolution");
    test.clear();
    points(test.gain, {{0, 0.5}});
    points(test.expression, {{0, 0.0}});
    points(test.pedal, {{0, 1.0}, {96, 0.0}});
    test.parameters.set({&test.pedal, &test.expression, &test.gain});
    test.events.count = 3;
    test.events.values[0] = note_on(17, 20, 72, 0.625F, 4);
    test.events.values[1] = note_off(63, 10, 68);
    test.events.values[2] = note_off(95, 20, 72, 4);
    test.accepted(256);
    require(!any_audio(test.target, 256), "muted release block is not silent");
    require(test.target.processor.note_tracker().active_count() == 0, "muted releases retained owners");
    require(!test.target.processor.current_state().sustain, "muted sustain release was lost");
    test.same_future_filter_audio();
    test.clear();
    points(test.expression, {{0, 1.0}});
    test.parameters.set({&test.expression});
    test.accepted(256);
    require(any_audio(test.target, 256), "unmute lost the released tail");
    require(test.target.processor.note_tracker().active_count() == 0, "tail restart invented ownership");
    test.same_future_filter_audio();
}

template<class Sample>
void flush_recall_for() {
    auto fixture = std::make_unique<Fixture<Sample>>();
    auto& test = *fixture;
    test.warm();
    const auto before_flush = std::make_unique<HostEventProcessor>(test.target.processor.engine_processor());
    points(test.gain, {{0, 0.375}, {0, 0.625}});
    points(test.expression, {{0, 0.125}, {0, 0.75}, {0, 0.25}});
    points(test.pedal, {{0, 1.0}, {0, 0.0}});
    test.parameters.set({&test.gain, &test.pedal, &test.expression});
    test.accepted(0);
    require(test.target.processor.engine_processor().frame_cursor() == before_flush->frame_cursor(),
            "zero-frame flush advanced time");
    same_mechanics(test.target.processor.engine_processor().engine(), before_flush->engine());
    test.clear();
    test.accepted(1);
    require(any_audio(test.target, 1), "flush continuation is silent");

    // Deliberately make both live controls stale before a pending recall. Both
    // peers reset at the same accepted boundary, preserving physical history.
    points(test.gain, {{0, 0.375}});
    points(test.expression, {{0, 0.875}});
    test.parameters.set({&test.gain, &test.expression});
    test.accepted(0);
    test.clear();
    test.recall({0.625F, false, 0.25F});
    points(test.expression, {{127, 0.75}});
    test.parameters.set({&test.expression});
    test.events.count = 1;
    test.events.values[0] = note_on(0, 30, 65, 0.625F);
    test.accepted(128);
    require(any_audio(test.target, 128), "recalled-anchor fixture is silent after startup");

    test.clear();
    test.recall({0.25F, true, 0.125F});
    points(test.gain, {{0, 0.75}});
    points(test.expression, {{0, 0.875}, {0, 0.5}});
    points(test.pedal, {{0, 0.0}});
    test.parameters.set({&test.expression, &test.pedal, &test.gain});
    test.accepted(0);
    require(test.target.processor.engine_processor().frame_cursor() == 0 &&
            test.target.processor.note_tracker().active_count() == 0,
            "recall plus flush did not reset at its boundary");
    test.clear();
    test.events.count = 1;
    test.events.values[0] = note_on(0, 31, 69, 0.75F);
    test.accepted(256);
    require(any_audio(test.target, 256), "recall-plus-flush continuation is silent");

    test.clear();
    test.recall({0.5F, false, 1.0F});
    test.events.count = 1;
    test.events.values[0] = note_on(0, 32, 70, 0.5F);
    test.accepted(256);
    require(any_audio(test.target, 256), "legacy recall did not restore unity audio");
}

template<class Sample>
void initialize_partition(Fixture<Sample>& fixture) {
    fixture.warm();
    points(fixture.gain, {{0, 0.25}});
    points(fixture.expression, {{0, 1.0}});
    fixture.parameters.set({&fixture.gain, &fixture.expression});
    fixture.accepted(0);
    fixture.clear();
}

template<class Sample>
void partition_curves_for() {
    auto whole = std::make_unique<Fixture<Sample>>();
    initialize_partition(*whole);
    points(whole->gain, {{15, 0.75}, {31, 0.25}});
    points(whole->expression, {{15, 0.5}, {31, 1.0}});
    const Curve global_gain{0.25F, &whole->gain}, global_expression{1.0F, &whole->expression};
    const std::array<vst::Event, 4> global_events{
        note_on(0, 10, 68, 0.5F), note_on(8, 20, 72, 0.625F, 3),
        note_off(16, 10, 68), note_off(24, 20, 72, 3)};
    whole->parameters.set({&whole->gain, &whole->expression});
    whole->events.count = static_cast<sdk::int32>(global_events.size());
    std::copy(global_events.begin(), global_events.end(), whole->events.values.begin());
    whole->accepted(32);
    const auto complete_audio = whole->target.left;
    const auto complete_state = std::make_unique<ProcessorSnapshot>(whole->target.processor);
    whole->clear();
    whole->accepted(256);
    require(any_audio(whole->target, 256), "partition continuation has no released tail");
    const auto complete_tail = whole->target.left;

    const std::array<sdk::int32, 6> first{1, 2, 4, 1, 8, 16};
    const std::array<sdk::int32, 4> boundaries{8, 8, 8, 8};
    for (const auto widths : {std::span<const sdk::int32>{first}, std::span<const sdk::int32>{boundaries}}) {
        auto split = std::make_unique<Fixture<Sample>>();
        initialize_partition(*split);
        sdk::int32 begin = 0;
        for (const sdk::int32 width : widths) {
            split->clear();
            const sdk::int32 end = begin + width;
            const auto supply = [&](ParameterQueue& queue, const Curve& curve) {
                for (const auto& [offset, value] : curve.anchors) {
                    if (offset >= begin && offset < end - 1) {
                        queue.values[static_cast<std::size_t>(queue.count++)] =
                            {offset - begin, static_cast<double>(value)};
                    }
                }
                const double boundary = curve.at(end - 1);
                require(static_cast<double>(canonical(boundary)) == boundary,
                        "partition boundary is not exactly binary32");
                queue.values[static_cast<std::size_t>(queue.count++)] = {width - 1, boundary};
            };
            supply(split->gain, global_gain);
            supply(split->expression, global_expression);
            split->parameters.set({&split->expression, &split->gain});
            for (auto event : global_events) {
                if (event.sampleOffset >= begin && event.sampleOffset < end) {
                    event.sampleOffset -= begin;
                    split->events.values[static_cast<std::size_t>(split->events.count++)] = event;
                }
            }
            split->accepted(width);
            for (sdk::int32 frame = 0; frame < width; ++frame) {
                require(same_bits(split->target.left[static_cast<std::size_t>(frame)],
                                  complete_audio[static_cast<std::size_t>(begin + frame)]),
                        "exact dyadic partition changed audio bytes");
            }
            begin = end;
        }
        require(begin == 32, "partition schedule length differs");
        complete_state->expect_unchanged(split->target.processor);
        split->clear();
        split->accepted(256);
        for (std::size_t frame = 0; frame < 256; ++frame) {
            require(same_bits(split->target.left[frame], complete_tail[frame]),
                    "exact dyadic partition changed subsequent tail bytes");
        }
    }
}

void default_queues() { default_queues_for<float>(); default_queues_for<double>(); }
void anchor_duplicates() { anchor_duplicates_for<float>(); anchor_duplicates_for<double>(); }
void combined_curves() { combined_curves_for<float>(); combined_curves_for<double>(); }
void nondyadic_curve() { nondyadic_curve_for<float>(); nondyadic_curve_for<double>(); }
void muted_resume() { muted_resume_for<float>(); muted_resume_for<double>(); }
void flush_recall() { flush_recall_for<float>(); flush_recall_for<double>(); }
void partition_curves() { partition_curves_for<float>(); partition_curves_for<double>(); }

using Selector = std::pair<std::string_view, void (*)()>;
constexpr std::array<Selector, 7> selectors{{
    {"default_queues", default_queues}, {"anchor_duplicates", anchor_duplicates},
    {"combined_curves", combined_curves}, {"nondyadic_curve", nondyadic_curve},
    {"muted_resume", muted_resume}, {"flush_recall", flush_recall},
    {"partition_curves", partition_curves}
}};

struct RoundToNearest {
    int previous = std::fegetround();
    RoundToNearest() { require(std::fesetround(FE_TONEAREST) == 0, "select ordinary round-to-nearest"); }
    ~RoundToNearest() { (void)std::fesetround(previous); }
};
} // namespace

int main(int argc, char** argv) {
    if (argc != 2) { std::cerr << "Usage: rhodes_vst3_expression_audio_tests <selector>\n"; return 2; }
    for (const auto& [name, run] : selectors) {
        if (name != argv[1]) { continue; }
        try {
            const RoundToNearest ordinary_rounding;
            run();
            std::cout << "PASS " << name << " (model " << kStateModel << ", float+double, "
                      << checked_samples << " scalar-checked samples, " << checked_callbacks
                      << " accepted callbacks)\n";
            return 0;
        } catch (const std::exception& error) {
            std::cerr << "FAIL " << name << " (model " << kStateModel << "): " << error.what() << '\n';
            return 1;
        }
    }
    std::cerr << "Unknown expression audio selector: " << argv[1] << '\n';
    return 2;
}
