#include "tests/cpp/vst3_command_test_support.hpp"
#include "pluginterfaces/vst/ivstmessage.h"

#include <bit>
#include <cfenv>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <new>
#include <utility>

#if defined(__SSE__)
#include <xmmintrin.h>
#endif

// A callback-local ordinary C++ allocation probe. SDK messages and host buffers
// use fixed test storage. This does not instrument arbitrary malloc or locks.
namespace audition_allocation_probe {
thread_local bool enabled = false;
thread_local std::size_t calls = 0;
void* allocate(std::size_t bytes) {
    if (enabled) { ++calls; }
    if (auto* memory = std::malloc(bytes == 0 ? 1 : bytes)) { return memory; }
    throw std::bad_alloc{};
}
#if defined(_MSC_VER)
__declspec(noinline)
#elif defined(__GNUC__)
__attribute__((noinline))
#endif
void deallocate(void* memory) noexcept { std::free(memory); }
}
void* operator new(std::size_t bytes) { return audition_allocation_probe::allocate(bytes); }
void* operator new[](std::size_t bytes) { return audition_allocation_probe::allocate(bytes); }
void operator delete(void* memory) noexcept { audition_allocation_probe::deallocate(memory); }
void operator delete[](void* memory) noexcept { audition_allocation_probe::deallocate(memory); }
void operator delete(void* memory, std::size_t) noexcept {
    audition_allocation_probe::deallocate(memory);
}
void operator delete[](void* memory, std::size_t) noexcept {
    audition_allocation_probe::deallocate(memory);
}

namespace {
using namespace rhodes;
using namespace rhodes::vst3;
using namespace rhodes::vst3::test_host;
using ScalarEngine = std::remove_cvref_t<
    decltype(std::declval<const HostEventProcessor&>().engine())>;
using Host = ProcessorHost<double, 96>;
std::size_t checks = 0;
std::size_t callbacks = 0;
std::size_t frames_compared = 0;

void check(bool condition, std::string_view message) {
    ++checks;
    require(condition, message);
}

struct Attributes final : FixedHostObject<vst::IAttributeList> {
    sdk::int64 key = 40;
    sdk::int64 down = 1;
    double velocity = 0.5;
    bool key_available = true;
    bool down_available = true;
    bool velocity_available = true;
    sdk::tresult PLUGIN_API getInt(AttrID id, sdk::int64& value) override {
        if (std::strcmp(id, "key") == 0 && key_available) {
            value = key;
            return sdk::kResultOk;
        }
        if (std::strcmp(id, "down") == 0 && down_available) {
            value = down;
            return sdk::kResultOk;
        }
        return sdk::kResultFalse;
    }
    sdk::tresult PLUGIN_API getFloat(AttrID id, double& value) override {
        if (std::strcmp(id, "velocity") != 0 || !velocity_available) {
            return sdk::kResultFalse;
        }
        value = velocity;
        return sdk::kResultOk;
    }
    sdk::tresult PLUGIN_API setInt(AttrID, sdk::int64) override { return sdk::kNotImplemented; }
    sdk::tresult PLUGIN_API setFloat(AttrID, double) override { return sdk::kNotImplemented; }
    sdk::tresult PLUGIN_API setString(AttrID, const vst::TChar*) override {
        return sdk::kNotImplemented;
    }
    sdk::tresult PLUGIN_API getString(AttrID, vst::TChar*, sdk::uint32) override {
        return sdk::kNotImplemented;
    }
    sdk::tresult PLUGIN_API setBinary(AttrID, const void*, sdk::uint32) override {
        return sdk::kNotImplemented;
    }
    sdk::tresult PLUGIN_API getBinary(AttrID, const void*&, sdk::uint32&) override {
        return sdk::kNotImplemented;
    }
};

struct Message final : FixedHostObject<vst::IMessage> {
    const char* id = "RhodesPhotoNoteV1";
    Attributes attributes{};
    bool attributes_available = true;
    sdk::FIDString PLUGIN_API getMessageID() override { return id; }
    void PLUGIN_API setMessageID(sdk::FIDString value) override { id = value; }
    vst::IAttributeList* PLUGIN_API getAttributes() override {
        return attributes_available ? &attributes : nullptr;
    }
};

sdk::tresult photo(RhodesProcessor& processor, int key, bool down, double velocity = 0.5) {
    Message message;
    message.attributes.key = key;
    message.attributes.down = down ? 1 : 0;
    message.attributes.velocity = velocity;
    return processor.notify(&message);
}

void release_photo(RhodesProcessor& processor) {
    Message message;
    message.id = "RhodesPhotoReleaseV1";
    message.attributes_available = false; // This message has no attributes.
    check(processor.notify(&message) == sdk::kResultOk, "release message rejected");
}

sdk::tresult process(Host& host) {
    audition_allocation_probe::calls = 0;
    audition_allocation_probe::enabled = true;
    const auto result = host.processor.process(host.data);
    audition_allocation_probe::enabled = false;
    ++callbacks;
    check(audition_allocation_probe::calls == 0, "process allocated ordinary C++ storage");
    return result;
}

void flush(Host& host) {
    const auto frames = host.data.numSamples;
    host.data.numSamples = 0;
    check(process(host) == sdk::kResultOk, "zero-frame flush failed");
    host.data.numSamples = frames;
}

// The reference bypasses both the message adapter and BasicEventProcessor. Its
// explicit physical timeline is selected by each fixture, including releases
// that must be suppressed. Per-sample rendering matches the host's neutral path.
void render(Host& host, ScalarEngine& expected, std::span<const EngineEvent> events = {}) {
    check(process(host) == sdk::kResultOk, "render failed");
    std::size_t next = 0;
    for (sdk::int32 frame = 0; frame < host.data.numSamples; ++frame) {
        while (next < events.size() && events[next].offset == static_cast<std::uint32_t>(frame)) {
            const auto& event = events[next++];
            if (event.type == EventType::note_on) {
                check(expected.note_on(event.key, event.value), "reference strike failed");
            } else if (event.type == EventType::note_off) {
                check(expected.note_off(event.key), "reference release failed");
            } else {
                check(event.type == EventType::sustain, "unsupported reference event");
                expected.set_sustain(event.value == 1.0);
            }
        }
        double volts = 0.0;
        check(expected.process(std::span<double>(&volts, 1)), "reference render failed");
        const auto index = static_cast<std::size_t>(frame);
        check(host.left[index] == 16.0 * volts && host.right[index] == 16.0 * volts,
              "photo/MIDI timeline differs from physical reference");
        ++frames_compared;
    }
    check(next == events.size(), "unvisited reference event");
    same_mechanics(host.processor.engine_processor().engine(), expected);
}

void unchanged(const ProcessorSnapshot& before, const Host& host, bool held40, bool held41) {
    before.expect_unchanged(host.processor);
    check(host.processor.ui_key_held(40) == held40 &&
          host.processor.ui_key_held(41) == held41, "rejection changed photo ownership");
}

void message_validation() {
    Message message;
    auto uninitialized = std::make_unique<RhodesProcessor>();
    check(uninitialized->notify(&message) == sdk::kResultFalse, "pre-init message accepted");
    message.id = "RhodesPhotoReleaseV1";
    check(uninitialized->notify(&message) == sdk::kResultFalse, "pre-init release accepted");

    auto host = std::make_unique<Host>();
    check(photo(host->processor, 40, true) == sdk::kResultOk, "valid message rejected");
    const ProcessorSnapshot before(host->processor);
    check(host->processor.notify(nullptr) == sdk::kResultFalse, "null message accepted");
    for (const auto* id : {static_cast<const char*>(nullptr), "unknown", "RhodesPhotoNoteV2"}) {
        message.id = id;
        check(host->processor.notify(&message) == sdk::kResultFalse, "invalid ID accepted");
    }
    message.id = "RhodesPhotoNoteV1";
    message.attributes_available = false;
    check(host->processor.notify(&message) == sdk::kResultFalse, "missing attributes accepted");
    message.attributes_available = true;
    for (auto* available : {&message.attributes.key_available,
                           &message.attributes.down_available,
                           &message.attributes.velocity_available}) {
        *available = false;
        check(host->processor.notify(&message) == sdk::kResultFalse,
              "missing/wrong-type attribute accepted");
        *available = true;
    }
    for (const auto key : std::array<sdk::int64, 3>{-1, 73, std::numeric_limits<sdk::int64>::max()}) {
        message.attributes.key = key;
        check(host->processor.notify(&message) == sdk::kResultFalse, "invalid key accepted");
    }
    message.attributes.key = 40;
    for (const auto down : std::array<sdk::int64, 3>{-1, 2, std::numeric_limits<sdk::int64>::max()}) {
        message.attributes.down = down;
        check(host->processor.notify(&message) == sdk::kResultFalse, "invalid down accepted");
    }
    message.attributes.down = 1;
    constexpr std::array<std::uint64_t, 7> invalid{
        0xbff0000000000000ULL, 0x8000000000000001ULL, 0x3ff0000000000001ULL,
        0x7ff0000000000000ULL, 0xfff0000000000000ULL,
        0x7ff8000000000001ULL, 0x7ff0000000000001ULL};
    for (const auto bits : invalid) {
        message.attributes.velocity = std::bit_cast<double>(bits);
        std::feclearexcept(FE_ALL_EXCEPT);
        const auto result = host->processor.notify(&message);
        const auto invalid_exception = std::fetestexcept(FE_INVALID);
        check(result == sdk::kResultFalse && invalid_exception == 0,
              "invalid velocity accepted or raised FE_INVALID");
    }
    unchanged(before, *host, false, false);
    flush(*host);
    check(host->processor.ui_key_held(40) && !host->processor.ui_key_held(73),
          "invalid message changed pending valid command");
    check(host->processor.note_tracker().active_count() == 0 &&
          host->processor.engine_processor().frame_cursor() == 0,
          "UI-only flush claimed MIDI ownership or advanced time");
    check(host->processor.terminate() == sdk::kResultOk, "explicit termination failed");
    check(photo(host->processor, 40, true) == sdk::kResultFalse, "post-terminate note accepted");
}

void overlap_and_boundary_order() {
    auto host = std::make_unique<Host>();
    auto expected = std::make_unique<ScalarEngine>();
    EventList midi;
    host->data.inputEvents = &midi;
    midi.count = 1;
    midi.values[0] = note_on(0, 1, 68, 0.25F);
    check(photo(host->processor, 40, true) == sdk::kResultOk, "first photo strike");
    check(expected->note_on(40, 0.5), "reference first photo strike");
    const std::array first{EngineEvent{0, 0, EventType::note_on, 40, 0.25}};
    render(*host, *expected, first);
    check(host->processor.ui_key_held(40) && host->processor.note_tracker().held_count(40) == 1,
          "photo and MIDI did not independently hold key");

    midi.values[0] = note_off(32, 1);
    render(*host, *expected); // MIDI release is suppressed by photo ownership.
    check(host->processor.note_tracker().held_count(40) == 0 && host->processor.ui_key_held(40),
          "MIDI release changed photo ownership");

    check(photo(host->processor, 40, true, 0.125) == sdk::kResultOk, "photo retrigger");
    check(expected->note_on(40, 0.125), "reference retrigger");
    midi.values[0] = note_on(0, 2, 68, 0.375F);
    const std::array second{EngineEvent{0, 0, EventType::note_on, 40, 0.375}};
    render(*host, *expected, second);

    // The old MIDI holder persists until frame 48. Using the end-of-block
    // candidate for the photo release would wrongly damp the first 48 frames.
    check(photo(host->processor, 40, false) == sdk::kResultOk, "photo release");
    midi.values[0] = note_off(48, 2);
    const std::array release{EngineEvent{48, 0, EventType::note_off, 40, 0.0}};
    render(*host, *expected, release);
    check(!host->processor.ui_key_held(40), "released photo owner retained");

    midi.count = 0;
    check(photo(host->processor, 41, true) == sdk::kResultOk, "new photo key");
    check(expected->note_on(41, 0.5), "reference new photo key");
    render(*host, *expected);
    // Conversely, a future host on must not protect a currently unowned key.
    check(photo(host->processor, 41, false) == sdk::kResultOk, "new photo release");
    check(expected->note_off(41), "reference early photo release");
    midi.count = 1;
    midi.values[0] = note_on(48, 3, 69, 0.25F);
    const std::array future{EngineEvent{48, 0, EventType::note_on, 41, 0.25}};
    render(*host, *expected, future);
}

void zero_flush_and_emergency_isolation() {
    auto host = std::make_unique<Host>();
    auto expected = std::make_unique<ScalarEngine>();
    EventList midi;
    midi.count = 1;
    midi.values[0] = note_on(0, 1);
    host->data.inputEvents = &midi;
    const std::array first{EngineEvent{0, 0, EventType::note_on, 40, 0.75}};
    render(*host, *expected, first);
    midi.count = 0;
    check(photo(host->processor, 40, true, 0.25) == sdk::kResultOk, "overlapping photo note");
    check(expected->note_on(40, 0.25), "reference overlap");
    CommandQueue command;
    command.count = 1;
    command.values[0] = {0, 0.0};
    CommandChanges changes;
    changes.count = 1;
    changes.values[0] = &command;
    host->data.inputParameterChanges = &changes;
    const auto cursor = host->processor.engine_processor().frame_cursor();
    flush(*host); // CC123 removes MIDI ownership, but photo keeps the physical key held.
    check(host->processor.engine_processor().frame_cursor() == cursor &&
          host->processor.note_tracker().active_count() == 0 && host->processor.ui_key_held(40),
          "zero-frame CC123 released photo key or advanced cursor");
    host->data.inputParameterChanges = nullptr;
    render(*host, *expected);
    check(photo(host->processor, 40, false) == sdk::kResultOk, "zero-frame photo up");
    check(expected->note_off(40), "reference zero-frame up");
    flush(*host);
    render(*host, *expected); // Includes retained delayed/filter release history.

    midi.count = 2;
    midi.values[0] = note_on(0, 2, 28, 0.25F);
    midi.values[1] = note_on(0, 3, 68, 0.25F);
    const std::array held{EngineEvent{0, 0, EventType::note_on, 0, 0.25},
                          EngineEvent{0, 1, EventType::note_on, 40, 0.25}};
    render(*host, *expected, held);
    midi.count = 0;
    check(photo(host->processor, 40, true, 0.25) == sdk::kResultOk, "photo shared owner");
    check(expected->note_on(40, 0.25), "reference shared owner");
    render(*host, *expected);
    ParameterQueue sustain;
    sustain.id = kSustainId;
    sustain.count = 1;
    sustain.values[0] = {0, 1.0};
    changes.values[0] = &sustain;
    host->data.inputParameterChanges = &changes;
    flush(*host);
    expected->set_sustain(true);
    host->data.inputParameterChanges = nullptr;
    release_photo(host->processor);
    check(photo(host->processor, 72, true, 0.25) == sdk::kResultOk, "post-release captured down");
    check(expected->note_on(72, 0.25) && expected->note_off(72), "reference captured down/up");
    render(*host, *expected);
    check(!host->processor.ui_key_held(40) && !host->processor.ui_key_held(72) &&
          host->processor.note_tracker().active_count() == 2 &&
          host->processor.note_tracker().held_count(40) == 1 &&
          host->processor.current_state().sustain, "emergency release affected MIDI or sustain");
    // Note-on with zero velocity must not issue engine.note_on(0) against MIDI.
    check(photo(host->processor, 40, true, -0.0) == sdk::kResultOk, "signed-zero photo down");
    render(*host, *expected);
}

void aborted_preflight_and_recall() {
    auto host = std::make_unique<Host>();
    auto expected = std::make_unique<ScalarEngine>();
    check(photo(host->processor, 40, true) == sdk::kResultOk, "seed photo");
    check(expected->note_on(40, 0.5), "reference seed");
    render(*host, *expected);
    check(photo(host->processor, 41, true) == sdk::kResultOk, "pending photo");
    release_photo(host->processor);
    ParameterQueue malformed;
    malformed.count = 2;
    malformed.values[0] = {0, 0.5};
    malformed.values[1] = {95, std::numeric_limits<double>::quiet_NaN()};
    ParameterChanges changes;
    changes.count = 1;
    changes.values[0] = &malformed;
    host->data.inputParameterChanges = &changes;
    const ProcessorSnapshot before(host->processor);
    check(process(*host) == sdk::kResultFalse, "late-invalid block accepted");
    unchanged(before, *host, true, false);
    host->data.inputParameterChanges = nullptr;
    check(expected->note_on(41, 0.5) && expected->note_off(40) && expected->note_off(41),
          "reference pending batch");
    render(*host, *expected);
    check(!host->processor.ui_key_held(40) && !host->processor.ui_key_held(41),
          "aborted transaction consumed release epoch or pending command");
    render(*host, *expected); // A committed batch must not replay.

    check(photo(host->processor, 40, true) == sdk::kResultOk, "recall seed");
    flush(*host);
    check(photo(host->processor, 41, true) == sdk::kResultOk, "stale pending command");
    release_photo(host->processor);
    MemoryStream state;
    state.load({0.5F, false, 1.0F}, 0, kStateModel);
    check(host->processor.setState(&state) == sdk::kResultOk, "pending recall failed");
    const ProcessorSnapshot pending(host->processor);
    host->data.numSamples = -1;
    check(process(*host) == sdk::kResultFalse, "invalid recall block accepted");
    unchanged(pending, *host, true, false);
    host->data.numSamples = 96;
    flush(*host);
    check(!host->processor.ui_key_held(40) && !host->processor.ui_key_held(41) &&
          host->processor.engine_processor().engine().mechanical_energy_joules() == 0.0 &&
          host->processor.engine_processor().frame_cursor() == 0,
          "accepted recall retained stale photo ownership or commands");
    expected->reset();
    check(photo(host->processor, 42, true) == sdk::kResultOk, "fresh post-recall down");
    check(expected->note_on(42, 0.5), "reference post-recall down");
    render(*host, *expected);
    check(host->processor.ui_key_held(42), "completed recall replayed release epoch");

    check(photo(host->processor, 41, true) == sdk::kResultOk, "lifecycle stale down");
    check(host->processor.setProcessing(false) == sdk::kResultOk, "lifecycle reset failed");
    check(!host->processor.ui_key_held(42), "lifecycle retained held UI");
    check(host->processor.setProcessing(true) == sdk::kResultOk, "lifecycle restart failed");
    expected->reset();
    render(*host, *expected);
    check(!host->processor.ui_key_held(41), "lifecycle replayed stale UI command");
}

void independent_capacity_and_overflow() {
    auto host = std::make_unique<Host>();
    auto expected = std::make_unique<ScalarEngine>();
    EventList midi;
    midi.count = 256;
    host->data.inputEvents = &midi;
    std::array<EngineEvent, 256> physical{};
    for (std::size_t index = 0; index < physical.size(); ++index) {
        midi.values[index] = note_on(0, -1, 68, 0.00390625F);
        physical[index] = {0, static_cast<std::uint32_t>(index), EventType::note_on,
                           40, 0.00390625};
        check(photo(host->processor, 0, true, 0.00390625) == sdk::kResultOk,
              "256-command UI batch rejected");
        check(expected->note_on(0, 0.00390625), "reference large UI batch");
    }
    // A malformed message against a full queue must not request release.
    check(photo(host->processor, -1, false) == sdk::kResultFalse,
          "invalid full-queue message accepted");
    render(*host, *expected, physical);
    check(host->processor.ui_key_held(0) && host->processor.note_tracker().active_count() == 256,
          "UI batch was mixed into host dispatch/ownership capacity");

    check(photo(host->processor, 41, true) == sdk::kResultOk, "pending ownership-failure photo");
    midi.count = 1;
    midi.values[0] = note_on(0, 7, 69);
    const ProcessorSnapshot full(host->processor);
    check(process(*host) == sdk::kResultFalse, "MIDI ownership overflow accepted");
    unchanged(full, *host, false, false);
    check(host->processor.ui_key_held(0), "ownership rejection cleared another UI key");
    midi.count = 0;
    check(expected->note_on(41, 0.5), "reference retained pending UI");
    render(*host, *expected);

    for (int index = 0; index < 256; ++index) {
        const auto key = index % 73;
        check(photo(host->processor, key, true, 0.00390625) == sdk::kResultOk,
              "overflow fixture fill failed");
        check(expected->note_on(static_cast<std::size_t>(key), 0.00390625),
              "reference overflow fill");
    }
    check(photo(host->processor, 0, false) == sdk::kResultFalse,
          "257th queued command accepted");
    for (std::size_t key = 0; key < key_count; ++key) {
        if (key != 40) { check(expected->note_off(key), "reference overflow emergency release"); }
    }
    render(*host, *expected);
    for (std::size_t key = 0; key < key_count; ++key) {
        check(!host->processor.ui_key_held(key), "overflow left photo note stuck");
    }
    check(host->processor.note_tracker().active_count() == 256 &&
          host->processor.note_tracker().held_count(40) == 256,
          "overflow emergency released external MIDI owners");
    check(photo(host->processor, 72, true, 0.25) == sdk::kResultOk, "queue did not recover space");
    check(expected->note_on(72, 0.25), "reference queue recovery");
    render(*host, *expected);
    check(host->processor.ui_key_held(72), "old release epoch killed later accepted command");
}

void floating_bits_and_instance_isolation() {
    auto first = std::make_unique<Host>();
    auto second = std::make_unique<Host>();
    auto expected = std::make_unique<ScalarEngine>();
    const double tiny = *control::widen(std::bit_cast<float>(std::uint32_t{1}));
#if defined(__SSE__)
    const auto csr = _mm_getcsr();
    _mm_setcsr(csr | 0x8040U);
#endif
    const auto positive = photo(first->processor, 40, true, tiny);
    const auto negative = photo(first->processor, 40, false,
                               std::bit_cast<double>(0x8000000000000001ULL));
#if defined(__SSE__)
    _mm_setcsr(csr);
#endif
    check(positive == sdk::kResultOk && negative == sdk::kResultFalse,
          "DAZ/FTZ changed message-domain validation");
    check(expected->note_on(40, tiny), "reference tiny positive strike");
    render(*first, *expected);
    flush(*second);
    check(first->processor.ui_key_held(40) && !second->processor.ui_key_held(40),
          "instances shared photo queue/ownership");
    release_photo(second->processor);
    flush(*second);
    render(*first, *expected);
    check(first->processor.ui_key_held(40), "other instance released photo note");
}

struct PortEngine {
    int strikes = 0;
    int releases = 0;
    int resets = 0;
    bool prepare(double) noexcept { return true; }
    void reset() noexcept { ++resets; }
    void set_sustain(bool) noexcept {}
    bool note_on(std::size_t key, double) noexcept {
        ++strikes;
        return key != 7;
    }
    bool note_off(std::size_t) noexcept { ++releases; return true; }
    void all_notes_off() noexcept {}
    bool process(std::span<double> output) noexcept {
        std::fill(output.begin(), output.end(), 0.0);
        return true;
    }
};

void direct_press_port() {
    BasicEventProcessor<PortEngine> processor;
    check(!processor.press_key(73, 0.5) && !processor.requires_reset(), "invalid port key faulted");
    check(!processor.press_key(1, std::bit_cast<double>(0x8000000000000001ULL)) &&
          !processor.requires_reset(), "invalid port velocity faulted");
    check(processor.engine().strikes == 0 && processor.frame_cursor() == 0, "invalid port mutated");
    check(processor.press_key(1, 0.25) && processor.press_key(1, 0.5), "port retrigger failed");
    check(processor.engine().strikes == 2 && processor.engine().resets == 0 &&
          processor.frame_cursor() == 0, "port reset history or advanced time");
    check(processor.release_key(1), "parallel release port failed");
    check(!processor.press_key(7, 0.25) && processor.requires_reset(), "port failure not fenced");
    check(!processor.press_key(1, 0.25) && !processor.release_key(1) &&
          processor.engine().strikes == 3 && processor.engine().releases == 1,
          "fault fence allowed further physical calls");
    std::array<double, 1> output{1.0};
    check(processor.process(output).status == ProcessStatus::reset_required && output[0] == 0.0,
          "port fault fence did not silence subsequent processing");
    processor.reset();
    check(processor.press_key(1, 0.25) && !processor.requires_reset(), "port reset did not recover");
}
} // namespace

int main() {
    try {
        direct_press_port();
        message_validation();
        overlap_and_boundary_order();
        zero_flush_and_emergency_isolation();
        aborted_preflight_and_recall();
        independent_capacity_and_overflow();
        floating_bits_and_instance_isolation();
        std::cout << "PASS photo audition model=" << kStateModel << " checks=" << checks
                  << " callbacks=" << callbacks << " frames=" << frames_compared
                  << " audio_new_calls=0\n";
    } catch (const std::exception& error) {
        audition_allocation_probe::enabled = false;
        std::cerr << "FAIL photo audition model=" << kStateModel << ": " << error.what() << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
