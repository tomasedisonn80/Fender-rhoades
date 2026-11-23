#include "rhodes/quadrature_filter_coefficients.hpp"
#include "rhodes/research_engine.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <numbers>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace {
using Engine = rhodes::ResearchEngine;
using Plan = rhodes::FractionalObservationPlan;
using Fractions = std::array<double, rhodes::observation_node_count>;
using HeldKeys = std::array<bool, rhodes::key_count>;
using Motion = std::array<std::array<rhodes::ModalState, rhodes::modes_per_key>,
                          rhodes::key_count>;
constexpr auto nodes = rhodes::quadrature_filter::nodes;
constexpr long double voltage_tolerance = 1.0e-13L;

void require(bool condition, std::string_view message) {
    if (!condition) { throw std::runtime_error(std::string{message}); }
}

void same(double actual, double expected, std::string_view message) {
    require(std::bit_cast<std::uint64_t>(actual) ==
                std::bit_cast<std::uint64_t>(expected), message);
}

std::array<double, 9> rates() {
    const double infinity = std::numeric_limits<double>::infinity();
    return {32000.0, 44100.0, 48000.0, std::nextafter(48000.0, infinity),
            64000.0, 88200.0, 96000.0, std::nextafter(96000.0, infinity),
            192000.0};
}

Motion motion(const Engine& engine) {
    Motion result{};
    for (std::size_t key = 0; key < rhodes::key_count; ++key) {
        for (std::size_t mode = 0; mode < rhodes::modes_per_key; ++mode) {
            const auto state = engine.modal_state(key, mode);
            require(state.has_value(), "valid modal-state index rejected");
            result[key][mode] = *state;
        }
    }
    return result;
}

void same_motion(const Engine& actual, const Motion& expected) {
    const auto states = motion(actual);
    for (std::size_t key = 0; key < rhodes::key_count; ++key) {
        for (std::size_t mode = 0; mode < rhodes::modes_per_key; ++mode) {
            same(states[key][mode].displacement_m, expected[key][mode].displacement_m,
                 "observation/preparation changed displacement bits");
            same(states[key][mode].velocity_m_per_s, expected[key][mode].velocity_m_per_s,
                 "observation/preparation changed velocity bits");
        }
    }
}

void advance(Engine& engine, std::size_t frames) {
    std::array<double, 256> output{};
    require(frames <= output.size(), "fixture advance exceeds scratch buffer");
    require(engine.process(std::span<double>{output}.first(frames)), "process failed");
}

void same_future(Engine& actual, Engine& expected) {
    std::array<double, 129> a{}, b{};
    require(actual.process(a) && expected.process(b), "continuation failed");
    for (std::size_t i = 0; i < a.size(); ++i) {
        same(a[i], b[i], "observation/preparation changed future output bits");
    }
    same_motion(actual, motion(expected));
}

struct Fixture {
    // Plans and engines are deliberately heap-owned: native Windows tests use
    // a 1 MiB default thread stack, and each prepared plan is about 84 KiB.
    std::unique_ptr<Engine> engine = std::make_unique<Engine>();
    std::unique_ptr<Plan> plan = std::make_unique<Plan>();
    HeldKeys held{};
    bool sustain = false;

    explicit Fixture(double rate) {
        require(engine->prepare(rate), "valid rate rejected");
        require(engine->prepare_observation_plan(nodes, *plan), "valid nodes rejected");
    }
    void strike(std::size_t key, double intensity) {
        require(engine->note_on(key, intensity), "valid strike rejected");
        held[key] = intensity != 0.0;
    }
    void strike_all() {
        for (std::size_t key = 0; key < rhodes::key_count; ++key) {
            strike(key, 0.1 + 0.9 * static_cast<double>(key % 7) / 6.0);
        }
    }
    void release_even() {
        for (std::size_t key = 0; key < rhodes::key_count; key += 2) {
            require(engine->note_off(key), "valid release rejected");
            held[key] = false;
        }
    }
    void pedal(bool down) {
        engine->set_sustain(down);
        sustain = down;
    }
    void all_off() {
        engine->all_notes_off();
        held.fill(false);
    }
};

// Independent amplitude-form solution of q'' + 2*beta*q' + omega^2*q = 0.
// It neither inspects prepared matrices nor calls the oscillator or pickup
// helpers. Constants describe the declared synthetic model, not a specimen.
long double reference_voltage(const Engine& engine, const HeldKeys& held,
                              bool sustain, double fraction) {
    constexpr std::array<long double, 3> ratios{1.0L, 2.73L, 5.41L};
    constexpr std::array<long double, 3> observation{1.0L, 0.3L, 0.08L};
    constexpr std::array<long double, 3> free_loss{0.8L, 3.0L, 7.0L};
    constexpr std::array<long double, 3> damped_loss{30.0L, 35.0L, 45.0L};
    const long double seconds = static_cast<long double>(fraction) /
                                static_cast<long double>(engine.sample_rate_hz());
    long double series_voltage = 0.0L;
    for (std::size_t key = 0; key < rhodes::key_count; ++key) {
        const long double fundamental = 440.0L *
            std::exp2((static_cast<long double>(key) - 41.0L) / 12.0L);
        long double displacement = 0.0L;
        long double velocity = 0.0L;
        for (std::size_t mode = 0; mode < rhodes::modes_per_key; ++mode) {
            const auto state = engine.modal_state(key, mode);
            require(state.has_value(), "reference modal-state lookup failed");
            const long double beta = (held[key] || sustain)
                ? free_loss[mode] : damped_loss[mode];
            const long double omega = 2.0L * std::numbers::pi_v<long double> *
                                      fundamental * ratios[mode];
            const long double wd = std::sqrt(omega * omega - beta * beta);
            const long double a = static_cast<long double>(state->displacement_m);
            const long double b = (static_cast<long double>(state->velocity_m_per_s) +
                                   beta * a) / wd;
            const long double cosine = std::cos(wd * seconds);
            const long double sine = std::sin(wd * seconds);
            const long double envelope = std::exp(-beta * seconds);
            const long double q = envelope * (a * cosine + b * sine);
            const long double v = envelope * ((b * wd - beta * a) * cosine -
                                               (a * wd + beta * b) * sine);
            displacement += observation[mode] * q;
            velocity += observation[mode] * v;
        }
        // First combine modal motion, then differentiate the local polynomial
        // linkage. All silent branches remain in the full resistive network.
        const long double pickup_voltage = -velocity *
            (0.4L + 1600.0L * displacement +
             1200000.0L * displacement * displacement);
        series_voltage += pickup_voltage / (key < 4 ? 4.0L : 3.0L);
    }
    // 1 Mohm / (1 Mohm + 200 ohm * (1/4 + 23/3)), reduced independently.
    return series_voltage * 12000.0L / 12019.0L;
}

struct AnalyticChecks {
    std::size_t count = 0;
    long double maximum_error = 0.0L;

    void check(const Fixture& fixture, const Fractions& fractions = nodes) {
        for (std::size_t node = 0; node < fractions.size(); ++node) {
            const auto actual = fixture.engine->observe(*fixture.plan, node);
            require(actual.has_value() && std::isfinite(*actual),
                    "prepared observation missing or nonfinite");
            const long double expected = reference_voltage(*fixture.engine, fixture.held,
                                                          fixture.sustain, fractions[node]);
            const long double error = std::abs(static_cast<long double>(*actual) - expected);
            maximum_error = std::max(maximum_error, error);
            require(error <= voltage_tolerance, "independent loaded-voltage mismatch");
            ++count;
        }
    }
    void print() const {
        std::cout << count << " independent observations; maximum error="
                  << std::setprecision(9) << maximum_error << " V\n";
    }
};

void analytic_nodes() {
    AnalyticChecks checks;
    constexpr std::array<std::size_t, 3> keys{0, 36, 72};
    constexpr std::array<double, 3> intensities{0.02, 0.37, 1.0};
    for (const double rate : rates()) {
        for (std::size_t scenario = 0; scenario < 4; ++scenario) {
            Fixture fixture{rate};
            if (scenario < keys.size()) {
                fixture.strike(keys[scenario], intensities[scenario]);
            } else {
                fixture.strike_all();
            }
            checks.check(fixture); // Includes the exact strike boundary.
            advance(*fixture.engine, 17);
            checks.check(fixture);
            advance(*fixture.engine, 137);
            checks.check(fixture);
        }
    }
    require(checks.count == 648, "analytic fixture coverage changed");
    checks.print();
}

void plan_domains() {
    Fixture fixture{48000.0};
    fixture.strike_all();
    advance(*fixture.engine, 97);
    fixture.release_even();
    const auto untouched = std::make_unique<Engine>(*fixture.engine);
    const auto original_motion = motion(*fixture.engine);
    const auto empty_plan = std::make_unique<Plan>();
    std::array<double, rhodes::observation_node_count> expected{};
    for (std::size_t node = 0; node < nodes.size(); ++node) {
        require(!fixture.engine->observe(*empty_plan, node), "default plan accepted");
        const auto value = fixture.engine->observe(*fixture.plan, node);
        require(value.has_value(), "baseline observation failed");
        expected[node] = *value;
    }
    require(!fixture.engine->observe(*fixture.plan, nodes.size()), "one-past node accepted");
    require(!fixture.engine->observe(*fixture.plan, std::numeric_limits<std::size_t>::max()),
            "maximum node index accepted");

    const auto preserved = [&] {
        for (std::size_t node = 0; node < nodes.size(); ++node) {
            const auto value = fixture.engine->observe(*fixture.plan, node);
            require(value.has_value(), "rejection invalidated prior plan");
            same(*value, expected[node], "rejected preparation changed plan results");
        }
        same_motion(*fixture.engine, original_motion);
        same(fixture.engine->sample_rate_hz(), 48000.0, "rejection changed rate");
    };
    const auto rejected = [&](const Fractions& fractions) {
        require(!fixture.engine->prepare_observation_plan(fractions, *fixture.plan),
                "invalid fractions accepted");
        preserved();
    };
    const double infinity = std::numeric_limits<double>::infinity();
    const double tiny = std::numeric_limits<double>::denorm_min();
    const std::array<double, 12> invalid{
        0.0, -0.0, -tiny, -0.1, -1.0, 1.0, std::nextafter(1.0, infinity),
        std::numeric_limits<double>::max(), infinity, -infinity,
        std::numeric_limits<double>::quiet_NaN(),
        -std::numeric_limits<double>::quiet_NaN()};
    for (std::size_t node = 0; node < nodes.size(); ++node) {
        for (const double value : invalid) {
            auto fractions = nodes;
            fractions[node] = value;
            rejected(fractions);
        }
    }
    for (std::size_t node = 1; node < nodes.size(); ++node) {
        auto fractions = nodes;
        fractions[node] = fractions[node - 1];
        rejected(fractions);
        fractions[node] = std::nextafter(fractions[node - 1], 0.0);
        rejected(fractions);
    }
    auto descending = nodes;
    std::reverse(descending.begin(), descending.end());
    rejected(descending);

    const std::array<double, 8> invalid_rates{
        0.0, -0.0, -1.0, std::nextafter(32000.0, 0.0),
        std::nextafter(192000.0, infinity), infinity, -infinity,
        std::numeric_limits<double>::quiet_NaN()};
    for (const double rate : invalid_rates) {
        require(!fixture.engine->prepare(rate), "invalid engine rate accepted");
        preserved();
    }
    for (const double rate : rates()) {
        const auto other = std::make_unique<Engine>();
        require(other->prepare(rate), "comparison engine preparation failed");
        for (std::size_t node = 0; node < nodes.size(); ++node) {
            const auto result = other->observe(*fixture.plan, node);
            if (rate == 48000.0) {
                require(result.has_value() && *result == 0.0,
                        "same-rate plan reuse rejected");
            } else {
                require(!result, "different or adjacent sample rate accepted");
            }
        }
    }
    preserved();
    same_future(*fixture.engine, *untouched);

    // Strictly adjacent positive numbers are valid, even when a fractional
    // duration underflows to zero. An endpoint at 1.0 remains invalid.
    AnalyticChecks checks;
    const std::array<Fractions, 3> valid{
        Fractions{tiny, std::nextafter(tiny, infinity),
                  std::numeric_limits<double>::min(), 0.25, 0.5,
                  std::nextafter(1.0, 0.0)},
        Fractions{0.1, 0.2, 0.3, 0.5, std::nextafter(0.5, infinity), 0.9},
        Fractions{0.1, 0.2, 0.3, 0.4,
                  std::nextafter(std::nextafter(1.0, 0.0), 0.0),
                  std::nextafter(1.0, 0.0)}};
    for (const auto& fractions : valid) {
        require(fixture.engine->prepare_observation_plan(fractions, *fixture.plan),
                "valid adjacent/boundary fractions rejected");
        checks.check(fixture, fractions);
    }
    checks.print();
}

void observation_nonmutation() {
    for (const double rate : rates()) {
        Fixture fixture{rate};
        fixture.strike_all();
        advance(*fixture.engine, 137);
        fixture.release_even();
        const auto reference = std::make_unique<Engine>(*fixture.engine);
        const auto alternative = std::make_unique<Plan>();
        constexpr Fractions alternate_nodes{0.07, 0.2, 0.4, 0.6, 0.8, 0.95};
        for (std::size_t stage = 0; stage < 6; ++stage) {
            if (stage == 1) {
                fixture.engine->set_sustain(true);
                reference->set_sustain(true);
            } else if (stage == 2) {
                fixture.engine->all_notes_off();
                reference->all_notes_off();
            } else if (stage == 3) {
                fixture.engine->set_sustain(false);
                reference->set_sustain(false);
            } else if (stage == 4) {
                require(fixture.engine->note_on(72, 0.91) && reference->note_on(72, 0.91),
                        "moving retrigger failed");
            } else if (stage == 5) {
                require(fixture.engine->note_on(72, 0.0) && reference->note_on(72, 0.0),
                        "zero-intensity release failed");
            }
            const auto before = motion(*fixture.engine);
            const double energy = fixture.engine->mechanical_energy_joules();
            require(fixture.engine->prepare_observation_plan(alternate_nodes, *alternative),
                    "alternate preparation failed");
            for (std::size_t pass = 0; pass < 3; ++pass) {
                for (std::size_t node = nodes.size(); node-- > 0;) {
                    require(fixture.engine->observe(*fixture.plan, node).has_value() &&
                            fixture.engine->observe(*alternative, node).has_value(),
                            "repeated observation failed");
                }
            }
            auto bad = alternate_nodes;
            bad[4] = bad[3];
            require(!fixture.engine->prepare_observation_plan(bad, *alternative),
                    "duplicate fractional node accepted");
            require(!fixture.engine->observe(*alternative, nodes.size()),
                    "invalid repeated observation accepted");
            same_motion(*fixture.engine, before);
            same(fixture.engine->mechanical_energy_joules(), energy,
                 "observation/preparation changed mechanical energy");
            same_future(*fixture.engine, *reference);
        }
    }
}

void damping_regimes() {
    AnalyticChecks checks;
    for (const double rate : rates()) {
        Fixture fixture{rate};
        fixture.strike_all();
        advance(*fixture.engine, 137);
        checks.check(fixture); // Held: free decay.

        auto before = motion(*fixture.engine);
        fixture.release_even();
        same_motion(*fixture.engine, before);
        checks.check(fixture); // Mixed released and held keys.
        advance(*fixture.engine, 11);
        checks.check(fixture);

        before = motion(*fixture.engine);
        fixture.pedal(true);
        same_motion(*fixture.engine, before);
        checks.check(fixture); // Already released notes return to free decay.
        advance(*fixture.engine, 11);

        before = motion(*fixture.engine);
        fixture.all_off();
        same_motion(*fixture.engine, before);
        checks.check(fixture); // Pedal still holds every unheld note.

        before = motion(*fixture.engine);
        fixture.pedal(false);
        same_motion(*fixture.engine, before);
        checks.check(fixture); // All keys damped.
        advance(*fixture.engine, 41);

        before = motion(*fixture.engine);
        require(std::abs(before[72][0].displacement_m) > 1.0e-10 &&
                std::abs(before[72][0].velocity_m_per_s) > 1.0e-7,
                "retrigger fixture is not moving");
        fixture.strike(72, 0.66);
        constexpr std::array<double, 3> couplings{1.0, 0.45, 0.2};
        for (std::size_t mode = 0; mode < rhodes::modes_per_key; ++mode) {
            const auto after = fixture.engine->modal_state(72, mode);
            require(after.has_value(), "retrigger state unavailable");
            same(after->displacement_m, before[72][mode].displacement_m,
                 "moving retrigger reset displacement");
            require(std::abs(after->velocity_m_per_s - before[72][mode].velocity_m_per_s -
                             0.06 * 0.66 * couplings[mode]) <= 1.0e-16,
                    "moving retrigger did not retain velocity and add impulse");
        }
        checks.check(fixture);

        before = motion(*fixture.engine);
        fixture.strike(72, 0.0);
        same_motion(*fixture.engine, before);
        checks.check(fixture); // A zero-intensity strike releases the key.

        fixture.engine->reset();
        fixture.held.fill(false);
        fixture.sustain = false;
        same_motion(*fixture.engine, Motion{});
        same(fixture.engine->sample_rate_hz(), rate, "reset changed plan's rate");
        for (std::size_t node = 0; node < nodes.size(); ++node) {
            const auto value = fixture.engine->observe(*fixture.plan, node);
            require(value.has_value() && *value == 0.0, "reset did not reuse valid plan");
        }
        Fixture fresh{rate};
        for (const std::size_t key : {std::size_t{0}, std::size_t{36}, std::size_t{72}}) {
            fixture.strike(key, 0.73);
            fresh.strike(key, 0.73);
        }
        checks.check(fixture);
        for (std::size_t node = 0; node < nodes.size(); ++node) {
            const auto actual = fixture.engine->observe(*fixture.plan, node);
            const auto expected = fresh.engine->observe(*fresh.plan, node);
            require(actual.has_value() && expected.has_value(), "post-reset plan failed");
            same(*actual, *expected, "reused plan differs from freshly prepared plan");
        }
        same_future(*fixture.engine, *fresh.engine);
    }
    require(checks.count == 486, "damping fixture coverage changed");
    checks.print();
}

using Test = void (*)();
constexpr std::array<std::pair<std::string_view, Test>, 4> tests{{
    {"analytic_nodes", analytic_nodes}, {"plan_domains", plan_domains},
    {"observation_nonmutation", observation_nonmutation},
    {"damping_regimes", damping_regimes}
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
    if (!found) { std::cerr << "Unknown fractional-observation test selector\n"; }
    return found ? 0 : 1;
}
