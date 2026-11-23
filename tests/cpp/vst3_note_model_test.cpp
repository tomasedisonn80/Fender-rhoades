#include "tests/cpp/vst3_test_support.hpp"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace {
using namespace rhodes;
using namespace rhodes::vst3;
using namespace rhodes::vst3::test_host;
// Share only the selected scalar audio model; ownership, admission and physical
// action scheduling remain independent of the production note tracker.
using ScalarEngine = std::remove_cvref_t<
    decltype(std::declval<const HostEventProcessor&>().engine())>;
#if defined(RHODES_QUADRATURE_VST3)
static_assert(kQuadratureAudio && !kFilteredAudio && kStateModel == 3);
static_assert(std::is_same_v<ScalarEngine, QuadratureEngine>);
#elif defined(RHODES_FILTERED_VST3)
static_assert(kFilteredAudio && kStateModel == 2);
static_assert(std::is_same_v<ScalarEngine, AudioEngine>);
#else
static_assert(!kFilteredAudio && kStateModel == 1);
static_assert(std::is_same_v<ScalarEngine, ResearchEngine>);
#endif
constexpr std::size_t frames = 32;
static_assert(key_count == 73 && maximum_block_events == 256);

struct Counts {
    std::size_t calls = 0;
    std::size_t accepted = 0;
    std::size_t rejected = 0;
    std::size_t checks = 0;
    std::size_t physical_actions = 0;
} counts;

void check(bool condition, std::string_view message) {
    ++counts.checks;
    if (!condition) { throw std::runtime_error(std::string{message}); }
}

struct Message {
    bool on;
    int offset;
    std::int32_t id;
    int pitch;
    int channel = 0;
    float velocity = 0.5F;
};

Message on(int offset, std::int32_t id, int pitch, int channel = 0, float velocity = 0.5F) {
    return {true, offset, id, pitch, channel, velocity};
}
Message off(int offset, std::int32_t id, int pitch, int channel = 0) {
    return {false, offset, id, pitch, channel, 0.0F};
}

struct Action {
    int offset;
    bool on;
    std::size_t key;
    double velocity;
};

// Independent ownership representation: one associative identity map plus a
// multiset of anonymous channel/pitch owners. No production NoteTracker is used
// to decide admission, count holders, or derive physical actions.
struct Model {
    std::map<std::pair<int, std::int32_t>, int> identities;
    std::multiset<std::pair<int, int>> anonymous;

    std::size_t size() const { return identities.size() + anonymous.size(); }
    std::size_t holders(int pitch) const {
        std::size_t total = 0;
        for (const auto& [identity, held_pitch] : identities) {
            (void)identity;
            if (held_pitch == pitch) { ++total; }
        }
        for (const auto& holder : anonymous) {
            if (holder.second == pitch) { ++total; }
        }
        return total;
    }

    bool apply(const Message& message, std::vector<Action>& actions) {
        if (message.channel < 0 || message.channel >= 16 ||
            message.pitch < 0 || message.pitch > 127 ||
            !std::isfinite(message.velocity) || message.velocity < 0.0F ||
            message.velocity > 1.0F) {
            return false;
        }
        if (message.pitch < 28 || message.pitch > 100) { return true; }
        const auto key = static_cast<std::size_t>(message.pitch - 28);
        const bool strike = message.on && message.velocity != 0.0F;
        bool removed = false;
        if (message.id == -1) {
            const auto owner = std::pair{message.channel, message.pitch};
            if (strike) {
                if (size() == 256) { return false; }
                anonymous.insert(owner);
            } else if (const auto found = anonymous.find(owner); found != anonymous.end()) {
                anonymous.erase(found);
                removed = true;
            }
        } else {
            const auto identity = std::pair{message.channel, message.id};
            const auto found = identities.find(identity);
            if (found != identities.end() && found->second != message.pitch) { return false; }
            if (strike && found == identities.end()) {
                if (size() == 256) { return false; }
                identities.emplace(identity, message.pitch);
            } else if (!strike && found != identities.end()) {
                identities.erase(found);
                removed = true;
            }
        }
        if (strike) {
            actions.push_back({message.offset, true, key, static_cast<double>(message.velocity)});
        } else if (removed && holders(message.pitch) == 0) {
            actions.push_back({message.offset, false, key, 0.0});
        }
        return true;
    }

    std::vector<Message> releases() const {
        std::vector<Message> result;
        for (const auto& [identity, pitch] : identities) {
            result.push_back(off(0, identity.second, pitch, identity.first));
        }
        for (const auto& [channel, pitch] : anonymous) {
            result.push_back(off(0, -1, pitch, channel));
        }
        return result;
    }
};

template<class Sample>
struct Fixture {
    ProcessorHost<Sample, frames> host;
    ScalarEngine reference;
    EventList events;
    Model model;
    InstrumentState live{};
    std::optional<InstrumentState> pending;
    std::uint64_t cursor = 0;

    explicit Fixture(double rate) : host(rate) {
        check(reference.prepare(rate), "reference prepare failed");
    }

    void recall(InstrumentState state) {
        MemoryStream stream;
        stream.load(state, 0, kStateModel);
        check(host.processor.setState(&stream) == sdk::kResultOk, "valid recall rejected");
        pending = state;
        state_matches();
    }

    template<class Engine>
    void mechanics_match(const Engine& actual, const Engine& expected) {
        check(actual.sample_rate_hz() == expected.sample_rate_hz(), "sample rate mismatch");
        check(actual.mechanical_energy_joules() == expected.mechanical_energy_joules(),
              "mechanical energy mismatch");
        for (std::size_t key = 0; key < key_count; ++key) {
            for (std::size_t mode = 0; mode < modes_per_key; ++mode) {
                const auto a = actual.modal_state(key, mode);
                const auto b = expected.modal_state(key, mode);
                check(a && b && a->displacement_m == b->displacement_m &&
                      a->velocity_m_per_s == b->velocity_m_per_s, "modal state mismatch");
            }
        }
    }

    void state_matches() {
        check(!host.processor.requires_reset(), "unexpected arithmetic fault");
        check(host.processor.engine_processor().frame_cursor() == cursor, "cursor mismatch");
        check(host.processor.current_state() == live, "live configuration mismatch");
        check(host.processor.published_state() == pending.value_or(live),
              "published/pending configuration mismatch");
        check(host.processor.note_tracker().active_count() == model.size(), "owner count mismatch");
        for (std::size_t key = 0; key < key_count; ++key) {
            check(host.processor.note_tracker().held_count(key) ==
                  model.holders(static_cast<int>(key) + 28), "per-key owner count mismatch");
        }
        mechanics_match(host.processor.engine_processor().engine(), reference);
    }

    bool run(const std::vector<Message>& input, std::optional<bool> declared = std::nullopt) {
        Model candidate = pending ? Model{} : model;
        std::vector<Action> actions;
        bool allowed = input.size() <= 256;
        int previous_offset = -1;
        for (const auto& message : input) {
            if (!allowed) { break; }
            if (message.offset < 0 || message.offset >= static_cast<int>(frames) ||
                message.offset < previous_offset) {
                allowed = false;
                break;
            }
            previous_offset = message.offset;
            allowed = candidate.apply(message, actions);
        }
        if (declared) { check(allowed == *declared, "hand-declared model outcome disagrees"); }

        events.count = static_cast<sdk::int32>(input.size());
        events.failed_index = -1;
        events.get_calls = 0;
        for (std::size_t index = 0; index < std::min(input.size(), events.values.size()); ++index) {
            const auto& message = input[index];
            auto event = message.on ?
                note_on(message.offset, message.id, static_cast<sdk::int16>(message.pitch),
                        message.velocity, static_cast<sdk::int16>(message.channel)) :
                note_off(message.offset, message.id, static_cast<sdk::int16>(message.pitch),
                         static_cast<sdk::int16>(message.channel));
            if (!message.on) { event.noteOff.velocity = message.velocity; }
            events.values[index] = event;
        }
        host.data.inputEvents = &events;
        host.data.inputParameterChanges = nullptr;
        host.fill(Sample{7});
        ++counts.calls;
        const auto result = host.processor.process(host.data);
        check((result == sdk::kResultOk) == allowed, "host admission differs from map model");
        if (!allowed) {
            ++counts.rejected;
            for (std::size_t frame = 0; frame < frames; ++frame) {
                check(host.left[frame] == Sample{0} && host.right[frame] == Sample{0},
                      "rejected note block did not silence both channels");
            }
            check(host.output.silenceFlags == 3, "rejected block silence flags");
            state_matches();
            // Inspect future trajectories too: a wrong hidden held/damping flag
            // can preserve the current q/v while changing the next samples.
            auto observed = std::make_unique<ScalarEngine>(host.processor.engine_processor().engine());
            auto expected = std::make_unique<ScalarEngine>(reference);
            std::array<double, 17> a{}, b{};
            check(observed->process(a) && expected->process(b) && a == b,
                  "rejection changed future mechanical trajectory");
            return false;
        }

        ++counts.accepted;
        if (pending) {
            reference.reset();
            live = *pending;
            reference.set_sustain(live.sustain);
            cursor = 0;
            pending.reset();
        }
        model = std::move(candidate);
        counts.physical_actions += actions.size();
        std::size_t action_index = 0;
        bool any_nonzero = false;
        for (std::size_t frame = 0; frame < frames; ++frame) {
            while (action_index < actions.size() &&
                   actions[action_index].offset == static_cast<int>(frame)) {
                const auto& action = actions[action_index++];
                check(action.on ? reference.note_on(action.key, action.velocity) :
                                  reference.note_off(action.key), "reference physical action failed");
            }
            double volts = 0.0;
            check(reference.process(std::span<double>(&volts, 1)), "reference sample failed");
            const auto expected = static_cast<Sample>(
                (32.0 * static_cast<double>(live.monitoring_gain)) * volts);
            // Identical mechanical engine and arithmetic order: zero numerical
            // tolerance isolates ownership/order/offset errors in the adapter.
            check(std::isfinite(host.left[frame]) && host.left[frame] == expected &&
                  host.right[frame] == expected, "host audio differs from independent note model");
            any_nonzero = any_nonzero || expected != Sample{0};
        }
        check(action_index == actions.size(), "reference action was not dispatched");
        check(host.output.silenceFlags == (any_nonzero ? 0U : 3U), "accepted block silence flags");
        cursor += frames;
        state_matches();
        return true;
    }
};

template<class Sample>
void identity_trace() {
    for (const double rate : {32000.0, 192000.0}) {
        auto f = std::make_unique<Fixture<Sample>>(rate);
        (void)f->run({
            on(0, 7, 28, 0), on(0, 7, 28, 1, 0.25F), on(2, 7, 28, 0, 0.75F),
            on(4, -1, 28), on(4, -1, 28, 0, 0.25F),
            on(6, std::numeric_limits<std::int32_t>::min(), 100, 15, 0.375F),
            on(7, std::numeric_limits<std::int32_t>::max(), 100, 15, 0.125F),
            off(8, 7, 28), on(9, -1, 28, 0, 0.0F), off(10, 7, 27),
            on(11, 7, 101, 1), off(12, -1, 28), off(15, 7, 28, 1),
            off(17, std::numeric_limits<std::int32_t>::min(), 100, 15),
            off(31, std::numeric_limits<std::int32_t>::max(), 100, 15)}, true);
        check(f->model.size() == 0, "identity trace did not release every owner");
        (void)f->run({on(0, -2, 68, 4), off(0, -2, 68, 4), on(0, -2, 68, 4, 0.25F),
                      on(1, -2, 0, 4), off(2, -2, 127, 4)}, true);
        (void)f->run({on(0, 8, 60), on(31, -2, 69, 4)}, false);
        (void)f->run({on(0, 8, 60), off(31, -2, 69, 4)}, false);
        (void)f->run({on(0, -2, 68, 4, -0.0F)}, true);
        (void)f->run({}, true);
    }
}

template<class Sample>
void capacity_reuse() {
    auto f = std::make_unique<Fixture<Sample>>(48000.0);
    std::vector<Message> initial;
    for (int index = 0; index < 256; ++index) {
        initial.push_back(on(0, index % 5 == 0 ? -1 : 1000 + index,
                             28 + index % 73, index % 16, 0.125F));
    }
    (void)f->run(initial, true);
    check(f->model.size() == 256, "capacity fixture is not full");
    (void)f->run({on(0, 1001, 29, 1, 0.25F), on(31, 1007, 101, 7)}, true);
    (void)f->run({on(31, -1, 61, 3)}, false);
    (void)f->run({off(0, 1003, 31, 3), on(1, 9000, 61, 3),
                  on(31, -1, 31, 3)}, false);
    (void)f->run(std::vector<Message>(257, on(0, 1001, 29, 1)), false);
    std::vector<Message> half;
    for (std::size_t index = 0; index < 128; ++index) {
        const auto& message = initial[index];
        half.push_back(off(0, message.id, message.pitch, message.channel));
    }
    (void)f->run(half, true);
    check(f->model.size() == 128, "half release count");
    std::vector<Message> refill;
    for (int index = 0; index < 128; ++index) {
        refill.push_back(on(0, 20000 + index, 28 + (7 * index) % 73, 15 - index % 16, 0.25F));
    }
    (void)f->run(refill, true);
    check(f->model.size() == 256, "refill count");
    (void)f->run(f->model.releases(), true);
    check(f->model.size() == 0, "full release leaked owners");
    (void)f->run({}, true);
}

template<class Sample>
void recall_rejection() {
    for (const double rate : {32000.0, 192000.0}) {
        auto f = std::make_unique<Fixture<Sample>>(rate);
        (void)f->run({on(0, 7, 68, 2), on(5, -1, 70, 3, 0.25F)}, true);
        f->recall({0.25F, true});
        (void)f->run({on(0, 9, 60, 4), on(31, 9, 61, 4)}, false);
        (void)f->run({on(0, 7, 69, 2),
                      on(31, 99, 60, 0, std::numeric_limits<float>::quiet_NaN())}, false);
        (void)f->run({on(0, 7, 69, 2), off(31, -1, 70, 3)}, true);
        (void)f->run({off(0, 7, 69, 2)}, true);
        f->recall({0.75F, false});
        f->recall({0.125F, true});
        (void)f->run({on(0, 31, 72), on(31, 32, 128)}, false);
        (void)f->run({on(0, 7, 72, 2, 0.375F)}, true);
        (void)f->run({off(31, 7, 72, 2)}, true);
        (void)f->run({}, true);
    }
}

struct Random {
    std::uint32_t value;
    std::uint32_t next() {
        value ^= value << 13U;
        value ^= value >> 17U;
        value ^= value << 5U;
        return value;
    }
};

template<class Sample>
void random_model() {
    // Retain all four deterministic traces while exercising the 4x/2x/1x
    // filtered-rate branches without multiplying the randomized workload.
    constexpr std::array<std::pair<std::uint32_t, double>, 4> scenarios{{
        {0x74a01131U, 32000.0}, {0x9826ab03U, 192000.0},
        {0x164312a1U, 48000.0}, {0x83ad18f5U, 96000.0}}};
    for (const auto& [seed, rate] : scenarios) {
        Random random{seed};
        auto f = std::make_unique<Fixture<Sample>>(rate);
        for (int block = 0; block < 180; ++block) {
            if (block % 31 == 0) {
                const float gain = (block % 2 == 0) ? 0.25F : 0.75F;
                f->recall({gain, (random.next() & 1U) != 0U});
            }
            std::vector<Message> messages;
            const auto count = static_cast<int>(random.next() % 13U);
            for (int index = 0; index < count; ++index) {
                const int channel = static_cast<int>(random.next() % 16U);
                const auto id = static_cast<std::int32_t>(random.next() % 64U) - 16;
                const int pitch = 28 + ((static_cast<int>(id) + 16) * 11 + channel * 7) % 73;
                const int offset = count > 1 ? (index * 29) / (count - 1) : 0;
                const bool strike = random.next() % 3U != 0U;
                const auto scaled = static_cast<float>(random.next() % 8U + 1U) / 8.0F;
                auto message = strike ? on(offset, id, pitch, channel, scaled) :
                                        off(offset, id, pitch, channel);
                if (random.next() % 11U == 0U) { message.pitch = (index & 1) != 0 ? 27 : 101; }
                if (random.next() % 9U == 0U) { message.velocity = 0.0F; }
                messages.push_back(message);
            }
            std::optional<bool> declared;
            if (block % 13 == 5) {
                messages.push_back(on(30, 100000 + block, 60, 5));
                messages.push_back(off(31, 100000 + block, 61, 5));
                declared = false;
            } else if (block % 23 == 7) {
                messages.push_back(on(31, 7, 60, 16));
                declared = false;
            }
            (void)f->run(messages, declared);
        }
        (void)f->run(f->model.releases(), true);
        (void)f->run({}, true);
        check(f->model.size() == 0, "random trace final ownership leak");
    }
}

using Test = void (*)();
void identities() { identity_trace<float>(); identity_trace<double>(); }
void capacity() { capacity_reuse<float>(); capacity_reuse<double>(); }
void recalls() { recall_rejection<float>(); recall_rejection<double>(); }
void randomized() { random_model<float>(); random_model<double>(); }
constexpr std::array<std::pair<std::string_view, Test>, 4> tests{{
    {"identity_model", identities}, {"capacity_model", capacity},
    {"recall_model", recalls}, {"random_note_model", randomized}}};
} // namespace

int main(int argc, char** argv) {
    const std::string_view selected = argc > 1 ? argv[1] : "all";
    bool found = false;
    try {
        for (const auto& [name, test] : tests) {
            if (selected != "all" && selected != name) { continue; }
            found = true;
            counts = {};
            test();
            std::cout << "PASS " << name << ": " << counts.calls << " processor calls, "
                      << counts.accepted << " accepted, " << counts.rejected << " rejected, "
                      << counts.physical_actions << " physical actions, "
                      << counts.checks << " explicit checks\n";
        }
        if (!found) { throw std::runtime_error("Unknown note-model selector"); }
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << selected << ": " << error.what() << '\n';
        return 1;
    }
    return 0;
}
