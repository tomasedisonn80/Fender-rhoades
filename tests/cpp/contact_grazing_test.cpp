#include "rhodes/contact_reference.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

// Independent harmonic limits of the general analytic contact certificate.
// All checks remain active under NDEBUG; constants are synthetic SI fixtures.
namespace {
namespace c = rhodes::contact;
void require(bool condition, std::string_view message) {
    if (!condition) { throw std::runtime_error(std::string{message}); }
}
void require(c::Result result, std::string_view message) {
    if (!result) {
        throw std::runtime_error(std::string{message} + " failure=" +
                                 std::to_string(static_cast<int>(result.failure)));
    }
}
void near(double actual, double expected, double absolute, double relative,
          std::string_view message) {
    require(std::isfinite(actual) && std::isfinite(expected) &&
            std::abs(actual - expected) <= absolute + relative * std::abs(expected), message);
}

struct Fixture {
    c::Configuration config{};
    c::State initial{};
    double frequency;
    Fixture(double hz, double amplitude, double projection, double origin,
            bool unprojected_motion = false) : frequency(hz) {
        config.modes = {{{0.002, hz, 0.0}, {0.001, 701.0, 0.0}}};
        config.hammer_projection = {projection, unprojected_motion ? 0.0 : 0.35};
        initial.time_s = origin;
        initial.q_m[0] = -std::copysign(amplitude, projection);
        initial.hammer_x_m = -std::abs(projection) * amplitude;
        if (unprojected_motion) {
            initial.q_m[1] = amplitude / 4.0;
            initial.v_m_per_s[1] = 0.03;
        }
    }
    c::State harmonic(double time) const {
        c::State result = initial;
        result.time_s = time;
        for (std::size_t j = 0; j < c::mode_count; ++j) {
            const double omega = 2.0 * std::numbers::pi * config.modes[j].frequency_hz;
            const double phase = omega * (time - initial.time_s);
            result.q_m[j] = initial.q_m[j] * std::cos(phase) +
                            initial.v_m_per_s[j] * std::sin(phase) / omega;
            result.v_m_per_s[j] = -omega * initial.q_m[j] * std::sin(phase) +
                                  initial.v_m_per_s[j] * std::cos(phase);
        }
        return result;
    }
    double energy(const c::State& state) const {
        double result = 0.5 * config.hammer_mass_kg *
                        state.hammer_v_m_per_s * state.hammer_v_m_per_s;
        for (std::size_t j = 0; j < c::mode_count; ++j) {
            const double omega = 2.0 * std::numbers::pi * config.modes[j].frequency_hz;
            result += 0.5 * config.modes[j].mass_kg *
                (state.v_m_per_s[j] * state.v_m_per_s[j] +
                 omega * omega * state.q_m[j] * state.q_m[j]);
        }
        return result;
    }
};

void check_harmonic(const c::Reference& solver, const Fixture& fixture) {
    const auto expected = fixture.harmonic(solver.state().time_s);
    for (std::size_t j = 0; j < c::mode_count; ++j) {
        near(solver.state().q_m[j], expected.q_m[j], 1e-11, 1e-8, "harmonic position");
        near(solver.state().v_m_per_s[j], expected.v_m_per_s[j], 1e-8, 1e-8, "harmonic velocity");
    }
    require(solver.state().hammer_x_m == fixture.initial.hammer_x_m &&
            solver.state().hammer_v_m_per_s == 0.0, "stationary hammer moved");
    require(solver.ledger().hammer_impulse_ns == 0.0 &&
            solver.ledger().damper_impulse_ns == 0.0 &&
            solver.ledger().total_loss_j() == 0.0 &&
            solver.ledger().total_work_j() == 0.0, "force-free impulse/loss/work");
    near(solver.ledger().current_energy_j, fixture.energy(fixture.initial), 1e-10, 1e-8,
         "independent harmonic energy");
    require(solver.diagnostics().work.analytic_free_evaluations > 0,
            "eligible geometry did not exercise analytic path");
}

void check_maxima(const c::Reference& solver, const Fixture& fixture, std::size_t expected_count) {
    require(solver.events().size() == expected_count, "unexpected grazing count");
    for (std::size_t i = 0; i < expected_count; ++i) {
        const auto& event = solver.events()[i];
        const double expected = fixture.initial.time_s + static_cast<double>(i) / fixture.frequency;
        require(event.contact == c::ContactId::hammer && event.kind == c::EventKind::grazing,
                "non-grazing event in certified tangent");
        near(event.time_s, expected, 1e-7, 1e-4, "independent grazing time");
        require(event.bracket_begin_s <= event.time_s && event.time_s <= event.bracket_end_s &&
                event.bracket_end_s - event.bracket_begin_s <= solver.settings().event_time_tolerance_s,
                "invalid grazing bracket");
        // Rounding of an independently formed period is far below the root budget.
        require(expected >= event.bracket_begin_s - 16.0 * std::numeric_limits<double>::epsilon() &&
                expected <= event.bracket_end_s + 16.0 * std::numeric_limits<double>::epsilon(),
                "grazing bracket excludes independent harmonic maximum");
        require(event.bracket_gap_uncertainty_m <= solver.settings().event_gap_tolerance_m,
                "grazing gap uncertainty");
    }
}

void generalized_harmonics() {
    for (double frequency : {87.5, 1123.0}) {
        for (double amplitude : {0.0002, 0.000035}) {
            for (double projection : {-0.5, 2.0}) {
                Fixture fixture{frequency, amplitude, projection, 0.375,
                                projection > 0.0};
                c::Reference solver;
                require(solver.initialize(fixture.config, fixture.initial), "harmonic initialize");
                require(solver.advance(2.25 / frequency), "multiple harmonic maxima");
                check_harmonic(solver, fixture);
                check_maxima(solver, fixture, 3);
            }
        }
    }
}

void envelope_sign() {
    Fixture miss{1123.0, 0.0002, -0.5, 0.125};
    miss.initial.hammer_x_m -= 1e-8;
    c::Reference solver;
    require(solver.initialize(miss.config, miss.initial), "miss initialize");
    require(solver.advance(1.25 / miss.frequency), "strictly separated envelope");
    check_harmonic(solver, miss);
    require(solver.events().empty(), "strict miss labeled tangent");

    for (double penetration : {1e-8, 0.0}) {
        Fixture contact{87.5, 0.0002, -0.5, 0.125};
        contact.initial.hammer_x_m = penetration == 0.0 ?
            std::nextafter(contact.initial.hammer_x_m, 0.0) :
            contact.initial.hammer_x_m + penetration;
        require(solver.initialize(contact.config, contact.initial), "positive envelope initialize");
        const auto initial = solver.state();
        const auto result = solver.advance(1e-8);
        require(result.attempted.analytic_free_evaluations == 0 &&
                solver.diagnostics().work.analytic_free_evaluations == 0,
                "positive envelope incorrectly certified as zero-force");
        if (result) {
            require(solver.ledger().hammer_impulse_ns > 0.0, "positive force lost its impulse");
        } else {
            require(result.status == c::Status::numerical_failure &&
                    (result.failure == c::Failure::unresolved_grazing ||
                     result.failure == c::Failure::unresolved_event),
                    "unexpected positive-envelope failure");
            require(solver.state() == initial, "uncertain envelope failure mutated state");
        }
    }
}

void partitions() {
    Fixture fixture{1123.0, 0.000035, -0.5, 0.125, true};
    c::Reference whole, divided;
    require(whole.initialize(fixture.config, fixture.initial), "whole initialize");
    require(divided.initialize(fixture.config, fixture.initial), "divided initialize");
    const double finish = fixture.initial.time_s + 2.25 / fixture.frequency;
    require(whole.advance(finish - whole.state().time_s), "whole interval");
    for (double periods : {0.13, 0.44, 0.51, 1.19, 1.93, 2.25}) {
        const double target = fixture.initial.time_s + periods / fixture.frequency;
        require(divided.advance(target - divided.state().time_s), "partitioned interval");
    }
    check_harmonic(whole, fixture);
    check_harmonic(divided, fixture);
    check_maxima(whole, fixture, 3);
    check_maxima(divided, fixture, 3);
    for (std::size_t j = 0; j < c::mode_count; ++j) {
        near(divided.state().q_m[j], whole.state().q_m[j], 1e-11, 1e-8, "partition position");
        near(divided.state().v_m_per_s[j], whole.state().v_m_per_s[j], 1e-8, 1e-8, "partition velocity");
    }
    for (std::size_t i = 0; i < whole.events().size(); ++i) {
        near(divided.events()[i].time_s, whole.events()[i].time_s, 1e-8, 0.0,
             "partition event time");
    }
}

void relaunch_invalidation() {
    Fixture fixture{87.5, 0.0002, -0.5, 0.125};
    c::Reference solver;
    require(solver.initialize(fixture.config, fixture.initial), "action initialize");
    require(solver.advance(0.25 / fixture.frequency), "analytic before action");
    const auto before = solver.state();
    require(solver.relaunch_hammer(1.0), "moving hammer action");
    require(solver.state().q_m == before.q_m && solver.state().v_m_per_s == before.v_m_per_s &&
            solver.state().hammer_x_m == before.hammer_x_m, "relaunch changed persistent coordinates");
    near(solver.ledger().relaunch_work_j, 0.5 * fixture.config.hammer_mass_kg,
         1e-12, 1e-10, "exact action kinetic work");
    const auto analytic_count = solver.diagnostics().work.analytic_free_evaluations;
    const auto result = solver.advance(5e-6);
    require(result, "numerical continuation after relaunch");
    require(result.attempted.analytic_free_evaluations == 0 &&
            solver.diagnostics().work.analytic_free_evaluations == analytic_count,
            "moving hammer retained stationary analytic certificate");
    near(solver.state().hammer_x_m, before.hammer_x_m + 5e-6, 1e-11, 1e-8,
         "launched free hammer displacement");
    require(solver.state().hammer_v_m_per_s == 1.0, "launched free hammer velocity");
    const auto expected = fixture.harmonic(solver.state().time_s);
    near(solver.state().q_m[0], expected.q_m[0], 1e-8, 1e-4, "post-action modal position");
    near(solver.state().v_m_per_s[0], expected.v_m_per_s[0], 1e-5, 1e-4,
         "post-action modal velocity");
}

using Test = void (*)();
constexpr std::array<std::pair<std::string_view, Test>, 4> tests{{
    {"generalized_harmonics", generalized_harmonics}, {"envelope_sign", envelope_sign},
    {"partitions", partitions}, {"relaunch_invalidation", relaunch_invalidation}
}};
} // namespace

int main(int argc, char** argv) {
    const std::string_view selected = argc > 1 ? argv[1] : "all";
    bool found = false, passed = true;
    for (const auto& [name, test] : tests) {
        if (selected != "all" && selected != name) { continue; }
        found = true;
        try { test(); std::cout << "PASS " << name << '\n'; }
        catch (const std::exception& error) {
            passed = false;
            std::cerr << "FAIL " << name << ": " << error.what() << '\n';
        }
    }
    return found && passed ? 0 : 1;
}
