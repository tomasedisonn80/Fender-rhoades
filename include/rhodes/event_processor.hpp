#pragma once

#include "rhodes/research_engine.hpp"
#include "rhodes/control_value.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <span>

namespace rhodes {

inline constexpr std::size_t maximum_block_frames = 4096;
inline constexpr std::size_t maximum_block_events = 256;
inline constexpr std::size_t no_failure_index = std::numeric_limits<std::size_t>::max();

enum class EventType : std::uint8_t { note_on, note_off, sustain, all_notes_off, reset };

// Physical-key events. A host translates MIDI/channel ownership before this API.
// Equal-offset events execute in strictly increasing producer sequence order.
struct EngineEvent {
    std::uint32_t offset = 0;
    std::uint32_t sequence = 0;
    EventType type = EventType::note_on;
    std::size_t key = 0;
    double value = 0.0; // Note intensity [0,1], sustain 0 or 1; otherwise zero.
};

enum class ProcessStatus : std::uint8_t { ok, invalid_input, arithmetic_fault, reset_required };
enum class ProcessSubsystem : std::uint8_t { none, events, engine };

struct ProcessResult {
    ProcessStatus status = ProcessStatus::ok;
    std::size_t frames_processed = 0;
    std::size_t failing_sample = no_failure_index;
    std::size_t failing_event = no_failure_index;
    bool requires_reset = false;
    ProcessSubsystem subsystem = ProcessSubsystem::none;
    [[nodiscard]] bool succeeded() const noexcept { return status == ProcessStatus::ok; }
};

// Static engine substitution permits deterministic fault tests without adding a
// production fault switch, allocation, lock, virtual dispatch or callback hook.
// Engine follows ResearchEngine's noexcept operations and failure semantics.
template<class Engine>
class BasicEventProcessor {
public:
    [[nodiscard]] bool prepare(double sample_rate_hz) noexcept {
        if (!engine_.prepare(sample_rate_hz)) { return false; }
        cursor_ = 0;
        faulted_ = false;
        return true;
    }

    void reset() noexcept {
        engine_.reset();
        cursor_ = 0;
        faulted_ = false;
    }

    // Audio-owner configuration port for a validated host parameter-only flush.
    // Changes the surrogate loss selection without advancing or resetting q/v.
    // A host must preflight its entire transaction before calling this method.
    [[nodiscard]] bool set_sustain(bool down) noexcept {
        if (faulted_) { return false; }
        engine_.set_sustain(down);
        return true;
    }

    // Audio-owner release port for a fully preflighted parameter-only flush.
    // A release changes damping without rendering, advancing the cursor or
    // clearing mechanical/filter state. Invalid keys do not introduce a fault;
    // unexpected failure of a valid release fences subsequent processing.
    [[nodiscard]] bool release_key(std::size_t key) noexcept {
        if (key >= key_count || faulted_) { return false; }
        if (!engine_.note_off(key)) {
            faulted_ = true;
            return false;
        }
        return true;
    }

    [[nodiscard]] ProcessResult process(std::span<double> output,
                                        std::span<const EngineEvent> events = {}) noexcept {
        const auto reject = [&](std::size_t event) noexcept {
            std::fill(output.begin(), output.end(), 0.0);
            return ProcessResult{ProcessStatus::invalid_input, 0, no_failure_index, event,
                                 faulted_, ProcessSubsystem::events};
        };
        if (output.size() > maximum_block_frames || events.size() > maximum_block_events ||
            output.size() > std::numeric_limits<std::uint64_t>::max() - cursor_) {
            return reject(no_failure_index);
        }
        for (std::size_t i = 0; i < events.size(); ++i) {
            const auto& event = events[i];
            if (event.offset >= output.size() || !valid_event(event) ||
                (i > 0 && (event.offset < events[i - 1].offset ||
                 (event.offset == events[i - 1].offset &&
                  event.sequence <= events[i - 1].sequence)))) {
                return reject(i);
            }
        }
        if (faulted_) {
            std::fill(output.begin(), output.end(), 0.0);
            return {ProcessStatus::reset_required, 0, no_failure_index, no_failure_index,
                    true, ProcessSubsystem::engine};
        }
        std::size_t event_index = 0;
        for (std::size_t frame = 0; frame < output.size(); ++frame) {
            while (event_index < events.size() && events[event_index].offset == frame) {
                if (!apply(events[event_index])) {
                    return fail(output, frame, event_index, ProcessSubsystem::events);
                }
                ++event_index;
            }
            if (!engine_.process(output.subspan(frame, 1))) {
                return fail(output, frame, no_failure_index, ProcessSubsystem::engine);
            }
            ++cursor_;
        }
        return {ProcessStatus::ok, output.size(), no_failure_index, no_failure_index,
                false, ProcessSubsystem::none};
    }

    [[nodiscard]] const Engine& engine() const noexcept { return engine_; }
    [[nodiscard]] std::uint64_t frame_cursor() const noexcept { return cursor_; }
    [[nodiscard]] bool requires_reset() const noexcept { return faulted_; }

private:
    static bool valid_event(const EngineEvent& event) noexcept {
        switch (event.type) {
        case EventType::note_on:
            return event.key < key_count && control::valid_normalized(event.value);
        case EventType::note_off:
            return event.key < key_count && control::is_zero(event.value);
        case EventType::sustain:
            return event.key == 0 && control::is_boolean(event.value);
        case EventType::all_notes_off:
        case EventType::reset:
            return event.key == 0 && control::is_zero(event.value);
        }
        return false;
    }

    bool apply(const EngineEvent& event) noexcept {
        switch (event.type) {
        case EventType::note_on: return engine_.note_on(event.key, event.value);
        case EventType::note_off: return engine_.note_off(event.key);
        case EventType::sustain: engine_.set_sustain(!control::is_zero(event.value)); return true;
        case EventType::all_notes_off: engine_.all_notes_off(); return true;
        case EventType::reset: engine_.reset(); return true;
        }
        return false;
    }

    ProcessResult fail(std::span<double> output, std::size_t frame,
                       std::size_t event, ProcessSubsystem subsystem) noexcept {
        std::fill(output.begin() + static_cast<std::ptrdiff_t>(frame), output.end(), 0.0);
        faulted_ = true;
        return {ProcessStatus::arithmetic_fault, frame, frame, event, true, subsystem};
    }

    Engine engine_{};
    std::uint64_t cursor_ = 0;
    bool faulted_ = false;
};

using EventProcessor = BasicEventProcessor<ResearchEngine>;

} // namespace rhodes
