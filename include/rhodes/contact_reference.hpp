#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace rhodes::contact {

// Synthetic offline reference only. All coordinates are unshifted SI modal
// coordinates; no parameter in this interface is a measured Rhodes property.
inline constexpr std::size_t mode_count = 2;

enum class Status { success, invalid_input, budget_exhausted, numerical_failure };
enum class Failure {
    none, not_initialized, invalid_configuration, invalid_state, invalid_settings,
    invalid_time, unsupported_trajectory, discontinuous_face_position,
    trial_limit, evaluation_limit, rejection_limit, root_limit, subdivision_limit,
    event_limit, history_limit, minimum_progress, state_domain, arithmetic,
    unresolved_grazing, singular_equilibrium, allocation_failure, unresolved_event
};

struct WorkCounters {
    std::uint64_t trials = 0;
    std::uint64_t rhs_evaluations = 0;
    std::uint64_t accepted_steps = 0; // Numerical path uses two RK4 half-steps.
    std::uint64_t analytic_free_evaluations = 0; // Certified zero-force branch only.
    std::uint64_t rejected_steps = 0;
    std::uint64_t root_iterations = 0;
    std::uint64_t extrema_checks = 0;
    std::uint64_t event_subdivisions = 0;
    bool operator==(const WorkCounters&) const = default;
};

struct Result {
    Status status = Status::success;
    Failure failure = Failure::none;
    WorkCounters attempted{}; // Attempted work survives only in this return value.
    double attempted_until_s = 0.0;
    [[nodiscard]] explicit operator bool() const noexcept { return status == Status::success; }
};

struct Mode {
    double mass_kg = 0.002;
    double frequency_hz = 200.0; // Zero permits a free mass.
    double decay_per_s = 0.0;    // C = 2 m beta; no underdamped-only restriction.
    bool operator==(const Mode&) const = default;
};

struct ContactLaw {
    bool enabled = true;
    double k_n_per_m = 20000.0;
    double c_ns_per_m2 = 0.0;
    double exponent = 1.0; // The supported constitutive domain is exactly p=1.
    bool operator==(const ContactLaw&) const = default;
};

enum class TrajectoryKind { constant, linear, sinusoidal };
struct Trajectory {
    TrajectoryKind kind = TrajectoryKind::constant;
    double offset_m = 0.0;
    double origin_s = 0.0;
    double velocity_m_per_s = 0.0; // Linear: offset + velocity*(t-origin).
    double amplitude_m = 0.0;     // Sinusoidal: offset + amplitude*cos(theta).
    double angular_frequency_rad_per_s = 0.0;
    double phase_rad = 0.0;       // theta = angular_frequency*(t-origin)+phase.
    bool operator==(const Trajectory&) const = default;
};

struct FaceSample {
    double position_m = 0.0;
    double velocity_m_per_s = 0.0;
    double acceleration_m_per_s2 = 0.0;
    bool operator==(const FaceSample&) const = default;
};

struct Configuration {
    std::array<Mode, mode_count> modes{{{0.002, 200.0, 1.0}, {0.001, 650.0, 3.0}}};
    double hammer_mass_kg = 0.008;
    std::array<double, mode_count> hammer_projection{1.0, 0.35};
    std::array<double, mode_count> damper_projection{1.0, -0.25};
    double hammer_clearance_m = 0.0;
    ContactLaw hammer{};
    ContactLaw damper{false, 12000.0, 30000.0, 1.0};
    Trajectory damper_motion{};
    bool operator==(const Configuration&) const = default;
};

struct State {
    double time_s = 0.0;
    std::array<double, mode_count> q_m{};
    std::array<double, mode_count> v_m_per_s{};
    double hammer_x_m = 0.0;
    double hammer_v_m_per_s = 0.0;
    bool operator==(const State&) const = default;
};

// Public finite domain: masses [1e-6,1] kg; frequency [0,20000] Hz;
// beta [0,1e5]/s; k,c [0,1e8] in the units above; projections [-100,100];
// positions/clearance/face [-1,1] m; velocities [-1000,1000] m/s;
// stored time [0,1000] s; each advance increment [1e-12,1] s;
// all derived scalars and ledger accumulators are finite and <=1e24 in magnitude.
struct Settings {
    double maximum_step_s = 20e-6;
    double minimum_step_s = 1e-14;
    double relative_tolerance = 1e-8;
    double position_absolute_tolerance_m = 1e-12;
    double velocity_absolute_tolerance_m_per_s = 1e-9;
    double energy_absolute_tolerance_j = 1e-13;
    double impulse_absolute_tolerance_ns = 1e-13;
    double reference_mass_kg = 0.001; // Mass-weighted errors preserve mode rescaling.
    double event_time_tolerance_s = 1e-8;
    double event_gap_tolerance_m = 5e-13;
    std::uint64_t maximum_trials = 100000;
    std::uint64_t maximum_rhs_evaluations = 1200000;
    std::uint64_t maximum_consecutive_rejections = 64;
    std::uint64_t maximum_root_iterations = 64; // Per root, not a reset of RHS count.
    std::uint64_t maximum_subdivision_depth = 32;
    std::uint64_t maximum_event_rows = 100000; // Persistent total history bound.
    std::uint64_t maximum_interval_rows = 100000;
    bool record_intervals = true;
    bool locate_peak_candidates = false; // Optional, uncertified stationary-point search.
    bool operator==(const Settings&) const = default;
};

enum class Branch { disabled, separated, active, clipped, grazing };
struct ContactSample {
    double gap_m = 0.0; // Signed delta: positive is compression.
    double gap_rate_m_per_s = 0.0;
    double compression_m = 0.0;
    double raw_factor_n_per_m = 0.0; // k + c*delta_dot, also reported when separated.
    double force_n = 0.0;
    double potential_j = 0.0;
    double active_loss_power_w = 0.0;
    double clipped_loss_power_w = 0.0;
    Branch branch = Branch::disabled;
    bool operator==(const ContactSample&) const = default;
};

struct Observation {
    ContactSample hammer{};
    ContactSample damper{};
    FaceSample face{};
    std::array<double, mode_count> modal_acceleration_m_per_s2{};
    double hammer_acceleration_m_per_s2 = 0.0;
    double mechanical_energy_j = 0.0; // Modal + hammer kinetic and modal elastic.
    double total_energy_j = 0.0;      // Includes both positive-compression potentials.
    double background_loss_power_w = 0.0;
    double damper_work_power_w = 0.0; // Signed actuator power -F_d*ell_dot.
    double power_residual_w = 0.0;    // Analytic dE/dt + physical losses - actuator power.
    bool operator==(const Observation&) const = default;
};

struct Ledger {
    double initial_energy_j = 0.0;
    double current_energy_j = 0.0;
    double hammer_active_loss_j = 0.0;
    double hammer_clipped_loss_j = 0.0;
    double damper_active_loss_j = 0.0;
    double damper_clipped_loss_j = 0.0;
    double background_loss_j = 0.0;
    double damper_work_j = 0.0; // Signed, including negative withdrawal work.
    double relaunch_work_j = 0.0;
    double hammer_impulse_ns = 0.0;
    double damper_impulse_ns = 0.0;
    double cumulative_residual_j = 0.0;
    double maximum_absolute_cumulative_residual_j = 0.0;
    double sum_absolute_interval_residual_j = 0.0;
    [[nodiscard]] double total_loss_j() const noexcept {
        return hammer_active_loss_j + hammer_clipped_loss_j +
               damper_active_loss_j + damper_clipped_loss_j + background_loss_j;
    }
    [[nodiscard]] double total_work_j() const noexcept { return damper_work_j + relaunch_work_j; }
    bool operator==(const Ledger&) const = default;
};

enum class ContactId { hammer, damper, action };
enum class EventKind {
    onset, separation, clamp_enter, clamp_exit, grazing,
    hammer_relaunch, damper_motion_change
};
struct Event {
    ContactId contact = ContactId::hammer;
    EventKind kind = EventKind::onset;
    double time_s = 0.0; // Bracket midpoint; external time for actions and their ties.
    double bracket_begin_s = 0.0;
    double bracket_end_s = 0.0;
    double bracket_gap_uncertainty_m = 0.0;
    double gap_m = 0.0;
    double relative_velocity_m_per_s = 0.0;
    double work_j = 0.0;
    Branch before = Branch::separated;
    Branch after = Branch::active;
    bool action_time_tie = false; // Root within event resolution ordered before an action.
    bool operator==(const Event&) const = default;
};

struct Interval {
    double begin_s = 0.0;
    double end_s = 0.0;
    double step_s = 0.0; // Numerical RK4 substeps are step_s/2; analytic path has none.
    double scaled_error = 0.0;
    double energy_change_j = 0.0;
    double physical_loss_j = 0.0;
    double actuator_work_j = 0.0;
    double residual_j = 0.0;
    bool operator==(const Interval&) const = default;
};

enum class PeakOrigin { disabled, sample, stationary_candidate, action_before, action_after };
struct PeakCandidate {
    double value = 0.0; // Metres for compression records; newtons for force records.
    double time_s = 0.0; // Time of the selected value, including root-bracket samples.
    double bracket_begin_s = 0.0;
    double bracket_end_s = 0.0;
    PeakOrigin origin = PeakOrigin::disabled; // Also used for a disabled contact.
    bool operator==(const PeakCandidate&) const = default;
};

struct Diagnostics {
    WorkCounters work{};
    double minimum_accepted_step_s = 0.0;
    double maximum_accepted_step_s = 0.0;
    double maximum_scaled_error = 0.0;
    double maximum_absolute_power_residual_w = 0.0;
    double maximum_event_bracket_width_s = 0.0;
    double maximum_event_gap_uncertainty_m = 0.0;
    // Maxima over accepted endpoints and checked interior samples only; these
    // are not certified continuous-time extrema or a peak-convergence claim.
    double sampled_maximum_hammer_compression_m = 0.0;
    double sampled_maximum_hammer_compression_time_s = 0.0;
    double sampled_maximum_hammer_force_n = 0.0;
    double sampled_maximum_hammer_force_time_s = 0.0;
    double sampled_maximum_damper_compression_m = 0.0;
    double sampled_maximum_damper_compression_time_s = 0.0;
    double sampled_maximum_damper_force_n = 0.0;
    double sampled_maximum_damper_force_time_s = 0.0;
    // Opt-in samples and located stationary candidates on fixed-anchor replay.
    // Finite sign-change sampling cannot certify continuous extrema. An enabled
    // all-zero record has sample origin; sample/action brackets are degenerate.
    PeakCandidate located_hammer_compression{};
    PeakCandidate located_hammer_force{};
    PeakCandidate located_damper_compression{};
    PeakCandidate located_damper_force{};
    std::uint64_t grazing_candidates = 0;
    Branch hammer_branch = Branch::disabled;
    Branch damper_branch = Branch::disabled;
    bool operator==(const Diagnostics&) const = default;
};

struct Equilibrium {
    State state{}; // Unshifted coordinates, not q-q_rest.
    double compression_m = 0.0;
    double force_n = 0.0;
    std::array<double, mode_count> static_force_residual_n{};
};

// Pure guarded observations for fixture construction and independent tests.
[[nodiscard]] Result sample_face(const Trajectory&, double time_s, FaceSample&) noexcept;
[[nodiscard]] Result observe(const Configuration&, const State&, Observation&) noexcept;
// Fixed enabled damper, strictly supported modes, and separated/disabled hammer.
// Preserves the seed time and hammer coordinates; modal positions are assembled.
[[nodiscard]] Result fixed_damper_equilibrium(const Configuration&, const State& seed,
                                            Equilibrium&) noexcept;

class ReferenceSolver;
class Reference {
public:
    // Explicit fixture/reset initialization; only a successful call replaces history.
    [[nodiscard]] Result initialize(const Configuration&, const State&, const Settings& = {});
    // Atomic whole call: any failure preserves all persistent state and diagnostics.
    [[nodiscard]] Result advance(double dt_s);
    // Changes only hammer velocity, with exact signed kinetic work; no position reset.
    // A natural root bracket entirely within event_time_tolerance_s after the
    // action time is treated as a simultaneous tie and recorded before the action.
    // The expanded bracket must also satisfy event_gap_tolerance_m; otherwise
    // the action fails explicitly with unresolved_event. A tie row retains its
    // full bracket and has action_time_tie=true; mechanics
    // are never projected onto a contact surface to force equality.
    [[nodiscard]] Result relaunch_hammer(double velocity_m_per_s);
    // At the current time, position must be continuous. A finite velocity breakpoint
    // is permitted and recorded; all mechanical coordinates/velocities persist.
    [[nodiscard]] Result set_damper_motion(const Trajectory&);

    [[nodiscard]] bool initialized() const noexcept { return initialized_; }
    [[nodiscard]] const Configuration& config() const noexcept { return config_; }
    [[nodiscard]] const Settings& settings() const noexcept { return settings_; }
    [[nodiscard]] const State& state() const noexcept { return state_; }
    [[nodiscard]] const Ledger& ledger() const noexcept { return ledger_; }
    [[nodiscard]] const Diagnostics& diagnostics() const noexcept { return diagnostics_; }
    [[nodiscard]] const Observation& observation() const noexcept { return observation_; }
    [[nodiscard]] const std::vector<Event>& events() const noexcept { return events_; }
    [[nodiscard]] const std::vector<Interval>& intervals() const noexcept { return intervals_; }

private:
    friend class ReferenceSolver;
    Configuration config_{};
    Settings settings_{};
    State state_{};
    Ledger ledger_{};
    Diagnostics diagnostics_{};
    Observation observation_{};
    std::vector<Event> events_{};
    std::vector<Interval> intervals_{};
    std::array<double, 12> compensation_{};
    double next_step_s_ = 0.0;
    // A stationary hammer and at most one projected undamped oscillator can
    // have a globally nonpositive gap certified from its harmonic amplitude.
    // Retaining its anchor avoids artificial phase/amplitude drift at tangency.
    State analytic_anchor_{};
    bool analytic_free_motion_ = false;
    int analytic_grazing_mode_ = -1;
    bool initialized_ = false;
};

} // namespace rhodes::contact
