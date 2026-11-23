#include "vst3_test_support.hpp"

#include "pluginterfaces/vst/ivstmidicontrollers.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>
#if defined(_MSC_VER)
#include <malloc.h>
#endif

// Intercept ordinary, array, aligned and nothrow C++ allocation. Host fixtures
// use only fixed storage during callbacks. This checks allocations reached by
// these actual process() calls, not arbitrary malloc calls or other hosts.
namespace allocation_probe {
thread_local bool enabled = false;
thread_local std::size_t attempts = 0;
std::size_t process_calls = 0;

void record() noexcept { if (enabled) { ++attempts; } }

void* allocate(std::size_t bytes) {
    record();
    if (void* memory = std::malloc(bytes == 0 ? 1 : bytes)) { return memory; }
    throw std::bad_alloc{};
}

// Keep the replacement deallocator opaque to GCC's mismatched-new-delete
// heuristic, which otherwise treats our legitimate new-to-C-allocation bridge as a
// mismatch when it inlines the explicit counter-calibration calls below.
#if defined(_MSC_VER)
__declspec(noinline)
#elif defined(__GNUC__)
__attribute__((noinline))
#endif
void deallocate(void* memory) noexcept { std::free(memory); }

void* allocate_aligned(std::size_t bytes, std::align_val_t alignment) {
    record();
    const auto align = static_cast<std::size_t>(alignment);
    bytes = bytes == 0 ? 1 : bytes;
    if (bytes > std::numeric_limits<std::size_t>::max() - (align - 1)) {
        throw std::bad_alloc{};
    }
#if defined(_MSC_VER)
    void* memory = _aligned_malloc(bytes, align);
#else
    const auto rounded = ((bytes + align - 1) / align) * align;
    void* memory = std::aligned_alloc(align, rounded);
#endif
    if (memory != nullptr) { return memory; }
    throw std::bad_alloc{};
}

void free_aligned(void* memory) noexcept {
#if defined(_MSC_VER)
    _aligned_free(memory);
#else
    deallocate(memory);
#endif
}

struct Scope {
    Scope() noexcept { attempts = 0; enabled = true; }
    ~Scope() { enabled = false; }
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
};
} // namespace allocation_probe

void* operator new(std::size_t bytes) { return allocation_probe::allocate(bytes); }
void* operator new[](std::size_t bytes) { return allocation_probe::allocate(bytes); }
void operator delete(void* memory) noexcept { allocation_probe::deallocate(memory); }
void operator delete[](void* memory) noexcept { allocation_probe::deallocate(memory); }
void operator delete(void* memory, std::size_t) noexcept { allocation_probe::deallocate(memory); }
void operator delete[](void* memory, std::size_t) noexcept { allocation_probe::deallocate(memory); }
void* operator new(std::size_t bytes, const std::nothrow_t&) noexcept {
    try { return allocation_probe::allocate(bytes); } catch (...) { return nullptr; }
}
void* operator new[](std::size_t bytes, const std::nothrow_t&) noexcept {
    try { return allocation_probe::allocate(bytes); } catch (...) { return nullptr; }
}
void operator delete(void* memory, const std::nothrow_t&) noexcept {
    allocation_probe::deallocate(memory);
}
void operator delete[](void* memory, const std::nothrow_t&) noexcept {
    allocation_probe::deallocate(memory);
}
void* operator new(std::size_t bytes, std::align_val_t alignment) {
    return allocation_probe::allocate_aligned(bytes, alignment);
}
void* operator new[](std::size_t bytes, std::align_val_t alignment) {
    return allocation_probe::allocate_aligned(bytes, alignment);
}
void operator delete(void* memory, std::align_val_t) noexcept {
    allocation_probe::free_aligned(memory);
}
void operator delete[](void* memory, std::align_val_t) noexcept {
    allocation_probe::free_aligned(memory);
}
void operator delete(void* memory, std::size_t, std::align_val_t) noexcept {
    allocation_probe::free_aligned(memory);
}
void operator delete[](void* memory, std::size_t, std::align_val_t) noexcept {
    allocation_probe::free_aligned(memory);
}
void* operator new(std::size_t bytes, std::align_val_t alignment,
                   const std::nothrow_t&) noexcept {
    try { return allocation_probe::allocate_aligned(bytes, alignment); }
    catch (...) { return nullptr; }
}
void* operator new[](std::size_t bytes, std::align_val_t alignment,
                     const std::nothrow_t&) noexcept {
    try { return allocation_probe::allocate_aligned(bytes, alignment); }
    catch (...) { return nullptr; }
}
void operator delete(void* memory, std::align_val_t, const std::nothrow_t&) noexcept {
    allocation_probe::free_aligned(memory);
}
void operator delete[](void* memory, std::align_val_t, const std::nothrow_t&) noexcept {
    allocation_probe::free_aligned(memory);
}

namespace {
using namespace rhodes;
using namespace rhodes::vst3;
using namespace rhodes::vst3::test_host;

// Use the selected variant's physical/audio model while keeping note ownership,
// event scheduling and gain automation independent of the VST3 adapter.
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

sdk::tresult process(RhodesProcessor& processor, vst::ProcessData& data) {
    sdk::tresult result = sdk::kResultFalse;
    std::size_t allocations = 0;
    {
        allocation_probe::Scope scope;
        result = processor.process(data);
        allocations = allocation_probe::attempts;
    }
    ++allocation_probe::process_calls;
    require(allocations == 0, "C++ allocation occurred inside processor.process");
    return result;
}

template <class Sample, std::size_t Frames>
void expect_sample(const ProcessorHost<Sample, Frames>& host, std::size_t frame,
                   double expected) {
    require(host.left[frame] == host.right[frame], "stereo channels differ");
    require(host.left[frame] == static_cast<Sample>(expected),
            "host output differs from independent scalar schedule");
}

double scalar_sample(ScalarEngine& engine) {
    double volts = 0.0;
    require(engine.process(std::span<double>{&volts, 1}), "scalar engine process");
    return volts;
}

template <class Sample>
void scalar_at_rate(double rate) {
    ProcessorHost<Sample, 73> host{rate};
    EventList events;
    events.count = 4;
    events.values[0] = note_on(0, 1, 28, 0.25F);
    events.values[1] = note_on(17, 2, 68, 0.75F);
    events.values[2] = note_off(37, 1, 28);
    events.values[3] = note_off(65, 2, 68);
    host.data.inputEvents = &events;
    host.output.silenceFlags = std::numeric_limits<sdk::uint64>::max();
    require(process(host.processor, host.data) == sdk::kResultOk, "scalar host render");

    const auto reference_storage = std::make_unique<ScalarEngine>();
    auto& reference = *reference_storage;
    require(reference.prepare(rate), "scalar reference prepare");
    // This schedule does not use NoteTracker, EventProcessor, or translated
    // plugin events. It independently drives the underlying scalar model.
    for (std::size_t frame = 0; frame < host.left.size(); ++frame) {
        if (frame == 0) { require(reference.note_on(0, 0.25), "scalar first note"); }
        if (frame == 17) { require(reference.note_on(40, 0.75), "scalar second note"); }
        if (frame == 37) { require(reference.note_off(0), "scalar first release"); }
        if (frame == 65) { require(reference.note_off(40), "scalar second release"); }
        expect_sample(host, frame, 16.0 * scalar_sample(reference));
    }
    require(std::any_of(host.left.begin(), host.left.end(),
                        [](Sample value) { return value != Sample{0}; }),
            "scalar fixture produced only silence");
    require(host.output.silenceFlags == 0, "nonzero output retained silent flags");
    require(host.processor.engine_processor().frame_cursor() == 73, "scalar frame count");
    require(host.processor.note_tracker().active_count() == 0, "scalar releases lost");
    same_mechanics(host.processor.engine_processor().engine(), reference);
}

void scalar_output() {
    for (double rate : {32000.0, 48000.0, 96000.0, 192000.0}) {
        scalar_at_rate<double>(rate);
        scalar_at_rate<float>(rate);
    }
}

void overlapping_notes() {
    ProcessorHost<double, 16> host;
    EventList events;
    events.count = 4;
    events.values[0] = note_on(0, 101, 68, 0.75F, 0);
    events.values[1] = note_on(4, 102, 68, 0.5F, 1);
    events.values[2] = note_on(8, 101, 68, 0.25F, 0);
    events.values[3] = note_off(12, 101, 68, 0);
    host.data.inputEvents = &events;
    require(process(host.processor, host.data) == sdk::kResultOk, "overlap first block");
    require(host.processor.note_tracker().active_count() == 1 &&
            host.processor.note_tracker().held_count(40) == 1,
            "overlap release removed another channel's holder");
    const auto reference_storage = std::make_unique<ScalarEngine>();
    auto& reference = *reference_storage;
    for (std::size_t frame = 0; frame < 16; ++frame) {
        if (frame == 0) { require(reference.note_on(40, 0.75), "overlap first strike"); }
        if (frame == 4) { require(reference.note_on(40, 0.5), "overlap second strike"); }
        if (frame == 8) { require(reference.note_on(40, 0.25), "identified restrike"); }
        // The release at frame 12 must not damp a key held on channel 1.
        expect_sample(host, frame, 16.0 * scalar_sample(reference));
    }
    events.count = 1;
    events.values[0] = note_off(4, 102, 68, 1);
    require(process(host.processor, host.data) == sdk::kResultOk, "overlap final release");
    for (std::size_t frame = 0; frame < 16; ++frame) {
        if (frame == 4) { require(reference.note_off(40), "last-holder physical release"); }
        expect_sample(host, frame, 16.0 * scalar_sample(reference));
    }
    require(host.processor.note_tracker().active_count() == 0, "overlap holder leak");
    same_mechanics(host.processor.engine_processor().engine(), reference);

    require(host.processor.setProcessing(false) == sdk::kResultOk, "anonymous reset");
    reference.reset();
    events.count = 4;
    events.values[0] = note_on(0, -1, 68, 0.75F);
    events.values[1] = note_on(4, -1, 68, 0.5F);
    events.values[2] = note_on(8, -1, 68, 0.0F);
    events.values[3] = note_off(12, -1, 68);
    require(process(host.processor, host.data) == sdk::kResultOk, "anonymous overlap");
    for (std::size_t frame = 0; frame < 16; ++frame) {
        if (frame == 0) { require(reference.note_on(40, 0.75), "anonymous first strike"); }
        if (frame == 4) { require(reference.note_on(40, 0.5), "anonymous second strike"); }
        if (frame == 12) { require(reference.note_off(40), "anonymous final release"); }
        expect_sample(host, frame, 16.0 * scalar_sample(reference));
    }
    require(host.processor.note_tracker().active_count() == 0,
            "zero-velocity anonymous release lost");

    require(host.processor.setProcessing(true) == sdk::kResultOk, "range reset");
    events.count = 2;
    events.values[0] = note_on(0, -2, 27);
    events.values[1] = note_on(1, -2, 101);
    require(process(host.processor, host.data) == sdk::kResultOk, "outside-keyboard notes");
    host.expect_silence();
    require(host.processor.note_tracker().active_count() == 0,
            "outside-keyboard pitch acquired ownership");
}

void linear_automation() {
    ProcessorHost<double, 24> host;
    EventList events;
    events.count = 2;
    events.values[0] = note_on(0, 1);
    events.values[1] = note_off(4, 1);
    const auto queue_storage = std::make_unique<std::array<ParameterQueue, 2>>();
    auto& gain = (*queue_storage)[0];
    auto& pedal = (*queue_storage)[1];
    gain.count = 3;
    gain.values[0] = {7, 0.75};
    gain.values[1] = {7, 0.25};
    gain.values[2] = {15, 0.5};
    pedal.id = kSustainId;
    pedal.count = 3;
    pedal.values[0] = {4, 0.0};
    pedal.values[1] = {4, 0.5};
    pedal.values[2] = {17, 0.499999};
    ParameterChanges parameters;
    parameters.count = 2;
    parameters.values = {&gain, &pedal};
    host.data.inputEvents = &events;
    host.data.inputParameterChanges = &parameters;
    require(process(host.processor, host.data) == sdk::kResultOk, "linear automation render");
    const auto reference_storage = std::make_unique<ScalarEngine>();
    auto& reference = *reference_storage;
    require(reference.note_on(40, 0.75), "automation reference strike");
    for (std::size_t frame = 0; frame < host.left.size(); ++frame) {
        if (frame == 4) {
            reference.set_sustain(true);
            require(reference.note_off(40), "pedal-before-release reference");
        }
        if (frame == 17) { reference.set_sustain(false); }
        const double normalized = frame <= 7 ?
            0.5 + (0.25 - 0.5) * static_cast<double>(frame + 1) / 8.0 :
            (frame <= 15 ? 0.25 + (0.5 - 0.25) * static_cast<double>(frame - 7) / 8.0 : 0.5);
        expect_sample(host, frame, (32.0 * normalized) * scalar_sample(reference));
    }
    require(host.processor.current_state() == InstrumentState{0.5F, false} &&
            host.processor.published_state() == host.processor.current_state(),
            "automation final state was not published");
    same_mechanics(host.processor.engine_processor().engine(), reference);

    events.count = 0;
    pedal.count = 0;
    gain.count = 1;
    gain.values[0] = {7, 1.0};
    require(process(host.processor, host.data) == sdk::kResultOk, "next-block gain anchor");
    for (std::size_t frame = 0; frame < host.left.size(); ++frame) {
        const double normalized = frame <= 7 ?
            0.5 + 0.5 * static_cast<double>(frame + 1) / 8.0 : 1.0;
        expect_sample(host, frame, (32.0 * normalized) * scalar_sample(reference));
    }
}

void dense_automation() {
    static_assert(kMaximumHostFrames == 8192);
    static_assert(maximum_block_frames == 4096);
    const auto host_storage = std::make_unique<ProcessorHost<double, kMaximumHostFrames + 3>>();
    auto& host = *host_storage;
    EventList events;
    events.count = 2;
    events.values[0] = note_on(0, 1);
    events.values[1] = note_off(8191, 1);
    const auto queue_storage = std::make_unique<std::array<ParameterQueue, 2>>();
    auto& gain = (*queue_storage)[0];
    auto& pedal = (*queue_storage)[1];
    gain.count = static_cast<sdk::int32>(kMaximumHostFrames);
    pedal.id = kSustainId;
    pedal.count = gain.count;
    for (sdk::int32 frame = 0; frame < gain.count; ++frame) {
        const auto at = static_cast<std::size_t>(frame);
        gain.values[at] = {frame, frame % 2 == 0 ? 0.25 : 0.75};
        pedal.values[at] = {frame, static_cast<double>(frame % 2)};
    }
    ParameterChanges parameters;
    parameters.count = 2;
    parameters.values = {&gain, &pedal};
    host.data.inputEvents = &events;
    host.data.inputParameterChanges = &parameters;
    require(process(host.processor, host.data) == sdk::kResultOk,
            "8192-frame block with two dense 8192-point queues failed");
    require(host.processor.engine_processor().frame_cursor() == kMaximumHostFrames &&
            host.processor.current_state() == InstrumentState{0.75F, true},
            "dense queues did not reach their final point");
    require(gain.get_calls == kMaximumHostFrames && pedal.get_calls == kMaximumHostFrames,
            "dense queues were truncated");
    require(host.left == host.right && std::all_of(host.left.begin(), host.left.end(),
                                                  [](double x) { return std::isfinite(x); }),
            "dense output is nonfinite or stereo differs");
    const ProcessorSnapshot before{host.processor};
    events.count = 0;
    const auto unchanged = [](double value) { return value == 7.0; };

    // Fail only after visiting all 8192 points of each queue. The snapshot's
    // continuation comparison includes the filtered variant's FIR history.
    pedal.failed_index = 8191;
    const auto gain_calls = gain.get_calls;
    const auto pedal_calls = pedal.get_calls;
    host.fill(7.0);
    require(process(host.processor, host.data) != sdk::kResultOk,
            "late dense-queue read failure was accepted");
    require(gain.get_calls - gain_calls == kMaximumHostFrames &&
            pedal.get_calls - pedal_calls == kMaximumHostFrames,
            "late failure did not reach the final dense-queue point");
    before.expect_unchanged(host.processor);
    host.expect_silence(kMaximumHostFrames);
    require(std::all_of(host.left.begin() + kMaximumHostFrames, host.left.end(), unchanged) &&
            std::all_of(host.right.begin() + kMaximumHostFrames, host.right.end(), unchanged),
            "late rejection wrote beyond the declared frame count");
    pedal.failed_index = -1;

    host.data.numSamples = 8193;
    host.fill(7.0);
    // Real storage for every declared sample, with a guard on each side.
    // Unsupported frame counts must leave both samples and flags untouched.
    host.channels = {host.left.data() + 1, host.right.data() + 1};
    const auto sentinel_flags = std::numeric_limits<sdk::uint64>::max();
    host.output.silenceFlags = sentinel_flags;
    require(process(host.processor, host.data) != sdk::kResultOk, "8193 frames accepted");
    before.expect_unchanged(host.processor);
    require(std::all_of(host.left.begin(), host.left.end(), unchanged) &&
            std::all_of(host.right.begin(), host.right.end(), unchanged),
            "unsupported frame count changed output samples or guards");
    require(host.output.silenceFlags == sentinel_flags,
            "unsupported frame count changed silence flags");

    host.channels = {host.left.data(), host.right.data()};
    host.data.numSamples = 8192;
    for (auto* queue : {&gain, &pedal}) {
        queue->count = 8193;
        const auto calls = queue->get_calls;
        host.fill(7.0);
        require(process(host.processor, host.data) != sdk::kResultOk, "8193 points accepted");
        require(queue->get_calls == calls, "oversized queue was read before count validation");
        before.expect_unchanged(host.processor);
        host.expect_silence(kMaximumHostFrames);
        require(std::all_of(host.left.begin() + kMaximumHostFrames, host.left.end(), unchanged) &&
                std::all_of(host.right.begin() + kMaximumHostFrames, host.right.end(), unchanged),
                "rejection wrote beyond the declared frame count");
        queue->count = 8192;
    }
}

void invalid_atomicity() {
    ProcessorHost<double, 16> host;
    EventList events;
    events.count = 1;
    events.values[0] = note_on(0, 1);
    host.data.inputEvents = &events;
    require(process(host.processor, host.data) == sdk::kResultOk, "atomicity initial note");
    const ProcessorSnapshot before{host.processor};
    events.count = 0;
    const auto gain_storage = std::make_unique<ParameterQueue>();
    auto& gain = *gain_storage;
    gain.count = 2;
    gain.values[0] = {0, 0.75};
    gain.values[1] = {15, 0.25};
    ParameterChanges parameters;
    parameters.count = 1;
    parameters.values[0] = &gain;
    host.data.inputParameterChanges = &parameters;
    const auto reject = [&]() {
        host.fill(7.0);
        require(process(host.processor, host.data) != sdk::kResultOk,
                "malformed host input was accepted");
        before.expect_unchanged(host.processor);
        host.expect_silence();
    };
    for (double invalid : {std::numeric_limits<double>::quiet_NaN(),
                           std::numeric_limits<double>::infinity(), -0.01, 1.01}) {
        gain.values[1].value = invalid;
        reject();
    }
    gain.values[1].value = 0.25;
    gain.failed_index = 1;
    reject();
    gain.failed_index = -1;
    for (sdk::int32 offset : {-1, 16}) {
        gain.values[1].offset = offset;
        reject();
    }
    gain.values[0].offset = 8;
    gain.values[1].offset = 7;
    reject();
    gain.values[0].offset = 0;
    gain.values[1].offset = 15;
    gain.id = 999;
    reject();
    gain.id = kGainId;
    parameters.count = 2;
    parameters.values[1] = &gain;
    reject();
    parameters.values[1] = nullptr;
    reject();
    for (sdk::int32 count : {-1, 3}) { parameters.count = count; reject(); }
    host.data.inputParameterChanges = nullptr;

    events.count = 2;
    events.values[0] = note_on(0, 2, 60);
    events.values[1] = note_on(15, 3, 61);
    events.values[1].noteOn.velocity = std::numeric_limits<float>::quiet_NaN();
    reject();
    events.values[1] = note_on(15, 3, 61);
    events.failed_index = 1;
    reject();
    events.failed_index = -1;
    events.values[1].noteOn.channel = 16;
    reject();
    events.values[1] = note_on(15, 3, 61);
    events.values[1].noteOn.tuning = std::numeric_limits<float>::infinity();
    reject();
    events.values[1] = note_on(15, 3, 61);
    events.values[1].ppqPosition = std::numeric_limits<double>::quiet_NaN();
    reject();
    events.values[1] = note_on(15, 3, 61);
    events.values[1].type = vst::Event::kLegacyMIDICCOutEvent;
    reject();
    events.values[1] = note_on(15, 2, 61); // Same identity, conflicting pitch.
    reject();
    events.values[1] = note_on(16, 3, 61);
    reject();
    events.values[0] = note_on(8, 2, 60);
    events.values[1] = note_on(7, 3, 61);
    reject();
    for (sdk::int32 count : {-1, 257}) { events.count = count; reject(); }

    // Retained ownership must still answer the original identity after all
    // those failed transactions, not merely have the same aggregate count.
    events.count = 1;
    events.values[0] = note_off(0, 1);
    require(process(host.processor, host.data) == sdk::kResultOk &&
            host.processor.note_tracker().active_count() == 0,
            "rejections corrupted original note identity");

    require(host.processor.setProcessing(false) == sdk::kResultOk, "capacity reset");
    events.count = 256;
    for (sdk::int32 i = 0; i < events.count; ++i) {
        events.values[static_cast<std::size_t>(i)] = note_on(0, i);
    }
    require(process(host.processor, host.data) == sdk::kResultOk &&
            host.processor.note_tracker().active_count() == NoteTracker::capacity,
            "exact holder capacity was not accepted");
    const ProcessorSnapshot full{host.processor};
    events.count = 1;
    events.values[0] = note_on(0, 999);
    host.fill(7.0);
    require(process(host.processor, host.data) != sdk::kResultOk,
            "holder capacity overflow was accepted");
    full.expect_unchanged(host.processor);
    host.expect_silence();

    require(host.processor.setProcessing(false) == sdk::kResultOk, "dispatch reset");
    events.count = 256;
    for (sdk::int32 i = 0; i < events.count; ++i) {
        events.values[static_cast<std::size_t>(i)] = note_on(0, i);
    }
    gain.id = kSustainId;
    gain.count = 1;
    gain.values[0] = {0, 1.0};
    parameters.count = 1;
    host.data.inputParameterChanges = &parameters;
    const ProcessorSnapshot empty{host.processor};
    require(process(host.processor, host.data) != sdk::kResultOk,
            "256 simultaneous notes plus a pedal dispatch were accepted");
    empty.expect_unchanged(host.processor);
    host.expect_silence();
}

void parameter_flush() {
    ProcessorHost<double, 16> host;
    EventList events;
    events.count = 1;
    events.values[0] = note_on(0, 1);
    host.data.inputEvents = &events;
    require(process(host.processor, host.data) == sdk::kResultOk, "flush initial note");
    const auto before_storage = std::make_unique<const HostEventProcessor>(host.processor.engine_processor());
    const auto& before = *before_storage;
    events.count = 0;
    const auto queue_storage = std::make_unique<std::array<ParameterQueue, 2>>();
    auto& gain = (*queue_storage)[0];
    auto& pedal = (*queue_storage)[1];
    gain.count = 2;
    gain.values[0] = {0, 0.25};
    gain.values[1] = {0, 0.8};
    pedal.id = kSustainId;
    pedal.count = 1;
    pedal.values[0] = {0, 0.5};
    ParameterChanges parameters;
    parameters.count = 2;
    parameters.values = {&gain, &pedal};
    host.data.inputParameterChanges = &parameters;
    host.data.numSamples = 0;
    host.data.numOutputs = 0;
    host.data.outputs = nullptr;
    host.fill(7.0);
    require(process(host.processor, host.data) == sdk::kResultOk, "bufferless parameter flush");
    require(host.processor.current_state() == InstrumentState{0.8F, true} &&
            host.processor.published_state() == host.processor.current_state(),
            "flush did not canonicalize/publish last parameter values");
    require(host.processor.engine_processor().frame_cursor() == before.frame_cursor(),
            "flush advanced frame cursor");
    same_mechanics(host.processor.engine_processor().engine(), before.engine());
    require(host.left.front() == 7.0 && host.right.back() == 7.0,
            "zero-frame flush touched samples");

    host.data.numOutputs = 1;
    host.data.outputs = &host.output;
    host.output.numChannels = 0;
    host.output.channelBuffers64 = nullptr;
    require(process(host.processor, host.data) == sdk::kResultOk, "zero-channel flush");
    host.output.numChannels = 2;
    require(process(host.processor, host.data) == sdk::kResultOk, "pointerless stereo flush");
    const ProcessorSnapshot flushed{host.processor};
    events.count = 1;
    require(process(host.processor, host.data) != sdk::kResultOk, "note event in flush accepted");
    flushed.expect_unchanged(host.processor);
    events.count = 0;
    gain.values[1].offset = 1;
    require(process(host.processor, host.data) != sdk::kResultOk, "nonzero flush offset accepted");
    flushed.expect_unchanged(host.processor);

    host.data.numSamples = 16;
    host.output.channelBuffers64 = host.channels.data();
    host.data.inputParameterChanges = nullptr;
    events.count = 1;
    events.values[0] = note_off(0, 1);
    require(process(host.processor, host.data) == sdk::kResultOk, "render after flush");
    const auto reference_storage = std::make_unique<ScalarEngine>(before.engine());
    auto& reference = *reference_storage;
    reference.set_sustain(true);
    require(reference.note_off(40), "flush reference release");
    for (std::size_t frame = 0; frame < 16; ++frame) {
        expect_sample(host, frame, (32.0 * static_cast<double>(0.8F)) * scalar_sample(reference));
    }

    const auto unconfigured_storage = std::make_unique<RhodesProcessor>();
    auto& unconfigured = *unconfigured_storage;
    require(unconfigured.initialize(nullptr) == sdk::kResultOk, "unconfigured flush initialize");
    vst::ProcessData flush{};
    require(process(unconfigured, flush) == sdk::kResultOk, "flush before processing setup");
    require(unconfigured.terminate() == sdk::kResultOk, "unconfigured flush terminate");
}

void lifecycle() {
    ProcessorHost<double, 16> host;
    require(host.processor.getBusCount(vst::kAudio, vst::kInput) == 0 &&
            host.processor.getBusCount(vst::kAudio, vst::kOutput) == 1 &&
            host.processor.getBusCount(vst::kEvent, vst::kInput) == 1,
            "unexpected processor buses");
    require(host.processor.initialize(nullptr) != sdk::kResultOk, "duplicate initialize accepted");
    require(host.processor.canProcessSampleSize(vst::kSample32) == sdk::kResultTrue &&
            host.processor.canProcessSampleSize(vst::kSample64) == sdk::kResultTrue &&
            host.processor.canProcessSampleSize(99) != sdk::kResultTrue,
            "sample-size capability differs");
    require(host.processor.getTailSamples() == vst::kInfiniteTail, "synthetic infinite tail");
    vst::SpeakerArrangement arrangement = vst::SpeakerArr::kStereo;
    require(host.processor.setBusArrangements(nullptr, 0, &arrangement, 1) == sdk::kResultOk,
            "stereo arrangement rejected");
    arrangement = vst::SpeakerArr::kMono;
    require(host.processor.setBusArrangements(nullptr, 0, &arrangement, 1) != sdk::kResultOk,
            "mono arrangement accepted");
    require(host.processor.getBusArrangement(vst::kOutput, 0, arrangement) == sdk::kResultOk &&
            arrangement == vst::SpeakerArr::kStereo, "rejected arrangement changed bus");
    EventList events;
    events.count = 1;
    events.values[0] = note_on(0, 1);
    host.data.inputEvents = &events;
    require(process(host.processor, host.data) == sdk::kResultOk, "lifecycle initial render");
    const ProcessorSnapshot before{host.processor};
    const vst::ProcessSetup valid{vst::kRealtime, vst::kSample64, 8192, 48000.0};
    for (double rate : {22050.0, 192001.0, std::numeric_limits<double>::quiet_NaN()}) {
        auto setup = valid;
        setup.sampleRate = rate;
        require(host.processor.setupProcessing(setup) != sdk::kResultOk, "invalid rate accepted");
        before.expect_unchanged(host.processor);
    }
    for (sdk::int32 frames : {0, 8193}) {
        auto setup = valid;
        setup.maxSamplesPerBlock = frames;
        require(host.processor.setupProcessing(setup) != sdk::kResultOk, "invalid setup bound accepted");
        before.expect_unchanged(host.processor);
    }
    auto setup = valid;
    setup.symbolicSampleSize = 99;
    require(host.processor.setupProcessing(setup) != sdk::kResultOk, "invalid setup format accepted");
    before.expect_unchanged(host.processor);
    setup = valid;
    setup.processMode = -1;
    require(host.processor.setupProcessing(setup) != sdk::kResultOk, "invalid setup mode accepted");
    before.expect_unchanged(host.processor);

    host.channels[1] = nullptr;
    host.fill(7.0);
    require(process(host.processor, host.data) != sdk::kResultOk, "active null channel accepted");
    before.expect_unchanged(host.processor);
    require(std::all_of(host.left.begin(), host.left.end(), [](double x) { return x == 0.0; }),
            "available rejected output was not silenced");
    host.channels[1] = host.right.data();
    host.data.processMode = vst::kOffline;
    require(process(host.processor, host.data) != sdk::kResultOk, "offline/setup mismatch accepted");
    before.expect_unchanged(host.processor);
    host.data.processMode = vst::kRealtime;

    for (int transition = 0; transition < 5; ++transition) {
        require(process(host.processor, host.data) == sdk::kResultOk, "lifecycle priming render");
        sdk::tresult result = sdk::kResultFalse;
        if (transition == 0) { result = host.processor.setProcessing(false); }
        if (transition == 1) { result = host.processor.setProcessing(true); }
        if (transition == 2) { result = host.processor.setActive(false); }
        if (transition == 3) { result = host.processor.setActive(true); }
        if (transition == 4) { setup = valid; result = host.processor.setupProcessing(setup); }
        require(result == sdk::kResultOk && host.processor.note_tracker().active_count() == 0 &&
                host.processor.engine_processor().frame_cursor() == 0 &&
                host.processor.engine_processor().engine().mechanical_energy_joules() == 0.0 &&
                !host.processor.requires_reset(), "lifecycle did not reset audio ownership");
    }
    require(host.processor.activateBus(vst::kAudio, vst::kOutput, 0, false) == sdk::kResultOk,
            "deactivate output bus");
    host.channels = {nullptr, nullptr};
    require(process(host.processor, host.data) == sdk::kResultOk &&
            host.processor.engine_processor().frame_cursor() == 16,
            "inactive null output channels did not advance accepted audio");
}

void deferred_state() {
    ProcessorHost<double, 16> host;
    EventList events;
    events.count = 1;
    events.values[0] = note_on(0, 1);
    host.data.inputEvents = &events;
    require(process(host.processor, host.data) == sdk::kResultOk, "deferred initial render");
    const auto before_storage = std::make_unique<const HostEventProcessor>(host.processor.engine_processor());
    const auto& before = *before_storage;
    MemoryStream stream;
    stream.load({0.125F, true}, 0, kStateModel);
    stream.transfer_limit = 1;
    require(host.processor.setState(&stream) == sdk::kResultOk && stream.read_calls == 16,
            "processor short-read recall");
    require(host.processor.current_state() == InstrumentState{} &&
            host.processor.published_state() == InstrumentState{0.125F, true} &&
            host.processor.note_tracker().active_count() == 1 &&
            host.processor.engine_processor().frame_cursor() == before.frame_cursor(),
            "setState mutated the audio owner before a boundary");
    same_mechanics(host.processor.engine_processor().engine(), before.engine());
    MemoryStream saved;
    saved.transfer_limit = 1;
    require(host.processor.getState(&saved) == sdk::kResultOk && saved.write_calls == 16,
            "processor short-write save");
    saved.position = 0;
    require(read_state_stream(&saved, kStateModel) == InstrumentState{0.125F, true},
            "save omitted the pending published recall");
    stream.load({0.875F, false}, 0, kStateModel);
    require(host.processor.setState(&stream) == sdk::kResultOk, "newer pending recall");
    const ProcessorSnapshot pending{host.processor};
    stream.position = 0;
    stream.extent = 17;
    require(host.processor.setState(&stream) != sdk::kResultOk, "extended processor state accepted");
    pending.expect_unchanged(host.processor);
    events.count = 0;
    host.fill(7.0);
    require(process(host.processor, host.data) == sdk::kResultOk,
            "deferred recall was not applied at process boundary");
    require(host.processor.current_state() == InstrumentState{0.875F, false} &&
            host.processor.note_tracker().active_count() == 0 &&
            host.processor.engine_processor().frame_cursor() == 16 &&
            host.processor.engine_processor().engine().mechanical_energy_joules() == 0.0,
            "newest recall did not reset mechanics and ownership");
    host.expect_silence();

    stream.load({0.25F, true}, 0, kStateModel);
    require(host.processor.setState(&stream) == sdk::kResultOk, "flush-boundary recall");
    host.data.numSamples = 0;
    host.data.numOutputs = 0;
    host.data.outputs = nullptr;
    require(process(host.processor, host.data) == sdk::kResultOk &&
            host.processor.current_state() == InstrumentState{0.25F, true} &&
            host.processor.engine_processor().frame_cursor() == 0,
            "zero-frame boundary did not apply recall");
}

void recall_abort() {
    ProcessorHost<double, 16> host;
    EventList events;
    events.count = 1;
    events.values[0] = note_on(0, 1);
    host.data.inputEvents = &events;
    require(process(host.processor, host.data) == sdk::kResultOk, "abort initial render");
    MemoryStream stream;
    stream.load({0.125F, true}, 0, kStateModel);
    require(host.processor.setState(&stream) == sdk::kResultOk, "abort pending recall");
    const ProcessorSnapshot before{host.processor};
    events.count = 2;
    events.values[0] = note_on(0, 42, 60);
    events.values[1] = note_on(0, 42, 61);
    host.fill(7.0);
    require(process(host.processor, host.data) != sdk::kResultOk,
            "recall candidate ownership conflict accepted");
    before.expect_unchanged(host.processor);
    host.expect_silence();
    events.count = 0;
    require(process(host.processor, host.data) == sdk::kResultOk &&
            host.processor.current_state() == InstrumentState{0.125F, true} &&
            host.processor.note_tracker().active_count() == 0 &&
            host.processor.engine_processor().frame_cursor() == 16,
            "aborted recall was lost before the next valid block");
    host.expect_silence();

    events.count = 1;
    events.values[0] = note_on(0, 77, 60);
    require(process(host.processor, host.data) == sdk::kResultOk, "recall old identity");
    stream.load({0.25F, false}, 0, kStateModel);
    require(host.processor.setState(&stream) == sdk::kResultOk, "identity-reset recall");
    events.values[0] = note_on(0, 77, 61);
    require(process(host.processor, host.data) == sdk::kResultOk,
            "new recall incorrectly validated against stale ownership");
    const auto reference_storage = std::make_unique<ScalarEngine>();
    auto& reference = *reference_storage;
    require(reference.note_on(33, 0.75), "recall fresh reference strike");
    for (std::size_t frame = 0; frame < 16; ++frame) {
        expect_sample(host, frame, 8.0 * scalar_sample(reference));
    }
    require(host.processor.note_tracker().active_count() == 1 &&
            host.processor.note_tracker().held_count(33) == 1,
            "recall did not replace the old identity atomically");
}

void stream_errors() {
    constexpr InstrumentState state{0.125F, true};
    constexpr EncodedInstrumentState golden{
        'R', 'H', 'S', 'T', 1, 0, static_cast<std::uint8_t>(kStateModel), 0,
        0, 0, 0, 0x3e, 0, 0, 0x80, 0x3f};
    for (sdk::int32 chunk : {1, 3, 16}) {
        MemoryStream stream;
        stream.transfer_limit = chunk;
        require(write_state_stream(&stream, state, kStateModel), "short-write stream helper");
        require(std::equal(golden.begin(), golden.end(), stream.bytes.begin()),
                "stream output differs from independent golden bytes");
        const auto expected_calls = static_cast<std::size_t>((16 + chunk - 1) / chunk);
        require(stream.write_calls == expected_calls && stream.extent == 16,
                "short writes did not finish in a bounded number of calls");
        stream.position = 0;
        require(read_state_stream(&stream, kStateModel) == state &&
                stream.read_calls == expected_calls &&
                stream.position == 16, "short reads did not reconstruct the state");
    }
    MemoryStream prefixed;
    std::copy(golden.begin(), golden.end(), prefixed.bytes.begin() + 3);
    prefixed.position = 3;
    prefixed.extent = 19;
    require(read_state_stream(&prefixed, kStateModel) == state && prefixed.position == 19,
            "state stream at a nonzero starting position failed");
    require(!read_state_stream(nullptr, kStateModel) &&
            !write_state_stream(nullptr, state, kStateModel),
            "null state stream accepted");

    // Shared helper defaults keep model 1 in every selected executable.
    // Variant expectations above and below always pass their model explicitly.
    auto direct_golden = golden;
    direct_golden[6] = 1;
    MemoryStream default_writer;
    require(write_state_stream(&default_writer, state) &&
            std::equal(direct_golden.begin(), direct_golden.end(), default_writer.bytes.begin()),
            "default stream write no longer encodes the original model");
    default_writer.position = 0;
    require(read_state_stream(&default_writer) == state,
            "default stream read no longer accepts the original model");
    MemoryStream default_loader;
    default_loader.load(state);
    require(std::equal(direct_golden.begin(), direct_golden.end(), default_loader.bytes.begin()),
            "shared fixture loader default changed with the selected variant");
    for (const auto model : std::array<std::uint16_t, 3>{1, 2, 3}) {
        if (model == kStateModel) { continue; }
        MemoryStream other_model;
        other_model.load(state, 0, model);
        require(!read_state_stream(&other_model, kStateModel),
                "stream helper accepted another variant's model");
    }

    for (TransferFault fault : {TransferFault::result_error, TransferFault::zero_progress,
                                TransferFault::negative_count, TransferFault::overreported_count}) {
        for (std::size_t fail_at : {1U, 3U}) {
            MemoryStream reader;
            reader.load(state, 0, kStateModel);
            reader.transfer_limit = 1;
            reader.read_fault = fault;
            reader.read_fault_at = fail_at;
            require(!read_state_stream(&reader, kStateModel) && reader.read_calls == fail_at,
                    "invalid read transfer was accepted or retried");
            MemoryStream writer;
            writer.transfer_limit = 1;
            writer.write_fault = fault;
            writer.write_fault_at = fail_at;
            require(!write_state_stream(&writer, state, kStateModel) &&
                    writer.write_calls == fail_at,
                    "invalid write transfer was accepted or retried");
        }
    }
    for (int failure = 0; failure < 8; ++failure) {
        MemoryStream stream;
        stream.load(state, 0, kStateModel);
        if (failure == 0) { stream.fail_tell = true; }
        if (failure == 1) { stream.fail_seek_end = true; }
        if (failure == 2) { stream.fail_seek_set = true; }
        if (failure == 3) { stream.wrong_restored_position = true; }
        if (failure == 4) { stream.position = -1; }
        if (failure == 5) { stream.position = 17; }
        if (failure == 6) { stream.extent = 15; }
        if (failure == 7) { stream.extent = 17; }
        require(!read_state_stream(&stream, kStateModel) && stream.read_calls == 0,
                "invalid stream bounds/seek were read or accepted");
    }
    for (std::size_t byte : {0U, 4U, 6U, 12U}) {
        MemoryStream stream;
        stream.load(state, 0, kStateModel);
        stream.bytes[byte] = 0xff;
        require(!read_state_stream(&stream, kStateModel), "corrupt state payload accepted");
    }
    MemoryStream writer;
    require(!write_state_stream(&writer,
                {std::numeric_limits<float>::quiet_NaN(), false}, kStateModel) &&
            writer.write_calls == 0,
            "invalid state was written before validation");
}

void controller_mapping() {
    // Keep a nonnull host context alive through controller destruction.
    MemoryStream host_context;
    ControllerHost host;
    auto& controller = host.controller;
    require(controller.getParameterCount() == 2, "controller parameter count");
    vst::ParameterInfo gain_info{}, pedal_info{};
    require(controller.getParameterInfo(0, gain_info) == sdk::kResultOk &&
            controller.getParameterInfo(1, pedal_info) == sdk::kResultOk &&
            gain_info.id == kGainId && gain_info.defaultNormalizedValue == 0.5 &&
            gain_info.stepCount == 0 && pedal_info.id == kSustainId &&
            pedal_info.defaultNormalizedValue == 0.0 && pedal_info.stepCount == 1,
            "controller parameter metadata");
    require(controller.normalizedParamToPlain(kGainId, 0.5) == 16.0 &&
            controller.plainParamToNormalized(kGainId, 16.0) == 0.5,
            "monitoring gain conversion");
    for (sdk::int16 channel = 0; channel < 16; ++channel) {
        vst::ParamID tag = 999;
        require(controller.getMidiControllerAssignment(0, channel, vst::kCtrlSustainOnOff, tag)
                    == sdk::kResultOk && tag == kSustainId,
                "CC64 missing on a supported MIDI channel");
    }
    for (const auto& [bus, channel] :
         std::array<std::pair<sdk::int32, sdk::int16>, 4>{{{-1, 0}, {1, 0}, {0, -1}, {0, 16}}}) {
        vst::ParamID tag = 999;
        require(controller.getMidiControllerAssignment(bus, channel, vst::kCtrlSustainOnOff, tag)
                    != sdk::kResultOk && tag == 999,
                "invalid MIDI mapping changed the output tag");
    }
    vst::ParamID tag = 999;
    require(controller.getMidiControllerAssignment(0, 0, vst::kCtrlAllNotesOff, tag)
                != sdk::kResultOk && tag == 999, "unsupported CC123 mapped to a parameter");
    void* mapping = nullptr;
    require(controller.queryInterface(vst::IMidiMapping::iid, &mapping) == sdk::kResultOk &&
            mapping != nullptr, "controller did not expose IMidiMapping");
    (void)static_cast<vst::IMidiMapping*>(mapping)->release();

    for (double invalid : {-0.01, 1.01, std::numeric_limits<double>::quiet_NaN(),
                           std::numeric_limits<double>::infinity()}) {
        require(controller.setParamNormalized(kGainId, invalid) != sdk::kResultOk &&
                controller.getParamNormalized(kGainId) == 0.5,
                "invalid normalized gain mutated controller");
    }
    require(controller.setParamNormalized(999, 0.5) != sdk::kResultOk,
            "unknown controller parameter accepted");
    require(controller.setParamNormalized(kGainId, 0.123456789) == sdk::kResultOk &&
            controller.getParamNormalized(kGainId) == static_cast<double>(0.123456789F),
            "gain was not canonicalized to stored float precision");
    require(controller.setParamNormalized(kGainId, -0.0) == sdk::kResultOk &&
            !std::signbit(controller.getParamNormalized(kGainId)), "negative zero not canonicalized");
    require(controller.setParamNormalized(kSustainId, 0.499999) == sdk::kResultOk &&
            controller.getParamNormalized(kSustainId) == 0.0 &&
            controller.setParamNormalized(kSustainId, 0.5) == sdk::kResultOk &&
            controller.getParamNormalized(kSustainId) == 1.0, "sustain threshold differs");
    for (const auto& [value, expected] :
         std::array<std::pair<double, double>, 2>{{{0.0, 0.0}, {0.5, 1.0}}}) {
        vst::String128 label{};
        double parsed = -1.0;
        require(controller.getParamStringByValue(kSustainId, value, label) == sdk::kResultOk &&
                controller.getParamValueByString(kSustainId, label, parsed) == sdk::kResultOk &&
                parsed == expected, "sustain label round-trip failed");
        const auto* text = expected == 0.0 ? STR16("Off") : STR16("On");
        const std::size_t length = expected == 0.0 ? 4 : 3;
        require(std::equal(label, label + length, text), "sustain label is not Off/On");
    }

    MemoryStream stream;
    stream.load({0.125F, true}, 0, kStateModel);
    stream.transfer_limit = 1;
    require(controller.setComponentState(&stream) == sdk::kResultOk &&
            controller.getParamNormalized(kGainId) == 0.125 &&
            controller.getParamNormalized(kSustainId) == 1.0,
            "controller component-state recall");
    stream.position = 0;
    stream.extent = 17;
    require(controller.setComponentState(&stream) != sdk::kResultOk &&
            controller.getParamNormalized(kGainId) == 0.125 &&
            controller.getParamNormalized(kSustainId) == 1.0,
            "malformed component state partially changed controller");
    stream.load({0.25F, false}, 0, kStateModel);
    require(controller.setState(&stream) == sdk::kResultOk, "controller own-state recall");
    MemoryStream saved;
    saved.transfer_limit = 1;
    require(controller.getState(&saved) == sdk::kResultOk, "controller save");
    saved.position = 0;
    require(read_state_stream(&saved, kStateModel) == InstrumentState{0.25F, false},
            "controller state round-trip differs");

    const auto reject_duplicate = [&](sdk::FUnknown* context) {
        auto* gain = controller.getParameterObject(kGainId);
        auto* pedal = controller.getParameterObject(kSustainId);
        const auto gain_value = controller.getParamNormalized(kGainId);
        const auto pedal_value = controller.getParamNormalized(kSustainId);
        require(controller.initialize(context) != sdk::kResultOk,
                "duplicate controller initialization succeeded");
        require(controller.getParameterCount() == 2 &&
                controller.getParameterObject(kGainId) == gain &&
                controller.getParameterObject(kSustainId) == pedal &&
                controller.getParamNormalized(kGainId) == gain_value &&
                controller.getParamNormalized(kSustainId) == pedal_value,
                "duplicate initialization changed parameter objects or values");
    };
    require(controller.setParamNormalized(kSustainId, 1.0) == sdk::kResultOk,
            "controller lifecycle pedal setup");
    reject_duplicate(nullptr);
    reject_duplicate(&host_context);

    require(controller.terminate() == sdk::kResultOk &&
            controller.getParameterCount() == 0, "controller termination retained parameters");
    require(controller.initialize(&host_context) == sdk::kResultOk &&
            controller.getParameterCount() == 2 &&
            controller.getParamNormalized(kGainId) == 0.5 &&
            controller.getParamNormalized(kSustainId) == 0.0,
            "controller nonnull-context reinitialization failed");
    require(controller.setParamNormalized(kGainId, 0.75) == sdk::kResultOk &&
            controller.setParamNormalized(kSustainId, 1.0) == sdk::kResultOk,
            "reinitialized controller lifecycle setup");
    reject_duplicate(&host_context);
    reject_duplicate(nullptr);
    require(controller.terminate() == sdk::kResultOk &&
            controller.initialize(nullptr) == sdk::kResultOk &&
            controller.getParameterCount() == 2 &&
            controller.getParamNormalized(kGainId) == 0.5 &&
            controller.getParamNormalized(kSustainId) == 0.0,
            "controller null-context reinitialization failed");
}

void calibrate_allocation_probe() {
    // Direct replacement-operator calls prevent compiler allocation elision
    // from making this counter calibration vacuous in an optimized build.
    std::array<void*, 8> pointers{};
    std::size_t observed = 0;
    constexpr auto alignment = std::align_val_t{64};
    {
        allocation_probe::Scope scope;
        pointers[0] = ::operator new(8);
        pointers[1] = ::operator new[](8);
        pointers[2] = ::operator new(8, std::nothrow);
        pointers[3] = ::operator new[](8, std::nothrow);
        pointers[4] = ::operator new(8, alignment);
        pointers[5] = ::operator new[](8, alignment);
        pointers[6] = ::operator new(8, alignment, std::nothrow);
        pointers[7] = ::operator new[](8, alignment, std::nothrow);
        observed = allocation_probe::attempts;
    }
    const bool all_allocated = std::all_of(pointers.begin(), pointers.end(),
                                          [](const void* p) { return p != nullptr; });
    ::operator delete(pointers[0]);
    ::operator delete[](pointers[1]);
    ::operator delete(pointers[2], std::nothrow);
    ::operator delete[](pointers[3], std::nothrow);
    ::operator delete(pointers[4], alignment);
    ::operator delete[](pointers[5], alignment);
    ::operator delete(pointers[6], alignment, std::nothrow);
    ::operator delete[](pointers[7], alignment, std::nothrow);
    require(observed == pointers.size(), "allocation trap failed its Release self-check");
    require(all_allocated,
            "allocation calibration could not allocate all eight forms");
}

template<class Sample>
void allocation_guard_at_rate(double rate) {
    ProcessorHost<Sample, 64> host{rate};
    EventList events;
    events.count = 1;
    events.values[0] = note_on(0, 1);
    const auto gain_storage = std::make_unique<ParameterQueue>();
    auto& gain = *gain_storage;
    gain.count = 2;
    gain.values[0] = {0, 0.25};
    gain.values[1] = {63, 0.75};
    ParameterChanges parameters;
    parameters.count = 1;
    parameters.values[0] = &gain;
    host.data.inputEvents = &events;
    host.data.inputParameterChanges = &parameters;
    for (int block = 0; block < 16; ++block) {
        require(process(host.processor, host.data) == sdk::kResultOk,
                "guarded repeated render failed");
    }
    gain.values[1].value = std::numeric_limits<double>::quiet_NaN();
    require(process(host.processor, host.data) != sdk::kResultOk,
            "guarded malformed input accepted");
    gain.count = 1;
    events.count = 0;
    host.data.numSamples = 0;
    host.data.numOutputs = 0;
    host.data.outputs = nullptr;
    require(process(host.processor, host.data) == sdk::kResultOk, "guarded flush failed");
    MemoryStream stream;
    stream.load({0.125F, true}, 0, kStateModel);
    require(host.processor.setState(&stream) == sdk::kResultOk, "guarded recall setup");
    require(process(host.processor, host.data) == sdk::kResultOk, "guarded recall boundary failed");
}

void allocation_guard() {
    // Exercise both negotiated output formats and every filtered rate branch.
    // Construction and stream I/O are outside the measured process() interval.
    for (double rate : {48000.0, 96000.0, 192000.0}) {
        allocation_guard_at_rate<float>(rate);
        allocation_guard_at_rate<double>(rate);
    }
}

using Fixture = std::pair<std::string_view, void (*)()>;
constexpr std::array<Fixture, 12> fixtures{{
    {"scalar_output", scalar_output},
    {"overlapping_notes", overlapping_notes},
    {"linear_automation", linear_automation},
    {"dense_automation", dense_automation},
    {"invalid_atomicity", invalid_atomicity},
    {"parameter_flush", parameter_flush},
    {"lifecycle", lifecycle},
    {"deferred_state", deferred_state},
    {"recall_abort", recall_abort},
    {"stream_errors", stream_errors},
    {"controller_mapping", controller_mapping},
    {"allocation_guard", allocation_guard}
}};

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: rhodes_vst3_host_tests <fixture>\nAvailable fixtures:\n";
        for (const auto& [name, unused] : fixtures) {
            (void)unused;
            std::cerr << "  " << name << '\n';
        }
        return 2;
    }
    const std::string_view requested{argv[1]};
    for (const auto& [name, run] : fixtures) {
        if (name != requested) { continue; }
        try {
            calibrate_allocation_probe();
            run();
            std::cout << "PASS " << name << " (" << allocation_probe::process_calls
                      << " allocation-guarded process calls; 8 allocation forms calibrated)\n";
            return 0;
        } catch (const std::exception& error) {
            std::cerr << "FAIL " << name << ": " << error.what() << '\n';
            return 1;
        }
    }
    std::cerr << "Unknown fixture: " << requested << '\n';
    return 2;
}
