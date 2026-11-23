#include "rhodes/note_tracker.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace {
using rhodes::EventType;
using rhodes::MidiNote;
using rhodes::NoteStatus;
using rhodes::NoteTracker;

static_assert(NoteTracker::capacity == 256);
static_assert(std::is_trivially_copyable_v<NoteTracker>);
static_assert(std::is_nothrow_copy_assignable_v<NoteTracker>);
static_assert(noexcept(std::declval<NoteTracker&>().apply(MidiNote{})));
static_assert(noexcept(std::declval<NoteTracker&>().release_channel(0)));
static_assert(noexcept(std::declval<NoteTracker&>().reset()));

void require(bool condition, std::string_view message) {
    if (!condition) { throw std::runtime_error(std::string{message}); }
}

void expect_none(const rhodes::NoteResult& result, NoteStatus status = NoteStatus::ok) {
    require(result.status == status, "unexpected note status");
    require(result.succeeded() == (status == NoteStatus::ok), "incorrect success predicate");
    require(!result.change.has_value(), "unexpected physical-key change");
}

void expect_change(const rhodes::NoteResult& result, EventType type,
                   std::size_t key, double value = 0.0) {
    require(result.status == NoteStatus::ok && result.succeeded(), "expected accepted change");
    require(result.change.has_value(), "missing physical-key change");
    require(result.change->type == type && result.change->key == key &&
            result.change->value == value, "incorrect physical-key change");
}

void expect_counts(const NoteTracker& tracker, std::size_t active,
                   std::size_t key, std::size_t held) {
    require(tracker.active_count() == active, "incorrect active holder count");
    require(tracker.held_count(key) == held, "incorrect physical-key holder count");
    std::size_t total = 0;
    for (std::size_t index = 0; index < rhodes::key_count; ++index) {
        total += tracker.held_count(index);
    }
    require(total == active, "per-key counts disagree with active holders");
}

void range_and_validation() {
    NoteTracker tracker;
    expect_change(tracker.apply({true, 0, 28, 1, 1.0}), EventType::note_on, 0, 1.0);
    expect_change(tracker.apply({true, 15, 100, 2, 0.5}), EventType::note_on, 72, 0.5);
    expect_counts(tracker, 2, 72, 1);
    for (const int pitch : {0, 27, 101, 127}) {
        // Even an existing identity is harmless when the pitch is outside the keyboard.
        expect_none(tracker.apply({true, 0, pitch, 1, 0.25}));
        expect_none(tracker.apply({false, 0, pitch, 1, 0.25}));
    }
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double infinity = std::numeric_limits<double>::infinity();
    const std::array<MidiNote, 14> invalid{{
        {true, -1, 28, 1, 1.0}, {true, 16, 28, 1, 1.0},
        {true, 0, -1, 1, 1.0}, {true, 0, 128, 1, 1.0},
        {true, 0, 28, 1, -0.01}, {true, 0, 28, 1, 1.01},
        {true, 0, 28, 1, nan}, {true, 0, 28, 1, infinity},
        {false, 0, 28, 1, -0.01}, {false, 0, 28, 1, 1.01},
        {false, 0, 28, 1, nan}, {false, 0, 28, 1, -infinity},
        {true, 0, 27, 1, nan}, {false, 16, 101, 1, 0.0}
    }};
    for (const auto& note : invalid) {
        expect_none(tracker.apply(note), NoteStatus::invalid_input);
        expect_counts(tracker, 2, 0, 1);
        require(tracker.held_count(72) == 1, "invalid note changed distant key");
    }
    require(tracker.held_count(73) == 0 &&
            tracker.held_count(std::numeric_limits<std::size_t>::max()) == 0,
            "out-of-range held query was not zero");
    expect_change(tracker.apply({false, 0, 28, 1, 1.0}), EventType::note_off, 0);
    expect_change(tracker.apply({false, 15, 100, 2, 0.0}), EventType::note_off, 72);
    expect_counts(tracker, 0, 0, 0);
}

void identified_overlap() {
    NoteTracker tracker;
    expect_change(tracker.apply({true, 0, 60, 7, 0.5}), EventType::note_on, 32, 0.5);
    expect_change(tracker.apply({true, 0, 60, 7, 0.75}), EventType::note_on, 32, 0.75);
    expect_counts(tracker, 1, 32, 1);
    expect_change(tracker.apply({true, 0, 60, 8, 0.5}), EventType::note_on, 32, 0.5);
    expect_change(tracker.apply({true, 1, 60, 7, 0.25}), EventType::note_on, 32, 0.25);
    expect_counts(tracker, 3, 32, 3);
    expect_none(tracker.apply({false, 0, 60, 9, 0.0}));
    expect_none(tracker.apply({false, 2, 60, 7, 0.0}));
    expect_none(tracker.apply({false, 0, 60, 7, 0.0}));
    expect_none(tracker.apply({false, 0, 60, 7, 0.0}));
    expect_counts(tracker, 2, 32, 2);
    expect_none(tracker.apply({false, 0, 60, 8, 0.8}));
    expect_counts(tracker, 1, 32, 1);
    expect_change(tracker.apply({false, 1, 60, 7, 0.0}), EventType::note_off, 32);
    expect_none(tracker.apply({false, 1, 60, 7, 0.0}));
    expect_counts(tracker, 0, 32, 0);

    for (const auto id : {std::numeric_limits<std::int32_t>::min(), -10000, -2, 0,
                          std::numeric_limits<std::int32_t>::max()}) {
        expect_change(tracker.apply({true, 3, 64, id, 0.4}), EventType::note_on, 36, 0.4);
        expect_change(tracker.apply({true, 3, 64, id, 0.6}), EventType::note_on, 36, 0.6);
        expect_counts(tracker, 1, 36, 1);
        expect_change(tracker.apply({false, 3, 64, id, 0.0}), EventType::note_off, 36);
    }
}

void anonymous_and_zero_velocity() {
    NoteTracker tracker;
    expect_change(tracker.apply({true, 0, 60, 7, 0.5}), EventType::note_on, 32, 0.5);
    expect_none(tracker.apply({false, 0, 60, -1, 0.0}));
    expect_counts(tracker, 1, 32, 1);
    expect_change(tracker.apply({true, 0, 60, -1, 0.5}), EventType::note_on, 32, 0.5);
    expect_change(tracker.apply({true, 0, 60, -1, 0.8}), EventType::note_on, 32, 0.8);
    expect_change(tracker.apply({true, 1, 60, -1, 0.25}), EventType::note_on, 32, 0.25);
    expect_change(tracker.apply({true, 0, 61, -1, 0.25}), EventType::note_on, 33, 0.25);
    expect_counts(tracker, 5, 32, 4);
    expect_none(tracker.apply({false, 0, 60, 99, 0.0}));
    expect_none(tracker.apply({true, 0, 60, -1, 0.0}));
    expect_counts(tracker, 4, 32, 3);
    expect_none(tracker.apply({false, 0, 60, -1, 0.0}));
    expect_none(tracker.apply({false, 0, 60, -1, 0.0}));
    expect_counts(tracker, 3, 32, 2);
    expect_none(tracker.apply({true, 0, 60, 7, -0.0}));
    expect_counts(tracker, 2, 32, 1);
    expect_change(tracker.apply({true, 1, 60, -1, 0.0}), EventType::note_off, 32);
    expect_change(tracker.apply({false, 0, 61, -1, 0.75}), EventType::note_off, 33);
    expect_none(tracker.apply({true, 0, 60, 100, 0.0}));
    expect_counts(tracker, 0, 32, 0);
}

void conflict_atomicity() {
    NoteTracker tracker;
    expect_change(tracker.apply({true, 4, 40, -2, 0.5}), EventType::note_on, 12, 0.5);
    expect_change(tracker.apply({true, 4, 41, -1, 0.5}), EventType::note_on, 13, 0.5);
    for (const MidiNote note : {MidiNote{true, 4, 41, -2, 0.5},
                               MidiNote{false, 4, 41, -2, 0.0},
                               MidiNote{true, 4, 41, -2, 0.0}}) {
        expect_none(tracker.apply(note), NoteStatus::invalid_input);
        expect_counts(tracker, 2, 12, 1);
        require(tracker.held_count(13) == 1, "conflict consumed anonymous holder");
    }
    // A different channel may use the same ID with another pitch.
    expect_change(tracker.apply({true, 5, 41, -2, 0.5}), EventType::note_on, 13, 0.5);
    expect_change(tracker.apply({false, 4, 40, -2, 0.0}), EventType::note_off, 12);
    expect_none(tracker.apply({false, 4, 41, -1, 0.0}));
    expect_change(tracker.apply({false, 5, 41, -2, 0.0}), EventType::note_off, 13);
    // Reusing the released ID with a different pitch is now valid.
    expect_change(tracker.apply({true, 4, 41, -2, 0.5}), EventType::note_on, 13, 0.5);
    expect_change(tracker.apply({false, 4, 41, -2, 0.0}), EventType::note_off, 13);
    expect_counts(tracker, 0, 13, 0);
}

void capacity_and_reuse() {
    NoteTracker tracker;
    for (std::int32_t id = 0; id < 256; ++id) {
        expect_change(tracker.apply({true, 0, 28, id, 0.5}), EventType::note_on, 0, 0.5);
    }
    expect_counts(tracker, 256, 0, 256);
    expect_none(tracker.apply({true, 0, 29, 256, 0.5}), NoteStatus::capacity_exceeded);
    expect_none(tracker.apply({true, 0, 28, -1, 0.5}), NoteStatus::capacity_exceeded);
    expect_none(tracker.apply({true, 0, 29, 0, 0.5}), NoteStatus::invalid_input);
    expect_change(tracker.apply({true, 0, 28, 0, 0.75}), EventType::note_on, 0, 0.75);
    expect_none(tracker.apply({false, 0, 29, 256, 0.0}));
    expect_none(tracker.apply({true, 0, 127, -1, 0.5}));
    expect_counts(tracker, 256, 0, 256);
    require(tracker.held_count(1) == 0, "rejected capacity request changed another key");
    expect_none(tracker.apply({false, 0, 28, 127, 0.0}));
    expect_change(tracker.apply({true, 0, 29, 256, 0.5}), EventType::note_on, 1, 0.5);
    expect_counts(tracker, 256, 0, 255);
    for (std::int32_t id = 0; id < 256; ++id) {
        const auto result = tracker.apply({false, 0, 28, id, 0.0});
        if (id == 255) { expect_change(result, EventType::note_off, 0); }
        else { expect_none(result); }
    }
    expect_change(tracker.apply({false, 0, 29, 256, 0.0}), EventType::note_off, 1);
    expect_counts(tracker, 0, 0, 0);

    for (std::size_t index = 0; index < NoteTracker::capacity; ++index) {
        expect_change(tracker.apply({true, 15, 100, -1, 0.5}), EventType::note_on, 72, 0.5);
    }
    expect_none(tracker.apply({true, 15, 100, -1, 0.5}), NoteStatus::capacity_exceeded);
    expect_counts(tracker, 256, 72, 256);
    for (std::size_t index = 0; index < NoteTracker::capacity; ++index) {
        const auto result = tracker.apply({true, 15, 100, -1, 0.0});
        if (index + 1 == NoteTracker::capacity) { expect_change(result, EventType::note_off, 72); }
        else { expect_none(result); }
    }
    expect_counts(tracker, 0, 72, 0);
}

void channel_release_and_reset() {
    NoteTracker tracker;
    for (int pitch = 28; pitch <= 100; ++pitch) {
        expect_change(tracker.apply({true, 2, pitch, pitch, 0.5}), EventType::note_on,
                      static_cast<std::size_t>(pitch - 28), 0.5);
    }
    expect_change(tracker.apply({true, 2, 28, -1, 0.5}), EventType::note_on, 0, 0.5);
    expect_change(tracker.apply({true, 3, 100, 100, 0.5}), EventType::note_on, 72, 0.5);
    for (const int channel : {-1, 16}) {
        const auto result = tracker.release_channel(channel);
        require(result.status == NoteStatus::invalid_input && !result.succeeded() &&
                result.count == 0, "invalid channel release accepted");
        expect_counts(tracker, 75, 0, 2);
    }
    auto released = tracker.release_channel(2);
    require(released.succeeded() && released.count == 72, "channel release lost unique keys");
    for (std::size_t key = 0; key < released.count; ++key) {
        require(released.keys[key] == key, "channel releases were duplicated or not ascending");
        require(tracker.held_count(key) == 0, "channel release retained its holder");
    }
    expect_counts(tracker, 1, 72, 1);
    released = tracker.release_channel(2);
    require(released.succeeded() && released.count == 0, "repeated channel release changed state");
    released = tracker.release_channel(3);
    require(released.succeeded() && released.count == 1 && released.keys[0] == 72,
            "last channel did not release shared key");
    expect_counts(tracker, 0, 72, 0);

    for (int pitch = 100; pitch >= 28; --pitch) {
        expect_change(tracker.apply({true, 15, pitch, -1, 0.5}), EventType::note_on,
                      static_cast<std::size_t>(pitch - 28), 0.5);
    }
    const auto copy = tracker;
    released = tracker.release_channel(15);
    require(released.succeeded() && released.count == 73, "full 73-key release was truncated");
    for (std::size_t key = 0; key < released.count; ++key) {
        require(released.keys[key] == key, "full keyboard release was not ordered");
    }
    expect_counts(copy, 73, 0, 1);
    tracker = copy;
    tracker.reset();
    tracker.reset();
    expect_counts(tracker, 0, 72, 0);
    expect_none(tracker.apply({false, 15, 100, -1, 0.0}));
    expect_change(tracker.apply({true, 15, 100, 7, 0.5}), EventType::note_on, 72, 0.5);
    expect_change(tracker.apply({false, 15, 100, 7, 0.0}), EventType::note_off, 72);
}

using Test = void (*)();
constexpr std::array<std::pair<std::string_view, Test>, 6> tests{{
    {"range_and_validation", range_and_validation},
    {"identified_overlap", identified_overlap},
    {"anonymous_and_zero_velocity", anonymous_and_zero_velocity},
    {"conflict_atomicity", conflict_atomicity},
    {"capacity_and_reuse", capacity_and_reuse},
    {"channel_release_and_reset", channel_release_and_reset}
}};
} // namespace

int main(int argc, char** argv) {
    const std::string_view selected = argc > 1 ? std::string_view{argv[1]} : "all";
    try {
        require(argc <= 2, "usage: note_tracker_test [fixture|all]");
        bool found = false;
        for (const auto& [name, test] : tests) {
            if (selected == "all" || selected == name) {
                test();
                std::cout << "PASS " << name << '\n';
                found = true;
            }
        }
        require(found, "unknown note-tracker fixture");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << selected << ": " << error.what() << '\n';
        return 1;
    }
}
