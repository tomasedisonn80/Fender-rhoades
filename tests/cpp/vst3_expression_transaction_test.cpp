#include "tests/cpp/vst3_test_support.hpp"

#include <bit>
#include <cmath>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

namespace {
using namespace rhodes;
using namespace rhodes::vst3;
using namespace rhodes::vst3::test_host;
using ScalarEngine = std::remove_cvref_t<
    decltype(std::declval<const HostEventProcessor&>().engine())>;

#if defined(RHODES_QUADRATURE_VST3)
static_assert(kStateModel == 3 && std::is_same_v<ScalarEngine, QuadratureEngine>);
#elif defined(RHODES_FILTERED_VST3)
static_assert(kStateModel == 2 && std::is_same_v<ScalarEngine, AudioEngine>);
#else
static_assert(kStateModel == 1 && std::is_same_v<ScalarEngine, ResearchEngine>);
#endif
static_assert(kGainId == 0 && kSustainId == 1 && kExpressionId == 2);
static_assert(kControllerParameterCount == 19 && kMaximumHostFrames == 8192);
constexpr std::size_t frames = 128;
constexpr InstrumentState seeded_state{0.625F, true, 0.75F};
constexpr InstrumentState pending_state{0.25F, false, 0.125F};
constexpr InstrumentState newer_state{0.75F, true, 0.5F};

struct Counts {
    std::size_t accepted = 0;
    std::size_t rejected = 0;
    std::size_t samples = 0;
    std::size_t history_samples = 0;
} counts;

template<class Value>
bool same_bits(Value actual, Value expected) {
    using Bits = std::conditional_t<sizeof(Value) == 4, std::uint32_t, std::uint64_t>;
    return std::bit_cast<Bits>(actual) == std::bit_cast<Bits>(expected);
}

bool same_state(InstrumentState actual, InstrumentState expected) {
    return same_bits(actual.monitoring_gain, expected.monitoring_gain) &&
           actual.sustain == expected.sustain && same_bits(actual.expression, expected.expression);
}

// Independently authored wire layout; no production codec is the save oracle.
// All fixture states are exact dyadic values and have positive canonical zero.
constexpr std::array<std::uint8_t, 20> wire(InstrumentState state) {
    std::array<std::uint8_t, 20> bytes{'R', 'H', 'S', 'T', 2, 0,
        static_cast<std::uint8_t>(kStateModel), 0};
    if (state.expression == 1.0F) { bytes[4] = 1; }
    const std::array words{std::bit_cast<std::uint32_t>(state.monitoring_gain),
        state.sustain ? 0x3f800000U : 0U, std::bit_cast<std::uint32_t>(state.expression)};
    for (std::size_t word = 0; word != words.size(); ++word) {
        for (std::size_t byte = 0; byte != 4; ++byte) {
            bytes[8 + 4 * word + byte] = static_cast<std::uint8_t>(words[word] >> (8 * byte));
        }
    }
    return bytes;
}
static_assert(wire(seeded_state) == std::array<std::uint8_t, 20>{
    'R', 'H', 'S', 'T', 2, 0, static_cast<std::uint8_t>(kStateModel), 0,
    0, 0, 0x20, 0x3f, 0, 0, 0x80, 0x3f, 0, 0, 0x40, 0x3f});
static_assert(wire(pending_state) == std::array<std::uint8_t, 20>{
    'R', 'H', 'S', 'T', 2, 0, static_cast<std::uint8_t>(kStateModel), 0,
    0, 0, 0x80, 0x3e, 0, 0, 0, 0, 0, 0, 0, 0x3e});

void expect_wire(RhodesProcessor& processor, InstrumentState state) {
    MemoryStream stream;
    stream.bytes.fill(0xa5);
    require(processor.getState(&stream) == sdk::kResultOk, "transaction save failed");
    const auto expected = wire(state);
    const std::size_t extent = state.expression == 1.0F ? 16U : 20U;
    require(stream.extent == static_cast<sdk::int64>(extent) &&
            std::equal(expected.begin(), expected.begin() + static_cast<std::ptrdiff_t>(extent),
                       stream.bytes.begin()), "transaction changed exact saved bytes");
    require(std::all_of(stream.bytes.begin() + static_cast<std::ptrdiff_t>(extent),
                        stream.bytes.end(), [](std::uint8_t byte) { return byte == 0xa5; }),
            "transaction save wrote beyond its exact wire extent");
}

// These raw adapters intentionally do not sort, merge, clamp or deduplicate.
// Their getter counters expose the actual rejection/traversal boundary.
template<std::size_t Capacity = 4>
struct RawQueue final : FixedHostObject<vst::IParamValueQueue> {
    std::array<ParameterQueue::Point, Capacity> values{};
    vst::ParamID id = kExpressionId;
    sdk::int32 count = 0;
    sdk::int32 failed_index = -1;
    sdk::int32 missing_offset = -1;
    sdk::int32 missing_value = -1;
    std::size_t id_calls = 0;
    std::size_t count_calls = 0;
    std::size_t point_calls = 0;

    void set(vst::ParamID parameter, std::initializer_list<ParameterQueue::Point> supplied) {
        require(supplied.size() <= Capacity, "oversized raw queue fixture");
        id = parameter;
        count = static_cast<sdk::int32>(supplied.size());
        std::copy(supplied.begin(), supplied.end(), values.begin());
        failed_index = missing_offset = missing_value = -1;
        clear_counts();
    }
    void clear_counts() { id_calls = count_calls = point_calls = 0; }
    vst::ParamID PLUGIN_API getParameterId() override { ++id_calls; return id; }
    sdk::int32 PLUGIN_API getPointCount() override { ++count_calls; return count; }
    sdk::tresult PLUGIN_API getPoint(sdk::int32 index, sdk::int32& offset,
                                    vst::ParamValue& value) override {
        ++point_calls;
        if (index < 0 || index >= count || index == failed_index ||
            static_cast<std::size_t>(index) >= Capacity) { return sdk::kResultFalse; }
        const auto& point = values[static_cast<std::size_t>(index)];
        if (index != missing_offset) { offset = point.offset; }
        if (index != missing_value) { value = point.value; }
        return sdk::kResultOk;
    }
    sdk::tresult PLUGIN_API addPoint(sdk::int32, vst::ParamValue, sdk::int32&) override {
        return sdk::kNotImplemented;
    }
};

struct RawChanges final : FixedHostObject<vst::IParameterChanges> {
    std::array<vst::IParamValueQueue*, 19> values{};
    sdk::int32 count = 0;
    std::size_t count_calls = 0;
    std::size_t queue_calls = 0;

    void set(std::initializer_list<vst::IParamValueQueue*> queues) {
        require(queues.size() <= values.size(), "oversized raw changes fixture");
        values.fill(nullptr);
        std::copy(queues.begin(), queues.end(), values.begin());
        count = static_cast<sdk::int32>(queues.size());
        clear_counts();
    }
    void clear_counts() { count_calls = queue_calls = 0; }
    sdk::int32 PLUGIN_API getParameterCount() override { ++count_calls; return count; }
    vst::IParamValueQueue* PLUGIN_API getParameterData(sdk::int32 index) override {
        ++queue_calls;
        return index >= 0 && index < count && static_cast<std::size_t>(index) < values.size() ?
            values[static_cast<std::size_t>(index)] : nullptr;
    }
    vst::IParamValueQueue* PLUGIN_API addParameterData(const vst::ParamID&,
                                                     sdk::int32&) override { return nullptr; }
};

template<class Sample>
struct Host : ProcessorHost<Sample, frames + 2> {
    std::array<Sample*, 1> mono{this->left.data() + 1};
    std::array<Sample*, 2> stereo{this->left.data() + 1, this->right.data() + 1};
    sdk::int32 width;
    static constexpr Sample sentinel = Sample{0.3125};
    static constexpr sdk::uint64 sentinel_flags = 0x123456789abcdef0ULL;

    explicit Host(sdk::int32 channels) : width(channels) {
        require(this->processor.setProcessing(false) == sdk::kResultOk &&
                this->processor.setActive(false) == sdk::kResultOk, "suspend transaction host");
        auto arrangement = width == 1 ? vst::SpeakerArr::kMono : vst::SpeakerArr::kStereo;
        require(this->processor.setBusArrangements(nullptr, 0, &arrangement, 1) == sdk::kResultOk &&
                this->processor.setActive(true) == sdk::kResultOk &&
                this->processor.setProcessing(true) == sdk::kResultOk, "negotiate transaction layout");
        this->data.numSamples = static_cast<sdk::int32>(frames);
        supplied();
    }
    void supplied() {
        this->data.numOutputs = 1;
        this->data.outputs = &this->output;
        this->output.numChannels = width;
        if constexpr (std::is_same_v<Sample, float>) {
            this->output.channelBuffers32 = width == 1 ? mono.data() : stereo.data();
        } else {
            this->output.channelBuffers64 = width == 1 ? mono.data() : stereo.data();
        }
    }
    void active(bool value) {
        require(this->processor.activateBus(vst::kAudio, vst::kOutput, 0, value) == sdk::kResultOk,
                "transaction output activation failed");
    }
    void initialize_output() {
        this->fill(sentinel);
        this->output.silenceFlags = sentinel_flags;
    }
    bool writes(sdk::int32 channel) const {
        if (this->data.numOutputs == 0 || this->data.numSamples == 0 ||
            channel >= this->output.numChannels) { return false; }
        return (width == 1 ? mono[static_cast<std::size_t>(channel)] :
                            stereo[static_cast<std::size_t>(channel)]) != nullptr;
    }
    void expect_sample(std::size_t frame, Sample expected) const {
        require(same_bits(this->left[frame + 1], writes(0) ? expected : sentinel) &&
                same_bits(this->right[frame + 1], writes(1) ? expected : sentinel),
                "transaction sample bits or unused storage changed");
    }
    void expect_guards() const {
        require(this->left.front() == sentinel && this->left.back() == sentinel &&
                this->right.front() == sentinel && this->right.back() == sentinel,
                "transaction wrote beyond declared sample extent");
    }
    void expect_flags(bool nonzero) const {
        const auto mask = this->output.numChannels == 0 ? 0U : width == 1 ? 1U : 3U;
        require(this->output.silenceFlags == (this->data.numOutputs == 0 ? sentinel_flags :
                    (nonzero && this->data.numSamples != 0 ? 0U : mask)),
                "transaction changed an omitted silence flag or reported the wrong mask");
    }
};

struct Owner {
    sdk::int32 id;
    sdk::int16 pitch;
    sdk::int16 channel;
    float velocity;
};
constexpr std::array<Owner, 6> initial_owners{{
    {7, 60, 3, 0.5F}, {-2, 60, 9, 0.375F}, {-1, 72, 3, 0.5F},
    {-1, 72, 3, 0.25F}, {44, 84, 15, 0.625F}, {7, 65, 8, 0.5F}}};

// Independent ownership list and explicit physical actions. Neither admission
// decisions nor the expected release schedule uses the production NoteTracker.
struct Oracle {
    std::unique_ptr<ScalarEngine> engine = std::make_unique<ScalarEngine>();
    std::vector<Owner> owners;
    InstrumentState live{};
    std::optional<InstrumentState> pending;
    std::uint64_t cursor = 0;

    Oracle() { require(engine->prepare(48000.0), "prepare transaction oracle"); }
    static std::size_t key(int pitch) { return static_cast<std::size_t>(pitch - 28); }
    std::size_t holders(int pitch) const {
        return static_cast<std::size_t>(std::count_if(owners.begin(), owners.end(),
            [pitch](const Owner& owner) { return owner.pitch == pitch; }));
    }
    void strike(Owner owner) {
        const auto found = std::find_if(owners.begin(), owners.end(), [&](const Owner& candidate) {
            return owner.id != -1 && candidate.id == owner.id && candidate.channel == owner.channel;
        });
        require(found == owners.end() || found->pitch == owner.pitch, "invalid oracle identity reuse");
        if (owner.id == -1 || found == owners.end()) { owners.push_back(owner); }
        require(engine->note_on(key(owner.pitch), owner.velocity), "oracle strike failed");
    }
    void release(sdk::int32 id, sdk::int16 pitch, sdk::int16 channel) {
        const auto found = std::find_if(owners.begin(), owners.end(), [&](const Owner& owner) {
            return owner.id == id && owner.channel == channel && (id != -1 || owner.pitch == pitch);
        });
        if (found == owners.end()) { return; }
        require(found->pitch == pitch, "invalid oracle identity release");
        owners.erase(found);
        if (holders(pitch) == 0) { require(engine->note_off(key(pitch)), "oracle release failed"); }
    }
    void release_channel(int channel) {
        std::array<std::size_t, key_count> before{};
        for (const auto& owner : owners) { ++before[key(owner.pitch)]; }
        std::erase_if(owners, [channel](const Owner& owner) { return owner.channel == channel; });
        for (std::size_t index = 0; index != key_count; ++index) {
            if (before[index] != 0 && holders(static_cast<int>(index) + 28) == 0) {
                require(engine->note_off(index), "oracle channel release failed");
            }
        }
    }
    void sustain(bool down) { live.sustain = down; engine->set_sustain(down); }
    void controls(InstrumentState state) {
        live.monitoring_gain = state.monitoring_gain;
        live.expression = state.expression;
        sustain(state.sustain);
    }
    void consume_recall() {
        if (!pending) { return; }
        live = *pending;
        pending.reset();
        engine->reset();
        engine->set_sustain(live.sustain);
        owners.clear();
        cursor = 0;
    }
    void recall(RhodesProcessor& processor, InstrumentState state) {
        MemoryStream stream;
        const auto bytes = wire(state);
        const std::size_t extent = state.expression == 1.0F ? 16U : 20U;
        std::copy_n(bytes.begin(), extent, stream.bytes.begin());
        stream.extent = static_cast<sdk::int64>(extent);
        require(processor.setState(&stream) == sdk::kResultOk, "literal transaction recall rejected");
        pending = state;
        inspect(processor);
    }
    void inspect(RhodesProcessor& processor) const {
        require(same_state(processor.current_state(), live) &&
                same_state(processor.published_state(), pending.value_or(live)),
                "transaction changed exact live/published configuration");
        require(!processor.requires_reset() && processor.engine_processor().frame_cursor() == cursor,
                "transaction changed fault latch or frame cursor");
        const auto& actual = processor.engine_processor().engine();
        require(same_bits(actual.sample_rate_hz(), engine->sample_rate_hz()) &&
                same_bits(actual.mechanical_energy_joules(), engine->mechanical_energy_joules()),
                "transaction changed mechanical energy or sample rate");
        require(processor.note_tracker().active_count() == owners.size(), "transaction owner total");
        for (std::size_t index = 0; index != key_count; ++index) {
            require(processor.note_tracker().held_count(index) == holders(static_cast<int>(index) + 28),
                    "transaction per-key ownership");
            for (std::size_t mode = 0; mode != modes_per_key; ++mode) {
                const auto a = actual.modal_state(index, mode);
                const auto b = engine->modal_state(index, mode);
                require(a && b && same_bits(a->displacement_m, b->displacement_m) &&
                        same_bits(a->velocity_m_per_s, b->velocity_m_per_s),
                        "transaction changed exact modal state");
            }
        }
        // Drain a copy by independently known identities, including anonymous
        // multiplicity and the same ID in different channels. Counts alone can
        // miss replacement of one holder by another on the same physical key.
        auto notes = processor.note_tracker();
        std::array<std::size_t, key_count> remaining{};
        for (const auto& owner : owners) { ++remaining[key(owner.pitch)]; }
        for (const auto& owner : owners) {
            const auto result = notes.apply({false, owner.channel, owner.pitch, owner.id, 0.0});
            const bool last = --remaining[key(owner.pitch)] == 0;
            require(result.succeeded() && result.change.has_value() == last,
                    "transaction changed identified/anonymous release behavior");
            if (last) {
                require(result.change->type == EventType::note_off &&
                        result.change->key == key(owner.pitch), "transaction identity released wrong key");
            }
        }
        require(notes.active_count() == 0, "transaction retained an unexpected holder identity");
        expect_wire(processor, pending.value_or(live));
    }
    void history(const RhodesProcessor& processor) const {
        const auto actual = std::make_unique<ScalarEngine>(processor.engine_processor().engine());
        const auto expected = std::make_unique<ScalarEngine>(*engine);
        std::array<double, 129> a{}, b{};
        require(actual->process(a) && expected->process(b), "rejection history rendering failed");
        for (std::size_t index = 0; index != a.size(); ++index) {
            require(same_bits(a[index], b[index]), "rejection changed pre-gain 129-frame history");
        }
        require(std::any_of(b.begin(), b.end(), [](double value) { return value != 0.0; }),
                "rejection fixture lacks nonzero mechanical/filter history");
        counts.history_samples += a.size();
    }
};

template<class Sample>
struct Fixture {
    Host<Sample> host;
    Oracle oracle;
    explicit Fixture(sdk::int32 width) : host(width) {}

    template<class Actions>
    void accept(Actions actions, bool audible = false) {
        host.initialize_output();
        require(host.processor.process(host.data) == sdk::kResultOk, "valid transaction rejected");
        ++counts.accepted;
        oracle.consume_recall();
        bool nonzero = false;
        if (host.data.numSamples == 0) { actions(std::size_t{0}); }
        for (std::size_t frame = 0; frame < static_cast<std::size_t>(host.data.numSamples); ++frame) {
            actions(frame);
            double volts = 0.0;
            require(oracle.engine->process(std::span<double>(&volts, 1)), "transaction scalar render failed");
            const double monitored = (32.0 * oracle.live.monitoring_gain) * volts;
            const double converted = oracle.live.expression == 1.0F ? monitored :
                monitored * static_cast<double>(oracle.live.expression);
            const auto expected = static_cast<Sample>(converted);
            host.expect_sample(frame, expected);
            nonzero = nonzero || expected != Sample{0};
            ++counts.samples;
        }
        if (host.data.numSamples == 0) {
            for (std::size_t frame = 0; frame != frames; ++frame) { host.expect_sample(frame, Sample{0}); }
        }
        require(!audible || nonzero, "accepted continuation is vacuously silent");
        host.expect_guards();
        host.expect_flags(nonzero);
        oracle.cursor += static_cast<std::uint64_t>(host.data.numSamples);
        oracle.inspect(host.processor);
    }
    void reject(std::string_view context) {
        host.initialize_output();
        require(host.processor.process(host.data) == sdk::kResultFalse,
                std::string(context) + ": malformed transaction accepted");
        ++counts.rejected;
        for (std::size_t frame = 0; frame != frames; ++frame) { host.expect_sample(frame, Sample{0}); }
        host.expect_guards();
        host.expect_flags(false);
        oracle.inspect(host.processor);
        // This comparison occurs before any successful pending recall can hide
        // mutation of damping, modal state, FIR memory or output phase.
        oracle.history(host.processor);
    }
    void tail(bool audible = true) {
        host.data.inputParameterChanges = nullptr;
        host.data.inputEvents = nullptr;
        accept([](std::size_t) {}, audible);
    }
    void seed() {
        RawQueue<> gain, sustain, expression;
        gain.set(kGainId, {{0, seeded_state.monitoring_gain}});
        sustain.set(kSustainId, {{0, 1.0}});
        expression.set(kExpressionId, {{0, seeded_state.expression}});
        RawChanges changes;
        changes.set({&expression, &gain, &sustain});
        EventList events;
        events.count = static_cast<sdk::int32>(initial_owners.size());
        for (std::size_t index = 0; index != initial_owners.size(); ++index) {
            const auto& owner = initial_owners[index];
            events.values[index] = note_on(static_cast<sdk::int32>(index), owner.id, owner.pitch,
                                          owner.velocity, owner.channel);
        }
        host.data.inputEvents = &events;
        host.data.inputParameterChanges = &changes;
        accept([&](std::size_t frame) {
            if (frame == 0) { oracle.controls(seeded_state); }
            if (frame < initial_owners.size()) { oracle.strike(initial_owners[frame]); }
        }, true);
        tail();
        require(oracle.cursor == 256 && oracle.owners.size() == 6, "invalid warmed transaction fixture");
    }
};

struct CoBatch {
    RawQueue<> gain, sustain, command, expression;
    RawChanges changes;
    EventList events;
    CoBatch() {
        gain.set(kGainId, {{0, 0.875}});
        sustain.set(kSustainId, {{0, 0.0}});
        command.set(kAllNotesOffBaseId + 3, {{32, 0.0}});
        expression.set(kExpressionId, {{0, 0.5}, {63, 0.25}, {127, 0.125}});
        changes.set({&gain, &sustain, &command, &expression});
        events.count = 2;
        events.values[0] = note_off(1, 7, 60, 3);
        events.values[1] = note_on(4, 99, 90, 0.5F, 4);
    }
    template<class Sample> void attach(Host<Sample>& host) {
        host.data.inputParameterChanges = &changes;
        host.data.inputEvents = &events;
    }
    void clear_counts() {
        gain.clear_counts(); sustain.clear_counts(); command.clear_counts(); expression.clear_counts();
        changes.clear_counts(); events.get_calls = 0;
    }
    void earlier_queues() const {
        require(gain.point_calls == 1 && sustain.point_calls == 1 && command.point_calls == 1 &&
                changes.queue_calls == 4, "rejection did not stage every preceding valid queue");
    }
};

template<class Sample>
void queue_shapes_for(sdk::int32 width) {
    Fixture<Sample> fixture(width);
    fixture.seed();
    fixture.oracle.recall(fixture.host.processor, pending_state);
    CoBatch batch;
    RawQueue<> last;
    batch.changes.set({&batch.gain, &batch.sustain, &batch.expression, &batch.command, &last});
    batch.attach(fixture.host);
    for (const auto duplicate : {kGainId, kSustainId, kExpressionId, kAllNotesOffBaseId + 3}) {
        last.set(duplicate, {});
        batch.clear_counts();
        fixture.reject("empty duplicate queue");
        require(batch.changes.queue_calls == 5 && last.id_calls == 1 &&
                last.count_calls == 0 && last.point_calls == 0 && batch.events.get_calls == 0,
                "duplicate queue was merged, traversed or passed to events");
    }
    for (const vst::ParamID unknown : {vst::ParamID{3}, kAllNotesOffBaseId - 1,
            kAllNotesOffBaseId + 16, std::numeric_limits<vst::ParamID>::max()}) {
        last.set(unknown, {});
        batch.clear_counts();
        fixture.reject("unknown raw parameter ID");
        require(batch.changes.queue_calls == 5 && last.count_calls == 0 && last.point_calls == 0,
                "unknown parameter queue was traversed");
    }
    batch.changes.values[4] = nullptr;
    batch.clear_counts();
    fixture.reject("late null queue");
    require(batch.changes.queue_calls == 5 && batch.expression.point_calls == 3 &&
            batch.command.point_calls == 1 && batch.events.get_calls == 0,
            "late null queue skipped preceding valid controls");
    for (const sdk::int32 count : {-1, 20, std::numeric_limits<sdk::int32>::max()}) {
        batch.changes.count = count;
        batch.clear_counts();
        fixture.reject("raw queue count bound");
        require(batch.changes.count_calls == 1 && batch.changes.queue_calls == 0,
                "invalid queue count traversed host queue pointers");
    }
    // An empty retry must consume the still-pending recall exactly once and
    // must not replay gain/sustain/Expression/CC123 scratch from rejected work.
    fixture.tail(false);
    require(same_state(fixture.oracle.live, pending_state) && fixture.oracle.owners.empty() &&
            fixture.oracle.cursor == frames, "empty retry failed to install the pending recall");
    batch.events.count = 1;
    batch.events.values[0] = note_on(0, 76, 80, 0.5F, 9);
    fixture.host.data.inputEvents = &batch.events;
    fixture.accept([&](std::size_t frame) {
        if (frame == 0) { fixture.oracle.strike({76, 80, 9, 0.5F}); }
    }, true);
    fixture.tail();

    // Nineteen distinct raw queues in three explicit permutations. Expression
    // and channel zero are adjacent in ascending order, exposing index overlap.
    for (std::size_t order = 0; order != 3; ++order) {
        Fixture<Sample> accepted(width);
        accepted.seed();
        std::array<RawQueue<1>, 19> queues;
        RawChanges changes;
        changes.count = 19;
        for (std::size_t index = 0; index != queues.size(); ++index) {
            const vst::ParamID id = index < 3 ? static_cast<vst::ParamID>(index) :
                kAllNotesOffBaseId + static_cast<vst::ParamID>(index - 3);
            const double value = index == 0 ? 0.75 : index == 2 ? 0.5 : 0.0;
            queues[index].set(id, {{0, value}});
            const auto source = order == 0 ? index : order == 1 ? 18 - index : (7 * index + 2) % 19;
            changes.values[index] = &queues[source];
        }
        EventList events;
        events.count = 16;
        for (sdk::int16 channel = 0; channel != 16; ++channel) {
            events.values[static_cast<std::size_t>(channel)] =
                note_on(0, 200 + channel, static_cast<sdk::int16>(60 + channel), 0.5F, channel);
        }
        accepted.host.data.inputParameterChanges = &changes;
        accepted.host.data.inputEvents = &events;
        accepted.accept([&](std::size_t frame) {
            if (frame != 0) { return; }
            accepted.oracle.controls({0.75F, false, 0.5F});
            for (sdk::int16 channel = 0; channel != 16; ++channel) { accepted.oracle.release_channel(channel); }
            for (sdk::int16 channel = 0; channel != 16; ++channel) {
                accepted.oracle.strike({200 + channel, static_cast<sdk::int16>(60 + channel), channel, 0.5F});
            }
        }, true);
        require(changes.queue_calls == 19 && events.get_calls == 16, "nineteen queues were truncated");
        for (const auto& queue : queues) { require(queue.point_calls == 1, "missing nineteenth-queue point"); }
        accepted.tail();
    }
}

template<class Sample>
void expression_points_for(sdk::int32 width) {
    Fixture<Sample> fixture(width);
    fixture.seed();
    fixture.oracle.recall(fixture.host.processor, pending_state);
    CoBatch batch;
    batch.attach(fixture.host);
    for (const std::uint64_t bits : {0xbfc0000000000000ULL, 0x8000000000000001ULL,
            0x3ff0000000000001ULL, 0x7ff8000000000001ULL, 0x7ff0000000000001ULL,
            0x7ff0000000000000ULL, 0xfff0000000000000ULL}) {
        batch.expression.values[2].value = std::bit_cast<double>(bits);
        batch.clear_counts();
        fixture.reject("final invalid Expression value");
        batch.earlier_queues();
        require(batch.expression.point_calls == 3 && batch.events.get_calls == 0,
                "final invalid Expression point did not stop before events");
    }
    batch.expression.values[2].value = 0.125;
    for (const sdk::int32 offset : {-1, 62, 128, std::numeric_limits<sdk::int32>::max()}) {
        batch.expression.values[2].offset = offset;
        batch.clear_counts();
        fixture.reject("final Expression offset/order");
        batch.earlier_queues();
        require(batch.expression.point_calls == 3, "invalid final Expression offset was not read");
    }
    batch.expression.values[2].offset = 127;
    for (int failure = 0; failure != 3; ++failure) {
        batch.expression.failed_index = failure == 0 ? 2 : -1;
        batch.expression.missing_offset = failure == 1 ? 2 : -1;
        batch.expression.missing_value = failure == 2 ? 2 : -1;
        batch.clear_counts();
        fixture.reject("failed or incomplete final Expression getter");
        batch.earlier_queues();
        require(batch.expression.point_calls == 3, "final Expression getter was not attempted");
    }
    batch.expression.failed_index = batch.expression.missing_offset = batch.expression.missing_value = -1;
    for (const sdk::int32 count : {-1, 8193, std::numeric_limits<sdk::int32>::max()}) {
        batch.expression.count = count;
        batch.clear_counts();
        fixture.reject("Expression raw point-count bound");
        batch.earlier_queues();
        require(batch.expression.point_calls == 0, "invalid Expression point count was traversed");
    }
    const auto maximum = std::make_unique<RawQueue<8192>>();
    maximum->id = kExpressionId;
    maximum->count = 8192;
    maximum->values.fill({0, 0.5});
    batch.changes.values[3] = maximum.get();
    maximum->values.back().value = std::numeric_limits<double>::infinity();
    batch.clear_counts();
    fixture.reject("8192nd Expression point is nonfinite");
    batch.earlier_queues();
    require(maximum->point_calls == 8192 && batch.events.get_calls == 0,
            "nonfinite 8192nd Expression point was not read before rejection");
    maximum->values.back().value = 0.375;
    maximum->failed_index = 8191;
    maximum->clear_counts();
    batch.clear_counts();
    fixture.reject("8192nd Expression point getter fails");
    batch.earlier_queues();
    require(maximum->point_calls == 8192 && batch.events.get_calls == 0,
            "failed 8192nd Expression getter did not preserve bounded traversal");
    maximum->failed_index = -1;
    maximum->clear_counts();
    batch.clear_counts();
    fixture.accept([&](std::size_t frame) {
        if (frame == 0) { fixture.oracle.controls({0.875F, false, 0.375F}); }
        if (frame == 1) { fixture.oracle.release(7, 60, 3); }
        if (frame == 4) { fixture.oracle.strike({99, 90, 4, 0.5F}); }
        if (frame == 32) { fixture.oracle.release_channel(3); }
    }, true);
    require(maximum->point_calls == 8192 && batch.events.get_calls == 2,
            "maximum Expression raw points or final duplicate value were lost");
    require(fixture.oracle.cursor == frames && fixture.oracle.owners.size() == 1,
            "maximum-point retry did not consume recall exactly once");
    fixture.tail();
}

template<class Sample>
void late_co_batches_for(sdk::int32 width) {
    Fixture<Sample> fixture(width);
    fixture.seed();
    fixture.oracle.recall(fixture.host.processor, pending_state);
    CoBatch batch;
    batch.attach(fixture.host);
    batch.events.count = 3;
    for (int failure = 0; failure != 7; ++failure) {
        batch.events.values[2] = note_on(127, 100, 88, 0.5F, 8);
        batch.events.failed_index = -1;
        if (failure == 0) { batch.events.values[2].noteOn.pitch = 128; }
        if (failure == 1) { batch.events.values[2].noteOn.velocity = std::numeric_limits<float>::quiet_NaN(); }
        if (failure == 2) { batch.events.failed_index = 2; }
        if (failure == 3) { batch.events.values[2].sampleOffset = 3; }
        if (failure == 4) { batch.events.values[2].sampleOffset = 128; }
        if (failure == 5) { batch.events.values[2].ppqPosition = std::numeric_limits<double>::infinity(); }
        if (failure == 6) { batch.events.values[2].type = vst::Event::kDataEvent; }
        batch.clear_counts();
        fixture.reject("late event after valid Expression/gain/sustain/CC123");
        batch.earlier_queues();
        require(batch.expression.point_calls == 3 && batch.events.get_calls == 3,
                "late event syntax rejected before the valid co-batch prefix was read");
    }
    batch.events.failed_index = -1;
    batch.events.count = 2;
    batch.changes.set({&batch.gain, &batch.sustain, &batch.expression, &batch.command});
    batch.command.set(kAllNotesOffBaseId + 3, {{32, 0.0}, {127, std::numeric_limits<double>::quiet_NaN()}});
    batch.clear_counts();
    fixture.reject("late CC123 after complete valid Expression queue");
    require(batch.expression.point_calls == 3 && batch.command.point_calls == 2 &&
            batch.events.get_calls == 0, "late command rejection did not follow valid Expression");
    batch.command.set(kAllNotesOffBaseId + 3, {{32, 0.0}});
    batch.expression.set(kExpressionId, {{0, 0.5}});
    fixture.accept([&](std::size_t frame) {
        if (frame == 0) { fixture.oracle.controls({0.875F, false, 0.5F}); }
        if (frame == 1) { fixture.oracle.release(7, 60, 3); }
        if (frame == 4) { fixture.oracle.strike({99, 90, 4, 0.5F}); }
        if (frame == 32) { fixture.oracle.release_channel(3); }
    }, true);
    require(fixture.oracle.cursor == frames && fixture.oracle.owners.size() == 1,
            "valid co-batch retry retained old owners or failed to reset cursor");
    fixture.tail();
}

template<class Sample>
void ownership_recall_for(sdk::int32 width) {
    for (const bool replace_recall : {false, true}) {
        Fixture<Sample> fixture(width);
        fixture.seed();
        fixture.oracle.recall(fixture.host.processor, pending_state);
        CoBatch batch;
        batch.gain.set(kGainId, {});
        batch.sustain.set(kSustainId, {});
        batch.expression.set(kExpressionId, {});
        batch.events.values[0] = note_on(0, 7, 72, 0.5F, 3);
        batch.events.values[1] = note_on(31, 7, 73, 0.5F, 3);
        batch.attach(fixture.host);
        for (int attempt = 0; attempt != 2; ++attempt) {
            batch.clear_counts();
            fixture.reject("identity reuse before later release after recall selection");
            require(batch.changes.queue_calls == 4 && batch.events.get_calls == 2,
                    "ownership rejection did not complete syntax preflight");
        }
        if (replace_recall) { fixture.oracle.recall(fixture.host.processor, newer_state); }
        batch.events.values[1].sampleOffset = 32;
        fixture.accept([&](std::size_t frame) {
            if (frame == 0) { fixture.oracle.strike({7, 72, 3, 0.5F}); }
            if (frame == 32) {
                fixture.oracle.release_channel(3);
                fixture.oracle.strike({7, 73, 3, 0.5F});
            }
        }, true);
        require(same_state(fixture.oracle.live, replace_recall ? newer_state : pending_state) &&
                fixture.oracle.cursor == frames && fixture.oracle.owners.size() == 1,
                "retry installed wrong recall or failed chronological identity reuse");
        batch.events.count = 1;
        batch.events.values[0] = note_off(0, 7, 60, 3);
        fixture.host.data.inputParameterChanges = nullptr;
        fixture.reject("old-pitch identity after successful recalled reuse");
        fixture.tail();
        batch.events.values[0] = note_off(0, 7, 73, 3);
        fixture.host.data.inputEvents = &batch.events;
        fixture.accept([&](std::size_t frame) {
            if (frame == 0) { fixture.oracle.release(7, 73, 3); }
        }, true);
        fixture.tail();
    }
}

template<class Sample>
void flush_recall_for(sdk::int32 width) {
    for (int form = 0; form != 3; ++form) {
        Fixture<Sample> fixture(width);
        fixture.seed();
        auto& host = fixture.host;
        CoBatch batch;
        batch.gain.set(kGainId, {{0, 0.875}, {0, 0.25}});
        batch.expression.set(kExpressionId, {{0, 0.5}, {0, 0.0}, {0, 0.125}});
        batch.sustain.set(kSustainId, {{0, 1.0}, {0, 0.0}});
        batch.command.set(kAllNotesOffBaseId + 3, {{0, 0.0}});
        batch.events.count = 0;
        batch.attach(host);
        const auto flush_buffers = [&]() {
            host.supplied();
            host.data.numSamples = 0;
            if (form == 0) { host.data.numOutputs = 0; }
            if (form == 1) { host.output.numChannels = 0; }
            // Deliberately absent sample-pointer arrays in every supplied flush.
            if constexpr (std::is_same_v<Sample, float>) { host.output.channelBuffers32 = nullptr; }
            else { host.output.channelBuffers64 = nullptr; }
        };
        flush_buffers();
        fixture.accept([&](std::size_t) {
            fixture.oracle.controls({0.25F, false, 0.125F});
            fixture.oracle.release_channel(3);
        });
        require(fixture.oracle.cursor == 256 && fixture.oracle.owners.size() == 3,
                "live flush advanced time or failed shared-pitch/anonymous releases");
        host.supplied(); host.data.numSamples = static_cast<sdk::int32>(frames);
        fixture.tail();
        fixture.oracle.recall(host.processor, newer_state);
        batch.attach(host);
        flush_buffers();
        for (int failure = 0; failure != 6; ++failure) {
            batch.expression.set(kExpressionId, {{0, 0.5}, {0, 0.0}, {0, 0.125}});
            batch.events.count = 0;
            if (failure == 0) { batch.expression.values[2].offset = 1; }
            if (failure == 1) { batch.expression.values[2].value = std::numeric_limits<double>::quiet_NaN(); }
            if (failure == 2) { batch.expression.failed_index = 2; }
            if (failure == 3) { batch.expression.count = -1; }
            if (failure == 4) { batch.expression.count = 8193; }
            if (failure == 5) {
                batch.events.count = 1;
                batch.events.values[0] = note_on(0, 88, 80, 0.5F, 9);
            }
            batch.clear_counts();
            fixture.reject("late zero-frame Expression/event rejection");
            require(batch.events.get_calls == 0, "flush rejection accessed a forbidden event body");
        }
        batch.events.count = 0;
        batch.gain.set(kGainId, {{0, 0.5}});
        batch.expression.set(kExpressionId, {{0, 0.75}, {0, 0.25}});
        fixture.accept([&](std::size_t) {
            fixture.oracle.controls({0.5F, false, 0.25F});
            fixture.oracle.release_channel(3);
        });
        require(fixture.oracle.cursor == 0 && fixture.oracle.owners.empty(),
                "flush retry did not install pending recall before final controls/releases");
        host.supplied(); host.data.numSamples = static_cast<sdk::int32>(frames);
        host.data.inputParameterChanges = nullptr;
        batch.events.count = 1;
        batch.events.values[0] = note_on(0, 88, 80, 0.5F, 9);
        fixture.accept([&](std::size_t frame) {
            if (frame == 0) { fixture.oracle.strike({88, 80, 9, 0.5F}); }
        }, true);
        fixture.tail();
    }
}

template<class Sample>
void inactive_transactions_for(sdk::int32 width) {
    Fixture<Sample> fixture(width);
    fixture.seed();
    auto& host = fixture.host;
    fixture.oracle.recall(host.processor, pending_state);
    CoBatch batch;
    batch.attach(host);
    host.data.numOutputs = 0;
    // Retain a non-null unused bus so omitted-output callbacks must preserve
    // every sample and its silence flag on both success and rejection paths.
    fixture.reject("omitted active output with pending Expression recall");
    require(batch.changes.count_calls == 0 && batch.events.get_calls == 0,
            "active-output omission traversed host controls");
    host.active(false);
    batch.expression.values[2].value = std::numeric_limits<double>::quiet_NaN();
    batch.clear_counts();
    fixture.reject("omitted inactive output with late malformed Expression");
    batch.earlier_queues();
    batch.expression.set(kExpressionId, {});
    batch.gain.set(kGainId, {});
    batch.sustain.set(kSustainId, {});
    batch.events.values[0] = note_on(0, 7, 72, 0.5F, 3);
    batch.events.values[1] = note_on(31, 7, 73, 0.5F, 3);
    fixture.reject("omitted inactive ownership rejection after recall selection");
    batch.events.values[1].sampleOffset = 32;
    batch.gain.set(kGainId, {{0, 0.75}});
    batch.sustain.set(kSustainId, {{0, 1.0}});
    batch.expression.set(kExpressionId, {{0, 0.0}});
    fixture.accept([&](std::size_t frame) {
        if (frame == 0) {
            fixture.oracle.controls({0.75F, true, 0.0F});
            fixture.oracle.strike({7, 72, 3, 0.5F});
        }
        if (frame == 32) {
            fixture.oracle.release_channel(3);
            fixture.oracle.strike({7, 73, 3, 0.5F});
        }
    });
    host.data.outputs = nullptr;
    fixture.tail(false);
    require(fixture.oracle.cursor == 256 && fixture.oracle.owners.size() == 1,
            "zero Expression with omitted output stopped synthesis or lost ownership");
    fixture.oracle.history(host.processor);
    host.active(true);
    host.supplied();
    batch.expression.set(kExpressionId, {{0, 1.0}});
    batch.changes.set({&batch.expression});
    host.data.inputParameterChanges = &batch.changes;
    fixture.accept([&](std::size_t frame) { if (frame == 0) { fixture.oracle.live.expression = 1.0F; } }, true);
    fixture.tail();

    // A supplied inactive bus permits null channel samples; cleanup must skip
    // them while retaining the negotiated silence mask and the pending recall.
    host.active(false);
    if (width == 1) { host.mono[0] = nullptr; }
    else { host.stereo[0] = host.stereo[1] = nullptr; }
    fixture.oracle.recall(host.processor, pending_state);
    batch.expression.set(kExpressionId, {{0, 0.5}, {127, 1.0000000000000002}});
    host.data.inputParameterChanges = &batch.changes;
    fixture.reject("supplied inactive null samples with late Expression error");
    batch.expression.set(kExpressionId, {{0, 0.5}});
    batch.events.count = 1;
    batch.events.values[0] = note_on(0, 91, 79, 0.5F, 2);
    host.data.inputEvents = &batch.events;
    fixture.accept([&](std::size_t frame) {
        if (frame == 0) {
            fixture.oracle.live.expression = 0.5F;
            fixture.oracle.strike({91, 79, 2, 0.5F});
        }
    }, true);
    host.mono[0] = host.left.data() + 1;
    host.stereo = {host.left.data() + 1, host.right.data() + 1};
    host.active(true);
    fixture.tail();
}

template<class Sample, class Function>
void layouts(Function function) { for (const sdk::int32 width : {1, 2}) { function.template operator()<Sample>(width); } }

void queue_shapes() {
    const auto run = []<class Sample>(sdk::int32 width) { queue_shapes_for<Sample>(width); };
    layouts<float>(run); layouts<double>(run);
}
void expression_points() {
    const auto run = []<class Sample>(sdk::int32 width) { expression_points_for<Sample>(width); };
    layouts<float>(run); layouts<double>(run);
}
void late_co_batches() {
    const auto run = []<class Sample>(sdk::int32 width) { late_co_batches_for<Sample>(width); };
    layouts<float>(run); layouts<double>(run);
}
void ownership_recall() {
    const auto run = []<class Sample>(sdk::int32 width) { ownership_recall_for<Sample>(width); };
    layouts<float>(run); layouts<double>(run);
}
void flush_recall() {
    const auto run = []<class Sample>(sdk::int32 width) { flush_recall_for<Sample>(width); };
    layouts<float>(run); layouts<double>(run);
}
void inactive_transactions() {
    const auto run = []<class Sample>(sdk::int32 width) { inactive_transactions_for<Sample>(width); };
    layouts<float>(run); layouts<double>(run);
}

using Test = void (*)();
constexpr std::array<std::pair<std::string_view, Test>, 6> tests{{
    {"queue_shapes", queue_shapes}, {"expression_points", expression_points},
    {"late_co_batches", late_co_batches}, {"ownership_recall", ownership_recall},
    {"flush_recall", flush_recall}, {"inactive_transactions", inactive_transactions}
}};
} // namespace

int main(int argc, char** argv) {
    const std::string_view selected = argc > 1 ? std::string_view{argv[1]} : "all";
    try {
        require(argc <= 2, "usage: vst3_expression_transaction_test [selector|all]");
        bool found = false;
        for (const auto& [name, test] : tests) {
            if (selected == "all" || selected == name) {
                test();
                std::cout << "PASS " << name << '\n';
                found = true;
            }
        }
        require(found, "unknown Expression transaction selector");
        std::cout << "model=" << kStateModel << " accepted=" << counts.accepted
                  << " rejected=" << counts.rejected << " scalar_frames=" << counts.samples
                  << " history_frames=" << counts.history_samples
                  << " formats=2 layouts=2 host_arithmetic_injection=unavailable\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << selected << ": " << error.what() << '\n';
        return 1;
    }
}
