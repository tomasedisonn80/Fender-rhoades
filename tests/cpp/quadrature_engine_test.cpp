#include "rhodes/event_processor.hpp"
#include "rhodes/quadrature_engine.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <new>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Calibrated ordinary/array C++ allocations only. The separate VST3 host
// fixture covers aligned/nothrow forms; this is not a malloc or locking probe.
namespace qengine_allocation_probe {
thread_local bool enabled = false;
thread_local std::size_t attempts = 0;
void* allocate(std::size_t bytes) {
    if (enabled) { ++attempts; }
    if (void* memory = std::malloc(bytes == 0 ? 1 : bytes)) { return memory; }
    throw std::bad_alloc{};
}
#if defined(_MSC_VER)
__declspec(noinline)
#elif defined(__GNUC__)
__attribute__((noinline))
#endif
void deallocate(void* memory) noexcept { std::free(memory); }
struct Scope {
    Scope() noexcept { attempts = 0; enabled = true; }
    ~Scope() { enabled = false; }
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
};
} // namespace qengine_allocation_probe

void* operator new(std::size_t bytes) { return qengine_allocation_probe::allocate(bytes); }
void* operator new[](std::size_t bytes) { return qengine_allocation_probe::allocate(bytes); }
void operator delete(void* memory) noexcept { qengine_allocation_probe::deallocate(memory); }
void operator delete[](void* memory) noexcept { qengine_allocation_probe::deallocate(memory); }
void operator delete(void* memory, std::size_t) noexcept {
    qengine_allocation_probe::deallocate(memory);
}
void operator delete[](void* memory, std::size_t) noexcept {
    qengine_allocation_probe::deallocate(memory);
}

namespace {
using rhodes::EngineEvent;
using rhodes::EventType;
using rhodes::ProcessStatus;
using rhodes::QuadratureEngine;
using Processor = rhodes::BasicEventProcessor<QuadratureEngine>;
constexpr auto no_failure = rhodes::no_failure_index;
constexpr std::size_t frames = 321;
const std::array<double, 9> rates{{32000.0, 44100.0, 48000.0,
    std::nextafter(48000.0, std::numeric_limits<double>::infinity()), 64000.0,
    88200.0, 96000.0,
    std::nextafter(96000.0, std::numeric_limits<double>::infinity()), 192000.0}};

void require(bool condition, std::string_view message) {
    if (!condition) { throw std::runtime_error(std::string{message}); }
}

void expect_zero(std::span<const double> output) {
    require(std::all_of(output.begin(), output.end(),
        [](double value) { return value == 0.0; }), "failed suffix is not zero");
}

template<class A, class B>
void same_physics(const A& actual, const B& expected) {
    require(actual.mechanical_energy_joules() == expected.mechanical_energy_joules(),
            "boundary mechanical energy changed");
    for (std::size_t key = 0; key < rhodes::key_count; ++key) {
        for (std::size_t mode = 0; mode < rhodes::modes_per_key; ++mode) {
            const auto a = actual.modal_state(key, mode);
            const auto b = expected.modal_state(key, mode);
            require(a && b && a->displacement_m == b->displacement_m &&
                    a->velocity_m_per_s == b->velocity_m_per_s,
                    "mechanical state differs at the same host boundary");
        }
    }
}

template<class Engine>
void scalar_events(Engine& engine, std::size_t frame) {
    switch (frame) {
    case 0: require(engine.note_on(0, 0.7), "initial strike"); break;
    case 1: require(engine.note_on(36, 1.0), "middle strike"); break;
    case 17: require(engine.note_on(72, 0.9), "top strike"); break;
    case 63:
        require(engine.note_off(36), "release before retrigger");
        require(engine.note_on(36, 0.3), "moving retrigger");
        break;
    case 64: engine.set_sustain(true); require(engine.note_off(0), "pedal release"); break;
    case 127: engine.all_notes_off(); break;
    case 128: engine.set_sustain(false); break;
    case 129: require(engine.note_on(72, 0.5), "restrike after pedal"); break;
    case 193: engine.reset(); require(engine.note_on(36, 0.8), "strike after reset"); break;
    case 257: require(engine.note_off(36), "late release"); break;
    case 320: require(engine.note_on(0, 1.0), "final-frame strike"); break;
    default: break;
    }
}

constexpr std::array<EngineEvent, 15> script{{
    {0, 0, EventType::note_on, 0, 0.7},
    {1, 0, EventType::note_on, 36, 1.0},
    {17, 0, EventType::note_on, 72, 0.9},
    {63, 0, EventType::note_off, 36, 0.0},
    {63, 1, EventType::note_on, 36, 0.3},
    {64, 0, EventType::sustain, 0, 1.0},
    {64, 1, EventType::note_off, 0, 0.0},
    {127, 0, EventType::all_notes_off, 0, 0.0},
    {128, 0, EventType::sustain, 0, 0.0},
    {129, 0, EventType::note_on, 72, 0.5},
    {193, 0, EventType::reset, 0, 0.0},
    {193, 1, EventType::note_on, 36, 0.8},
    {257, 0, EventType::note_off, 36, 0.0},
    {320, 0, EventType::note_on, 0, 1.0},
    {320, 1, EventType::note_off, 72, 0.0}
}};

void qengine_events() {
    long double maximum_error = 0.0L;
    for (const double rate : rates) {
        auto audio = std::make_unique<QuadratureEngine>();
        auto raw = std::make_unique<rhodes::ResearchEngine>();
        auto plan = std::make_unique<rhodes::FractionalObservationPlan>();
        require(audio->prepare(rate) && raw->prepare(rate) &&
                raw->prepare_observation_plan(rhodes::quadrature_filter::nodes, *plan),
                "event oracle prepare");
        require(audio->sample_rate_hz() == rate && audio->node_count() == 6 &&
                audio->latency_frames() == 48, "quadrature metadata");
        // Chronological storage and ordinary long-double convolution deliberately
        // avoid the production ring/filter and wrapper. Coefficients and the
        // fractional-observation API have separate independent-oracle fixtures.
        std::vector<rhodes::QuadratureFir::Cell> cells;
        cells.reserve(frames);
        for (std::size_t frame = 0; frame < frames; ++frame) {
            scalar_events(*audio, frame);
            scalar_events(*raw, frame);
            if (frame == 193) { cells.clear(); }
            long double expected = 0.0L;
            for (std::size_t age = 0;
                 age < cells.size() && age < rhodes::quadrature_filter::history_frames; ++age) {
                for (std::size_t node = 0; node < 6; ++node) {
                    expected += static_cast<long double>(cells[cells.size() - 1 - age][node]) *
                                rhodes::quadrature_filter::coefficients[node][age];
                }
            }
            rhodes::QuadratureFir::Cell cell{};
            for (std::size_t node = 0; node < cell.size(); ++node) {
                const auto value = raw->observe(*plan, node);
                require(value.has_value(), "independent schedule observation failed");
                cell[node] = *value;
            }
            cells.push_back(cell);
            std::array<double, 1> actual{}, discarded{};
            require(audio->process(actual) && raw->process(discarded), "event frame failed");
            maximum_error = std::max(maximum_error,
                std::abs(static_cast<long double>(actual[0]) - expected));
            require(maximum_error <= 2e-14L, "completed-cell event convolution differs");
            same_physics(*audio, *raw);
            if (frame == 0 || frame == 193) {
                require(actual[0] == 0.0, "strike published its incomplete current cell");
            }
            if (frame == 1 || frame == 194) {
                require(actual[0] != 0.0, "first completed struck cell was lost");
            }
        }
        auto forward = std::make_unique<Processor>();
        auto reverse = std::make_unique<Processor>();
        require(forward->prepare(rate) && reverse->prepare(rate), "reset-order prepare");
        std::array<double, 100> warm{};
        constexpr std::array<EngineEvent, 1> start{{{0, 0, EventType::note_on, 72, 1.0}}};
        require(forward->process(warm, start).succeeded() &&
                reverse->process(warm, start).succeeded(), "reset-order warmup");
        constexpr std::array<EngineEvent, 2> reset_strike{{
            {0, 0, EventType::reset, 0, 0.0}, {0, 1, EventType::note_on, 36, 0.8}}};
        constexpr std::array<EngineEvent, 2> strike_reset{{
            {0, 0, EventType::note_on, 36, 0.8}, {0, 1, EventType::reset, 0, 0.0}}};
        std::array<double, 128> a{}, b{};
        require(forward->process(a, reset_strike).succeeded() &&
                reverse->process(b, strike_reset).succeeded(), "reset-order processing");
        require(a[0] == 0.0 && b[0] == 0.0 && a != b && a[1] != 0.0,
                "equal-offset reset/strike ordering was lost");
        expect_zero(b);
        require(forward->engine().mechanical_energy_joules() > 0.0 &&
                reverse->engine().mechanical_energy_joules() == 0.0,
                "reset/strike mechanics do not distinguish producer order");
    }
    std::cout << "event_rates=9 oracle_frames=" << rates.size() * frames
              << " maximum_convolution_error=" << static_cast<double>(maximum_error) << '\n';
}

void qengine_partitions() {
    constexpr std::array<std::array<std::size_t, 7>, 4> partitions{{
        {{frames, frames, frames, frames, frames, frames, frames}},
        {{1, 1, 1, 1, 1, 1, 1}},
        {{0, 1, 7, 64, 0, 33, 96}},
        {{63, 0, 1, 64, 1, 0, 127}}
    }};
    for (const double rate : rates) {
        auto baseline = std::make_unique<Processor>();
        require(baseline->prepare(rate), "partition baseline prepare");
        std::array<double, frames> expected{};
        require(baseline->process(expected, script).succeeded(), "partition baseline render");
        for (const auto& sizes : partitions) {
            auto actual = std::make_unique<Processor>();
            require(actual->prepare(rate), "partition prepare");
            std::array<double, frames> output{};
            std::size_t first = 0;
            std::size_t index = 0;
            while (first < frames) {
                const auto count = std::min(sizes[index++ % sizes.size()], frames - first);
                std::array<EngineEvent, script.size()> local{};
                std::size_t events = 0;
                for (auto event : script) {
                    if (event.offset >= first && event.offset < first + count) {
                        event.offset -= static_cast<std::uint32_t>(first);
                        local[events++] = event;
                    }
                }
                const auto result = actual->process(std::span<double>{output}.subspan(first, count),
                    std::span<const EngineEvent>{local}.first(events));
                require(result.succeeded() && result.frames_processed == count &&
                        !result.requires_reset, "partition process result");
                first += count;
                require(actual->frame_cursor() == first, "empty or mixed block changed cursor");
            }
            require(output == expected, "block partition changed Q6 samples");
            same_physics(actual->engine(), baseline->engine());
        }
    }
    std::cout << "partition_cases=36 including_empty_and_single_frame_blocks\n";
}

enum class Fault { none, missing_node, nan_node, inf_node, advance_false,
                   advance_nan, advance_inf, huge_constant };

// A deterministic substitute exposes observations/advances through counters.
// No production fault switch or accessor is added. The private plan is unused:
// this fake tests wrapper transactions, not ResearchEngine's plan calculation.
struct FaultEngine {
    inline static Fault fault = Fault::none;
    inline static std::size_t fail_frame = no_failure;
    inline static std::size_t fail_node = 0;
    inline static std::size_t observation_calls = 0;
    inline static std::size_t advance_calls = 0;
    inline static std::size_t reset_calls = 0;
    inline static std::size_t plan_calls = 0;
    inline static bool reject_core_prepare = false;
    inline static bool reject_plan_prepare = false;
    std::size_t frame = 0;
    std::size_t events = 0;

    static void clear_controls() noexcept {
        fault = Fault::none;
        fail_frame = no_failure;
        fail_node = observation_calls = advance_calls = reset_calls = plan_calls = 0;
        reject_core_prepare = reject_plan_prepare = false;
    }
    bool prepare(double) noexcept {
        if (reject_core_prepare) { return false; }
        frame = events = 0;
        return true;
    }
    bool prepare_observation_plan(const std::array<double, 6>&,
                                  rhodes::FractionalObservationPlan&) const noexcept {
        ++plan_calls;
        return !reject_plan_prepare;
    }
    std::optional<double> observe(const rhodes::FractionalObservationPlan&,
                                  std::size_t node) const noexcept {
        ++observation_calls;
        if (frame == fail_frame && node == fail_node) {
            if (fault == Fault::missing_node) { return std::nullopt; }
            if (fault == Fault::nan_node) { return std::numeric_limits<double>::quiet_NaN(); }
            if (fault == Fault::inf_node) { return std::numeric_limits<double>::infinity(); }
        }
        if (fault == Fault::huge_constant) { return std::numeric_limits<double>::max(); }
        return 0.25 + static_cast<double>(frame) * 0.001 + static_cast<double>(node) * 0.01;
    }
    std::optional<std::array<double, 6>> observe_cell(
        const rhodes::FractionalObservationPlan& plan) const noexcept {
        std::array<double, 6> cell{};
        for (std::size_t node = 0; node < cell.size(); ++node) {
            const auto value = observe(plan, node);
            if (!value || !std::isfinite(*value)) { return std::nullopt; }
            cell[node] = *value;
        }
        return cell;
    }
    bool process(std::span<double> output) noexcept {
        for (auto& value : output) {
            const auto current = frame++;
            ++advance_calls;
            value = 71.0;
            if (current == fail_frame) {
                if (fault == Fault::advance_false) { return false; }
                if (fault == Fault::advance_nan) { value = std::numeric_limits<double>::quiet_NaN(); }
                if (fault == Fault::advance_inf) { value = std::numeric_limits<double>::infinity(); }
            }
        }
        return true;
    }
    void reset() noexcept { frame = events = 0; ++reset_calls; }
    bool note_on(std::size_t, double) noexcept { ++events; return true; }
    bool note_off(std::size_t) noexcept { ++events; return true; }
    void set_sustain(bool) noexcept { ++events; }
    void all_notes_off() noexcept { ++events; }
    std::optional<rhodes::ModalState> modal_state(std::size_t, std::size_t) const noexcept {
        return rhodes::ModalState{static_cast<double>(frame), static_cast<double>(events)};
    }
    double mechanical_energy_joules() const noexcept { return static_cast<double>(frame); }
};
using FaultAudio = rhodes::BasicQuadratureEngine<FaultEngine>;
using FaultProcessor = rhodes::BasicEventProcessor<FaultAudio>;

void qengine_prepare() {
    const double infinity = std::numeric_limits<double>::infinity();
    const std::array<double, 9> invalid{{0.0, -0.0, -48000.0, infinity, -infinity,
        std::numeric_limits<double>::quiet_NaN(),
        std::nextafter(32000.0, 0.0), std::nextafter(192000.0, infinity), 1000000.0}};
    for (const double rate : rates) {
        auto baseline = std::make_unique<QuadratureEngine>();
        require(baseline->prepare(rate) && baseline->note_on(0, 0.7) &&
                baseline->note_on(72, 1.0), "prepare atomicity fixture");
        baseline->set_sustain(true);
        require(baseline->note_off(0), "prepare atomicity released key");
        std::array<double, 117> warm{};
        require(baseline->process(warm), "prepare atomicity warmup");
        auto expected_engine = std::make_unique<QuadratureEngine>(*baseline);
        require(baseline->process({}), "empty wrapper call failed");
        same_physics(*baseline, *expected_engine);
        std::array<double, 129> expected{};
        require(expected_engine->process(expected), "prepare baseline continuation");
        for (const double bad_rate : invalid) {
            auto actual = std::make_unique<QuadratureEngine>(*baseline);
            require(!actual->prepare(bad_rate), "invalid host rate was accepted");
            require(actual->sample_rate_hz() == rate, "invalid rate changed metadata");
            same_physics(*actual, *baseline);
            std::array<double, 129> output{};
            require(actual->process(output) && output == expected,
                    "rejected prepare changed plan, history or controls");
            same_physics(*actual, *expected_engine);
        }
        auto replaced = std::make_unique<QuadratureEngine>(*baseline);
        require(replaced->prepare(44100.0) && replaced->sample_rate_hz() == 44100.0 &&
                replaced->mechanical_energy_joules() == 0.0 && replaced->process(warm),
                "successful prepare failed to replace rate and reset mechanics");
        expect_zero(warm);
        auto reset = std::make_unique<QuadratureEngine>(*baseline);
        reset->reset();
        require(reset->sample_rate_hz() == rate && reset->mechanical_energy_joules() == 0.0,
                "reset lost host rate or retained mechanics");
        require(reset->process(warm), "reset render");
        expect_zero(warm);
        require(reset->prepare(44100.0) && reset->sample_rate_hz() == 44100.0,
                "valid replacement rate not published");
    }
    for (const bool plan_failure : {false, true}) {
        FaultEngine::clear_controls();
        auto baseline = std::make_unique<FaultAudio>();
        std::array<double, 117> warm{};
        require(baseline->process(warm), "fake prepare warmup");
        auto actual = std::make_unique<FaultAudio>(*baseline);
        FaultEngine::reject_core_prepare = !plan_failure;
        FaultEngine::reject_plan_prepare = plan_failure;
        require(!actual->prepare(96000.0), "injected preparation failure was accepted");
        require(actual->sample_rate_hz() == 48000.0, "failed plan published rate");
        FaultEngine::reject_core_prepare = FaultEngine::reject_plan_prepare = false;
        std::array<double, 129> a{}, b{};
        require(actual->process(a) && baseline->process(b) && a == b,
                "failed core/plan preparation changed history");
        const auto prepared = FaultEngine::plan_calls;
        actual->reset();
        require(FaultEngine::plan_calls == prepared, "reset recomputed observation plan");
    }
    FaultEngine::clear_controls();
    std::cout << "invalid_rate_transactions=81 injected_prepare_failures=2\n";
}

void check_suffix(std::span<const double> actual, std::span<const double> expected,
                  std::size_t failure) {
    require(std::equal(actual.begin(), actual.begin() + static_cast<std::ptrdiff_t>(failure),
                       expected.begin()), "fault changed successful output prefix");
    expect_zero(actual.subspan(failure));
}

void check_fence(FaultProcessor& processor, std::size_t cursor) {
    const auto observations = FaultEngine::observation_calls;
    const auto advances = FaultEngine::advance_calls;
    const auto resets = FaultEngine::reset_calls;
    std::array<double, 8> output{};
    output.fill(77.0);
    constexpr std::array<EngineEvent, 1> reset{{{0, 0, EventType::reset, 0, 0.0}}};
    const auto result = processor.process(output, reset);
    require(result.status == ProcessStatus::reset_required && result.frames_processed == 0 &&
            result.failing_sample == no_failure && result.failing_event == no_failure &&
            result.requires_reset && result.subsystem == rhodes::ProcessSubsystem::engine,
            "fault fence result changed");
    expect_zero(output);
    require(processor.process({}).status == ProcessStatus::reset_required,
            "empty block cleared fault fence");
    require(!processor.prepare(std::numeric_limits<double>::quiet_NaN()) &&
            processor.requires_reset() && processor.frame_cursor() == cursor,
            "invalid prepare cleared fault or changed cursor");
    require(FaultEngine::observation_calls == observations && FaultEngine::advance_calls == advances &&
            FaultEngine::reset_calls == resets, "fault fence executed queued reset or audio");
    FaultEngine::fault = Fault::none;
    processor.reset();
    require(!processor.requires_reset() && processor.frame_cursor() == 0,
            "explicit reset failed to recover processor");
    require(processor.process(output).succeeded() && output.front() == 0.0 && output[1] != 0.0,
            "reset did not clear failed filter history");
}

void qengine_faults() {
    constexpr std::size_t warm_frames = 120;
    constexpr std::size_t block_frames = 64;
    FaultEngine::clear_controls();
    auto reference = std::make_unique<FaultAudio>();
    std::array<double, warm_frames> warm{};
    std::array<double, block_frames> expected{};
    require(reference->process(warm) && reference->process(expected), "fault baseline render");
    require(std::all_of(expected.begin(), expected.end(),
        [](double value) { return std::isfinite(value) && value != 0.0; }),
        "provisional fault baseline is silent or nonfinite");
    std::size_t cases = 0;
    for (const auto fault : {Fault::missing_node, Fault::nan_node, Fault::inf_node,
                            Fault::advance_false, Fault::advance_nan, Fault::advance_inf}) {
        const bool node_fault = fault == Fault::missing_node || fault == Fault::nan_node ||
                                fault == Fault::inf_node;
        for (std::size_t node = 0; node < (node_fault ? 6U : 1U); ++node) {
            for (const std::size_t failed : {0U, 1U, 17U, 63U}) {
                FaultEngine::clear_controls();
                auto audio = std::make_unique<FaultAudio>();
                require(audio->process(warm), "direct fault warmup");
                FaultEngine::fault = fault;
                FaultEngine::fail_frame = warm_frames + failed;
                FaultEngine::fail_node = node;
                const auto before_nodes = FaultEngine::observation_calls;
                const auto before_advances = FaultEngine::advance_calls;
                std::array<double, block_frames> actual{};
                actual.fill(77.0);
                require(!audio->process(actual), "wrapper swallowed observation/advance fault");
                check_suffix(actual, expected, failed);
                require(FaultEngine::observation_calls - before_nodes ==
                        failed * 6 + (node_fault ? node + 1 : 6),
                        "wrapper observed later nodes after failure");
                require(FaultEngine::advance_calls - before_advances == failed +
                        (node_fault ? 0U : 1U), "wrapper advanced after failure");

                FaultEngine::clear_controls();
                auto processor = std::make_unique<FaultProcessor>();
                require(processor->process(warm).succeeded(), "processor fault warmup");
                FaultEngine::fault = fault;
                FaultEngine::fail_frame = warm_frames + failed;
                FaultEngine::fail_node = node;
                actual.fill(77.0);
                const auto result = processor->process(actual);
                require(result.status == ProcessStatus::arithmetic_fault &&
                        result.frames_processed == failed && result.failing_sample == failed &&
                        result.failing_event == no_failure && result.requires_reset &&
                        result.subsystem == rhodes::ProcessSubsystem::engine &&
                        processor->frame_cursor() == warm_frames + failed,
                        "processor lost Q6 fault coordinates or latch");
                check_suffix(actual, expected, failed);
                check_fence(*processor, warm_frames + failed);
                ++cases;
            }
        }
    }
    // Finite observations may still overflow the FIR sum. Discover the first
    // rejected cell with scalar calls, then check a full block's prefix/suffix.
    // Node and boundary counters prove failure happens before the next cell.
    FaultEngine::clear_controls();
    auto scalar = std::make_unique<FaultAudio>();
    FaultEngine::fault = Fault::huge_constant;
    std::array<double, 192> huge_expected{};
    std::size_t failure = no_failure;
    for (std::size_t frame = 0; frame < huge_expected.size(); ++frame) {
        if (!scalar->process(std::span<double>{huge_expected}.subspan(frame, 1))) {
            failure = frame;
            break;
        }
    }
    require(failure > 1 && failure < huge_expected.size(), "FIR overflow fixture did not fail");
    FaultEngine::clear_controls();
    auto huge = std::make_unique<FaultProcessor>();
    FaultEngine::fault = Fault::huge_constant;
    std::array<double, 192> huge_actual{};
    huge_actual.fill(77.0);
    const auto overflow = huge->process(huge_actual);
    require(overflow.status == ProcessStatus::arithmetic_fault &&
            overflow.failing_sample == failure && overflow.frames_processed == failure &&
            overflow.requires_reset && FaultEngine::advance_calls == failure &&
            FaultEngine::observation_calls == 6 * failure,
            "filter overflow published output or advanced an extra cell");
    check_suffix(huge_actual, huge_expected, failure);
    check_fence(*huge, failure);
    FaultEngine::clear_controls();
    std::cout << "node_and_advance_fault_cases=" << cases
              << " each_direct_and_fenced; FIR_overflow_frame=" << failure << '\n';
}

void qengine_allocations() {
    std::array<void*, 2> calibration{};
    std::size_t observed = 0;
    {
        qengine_allocation_probe::Scope guard;
        calibration[0] = ::operator new(8);
        calibration[1] = ::operator new[](8);
        observed = qengine_allocation_probe::attempts;
    }
    ::operator delete(calibration[0]);
    ::operator delete[](calibration[1]);
    require(observed == 2, "ordinary/array allocation probe did not calibrate");
    std::array<EngineEvent, rhodes::key_count + 3> events{};
    for (std::size_t key = 0; key < rhodes::key_count; ++key) {
        events[key] = {0, static_cast<std::uint32_t>(key), EventType::note_on, key, 0.7};
    }
    events[rhodes::key_count] = {1, 0, EventType::sustain, 0, 1.0};
    events[rhodes::key_count + 1] = {32, 0, EventType::all_notes_off, 0, 0.0};
    events[rhodes::key_count + 2] = {64, 0, EventType::sustain, 0, 0.0};
    std::size_t callbacks = 0;
    for (const double rate : rates) {
        auto processor = std::make_unique<Processor>();
        require(processor->prepare(rate), "allocation prepare");
        std::array<double, 128> output{};
        bool ok = true;
        {
            qengine_allocation_probe::Scope guard;
            for (std::size_t repeat = 0; repeat < 4; ++repeat) {
                ok = processor->process(output, events).succeeded() && ok;
                ok = processor->process({}).succeeded() && ok;
                callbacks += 2;
            }
            processor->reset();
            ok = processor->process(output).succeeded() && ok;
            ++callbacks;
            observed = qengine_allocation_probe::attempts;
        }
        require(ok && observed == 0, "allocation or failure in guarded Q6 audio calls");
        expect_zero(output);
    }
    std::cout << "allocation_guarded_callbacks=" << callbacks
              << " calibrated_allocation_forms=2 observed_allocations=0\n";
}

using Test = void (*)();
constexpr std::array<std::pair<std::string_view, Test>, 5> tests{{
    {"qengine_events", qengine_events},
    {"qengine_partitions", qengine_partitions},
    {"qengine_prepare", qengine_prepare},
    {"qengine_faults", qengine_faults},
    {"qengine_allocations", qengine_allocations}
}};
} // namespace

int main(int argc, char** argv) {
    const std::string_view selected = argc > 1 ? std::string_view{argv[1]} : "all";
    try {
        require(argc <= 2, "usage: quadrature_engine_test [fixture|all]");
        bool found = false;
        for (const auto& [name, test] : tests) {
            if (selected == "all" || selected == name) {
                test();
                std::cout << "PASS " << name << '\n';
                found = true;
            }
        }
        require(found, "unknown quadrature-engine fixture");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << selected << ": " << error.what() << '\n';
        return 1;
    }
}
