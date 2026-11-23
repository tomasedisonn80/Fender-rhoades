#include "rhodes/contact_reference.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

// Domain tables come from the public finite-domain contract. They drive public
// operations, never a copy of the implementation's validation predicate.
namespace {
namespace contact = rhodes::contact;
using contact::Configuration;
using contact::Failure;
using contact::Reference;
using contact::Settings;
using contact::State;
std::size_t checks = 0;
std::size_t cases = 0;
std::string context;
constexpr double infinity = std::numeric_limits<double>::infinity();
constexpr double nan = std::numeric_limits<double>::quiet_NaN();

void require(bool condition, std::string_view message) {
    ++checks;
    if (!condition) { throw std::runtime_error(context + ": " + std::string(message)); }
}
void success(contact::Result result) {
    require(result.status == contact::Status::success && result.failure == Failure::none,
            "operation must succeed");
}
void invalid(contact::Result result, Failure reason) {
    require(result.status == contact::Status::invalid_input, "typed invalid-input status");
    require(result.failure == reason, "precise rejection reason");
    require(result.attempted == contact::WorkCounters{}, "invalid input performed numerical work");
}
void same(const Reference& a, const Reference& b) {
    require(a.initialized() == b.initialized(), "initialized flag changed");
    require(a.config() == b.config(), "configuration changed");
    require(a.settings() == b.settings(), "settings changed");
    require(a.state() == b.state(), "mechanical state changed");
    require(a.observation() == b.observation(), "observation changed");
    require(a.ledger() == b.ledger(), "energy/impulse ledger changed");
    require(a.diagnostics() == b.diagnostics(), "persistent diagnostics changed");
    require(a.events() == b.events(), "event history changed");
    require(a.intervals() == b.intervals(), "accepted-interval history changed");
}
Configuration quiet_config() {
    Configuration config;
    config.hammer.enabled = false;
    config.damper.enabled = false;
    return config;
}
Reference populated() {
    Reference reference;
    State initial;
    initial.hammer_x_m = -0.0002;
    initial.hammer_v_m_per_s = 1.0;
    success(reference.initialize({}, initial));
    success(reference.advance(0.0003));
    require(!reference.events().empty() && !reference.intervals().empty(),
            "rollback baseline must have both histories");
    require(reference.diagnostics().work.accepted_steps > 0, "baseline must have work history");
    return reference;
}
template<class Action>
void rejected_then_continue(const Reference& baseline, Failure reason, Action action) {
    ++cases;
    Reference subject = baseline;
    Reference control = baseline;
    invalid(action(subject), reason);
    same(subject, control);
    // This continuation also probes unexposed adaptive steps, compensated sums,
    // and any retained analytical anchor after rejection.
    success(subject.advance(10e-6));
    success(control.advance(10e-6));
    same(subject, control);
}
template<class Owner>
struct Range {
    std::string_view name;
    double low;
    double high;
    void (*set)(Owner&, double);
};
template<class Owner, std::size_t N, class Accept, class Reject>
void ranges(const std::array<Range<Owner>, N>& fields, Owner base,
            Accept accept, Reject reject) {
    for (const auto& field : fields) {
        for (double value : {field.low, field.high}) {
            context = std::string(field.name) + " accepted endpoint";
            auto candidate = base;
            field.set(candidate, value);
            ++cases;
            accept(candidate);
        }
        for (double value : {std::nextafter(field.low, -infinity),
                             std::nextafter(field.high, infinity), nan, infinity, -infinity}) {
            context = std::string(field.name) + " rejected outsider/nonfinite";
            auto candidate = base;
            field.set(candidate, value);
            reject(candidate);
        }
    }
}

void configuration_bounds() {
    context = "configuration baseline";
    const Reference baseline = populated();
    const std::array<Range<Configuration>, 22> fields{{
        {"hammer mass", 1e-6, 1.0, [](auto& c, double x) { c.hammer_mass_kg = x; }},
        {"clearance", -1.0, 1.0, [](auto& c, double x) { c.hammer_clearance_m = x; }},
        {"mode 0 mass", 1e-6, 1.0, [](auto& c, double x) { c.modes[0].mass_kg = x; }},
        {"mode 1 mass", 1e-6, 1.0, [](auto& c, double x) { c.modes[1].mass_kg = x; }},
        {"mode 0 frequency", 0.0, 20000.0, [](auto& c, double x) { c.modes[0].frequency_hz = x; }},
        {"mode 1 frequency", 0.0, 20000.0, [](auto& c, double x) { c.modes[1].frequency_hz = x; }},
        {"mode 0 decay", 0.0, 1e5, [](auto& c, double x) { c.modes[0].decay_per_s = x; }},
        {"mode 1 decay", 0.0, 1e5, [](auto& c, double x) { c.modes[1].decay_per_s = x; }},
        {"hammer projection 0", -100.0, 100.0, [](auto& c, double x) { c.hammer_projection[0] = x; }},
        {"hammer projection 1", -100.0, 100.0, [](auto& c, double x) { c.hammer_projection[1] = x; }},
        {"damper projection 0", -100.0, 100.0, [](auto& c, double x) { c.damper_projection[0] = x; }},
        {"damper projection 1", -100.0, 100.0, [](auto& c, double x) { c.damper_projection[1] = x; }},
        {"hammer k", 0.0, 1e8, [](auto& c, double x) { c.hammer.k_n_per_m = x; }},
        {"hammer c", 0.0, 1e8, [](auto& c, double x) { c.hammer.c_ns_per_m2 = x; }},
        {"disabled damper k", 0.0, 1e8, [](auto& c, double x) { c.damper.k_n_per_m = x; }},
        {"disabled damper c", 0.0, 1e8, [](auto& c, double x) { c.damper.c_ns_per_m2 = x; }},
        {"face offset", -1.0, 1.0, [](auto& c, double x) { c.damper_motion.offset_m = x; }},
        {"face origin", 0.0, 1000.0, [](auto& c, double x) { c.damper_motion.origin_s = x; }},
        {"face velocity", -1000.0, 1000.0, [](auto& c, double x) { c.damper_motion.velocity_m_per_s = x; }},
        {"face amplitude", -1.0, 1.0, [](auto& c, double x) { c.damper_motion.amplitude_m = x; }},
        {"face angular frequency", 0.0, 40000.0 * std::numbers::pi,
            [](auto& c, double x) { c.damper_motion.angular_frequency_rad_per_s = x; }},
        {"face phase", -1e6, 1e6, [](auto& c, double x) { c.damper_motion.phase_rad = x; }}
    }};
    const auto accept = [](const Configuration& config) {
        Reference subject;
        success(subject.initialize(config, {}));
        require(subject.config() == config, "accepted configuration changed");
    };
    const auto reject = [&](const Configuration& config) {
        rejected_then_continue(baseline, Failure::invalid_configuration,
            [&](Reference& subject) { return subject.initialize(config, {}); });
    };
    ranges(fields, quiet_config(), accept, reject);
    for (bool hammer : {false, true}) {
        for (double exponent : {std::nextafter(1.0, 0.0), std::nextafter(1.0, infinity),
                                0.0, nan, infinity, -infinity}) {
            context = "exact p=1 including disabled law";
            auto config = quiet_config();
            (hammer ? config.hammer : config.damper).exponent = exponent;
            reject(config);
        }
    }
    context = "multiple invalid fields must not replace working configuration";
    auto config = quiet_config();
    config.modes[0].mass_kg = 0.0;
    config.damper.c_ns_per_m2 = nan;
    config.damper_motion.phase_rad = infinity;
    reject(config);
}

void settings_bounds() {
    context = "settings baseline";
    const Reference baseline = populated();
    Settings base;
    base.maximum_step_s = 1.0;
    const std::array<Range<Settings>, 10> fields{{
        {"maximum step", 1e-14, 1.0, [](auto& s, double x) { s.maximum_step_s = x; }},
        {"minimum step", 1e-14, 1.0, [](auto& s, double x) { s.minimum_step_s = x; }},
        {"relative tolerance", 1e-18, 1.0, [](auto& s, double x) { s.relative_tolerance = x; }},
        {"position tolerance", 1e-18, 1.0, [](auto& s, double x) { s.position_absolute_tolerance_m = x; }},
        {"velocity tolerance", 1e-18, 1.0, [](auto& s, double x) { s.velocity_absolute_tolerance_m_per_s = x; }},
        {"energy tolerance", 1e-18, 1.0, [](auto& s, double x) { s.energy_absolute_tolerance_j = x; }},
        {"impulse tolerance", 1e-18, 1.0, [](auto& s, double x) { s.impulse_absolute_tolerance_ns = x; }},
        {"reference mass", 1e-6, 1.0, [](auto& s, double x) { s.reference_mass_kg = x; }},
        {"event time tolerance", 1e-14, 1e-4, [](auto& s, double x) { s.event_time_tolerance_s = x; }},
        {"event gap tolerance", 1e-16, 1e-12, [](auto& s, double x) { s.event_gap_tolerance_m = x; }}
    }};
    const auto accept = [](const Settings& settings) {
        Reference subject;
        success(subject.initialize(quiet_config(), {}, settings));
        require(subject.settings() == settings, "accepted settings changed");
    };
    const auto reject = [&](const Settings& settings) {
        rejected_then_continue(baseline, Failure::invalid_settings,
            [&](Reference& subject) { return subject.initialize(quiet_config(), {}, settings); });
    };
    ranges(fields, base, accept, reject);
    struct Cap { std::string_view name; std::uint64_t high; std::uint64_t Settings::*member; };
    constexpr std::array<Cap, 7> caps{{
        {"trials", 100000, &Settings::maximum_trials},
        {"RHS evaluations", 1200000, &Settings::maximum_rhs_evaluations},
        {"consecutive rejections", 64, &Settings::maximum_consecutive_rejections},
        {"root iterations", 64, &Settings::maximum_root_iterations},
        {"subdivision depth", 32, &Settings::maximum_subdivision_depth},
        {"event rows", 100000, &Settings::maximum_event_rows},
        {"interval rows", 100000, &Settings::maximum_interval_rows}
    }};
    for (const auto& cap : caps) {
        context = std::string(cap.name) + " cap endpoints";
        for (const auto value : {std::uint64_t{1}, cap.high}) {
            auto settings = base;
            settings.*(cap.member) = value;
            ++cases;
            accept(settings);
        }
        for (const auto value : {std::uint64_t{0}, cap.high + 1,
                                 std::numeric_limits<std::uint64_t>::max()}) {
            auto settings = base;
            settings.*(cap.member) = value;
            reject(settings);
        }
    }
    context = "dependent step bounds";
    auto settings = base;
    settings.maximum_step_s = 20e-6;
    settings.minimum_step_s = std::nextafter(settings.maximum_step_s, infinity);
    reject(settings);
    context = "multiple invalid settings";
    settings.relative_tolerance = nan;
    settings.maximum_event_rows = 0;
    reject(settings);

    auto free = quiet_config();
    for (auto& mode : free.modes) { mode.frequency_hz = 0.0; mode.decay_per_s = 0.0; }
    context = "valid settings can return atomic minimum-progress failure";
    settings = Settings{};
    settings.maximum_step_s = 1.0;
    settings.minimum_step_s = 1.0;
    Reference stalled;
    success(stalled.initialize(free, {}, settings));
    Reference control = stalled;
    const auto progress = stalled.advance(1e-12);
    require(progress.status == contact::Status::numerical_failure &&
            progress.failure == Failure::minimum_progress, "minimum progress must fail explicitly");
    same(stalled, control);
    success(stalled.advance(1.0));
    success(control.advance(1.0));
    same(stalled, control);
    ++cases;

    context = "event history is a persistent cap across successful actions";
    settings = Settings{};
    settings.maximum_event_rows = 1;
    Reference event_full;
    success(event_full.initialize(free, {}, settings));
    success(event_full.relaunch_hammer(0.1));
    require(event_full.events().size() == 1, "first action did not consume one event row");
    control = event_full;
    const auto event_overflow = event_full.relaunch_hammer(0.2);
    require(event_overflow.status == contact::Status::budget_exhausted &&
            event_overflow.failure == Failure::event_limit, "persistent event cap not enforced");
    same(event_full, control);
    success(event_full.advance(10e-6));
    success(control.advance(10e-6));
    same(event_full, control);
    ++cases;

    context = "interval history is a persistent cap across successful advances";
    settings = Settings{};
    settings.maximum_step_s = 100e-6;
    settings.maximum_interval_rows = 1;
    Reference interval_full;
    success(interval_full.initialize(free, {}, settings));
    success(interval_full.advance(100e-6));
    require(interval_full.intervals().size() == 1, "first advance did not consume one interval row");
    control = interval_full;
    const auto interval_overflow = interval_full.advance(100e-6);
    require(interval_overflow.status == contact::Status::budget_exhausted &&
            interval_overflow.failure == Failure::history_limit, "persistent interval cap not enforced");
    require(interval_overflow.attempted.rhs_evaluations > 0,
            "failed numerical work must remain visible only in the result");
    same(interval_full, control);
    success(interval_full.relaunch_hammer(0.2));
    success(control.relaunch_hammer(0.2));
    same(interval_full, control);
    ++cases;
}

void state_actions() {
    context = "state baseline";
    const Reference baseline = populated();
    const std::array<Range<State>, 7> fields{{
        {"stored time", 0.0, 1000.0, [](auto& s, double x) { s.time_s = x; }},
        {"modal position 0", -1.0, 1.0, [](auto& s, double x) { s.q_m[0] = x; }},
        {"modal position 1", -1.0, 1.0, [](auto& s, double x) { s.q_m[1] = x; }},
        {"modal velocity 0", -1000.0, 1000.0, [](auto& s, double x) { s.v_m_per_s[0] = x; }},
        {"modal velocity 1", -1000.0, 1000.0, [](auto& s, double x) { s.v_m_per_s[1] = x; }},
        {"hammer position", -1.0, 1.0, [](auto& s, double x) { s.hammer_x_m = x; }},
        {"hammer velocity", -1000.0, 1000.0, [](auto& s, double x) { s.hammer_v_m_per_s = x; }}
    }};
    ranges(fields, State{}, [](const State& state) {
        Reference subject;
        success(subject.initialize(quiet_config(), state));
        require(subject.state() == state, "accepted initial state changed");
    }, [&](const State& state) {
        rejected_then_continue(baseline, Failure::invalid_state,
            [&](Reference& subject) { return subject.initialize(quiet_config(), state); });
    });
    context = "uninitialized operations";
    Reference absent;
    const Reference untouched = absent;
    invalid(absent.advance(1e-12), Failure::not_initialized);
    invalid(absent.relaunch_hammer(0.0), Failure::not_initialized);
    invalid(absent.set_damper_motion({}), Failure::not_initialized);
    same(absent, untouched);
    cases += 3;
    for (double dt : {0.0, -0.0, std::nextafter(1e-12, 0.0),
                      std::nextafter(1.0, infinity), nan, infinity, -infinity}) {
        context = "invalid advance increment";
        rejected_then_continue(baseline, Failure::invalid_time,
            [=](Reference& subject) { return subject.advance(dt); });
    }
    auto free = quiet_config();
    for (auto& mode : free.modes) { mode.frequency_hz = 0.0; mode.decay_per_s = 0.0; }
    Settings wide;
    wide.maximum_step_s = 1.0;
    for (double dt : {1e-12, 1.0}) {
        context = "valid advance endpoints on a stationary free system";
        Reference subject;
        success(subject.initialize(free, {}, wide));
        success(subject.advance(dt));
        require(subject.state().time_s == dt, "exact requested time not reached");
        require(subject.state().q_m == std::array<double, 2>{}, "stationary position changed");
        ++cases;
    }
    context = "exact stored-time upper endpoint";
    State time_seed;
    time_seed.time_s = 999.0;
    Reference time_end;
    success(time_end.initialize(free, time_seed, wide));
    success(time_end.advance(1.0));
    require(time_end.state().time_s == 1000.0, "stored-time endpoint rejected");
    const auto at_end = time_end;
    invalid(time_end.advance(1e-12), Failure::invalid_time);
    same(time_end, at_end);
    ++cases;
    for (double velocity : {std::nextafter(-1000.0, -infinity),
                            std::nextafter(1000.0, infinity), nan, infinity, -infinity}) {
        context = "invalid relaunch velocity";
        rejected_then_continue(baseline, Failure::invalid_state,
            [=](Reference& subject) { return subject.relaunch_hammer(velocity); });
    }
    for (double velocity : {-1000.0, 1000.0}) {
        context = "signed relaunch velocity endpoints";
        State seed;
        seed.q_m = {0.125, -0.25};
        seed.v_m_per_s = {0.5, -0.75};
        seed.hammer_x_m = -0.125;
        Reference subject;
        success(subject.initialize(free, seed));
        success(subject.relaunch_hammer(velocity));
        auto expected = seed;
        expected.hammer_v_m_per_s = velocity;
        require(subject.state() == expected, "relaunch reset another mechanical coordinate");
        require(subject.ledger().relaunch_work_j == 4000.0, "endpoint launch kinetic work");
        require(subject.events().size() == 1 &&
                subject.events()[0].kind == contact::EventKind::hammer_relaunch,
                "launch event missing or duplicated");
        ++cases;
    }
}

void trajectory_guards() {
    context = "trajectory baseline";
    const Reference baseline = populated();
    const contact::FaceSample sentinel{0.125, -0.25, 0.5};
    for (double time : {std::nextafter(0.0, -infinity),
                        std::nextafter(1000.0, infinity), nan, infinity, -infinity}) {
        context = "pure face evaluator rejects time without changing output";
        auto output = sentinel;
        invalid(contact::sample_face({}, time, output), Failure::invalid_time);
        require(output == sentinel, "rejected face evaluator changed caller output");
        ++cases;
    }
    std::array<contact::Trajectory, 4> bad{};
    bad[0].kind = static_cast<contact::TrajectoryKind>(-1);
    bad[1].kind = static_cast<contact::TrajectoryKind>(3);
    bad[2].kind = contact::TrajectoryKind::sinusoidal;
    bad[2].offset_m = 0.75;
    bad[2].amplitude_m = 0.5;
    bad[2].phase_rad = std::numbers::pi; // Current sample in range, global envelope outside.
    bad[3].kind = contact::TrajectoryKind::sinusoidal;
    bad[3].amplitude_m = 0.1;
    bad[3].angular_frequency_rad_per_s = 20000.0; // Current speed zero, envelope too high.
    for (const auto& trajectory : bad) {
        context = "invalid trajectory kind or global envelope";
        auto output = sentinel;
        invalid(contact::sample_face(trajectory, 0.0, output), Failure::unsupported_trajectory);
        require(output == sentinel, "rejected trajectory changed caller output");
        rejected_then_continue(baseline, Failure::unsupported_trajectory,
            [&](Reference& subject) { return subject.set_damper_motion(trajectory); });
    }
    context = "one-ULP face discontinuity is not a permitted teleport";
    auto jump = baseline.config().damper_motion;
    jump.offset_m = std::nextafter(baseline.observation().face.position_m, infinity);
    rejected_then_continue(baseline, Failure::discontinuous_face_position,
        [&](Reference& subject) { return subject.set_damper_motion(jump); });
    context = "valid continuous velocity breakpoint preserves mechanics";
    auto trajectory = baseline.config().damper_motion;
    trajectory.kind = contact::TrajectoryKind::linear;
    trajectory.origin_s = baseline.state().time_s;
    trajectory.offset_m = baseline.observation().face.position_m;
    trajectory.velocity_m_per_s = 0.001;
    Reference changed = baseline;
    success(changed.set_damper_motion(trajectory));
    require(changed.state() == baseline.state(), "motion breakpoint reset mechanics");
    require(changed.observation().face.position_m == baseline.observation().face.position_m,
            "continuous breakpoint changed face position");
    require(changed.ledger().current_energy_j == baseline.ledger().current_energy_j,
            "continuous breakpoint changed stored energy");
    ++cases;
    context = "derived linear face leaves domain during an advance";
    auto config = quiet_config();
    config.damper_motion.kind = contact::TrajectoryKind::linear;
    config.damper_motion.velocity_m_per_s = 2.0;
    Reference subject;
    success(subject.initialize(config, {}));
    const Reference original = subject;
    invalid(subject.advance(1.0), Failure::unsupported_trajectory);
    same(subject, original);
    Reference control = original;
    success(subject.advance(0.001));
    success(control.advance(0.001));
    same(subject, control);
    auto output = sentinel;
    invalid(contact::sample_face(config.damper_motion, 1.0, output), Failure::invalid_state);
    require(output == sentinel, "derived-domain rejection changed caller output");
    ++cases;
    context = "pure observation rejection preserves full output";
    contact::Observation observation;
    observation.total_energy_j = 42.0;
    observation.face = sentinel;
    const auto saved = observation;
    config = quiet_config();
    config.modes[1].mass_kg = nan;
    invalid(contact::observe(config, {}, observation), Failure::invalid_configuration);
    require(observation == saved, "configuration rejection changed observation");
    State state;
    state.v_m_per_s[0] = infinity;
    invalid(contact::observe(quiet_config(), state, observation), Failure::invalid_state);
    require(observation == saved, "state rejection changed observation");
    cases += 2;
}
} // namespace

int main(int argc, char** argv) {
    const std::string_view selected = argc == 2 ? argv[1] : "all";
    const std::array<std::pair<std::string_view, void (*)()>, 4> tests{{
        {"configuration_bounds", configuration_bounds}, {"settings_bounds", settings_bounds},
        {"state_actions", state_actions}, {"trajectory_guards", trajectory_guards}
    }};
    bool found = false;
    try {
        for (const auto& [name, run] : tests) {
            if (selected != "all" && selected != name) { continue; }
            found = true;
            checks = 0;
            cases = 0;
            run();
            std::cout << "PASS " << name << " cases=" << cases << " checks=" << checks << '\n';
        }
        if (!found || argc > 2) { throw std::runtime_error("unknown test selector or extra argument"); }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << selected << ": " << error.what() << '\n';
        return 1;
    }
}
