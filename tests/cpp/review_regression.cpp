#include "rhodes/research_engine.hpp"

#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace {
void require(bool condition, std::string_view message) {
    if (!condition) { throw std::runtime_error(std::string{message}); }
}

void near(double actual, long double expected, long double tolerance,
          std::string_view message) {
    require(std::isfinite(actual) && std::isfinite(expected) &&
            std::abs(static_cast<long double>(actual) - expected) <= tolerance, message);
}

void same_state(rhodes::ModalState actual, rhodes::ModalState expected) {
    require(actual.displacement_m == expected.displacement_m &&
            actual.velocity_m_per_s == expected.velocity_m_per_s,
            "rejected operation changed mechanical state");
}

void modal_boundaries() {
    constexpr rhodes::ModalState initial{0.0001, -0.03};
    for (double rate : {32000.0, 44100.0, 192000.0}) {
        for (double frequency : {0.0001, 440.0, 0.449999 * rate}) {
            for (double fraction : {0.0, 0.001, 0.8, 1.0 - 1e-12}) {
                const double decay = fraction * 2.0 * std::numbers::pi * frequency;
                rhodes::ModalOscillator mode;
                require(mode.configure({frequency, decay, 0.001}, rate),
                        "supported modal boundary rejected");
                require(mode.set_state(initial), "set boundary initial condition");
                // Independent scalar closed form, not the implementation's 2x2 matrix.
                // Wider arithmetic helps on hosts where long double is wider; the
                // tolerance also supports MSVC's same-width long double.
                const long double omega = 2.0L * std::numbers::pi_v<long double> * frequency;
                const long double beta = decay;
                const long double wd = std::sqrt(omega * omega - beta * beta);
                const long double a = initial.displacement_m;
                const long double b = (initial.velocity_m_per_s + beta * a) / wd;
                for (int frame = 1; frame <= 257; ++frame) {
                    require(mode.advance(), "advance boundary trajectory");
                    if (frame != 1 && frame != 17 && frame != 257) { continue; }
                    const long double time = static_cast<long double>(frame) / rate;
                    const long double sine = std::sin(wd * time);
                    const long double cosine = std::cos(wd * time);
                    const long double envelope = std::exp(-beta * time);
                    const long double q = envelope * (a * cosine + b * sine);
                    const long double v = -beta * q + envelope * wd * (-a * sine + b * cosine);
                    near(mode.state().displacement_m, q, 2e-14L + 3e-12L * std::abs(q),
                         "boundary displacement disagrees with scalar reference");
                    near(mode.state().velocity_m_per_s, v, 2e-11L + 3e-12L * std::abs(v),
                         "boundary velocity disagrees with scalar reference");
                }
            }
        }
    }
}

void rejected_updates() {
    constexpr double nan = std::numeric_limits<double>::quiet_NaN();
    constexpr double inf = std::numeric_limits<double>::infinity();
    constexpr double largest = std::numeric_limits<double>::max();
    rhodes::ModalOscillator mode;
    require(!mode.advance() && !mode.add_impulse(0.1) && !mode.set_decay(1.0) &&
            !mode.set_state({0.0, 0.0}), "unconfigured operations must reject");
    require(mode.configure({440.0, 3.0, 0.001}, 48000.0), "configure atomicity fixture");
    require(mode.set_state({0.0002, -0.03}), "initialize atomicity fixture");
    auto control = mode;
    const auto before = mode.state();
    for (double rate : {std::nextafter(32000.0, 0.0), std::nextafter(192000.0, inf), nan, inf}) {
        require(!mode.configure({440.0, 3.0, 0.001}, rate), "reject invalid rate");
    }
    const std::array<rhodes::ModalParameters, 12> invalid{{
        {0.0, 3.0, 0.001}, {-1.0, 3.0, 0.001}, {nan, 3.0, 0.001},
        {inf, 3.0, 0.001}, {21600.0, 3.0, 0.001},
        {440.0, -1.0, 0.001}, {440.0, nan, 0.001}, {440.0, inf, 0.001},
        {440.0, 3.0, 0.0}, {440.0, 3.0, -1.0},
        {440.0, 3.0, nan}, {440.0, 3.0, inf}
    }};
    for (const auto& parameters : invalid) {
        require(!mode.configure(parameters, 48000.0), "reject invalid modal parameter");
    }
    for (double decay : {-1.0, nan, inf, 2.0 * std::numbers::pi * 440.0}) {
        require(!mode.set_decay(decay), "reject invalid or critically damped decay");
    }
    for (const auto state : std::array<rhodes::ModalState, 4>{{
            {nan, 0.0}, {0.0, inf}, {largest, 0.0}, {0.0, largest}}}) {
        require(!mode.set_state(state), "reject nonfinite or overflowing state");
    }
    for (double impulse : {nan, inf, largest}) {
        require(!mode.add_impulse(impulse), "reject nonfinite or overflowing impulse");
    }
    same_state(mode.state(), before);
    for (int frame = 0; frame < 128; ++frame) {
        require(mode.advance() && control.advance(), "advance atomicity comparison");
        same_state(mode.state(), control.state());
    }
    rhodes::PolynomialPickup pickup;
    const auto voltage = pickup.voltage(0.0002, 0.01);
    require(!pickup.configure(nan, 3.0, 4.0), "reject pickup reconfiguration");
    require(pickup.voltage(0.0002, 0.01) == voltage, "pickup rejection changed coefficients");
    rhodes::SyntheticResistiveHarp harp;
    std::array<double, rhodes::key_count> sources{};
    sources[0] = 1.0;
    const auto loaded = harp.voltage(sources);
    require(!harp.configure(largest, 1.0) && !harp.configure(1.0, nan),
            "reject overflowing or nonfinite network parameters");
    require(harp.voltage(sources) == loaded, "network rejection changed parameters");
}

void engine_controls() {
    constexpr double inf = std::numeric_limits<double>::infinity();
    constexpr double nan = std::numeric_limits<double>::quiet_NaN();
    rhodes::ResearchEngine engine;
    engine.set_sustain(true);
    require(engine.note_on(0, 0.7) && engine.note_on(72, 0.8), "initialize endpoint keys");
    std::array<double, 31> warmup{};
    require(engine.process(warmup), "warm up engine controls fixture");
    auto control = engine;
    for (double rate : {std::nextafter(32000.0, 0.0), std::nextafter(192000.0, inf), nan, inf}) {
        require(!engine.prepare(rate), "reject invalid engine sample rate");
    }
    for (double intensity : {-0.001, 1.001, nan, inf}) {
        require(!engine.note_on(0, intensity), "reject invalid strike intensity");
    }
    require(!engine.note_on(rhodes::key_count, 0.5) &&
            !engine.note_off(std::numeric_limits<std::size_t>::max()), "reject invalid keys");
    require(!engine.modal_state(rhodes::key_count, 0) &&
            !engine.modal_state(0, rhodes::modes_per_key), "reject invalid state indices");
    require(engine.process(std::span<double>{}), "empty processing span should succeed");
    require(engine.sample_rate_hz() == control.sample_rate_hz(), "rejected prepare changed rate");
    std::array<double, 128> actual{}, expected{};
    require(engine.process(actual) && control.process(expected) && actual == expected,
            "rejected engine operation or empty span changed trajectory");
    require(engine.note_on(0, 0.0) && control.note_off(0), "zero-intensity release");
    engine.all_notes_off();
    require(control.note_off(72), "release remaining control key");
    require(engine.process(actual) && control.process(expected) && actual == expected,
            "release or all-notes-off changed sustain behavior");
    engine.set_sustain(false);
    control.set_sustain(false);
    require(engine.process(actual) && control.process(expected) && actual == expected,
            "pedal-up trajectory differs after equivalent releases");
    require(engine.prepare(32000.0), "prepare accepted endpoint resets engine");
    require(engine.mechanical_energy_joules() == 0.0, "prepare retained mechanical energy");
}

void engine_combined_pickup() {
    // Independent contract from docs/NUMERICS.md: these are synthetic values,
    // not specimen measurements or parameters read from the production helpers.
    static_assert(rhodes::key_count == 73 && rhodes::modes_per_key == 3);
    constexpr std::array<long double, 3> observation{1.0L, 0.3L, 0.08L};
    constexpr long double branch_ohm = 200.0L;
    constexpr long double load_ohm = 1000000.0L;
    const auto emf = [](long double x, long double velocity) {
        // -d(0.4*x + 800*x^2 + 400000*x^3)/dt, in volts.
        return -(0.4L * velocity + 1600.0L * x * velocity +
                 1200000.0L * x * x * velocity);
    };

    rhodes::ResearchEngine engine;
    require(engine.prepare(48000.0), "prepare combined-pickup fixture");
    // Two sources in the first four-branch group, one across its boundary,
    // and two later three-branch groups leave 68 silent branches connected.
    constexpr std::array<std::size_t, 5> excited_keys{0, 3, 4, 37, 72};
    constexpr std::array<double, 5> intensities{0.81, 0.57, 0.69, 0.93, 0.76};
    for (std::size_t i = 0; i < excited_keys.size(); ++i) {
        require(engine.note_on(excited_keys[i], intensities[i]),
                "excite combined-pickup fixture");
    }

    std::size_t observations = 0;
    long double maximum_error = 0.0L;
    long double minimum_separate_gap = std::numeric_limits<long double>::infinity();
    const auto check_next_sample = [&] {
        // Finish the entire snapshot before calling process: output observes
        // these states at t, not the advanced states at t + 1/Fs.
        std::array<std::array<rhodes::ModalState, 3>, 73> snapshot{};
        for (std::size_t key = 0; key < snapshot.size(); ++key) {
            for (std::size_t mode = 0; mode < observation.size(); ++mode) {
                const auto state = engine.modal_state(key, mode);
                require(state.has_value(), "read all pre-sample modal states");
                snapshot[key][mode] = *state;
            }
        }
        for (const auto key : excited_keys) {
            for (const auto state : snapshot[key]) {
                require(std::abs(state.displacement_m) > 1e-12 &&
                        std::abs(state.velocity_m_per_s) > 1e-8,
                        "fixture must exercise all three moving modes per excited key");
            }
        }

        std::array<long double, 73> sources{}, separately_converted{};
        for (std::size_t key = 0; key < snapshot.size(); ++key) {
            long double x = 0.0L;
            long double velocity = 0.0L;
            for (std::size_t mode = 0; mode < observation.size(); ++mode) {
                const long double modal_x = observation[mode] * snapshot[key][mode].displacement_m;
                const long double modal_v = observation[mode] * snapshot[key][mode].velocity_m_per_s;
                x += modal_x;
                velocity += modal_v;
                // Deliberately incorrect alternative: loses nonlinear cross terms.
                separately_converted[key] += emf(modal_x, modal_v);
            }
            sources[key] = emf(x, velocity);
        }

        // Solve each parallel group by KCL, then connect the 24 Thevenin
        // equivalents in series. Every branch contributes conductance even
        // when its source is zero; neither production electrical helper is used.
        long double open_circuit = 0.0L;
        long double separate_open_circuit = 0.0L;
        long double series_ohm = 0.0L;
        std::size_t key = 0;
        for (std::size_t group = 0; group < 24; ++group) {
            const std::size_t branches = group == 0 ? 4 : 3;
            long double conductance = 0.0L;
            long double source_current = 0.0L;
            long double separate_source_current = 0.0L;
            for (std::size_t branch = 0; branch < branches; ++branch, ++key) {
                conductance += 1.0L / branch_ohm;
                source_current += sources[key] / branch_ohm;
                separate_source_current += separately_converted[key] / branch_ohm;
            }
            open_circuit += source_current / conductance;
            separate_open_circuit += separate_source_current / conductance;
            series_ohm += 1.0L / conductance;
        }
        require(key == snapshot.size(), "network oracle must retain all 73 branches");
        const long double expected = open_circuit * load_ohm / (series_ohm + load_ohm);
        const long double separate = separate_open_circuit * load_ohm / (series_ohm + load_ohm);
        const long double tolerance = 2e-14L + 2e-12L * std::abs(expected);
        const long double separate_gap = std::abs(expected - separate);
        require(separate_gap > 1e-6L && separate_gap > 1000000.0L * tolerance,
                "fixture cannot distinguish conversion before modal summation");

        std::array<double, 1> output{};
        require(engine.process(output), "process observed combined-pickup sample");
        near(output[0], expected, tolerance,
             "engine output disagrees with combined-flux and loaded-network oracle");
        const long double error = std::abs(static_cast<long double>(output[0]) - expected);
        if (error > maximum_error) { maximum_error = error; }
        if (separate_gap < minimum_separate_gap) { minimum_separate_gap = separate_gap; }
        ++observations;
    };

    std::array<double, 257> warmup{};
    for (const std::size_t frames : {31U, 113U, 257U}) {
        require(engine.process(std::span<double>{warmup}.first(frames)),
                "advance combined-pickup fixture to a different phase");
        check_next_sample();
        require(engine.note_on(3, 0.23) && engine.note_on(4, 0.31) &&
                engine.note_on(72, 0.17), "retrigger moving combined-pickup fixture");
        check_next_sample();
    }
    std::cout << "combined-pickup observations=" << observations
              << " max_error_V=" << maximum_error
              << " min_separate_conversion_gap_V=" << minimum_separate_gap << '\n';
}

void network_extreme_divider() {
    rhodes::SyntheticResistiveHarp harp;
    std::array<double, rhodes::key_count> sources{};
    // The mathematical result is representable even though source/load overflows;
    // the second case also underflows a naive reciprocal load/source to zero.
    struct Case { double load; double source; long double expected; };
    constexpr std::array<Case, 2> cases{{
        {1e-20, 1e100, 3.15789473684210526316e-222L},
        {1e-100, 1e300, 3.15789473684210526316e-102L}
    }};
    for (const auto& test : cases) {
        require(harp.configure(1e300, test.load), "configure finite extreme resistances");
        for (double sign : {-1.0, 1.0}) {
            sources[0] = sign * test.source;
            const auto voltage = harp.voltage(sources);
            require(voltage.has_value(), "representable loaded voltage was rejected");
            near(*voltage, sign * test.expected, test.expected * 2e-14L,
                 "extreme divider returned incorrect loaded voltage");
        }
    }
}

using Test = void (*)();
constexpr std::array<std::pair<std::string_view, Test>, 5> tests{{
    {"modal_boundaries", modal_boundaries}, {"rejected_updates", rejected_updates},
    {"engine_controls", engine_controls}, {"engine_combined_pickup", engine_combined_pickup},
    {"network_extreme_divider", network_extreme_divider}
}};
}

int main(int argc, char** argv) {
    const std::string_view selected = argc > 1 ? std::string_view{argv[1]} : "all";
    try {
        bool found = false;
        for (const auto& [name, test] : tests) {
            if (selected == "all" || selected == name) {
                test();
                std::cout << "PASS " << name << '\n';
                found = true;
            }
        }
        require(found, "unknown regression test name");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << selected << ": " << error.what() << '\n';
        return 1;
    }
}
