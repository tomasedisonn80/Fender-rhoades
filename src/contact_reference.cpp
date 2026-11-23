#include "rhodes/contact_reference.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <new>
#include <numbers>
#include <stdexcept>
#include <utility>

namespace rhodes::contact {
namespace {
constexpr double scalar_limit = 1e24;
constexpr double minimum_mass = 1e-6;
constexpr double maximum_time = 1000.0;
constexpr std::size_t integrated_count = 8;
constexpr std::size_t dimension = 6 + integrated_count;
using Vector = std::array<double, dimension>;

[[nodiscard]] bool bounded(double x, double lo, double hi) noexcept {
    return std::isfinite(x) && x >= lo && x <= hi;
}
[[nodiscard]] bool finite_scalar(double x) noexcept {
    return bounded(x, -scalar_limit, scalar_limit);
}
[[nodiscard]] Result fail(Status status, Failure reason, double t = 0.0) noexcept {
    return {status, reason, {}, t};
}
[[nodiscard]] bool valid_trajectory(const Trajectory& tr) noexcept {
    if (!bounded(tr.offset_m, -1.0, 1.0) || !bounded(tr.origin_s, 0.0, maximum_time) ||
        !bounded(tr.velocity_m_per_s, -1000.0, 1000.0) ||
        !bounded(tr.amplitude_m, -1.0, 1.0) ||
        !bounded(tr.angular_frequency_rad_per_s, 0.0, 40000.0 * std::numbers::pi) ||
        !bounded(tr.phase_rad, -1e6, 1e6)) {
        return false;
    }
    switch (tr.kind) {
    case TrajectoryKind::constant:
    case TrajectoryKind::linear:
        return true;
    case TrajectoryKind::sinusoidal:
        return std::abs(tr.offset_m) + std::abs(tr.amplitude_m) <= 1.0 &&
               std::abs(tr.amplitude_m * tr.angular_frequency_rad_per_s) <= 1000.0;
    }
    return false;
}
[[nodiscard]] FaceSample face_unchecked(const Trajectory& tr, double t) noexcept {
    FaceSample face{tr.offset_m, 0.0, 0.0};
    if (tr.kind == TrajectoryKind::linear) {
        face.position_m += tr.velocity_m_per_s * (t - tr.origin_s);
        face.velocity_m_per_s = tr.velocity_m_per_s;
    } else if (tr.kind == TrajectoryKind::sinusoidal) {
        const double omega = tr.angular_frequency_rad_per_s;
        const double angle = omega * (t - tr.origin_s) + tr.phase_rad;
        const double oscillation = tr.amplitude_m * std::cos(angle);
        face.position_m += oscillation;
        face.velocity_m_per_s = -tr.amplitude_m * omega * std::sin(angle);
        face.acceleration_m_per_s2 = -omega * omega * oscillation;
    }
    return face;
}
[[nodiscard]] bool valid_face(const FaceSample& face) noexcept {
    return bounded(face.position_m, -1.0, 1.0) &&
           bounded(face.velocity_m_per_s, -1000.0, 1000.0) &&
           finite_scalar(face.acceleration_m_per_s2);
}
[[nodiscard]] bool trajectory_interval(const Trajectory& tr, double from, double to) noexcept {
    // For a line the extrema are the endpoints. For a sinusoid valid_trajectory
    // certifies its entire global amplitude/velocity envelope, including interiors.
    return valid_trajectory(tr) && valid_face(face_unchecked(tr, from)) &&
           valid_face(face_unchecked(tr, to));
}
[[nodiscard]] bool valid_law(const ContactLaw& law) noexcept {
    return bounded(law.k_n_per_m, 0.0, 1e8) &&
           bounded(law.c_ns_per_m2, 0.0, 1e8) && law.exponent == 1.0;
}
[[nodiscard]] bool valid_configuration(const Configuration& cfg) noexcept {
    if (!bounded(cfg.hammer_mass_kg, minimum_mass, 1.0) ||
        !bounded(cfg.hammer_clearance_m, -1.0, 1.0) ||
        !valid_law(cfg.hammer) || !valid_law(cfg.damper) ||
        !valid_trajectory(cfg.damper_motion)) {
        return false;
    }
    for (std::size_t j = 0; j < mode_count; ++j) {
        const auto& m = cfg.modes[j];
        if (!bounded(m.mass_kg, minimum_mass, 1.0) ||
            !bounded(m.frequency_hz, 0.0, 20000.0) ||
            !bounded(m.decay_per_s, 0.0, 1e5) ||
            !bounded(cfg.hammer_projection[j], -100.0, 100.0) ||
            !bounded(cfg.damper_projection[j], -100.0, 100.0)) {
            return false;
        }
    }
    return true;
}
[[nodiscard]] bool valid_state(const State& state) noexcept {
    if (!bounded(state.time_s, 0.0, maximum_time) ||
        !bounded(state.hammer_x_m, -1.0, 1.0) ||
        !bounded(state.hammer_v_m_per_s, -1000.0, 1000.0)) {
        return false;
    }
    for (std::size_t j = 0; j < mode_count; ++j) {
        if (!bounded(state.q_m[j], -1.0, 1.0) ||
            !bounded(state.v_m_per_s[j], -1000.0, 1000.0)) {
            return false;
        }
    }
    return true;
}
[[nodiscard]] bool valid_settings(const Settings& s) noexcept {
    const auto positive = [](double x) { return bounded(x, 1e-18, 1.0); };
    return bounded(s.maximum_step_s, 1e-14, 1.0) &&
        bounded(s.minimum_step_s, 1e-14, s.maximum_step_s) &&
        positive(s.relative_tolerance) && positive(s.position_absolute_tolerance_m) &&
        positive(s.velocity_absolute_tolerance_m_per_s) &&
        positive(s.energy_absolute_tolerance_j) && positive(s.impulse_absolute_tolerance_ns) &&
        bounded(s.reference_mass_kg, minimum_mass, 1.0) &&
        bounded(s.event_time_tolerance_s, 1e-14, 1e-4) &&
        bounded(s.event_gap_tolerance_m, 1e-16, 1e-12) &&
        s.maximum_trials > 0 && s.maximum_trials <= 100000 &&
        s.maximum_rhs_evaluations > 0 && s.maximum_rhs_evaluations <= 1200000 &&
        s.maximum_consecutive_rejections > 0 && s.maximum_consecutive_rejections <= 64 &&
        s.maximum_root_iterations > 0 && s.maximum_root_iterations <= 64 &&
        s.maximum_subdivision_depth > 0 && s.maximum_subdivision_depth <= 32 &&
        s.maximum_event_rows > 0 && s.maximum_event_rows <= 100000 &&
        s.maximum_interval_rows > 0 && s.maximum_interval_rows <= 100000;
}
[[nodiscard]] ContactSample contact_sample(const ContactLaw& law, double delta,
                                           double rate) noexcept {
    ContactSample out;
    out.gap_m = delta;
    out.gap_rate_m_per_s = rate;
    if (!law.enabled) {
        return out;
    }
    out.raw_factor_n_per_m = law.k_n_per_m + law.c_ns_per_m2 * rate;
    if (delta <= 0.0) {
        out.branch = delta == 0.0 && rate == 0.0 ? Branch::grazing : Branch::separated;
        return out;
    }
    out.compression_m = delta;
    out.potential_j = 0.5 * law.k_n_per_m * delta * delta;
    if (out.raw_factor_n_per_m > 0.0) {
        out.branch = Branch::active;
        out.force_n = delta * out.raw_factor_n_per_m;
        out.active_loss_power_w = law.c_ns_per_m2 * delta * rate * rate;
    } else {
        out.branch = Branch::clipped;
        out.clipped_loss_power_w = -law.k_n_per_m * delta * rate;
    }
    return out;
}
[[nodiscard]] bool finite_contact(const ContactSample& s) noexcept {
    return finite_scalar(s.gap_m) && finite_scalar(s.gap_rate_m_per_s) &&
        finite_scalar(s.raw_factor_n_per_m) && bounded(s.force_n, 0.0, scalar_limit) &&
        bounded(s.potential_j, 0.0, scalar_limit) &&
        bounded(s.active_loss_power_w, 0.0, scalar_limit) &&
        bounded(s.clipped_loss_power_w, 0.0, scalar_limit);
}
[[nodiscard]] bool observe_unchecked(const Configuration& cfg, const State& state,
                                      Observation& out) noexcept {
    out = {};
    out.face = face_unchecked(cfg.damper_motion, state.time_s);
    if (!valid_face(out.face)) {
        return false;
    }
    double hammer_gap = state.hammer_x_m - cfg.hammer_clearance_m;
    double hammer_rate = state.hammer_v_m_per_s;
    double damper_gap = -out.face.position_m;
    double damper_rate = -out.face.velocity_m_per_s;
    for (std::size_t j = 0; j < mode_count; ++j) {
        hammer_gap -= cfg.hammer_projection[j] * state.q_m[j];
        hammer_rate -= cfg.hammer_projection[j] * state.v_m_per_s[j];
        damper_gap += cfg.damper_projection[j] * state.q_m[j];
        damper_rate += cfg.damper_projection[j] * state.v_m_per_s[j];
    }
    out.hammer = contact_sample(cfg.hammer, hammer_gap, hammer_rate);
    out.damper = contact_sample(cfg.damper, damper_gap, damper_rate);
    if (!finite_contact(out.hammer) || !finite_contact(out.damper)) {
        return false;
    }
    out.hammer_acceleration_m_per_s2 = -out.hammer.force_n / cfg.hammer_mass_kg;
    out.mechanical_energy_j = 0.5 * cfg.hammer_mass_kg *
        state.hammer_v_m_per_s * state.hammer_v_m_per_s;
    double derivative = cfg.hammer_mass_kg * state.hammer_v_m_per_s *
        out.hammer_acceleration_m_per_s2;
    for (std::size_t j = 0; j < mode_count; ++j) {
        const auto& m = cfg.modes[j];
        const double omega = 2.0 * std::numbers::pi * m.frequency_hz;
        const double stiffness = m.mass_kg * omega * omega;
        const double damping = 2.0 * m.mass_kg * m.decay_per_s;
        const double q = state.q_m[j];
        const double v = state.v_m_per_s[j];
        const double a = (-stiffness * q - damping * v +
            cfg.hammer_projection[j] * out.hammer.force_n -
            cfg.damper_projection[j] * out.damper.force_n) / m.mass_kg;
        out.modal_acceleration_m_per_s2[j] = a;
        out.mechanical_energy_j += 0.5 * (m.mass_kg * v * v + stiffness * q * q);
        out.background_loss_power_w += damping * v * v;
        derivative += m.mass_kg * v * a + stiffness * q * v;
    }
    if (cfg.hammer.enabled && hammer_gap > 0.0) {
        derivative += cfg.hammer.k_n_per_m * hammer_gap * hammer_rate;
    }
    if (cfg.damper.enabled && damper_gap > 0.0) {
        derivative += cfg.damper.k_n_per_m * damper_gap * damper_rate;
    }
    out.total_energy_j = out.mechanical_energy_j + out.hammer.potential_j + out.damper.potential_j;
    out.damper_work_power_w = -out.damper.force_n * out.face.velocity_m_per_s;
    out.power_residual_w = derivative + out.background_loss_power_w +
        out.hammer.active_loss_power_w + out.hammer.clipped_loss_power_w +
        out.damper.active_loss_power_w + out.damper.clipped_loss_power_w -
        out.damper_work_power_w;
    return finite_scalar(out.hammer_acceleration_m_per_s2) &&
        finite_scalar(out.modal_acceleration_m_per_s2[0]) &&
        finite_scalar(out.modal_acceleration_m_per_s2[1]) &&
        bounded(out.total_energy_j, 0.0, scalar_limit) &&
        bounded(out.background_loss_power_w, 0.0, scalar_limit) &&
        finite_scalar(out.damper_work_power_w) && finite_scalar(out.power_residual_w);
}
[[nodiscard]] State vector_state(const Vector& y, double t) noexcept {
    return {t, {y[0], y[1]}, {y[2], y[3]}, y[4], y[5]};
}
[[nodiscard]] Vector state_vector(const State& s) noexcept {
    return {s.q_m[0], s.q_m[1], s.v_m_per_s[0], s.v_m_per_s[1],
            s.hammer_x_m, s.hammer_v_m_per_s, 0.0, 0.0, 0.0, 0.0,
            0.0, 0.0, 0.0, 0.0};
}
[[nodiscard]] bool opposite(double a, double b) noexcept {
    return (a < 0.0 && b > 0.0) || (a > 0.0 && b < 0.0);
}
[[nodiscard]] double progress_floor(double t, const Settings& settings) noexcept {
    const double ulp = std::nextafter(t, std::numeric_limits<double>::infinity()) - t;
    return std::max(settings.minimum_step_s, 4.0 * ulp);
}
[[nodiscard]] Branch forward_branch(const ContactSample& sample) noexcept {
    if (sample.branch == Branch::disabled) {
        return Branch::disabled;
    }
    if (sample.gap_m == 0.0 && sample.gap_rate_m_per_s > 0.0) {
        return sample.raw_factor_n_per_m > 0.0 ? Branch::active : Branch::clipped;
    }
    return sample.branch;
}
void sampled_peaks(Diagnostics& diag, const Observation& obs, double t) noexcept {
    if (obs.hammer.compression_m > diag.sampled_maximum_hammer_compression_m) {
        diag.sampled_maximum_hammer_compression_m = obs.hammer.compression_m;
        diag.sampled_maximum_hammer_compression_time_s = t;
    }
    if (obs.hammer.force_n > diag.sampled_maximum_hammer_force_n) {
        diag.sampled_maximum_hammer_force_n = obs.hammer.force_n;
        diag.sampled_maximum_hammer_force_time_s = t;
    }
    if (obs.damper.compression_m > diag.sampled_maximum_damper_compression_m) {
        diag.sampled_maximum_damper_compression_m = obs.damper.compression_m;
        diag.sampled_maximum_damper_compression_time_s = t;
    }
    if (obs.damper.force_n > diag.sampled_maximum_damper_force_n) {
        diag.sampled_maximum_damper_force_n = obs.damper.force_n;
        diag.sampled_maximum_damper_force_time_s = t;
    }
}
void add_work(WorkCounters& to, const WorkCounters& from) noexcept {
    to.trials += from.trials;
    to.rhs_evaluations += from.rhs_evaluations;
    to.accepted_steps += from.accepted_steps;
    to.analytic_free_evaluations += from.analytic_free_evaluations;
    to.rejected_steps += from.rejected_steps;
    to.root_iterations += from.root_iterations;
    to.extrema_checks += from.extrema_checks;
    to.event_subdivisions += from.event_subdivisions;
}
} // namespace

Result sample_face(const Trajectory& tr, double t, FaceSample& output) noexcept {
    if (!valid_trajectory(tr)) {
        return fail(Status::invalid_input, Failure::unsupported_trajectory, t);
    }
    if (!bounded(t, 0.0, maximum_time)) {
        return fail(Status::invalid_input, Failure::invalid_time, t);
    }
    const auto candidate = face_unchecked(tr, t);
    if (!valid_face(candidate)) {
        return fail(Status::invalid_input, Failure::invalid_state, t);
    }
    output = candidate;
    return {};
}

Result observe(const Configuration& cfg, const State& state, Observation& output) noexcept {
    if (!valid_configuration(cfg)) {
        return fail(Status::invalid_input, Failure::invalid_configuration, state.time_s);
    }
    if (!valid_state(state)) {
        return fail(Status::invalid_input, Failure::invalid_state, state.time_s);
    }
    Observation candidate;
    if (!observe_unchecked(cfg, state, candidate)) {
        return fail(Status::numerical_failure, Failure::arithmetic, state.time_s);
    }
    output = candidate;
    return {};
}

Result fixed_damper_equilibrium(const Configuration& cfg, const State& seed,
                                Equilibrium& output) noexcept {
    if (!valid_configuration(cfg) || !cfg.damper.enabled ||
        cfg.damper_motion.kind != TrajectoryKind::constant) {
        return fail(Status::invalid_input, Failure::invalid_configuration, seed.time_s);
    }
    if (!valid_state(seed) || seed.hammer_v_m_per_s != 0.0) {
        return fail(Status::invalid_input, Failure::invalid_state, seed.time_s);
    }
    std::array<double, mode_count> stiffness{};
    double compliance = 0.0;
    for (std::size_t j = 0; j < mode_count; ++j) {
        const double omega = 2.0 * std::numbers::pi * cfg.modes[j].frequency_hz;
        stiffness[j] = cfg.modes[j].mass_kg * omega * omega;
        if (stiffness[j] == 0.0) {
            return fail(Status::invalid_input, Failure::singular_equilibrium, seed.time_s);
        }
        compliance += cfg.damper_projection[j] * cfg.damper_projection[j] / stiffness[j];
    }
    Equilibrium candidate;
    candidate.state = seed;
    candidate.state.q_m = {};
    candidate.state.v_m_per_s = {};
    const double free_gap = -cfg.damper_motion.offset_m;
    candidate.compression_m = std::max(0.0, free_gap) /
        (1.0 + cfg.damper.k_n_per_m * compliance);
    candidate.force_n = cfg.damper.k_n_per_m * candidate.compression_m;
    for (std::size_t j = 0; j < mode_count; ++j) {
        candidate.state.q_m[j] = -cfg.damper_projection[j] * candidate.force_n / stiffness[j];
        candidate.static_force_residual_n[j] =
            stiffness[j] * candidate.state.q_m[j] + cfg.damper_projection[j] * candidate.force_n;
    }
    Observation observation;
    const auto result = observe(cfg, candidate.state, observation);
    if (!result) {
        return result;
    }
    if (cfg.hammer.enabled && observation.hammer.gap_m > 0.0) {
        return fail(Status::invalid_input, Failure::invalid_state, seed.time_s);
    }
    output = candidate;
    return {};
}

class ReferenceSolver {
public:
    explicit ReferenceSolver(Reference& reference) : ref_(reference) {}
    [[nodiscard]] Result advance(double dt);
    [[nodiscard]] Result action_relaunch(double velocity);
    [[nodiscard]] Result action_motion(const Trajectory& trajectory);
    [[nodiscard]] Result initialization_events();
private:
    struct Point { double t = 0.0; Vector y{}; Observation observation{}; };
    struct Trial { Vector midpoint{}; Vector fine{}; double error = 0.0; };
    enum class RootFunction { gap, rate, clamp, clamp_rate };
    struct Root { Point left{}; Point right{}; Point middle{}; double uncertainty = 0.0; };

    Reference& ref_;
    WorkCounters work_{};
    Status status_ = Status::success;
    Failure failure_ = Failure::none;
    double attempted_until_ = 0.0;
    double trial_power_residual_ = 0.0;

    bool stop(Status status, Failure reason) noexcept;
    [[nodiscard]] Result result() const noexcept;
    [[nodiscard]] bool rhs(double t, const Vector& y, Vector& derivative,
                           Observation* observation = nullptr) noexcept;
    [[nodiscard]] bool rk4(double t, const Vector& y, double h, Vector& output) noexcept;
    [[nodiscard]] bool trial(double t, const Vector& y, double h, Trial& output) noexcept;
    [[nodiscard]] double error_norm(const Vector& y, const Vector& coarse,
                                    const Vector& fine) const noexcept;
    [[nodiscard]] bool point(double t, const Vector& y, Point& output) noexcept;
    [[nodiscard]] bool replay(const Point& start, double t, Point& output) noexcept;
    [[nodiscard]] const ContactSample& sample(const Point&, ContactId) const noexcept;
    [[nodiscard]] double root_value(const Point&, ContactId, RootFunction) const noexcept;
    [[nodiscard]] bool refine(const Point& start, Point left, Point right,
                               ContactId, RootFunction, Root& output) noexcept;
    [[nodiscard]] bool scan_segment(const Point& start, const Point& left, const Point& right,
                                     ContactId, RootFunction, std::uint64_t depth,
                                     std::vector<Event>& found);
    [[nodiscard]] bool scan(const Point& start, const Trial&, double h,
                            std::array<Point, 5>& points, std::vector<Event>& found);
    [[nodiscard]] bool emit(std::vector<Event>& events, const Event& event);
    [[nodiscard]] bool append_event(const Event& event);
    [[nodiscard]] bool commit_interval(const Point&, const Trial&, double h,
                                        const std::array<Point, 5>& points,
                                        std::vector<Event>& found);
    [[nodiscard]] bool add(double& accumulator, double increment, std::size_t index) noexcept;
    [[nodiscard]] bool action_reclassify(const Observation& before, const Observation& after);
    [[nodiscard]] bool before_action();
    [[nodiscard]] bool retain_peak(PeakCandidate&, double value, double t,
                                    double begin, double end, PeakOrigin) noexcept;
    [[nodiscard]] bool peak_samples(const Observation&, double t, PeakOrigin) noexcept;
    [[nodiscard]] bool replay_peak(const Point& start, double t, Point&) noexcept;
    [[nodiscard]] bool peak_derivative(const Point&, ContactId, bool force,
                                        double& value, bool& eligible) noexcept;
    [[nodiscard]] bool peak_root(const Root&, ContactId, bool force) noexcept;
    [[nodiscard]] bool peak_segment(const Point& start, Point left, Point right,
                                    ContactId, bool force) noexcept;
    [[nodiscard]] bool collect_interval_peaks(const Point& start, double h) noexcept;
    void configure_analytic_free_motion() noexcept;
    [[nodiscard]] bool analytic_free_step(double end, const Vector& y, Vector& output) noexcept;
};

bool ReferenceSolver::stop(Status status, Failure reason) noexcept {
    if (status_ == Status::success) {
        status_ = status;
        failure_ = reason;
    }
    return false;
}
Result ReferenceSolver::result() const noexcept {
    return {status_, failure_, work_, attempted_until_};
}

void ReferenceSolver::configure_analytic_free_motion() noexcept {
    ref_.analytic_free_motion_ = false;
    ref_.analytic_grazing_mode_ = -1;
    const auto& cfg = ref_.config_;
    const auto& state = ref_.state_;
    // This certificate is deliberately narrow: no contact force has to be
    // approximated or suppressed, and no physical loss integral is inferred
    // from an energy difference. General near-tangencies still fail explicitly.
    if (cfg.damper.enabled || !cfg.hammer.enabled || state.hammer_v_m_per_s != 0.0) {
        return;
    }
    long double constant = static_cast<long double>(state.hammer_x_m) -
                           static_cast<long double>(cfg.hammer_clearance_m);
    long double amplitude = 0.0L;
    int oscillating_mode = -1;
    for (std::size_t j = 0; j < mode_count; ++j) {
        const auto& mode = cfg.modes[j];
        if (mode.decay_per_s != 0.0) { return; }
        const long double b = cfg.hammer_projection[j];
        if (b == 0.0L) { continue; }
        if (mode.frequency_hz == 0.0) {
            if (state.v_m_per_s[j] != 0.0) { return; }
            constant -= b * static_cast<long double>(state.q_m[j]);
            continue;
        }
        const long double omega = 2.0 * std::numbers::pi * mode.frequency_hz;
        const long double radius = std::hypot(static_cast<long double>(state.q_m[j]),
            static_cast<long double>(state.v_m_per_s[j]) / omega);
        if (radius == 0.0L) { continue; }
        if (oscillating_mode != -1) { return; }
        oscillating_mode = static_cast<int>(j);
        amplitude = std::abs(b) * radius;
    }
    // A strictly positive envelope is never rounded down to certify a miss.
    // The tangent label is reserved for an envelope nonpositive in extended
    // precision whose radius and clearance coincide at the public precision.
    if (constant + amplitude > 0.0L) { return; }
    ref_.analytic_anchor_ = state;
    ref_.analytic_free_motion_ = true;
    if (oscillating_mode >= 0 &&
        static_cast<double>(amplitude) == static_cast<double>(-constant)) {
        ref_.analytic_grazing_mode_ = oscillating_mode;
    }
}

bool ReferenceSolver::analytic_free_step(double end, const Vector& y, Vector& output) noexcept {
    ++work_.analytic_free_evaluations;
    output = y;
    const auto& anchor = ref_.analytic_anchor_;
    const long double elapsed = static_cast<long double>(end) - anchor.time_s;
    for (std::size_t j = 0; j < mode_count; ++j) {
        const auto& mode = ref_.config_.modes[j];
        if (mode.frequency_hz == 0.0) {
            output[j] = static_cast<double>(static_cast<long double>(anchor.q_m[j]) +
                static_cast<long double>(anchor.v_m_per_s[j]) * elapsed);
            output[j + 2] = anchor.v_m_per_s[j];
        } else {
            const long double omega = 2.0 * std::numbers::pi * mode.frequency_hz;
            const long double q = anchor.q_m[j];
            const long double sine_coefficient = static_cast<long double>(anchor.v_m_per_s[j]) / omega;
            const long double radius = std::hypot(q, sine_coefficient);
            const long double phase = std::atan2(-sine_coefficient, q);
            const long double angle = omega * elapsed + phase;
            output[j] = static_cast<double>(radius * std::cos(angle));
            output[j + 2] = static_cast<double>(-omega * radius * std::sin(angle));
        }
    }
    output[4] = anchor.hammer_x_m;
    output[5] = 0.0;
    Vector ignored{};
    Observation observation;
    if (!rhs(end, output, ignored, &observation)) { return false; }
    if (observation.hammer.force_n != 0.0 || observation.damper.force_n != 0.0 ||
        observation.background_loss_power_w != 0.0) {
        return stop(Status::numerical_failure, Failure::unresolved_grazing);
    }
    return true;
}

bool ReferenceSolver::rhs(double t, const Vector& y, Vector& dy, Observation* output) noexcept {
    if (work_.rhs_evaluations >= ref_.settings_.maximum_rhs_evaluations) {
        return stop(Status::budget_exhausted, Failure::evaluation_limit);
    }
    ++work_.rhs_evaluations;
    attempted_until_ = std::max(attempted_until_, t);
    const State state = vector_state(y, t);
    if (!valid_state(state)) {
        return stop(Status::numerical_failure, Failure::state_domain);
    }
    for (const double value : y) {
        if (!finite_scalar(value)) {
            return stop(Status::numerical_failure, Failure::arithmetic);
        }
    }
    Observation obs;
    if (!observe_unchecked(ref_.config_, state, obs)) {
        return stop(Status::numerical_failure, Failure::arithmetic);
    }
    dy = {y[2], y[3], obs.modal_acceleration_m_per_s2[0],
          obs.modal_acceleration_m_per_s2[1], y[5], obs.hammer_acceleration_m_per_s2,
          obs.hammer.active_loss_power_w, obs.hammer.clipped_loss_power_w,
          obs.damper.active_loss_power_w, obs.damper.clipped_loss_power_w,
          obs.background_loss_power_w, obs.damper_work_power_w,
          obs.hammer.force_n, obs.damper.force_n};
    trial_power_residual_ = std::max(trial_power_residual_, std::abs(obs.power_residual_w));
    if (output != nullptr) {
        *output = obs;
    }
    return true;
}

bool ReferenceSolver::rk4(double t, const Vector& y, double h, Vector& output) noexcept {
    const double mid = t + 0.5 * h;
    const double end = t + h;
    if (!(mid > t && end > mid) || !std::isfinite(end)) {
        return stop(Status::numerical_failure, Failure::minimum_progress);
    }
    if (ref_.analytic_free_motion_) {
        return analytic_free_step(end, y, output);
    }
    Vector k1{}, k2{}, k3{}, k4{}, stage{};
    if (!rhs(t, y, k1)) {
        return false;
    }
    for (std::size_t i = 0; i < dimension; ++i) {
        stage[i] = y[i] + 0.5 * h * k1[i];
    }
    if (!rhs(mid, stage, k2)) {
        return false;
    }
    for (std::size_t i = 0; i < dimension; ++i) {
        stage[i] = y[i] + 0.5 * h * k2[i];
    }
    if (!rhs(mid, stage, k3)) {
        return false;
    }
    for (std::size_t i = 0; i < dimension; ++i) {
        stage[i] = y[i] + h * k3[i];
    }
    if (!rhs(end, stage, k4)) {
        return false;
    }
    for (std::size_t i = 0; i < dimension; ++i) {
        output[i] = y[i] + (h / 6.0) * (k1[i] + 2.0 * k2[i] + 2.0 * k3[i] + k4[i]);
        if (!finite_scalar(output[i])) {
            return stop(Status::numerical_failure, Failure::arithmetic);
        }
    }
    if (!valid_state(vector_state(output, end))) {
        return stop(Status::numerical_failure, Failure::state_domain);
    }
    return true;
}

double ReferenceSolver::error_norm(const Vector& y, const Vector& coarse,
                                   const Vector& fine) const noexcept {
    double largest = 0.0;
    for (std::size_t i = 0; i < dimension; ++i) {
        double weight = 1.0;
        double absolute = ref_.settings_.energy_absolute_tolerance_j;
        if (i < 6) {
            const double mass = i < 4 ? ref_.config_.modes[i % 2].mass_kg : ref_.config_.hammer_mass_kg;
            weight = std::sqrt(mass / ref_.settings_.reference_mass_kg);
            absolute = (i < 2 || i == 4) ? ref_.settings_.position_absolute_tolerance_m :
                ref_.settings_.velocity_absolute_tolerance_m_per_s;
        } else if (i >= 12) {
            absolute = ref_.settings_.impulse_absolute_tolerance_ns;
        }
        const double scale = absolute + ref_.settings_.relative_tolerance *
            std::max(std::abs(weight * y[i]), std::abs(weight * fine[i]));
        largest = std::max(largest, std::abs(weight * (fine[i] - coarse[i])) / (15.0 * scale));
    }
    // Event localization cannot repair accumulated mechanical phase error. Keep
    // each local contact-gap defect below a small fraction of the independent
    // gap-resolution budget, even when the caller requests a looser waveform
    // tolerance. This is an additional integration accuracy control, not a
    // widened root bracket or a projection of the accepted mechanics.
    for (const auto id : {ContactId::hammer, ContactId::damper}) {
        const auto& law = id == ContactId::hammer ? ref_.config_.hammer : ref_.config_.damper;
        if (!law.enabled) { continue; }
        double defect = id == ContactId::hammer ? fine[4] - coarse[4] : 0.0;
        for (std::size_t j = 0; j < mode_count; ++j) {
            const double projection = id == ContactId::hammer ?
                -ref_.config_.hammer_projection[j] : ref_.config_.damper_projection[j];
            defect += projection * (fine[j] - coarse[j]);
        }
        const double scale = 0.002 * ref_.settings_.event_gap_tolerance_m;
        largest = std::max(largest, std::abs(defect) / (15.0 * scale));
    }
    return largest;
}

bool ReferenceSolver::trial(double t, const Vector& y, double h, Trial& output) noexcept {
    if (work_.trials >= ref_.settings_.maximum_trials) {
        return stop(Status::budget_exhausted, Failure::trial_limit);
    }
    ++work_.trials;
    Vector coarse{};
    if (!rk4(t, y, h, coarse) || !rk4(t, y, 0.5 * h, output.midpoint) ||
        !rk4(t + 0.5 * h, output.midpoint, 0.5 * h, output.fine)) {
        return false;
    }
    output.error = error_norm(y, coarse, output.fine);
    return std::isfinite(output.error) || stop(Status::numerical_failure, Failure::arithmetic);
}

bool ReferenceSolver::point(double t, const Vector& y, Point& output) noexcept {
    Vector ignored{};
    output.t = t;
    output.y = y;
    return rhs(t, y, ignored, &output.observation);
}

bool ReferenceSolver::replay(const Point& start, double t, Point& output) noexcept {
    if (t == start.t) {
        output = start;
        return true;
    }
    const double h = t - start.t;
    if (h < progress_floor(start.t, ref_.settings_)) {
        return stop(Status::numerical_failure, Failure::minimum_progress);
    }
    Trial candidate;
    if (!trial(start.t, start.y, h, candidate)) {
        return false;
    }
    if (candidate.error > 1.0) {
        return stop(Status::numerical_failure, Failure::arithmetic);
    }
    return point(t, candidate.fine, output);
}

const ContactSample& ReferenceSolver::sample(const Point& point_value, ContactId id) const noexcept {
    return id == ContactId::hammer ? point_value.observation.hammer : point_value.observation.damper;
}

double ReferenceSolver::root_value(const Point& p, ContactId id, RootFunction function) const noexcept {
    const auto& s = sample(p, id);
    if (function == RootFunction::gap) {
        return s.gap_m;
    }
    if (function == RootFunction::rate) {
        return s.gap_rate_m_per_s;
    }
    if (function == RootFunction::clamp) {
        return s.raw_factor_n_per_m;
    }
    double acceleration = id == ContactId::hammer ? p.observation.hammer_acceleration_m_per_s2 :
        -p.observation.face.acceleration_m_per_s2;
    for (std::size_t j = 0; j < mode_count; ++j) {
        acceleration += (id == ContactId::hammer ? -ref_.config_.hammer_projection[j] :
            ref_.config_.damper_projection[j]) * p.observation.modal_acceleration_m_per_s2[j];
    }
    const double damping = id == ContactId::hammer ? ref_.config_.hammer.c_ns_per_m2 :
        ref_.config_.damper.c_ns_per_m2;
    return damping * acceleration;
}

bool ReferenceSolver::refine(const Point& start, Point left, Point right, ContactId id,
                             RootFunction function, Root& output) noexcept {
    double a = root_value(left, id, function);
    double b = root_value(right, id, function);
    for (std::uint64_t iteration = 0; iteration < ref_.settings_.maximum_root_iterations; ++iteration) {
        ++work_.root_iterations;
        const double width = right.t - left.t;
        const double gap_uncertainty = function == RootFunction::gap ?
            std::max(std::abs(sample(left, id).gap_m), std::abs(sample(right, id).gap_m)) :
            std::abs(sample(right, id).gap_m - sample(left, id).gap_m);
        const bool gap_ready = function != RootFunction::gap ||
            gap_uncertainty <= ref_.settings_.event_gap_tolerance_m;
        if (width <= ref_.settings_.event_time_tolerance_s && gap_ready) {
            output.left = left;
            output.right = right;
            output.uncertainty = gap_uncertainty;
            const double middle = left.t + 0.5 * width;
            if (middle == left.t || middle == right.t) {
                output.middle = std::abs(a) <= std::abs(b) ? left : right;
                return true;
            }
            return replay(start, middle, output.middle);
        }
        const double middle = left.t + 0.5 * width;
        if (!(middle > left.t && middle < right.t)) {
            return stop(Status::numerical_failure, Failure::minimum_progress);
        }
        Point mid;
        if (!replay(start, middle, mid)) {
            return false;
        }
        const double value = root_value(mid, id, function);
        if (value == 0.0) {
            output = {mid, mid, mid, 0.0};
            return true;
        }
        if (a == 0.0) {
            right = left;
            b = a;
        } else if (b == 0.0 || opposite(a, value)) {
            right = mid;
            b = value;
        } else {
            left = mid;
            a = value;
        }
    }
    return stop(Status::budget_exhausted, Failure::root_limit);
}

bool ReferenceSolver::emit(std::vector<Event>& events, const Event& event) {
    if (event.contact != ContactId::action) {
        const auto duplicate = [this, &event](const std::vector<Event>& rows) {
            for (auto it = rows.rbegin(); it != rows.rend(); ++it) {
                if (it->contact == event.contact) {
                    return it->kind == event.kind &&
                        ((it->bracket_begin_s <= event.bracket_end_s &&
                          event.bracket_begin_s <= it->bracket_end_s) ||
                         (it->action_time_tie && event.time_s >= it->time_s &&
                          event.time_s - it->time_s <= ref_.settings_.event_time_tolerance_s));
                }
            }
            return false;
        };
        if (duplicate(events) || duplicate(ref_.events_)) {
            return true;
        }
    }
    if (events.size() + ref_.events_.size() >= ref_.settings_.maximum_event_rows) {
        return stop(Status::budget_exhausted, Failure::event_limit);
    }
    events.push_back(event);
    return true;
}

bool ReferenceSolver::scan_segment(const Point& start, const Point& left, const Point& right,
                                   ContactId id, RootFunction function, std::uint64_t depth,
                                   std::vector<Event>& found) {
    if (depth > ref_.settings_.maximum_subdivision_depth) {
        return stop(Status::budget_exhausted, Failure::subdivision_limit);
    }
    const auto& law = id == ContactId::hammer ? ref_.config_.hammer : ref_.config_.damper;
    if (!law.enabled || (function == RootFunction::clamp && law.c_ns_per_m2 == 0.0)) {
        return true;
    }
    const double a = root_value(left, id, function);
    const double b = root_value(right, id, function);
    if (function == RootFunction::gap && id == ContactId::hammer &&
        ref_.analytic_free_motion_) {
        if (ref_.analytic_grazing_mode_ < 0) { return true; }
        const double da = sample(left, id).gap_rate_m_per_s;
        const double db = sample(right, id).gap_rate_m_per_s;
        // Only the gap maximum is the tangent. The other turning point of the
        // oscillator is separated by twice the amplitude.
        if (!(da >= 0.0 && db <= 0.0 && (da != 0.0 || db != 0.0))) { return true; }
        ++work_.extrema_checks;
        Root extremum;
        if (da == 0.0) { extremum = {left, left, left, 0.0}; }
        else if (db == 0.0) { extremum = {right, right, right, 0.0}; }
        else if (!refine(start, left, right, id, RootFunction::rate, extremum)) { return false; }
        const double uncertainty = std::max({std::abs(sample(extremum.left, id).gap_m),
            std::abs(sample(extremum.right, id).gap_m),
            std::abs(sample(extremum.middle, id).gap_m)});
        if (uncertainty > ref_.settings_.event_gap_tolerance_m) {
            return stop(Status::numerical_failure, Failure::unresolved_grazing);
        }
        Event event;
        event.contact = id;
        event.kind = EventKind::grazing;
        event.time_s = extremum.middle.t;
        event.bracket_begin_s = extremum.left.t;
        event.bracket_end_s = extremum.right.t;
        event.bracket_gap_uncertainty_m = uncertainty;
        event.gap_m = sample(extremum.middle, id).gap_m;
        event.relative_velocity_m_per_s = sample(extremum.middle, id).gap_rate_m_per_s;
        event.before = Branch::separated;
        event.after = Branch::separated;
        return emit(found, event);
    }
    if (opposite(a, b) || (a == 0.0 && b != 0.0) || (b == 0.0 && a != 0.0)) {
        Root root;
        if (a == 0.0) {
            root = {left, left, left, 0.0};
        } else if (b == 0.0) {
            root = {right, right, right, 0.0};
        } else if (!refine(start, left, right, id, function, root)) {
            return false;
        }
        const auto& s = sample(root.middle, id);
        if (function == RootFunction::clamp && s.gap_m <= 0.0) {
            return true;
        }
        const bool increasing = b > a;
        Event event;
        event.contact = id;
        event.time_s = root.middle.t;
        event.bracket_begin_s = root.left.t;
        event.bracket_end_s = root.right.t;
        event.bracket_gap_uncertainty_m = root.uncertainty;
        event.gap_m = s.gap_m;
        event.relative_velocity_m_per_s = s.gap_rate_m_per_s;
        if (function == RootFunction::gap) {
            event.kind = increasing ? EventKind::onset : EventKind::separation;
            event.before = increasing ? Branch::separated :
                (s.raw_factor_n_per_m > 0.0 ? Branch::active : Branch::clipped);
            event.after = increasing ?
                (s.raw_factor_n_per_m > 0.0 ? Branch::active : Branch::clipped) : Branch::separated;
        } else {
            event.kind = increasing ? EventKind::clamp_exit : EventKind::clamp_enter;
            event.before = increasing ? Branch::clipped : Branch::active;
            event.after = increasing ? Branch::active : Branch::clipped;
        }
        return emit(found, event);
    }

    const RootFunction derivative = function == RootFunction::gap ? RootFunction::rate : RootFunction::clamp_rate;
    const double da = root_value(left, id, derivative);
    const double db = root_value(right, id, derivative);
    if (opposite(da, db)) {
        ++work_.extrema_checks;
        Root extremum;
        if (!refine(start, left, right, id, derivative, extremum)) {
            return false;
        }
        const double value = root_value(extremum.middle, id, function);
        if (function == RootFunction::gap && std::abs(value) <= ref_.settings_.event_gap_tolerance_m) {
            // The trajectory error is not a sign certificate. An unresolved
            // tangent is a typed failure, never a silently missed short impact.
            return stop(Status::numerical_failure, Failure::unresolved_grazing);
        }
        if (opposite(a, value) || opposite(value, b)) {
            ++work_.event_subdivisions;
            return scan_segment(start, left, extremum.middle, id, function, depth + 1, found) &&
                   scan_segment(start, extremum.middle, right, id, function, depth + 1, found);
        }
    }
    return true;
}

bool ReferenceSolver::scan(const Point& start, const Trial& candidate, double h,
                           std::array<Point, 5>& points, std::vector<Event>& found) {
    points[0] = start;
    Vector quarter{}, three_quarter{};
    if (!rk4(start.t, start.y, 0.25 * h, quarter) ||
        !point(start.t + 0.25 * h, quarter, points[1]) ||
        !point(start.t + 0.5 * h, candidate.midpoint, points[2]) ||
        !rk4(start.t + 0.5 * h, candidate.midpoint, 0.25 * h, three_quarter) ||
        !point(start.t + 0.75 * h, three_quarter, points[3]) ||
        !point(start.t + h, candidate.fine, points[4])) {
        return false;
    }
    for (const auto id : {ContactId::hammer, ContactId::damper}) {
        for (std::size_t i = 0; i + 1 < points.size(); ++i) {
            if (!scan_segment(start, points[i], points[i + 1], id, RootFunction::gap, 0, found) ||
                !scan_segment(start, points[i], points[i + 1], id, RootFunction::clamp, 0, found)) {
                return false;
            }
        }
    }
    return true;
}

bool ReferenceSolver::retain_peak(PeakCandidate& peak, double value, double t,
                                  double begin, double end, PeakOrigin origin) noexcept {
    if (!bounded(value, 0.0, scalar_limit) || !bounded(begin, 0.0, maximum_time) ||
        !bounded(end, begin, maximum_time) || !bounded(t, begin, end)) {
        return stop(Status::numerical_failure, Failure::arithmetic);
    }
    // Preserve ties except for adding a more specific source to the same sample.
    // This lets an exact sampled root or an action side retain its provenance.
    const bool annotate_sample = value == peak.value && t == peak.time_s &&
        peak.origin == PeakOrigin::sample && origin != PeakOrigin::sample;
    if (peak.origin == PeakOrigin::disabled || value > peak.value || annotate_sample) {
        peak = {value, t, begin, end, origin};
    }
    return true;
}

bool ReferenceSolver::peak_samples(const Observation& observation, double t,
                                   PeakOrigin origin) noexcept {
    if (!ref_.settings_.locate_peak_candidates) {
        return true;
    }
    auto& diagnostics = ref_.diagnostics_;
    if (ref_.config_.hammer.enabled &&
        (!retain_peak(diagnostics.located_hammer_compression,
                      observation.hammer.compression_m, t, t, t, origin) ||
         !retain_peak(diagnostics.located_hammer_force,
                      observation.hammer.force_n, t, t, t, origin))) {
        return false;
    }
    if (ref_.config_.damper.enabled &&
        (!retain_peak(diagnostics.located_damper_compression,
                      observation.damper.compression_m, t, t, t, origin) ||
         !retain_peak(diagnostics.located_damper_force,
                      observation.damper.force_n, t, t, t, origin))) {
        return false;
    }
    return true;
}

bool ReferenceSolver::replay_peak(const Point& start, double t, Point& output) noexcept {
    return replay(start, t, output) &&
        peak_samples(output.observation, output.t, PeakOrigin::sample);
}

bool ReferenceSolver::peak_derivative(const Point& p, ContactId id, bool force,
                                     double& value, bool& eligible) noexcept {
    const auto& s = sample(p, id);
    eligible = s.gap_m > 0.0 && (!force || s.raw_factor_n_per_m > 0.0);
    if (!force) {
        // The signed gap rate can bracket a positive compression maximum even
        // when a segment endpoint is separated. Eligibility is checked at roots.
        value = s.gap_rate_m_per_s;
        return finite_scalar(value) || stop(Status::numerical_failure, Failure::arithmetic);
    }
    value = 0.0;
    if (!eligible) {
        // Separated and clipped force plateaus are sampled zeros, not roots of
        // the positive-force derivative. Never enumerate their zero continuum.
        return true;
    }
    double acceleration = id == ContactId::hammer ? p.observation.hammer_acceleration_m_per_s2 :
        -p.observation.face.acceleration_m_per_s2;
    if (!finite_scalar(acceleration)) {
        return stop(Status::numerical_failure, Failure::arithmetic);
    }
    for (std::size_t j = 0; j < mode_count; ++j) {
        const double projection = id == ContactId::hammer ?
            -ref_.config_.hammer_projection[j] : ref_.config_.damper_projection[j];
        const double term = projection * p.observation.modal_acceleration_m_per_s2[j];
        if (!finite_scalar(term) || !finite_scalar(acceleration + term)) {
            return stop(Status::numerical_failure, Failure::arithmetic);
        }
        acceleration += term;
    }
    const auto& law = id == ContactId::hammer ? ref_.config_.hammer : ref_.config_.damper;
    const double velocity_term = s.gap_rate_m_per_s * s.raw_factor_n_per_m;
    const double compressed_damping = law.c_ns_per_m2 * s.gap_m;
    const double acceleration_term = compressed_damping * acceleration;
    value = velocity_term + acceleration_term;
    return (finite_scalar(velocity_term) && finite_scalar(compressed_damping) &&
            finite_scalar(acceleration_term) && finite_scalar(value)) ||
        stop(Status::numerical_failure, Failure::arithmetic);
}

bool ReferenceSolver::peak_root(const Root& root, ContactId id, bool force) noexcept {
    auto& diagnostics = ref_.diagnostics_;
    auto& peak = id == ContactId::hammer ?
        (force ? diagnostics.located_hammer_force : diagnostics.located_hammer_compression) :
        (force ? diagnostics.located_damper_force : diagnostics.located_damper_compression);
    // Keep the largest value in the final bracket, not an assumed exact value at
    // its midpoint. All three points use the same fixed interval anchor.
    for (const auto* p : {&root.left, &root.middle, &root.right}) {
        const auto& s = sample(*p, id);
        if (s.gap_m > 0.0 && (!force || s.raw_factor_n_per_m > 0.0) &&
            !retain_peak(peak, force ? s.force_n : s.compression_m, p->t,
                         root.left.t, root.right.t, PeakOrigin::stationary_candidate)) {
            return false;
        }
    }
    return true;
}

bool ReferenceSolver::peak_segment(const Point& start, Point left, Point right,
                                   ContactId id, bool force) noexcept {
    double a = 0.0;
    double b = 0.0;
    bool left_eligible = false;
    bool right_eligible = false;
    if (!peak_derivative(left, id, force, a, left_eligible) ||
        !peak_derivative(right, id, force, b, right_eligible)) {
        return false;
    }
    if ((force && (!left_eligible || !right_eligible)) ||
        !(opposite(a, b) || (a == 0.0 && b != 0.0) || (b == 0.0 && a != 0.0))) {
        return true;
    }
    ++work_.extrema_checks;
    if (a == 0.0) {
        return left_eligible ? peak_root({left, left, left, 0.0}, id, force) : true;
    }
    if (b == 0.0) {
        return right_eligible ? peak_root({right, right, right, 0.0}, id, force) : true;
    }
    for (std::uint64_t iteration = 0;
         iteration < ref_.settings_.maximum_root_iterations; ++iteration) {
        ++work_.root_iterations;
        const double width = right.t - left.t;
        const double middle = left.t + 0.5 * width;
        if (!(middle > left.t && middle < right.t)) {
            return stop(Status::numerical_failure, Failure::minimum_progress);
        }
        Point mid;
        if (!replay_peak(start, middle, mid)) {
            return false;
        }
        double value = 0.0;
        bool eligible = false;
        if (!peak_derivative(mid, id, force, value, eligible)) {
            return false;
        }
        if (force && !eligible) {
            // A force-branch boundary interrupted the sampled bracket. Its
            // samples remain candidates, but it does not establish a stationary
            // root on a smooth positive-force branch.
            return true;
        }
        if (value == 0.0) {
            return eligible ? peak_root({mid, mid, mid, 0.0}, id, force) : true;
        }
        if (width <= ref_.settings_.event_time_tolerance_s) {
            return eligible ? peak_root({left, right, mid, 0.0}, id, force) : true;
        }
        if (opposite(a, value)) {
            right = mid;
        } else {
            left = mid;
            a = value;
        }
    }
    return stop(Status::budget_exhausted, Failure::root_limit);
}

bool ReferenceSolver::collect_interval_peaks(const Point& start, double h) noexcept {
    // Deliberately independent of scan's RK compositions and event rows. Both
    // bracket endpoints and every refinement use replay from this one anchor;
    // the existing certified zero-force analytic branch remains available.
    std::array<Point, 5> points{};
    for (std::size_t i = 0; i < points.size(); ++i) {
        if (!replay_peak(start, start.t + 0.25 * static_cast<double>(i) * h, points[i])) {
            return false;
        }
    }
    for (const auto id : {ContactId::hammer, ContactId::damper}) {
        const auto& law = id == ContactId::hammer ? ref_.config_.hammer : ref_.config_.damper;
        if (!law.enabled) {
            continue;
        }
        for (std::size_t i = 0; i + 1 < points.size(); ++i) {
            if (!peak_segment(start, points[i], points[i + 1], id, false) ||
                !peak_segment(start, points[i], points[i + 1], id, true)) {
                return false;
            }
        }
    }
    return true;
}

bool ReferenceSolver::append_event(const Event& event) {
    // Duplicate endpoint detections share a bracket. A later distinct onset is
    // retained even if its gap or time happens to be very small.
    for (auto it = ref_.events_.rbegin();
         event.contact != ContactId::action && it != ref_.events_.rend(); ++it) {
        if (it->contact == event.contact) {
            if (it->kind == event.kind &&
                ((it->bracket_begin_s <= event.bracket_end_s &&
                  event.bracket_begin_s <= it->bracket_end_s) ||
                 (it->action_time_tie && event.time_s >= it->time_s &&
                  event.time_s - it->time_s <= ref_.settings_.event_time_tolerance_s))) {
                return true;
            }
            break;
        }
    }
    if (ref_.events_.size() >= ref_.settings_.maximum_event_rows) {
        return stop(Status::budget_exhausted, Failure::event_limit);
    }
    ref_.events_.push_back(event);
    ref_.diagnostics_.maximum_event_bracket_width_s = std::max(
        ref_.diagnostics_.maximum_event_bracket_width_s, event.bracket_end_s - event.bracket_begin_s);
    ref_.diagnostics_.maximum_event_gap_uncertainty_m = std::max(
        ref_.diagnostics_.maximum_event_gap_uncertainty_m, event.bracket_gap_uncertainty_m);
    if (event.kind == EventKind::grazing) {
        ++ref_.diagnostics_.grazing_candidates;
    }
    return true;
}

bool ReferenceSolver::add(double& accumulator, double increment, std::size_t index) noexcept {
    const double adjusted = increment - ref_.compensation_[index];
    const double next = accumulator + adjusted;
    const double compensation = (next - accumulator) - adjusted;
    if (!finite_scalar(next) || !finite_scalar(compensation)) {
        return stop(Status::numerical_failure, Failure::arithmetic);
    }
    accumulator = next;
    ref_.compensation_[index] = compensation;
    return true;
}

bool ReferenceSolver::commit_interval(const Point& start, const Trial& candidate, double h,
                                      const std::array<Point, 5>& points,
                                      std::vector<Event>& found) {
    auto& ledger = ref_.ledger_;
    const double energy_change = points.back().observation.total_energy_j - ledger.current_energy_j;
    const double loss = candidate.fine[6] + candidate.fine[7] + candidate.fine[8] +
                        candidate.fine[9] + candidate.fine[10];
    const double work = candidate.fine[11];
    const double residual = energy_change + loss - work;
    if (!finite_scalar(residual) || !finite_scalar(loss) || !finite_scalar(work)) {
        return stop(Status::numerical_failure, Failure::arithmetic);
    }
    if (!add(ledger.hammer_active_loss_j, candidate.fine[6], 0) ||
        !add(ledger.hammer_clipped_loss_j, candidate.fine[7], 1) ||
        !add(ledger.damper_active_loss_j, candidate.fine[8], 2) ||
        !add(ledger.damper_clipped_loss_j, candidate.fine[9], 3) ||
        !add(ledger.background_loss_j, candidate.fine[10], 4) ||
        !add(ledger.damper_work_j, candidate.fine[11], 5) ||
        !add(ledger.hammer_impulse_ns, candidate.fine[12], 6) ||
        !add(ledger.damper_impulse_ns, candidate.fine[13], 7) ||
        !add(ledger.cumulative_residual_j, residual, 9) ||
        !add(ledger.sum_absolute_interval_residual_j, std::abs(residual), 10)) {
        return false;
    }
    ledger.current_energy_j = points.back().observation.total_energy_j;
    ledger.maximum_absolute_cumulative_residual_j = std::max(
        ledger.maximum_absolute_cumulative_residual_j, std::abs(ledger.cumulative_residual_j));
    if (ref_.settings_.record_intervals) {
        if (ref_.intervals_.size() >= ref_.settings_.maximum_interval_rows) {
            return stop(Status::budget_exhausted, Failure::history_limit);
        }
        ref_.intervals_.push_back({start.t, start.t + h, h, candidate.error,
                                  energy_change, loss, work, residual});
    }
    std::stable_sort(found.begin(), found.end(), [](const Event& a, const Event& b) {
        if (a.time_s != b.time_s) {
            return a.time_s < b.time_s;
        }
        if (a.contact != b.contact) {
            return a.contact < b.contact;
        }
        return a.kind < b.kind;
    });
    for (const auto& event : found) {
        if (!append_event(event)) {
            return false;
        }
    }
    ref_.state_ = vector_state(candidate.fine, start.t + h);
    ref_.observation_ = points.back().observation;
    auto& diagnostics = ref_.diagnostics_;
    diagnostics.hammer_branch = forward_branch(ref_.observation_.hammer);
    diagnostics.damper_branch = forward_branch(ref_.observation_.damper);
    diagnostics.minimum_accepted_step_s = diagnostics.minimum_accepted_step_s == 0.0 ? h :
        std::min(diagnostics.minimum_accepted_step_s, h);
    diagnostics.maximum_accepted_step_s = std::max(diagnostics.maximum_accepted_step_s, h);
    diagnostics.maximum_scaled_error = std::max(diagnostics.maximum_scaled_error, candidate.error);
    diagnostics.maximum_absolute_power_residual_w = std::max(
        diagnostics.maximum_absolute_power_residual_w, trial_power_residual_);
    for (const auto& p : points) {
        sampled_peaks(diagnostics, p.observation, p.t);
    }
    ++work_.accepted_steps;
    return true;
}

Result ReferenceSolver::advance(double dt) {
    const double target = ref_.state_.time_s + dt;
    attempted_until_ = ref_.state_.time_s;
    if (!bounded(dt, 1e-12, 1.0) || !std::isfinite(target) ||
        !(target > ref_.state_.time_s) || target > maximum_time) {
        stop(Status::invalid_input, Failure::invalid_time);
        return result();
    }
    if (!trajectory_interval(ref_.config_.damper_motion, ref_.state_.time_s, target)) {
        stop(Status::invalid_input, Failure::unsupported_trajectory);
        return result();
    }
    // A prescribed sinusoid can be zero-force at every RK stage while crossing
    // the body in between. Its known frequency must bound event sampling even
    // when the local state error is exactly zero. Modal frequencies receive the
    // same cap; this is a sampling guard, not a proof for all possible tangencies.
    double fastest_angular_frequency = 0.0;
    for (const auto& mode : ref_.config_.modes) {
        fastest_angular_frequency = std::max(fastest_angular_frequency,
            2.0 * std::numbers::pi * mode.frequency_hz);
    }
    if (ref_.config_.damper.enabled &&
        ref_.config_.damper_motion.kind == TrajectoryKind::sinusoidal) {
        fastest_angular_frequency = std::max(fastest_angular_frequency,
            ref_.config_.damper_motion.angular_frequency_rad_per_s);
    }
    const double sampling_ceiling = fastest_angular_frequency > 0.0 ?
        std::min(ref_.settings_.maximum_step_s,
                 std::numbers::pi / (4.0 * fastest_angular_frequency)) :
        ref_.settings_.maximum_step_s;
    std::uint64_t consecutive_rejections = 0;
    double h = std::min(sampling_ceiling,
                        ref_.next_step_s_ > 0.0 ? ref_.next_step_s_ : sampling_ceiling);
    while (ref_.state_.time_s < target) {
        const double remaining = target - ref_.state_.time_s;
        h = std::min(h, remaining);
        const double floor = progress_floor(ref_.state_.time_s, ref_.settings_);
        if (h < floor) {
            stop(Status::numerical_failure, Failure::minimum_progress);
            return result();
        }
        // Avoid creating a final interval smaller than the progress floor.
        if (remaining > h && remaining - h < floor) {
            h = 0.5 * remaining;
        }
        const Point start{ref_.state_.time_s, state_vector(ref_.state_), ref_.observation_};
        Trial candidate;
        trial_power_residual_ = 0.0;
        bool accepted = trial(start.t, start.y, h, candidate);
        std::array<Point, 5> points{};
        std::vector<Event> found;
        if (accepted && candidate.error <= 1.0) {
            accepted = scan(start, candidate, h, points, found);
        } else {
            accepted = false;
        }
        if (accepted) {
            // End a crossing interval at the first natural branch root's
            // post-root bracket endpoint, then restart from its unprojected
            // mechanical state. One additional replay per outer trial avoids
            // repeatedly chasing a midpoint toward the same zero. All replay
            // and scan work consumes the existing call-wide budgets.
            double split_h = h;
            for (const auto& event : found) {
                const double offset = event.bracket_end_s - start.t;
                if (event.before != event.after && offset >= floor &&
                    h - offset >= floor) {
                    split_h = std::min(split_h, offset);
                }
            }
            if (split_h < h) {
                h = split_h;
                found.clear();
                // Keep this diagnostic scoped to the interval being accepted,
                // excluding evaluations from the abandoned longer trial.
                trial_power_residual_ = 0.0;
                accepted = trial(start.t, start.y, h, candidate);
                if (accepted && candidate.error <= 1.0) {
                    accepted = scan(start, candidate, h, points, found);
                } else {
                    accepted = false;
                }
            }
        }
        if (!accepted) {
            if (status_ == Status::budget_exhausted || failure_ == Failure::unresolved_grazing) {
                return result();
            }
            ++work_.rejected_steps;
            ++consecutive_rejections;
            if (consecutive_rejections >= ref_.settings_.maximum_consecutive_rejections) {
                stop(Status::budget_exhausted, Failure::rejection_limit);
                // A recoverable trial error must not mask the bounded terminal cause.
                status_ = Status::budget_exhausted;
                failure_ = Failure::rejection_limit;
                return result();
            }
            if (0.5 * h < floor) {
                status_ = Status::numerical_failure;
                failure_ = Failure::minimum_progress;
                return result();
            }
            status_ = Status::success;
            failure_ = Failure::none;
            h *= 0.5;
            continue;
        }
        if (ref_.settings_.locate_peak_candidates) {
            const double accepted_power_residual = trial_power_residual_;
            if (!collect_interval_peaks(start, h) ||
                !peak_samples(points.back().observation, points.back().t, PeakOrigin::sample)) {
                return result();
            }
            // Extra diagnostic evaluations must not redefine the independently
            // sampled legacy diagnostics or change the accepted trajectory.
            trial_power_residual_ = accepted_power_residual;
        }
        if (!commit_interval(start, candidate, h, points, found)) {
            return result();
        }
        consecutive_rejections = 0;
        const double factor = candidate.error == 0.0 ? 2.0 :
            std::clamp(0.9 * std::pow(candidate.error, -0.2), 0.2, 2.0);
        h = std::min(sampling_ceiling, h * factor);
        ref_.next_step_s_ = h;
    }
    add_work(ref_.diagnostics_.work, work_);
    return result();
}

Result ReferenceSolver::initialization_events() {
    attempted_until_ = ref_.state_.time_s;
    configure_analytic_free_motion();
    if (!peak_samples(ref_.observation_, ref_.state_.time_s, PeakOrigin::sample)) {
        return result();
    }
    for (const auto id : {ContactId::hammer, ContactId::damper}) {
        const auto& s = id == ContactId::hammer ? ref_.observation_.hammer : ref_.observation_.damper;
        if (s.branch == Branch::disabled) {
            continue;
        }
        Event event;
        event.contact = id;
        event.time_s = ref_.state_.time_s;
        event.bracket_begin_s = event.time_s;
        event.bracket_end_s = event.time_s;
        event.gap_m = s.gap_m;
        event.relative_velocity_m_per_s = s.gap_rate_m_per_s;
        event.before = Branch::separated;
        event.after = forward_branch(s);
        bool needed = false;
        if (s.gap_m == 0.0 && s.gap_rate_m_per_s > 0.0) {
            event.kind = EventKind::onset;
            needed = true;
        } else if (s.branch == Branch::clipped) {
            event.kind = EventKind::clamp_enter;
            event.before = Branch::active;
            needed = true;
        } else if (s.branch == Branch::grazing) {
            event.kind = EventKind::grazing;
            needed = true;
        }
        if (needed && !append_event(event)) {
            return result();
        }
    }
    return result();
}

bool ReferenceSolver::before_action() {
    const double t = ref_.state_.time_s;
    const double horizon = std::min(ref_.settings_.event_time_tolerance_s, maximum_time - t);
    if (horizon < progress_floor(t, ref_.settings_)) {
        return true;
    }
    const Point start{t, state_vector(ref_.state_), ref_.observation_};
    for (const auto id : {ContactId::hammer, ContactId::damper}) {
        const auto& s = sample(start, id);
        if (s.branch == Branch::disabled || s.gap_m == 0.0 ||
            !opposite(s.gap_m, s.gap_rate_m_per_s) ||
            std::abs(s.gap_m) > std::abs(s.gap_rate_m_per_s) * horizon) {
            continue;
        }
        Point end;
        if (!replay(start, t + horizon, end)) {
            return false;
        }
        if (!opposite(s.gap_m, sample(end, id).gap_m)) {
            continue;
        }
        Root root;
        if (!refine(start, start, end, id, RootFunction::gap, root)) {
            return false;
        }
        if (std::abs(s.gap_m) > ref_.settings_.event_gap_tolerance_m) {
            return stop(Status::numerical_failure, Failure::unresolved_event);
        }
        const bool onset = s.gap_m < 0.0;
        Event event;
        event.contact = id;
        event.kind = onset ? EventKind::onset : EventKind::separation;
        event.time_s = t;
        event.bracket_begin_s = t;
        event.bracket_end_s = root.right.t;
        event.bracket_gap_uncertainty_m = std::max(std::abs(s.gap_m), root.uncertainty);
        event.gap_m = s.gap_m;
        event.relative_velocity_m_per_s = s.gap_rate_m_per_s;
        event.before = onset ? Branch::separated : forward_branch(s);
        event.after = onset ? (s.raw_factor_n_per_m > 0.0 ? Branch::active : Branch::clipped) :
                              Branch::separated;
        event.action_time_tie = true;
        if (!append_event(event)) {
            return false;
        }
    }
    return true;
}

bool ReferenceSolver::action_reclassify(const Observation& before, const Observation& after) {
    for (const auto id : {ContactId::hammer, ContactId::damper}) {
        const auto& a = id == ContactId::hammer ? before.hammer : before.damper;
        const auto& b = id == ContactId::hammer ? after.hammer : after.damper;
        const Branch from = forward_branch(a);
        const Branch to = forward_branch(b);
        if (from == to || from == Branch::disabled) {
            continue;
        }
        Event event;
        event.contact = id;
        event.time_s = ref_.state_.time_s;
        event.bracket_begin_s = event.time_s;
        event.bracket_end_s = event.time_s;
        event.gap_m = b.gap_m;
        event.relative_velocity_m_per_s = b.gap_rate_m_per_s;
        event.before = from;
        event.after = to;
        if (to == Branch::grazing) {
            event.kind = EventKind::grazing;
        } else if (to == Branch::separated) {
            event.kind = EventKind::separation;
        } else if (from == Branch::separated || from == Branch::grazing) {
            event.kind = EventKind::onset;
        } else {
            event.kind = to == Branch::clipped ? EventKind::clamp_enter : EventKind::clamp_exit;
        }
        if (!append_event(event)) {
            return false;
        }
    }
    ref_.diagnostics_.hammer_branch = forward_branch(after.hammer);
    ref_.diagnostics_.damper_branch = forward_branch(after.damper);
    return true;
}

Result ReferenceSolver::action_relaunch(double velocity) {
    attempted_until_ = ref_.state_.time_s;
    if (!bounded(velocity, -1000.0, 1000.0)) {
        stop(Status::invalid_input, Failure::invalid_state);
        return result();
    }
    if (!before_action()) {
        return result();
    }
    const Observation before = ref_.observation_;
    if (!peak_samples(before, ref_.state_.time_s, PeakOrigin::action_before)) {
        return result();
    }
    const double prior_velocity = ref_.state_.hammer_v_m_per_s;
    const double work = 0.5 * ref_.config_.hammer_mass_kg *
        (velocity - prior_velocity) * (velocity + prior_velocity);
    ref_.state_.hammer_v_m_per_s = velocity;
    Observation after;
    if (!observe_unchecked(ref_.config_, ref_.state_, after)) {
        stop(Status::numerical_failure, Failure::arithmetic);
        return result();
    }
    const double residual = after.total_energy_j - before.total_energy_j - work;
    if (!add(ref_.ledger_.relaunch_work_j, work, 8) ||
        !add(ref_.ledger_.cumulative_residual_j, residual, 9) ||
        !add(ref_.ledger_.sum_absolute_interval_residual_j, std::abs(residual), 10)) {
        return result();
    }
    ref_.ledger_.current_energy_j = after.total_energy_j;
    ref_.ledger_.maximum_absolute_cumulative_residual_j = std::max(
        ref_.ledger_.maximum_absolute_cumulative_residual_j,
        std::abs(ref_.ledger_.cumulative_residual_j));
    const double t = ref_.state_.time_s;
    if (!append_event({ContactId::action, EventKind::hammer_relaunch, t, t, t, 0.0,
                       after.hammer.gap_m, after.hammer.gap_rate_m_per_s, work,
                       forward_branch(before.hammer), forward_branch(after.hammer)}) ||
        !action_reclassify(before, after)) {
        return result();
    }
    ref_.observation_ = after;
    configure_analytic_free_motion();
    if (!peak_samples(after, t, PeakOrigin::action_after)) {
        return result();
    }
    sampled_peaks(ref_.diagnostics_, after, t);
    ref_.next_step_s_ = ref_.settings_.maximum_step_s;
    add_work(ref_.diagnostics_.work, work_);
    return result();
}

Result ReferenceSolver::action_motion(const Trajectory& trajectory) {
    attempted_until_ = ref_.state_.time_s;
    FaceSample face;
    const auto checked = sample_face(trajectory, ref_.state_.time_s, face);
    if (!checked) {
        return checked;
    }
    // Continuity is a physical precondition, not a tolerance that permits an
    // unaccounted face displacement. Equivalent formulas must share this value.
    if (face.position_m != ref_.observation_.face.position_m) {
        stop(Status::invalid_input, Failure::discontinuous_face_position);
        return result();
    }
    if (!before_action()) {
        return result();
    }
    const Observation before = ref_.observation_;
    if (!peak_samples(before, ref_.state_.time_s, PeakOrigin::action_before)) {
        return result();
    }
    ref_.config_.damper_motion = trajectory;
    Observation after;
    if (!observe_unchecked(ref_.config_, ref_.state_, after)) {
        stop(Status::numerical_failure, Failure::arithmetic);
        return result();
    }
    const double t = ref_.state_.time_s;
    if (!append_event({ContactId::action, EventKind::damper_motion_change, t, t, t, 0.0,
                       after.damper.gap_m, after.damper.gap_rate_m_per_s, 0.0,
                       forward_branch(before.damper), forward_branch(after.damper)}) ||
        !action_reclassify(before, after)) {
        return result();
    }
    ref_.observation_ = after;
    configure_analytic_free_motion();
    if (!peak_samples(after, t, PeakOrigin::action_after)) {
        return result();
    }
    sampled_peaks(ref_.diagnostics_, after, t);
    ref_.next_step_s_ = ref_.settings_.maximum_step_s;
    add_work(ref_.diagnostics_.work, work_);
    return result();
}

Result Reference::initialize(const Configuration& configuration, const State& initial,
                            const Settings& settings) {
    if (!valid_configuration(configuration)) {
        return fail(Status::invalid_input, Failure::invalid_configuration, initial.time_s);
    }
    if (!valid_settings(settings)) {
        return fail(Status::invalid_input, Failure::invalid_settings, initial.time_s);
    }
    if (!valid_state(initial)) {
        return fail(Status::invalid_input, Failure::invalid_state, initial.time_s);
    }
    try {
        Reference candidate;
        candidate.config_ = configuration;
        candidate.settings_ = settings;
        candidate.state_ = initial;
        if (!observe_unchecked(configuration, initial, candidate.observation_)) {
            return fail(Status::numerical_failure, Failure::arithmetic, initial.time_s);
        }
        candidate.ledger_.initial_energy_j = candidate.observation_.total_energy_j;
        candidate.ledger_.current_energy_j = candidate.observation_.total_energy_j;
        candidate.next_step_s_ = settings.maximum_step_s;
        candidate.diagnostics_.hammer_branch = forward_branch(candidate.observation_.hammer);
        candidate.diagnostics_.damper_branch = forward_branch(candidate.observation_.damper);
        sampled_peaks(candidate.diagnostics_, candidate.observation_, initial.time_s);
        candidate.initialized_ = true;
        ReferenceSolver solver(candidate);
        const auto result = solver.initialization_events();
        if (result) {
            *this = std::move(candidate);
        }
        return result;
    } catch (const std::bad_alloc&) {
        return fail(Status::budget_exhausted, Failure::allocation_failure, initial.time_s);
    } catch (const std::length_error&) {
        return fail(Status::budget_exhausted, Failure::allocation_failure, initial.time_s);
    }
}

Result Reference::advance(double dt) {
    if (!initialized_) {
        return fail(Status::invalid_input, Failure::not_initialized);
    }
    try {
        Reference candidate = *this;
        ReferenceSolver solver(candidate);
        const auto result = solver.advance(dt);
        if (result) {
            *this = std::move(candidate);
        }
        return result;
    } catch (const std::bad_alloc&) {
        return fail(Status::budget_exhausted, Failure::allocation_failure, state_.time_s);
    } catch (const std::length_error&) {
        return fail(Status::budget_exhausted, Failure::allocation_failure, state_.time_s);
    }
}

Result Reference::relaunch_hammer(double velocity) {
    if (!initialized_) {
        return fail(Status::invalid_input, Failure::not_initialized);
    }
    try {
        Reference candidate = *this;
        ReferenceSolver solver(candidate);
        const auto result = solver.action_relaunch(velocity);
        if (result) {
            *this = std::move(candidate);
        }
        return result;
    } catch (const std::bad_alloc&) {
        return fail(Status::budget_exhausted, Failure::allocation_failure, state_.time_s);
    } catch (const std::length_error&) {
        return fail(Status::budget_exhausted, Failure::allocation_failure, state_.time_s);
    }
}

Result Reference::set_damper_motion(const Trajectory& trajectory) {
    if (!initialized_) {
        return fail(Status::invalid_input, Failure::not_initialized);
    }
    try {
        Reference candidate = *this;
        ReferenceSolver solver(candidate);
        const auto result = solver.action_motion(trajectory);
        if (result) {
            *this = std::move(candidate);
        }
        return result;
    } catch (const std::bad_alloc&) {
        return fail(Status::budget_exhausted, Failure::allocation_failure, state_.time_s);
    } catch (const std::length_error&) {
        return fail(Status::budget_exhausted, Failure::allocation_failure, state_.time_s);
    }
}

} // namespace rhodes::contact
