#include "tests/cpp/vst3_test_support.hpp"

#include <bit>
#include <cmath>
#include <cstring>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <utility>

#if defined(__SSE__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 1)
#define RHODES_PRESSURE_MXCSR 1
#include <xmmintrin.h>
#if defined(_MSC_VER)
#include <immintrin.h>
#endif
#else
#define RHODES_PRESSURE_MXCSR 0
#endif

namespace {
using namespace rhodes;
using namespace rhodes::vst3;
using namespace rhodes::vst3::test_host;
using ScalarEngine = std::remove_cvref_t<
    decltype(std::declval<const HostEventProcessor&>().engine())>;

#if defined(RHODES_QUADRATURE_VST3)
static_assert(kQuadratureAudio && !kFilteredAudio && kStateModel == 3);
static_assert(std::is_same_v<ScalarEngine, QuadratureEngine>);
#elif defined(RHODES_FILTERED_VST3)
static_assert(kFilteredAudio && !kQuadratureAudio && kStateModel == 2);
static_assert(std::is_same_v<ScalarEngine, AudioEngine>);
#else
static_assert(!kFilteredAudio && !kQuadratureAudio && kStateModel == 1);
static_assert(std::is_same_v<ScalarEngine, ResearchEngine>);
#endif
static_assert(maximum_block_events == 256 && key_count == 73);
static_assert(sizeof(float) == sizeof(std::uint32_t) &&
              std::numeric_limits<float>::is_iec559);
constexpr std::size_t frames = 128;
constexpr unsigned ftz = 1U << 15U;
constexpr unsigned daz = 1U << 6U;
std::size_t calls = 0;
std::size_t comparisons = 0;
std::size_t rejections = 0;

vst::Event pressure(sdk::int32 offset, sdk::int32 id = -1, sdk::int16 pitch = 68,
                    sdk::int16 channel = 0, float value = 0.5F) {
    vst::Event event{};
    event.type = vst::Event::kPolyPressureEvent;
    event.sampleOffset = offset;
    event.polyPressure.channel = channel;
    event.polyPressure.pitch = pitch;
    event.polyPressure.noteId = id;
    event.polyPressure.pressure = value;
    return event;
}

// Write the SDK binary32 payload without a floating-point conversion: DAZ must
// not turn a negative subnormal test input into an innocent negative zero.
void pressure_bits(vst::Event& event, std::uint32_t bits) {
    std::memcpy(&event.polyPressure.pressure, &bits, sizeof(bits));
    require(std::bit_cast<std::uint32_t>(event.polyPressure.pressure) == bits,
            "raw pressure fixture changed its bits");
}

sdk::tresult invoke(RhodesProcessor& processor, vst::ProcessData& data) {
#if RHODES_PRESSURE_MXCSR
    const auto controls = _mm_getcsr() & ~0x3fU;
#endif
    ++calls;
    const auto result = processor.process(data);
#if RHODES_PRESSURE_MXCSR
    require((_mm_getcsr() & ~0x3fU) == controls,
            "pressure processing changed caller MXCSR controls");
#endif
    return result;
}

void unchanged_history(const ProcessorSnapshot& before, const RhodesProcessor& processor) {
    before.expect_unchanged(processor);
    // Probe beyond the complete 96-frame convolution memory before a pending
    // recall can reset it and hide damage caused by a rejected pressure event.
    const auto actual = std::make_unique<ScalarEngine>(processor.engine_processor().engine());
    const auto expected = std::make_unique<ScalarEngine>(before.engine.engine());
    std::array<double, frames + 1> actual_output{}, expected_output{};
    require(actual->process(actual_output) && expected->process(expected_output),
            "pressure rejection continuation failed");
    require(actual_output == expected_output, "pressure rejection changed future audio history");
}

template<class Sample>
struct Paired {
    ProcessorHost<Sample, frames> with_pressure;
    ProcessorHost<Sample, frames> without_pressure;
    EventList input;
    EventList reference_input;

    void compare() const {
        ++comparisons;
        const auto& actual = with_pressure.processor;
        const auto& expected = without_pressure.processor;
        require(actual.current_state() == expected.current_state() &&
                actual.published_state() == expected.published_state(),
                "pressure changed live or published configuration");
        require(actual.engine_processor().frame_cursor() ==
                expected.engine_processor().frame_cursor(), "pressure changed frame cursor");
        require(!actual.requires_reset() && !expected.requires_reset(), "unexpected processor fault");
        require(actual.note_tracker().active_count() == expected.note_tracker().active_count(),
                "pressure changed total note ownership");
        for (std::size_t key = 0; key < key_count; ++key) {
            require(actual.note_tracker().held_count(key) == expected.note_tracker().held_count(key),
                    "pressure changed per-key ownership");
        }
        same_mechanics(actual.engine_processor().engine(), expected.engine_processor().engine());
        using Bits = std::conditional_t<std::is_same_v<Sample, float>, std::uint32_t, std::uint64_t>;
        for (std::size_t frame = 0; frame < frames; ++frame) {
            require(std::isfinite(with_pressure.left[frame]) &&
                    std::isfinite(with_pressure.right[frame]), "nonfinite pressure trace output");
            require(std::bit_cast<Bits>(with_pressure.left[frame]) ==
                        std::bit_cast<Bits>(without_pressure.left[frame]) &&
                    std::bit_cast<Bits>(with_pressure.right[frame]) ==
                        std::bit_cast<Bits>(without_pressure.right[frame]),
                    "pressure changed stereo output bits");
        }
        require(with_pressure.output.silenceFlags == without_pressure.output.silenceFlags,
                "pressure changed silence flags");
    }

    void run(std::span<const vst::Event> events, vst::IParameterChanges* changes = nullptr) {
        require(events.size() <= input.values.size(), "accepted event fixture exceeds storage");
        input.count = static_cast<sdk::int32>(events.size());
        input.get_calls = 0;
        reference_input.count = 0;
        reference_input.get_calls = 0;
        for (std::size_t index = 0; index < events.size(); ++index) {
            input.values[index] = events[index];
            if (events[index].type != vst::Event::kPolyPressureEvent) {
                reference_input.values[static_cast<std::size_t>(reference_input.count++)] = events[index];
            }
        }
        with_pressure.data.inputEvents = &input;
        without_pressure.data.inputEvents = &reference_input;
        with_pressure.data.inputParameterChanges = changes;
        without_pressure.data.inputParameterChanges = changes;
        with_pressure.fill(Sample{7});
        without_pressure.fill(Sample{7});
        require(invoke(with_pressure.processor, with_pressure.data) == sdk::kResultOk,
                "well-formed pressure trace rejected");
        require(invoke(without_pressure.processor, without_pressure.data) == sdk::kResultOk,
                "pressure-free reference trace rejected");
        require(input.get_calls == events.size() && reference_input.get_calls ==
                static_cast<std::size_t>(reference_input.count), "accepted input list was truncated");
        compare();
    }

    void run(std::initializer_list<vst::Event> events, vst::IParameterChanges* changes = nullptr) {
        run(std::span<const vst::Event>(events.begin(), events.size()), changes);
    }

    void owners(std::size_t total, std::size_t key, std::size_t held) const {
        require(with_pressure.processor.note_tracker().active_count() == total &&
                with_pressure.processor.note_tracker().held_count(key) == held,
                "pressure trace missed the declared ownership outcome");
    }

    void audible() const {
        require(std::any_of(without_pressure.left.begin(), without_pressure.left.end(),
                           [](Sample sample) { return sample != Sample{0}; }),
                "pressure comparison fixture has no audible samples");
    }

    void recall(InstrumentState state) {
        for (auto* processor : {&with_pressure.processor, &without_pressure.processor}) {
            MemoryStream stream;
            stream.load(state, 0, kStateModel);
            require(processor->setState(&stream) == sdk::kResultOk, "pressure fixture recall rejected");
        }
    }
};

template<class Sample>
void reject(ProcessorHost<Sample, frames>& host, EventList& input,
            const ProcessorSnapshot& before) {
    host.data.inputEvents = &input;
    input.get_calls = 0;
    host.fill(Sample{7});
    require(invoke(host.processor, host.data) != sdk::kResultOk,
            "malformed pressure input was accepted");
    ++rejections;
    if (host.data.numSamples == 0) {
        require(std::all_of(host.left.begin(), host.left.end(),
                            [](Sample sample) { return sample == Sample{7}; }) &&
                std::all_of(host.right.begin(), host.right.end(),
                            [](Sample sample) { return sample == Sample{7}; }),
                "zero-frame rejection wrote caller samples");
    } else {
        host.expect_silence();
    }
    unchanged_history(before, host.processor);
}

template<class Sample>
void accepted_traces() {
    Paired<Sample> pair;
    auto metadata = pressure(127, std::numeric_limits<sdk::int32>::min(), 127, 15, 1.0F);
    metadata.ppqPosition = -123.5;
    pair.run({pressure(0, -1, 0, 0, 0.0F), pressure(0, 7),
              note_on(0, 7, 68, 0.75F), note_on(3, -2, 68, 0.5F, 15),
              note_on(3, -1, 72, 0.25F, 1), note_on(3, -1, 72, 0.5F, 1),
              pressure(3, 7, 69), pressure(4, 99, 68, 3), pressure(7, -1, 27),
              pressure(15, std::numeric_limits<sdk::int32>::max(), 101, 15), metadata});
    pair.owners(4, 40, 2);
    pair.owners(4, 44, 2);
    pair.audible();

    // Pressure neither creates owners nor retargets an existing signed ID.
    // Releases sharing its offset retain their original producer order.
    pair.run({pressure(0, 7), note_off(0, 7), pressure(0, 7),
              pressure(7, -2, 68, 15), note_off(7, -2, 68, 15), pressure(7, -2, 68, 15),
              note_off(12, -1, 72, 1), pressure(12, -1, 72, 1),
              note_on(20, 9, 80, 0.625F), pressure(20, -2, 69, 15),
              note_off(31, -1, 72, 1), pressure(127, -1, 72, 1)});
    pair.owners(1, 52, 1);
    pair.owners(1, 40, 0);
    pair.owners(1, 44, 0);

    ParameterQueue pedal;
    pedal.id = kSustainId;
    pedal.count = 1;
    pedal.values[0] = {0, 1.0};
    ParameterChanges changes;
    changes.count = 1;
    changes.values[0] = &pedal;
    pair.run({pressure(0, 9, 80), note_off(0, 9, 80), pressure(0, 9, 80)}, &changes);
    pair.owners(0, 52, 0);
    require(pair.with_pressure.processor.current_state().sustain,
            "pressure/release changed shared sustain");

    // Exercise every legal MIDI pitch, including the 55 outside the physical
    // keyboard, and every channel after release. No live identity is required.
    std::array<vst::Event, 128> all_pitches{};
    for (std::size_t index = 0; index < all_pitches.size(); ++index) {
        all_pitches[index] = pressure(static_cast<sdk::int32>(index),
            -static_cast<sdk::int32>(index), static_cast<sdk::int16>(index),
            static_cast<sdk::int16>(index % 16), (index % 2) == 0 ? 0.0F : 1.0F);
    }
    pair.run(all_pitches);
    pair.owners(0, 52, 0);
    pair.audible();
    pedal.values[0] = {0, 0.0};
    pair.run({}, &changes);
    pair.run({});
    pair.run({});
}

template<class Sample>
void invalid_fields_and_recall() {
    Paired<Sample> pair;
    pair.run({note_on(0, 7), note_on(2, -2, 68, 0.5F, 15), note_on(5, -1, 72, 0.5F, 1)});
    pair.run({});
    pair.audible();
    const InstrumentState pending{0.25F, true};
    pair.recall(pending);
    const ProcessorSnapshot before(pair.with_pressure.processor);

    ParameterQueue gain;
    ParameterQueue pedal;
    gain.count = pedal.count = 1;
    gain.values[0] = {0, 0.875};
    pedal.id = kSustainId;
    pedal.values[0] = {1, 0.0};
    ParameterChanges changes;
    changes.count = 2;
    changes.values = {&gain, &pedal};
    pair.with_pressure.data.inputParameterChanges = &changes;

    EventList malformed;
    malformed.count = 4;
    malformed.values[0] = note_off(0, 7);
    malformed.values[1] = note_on(1, 99, 80);
    malformed.values[2] = pressure(2, -2, 68, 15);
    const auto bad = [&](const vst::Event& event) {
        malformed.values[3] = event;
        reject(pair.with_pressure, malformed, before);
        require(malformed.get_calls == 4, "late pressure rejection did not reach the last event");
    };
    for (const auto channel : std::array<sdk::int16, 2>{-1, 16}) {
        bad(pressure(127, -1, 68, channel));
    }
    for (const auto pitch : std::array<sdk::int16, 2>{-1, 128}) {
        bad(pressure(127, -1, pitch));
    }
    for (const std::uint32_t bits : {0x7fc00001U, 0x7f800001U, 0x7f800000U,
                                    0xff800000U, 0xbe000000U, 0x3f800001U, 0x7f7fffffU}) {
        auto event = pressure(127);
        pressure_bits(event, bits);
        bad(event);
    }
    for (const sdk::int32 bus : {-1, 1}) {
        auto event = pressure(127);
        event.busIndex = bus;
        bad(event);
    }
    for (const auto ppq : {std::numeric_limits<double>::quiet_NaN(),
                           std::numeric_limits<double>::infinity(),
                           -std::numeric_limits<double>::infinity()}) {
        auto event = pressure(127);
        event.ppqPosition = ppq;
        bad(event);
    }
    for (const sdk::int32 offset : {-1, 1, 128}) { bad(pressure(offset)); }
    malformed.values[3] = pressure(127);
    malformed.failed_index = 3;
    reject(pair.with_pressure, malformed, before);

    // A pressure event cannot consume a recall. Reusing the old ID on a new
    // pitch also proves the next accepted block installs the pending reset.
    pair.run({pressure(0, 7, 69), note_on(0, 7, 69), pressure(127, 7, 69)});
    pair.owners(1, 41, 1);
    require(pair.with_pressure.processor.current_state() == pending &&
            pair.with_pressure.processor.published_state() == pending &&
            pair.with_pressure.processor.engine_processor().frame_cursor() == frames,
            "late pressure rejection lost or prematurely installed pending recall");
    pair.run({note_off(0, 7, 69), pressure(127, 7, 69)});
    pair.owners(0, 41, 0);
}

template<class Sample>
void ordering_and_budget() {
    Paired<Sample> pair;
    pair.run({note_on(0, 7)});
    const ProcessorSnapshot before(pair.with_pressure.processor);
    EventList malformed;
    malformed.count = 2;
    malformed.values[0] = pressure(31, -1, 0);
    malformed.values[1] = note_off(30, 7);
    reject(pair.with_pressure, malformed, before);
    malformed.values[1] = pressure(30, -1, 127);
    reject(pair.with_pressure, malformed, before);
    malformed.count = 3;
    malformed.values[0] = note_on(4, 99, 72);
    malformed.values[1] = pressure(127, -1, 101);
    malformed.values[2] = note_off(5, 7);
    reject(pair.with_pressure, malformed, before);

    // A declared 257-event list is rejected before reading its 256 real
    // slots, even though all readable entries are pressure and emit no action.
    malformed.values.fill(pressure(0));
    malformed.count = 257;
    reject(pair.with_pressure, malformed, before);
    require(malformed.get_calls == 0, "over-budget pressure list was traversed");
    malformed.count = -1;
    reject(pair.with_pressure, malformed, before);
    require(malformed.get_calls == 0, "negative pressure list count was traversed");

    std::array<vst::Event, maximum_block_events> full{};
    full.fill(pressure(0, -1, 127, 15));
    pair.run(full);
    pair.owners(1, 40, 1);
    pair.audible();
    full.back() = note_off(127, 7);
    pair.run(full);
    pair.owners(0, 40, 0);

    // Input events and physical actions have distinct limits: 255 ignored
    // pressure events, a note and a pedal point remain a two-action dispatch.
    ParameterQueue pedal;
    pedal.id = kSustainId;
    pedal.count = 1;
    pedal.values[0] = {0, 1.0};
    ParameterChanges changes;
    changes.count = 1;
    changes.values[0] = &pedal;
    full.back() = note_on(0, -2, 100, 0.5F, 15);
    pair.run(full, &changes);
    pair.owners(1, 72, 1);
    pair.run({pressure(0, -2, 100, 15), note_off(0, -2, 100, 15),
              pressure(0, -2, 100, 15)});
    pair.owners(0, 72, 0);
}

template<class Sample>
void zero_frame_flush() {
    Paired<Sample> pair;
    pair.run({note_on(0, -2, 68, 0.5F, 15)});
    pair.recall({0.375F, true});
    const ProcessorSnapshot before(pair.with_pressure.processor);
    EventList events;
    events.count = 1;
    events.values[0] = pressure(0, -2, 68, 15);
    pair.with_pressure.data.numSamples = 0;
    reject(pair.with_pressure, events, before);
    require(events.get_calls == 0, "zero-frame event list should reject before reading events");
    pair.without_pressure.data.numSamples = 0;
    pair.run({});
    require(pair.with_pressure.processor.current_state() == InstrumentState{0.375F, true} &&
            pair.with_pressure.processor.engine_processor().frame_cursor() == 0,
            "empty flush lost the rejected pressure block's pending recall");
    pair.owners(0, 40, 0);
    pair.with_pressure.data.numSamples = pair.without_pressure.data.numSamples =
        static_cast<sdk::int32>(frames);
    pair.run({pressure(0, -2, 68, 15)});
    pair.with_pressure.expect_silence();
}

template<class Sample>
void raw_pressure_cases() {
    Paired<Sample> pair;
    pair.run({note_on(0, -2, 68, 0.75F, 15)});
    for (const std::uint32_t bits : {0x00000000U, 0x80000000U, 0x00000001U,
                                    0x007fffffU, 0x00800000U, 0x3f800000U}) {
        auto event = pressure(63, -2, 68, 15);
        pressure_bits(event, bits);
        pair.run({event});
        pair.owners(1, 40, 1);
        pair.audible();
        require(std::bit_cast<std::uint32_t>(pair.input.values[0].polyPressure.pressure) == bits,
                "pressure processing changed host event bits");
    }
    const ProcessorSnapshot before(pair.with_pressure.processor);
    EventList malformed;
    malformed.count = 2;
    malformed.values[0] = note_off(0, -2, 68, 15);
    for (const std::uint32_t bits : {0x80000001U, 0x807fffffU}) {
        malformed.values[1] = pressure(127, -2, 68, 15);
        pressure_bits(malformed.values[1], bits);
        reject(pair.with_pressure, malformed, before);
        require(std::bit_cast<std::uint32_t>(malformed.values[1].polyPressure.pressure) == bits,
                "negative subnormal pressure fixture was flushed");
    }
    pair.run({pressure(0, -2, 68, 15), note_off(0, -2, 68, 15),
              pressure(127, -2, 68, 15)});
    pair.owners(0, 40, 0);
    pair.run({});
}

#if RHODES_PRESSURE_MXCSR
struct RestoreControls {
    unsigned original = _mm_getcsr();
    ~RestoreControls() { _mm_setcsr(original); }
};

unsigned supported_controls() {
    alignas(16) std::array<unsigned char, 512> saved{};
#if defined(_MSC_VER)
    _fxsave(saved.data());
#else
    asm volatile("fxsave %0" : "=m"(saved));
#endif
    std::uint32_t mask = 0;
    std::memcpy(&mask, saved.data() + 28, sizeof(mask));
    return mask;
}
#endif

void raw_mode(std::string_view name, unsigned controls) {
#if RHODES_PRESSURE_MXCSR
    const auto original = _mm_getcsr();
    if ((controls & ~supported_controls()) != 0U) {
        std::cout << "SKIP pressure " << name << ": requested MXCSR controls unavailable\n";
        return;
    }
    {
        RestoreControls restore;
        const auto requested = (original & ~(ftz | daz | 0x3fU)) | controls | 0x1f80U;
        _mm_setcsr(requested);
        require(_mm_getcsr() == requested, "requested pressure MXCSR mode was not installed");
        raw_pressure_cases<float>();
        raw_pressure_cases<double>();
    }
    require(_mm_getcsr() == original, "pressure suite did not restore complete MXCSR");
#else
    if (controls != 0U) {
        std::cout << "SKIP pressure " << name << ": x86 MXCSR controls unavailable\n";
        return;
    }
    raw_pressure_cases<float>();
    raw_pressure_cases<double>();
#endif
    std::cout << "PASS pressure raw binary32 " << name << '\n';
}

template<class Sample>
void cases() {
    accepted_traces<Sample>();
    invalid_fields_and_recall<Sample>();
    ordering_and_budget<Sample>();
    zero_frame_flush<Sample>();
}
} // namespace

int main() {
    try {
        cases<float>();
        cases<double>();
        raw_mode("baseline", 0U);
        raw_mode("ftz", ftz);
        raw_mode("daz", daz);
        raw_mode("ftz_daz", ftz | daz);
        std::cout << "PASS model " << kStateModel << " pressure: " << calls
                  << " process calls, " << comparisons << " paired audio/ownership comparisons, "
                  << rejections << " atomic rejections; float and double\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL model " << kStateModel << " pressure: " << error.what() << '\n';
        return 1;
    }
}
