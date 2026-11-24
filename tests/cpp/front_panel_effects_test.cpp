#include "rhodes/front_panel_effects.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <limits>
#include <new>
#include <numbers>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace allocation_probe {
bool enabled = false;
std::size_t calls = 0;
}

void* operator new(std::size_t size) {
    if (allocation_probe::enabled) { ++allocation_probe::calls; }
    if (void* memory = std::malloc(size == 0 ? 1 : size)) { return memory; }
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }
void operator delete[](void* memory, std::size_t) noexcept { std::free(memory); }

namespace {
using rhodes::FrontPanelEffects;
using rhodes::FrontPanelState;
using rhodes::StereoSample;
constexpr std::array<double, 5> rates{32000.0, 44100.0, 48000.0, 96000.0, 192000.0};
std::size_t checks = 0;

void require(bool condition, std::string_view message) {
    ++checks;
    if (!condition) { throw std::runtime_error(std::string{message}); }
}

bool same_bits(double first, double second) {
    return std::bit_cast<std::uint64_t>(first) == std::bit_cast<std::uint64_t>(second);
}

void identical(StereoSample value, double input) {
    require(same_bits(value.left, input) && same_bits(value.right, input),
            "bypass changed an input bit or zero sign");
}

void identical(StereoSample first, StereoSample second) {
    require(same_bits(first.left, second.left) && same_bits(first.right, second.right),
            "replay differs after identical operations");
}

void near(double actual, long double expected, long double tolerance,
          std::string_view message) {
    require(std::isfinite(actual) && std::abs(static_cast<long double>(actual) - expected) <= tolerance,
            message);
}

long double gain(float normalized) {
    return std::pow(10.0L, (24.0L * static_cast<long double>(normalized) - 12.0L) / 20.0L);
}

// Frequency-domain oracle: evaluate analog shelf ratios after the prewarped
// bilinear frequency mapping. It does not use production coefficients, filter
// history, ramp helpers, or an alternative copy of the processing recurrence.
std::complex<long double> response(FrontPanelState state, double fs, double hz) {
    const long double gb = gain(state.bass);
    const long double gt = gain(state.treble);
    if (hz == 0.0) { return {gb, 0.0L}; }
    if (hz == fs * 0.5) { return {gt, 0.0L}; }
    const long double mapped = std::tan(std::numbers::pi_v<long double> * hz / fs);
    const long double bass_ratio = mapped / std::tan(std::numbers::pi_v<long double> * 200.0L / fs);
    const long double treble_ratio = mapped / std::tan(std::numbers::pi_v<long double> * 2500.0L / fs);
    return std::complex<long double>{gb, bass_ratio} /
               std::complex<long double>{1.0L, bass_ratio} *
           (std::complex<long double>{1.0L, gt * treble_ratio} /
               std::complex<long double>{1.0L, treble_ratio});
}

void identity() {
    constexpr std::array<double, 14> inputs{
        0.0, -0.0, 1.0, -1.0, 0.1, -0.1, 123.0, -8192.5,
        std::numeric_limits<double>::denorm_min(), -std::numeric_limits<double>::denorm_min(),
        std::numeric_limits<double>::min(), -std::numeric_limits<double>::min(),
        std::numeric_limits<double>::max(), -std::numeric_limits<double>::max()};
    const std::array<FrontPanelState, 5> bypass_states{{
        {}, {0.5F, 0.5F, 1.0F, 1.0F, false}, {0.5F, 0.5F, 0.0F, 0.0F, true},
        {0.5F, 0.5F, -0.0F, 1.0F, true}, {0.5F, 0.5F, 0.75F, 0.0F, false}}};
    FrontPanelEffects effect;
    require(effect.sample_rate_hz() == 48000.0, "default sample rate differs");
    require(effect.latency_frames() == 0, "effects added a frame delay");
    for (double fs : rates) {
        require(effect.prepare(fs), "supported rate rejected");
        for (const auto state : bypass_states) {
            effect.reset(state);
            for (std::size_t repeat = 0; repeat < 8; ++repeat) {
                for (double input : inputs) { identical(effect.process(input, state), input); }
            }
        }
        effect.reset();
        identical(effect.process(1.0, {}), 1.0);
        identical(effect.process(-0.0, {}), -0.0);
        // Inactive intensity/speed automation cannot make an audio change.
        for (std::size_t n = 0; n < 256; ++n) {
            FrontPanelState state;
            state.intensity = (n & 1U) == 0 ? 1.0F : 0.0F;
            state.speed = (n & 2U) == 0 ? 1.0F : 0.0F;
            const double input = inputs[n % inputs.size()];
            identical(effect.process(input, state), input);
        }
        require(!effect.requires_reset(), "neutral finite input raised a fault");
    }
}

void frequency_response() {
    constexpr std::array<std::pair<float, float>, 8> settings{{
        {0.0F, 0.5F}, {1.0F, 0.5F}, {0.5F, 0.0F}, {0.5F, 1.0F},
        {0.0F, 1.0F}, {1.0F, 0.0F}, {0.0F, 0.0F}, {1.0F, 1.0F}}};
    long double worst = 0.0L;
    std::size_t cases = 0;
    FrontPanelEffects effect;
    for (double fs : rates) {
        require(effect.prepare(fs), "response rate rejected");
        const std::array<double, 6> frequencies{0.0, 40.0, 200.0, 2500.0, 8000.0, fs * 0.5};
        const auto settle = static_cast<std::size_t>(fs * 0.1);
        for (const auto& [bass, treble] : settings) {
            const FrontPanelState state{bass, treble, 0.0F, 0.5F, false};
            for (double hz : frequencies) {
                effect.reset(state);
                const auto transfer = response(state, fs, hz);
                for (std::size_t n = 0; n < settle + 512; ++n) {
                    const long double angle = 2.0L * std::numbers::pi_v<long double> * hz * n / fs;
                    const long double cosine = hz == fs * 0.5 ? ((n & 1U) == 0 ? 1.0L : -1.0L) :
                                               std::cos(angle);
                    const long double sine = hz == fs * 0.5 ? 0.0L : std::sin(angle);
                    const auto output = effect.process(static_cast<double>(cosine), state);
                    require(std::isfinite(output.left) && same_bits(output.left, output.right),
                            "tone-only response is nonfinite or differs across channels");
                    if (n < settle) { continue; }
                    const long double expected = transfer.real() * cosine - transfer.imag() * sine;
                    const long double error = std::abs(static_cast<long double>(output.left) - expected);
                    worst = std::max(worst, error);
                    require(error <= 2.0e-11L, "tone differs from independent frequency-domain oracle");
                }
                ++cases;
            }
        }
    }
    std::cout << "response_cases=" << cases << " maximum_absolute_error=" << worst << '\n';
}

void tremolo() {
    for (double fs : rates) {
        FrontPanelEffects effect;
        require(effect.prepare(fs), "tremolo rate rejected");
        for (float normalized_speed : {0.0F, 1.0F}) {
            const long double hz = normalized_speed == 0.0F ? 0.5L : 12.0L;
            for (float depth : {0.25F, 1.0F}) {
                const FrontPanelState state{0.5F, 0.5F, depth, normalized_speed, true};
                effect.reset(state);
                const auto count = static_cast<std::size_t>(std::ceil(2.0L * fs / hz));
                double minimum_left = 1.0;
                double maximum_left = 0.0;
                for (std::size_t n = 0; n < count; ++n) {
                    const long double wave = std::sin(2.0L * std::numbers::pi_v<long double> * hz * n / fs);
                    const auto output = effect.process(1.0, state);
                    near(output.left, 1.0L - 0.5L * depth * (1.0L + wave), 2.0e-10L,
                         "left tremolo disagrees with absolute-time sine oracle");
                    near(output.right, 1.0L - 0.5L * depth * (1.0L - wave), 2.0e-10L,
                         "right tremolo disagrees with absolute-time sine oracle");
                    near(0.5 * output.left + 0.5 * output.right, 1.0L - 0.5L * depth, 2.0e-16L,
                         "opposed gains did not have constant mono sum");
                    require(output.left >= 0.0 && output.left <= 1.0 &&
                            output.right >= 0.0 && output.right <= 1.0,
                            "tremolo escaped its nonnegative bounded gain range");
                    minimum_left = std::min(minimum_left, output.left);
                    maximum_left = std::max(maximum_left, output.left);
                }
                require(minimum_left <= 1.0 - depth + 2.0e-6 && maximum_left >= 1.0 - 2.0e-6,
                        "tremolo failed to reach expected gain endpoints");
            }
        }
    }
}

void transitions() {
    for (double fs : {32000.0, 44100.5, 48000.0, 192000.0}) {
        FrontPanelEffects effect;
        require(effect.prepare(fs), "transition rate rejected");
        const auto ramp = static_cast<std::size_t>(std::ceil(fs / 100.0));
        require(effect.smoothing_frames() == ramp, "smoothing duration is not 10 ms rounded up");
        FrontPanelState state{0.5F, 0.5F, 1.0F, 0.0F, true};
        // The mono sum removes the oscillator, independently exposing depth.
        for (std::size_t n = 0; n < ramp; ++n) {
            const auto output = effect.process(1.0, state);
            const long double expected = 1.0L - 0.5L * static_cast<long double>(n + 1) / ramp;
            near(0.5 * output.left + 0.5 * output.right, expected, 2.0e-14L,
                 "depth activation was not a finite linear ramp");
        }
        for (std::size_t n = 0; n < 10; ++n) {
            const auto output = effect.process(1.0, state);
            near(0.5 * output.left + 0.5 * output.right, 0.5L, 2.0e-16L,
                 "depth did not settle at its exact target");
        }
        state.vibrato = false;
        for (std::size_t n = 0; n < ramp; ++n) {
            const auto output = effect.process(1.0, state);
            const long double expected = 0.5L + 0.5L * static_cast<long double>(n + 1) / ramp;
            near(0.5 * output.left + 0.5 * output.right, expected, 2.0e-14L,
                 "disable was not a smooth finite depth ramp");
        }
        identical(effect.process(-0.0, state), -0.0);
        identical(effect.process(std::numeric_limits<double>::max(), state),
                  std::numeric_limits<double>::max());

        // Speed changes integrate the linearly changing instantaneous rate;
        // compare to a closed-form arithmetic-series phase, not a phase loop.
        state = {0.5F, 0.5F, 1.0F, 0.0F, true};
        effect.reset(state);
        state.speed = 1.0F;
        for (std::size_t n = 0; n < ramp + 400; ++n) {
            const long double k = static_cast<long double>(std::min(n, ramp));
            const long double cycles = (0.5L * k + 11.5L * k * (k + 1.0L) /
                (2.0L * ramp) + 12.0L * static_cast<long double>(n - std::min(n, ramp))) / fs;
            const long double wave = std::sin(2.0L * std::numbers::pi_v<long double> * cycles);
            const auto output = effect.process(1.0, state);
            near(output.left, 0.5L * (1.0L - wave), 2.0e-13L, "speed ramp phase differs");
        }

        // A settled DC signal independently reveals the gain ramp. A changing
        // inactive speed must not restart that ramp.
        state = {0.0F, 0.5F, 0.0F, 0.5F, false};
        effect.reset(state);
        for (std::size_t n = 0; n < static_cast<std::size_t>(fs * 0.1); ++n) {
            (void)effect.process(1.0, state);
        }
        const long double first_gain = gain(0.0F);
        const long double last_gain = gain(1.0F);
        state.bass = 1.0F;
        for (std::size_t n = 0; n < ramp; ++n) {
            state.speed = (n & 1U) == 0 ? 1.0F : 0.0F;
            const auto output = effect.process(1.0, state);
            near(output.left, first_gain + (last_gain - first_gain) * (n + 1) / ramp,
                 5.0e-13L, "bass gain ramp is discontinuous or restarted by another control");
        }

        // Every return-to-neutral ramp terminates in an exact bit-preserving
        // path, including while filter history and oscillator phase were live.
        state = {0.0F, 1.0F, 0.75F, 1.0F, true};
        effect.reset(state);
        for (std::size_t n = 0; n < 1000; ++n) { (void)effect.process(0.25, state); }
        for (std::size_t n = 0; n < ramp; ++n) {
            const auto output = effect.process(0.25, {});
            require(std::isfinite(output.left) && std::isfinite(output.right),
                    "neutral transition became nonfinite");
            if (n + 1 == ramp) { identical(output, 0.25); }
        }
        for (double input : {0.0, -0.0, 0.25, -0.75}) { identical(effect.process(input, {}), input); }

        // Retargeting from the middle of a depth ramp preserves its current
        // value and reaches the new target in one more 10 ms interval.
        effect.reset();
        state = {0.5F, 0.5F, 1.0F, 0.5F, true};
        const auto halfway = ramp / 2;
        for (std::size_t n = 0; n < halfway; ++n) { (void)effect.process(1.0, state); }
        state.intensity = 0.0F;
        const long double initial_depth = static_cast<long double>(halfway) / ramp;
        for (std::size_t n = 0; n < ramp; ++n) {
            const auto output = effect.process(1.0, state);
            const long double depth = initial_depth * (1.0L - static_cast<long double>(n + 1) / ramp);
            near(0.5 * output.left + 0.5 * output.right, 1.0L - depth * 0.5L, 2.0e-14L,
                 "mid-ramp retarget lost continuity");
        }
        identical(effect.process(-0.0, state), -0.0);
    }
}

void prepare_atomicity() {
    FrontPanelEffects effect;
    const std::array<double, 8> accepted{
        32000.0, std::nextafter(32000.0, 192000.0), 44100.5, 48000.0,
        88200.0, 96000.0, std::nextafter(192000.0, 32000.0), 192000.0};
    for (double fs : accepted) {
        require(effect.prepare(fs) && effect.sample_rate_hz() == fs, "supported rate edge rejected");
    }
    require(effect.prepare(44100.5), "atomicity setup rejected");
    FrontPanelState state{0.1F, 0.9F, 0.8F, 0.7F, true};
    effect.reset(state);
    for (std::size_t n = 0; n < 1000; ++n) {
        (void)effect.process(std::sin(static_cast<double>(n) * 0.11), state);
    }
    state.bass = 0.9F;
    state.speed = 0.1F;
    (void)effect.process(0.3, state); // Retain an in-progress ramp as well as history.
    FrontPanelEffects reference = effect;
    const std::array<double, 10> rejected{
        -1.0, 0.0, -0.0, 8000.0, std::nextafter(32000.0, 0.0),
        std::nextafter(192000.0, std::numeric_limits<double>::infinity()),
        384000.0, std::numeric_limits<double>::quiet_NaN(),
        std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity()};
    for (double fs : rejected) {
        require(!effect.prepare(fs), "unsupported rate accepted");
        require(effect.sample_rate_hz() == 44100.5 &&
                effect.smoothing_frames() == reference.smoothing_frames(), "rejected prepare changed setup");
        for (std::size_t n = 0; n < 257; ++n) {
            const double input = std::cos(static_cast<double>(n) * 0.13);
            identical(effect.process(input, state), reference.process(input, state));
        }
    }
    require(effect.prepare(48000.0), "successful reprepare failed");
    FrontPanelEffects fresh;
    for (std::size_t n = 0; n < 2048; ++n) {
        const double input = std::sin(static_cast<double>(n) * 0.13);
        identical(effect.process(input, state), fresh.process(input, state));
    }
}

void reset_replay() {
    FrontPanelEffects effect;
    FrontPanelEffects reference;
    const FrontPanelState initial{0.125F, 0.875F, 0.75F, 1.0F, true};
    for (double fs : rates) {
        require(effect.prepare(fs) && reference.prepare(fs), "reset rate rejected");
        effect.reset(initial);
        for (std::size_t n = 0; n < 5000; ++n) {
            (void)effect.process(std::cos(static_cast<double>(n) * 0.19), initial);
        }
        effect.reset(initial);
        reference.reset(initial);
        for (std::size_t n = 0; n < 4096; ++n) {
            auto state = initial;
            if (n > 300) { state.bass = 0.75F; }
            if (n > 600) { state.intensity = 0.25F; }
            if (n > 1300) { state.speed = 0.0F; }
            if (n > 2300) { state = {}; }
            const double input = std::sin(static_cast<double>(n) * 0.17);
            identical(effect.process(input, state), reference.process(input, state));
        }
        effect.reset();
        identical(effect.process(-0.0, {}), -0.0);
        identical(effect.process(1.0, {}), 1.0);
    }
}

void faults() {
    const auto is_failure = [](StereoSample sample) {
        return std::isnan(sample.left) && std::isnan(sample.right);
    };
    FrontPanelEffects effect;
    for (double invalid : {std::numeric_limits<double>::quiet_NaN(),
                           std::numeric_limits<double>::infinity(),
                           -std::numeric_limits<double>::infinity()}) {
        effect.reset();
        require(is_failure(effect.process(invalid, {})) && effect.requires_reset(),
                "nonfinite input was hidden");
        require(is_failure(effect.process(1.0, {})), "neutral bypass hid a latched fault");
        require(!effect.prepare(0.0) && effect.requires_reset(), "rejected prepare cleared a fault");
        effect.reset();
        identical(effect.process(-0.0, {}), -0.0);
    }
    const std::array<float, 6> invalid_controls{
        -std::numeric_limits<float>::denorm_min(), -0.01F, std::nextafter(1.0F, 2.0F),
        std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(),
        -std::numeric_limits<float>::infinity()};
    for (float invalid : invalid_controls) {
        for (std::size_t field = 0; field < 4; ++field) {
            FrontPanelState state;
            if (field == 0) { state.bass = invalid; }
            if (field == 1) { state.treble = invalid; }
            if (field == 2) { state.intensity = invalid; }
            if (field == 3) { state.speed = invalid; }
            effect.reset();
            require(is_failure(effect.process(0.0, state)) && effect.requires_reset(),
                    "invalid control was silently bypassed or clamped");
            effect.reset(state);
            require(effect.requires_reset() && is_failure(effect.process(1.0, {})),
                    "invalid reset supplied a healthy configuration");
            require(effect.prepare(48000.0) && !effect.requires_reset(), "valid prepare did not recover");
        }
    }
    const FrontPanelState boost{1.0F, 1.0F, 0.0F, 0.5F, false};
    effect.reset(boost);
    require(is_failure(effect.process(std::numeric_limits<double>::max(), boost)),
            "overflowing finite input was clipped or silently accepted");
    require(effect.requires_reset() && is_failure(effect.process(0.0, {})),
            "arithmetic fault did not remain visible");
    effect.reset();
    identical(effect.process(std::numeric_limits<double>::max(), {}),
              std::numeric_limits<double>::max());
}

void allocation_free() {
    static_assert(std::is_trivially_copyable_v<FrontPanelEffects>);
    static_assert(sizeof(FrontPanelEffects) <= 256);
    FrontPanelEffects effect;
    allocation_probe::calls = 0;
    allocation_probe::enabled = true;
    bool okay = true;
    for (double fs : rates) {
        okay = effect.prepare(fs) && okay;
        for (std::size_t n = 0; n < 8192; ++n) {
            const FrontPanelState state{(n & 1U) == 0 ? 0.0F : 1.0F,
                (n & 2U) == 0 ? 0.0F : 1.0F, (n & 4U) == 0 ? 0.0F : 1.0F,
                (n & 8U) == 0 ? 0.0F : 1.0F, (n & 16U) != 0};
            if (n % 113 == 0) { effect.reset(state); }
            const auto output = effect.process((n & 1U) == 0 ? 0.3 : -0.2, state);
            okay = std::isfinite(output.left) && std::isfinite(output.right) && okay;
        }
    }
    allocation_probe::enabled = false;
    require(okay, "bounded control stress failed");
    require(allocation_probe::calls == 0, "effects allocated through C++ new/new[]");
    std::cout << "effects_bytes=" << sizeof(FrontPanelEffects)
              << " monitored_cpp_allocations=" << allocation_probe::calls << '\n';
}

using Test = void (*)();
constexpr std::array<std::pair<std::string_view, Test>, 8> tests{{
    {"identity", identity}, {"frequency_response", frequency_response},
    {"tremolo", tremolo}, {"transitions", transitions},
    {"prepare_atomicity", prepare_atomicity}, {"reset_replay", reset_replay},
    {"faults", faults}, {"allocation_free", allocation_free}}};
} // namespace

int main(int argc, char** argv) {
    const std::string_view selected = argc > 1 ? argv[1] : "all";
    try {
        require(argc <= 2, "usage: front_panel_effects_test [fixture|all]");
        std::cout << std::setprecision(17);
        bool found = false;
        for (const auto& [name, test] : tests) {
            if (selected != "all" && selected != name) { continue; }
            checks = 0;
            test();
            std::cout << "PASS " << name << " (" << checks << " checks)\n";
            found = true;
        }
        require(found, "unknown front-panel-effects fixture");
        return 0;
    } catch (const std::exception& error) {
        allocation_probe::enabled = false;
        std::cerr << "FAIL " << selected << ": " << error.what() << '\n';
        return 1;
    }
}
