#include "rhodes/event_processor.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace {
using rhodes::EngineEvent;
using rhodes::EventProcessor;
using rhodes::EventType;
using rhodes::ProcessStatus;
using rhodes::ProcessSubsystem;
constexpr auto no_failure = rhodes::no_failure_index;

void require(bool condition, std::string_view message) {
    if (!condition) { throw std::runtime_error(std::string{message}); }
}

void expect_result(const rhodes::ProcessResult& result, ProcessStatus status,
                   std::size_t frames, std::size_t sample, std::size_t event,
                   bool reset, ProcessSubsystem subsystem) {
    require(result.status == status, "unexpected process status");
    require(result.succeeded() == (status == ProcessStatus::ok), "incorrect succeeded flag");
    require(result.frames_processed == frames, "incorrect processed-frame count");
    require(result.failing_sample == sample, "incorrect failing-sample index");
    require(result.failing_event == event, "incorrect failing-event index");
    require(result.requires_reset == reset, "incorrect reset requirement");
    require(result.subsystem == subsystem, "incorrect failure subsystem");
}

void expect_ok(const rhodes::ProcessResult& result, std::size_t frames) {
    expect_result(result, ProcessStatus::ok, frames, no_failure, no_failure,
                  false, ProcessSubsystem::none);
}

void expect_zero(std::span<const double> output) {
    require(std::all_of(output.begin(), output.end(), [](double value) { return value == 0.0; }),
            "rejected or failed output was not completely silenced");
}

void same_engine(const rhodes::ResearchEngine& actual,
                 const rhodes::ResearchEngine& expected) {
    require(actual.sample_rate_hz() == expected.sample_rate_hz(), "sample rate changed");
    require(actual.mechanical_energy_joules() == expected.mechanical_energy_joules(),
            "mechanical energy differs");
    for (std::size_t key = 0; key < rhodes::key_count; ++key) {
        for (std::size_t mode = 0; mode < rhodes::modes_per_key; ++mode) {
            const auto observed = actual.modal_state(key, mode);
            const auto reference = expected.modal_state(key, mode);
            require(observed.has_value() && reference.has_value(), "missing modal state");
            require(observed->displacement_m == reference->displacement_m &&
                    observed->velocity_m_per_s == reference->velocity_m_per_s,
                    "modal state differs at key " + std::to_string(key) +
                    ", mode " + std::to_string(mode));
        }
    }
}

constexpr std::array<EngineEvent, 6> scheduled_events{{
    {0, 0, EventType::note_on, 17, 0.75},
    {63, 1, EventType::note_off, 17, 0.0},
    {63, 2, EventType::note_on, 17, 0.5},
    {64, 3, EventType::sustain, 0, 1.0},
    {64, 4, EventType::note_off, 17, 0.0},
    {128, 5, EventType::sustain, 0, 0.0}
}};

struct ScalarReference {
    rhodes::ResearchEngine engine;
    std::array<double, 130> output{};
};

ScalarReference scalar_reference(bool reverse_tie = false) {
    ScalarReference reference;
    // Direct scalar scheduling is independent of the event dispatcher and its
    // event table. Reversing the tie changes held state before sample 63.
    for (std::size_t frame = 0; frame < reference.output.size(); ++frame) {
        if (frame == 0) { require(reference.engine.note_on(17, 0.75), "scalar initial strike"); }
        if (frame == 63) {
            if (reverse_tie) {
                require(reference.engine.note_on(17, 0.5), "scalar reversed restrike");
                require(reference.engine.note_off(17), "scalar reversed release");
            } else {
                require(reference.engine.note_off(17), "scalar release");
                require(reference.engine.note_on(17, 0.5), "scalar restrike");
            }
        }
        if (frame == 64) {
            reference.engine.set_sustain(true);
            require(reference.engine.note_off(17), "scalar sustained release");
        }
        if (frame == 128) { reference.engine.set_sustain(false); }
        require(reference.engine.process(std::span<double>{reference.output}.subspan(frame, 1)),
                "scalar sample processing failed");
    }
    return reference;
}

void scalar_partitions() {
    const auto reference = scalar_reference();
    require(std::any_of(reference.output.begin(), reference.output.end(),
                        [](double value) { return value != 0.0; }), "scalar fixture is silent");
    for (const std::size_t partition : {130U, 64U, 1U}) {
        EventProcessor processor;
        std::array<double, 130> output{};
        for (std::size_t first = 0; first < output.size();) {
            const auto frames = std::min(partition, output.size() - first);
            std::array<EngineEvent, scheduled_events.size()> local{};
            std::size_t count = 0;
            for (auto event : scheduled_events) {
                if (event.offset >= first && event.offset < first + frames) {
                    event.offset -= static_cast<std::uint32_t>(first);
                    local[count++] = event;
                }
            }
            expect_ok(processor.process(std::span<double>{output}.subspan(first, frames),
                                        std::span<const EngineEvent>{local}.first(count)), frames);
            first += frames;
            require(processor.frame_cursor() == first, "partition cursor differs");
            require(!processor.requires_reset(), "successful partition latched a fault");
        }
        require(output == reference.output, "partition differs from independent scalar schedule");
        same_engine(processor.engine(), reference.engine);
    }
}

void equal_offset_order() {
    const auto forward = scalar_reference();
    const auto reversed = scalar_reference(true);
    auto events = scheduled_events;
    std::swap(events[1].type, events[2].type);
    std::swap(events[1].value, events[2].value);
    EventProcessor processor;
    std::array<double, 130> output{};
    expect_ok(processor.process(output, events), output.size());
    require(output == reversed.output, "equal-offset producer order was not preserved");
    require(output != forward.output, "reversed tie failed to distinguish event order");
    same_engine(processor.engine(), reversed.engine);

    // Sequence ordering is local to one offset; it may restart at a later frame.
    constexpr std::array<EngineEvent, 2> restarted_sequence{{
        {0, 9, EventType::note_on, 17, 0.5}, {1, 0, EventType::note_off, 17, 0.0}
    }};
    std::array<double, 2> short_output{};
    expect_ok(processor.process(short_output, restarted_sequence), short_output.size());
}

EventProcessor active_processor() {
    EventProcessor processor;
    require(processor.prepare(44100.0), "prepare active preflight fixture");
    constexpr std::array<EngineEvent, 3> events{{
        {0, 0, EventType::note_on, 17, 0.75},
        {1, 0, EventType::sustain, 0, 1.0},
        {2, 0, EventType::note_off, 17, 0.0}
    }};
    std::array<double, 29> output{};
    expect_ok(processor.process(output, events), output.size());
    return processor;
}

void same_processor(const EventProcessor& actual, const EventProcessor& expected) {
    require(actual.frame_cursor() == expected.frame_cursor(), "rejection advanced cursor");
    require(actual.requires_reset() == expected.requires_reset(), "rejection changed fault latch");
    same_engine(actual.engine(), expected.engine());
    auto actual_engine = actual.engine();
    auto expected_engine = expected.engine();
    std::array<double, 67> actual_output{}, expected_output{};
    require(actual_engine.process(actual_output) && expected_engine.process(expected_output),
            "continuation after rejection failed");
    require(actual_output == expected_output, "rejection changed future trajectory or controls");
}

void preflight_atomicity() {
    auto processor = active_processor();
    const auto before = processor;
    constexpr std::array<EngineEvent, 4> invalid{{
        {0, 0, EventType::note_on, 1, 1.0},
        {0, 1, EventType::sustain, 0, 0.0},
        {0, 2, EventType::reset, 0, 0.0},
        {1, 0, EventType::note_off, rhodes::key_count, 0.0}
    }};
    std::array<double, 2> output{71.0, 83.0};
    expect_result(processor.process(output, invalid), ProcessStatus::invalid_input, 0,
                  no_failure, 3, false, ProcessSubsystem::events);
    expect_zero(output);
    same_processor(processor, before);
}

void invalid_events() {
    const auto baseline = active_processor();
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double infinity = std::numeric_limits<double>::infinity();
    const std::array<EngineEvent, 18> invalid{{
        {0, 0, EventType::note_on, rhodes::key_count, 0.5},
        {0, 0, EventType::note_on, 0, -0.01},
        {0, 0, EventType::note_on, 0, 1.01},
        {0, 0, EventType::note_on, 0, nan},
        {0, 0, EventType::note_on, 0, infinity},
        {0, 0, EventType::note_off, rhodes::key_count, 0.0},
        {0, 0, EventType::note_off, 0, 0.1},
        {0, 0, EventType::sustain, 1, 1.0},
        {0, 0, EventType::sustain, 0, 0.5},
        {0, 0, EventType::sustain, 0, -1.0},
        {0, 0, EventType::all_notes_off, 1, 0.0},
        {0, 0, EventType::all_notes_off, 0, 1.0},
        {0, 0, EventType::reset, 1, 0.0},
        {0, 0, EventType::reset, 0, 1.0},
        {0, 0, EventType::reset, 0, nan},
        {0, 0, static_cast<EventType>(255), 0, 0.0},
        {2, 0, EventType::reset, 0, 0.0},
        {std::numeric_limits<std::uint32_t>::max(), 0, EventType::reset, 0, 0.0}
    }};
    for (std::size_t index = 0; index < invalid.size(); ++index) {
        auto processor = baseline;
        std::array<double, 2> output{71.0, 83.0};
        const auto result = processor.process(output, std::span<const EngineEvent>{invalid}.subspan(index, 1));
        expect_result(result, ProcessStatus::invalid_input, 0, no_failure, 0,
                      false, ProcessSubsystem::events);
        expect_zero(output);
        same_processor(processor, baseline);
    }
    constexpr std::array<std::array<EngineEvent, 2>, 3> invalid_order{{
        {{{1, 0, EventType::note_off, 1, 0.0}, {0, 1, EventType::note_off, 1, 0.0}}},
        {{{0, 4, EventType::note_off, 1, 0.0}, {0, 4, EventType::note_on, 1, 1.0}}},
        {{{0, 4, EventType::note_off, 1, 0.0}, {0, 3, EventType::note_on, 1, 1.0}}}
    }};
    for (const auto& events : invalid_order) {
        auto processor = baseline;
        std::array<double, 2> output{71.0, 83.0};
        expect_result(processor.process(output, events), ProcessStatus::invalid_input, 0,
                      no_failure, 1, false, ProcessSubsystem::events);
        expect_zero(output);
        same_processor(processor, baseline);
    }
}

// Deterministic substitution exercises failure semantics without a production
// fault switch. The failing sample is deliberately written before returning false.
template<bool FailSample, bool FailEvent>
struct InstrumentedEngine {
    std::size_t samples_attempted = 0;
    std::size_t note_on_attempts = 0;
    std::size_t note_off_calls = 0;
    std::size_t sustain_calls = 0;
    std::size_t all_notes_off_calls = 0;
    std::size_t reset_calls = 0;

    bool prepare(double rate) noexcept {
        if (!std::isfinite(rate) || rate < rhodes::minimum_sample_rate_hz ||
            rate > rhodes::maximum_sample_rate_hz) { return false; }
        reset();
        return true;
    }
    void reset() noexcept {
        samples_attempted = note_on_attempts = note_off_calls = 0;
        sustain_calls = all_notes_off_calls = 0;
        ++reset_calls;
    }
    bool note_on(std::size_t key, double) noexcept {
        ++note_on_attempts;
        return !(FailEvent && key == 17);
    }
    bool note_off(std::size_t) noexcept { ++note_off_calls; return true; }
    void set_sustain(bool) noexcept { ++sustain_calls; }
    void all_notes_off() noexcept { ++all_notes_off_calls; }
    bool process(std::span<double> output) noexcept {
        for (double& value : output) {
            value = static_cast<double>(samples_attempted + 1);
            if (samples_attempted++ == 17 && FailSample) { return false; }
        }
        return true;
    }
};

void block_limits() {
    static_assert(rhodes::maximum_block_frames == 4096);
    static_assert(rhodes::maximum_block_events == 256);
    auto processor = active_processor();
    const auto before = processor;
    expect_ok(processor.process({}), 0);
    same_processor(processor, before);
    constexpr std::array<EngineEvent, 1> reset_event{{{0, 0, EventType::reset, 0, 0.0}}};
    expect_result(processor.process({}, reset_event), ProcessStatus::invalid_input, 0,
                  no_failure, 0, false, ProcessSubsystem::events);
    same_processor(processor, before);

    std::array<EngineEvent, 257> events{};
    for (std::size_t index = 0; index < events.size(); ++index) {
        events[index] = {0, static_cast<std::uint32_t>(index), EventType::note_off, 1, 0.0};
    }
    std::array<double, 4097> output{};
    output.fill(71.0);
    expect_result(processor.process(std::span<double>{output}.first(2), events),
                  ProcessStatus::invalid_input, 0, no_failure, no_failure,
                  false, ProcessSubsystem::events);
    expect_zero(std::span<const double>{output}.first(2));
    same_processor(processor, before);
    output.fill(71.0);
    expect_result(processor.process(output), ProcessStatus::invalid_input, 0,
                  no_failure, no_failure, false, ProcessSubsystem::events);
    expect_zero(output);
    same_processor(processor, before);

    rhodes::BasicEventProcessor<InstrumentedEngine<false, false>> maximum;
    output.fill(71.0);
    expect_ok(maximum.process(std::span<double>{output}.first(4096),
                              std::span<const EngineEvent>{events}.first(256)), 4096);
    require(maximum.engine().note_off_calls == 256, "maximum event block dropped events");
    require(maximum.engine().samples_attempted == 4096 && maximum.frame_cursor() == 4096,
            "maximum frame block was truncated");
    for (std::size_t index = 0; index < 4096; ++index) {
        require(output[index] == static_cast<double>(index + 1), "maximum block sample differs");
    }
    require(output.back() == 71.0, "processing wrote beyond the requested output span");
}

void sample_fault_reset() {
    rhodes::BasicEventProcessor<InstrumentedEngine<true, false>> processor;
    std::array<double, 64> output{};
    output.fill(77.0);
    expect_result(processor.process(output), ProcessStatus::arithmetic_fault, 17, 17,
                  no_failure, true, ProcessSubsystem::engine);
    require(processor.frame_cursor() == 17 && processor.requires_reset(), "sample fault not latched");
    require(processor.engine().samples_attempted == 18, "processing continued after sample fault");
    for (std::size_t index = 0; index < output.size(); ++index) {
        const double expected = index < 17 ? static_cast<double>(index + 1) : 0.0;
        require(output[index] == expected, "sample fault lost prefix or retained failed suffix");
    }
    constexpr std::array<EngineEvent, 1> reset_event{{{0, 0, EventType::reset, 0, 0.0}}};
    output.fill(77.0);
    expect_result(processor.process(output, reset_event), ProcessStatus::reset_required, 0,
                  no_failure, no_failure, true, ProcessSubsystem::engine);
    expect_zero(output);
    require(processor.engine().samples_attempted == 18 && processor.engine().reset_calls == 0,
            "fault fence executed samples or a queued reset event");
    expect_result(processor.process({}), ProcessStatus::reset_required, 0, no_failure,
                  no_failure, true, ProcessSubsystem::engine);
    expect_result(processor.process({}, reset_event), ProcessStatus::invalid_input, 0,
                  no_failure, 0, true, ProcessSubsystem::events);
    require(!processor.prepare(std::numeric_limits<double>::quiet_NaN()), "invalid prepare accepted");
    require(processor.requires_reset() && processor.frame_cursor() == 17 &&
            processor.engine().samples_attempted == 18, "invalid prepare cleared fault state");
    processor.reset();
    require(!processor.requires_reset() && processor.frame_cursor() == 0 &&
            processor.engine().samples_attempted == 0, "explicit reset failed to recover");
    expect_ok(processor.process(std::span<double>{output}.first(2)), 2);
    require(output[0] == 1.0 && output[1] == 2.0, "reset failed to restore sample sequence");
    expect_result(processor.process(output), ProcessStatus::arithmetic_fault, 15, 15,
                  no_failure, true, ProcessSubsystem::engine);
    require(processor.frame_cursor() == 17, "fault coordinate was confused with global cursor");
    require(processor.prepare(48000.0), "valid prepare failed after fault");
    require(!processor.requires_reset() && processor.frame_cursor() == 0, "prepare failed to recover");
    expect_ok(processor.process(std::span<double>{output}.first(2)), 2);
    require(output[0] == 1.0 && output[1] == 2.0, "prepare failed to reset engine state");
}

void event_fault_reset() {
    rhodes::BasicEventProcessor<InstrumentedEngine<false, true>> processor;
    constexpr std::array<EngineEvent, 3> events{{
        {0, 0, EventType::note_off, 2, 0.0},
        {5, 1, EventType::note_on, 17, 0.75},
        {5, 2, EventType::reset, 0, 0.0}
    }};
    std::array<double, 16> output{};
    output.fill(77.0);
    expect_result(processor.process(output, events), ProcessStatus::arithmetic_fault, 5, 5,
                  1, true, ProcessSubsystem::events);
    require(processor.requires_reset() && processor.frame_cursor() == 5, "event fault not latched");
    require(processor.engine().samples_attempted == 5 && processor.engine().note_on_attempts == 1 &&
            processor.engine().note_off_calls == 1 && processor.engine().reset_calls == 0,
            "event failure lost earlier work or executed later events/samples");
    for (std::size_t index = 0; index < output.size(); ++index) {
        const double expected = index < 5 ? static_cast<double>(index + 1) : 0.0;
        require(output[index] == expected, "event failure lost prefix or retained suffix");
    }
    output.fill(77.0);
    expect_result(processor.process(output, events), ProcessStatus::reset_required, 0,
                  no_failure, no_failure, true, ProcessSubsystem::engine);
    expect_zero(output);
    require(processor.engine().samples_attempted == 5 && processor.engine().note_on_attempts == 1 &&
            processor.engine().note_off_calls == 1, "event fault fence mutated engine");
    processor.reset();
    require(!processor.requires_reset() && processor.frame_cursor() == 0, "event fault reset failed");
    constexpr std::array<EngineEvent, 1> valid{{{0, 0, EventType::note_on, 18, 0.5}}};
    expect_ok(processor.process(output, valid), output.size());
    require(output.front() == 1.0 && output.back() == 16.0 &&
            processor.engine().note_on_attempts == 1, "event fault recovery failed");
}

using Test = void (*)();
constexpr std::array<std::pair<std::string_view, Test>, 7> tests{{
    {"scalar_partitions", scalar_partitions},
    {"equal_offset_order", equal_offset_order},
    {"preflight_atomicity", preflight_atomicity},
    {"invalid_events", invalid_events},
    {"block_limits", block_limits},
    {"sample_fault_reset", sample_fault_reset},
    {"event_fault_reset", event_fault_reset}
}};
} // namespace

int main(int argc, char** argv) {
    const std::string_view selected = argc > 1 ? std::string_view{argv[1]} : "all";
    try {
        require(argc <= 2, "usage: event_processor_test [fixture|all]");
        bool found = false;
        for (const auto& [name, test] : tests) {
            if (selected == "all" || selected == name) {
                test();
                std::cout << "PASS " << name << '\n';
                found = true;
            }
        }
        require(found, "unknown event-processor fixture");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << selected << ": " << error.what() << '\n';
        return 1;
    }
}
