#include "rhodes/event_processor.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// This is a dispatcher model, not a physical instrument. Integer/dyadic
// arithmetic makes differences in event timing and control ordering exact.
namespace {
using rhodes::EngineEvent;
using rhodes::EventType;

void require(bool condition, std::string_view message) {
    if (!condition) { throw std::runtime_error(std::string{message}); }
}

struct Call {
    EventType type{};
    std::size_t key = 0;
    std::uint64_t value_bits = 0;
    friend bool operator==(const Call&, const Call&) = default;
};

struct DispatchEngine {
    std::array<std::int64_t, rhodes::key_count> levels{};
    std::array<bool, rhodes::key_count> held{};
    std::array<Call, 2048> calls{};
    std::size_t call_count = 0;
    std::uint64_t phase = 0;
    std::uint64_t rendered = 0;
    double rate = 48000.0;
    bool pedal = false;

    void record(EventType type, std::size_t key, double value) noexcept {
        if (call_count < calls.size()) {
            calls[call_count] = {type, key, std::bit_cast<std::uint64_t>(value)};
        }
        ++call_count;
    }
    bool prepare(double value) noexcept {
        if (!std::isfinite(value) || value < 32000.0 || value > 192000.0) { return false; }
        rate = value;
        reset();
        return true;
    }
    void reset() noexcept {
        record(EventType::reset, 0, 0.0);
        levels.fill(0);
        held.fill(false);
        pedal = false;
        phase = 0;
    }
    bool note_on(std::size_t key, double value) noexcept {
        if (key >= levels.size() || !std::isfinite(value) || value < 0.0 || value > 1.0) {
            return false;
        }
        record(EventType::note_on, key, value);
        held[key] = value != 0.0;
        levels[key] += static_cast<std::int64_t>(8.0 * value);
        return true;
    }
    bool note_off(std::size_t key) noexcept {
        if (key >= levels.size()) { return false; }
        record(EventType::note_off, key, 0.0);
        held[key] = false;
        return true;
    }
    void set_sustain(bool down) noexcept {
        record(EventType::sustain, 0, down ? 1.0 : 0.0);
        pedal = down;
    }
    void all_notes_off() noexcept {
        record(EventType::all_notes_off, 0, 0.0);
        held.fill(false);
    }
    bool process(std::span<double> output) noexcept {
        for (double& sample : output) {
            std::int64_t total = 0;
            for (std::size_t key = 0; key < levels.size(); ++key) {
                total += static_cast<std::int64_t>(key + 1) * levels[key];
            }
            sample = static_cast<double>(total) / 8.0 + static_cast<double>(phase % 17);
            for (std::size_t key = 0; key < levels.size(); ++key) {
                if (levels[key] > 0) { levels[key] += held[key] || pedal ? 1 : -1; }
            }
            ++phase;
            ++rendered;
        }
        return true;
    }
    friend bool operator==(const DispatchEngine&, const DispatchEngine&) = default;
};
using Processor = rhodes::BasicEventProcessor<DispatchEngine>;

// Independent scalar state machine: it neither calls the test engine nor uses
// BasicEventProcessor. A dynamic action ledger is compared with the fixed log.
struct Oracle {
    struct Key { std::int64_t level = 0; bool held = false; };
    std::vector<Key> keys = std::vector<Key>(rhodes::key_count);
    std::vector<Call> actions;
    std::uint64_t tick = 0;
    std::uint64_t samples = 0;
    double rate = 48000.0;
    bool sustain = false;

    void action(const EngineEvent& event) {
        actions.push_back({event.type, event.key, std::bit_cast<std::uint64_t>(event.value)});
        switch (event.type) {
        case EventType::note_on:
            keys.at(event.key).held = event.value > 0;
            keys.at(event.key).level += static_cast<std::int64_t>(event.value / 0.125);
            break;
        case EventType::note_off: keys.at(event.key).held = false; break;
        case EventType::sustain: sustain = event.value == 1.0; break;
        case EventType::all_notes_off:
            for (auto& key : keys) { key.held = false; }
            break;
        case EventType::reset:
            for (auto& key : keys) { key = {}; }
            sustain = false;
            tick = 0;
            break;
        }
    }
    double sample() {
        std::int64_t units = 8 * static_cast<std::int64_t>(tick % 17);
        std::int64_t weight = 0;
        for (const auto& key : keys) { units += ++weight * key.level; }
        for (auto& key : keys) {
            if (key.level != 0) {
                if (sustain || key.held) { ++key.level; }
                else { --key.level; }
            }
        }
        ++tick;
        ++samples;
        return 0.125 * static_cast<double>(units);
    }
};

void same(const DispatchEngine& actual, const Oracle& expected) {
    require(actual.call_count == expected.actions.size() && actual.call_count <= actual.calls.size(),
            "event call count or fixed test log capacity differs");
    require(std::equal(expected.actions.begin(), expected.actions.end(), actual.calls.begin()),
            "event order, arguments, duplication or loss differs");
    require(actual.phase == expected.tick && actual.rendered == expected.samples &&
            actual.rate == expected.rate && actual.pedal == expected.sustain,
            "sample clock or global control state differs");
    for (std::size_t key = 0; key < actual.levels.size(); ++key) {
        require(actual.levels[key] == expected.keys[key].level &&
                actual.held[key] == expected.keys[key].held, "per-key continuation state differs");
    }
}

void success(const rhodes::ProcessResult& result, std::size_t count) {
    require(result.succeeded() && result.frames_processed == count &&
            result.failing_sample == rhodes::no_failure_index &&
            result.failing_event == rhodes::no_failure_index && !result.requires_reset &&
            result.subsystem == rhodes::ProcessSubsystem::none, "successful result metadata differs");
}

std::vector<double> scalar(Oracle& oracle, std::size_t frames,
                           std::span<const EngineEvent> events) {
    std::vector<double> output;
    output.reserve(frames);
    std::size_t next = 0;
    for (std::size_t time = 0; time < frames; ++time) {
        while (next < events.size() && events[next].offset == time) { oracle.action(events[next++]); }
        output.push_back(oracle.sample());
    }
    require(next == events.size(), "invalid scalar fixture schedule");
    return output;
}

struct Random {
    std::uint32_t value;
    std::uint32_t next() noexcept {
        value = value * 1664525U + 1013904223U; // Defined unsigned wrap, no library distribution.
        return value;
    }
};

void control_order() {
    std::array<EngineEvent, 9> events{{
        {0, 3, EventType::note_on, 72, 1.0}, {4, 0, EventType::sustain, 0, 1.0},
        {4, 2, EventType::all_notes_off, 0, 0.0}, {7, 10, EventType::reset, 0, 0.0},
        {7, 11, EventType::note_on, 4, 0.75}, {7, 20, EventType::sustain, 0, 1.0},
        {7, 21, EventType::all_notes_off, 0, 0.0}, {8, 0, EventType::sustain, 0, 0.0},
        {15, 0, EventType::note_on, 4, 0.0}
    }};
    Oracle oracle;
    const auto expected = scalar(oracle, 32, events);
    Processor processor;
    std::array<double, 32> output{};
    success(processor.process(output, events), output.size());
    require(std::equal(output.begin(), output.end(), expected.begin()), "ordered controls differ");
    same(processor.engine(), oracle);
    require(processor.frame_cursor() == 32, "queued reset incorrectly reset the external cursor");
    std::swap(events[3].type, events[4].type);
    std::swap(events[3].key, events[4].key);
    std::swap(events[3].value, events[4].value);
    Oracle reversed;
    const auto reverse_output = scalar(reversed, 32, events);
    Processor alternate;
    success(alternate.process(output, events), output.size());
    require(std::equal(output.begin(), output.end(), reverse_output.begin()), "reversed tie differs");
    require(reverse_output != expected, "order fixture failed to distinguish reset/strike order");
    same(alternate.engine(), reversed);
}

void random_partitions() {
    constexpr std::size_t frames = 8195;
    for (std::uint32_t seed = 1; seed <= 8; ++seed) {
        Random rng{0xa0470000U + seed};
        std::vector<EngineEvent> events;
        for (std::size_t i = 0; i < 220; ++i) {
            const auto type = static_cast<EventType>(rng.next() % 5);
            const bool note = type == EventType::note_on || type == EventType::note_off;
            const auto key = note ? static_cast<std::size_t>(rng.next() % rhodes::key_count) : 0;
            const double value = type == EventType::note_on ? 0.125 * (rng.next() % 9) :
                type == EventType::sustain ? static_cast<double>(rng.next() % 2) : 0.0;
            events.push_back({static_cast<std::uint32_t>(rng.next() % frames), 0, type, key, value});
        }
        for (const std::uint32_t at : {0U, 1U, 4095U, 4096U, 8194U}) {
            events.push_back({at, 0, EventType::reset, 0, 0.0});
            events.push_back({at, 0, EventType::note_on, 72, 0.875});
            events.push_back({at, 0, EventType::sustain, 0, 1.0});
        }
        std::stable_sort(events.begin(), events.end(), [](const auto& a, const auto& b) {
            return a.offset < b.offset;
        });
        for (std::size_t i = 0; i < events.size(); ++i) {
            events[i].sequence = i != 0 && events[i].offset == events[i - 1].offset ?
                events[i - 1].sequence + 1 + rng.next() % 7 : rng.next() % 32;
        }
        Oracle oracle;
        const auto expected = scalar(oracle, frames, events);
        for (unsigned strategy = 0; strategy < 5; ++strategy) {
            Processor processor;
            std::vector<double> output(frames, -99.0);
            std::size_t first = 0, next = 0, block = 0;
            success(processor.process({}), 0);
            while (first < frames) {
                const std::size_t proposed = strategy == 0 ? 1 : strategy == 1 ? 4096 :
                    strategy == 2 ? 17 : strategy == 3 ? (block % 2 == 0 ? 1 : 4096) :
                    1 + rng.next() % rhodes::maximum_block_frames;
                const auto count = std::min(proposed, frames - first);
                std::array<EngineEvent, rhodes::maximum_block_events> local{};
                std::size_t used = 0;
                while (next < events.size() && events[next].offset < first + count) {
                    auto event = events[next++];
                    require(event.offset >= first && used < local.size(), "invalid partition fixture");
                    event.offset -= static_cast<std::uint32_t>(first);
                    local[used++] = event;
                }
                success(processor.process(std::span<double>{output}.subspan(first, count),
                                          std::span<const EngineEvent>{local}.first(used)), count);
                first += count;
                ++block;
                require(processor.frame_cursor() == first && !processor.requires_reset(),
                        "partition cursor or fault state differs");
            }
            success(processor.process({}), 0);
            require(output == expected && next == events.size(), "random partition trajectory differs");
            same(processor.engine(), oracle);
        }
    }
}

Processor active() {
    Processor processor;
    const std::array<EngineEvent, 3> events{{
        {0, 0, EventType::note_on, 3, 0.75}, {1, 0, EventType::sustain, 0, 1.0},
        {2, 0, EventType::note_off, 3, 0.0}
    }};
    std::array<double, 23> output{};
    success(processor.process(output, events), output.size());
    return processor;
}

void rejected(Processor& processor, std::span<double> output,
              std::span<const EngineEvent> events, std::size_t failing_event) {
    const Processor before = processor;
    const auto result = processor.process(output, events);
    require(result.status == rhodes::ProcessStatus::invalid_input && !result.succeeded() &&
            result.frames_processed == 0 && result.failing_event == failing_event &&
            result.failing_sample == rhodes::no_failure_index && !result.requires_reset &&
            result.subsystem == rhodes::ProcessSubsystem::events, "rejection metadata differs");
    require(std::all_of(output.begin(), output.end(), [](double x) { return x == 0.0; }),
            "rejected block retained output");
    require(processor.frame_cursor() == before.frame_cursor() &&
            processor.engine() == before.engine() && !processor.requires_reset(),
            "rejected block changed control, event log, cursor or engine state");
    auto a = processor.engine(), b = before.engine();
    std::array<double, 31> left{}, right{};
    require(a.process(left) && b.process(right) && left == right && a == b,
            "rejected block changed continuation");
}

void transaction_rejection() {
    std::array<EngineEvent, 256> events{};
    for (std::size_t i = 0; i < events.size(); ++i) {
        events[i] = {0, static_cast<std::uint32_t>(i), EventType::note_on, i % rhodes::key_count, 0.5};
    }
    events[1] = {0, 1, EventType::reset, 0, 0.0};
    const std::array<EngineEvent, 10> invalid_last{{
        {0, 255, EventType::note_on, 73, 0.5},
        {0, 255, EventType::note_on, 2, std::numeric_limits<double>::quiet_NaN()},
        {0, 255, EventType::note_on, 2, std::numeric_limits<double>::infinity()},
        {0, 255, EventType::note_on, 2, -0.125}, {0, 255, EventType::note_off, 2, 0.125},
        {0, 255, EventType::sustain, 1, 1.0}, {0, 255, EventType::reset, 0, 1.0},
        {0, 255, static_cast<EventType>(255), 0, 0.0},
        {0, 254, EventType::note_off, 0, 0.0}, {16, 0, EventType::note_off, 0, 0.0}
    }};
    for (const auto& last : invalid_last) {
        auto processor = active();
        events.back() = last;
        std::array<double, 16> output{};
        output.fill(123.0);
        rejected(processor, output, events, 255);
    }
    auto processor = active();
    events[254].offset = 2;
    events[255] = {1, 0, EventType::note_off, 0, 0.0};
    std::array<double, 16> output{};
    output.fill(123.0);
    rejected(processor, output, events, 255);
}

void capacity_and_cursor() {
    std::array<EngineEvent, 257> events{};
    for (std::size_t i = 0; i < events.size(); ++i) {
        events[i] = {0, static_cast<std::uint32_t>(i), EventType::note_on,
                     i % rhodes::key_count, 0.125 * static_cast<double>(1 + i % 8)};
    }
    Processor processor;
    Oracle oracle;
    const auto accepted = std::span<const EngineEvent>{events}.first(256);
    const auto expected = scalar(oracle, 4096, accepted);
    std::array<double, 4097> output{};
    output.back() = 91.0;
    success(processor.process(std::span<double>{output}.first(4096), accepted), 4096);
    require(std::equal(expected.begin(), expected.end(), output.begin()) && output.back() == 91.0,
            "maximum accepted block changed samples or guard");
    same(processor.engine(), oracle);
    output.fill(91.0);
    rejected(processor, std::span<double>{output}.first(4096), events, rhodes::no_failure_index);
    require(output.back() == 91.0, "event rejection wrote outside output span");
    output.fill(91.0);
    rejected(processor, output, {}, rhodes::no_failure_index);
    const EngineEvent reset{0, 0, EventType::reset, 0, 0.0};
    rejected(processor, {}, std::span<const EngineEvent>{&reset, 1}, 0);

    processor.reset();
    oracle.action(reset);
    require(processor.frame_cursor() == 0, "external reset failed to reset cursor");
    same(processor.engine(), oracle);
    require(processor.set_sustain(true), "validated sustain port failed");
    oracle.action({0, 0, EventType::sustain, 0, 1.0});
    require(processor.frame_cursor() == 0, "sustain port advanced cursor");
    same(processor.engine(), oracle);
    require(processor.prepare(96000.0), "valid preparation failed");
    oracle.rate = 96000.0;
    oracle.action(reset);
    same(processor.engine(), oracle);
    const auto before = processor;
    require(!processor.prepare(std::numeric_limits<double>::quiet_NaN()), "invalid preparation accepted");
    require(processor.engine() == before.engine() && processor.frame_cursor() == 0,
            "invalid preparation changed processor");
}

using Test = void (*)();
constexpr std::array<std::pair<std::string_view, Test>, 4> tests{{
    {"control_order", control_order}, {"random_partitions", random_partitions},
    {"transaction_rejection", transaction_rejection}, {"capacity_and_cursor", capacity_and_cursor}
}};
} // namespace

int main(int argc, char** argv) {
    const std::string_view selected = argc > 1 ? std::string_view{argv[1]} : "all";
    bool found = false;
    try {
        for (const auto& [name, test] : tests) {
            if (selected != "all" && selected != name) { continue; }
            found = true;
            test();
            std::cout << "PASS " << name << '\n';
        }
        require(found, "unknown event-model selector");
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << selected << ": " << error.what() << '\n';
        return 1;
    }
    return 0;
}
