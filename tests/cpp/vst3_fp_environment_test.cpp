#include "tests/cpp/vst3_test_support.hpp"

#include <bit>
#include <cstring>
#include <iostream>

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
namespace host = rhodes::vst3::test_host;
namespace sdk = Steinberg;
namespace vst = Steinberg::Vst;
constexpr unsigned ftz = 1U << 15U;
constexpr unsigned daz = 1U << 6U;
// Independent precomputed IEEE-754 values: nearest binary64 1e-40, its
// nearest-even binary32 value, and the exact binary64 widening of that float.
constexpr std::uint64_t gain_input_bits = 0x37a16c262777579cULL;
constexpr std::uint32_t gain_bits = 0x000116c2U;
constexpr std::uint64_t gain_widened_bits = 0x37a16c2000000000ULL;
std::size_t checks = 0;
std::size_t failed_cases = 0;

void check(bool condition, std::string_view message) {
    ++checks;
    if (!condition) { throw std::runtime_error(std::string{message}); }
}

RHODES_TEST_NOINLINE double input_double(std::uint64_t bits) {
    volatile std::uint64_t runtime_bits = bits;
    return std::bit_cast<double>(static_cast<std::uint64_t>(runtime_bits));
}
RHODES_TEST_NOINLINE float input_float(std::uint32_t bits) {
    volatile std::uint32_t runtime_bits = bits;
    return std::bit_cast<float>(static_cast<std::uint32_t>(runtime_bits));
}
RHODES_TEST_NOINLINE sdk::tresult process(rhodes::vst3::RhodesProcessor& processor,
                                         vst::ProcessData& data) {
    return processor.process(data);
}
RHODES_TEST_NOINLINE sdk::tresult set_parameter(rhodes::vst3::RhodesController& controller,
                                               vst::ParamID id, double value) {
    return controller.setParamNormalized(id, value);
}

// Construct and inspect state bytes without the production codec or conversion
// helpers. A flush mode must never change the oracle's notion of zero.
void load_state(host::MemoryStream& stream, std::uint32_t gain, bool sustain) {
    stream.bytes = {};
    const std::array<std::uint8_t, 8> prefix{'R', 'H', 'S', 'T', 1, 0, 1, 0};
    std::copy(prefix.begin(), prefix.end(), stream.bytes.begin());
    for (std::size_t digit = 0; digit < 4; ++digit) {
        stream.bytes[8 + digit] = static_cast<std::uint8_t>((gain >> (8U * digit)) & 0xffU);
    }
    if (sustain) { stream.bytes[14] = 0x80U; stream.bytes[15] = 0x3fU; }
    stream.position = 0;
    stream.extent = 16;
}

void expect_stream(const host::MemoryStream& stream, std::uint32_t gain, bool sustain) {
    host::MemoryStream expected;
    load_state(expected, gain, sustain);
    check(stream.extent == 16, "serialized configuration size changed");
    for (std::size_t index = 0; index < 16; ++index) {
        check(stream.bytes[index] == expected.bytes[index], "serialized gain/pedal bits changed");
    }
}

void expect_gain(rhodes::vst3::RhodesProcessor& processor, std::uint32_t gain,
                 bool sustain = false) {
    for (const auto state : {processor.current_state(), processor.published_state()}) {
        check(std::bit_cast<std::uint32_t>(state.monitoring_gain) == gain,
              "live or published gain bits changed");
        check(state.sustain == sustain, "live or published sustain changed");
    }
    host::MemoryStream output;
    check(processor.getState(&output) == sdk::kResultOk, "processor state save failed");
    expect_stream(output, gain, sustain);
}

void expect_controller(rhodes::vst3::RhodesController& controller, bool sustain = false) {
    check(std::bit_cast<std::uint64_t>(controller.getParamNormalized(rhodes::vst3::kGainId)) ==
          gain_widened_bits, "controller did not preserve exact widened gain bits");
    check(std::bit_cast<std::uint64_t>(controller.getParamNormalized(rhodes::vst3::kSustainId)) ==
          (sustain ? 0x3ff0000000000000ULL : 0ULL), "controller pedal changed");
    host::MemoryStream output;
    check(controller.getState(&output) == sdk::kResultOk, "controller state save failed");
    expect_stream(output, gain_bits, sustain);
}

template <class Sample>
void expect_silence(const host::ProcessorHost<Sample, 4>& instance) {
    using Bits = std::conditional_t<std::is_same_v<Sample, float>, std::uint32_t, std::uint64_t>;
    for (std::size_t index = 0; index < instance.left.size(); ++index) {
        check(std::bit_cast<Bits>(instance.left[index]) == 0U &&
              std::bit_cast<Bits>(instance.right[index]) == 0U,
              "rejected block did not silence all stereo samples");
    }
    check(instance.output.silenceFlags == 3, "rejected block silence flags changed");
}

template <class Sample>
void seed(host::ProcessorHost<Sample, 4>& instance, host::EventList& events) {
    events.count = 1;
    events.values[0] = host::note_on(0, 7);
    instance.data.inputEvents = &events;
    check(process(instance.processor, instance.data) == sdk::kResultOk, "host seed note failed");
    instance.data.inputEvents = nullptr;
}

void controller_cases() {
    host::ControllerHost instance;
    check(set_parameter(instance.controller, rhodes::vst3::kGainId,
                        input_double(gain_input_bits)) == sdk::kResultOk,
          "controller tiny gain rejected");
    expect_controller(instance.controller);
    for (const auto id : {rhodes::vst3::kGainId, rhodes::vst3::kSustainId}) {
        check(set_parameter(instance.controller, id,
                            input_double(0x8000000000000001ULL)) != sdk::kResultOk,
              "controller accepted a negative subnormal control");
        expect_controller(instance.controller);
    }
    for (const bool component : {false, true}) {
        host::MemoryStream stream;
        load_state(stream, gain_bits, true);
        const auto result = component ? instance.controller.setComponentState(&stream) :
                                        instance.controller.setState(&stream);
        check(result == sdk::kResultOk, "controller tiny-gain recall failed");
        expect_controller(instance.controller, true);
    }
}

template <class Sample>
void invalid_note_cases() {
    for (const bool on : {false, true}) {
        for (const auto bits : {0x80000001U, 0x807fffffU}) {
            host::ProcessorHost<Sample, 4> instance;
            host::EventList events;
            seed(instance, events);
            const host::ProcessorSnapshot before(instance.processor);
            events.count = 2;
            events.values[0] = host::note_on(0, 8);
            events.values[1] = on ? host::note_on(3, 7) : host::note_off(3, 7);
            if (on) { events.values[1].noteOn.velocity = input_float(bits); }
            else { events.values[1].noteOff.velocity = input_float(bits); }
            instance.data.inputEvents = &events;
            instance.fill(Sample{9});
            check(process(instance.processor, instance.data) != sdk::kResultOk,
                  "host accepted a negative subnormal note velocity");
            expect_silence(instance);
            before.expect_unchanged(instance.processor);
        }
    }
}

template <class Sample>
void positive_note_cases() {
    for (const auto bits : {0x1U, 0x007fffffU}) {
        host::ProcessorHost<Sample, 4> instance;
        host::EventList events;
        seed(instance, events);
        for (const auto id : {7, 8}) {
            auto expected = instance.processor.engine_processor().engine();
            std::array<double, 4> output{};
            check(expected.process(output), "held-key reference render failed");
            events.values[0] = host::note_on(0, id, 68, input_float(bits));
            instance.data.inputEvents = &events;
            check(process(instance.processor, instance.data) == sdk::kResultOk,
                  "host positive tiny note rejected");
            const std::size_t wanted = id == 7 ? 1U : 2U;
            check(instance.processor.note_tracker().active_count() == wanted &&
                  instance.processor.note_tracker().held_count(40) == wanted,
                  "positive tiny host note lost a holder");
            host::same_mechanics(instance.processor.engine_processor().engine(), expected);
        }
        events.values[0] = host::note_off(0, 7);
        check(process(instance.processor, instance.data) == sdk::kResultOk &&
              instance.processor.note_tracker().held_count(40) == 1,
              "shared tiny note holder was lost");
        events.values[0] = host::note_off(0, 8);
        check(process(instance.processor, instance.data) == sdk::kResultOk &&
              instance.processor.note_tracker().active_count() == 0,
              "tiny note holder identity was not retained");
    }
}

template <class Sample>
void invalid_parameter_cases() {
    for (const auto id : {rhodes::vst3::kGainId, rhodes::vst3::kSustainId}) {
        host::ProcessorHost<Sample, 4> instance;
        host::EventList events;
        seed(instance, events);
        const host::ProcessorSnapshot before(instance.processor);
        host::ParameterQueue queue;
        queue.id = id;
        queue.count = 2;
        queue.values[0] = {0, 0.75};
        queue.values[1] = {3, input_double(0x8000000000000001ULL)};
        host::ParameterChanges changes;
        changes.count = 1;
        changes.values[0] = &queue;
        instance.data.inputParameterChanges = &changes;
        instance.fill(Sample{9});
        check(process(instance.processor, instance.data) != sdk::kResultOk,
              "host accepted a negative subnormal automation point");
        expect_silence(instance);
        before.expect_unchanged(instance.processor);
    }
}

template <class Sample>
void automation_cases() {
    for (const bool flush : {false, true}) {
        host::ProcessorHost<Sample, 4> instance;
        host::ParameterQueue queue;
        queue.count = 1;
        queue.values[0] = {0, input_double(gain_input_bits)};
        host::ParameterChanges changes;
        changes.count = 1;
        changes.values[0] = &queue;
        instance.data.inputParameterChanges = &changes;
        instance.data.numSamples = flush ? 0 : 4;
        check(process(instance.processor, instance.data) == sdk::kResultOk,
              "tiny gain automation rejected");
        expect_gain(instance.processor, gain_bits);
        const auto cursor = instance.processor.engine_processor().frame_cursor();
        instance.data.inputParameterChanges = nullptr;
        instance.data.numSamples = 4;
        check(process(instance.processor, instance.data) == sdk::kResultOk,
              "audio block after tiny automation failed");
        check(instance.processor.engine_processor().frame_cursor() == cursor + 4,
              "empty automation audio block did not advance normally");
        expect_gain(instance.processor, gain_bits);
    }
}

template <class Sample>
void recall_cases() {
    host::ProcessorHost<Sample, 4> instance;
    host::MemoryStream stream;
    load_state(stream, gain_bits, true);
    check(instance.processor.setState(&stream) == sdk::kResultOk, "tiny gain recall rejected");
    instance.data.numSamples = 0;
    check(process(instance.processor, instance.data) == sdk::kResultOk,
          "zero-frame recall flush failed");
    check(instance.processor.engine_processor().frame_cursor() == 0, "flush advanced the cursor");
    expect_gain(instance.processor, gain_bits, true);
    instance.data.numSamples = 4;
    check(process(instance.processor, instance.data) == sdk::kResultOk,
          "empty audio block after recall failed");
    check(instance.processor.engine_processor().frame_cursor() == 4,
          "empty recall audio block did not advance normally");
    expect_gain(instance.processor, gain_bits, true);
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
    run_case("controller", controller_cases);
    run_case("float invalid note", invalid_note_cases<float>);
    run_case("double invalid note", invalid_note_cases<double>);
    run_case("float positive note", positive_note_cases<float>);
    run_case("double positive note", positive_note_cases<double>);
    run_case("float invalid automation", invalid_parameter_cases<float>);
    run_case("double invalid automation", invalid_parameter_cases<double>);
    run_case("float automation", automation_cases<float>);
    run_case("double automation", automation_cases<double>);
    run_case("float recall", recall_cases<float>);
    run_case("double recall", recall_cases<double>);
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
    cases();
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
