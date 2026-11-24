#pragma once

#include "rhodes/control_value.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>

namespace rhodes {

struct EditorAuditionCommand {
    int key{}; // Physical key, 0..72 (not a MIDI pitch).
    bool down{};
    float velocity{}; // Normalized 0..1, including either signed zero.
};

namespace editor_audition_detail {

// A narrow counter instantiation lets the same exhaustion paths be exercised
// without a test-only mutation API. The public transport uses uint64_t below.
template <typename Counter>
class Queue final {
public:
    using Command = EditorAuditionCommand;
    using counter_type = Counter;
    static constexpr std::size_t capacity = 256;
    static constexpr counter_type counter_limit = std::numeric_limits<Counter>::max();

    static_assert(std::is_unsigned_v<Counter> && counter_limit > capacity);
    static_assert(std::atomic<Counter>::is_always_lock_free,
                  "Editor audition requires lock-free cursor and epoch atomics");

    struct Snapshot {
        std::array<Command, capacity> commands{};
        std::size_t count{};
        counter_type read_begin{};
        counter_type read_end{};
        counter_type release_epoch{};

    private:
        friend class Queue;
        const Queue* owner_{};
        counter_type captured_begin_{};
        counter_type captured_end_{};
        counter_type captured_epoch_{};
    };

    Queue() noexcept = default;
    Queue(const Queue&) = delete;
    Queue& operator=(const Queue&) = delete;

    // Producer only. Invalid input has no effect. Full/exhausted transports
    // request an out-of-band release before rejecting, so a final key-up can
    // never disappear merely because the ring has no free slot. No retry.
    [[nodiscard]] bool enqueue(Command command) noexcept {
        if (command.key < 0 || command.key > 72 ||
            !control::valid_normalized(command.velocity)) {
            return false;
        }
        const auto write = write_.load(std::memory_order_relaxed);
        const auto read = read_.load(std::memory_order_acquire);
        if (write == counter_limit ||
            release_epoch_.load(std::memory_order_relaxed) == counter_limit ||
            static_cast<std::size_t>(write - read) >= capacity) {
            request_release_all();
            return false;
        }
        commands_[slot(write)] = command;
        write_.store(static_cast<counter_type>(write + 1U), std::memory_order_release);
        return true;
    }

    // Producer only. Saturation is terminal: enqueue then rejects every new
    // command. With no later downs possible, the final epoch needs applying
    // only once. Neither cursor nor epoch ever wraps or reuses a snapshot ID.
    void request_release_all() noexcept {
        const auto epoch = release_epoch_.load(std::memory_order_relaxed);
        if (epoch != counter_limit) {
            release_epoch_.store(static_cast<counter_type>(epoch + 1U),
                                 std::memory_order_release);
        }
    }

    // Consumer only; does not consume anything. Loading the epoch FIRST is
    // essential: an acquired release makes every preceding producer command
    // visible to the following write load. Thus the snapshot contains all
    // commands preceding its release. A later release remains pending for a
    // subsequent peek. The producer cannot reuse these slots before commit.
    [[nodiscard]] Snapshot peek() const noexcept {
        Snapshot snapshot;
        snapshot.owner_ = this;
        snapshot.release_epoch = release_epoch_.load(std::memory_order_acquire);
        snapshot.read_begin = read_.load(std::memory_order_relaxed);
        snapshot.read_end = write_.load(std::memory_order_acquire);
        snapshot.count = static_cast<std::size_t>(snapshot.read_end - snapshot.read_begin);
        snapshot.captured_begin_ = snapshot.read_begin;
        snapshot.captured_end_ = snapshot.read_end;
        snapshot.captured_epoch_ = snapshot.release_epoch;
        for (std::size_t index = 0; index < snapshot.count; ++index) {
            snapshot.commands[index] = commands_[slot(
                static_cast<counter_type>(snapshot.read_begin + index))];
        }
        return snapshot;
    }

    // Consumer only, after successful process preflight. The captured prefix
    // alone is released; commands published after peek stay queued. Abandoning
    // a snapshot changes nothing. Foreign, stale, and altered boundaries fail
    // without mutation. Empty repeated commits are harmless.
    [[nodiscard]] bool commit(const Snapshot& snapshot) noexcept {
        if (snapshot.owner_ != this ||
            snapshot.read_begin != snapshot.captured_begin_ ||
            snapshot.read_end != snapshot.captured_end_ ||
            snapshot.release_epoch != snapshot.captured_epoch_ ||
            snapshot.read_begin != read_.load(std::memory_order_relaxed) ||
            snapshot.read_end < snapshot.read_begin ||
            snapshot.count > capacity ||
            static_cast<std::size_t>(snapshot.read_end - snapshot.read_begin) != snapshot.count) {
            return false;
        }
        read_.store(snapshot.read_end, std::memory_order_release);
        return true;
    }

    // Consumer only, for a reset that already releases its owned note state.
    // Discards just the currently published prefix, without touching producer
    // state or acknowledging any release epoch. Later commands remain queued.
    void discard() noexcept {
        const auto end = write_.load(std::memory_order_acquire);
        read_.store(end, std::memory_order_release);
    }

private:
    [[nodiscard]] static constexpr std::size_t slot(counter_type cursor) noexcept {
        return static_cast<std::size_t>(cursor & (capacity - 1U));
    }

    std::array<Command, capacity> commands_{};
    std::atomic<counter_type> read_{0};
    std::atomic<counter_type> write_{0};
    std::atomic<counter_type> release_epoch_{0};
};

} // namespace editor_audition_detail

// Exactly one serialized UI/SDK-notify producer calls enqueue/release. Exactly
// one audio owner calls peek/commit/discard; a lifecycle thread may assume that
// ownership only while audio is stopped. This is SPSC, not MPSC or reentrant.
// Destruction requires both owners stopped, and snapshots must not outlive it.
//
// Slot ownership passes by release/acquire cursors. Only the producer writes
// an unpublished free slot; only the consumer reads a published, uncommitted
// slot. Slots therefore need no atomics. All operations are allocation-free,
// lock-free and bounded (peek copies at most 256 commands), with no retry loop.
//
// Integration transaction:
//   1. Peek and preflight the snapshot together with the host block.
//   2. Apply its commands to UI-owned note state, then release that UI state if
//      snapshot.release_epoch differs from the audio-owned applied epoch.
//      Release wins over the ENTIRE captured batch, including racing downs.
//   3. Commit the snapshot and record its epoch only on successful processing.
// An aborted block does neither. Never clear a shared release flag, release
// external MIDI holders, or reset shared sustain in response to this transport.
// At theoretical 64-bit exhaustion, reconstruct the transport while stopped;
// discard intentionally cannot reset counters or re-enable an exhausted queue.
using EditorAuditionQueue = editor_audition_detail::Queue<std::uint64_t>;

} // namespace rhodes
