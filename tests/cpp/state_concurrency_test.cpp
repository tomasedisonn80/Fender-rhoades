#include "rhodes/instrument_state.hpp"

#include <array>
#include <barrier>
#include <cstddef>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>

namespace {
using rhodes::InstrumentState;
using rhodes::StateMailbox;

void require(bool condition, std::string_view message) {
    if (!condition) { throw std::runtime_error(std::string{message}); }
}

constexpr InstrumentState first{0.125F, true};
constexpr InstrumentState second{0.875F, false};
constexpr InstrumentState automation_a{0.375F, true};
constexpr InstrumentState automation_b{0.625F, false};
constexpr InstrumentState final_recall{0.9375F, true};
constexpr std::size_t iterations = 100000;

bool coherent(InstrumentState state) {
    constexpr std::array<InstrumentState, 6> allowed{
        InstrumentState{}, first, second, automation_a, automation_b, final_recall};
    for (const auto expected : allowed) {
        if (state == expected) { return true; }
    }
    return false;
}

// Barriers belong to the test harness, not to the mailbox or a real callback.
// The audio owner takes one token, the UI writes after that consume, and only
// then may the audio owner publish or abort. No thread retries an operation.
void ordered_race(bool abort, bool identical, bool write_new_recall) {
    StateMailbox mailbox;
    require(mailbox.request_recall(first), "initial recall rejected");
    const auto expected = identical || !write_new_recall ? first : second;
    std::barrier phase{2};
    struct Outcome {
        bool initial = false;
        bool publication = false;
        bool snapshot = false;
        bool pending = false;
        bool completed = false;
    } outcome;
    std::jthread audio([&] {
        const auto token = mailbox.begin_block();
        outcome.initial = token.apply_recall && token.may_publish && token.recalled == first;
        phase.arrive_and_wait();
        phase.arrive_and_wait();
        if (abort) {
            mailbox.abort_block(token);
            outcome.publication = true;
        } else {
            outcome.publication = !mailbox.end_block(token, automation_a);
        }
        outcome.snapshot = mailbox.snapshot() == expected;
        const auto pending = mailbox.begin_block();
        outcome.pending = pending.apply_recall && pending.may_publish && pending.recalled == expected;
        outcome.completed = mailbox.end_block(pending, expected) && !mailbox.begin_block().apply_recall;
    });
    phase.arrive_and_wait();
    const bool accepted = !write_new_recall || mailbox.request_recall(expected);
    phase.arrive_and_wait();
    audio.join();
    require(accepted && outcome.initial, "ordered race setup failed");
    require(outcome.publication, "audio publication overwrote a later UI recall");
    require(outcome.snapshot && outcome.pending, "abort/publication lost the pending recall");
    require(outcome.completed, "ordered recall was not consumed exactly once");
}

void ordered_races() {
    ordered_race(false, false, true);
    ordered_race(false, true, true);
    ordered_race(true, false, true);
    ordered_race(true, true, true);
    ordered_race(true, false, false);
}

void concurrent_stress() {
    StateMailbox mailbox;
    std::barrier phase{3};
    // Each outcome is written by one thread and read only after join.
    struct AudioOutcome {
        std::size_t invalid = 0;
        std::size_t consumed = 0;
        std::size_t published = 0;
        bool final_protected = false;
        bool final_consumed = false;
    } audio_outcome;
    std::size_t invalid_observations = 0;
    std::size_t rejected_ui_writes = 0;
    std::jthread audio([&] {
        phase.arrive_and_wait();
        for (std::size_t i = 0; i < iterations; ++i) {
            const auto token = mailbox.begin_block();
            if (token.apply_recall) {
                ++audio_outcome.consumed;
                if (!token.may_publish || !coherent(token.recalled)) { ++audio_outcome.invalid; }
            }
            if (i % 7 == 0) {
                mailbox.abort_block(token);
            } else if (mailbox.end_block(token, i % 2 == 0 ? automation_a : automation_b)) {
                ++audio_outcome.published;
            }
            if (!coherent(mailbox.snapshot())) { ++audio_outcome.invalid; }
        }
        const auto old = mailbox.begin_block();
        phase.arrive_and_wait();
        phase.arrive_and_wait();
        audio_outcome.final_protected = !mailbox.end_block(old, automation_a) &&
                                        mailbox.snapshot() == final_recall;
        const auto pending = mailbox.begin_block();
        audio_outcome.final_consumed = pending.apply_recall && pending.may_publish &&
            pending.recalled == final_recall && mailbox.end_block(pending, pending.recalled) &&
            !mailbox.begin_block().apply_recall;
    });
    std::jthread observer([&] {
        phase.arrive_and_wait();
        for (std::size_t i = 0; i < iterations; ++i) {
            if (!coherent(mailbox.snapshot())) { ++invalid_observations; }
        }
        phase.arrive_and_wait();
        phase.arrive_and_wait();
        if (mailbox.snapshot() != final_recall) { ++invalid_observations; }
    });
    phase.arrive_and_wait();
    for (std::size_t i = 0; i < iterations; ++i) {
        if (!mailbox.request_recall(i % 2 == 0 ? first : second)) { ++rejected_ui_writes; }
    }
    // The final write is deliberately after the audio owner's last token and
    // after all stress writers have quiesced. It therefore must stay pending.
    phase.arrive_and_wait();
    const bool final_accepted = mailbox.request_recall(final_recall);
    phase.arrive_and_wait();
    audio.join();
    observer.join();
    require(rejected_ui_writes == 0 && final_accepted, "valid concurrent UI recall rejected");
    require(audio_outcome.invalid == 0 && invalid_observations == 0,
            "snapshot or consumed recall combined fields from different publications");
    require(audio_outcome.final_protected && audio_outcome.final_consumed,
            "final pending recall was overwritten or not consumed exactly once");
    std::cout << "stress iterations=" << iterations << " consumed=" << audio_outcome.consumed
              << " publications=" << audio_outcome.published << '\n';
}
} // namespace

int main(int argc, char** argv) {
    try {
        const std::string_view selected = argc > 1 ? argv[1] : "all";
        bool found = false;
        const auto run = [&](std::string_view name, auto test) {
            if (selected == "all" || selected == name) {
                test();
                found = true;
                std::cout << "PASS " << name << '\n';
            }
        };
        run("ordered_races", ordered_races);
        run("concurrent_stress", concurrent_stress);
        require(found, "unknown concurrency fixture");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}
