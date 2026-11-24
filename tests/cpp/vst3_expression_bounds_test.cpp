#include "tests/cpp/vst3_test_support.hpp"

#include <bit>
#include <cstdlib>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <new>
#include <utility>
#if defined(_MSC_VER)
#include <malloc.h>
#endif

// All host storage, snapshots and scalar references are prepared outside the
// process() interval. These replacements observe C++ allocation/deallocation
// reached by this executable, including its deliberately constant-work getters.
// They do not instrument arbitrary malloc, locks, foreign hosts or deadlines.
namespace expression_allocation_probe {
thread_local bool enabled = false;
thread_local std::size_t allocations = 0;
thread_local std::size_t deallocations = 0;

void* allocate(std::size_t bytes) {
    if (enabled) { ++allocations; }
    if (void* memory = std::malloc(bytes == 0 ? 1 : bytes)) { return memory; }
    throw std::bad_alloc{};
}

// Prevent GCC's mismatched-new-delete heuristic from inlining through the
// intentional C++-operator-to-C-allocator bridge during probe calibration.
#if defined(_MSC_VER)
__declspec(noinline)
#elif defined(__GNUC__)
__attribute__((noinline))
#endif
void deallocate(void* memory) noexcept {
    if (enabled) { ++deallocations; }
    std::free(memory);
}

void* allocate_aligned(std::size_t bytes, std::align_val_t alignment) {
    if (enabled) { ++allocations; }
    const auto align = static_cast<std::size_t>(alignment);
    bytes = bytes == 0 ? 1 : bytes;
    if (bytes > std::numeric_limits<std::size_t>::max() - (align - 1)) {
        throw std::bad_alloc{};
    }
#if defined(_MSC_VER)
    void* memory = _aligned_malloc(bytes, align);
#else
    void* memory = std::aligned_alloc(align, ((bytes + align - 1) / align) * align);
#endif
    if (memory != nullptr) { return memory; }
    throw std::bad_alloc{};
}

void deallocate_aligned(void* memory) noexcept {
#if defined(_MSC_VER)
    if (enabled) { ++deallocations; }
    _aligned_free(memory);
#else
    deallocate(memory);
#endif
}

struct Scope {
    Scope() noexcept { allocations = deallocations = 0; enabled = true; }
    ~Scope() { enabled = false; }
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
};
} // namespace expression_allocation_probe

void* operator new(std::size_t bytes) { return expression_allocation_probe::allocate(bytes); }
void* operator new[](std::size_t bytes) { return expression_allocation_probe::allocate(bytes); }
void operator delete(void* memory) noexcept { expression_allocation_probe::deallocate(memory); }
void operator delete[](void* memory) noexcept { expression_allocation_probe::deallocate(memory); }
void operator delete(void* memory, std::size_t) noexcept {
    expression_allocation_probe::deallocate(memory);
}
void operator delete[](void* memory, std::size_t) noexcept {
    expression_allocation_probe::deallocate(memory);
}
void* operator new(std::size_t bytes, const std::nothrow_t&) noexcept {
    try { return expression_allocation_probe::allocate(bytes); } catch (...) { return nullptr; }
}
void* operator new[](std::size_t bytes, const std::nothrow_t&) noexcept {
    try { return expression_allocation_probe::allocate(bytes); } catch (...) { return nullptr; }
}
void operator delete(void* memory, const std::nothrow_t&) noexcept {
    expression_allocation_probe::deallocate(memory);
}
void operator delete[](void* memory, const std::nothrow_t&) noexcept {
    expression_allocation_probe::deallocate(memory);
}
void* operator new(std::size_t bytes, std::align_val_t alignment) {
    return expression_allocation_probe::allocate_aligned(bytes, alignment);
}
void* operator new[](std::size_t bytes, std::align_val_t alignment) {
    return expression_allocation_probe::allocate_aligned(bytes, alignment);
}
void operator delete(void* memory, std::align_val_t) noexcept {
    expression_allocation_probe::deallocate_aligned(memory);
}
void operator delete[](void* memory, std::align_val_t) noexcept {
    expression_allocation_probe::deallocate_aligned(memory);
}
void operator delete(void* memory, std::size_t, std::align_val_t) noexcept {
    expression_allocation_probe::deallocate_aligned(memory);
}
void operator delete[](void* memory, std::size_t, std::align_val_t) noexcept {
    expression_allocation_probe::deallocate_aligned(memory);
}
void* operator new(std::size_t bytes, std::align_val_t alignment,
                   const std::nothrow_t&) noexcept {
    try { return expression_allocation_probe::allocate_aligned(bytes, alignment); }
    catch (...) { return nullptr; }
}
void* operator new[](std::size_t bytes, std::align_val_t alignment,
                     const std::nothrow_t&) noexcept {
    try { return expression_allocation_probe::allocate_aligned(bytes, alignment); }
    catch (...) { return nullptr; }
}
void operator delete(void* memory, std::align_val_t, const std::nothrow_t&) noexcept {
    expression_allocation_probe::deallocate_aligned(memory);
}
void operator delete[](void* memory, std::align_val_t, const std::nothrow_t&) noexcept {
    expression_allocation_probe::deallocate_aligned(memory);
}

namespace {
using namespace rhodes;
using namespace rhodes::vst3;
using namespace rhodes::vst3::test_host;
using ScalarEngine = std::remove_cvref_t<
    decltype(std::declval<const HostEventProcessor&>().engine())>;

static_assert(kGainId == 0 && kSustainId == 1 && kExpressionId == 2);
static_assert(kAllNotesOffBaseId == 0x1000 && kControllerParameterCount == 24);
static_assert(kMaximumHostFrames == 8192 && kMaximumChannelCommands == 256);
static_assert(maximum_block_events == 256 && key_count == 73);
constexpr std::size_t frames = 128;
constexpr std::size_t maximum_point_reads = 3 * 8192 + 256;

struct Traversal {
    std::size_t parameter_counts = 0;
    std::size_t queues = 0;
    std::size_t ids = 0;
    std::size_t point_counts = 0;
    std::size_t points = 0;
    std::size_t event_counts = 0;
    std::size_t events = 0;
};
Traversal last{};
struct Summary {
    std::size_t callbacks = 0;
    std::size_t accepted = 0;
    std::size_t rejected = 0;
    std::size_t point_reads = 0;
    std::size_t event_reads = 0;
    std::size_t peak_points = 0;
    std::size_t peak_events = 0;
} summary;

struct Point { sdk::int32 offset = 0; double value = 0.0; };

struct Queue final : FixedHostObject<vst::IParamValueQueue> {
    std::span<Point> values;
    vst::ParamID id = 0;
    sdk::int32 count = 0;
    sdk::int32 failed_index = -1;
    std::size_t id_calls = 0;
    std::size_t count_calls = 0;
    std::size_t point_calls = 0;

    void reset_calls() noexcept { id_calls = count_calls = point_calls = 0; }
    vst::ParamID PLUGIN_API getParameterId() override { ++last.ids; ++id_calls; return id; }
    sdk::int32 PLUGIN_API getPointCount() override {
        ++last.point_counts; ++count_calls; return count;
    }
    sdk::tresult PLUGIN_API getPoint(sdk::int32 index, sdk::int32& offset,
                                    vst::ParamValue& value) override {
        ++last.points;
        ++point_calls;
        if (index < 0 || index >= count || index == failed_index ||
            static_cast<std::size_t>(index) >= values.size()) { return sdk::kResultFalse; }
        const auto& point = values[static_cast<std::size_t>(index)];
        offset = point.offset;
        value = point.value;
        return sdk::kResultOk;
    }
    sdk::tresult PLUGIN_API addPoint(sdk::int32, vst::ParamValue, sdk::int32&) override {
        return sdk::kNotImplemented;
    }
};

struct Changes final : FixedHostObject<vst::IParameterChanges> {
    std::array<vst::IParamValueQueue*, 19> values{};
    sdk::int32 count = 0;
    sdk::int32 PLUGIN_API getParameterCount() override { ++last.parameter_counts; return count; }
    vst::IParamValueQueue* PLUGIN_API getParameterData(sdk::int32 index) override {
        ++last.queues;
        return index >= 0 && index < count && static_cast<std::size_t>(index) < values.size() ?
            values[static_cast<std::size_t>(index)] : nullptr;
    }
    vst::IParamValueQueue* PLUGIN_API addParameterData(const vst::ParamID&,
                                                     sdk::int32&) override { return nullptr; }
};

struct Events final : FixedHostObject<vst::IEventList> {
    std::array<vst::Event, 256> values{};
    sdk::int32 count = 0;
    sdk::int32 PLUGIN_API getEventCount() override { ++last.event_counts; return count; }
    sdk::tresult PLUGIN_API getEvent(sdk::int32 index, vst::Event& event) override {
        ++last.events;
        if (index < 0 || index >= count || static_cast<std::size_t>(index) >= values.size()) {
            return sdk::kResultFalse;
        }
        event = values[static_cast<std::size_t>(index)];
        return sdk::kResultOk;
    }
    sdk::tresult PLUGIN_API addEvent(vst::Event&) override { return sdk::kNotImplemented; }
};

// This legacy traffic oracle keeps three persistent queues and sixteen command
// queues. Appended panel controls do not change this fixture. Only three queues
// need 8192 raw points; commands need 256 each. Traffic is always heap-owned.
struct Traffic {
    std::array<std::array<Point, 8192>, 3> persistent{};
    std::array<std::array<Point, 256>, 16> commands{};
    std::array<Queue, 19> queues{};
    Changes changes;
    Events events;

    Traffic() {
        for (std::size_t index = 0; index < 19; ++index) {
            auto& queue = queues[index];
            queue.id = index < 3 ? static_cast<vst::ParamID>(index) :
                0x1000U + static_cast<vst::ParamID>(index - 3);
            queue.values = index < 3 ? std::span<Point>{persistent[index]} :
                                      std::span<Point>{commands[index - 3]};
            queue.count = 1;
            queue.values[0] = {0, index == 0 ? 0.25 : index == 2 ? 0.5 : 0.0};
            changes.values[index] = &queue;
        }
        changes.count = 19;
    }
    void reset_calls() noexcept { for (auto& queue : queues) { queue.reset_calls(); } }
    void select(std::initializer_list<std::size_t> order) {
        require(order.size() <= changes.values.size(), "invalid fixture queue order");
        changes.count = static_cast<sdk::int32>(order.size());
        std::size_t index = 0;
        for (const auto queue : order) { changes.values[index++] = &queues.at(queue); }
    }
    void expression_last() noexcept {
        changes.count = 19;
        changes.values[0] = &queues[0];
        changes.values[1] = &queues[1];
        for (std::size_t index = 3; index < 19; ++index) {
            changes.values[index - 1] = &queues[index];
        }
        changes.values[18] = &queues[2];
    }
};

sdk::tresult process(RhodesProcessor& processor, vst::ProcessData& data) {
    last = {};
    sdk::tresult result = sdk::kResultFalse;
    std::size_t allocations = 0, deallocations = 0;
    {
        expression_allocation_probe::Scope scope;
        result = processor.process(data);
        allocations = expression_allocation_probe::allocations;
        deallocations = expression_allocation_probe::deallocations;
    }
    ++summary.callbacks;
    if (result == sdk::kResultOk) { ++summary.accepted; }
    else { ++summary.rejected; }
    summary.point_reads += last.points;
    summary.event_reads += last.events;
    summary.peak_points = std::max(summary.peak_points, last.points);
    summary.peak_events = std::max(summary.peak_events, last.events);
    require(allocations == 0, "process() reached a C++ allocation");
    require(deallocations == 0, "process() reached a C++ deallocation");
    require(last.parameter_counts <= 1 && last.queues <= 19 && last.ids <= last.queues &&
            last.point_counts <= last.ids && last.points <= maximum_point_reads &&
            last.event_counts <= 1 && last.events <= 256,
            "process() exceeded a host getter traversal bound");
    return result;
}

void expect_reads(std::size_t queues, std::size_t ids, std::size_t counts,
                  std::size_t points, std::size_t event_counts = 0,
                  std::size_t events = 0) {
    require(last.parameter_counts == 1 && last.queues == queues && last.ids == ids &&
            last.point_counts == counts && last.points == points &&
            last.event_counts == event_counts && last.events == events,
            "unexpected exact host getter call counts");
}

template<class Sample, std::size_t Frames>
void reject(ProcessorHost<Sample, Frames>& host) {
    const auto before = std::make_unique<ProcessorSnapshot>(host.processor);
    host.fill(Sample{7});
    require(process(host.processor, host.data) == sdk::kResultFalse, "invalid transaction accepted");
    if (host.data.numOutputs == 1 && host.data.numSamples > 0) {
        const auto count = static_cast<std::size_t>(host.data.numSamples);
        require(std::all_of(host.left.begin(), host.left.begin() +
                           static_cast<std::ptrdiff_t>(count),
                           [](Sample value) { return value == Sample{0}; }),
                "rejection did not clear the supplied left channel");
        if (host.output.numChannels == 2) { host.expect_silence(count); }
        else { require(host.output.silenceFlags == 1, "rejected mono silence mask"); }
    }
    before->expect_unchanged(host.processor);
    // Cover more than the 96-frame FIR history, before any pending recall can
    // hide corruption. Copies and checks run outside the guarded callback.
    const auto actual = std::make_unique<ScalarEngine>(host.processor.engine_processor().engine());
    const auto expected = std::make_unique<ScalarEngine>(before->engine.engine());
    std::array<double, 129> actual_tail{}, expected_tail{};
    require(actual->process(actual_tail) && expected->process(expected_tail) &&
            actual_tail == expected_tail, "rejected transaction changed future engine samples");
}

vst::Event pressure(sdk::int32 offset, sdk::int16 channel = 0) {
    vst::Event event{};
    event.type = vst::Event::kPolyPressureEvent;
    event.sampleOffset = offset;
    event.polyPressure.channel = channel;
    event.polyPressure.pitch = 68;
    event.polyPressure.pressure = 0.5F;
    event.polyPressure.noteId = -1;
    return event;
}

template<class Sample, std::size_t Frames>
void seed(ProcessorHost<Sample, Frames>& host, Events& events, bool all_keys = false) {
    events.count = all_keys ? 73 : 1;
    for (sdk::int32 index = 0; index < events.count; ++index) {
        events.values[static_cast<std::size_t>(index)] = note_on(0, index,
            static_cast<sdk::int16>(all_keys ? 28 + index : 68), 0.25F,
            static_cast<sdk::int16>(all_keys ? index % 16 : 0));
    }
    host.data.inputParameterChanges = nullptr;
    host.data.inputEvents = &events;
    require(process(host.processor, host.data) == sdk::kResultOk, "seed note block failed");
    host.data.inputEvents = nullptr;
}

void recall(RhodesProcessor& processor, InstrumentState state) {
    MemoryStream stream;
    stream.bytes = {'R', 'H', 'S', 'T', 2, 0, static_cast<std::uint8_t>(kStateModel), 0};
    const auto word = [&](std::size_t offset, float value) {
        const auto bits = std::bit_cast<std::uint32_t>(value);
        for (std::size_t byte = 0; byte < 4; ++byte) {
            stream.bytes[offset + byte] = static_cast<std::uint8_t>(bits >> (8 * byte));
        }
    };
    word(8, state.monitoring_gain);
    word(12, state.sustain ? 1.0F : 0.0F);
    word(16, state.expression);
    stream.extent = 20;
    require(processor.setState(&stream) == sdk::kResultOk, "independent v2 recall fixture failed");
}

template<class Sample>
void queue_limits_for() {
    const auto host = std::make_unique<ProcessorHost<Sample, frames>>();
    const auto traffic = std::make_unique<Traffic>();
    auto& q = traffic->queues;
    host->data.inputParameterChanges = &traffic->changes;
    const std::array<std::array<std::size_t, 19>, 3> orders{{
        {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18},
        {18, 17, 16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0},
        {0, 1, 3, 2, 18, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17}
    }};
    for (const auto& order : orders) {
        traffic->reset_calls();
        for (std::size_t index = 0; index < order.size(); ++index) {
            traffic->changes.values[index] = &q[order[index]];
        }
        require(process(host->processor, host->data) == sdk::kResultOk,
                "nineteen distinct queues rejected");
        expect_reads(19, 19, 19, 19);
        for (const auto& queue : q) {
            require(queue.id_calls == 1 && queue.count_calls == 1 && queue.point_calls == 1,
                    "a distinct queue was skipped or revisited");
        }
        require(host->processor.current_state() == InstrumentState{0.25F, false, 0.5F} &&
                host->processor.current_state() == host->processor.published_state(),
                "nineteen-queue control state differs");
    }
    seed(*host, traffic->events);
    host->data.inputParameterChanges = &traffic->changes;
    host->data.inputEvents = &traffic->events;
    for (const sdk::int32 count : {-1, 25, std::numeric_limits<sdk::int32>::max()}) {
        traffic->changes.count = count;
        traffic->reset_calls();
        reject(*host);
        expect_reads(0, 0, 0, 0);
    }
    std::array<Point, 1> duplicate_values{{{0, 0.75}}};
    Queue duplicate;
    duplicate.values = duplicate_values;
    duplicate.count = 1;
    traffic->changes.count = 2;
    for (std::size_t index = 0; index < 19; ++index) {
        traffic->changes.values[0] = &q[index];
        traffic->changes.values[1] = &duplicate;
        duplicate.id = q[index].id;
        traffic->reset_calls();
        duplicate.reset_calls();
        reject(*host);
        expect_reads(2, 2, 1, 1);
        require(q[index].point_calls == 1 && duplicate.id_calls == 1 &&
                duplicate.count_calls == 0 && duplicate.point_calls == 0,
                "duplicate queue reached its point-count or point getter");
    }
}

template<class Sample>
void persistent_limits_for() {
    const auto host = std::make_unique<ProcessorHost<Sample, frames>>();
    const auto traffic = std::make_unique<Traffic>();
    InstrumentState expected{};
    const std::array<double, 3> final_values{0.75, 1.0, 0.375};
    const std::array<double, 3> flushed_values{0.25, 0.0, 0.625};
    for (std::size_t index = 0; index < 3; ++index) {
        auto& queue = traffic->queues[index];
        traffic->select({index});
        host->data.inputParameterChanges = &traffic->changes;
        for (auto& point : queue.values) { point = {0, 0.0}; }
        queue.values.back().value = final_values[index];
        for (const sdk::int32 count : {-1, 8193, std::numeric_limits<sdk::int32>::max()}) {
            queue.count = count;
            traffic->reset_calls();
            reject(*host);
            expect_reads(1, 1, 1, 0);
            require(queue.point_calls == 0, "oversized persistent queue read raw points");
        }
        queue.count = 8192;
        traffic->reset_calls();
        require(process(host->processor, host->data) == sdk::kResultOk,
                "8192 duplicate persistent points rejected");
        expect_reads(1, 1, 1, 8192);
        if (index == 0) { expected.monitoring_gain = 0.75F; }
        else if (index == 1) { expected.sustain = true; }
        else { expected.expression = 0.375F; }
        require(host->processor.current_state() == expected &&
                host->processor.published_state() == expected,
                "raw duplicate points did not retain the final value");

        const auto cursor = host->processor.engine_processor().frame_cursor();
        host->data.numSamples = 0;
        host->data.numOutputs = 0;
        host->data.outputs = nullptr;
        host->output.silenceFlags = 0x5a;
        host->fill(Sample{7});
        queue.values.back().value = flushed_values[index];
        require(process(host->processor, host->data) == sdk::kResultOk,
                "8192 duplicate persistent flush points rejected");
        expect_reads(1, 1, 1, 8192);
        if (index == 0) { expected.monitoring_gain = 0.25F; }
        else if (index == 1) { expected.sustain = false; }
        else { expected.expression = 0.625F; }
        require(host->processor.current_state() == expected &&
                host->processor.published_state() == expected &&
                host->processor.engine_processor().frame_cursor() == cursor &&
                host->output.silenceFlags == 0x5a &&
                std::all_of(host->left.begin(), host->left.end(),
                            [](Sample value) { return value == Sample{7}; }) &&
                std::all_of(host->right.begin(), host->right.end(),
                            [](Sample value) { return value == Sample{7}; }),
                "persistent flush changed time/buffers or missed its final value");
        queue.count = 8193;
        reject(*host);
        expect_reads(1, 1, 1, 0);
        host->data.numSamples = static_cast<sdk::int32>(frames);
        host->data.numOutputs = 1;
        host->data.outputs = &host->output;
    }
}

template<class Sample>
void maximum_points_for() {
    const auto host = std::make_unique<ProcessorHost<Sample, 8192>>();
    const auto traffic = std::make_unique<Traffic>();
    traffic->expression_last();
    for (std::size_t control = 0; control < 3; ++control) {
        auto& queue = traffic->queues[control];
        queue.count = 8192;
        for (std::size_t point = 0; point < 8192; ++point) {
            queue.values[point] = {static_cast<sdk::int32>(point),
                                  control == 0 ? 0.25 : control == 2 ? 0.75 : 0.0};
        }
    }
    for (std::size_t channel = 0; channel < 16; ++channel) {
        auto& queue = traffic->queues[3 + channel];
        queue.count = 16;
        for (std::size_t point = 0; point < 16; ++point) {
            queue.values[point] = {static_cast<sdk::int32>(point * 512), 0.0};
        }
    }
    traffic->events.count = 256;
    for (std::size_t index = 0; index < 256; ++index) {
        traffic->events.values[index] = pressure(static_cast<sdk::int32>(index * 32));
    }
    traffic->events.values[0] = note_on(0, 700, 68, 0.25F);
    host->data.inputParameterChanges = &traffic->changes;
    host->data.inputEvents = &traffic->events;
    require(process(host->processor, host->data) == sdk::kResultOk,
            "full 24832-point plus 256-event block rejected");
    expect_reads(19, 19, 19, maximum_point_reads, 1, 256);
    require(traffic->queues[2].point_calls == 8192 &&
            host->processor.current_state() == InstrumentState{0.25F, false, 0.75F} &&
            host->processor.published_state() == host->processor.current_state() &&
            host->processor.engine_processor().frame_cursor() == 8192 &&
            std::any_of(host->left.begin(), host->left.end(),
                        [](Sample value) { return value != Sample{0}; }),
            "maximum-point accepted block lacks state, time or audio witness");

    const InstrumentState recalled{0.625F, true, 0.125F};
    recall(host->processor, recalled);
    auto& expression = traffic->queues[2];
    for (auto& point : expression.values) { point.value = 0.0; }
    for (int malformed = 0; malformed < 4; ++malformed) {
        expression.values.back() = {8191, 0.0};
        expression.failed_index = -1;
        if (malformed == 0) { expression.values.back().value = std::numeric_limits<double>::quiet_NaN(); }
        else if (malformed == 1) { expression.failed_index = 8191; }
        else if (malformed == 2) { expression.values.back().offset = 8192; }
        else { expression.values.back().offset = 8189; }
        traffic->reset_calls();
        reject(*host);
        expect_reads(19, 19, 19, maximum_point_reads);
        require(expression.point_calls == 8192 &&
                host->processor.current_state() == InstrumentState{0.25F, false, 0.75F} &&
                host->processor.published_state() == recalled,
                "last expression point did not preserve live state and pending recall");
        std::size_t preceding = 0;
        for (std::size_t index = 0; index < 19; ++index) {
            if (index != 2) { preceding += traffic->queues[index].point_calls; }
        }
        require(preceding == 16640, "late expression rejection prefix is not 16640 points");
    }
    host->data.numSamples = 0;
    host->data.numOutputs = 0;
    host->data.outputs = nullptr;
    host->data.inputParameterChanges = nullptr;
    host->data.inputEvents = nullptr;
    require(process(host->processor, host->data) == sdk::kResultOk &&
            host->processor.current_state() == recalled &&
            host->processor.published_state() == recalled &&
            host->processor.engine_processor().frame_cursor() == 0 &&
            host->processor.note_tracker().active_count() == 0,
            "rejected maximum input lost the pending recall");

    expression.failed_index = -1;
    expression.values.back().value = 0.875;
    for (auto& queue : traffic->queues) {
        for (auto& point : queue.values) { point.offset = 0; }
    }
    host->data.inputParameterChanges = &traffic->changes;
    require(process(host->processor, host->data) == sdk::kResultOk,
            "full 24832-raw-point duplicate flush rejected");
    expect_reads(19, 19, 19, maximum_point_reads);
    require(host->processor.current_state() == InstrumentState{0.25F, false, 0.875F} &&
            host->processor.engine_processor().frame_cursor() == 0,
            "maximum duplicate flush changed time or ignored its final expression");
}

template<class Sample>
void command_events_for() {
    const auto host = std::make_unique<ProcessorHost<Sample, frames>>();
    const auto traffic = std::make_unique<Traffic>();
    seed(*host, traffic->events, true);
    host->data.inputParameterChanges = &traffic->changes;
    host->data.inputEvents = &traffic->events;
    for (std::size_t index = 3; index < 19; ++index) {
        traffic->queues[index].count = 16;
        for (auto& point : traffic->queues[index].values) { point = {31, 0.0}; }
    }
    traffic->events.count = 256;
    for (auto& event : traffic->events.values) { event = pressure(31); }

    for (const sdk::int32 count : {257, std::numeric_limits<sdk::int32>::max()}) {
        traffic->queues[3].count = count;
        traffic->reset_calls();
        reject(*host);
        expect_reads(4, 4, 4, 3);
        require(traffic->queues[3].point_calls == 0,
                "oversized first command queue read its points");
    }
    traffic->queues[3].count = 16;
    traffic->queues[18].count = 17;
    traffic->reset_calls();
    reject(*host);
    expect_reads(19, 19, 19, 3 + 240);
    require(traffic->queues[18].point_calls == 0, "aggregate 257th command queue read points");

    for (std::size_t index = 3; index < 19; ++index) { traffic->queues[index].count = 0; }
    traffic->queues[3].count = 256;
    traffic->queues[4].count = 1;
    traffic->reset_calls();
    reject(*host);
    expect_reads(5, 5, 5, 3 + 256);
    require(traffic->queues[3].point_calls == 256 && traffic->queues[4].point_calls == 0,
            "raw repeated commands were deduplicated or the excess queue was read");

    for (std::size_t index = 3; index < 19; ++index) { traffic->queues[index].count = 16; }
    for (const sdk::int32 count : {-1, 257, std::numeric_limits<sdk::int32>::max()}) {
        traffic->events.count = count;
        reject(*host);
        expect_reads(19, 19, 19, 3 + 256, 1, 0);
    }
    traffic->events.count = 256;
    traffic->events.values.back().polyPressure.pressure = 1.25F;
    reject(*host);
    expect_reads(19, 19, 19, 3 + 256, 1, 256);
    traffic->events.values.back().polyPressure.pressure = 0.5F;
    require(process(host->processor, host->data) == sdk::kResultOk,
            "simultaneous 256-command and 256-event budgets were combined or rejected");
    expect_reads(19, 19, 19, 3 + 256, 1, 256);
    require(host->processor.note_tracker().active_count() == 0 &&
            host->processor.current_state() == InstrumentState{0.25F, false, 0.5F},
            "accepted command/event boundary lost releases or expression");
}

float intensity(std::size_t index) { return 0.125F + 0.0625F * static_cast<float>(index % 4); }
sdk::int16 pitch(std::size_t key) { return static_cast<sdk::int16>(28 + key); }

// Independent holder counts and a scalar core establish the actual expansion,
// rather than assuming that an accepted return means all releases happened.
struct Oracle {
    std::unique_ptr<ScalarEngine> engine = std::make_unique<ScalarEngine>();
    std::array<std::size_t, 73> holders{};
    InstrumentState state{};
    std::size_t strikes = 0, releases = 0;
    std::uint64_t cursor = 0;
    Oracle() { require(engine->prepare(48000.0), "scalar oracle prepare failed"); }
    void strike(std::size_t key, float velocity) {
        require(engine->note_on(key, static_cast<double>(velocity)), "scalar oracle strike failed");
        ++holders[key]; ++strikes;
    }
    void release_all() {
        for (std::size_t key = 0; key < 73; ++key) {
            if (holders[key] == 0) { continue; }
            require(engine->note_off(key), "scalar oracle release failed");
            holders[key] = 0; ++releases;
        }
    }
    void sustain(bool down) { engine->set_sustain(down); state.sustain = down; }
    template<class Sample, class Actions>
    void compare(ProcessorHost<Sample, frames>& host, Actions actions) {
        require(process(host.processor, host.data) == sdk::kResultOk,
                "valid expression/dispatch boundary rejected");
        using Bits = std::conditional_t<std::is_same_v<Sample, float>, std::uint32_t, std::uint64_t>;
        bool nonzero = false;
        for (std::size_t frame = 0; frame < frames; ++frame) {
            actions(frame);
            double volts = 0.0;
            require(engine->process(std::span<double>(&volts, 1)), "scalar oracle render failed");
            const double monitored = (32.0 * static_cast<double>(state.monitoring_gain)) * volts;
            const Sample expected = static_cast<Sample>(state.expression == 1.0F ? monitored :
                monitored * static_cast<double>(state.expression));
            require(std::bit_cast<Bits>(host.left[frame]) == std::bit_cast<Bits>(expected) &&
                    std::bit_cast<Bits>(host.right[frame]) == std::bit_cast<Bits>(expected),
                    "expression/dispatch audio differs from scalar oracle");
            nonzero = nonzero || expected != Sample{0};
        }
        cursor += frames;
        require(host.output.silenceFlags == (nonzero ? 0U : 3U) &&
                host.processor.engine_processor().frame_cursor() == cursor &&
                host.processor.current_state() == state && host.processor.published_state() == state &&
                !host.processor.requires_reset(), "dispatch state, cursor, silence or fault differs");
        std::size_t total = 0;
        for (std::size_t key = 0; key < 73; ++key) {
            total += holders[key];
            require(host.processor.note_tracker().held_count(key) == holders[key],
                    "dispatch per-key ownership differs");
        }
        require(host.processor.note_tracker().active_count() == total, "dispatch owner total differs");
        same_mechanics(host.processor.engine_processor().engine(), *engine);
    }
    template<class Sample>
    void prehold(ProcessorHost<Sample, frames>& host, Events& events) {
        events.count = 73;
        for (std::size_t key = 0; key < 73; ++key) {
            events.values[key] = note_on(0, static_cast<sdk::int32>(key), pitch(key), intensity(key));
        }
        host.data.inputEvents = &events;
        compare(host, [&](std::size_t frame) {
            if (frame == 0) {
                for (std::size_t key = 0; key < 73; ++key) { strike(key, intensity(key)); }
            }
        });
    }
};

template<class Sample>
void dispatch_for() {
    {
        const auto host = std::make_unique<ProcessorHost<Sample, frames>>();
        const auto traffic = std::make_unique<Traffic>();
        const auto oracle = std::make_unique<Oracle>();
        oracle->prehold(*host, traffic->events);
        constexpr std::array<std::size_t, 4> sizes{73, 73, 73, 37};
        constexpr std::array<sdk::int32, 5> offsets{0, 32, 64, 96, 127};
        std::size_t index = 0;
        for (std::size_t group = 0; group < sizes.size(); ++group) {
            for (std::size_t key = 0; key < sizes[group]; ++key) {
                traffic->events.values[index] = note_on(offsets[group],
                    static_cast<sdk::int32>(1000 + index), pitch(key), intensity(index));
                ++index;
            }
        }
        require(index == 256, "full expansion fixture did not contain 256 notes");
        traffic->events.count = 256;
        auto& command = traffic->queues[3];
        command.count = 5;
        for (std::size_t point = 0; point < 5; ++point) { command.values[point] = {offsets[point], 0.0}; }
        traffic->select({0, 2, 3, 1});
        host->data.inputParameterChanges = &traffic->changes;
        const auto strikes = oracle->strikes, releases = oracle->releases;
        oracle->compare(*host, [&](std::size_t frame) {
            if (frame == 0) {
                oracle->state.monitoring_gain = 0.25F;
                oracle->state.expression = 0.5F;
                oracle->sustain(false);
            }
            std::size_t first = 0;
            for (std::size_t group = 0; group < sizes.size(); ++group) {
                if (frame == static_cast<std::size_t>(offsets[group])) {
                    oracle->release_all();
                    for (std::size_t key = 0; key < sizes[group]; ++key) {
                        oracle->strike(key, intensity(first + key));
                    }
                }
                first += sizes[group];
            }
            if (frame == 127) { oracle->release_all(); }
        });
        expect_reads(4, 4, 4, 8, 1, 256);
        require(oracle->strikes - strikes == 256 && oracle->releases - releases == 329 &&
                host->processor.note_tracker().active_count() == 0,
                "expression traffic changed the 585 physical-action expansion");
        host->data.inputParameterChanges = nullptr;
        host->data.inputEvents = nullptr;
        oracle->compare(*host, [](std::size_t) {});
    }
    {
        const auto host = std::make_unique<ProcessorHost<Sample, frames>>();
        const auto traffic = std::make_unique<Traffic>();
        const auto oracle = std::make_unique<Oracle>();
        oracle->prehold(*host, traffic->events);
        traffic->events.count = 183;
        for (std::size_t index = 0; index < 183; ++index) {
            traffic->events.values[index] = note_on(63, static_cast<sdk::int32>(1000 + index),
                pitch(index % 73), intensity(index));
        }
        traffic->queues[0].values[0] = {63, 0.25};
        traffic->queues[2].values[0] = {63, 0.5};
        traffic->queues[3].values[0] = {63, 0.0};
        traffic->queues[1].values[0] = {63, 1.0};
        traffic->select({0, 2, 3, 1});
        host->data.inputParameterChanges = &traffic->changes;
        reject(*host); // 73 releases + 183 strikes + sustain = 257.
        expect_reads(4, 4, 4, 4, 1, 183);
        traffic->select({0, 2, 3});
        const auto strikes = oracle->strikes, releases = oracle->releases;
        oracle->compare(*host, [&](std::size_t frame) {
            // Independent barycentric expression/gain curves with exactly
            // representable binary32 dyadic anchors and a 64-sample span.
            if (frame < 64) {
                const double left = static_cast<double>(63 - frame);
                const double right = static_cast<double>(frame + 1);
                oracle->state.monitoring_gain = static_cast<float>((0.5 * left + 0.25 * right) / 64.0);
                oracle->state.expression = static_cast<float>((1.0 * left + 0.5 * right) / 64.0);
            }
            if (frame == 63) {
                oracle->release_all();
                for (std::size_t note = 0; note < 183; ++note) {
                    oracle->strike(note % 73, intensity(note));
                }
            }
        });
        expect_reads(3, 3, 3, 3, 1, 183);
        require(oracle->strikes - strikes == 183 && oracle->releases - releases == 73,
                "gain/expression consumed a slot in the 256-action physical dispatch");
        traffic->events.count = 182;
        traffic->select({0, 2, 3, 1});
        oracle->compare(*host, [&](std::size_t frame) {
            if (frame == 63) {
                oracle->sustain(true);
                oracle->release_all();
                for (std::size_t note = 0; note < 182; ++note) {
                    oracle->strike(note % 73, intensity(note));
                }
            }
        });
        expect_reads(4, 4, 4, 4, 1, 182);
        require(host->processor.note_tracker().active_count() == 182,
                "73 releases + 182 strikes + sustain did not fit exactly 256 slots");
        host->data.inputParameterChanges = nullptr;
        host->data.inputEvents = nullptr;
        oracle->compare(*host, [](std::size_t) {});
    }
}

template<class Sample>
void allocations_for() {
    for (const double rate : {48000.0, 192000.0}) {
        for (const sdk::int32 width : {1, 2}) {
            const auto host = std::make_unique<ProcessorHost<Sample, frames>>(rate);
            const auto traffic = std::make_unique<Traffic>();
            std::array<Sample*, 1> mono{host->left.data()};
            if (width == 1) {
                require(host->processor.setProcessing(false) == sdk::kResultOk &&
                        host->processor.setActive(false) == sdk::kResultOk,
                        "suspend allocation fixture for mono");
                auto arrangement = vst::SpeakerArr::kMono;
                require(host->processor.setBusArrangements(nullptr, 0, &arrangement, 1) ==
                            sdk::kResultOk &&
                        host->processor.setActive(true) == sdk::kResultOk &&
                        host->processor.setProcessing(true) == sdk::kResultOk,
                        "prepare mono allocation fixture");
                host->output.numChannels = 1;
                if constexpr (std::is_same_v<Sample, float>) { host->output.channelBuffers32 = mono.data(); }
                else { host->output.channelBuffers64 = mono.data(); }
            }
            traffic->expression_last();
            for (std::size_t control = 0; control < 3; ++control) {
                auto& queue = traffic->queues[control];
                queue.count = 128;
                for (std::size_t point = 0; point < 128; ++point) {
                    const double value = control == 0 ? static_cast<double>(32 + point % 65) / 128.0 :
                        control == 2 ? static_cast<double>(96 - point % 65) / 128.0 : 0.0;
                    queue.values[point] = {static_cast<sdk::int32>(point), value};
                }
            }
            traffic->events.count = 256;
            for (sdk::int16 channel = 0; channel < 16; ++channel) {
                auto& command = traffic->queues[3 + static_cast<std::size_t>(channel)];
                command.count = 16;
                for (sdk::int32 step = 0; step < 16; ++step) {
                    command.values[static_cast<std::size_t>(step)] = {8 * step, 0.0};
                    traffic->events.values[static_cast<std::size_t>(16 * step + channel)] =
                        step % 2 == 0 || step == 15 ?
                        note_on(step == 15 ? 127 : 8 * step, channel,
                                static_cast<sdk::int16>(28 + channel), 0.25F, channel) :
                        pressure(8 * step, channel);
                }
            }
            host->data.inputParameterChanges = &traffic->changes;
            host->data.inputEvents = &traffic->events;
            for (int block = 0; block < 2; ++block) {
                require(process(host->processor, host->data) == sdk::kResultOk,
                        "prepared dense expression callback failed");
                expect_reads(19, 19, 19, 640, 1, 256);
                require(std::any_of(host->left.begin(), host->left.end(),
                                    [](Sample value) { return value != Sample{0}; }),
                        "dense allocation fixture was vacuously silent");
            }
            recall(host->processor, {0.625F, true, 0.125F});
            auto& expression = traffic->queues[2];
            const double final_expression = expression.values[127].value;
            expression.values[127].value = std::numeric_limits<double>::quiet_NaN();
            reject(*host);
            expect_reads(19, 19, 19, 640);
            expression.values[127].value = final_expression;
            require(process(host->processor, host->data) == sdk::kResultOk &&
                    host->processor.engine_processor().frame_cursor() == 128,
                    "prepared dense recall callback failed or recall was lost");
            expect_reads(19, 19, 19, 640, 1, 256);

            require(host->processor.activateBus(vst::kAudio, vst::kOutput, 0, false) == sdk::kResultOk,
                    "deactivate output for allocation fixture");
            host->data.numOutputs = 0;
            host->data.outputs = nullptr;
            host->fill(Sample{7});
            host->output.silenceFlags = 0x5a;
            require(process(host->processor, host->data) == sdk::kResultOk &&
                    host->processor.engine_processor().frame_cursor() == 256,
                    "inactive omitted-output callback did not continue processing");
            expect_reads(19, 19, 19, 640, 1, 256);
            expression.count = 8193;
            reject(*host);
            expect_reads(19, 19, 19, 512);
            expression.count = 128;
            const auto before = std::make_unique<ScalarEngine>(host->processor.engine_processor().engine());
            for (auto& queue : traffic->queues) {
                for (auto& point : queue.values) { point.offset = 0; }
            }
            traffic->events.count = 0;
            host->data.numSamples = 0;
            expression.values[127].value = 0.875;
            require(process(host->processor, host->data) == sdk::kResultOk,
                    "prepared nineteen-queue inactive flush failed");
            expect_reads(19, 19, 19, 640, 1, 0);
            require(host->processor.current_state().expression == 0.875F &&
                    host->processor.published_state() == host->processor.current_state() &&
                    host->processor.note_tracker().active_count() == 0 &&
                    host->processor.engine_processor().frame_cursor() == 256 &&
                    host->output.silenceFlags == 0x5a &&
                    std::all_of(host->left.begin(), host->left.end(),
                                [](Sample value) { return value == Sample{7}; }) &&
                    std::all_of(host->right.begin(), host->right.end(),
                                [](Sample value) { return value == Sample{7}; }),
                    "inactive flush changed time/buffers or missed releases/expression");
            same_mechanics(host->processor.engine_processor().engine(), *before);
        }
    }
}

void calibrate_probe() {
    // Direct operator calls prevent optimized new-expression elision. Cover
    // all eight allocation forms and all twelve corresponding delete forms,
    // including the sized overloads. Calibration is not a process observation.
    std::array<void*, 12> pointers{};
    constexpr auto alignment = std::align_val_t{64};
    std::size_t allocations = 0, deallocations = 0;
    {
        expression_allocation_probe::Scope scope;
        pointers[0] = ::operator new(8);
        pointers[1] = ::operator new[](8);
        pointers[2] = ::operator new(8, std::nothrow);
        pointers[3] = ::operator new[](8, std::nothrow);
        pointers[4] = ::operator new(8, alignment);
        pointers[5] = ::operator new[](8, alignment);
        pointers[6] = ::operator new(8, alignment, std::nothrow);
        pointers[7] = ::operator new[](8, alignment, std::nothrow);
        pointers[8] = ::operator new(8);
        pointers[9] = ::operator new[](8);
        pointers[10] = ::operator new(8, alignment);
        pointers[11] = ::operator new[](8, alignment);
        ::operator delete(pointers[0]);
        ::operator delete[](pointers[1]);
        ::operator delete(pointers[2], std::nothrow);
        ::operator delete[](pointers[3], std::nothrow);
        ::operator delete(pointers[4], alignment);
        ::operator delete[](pointers[5], alignment);
        ::operator delete(pointers[6], alignment, std::nothrow);
        ::operator delete[](pointers[7], alignment, std::nothrow);
        ::operator delete(pointers[8], std::size_t{8});
        ::operator delete[](pointers[9], std::size_t{8});
        ::operator delete(pointers[10], std::size_t{8}, alignment);
        ::operator delete[](pointers[11], std::size_t{8}, alignment);
        allocations = expression_allocation_probe::allocations;
        deallocations = expression_allocation_probe::deallocations;
    }
    require(std::all_of(pointers.begin(), pointers.end(), [](const void* value) { return value != nullptr; }) &&
            allocations == 12 && deallocations == 12,
            "C++ allocation/deallocation probe calibration failed");
}

void queue_limits() { queue_limits_for<float>(); queue_limits_for<double>(); }
void persistent_limits() { persistent_limits_for<float>(); persistent_limits_for<double>(); }
void maximum_points() { maximum_points_for<float>(); maximum_points_for<double>(); }
void command_events() { command_events_for<float>(); command_events_for<double>(); }
void dispatch() { dispatch_for<float>(); dispatch_for<double>(); }
void allocations() { allocations_for<float>(); allocations_for<double>(); }
using Selector = std::pair<std::string_view, void (*)()>;
constexpr std::array<Selector, 6> selectors{{
    {"queue_limits", queue_limits}, {"persistent_limits", persistent_limits},
    {"maximum_points", maximum_points}, {"command_events", command_events},
    {"dispatch", dispatch}, {"allocations", allocations}
}};
} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: rhodes_vst3_expression_bounds_tests <selector|all>\n";
        return 2;
    }
    const std::string_view requested{argv[1]};
    bool matched = false;
    try {
        calibrate_probe();
        for (const auto& [name, run] : selectors) {
            if (requested != "all" && requested != name) { continue; }
            matched = true;
            summary = {};
            run();
            std::cout << "PASS " << name << " (model " << kStateModel << ", float+double, "
                      << summary.callbacks << " guarded callbacks: " << summary.accepted << " accepted, "
                      << summary.rejected << " rejected; " << summary.point_reads << " point reads, "
                      << summary.event_reads << " event reads; peaks " << summary.peak_points << '/'
                      << summary.peak_events << "; 0 C++ allocations/deallocations)\n";
        }
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << requested << " (model " << kStateModel << "): " << error.what() << '\n';
        return 1;
    }
    if (!matched) { std::cerr << "Unknown expression bounds selector: " << requested << '\n'; return 2; }
    return 0;
}
