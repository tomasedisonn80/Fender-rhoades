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

#if defined(RHODES_QUADRATURE_VST3)
static_assert(kStateModel == 3 && std::is_same_v<ScalarEngine, QuadratureEngine>);
#elif defined(RHODES_FILTERED_VST3)
static_assert(kStateModel == 2 && std::is_same_v<ScalarEngine, AudioEngine>);
#else
static_assert(kStateModel == 1 && std::is_same_v<ScalarEngine, ResearchEngine>);
#endif
static_assert(kMaximumChannelCommands == 256 && kControllerParameterCount == 18);
constexpr std::size_t frames = 128;

struct Owner {
    sdk::int32 id;
    sdk::int16 pitch;
    sdk::int16 channel;
    float velocity;
};

constexpr std::array<Owner, 6> initial_owners{{
    {7, 60, 3, 0.5F}, {-2, 60, 9, 0.375F}, {-1, 72, 3, 0.5F},
    {-1, 72, 3, 0.25F}, {44, 84, 15, 0.625F}, {7, 65, 8, 0.5F}}};

std::size_t key(sdk::int16 midi_pitch) {
    return static_cast<std::size_t>(midi_pitch - 28);
}

void recall(RhodesProcessor& processor, InstrumentState state) {
    MemoryStream stream;
    stream.load(state, 0, kStateModel);
    require(processor.setState(&stream) == sdk::kResultOk, "command fixture recall failed");
    require(processor.published_state() == state, "requested command recall was not published");
}

vst::Event pressure(sdk::int32 offset) {
    vst::Event event{};
    event.type = vst::Event::kPolyPressureEvent;
    event.sampleOffset = offset;
    event.polyPressure.channel = 3;
    event.polyPressure.pitch = 74;
    event.polyPressure.noteId = 7;
    event.polyPressure.pressure = 0.5F;
    return event;
}

template<class Sample>
void expect_frame(const ProcessorHost<Sample, frames>& host, std::size_t frame,
                  Sample expected, std::string_view context) {
    using Bits = std::conditional_t<std::is_same_v<Sample, float>, std::uint32_t, std::uint64_t>;
    const auto left = host.channels[0] != nullptr ? expected : Sample{0.25};
    const auto right = host.channels[1] != nullptr ? expected : Sample{0.25};
    require(std::bit_cast<Bits>(host.left[frame]) == std::bit_cast<Bits>(left) &&
            std::bit_cast<Bits>(host.right[frame]) == std::bit_cast<Bits>(right), context);
}

// Fixture actions supply physical strikes/releases directly. The oracle never
// uses the adapter's command translation, event dispatcher, or NoteTracker.
struct Oracle {
    std::unique_ptr<ScalarEngine> engine = std::make_unique<ScalarEngine>();
    std::array<std::size_t, key_count> holders{};
    InstrumentState state{};
    std::uint64_t cursor = 0;

    Oracle() { require(engine->prepare(48000.0), "command oracle preparation failed"); }

    void strike(sdk::int16 midi_pitch, float velocity, bool new_holder = true) {
        require(engine->note_on(key(midi_pitch), static_cast<double>(velocity)),
                "command oracle strike failed");
        if (new_holder) { ++holders[key(midi_pitch)]; }
    }

    void release(sdk::int16 midi_pitch) {
        auto& count = holders[key(midi_pitch)];
        require(count != 0, "command oracle released a missing fixture holder");
        if (--count == 0) {
            require(engine->note_off(key(midi_pitch)), "command oracle release failed");
        }
    }

    void sustain(bool down) {
        engine->set_sustain(down);
        state.sustain = down;
    }

    void install(InstrumentState recalled) {
        engine->reset();
        holders.fill(0);
        state = recalled;
        engine->set_sustain(state.sustain);
        cursor = 0;
    }

    template<class Sample>
    void inspect(const ProcessorHost<Sample, frames>& host) const {
        require(host.processor.current_state() == state && host.processor.published_state() == state,
                "command live/published configuration differs from oracle");
        require(host.processor.engine_processor().frame_cursor() == cursor,
                "command cursor differs from oracle");
        require(!host.processor.requires_reset(), "valid command trace faulted the processor");
        std::size_t total = 0;
        for (std::size_t index = 0; index < holders.size(); ++index) {
            total += holders[index];
            require(host.processor.note_tracker().held_count(index) == holders[index],
                    "command per-key holder count differs from oracle");
        }
        require(host.processor.note_tracker().active_count() == total,
                "command total holder count differs from oracle");
        same_mechanics(host.processor.engine_processor().engine(), *engine);
    }

    template<class Sample, class Actions>
    void block(ProcessorHost<Sample, frames>& host, Actions actions) {
        require(host.data.numSamples == static_cast<sdk::int32>(frames), "invalid oracle block size");
        host.fill(Sample{0.25});
        require(host.processor.process(host.data) == sdk::kResultOk, "valid command transaction rejected");
        bool nonzero = false;
        for (std::size_t frame = 0; frame < frames; ++frame) {
            actions(frame);
            double volts = 0.0;
            require(engine->process(std::span<double>(&volts, 1)), "command oracle rendering failed");
            const auto expected = static_cast<Sample>((32.0 * static_cast<double>(state.monitoring_gain)) * volts);
            expect_frame(host, frame, expected, "command transaction stereo bits differ from scalar oracle");
            nonzero = nonzero || expected != Sample{0};
        }
        cursor += frames;
        require(host.output.silenceFlags == (nonzero ? 0U : 3U), "command transaction silence flags");
        inspect(host);
    }

    template<class Sample, class Actions>
    void flush(ProcessorHost<Sample, frames>& host, Actions actions) {
        require(host.data.numSamples == 0, "invalid oracle flush size");
        host.fill(Sample{0.25});
        require(host.processor.process(host.data) == sdk::kResultOk, "valid command flush rejected");
        actions();
        require(std::all_of(host.left.begin(), host.left.end(), [](Sample value) { return value == Sample{0.25}; }) &&
                std::all_of(host.right.begin(), host.right.end(), [](Sample value) { return value == Sample{0.25}; }),
                "command flush accessed sample storage");
        inspect(host);
    }

    template<class Sample>
    void tail(ProcessorHost<Sample, frames>& host) {
        host.data.inputEvents = nullptr;
        host.data.inputParameterChanges = nullptr;
        block(host, [](std::size_t) {});
    }
};

// Snapshot counts alone cannot distinguish a removed identified holder from a
// replacement at the same key. Drain copies with the original identities too.
void unchanged_identities(const ProcessorSnapshot& before, const RhodesProcessor& processor,
                          std::span<const Owner> owners) {
    auto observed = processor.note_tracker();
    auto expected = before.notes;
    for (const auto& owner : owners) {
        const MidiNote release{false, owner.channel, owner.pitch, owner.id, 0.0};
        const auto actual = observed.apply(release);
        const auto reference = expected.apply(release);
        require(actual.status == reference.status && actual.change.has_value() == reference.change.has_value(),
                "rejected commands changed a holder's identity");
        if (actual.change) {
            require(actual.change->type == reference.change->type && actual.change->key == reference.change->key,
                    "rejected commands changed an identity's physical release");
        }
        require(observed.active_count() == expected.active_count(), "rejected command identity drain differs");
    }
    require(observed.active_count() == 0 && expected.active_count() == 0,
            "rejected command retained an unexpected identity");
}

template<class Sample>
void reject(ProcessorHost<Sample, frames>& host, const ProcessorSnapshot& before,
            std::span<const Owner> owners, std::string_view context) {
    host.fill(Sample{0.25});
    require(host.processor.process(host.data) == sdk::kResultFalse,
            std::string(context) + ": invalid command transaction accepted");
    if (host.data.numSamples == 0) {
        require(std::all_of(host.left.begin(), host.left.end(), [](Sample value) { return value == Sample{0.25}; }) &&
                std::all_of(host.right.begin(), host.right.end(), [](Sample value) { return value == Sample{0.25}; }),
                "rejected zero-frame commands wrote samples");
    } else {
        for (std::size_t frame = 0; frame < frames; ++frame) {
            expect_frame(host, frame, Sample{0}, "rejected command changed absent storage or failed to silence");
        }
        require(host.output.silenceFlags == 3, "rejected command silence flags");
    }
    before.expect_unchanged(host.processor);
    unchanged_identities(before, host.processor, owners);
    // Check all of the 96-frame convolution memory before a later recall can
    // erase any history corruption caused by a rejected command transaction.
    const auto actual = std::make_unique<ScalarEngine>(host.processor.engine_processor().engine());
    const auto expected = std::make_unique<ScalarEngine>(before.engine.engine());
    std::array<double, frames + 1> actual_tail{}, expected_tail{};
    require(actual->process(actual_tail) && expected->process(expected_tail) && actual_tail == expected_tail,
            "rejected commands changed future audio");
    MemoryStream saved;
    require(host.processor.getState(&saved) == sdk::kResultOk, "save after command rejection failed");
    const auto encoded = encode_instrument_state(before.published, kStateModel);
    require(encoded.has_value() && saved.extent == static_cast<sdk::int64>(instrument_state_bytes) &&
            std::equal(encoded->begin(), encoded->end(), saved.bytes.begin()),
            "command rejection changed the pending saved configuration");
}

template<class Sample>
void seed(ProcessorHost<Sample, frames>& host, Oracle& oracle) {
    EventList notes;
    notes.count = static_cast<sdk::int32>(initial_owners.size());
    for (std::size_t index = 0; index < initial_owners.size(); ++index) {
        const auto& owner = initial_owners[index];
        notes.values[index] = note_on(static_cast<sdk::int32>(index), owner.id, owner.pitch,
                                      owner.velocity, owner.channel);
    }
    CommandQueue gain, pedal;
    gain.id = kGainId;
    gain.count = 1;
    gain.values[0] = {0, 0.625};
    pedal.id = kSustainId;
    pedal.count = 1;
    pedal.values[0] = {0, 1.0};
    CommandChanges changes;
    changes.count = 2;
    changes.values[0] = &gain;
    changes.values[1] = &pedal;
    host.data.inputEvents = &notes;
    host.data.inputParameterChanges = &changes;
    oracle.block(host, [&](std::size_t frame) {
        if (frame == 0) { oracle.state.monitoring_gain = 0.625F; oracle.sustain(true); }
        if (frame < initial_owners.size()) {
            const auto& owner = initial_owners[frame];
            oracle.strike(owner.pitch, owner.velocity);
        }
    });
    oracle.tail(host);
    require(std::any_of(host.left.begin(), host.left.end(), [](Sample value) { return value != Sample{0}; }),
            "atomic rejection fixture has no audible history");
}

template<class Sample>
void malformed_transactions_and_recall() {
    ProcessorHost<Sample, frames> host;
    Oracle oracle;
    seed(host, oracle);
    const InstrumentState pending{0.25F, false};
    recall(host.processor, pending);
    const ProcessorSnapshot before(host.processor);
    require(before.live != before.published && before.engine.frame_cursor() == 2 * frames,
            "pending-recall rejection fixture is not distinct from live state");

    EventList notes;
    notes.count = 2;
    notes.values[0] = note_off(0, 7, 60, 3);
    notes.values[1] = note_on(4, 99, 90, 0.5F, 4);
    CommandQueue gain, pedal, first, last;
    gain.id = kGainId;
    gain.count = 1;
    gain.values[0] = {0, 0.875};
    pedal.id = kSustainId;
    pedal.count = 1;
    pedal.values[0] = {1, 1.0};
    first.id = kAllNotesOffBaseId + 3;
    first.count = 2;
    first.values[0] = {4, 0.0};
    first.values[1] = {11, 1.0};
    last.id = kAllNotesOffBaseId + 15;
    last.count = 3;
    last.values[0] = {12, 0.0};
    last.values[1] = {48, 0.5};
    last.values[2] = {127, 1.0};
    CommandChanges changes;
    changes.count = 4;
    changes.values[0] = &gain;
    changes.values[1] = &pedal;
    changes.values[2] = &first;
    changes.values[3] = &last;
    host.data.inputEvents = &notes;
    host.data.inputParameterChanges = &changes;
    const auto bad = [&](std::string_view context) {
        gain.reads = pedal.reads = first.reads = last.reads = changes.reads = notes.get_calls = 0;
        reject(host, before, initial_owners, context);
    };
    for (const std::uint64_t bits : {
            0xbff0000000000000ULL, 0x8000000000000001ULL, 0x3ff0000000000001ULL,
            0x7ff8000000000001ULL, 0x7ff0000000000001ULL,
            0x7ff0000000000000ULL, 0xfff0000000000000ULL}) {
        last.values[2] = {127, std::bit_cast<double>(bits)};
        bad("late non-normalized command");
        require(last.reads == 3 && first.reads == 2 && gain.reads == 1 && pedal.reads == 1 &&
                notes.get_calls == 0, "late invalid command did not follow complete earlier queues");
    }
    last.values[2].value = 1.0;
    for (const sdk::int32 offset : {-1, 47, 128, std::numeric_limits<sdk::int32>::max()}) {
        last.values[2].offset = offset;
        bad("late command offset/order");
        require(last.reads == 3, "late command offset rejection skipped the offending point");
    }
    last.values[2].offset = 127;
    last.failed_index = 2;
    bad("late failed command point getter");
    require(last.reads == 3 && notes.get_calls == 0, "failed final command getter traversal");
    last.failed_index = -1;
    changes.values[3] = nullptr;
    bad("late null command queue getter");
    require(changes.reads == 4 && first.reads == 2 && last.reads == 0,
            "null last queue did not follow staged valid commands");
    changes.values[3] = &last;

    last.count = 0;
    for (const auto duplicate : {kGainId, kSustainId, first.id}) {
        last.id = duplicate;
        bad("duplicate parameter queue");
        require(changes.reads == 4 && last.reads == 0, "duplicate queue points were read");
    }
    for (const vst::ParamID unknown : {vst::ParamID{2}, kAllNotesOffBaseId - 1,
                                     kAllNotesOffBaseId + 16, std::numeric_limits<vst::ParamID>::max()}) {
        last.id = unknown;
        bad("unknown parameter ID");
        require(changes.reads == 4 && last.reads == 0, "unknown queue points were read");
    }
    last.id = kAllNotesOffBaseId + 15;
    last.count = -1;
    bad("negative command point count");
    require(last.reads == 0, "negative command point count was traversed");
    last.count = 3;
    for (const sdk::int32 count : {-1, 19}) {
        changes.count = count;
        bad("invalid total queue count");
        require(changes.reads == 0, "invalid queue count was traversed");
    }
    changes.count = 4;

    // The bounds suite exhausts raw/expanded capacities. This one aggregate
    // overflow specifically proves that a pending recall survives the failure.
    first.count = 128;
    last.count = 129;
    first.values.fill({4, 0.0});
    last.values.fill({12, 0.0});
    bad("aggregate command overflow with pending recall");
    require(first.reads == 128 && last.reads == 0 && notes.get_calls == 0,
            "aggregate overflow read the excess queue or consumed events");

    first.count = 1;
    last.count = 1;
    first.values[0] = {4, 0.0};
    last.values[0] = {12, 0.0};
    notes.values[1].noteOn.pitch = 128;
    bad("late event syntax after valid commands");
    require(notes.get_calls == 2 && last.reads == 1, "event rejection skipped preceding command syntax");

    // Syntax passes and begin_block selects the recall, but the first ID is
    // reused before the later channel command can release it. Abort must put
    // the selected recall back without installing it or touching live owners.
    notes.values[0] = note_on(1, 7, 72, 0.5F, 3);
    notes.values[1] = note_on(2, 7, 73, 0.5F, 3);
    bad("ownership conflict after selecting pending recall");
    require(notes.get_calls == 2, "ownership rejection did not complete event syntax");

    CommandQueue middle;
    middle.id = kAllNotesOffBaseId + 9;
    middle.count = 1;
    middle.values[0] = {31, 0.0};
    first.count = 2;
    first.values[0] = {0, -0.0};
    first.values[1] = {64, 0.0};
    last.values[0] = {0, 0.0};
    changes.count = 3;
    changes.values[0] = &last;
    changes.values[1] = &middle;
    changes.values[2] = &first;
    notes.count = 5;
    notes.values[0] = note_on(0, 7, 69, 0.5F, 3);
    notes.values[1] = note_on(0, 44, 86, 0.625F, 15);
    notes.values[2] = note_on(2, -2, 64, 0.375F, 9);
    notes.values[3] = note_on(31, -2, 75, 0.25F, 9);
    notes.values[4] = note_on(64, 7, 70, 0.5F, 3);
    oracle.install(pending);
    oracle.block(host, [&](std::size_t frame) {
        if (frame == 0) { oracle.strike(69, 0.5F); oracle.strike(86, 0.625F); }
        if (frame == 2) { oracle.strike(64, 0.375F); }
        if (frame == 31) { oracle.release(64); oracle.strike(75, 0.25F); }
        if (frame == 64) { oracle.release(69); oracle.strike(70, 0.5F); }
    });
    require(host.processor.note_tracker().active_count() == 3 &&
            host.processor.engine_processor().frame_cursor() == frames,
            "accepted command recall did not replace old ownership and reset the cursor");
    oracle.tail(host);
}

template<class Sample>
void chronological_identity_reuse() {
    ProcessorHost<Sample, frames> host;
    Oracle oracle;
    constexpr std::array<Owner, 4> owners{{
        {77, 60, 2, 0.5F}, {77, 64, 9, 0.375F}, {-2, 72, 2, 0.25F}, {-1, 60, 2, 0.5F}}};
    EventList notes;
    notes.count = static_cast<sdk::int32>(owners.size());
    for (std::size_t index = 0; index < owners.size(); ++index) {
        const auto& owner = owners[index];
        notes.values[index] = note_on(0, owner.id, owner.pitch, owner.velocity, owner.channel);
    }
    host.data.inputEvents = &notes;
    oracle.block(host, [&](std::size_t frame) {
        if (frame == 0) { for (const auto& owner : owners) { oracle.strike(owner.pitch, owner.velocity); } }
    });
    const ProcessorSnapshot before(host.processor);
    CommandQueue command;
    command.id = kAllNotesOffBaseId + 2;
    command.count = 3;
    command.values[0] = {16, 0.0};
    command.values[1] = {48, 1.0};
    command.values[2] = {48, 0.0};
    CommandChanges changes;
    changes.count = 1;
    changes.values[0] = &command;
    host.data.inputParameterChanges = &changes;
    notes.count = 2;
    notes.values[0] = note_off(0, -2, 72, 2);
    notes.values[1] = note_on(15, 77, 67, 0.5F, 2);
    reject(host, before, owners, "reuse before chronological channel release");
    notes.count = 3;
    notes.values[0] = note_on(16, 77, 67, 0.5F, 2);
    notes.values[1] = note_on(32, 77, 68, 0.5F, 2);
    notes.values[2] = note_off(32, 77, 67, 2);
    reject(host, before, owners, "reversed equal-offset note reuse");

    notes.count = 9;
    notes.values[0] = note_on(16, 77, 67, 0.5F, 2);
    notes.values[1] = note_on(16, -2, 74, 0.25F, 2);
    notes.values[2] = note_off(32, 77, 67, 2);
    notes.values[3] = note_on(32, 77, 68, 0.375F, 2);
    notes.values[4] = note_on(48, 77, 69, 0.5F, 2);
    notes.values[5] = note_on(48, -2, 75, 0.25F, 2);
    notes.values[6] = note_on(48, 77, 69, 0.25F, 2);
    notes.values[7] = note_off(64, 77, 69, 2);
    notes.values[8] = note_on(64, 77, 70, 0.5F, 2);
    oracle.block(host, [&](std::size_t frame) {
        if (frame == 16) {
            oracle.release(60); oracle.release(60); oracle.release(72);
            oracle.strike(67, 0.5F); oracle.strike(74, 0.25F);
        }
        if (frame == 32) { oracle.release(67); oracle.strike(68, 0.375F); }
        if (frame == 48) {
            oracle.release(68); oracle.release(74);
            oracle.strike(69, 0.5F); oracle.strike(75, 0.25F); oracle.strike(69, 0.25F, false);
        }
        if (frame == 64) { oracle.release(69); oracle.strike(70, 0.5F); }
    });
    require(host.processor.note_tracker().active_count() == 3 &&
            host.processor.note_tracker().held_count(key(64)) == 1,
            "chronological reuse removed the other channel's identical ID");
    oracle.tail(host);
}

template<class Sample>
void match_other_order(ProcessorHost<Sample, frames>& other,
                       const ProcessorHost<Sample, frames>& canonical, const Oracle& oracle) {
    require(other.processor.process(other.data) == sdk::kResultOk, "permuted command queues rejected");
    oracle.inspect(other);
    using Bits = std::conditional_t<std::is_same_v<Sample, float>, std::uint32_t, std::uint64_t>;
    for (std::size_t frame = 0; frame < frames; ++frame) {
        require(std::bit_cast<Bits>(other.left[frame]) == std::bit_cast<Bits>(canonical.left[frame]) &&
                std::bit_cast<Bits>(other.right[frame]) == std::bit_cast<Bits>(canonical.right[frame]),
                "parameter queue order changed output bits");
    }
    require(other.output.silenceFlags == canonical.output.silenceFlags, "queue order changed silence flags");
}

template<class Sample>
void sixteen_channel_queue_order() {
    ProcessorHost<Sample, frames> canonical, reversed, shuffled;
    Oracle oracle;
    EventList notes;
    notes.count = 32;
    for (sdk::int16 channel = 0; channel < 16; ++channel) {
        const auto index = 2 * static_cast<std::size_t>(channel);
        notes.values[index] = note_on(0, 1000 + channel, 60, 0.125F, channel);
        notes.values[index + 1] = note_on(0, 2000 + channel, static_cast<sdk::int16>(28 + channel), 0.25F, channel);
    }
    for (auto* host : {&canonical, &reversed, &shuffled}) { host->data.inputEvents = &notes; }
    oracle.block(canonical, [&](std::size_t frame) {
        if (frame == 0) {
            for (sdk::int16 channel = 0; channel < 16; ++channel) {
                oracle.strike(60, 0.125F); oracle.strike(static_cast<sdk::int16>(28 + channel), 0.25F);
            }
        }
    });
    match_other_order(reversed, canonical, oracle);
    match_other_order(shuffled, canonical, oracle);

    const auto first_offset = [](sdk::int16 channel) { return 16 + ((channel * 7) % 4) * 16; };
    std::array<CommandQueue, 16> commands;
    CommandQueue gain, pedal;
    gain.id = kGainId;
    gain.count = 1;
    gain.values[0] = {0, 0.75};
    pedal.id = kSustainId;
    pedal.count = 2;
    pedal.values[0] = {16, 1.0};
    pedal.values[1] = {96, 0.0};
    CommandChanges ascending, descending, permutation;
    ascending.count = descending.count = permutation.count = 18;
    for (sdk::int16 channel = 0; channel < 16; ++channel) {
        auto& queue = commands[static_cast<std::size_t>(channel)];
        queue.id = kAllNotesOffBaseId + static_cast<vst::ParamID>(channel);
        queue.count = 3;
        queue.values[0] = {first_offset(channel), channel % 2 == 0 ? -0.0 : 1.0};
        queue.values[1] = {96, 0.0};
        queue.values[2] = {96, 0.5};
        ascending.values[static_cast<std::size_t>(channel)] = &queue;
    }
    ascending.values[16] = &gain;
    ascending.values[17] = &pedal;
    constexpr std::array<std::size_t, 18> order{17, 3, 12, 1, 8, 15, 16, 6, 0, 11, 5, 14, 2, 9, 4, 13, 7, 10};
    for (std::size_t index = 0; index < ascending.values.size(); ++index) {
        descending.values[index] = ascending.values[17 - index];
        permutation.values[index] = ascending.values[order[index]];
    }
    canonical.data.inputParameterChanges = &ascending;
    reversed.data.inputParameterChanges = &descending;
    shuffled.data.inputParameterChanges = &permutation;
    notes.count = 0;
    for (const sdk::int32 offset : {16, 32, 48, 64}) {
        for (sdk::int16 channel = 15; channel >= 0; --channel) {
            if (first_offset(channel) == offset) {
                notes.values[static_cast<std::size_t>(notes.count++)] =
                    note_on(offset, 1000 + channel, static_cast<sdk::int16>(72 + channel), 0.375F, channel);
            }
        }
    }
    for (sdk::int16 channel = 15; channel >= 0; --channel) {
        notes.values[static_cast<std::size_t>(notes.count++)] =
            note_on(96, 1000 + channel, static_cast<sdk::int16>(84 + channel % 8), 0.5F, channel);
    }
    oracle.block(canonical, [&](std::size_t frame) {
        if (frame == 0) { oracle.state.monitoring_gain = 0.75F; }
        if (frame == 16) { oracle.sustain(true); }
        if (frame == 96) { oracle.sustain(false); }
        for (sdk::int16 channel = 0; channel < 16; ++channel) {
            if (frame == static_cast<std::size_t>(first_offset(channel))) {
                oracle.release(static_cast<sdk::int16>(28 + channel)); oracle.release(60);
            }
            if (frame == 96) { oracle.release(static_cast<sdk::int16>(72 + channel)); }
        }
        for (sdk::int16 channel = 15; channel >= 0; --channel) {
            if (frame == static_cast<std::size_t>(first_offset(channel))) {
                oracle.strike(static_cast<sdk::int16>(72 + channel), 0.375F);
            }
            if (frame == 96) { oracle.strike(static_cast<sdk::int16>(84 + channel % 8), 0.5F); }
        }
    });
    match_other_order(reversed, canonical, oracle);
    match_other_order(shuffled, canonical, oracle);
    require(canonical.processor.note_tracker().active_count() == 16,
            "duplicate equal-offset commands released the new notes");
    for (auto* host : {&canonical, &reversed, &shuffled}) {
        host->data.inputEvents = nullptr;
        host->data.inputParameterChanges = nullptr;
    }
    oracle.tail(canonical);
    match_other_order(reversed, canonical, oracle);
    match_other_order(shuffled, canonical, oracle);
}

template<class Sample>
void zero_frame_syntax_and_saved_state() {
    ProcessorHost<Sample, frames> host;
    Oracle oracle;
    seed(host, oracle);
    const InstrumentState pending{0.375F, true};
    recall(host.processor, pending);
    const ProcessorSnapshot before(host.processor);
    host.data.numSamples = 0;
    host.data.numOutputs = 0;
    host.data.outputs = nullptr;
    CommandQueue gain, pedal, command;
    gain.id = kGainId;
    gain.count = 1;
    gain.values[0] = {0, 0.75};
    pedal.id = kSustainId;
    pedal.count = 1;
    pedal.values[0] = {0, 0.0};
    command.id = kAllNotesOffBaseId + 3;
    command.count = 2;
    command.values[0] = {0, 0.0};
    command.values[1] = {0, 1.0};
    CommandChanges changes;
    changes.count = 3;
    changes.values[0] = &gain;
    changes.values[1] = &pedal;
    changes.values[2] = &command;
    host.data.inputParameterChanges = &changes;
    for (const sdk::int32 offset : {-1, 1}) {
        command.values[1].offset = offset;
        reject(host, before, initial_owners, "nonzero command offset in flush");
    }
    command.values[1] = {0, std::numeric_limits<double>::quiet_NaN()};
    reject(host, before, initial_owners, "nonfinite command in flush");
    command.values[1] = {0, 1.0};
    command.failed_index = 1;
    reject(host, before, initial_owners, "late failed command getter in flush");
    command.failed_index = -1;
    EventList notes;
    notes.count = 1;
    notes.values[0] = note_on(0, 7, 69, 0.5F, 3);
    host.data.inputEvents = &notes;
    reject(host, before, initial_owners, "nonempty event list in command flush");
    require(notes.get_calls == 0, "zero-frame command flush read a forbidden event");
    host.data.inputEvents = nullptr;
    oracle.flush(host, [&] {
        oracle.install(pending);
        oracle.state.monitoring_gain = 0.75F;
        oracle.sustain(false);
    });
    require(host.processor.engine_processor().frame_cursor() == 0 &&
            host.processor.note_tracker().active_count() == 0,
            "valid command flush failed to consume the preserved recall");

    host.data.numSamples = static_cast<sdk::int32>(frames);
    host.data.numOutputs = 1;
    host.data.outputs = &host.output;
    host.data.inputParameterChanges = nullptr;
    host.data.inputEvents = &notes;
    notes.count = 3;
    notes.values[0] = note_on(0, 11, 60, 0.5F, 4);
    notes.values[1] = note_on(0, 12, 60, 0.25F, 5);
    notes.values[2] = note_on(0, 13, 72, 0.375F, 4);
    oracle.block(host, [&](std::size_t frame) {
        if (frame == 0) { oracle.strike(60, 0.5F); oracle.strike(60, 0.25F); oracle.strike(72, 0.375F); }
    });
    MemoryStream saved_before, saved_after;
    require(host.processor.getState(&saved_before) == sdk::kResultOk, "save before flush failed");
    host.data.numSamples = 0;
    host.data.numOutputs = 0;
    host.data.outputs = nullptr;
    host.data.inputEvents = nullptr;
    host.data.inputParameterChanges = &changes;
    changes.count = 1;
    changes.values[0] = &command;
    command.id = kAllNotesOffBaseId + 4;
    oracle.flush(host, [&] { oracle.release(60); oracle.release(72); });
    require(host.processor.engine_processor().frame_cursor() == frames &&
            host.processor.note_tracker().active_count() == 1,
            "ordinary command flush advanced time or released the other channel");
    require(host.processor.getState(&saved_after) == sdk::kResultOk &&
            saved_before.extent == static_cast<sdk::int64>(instrument_state_bytes) &&
            saved_after.extent == saved_before.extent && saved_after.bytes == saved_before.bytes,
            "transient commands changed the 16-byte saved configuration");
    host.data.numSamples = static_cast<sdk::int32>(frames);
    host.data.numOutputs = 1;
    host.data.outputs = &host.output;
    oracle.tail(host);

    ProcessorHost<Sample, frames> restored;
    Oracle restored_oracle;
    saved_after.position = 0;
    require(restored.processor.setState(&saved_after) == sdk::kResultOk, "restore after command flush failed");
    restored_oracle.install(oracle.state);
    notes.count = 2;
    notes.values[0] = note_on(0, 11, 60, 0.5F, 4);
    notes.values[1] = note_on(1, 13, 72, 0.375F, 4);
    restored.data.inputEvents = &notes;
    restored_oracle.block(restored, [&](std::size_t frame) {
        if (frame == 0) { restored_oracle.strike(60, 0.5F); }
        if (frame == 1) { restored_oracle.strike(72, 0.375F); }
    });
    restored_oracle.tail(restored);
    require(restored.processor.note_tracker().active_count() == 2,
            "saved or previously delivered channel commands replayed after restoration");
}

template<class Sample>
void inactive_commands_and_recall(unsigned mask) {
    ProcessorHost<Sample, frames> host;
    Oracle oracle;
    seed(host, oracle);
    const auto output_active = [&](bool active) {
        require(host.processor.activateBus(vst::kAudio, vst::kOutput, 0, active) == sdk::kResultOk,
                "command output bus activation failed");
        host.channels = {host.left.data(), host.right.data()};
        if (!active) {
            if ((mask & 1U) == 0U) { host.channels[0] = nullptr; }
            if ((mask & 2U) == 0U) { host.channels[1] = nullptr; }
        }
    };
    const auto audible_tail = [&] {
        output_active(true);
        oracle.tail(host);
        require(std::any_of(host.left.begin(), host.left.end(), [](Sample value) { return value != Sample{0}; }),
                "reactivated command continuation has no audible history");
    };

    CommandQueue command, pedal, gain;
    command.id = kAllNotesOffBaseId + 3;
    command.count = 2;
    command.values[0] = command.values[1] = {17, 0.0};
    pedal.id = kSustainId;
    pedal.count = 3;
    pedal.values[0] = {0, 0.0};
    pedal.values[1] = {17, 1.0};
    pedal.values[2] = {50, 0.0};
    gain.id = kGainId;
    gain.count = 1;
    gain.values[0] = {0, 0.875};
    CommandChanges changes;
    changes.count = 2;
    changes.values[0] = &command;
    changes.values[1] = &pedal;
    EventList notes;
    notes.count = 5;
    notes.values[0] = pressure(0);
    notes.values[1] = pressure(17);
    notes.values[2] = note_on(17, 7, 74, 0.625F, 3);
    notes.values[3] = pressure(17);
    notes.values[4] = note_off(80, -2, 60, 9);
    host.data.inputParameterChanges = &changes;
    host.data.inputEvents = &notes;
    output_active(false);
    oracle.block(host, [&](std::size_t frame) {
        if (frame == 0) { oracle.sustain(false); }
        if (frame == 17) {
            oracle.sustain(true);
            oracle.release(60); oracle.release(72); oracle.release(72);
            oracle.strike(74, 0.625F);
        }
        if (frame == 50) { oracle.sustain(false); }
        if (frame == 80) { oracle.release(60); }
    });
    constexpr std::array<Owner, 3> remaining{{
        {7, 74, 3, 0.625F}, {44, 84, 15, 0.625F}, {7, 65, 8, 0.5F}}};
    audible_tail();

    // Reject at the final command point and then after all queues plus a note
    // have been staged. A clean inactive continuation must not replay scratch.
    output_active(false);
    const ProcessorSnapshot before(host.processor);
    changes.count = 3;
    changes.values[0] = &gain;
    changes.values[1] = &pedal;
    changes.values[2] = &command;
    command.values[0] = {0, 0.0};
    command.values[1] = {127, std::numeric_limits<double>::quiet_NaN()};
    pedal.count = 1;
    pedal.values[0] = {0, 1.0};
    notes.count = 2;
    notes.values[0] = note_on(0, 301, 80, 0.25F, 3);
    notes.values[1] = pressure(127);
    host.data.inputParameterChanges = &changes;
    host.data.inputEvents = &notes;
    gain.reads = pedal.reads = command.reads = notes.get_calls = 0;
    reject(host, before, remaining, "inactive late malformed command");
    require(gain.reads == 1 && pedal.reads == 1 && command.reads == 2 && notes.get_calls == 0,
            "inactive command rejection did not finish earlier queues before rejecting");
    command.values[1].value = 0.0;
    notes.values[1].polyPressure.pressure = std::numeric_limits<float>::quiet_NaN();
    gain.reads = pedal.reads = command.reads = notes.get_calls = 0;
    reject(host, before, remaining, "inactive late malformed pressure");
    require(gain.reads == 1 && pedal.reads == 1 && command.reads == 2 && notes.get_calls == 2,
            "inactive pressure rejection skipped staged commands or the preceding note");
    oracle.tail(host);
    audible_tail();

    // The first strike is legal only with the selected recall's empty owners.
    // The second conflicts before CC123: abort must preserve the live owners,
    // full history, and pending recall for the next inactive transaction.
    output_active(false);
    const InstrumentState pending{0.375F, true};
    recall(host.processor, pending);
    const ProcessorSnapshot pending_before(host.processor);
    command.values[0] = command.values[1] = {17, 0.0};
    pedal.count = 2;
    pedal.values[0] = {17, 1.0};
    pedal.values[1] = {50, 0.0};
    notes.count = 3;
    notes.values[0] = note_on(0, 7, 82, 0.5F, 3);
    notes.values[1] = pressure(1);
    notes.values[2] = note_on(2, 7, 83, 0.375F, 3);
    host.data.inputParameterChanges = &changes;
    host.data.inputEvents = &notes;
    notes.get_calls = 0;
    reject(host, pending_before, remaining, "inactive ownership abort with selected recall");
    require(notes.get_calls == 3, "inactive ownership abort did not complete event syntax");
    changes.count = 2;
    changes.values[0] = &command;
    changes.values[1] = &pedal;
    notes.count = 4;
    notes.values[1] = pressure(17);
    notes.values[2] = note_on(17, 7, 83, 0.375F, 3);
    notes.values[3] = pressure(127);
    oracle.install(pending);
    oracle.block(host, [&](std::size_t frame) {
        if (frame == 0) { oracle.strike(82, 0.5F); }
        if (frame == 17) {
            oracle.sustain(true); oracle.release(82); oracle.strike(83, 0.375F);
        }
        if (frame == 50) { oracle.sustain(false); }
    });
    require(host.processor.engine_processor().frame_cursor() == frames,
            "inactive accepted recall did not reset the cursor");
    audible_tail();

    // Leave every staging array populated again, then reset and strike afresh.
    // A stale later command would release that new holder in this same block.
    output_active(false);
    const ProcessorSnapshot lifecycle_before(host.processor);
    constexpr std::array<Owner, 1> recalled_owner{{{7, 83, 3, 0.375F}}};
    changes.count = 3;
    changes.values[0] = &gain;
    changes.values[1] = &pedal;
    changes.values[2] = &command;
    command.values[0] = {0, 0.0};
    command.values[1] = {64, 0.0};
    pedal.count = 1;
    pedal.values[0] = {0, 1.0};
    notes.count = 2;
    notes.values[0] = note_on(0, 301, 80, 0.25F, 3);
    notes.values[1] = pressure(127);
    notes.values[1].polyPressure.pressure = std::numeric_limits<float>::quiet_NaN();
    host.data.inputParameterChanges = &changes;
    host.data.inputEvents = &notes;
    reject(host, lifecycle_before, recalled_owner, "inactive rejection before lifecycle reset");
    require(host.processor.setProcessing(false) == sdk::kResultOk &&
            host.processor.setProcessing(true) == sdk::kResultOk, "command lifecycle reset failed");
    oracle.install(oracle.state);
    host.data.inputParameterChanges = nullptr;
    notes.count = 1;
    notes.values[0] = note_on(0, 501, 82, 0.5F, 3);
    oracle.block(host, [&](std::size_t frame) {
        if (frame == 0) { oracle.strike(82, 0.5F); }
    });
    audible_tail();
}

template<class Sample>
void run() {
    malformed_transactions_and_recall<Sample>();
    chronological_identity_reuse<Sample>();
    sixteen_channel_queue_order<Sample>();
    zero_frame_syntax_and_saved_state<Sample>();
    for (unsigned mask = 0; mask < 4; ++mask) { inactive_commands_and_recall<Sample>(mask); }
}
} // namespace

int main() {
    try {
        run<float>();
        run<double>();
        std::cout << kPluginName << ": command syntax/ownership rejection preserves live and pending state, "
                     "identities and full audio history; fresh recall, chronological reuse, "
                     "16-channel queue permutations, transient zero-frame flush and inactive "
                     "command/pressure recall/lifecycle crossproduct passed (both formats, four pointer masks).\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
