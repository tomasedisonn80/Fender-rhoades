#include "rhodes/contact_reference.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Always-active checks: Release/NDEBUG must not remove numerical acceptance.
// Synthetic constants and budgets are frozen in CONTACT_ACCEPTANCE_20261123.
// Independent equations below deliberately do not call the candidate evaluator.
namespace {
namespace contact = rhodes::contact;
using contact::Configuration;
using contact::Reference;
using contact::Settings;
using contact::State;
constexpr double pi = std::numbers::pi;
constexpr double reduced_mass = 0.0016;
const double relative_frequency = std::sqrt(20000.0 / reduced_mass);
const double separation_time = pi / relative_frequency;
constexpr std::array<double, 4> steps{20e-6, 10e-6, 5e-6, 2.5e-6};

void require(bool condition, std::string_view message) {
    if (!condition) { throw std::runtime_error(std::string{message}); }
}
void require(contact::Result result, std::string_view message) {
    if (!result) {
        throw std::runtime_error(std::string{message} + " status=" +
                                 std::to_string(static_cast<int>(result.status)) +
                                 " failure=" + std::to_string(static_cast<int>(result.failure)));
    }
}
void near(double actual, double expected, double absolute, double relative,
          std::string_view message) {
    if (!std::isfinite(actual) || !std::isfinite(expected) ||
        std::abs(actual - expected) > absolute + relative * std::abs(expected)) {
        throw std::runtime_error(std::string{message} + ": actual=" +
                                 std::to_string(actual) + " expected=" + std::to_string(expected));
    }
}
void position(double value, double expected, std::string_view what) {
    near(value, expected, 1e-8, 1e-4, what);
}
void velocity(double value, double expected, std::string_view what) {
    near(value, expected, 1e-5, 1e-4, what);
}
void impulse(double value, double expected, std::string_view what) {
    near(value, expected, 1e-8, 1e-4, what);
}

Settings options(double step = 2.5e-6) {
    Settings settings;
    settings.maximum_step_s = step;
    settings.event_time_tolerance_s = 2.5e-9;
    return settings;
}
Configuration free_configuration(double damping = 0.0) {
    Configuration config;
    config.modes = {{{0.002, 0.0, 0.0}, {0.001, 650.0, 0.0}}};
    config.hammer_projection = {1.0, 0.0};
    config.hammer.c_ns_per_m2 = damping;
    return config;
}
State free_initial() {
    State state;
    state.hammer_v_m_per_s = 1.0;
    return state;
}
State supported_initial() {
    State state;
    state.hammer_x_m = -0.0002;
    state.hammer_v_m_per_s = 1.0;
    return state;
}

struct Law {
    double force;
    double potential;
    double loss_power;
};
Law independent_law(double gap, double speed, const contact::ContactLaw& law) {
    if (!law.enabled || gap <= 0.0) { return {0.0, 0.0, 0.0}; }
    const double elastic = law.k_n_per_m * gap;
    const double raw = elastic + law.c_ns_per_m2 * gap * speed;
    return raw <= 0.0 ? Law{0.0, 0.5 * elastic * gap, -elastic * speed}
                      : Law{raw, 0.5 * elastic * gap, law.c_ns_per_m2 * gap * speed * speed};
}
std::pair<double, double> independent_face(const contact::Trajectory& motion, double time) {
    const double elapsed = time - motion.origin_s;
    if (motion.kind == contact::TrajectoryKind::sinusoidal) {
        const double phase = motion.angular_frequency_rad_per_s * elapsed + motion.phase_rad;
        return {motion.offset_m + motion.amplitude_m * std::cos(phase),
                -motion.amplitude_m * motion.angular_frequency_rad_per_s * std::sin(phase)};
    }
    if (motion.kind == contact::TrajectoryKind::linear) {
        return {motion.offset_m + motion.velocity_m_per_s * elapsed, motion.velocity_m_per_s};
    }
    return {motion.offset_m, 0.0};
}
double dot(const std::array<double, 2>& a, const std::array<double, 2>& b) {
    return a[0] * b[0] + a[1] * b[1];
}
double independent_energy(const Configuration& config, const State& state) {
    double energy = 0.5 * config.hammer_mass_kg * state.hammer_v_m_per_s * state.hammer_v_m_per_s;
    for (std::size_t j = 0; j < 2; ++j) {
        const auto mode = config.modes[j];
        const double omega = 2.0 * pi * mode.frequency_hz;
        energy += 0.5 * mode.mass_kg * (state.v_m_per_s[j] * state.v_m_per_s[j] +
                                      omega * omega * state.q_m[j] * state.q_m[j]);
    }
    const auto [face, face_speed] = independent_face(config.damper_motion, state.time_s);
    energy += independent_law(state.hammer_x_m - dot(config.hammer_projection, state.q_m) - config.hammer_clearance_m,
                              state.hammer_v_m_per_s - dot(config.hammer_projection, state.v_m_per_s), config.hammer).potential;
    energy += independent_law(dot(config.damper_projection, state.q_m) - face,
                              dot(config.damper_projection, state.v_m_per_s) - face_speed, config.damper).potential;
    return energy;
}
void instantaneous(const Reference& reference) {
    const auto& c = reference.config();
    const auto& s = reference.state();
    const auto& o = reference.observation();
    const auto [face, face_speed] = independent_face(c.damper_motion, s.time_s);
    const double dh = s.hammer_x_m - dot(c.hammer_projection, s.q_m) - c.hammer_clearance_m;
    const double wh = s.hammer_v_m_per_s - dot(c.hammer_projection, s.v_m_per_s);
    const double dd = dot(c.damper_projection, s.q_m) - face;
    const double wd = dot(c.damper_projection, s.v_m_per_s) - face_speed;
    const auto h = independent_law(dh, wh, c.hammer);
    const auto d = independent_law(dd, wd, c.damper);
    near(o.hammer.force_n, h.force, 1e-10, 1e-10, "independent hammer force law");
    near(o.damper.force_n, d.force, 1e-10, 1e-10, "independent damper force law");
    if (h.force == 0.0) { require(o.hammer.force_n == 0.0, "separated/clamped hammer force exactly zero"); }
    if (d.force == 0.0) { require(o.damper.force_n == 0.0, "separated/clamped damper force exactly zero"); }
    near(o.total_energy_j, independent_energy(c, s), 1e-15, 1e-12, "independent stored energy");
    near(o.hammer.active_loss_power_w + o.hammer.clipped_loss_power_w, h.loss_power,
         1e-12, 1e-10, "branch-specific hammer loss power");
    near(o.damper.active_loss_power_w + o.damper.clipped_loss_power_w, d.loss_power,
         1e-12, 1e-10, "branch-specific damper loss power");
    near(o.damper_work_power_w, -d.force * face_speed, 1e-12, 1e-10, "signed actuator power");
    double derivative = c.hammer_mass_kg * s.hammer_v_m_per_s * o.hammer_acceleration_m_per_s2;
    double background = 0.0;
    for (std::size_t j = 0; j < 2; ++j) {
        const auto mode = c.modes[j];
        const double omega = 2.0 * pi * mode.frequency_hz;
        derivative += mode.mass_kg * s.v_m_per_s[j] * o.modal_acceleration_m_per_s2[j] +
                      mode.mass_kg * omega * omega * s.q_m[j] * s.v_m_per_s[j];
        background += 2.0 * mode.mass_kg * mode.decay_per_s * s.v_m_per_s[j] * s.v_m_per_s[j];
    }
    if (c.hammer.enabled) { derivative += c.hammer.k_n_per_m * std::max(0.0, dh) * wh; }
    if (c.damper.enabled) { derivative += c.damper.k_n_per_m * std::max(0.0, dd) * wd; }
    const double residual = derivative + background + h.loss_power + d.loss_power + d.force * face_speed;
    near(residual, 0.0, 1e-10, 0.0, "independent instantaneous power identity");
}
void energy_gate(const Reference& reference, double scale) {
    const auto& ledger = reference.ledger();
    const double cumulative_budget = 2e-9 + 5e-5 * scale;
    const double interval_budget = 4e-9 + 2e-4 * scale;
    const double residual = independent_energy(reference.config(), reference.state()) - ledger.initial_energy_j +
                            ledger.total_loss_j() - ledger.total_work_j();
    require(std::abs(residual) <= cumulative_budget, "cumulative independently assembled energy residual");
    require(ledger.maximum_absolute_cumulative_residual_j <= cumulative_budget,
            "maximum cumulative energy residual");
    require(ledger.sum_absolute_interval_residual_j <= interval_budget,
            "sum absolute accepted-interval residuals");
    require(ledger.hammer_active_loss_j >= 0.0 && ledger.hammer_clipped_loss_j >= 0.0 &&
            ledger.damper_active_loss_j >= 0.0 && ledger.damper_clipped_loss_j >= 0.0 &&
            ledger.background_loss_j >= 0.0, "physical losses nonnegative");
    for (const auto& interval : reference.intervals()) {
        require(interval.end_s > interval.begin_s && interval.scaled_error <= 1.0,
                "every accepted interval progresses with converged local error");
        near(interval.residual_j, interval.energy_change_j + interval.physical_loss_j - interval.actuator_work_j,
             1e-15, 1e-10, "accepted-interval residual accounting");
    }
}
template<class Check>
void sample_grid(Reference& reference, double end, Check check) {
    check(reference);
    std::size_t index = 1;
    while (reference.state().time_s < end - 1e-15) {
        const double next = std::min(end, static_cast<double>(index) * 20e-6);
        require(reference.advance(next - reference.state().time_s), "advance common physical-time grid");
        check(reference);
        ++index;
    }
    near(reference.state().time_s, end, 1e-15, 0.0, "complete requested final time");
}
State conservative_exact(double time) {
    State expected;
    expected.time_s = time;
    const double contact_time = std::min(time, separation_time);
    const double compression = time >= separation_time ? 0.0 : std::sin(relative_frequency * time) / relative_frequency;
    const double relative = time >= separation_time ? -1.0 : std::cos(relative_frequency * time);
    expected.hammer_x_m = 0.8 * contact_time + 0.2 * compression;
    expected.q_m[0] = 0.8 * contact_time - 0.8 * compression;
    expected.hammer_v_m_per_s = 0.8 + 0.2 * relative;
    expected.v_m_per_s[0] = 0.8 - 0.8 * relative;
    expected.hammer_x_m += expected.hammer_v_m_per_s * (time - contact_time);
    expected.q_m[0] += expected.v_m_per_s[0] * (time - contact_time);
    return expected;
}
void event_brackets(const Reference& reference) {
    double previous = -1.0;
    for (const auto& event : reference.events()) {
        require(std::isfinite(event.time_s) && event.time_s >= previous, "chronological finite events");
        require(event.bracket_begin_s <= event.time_s && event.time_s <= event.bracket_end_s,
                "event midpoint inside bracket");
        require(event.bracket_end_s - event.bracket_begin_s <= reference.settings().event_time_tolerance_s * (1.0 + 1e-9),
                "event bracket respects frozen requested width");
        previous = event.time_s;
    }
}

void conservative_impact() {
    for (double step : steps) {
        Reference reference;
        require(reference.initialize(free_configuration(), free_initial(), options(step)), "initialize exact free impact");
        sample_grid(reference, 0.002, [](const Reference& candidate) {
            const auto& s = candidate.state();
            const auto e = conservative_exact(s.time_s);
            position(s.hammer_x_m, e.hammer_x_m, "analytic hammer position");
            position(s.q_m[0], e.q_m[0], "analytic resonator position");
            velocity(s.hammer_v_m_per_s, e.hammer_v_m_per_s, "analytic hammer velocity");
            velocity(s.v_m_per_s[0], e.v_m_per_s[0], "analytic resonator velocity");
            const double expected_impulse = s.time_s >= separation_time ? 0.0032 :
                                            reduced_mass * (1.0 - std::cos(relative_frequency * s.time_s));
            impulse(candidate.ledger().hammer_impulse_ns, expected_impulse, "analytic integrated impulse");
            near(0.008 * s.hammer_v_m_per_s + 0.002 * s.v_m_per_s[0], 0.008,
                 5e-9, 1e-6, "free impact momentum");
            instantaneous(candidate);
        });
        std::size_t onsets = 0;
        std::size_t separations = 0;
        for (const auto& event : reference.events()) {
            if (event.contact != contact::ContactId::hammer) { continue; }
            if (event.kind == contact::EventKind::onset) {
                ++onsets;
                near(event.time_s, 0.0, 1e-15, 0.0, "initial impact onset at zero");
            }
            if (event.kind == contact::EventKind::separation) {
                ++separations;
                near(event.time_s, separation_time, 1e-7, 1e-4, "analytic separation event time");
                require(event.relative_velocity_m_per_s < 0.0, "separation is opening motion");
            }
        }
        require(onsets == 1 && separations == 1, "one initial onset and one separation");
        require(reference.ledger().total_loss_j() == 0.0, "conservative impact has zero physical loss");
        event_brackets(reference);
        energy_gate(reference, 0.004);
        std::cout << "conservative h=" << step << " impulse=" << reference.ledger().hammer_impulse_ns
                  << " residual=" << reference.ledger().maximum_absolute_cumulative_residual_j << '\n';
    }
}
double damped_rebound() {
    const auto phi = [](double value) { return 2.0 * value - std::log1p(2.0 * value); };
    double low = -0.499999999999;
    double high = -1e-15;
    for (int i = 0; i < 100; ++i) {
        const double middle = 0.5 * (low + high);
        if (phi(middle) > phi(1.0)) { low = middle; } else { high = middle; }
    }
    return 0.5 * (low + high);
}
void damped_impact() {
    const double rebound = damped_rebound();
    const double loss = 0.5 * reduced_mass * (1.0 - rebound * rebound);
    for (double step : steps) {
        Reference reference;
        require(reference.initialize(free_configuration(40000.0), free_initial(), options(step)), "initialize damped analytic rebound");
        sample_grid(reference, 0.002, [](const Reference& candidate) { instantaneous(candidate); });
        const auto& s = reference.state();
        velocity(s.hammer_v_m_per_s - s.v_m_per_s[0], rebound, "scalar first-integral rebound");
        impulse(reference.ledger().hammer_impulse_ns, reduced_mass * (1.0 - rebound), "damped analytic impulse");
        near(reference.ledger().total_loss_j(), loss, 2e-9 + 5e-5 * 0.004, 0.0, "independent damped physical loss");
        require(reference.ledger().hammer_clipped_loss_j == 0.0, "ordinary damped free impact does not prove clamp coverage");
        energy_gate(reference, 0.004);
        event_brackets(reference);
    }
}
void forced_clamp() {
    State initial;
    initial.hammer_x_m = 0.0001;
    initial.hammer_v_m_per_s = -1.0;
    Reference reference;
    require(reference.initialize(free_configuration(40000.0), initial, options()), "initialize forced positive-compression clamp");
    require(reference.observation().hammer.branch == contact::Branch::clipped, "positive compression classified separately from separation");
    sample_grid(reference, 0.0002, [](const Reference& candidate) {
        const auto& s = candidate.state();
        position(s.hammer_x_m, 0.0001 - s.time_s, "clipped free hammer trajectory");
        require(s.hammer_v_m_per_s == -1.0 && s.v_m_per_s[0] == 0.0,
                "clipped force cannot change velocities");
        require(candidate.observation().hammer.force_n == 0.0 && candidate.ledger().hammer_impulse_ns == 0.0,
                "forced clamp force and impulse exactly zero");
        const double gap = std::max(0.0, 0.0001 - s.time_s);
        near(candidate.ledger().hammer_clipped_loss_j, 10000.0 * (1e-8 - gap * gap),
             2e-9 + 5e-5 * 0.0041, 0.0, "independent integral of clamp loss");
        instantaneous(candidate);
    });
    near(reference.ledger().hammer_clipped_loss_j, 0.0001, 2e-9 + 5e-5 * 0.0041, 0.0,
         "clamp loses stored potential, not unclipped damping expression");
    require(reference.ledger().hammer_active_loss_j == 0.0, "no active loss credit on clamp branch");
    std::size_t separations = 0;
    for (const auto& event : reference.events()) {
        if (event.kind == contact::EventKind::separation && event.contact == contact::ContactId::hammer) {
            ++separations;
            near(event.time_s, 0.0001, 1e-7, 1e-4, "clamp geometric separation time");
        }
    }
    require(separations == 1, "forced clamp separates once");
    energy_gate(reference, 0.0041);
}
std::pair<double, double> oscillator_exact(double q0, double v0, const contact::Mode& mode, double time) {
    const double omega = 2.0 * pi * mode.frequency_hz;
    const double beta = mode.decay_per_s;
    const double wd = std::sqrt(omega * omega - beta * beta);
    const double a = q0;
    const double b = (v0 + beta * q0) / wd;
    const double sine = std::sin(wd * time);
    const double cosine = std::cos(wd * time);
    const double envelope = std::exp(-beta * time);
    const double q = envelope * (a * cosine + b * sine);
    return {q, -beta * q + envelope * wd * (-a * sine + b * cosine)};
}
void separated_oscillator() {
    Configuration config;
    State initial;
    initial.q_m = {0.00008, -0.00002};
    initial.v_m_per_s = {0.2, -0.1};
    initial.hammer_x_m = -0.002;
    constexpr std::array<double, 5> outer{17e-6, 31e-6, 7e-6, 103e-6, 2.5e-6};
    for (double step : steps) {
        Reference reference;
        require(reference.initialize(config, initial, options(step)), "initialize separated oscillator");
        std::size_t n = 0;
        while (reference.state().time_s < 0.020 - 1e-15) {
            const double dt = std::min(outer[n % outer.size()], 0.020 - reference.state().time_s);
            require(reference.advance(dt), "arbitrary positive physical-time increments");
            for (std::size_t j = 0; j < 2; ++j) {
                const auto [q, v] = oscillator_exact(initial.q_m[j], initial.v_m_per_s[j], config.modes[j], reference.state().time_s);
                position(reference.state().q_m[j], q, "separated scalar oscillator position");
                velocity(reference.state().v_m_per_s[j], v, "separated scalar oscillator velocity");
            }
            require(reference.observation().hammer.force_n == 0.0 && reference.ledger().hammer_impulse_ns == 0.0,
                    "separated oscillator has exactly zero contact force/impulse");
            instantaneous(reference);
            ++n;
        }
        require(reference.events().empty(), "separated oscillation has no contact events");
        energy_gate(reference, 0.0001);
    }
}
std::pair<Configuration, State> preload_fixture() {
    Configuration config;
    config.damper.enabled = true;
    config.damper_motion.offset_m = -0.00005;
    State initial;
    initial.hammer_x_m = -0.002;
    std::array<double, 2> stiffness{};
    double compliance = 0.0;
    for (std::size_t j = 0; j < 2; ++j) {
        const double omega = 2.0 * pi * config.modes[j].frequency_hz;
        stiffness[j] = config.modes[j].mass_kg * omega * omega;
        compliance += config.damper_projection[j] * config.damper_projection[j] / stiffness[j];
    }
    const double compression = -config.damper_motion.offset_m / (1.0 + config.damper.k_n_per_m * compliance);
    const double force = config.damper.k_n_per_m * compression;
    for (std::size_t j = 0; j < 2; ++j) { initial.q_m[j] = -config.damper_projection[j] * force / stiffness[j]; }
    return {config, initial};
}
void preload() {
    const auto [config, equilibrium] = preload_fixture();
    contact::Equilibrium helper;
    require(contact::fixed_damper_equilibrium(config, equilibrium, helper), "guarded equilibrium helper");
    for (std::size_t j = 0; j < 2; ++j) {
        near(helper.state.q_m[j], equilibrium.q_m[j], 1e-15, 1e-12, "assembled independent equilibrium");
        near(helper.static_force_residual_n[j], 0.0, 1e-10, 0.0, "equilibrium force residual");
    }
    for (double displacement : {0.0, 1e-6, -1e-6}) {
        State initial = equilibrium;
        initial.q_m[0] += displacement;
        Reference reference;
        require(reference.initialize(config, initial, options()), "initialize unshifted fixed preload");
        const double energy = independent_energy(config, initial);
        sample_grid(reference, 0.020, [&](const Reference& candidate) {
            instantaneous(candidate);
            require(candidate.observation().total_energy_j <= energy + 2e-9 + 5e-5 * 0.00001,
                    "fixed preloaded stored-plus-modal energy does not grow");
            if (displacement == 0.0) {
                for (std::size_t j = 0; j < 2; ++j) {
                    position(candidate.state().q_m[j], equilibrium.q_m[j], "resting equilibrium remains fixed");
                    velocity(candidate.state().v_m_per_s[j], 0.0, "resting equilibrium remains still");
                }
            }
        });
        require(reference.ledger().damper_work_j == 0.0, "fixed face supplies no work");
        energy_gate(reference, 0.00001);
    }
}
void moving_damper() {
    const auto [base, initial] = preload_fixture();
    for (double sign : {-1.0, 1.0}) {
        auto config = base;
        config.damper_motion.kind = contact::TrajectoryKind::linear;
        config.damper_motion.velocity_m_per_s = sign * 0.02;
        Reference reference;
        require(reference.initialize(config, initial, options()), "initialize signed damper port fixture");
        require(sign * reference.observation().damper_work_power_w < 0.0, "opposite travel has opposite initial work signs");
        sample_grid(reference, 0.0001, [](const Reference& candidate) { instantaneous(candidate); });
        require(sign * reference.ledger().damper_work_j < 0.0, "signed actuator work retained");
        energy_gate(reference, 0.0001);
    }
    auto config = base;
    config.damper_motion.kind = contact::TrajectoryKind::sinusoidal;
    config.damper_motion.offset_m = 0.00015;
    config.damper_motion.amplitude_m = 0.00025;
    config.damper_motion.angular_frequency_rad_per_s = 2.0 * pi / 0.012;
    State start;
    start.hammer_x_m = -0.002;
    Reference reference;
    require(reference.initialize(config, start, options()), "initialize continuous moving face");
    sample_grid(reference, 0.024, [](const Reference& candidate) { instantaneous(candidate); });
    require(reference.ledger().damper_impulse_ns > 0.0 && reference.ledger().total_loss_j() > 0.0,
            "moving face engages and dissipates");
    event_brackets(reference);
    energy_gate(reference, 0.0001);
}
void exact_relaunch_preservation(Reference& reference, double speed) {
    const auto before = reference.state();
    const auto energy = reference.observation().total_energy_j;
    const double old_work = reference.ledger().relaunch_work_j;
    const double expected = 0.5 * reference.config().hammer_mass_kg *
                            (speed * speed - before.hammer_v_m_per_s * before.hammer_v_m_per_s);
    require(reference.relaunch_hammer(speed), "velocity-only hammer relaunch");
    const auto& after = reference.state();
    require(after.q_m == before.q_m && after.v_m_per_s == before.v_m_per_s &&
            after.hammer_x_m == before.hammer_x_m && after.time_s == before.time_s,
            "event preserves every position, time, and modal velocity exactly");
    require(after.hammer_v_m_per_s == speed, "event sets requested hammer velocity");
    near(reference.ledger().relaunch_work_j - old_work, expected, 1e-12, 1e-10, "exact signed launch kinetic work");
    near(reference.observation().total_energy_j - energy, expected, 1e-12, 1e-10, "launch changes no contact potential");
}
void relaunch() {
    Configuration config;
    config.hammer.c_ns_per_m2 = 40000.0;
    for (double sign : {1.0, -1.0}) {
        State initial;
        initial.q_m = {0.00008, -0.00002};
        initial.v_m_per_s = {sign * 0.2, -sign * 0.1};
        initial.hammer_x_m = dot(config.hammer_projection, initial.q_m) + 0.00005;
        initial.hammer_v_m_per_s = -0.3;
        Reference reference;
        require(reference.initialize(config, initial, options()), "initialize opposite-phase relaunch");
        exact_relaunch_preservation(reference, 1.0);
        near(reference.ledger().relaunch_work_j, 0.00364, 1e-12, 1e-10, "frozen phase relaunch work");
        near(reference.observation().hammer.force_n, sign > 0.0 ? 2.67 : 3.33,
             1e-10, 1e-10, "phase-dependent immediate contact force");
        near(reference.observation().hammer.gap_rate_m_per_s, sign > 0.0 ? 0.835 : 1.165,
             1e-12, 1e-10, "relative impact velocity distinct from launch velocity");
        require(reference.advance(0.002), "advance phase fixture after launch");
        energy_gate(reference, 0.005);
    }
    Reference evolved;
    require(evolved.initialize(Configuration{}, supported_initial(), options()), "initialize evolved retrigger");
    require(evolved.advance(0.0037), "evolve before retrigger");
    exact_relaunch_preservation(evolved, 1.0);
    require(evolved.advance(0.020 - evolved.state().time_s), "continue evolved retrigger");
    energy_gate(evolved, 0.005);
}
void event_order() {
    for (double step : steps) {
    for (double offset : {-1e-6, 0.0, 1e-6}) {
        Reference reference;
        require(reference.initialize(free_configuration(), free_initial(), options(step)), "initialize event ordering");
        const double external_time = separation_time + offset;
        require(reference.advance(external_time), "integrate old dynamics to exact external time");
        const auto expected = conservative_exact(external_time);
        position(reference.state().q_m[0], expected.q_m[0], "pre-event analytic position");
        velocity(reference.state().v_m_per_s[0], expected.v_m_per_s[0], "pre-event analytic velocity");
        exact_relaunch_preservation(reference, 1.0);
        require(reference.events().back().kind == contact::EventKind::hammer_relaunch,
                "action recorded after old dynamics reach event time");
        require(reference.events().back().time_s == external_time, "relaunch event uses exact external time");
        if (offset >= 0.0) {
            require(std::any_of(reference.events().begin(), reference.events().end(), [](const auto& event) {
                return event.kind == contact::EventKind::separation;
            }), "at/after-separation launch processes separation before action");
        }
        require(reference.advance(0.002 - external_time), "continue through event ordering fixture");
        event_brackets(reference);
        energy_gate(reference, 0.005);
    }
    }
}
double pickup(const State& state, const std::array<double, 2>& projection) {
    const double x = dot(projection, state.q_m);
    const double v = dot(projection, state.v_m_per_s);
    return -(0.4 + 1600.0 * x + 1200000.0 * x * x) * v;
}
void compare_scaled(const Reference& base, const Reference& scaled) {
    constexpr std::array<double, 2> scale{2.0, -3.0};
    for (std::size_t j = 0; j < 2; ++j) {
        near(scaled.state().q_m[j] / scale[j], base.state().q_m[j], 1e-11, 1e-8, "rescaled physical displacement");
        near(scaled.state().v_m_per_s[j] / scale[j], base.state().v_m_per_s[j], 1e-8, 1e-8, "rescaled physical velocity");
    }
    near(scaled.state().hammer_x_m, base.state().hammer_x_m, 1e-11, 1e-8, "rescaled hammer position");
    near(scaled.state().hammer_v_m_per_s, base.state().hammer_v_m_per_s, 1e-8, 1e-8, "rescaled hammer velocity");
    for (const auto& pair : {std::pair{base.observation().hammer, scaled.observation().hammer},
                           std::pair{base.observation().damper, scaled.observation().damper}}) {
        near(pair.second.gap_m, pair.first.gap_m, 1e-11, 1e-8, "rescaled gap");
        near(pair.second.force_n, pair.first.force_n, 1e-7, 1e-8, "rescaled contact force");
    }
    near(scaled.ledger().hammer_impulse_ns, base.ledger().hammer_impulse_ns, 1e-10, 1e-8, "rescaled hammer impulse");
    near(scaled.ledger().damper_impulse_ns, base.ledger().damper_impulse_ns, 1e-10, 1e-8, "rescaled damper impulse");
    near(scaled.ledger().current_energy_j, base.ledger().current_energy_j, 1e-10, 1e-8, "rescaled energy");
    near(scaled.ledger().total_loss_j(), base.ledger().total_loss_j(), 1e-10, 1e-8, "rescaled physical loss");
    near(scaled.ledger().total_work_j(), base.ledger().total_work_j(), 1e-10, 1e-8, "rescaled signed work");
    near(pickup(scaled.state(), {0.5, -0.2 / 3.0}), pickup(base.state(), {1.0, 0.2}),
         1e-7, 1e-8, "rescaled nonlinear pickup voltage after modal summation");
}
void scaling() {
    for (int fixture = 0; fixture < 3; ++fixture) {
        Configuration base;
        State initial = supported_initial();
        double end = 0.020;
        if (fixture == 1) {
            base.damper.enabled = true;
            base.damper_motion.kind = contact::TrajectoryKind::sinusoidal;
            base.damper_motion.offset_m = 0.00015;
            base.damper_motion.amplitude_m = 0.00025;
            base.damper_motion.angular_frequency_rad_per_s = 2.0 * pi / 0.012;
            initial = State{};
            initial.hammer_x_m = -0.002;
            end = 0.024;
        }
        Configuration transformed = base;
        State scaled_initial = initial;
        constexpr std::array<double, 2> scale{2.0, -3.0};
        for (std::size_t j = 0; j < 2; ++j) {
            transformed.modes[j].mass_kg /= scale[j] * scale[j];
            transformed.hammer_projection[j] /= scale[j];
            transformed.damper_projection[j] /= scale[j];
            scaled_initial.q_m[j] *= scale[j];
            scaled_initial.v_m_per_s[j] *= scale[j];
        }
        Reference a;
        Reference b;
        require(a.initialize(base, initial, options()) , "initialize base coordinate fixture");
        require(b.initialize(transformed, scaled_initial, options()), "initialize signed coordinate fixture");
        const auto count = static_cast<std::size_t>(std::llround(end / 20e-6));
        for (std::size_t i = 1; i <= count; ++i) {
            const double time = static_cast<double>(i) * 20e-6;
            require(a.advance(time - a.state().time_s), "advance base coordinates");
            require(b.advance(time - b.state().time_s), "advance scaled coordinates");
            if (fixture == 2 && i == 185) {
                exact_relaunch_preservation(a, 1.0);
                exact_relaunch_preservation(b, 1.0);
            }
            compare_scaled(a, b);
        }
        require(a.events().size() == b.events().size(), "rescaled event counts agree exactly");
        for (std::size_t i = 0; i < a.events().size(); ++i) {
            require(a.events()[i].kind == b.events()[i].kind && a.events()[i].contact == b.events()[i].contact,
                    "rescaled event order agrees exactly");
            near(b.events()[i].time_s, a.events()[i].time_s, 1e-9, 1e-8, "rescaled event time");
        }
        energy_gate(a, fixture == 1 ? 0.0001 : fixture == 2 ? 0.005 : 0.004);
        energy_gate(b, fixture == 1 ? 0.0001 : fixture == 2 ? 0.005 : 0.004);
    }
}

void same_reference(const Reference& actual, const Reference& expected) {
    require(actual.initialized() == expected.initialized() && actual.config() == expected.config() &&
            actual.settings() == expected.settings() && actual.state() == expected.state() &&
            actual.ledger() == expected.ledger() && actual.diagnostics() == expected.diagnostics() &&
            actual.observation() == expected.observation() && actual.events() == expected.events() &&
            actual.intervals() == expected.intervals(), "failed call changed persistent state/history/diagnostics");
}
void invalid_inputs() {
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    Reference reference;
    const Reference uninitialized = reference;
    const auto absent = reference.advance(1e-4);
    require(!absent && absent.status == contact::Status::invalid_input, "uninitialized advance typed failure");
    same_reference(reference, uninitialized);
    require(reference.initialize(Configuration{}, supported_initial(), options()), "initialize rollback baseline");
    require(reference.advance(0.0003), "populate state, ledger, and histories before invalid calls");
    const Reference baseline = reference;
    for (double dt : {0.0, -1.0, 1e-13, std::nextafter(1.0, inf), nan, inf}) {
        const auto result = reference.advance(dt);
        require(!result && result.status == contact::Status::invalid_input, "invalid dt typed failure");
        same_reference(reference, baseline);
    }
    for (double speed : {std::nextafter(1000.0, inf), -1001.0, nan, inf}) {
        const auto result = reference.relaunch_hammer(speed);
        require(!result && result.status == contact::Status::invalid_input, "invalid relaunch typed failure");
        same_reference(reference, baseline);
    }
    for (int field = 0; field < 11; ++field) {
        auto config = Configuration{};
        switch (field) {
            case 0: config.modes[0].mass_kg = 0.0; break;
            case 1: config.modes[0].mass_kg = nan; break;
            case 2: config.modes[1].frequency_hz = -1.0; break;
            case 3: config.modes[1].frequency_hz = 20001.0; break;
            case 4: config.modes[0].decay_per_s = inf; break;
            case 5: config.hammer_mass_kg = 2.0; break;
            case 6: config.hammer_projection[1] = 101.0; break;
            case 7: config.hammer.k_n_per_m = -1.0; break;
            case 8: config.hammer.c_ns_per_m2 = inf; break;
            case 9: config.hammer.exponent = 1.5; break;
            default: config.hammer_clearance_m = nan; break;
        }
        const auto result = reference.initialize(config, supported_initial(), options());
        require(!result && result.status == contact::Status::invalid_input, "invalid configuration typed failure");
        same_reference(reference, baseline);
    }
    for (int field = 0; field < 5; ++field) {
        auto state = supported_initial();
        if (field == 0) { state.q_m[0] = inf; }
        if (field == 1) { state.v_m_per_s[1] = nan; }
        if (field == 2) { state.time_s = 1001.0; }
        if (field == 3) { state.hammer_x_m = 2.0; }
        if (field == 4) { state.hammer_v_m_per_s = -1001.0; }
        const auto result = reference.initialize(Configuration{}, state, options());
        require(!result && result.status == contact::Status::invalid_input, "invalid initial state typed failure");
        same_reference(reference, baseline);
    }
    auto trajectory = baseline.config().damper_motion;
    trajectory.offset_m += 0.001;
    const auto jump = reference.set_damper_motion(trajectory);
    require(!jump && jump.status == contact::Status::invalid_input,
            "discontinuous prescribed face must not teleport stored contact energy");
    same_reference(reference, baseline);
    auto bad_settings = options();
    bad_settings.maximum_step_s = nan;
    require(!reference.initialize(Configuration{}, supported_initial(), bad_settings), "invalid integration settings rejected");
    same_reference(reference, baseline);
    // Public rollback equality plus deterministic continuation exercises hidden
    // compensated sums and adaptive-controller history after rejected calls.
    Reference control = baseline;
    require(reference.advance(0.0004), "continue invalid-call candidate");
    require(control.advance(0.0004), "continue rollback control");
    same_reference(reference, control);
    State boundary = supported_initial();
    boundary.time_s = 1000.0;
    Reference time_limit;
    require(time_limit.initialize(Configuration{}, boundary, options()), "stored-time upper endpoint permitted");
    const auto before = time_limit;
    const auto overflow = time_limit.advance(1e-12);
    require(!overflow && overflow.status == contact::Status::invalid_input, "stored time plus dt finite domain");
    same_reference(time_limit, before);
}
void cap_exhaustion() {
    for (int cap = 0; cap < 5; ++cap) {
        auto settings = options();
        if (cap == 0) { settings.maximum_trials = 1; }
        if (cap == 1) { settings.maximum_rhs_evaluations = 1; }
        if (cap == 2) { settings.maximum_event_rows = 1; }
        if (cap == 3) { settings.maximum_interval_rows = 1; }
        if (cap == 4) {
            settings.maximum_root_iterations = 1;
            settings.event_time_tolerance_s = 1e-12;
        }
        Reference reference;
        require(reference.initialize(free_configuration(), free_initial(), settings), "initialize explicit finite-cap fixture");
        const auto before = reference;
        const auto result = reference.advance(0.002);
        require(!result && result.status == contact::Status::budget_exhausted,
                "resource cap returns typed exhaustion, never partial success");
        require(result.attempted.trials > 0, "failed call retains attempted-work diagnostics in return value");
        same_reference(reference, before);
        std::cout << "cap=" << cap << " failure=" << static_cast<int>(result.failure)
                  << " trials=" << result.attempted.trials
                  << " rhs=" << result.attempted.rhs_evaluations << '\n';
    }
}
void grazing_case(double perturbation) {
    constexpr double turning = 0.0010073;
    constexpr double amplitude = 0.0001;
    const double omega = 2.0 * pi * 200.0;
    {
        Configuration config;
        config.modes[0].decay_per_s = 0.0;
        config.modes[1].decay_per_s = 0.0;
        config.hammer_projection = {1.0, 0.0};
        State initial;
        initial.q_m = {-amplitude * std::cos(omega * turning), 0.0};
        initial.v_m_per_s = {-amplitude * omega * std::sin(omega * turning), 0.0};
        initial.hammer_x_m = -amplitude + perturbation;
        Reference reference;
        require(reference.initialize(config, initial, options()), "initialize distinct grazing geometry");
        require(reference.advance(turning - 30e-6), "advance to grazing outer interval");
        require(reference.advance(60e-6), "resolve two-root/tangent 60 us outer interval");
        const double delivered_impulse = reference.ledger().hammer_impulse_ns;
        if (perturbation <= 0.0) {
            require(delivered_impulse == 0.0, "grazing miss/tangent has exactly zero impulse");
        } else {
            require(delivered_impulse > 0.0, "short contact cannot be merged with miss or tangent");
            std::size_t count = 0;
            for (const auto& event : reference.events()) {
                if (event.kind == contact::EventKind::onset || event.kind == contact::EventKind::separation) { ++count; }
            }
            require(count == 2, "short contact has independently classified onset and separation");
        }
        for (const auto& event : reference.events()) {
            require(event.bracket_gap_uncertainty_m < 1e-12, "tight grazing gap uncertainty");
        }
        if (perturbation == 0.0) {
            require(std::any_of(reference.events().begin(), reference.events().end(), [](const auto& event) {
                return event.kind == contact::EventKind::grazing;
            }), "tangent must be distinguished from a miss");
        }
        event_brackets(reference);
        energy_gate(reference, 0.0001);
        std::cout << "grazing shift=" << perturbation << " impulse=" << delivered_impulse
                  << " (independent tight short-contact impulse gate is in Python oracle)\n";
    }
}
void grazing_miss() { grazing_case(-1e-8); }
void grazing_tangent() { grazing_case(0.0); }
void grazing_contact() { grazing_case(1e-8); }

using Test = void (*)();
constexpr std::array<std::pair<std::string_view, Test>, 14> tests{{
    {"conservative_impact", conservative_impact}, {"damped_impact", damped_impact},
    {"forced_clamp", forced_clamp}, {"separated_oscillator", separated_oscillator},
    {"preload", preload}, {"moving_damper", moving_damper}, {"relaunch", relaunch},
    {"event_order", event_order}, {"scaling", scaling}, {"invalid_inputs", invalid_inputs},
    {"cap_exhaustion", cap_exhaustion}, {"grazing_miss", grazing_miss},
    {"grazing_tangent", grazing_tangent}, {"grazing_contact", grazing_contact}
}};
} // namespace

int main(int argc, char** argv) {
    const std::string_view selected = argc > 1 ? std::string_view{argv[1]} : "all";
    bool found = false;
    bool passed = true;
    for (const auto& [name, test] : tests) {
        if (selected != "all" && selected != name) { continue; }
        found = true;
        try {
            test();
            std::cout << "PASS " << name << '\n';
        } catch (const std::exception& error) {
            std::cerr << "FAIL " << name << ": " << error.what() << '\n';
            passed = false;
        }
    }
    if (!found) { std::cerr << "Unknown contact test name: " << selected << '\n'; }
    return found && passed ? 0 : 1;
}
