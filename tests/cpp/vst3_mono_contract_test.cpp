#include "tests/cpp/vst3_test_support.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__unix__) || defined(__APPLE__)
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace {
using namespace rhodes;
using namespace rhodes::vst3;
using namespace rhodes::vst3::test_host;

// Explicit variant selection keeps the oracle independent of HostEventProcessor.
#if defined(RHODES_QUADRATURE_VST3)
using ScalarEngine = QuadratureEngine;
constexpr std::uint8_t expected_model = 3;
constexpr sdk::uint32 expected_latency = 48;
#elif defined(RHODES_FILTERED_VST3)
using ScalarEngine = AudioEngine;
constexpr std::uint8_t expected_model = 2;
constexpr sdk::uint32 expected_latency = 48;
#else
using ScalarEngine = ResearchEngine;
constexpr std::uint8_t expected_model = 1;
constexpr sdk::uint32 expected_latency = 0;
#endif
static_assert(kStateModel == expected_model && kLatencyFrames == expected_latency);
static_assert(kMaximumHostFrames == 8192);
constexpr std::size_t sample_capacity = 8193;
constexpr sdk::int32 block_frames = 128;
constexpr sdk::uint64 untouched_flags = 0x6a5b4c3d2e1f9876ULL;
std::size_t checks = 0;
std::size_t cases = 0;
std::size_t process_calls = 0;
std::size_t guarded_arrays = 0;

void check(bool condition, std::string_view message) {
    ++checks;
    if (!condition) { throw std::runtime_error(std::string{message}); }
}

template<class Sample>
bool same_bits(Sample left, Sample right) {
    using Bits = std::conditional_t<sizeof(Sample) == 4, std::uint32_t, std::uint64_t>;
    return std::bit_cast<Bits>(left) == std::bit_cast<Bits>(right);
}

sdk::tresult invoke(RhodesProcessor& processor, vst::ProcessData& data) {
    ++process_calls;
    return processor.process(data);
}

// The mono array is a real std::array<Sample*, 1>, placed immediately before
// an inaccessible page. A second pointer load faults rather than finding a
// harmless spare slot. The stereo fixture has exactly two entries as well.
// Unsupported platforms retain exact-size arrays, without claiming page guards.
template<class Sample>
class PointerEntries {
public:
    explicit PointerEntries(std::size_t count) : count_(count) {
        check(count == 1 || count == 2, "invalid pointer-array fixture width");
#if defined(_WIN32)
        SYSTEM_INFO info{};
        GetSystemInfo(&info);
        page_size_ = static_cast<std::size_t>(info.dwPageSize);
        mapping_ = VirtualAlloc(nullptr, 2 * page_size_, MEM_RESERVE | MEM_COMMIT,
                                PAGE_READWRITE);
        check(mapping_ != nullptr, "VirtualAlloc pointer guard failed");
        DWORD previous = 0;
        if (VirtualProtect(static_cast<std::byte*>(mapping_) + page_size_, page_size_,
                           PAGE_NOACCESS, &previous) == 0) {
            (void)VirtualFree(mapping_, 0, MEM_RELEASE);
            mapping_ = nullptr;
            check(false, "VirtualProtect pointer guard failed");
        }
#elif defined(__unix__) || defined(__APPLE__)
        const auto page = sysconf(_SC_PAGESIZE);
        check(page > 0, "could not determine pointer-guard page size");
        page_size_ = static_cast<std::size_t>(page);
        mapping_ = mmap(nullptr, 2 * page_size_, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (mapping_ == MAP_FAILED) { mapping_ = nullptr; }
        check(mapping_ != nullptr, "mmap pointer guard failed");
        if (mprotect(static_cast<std::byte*>(mapping_) + page_size_, page_size_,
                     PROT_NONE) != 0) {
            (void)munmap(mapping_, 2 * page_size_);
            mapping_ = nullptr;
            check(false, "mprotect pointer guard failed");
        }
#endif
        if (mapping_ != nullptr) {
            auto* end = static_cast<std::byte*>(mapping_) + page_size_;
            if (count == 1) {
                using Array = std::array<Sample*, 1>;
                entries_ = std::construct_at(reinterpret_cast<Array*>(end - sizeof(Array)))->data();
            } else {
                using Array = std::array<Sample*, 2>;
                entries_ = std::construct_at(reinterpret_cast<Array*>(end - sizeof(Array)))->data();
            }
            ++guarded_arrays;
        } else {
            fallback_ = std::make_unique<Sample*[]>(count);
            entries_ = fallback_.get();
        }
    }

    ~PointerEntries() {
#if defined(_WIN32)
        if (mapping_ != nullptr) { (void)VirtualFree(mapping_, 0, MEM_RELEASE); }
#elif defined(__unix__) || defined(__APPLE__)
        if (mapping_ != nullptr) { (void)munmap(mapping_, 2 * page_size_); }
#endif
    }
    PointerEntries(const PointerEntries&) = delete;
    PointerEntries& operator=(const PointerEntries&) = delete;
    Sample** data() noexcept { return entries_; }
    void set(std::size_t index, Sample* value) {
        check(index < count_, "fixture wrote past its pointer-array bound");
        entries_[index] = value;
    }
    void* inaccessible() const noexcept {
        if (mapping_ != nullptr) { return static_cast<std::byte*>(mapping_) + page_size_; }
        return reinterpret_cast<void*>(std::uintptr_t{1});
    }

private:
    std::size_t count_;
    std::size_t page_size_ = 0;
    void* mapping_ = nullptr;
    Sample** entries_ = nullptr;
    std::unique_ptr<Sample*[]> fallback_;
};

template<class Sample>
struct Buffers {
    static constexpr sdk::int32 sample_size = std::is_same_v<Sample, float> ?
        vst::kSample32 : vst::kSample64;
    std::array<std::array<Sample, sample_capacity + 2>, 3> samples{};
    PointerEntries<Sample> mono{1};
    PointerEntries<Sample> stereo{2};
    std::array<vst::AudioBusBuffers, 2> buses{};

    void restore(int width, unsigned supplied) {
        for (auto& channel : samples) {
            channel.fill(Sample{7});
            channel.front() = Sample{-123};
            channel.back() = Sample{123};
        }
        mono.set(0, (supplied & 1U) != 0U ? samples[0].data() + 1 : nullptr);
        for (std::size_t channel = 0; channel < 2; ++channel) {
            stereo.set(channel, (supplied & (1U << channel)) != 0U ?
                       samples[channel].data() + 1 : nullptr);
        }
        for (auto& bus : buses) {
            bus = {};
            bus.numChannels = width;
            bus.silenceFlags = untouched_flags;
            set_entries(bus, width == 1 ? mono.data() : stereo.data());
        }
    }

    static void set_entries(vst::AudioBusBuffers& bus, Sample** entries) {
        if constexpr (std::is_same_v<Sample, float>) { bus.channelBuffers32 = entries; }
        else { bus.channelBuffers64 = entries; }
    }

    void poison_entries() { set_entries(buses[0], static_cast<Sample**>(mono.inaccessible())); }
    vst::AudioBusBuffers* poison_bus() const {
        return static_cast<vst::AudioBusBuffers*>(mono.inaccessible());
    }

    void guards_and_tail(std::size_t frames) const {
        check(frames <= sample_capacity, "invalid sample-canary check extent");
        for (const auto& channel : samples) {
            check(channel.front() == Sample{-123} && channel.back() == Sample{123},
                  "sample guard changed");
            check(std::all_of(channel.begin() + static_cast<std::ptrdiff_t>(frames + 1),
                              channel.end() - 1, [](Sample value) { return value == Sample{7}; }),
                  "write exceeded the declared sample count");
        }
        check(buses[1].silenceFlags == untouched_flags, "processing touched the unused second bus");
    }

    void cleared(std::size_t frames, unsigned mask) const {
        guards_and_tail(frames);
        for (std::size_t channel = 0; channel < samples.size(); ++channel) {
            const Sample expected = (mask & (1U << channel)) != 0U ? Sample{0} : Sample{7};
            check(std::all_of(samples[channel].begin() + 1,
                              samples[channel].begin() + static_cast<std::ptrdiff_t>(frames + 1),
                              [expected](Sample value) { return same_bits(value, expected); }),
                  "cleanup cleared the wrong sample region");
        }
    }

    void unchanged() const { cleared(0, 0); }

    void matches(std::span<const Sample> expected, unsigned supplied, int width) const {
        guards_and_tail(expected.size());
        for (std::size_t channel = 0; channel < samples.size(); ++channel) {
            for (std::size_t frame = 0; frame < expected.size(); ++frame) {
                const auto value = (supplied & (1U << channel)) != 0U ? expected[frame] : Sample{7};
                check(same_bits(samples[channel][frame + 1], value),
                      "output differs from exact scalar oracle or touched an absent channel");
            }
        }
        const bool nonzero = std::any_of(expected.begin(), expected.end(),
                                        [](Sample value) { return value != Sample{0}; });
        const sdk::uint64 mask = width == 1 ? 1U : 3U;
        check(buses[0].silenceFlags == (nonzero ? 0U : mask), "rendered silence mask is incorrect");
    }
};

template<class Sample>
struct Host {
    RhodesProcessor processor;
    Buffers<Sample> buffers;
    vst::ProcessData data{};
    int width = 2;
    bool initialized = false;

    explicit Host(int requested_width = 2, bool output_active = true,
                  bool configure = true, double rate = 48000.0) : width(requested_width) {
        check(processor.initialize(nullptr) == sdk::kResultOk, "initialize mono-test host");
        initialized = true;
        if (width == 1) { arrange(1); }
        check(processor.activateBus(vst::kAudio, vst::kOutput, 0, output_active) == sdk::kResultOk,
              "set output-bus activity");
        if (configure) { setup(rate); }
        restore();
    }
    ~Host() { if (initialized) { (void)processor.terminate(); } }
    Host(const Host&) = delete;
    Host& operator=(const Host&) = delete;

    void setup(double rate = 48000.0) {
        vst::ProcessSetup setup{vst::kRealtime, Buffers<Sample>::sample_size, block_frames, rate};
        check(processor.setupProcessing(setup) == sdk::kResultOk, "configure mono-test host");
        check(processor.setActive(true) == sdk::kResultOk, "activate mono-test component");
        check(processor.setProcessing(true) == sdk::kResultOk, "start mono-test processor");
    }
    void arrange(int requested_width) {
        auto layout = requested_width == 1 ? vst::SpeakerArr::kMono : vst::SpeakerArr::kStereo;
        check(processor.setBusArrangements(nullptr, 0, &layout, 1) == sdk::kResultOk,
              "valid output negotiation rejected");
        width = requested_width;
    }
    void restore(sdk::int32 frames = block_frames, unsigned supplied = 7) {
        const auto mask = supplied == 7 ? (width == 1 ? 1U : 3U) : supplied;
        buffers.restore(width, mask);
        data = {};
        data.processMode = vst::kRealtime;
        data.symbolicSampleSize = Buffers<Sample>::sample_size;
        data.numSamples = frames;
        data.numOutputs = 1;
        data.outputs = buffers.buses.data();
    }
};

struct Queue final : FixedHostObject<vst::IParamValueQueue> {
    struct Point { sdk::int32 offset; double value; };
    std::array<Point, 8> points{};
    vst::ParamID id;
    sdk::int32 count = 0;
    explicit Queue(vst::ParamID identity) : id(identity) {}
    vst::ParamID PLUGIN_API getParameterId() override { return id; }
    sdk::int32 PLUGIN_API getPointCount() override { return count; }
    sdk::tresult PLUGIN_API getPoint(sdk::int32 index, sdk::int32& offset, double& value) override {
        if (index < 0 || index >= count || static_cast<std::size_t>(index) >= points.size()) {
            return sdk::kResultFalse;
        }
        const auto point = points[static_cast<std::size_t>(index)];
        offset = point.offset;
        value = point.value;
        return sdk::kResultOk;
    }
    sdk::tresult PLUGIN_API addPoint(sdk::int32, double, sdk::int32&) override {
        return sdk::kNotImplemented;
    }
    void set(std::initializer_list<Point> values) {
        check(values.size() <= points.size(), "too many points for bounded test queue");
        count = static_cast<sdk::int32>(values.size());
        std::copy(values.begin(), values.end(), points.begin());
    }
};

struct Script final : FixedHostObject<vst::IParameterChanges> {
    // Literal public IDs deliberately do not use the adapter's ID mapping.
    Queue gain{0}, sustain{1}, expression{2}, release0{0x1000}, release1{0x1001};
    std::array<Queue*, 5> queues{&release1, &expression, &gain, &release0, &sustain};
    EventList events;
    sdk::int32 PLUGIN_API getParameterCount() override { return 5; }
    vst::IParamValueQueue* PLUGIN_API getParameterData(sdk::int32 index) override {
        return index >= 0 && index < 5 ? queues[static_cast<std::size_t>(index)] : nullptr;
    }
    vst::IParamValueQueue* PLUGIN_API addParameterData(const vst::ParamID&, sdk::int32&) override {
        return nullptr;
    }
};

struct Packet {
    std::array<std::uint8_t, 20> bytes{};
    std::size_t size = 0;
};

Packet literal_state(InstrumentState state) {
    Packet packet{{'R', 'H', 'S', 'T', 1, 0, expected_model, 0}, 16};
    if (state.expression != 1.0F) { packet.bytes[4] = 2; packet.size = 20; }
    const auto word = [&](std::size_t at, float value) {
        const auto bits = std::bit_cast<std::uint32_t>(value);
        for (std::size_t byte = 0; byte < 4; ++byte) {
            packet.bytes[at + byte] = static_cast<std::uint8_t>(bits >> (8U * byte));
        }
    };
    word(8, state.monitoring_gain);
    word(12, state.sustain ? 1.0F : 0.0F);
    if (packet.size == 20) { word(16, state.expression); }
    return packet;
}

void expect_saved(RhodesProcessor& processor, InstrumentState expected) {
    const auto packet = literal_state(expected);
    MemoryStream stream;
    check(processor.getState(&stream) == sdk::kResultOk, "getState failed");
    check(stream.extent == static_cast<sdk::int64>(packet.size) &&
          stream.position == static_cast<sdk::int64>(packet.size), "saved state has wrong exact extent");
    check(std::equal(packet.bytes.begin(), packet.bytes.begin() +
                     static_cast<std::ptrdiff_t>(packet.size), stream.bytes.begin()),
          "saved bytes differ from independently assembled configuration");
}

void recall(RhodesProcessor& processor, InstrumentState state) {
    const auto packet = literal_state(state);
    MemoryStream stream;
    std::copy(packet.bytes.begin(), packet.bytes.end(), stream.bytes.begin());
    stream.extent = static_cast<sdk::int64>(packet.size);
    check(processor.setState(&stream) == sdk::kResultOk, "literal recall packet rejected");
}

using Curve = std::map<sdk::int32, long double>;
Curve curve(const Queue& queue) {
    Curve result;
    for (sdk::int32 index = 0; index < queue.count; ++index) {
        const auto& point = queue.points[static_cast<std::size_t>(index)];
        result[point.offset] = static_cast<float>(point.value);
    }
    return result;
}

double interpolated(const Curve& points, float previous, sdk::int32 frame) {
    if (points.empty()) { return previous; }
    const auto right = points.lower_bound(frame);
    if (right == points.end()) { return static_cast<double>(points.rbegin()->second); }
    const auto left = right == points.begin() ?
        std::pair<sdk::int32, long double>{-1, previous} :
        std::pair<sdk::int32, long double>{std::prev(right)->first, std::prev(right)->second};
    if (right->first == frame) { return static_cast<double>(right->second); }
    // Barycentric interpolation is independent of the adapter's incremental
    // segment expression. All exercised denominators are powers of two, so
    // these dyadic curves admit exact binary64/host-format comparisons.
    return static_cast<double>((left.second * static_cast<long double>(right->first - frame) +
        right->second * static_cast<long double>(frame - left.first)) /
        static_cast<long double>(right->first - left.first));
}

struct ScalarModel {
    ScalarEngine engine;
    InstrumentState state{};
    std::uint64_t cursor = 0;
    std::map<std::pair<sdk::int16, sdk::int32>, sdk::int16> owners;

    explicit ScalarModel(double rate = 48000.0) {
        check(engine.prepare(rate), "prepare independent scalar engine");
    }
    void reset(InstrumentState recalled) {
        engine.reset();
        engine.set_sustain(recalled.sustain);
        owners.clear();
        cursor = 0;
        state = recalled;
    }
    std::size_t holders(sdk::int16 pitch) const {
        return static_cast<std::size_t>(std::count_if(owners.begin(), owners.end(),
            [pitch](const auto& owner) { return owner.second == pitch; }));
    }
    void release_channel(sdk::int16 channel) {
        std::array<bool, 73> previously_held{};
        for (const auto& [identity, pitch] : owners) {
            (void)identity;
            previously_held[static_cast<std::size_t>(pitch - 28)] = true;
        }
        std::erase_if(owners, [channel](const auto& owner) { return owner.first.first == channel; });
        for (std::size_t key = 0; key < previously_held.size(); ++key) {
            if (previously_held[key] && holders(static_cast<sdk::int16>(key + 28)) == 0) {
                check(engine.note_off(key), "independent CC123 physical release failed");
            }
        }
    }
    void note(const vst::Event& event) {
        const bool on = event.type == vst::Event::kNoteOnEvent && event.noteOn.velocity != 0.0F;
        const auto pitch = event.type == vst::Event::kNoteOnEvent ? event.noteOn.pitch : event.noteOff.pitch;
        const auto identity = event.type == vst::Event::kNoteOnEvent ?
            std::pair{event.noteOn.channel, event.noteOn.noteId} :
            std::pair{event.noteOff.channel, event.noteOff.noteId};
        if (on) {
            owners[identity] = pitch;
            check(engine.note_on(static_cast<std::size_t>(pitch - 28), event.noteOn.velocity),
                  "independent scalar strike failed");
        } else {
            const auto found = owners.find(identity);
            if (found == owners.end()) { return; }
            check(found->second == pitch, "oracle fixture changed identified pitch");
            owners.erase(found);
            if (holders(pitch) == 0) {
                check(engine.note_off(static_cast<std::size_t>(pitch - 28)), "scalar release failed");
            }
        }
    }
    void controls_and_notes(const Script& script, const Curve& pedal, sdk::int32 frame) {
        if (const auto found = pedal.find(frame); found != pedal.end()) {
            state.sustain = found->second >= 0.5L;
            engine.set_sustain(state.sustain);
        }
        for (const auto* command : {&script.release0, &script.release1}) {
            for (sdk::int32 index = 0; index < command->count; ++index) {
                if (command->points[static_cast<std::size_t>(index)].offset == frame) {
                    release_channel(command == &script.release0 ? 0 : 1);
                }
            }
        }
        for (sdk::int32 index = 0; index < script.events.count; ++index) {
            const auto& event = script.events.values[static_cast<std::size_t>(index)];
            if (event.sampleOffset == frame) { note(event); }
        }
    }
    template<class Sample>
    std::vector<Sample> advance(const Script& script, sdk::int32 frames) {
        const auto gains = curve(script.gain);
        const auto expressions = curve(script.expression);
        const auto pedal = curve(script.sustain);
        const auto initial = state;
        std::vector<Sample> expected(static_cast<std::size_t>(frames));
        if (frames == 0) {
            if (!gains.empty()) { state.monitoring_gain = static_cast<float>(gains.rbegin()->second); }
            if (!expressions.empty()) { state.expression = static_cast<float>(expressions.rbegin()->second); }
            controls_and_notes(script, pedal, 0);
            return expected;
        }
        for (sdk::int32 frame = 0; frame < frames; ++frame) {
            controls_and_notes(script, pedal, frame);
            double volts = 0.0;
            check(engine.process(std::span<double>(&volts, 1)), "scalar engine failed during oracle render");
            const double gain = interpolated(gains, initial.monitoring_gain, frame);
            const double expression = interpolated(expressions, initial.expression, frame);
            // This is the old left scalar with one final host-format cast.
            // The stereo peer is an additional check, not the audio oracle.
            const double monitored = (32.0 * gain) * volts;
            const double scalar = expression == 1.0 ? monitored : monitored * expression;
            expected[static_cast<std::size_t>(frame)] = static_cast<Sample>(scalar);
            state.monitoring_gain = static_cast<float>(gain);
            state.expression = static_cast<float>(expression);
            ++cursor;
        }
        return expected;
    }
    void verify(RhodesProcessor& processor) const {
        check(processor.current_state() == state && processor.published_state() == state,
              "live or published configuration differs from independent model");
        check(processor.engine_processor().frame_cursor() == cursor, "frame cursor differs from model");
        check(processor.note_tracker().active_count() == owners.size(), "note-owner total differs from model");
        for (std::size_t key = 0; key < 73; ++key) {
            check(processor.note_tracker().held_count(key) == holders(static_cast<sdk::int16>(key + 28)),
                  "per-key ownership differs from model");
        }
        check(!processor.requires_reset(), "valid mono transaction latched a fault");
        same_mechanics(processor.engine_processor().engine(), engine);
        expect_saved(processor, state);
    }
};

void full_snapshot(const ProcessorSnapshot& before, const RhodesProcessor& processor) {
    before.expect_unchanged(processor);
    // Include the complete 96-frame FIR memory and its boundary, even when a
    // pending recall would otherwise hide an accidental history mutation.
    auto actual = std::make_unique<ScalarEngine>(processor.engine_processor().engine());
    auto expected = std::make_unique<ScalarEngine>(before.engine.engine());
    std::array<double, 97> left{}, right{};
    check(actual->process(left) && expected->process(right), "snapshot trajectory check failed");
    check(std::equal(left.begin(), left.end(), right.begin(), same_bits<double>),
          "rejection or negotiation changed future scalar/filter trajectory");
}

bool name_is(const vst::String128 name, std::u16string_view expected) {
    if (expected.size() >= 128) { return false; }
    for (std::size_t index = 0; index < expected.size(); ++index) {
        if (name[index] != expected[index]) { return false; }
    }
    return name[expected.size()] == 0;
}

void metadata(RhodesProcessor& processor, int width) {
    check(processor.getBusCount(vst::kAudio, vst::kInput) == 0 &&
          processor.getBusCount(vst::kAudio, vst::kOutput) == 1,
          "audio bus counts changed");
    check(processor.getBusCount(vst::kEvent, vst::kInput) == 1 &&
          processor.getBusCount(vst::kEvent, vst::kOutput) == 0, "event bus counts changed");
    vst::BusInfo info{};
    check(processor.getBusInfo(vst::kAudio, vst::kOutput, 0, info) == sdk::kResultOk,
          "output bus metadata unavailable");
    check(info.channelCount == width && info.mediaType == vst::kAudio &&
          info.direction == vst::kOutput && info.busType == vst::kMain &&
          (info.flags & vst::BusInfo::kDefaultActive) != 0,
          "output metadata disagrees with negotiated width/type");
    check(name_is(info.name, width == 1 ? u"Mono Out" : u"Stereo Out"), "output bus name is incoherent");
    vst::SpeakerArrangement layout = vst::SpeakerArr::kEmpty;
    check(processor.getBusArrangement(vst::kOutput, 0, layout) == sdk::kResultOk &&
          layout == (width == 1 ? vst::SpeakerArr::kMono : vst::SpeakerArr::kStereo),
          "SDK speaker arrangement disagrees with bus metadata");
    check(processor.getBusInfo(vst::kEvent, vst::kInput, 0, info) == sdk::kResultOk &&
          info.channelCount == 16 && name_is(info.name, u"MIDI In"), "MIDI input changed with layout");
    check(processor.getLatencySamples() == expected_latency &&
          processor.getTailSamples() == vst::kInfiniteTail, "layout changed latency or tail declaration");
}

void phase(Script& script, int index) {
    for (auto* queue : script.queues) { queue->count = 0; }
    script.events.count = 0;
    switch (index) {
    case 0:
        script.events.count = 2;
        script.events.values[0] = note_on(0, 101, 60, 0.875F, 0);
        script.events.values[1] = note_on(7, 202, 67, 0.5F, 1);
        break;
    case 1:
        script.gain.set({{31, 0.25}, {63, 0.75}, {127, 0.5}});
        script.sustain.set({{20, 1.0}});
        script.release0.set({{110, 0.0}});
        script.events.count = 4;
        script.events.values[0] = note_off(36, 101, 60, 0);
        script.events.values[1] = note_on(55, 202, 67, 0.375F, 1);
        script.events.values[2] = note_on(72, 303, 60, 0.625F, 0);
        script.events.values[3] = note_off(94, 303, 60, 0);
        break;
    case 2:
        script.gain.set({{31, 0.625}, {95, 0.125}});
        script.expression.set({{0, 0.0}});
        script.release1.set({{16, 0.0}});
        script.events.count = 2;
        script.events.values[0] = note_on(48, 404, 72, 0.75F, 1);
        script.events.values[1] = note_off(80, 404, 72, 1);
        break;
    case 3:
        script.expression.set({{0, 1.0}});
        script.sustain.set({{0, 0.0}});
        break;
    case 4:
        script.gain.set({{31, 0.5}, {63, 0.25}, {127, 0.75}});
        script.expression.set({{31, 0.5}, {31, 0.25}, {63, 0.5}, {127, 1.0}});
        break;
    default: break;
    }
}

template<class Sample>
std::vector<Sample> accepted(Host<Sample>& host, ScalarModel& model, Script& script,
                             sdk::int32 frames = block_frames, unsigned supplied = 7,
                             int omission = 0) {
    host.restore(frames, supplied);
    host.data.inputParameterChanges = &script;
    host.data.inputEvents = &script.events;
    if (omission != 0) {
        host.data.numOutputs = 0;
        if (omission == 1) { host.data.outputs = nullptr; }
        if (omission == 3) { host.data.outputs = host.buffers.poison_bus(); }
    }
    auto expected = model.advance<Sample>(script, frames);
    check(invoke(host.processor, host.data) == sdk::kResultOk, "valid mono/stereo block rejected");
    if (omission != 0) {
        host.buffers.unchanged();
        check(host.buffers.buses[0].silenceFlags == untouched_flags,
              "omitted output changed a caller silence flag");
    } else {
        const auto mask = supplied == 7 ? (host.width == 1 ? 1U : 3U) : supplied;
        host.buffers.matches(expected, mask, host.width);
    }
    model.verify(host.processor);
    return expected;
}

template<class Sample>
void negotiation_cases() {
    {
        auto processor = std::make_unique<RhodesProcessor>();
        for (auto layout : {vst::SpeakerArr::kMono, vst::SpeakerArr::kStereo}) {
            check(processor->setBusArrangements(nullptr, 0, &layout, 1) != sdk::kResultOk,
                  "uninitialized negotiation accepted");
        }
    }
    for (bool active : {false, true}) {
        auto host = std::make_unique<Host<Sample>>(2, active);
        metadata(host->processor, 2); // No stereo negotiation preceded this read.
        check(host->processor.canProcessSampleSize(vst::kSample32) == sdk::kResultOk &&
              host->processor.canProcessSampleSize(vst::kSample64) == sdk::kResultOk,
              "supported sample formats changed");
        for (int width : {1, 2}) {
            ++cases;
            check(host->processor.setProcessing(false) == sdk::kResultOk &&
                  host->processor.setActive(false) == sdk::kResultOk, "suspend for arrangement negotiation");
            host->arrange(width);
            const InstrumentState pending{0.25F, true, 0.75F};
            recall(host->processor, pending);
            const ProcessorSnapshot before{host->processor};
            const bool prior_activity = host->processor.getAudioOutput(0)->isActive();
            for (int invalid = 0; invalid < 14; ++invalid) {
                std::array<vst::SpeakerArrangement, 2> layouts{
                    vst::SpeakerArr::kStereo, vst::SpeakerArr::kStereo};
                sdk::int32 inputs = 0;
                sdk::int32 outputs = 1;
                auto* requested = layouts.data();
                switch (invalid) {
                case 0: inputs = -1; break;
                case 1: inputs = 1; break;
                case 2: inputs = std::numeric_limits<sdk::int32>::max(); break;
                case 3: outputs = -1; break;
                case 4: outputs = 0; break;
                case 5: outputs = 2; break;
                case 6: outputs = std::numeric_limits<sdk::int32>::max(); break;
                case 7: requested = nullptr; break;
                case 8: layouts[0] = vst::SpeakerArr::kEmpty; break;
                case 9: layouts[0] = vst::kSpeakerL; break;
                case 10: layouts[0] = vst::kSpeakerC; break;
                case 11: layouts[0] = vst::kSpeakerL | vst::kSpeakerC; break;
                case 12: layouts[0] = vst::kSpeakerLs | vst::kSpeakerRs; break;
                case 13: layouts[0] = vst::SpeakerArr::k51; break;
                default: break;
                }
                check(host->processor.setBusArrangements(nullptr, inputs, requested, outputs) != sdk::kResultOk,
                      "unsupported negotiation accepted");
                metadata(host->processor, width);
                full_snapshot(before, host->processor);
                expect_saved(host->processor, pending);
                check((host->processor.getAudioOutput(0)->isActive() != 0) == prior_activity,
                      "rejected arrangement changed bus activation");
            }
            host->arrange(width == 1 ? 2 : 1);
            metadata(host->processor, host->width);
            full_snapshot(before, host->processor);
            check((host->processor.getAudioOutput(0)->isActive() != 0) == active,
                  "accepted arrangement changed bus activation");
            expect_saved(host->processor, pending);
            check(host->processor.setActive(true) == sdk::kResultOk &&
                  host->processor.setProcessing(true) == sdk::kResultOk, "resume after negotiation");
            auto model = std::make_unique<ScalarModel>();
            model->reset(pending);
            Script script;
            phase(script, 0);
            (void)accepted(*host, *model, script);
        }
    }
}

template<class Sample>
void scalar_cases() {
    for (double rate : {32000.0, 48000.0, 96000.0, 192000.0}) {
        ++cases;
        auto mono = std::make_unique<Host<Sample>>(1, true, true, rate);
        auto stereo = std::make_unique<Host<Sample>>(2, true, true, rate);
        auto model = std::make_unique<ScalarModel>(rate);
        Script script;
        for (int block = 0; block < 7; ++block) {
            phase(script, block);
            const auto expected = accepted(*mono, *model, script);
            stereo->restore();
            stereo->data.inputParameterChanges = &script;
            stereo->data.inputEvents = &script.events;
            check(invoke(stereo->processor, stereo->data) == sdk::kResultOk, "stereo scalar peer rejected");
            stereo->buffers.matches(expected, 3, 2);
            model->verify(stereo->processor);
            for (std::size_t frame = 1; frame <= static_cast<std::size_t>(block_frames); ++frame) {
                check(same_bits(mono->buffers.samples[0][frame], stereo->buffers.samples[0][frame]) &&
                      same_bits(stereo->buffers.samples[0][frame], stereo->buffers.samples[1][frame]),
                      "mono is not exactly the previous left scalar or stereo differs");
            }
            if (block == 0 || block == 3) {
                check(std::any_of(expected.begin(), expected.end(), [](Sample value) { return value != Sample{0}; }),
                      "scalar note/tail fixture was vacuous");
            }
            if (block == 2) {
                check(std::all_of(expected.begin(), expected.end(), [](Sample value) { return value == Sample{0}; }),
                      "zero expression did not silence the scalar");
                check(model->engine.mechanical_energy_joules() > 0.0 && model->owners.empty(),
                      "muted sustain/CC123 fixture did not retain released mechanical energy");
            }
        }
    }
}

template<class Sample>
void pointer_cases() {
    for (int width : {1, 2}) {
        const unsigned full_mask = width == 1 ? 1U : 3U;
        for (bool active : {false, true}) {
            for (unsigned mask = 0; mask <= full_mask; ++mask) {
                ++cases;
                auto host = std::make_unique<Host<Sample>>(width, active);
                auto model = std::make_unique<ScalarModel>();
                Script script;
                phase(script, 0);
                if (active && mask != full_mask) {
                    host->restore(block_frames, mask);
                    host->data.inputEvents = &script.events;
                    const ProcessorSnapshot before{host->processor};
                    check(invoke(host->processor, host->data) != sdk::kResultOk,
                          "active null sample pointer accepted");
                    host->buffers.cleared(static_cast<std::size_t>(block_frames), mask);
                    check(host->buffers.buses[0].silenceFlags == full_mask,
                          "active-null cleanup mask incorrect");
                    full_snapshot(before, host->processor);
                    (void)accepted(*host, *model, script);
                } else {
                    (void)accepted(*host, *model, script, block_frames, mask);
                    phase(script, 1);
                    (void)accepted(*host, *model, script);
                }
            }
            ++cases;
            auto host = std::make_unique<Host<Sample>>(width, active);
            host->restore();
            Buffers<Sample>::set_entries(host->buffers.buses[0], nullptr);
            const ProcessorSnapshot before{host->processor};
            check(invoke(host->processor, host->data) != sdk::kResultOk,
                  "supplied active/inactive bus without pointer array accepted");
            host->buffers.unchanged();
            check(host->buffers.buses[0].silenceFlags == full_mask, "null-array cleanup mask incorrect");
            full_snapshot(before, host->processor);
        }
    }
}

template<class Sample>
void omission_cases() {
    for (int width : {1, 2}) {
        for (int omission : {1, 2, 3}) {
            ++cases;
            auto host = std::make_unique<Host<Sample>>(width, false);
            auto model = std::make_unique<ScalarModel>();
            Script script;
            bool omitted_nonzero = false;
            for (int block = 0; block < 4; ++block) {
                phase(script, block);
                const auto expected = accepted(*host, *model, script, block_frames, 7, omission);
                omitted_nonzero = omitted_nonzero || std::any_of(expected.begin(), expected.end(),
                    [](Sample value) { return value != Sample{0}; });
                if (block == 0) { check(model->owners.size() == 2, "omitted note-on coverage was empty"); }
                if (block == 1) {
                    check(model->state.sustain && model->owners.size() == 1,
                          "omitted sustain/retrigger/release coverage was empty");
                }
                if (block == 2) {
                    check(model->owners.empty() && model->state.expression == 0.0F &&
                          model->engine.mechanical_energy_joules() > 0.0,
                          "omitted CC123/mute did not leave a ringing released system");
                }
            }
            check(omitted_nonzero && model->cursor == 512 && model->owners.empty(),
                  "omitted-output timeline did not advance the full independent model");
            check(host->processor.activateBus(vst::kAudio, vst::kOutput, 0, true) == sdk::kResultOk,
                  "reactivate omitted output");
            phase(script, 5);
            const auto audible_tail = accepted(*host, *model, script);
            check(std::any_of(audible_tail.begin(), audible_tail.end(),
                              [](Sample value) { return value != Sample{0}; }),
                  "omitted-output continuation had no audible release tail");

            // A malformed late point still rejects before consuming recall,
            // even with no usable output address for rejection cleanup.
            check(host->processor.activateBus(vst::kAudio, vst::kOutput, 0, false) == sdk::kResultOk,
                  "deactivate for malformed omission");
            const InstrumentState pending{0.375F, true, 0.625F};
            recall(host->processor, pending);
            const ProcessorSnapshot before{host->processor};
            host->restore();
            host->data.numOutputs = 0;
            host->data.outputs = host->buffers.poison_bus();
            script.expression.set({{0, 0.25}, {127, std::numeric_limits<double>::quiet_NaN()}});
            host->data.inputParameterChanges = &script;
            check(invoke(host->processor, host->data) != sdk::kResultOk, "malformed omitted transaction accepted");
            host->buffers.unchanged();
            check(host->buffers.buses[0].silenceFlags == untouched_flags, "omitted rejection touched flags");
            full_snapshot(before, host->processor);
            model->reset(pending);
            phase(script, 0);
            (void)accepted(*host, *model, script, block_frames, 7, omission);
        }
    }
}

template<class Sample>
void flush_cases() {
    for (int width : {1, 2}) {
        for (bool configured : {false, true}) {
            for (int shape : {0, 1, 2}) {
                for (sdk::int32 format : {vst::kSample32, vst::kSample64}) {
                    ++cases;
                    auto host = std::make_unique<Host<Sample>>(width, true, configured);
                    auto model = std::make_unique<ScalarModel>();
                    Script script;
                    if (configured) { phase(script, 0); (void)accepted(*host, *model, script); }
                    phase(script, 5);
                    script.gain.set({{0, 0.125}, {0, 0.375}});
                    script.expression.set({{0, 0.25}, {0, 0.625}});
                    script.sustain.set({{0, 1.0}});
                    script.release0.set({{0, 0.0}});
                    const auto cursor = model->cursor;
                    host->restore(0);
                    host->data.symbolicSampleSize = format;
                    host->data.processMode = shape == 0 ? vst::kRealtime :
                                             shape == 1 ? vst::kPrefetch : vst::kOffline;
                    host->data.inputParameterChanges = &script;
                    if (shape == 0) {
                        host->data.numOutputs = 0;
                        host->data.outputs = host->buffers.poison_bus();
                    } else {
                        host->buffers.buses[0].numChannels = shape == 1 ? 0 : width;
                        host->buffers.poison_entries();
                    }
                    (void)model->advance<Sample>(script, 0);
                    check(invoke(host->processor, host->data) == sdk::kResultOk,
                          "valid zero-frame shape/format/mode rejected");
                    host->buffers.unchanged();
                    const sdk::uint64 flags = shape == 0 ? untouched_flags :
                        shape == 1 ? 0U : width == 1 ? 1U : 3U;
                    check(host->buffers.buses[0].silenceFlags == flags, "zero-frame mask incorrect");
                    check(host->processor.engine_processor().frame_cursor() == cursor, "flush advanced audio time");
                    model->verify(host->processor);
                    if (!configured) {
                        host->setup();
                        model->reset(model->state);
                    }
                    phase(script, 5);
                    (void)accepted(*host, *model, script);
                }
            }
        }
    }
}

template<class Sample>
void rejection_cases() {
    for (int width : {1, 2}) {
        for (bool pending : {false, true}) {
            ++cases;
            auto host = std::make_unique<Host<Sample>>(width);
            auto model = std::make_unique<ScalarModel>();
            Script script;
            phase(script, 0);
            (void)accepted(*host, *model, script);
            const InstrumentState recalled{0.25F, true, 0.5F};
            if (pending) { recall(host->processor, recalled); }
            const ProcessorSnapshot before{host->processor};
            phase(script, 1);
            host->restore();
            host->data.numOutputs = 0;
            host->data.outputs = host->buffers.poison_bus();
            host->data.inputParameterChanges = &script;
            host->data.inputEvents = &script.events;
            check(invoke(host->processor, host->data) != sdk::kResultOk, "active omitted output accepted");
            host->buffers.unchanged();
            check(host->buffers.buses[0].silenceFlags == untouched_flags, "active omission touched flags");
            full_snapshot(before, host->processor);
            if (pending) { model->reset(recalled); }
            phase(script, 0);
            (void)accepted(*host, *model, script);
        }

        auto host = std::make_unique<Host<Sample>>(width);
        auto model = std::make_unique<ScalarModel>();
        Script script;
        phase(script, 0);
        (void)accepted(*host, *model, script);
        const InstrumentState pending{0.375F, true, 0.75F};
        recall(host->processor, pending);
        const ProcessorSnapshot before{host->processor};
        const int other_width = width == 1 ? 2 : 1;
        for (int declared : {1, 2}) {
            const auto full_mask = declared == 1 ? 1U : 3U;
            for (unsigned mask = 0; mask <= full_mask; ++mask) {
                for (sdk::int32 frames : {0, 16, 8192, 8193}) {
                    // At 0/16 frames, rejection comes specifically from layout
                    // mismatch; at 8192/8193 it comes from block-size limits.
                    if (frames < 8192 && declared != other_width) { continue; }
                    ++cases;
                    host->restore(frames);
                    host->buffers.restore(declared, mask);
                    if (frames == 0) { host->buffers.poison_entries(); }
                    check(invoke(host->processor, host->data) != sdk::kResultOk,
                          "layout mismatch or oversized block accepted");
                    const auto cleared = frames > 0 && frames <= 8192 ?
                        static_cast<std::size_t>(frames) : 0U;
                    host->buffers.cleared(cleared, frames == 0 || frames > 8192 ? 0U : mask);
                    check(host->buffers.buses[0].silenceFlags ==
                          (frames <= 8192 ? full_mask : untouched_flags),
                          "cleanup used negotiated width instead of supported declared width");
                    full_snapshot(before, host->processor);
                }
            }
        }
        for (sdk::int32 declared : {-1, 0, 3, std::numeric_limits<sdk::int32>::max()}) {
            ++cases;
            host->restore(16);
            host->buffers.buses[0].numChannels = declared;
            host->buffers.poison_entries();
            check(invoke(host->processor, host->data) != sdk::kResultOk, "unsupported channel count accepted");
            host->buffers.unchanged();
            check(host->buffers.buses[0].silenceFlags == untouched_flags,
                  "unsupported channel declaration changed flags");
            full_snapshot(before, host->processor);
        }
        for (int invalid = 0; invalid < 7; ++invalid) {
            ++cases;
            host->restore(16);
            host->buffers.poison_entries();
            switch (invalid) {
            case 0: host->data.numSamples = -1; break;
            case 1: host->data.numSamples = std::numeric_limits<sdk::int32>::max(); break;
            case 2: host->data.numOutputs = -1; break;
            case 3: host->data.numOutputs = 2; break;
            case 4: host->data.numOutputs = std::numeric_limits<sdk::int32>::max(); break;
            case 5: host->data.outputs = nullptr; break;
            case 6: host->data.symbolicSampleSize = 99; break;
            default: break;
            }
            check(invoke(host->processor, host->data) != sdk::kResultOk, "unsupported cleanup dimensions accepted");
            host->buffers.unchanged();
            check(host->buffers.buses[0].silenceFlags == untouched_flags, "unsupported dimensions changed flags");
            full_snapshot(before, host->processor);
        }
        // Retained pending recall must still reset once on the next valid call.
        model->reset(pending);
        phase(script, 0);
        (void)accepted(*host, *model, script);
    }
}

template<class Sample>
void lifecycle_cases() {
    for (int width : {1, 2}) {
        for (bool active : {false, true}) {
            ++cases;
            auto host = std::make_unique<Host<Sample>>(width, active);
            auto model = std::make_unique<ScalarModel>();
            Script script;
            phase(script, 0);
            script.gain.set({{0, 0.375}});
            script.expression.set({{0, 0.625}});
            script.sustain.set({{0, 1.0}});
            (void)accepted(*host, *model, script);
            const auto remembered = model->state;
            check(host->processor.setProcessing(false) == sdk::kResultOk, "stop processor failed");
            model->reset(remembered);
            model->verify(host->processor);
            metadata(host->processor, width);
            check(host->processor.setActive(false) == sdk::kResultOk, "deactivate component failed");
            model->verify(host->processor);
            host->arrange(width == 1 ? 2 : 1);
            metadata(host->processor, host->width);
            model->verify(host->processor);
            check((host->processor.getAudioOutput(0)->isActive() != 0) == active,
                  "suspended layout switch changed bus activation");
            check(host->processor.setActive(true) == sdk::kResultOk &&
                  host->processor.setProcessing(true) == sdk::kResultOk, "restart processor failed");
            phase(script, 0);
            (void)accepted(*host, *model, script);
            check(host->processor.terminate() == sdk::kResultOk, "terminate processor failed");
            host->initialized = false;
            check(host->processor.getBusCount(vst::kAudio, vst::kOutput) == 0,
                  "termination retained SDK output buses");
            model->reset(remembered);
            model->verify(host->processor);
            host->restore(0);
            host->data.numOutputs = 0;
            host->data.outputs = host->buffers.poison_bus();
            check(invoke(host->processor, host->data) != sdk::kResultOk,
                  "uninitialized zero-frame process accepted");
            auto layout = vst::SpeakerArr::kMono;
            check(host->processor.setBusArrangements(nullptr, 0, &layout, 1) != sdk::kResultOk,
                  "terminated processor accepted negotiation");
            check(host->processor.initialize(nullptr) == sdk::kResultOk, "reinitialize processor failed");
            host->initialized = true;
            host->width = 2;
            metadata(host->processor, 2);
            expect_saved(host->processor, remembered);
            check(host->processor.activateBus(vst::kAudio, vst::kOutput, 0, true) == sdk::kResultOk,
                  "activate reinitialized bus failed");
            host->setup();
            phase(script, 5);
            const auto silent = accepted(*host, *model, script);
            check(std::all_of(silent.begin(), silent.end(), [](Sample value) { return value == Sample{0}; }),
                  "lifecycle reset retained note or FIR tail");
            phase(script, 0);
            (void)accepted(*host, *model, script);
        }
    }
}

void negotiation() { negotiation_cases<float>(); negotiation_cases<double>(); }
void scalar() { scalar_cases<float>(); scalar_cases<double>(); }
void pointers() { pointer_cases<float>(); pointer_cases<double>(); }
void omission() { omission_cases<float>(); omission_cases<double>(); }
void flush() { flush_cases<float>(); flush_cases<double>(); }
void rejection() { rejection_cases<float>(); rejection_cases<double>(); }
void lifecycle() { lifecycle_cases<float>(); lifecycle_cases<double>(); }

using Fixture = std::pair<std::string_view, void (*)()>;
constexpr std::array<Fixture, 7> fixtures{{
    {"negotiation", negotiation}, {"scalar", scalar}, {"pointers", pointers},
    {"omission", omission}, {"flush", flush}, {"rejection", rejection},
    {"lifecycle", lifecycle}}};
} // namespace

int main(int argc, char** argv) {
    const std::string_view selected = argc > 1 ? argv[1] : "all";
    try {
        check(argc <= 2, "at most one mono-contract selector is accepted");
        bool found = false;
        for (const auto& [name, run] : fixtures) {
            if (selected != "all" && selected != name) { continue; }
            found = true;
            checks = cases = process_calls = guarded_arrays = 0;
            run();
            std::cout << "PASS " << name << " (" << cases << " cases, " << process_calls
                      << " processor calls, " << guarded_arrays << " guarded pointer arrays, "
                      << checks << " explicit checks)\n";
        }
        check(found, "unknown mono-contract selector");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << selected << ": " << error.what() << '\n';
        return 1;
    }
}
