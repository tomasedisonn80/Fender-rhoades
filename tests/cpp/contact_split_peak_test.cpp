#include "rhodes/contact_reference.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numbers>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

// Independent synthetic relative oscillators exercise split landing together
// with the optional diagnostic collector. These finite cases do not certify
// continuous extrema or replace the frozen contact refinement matrix.
namespace {
namespace c = rhodes::contact;
std::size_t checks = 0;
constexpr double hammer_omega = 200.0;
constexpr double end_time = 0.016;

void require(bool condition, std::string_view message) {
    ++checks;
    if (!condition) { throw std::runtime_error(std::string{message}); }
}
void success(c::Result result, std::string_view message) {
    require(static_cast<bool>(result), std::string{message} + " status=" +
        std::to_string(static_cast<int>(result.status)) + " failure=" +
        std::to_string(static_cast<int>(result.failure)));
}
void near(double actual, double expected, double tolerance, std::string_view message) {
    std::ostringstream detail;
    detail << std::setprecision(17) << message << ": actual=" << actual
           << " expected=" << expected << " tolerance=" << tolerance;
    require(std::isfinite(actual) && std::isfinite(expected) &&
        std::abs(actual - expected) <= tolerance, detail.str());
}

c::Configuration config(double damper_lag = 0.0) {
    c::Configuration result;
    result.modes = {c::Mode{1.0, 0.0, 0.0}, c::Mode{0.5, 0.0, 0.0}};
    result.hammer_mass_kg = 1.0;
    result.hammer_projection = {1.0, 0.0};
    result.damper_projection = {0.0, 1.0};
    result.hammer = {true, 20000.0, 0.0, 1.0};
    result.damper = result.hammer;
    if (damper_lag != 0.0) {
        const double omega = std::numbers::pi /
            (std::numbers::pi / hammer_omega + damper_lag);
        result.damper.k_n_per_m = result.modes[1].mass_kg * omega * omega;
    }
    return result;
}
c::Settings settings(bool peaks) {
    c::Settings result;
    result.maximum_step_s = 2e-4;
    result.event_time_tolerance_s = 2.5e-9;
    result.locate_peak_candidates = peaks;
    return result;
}
double damper_omega(const c::Configuration& configuration) {
    return std::sqrt(configuration.damper.k_n_per_m / configuration.modes[1].mass_kg);
}
c::State initial_state() {
    c::State result;
    result.v_m_per_s = {-0.5, 1.0};
    result.hammer_v_m_per_s = 0.5;
    return result;
}
c::State compressed_state(double time) {
    // The hammer relative reduced mass and the independent damper mass are
    // both 0.5 kg. Before separation delta=sin(200*t)/200 and delta'=cos(200*t).
    const double compression = std::sin(hammer_omega * time) / hammer_omega;
    const double speed = std::cos(hammer_omega * time);
    c::State result;
    result.time_s = time;
    result.q_m = {-0.5 * compression, compression};
    result.v_m_per_s = {-0.5 * speed, speed};
    result.hammer_x_m = 0.5 * compression;
    result.hammer_v_m_per_s = 0.5 * speed;
    return result;
}
void same(const c::Reference& actual, const c::Reference& expected) {
    require(actual.initialized() == expected.initialized(), "rollback initialization");
    require(actual.config() == expected.config(), "rollback configuration");
    require(actual.settings() == expected.settings(), "rollback settings");
    require(actual.state() == expected.state(), "rollback mechanical state");
    require(actual.observation() == expected.observation(), "rollback observation");
    require(actual.ledger() == expected.ledger(), "rollback complete ledger");
    require(actual.diagnostics() == expected.diagnostics(), "rollback legacy and optional diagnostics");
    require(actual.events() == expected.events(), "rollback event history");
    require(actual.intervals() == expected.intervals(), "rollback interval history");
}
void unchanged_trajectory(const c::Reference& enabled, const c::Reference& disabled) {
    require(enabled.state() == disabled.state(), "collector changed accepted mechanics");
    require(enabled.observation() == disabled.observation(), "collector changed accepted observation");
    require(enabled.ledger() == disabled.ledger(), "collector changed accepted ledger");
    require(enabled.events() == disabled.events(), "collector changed accepted events");
    require(enabled.intervals() == disabled.intervals(), "collector changed accepted intervals");
    auto actual = enabled.diagnostics();
    const auto& expected = disabled.diagnostics();
    require(actual.work.accepted_steps == expected.work.accepted_steps &&
        actual.work.rejected_steps == expected.work.rejected_steps,
        "collector changed step acceptance");
    require(actual.work.trials > expected.work.trials &&
        actual.work.rhs_evaluations > expected.work.rhs_evaluations,
        "collector replay did not consume the existing work budgets");
    // Every legacy diagnostic, including accepted-interval power residuals,
    // must remain bit-identical. Only collector work and peak records differ.
    actual.work = expected.work;
    actual.located_hammer_compression = expected.located_hammer_compression;
    actual.located_hammer_force = expected.located_hammer_force;
    actual.located_damper_compression = expected.located_damper_compression;
    actual.located_damper_force = expected.located_damper_force;
    require(actual == expected, "collector changed a legacy diagnostic");
}
void peak(const c::PeakCandidate& actual, double expected_value, double expected_time,
          double value_tolerance, const c::Settings& options) {
    require(actual.origin == c::PeakOrigin::stationary_candidate,
        "oscillator maximum must have stationary-candidate provenance");
    require(actual.bracket_begin_s <= actual.time_s && actual.time_s <= actual.bracket_end_s,
        "peak time is inside its reported bracket");
    require(actual.bracket_end_s - actual.bracket_begin_s <= options.event_time_tolerance_s,
        "peak bracket meets the unchanged time resolution");
    near(actual.time_s, expected_time, options.event_time_tolerance_s, "analytic peak time");
    near(actual.value, expected_value, value_tolerance, "analytic peak value");
}
const c::Event& separation(const c::Reference& reference, c::ContactId id) {
    const auto& events = reference.events();
    const auto found = std::find_if(events.begin(), events.end(), [id](const c::Event& event) {
        return event.contact == id && event.kind == c::EventKind::separation;
    });
    require(found != events.end(), "retain each contact's separation");
    return *found;
}
void check_solution(const c::Reference& reference) {
    const auto& events = reference.events();
    const auto& options = reference.settings();
    require(events.size() == 4, "exactly two initial onsets and two natural separations");
    require(events[0].contact == c::ContactId::hammer && events[1].contact == c::ContactId::damper &&
        events[0].kind == c::EventKind::onset && events[1].kind == c::EventKind::onset &&
        events[0].time_s == 0.0 && events[1].time_s == 0.0,
        "initial onsets keep deterministic contact order");
    const double omega = damper_omega(reference.config());
    const double hammer_separation = std::numbers::pi / hammer_omega;
    const double damper_separation = std::numbers::pi / omega;
    for (const auto id : {c::ContactId::hammer, c::ContactId::damper}) {
        const auto& event = separation(reference, id);
        require(event.before == c::Branch::active && event.after == c::Branch::separated &&
            !event.action_time_tie, "natural active-to-separated event semantics");
        require(event.bracket_begin_s <= event.time_s && event.time_s <= event.bracket_end_s &&
            event.bracket_end_s - event.bracket_begin_s <= options.event_time_tolerance_s &&
            event.bracket_gap_uncertainty_m <= options.event_gap_tolerance_m,
            "separation retains both unchanged bracket tolerances");
        near(event.time_s, id == c::ContactId::hammer ? hammer_separation : damper_separation,
            options.event_time_tolerance_s, "independent analytic separation time");
        double distance = std::numeric_limits<double>::max();
        for (const auto& interval : reference.intervals()) {
            distance = std::min(distance, std::abs(interval.end_s - event.time_s));
        }
        require(distance <= options.event_time_tolerance_s,
            "each separation lands at an accepted endpoint within event resolution");
    }
    require(events[2].time_s <= events[3].time_s, "natural events remain time ordered");
    const auto& state = reference.state();
    near(state.q_m[0], 0.5 * (end_time - hammer_separation), 5e-12, "analytic outgoing mode-0 position");
    near(state.hammer_x_m, -0.5 * (end_time - hammer_separation), 5e-12,
        "analytic outgoing hammer position");
    near(state.q_m[1], -(end_time - damper_separation), 5e-12, "analytic outgoing mode-1 position");
    near(state.v_m_per_s[0], 0.5, 1e-10, "analytic outgoing mode-0 velocity");
    near(state.hammer_v_m_per_s, -0.5, 1e-10, "analytic outgoing hammer velocity");
    near(state.v_m_per_s[1], -1.0, 1e-10, "analytic outgoing mode-1 velocity");
    near(reference.ledger().hammer_impulse_ns, 1.0, 1e-10, "independent hammer momentum change");
    near(reference.ledger().damper_impulse_ns, 1.0, 1e-10, "independent damper momentum change");
    near(reference.ledger().current_energy_j, 0.5, 1e-10, "independent conserved energy");
    require(reference.ledger().total_loss_j() == 0.0 && reference.ledger().total_work_j() == 0.0,
        "conservative contacts do not acquire loss or actuator work");
    const auto& diagnostics = reference.diagnostics();
    if (options.locate_peak_candidates) {
        peak(diagnostics.located_hammer_compression, 1.0 / hammer_omega,
            0.5 * hammer_separation, 2e-12, options);
        peak(diagnostics.located_hammer_force, reference.config().hammer.k_n_per_m / hammer_omega,
            0.5 * hammer_separation, 4e-8, options);
        peak(diagnostics.located_damper_compression, 1.0 / omega,
            0.5 * damper_separation, 2e-12, options);
        peak(diagnostics.located_damper_force, reference.config().damper.k_n_per_m / omega,
            0.5 * damper_separation, 4e-8, options);
    } else {
        for (const auto& candidate : {diagnostics.located_hammer_compression,
                diagnostics.located_hammer_force, diagnostics.located_damper_compression,
                diagnostics.located_damper_force}) {
            require(candidate == c::PeakCandidate{}, "disabled collector preserves disabled records");
        }
    }
}
void compare_pair(double lag) {
    c::Reference ordinary, collected;
    success(ordinary.initialize(config(lag), initial_state(), settings(false)), "ordinary initialize");
    success(collected.initialize(config(lag), initial_state(), settings(true)), "collected initialize");
    success(ordinary.advance(end_time), "ordinary advance through both roots");
    success(collected.advance(end_time), "collected advance through both roots");
    check_solution(ordinary);
    check_solution(collected);
    unchanged_trajectory(collected, ordinary);
    const auto& hammer = separation(ordinary, c::ContactId::hammer);
    const auto& damper = separation(ordinary, c::ContactId::damper);
    if (lag == 0.0) {
        require(hammer.time_s == damper.time_s && hammer.bracket_begin_s == damper.bracket_begin_s &&
            hammer.bracket_end_s == damper.bracket_end_s, "simultaneous roots retain identical brackets");
        require(ordinary.events()[2].contact == c::ContactId::hammer &&
            ordinary.events()[3].contact == c::ContactId::damper, "simultaneous roots keep contact order");
    } else {
        require(ordinary.config().hammer.k_n_per_m != ordinary.config().damper.k_n_per_m,
            "close-root fixture must use unequal physical frequencies");
        if (std::abs(lag) >= 1e-9) {
            const auto& first = lag > 0.0 ? hammer : damper;
            const auto& second = lag > 0.0 ? damper : hammer;
            require(first.time_s < second.time_s && first.bracket_end_s < second.bracket_begin_s,
                "resolvable close roots remain distinct and in their analytic order");
            require(ordinary.events()[2].contact == first.contact,
                "the earliest close root wins regardless of contact scan order");
        } else {
            require(hammer.bracket_begin_s <= damper.bracket_end_s &&
                damper.bracket_begin_s <= hammer.bracket_end_s,
                "sub-resolution unequal roots exercise overlapping contact brackets");
        }
    }
    success(ordinary.advance(1e-4), "ordinary continuation after both separations");
    success(collected.advance(1e-4), "collected continuation after both separations");
    unchanged_trajectory(collected, ordinary);
}
void split_simultaneous() { compare_pair(0.0); }
void split_close_roots() {
    for (const double lag : {1e-9, -1e-9, 1e-13, -1e-13}) { compare_pair(lag); }
}

void seed_near_root(c::Reference& reference, const c::Settings& options) {
    const auto state = compressed_state(std::numbers::pi / hammer_omega - 50e-9);
    success(reference.initialize(config(), state, options), "near-root initialize");
    success(reference.advance(40e-9), "populate history before the crossing call");
    require(reference.intervals().size() == 1, "rollback seed has accepted interval history");
    require(reference.observation().hammer.gap_m > 0.0 && reference.observation().damper.gap_m > 0.0,
        "rollback seed stays before both separations");
    if (options.locate_peak_candidates) {
        require(reference.diagnostics().located_hammer_force.value > 0.0 &&
            reference.diagnostics().located_damper_force.value > 0.0,
            "rollback seed has nonzero optional diagnostics");
    }
}
void split_peak_rollback() {
    for (const bool peaks : {false, true}) {
        c::Reference completed;
        seed_near_root(completed, settings(peaks));
        const auto crossing = completed.advance(20e-9);
        success(crossing, "unlimited near-root crossing");
        require(crossing.attempted.accepted_steps == 2 && crossing.attempted.rejected_steps == 0 &&
            crossing.attempted.root_iterations > 0 && completed.events().size() == 2,
            "near-root fixture must split an otherwise accepted crossing and retain both roots");
        for (const auto failure : {c::Failure::trial_limit, c::Failure::evaluation_limit,
                c::Failure::event_limit, c::Failure::history_limit}) {
            auto options = settings(peaks);
            // Stop the final trial/evaluation after the split has made temporary
            // accepted progress. Budget values derive from the public attempted
            // work, without hard-coding platform-dependent root iteration counts.
            if (failure == c::Failure::trial_limit) {
                options.maximum_trials = crossing.attempted.trials - 1;
            } else if (failure == c::Failure::evaluation_limit) {
                options.maximum_rhs_evaluations = crossing.attempted.rhs_evaluations - 1;
            } else if (failure == c::Failure::event_limit) {
                options.maximum_event_rows = 1;
            } else {
                options.maximum_interval_rows = 2;
            }
            c::Reference reference;
            seed_near_root(reference, options);
            auto control = reference;
            const auto rejected = reference.advance(20e-9);
            require(rejected.status == c::Status::budget_exhausted && rejected.failure == failure,
                "combined split/collector returns its exact work-budget failure");
            require(rejected.attempted.root_iterations > 0 && rejected.attempted.rhs_evaluations > 0,
                "failed crossing reports its attempted root and RHS work");
            require(rejected.attempted.trials <= options.maximum_trials &&
                rejected.attempted.rhs_evaluations <= options.maximum_rhs_evaluations,
                "split and collector do not exceed call-wide trial or RHS caps");
            if (failure != c::Failure::event_limit) {
                require(rejected.attempted.accepted_steps == 1,
                    "budget failure must follow the temporary accepted split interval");
            }
            if (failure == c::Failure::trial_limit) {
                require(rejected.attempted.trials == options.maximum_trials,
                    "trial failure consumes the exact existing cap");
            } else if (failure == c::Failure::evaluation_limit) {
                require(rejected.attempted.rhs_evaluations == options.maximum_rhs_evaluations,
                    "RHS failure consumes the exact existing cap");
            }
            same(reference, control);
            success(reference.advance(1e-9), "continue after split/collector rollback");
            success(control.advance(1e-9), "unfailed control continuation");
            same(reference, control);
        }
    }
}
} // namespace

int main(int argc, char** argv) {
    const std::string_view selected = argc == 2 ? argv[1] : "all";
    const std::array<std::pair<std::string_view, void (*)()>, 3> tests{{
        {"split_simultaneous", split_simultaneous}, {"split_close_roots", split_close_roots},
        {"split_peak_rollback", split_peak_rollback}
    }};
    bool found = false;
    try {
        for (const auto& [name, run] : tests) {
            if (selected != "all" && selected != name) { continue; }
            found = true;
            checks = 0;
            run();
            std::cout << "PASS " << name << " checks=" << checks << '\n';
        }
        require(found && argc <= 2, "unknown test selector or extra argument");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << selected << ": " << error.what() << '\n';
        return 1;
    }
}
