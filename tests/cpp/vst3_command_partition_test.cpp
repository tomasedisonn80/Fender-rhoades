#include "tests/cpp/vst3_command_test_support.hpp"

#include <bit>
#include <cmath>
#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <utility>

namespace {
using namespace rhodes;
using namespace rhodes::vst3;
using namespace rhodes::vst3::test_host;
using ScalarEngine = std::remove_cvref_t<
    decltype(std::declval<const HostEventProcessor&>().engine())>;

#if defined(RHODES_QUADRATURE_VST3)
static_assert(kStateModel == 3 && std::is_same_v<ScalarEngine, QuadratureEngine>);
#elif defined(RHODES_FILTERED_VST3)
static_assert(kStateModel == 2 && std::is_same_v<ScalarEngine, AudioEngine>);
#else
static_assert(kStateModel == 1 && std::is_same_v<ScalarEngine, ResearchEngine>);
#endif
static_assert(maximum_block_events == 256 && kMaximumChannelCommands == 256 &&
              kControllerParameterCount == 18 && key_count == 73);
constexpr std::size_t kTraceFrames = 769;
constexpr std::size_t kHistoryProbeFrames = 129;

enum class MessageKind { on, off, pressure };
struct Message {
    MessageKind kind;
    sdk::int32 frame;
    sdk::int32 id;
    sdk::int16 pitch;
    sdk::int16 channel;
    float value;
};
constexpr Message strike(sdk::int32 at, sdk::int32 id, sdk::int16 pitch,
                         sdk::int16 channel, float value) {
    return {MessageKind::on, at, id, pitch, channel, value};
}
constexpr Message release(sdk::int32 at, sdk::int32 id, sdk::int16 pitch,
                          sdk::int16 channel) {
    return {MessageKind::off, at, id, pitch, channel, 0.0F};
}
constexpr Message pressure(sdk::int32 at, sdk::int32 id, sdk::int16 pitch,
                           sdk::int16 channel, float value) {
    return {MessageKind::pressure, at, id, pitch, channel, value};
}
constexpr auto kSmallestId = std::numeric_limits<sdk::int32>::min();
constexpr auto kLargestId = std::numeric_limits<sdk::int32>::max();

// Absolute positions, never regenerated from a callback's size. Array order is
// the producer's order for equal-offset notes and pressure. Gain stays at its
// initial 0.5 throughout: partition-dependent gain interpolation is absent.
constexpr std::array kMessages{
    pressure(0, -1, 0, 15, 0.0F),
    strike(0, 7, 60, 3, 0.5F), strike(0, 7, 60, 9, 0.375F),
    strike(0, -1, 72, 3, 0.5F), strike(0, -1, 72, 3, 0.25F),
    strike(0, kSmallestId, 100, 15, 0.625F), strike(0, kLargestId, 28, 0, 0.5F),
    pressure(1, kSmallestId, 127, 0, 1.0F),
    strike(17, 7, 60, 3, 0.25F), pressure(17, 7, 60, 9, 0.75F),
    release(31, 123, 60, 3), pressure(62, -7, 0, 1, 0.25F),
    pressure(63, 7, 60, 3, 0.5F), strike(63, 7, 67, 3, 0.75F),
    strike(63, -1, 72, 3, 0.375F), release(63, -1, 72, 3),
    strike(63, -1, 72, 3, 0.125F),
    release(64, 7, 60, 9), pressure(64, 7, 60, 9, 1.0F),
    strike(64, 7, 60, 9, 0.5F), strike(64, 7, 60, 3, 0.25F),
    strike(64, -2, 84, 2, 0.5F), strike(65, kSmallestId, 100, 15, 0.125F),
    pressure(65, kSmallestId, 100, 15, 0.0F), release(95, 7, 60, 3),
    pressure(96, 7, 60, 9, 0.375F), strike(96, 7, 65, 9, 0.5F),
    release(126, -2, 84, 2),
    strike(127, kSmallestId, 100, 15, 0.25F),
    strike(127, kLargestId, 28, 0, 0.25F), pressure(127, -1, 127, 15, 0.75F),
    strike(128, 100, 68, 4, 0.75F), strike(128, 101, 68, 5, 0.25F),
    strike(128, -1, 69, 4, 0.25F), strike(128, -1, 69, 4, 0.5F),
    pressure(129, 100, 68, 4, 0.5F), strike(129, 100, 70, 4, 0.5F),
    strike(129, -1, 69, 4, 0.375F), pressure(190, 101, 68, 5, 0.25F),
    strike(191, 100, 70, 4, 0.25F), pressure(191, 100, 70, 4, 1.0F),
    strike(191, 101, 68, 5, 0.375F),
    release(192, 100, 70, 4), strike(192, 101, 68, 5, 0.5F),
    strike(192, 88, 60, 8, 0.5F), strike(192, 88, 60, 0, 0.25F),
    strike(192, -1, 72, 8, 0.375F), strike(193, -1, 72, 8, 0.25F),
    pressure(193, -1, 72, 8, 0.5F), strike(223, 88, 65, 8, 0.375F),
    pressure(255, 88, 65, 8, 0.25F),
    // A complete fixed-64 callback contains only pressure. Its increasing
    // offsets must advance input ordering and time without acquiring holders.
    pressure(256, kSmallestId, 0, 0, 0.0F), pressure(271, -1, 60, 9, 0.25F),
    pressure(287, kLargestId, 127, 15, 1.0F), pressure(319, -2, 28, 2, 0.5F),
    strike(320, 202, 60, 2, 0.5F), strike(320, 303, 60, 12, 0.375F),
    strike(320, -1, 96, 2, 0.25F), strike(320, -1, 96, 2, 0.5F),
    pressure(321, 202, 60, 2, 0.5F),
    pressure(383, -1, 96, 2, 0.75F), strike(383, 202, 72, 2, 0.5F),
    strike(384, kLargestId, 100, 15, 0.375F), strike(384, -1, 28, 1, 0.25F),
    strike(384, 202, 60, 2, 0.625F), strike(384, 303, 60, 12, 0.5F),
    strike(385, 202, 60, 2, 0.125F), pressure(385, 202, 60, 2, 0.0F),
    strike(447, kLargestId, 100, 15, 0.25F),
    strike(448, -1, 28, 1, 0.25F), pressure(448, -1, 28, 1, 1.0F),
    pressure(511, 202, 60, 2, 0.5F), strike(511, 202, 60, 2, 0.25F),
    pressure(512, 303, 60, 12, 0.75F),
    // The final-sample strike is audible in the delayed variants' subsequent
    // history probe, and survives commands at this exact offset.
    pressure(768, kSmallestId, 127, 15, 1.0F), strike(768, 1414, 88, 14, 0.25F)};

struct ChannelPoint { sdk::int32 frame; sdk::int16 channel; double value; };
constexpr std::array kCommands{
    ChannelPoint{0, 15, 0.0}, ChannelPoint{0, 14, 0.0}, ChannelPoint{0, 13, 0.0},
    ChannelPoint{0, 12, 0.0}, ChannelPoint{0, 11, 0.0}, ChannelPoint{0, 10, 0.0},
    ChannelPoint{0, 9, 1.0}, ChannelPoint{0, 8, 0.0}, ChannelPoint{0, 7, 0.0},
    ChannelPoint{0, 6, 0.0}, ChannelPoint{0, 5, 0.0}, ChannelPoint{0, 4, 0.0},
    ChannelPoint{0, 3, 0.0}, ChannelPoint{0, 2, 0.0}, ChannelPoint{0, 1, 0.0},
    ChannelPoint{0, 0, 0.0},
    ChannelPoint{63, 3, 0.0}, ChannelPoint{63, 3, 0.0},
    ChannelPoint{64, 9, 0.0}, ChannelPoint{64, 3, 0.0},
    ChannelPoint{96, 9, 0.0}, ChannelPoint{96, 3, 0.0},
    ChannelPoint{127, 15, 0.0}, ChannelPoint{127, 15, 0.0}, ChannelPoint{127, 0, 0.0},
    ChannelPoint{128, 15, 0.5}, ChannelPoint{128, 0, 0.0},
    ChannelPoint{129, 4, 0.0},
    ChannelPoint{191, 5, 0.75}, ChannelPoint{191, 4, 0.0}, ChannelPoint{191, 4, 0.0},
    ChannelPoint{192, 5, 0.0}, ChannelPoint{192, 4, 0.0},
    ChannelPoint{223, 8, 0.0},
    ChannelPoint{255, 9, 0.0}, ChannelPoint{255, 8, 0.0},
    ChannelPoint{255, 5, 0.0}, ChannelPoint{255, 0, 0.0},
    ChannelPoint{320, 15, 0.0}, ChannelPoint{320, 2, 0.0},
    ChannelPoint{383, 2, 0.0}, ChannelPoint{383, 2, 0.0},
    ChannelPoint{384, 12, 0.0}, ChannelPoint{384, 2, 0.0},
    ChannelPoint{447, 15, 0.0},
    ChannelPoint{448, 15, 0.0}, ChannelPoint{448, 1, 0.0},
    ChannelPoint{449, 1, 0.0}, ChannelPoint{511, 2, 0.0},
    ChannelPoint{512, 12, 0.0}, ChannelPoint{512, 2, 0.0}, ChannelPoint{512, 0, 0.0},
    ChannelPoint{513, 2, 0.0}, ChannelPoint{575, 8, 0.0},
    ChannelPoint{768, 15, 0.0}, ChannelPoint{768, 14, 0.0}};

struct PedalPoint { sdk::int32 frame; double value; };
constexpr std::array kPedal{
    PedalPoint{0, 1.0}, PedalPoint{63, 1.0}, PedalPoint{63, 0.0},
    PedalPoint{64, 1.0}, PedalPoint{127, 0.0}, PedalPoint{128, 1.0},
    PedalPoint{191, 0.0}, PedalPoint{192, 1.0}, PedalPoint{255, 0.0},
    PedalPoint{320, 1.0}, PedalPoint{383, 0.0}, PedalPoint{384, 1.0},
    PedalPoint{447, 0.0}, PedalPoint{448, 1.0}, PedalPoint{511, 0.0},
    PedalPoint{512, 0.0}, PedalPoint{768, 0.0}};
static_assert(kTraceFrames <= kMaximumHostFrames && kMessages.size() <= 256 &&
              kCommands.size() <= 256 && kPedal.size() <= kMaximumHostFrames);

template<class Value>
bool same_bits(Value left, Value right) {
    using Bits = std::conditional_t<std::is_same_v<Value, float>, std::uint32_t, std::uint64_t>;
    return std::bit_cast<Bits>(left) == std::bit_cast<Bits>(right);
}

// Associative identified holders and anonymous multiplicities intentionally do
// not use NoteTracker, command translation, or the production event dispatcher.
// Only the chosen physical/audio engine is shared with the implementation.
struct Oracle {
    std::unique_ptr<ScalarEngine> engine = std::make_unique<ScalarEngine>();
    std::map<std::pair<sdk::int16, sdk::int32>, sdk::int16> identified;
    std::multiset<std::pair<sdk::int16, sdk::int16>> anonymous;
    InstrumentState state{};
    std::uint64_t cursor = 0;
    std::size_t message_index = 0;
    std::size_t command_index = 0;
    std::size_t pedal_index = 0;
    std::size_t actions = 0;

    explicit Oracle(double rate) { require(engine->prepare(rate), "partition oracle prepare"); }

    std::size_t size() const { return identified.size() + anonymous.size(); }
    std::size_t holders(sdk::int16 pitch) const {
        std::size_t result = 0;
        for (const auto& [identity, held_pitch] : identified) {
            (void)identity;
            if (held_pitch == pitch) { ++result; }
        }
        for (const auto& owner : anonymous) { if (owner.second == pitch) { ++result; } }
        return result;
    }

    void physical_release(sdk::int16 pitch) {
        require(engine->note_off(static_cast<std::size_t>(pitch - 28)), "oracle physical release");
        ++actions;
    }

    void release_channel(sdk::int16 channel) {
        std::set<sdk::int16> touched_pitches;
        for (auto owner = identified.begin(); owner != identified.end();) {
            if (owner->first.first == channel) {
                touched_pitches.insert(owner->second);
                owner = identified.erase(owner);
            } else { ++owner; }
        }
        for (auto owner = anonymous.begin(); owner != anonymous.end();) {
            if (owner->first == channel) {
                touched_pitches.insert(owner->second);
                owner = anonymous.erase(owner);
            } else { ++owner; }
        }
        for (const auto pitch : touched_pitches) {
            if (holders(pitch) == 0) { physical_release(pitch); }
        }
    }

    void note(const Message& message) {
        if (message.kind == MessageKind::pressure) { return; }
        const bool on = message.kind == MessageKind::on && message.value != 0.0F;
        bool removed = false;
        if (message.id == -1) {
            const auto owner = std::pair{message.channel, message.pitch};
            if (on) { anonymous.insert(owner); }
            else if (const auto found = anonymous.find(owner); found != anonymous.end()) {
                anonymous.erase(found);
                removed = true;
            }
        } else {
            const auto identity = std::pair{message.channel, message.id};
            const auto found = identified.find(identity);
            require(found == identified.end() || found->second == message.pitch,
                    "invalid identity reuse in absolute fixture");
            if (on) { identified[identity] = message.pitch; }
            else if (found != identified.end()) {
                identified.erase(found);
                removed = true;
            }
        }
        if (on) {
            require(engine->note_on(static_cast<std::size_t>(message.pitch - 28),
                                    static_cast<double>(message.value)), "oracle physical strike");
            ++actions;
        } else if (removed && holders(message.pitch) == 0) { physical_release(message.pitch); }
    }

    void landmarks(std::size_t position) const {
        // Hand-declared outcomes also audit the fixture/oracle interpretation.
        if (position == 0) {
            require(size() == 6 && holders(60) == 2 && holders(72) == 2 && state.sustain,
                    "initial shared-holder landmark");
        } else if (position == 63) {
            require(size() == 5 && holders(60) == 1 && holders(67) == 1 &&
                    holders(72) == 1 && !state.sustain, "release/restrike landmark");
        } else if (position == 64) {
            require(size() == 5 && holders(60) == 2 && holders(67) == 0 &&
                    holders(72) == 0 && state.sustain, "repeated-zero landmark");
        } else if (position == 129) {
            require(size() == 4 && holders(68) == 1 && holders(69) == 1 &&
                    holders(70) == 1 && state.sustain, "cross-channel survival landmark");
        } else if (position >= 255 && position < 320) {
            require(size() == 0 && !state.sustain, "pressure-only ownership landmark");
        } else if (position == 383) {
            require(size() == 2 && holders(60) == 1 && holders(72) == 1 &&
                    !state.sustain, "ragged shared-holder landmark");
        } else if (position >= 512 && position < 768) {
            require(size() == 0 && !state.sustain, "empty-tail ownership landmark");
        } else if (position == 768) {
            require(size() == 1 && holders(88) == 1 && !state.sustain,
                    "last-sample command/restrike landmark");
        }
    }

    double sample() {
        const auto position = static_cast<sdk::int32>(cursor);
        actions = 0;
        bool pedal_changed = false;
        while (pedal_index < kPedal.size() && kPedal[pedal_index].frame == position) {
            state.sustain = kPedal[pedal_index++].value >= 0.5;
            pedal_changed = true;
        }
        if (pedal_changed) { engine->set_sustain(state.sustain); ++actions; }

        // The independent oracle uses a channel set at each instant: duplicate
        // points at one offset are idempotent, while a later zero triggers anew.
        std::set<sdk::int16> released_channels;
        while (command_index < kCommands.size() && kCommands[command_index].frame == position) {
            released_channels.insert(kCommands[command_index++].channel);
        }
        for (const auto channel : released_channels) { release_channel(channel); }
        while (message_index < kMessages.size() && kMessages[message_index].frame == position) {
            note(kMessages[message_index++]);
        }
        require(size() <= 256 && actions <= maximum_block_events,
                "absolute fixture exceeds ownership/per-sample action budget");
        landmarks(static_cast<std::size_t>(cursor));
        double volts = 0.0;
        require(engine->process(std::span<double>(&volts, 1)), "partition oracle sample");
        ++cursor;
        return (32.0 * static_cast<double>(state.monitoring_gain)) * volts;
    }

    void inspect(const RhodesProcessor& processor) const {
        require(!processor.requires_reset(), "partition callback unexpectedly faulted");
        require(processor.engine_processor().frame_cursor() == cursor, "partition cursor differs");
        require(processor.current_state() == state && processor.published_state() == state,
                "partition live/published state differs");
        require(processor.note_tracker().active_count() == size(), "partition holder total differs");
        for (std::size_t index = 0; index < key_count; ++index) {
            require(processor.note_tracker().held_count(index) ==
                        holders(static_cast<sdk::int16>(index + 28)), "partition per-key holders differ");
        }
        same_mechanics(processor.engine_processor().engine(), *engine);

        // Counts alone miss replacement by the wrong channel/ID. Drain a copy
        // using independently known identities, checking each last-holder action.
        auto observed = processor.note_tracker();
        std::array<std::size_t, key_count> remaining{};
        for (std::size_t index = 0; index < remaining.size(); ++index) {
            remaining[index] = holders(static_cast<sdk::int16>(index + 28));
        }
        auto total = size();
        const auto drain = [&](sdk::int16 channel, sdk::int32 id, sdk::int16 pitch) {
            const auto index = static_cast<std::size_t>(pitch - 28);
            const auto result = observed.apply({false, channel, pitch, id, 0.0});
            require(result.succeeded() && remaining[index] != 0 && total != 0,
                    "partition identity drain failed");
            const bool last = --remaining[index] == 0;
            require(observed.active_count() == --total && result.change.has_value() == last,
                    "partition changed a channel/note identity");
            if (last) {
                require(result.change->type == EventType::note_off && result.change->key == index,
                        "partition identity emitted a wrong physical release");
            }
        };
        for (const auto& [identity, pitch] : identified) { drain(identity.first, identity.second, pitch); }
        for (const auto& [channel, pitch] : anonymous) { drain(channel, -1, pitch); }
        require(observed.active_count() == 0, "partition retained an unexpected identity");
    }
};

enum class Layout { whole, one_frame, fixed64, ragged };
constexpr std::array<std::size_t, 30> kRaggedEnds{
    1, 2, 17, 63, 64, 65, 95, 127, 128, 129, 191, 192, 193, 255, 256,
    257, 288, 320, 321, 383, 384, 385, 448, 511, 512, 513, 607, 640, 704, 769};

std::string_view layout_name(Layout layout) {
    switch (layout) {
    case Layout::whole: return "whole";
    case Layout::one_frame: return "one-frame";
    case Layout::fixed64: return "fixed64";
    case Layout::ragged: return "ragged";
    }
    return "invalid";
}

std::size_t block_end(Layout layout, std::size_t start, std::size_t callback) {
    switch (layout) {
    case Layout::whole: return kTraceFrames;
    case Layout::one_frame: return start + 1;
    case Layout::fixed64: return std::min(start + 64, kTraceFrames);
    case Layout::ragged:
        require(callback < kRaggedEnds.size(), "ragged partition exhausted early");
        return kRaggedEnds[callback];
    }
    return start;
}

struct Coverage {
    std::size_t calls = 0;
    std::size_t samples = 0;
    std::size_t empty_null = 0;
    std::size_t empty_objects = 0;
    std::size_t pressure_only = 0;
    bool pressure_advances = false;
    bool event_start = false;
    bool event_end = false;
    bool command_start = false;
    bool command_end = false;
    bool pedal_start = false;
    bool pedal_end = false;
};
std::size_t total_calls = 0;
std::size_t total_samples = 0;

template<class Sample>
struct HostFixture {
    ProcessorHost<Sample, kTraceFrames> host;
    EventList events;
    std::array<CommandQueue, 16> commands;
    ParameterQueue pedal;
    CommandChanges changes;
    Coverage coverage;

    explicit HostFixture(double rate) : host(rate) {
        pedal.id = kSustainId;
        for (std::size_t channel = 0; channel < commands.size(); ++channel) {
            commands[channel].id = kAllNotesOffBaseId + static_cast<vst::ParamID>(channel);
        }
    }

    void stage(std::size_t start, std::size_t end, std::size_t callback) {
        require(start < end && end <= kTraceFrames, "invalid partition extent");
        events.count = 0;
        events.get_calls = 0;
        pedal.count = 0;
        pedal.get_calls = 0;
        changes.count = 0;
        changes.reads = 0;
        for (auto& command : commands) { command.count = 0; command.reads = 0; }
        const auto inside = [start, end](sdk::int32 position) {
            return position >= static_cast<sdk::int32>(start) && position < static_cast<sdk::int32>(end);
        };
        bool only_pressure = true;
        for (const auto& message : kMessages) {
            if (!inside(message.frame)) { continue; }
            require(static_cast<std::size_t>(events.count) < events.values.size(), "host event bound");
            const auto offset = message.frame - static_cast<sdk::int32>(start);
            auto& event = events.values[static_cast<std::size_t>(events.count++)];
            if (message.kind == MessageKind::pressure) {
                event = {};
                event.type = vst::Event::kPolyPressureEvent;
                event.sampleOffset = offset;
                event.polyPressure.channel = message.channel;
                event.polyPressure.pitch = message.pitch;
                event.polyPressure.noteId = message.id;
                event.polyPressure.pressure = message.value;
            } else {
                only_pressure = false;
                event = message.kind == MessageKind::on ?
                    note_on(offset, message.id, message.pitch, message.value, message.channel) :
                    note_off(offset, message.id, message.pitch, message.channel);
            }
            coverage.event_start = coverage.event_start || offset == 0;
            coverage.event_end = coverage.event_end || offset == static_cast<sdk::int32>(end - start - 1);
        }
        std::size_t command_points = 0;
        for (const auto& point : kCommands) {
            if (!inside(point.frame)) { continue; }
            auto& queue = commands[static_cast<std::size_t>(point.channel)];
            require(static_cast<std::size_t>(queue.count) < queue.values.size(), "host command queue bound");
            const auto offset = point.frame - static_cast<sdk::int32>(start);
            queue.values[static_cast<std::size_t>(queue.count++)] = {offset, point.value};
            ++command_points;
            coverage.command_start = coverage.command_start || offset == 0;
            coverage.command_end = coverage.command_end || offset == static_cast<sdk::int32>(end - start - 1);
        }
        require(command_points <= 256, "host total command bound");
        // Reverse queue order is deliberate. Sustain comes last in the host
        // list, although the required physical dispatch order puts it first.
        for (std::size_t channel = commands.size(); channel > 0; --channel) {
            if (commands[channel - 1].count != 0) {
                changes.values[static_cast<std::size_t>(changes.count++)] = &commands[channel - 1];
            }
        }
        for (const auto& point : kPedal) {
            if (!inside(point.frame)) { continue; }
            require(static_cast<std::size_t>(pedal.count) < pedal.values.size(), "host pedal bound");
            const auto offset = point.frame - static_cast<sdk::int32>(start);
            pedal.values[static_cast<std::size_t>(pedal.count++)] = {offset, point.value};
            coverage.pedal_start = coverage.pedal_start || offset == 0;
            coverage.pedal_end = coverage.pedal_end || offset == static_cast<sdk::int32>(end - start - 1);
        }
        if (pedal.count != 0) { changes.values[static_cast<std::size_t>(changes.count++)] = &pedal; }
        require(changes.count <= 18, "host parameter queue bound");

        const bool null_empty = callback % 2 == 0;
        host.data.inputEvents = events.count != 0 || !null_empty ? &events : nullptr;
        host.data.inputParameterChanges = changes.count != 0 || !null_empty ? &changes : nullptr;
        host.data.numSamples = static_cast<sdk::int32>(end - start);
        if (events.count == 0 && changes.count == 0) {
            if (null_empty) { ++coverage.empty_null; }
            else { ++coverage.empty_objects; }
        }
        if (events.count != 0 && only_pressure && changes.count == 0) {
            ++coverage.pressure_only;
            coverage.pressure_advances = coverage.pressure_advances ||
                events.values[0].sampleOffset < events.values[static_cast<std::size_t>(events.count - 1)].sampleOffset;
        }
        host.fill(Sample{0.125});
    }

    void inspect_reads() const {
        require(events.get_calls == static_cast<std::size_t>(events.count),
                "partition skipped/repeated an event getter");
        require(changes.reads == static_cast<std::size_t>(changes.count),
                "partition skipped/repeated a parameter queue getter");
        require(pedal.get_calls == static_cast<std::size_t>(pedal.count),
                "partition skipped/repeated a pedal getter");
        for (const auto& command : commands) {
            require(command.reads == static_cast<std::size_t>(command.count),
                    "partition skipped/repeated a command getter");
        }
    }
};

template<class Sample>
struct Result {
    std::array<Sample, kTraceFrames> audio{};
    std::array<double, kHistoryProbeFrames> continuation{};
    EncodedInstrumentState saved{};
    InstrumentState live{};
    std::uint64_t cursor = 0;
    std::array<std::size_t, key_count> holders{};
};

template<class Sample>
Result<Sample> replay(double rate, Layout layout) {
    // Keep all large fixed host/engine storage off the 1 MiB Windows stack.
    const auto fixture = std::make_unique<HostFixture<Sample>>(rate);
    Oracle oracle(rate);
    Result<Sample> result;
    auto& host = fixture->host;
    for (std::size_t start = 0, callback = 0; start < kTraceFrames; ++callback) {
        const auto end = block_end(layout, start, callback);
        fixture->stage(start, end, callback);
        require(host.processor.process(host.data) == sdk::kResultOk, "valid partition callback rejected");
        ++fixture->coverage.calls;
        fixture->inspect_reads();
        bool nonzero = false;
        for (std::size_t position = start; position < end; ++position) {
            const auto expected = static_cast<Sample>(oracle.sample());
            const auto offset = position - start;
            if (!std::isfinite(host.left[offset]) || !same_bits(host.left[offset], expected) ||
                !same_bits(host.right[offset], expected)) {
                throw std::runtime_error("partition stereo/scalar bits differ at absolute frame " +
                                         std::to_string(position));
            }
            result.audio[position] = host.left[offset];
            nonzero = nonzero || expected != Sample{0};
            ++fixture->coverage.samples;
        }
        require(host.output.silenceFlags == (nonzero ? 0U : 3U), "partition silence flags differ");
        for (std::size_t index = end - start; index < kTraceFrames; ++index) {
            require(host.left[index] == Sample{0.125} && host.right[index] == Sample{0.125},
                    "partition wrote beyond this callback's output extent");
        }
        oracle.inspect(host.processor);
        start = end;
    }
    require(oracle.message_index == kMessages.size() && oracle.command_index == kCommands.size() &&
            oracle.pedal_index == kPedal.size(), "absolute timeline was not fully consumed");
    const auto& coverage = fixture->coverage;
    require(coverage.samples == kTraceFrames && coverage.event_start && coverage.event_end &&
            coverage.command_start && coverage.command_end && coverage.pedal_start && coverage.pedal_end,
            "partition omitted a planned block-start/end boundary");
    if (layout != Layout::whole) {
        require(coverage.empty_null != 0 && coverage.empty_objects != 0 && coverage.pressure_only != 0,
                "partition omitted null/empty or pressure-only callbacks");
    }
    if (layout == Layout::fixed64 || layout == Layout::ragged) {
        require(coverage.pressure_advances, "pressure-only callback did not advance sorted offsets");
    }
    require(std::any_of(result.audio.begin(), result.audio.end(), [](Sample sample) { return sample != Sample{0}; }),
            "partition fixture unexpectedly produced silence");

    // An empty zero-frame callback immediately follows populated scratch.
    // It must not advance mechanics, ownership, history, or touch output data.
    host.data.numSamples = 0;
    fixture->events.count = 0;
    fixture->changes.count = 0;
    host.data.inputEvents = &fixture->events;
    host.data.inputParameterChanges = &fixture->changes;
    host.fill(Sample{0.125});
    require(host.processor.process(host.data) == sdk::kResultOk, "empty partition flush rejected");
    ++fixture->coverage.calls;
    require(std::all_of(host.left.begin(), host.left.end(), [](Sample value) { return value == Sample{0.125}; }) &&
            std::all_of(host.right.begin(), host.right.end(), [](Sample value) { return value == Sample{0.125}; }),
            "empty partition flush touched samples");
    oracle.inspect(host.processor);

    // q/v equality alone cannot reveal a changed held flag or convolution
    // history. Probe beyond the complete filter memory, including the strike
    // at the very last trace sample. Copies leave the verified cursor intact.
    const auto actual = std::make_unique<ScalarEngine>(host.processor.engine_processor().engine());
    const auto expected = std::make_unique<ScalarEngine>(*oracle.engine);
    std::array<double, kHistoryProbeFrames> expected_continuation{};
    require(actual->process(result.continuation) && expected->process(expected_continuation),
            "partition history continuation failed");
    for (std::size_t index = 0; index < result.continuation.size(); ++index) {
        require(same_bits(result.continuation[index], expected_continuation[index]),
                "partition changed future damping/filter history");
    }
    same_mechanics(*actual, *expected);
    result.live = host.processor.current_state();
    result.cursor = host.processor.engine_processor().frame_cursor();
    for (std::size_t index = 0; index < result.holders.size(); ++index) {
        result.holders[index] = host.processor.note_tracker().held_count(index);
    }
    MemoryStream saved;
    require(host.processor.getState(&saved) == sdk::kResultOk &&
            saved.extent == static_cast<sdk::int64>(result.saved.size()), "partition save failed");
    std::copy_n(saved.bytes.begin(), result.saved.size(), result.saved.begin());
    const auto encoded = encode_instrument_state(oracle.state, kStateModel);
    require(encoded && result.saved == *encoded, "transient commands/pressure entered saved state");
    total_calls += coverage.calls;
    total_samples += coverage.samples;
    return result;
}

template<class Sample>
void compare_partitions(double rate) {
    const auto whole = replay<Sample>(rate, Layout::whole);
    for (const auto layout : {Layout::one_frame, Layout::fixed64, Layout::ragged}) {
        try {
            const auto partitioned = replay<Sample>(rate, layout);
            for (std::size_t index = 0; index < whole.audio.size(); ++index) {
                require(same_bits(whole.audio[index], partitioned.audio[index]),
                        "audio bits depend on callback partition");
            }
            for (std::size_t index = 0; index < whole.continuation.size(); ++index) {
                require(same_bits(whole.continuation[index], partitioned.continuation[index]),
                        "future audio bits depend on callback partition");
            }
            require(partitioned.live == whole.live && partitioned.saved == whole.saved &&
                    partitioned.holders == whole.holders && partitioned.cursor == whole.cursor &&
                    partitioned.cursor == kTraceFrames, "final state depends on callback partition");
        } catch (const std::exception& error) {
            throw std::runtime_error(std::string(layout_name(layout)) + ": " + error.what());
        }
    }
}
} // namespace

int main() {
    try {
        for (const double rate : {32000.0, 48000.0, 96000.0, 192000.0}) {
            try {
                compare_partitions<float>(rate);
                compare_partitions<double>(rate);
            } catch (const std::exception& error) {
                throw std::runtime_error(std::to_string(rate) + " Hz: " + error.what());
            }
        }
        std::cout << kPluginName << ": " << total_calls << " callbacks, " << total_samples
                  << " exact scalar-checked samples; whole/one-frame/fixed64/ragged CC123, pressure, "
                     "notes and sustain agree in both formats at four rates.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << kPluginName << ": " << error.what() << '\n';
        return 1;
    }
}
