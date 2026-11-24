#include "rhodes/audio_engine.hpp"
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

// Calibrated ordinary/array C++ allocation interception, matching the core
// quadrature fixture. This does not claim to intercept malloc or locking.
namespace release_allocation_probe {
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
} // namespace release_allocation_probe

void* operator new(std::size_t bytes) { return release_allocation_probe::allocate(bytes); }
void* operator new[](std::size_t bytes) { return release_allocation_probe::allocate(bytes); }
void operator delete(void* memory) noexcept { release_allocation_probe::deallocate(memory); }
void operator delete[](void* memory) noexcept { release_allocation_probe::deallocate(memory); }
void operator delete(void* memory, std::size_t) noexcept {
    release_allocation_probe::deallocate(memory);
}
void operator delete[](void* memory, std::size_t) noexcept {
    release_allocation_probe::deallocate(memory);
}

namespace {
using rhodes::EngineEvent;
using rhodes::EventType;
using rhodes::ProcessStatus;
using rhodes::ProcessSubsystem;
constexpr auto no_failure = rhodes::no_failure_index;

void require(bool condition, std::string_view message) {
    if (!condition) { throw std::runtime_error(std::string{message}); }
}

void expect_ok(const rhodes::ProcessResult& result, std::size_t frames) {
    require(result.succeeded() && result.status == ProcessStatus::ok &&
            result.frames_processed == frames && result.failing_sample == no_failure &&
            result.failing_event == no_failure && !result.requires_reset &&
            result.subsystem == ProcessSubsystem::none, "successful process result differs");
}

void expect_fenced(const rhodes::ProcessResult& result) {
    require(!result.succeeded() && result.status == ProcessStatus::reset_required &&
            result.frames_processed == 0 && result.failing_sample == no_failure &&
            result.failing_event == no_failure && result.requires_reset &&
            result.subsystem == ProcessSubsystem::engine, "fault fence result differs");
}

struct Mechanics {
    double rate = 0.0;
    double energy = 0.0;
    std::array<std::array<rhodes::ModalState, rhodes::modes_per_key>, rhodes::key_count> modes{};
};

template<class Engine>
Mechanics snapshot(const Engine& engine) {
    Mechanics state;
    state.rate = engine.sample_rate_hz();
    state.energy = engine.mechanical_energy_joules();
    for (std::size_t key = 0; key < rhodes::key_count; ++key) {
        for (std::size_t mode = 0; mode < rhodes::modes_per_key; ++mode) {
            const auto value = engine.modal_state(key, mode);
            require(value.has_value(), "valid modal state unavailable");
            state.modes[key][mode] = *value;
        }
    }
    return state;
}

template<class Engine>
void expect_mechanics(const Engine& engine, const Mechanics& expected) {
    const auto actual = snapshot(engine);
    require(actual.rate == expected.rate, "release changed sample rate");
    require(actual.energy == expected.energy, "release changed mechanical energy");
    for (std::size_t key = 0; key < rhodes::key_count; ++key) {
        for (std::size_t mode = 0; mode < rhodes::modes_per_key; ++mode) {
            require(actual.modes[key][mode].displacement_m ==
                        expected.modes[key][mode].displacement_m &&
                    actual.modes[key][mode].velocity_m_per_s ==
                        expected.modes[key][mode].velocity_m_per_s,
                    "release advanced or reset modal state");
        }
    }
}

template<class Engine>
std::unique_ptr<rhodes::BasicEventProcessor<Engine>> active_processor(double rate, bool sustain) {
    auto processor = std::make_unique<rhodes::BasicEventProcessor<Engine>>();
    require(processor->prepare(rate), "fixture prepare failed");
    require(processor->set_sustain(sustain), "fixture sustain failed");
    // Include an already released tail, a moving restrike, a separately held
    // high key, and enough rendered history to populate both convolution paths.
    constexpr std::array<EngineEvent, 5> events{{
        {0, 0, EventType::note_on, 0, 0.5},
        {0, 1, EventType::note_on, 36, 0.8},
        {0, 2, EventType::note_on, 72, 0.6},
        {19, 0, EventType::note_on, 36, 0.2},
        {31, 0, EventType::note_off, 0, 0.0}
    }};
    std::array<double, 137> warmup{};
    expect_ok(processor->process(warmup, events), warmup.size());
    require(processor->engine().mechanical_energy_joules() > 0.0 &&
            std::any_of(warmup.begin(), warmup.end(), [](double value) { return value != 0.0; }),
            "release fixture has no active mechanics or audio history");
    return processor;
}

template<class Engine>
void no_advance_and_tail_continuation(double rate) {
    auto processor = active_processor<Engine>(rate, false);
    auto reference = std::make_unique<Engine>(processor->engine());
    auto held = std::make_unique<Engine>(processor->engine());
    const auto before = snapshot(processor->engine());
    const auto cursor = processor->frame_cursor();

    // The reference calls the engine directly, bypassing the new port and the
    // event dispatcher. Exact future output also observes opaque FIR history.
    require(reference->note_off(36), "direct reference release failed");
    require(processor->release_key(36), "valid release failed");
    require(processor->frame_cursor() == cursor && !processor->requires_reset(),
            "release advanced the cursor or latched a fault");
    expect_mechanics(processor->engine(), before);
    expect_ok(processor->process({}), 0);
    require(processor->frame_cursor() == cursor, "empty flush advanced cursor");
    expect_mechanics(processor->engine(), before);

    std::array<double, 257> actual{}, expected{}, still_held{};
    expect_ok(processor->process(actual), actual.size());
    require(reference->process(expected) && held->process(still_held), "reference continuation failed");
    require(actual == expected, "release changed mechanics, controls, or convolution history");
    require(actual != still_held, "release did not change the held key's damping");
    require(std::any_of(actual.begin(), actual.end(), [](double value) { return value != 0.0; }),
            "ordinary release silenced the active tail");
    expect_mechanics(processor->engine(), snapshot(*reference));
    require(processor->frame_cursor() == cursor + actual.size(), "continuation cursor differs");

    const auto tail = snapshot(processor->engine());
    const auto tail_cursor = processor->frame_cursor();
    // Repeated releases, a previously released key, the last valid key, and a
    // never-struck key all retain q/v and continue to use ordinary damping.
    for (const std::size_t key : {36U, 36U, 0U, 72U, 37U}) {
        require(processor->release_key(key) && reference->note_off(key), "repeated/boundary release failed");
        require(processor->frame_cursor() == tail_cursor && !processor->requires_reset(),
                "repeated release changed cursor or fault state");
        expect_mechanics(processor->engine(), tail);
    }
    expect_ok(processor->process(actual), actual.size());
    require(reference->process(expected), "released-tail reference failed");
    require(actual == expected, "repeated release reset a tail or filter history");
    expect_mechanics(processor->engine(), snapshot(*reference));
}

template<class Engine>
void sustain_and_held_state(double rate) {
    auto processor = active_processor<Engine>(rate, true);
    auto reference = std::make_unique<Engine>(processor->engine());
    auto held = std::make_unique<Engine>(processor->engine());
    const auto before = snapshot(processor->engine());
    const auto cursor = processor->frame_cursor();
    require(processor->release_key(36) && reference->note_off(36), "sustained release failed");
    require(processor->frame_cursor() == cursor && !processor->requires_reset(),
            "sustained release advanced cursor or faulted");
    expect_mechanics(processor->engine(), before);

    std::array<double, 257> actual{}, expected{}, still_held{};
    expect_ok(processor->process(actual), actual.size());
    require(reference->process(expected) && held->process(still_held), "sustained reference failed");
    require(actual == expected && actual == still_held,
            "release changed sustain or interrupted the sustained trajectory");

    const auto sustained = snapshot(processor->engine());
    const auto sustained_cursor = processor->frame_cursor();
    require(processor->set_sustain(false), "pedal-up after release failed");
    reference->set_sustain(false);
    held->set_sustain(false);
    expect_mechanics(processor->engine(), sustained);
    require(processor->frame_cursor() == sustained_cursor, "pedal-up advanced cursor");
    expect_ok(processor->process(actual), actual.size());
    require(reference->process(expected) && held->process(still_held), "pedal-up reference failed");
    require(actual == expected, "sustained release lost state before pedal-up");
    require(actual != still_held, "release under sustain failed to clear the key's held state");
    expect_mechanics(processor->engine(), snapshot(*reference));
}

template<class Engine>
void invalid_key_atomicity(bool sustain) {
    auto processor = active_processor<Engine>(44100.0, sustain);
    auto reference = std::make_unique<Engine>(processor->engine());
    const auto before = snapshot(processor->engine());
    const auto cursor = processor->frame_cursor();
    constexpr std::array<std::size_t, 3> invalid{{
        rhodes::key_count, rhodes::key_count + 1, std::numeric_limits<std::size_t>::max()
    }};
    for (const auto key : invalid) {
        require(!processor->release_key(key), "invalid release key accepted");
        require(processor->frame_cursor() == cursor && !processor->requires_reset(),
                "invalid key advanced cursor or introduced a fault");
        expect_mechanics(processor->engine(), before);
    }
    std::array<double, 97> actual{}, expected{};
    expect_ok(processor->process(actual), actual.size());
    require(reference->process(expected), "invalid-key reference continuation failed");
    require(actual == expected, "invalid release changed hidden controls or filter history");
    require(processor->release_key(72) && reference->note_off(72),
            "invalid release prevented a later valid release");
    expect_ok(processor->process(actual), actual.size());
    require(reference->process(expected) && actual == expected,
            "valid continuation after invalid release differs");
}

void calibrate_allocation_guard() {
    std::array<void*, 2> calibration{};
    std::size_t observed = 0;
    {
        release_allocation_probe::Scope guard;
        calibration[0] = ::operator new(8);
        calibration[1] = ::operator new[](8);
        observed = release_allocation_probe::attempts;
    }
    ::operator delete(calibration[0]);
    ::operator delete[](calibration[1]);
    require(observed == 2, "ordinary/array allocation probe did not calibrate");
}

template<class Engine>
void allocation_free_releases(bool sustain) {
    auto processor = active_processor<Engine>(44100.0, sustain);
    bool ok = true;
    std::size_t observed = 0;
    {
        release_allocation_probe::Scope guard;
        for (std::size_t pass = 0; pass < 2; ++pass) {
            for (std::size_t key = 0; key < rhodes::key_count; ++key) {
                ok = processor->release_key(key) && ok;
            }
        }
        ok = !processor->release_key(rhodes::key_count) && ok;
        observed = release_allocation_probe::attempts;
    }
    require(ok && observed == 0, "release failed or allocated ordinary/array C++ storage");
    require(processor->frame_cursor() == 137 && !processor->requires_reset(),
            "guarded releases advanced cursor or faulted");
}

// A valid release may fail after changing engine state. Preserve that accepted
// partial state and fence later calls; the port cannot promise rollback.
template<bool FailSample = false>
struct ReleaseProbeEngine {
    std::size_t samples = 0;
    std::size_t releases = 0;
    std::size_t strikes = 0;
    std::size_t pedals = 0;
    std::size_t resets = 0;
    std::size_t last_key = no_failure;
    double rate = 48000.0;

    bool prepare(double value) noexcept {
        if (!std::isfinite(value) || value < rhodes::minimum_sample_rate_hz ||
            value > rhodes::maximum_sample_rate_hz) { return false; }
        reset();
        rate = value;
        return true;
    }
    void reset() noexcept {
        samples = releases = strikes = pedals = 0;
        last_key = no_failure;
        ++resets;
    }
    bool note_on(std::size_t, double) noexcept { ++strikes; return true; }
    bool note_off(std::size_t key) noexcept {
        ++releases;
        last_key = key;
        return key != 17;
    }
    void set_sustain(bool) noexcept { ++pedals; }
    void all_notes_off() noexcept { releases += rhodes::key_count; }
    bool process(std::span<double> output) noexcept {
        for (double& value : output) {
            value = static_cast<double>(samples + 1);
            if (samples++ == 4 && FailSample) { return false; }
        }
        return true;
    }
};

void dispatch_fault_and_recovery() {
    rhodes::BasicEventProcessor<ReleaseProbeEngine<>> processor;
    std::array<double, 3> warmup{};
    expect_ok(processor.process(warmup), warmup.size());
    for (const auto key : {rhodes::key_count, std::numeric_limits<std::size_t>::max()}) {
        require(!processor.release_key(key) && processor.engine().releases == 0 &&
                processor.engine().last_key == no_failure && !processor.requires_reset(),
                "invalid release reached the engine or latched a fault");
    }
    require(processor.release_key(72) && processor.engine().releases == 1 &&
            processor.engine().last_key == 72, "valid release was not dispatched exactly once");
    require(!processor.release_key(17), "failing valid release reported success");
    require(processor.requires_reset() && processor.frame_cursor() == 3 &&
            processor.engine().samples == 3 && processor.engine().releases == 2 &&
            processor.engine().last_key == 17, "release failure lost accepted partial state");
    require(!processor.release_key(72) && !processor.release_key(17) &&
            !processor.release_key(rhodes::key_count) && !processor.set_sustain(true),
            "faulted processor accepted an audio-owner control");
    require(processor.engine().releases == 2 && processor.engine().last_key == 17 &&
            processor.engine().pedals == 0, "fault fence dispatched a later control");

    constexpr std::array<EngineEvent, 1> reset_event{{{0, 0, EventType::reset, 0, 0.0}}};
    std::array<double, 5> output{};
    output.fill(77.0);
    expect_fenced(processor.process(output, reset_event));
    require(std::all_of(output.begin(), output.end(), [](double value) { return value == 0.0; }),
            "release fault did not silence subsequent output");
    expect_fenced(processor.process({}));
    require(processor.engine().samples == 3 && processor.engine().resets == 0 &&
            processor.frame_cursor() == 3, "fault fence rendered or executed a queued reset");
    require(!processor.prepare(std::numeric_limits<double>::quiet_NaN()) &&
            processor.requires_reset() && processor.frame_cursor() == 3 &&
            processor.engine().releases == 2 && processor.engine().samples == 3 &&
            processor.engine().resets == 0, "rejected prepare changed the release-fault state");

    processor.reset();
    require(!processor.requires_reset() && processor.frame_cursor() == 0 &&
            processor.engine().resets == 1 && processor.engine().releases == 0 &&
            processor.engine().samples == 0, "reset did not recover a release fault");
    require(processor.release_key(0), "release remained fenced after reset");
    expect_ok(processor.process(output), output.size());
    require(output.front() == 1.0 && output.back() == 5.0, "reset did not restore rendering");

    require(!processor.release_key(17) && processor.requires_reset(), "second release fault missing");
    require(processor.prepare(96000.0) && !processor.requires_reset() &&
            processor.frame_cursor() == 0 && processor.engine().rate == 96000.0 &&
            processor.engine().resets == 2 && processor.engine().releases == 0 &&
            processor.engine().samples == 0, "valid prepare did not recover a release fault");
    require(processor.release_key(72), "release remained fenced after prepare");
    expect_ok(processor.process(output), output.size());
    require(output.front() == 1.0 && output.back() == 5.0, "prepare did not restore rendering");
}

void preexisting_faults_block_release() {
    rhodes::BasicEventProcessor<ReleaseProbeEngine<>> event_fault;
    constexpr std::array<EngineEvent, 1> events{{{3, 0, EventType::note_off, 17, 0.0}}};
    std::array<double, 8> output{};
    const auto event_result = event_fault.process(output, events);
    require(event_result.status == ProcessStatus::arithmetic_fault &&
            event_result.frames_processed == 3 && event_result.failing_sample == 3 &&
            event_result.failing_event == 0 && event_result.requires_reset &&
            event_result.subsystem == ProcessSubsystem::events, "event fault fixture did not fail");
    require(!event_fault.release_key(72) && event_fault.engine().releases == 1 &&
            event_fault.engine().last_key == 17 && event_fault.engine().samples == 3 &&
            event_fault.frame_cursor() == 3, "preexisting event fault allowed release dispatch");

    rhodes::BasicEventProcessor<ReleaseProbeEngine<true>> sample_fault;
    const auto sample_result = sample_fault.process(output);
    require(sample_result.status == ProcessStatus::arithmetic_fault &&
            sample_result.frames_processed == 4 && sample_result.failing_sample == 4 &&
            sample_result.failing_event == no_failure && sample_result.requires_reset &&
            sample_result.subsystem == ProcessSubsystem::engine, "sample fault fixture did not fail");
    require(!sample_fault.release_key(0) && sample_fault.engine().releases == 0 &&
            sample_fault.engine().last_key == no_failure && sample_fault.engine().samples == 5 &&
            sample_fault.frame_cursor() == 4, "preexisting sample fault allowed release dispatch");
}

template<class Engine>
void engine_contract() {
    for (const double rate : {44100.0, 96000.0, 192000.0}) {
        no_advance_and_tail_continuation<Engine>(rate);
        sustain_and_held_state<Engine>(rate);
    }
    for (const bool sustain : {false, true}) {
        invalid_key_atomicity<Engine>(sustain);
        allocation_free_releases<Engine>(sustain);
    }
}
} // namespace

int main() {
    std::string_view fixture = "allocation_guard_calibration";
    try {
        calibrate_allocation_guard();
        fixture = "ResearchEngine";
        engine_contract<rhodes::ResearchEngine>();
        std::cout << "PASS " << fixture << '\n';
        fixture = "AudioEngine";
        engine_contract<rhodes::AudioEngine>();
        std::cout << "PASS " << fixture << '\n';
        fixture = "QuadratureEngine";
        engine_contract<rhodes::QuadratureEngine>();
        std::cout << "PASS " << fixture << '\n';
        fixture = "dispatch_fault_and_recovery";
        dispatch_fault_and_recovery();
        std::cout << "PASS " << fixture << '\n';
        fixture = "preexisting_faults_block_release";
        preexisting_faults_block_release();
        std::cout << "PASS " << fixture << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << fixture << ": " << error.what() << '\n';
        return 1;
    }
}
