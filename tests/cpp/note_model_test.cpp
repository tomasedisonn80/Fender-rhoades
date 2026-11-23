#include "rhodes/note_tracker.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <iterator>
#include <limits>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {
using rhodes::MidiNote;
using rhodes::NoteTracker;

void require(bool condition, std::string_view message) {
    if (!condition) { throw std::runtime_error(std::string{message}); }
}

enum class Decision { accept, invalid, full };
struct Change {
    bool strike;
    int pitch;
    double velocity;
};
struct Outcome {
    Decision decision = Decision::accept;
    std::optional<Change> change;
};

// A deliberately different representation from the candidate's fixed holder
// slots: unique identities map to pitches, anonymous pairs store multiplicity.
// Counts are derived from this ownership relation, never maintained in parallel.
struct Model {
    std::map<std::pair<int, std::int32_t>, int> identified;
    std::map<std::pair<int, int>, std::size_t> anonymous;

    [[nodiscard]] std::array<std::size_t, 73> counts() const {
        std::array<std::size_t, 73> result{};
        for (const auto& [identity, pitch] : identified) {
            (void)identity;
            ++result[static_cast<std::size_t>(pitch - 28)];
        }
        for (const auto& [pair, amount] : anonymous) {
            result[static_cast<std::size_t>(pair.second - 28)] += amount;
        }
        return result;
    }

    [[nodiscard]] std::size_t size() const {
        std::size_t result = identified.size();
        for (const auto& [pair, amount] : anonymous) {
            (void)pair;
            result += amount;
        }
        return result;
    }

    [[nodiscard]] std::vector<MidiNote> owners() const {
        std::vector<MidiNote> result;
        for (const auto& [identity, pitch] : identified) {
            result.push_back({false, identity.first, pitch, identity.second, 0.0});
        }
        for (const auto& [pair, amount] : anonymous) {
            for (std::size_t i = 0; i < amount; ++i) {
                result.push_back({false, pair.first, pair.second, -1, 0.0});
            }
        }
        return result;
    }

    Outcome apply(const MidiNote& input) {
        if (input.channel < 0 || input.channel >= 16 || input.pitch < 0 ||
            input.pitch >= 128 || !std::isfinite(input.velocity) ||
            input.velocity < 0.0 || input.velocity > 1.0) {
            return {Decision::invalid, {}};
        }
        if (input.pitch < 28 || input.pitch > 100) { return {}; }
        const bool strike = input.on && input.velocity != 0.0;
        const auto identity = std::pair{input.channel, input.note_id};
        const auto pair = std::pair{input.channel, input.pitch};
        if (input.note_id != -1) {
            const auto existing = identified.find(identity);
            if (existing != identified.end() && existing->second != input.pitch) {
                return {Decision::invalid, {}};
            }
            if (strike) {
                if (existing == identified.end()) {
                    if (size() == 256) { return {Decision::full, {}}; }
                    identified.emplace(identity, input.pitch);
                }
                return {Decision::accept, Change{true, input.pitch, input.velocity}};
            }
            if (existing == identified.end()) { return {}; }
            identified.erase(existing);
        } else if (strike) {
            if (size() == 256) { return {Decision::full, {}}; }
            ++anonymous[pair];
            return {Decision::accept, Change{true, input.pitch, input.velocity}};
        } else {
            const auto existing = anonymous.find(pair);
            if (existing == anonymous.end()) { return {}; }
            if (existing->second == 1) { anonymous.erase(existing); }
            else { --existing->second; }
        }
        if (counts()[static_cast<std::size_t>(input.pitch - 28)] == 0) {
            return {Decision::accept, Change{false, input.pitch, 0.0}};
        }
        return {};
    }

    std::vector<std::size_t> release_channel(int channel) {
        const auto before = counts();
        std::erase_if(identified, [channel](const auto& entry) {
            return entry.first.first == channel;
        });
        std::erase_if(anonymous, [channel](const auto& entry) {
            return entry.first.first == channel;
        });
        const auto after = counts();
        std::vector<std::size_t> released;
        for (std::size_t key = 0; key < before.size(); ++key) {
            if (before[key] != 0 && after[key] == 0) { released.push_back(key); }
        }
        return released;
    }
};

void same_result(const rhodes::NoteResult& actual, const rhodes::NoteResult& other) {
    require(actual.status == other.status && actual.change.has_value() == other.change.has_value(),
            "rejected operation changed owner-draining continuation");
    if (actual.change) {
        require(actual.change->type == other.change->type &&
                actual.change->key == other.change->key &&
                actual.change->value == other.change->value,
                "rejected operation changed a future physical transition");
    }
}

class Comparison {
public:
    Model model;
    NoteTracker candidate;
    std::size_t operations = 0;
    std::size_t rejections = 0;
    std::size_t strikes = 0;
    std::size_t releases = 0;

    void counts_match() const {
        const auto expected = model.counts();
        require(candidate.active_count() == model.size(), "model total-holder disagreement");
        for (std::size_t key = 0; key < expected.size(); ++key) {
            require(candidate.held_count(key) == expected[key], "model per-key disagreement");
        }
        require(candidate.held_count(73) == 0 &&
                candidate.held_count(std::numeric_limits<std::size_t>::max()) == 0,
                "out-of-range count query is not zero");
    }

    void unchanged_identities(const NoteTracker& before) const {
        // Counts alone cannot expose a hidden identity swap. Drain every owner
        // through copies and compare all emitted releases with the pre-call copy.
        NoteTracker after_copy = candidate;
        NoteTracker before_copy = before;
        for (const auto& note : model.owners()) {
            same_result(after_copy.apply(note), before_copy.apply(note));
        }
        require(after_copy.active_count() == 0 && before_copy.active_count() == 0,
                "rejection changed an identity despite matching initial counts");
    }

    Outcome note(const MidiNote& input) {
        const NoteTracker before = candidate;
        const auto expected = model.apply(input);
        const auto actual = candidate.apply(input);
        ++operations;
        const auto status = expected.decision == Decision::accept ? rhodes::NoteStatus::ok :
            expected.decision == Decision::full ? rhodes::NoteStatus::capacity_exceeded :
                                                 rhodes::NoteStatus::invalid_input;
        require(actual.status == status && actual.succeeded() == (expected.decision == Decision::accept),
                "model status disagreement");
        require(actual.change.has_value() == expected.change.has_value(), "model transition presence disagreement");
        if (expected.change) {
            const auto& change = *expected.change;
            require(actual.change->type == (change.strike ? rhodes::EventType::note_on :
                                                           rhodes::EventType::note_off) &&
                    actual.change->key == static_cast<std::size_t>(change.pitch - 28) &&
                    actual.change->value == change.velocity, "model physical transition disagreement");
            if (change.strike) { ++strikes; } else { ++releases; }
        }
        counts_match();
        if (expected.decision != Decision::accept) {
            ++rejections;
            unchanged_identities(before);
        }
        return expected;
    }

    void channel(int channel) {
        const NoteTracker before = candidate;
        const bool valid = channel >= 0 && channel < 16;
        const auto expected = valid ? model.release_channel(channel) : std::vector<std::size_t>{};
        const auto actual = candidate.release_channel(channel);
        ++operations;
        require(actual.status == (valid ? rhodes::NoteStatus::ok : rhodes::NoteStatus::invalid_input) &&
                actual.succeeded() == valid && actual.count == expected.size(),
                "model channel-release status/count disagreement");
        for (std::size_t i = 0; i < expected.size(); ++i) {
            require(actual.keys[i] == expected[i], "channel releases are not ascending/newly unowned keys");
        }
        counts_match();
        if (!valid) { ++rejections; unchanged_identities(before); }
    }

    void reset() {
        model = {};
        candidate.reset();
        ++operations;
        counts_match();
    }
};

void boundary_model() {
    Comparison test;
    constexpr std::array<std::int32_t, 6> ids{
        std::numeric_limits<std::int32_t>::min(), -10000, -2, 0, 19,
        std::numeric_limits<std::int32_t>::max()};
    for (int channel = 0; channel < 16; ++channel) {
        for (const auto id : ids) {
            (void)test.note({true, channel, 28, id, 0.5});
            (void)test.note({true, channel, 28, id, 0.75});
            require(test.note({true, channel, 100, id, 0.25}).decision == Decision::invalid,
                    "different playable pitch did not conflict");
            require(test.note({false, channel, 100, id, 0.0}).decision == Decision::invalid,
                    "conflicting off did not reject");
            for (const int pitch : {0, 27, 101, 127}) {
                (void)test.note({true, channel, pitch, id, 1.0});
                (void)test.note({false, channel, pitch, id, 0.0});
            }
        }
        (void)test.note({true, channel, 100, -1, 1.0});
        (void)test.note({true, channel, 100, -1, 0.25});
        (void)test.note({true, channel, 100, -1, -0.0});
        (void)test.note({false, channel, 100, 123456, 0.0});
        (void)test.note({false, channel, 28, -1, 0.0});
    }
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double infinity = std::numeric_limits<double>::infinity();
    for (const double velocity : {nan, infinity, -infinity, -0.01, 1.01}) {
        for (const bool on : {false, true}) {
            (void)test.note({on, 0, 28, -2, velocity});
            (void)test.note({on, 0, 27, -2, velocity});
        }
    }
    for (const int channel : {-1, 16}) {
        (void)test.note({true, channel, 28, 0, 1.0});
        test.channel(channel);
    }
    for (const int pitch : {-1, 128}) { (void)test.note({false, 0, pitch, 0, 0.0}); }
    for (int channel = 15; channel >= 0; --channel) { test.channel(channel); }
    require(test.candidate.active_count() == 0 && test.strikes > 0 && test.rejections > 0,
            "boundary scenario failed to exercise or drain ownership");
    (void)test.note({true, 0, 60, -2, 0.5});
    test.reset();
    (void)test.note({false, 0, 60, -2, 0.0});
    std::cout << "boundary operations=" << test.operations << " rejections=" << test.rejections << '\n';
}

void capacity_model() {
    Comparison test;
    for (int index = 0; index < 256; ++index) {
        (void)test.note({true, index % 16, 28 + index % 73, index, 0.5});
    }
    require(test.model.size() == 256, "identified capacity was not reached");
    require(test.note({true, 0, 60, 1000, 0.5}).decision == Decision::full, "257th owner accepted");
    require(test.note({true, 0, 60, -1, 0.5}).decision == Decision::full, "anonymous owner exceeded capacity");
    (void)test.note({true, 0, 28, 0, 0.75});
    (void)test.note({true, 0, 127, 0, 0.75});
    (void)test.note({true, 0, 29, 0, 0.75});
    (void)test.note({true, 0, 28, 0, 0.0});
    (void)test.note({true, 0, 100, -2, 0.5});
    require(test.model.size() == 256, "released slot was not reused");
    for (int channel = 0; channel < 16; ++channel) { test.channel(channel); }
    for (int index = 0; index < 256; ++index) {
        (void)test.note({true, index % 16, 28 + index % 73, -1, 0.5});
    }
    require(test.model.size() == 256, "anonymous capacity was not reached");
    (void)test.note({true, 0, 28, -1, 1.0});
    (void)test.note({true, 0, 28, 12345, 1.0});
    (void)test.note({false, 0, 28, 12345, 0.0});
    (void)test.note({true, 0, 28, -1, 0.0});
    (void)test.note({true, 0, 28, 12345, 1.0});
    for (const auto& owner : test.model.owners()) { (void)test.note(owner); }
    require(test.candidate.active_count() == 0 && test.rejections >= 5,
            "capacity scenario did not reject and drain owners");
    test.reset();
    std::cout << "capacity operations=" << test.operations << " rejections=" << test.rejections << '\n';
}

struct Random {
    std::uint64_t state = 0x946a70cbe129f35dULL;
    std::uint32_t next() {
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        return static_cast<std::uint32_t>(state >> 32U);
    }
    std::uint32_t below(std::uint32_t bound) { return next() % bound; }
};

void random_model() {
    Comparison test;
    Random random;
    std::array<bool, 16> channels{};
    constexpr std::array<int, 8> edges{-1, 0, 27, 28, 100, 101, 127, 128};
    constexpr std::array<std::int32_t, 6> special_ids{
        -1, -1, -2, -10000, std::numeric_limits<std::int32_t>::min(),
        std::numeric_limits<std::int32_t>::max()};
    constexpr std::array<double, 6> velocities{0.0, -0.0, 0.25, 0.5, 0.75, 1.0};
    for (std::size_t operation = 0; operation < 12000; ++operation) {
        const auto action = random.below(100);
        if (action == 0) { test.reset(); continue; }
        if (action < 5) { test.channel(static_cast<int>(random.below(18)) - 1); continue; }
        MidiNote note{random.below(100) < 65, static_cast<int>(random.below(16)),
            28 + static_cast<int>(random.below(73)),
            static_cast<std::int32_t>(random.below(32)), 0.5};
        const auto owners = test.model.owners();
        if (!owners.empty() && random.below(3) == 0) {
            note = owners[static_cast<std::size_t>(random.next()) % owners.size()];
            note.on = random.below(2) != 0;
            note.velocity = 0.5;
            if (random.below(5) == 0) { note.pitch = note.pitch == 100 ? 28 : note.pitch + 1; }
        } else if (random.below(3) == 0) {
            note.note_id = special_ids[random.below(static_cast<std::uint32_t>(special_ids.size()))];
        }
        if (random.below(7) == 0) { note.pitch = edges[random.below(static_cast<std::uint32_t>(edges.size()))]; }
        note.velocity = velocities[random.below(static_cast<std::uint32_t>(velocities.size()))];
        if (random.below(31) == 0) { note.channel = random.below(2) == 0 ? -1 : 16; }
        if (random.below(31) == 0) {
            constexpr std::array<double, 4> invalid{
                -0.25, 1.25, std::numeric_limits<double>::quiet_NaN(),
                std::numeric_limits<double>::infinity()};
            note.velocity = invalid[random.below(static_cast<std::uint32_t>(invalid.size()))];
        }
        if (note.channel >= 0 && note.channel < 16) { channels[static_cast<std::size_t>(note.channel)] = true; }
        (void)test.note(note);
    }
    require(test.operations == 12000 && test.rejections > 500 && test.strikes > 1000 && test.releases > 100,
            "fixed random stream did not exercise required outcomes");
    require(std::all_of(channels.begin(), channels.end(), [](bool seen) { return seen; }),
            "random stream missed a channel");
    for (int channel = 0; channel < 16; ++channel) { test.channel(channel); }
    require(test.candidate.active_count() == 0, "random stream left owners after complete channel drain");
    std::cout << "random operations=" << test.operations << " rejections=" << test.rejections
              << " strikes=" << test.strikes << " releases=" << test.releases << '\n';
}

} // namespace

int main(int argc, char** argv) {
    const std::string_view selected = argc > 1 ? argv[1] : "all";
    using Test = void (*)();
    const std::array<std::pair<std::string_view, Test>, 3> tests{{
        {"boundary_model", boundary_model}, {"capacity_model", capacity_model},
        {"random_model", random_model}
    }};
    bool found = false;
    try {
        for (const auto& [name, test] : tests) {
            if (selected != "all" && selected != name) { continue; }
            found = true;
            test();
            std::cout << "PASS " << name << '\n';
        }
        require(found, "unknown test selector");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << selected << ": " << error.what() << '\n';
        return 1;
    }
}
