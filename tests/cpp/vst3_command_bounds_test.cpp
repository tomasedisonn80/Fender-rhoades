#include "tests/cpp/vst3_command_test_support.hpp"

#include <bit>
#include <iostream>
#include <limits>
#include <utility>

namespace {
using namespace rhodes;
using namespace rhodes::vst3;
using namespace rhodes::vst3::test_host;
using ScalarEngine = std::remove_cvref_t<
    decltype(std::declval<const HostEventProcessor&>().engine())>;

static_assert(maximum_block_events == 256 && key_count == 73);
static_assert(kMaximumChannelCommands == 256 && kControllerParameterCount == 18);
constexpr std::size_t frames = 128;

float intensity(std::size_t index) {
    return 0.125F + 0.0625F * static_cast<float>(index % 4);
}

sdk::int16 pitch(std::size_t key) {
    return static_cast<sdk::int16>(28 + key);
}

// The oracle calls only the scalar engine, with fixture-derived holder counts.
// It does not use NoteTracker, the adapter's command translation, or dispatch.
struct Oracle {
    std::unique_ptr<ScalarEngine> engine = std::make_unique<ScalarEngine>();
    std::array<std::size_t, key_count> holders{};
    InstrumentState state{};
    std::uint64_t cursor = 0;
    std::size_t strikes = 0;
    std::size_t releases = 0;

    explicit Oracle(double rate) { require(engine->prepare(rate), "oracle preparation failed"); }

    void strike(std::size_t key, float velocity) {
        require(engine->note_on(key, static_cast<double>(velocity)), "oracle strike failed");
        ++holders[key];
        ++strikes;
    }

    void release_all() {
        for (std::size_t key = 0; key < key_count; ++key) {
            if (holders[key] != 0) {
                require(engine->note_off(key), "oracle release failed");
                holders[key] = 0;
                ++releases;
            }
        }
    }

    void sustain(bool down) {
        engine->set_sustain(down);
        state.sustain = down;
    }

    template<class Sample, class Actions>
    void compare(ProcessorHost<Sample, frames>& host, Actions actions) {
        require(host.processor.process(host.data) == sdk::kResultOk, "valid command bound rejected");
        bool nonzero = false;
        using Bits = std::conditional_t<std::is_same_v<Sample, float>, std::uint32_t, std::uint64_t>;
        for (std::size_t frame = 0; frame < frames; ++frame) {
            actions(frame);
            double volts = 0.0;
            require(engine->process(std::span<double>(&volts, 1)), "oracle rendering failed");
            const auto expected = static_cast<Sample>(
                (32.0 * static_cast<double>(state.monitoring_gain)) * volts);
            require(std::bit_cast<Bits>(host.left[frame]) == std::bit_cast<Bits>(expected) &&
                    std::bit_cast<Bits>(host.right[frame]) == std::bit_cast<Bits>(expected),
                    "command-bound audio differs from scalar oracle");
            nonzero = nonzero || expected != Sample{0};
        }
        cursor += frames;
        require(host.output.silenceFlags == (nonzero ? 0U : 3U), "command-bound silence flags");
        require(host.processor.engine_processor().frame_cursor() == cursor, "command-bound cursor");
        require(host.processor.current_state() == state && host.processor.published_state() == state,
                "command-bound live or published state differs");
        require(!host.processor.requires_reset(), "valid command bound faulted the processor");
        std::size_t count = 0;
        for (std::size_t key = 0; key < key_count; ++key) {
            count += holders[key];
            require(host.processor.note_tracker().held_count(key) == holders[key],
                    "command-bound per-key ownership differs");
        }
        require(host.processor.note_tracker().active_count() == count,
                "command-bound holder total differs");
        same_mechanics(host.processor.engine_processor().engine(), *engine);
    }

    template<class Sample>
    void tail(ProcessorHost<Sample, frames>& host) {
        host.data.inputEvents = nullptr;
        host.data.inputParameterChanges = nullptr;
        compare(host, [](std::size_t) {});
    }
};

template<class Sample>
void prehold(ProcessorHost<Sample, frames>& host, Oracle& oracle, bool all_channels = false) {
    EventList notes;
    notes.count = static_cast<sdk::int32>(key_count);
    for (std::size_t key = 0; key < key_count; ++key) {
        notes.values[key] = note_on(0, static_cast<sdk::int32>(key), pitch(key), intensity(key),
            static_cast<sdk::int16>(all_channels ? key % 16 : 0));
    }
    host.data.inputEvents = &notes;
    oracle.compare(host, [&](std::size_t frame) {
        if (frame == 0) {
            for (std::size_t key = 0; key < key_count; ++key) { oracle.strike(key, intensity(key)); }
        }
    });
    host.data.inputEvents = nullptr;
    require(host.processor.note_tracker().active_count() == 73, "preheld-key fixture incomplete");
}

template<class Sample>
void reject_atomically(ProcessorHost<Sample, frames>& host) {
    const ProcessorSnapshot before(host.processor);
    host.fill(Sample{0.25});
    require(host.processor.process(host.data) == sdk::kResultFalse, "over-budget commands accepted");
    host.expect_silence();
    before.expect_unchanged(host.processor);
    // A full continuation exceeds the 96-frame filter history and checks that
    // failed preflight did not damage delayed samples or release damping.
    const auto actual = std::make_unique<ScalarEngine>(host.processor.engine_processor().engine());
    const auto expected = std::make_unique<ScalarEngine>(before.engine.engine());
    std::array<double, frames> actual_tail{}, expected_tail{};
    require(actual->process(actual_tail) && expected->process(expected_tail) &&
            actual_tail == expected_tail, "command rejection changed future audio");
}

template<class Sample>
void full_expansion(double rate) {
    ProcessorHost<Sample, frames> host(rate);
    Oracle oracle(rate);
    prehold(host, oracle);
    EventList notes;
    CommandQueue commands;
    CommandChanges changes;
    constexpr std::array<std::size_t, 4> sizes{73, 73, 73, 37};
    constexpr std::array<sdk::int32, 5> offsets{0, 32, 64, 96, 127};
    std::size_t index = 0;
    for (std::size_t group = 0; group < sizes.size(); ++group) {
        for (std::size_t key = 0; key < sizes[group]; ++key) {
            notes.values[index] = note_on(offsets[group], static_cast<sdk::int32>(1000 + index),
                                           pitch(key), intensity(index));
            ++index;
        }
    }
    require(index == 256, "expanded-action fixture must use all 256 note events");
    notes.count = static_cast<sdk::int32>(index);
    commands.count = static_cast<sdk::int32>(offsets.size());
    for (std::size_t point = 0; point < offsets.size(); ++point) {
        commands.values[point] = {offsets[point], 0.0};
    }
    changes.count = 1;
    changes.values[0] = &commands;
    host.data.inputEvents = &notes;
    host.data.inputParameterChanges = &changes;
    const auto starting_strikes = oracle.strikes;
    const auto starting_releases = oracle.releases;
    oracle.compare(host, [&](std::size_t frame) {
        std::size_t first = 0;
        for (std::size_t group = 0; group < sizes.size(); ++group) {
            if (frame == static_cast<std::size_t>(offsets[group])) {
                oracle.release_all();
                for (std::size_t key = 0; key < sizes[group]; ++key) {
                    oracle.strike(key, intensity(first + key));
                }
            }
            first += sizes[group];
        }
        if (frame == 127) { oracle.release_all(); }
    });
    require(oracle.strikes - starting_strikes == 256 &&
            oracle.releases - starting_releases == 329,
            "fixture must exercise all 585 physical note actions");
    require(commands.reads == 5 && notes.get_calls == 256, "expanded fixture host-read count");
    require(host.processor.note_tracker().active_count() == 0, "last expanded release was lost");
    oracle.tail(host);
}

template<class Sample>
void one_queue_limit(double rate) {
    ProcessorHost<Sample, frames> host(rate);
    Oracle oracle(rate);
    prehold(host, oracle);
    CommandQueue commands;
    for (auto& point : commands.values) { point = {31, 0.0}; }
    CommandChanges changes;
    changes.count = 1;
    changes.values[0] = &commands;
    host.data.inputParameterChanges = &changes;
    for (const sdk::int32 count : {257, std::numeric_limits<sdk::int32>::max()}) {
        commands.count = count;
        commands.reads = changes.reads = 0;
        reject_atomically(host);
        require(commands.reads == 0 && changes.reads == 1,
                "oversized command queue read points before rejecting its declared count");
    }
    commands.count = 256;
    commands.reads = changes.reads = 0;
    oracle.compare(host, [&](std::size_t frame) { if (frame == 31) { oracle.release_all(); } });
    require(commands.reads == 256 && changes.reads == 1,
            "256 repeated command points were not read exactly once");
    oracle.tail(host);
}

template<class Sample>
void sixteen_queue_limit(double rate) {
    ProcessorHost<Sample, frames> host(rate);
    Oracle oracle(rate);
    prehold(host, oracle, true);
    std::array<CommandQueue, 16> commands;
    CommandQueue gain, pedal;
    gain.id = kGainId;
    gain.count = 1;
    gain.values[0] = {0, 0.25};
    pedal.id = kSustainId;
    pedal.count = 1;
    pedal.values[0] = {0, 1.0};
    CommandChanges changes;
    changes.count = 18;
    changes.values[0] = &gain;
    changes.values[1] = &pedal;
    for (std::size_t channel = 0; channel < commands.size(); ++channel) {
        commands[channel].id = kAllNotesOffBaseId + static_cast<vst::ParamID>(15 - channel);
        commands[channel].count = 16;
        for (auto& point : commands[channel].values) { point = {31, 0.0}; }
        changes.values[2 + channel] = &commands[channel];
    }
    host.data.inputParameterChanges = &changes;
    commands.back().count = 17; // 15*16 + 17 = 257, despite duplicate offsets.
    reject_atomically(host);
    require(changes.reads == 18 && gain.reads == 1 && pedal.reads == 1,
            "distributed rejection skipped preceding valid queues");
    for (std::size_t queue = 0; queue < commands.size(); ++queue) {
        require(commands[queue].reads == (queue == 15 ? 0U : 16U),
                "aggregate command overflow read the offending queue's points");
    }

    // At the other aggregate boundary, exactly 256 points can already have
    // been inspected before the next nonempty queue must be rejected unread.
    for (auto& queue : commands) { queue.count = 0; queue.reads = 0; }
    changes.reads = 0;
    commands[0].count = 256;
    commands[1].count = 1;
    reject_atomically(host);
    require(changes.reads == 4 && commands[0].reads == 256 && commands[1].reads == 0,
            "aggregate raw limit was applied after deduplication or excess reads");

    for (auto& queue : commands) { queue.count = 16; queue.reads = 0; }
    changes.reads = gain.reads = pedal.reads = 0;
    oracle.compare(host, [&](std::size_t frame) {
        if (frame == 0) { oracle.state.monitoring_gain = 0.25F; oracle.sustain(true); }
        if (frame == 31) { oracle.release_all(); }
    });
    require(changes.reads == 18 && gain.reads == 1 && pedal.reads == 1,
            "full 18-queue input was not traversed once");
    for (const auto& queue : commands) { require(queue.reads == 16, "command queue was reread"); }
    // Release damping must survive even when sustain made the first release
    // inaudible. Lifting the pedal exposes any missed physical release.
    changes.count = 1;
    changes.values[0] = &pedal;
    pedal.values[0] = {0, 0.0};
    oracle.compare(host, [&](std::size_t frame) { if (frame == 0) { oracle.sustain(false); } });
    oracle.tail(host);
}

template<class Sample>
void per_sample_limit(double rate) {
    ProcessorHost<Sample, frames> host(rate);
    Oracle oracle(rate);
    prehold(host, oracle);
    EventList notes;
    notes.count = 183;
    for (std::size_t index = 0; index < 183; ++index) {
        notes.values[index] = note_on(63, static_cast<sdk::int32>(1000 + index),
                                       pitch(index % key_count), intensity(index));
    }
    CommandQueue command, gain, pedal;
    command.count = 1;
    command.values[0] = {63, 0.0};
    gain.id = kGainId;
    gain.count = 1;
    gain.values[0] = {0, 0.25};
    pedal.id = kSustainId;
    pedal.count = 1;
    pedal.values[0] = {63, 1.0};
    CommandChanges changes;
    changes.count = 3;
    changes.values[0] = &gain;
    changes.values[1] = &command;
    changes.values[2] = &pedal;
    host.data.inputEvents = &notes;
    host.data.inputParameterChanges = &changes;
    reject_atomically(host); // 73 releases + 183 strikes + sustain = 257.
    require(notes.get_calls == 183 && command.reads == 1 && pedal.reads == 1,
            "per-sample rejection did not complete syntax preflight");

    changes.count = 2; // 73 releases + 183 strikes = exactly 256.
    const auto starting_strikes = oracle.strikes;
    const auto starting_releases = oracle.releases;
    oracle.compare(host, [&](std::size_t frame) {
        if (frame == 0) { oracle.state.monitoring_gain = 0.25F; }
        if (frame == 63) {
            oracle.release_all();
            for (std::size_t index = 0; index < 183; ++index) {
                oracle.strike(index % key_count, intensity(index));
            }
        }
    });
    require(oracle.strikes - starting_strikes == 183 &&
            oracle.releases - starting_releases == 73,
            "accepted dispatch did not exercise exactly 256 physical actions");
    require(host.processor.note_tracker().active_count() == 183,
            "equal-offset strikes failed to survive the channel release");

    // Sustain also fits when the physical count is reduced by one. Identified
    // holders from the preceding block are cleared before IDs are reused.
    notes.count = 182;
    changes.count = 3;
    oracle.compare(host, [&](std::size_t frame) {
        if (frame == 63) {
            oracle.sustain(true);
            oracle.release_all();
            for (std::size_t index = 0; index < 182; ++index) {
                oracle.strike(index % key_count, intensity(index));
            }
        }
    });
    require(host.processor.note_tracker().active_count() == 182,
            "256-action dispatch including sustain changed ownership");
    oracle.tail(host);
}

template<class Sample>
void run() {
    constexpr double rate = 48000.0;
    full_expansion<Sample>(rate);
    one_queue_limit<Sample>(rate);
    sixteen_queue_limit<Sample>(rate);
    per_sample_limit<Sample>(rate);
}
} // namespace

int main() {
    try {
        run<float>();
        run<double>();
        std::cout << kPluginName << ": 585-action expansion, 256 raw commands across 1/16 queues, "
                     "bounded 257-point rejection and 256/257 per-sample dispatch passed "
                     "with scalar audio/ownership and atomic snapshots (both formats).\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
