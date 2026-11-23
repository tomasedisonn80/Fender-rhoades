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

// Synthetic offline diagnostics, not the frozen continuous-extrema acceptance
// gate. Expected peaks below use analytic mechanics or a scalar invariant and
// quadrature; they never call the candidate RHS, replay, or root finder.
namespace {
namespace c = rhodes::contact;
std::size_t checks = 0;

void require(bool condition, std::string_view message) {
    ++checks;
    if (!condition) { throw std::runtime_error(std::string{message}); }
}
void success(c::Result result, std::string_view message) {
    require(static_cast<bool>(result), std::string{message} + " status=" +
        std::to_string(static_cast<int>(result.status)) + " failure=" +
        std::to_string(static_cast<int>(result.failure)));
}
void near(double actual, double expected, double absolute, std::string_view message) {
    std::ostringstream detail;
    detail << std::setprecision(17) << message << ": actual=" << actual
           << " expected=" << expected << " tolerance=" << absolute;
    require(std::isfinite(actual) && std::isfinite(expected) &&
        std::abs(actual - expected) <= absolute, detail.str());
}

c::Configuration free_config(double damping = 0.0) {
    c::Configuration config;
    config.modes = {{{0.002, 0.0, 0.0}, {0.001, 0.0, 0.0}}};
    config.hammer_projection = {1.0, 0.0};
    config.hammer.c_ns_per_m2 = damping;
    return config;
}
c::State impact_start() {
    c::State state;
    state.hammer_v_m_per_s = 1.0;
    return state;
}
c::Settings peak_settings(double step = 37e-6) {
    c::Settings settings;
    settings.locate_peak_candidates = true;
    settings.maximum_step_s = step;
    settings.relative_tolerance = 1e-10;
    settings.position_absolute_tolerance_m = 1e-14;
    settings.velocity_absolute_tolerance_m_per_s = 1e-11;
    settings.energy_absolute_tolerance_j = 1e-15;
    settings.impulse_absolute_tolerance_ns = 1e-15;
    settings.event_time_tolerance_s = 1e-10;
    return settings;
}
void same(const c::Reference& actual, const c::Reference& expected) {
    require(actual.initialized() == expected.initialized(), "rollback initialization");
    require(actual.config() == expected.config(), "rollback configuration");
    require(actual.settings() == expected.settings(), "rollback settings");
    require(actual.state() == expected.state(), "rollback mechanical state");
    require(actual.observation() == expected.observation(), "rollback observation");
    require(actual.ledger() == expected.ledger(), "rollback complete energy ledger");
    require(actual.diagnostics() == expected.diagnostics(), "rollback old and new diagnostics");
    require(actual.events() == expected.events(), "rollback complete event history");
    require(actual.intervals() == expected.intervals(), "rollback complete interval history");
}
void unchanged_trajectory(const c::Reference& enabled, const c::Reference& disabled) {
    require(enabled.state() == disabled.state(), "collector changed accepted mechanical state");
    require(enabled.observation() == disabled.observation(), "collector changed accepted observation");
    require(enabled.ledger() == disabled.ledger(), "collector changed energy ledger");
    require(enabled.events() == disabled.events(), "collector changed contact events");
    require(enabled.intervals() == disabled.intervals(), "collector changed accepted intervals");
    const auto& a = enabled.diagnostics();
    const auto& b = disabled.diagnostics();
    require(a.work.accepted_steps == b.work.accepted_steps &&
        a.work.rejected_steps == b.work.rejected_steps, "collector changed step acceptance");
    require(a.sampled_maximum_hammer_compression_m == b.sampled_maximum_hammer_compression_m &&
        a.sampled_maximum_hammer_compression_time_s == b.sampled_maximum_hammer_compression_time_s &&
        a.sampled_maximum_hammer_force_n == b.sampled_maximum_hammer_force_n &&
        a.sampled_maximum_hammer_force_time_s == b.sampled_maximum_hammer_force_time_s &&
        a.sampled_maximum_damper_compression_m == b.sampled_maximum_damper_compression_m &&
        a.sampled_maximum_damper_compression_time_s == b.sampled_maximum_damper_compression_time_s &&
        a.sampled_maximum_damper_force_n == b.sampled_maximum_damper_force_n &&
        a.sampled_maximum_damper_force_time_s == b.sampled_maximum_damper_force_time_s,
        "collector changed legacy sampled maxima");
    require(a.work.rhs_evaluations > b.work.rhs_evaluations,
        "optional replay work was not charged to the existing budget");
}
void bracket(const c::PeakCandidate& peak, double expected_time, double tolerance) {
    require(std::isfinite(peak.value) && peak.value >= 0.0, "finite nonnegative peak value");
    require(peak.bracket_begin_s <= peak.time_s && peak.time_s <= peak.bracket_end_s,
        "reported peak time must lie within its own bracket");
    require(peak.bracket_end_s - peak.bracket_begin_s <= 1.01 * tolerance,
        "stationary search must meet its requested time resolution");
    near(peak.time_s, expected_time, 5.0 * tolerance,
        "independently derived peak time (includes numerical trajectory error)");
    require(peak.origin == c::PeakOrigin::stationary_candidate,
        "off-grid maximum needs a located stationary origin");
}

void peak_defaults() {
    require(!c::Settings{}.locate_peak_candidates, "peak search must be default-off");
    c::Reference implicit, explicit_false;
    c::Settings settings;
    settings.locate_peak_candidates = false;
    success(implicit.initialize(free_config(), impact_start()), "default initialize");
    success(explicit_false.initialize(free_config(), impact_start(), settings), "explicit off initialize");
    success(implicit.advance(100e-6), "default advance");
    success(explicit_false.advance(100e-6), "explicit off advance");
    same(implicit, explicit_false);
    const auto& disabled = implicit.diagnostics();
    for (const auto& peak : {disabled.located_hammer_compression, disabled.located_hammer_force,
            disabled.located_damper_compression, disabled.located_damper_force}) {
        require(peak == c::PeakCandidate{}, "disabled collector must retain explicit disabled records");
    }

    // Separated stationary contacts have derivative plateaus and legitimate
    // all-zero results, with an initial sample origin rather than missing data.
    auto config = free_config();
    config.damper.enabled = true;
    config.damper_motion.offset_m = 0.002;
    c::State state;
    state.time_s = 0.125;
    state.hammer_x_m = -0.002;
    auto enabled = peak_settings(100e-6);
    c::Reference zero;
    success(zero.initialize(config, state, enabled), "zero initialize");
    success(zero.advance(100e-6), "zero plateau advance");
    const auto& diagnostics = zero.diagnostics();
    for (const auto& peak : {diagnostics.located_hammer_compression, diagnostics.located_hammer_force,
            diagnostics.located_damper_compression, diagnostics.located_damper_force}) {
        require(peak.value == 0.0 && peak.origin == c::PeakOrigin::sample,
            "enabled all-zero result must have an explicit sample origin");
        require(peak.time_s == state.time_s && peak.bracket_begin_s == state.time_s &&
            peak.bracket_end_s == state.time_s, "zero result retains the initial sample time");
    }
    require(diagnostics.work.root_iterations == 0, "zero plateaus must not enumerate roots");

    c::Reference rising;
    success(rising.initialize(free_config(), impact_start(), enabled), "rising initialize");
    success(rising.advance(1e-6), "rising endpoint advance");
    const auto& end_peak = rising.diagnostics().located_hammer_compression;
    require(end_peak.time_s == rising.state().time_s &&
        end_peak.value == rising.observation().hammer.compression_m,
        "accepted end state must be included in candidates");
    require(rising.diagnostics().located_damper_force.origin == c::PeakOrigin::disabled,
        "disabled contact remains disabled while collector is enabled");
}

void peak_conservative() {
    const auto config = free_config();
    const double reduced_mass = config.hammer_mass_kg * config.modes[0].mass_kg /
        (config.hammer_mass_kg + config.modes[0].mass_kg);
    const double omega = std::sqrt(config.hammer.k_n_per_m / reduced_mass);
    // Relative motion: delta = v0*sin(omega*t)/omega, v0=1 m/s.
    const double expected_time = std::numbers::pi / (2.0 * omega);
    for (const double step : {37e-6, 11e-6}) {
        const auto settings = peak_settings(step);
        auto off = settings;
        off.locate_peak_candidates = false;
        c::Reference reference, ordinary;
        success(reference.initialize(config, impact_start(), settings), "elastic initialize");
        success(ordinary.initialize(config, impact_start(), off), "ordinary elastic initialize");
        success(reference.advance(0.0008), "elastic maximum advance");
        success(ordinary.advance(0.0008), "ordinary elastic advance");
        const auto& d = reference.diagnostics();
        bracket(d.located_hammer_compression, expected_time, settings.event_time_tolerance_s);
        bracket(d.located_hammer_force, expected_time, settings.event_time_tolerance_s);
        near(d.located_hammer_compression.value, 1.0 / omega, 2e-12, "elastic compression maximum");
        near(d.located_hammer_force.value, config.hammer.k_n_per_m / omega, 4e-8,
            "elastic force maximum");
        unchanged_trajectory(reference, ordinary);
    }
}

struct DampedExpected { double force_time_s; double force_n; double compression_time_s; double compression_m; };
DampedExpected damped_expected() {
    constexpr long double mu = 0.0016L, k = 20000.0L, damping = 40000.0L, v0 = 1.0L;
    constexpr long double u0 = 1.0L + damping * v0 / k;
    // From mu*w*dw/ddelta = -delta*(k+c*w):
    // delta^2 = 2*mu*k/c^2 * (u0-u-log(u0/u)), u=1+c*w/k.
    // dF/dt=0 on loading gives w=c*delta^2/mu, hence this scalar equation.
    long double low = 1.0L, high = u0;
    for (int iteration = 0; iteration < 100; ++iteration) {
        const long double u = (low + high) / 2.0L;
        const long double residual = 3.0L * (u - 1.0L) - 2.0L * (u0 - 1.0L) +
            2.0L * std::log(u0 / u);
        if (residual > 0.0L) { high = u; } else { low = u; }
    }
    const long double u = (low + high) / 2.0L;
    const long double speed = k * (u - 1.0L) / damping;
    const long double compression = std::sqrt(mu * speed / damping);
    // Integrate dt/dw=-mu/(delta*(k+c*w)). x=sqrt(v0-w) removes
    // its integrable initial singularity. This is quadrature, not time stepping.
    const auto time_to = [](long double target_speed, int panels) {
        const long double end = std::sqrt(v0 - target_speed);
        const long double h = end / static_cast<long double>(panels);
        const auto integrand = [](long double x) {
            if (x == 0.0L) { return std::sqrt(2.0L * mu / (v0 * (k + damping * v0))); }
            const long double x2 = x * x;
            const long double delta2 = 2.0L * mu * (x2 / damping + k / (damping * damping) *
                std::log1p(-damping * x2 / (k + damping * v0)));
            return 2.0L * mu * x / (std::sqrt(delta2) * (k + damping * (v0 - x2)));
        };
        long double sum = integrand(0.0L) + integrand(end);
        for (int i = 1; i < panels; ++i) {
            sum += (i % 2 == 0 ? 2.0L : 4.0L) * integrand(static_cast<long double>(i) * h);
        }
        return sum * h / 3.0L;
    };
    const long double force_time = time_to(speed, 2048);
    const long double compression_time = time_to(0.0L, 2048);
    near(static_cast<double>(force_time), static_cast<double>(time_to(speed, 1024)),
        2e-14, "independent force-time quadrature refinement");
    near(static_cast<double>(compression_time), static_cast<double>(time_to(0.0L, 1024)),
        2e-14, "independent compression-time quadrature refinement");
    const long double maximum_compression = std::sqrt(2.0L * mu * k / (damping * damping) *
        (u0 - 1.0L - std::log(u0)));
    return {static_cast<double>(force_time), static_cast<double>(compression * k * u),
        static_cast<double>(compression_time), static_cast<double>(maximum_compression)};
}

void peak_damped() {
    const auto expected = damped_expected();
    require(expected.force_time_s < expected.compression_time_s,
        "damped force and compression maxima must be distinct");
    for (const double step : {37e-6, 11e-6}) {
        const auto settings = peak_settings(step);
        auto off = settings;
        off.locate_peak_candidates = false;
        c::Reference reference, ordinary;
        success(reference.initialize(free_config(40000.0), impact_start(), settings), "damped initialize");
        success(ordinary.initialize(free_config(40000.0), impact_start(), off), "ordinary damped initialize");
        success(reference.advance(0.0006), "damped advance");
        success(ordinary.advance(0.0006), "ordinary damped advance");
        const auto& d = reference.diagnostics();
        bracket(d.located_hammer_force, expected.force_time_s, settings.event_time_tolerance_s);
        bracket(d.located_hammer_compression, expected.compression_time_s, settings.event_time_tolerance_s);
        near(d.located_hammer_force.value, expected.force_n, 4e-8, "independent damped force maximum");
        near(d.located_hammer_compression.value, expected.compression_m, 2e-12,
            "independent damped compression maximum");
        double nearest_sample = std::numeric_limits<double>::max();
        for (const auto& interval : reference.intervals()) {
            for (int quarter = 0; quarter <= 4; ++quarter) {
                nearest_sample = std::min(nearest_sample, std::abs(expected.force_time_s -
                    (interval.begin_s + static_cast<double>(quarter) * interval.step_s / 4.0)));
            }
        }
        require(nearest_sample > 20.0 * settings.event_time_tolerance_s,
            "damped force fixture must remain off the interval sampling grid");
        unchanged_trajectory(reference, ordinary);
    }
}

void peak_damper_face() {
    auto config = free_config();
    config.hammer.enabled = false;
    config.damper.enabled = true;
    config.damper_projection = {0.0, 0.0};
    constexpr double base = 0.0003, amplitude = 0.0001, omega = 2000.0, phase = 0.137;
    config.damper_motion.kind = c::TrajectoryKind::sinusoidal;
    config.damper_motion.offset_m = -base;
    config.damper_motion.amplitude_m = amplitude;
    config.damper_motion.angular_frequency_rad_per_s = omega;
    config.damper_motion.phase_rad = phase;
    // A stationary projected point makes delta=-face exactly: relative
    // acceleration is +A*omega^2*cos(theta), the negative face acceleration.
    const auto force_derivative = [&](double theta, double acceleration_sign) {
        const double delta = base - amplitude * std::cos(theta);
        const double speed = amplitude * omega * std::sin(theta);
        const double acceleration = acceleration_sign * amplitude * omega * omega * std::cos(theta);
        return speed * (config.damper.k_n_per_m + config.damper.c_ns_per_m2 * speed) +
            config.damper.c_ns_per_m2 * delta * acceleration;
    };
    double low = std::numbers::pi / 2.0, high = std::numbers::pi;
    for (int iteration = 0; iteration < 80; ++iteration) {
        const double theta = (low + high) / 2.0;
        if (force_derivative(theta, 1.0) > 0.0) { low = theta; } else { high = theta; }
    }
    const double theta = (low + high) / 2.0;
    const double force_time = (theta - phase) / omega;
    const double expected_force = (base - amplitude * std::cos(theta)) *
        (config.damper.k_n_per_m + config.damper.c_ns_per_m2 * amplitude * omega * std::sin(theta));
    require(force_derivative(theta, -1.0) > 1000.0,
        "face fixture must discriminate the wrong relative-acceleration sign");
    const auto settings = peak_settings(43e-6);
    c::Reference reference;
    success(reference.initialize(config, {}, settings), "prescribed face initialize");
    success(reference.advance(0.0018), "prescribed face advance");
    const auto& d = reference.diagnostics();
    bracket(d.located_damper_force, force_time, settings.event_time_tolerance_s);
    bracket(d.located_damper_compression, (std::numbers::pi - phase) / omega,
        settings.event_time_tolerance_s);
    near(d.located_damper_force.value, expected_force, 1e-10, "analytic prescribed-face force maximum");
    near(d.located_damper_compression.value, base + amplitude, 1e-14,
        "analytic prescribed-face compression maximum");
    require(reference.state().q_m == std::array<double, 2>{} &&
        reference.state().v_m_per_s == std::array<double, 2>{},
        "zero projection must preserve the stationary mechanical point");
    require(d.located_hammer_force.origin == c::PeakOrigin::disabled,
        "disabled hammer must not acquire damper candidates");
}

void action_peak(const c::PeakCandidate& peak, double force, c::PeakOrigin origin, double time) {
    near(peak.value, force, 1e-14, "independent action-side force");
    require(peak.origin == origin, "action-side peak origin");
    require(peak.time_s == time && peak.bracket_begin_s == time && peak.bracket_end_s == time,
        "external action sides have exact degenerate time brackets");
}
void peak_actions() {
    const auto settings = peak_settings();
    for (const bool increasing : {true, false}) {
        auto config = free_config(40000.0);
        c::State state;
        state.time_s = 0.125;
        state.hammer_x_m = 0.0001;
        state.hammer_v_m_per_s = increasing ? 0.1 : 0.8;
        c::Reference reference;
        success(reference.initialize(config, state, settings), "action hammer initialize");
        const double velocity = increasing ? 0.8 : -0.6;
        success(reference.relaunch_hammer(velocity), "velocity-only hammer action");
        state.hammer_v_m_per_s = velocity;
        require(reference.state() == state, "hammer action must preserve every other mechanical coordinate");
        action_peak(reference.diagnostics().located_hammer_force,
            0.0001 * (20000.0 + 40000.0 * 0.8),
            increasing ? c::PeakOrigin::action_after : c::PeakOrigin::action_before, state.time_s);
        if (!increasing) {
            require(reference.observation().hammer.force_n == 0.0,
                "clipped after-action force must not erase the before-action maximum");
        }

        config.hammer.enabled = false;
        config.damper.enabled = true;
        config.damper_projection = {0.0, 0.0};
        config.damper_motion.kind = c::TrajectoryKind::linear;
        config.damper_motion.origin_s = state.time_s;
        config.damper_motion.offset_m = -0.0001;
        config.damper_motion.velocity_m_per_s = increasing ? 0.0 : -0.4;
        success(reference.initialize(config, state, settings), "action damper initialize");
        const auto ledger = reference.ledger();
        auto motion = config.damper_motion;
        motion.velocity_m_per_s = increasing ? -0.4 : 0.5;
        success(reference.set_damper_motion(motion), "velocity-only damper action");
        require(reference.state() == state, "damper velocity action must preserve all mechanics");
        require(reference.ledger() == ledger, "continuous face-velocity action adds no instantaneous energy");
        action_peak(reference.diagnostics().located_damper_force,
            0.0001 * (12000.0 + 30000.0 * 0.4),
            increasing ? c::PeakOrigin::action_after : c::PeakOrigin::action_before, state.time_s);
        if (!increasing) {
            require(reference.observation().damper.force_n == 0.0,
                "clipped damper side must retain its larger before-action force");
        }
    }
}

void peak_rollback() {
    // A single ordinary step fits each work budget; the extra collector replay
    // exhausts it. Histories already contain a successful external action.
    for (const auto failure : {c::Failure::trial_limit, c::Failure::evaluation_limit,
            c::Failure::minimum_progress}) {
        c::Settings settings;
        settings.locate_peak_candidates = true;
        double duration = 1e-6;
        if (failure == c::Failure::trial_limit) { settings.maximum_trials = 1; }
        if (failure == c::Failure::evaluation_limit) { settings.maximum_rhs_evaluations = 35; }
        if (failure == c::Failure::minimum_progress) {
            settings.minimum_step_s = 1e-6;
            settings.maximum_step_s = 2e-6;
            duration = 2e-6;
        }
        c::State initial;
        initial.hammer_x_m = 0.0001;
        initial.hammer_v_m_per_s = 0.2;
        c::Reference reference;
        success(reference.initialize(free_config(), initial, settings), "budget initialize");
        success(reference.relaunch_hammer(0.3), "populate action before budget failure");
        auto control = reference;
        const auto rejected = reference.advance(duration);
        require(rejected.failure == failure, "optional collector returned the wrong bounded failure");
        require(rejected.status == (failure == c::Failure::minimum_progress ?
            c::Status::numerical_failure : c::Status::budget_exhausted), "typed collector failure status");
        require(rejected.attempted.rhs_evaluations > 0, "failed attempted work must remain observable");
        same(reference, control);
        auto off = settings;
        off.locate_peak_candidates = false;
        c::Reference ordinary;
        success(ordinary.initialize(free_config(), initial, off), "ordinary budget initialize");
        success(ordinary.relaunch_hammer(0.3), "ordinary action");
        success(ordinary.advance(duration), "same budget must fit the ordinary step");
        success(reference.relaunch_hammer(0.4), "post-failure continuation");
        success(control.relaunch_hammer(0.4), "control continuation");
        same(reference, control);
    }

    // The damped force maximum precedes both the compression turning point and
    // separation. Only the optional force search needs a root in this interval.
    auto settings = peak_settings(20e-6);
    settings.maximum_root_iterations = 1;
    c::Reference reference;
    success(reference.initialize(free_config(40000.0), impact_start(), settings), "root budget initialize");
    success(reference.advance(40e-6), "populate intervals and nonzero peaks before root failure");
    auto control = reference;
    require(!reference.intervals().empty() && !reference.events().empty() &&
        reference.diagnostics().located_hammer_force.value > 0.0, "root rollback fixture must be populated");
    const auto rejected = reference.advance(210e-6);
    require(rejected.status == c::Status::budget_exhausted && rejected.failure == c::Failure::root_limit,
        "stationary root must honor its existing iteration cap");
    require(rejected.attempted.root_iterations > 0 && rejected.attempted.accepted_steps > 0,
        "root failure must follow actual root work and accepted temporary progress");
    same(reference, control);
    auto off = settings;
    off.locate_peak_candidates = false;
    c::Reference ordinary;
    success(ordinary.initialize(free_config(40000.0), impact_start(), off), "ordinary root budget initialize");
    success(ordinary.advance(40e-6), "ordinary seed advance");
    success(ordinary.advance(210e-6), "ordinary trajectory needs no force-stationary root");
    success(reference.advance(10e-6), "continue after rolled-back root failure");
    success(control.advance(10e-6), "unfailed control continuation");
    same(reference, control);
}
} // namespace

int main(int argc, char** argv) {
    const std::string_view selected = argc == 2 ? argv[1] : "all";
    const std::array<std::pair<std::string_view, void (*)()>, 6> tests{{
        {"peak_defaults", peak_defaults}, {"peak_conservative", peak_conservative},
        {"peak_damped", peak_damped}, {"peak_damper_face", peak_damper_face},
        {"peak_actions", peak_actions}, {"peak_rollback", peak_rollback}
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
