#include "rhodes/editor_audition.hpp"

#include <algorithm>
#include <array>
#include <barrier>
#include <bit>
#include <cfenv>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace {
using Command = rhodes::EditorAuditionCommand;
using Queue = rhodes::EditorAuditionQueue;

static_assert(Queue::capacity == 256);
static_assert(std::is_trivially_copyable_v<Command>);
static_assert(std::is_trivially_copyable_v<Queue::Snapshot>);
static_assert(!std::is_copy_constructible_v<Queue>);
static_assert(noexcept(std::declval<Queue&>().enqueue(Command{})));
static_assert(noexcept(std::declval<Queue&>().request_release_all()));
static_assert(noexcept(std::declval<const Queue&>().peek()));
static_assert(noexcept(std::declval<Queue&>().commit(std::declval<const Queue::Snapshot&>())));
static_assert(noexcept(std::declval<Queue&>().discard()));

void require(bool condition, std::string_view message) {
    if (!condition) { throw std::runtime_error(std::string{message}); }
}

bool same(Command actual, Command expected) {
    return actual.key == expected.key && actual.down == expected.down &&
        std::bit_cast<std::uint32_t>(actual.velocity) ==
        std::bit_cast<std::uint32_t>(expected.velocity);
}

Command command(std::size_t sequence) {
    return {static_cast<int>(sequence % 73U), (sequence & 1U) == 0U,
            static_cast<float>(sequence + 1U) * (1.0F / 131072.0F)};
}

void expect_sequence(const Queue::Snapshot& snapshot, std::size_t start,
                     std::size_t count) {
    require(snapshot.count == count, "incorrect snapshot count");
    require(snapshot.read_end - snapshot.read_begin == count,
            "snapshot boundaries disagree with count");
    for (std::size_t index = 0; index < count; ++index) {
        require(same(snapshot.commands[index], command(start + index)),
                "FIFO command contents changed");
    }
}

struct AudioState {
    std::array<bool, 73> held{};
    std::uint64_t applied_release_epoch{};

    void apply(const Queue::Snapshot& snapshot) {
        for (std::size_t index = 0; index < snapshot.count; ++index) {
            const auto event = snapshot.commands[index];
            held[static_cast<std::size_t>(event.key)] = event.down;
        }
        // The release follows the entire batch, including pending downs.
        if (snapshot.release_epoch != applied_release_epoch) { held.fill(false); }
    }

    bool commit(Queue& queue, const Queue::Snapshot& snapshot) {
        if (!queue.commit(snapshot)) { return false; }
        applied_release_epoch = snapshot.release_epoch;
        return true;
    }

    bool silent() const {
        return std::none_of(held.begin(), held.end(), [](bool down) { return down; });
    }
};

void fifo_and_ring_wrap() {
    Queue queue;
    require(queue.peek().count == 0 && queue.peek().release_epoch == 0,
            "new transport was not empty");
    std::size_t produced = 0;
    for (std::size_t round = 0; round < 100; ++round) {
        const auto begin = produced;
        for (std::size_t index = 0; index < 173; ++index) {
            require(queue.enqueue(command(produced++)), "first batch rejected");
        }
        const auto prefix = queue.peek();
        expect_sequence(prefix, begin, 173);
        for (std::size_t index = 0; index < 83; ++index) {
            require(queue.enqueue(command(produced++)), "full capacity was unavailable");
        }
        require(queue.commit(prefix), "captured prefix commit failed");
        expect_sequence(queue.peek(), begin + 173, 83);
        for (std::size_t index = 0; index < 71; ++index) {
            require(queue.enqueue(command(produced++)), "reclaimed slots were not reusable");
        }
        const auto wrapped = queue.peek();
        expect_sequence(wrapped, begin + 173, 154);
        require(wrapped.release_epoch == 0, "normal wraps requested emergency release");
        require(queue.commit(wrapped) && queue.peek().count == 0, "wrapped drain failed");
        expect_sequence(prefix, begin, 173); // Copies survive slot reuse.
    }
}

void invalid_commands_and_float_bits() {
    struct RestoreEnvironment {
        std::fenv_t saved{};
        RestoreEnvironment() { require(std::fegetenv(&saved) == 0, "cannot save FP environment"); }
        ~RestoreEnvironment() { std::fesetenv(&saved); }
    } restore;
    Queue queue;
    for (const int key : {std::numeric_limits<int>::min(), -1, 73,
                          std::numeric_limits<int>::max()}) {
        require(!queue.enqueue({key, true, 0.5F}), "out-of-range key accepted");
    }
    constexpr std::array<std::uint32_t, 10> invalid{
        0x80000001U, 0x807fffffU, 0xbf800000U, 0x3f800001U, 0x7f7fffffU,
        0x7f800000U, 0xff800000U, 0x7fc00001U, 0x7f800001U, 0xff800001U};
    for (const auto bits : invalid) {
        for (const bool down : {false, true}) {
            volatile auto runtime_bits = bits;
            const auto velocity = std::bit_cast<float>(static_cast<std::uint32_t>(runtime_bits));
            require(std::feclearexcept(FE_ALL_EXCEPT) == 0, "cannot clear FP exceptions");
            const bool accepted = queue.enqueue({40, down, velocity});
            const int raised = std::fetestexcept(FE_ALL_EXCEPT);
            require(!accepted && raised == 0, "invalid float accepted or raised an FP exception");
        }
    }
    require(queue.peek().count == 0 && queue.peek().release_epoch == 0,
            "invalid input mutated queue or release state");
    constexpr std::array<std::uint32_t, 7> valid{
        0U, 0x80000000U, 1U, 0x007fffffU, 0x00800000U, 0x3f7fffffU, 0x3f800000U};
    for (std::size_t index = 0; index < valid.size(); ++index) {
        require(queue.enqueue({index % 2 == 0 ? 0 : 72, index % 2 == 0,
                               std::bit_cast<float>(valid[index])}),
                "valid boundary command rejected");
    }
    const auto accepted = queue.peek();
    require(accepted.count == valid.size(), "valid boundary command lost");
    for (std::size_t index = 0; index < valid.size(); ++index) {
        require(std::bit_cast<std::uint32_t>(accepted.commands[index].velocity) == valid[index],
                "queue changed signed-zero or subnormal velocity bits");
    }
    require(queue.commit(accepted), "valid boundary commit failed");
}

void overflow_releases_final_key_up() {
    Queue queue;
    AudioState audio;
    for (std::size_t index = 0; index < Queue::capacity; ++index) {
        require(queue.enqueue({40, true, 0.75F}), "capacity smaller than 256");
    }
    require(!queue.enqueue({73, false, 0.0F}) && queue.peek().release_epoch == 0,
            "malformed full-queue command triggered an overflow release");
    require(!queue.enqueue({40, false, 0.0F}), "full queue accepted extra command");
    const auto snapshot = queue.peek();
    require(snapshot.count == Queue::capacity && snapshot.release_epoch == 1,
            "overflow changed FIFO or lost emergency release");
    audio.apply(snapshot);
    require(audio.silent(), "dropped final key-up left an audition note held");
    require(audio.commit(queue, snapshot), "overflow recovery commit failed");
    require(queue.enqueue({72, true, 1.0F}), "overflow prevented later recovery");
    const auto recovered = queue.peek();
    audio.apply(recovered);
    require(audio.held[72], "already applied release incorrectly cleared a new down");
    require(audio.commit(queue, recovered), "recovery commit failed");
    queue.request_release_all();
    const auto release_only = queue.peek();
    require(release_only.count == 0 && release_only.release_epoch == 2,
            "release-only snapshot missing");
    audio.apply(release_only);
    require(audio.commit(queue, release_only) && audio.silent(), "release-only commit failed");
}

void aborted_transaction_and_late_release() {
    Queue queue;
    AudioState audio;
    require(queue.enqueue({12, true, 0.25F}), "initial down rejected");
    queue.request_release_all();
    const auto abandoned = queue.peek();
    auto candidate = audio;
    candidate.apply(abandoned);
    // Reject host preflight: neither candidate nor snapshot is committed.
    require(queue.enqueue({60, true, 0.5F}), "post-snapshot command rejected");
    queue.request_release_all();
    const auto retry = queue.peek();
    require(retry.read_begin == abandoned.read_begin && retry.count == 2 &&
            retry.release_epoch == 2 && audio.applied_release_epoch == 0,
            "aborted transaction consumed commands or acknowledged a release");
    require(same(retry.commands[0], abandoned.commands[0]), "aborted prefix changed");
    audio.apply(retry);
    queue.request_release_all(); // Arrives after peek but before commit.
    require(audio.commit(queue, retry) && audio.applied_release_epoch == 2,
            "successful retry failed");
    const auto late = queue.peek();
    require(late.count == 0 && late.release_epoch == 3,
            "commit erased a later producer release");
    audio.apply(late);
    require(audio.commit(queue, late) && audio.silent(), "later release was not applied");
}

void snapshot_rejection_and_discard() {
    Queue queue;
    Queue other;
    require(queue.enqueue(command(0)) && other.enqueue(command(1)), "snapshot setup failed");
    const auto first = queue.peek();
    require(!other.commit(first), "foreign snapshot committed");
    require(!queue.commit(Queue::Snapshot{}), "unowned snapshot committed");
    auto altered = first;
    altered.count = Queue::capacity + 1;
    require(!queue.commit(altered), "oversize snapshot committed");
    altered = first;
    ++altered.read_begin;
    require(!queue.commit(altered), "altered begin committed");
    altered = first;
    ++altered.read_end;
    ++altered.count;
    require(queue.enqueue(command(1)), "later producer command rejected");
    require(!queue.commit(altered), "snapshot consumed a command published after peek");
    altered = first;
    ++altered.release_epoch;
    require(!queue.commit(altered), "altered release epoch committed");
    const auto wider = queue.peek();
    expect_sequence(wider, 0, 2);
    require(queue.commit(first), "unaltered prefix rejected");
    require(!queue.commit(first) && !queue.commit(wider), "stale snapshot committed");
    expect_sequence(queue.peek(), 1, 1);
    queue.request_release_all();
    const auto before_discard = queue.peek();
    queue.discard();
    require(!queue.commit(before_discard), "discarded snapshot committed");
    require(queue.peek().count == 0 && queue.peek().release_epoch == 1,
            "discard retained commands or cleared a release epoch");
    require(queue.enqueue(command(2)), "discard prevented subsequent enqueue");
    expect_sequence(queue.peek(), 2, 1);
    require(other.peek().count == 1 && other.peek().release_epoch == 0,
            "one instance changed another instance");
}

void counter_exhaustion() {
    using SmallQueue = rhodes::editor_audition_detail::Queue<std::uint16_t>;
    SmallQueue cursor_queue;
    std::size_t written = 0;
    while (written < SmallQueue::counter_limit) {
        const auto count = std::min(SmallQueue::capacity,
            static_cast<std::size_t>(SmallQueue::counter_limit) - written);
        for (std::size_t index = 0; index < count; ++index) {
            require(cursor_queue.enqueue({40, true, 0.5F}), "cursor exhausted too early");
        }
        const auto snapshot = cursor_queue.peek();
        require(snapshot.read_begin == written && snapshot.count == count &&
                snapshot.read_end == written + count, "cursor wrapped before exhaustion");
        require(cursor_queue.commit(snapshot), "exhaustion prefix commit failed");
        written += count;
    }
    require(!cursor_queue.enqueue({40, false, 0.0F}), "exhausted cursor wrapped");
    const auto terminal = cursor_queue.peek();
    require(terminal.count == 0 && terminal.read_begin == SmallQueue::counter_limit &&
            terminal.read_end == SmallQueue::counter_limit && terminal.release_epoch == 1,
            "cursor exhaustion lost final key-up release");
    cursor_queue.discard();
    require(!cursor_queue.enqueue({40, true, 0.5F}), "discard re-enabled exhausted cursor");

    SmallQueue epoch_queue;
    require(epoch_queue.enqueue({72, true, 1.0F}), "epoch setup failed");
    for (std::size_t index = 0; index < SmallQueue::counter_limit; ++index) {
        epoch_queue.request_release_all();
    }
    const auto final_release = epoch_queue.peek();
    require(final_release.count == 1 && final_release.release_epoch == SmallQueue::counter_limit,
            "release epoch wrapped or lost preceding command");
    require(epoch_queue.commit(final_release), "terminal epoch commit failed");
    for (int index = 0; index < 4; ++index) {
        epoch_queue.request_release_all();
        require(!epoch_queue.enqueue({72, true, 1.0F}), "terminal epoch accepted a new down");
    }
    require(epoch_queue.peek().count == 0 &&
            epoch_queue.peek().release_epoch == SmallQueue::counter_limit,
            "terminal release epoch did not saturate");
}

void ordered_producer_consumer() {
    Queue queue;
    require(queue.enqueue({10, true, 0.5F}), "ordered race setup failed");
    std::barrier phase{2}; // Scheduling only; the transport never waits.
    bool initial = false, prefix = false, final = false;
    std::jthread consumer([&] {
        AudioState audio;
        const auto first = queue.peek();
        initial = first.count == 1 && first.release_epoch == 0;
        phase.arrive_and_wait();
        phase.arrive_and_wait();
        audio.apply(first);
        prefix = audio.commit(queue, first) && audio.held[10];
        const auto next = queue.peek();
        final = next.count == 1 && next.commands[0].key == 11 && next.release_epoch == 1;
        audio.apply(next);
        final = audio.commit(queue, next) && audio.silent() && final;
    });
    phase.arrive_and_wait();
    queue.request_release_all();
    const bool accepted = queue.enqueue({11, true, 0.75F});
    phase.arrive_and_wait();
    consumer.join();
    require(accepted && initial && prefix && final,
            "ordered race consumed later commands or lost a release");
}

void concurrent_producer_consumer() {
    constexpr std::size_t attempts = 100000;
    Queue queue;
    std::vector<Command> accepted, observed;
    accepted.reserve(attempts);
    observed.reserve(attempts);
    std::barrier phase{2};
    std::size_t commit_failures = 0;
    std::size_t epoch_regressions = 0;
    bool released = false;
    std::jthread consumer([&] {
        AudioState audio;
        phase.arrive_and_wait();
        for (std::size_t pass = 0; pass < attempts; ++pass) {
            const auto snapshot = queue.peek();
            if (snapshot.release_epoch < audio.applied_release_epoch) { ++epoch_regressions; }
            if (pass % 11U != 0U) { // Deliberate aborted process transactions.
                for (std::size_t index = 0; index < snapshot.count; ++index) {
                    observed.push_back(snapshot.commands[index]);
                }
                audio.apply(snapshot);
                if (!audio.commit(queue, snapshot)) { ++commit_failures; }
            }
            if (pass % 64U == 0U) { std::this_thread::yield(); }
        }
        phase.arrive_and_wait(); // Producer finished; one bounded drain remains.
        const auto last = queue.peek();
        for (std::size_t index = 0; index < last.count; ++index) {
            observed.push_back(last.commands[index]);
        }
        audio.apply(last);
        if (!audio.commit(queue, last)) { ++commit_failures; }
        released = audio.silent() && last.release_epoch != 0;
    });
    phase.arrive_and_wait();
    for (std::size_t index = 0; index < attempts; ++index) {
        const auto event = command(index);
        if (queue.enqueue(event)) { accepted.push_back(event); }
        if (index % 127U == 0U) { queue.request_release_all(); }
        if (index % 64U == 0U) { std::this_thread::yield(); }
    }
    queue.request_release_all();
    phase.arrive_and_wait();
    consumer.join();
    require(commit_failures == 0 && epoch_regressions == 0,
            "concurrent commit failed or release epoch regressed");
    require(!accepted.empty() && accepted.size() == observed.size(),
            "concurrent transport lost or duplicated accepted commands");
    for (std::size_t index = 0; index < accepted.size(); ++index) {
        require(same(observed[index], accepted[index]), "concurrent FIFO contents were torn or reordered");
    }
    require(released && queue.peek().count == 0, "final concurrent release or drain was lost");
    std::cout << "stress attempted=" << attempts << " accepted=" << accepted.size()
              << " rejected=" << attempts - accepted.size() << '\n';
}

using Test = void (*)();
constexpr std::array<std::pair<std::string_view, Test>, 8> tests{{
    {"fifo_and_ring_wrap", fifo_and_ring_wrap},
    {"invalid_commands_and_float_bits", invalid_commands_and_float_bits},
    {"overflow_releases_final_key_up", overflow_releases_final_key_up},
    {"aborted_transaction_and_late_release", aborted_transaction_and_late_release},
    {"snapshot_rejection_and_discard", snapshot_rejection_and_discard},
    {"counter_exhaustion", counter_exhaustion},
    {"ordered_producer_consumer", ordered_producer_consumer},
    {"concurrent_producer_consumer", concurrent_producer_consumer}
}};
} // namespace

int main(int argc, char** argv) {
    const std::string_view selected = argc > 1 ? std::string_view{argv[1]} : "all";
    try {
        require(argc <= 2, "usage: editor_audition_test [fixture|all]");
        bool found = false;
        for (const auto& [name, test] : tests) {
            if (selected == "all" || selected == name) {
                test();
                std::cout << "PASS " << name << '\n';
                found = true;
            }
        }
        require(found, "unknown editor-audition fixture");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << selected << ": " << error.what() << '\n';
        return 1;
    }
}
