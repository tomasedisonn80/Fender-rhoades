#include "rhodes/research_engine.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace {
void require(bool condition, std::string_view message) {
    if (!condition) { throw std::runtime_error(std::string{message}); }
}

void same(double actual, double expected, std::string_view message) {
    require(std::bit_cast<std::uint64_t>(actual) == std::bit_cast<std::uint64_t>(expected),
            message);
}

void same_state(rhodes::ModalState actual, rhodes::ModalState expected) {
    same(actual.displacement_m, expected.displacement_m, "displacement bits changed");
    same(actual.velocity_m_per_s, expected.velocity_m_per_s, "velocity bits changed");
}

// A freshly configured oscillator cannot reuse a transition from an earlier
// decay, frequency or rate. Comparing complete trajectories therefore tests
// cache selection without copying or inspecting the cache implementation.
void compare_selection(rhodes::ModalOscillator& cached, rhodes::ModalParameters parameters,
                       double rate, double decay) {
    const auto before = cached.state();
    const double energy = cached.energy_joules();
    require(cached.set_decay(decay), "valid decay selection rejected");
    same_state(cached.state(), before);
    same(cached.energy_joules(), energy, "decay selection changed energy bits");
    parameters.decay_per_s = decay;
    rhodes::ModalOscillator fresh;
    require(fresh.configure(parameters, rate) && fresh.set_state(before),
            "fresh independent configuration rejected");
    for (int sample = 0; sample < 37; ++sample) {
        require(cached.advance() && fresh.advance(), "comparison advance failed");
        same_state(cached.state(), fresh.state());
        same(cached.energy_joules(), fresh.energy_joules(), "fresh trajectory energy differs");
    }
}

void cached_vs_fresh() {
    constexpr rhodes::ModalState initial{0.0001, -0.03};
    for (double rate : {32000.0, 44100.0, 48000.0, 96000.0, 192000.0}) {
        for (double frequency : {0.0001, 55.0, 440.0, 0.449999 * rate}) {
            const double omega = 2.0 * std::numbers::pi * frequency;
            const double near_critical = std::nextafter(omega, 0.0);
            const rhodes::ModalParameters parameters{frequency, 0.0, 0.001};
            rhodes::ModalOscillator cached;
            require(cached.configure(parameters, rate) && cached.set_state(initial),
                    "cache fixture configuration failed");
            // Covers a repeated active value, alternation, replacement by a
            // third value, zero and the closest supported critical boundary.
            for (double decay : {0.0, 0.001 * omega, 0.0, 0.001 * omega,
                                 0.25 * omega, 0.001 * omega, 0.0,
                                 near_critical, 0.0, near_critical,
                                 0.8 * omega, near_critical, 0.8 * omega}) {
                compare_selection(cached, parameters, rate, decay);
            }
        }
    }
}

void invalid_cache_updates() {
    constexpr double nan = std::numeric_limits<double>::quiet_NaN();
    constexpr double inf = std::numeric_limits<double>::infinity();
    rhodes::ModalOscillator unconfigured;
    require(!unconfigured.set_decay(0.0), "unconfigured zero decay accepted");
    rhodes::ModalOscillator actual;
    require(actual.configure({440.0, 3.0, 0.001}, 48000.0) &&
            actual.set_state({0.0002, -0.07}) && actual.set_decay(30.0) &&
            actual.set_decay(3.0), "invalid-update fixture setup failed");
    auto expected = actual;
    const auto initial = actual.state();
    const double energy = actual.energy_joules();
    for (double decay : {-1.0, nan, inf, 2.0 * std::numbers::pi * 440.0,
                         std::numeric_limits<double>::max()}) {
        require(!actual.set_decay(decay), "invalid decay accepted");
        same_state(actual.state(), initial);
        same(actual.energy_joules(), energy, "invalid decay changed energy");
    }
    require(!actual.configure({0.0, 30.0, 0.001}, 48000.0) &&
            !actual.configure({440.0, 30.0, 0.0}, 48000.0) &&
            !actual.configure({440.0, 30.0, 0.001}, 22050.0) &&
            !actual.configure({440.0, inf, 0.001}, 48000.0),
            "invalid configuration accepted");
    for (double decay : {30.0, 3.0, 30.0, 7.0, 3.0, 30.0}) {
        require(actual.set_decay(decay) && expected.set_decay(decay),
                "valid selection after rejection failed");
        for (int sample = 0; sample < 31; ++sample) {
            require(actual.advance() && expected.advance(), "post-rejection advance failed");
            same_state(actual.state(), expected.state());
        }
    }
}

void reconfigure_cache() {
    rhodes::ModalOscillator cached;
    require(cached.configure({440.0, 3.0, 0.001}, 48000.0) && cached.set_decay(30.0) &&
            cached.set_decay(3.0), "reconfigure prewarm failed");
    for (const auto& [parameters, rate] :
         std::array<std::pair<rhodes::ModalParameters, double>, 4>{{
             {{880.0, 3.0, 2.0}, 96000.0}, {{220.0, 30.0, 0.000001}, 32000.0},
             {{440.0, 3.0, 1000000.0}, 192000.0}, {{440.0, 30.0, 0.001}, 48000.0}}}) {
        require(cached.configure(parameters, rate), "valid reconfiguration rejected");
        same_state(cached.state(), {});
        require(cached.set_state({0.0001, -0.03}), "reconfigured state rejected");
        for (double decay : {3.0, 30.0, 3.0, 7.0, 30.0}) {
            compare_selection(cached, parameters, rate, decay);
        }
        cached.reset();
        same_state(cached.state(), {});
        require(cached.set_state({-0.0002, 0.08}), "state after reset rejected");
        compare_selection(cached, parameters, rate, 3.0);
    }
}

void redundant_pedal() {
    for (double rate : {32000.0, 48000.0, 192000.0}) {
        rhodes::ResearchEngine a, b;
        require(a.prepare(rate) && b.prepare(rate), "engine setup failed");
        for (std::size_t key = 0; key < rhodes::key_count; ++key) {
            require(a.note_on(key, 0.7) && b.note_on(key, 0.7), "initial strike failed");
        }
        for (int block = 0; block < 64; ++block) {
            const bool down = block % 2 == 0;
            a.set_sustain(down);
            b.set_sustain(down);
            for (int repeat = 0; repeat < 256; ++repeat) { a.set_sustain(down); }
            if (block == 1 || block == 31) { a.all_notes_off(); b.all_notes_off(); }
            if (block == 17) {
                require(a.note_on(40, 0.6) && b.note_on(40, 0.6), "retrigger failed");
            }
            std::array<double, 64> actual{}, expected{};
            require(a.process(actual) && b.process(expected), "engine render failed");
            for (std::size_t frame = 0; frame < actual.size(); ++frame) {
                same(actual[frame], expected[frame], "redundant pedal changed output bits");
            }
            same(a.mechanical_energy_joules(), b.mechanical_energy_joules(),
                 "redundant pedal changed energy bits");
        }
        a.reset(); b.reset();
        a.set_sustain(false);
        require(a.note_on(0, 0.7) && b.note_on(0, 0.7), "post-reset strike failed");
        std::array<double, 64> actual{}, expected{};
        require(a.process(actual) && b.process(expected) && actual == expected,
                "cache/reset changed the next strike");
    }
}

using Test = void (*)();
constexpr std::array<std::pair<std::string_view, Test>, 4> tests{{
    {"cached_vs_fresh", cached_vs_fresh}, {"invalid_cache_updates", invalid_cache_updates},
    {"reconfigure_cache", reconfigure_cache}, {"redundant_pedal", redundant_pedal}
}};
} // namespace

int main(int argc, char** argv) {
    const std::string_view selected = argc > 1 ? argv[1] : "all";
    bool found = false;
    for (const auto& [name, test] : tests) {
        if (selected != "all" && selected != name) { continue; }
        found = true;
        try {
            test();
            std::cout << "PASS " << name << '\n';
        } catch (const std::exception& error) {
            std::cerr << "FAIL " << name << ": " << error.what() << '\n';
            return 1;
        }
    }
    if (!found) { std::cerr << "Unknown cache test selector\n"; }
    return found ? 0 : 1;
}
