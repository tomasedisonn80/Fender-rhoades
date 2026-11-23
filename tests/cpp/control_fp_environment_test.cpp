#include "rhodes/event_processor.hpp"
#include "rhodes/note_tracker.hpp"

#include <array>
#include <bit>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

#if defined(__SSE__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 1)
#define RHODES_TEST_MXCSR 1
#include <xmmintrin.h>
#if defined(_MSC_VER)
#include <immintrin.h>
#endif
#else
#define RHODES_TEST_MXCSR 0
#endif

#if defined(_MSC_VER)
#define RHODES_TEST_NOINLINE __declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
#define RHODES_TEST_NOINLINE __attribute__((noinline))
#else
#define RHODES_TEST_NOINLINE
#endif

namespace {
constexpr unsigned ftz = 1U << 15U;
constexpr unsigned daz = 1U << 6U;
constexpr std::size_t key = 40;
std::size_t checks = 0;
std::size_t failed_cases = 0;

void check(bool condition, std::string_view message) {
    ++checks;
    if (!condition) { throw std::runtime_error(std::string{message}); }
}

// Runtime bit inputs and noinline calls keep the former floating comparisons
// inside the selected environment. No control_value helper is a test oracle.
RHODES_TEST_NOINLINE double input(std::uint64_t bits) {
    volatile std::uint64_t runtime_bits = bits;
    return std::bit_cast<double>(static_cast<std::uint64_t>(runtime_bits));
}
RHODES_TEST_NOINLINE bool strike(rhodes::ResearchEngine& engine, double value) {
    return engine.note_on(key, value);
}
RHODES_TEST_NOINLINE rhodes::NoteResult track(rhodes::NoteTracker& notes,
                                             const rhodes::MidiNote& note) {
    return notes.apply(note);
}
RHODES_TEST_NOINLINE rhodes::ProcessResult process(
    rhodes::EventProcessor& engine, std::span<double> output,
    std::span<const rhodes::EngineEvent> events = {}) {
    return engine.process(output, events);
}

void same_mechanics(const rhodes::ResearchEngine& actual,
                    const rhodes::ResearchEngine& expected) {
    for (std::size_t note = 0; note < rhodes::key_count; ++note) {
        for (std::size_t mode = 0; mode < rhodes::modes_per_key; ++mode) {
            const auto a = actual.modal_state(note, mode);
            const auto b = expected.modal_state(note, mode);
            check(a.has_value() && b.has_value(), "missing modal state");
            check(std::bit_cast<std::uint64_t>(a->displacement_m) ==
                  std::bit_cast<std::uint64_t>(b->displacement_m) &&
                  std::bit_cast<std::uint64_t>(a->velocity_m_per_s) ==
                  std::bit_cast<std::uint64_t>(b->velocity_m_per_s),
                  "rejected control changed mechanical state");
        }
    }
}

void same_future(rhodes::ResearchEngine actual, rhodes::ResearchEngine expected) {
    std::array<double, 17> a{}, b{};
    check(actual.process(a) && expected.process(b), "continuation failed");
    for (std::size_t index = 0; index < a.size(); ++index) {
        check(std::bit_cast<std::uint64_t>(a[index]) ==
              std::bit_cast<std::uint64_t>(b[index]), "held/released damping changed");
    }
    same_mechanics(actual, expected);
}

rhodes::ResearchEngine ringing_engine() {
    rhodes::ResearchEngine engine;
    check(strike(engine, 0.75), "seed strike failed");
    std::array<double, 4> output{};
    check(engine.process(output), "seed render failed");
    return engine;
}

rhodes::EventProcessor ringing_processor() {
    rhodes::EventProcessor engine;
    const std::array events{rhodes::EngineEvent{0, 0, rhodes::EventType::note_on, key, 0.75}};
    std::array<double, 4> output{};
    check(process(engine, output, events).succeeded(), "seed event failed");
    return engine;
}

void engine_cases() {
    for (const auto bits : {0x8000000000000001ULL, 0x800fffffffffffffULL}) {
        auto engine = ringing_engine();
        const auto before = engine;
        check(!strike(engine, input(bits)), "negative subnormal engine strike accepted");
        same_mechanics(engine, before);
        same_future(engine, before);
    }
    for (const auto bits : {0x1ULL, 0x000fffffffffffffULL}) {
        auto engine = ringing_engine();
        const auto before = engine;
        check(strike(engine, input(bits)), "positive tiny engine strike rejected");
        // A tiny impulse cannot change this already normal-size motion, but it
        // must leave the key held. Do not require tiny audio to survive FTZ.
        same_future(engine, before);
    }
    for (const auto bits : {0ULL, 0x8000000000000000ULL}) {
        auto engine = ringing_engine();
        auto released = engine;
        check(released.note_off(key), "reference release failed");
        check(strike(engine, input(bits)), "signed zero strike rejected");
        same_future(engine, released);
    }
}

void tracker_cases() {
    for (const auto bits : {0x8000000000000001ULL, 0x800fffffffffffffULL}) {
        for (const bool on : {false, true}) {
            rhodes::NoteTracker notes;
            check(track(notes, {true, 0, 68, 7, 0.75}).succeeded(), "seed holder failed");
            const auto rejected = track(notes, {on, 0, 68, 7, input(bits)});
            check(rejected.status == rhodes::NoteStatus::invalid_input && !rejected.change,
                  "negative subnormal tracker control accepted");
            check(notes.active_count() == 1 && notes.held_count(key) == 1,
                  "rejected tracker control changed ownership");
            const auto release = track(notes, {false, 0, 68, 7, 0.0});
            check(release.succeeded() && release.change &&
                  release.change->type == rhodes::EventType::note_off &&
                  notes.active_count() == 0, "rejection damaged holder identity");
        }
    }
    for (const auto bits : {0x1ULL, 0x000fffffffffffffULL}) {
        rhodes::NoteTracker notes;
        check(track(notes, {true, 0, 68, 7, 0.75}).succeeded(), "seed holder failed");
        for (const auto id : {7, 8}) {
            const auto accepted = track(notes, {true, 0, 68, id, input(bits)});
            check(accepted.succeeded() && accepted.change &&
                  accepted.change->type == rhodes::EventType::note_on,
                  "positive tiny tracker strike became a release");
            check(std::bit_cast<std::uint64_t>(accepted.change->value) == bits,
                  "tracker altered positive tiny intensity bits");
            const std::size_t wanted = id == 7 ? 1U : 2U;
            check(notes.active_count() == wanted && notes.held_count(key) == wanted,
                  "positive tiny tracker strike lost a holder");
        }
        const auto first = track(notes, {false, 0, 68, 7, 0.0});
        check(first.succeeded() && !first.change && notes.held_count(key) == 1,
              "first shared holder release stopped the physical key");
        const auto last = track(notes, {false, 0, 68, 8, 0.0});
        check(last.succeeded() && last.change &&
              last.change->type == rhodes::EventType::note_off && notes.active_count() == 0,
              "tiny holder identity was not retained");
    }
}

void event_rejection_cases() {
    for (const auto type : {rhodes::EventType::note_on, rhodes::EventType::note_off,
                           rhodes::EventType::sustain, rhodes::EventType::all_notes_off,
                           rhodes::EventType::reset}) {
        for (const auto magnitude : {0x1ULL, 0x000fffffffffffffULL}) {
            for (const bool negative : {false, true}) {
                if (type == rhodes::EventType::note_on && !negative) { continue; }
                auto engine = ringing_processor();
                const auto before = engine;
                const auto bits = magnitude | (negative ? 0x8000000000000000ULL : 0ULL);
                const bool keyed = type == rhodes::EventType::note_on ||
                                   type == rhodes::EventType::note_off;
                const std::array events{
                    rhodes::EngineEvent{0, 0, rhodes::EventType::note_on, key + 1, 0.5},
                    rhodes::EngineEvent{2, 1, type, keyed ? key : 0U, input(bits)}};
                std::array<double, 4> output{9.0, 9.0, 9.0, 9.0};
                const auto result = process(engine, output, events);
                check(result.status == rhodes::ProcessStatus::invalid_input &&
                      result.failing_event == 1 && result.frames_processed == 0,
                      "late malformed subnormal event was not rejected before processing");
                check(engine.frame_cursor() == before.frame_cursor() &&
                      engine.requires_reset() == before.requires_reset(),
                      "rejected event changed cursor or fault latch");
                for (const auto value : output) {
                    check(std::bit_cast<std::uint64_t>(value) == 0U,
                          "invalid event did not silence the entire output");
                }
                same_mechanics(engine.engine(), before.engine());
                same_future(engine.engine(), before.engine());
            }
        }
    }
}

void event_positive_cases() {
    for (const auto bits : {0x1ULL, 0x000fffffffffffffULL}) {
        auto actual = ringing_processor();
        auto expected = actual;
        const std::array events{rhodes::EngineEvent{
            0, 0, rhodes::EventType::note_on, key, input(bits)}};
        std::array<double, 4> a{}, b{};
        check(process(actual, a, events).succeeded() && process(expected, b).succeeded(),
              "positive tiny event failed");
        check(actual.frame_cursor() == expected.frame_cursor(), "tiny event changed cursor");
        for (std::size_t index = 0; index < a.size(); ++index) {
            check(std::bit_cast<std::uint64_t>(a[index]) ==
                  std::bit_cast<std::uint64_t>(b[index]), "tiny event released a held key");
        }
        same_future(actual.engine(), expected.engine());
    }
}

template <class Function>
void run_case(std::string_view name, Function function) {
    try { function(); }
    catch (const std::exception& error) {
        ++failed_cases;
        std::cerr << "  " << name << ": " << error.what() << '\n';
    }
}

void cases() {
    run_case("engine", engine_cases);
    run_case("tracker", tracker_cases);
    run_case("event rejection", event_rejection_cases);
    run_case("positive event", event_positive_cases);
}

#if RHODES_TEST_MXCSR
struct RestoreControls {
    unsigned original = _mm_getcsr();
    ~RestoreControls() { _mm_setcsr(original); }
};

unsigned supported_controls() {
    alignas(16) std::array<unsigned char, 512> saved{};
#if defined(_MSC_VER)
    _fxsave(saved.data());
#else
    asm volatile("fxsave %0" : "=m"(saved));
#endif
    std::uint32_t mask = 0;
    std::memcpy(&mask, saved.data() + 28, sizeof(mask));
    return mask;
}
#endif

bool run_mode(std::string_view name, unsigned controls) {
#if RHODES_TEST_MXCSR
    const auto original = _mm_getcsr();
    if ((controls & ~supported_controls()) != 0U) {
        std::cout << "SKIP " << name << ": requested MXCSR controls unavailable\n";
        return false;
    }
    {
        RestoreControls restore;
        // Use nearest rounding and mask exceptions for the fixture. Restore
        // every original control and status bit, including after a failure.
        const auto requested = (original & ~(ftz | daz | 0x603fU)) | controls | 0x1f80U;
        _mm_setcsr(requested);
        check(_mm_getcsr() == requested, "MXCSR write did not take effect");
        cases();
    }
    check(_mm_getcsr() == original, "MXCSR was not restored exactly");
#else
    if (controls != 0U) {
        std::cout << "SKIP " << name << ": x86 MXCSR controls unavailable\n";
        return false;
    }
    cases(); // Portable baseline still exercises actual control entry points.
#endif
    std::cout << (failed_cases == 0 ? "PASS " : "FAIL ") << name << " ("
              << checks << " always-active checks, " << failed_cases << " failed cases)\n";
    return true;
}
} // namespace

int main(int argc, char** argv) {
    const std::string_view selected = argc > 1 ? argv[1] : "all";
    try {
        constexpr std::array<std::string_view, 4> names{"baseline", "ftz", "daz", "ftz_daz"};
        constexpr std::array<unsigned, 4> controls{0U, ftz, daz, ftz | daz};
        bool found = false, ran = false, failed = false;
        for (std::size_t index = 0; index < names.size(); ++index) {
            if (selected != "all" && selected != names[index]) { continue; }
            found = true;
            checks = failed_cases = 0;
            ran = run_mode(names[index], controls[index]) || ran;
            failed = failed || failed_cases != 0;
        }
        check(found, "unknown floating-point environment selector");
        return failed ? 1 : (ran ? 0 : 77);
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << selected << ": " << error.what() << '\n';
        return 1;
    }
}
