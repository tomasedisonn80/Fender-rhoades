#pragma once

#include "rhodes/event_processor.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace rhodes {

struct MidiNote {
    bool on = true;
    int channel = 0;
    int pitch = 28;
    std::int32_t note_id = -1; // Only -1 means anonymous; other negative IDs are identified.
    double velocity = 0.0;
};

enum class NoteStatus : std::uint8_t { ok, invalid_input, capacity_exceeded };

// The adapter supplies ordering/offsets when translating this to EngineEvent.
struct NoteChange {
    EventType type = EventType::note_on;
    std::size_t key = 0;
    double value = 0.0;
};

struct NoteResult {
    NoteStatus status = NoteStatus::ok;
    std::optional<NoteChange> change;
    [[nodiscard]] bool succeeded() const noexcept { return status == NoteStatus::ok; }
};

struct NoteReleaseResult {
    NoteStatus status = NoteStatus::ok;
    std::array<std::size_t, key_count> keys{};
    std::size_t count = 0; // Only keys[0..count) are releases, in ascending key order.
    [[nodiscard]] bool succeeded() const noexcept { return status == NoteStatus::ok; }
};

// Fixed-storage ownership only: no mechanics, sustain state, allocation or locks.
// Copy this value for block preflight, then commit only after all events pass.
// Each operation is atomic on rejection. Every accepted positive on strikes;
// only removal of a key's last holder emits its physical release.
class NoteTracker {
public:
    static constexpr std::size_t capacity = 256;
    static constexpr int lowest_pitch = 28;
    static constexpr int highest_pitch = 100;
    static_assert(highest_pitch - lowest_pitch + 1 == key_count);

    [[nodiscard]] NoteResult apply(const MidiNote& note) noexcept {
        if (!valid_channel(note.channel) || note.pitch < 0 || note.pitch > 127 ||
            !std::isfinite(note.velocity) || note.velocity < 0.0 || note.velocity > 1.0) {
            return {NoteStatus::invalid_input, std::nullopt};
        }
        // Valid pitches outside this keyboard are ignored before identity lookup.
        if (note.pitch < lowest_pitch || note.pitch > highest_pitch) { return {}; }

        const auto key = static_cast<std::size_t>(note.pitch - lowest_pitch);
        const bool on = note.on && note.velocity > 0.0;
        std::size_t match = capacity;
        std::size_t free_slot = capacity;
        for (std::size_t index = 0; index < holders_.size(); ++index) {
            const auto& holder = holders_[index];
            if (!holder.active) {
                if (free_slot == capacity) { free_slot = index; }
            } else if (holder.channel == note.channel && holder.note_id == note.note_id &&
                       (note.note_id != -1 || holder.key == key)) {
                if (holder.key != key) { return {NoteStatus::invalid_input, std::nullopt}; }
                if (match == capacity) { match = index; }
            }
        }

        if (on) {
            if (note.note_id == -1 || match == capacity) {
                if (free_slot == capacity) {
                    return {NoteStatus::capacity_exceeded, std::nullopt};
                }
                holders_[free_slot] = {true, note.channel, key, note.note_id};
                ++held_counts_[key];
                ++active_count_;
            }
            return {NoteStatus::ok, NoteChange{EventType::note_on, key, note.velocity}};
        }

        if (match == capacity) { return {}; }
        holders_[match] = {};
        --active_count_;
        if (--held_counts_[key] == 0) {
            return {NoteStatus::ok, NoteChange{EventType::note_off, key, 0.0}};
        }
        return {};
    }

    // CC123-style ownership release. The caller emits note_off for returned keys;
    // shared sustain remains the caller's responsibility and is not reset here.
    [[nodiscard]] NoteReleaseResult release_channel(int channel) noexcept {
        if (!valid_channel(channel)) { return {NoteStatus::invalid_input, {}, 0}; }
        const auto previous_counts = held_counts_;
        for (auto& holder : holders_) {
            if (holder.active && holder.channel == channel) {
                --held_counts_[holder.key];
                --active_count_;
                holder = {};
            }
        }
        NoteReleaseResult result;
        for (std::size_t key = 0; key < key_count; ++key) {
            if (previous_counts[key] > 0 && held_counts_[key] == 0) {
                result.keys[result.count++] = key;
            }
        }
        return result;
    }

    // Ownership reset only. The caller chooses physical all-notes-off or panic.
    void reset() noexcept {
        holders_ = {};
        held_counts_ = {};
        active_count_ = 0;
    }

    // Out-of-range physical-key queries return zero and never index storage.
    [[nodiscard]] std::size_t held_count(std::size_t key) const noexcept {
        return key < key_count ? held_counts_[key] : 0;
    }
    [[nodiscard]] std::size_t active_count() const noexcept { return active_count_; }

private:
    struct Holder {
        bool active = false;
        int channel = 0;
        std::size_t key = 0;
        std::int32_t note_id = -1;
    };

    static bool valid_channel(int channel) noexcept { return channel >= 0 && channel < 16; }

    std::array<Holder, capacity> holders_{};
    std::array<std::size_t, key_count> held_counts_{};
    std::size_t active_count_ = 0;
};

} // namespace rhodes
