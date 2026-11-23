#include "rhodes/contact_reference.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <limits>
#include <locale>
#include <numbers>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
namespace c = rhodes::contact;
constexpr double grazing_time = 0.0010073;

struct Fixture {
    c::Configuration config{};
    c::State initial{};
    std::array<double, 2> pickup{1.0, 0.2};
    double duration = 0.020;
    double relaunch_time = -1.0;
    bool grazing = false;
};

Fixture fixture(std::string name) {
    Fixture f;
    f.initial.hammer_x_m = -0.0002;
    f.initial.hammer_v_m_per_s = 1.0;
    const bool scaled = name.ends_with("_scaled");
    if (scaled) { name.resize(name.size() - 7); }
    const bool free = name == "conservative_impact" || name == "damped_impact" ||
        name == "forced_clamp" || name.starts_with("event_");
    if (free) {
        f.config.modes = {{{0.002, 0.0, 0.0}, {0.001, 650.0, 0.0}}};
        f.config.hammer_projection = {1.0, 0.0};
        f.initial.hammer_x_m = 0.0;
        f.duration = 0.002;
    }
    if (name == "conservative_impact" || name == "supported_impact") {
        // Frozen base fixtures.
    } else if (name == "damped_impact") {
        f.config.hammer.c_ns_per_m2 = 40000.0;
    } else if (name == "forced_clamp") {
        f.config.hammer.c_ns_per_m2 = 40000.0;
        f.initial.hammer_x_m = 0.0001;
        f.initial.hammer_v_m_per_s = -1.0;
        f.duration = 0.0002;
    } else if (name == "supported_conservative") {
        for (auto& mode : f.config.modes) { mode.decay_per_s = 0.0; }
    } else if (name == "separated_oscillator") {
        f.initial.hammer_x_m = -0.002;
        f.initial.hammer_v_m_per_s = 0.0;
        f.initial.q_m = {0.00008, -0.00002};
        f.initial.v_m_per_s = {0.2, -0.1};
    } else if (name == "evolved_relaunch") {
        f.relaunch_time = 0.0037;
    } else if (name == "event_before" || name == "event_at" || name == "event_after") {
        const double separation = std::numbers::pi / std::sqrt(20000.0 / 0.0016);
        f.relaunch_time = separation + (name == "event_before" ? -1e-6 :
                                        name == "event_after" ? 1e-6 : 0.0);
    } else if (name == "relaunch_phase_plus" || name == "relaunch_phase_minus") {
        const double sign = name == "relaunch_phase_plus" ? 1.0 : -1.0;
        f.initial.q_m = {0.00008, -0.00002};
        f.initial.v_m_per_s = {sign * 0.2, -sign * 0.1};
        f.initial.hammer_x_m = 0.00008 + 0.35 * -0.00002 + 0.00005;
        f.initial.hammer_v_m_per_s = -0.3;
        f.config.hammer.c_ns_per_m2 = 40000.0;
        f.relaunch_time = 0.0;
        f.duration = 0.002;
    } else if (name.starts_with("preload_") || name == "moving_damper" ||
               name == "withdraw_damper" || name == "advance_damper") {
        if (name != "preload_equilibrium" && name != "preload_plus" && name != "preload_minus" &&
            name != "preload_linear_plus" && name != "preload_linear_minus" &&
            name != "moving_damper" && name != "withdraw_damper" && name != "advance_damper") {
            throw std::runtime_error("Unknown fixture");
        }
        f.initial.hammer_x_m = -0.002;
        f.initial.hammer_v_m_per_s = 0.0;
        f.config.damper.enabled = true;
        f.config.damper_motion.offset_m = -0.00005;
        std::array<double, 2> stiffness{};
        double compliance = 0.0;
        for (std::size_t i = 0; i < 2; ++i) {
            const double omega = 2.0 * std::numbers::pi * f.config.modes[i].frequency_hz;
            stiffness[i] = f.config.modes[i].mass_kg * omega * omega;
            compliance += f.config.damper_projection[i] * f.config.damper_projection[i] / stiffness[i];
        }
        const double force = f.config.damper.k_n_per_m * 0.00005 /
            (1.0 + f.config.damper.k_n_per_m * compliance);
        for (std::size_t i = 0; i < 2; ++i) {
            f.initial.q_m[i] = -f.config.damper_projection[i] * force / stiffness[i];
        }
        if (name.ends_with("_plus")) { f.initial.q_m[0] += 1e-6; }
        if (name.ends_with("_minus")) { f.initial.q_m[0] -= 1e-6; }
        if (name.starts_with("preload_linear_")) { f.config.damper.c_ns_per_m2 = 0.0; }
        if (name == "withdraw_damper" || name == "advance_damper") {
            f.config.damper_motion.kind = c::TrajectoryKind::linear;
            f.config.damper_motion.velocity_m_per_s = name == "withdraw_damper" ? 0.02 : -0.02;
            f.duration = 0.0001;
        }
        if (name == "moving_damper") {
            f.initial.q_m = {};
            f.config.damper_motion.kind = c::TrajectoryKind::sinusoidal;
            f.config.damper_motion.offset_m = 0.00015;
            f.config.damper_motion.amplitude_m = 0.00025;
            f.config.damper_motion.angular_frequency_rad_per_s = 2.0 * std::numbers::pi / 0.012;
            f.duration = 0.024;
        }
    } else if (name == "grazing_miss" || name == "grazing_tangent" || name == "grazing_contact") {
        const double omega = 2.0 * std::numbers::pi * 200.0;
        const double shift = name == "grazing_miss" ? -1e-8 : name == "grazing_contact" ? 1e-8 : 0.0;
        f.config.hammer_projection = {1.0, 0.0};
        for (auto& mode : f.config.modes) { mode.decay_per_s = 0.0; }
        f.initial.q_m = {-1e-4 * std::cos(omega * grazing_time), 0.0};
        f.initial.v_m_per_s = {-1e-4 * omega * std::sin(omega * grazing_time), 0.0};
        f.initial.hammer_x_m = -1e-4 + shift;
        f.initial.hammer_v_m_per_s = 0.0;
        f.duration = grazing_time + 30e-6;
        f.grazing = true;
    } else {
        throw std::runtime_error("Unknown fixture; see --help");
    }
    if (scaled) {
        if (name != "supported_impact" && name != "moving_damper" && name != "evolved_relaunch") {
            throw std::runtime_error("Only the three frozen coordinate-rescaling fixtures are supported");
        }
        constexpr std::array<double, 2> scale{2.0, -3.0};
        for (std::size_t i = 0; i < 2; ++i) {
            f.config.modes[i].mass_kg /= scale[i] * scale[i];
            f.config.hammer_projection[i] /= scale[i];
            f.config.damper_projection[i] /= scale[i];
            f.pickup[i] /= scale[i];
            f.initial.q_m[i] *= scale[i];
            f.initial.v_m_per_s[i] *= scale[i];
        }
    }
    return f;
}

double number(std::string_view text) {
    double value = 0.0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || !std::isfinite(value)) {
        throw std::runtime_error("Expected a finite number");
    }
    return value;
}

void usage() {
    std::cout << "rhodes_contact_reference: frozen synthetic offline contact fixtures, JSON to stdout\n"
        "Options: --fixture NAME --max-step SECONDS --event-tolerance SECONDS\n"
        "         --tolerance-scale FACTOR --output-step SECONDS\n"
        "         --locate-peak-candidates (optional, uncertified sign-change search)\n"
        "Defaults: conservative_impact, 20e-6, 1e-8, 1, 20e-6.\n"
        "Fixtures: conservative_impact, damped_impact, forced_clamp, supported_impact,\n"
        "supported_conservative, separated_oscillator, preload_equilibrium, preload_plus,\n"
        "preload_minus, preload_linear_plus, preload_linear_minus, moving_damper,\n"
        "withdraw_damper, advance_damper, relaunch_phase_plus, relaunch_phase_minus,\n"
        "evolved_relaunch, event_before, event_at, event_after, grazing_miss,\n"
        "grazing_tangent, grazing_contact. _scaled is available for supported_impact,\n"
        "moving_damper and evolved_relaunch. Failures return JSON success=false and exit 2.\n"
        "No measured parameters, real-time claim, waveform normalization or time shifting.\n";
}

struct Sample {
    c::State state;
    c::Observation observation;
    c::Ledger ledger;
};

struct RelaunchSnapshot {
    Sample before;
    Sample after;
    double requested_velocity_m_per_s;
    c::Result result;
};

std::string_view event_kind(c::EventKind kind) {
    switch (kind) {
    case c::EventKind::onset: return "onset";
    case c::EventKind::separation: return "separation";
    case c::EventKind::clamp_enter: return "clamp_on";
    case c::EventKind::clamp_exit: return "clamp_off";
    case c::EventKind::grazing: return "grazing";
    case c::EventKind::hammer_relaunch: return "relaunch";
    case c::EventKind::damper_motion_change: return "motion_change";
    }
    return "unknown";
}

std::string_view peak_origin(c::PeakOrigin origin) {
    switch (origin) {
    case c::PeakOrigin::disabled: return "disabled";
    case c::PeakOrigin::sample: return "sample";
    case c::PeakOrigin::stationary_candidate: return "stationary_candidate";
    case c::PeakOrigin::action_before: return "action_before";
    case c::PeakOrigin::action_after: return "action_after";
    }
    return "unknown";
}

void emit_peak(const c::PeakCandidate& peak, std::string_view value_key) {
    std::cout << "{\"" << value_key << "\":" << peak.value
        << ",\"time_s\":" << peak.time_s
        << ",\"bracket_lo_s\":" << peak.bracket_begin_s
        << ",\"bracket_hi_s\":" << peak.bracket_end_s
        << ",\"origin\":\"" << peak_origin(peak.origin) << "\"}";
}

void emit_located_peaks(const c::Diagnostics& diagnostics) {
    std::cout << ",\"located_peak_candidates\":{"
        "\"coverage\":\"uncertified_sign_change_search\","
        "\"evaluation_method\":\"fixed_interval_anchor_replay_two_rk4_half_steps_or_certified_free_motion\","
        "\"hammer_compression\":";
    emit_peak(diagnostics.located_hammer_compression, "value_m");
    std::cout << ",\"hammer_force\":";
    emit_peak(diagnostics.located_hammer_force, "value_n");
    std::cout << ",\"damper_compression\":";
    emit_peak(diagnostics.located_damper_compression, "value_m");
    std::cout << ",\"damper_force\":";
    emit_peak(diagnostics.located_damper_force, "value_n");
    std::cout << '}';
}

void emit_sample(const Sample& sample, const Fixture& input) {
    const auto& s = sample.state;
    const auto& o = sample.observation;
    const auto& l = sample.ledger;
    double x = 0.0, v = 0.0;
    for (std::size_t i = 0; i < 2; ++i) { x += input.pickup[i] * s.q_m[i]; v += input.pickup[i] * s.v_m_per_s[i]; }
    std::cout << "{\"time_s\":" << s.time_s << ",\"q_m\":[" << s.q_m[0] << ',' << s.q_m[1]
        << "],\"v_m_per_s\":[" << s.v_m_per_s[0] << ',' << s.v_m_per_s[1]
        << "],\"hammer_position_m\":" << s.hammer_x_m << ",\"hammer_velocity_m_per_s\":" << s.hammer_v_m_per_s
        << ",\"hammer_force_n\":" << o.hammer.force_n << ",\"damper_force_n\":" << o.damper.force_n
        << ",\"hammer_gap_m\":" << o.hammer.gap_m << ",\"damper_gap_m\":" << o.damper.gap_m
        << ",\"hammer_relative_velocity_m_per_s\":" << o.hammer.gap_rate_m_per_s
        << ",\"damper_relative_velocity_m_per_s\":" << o.damper.gap_rate_m_per_s
        << ",\"hammer_impulse_ns\":" << l.hammer_impulse_ns << ",\"damper_impulse_ns\":" << l.damper_impulse_ns
        << ",\"modal_loss_j\":" << l.background_loss_j
        << ",\"hammer_loss_j\":" << l.hammer_active_loss_j + l.hammer_clipped_loss_j
        << ",\"damper_loss_j\":" << l.damper_active_loss_j + l.damper_clipped_loss_j
        << ",\"hammer_clipped_loss_j\":" << l.hammer_clipped_loss_j
        << ",\"damper_clipped_loss_j\":" << l.damper_clipped_loss_j
        << ",\"actuator_work_j\":" << l.damper_work_j << ",\"launch_work_j\":" << l.relaunch_work_j
        << ",\"energy_j\":" << o.total_energy_j
        << ",\"modal_acceleration_m_per_s2\":[" << o.modal_acceleration_m_per_s2[0] << ',' << o.modal_acceleration_m_per_s2[1]
        << "],\"hammer_acceleration_m_per_s2\":" << o.hammer_acceleration_m_per_s2
        << ",\"instantaneous_power_residual_w\":" << o.power_residual_w
        << ",\"actuator_power_w\":" << o.damper_work_power_w
        << ",\"pickup_voltage_v\":" << -(0.4 + 1600.0 * x + 1200000.0 * x * x) * v << '}';
}
} // namespace

int main(int argc, char** argv) {
    try {
        std::string name = "conservative_impact";
        c::Settings settings;
        double output_step = 20e-6;
        double tolerance_scale = 1.0;
        for (int i = 1; i < argc; ++i) {
            const std::string_view flag = argv[i];
            if (flag == "--help") { usage(); return 0; }
            if (flag == "--locate-peak-candidates") {
                settings.locate_peak_candidates = true;
                continue;
            }
            if (flag != "--fixture" && flag != "--max-step" && flag != "--event-tolerance" &&
                flag != "--tolerance-scale" && flag != "--output-step") {
                throw std::runtime_error("Unknown option");
            }
            if (++i >= argc) { throw std::runtime_error("Missing option value"); }
            if (flag == "--fixture") { name = argv[i]; }
            else if (flag == "--max-step") { settings.maximum_step_s = number(argv[i]); }
            else if (flag == "--event-tolerance") { settings.event_time_tolerance_s = number(argv[i]); }
            else if (flag == "--tolerance-scale") { tolerance_scale = number(argv[i]); }
            else { output_step = number(argv[i]); }
        }
        if (tolerance_scale < 1e-4 || tolerance_scale > 100.0 || output_step < 1e-6 || output_step > 0.001) {
            throw std::runtime_error("Tolerance scale must be 1e-4..100; output step 1e-6..0.001 s");
        }
        settings.relative_tolerance *= tolerance_scale;
        settings.position_absolute_tolerance_m *= tolerance_scale;
        settings.velocity_absolute_tolerance_m_per_s *= tolerance_scale;
        settings.energy_absolute_tolerance_j *= tolerance_scale;
        settings.impulse_absolute_tolerance_ns *= tolerance_scale;
        const auto input = fixture(name);
        c::Reference reference;
        auto result = reference.initialize(input.config, input.initial, settings);
        if (!result) { throw std::runtime_error("Reference rejected fixture or solver settings"); }
        std::vector<Sample> samples;
        std::vector<RelaunchSnapshot> relaunch_snapshots;
        const auto snapshot = [&] {
            return Sample{reference.state(), reference.observation(), reference.ledger()};
        };
        const auto record = [&] { samples.push_back(snapshot()); };
        const auto relaunch = [&](double velocity_m_per_s) {
            const auto before = snapshot();
            const auto action_result = reference.relaunch_hammer(velocity_m_per_s);
            relaunch_snapshots.push_back({before, snapshot(), velocity_m_per_s, action_result});
            return action_result;
        };
        bool relaunched = input.relaunch_time < 0.0;
        if (input.relaunch_time == 0.0) {
            result = relaunch(1.0);
            relaunched = true;
        }
        record();
        std::size_t grid = 1;
        while (result && reference.state().time_s < input.duration) {
            double target = std::min(input.duration, static_cast<double>(grid) * output_step);
            if (target <= reference.state().time_s) { ++grid; continue; }
            if (input.grazing) {
                const double begin = grazing_time - 30e-6;
                if (reference.state().time_s < begin) { target = std::min(target, begin); }
                else { target = input.duration; } // Required 60-us outer grazing call.
            }
            if (!relaunched && input.relaunch_time <= target) { target = input.relaunch_time; }
            const double dt = target - reference.state().time_s;
            if (dt < 1e-12) { throw std::runtime_error("Output schedule lost representable progress"); }
            result = reference.advance(dt);
            if (!result) { break; }
            if (!relaunched && target == input.relaunch_time) {
                result = relaunch(1.0);
                relaunched = true;
            }
            record();
            if (samples.size() > 30000) { throw std::runtime_error("Output row cap exceeded"); }
        }
        std::cout.imbue(std::locale::classic());
        std::cout << std::setprecision(17);
        std::cout << "{\"schema_version\":1,\"model\":\"synthetic-contact-p1\",\"runs\":[{\"fixture\":\"" << name
            << "\",\"success\":" << (result ? "true" : "false")
            << ",\"failure_code\":" << static_cast<int>(result.failure)
            << ",\"maximum_step_s\":" << settings.maximum_step_s
            << ",\"event_tolerance_s\":" << settings.event_time_tolerance_s
            << ",\"absolute_tolerance\":" << settings.position_absolute_tolerance_m
            << ",\"relative_tolerance\":" << settings.relative_tolerance
            << ",\"velocity_absolute_tolerance\":" << settings.velocity_absolute_tolerance_m_per_s
            << ",\"energy_absolute_tolerance\":" << settings.energy_absolute_tolerance_j
            << ",\"impulse_absolute_tolerance\":" << settings.impulse_absolute_tolerance_ns
            << ",\"output_step_s\":" << output_step << ",\"samples\":[";
        for (std::size_t i = 0; i < samples.size(); ++i) { if (i) { std::cout << ','; } emit_sample(samples[i], input); }
        std::cout << "],\"relaunch_snapshots\":[";
        for (std::size_t i = 0; i < relaunch_snapshots.size(); ++i) {
            if (i) { std::cout << ','; }
            const auto& action = relaunch_snapshots[i];
            std::cout << "{\"time_s\":" << action.before.state.time_s
                << ",\"requested_velocity_m_per_s\":" << action.requested_velocity_m_per_s
                << ",\"success\":" << (action.result ? "true" : "false")
                << ",\"failure_code\":" << static_cast<int>(action.result.failure)
                << ",\"work_j\":" << action.after.ledger.relaunch_work_j - action.before.ledger.relaunch_work_j
                << ",\"before\":";
            emit_sample(action.before, input);
            std::cout << ",\"after\":";
            emit_sample(action.after, input);
            std::cout << '}';
        }
        std::cout << "],\"events\":[";
        for (std::size_t i = 0; i < reference.events().size(); ++i) {
            if (i) { std::cout << ','; }
            const auto& event = reference.events()[i];
            std::cout << "{\"contact\":\"" << (event.contact == c::ContactId::damper ? "damper" : "hammer")
                << "\",\"kind\":\"" << event_kind(event.kind) << "\",\"time_s\":" << event.time_s
                << ",\"bracket_lo_s\":" << event.bracket_begin_s << ",\"bracket_hi_s\":" << event.bracket_end_s
                << ",\"gap_uncertainty_m\":" << event.bracket_gap_uncertainty_m << ",\"work_j\":" << event.work_j << '}';
        }
        const auto& d = reference.diagnostics();
        const auto& ledger = reference.ledger();
        std::cout << "],\"diagnostics\":{\"max_abs_cumulative_residual_j\":" << ledger.maximum_absolute_cumulative_residual_j
            << ",\"sum_abs_interval_residual_j\":" << ledger.sum_absolute_interval_residual_j
            << ",\"maximum_power_residual_w\":" << d.maximum_absolute_power_residual_w
            << ",\"initial_energy_j\":" << ledger.initial_energy_j
            << ",\"minimum_accepted_step_s\":" << d.minimum_accepted_step_s
            << ",\"maximum_accepted_step_s\":" << d.maximum_accepted_step_s
            << ",\"accepted_steps\":" << d.work.accepted_steps << ",\"rejected_steps\":" << d.work.rejected_steps
            << ",\"rhs_evaluations\":" << d.work.rhs_evaluations
            << ",\"analytic_free_evaluations\":" << d.work.analytic_free_evaluations
            << ",\"event_subdivisions\":" << d.work.event_subdivisions
            << ",\"root_iterations\":" << d.work.root_iterations
            << ",\"grazing_candidates\":" << d.grazing_candidates
            << ",\"sampled_maxima\":{\"scope\":\"accepted endpoints and checked interior samples; not continuous-time extrema\""
            << ",\"hammer_compression_m\":" << d.sampled_maximum_hammer_compression_m
            << ",\"hammer_compression_time_s\":" << d.sampled_maximum_hammer_compression_time_s
            << ",\"hammer_force_n\":" << d.sampled_maximum_hammer_force_n
            << ",\"hammer_force_time_s\":" << d.sampled_maximum_hammer_force_time_s
            << ",\"damper_compression_m\":" << d.sampled_maximum_damper_compression_m
            << ",\"damper_compression_time_s\":" << d.sampled_maximum_damper_compression_time_s
            << ",\"damper_force_n\":" << d.sampled_maximum_damper_force_n
            << ",\"damper_force_time_s\":" << d.sampled_maximum_damper_force_time_s
            << '}';
        if (settings.locate_peak_candidates) { emit_located_peaks(d); }
        std::cout << "}}]}\n";
        return result ? 0 : 2;
    } catch (const std::exception& error) {
        std::cerr << "Contact reference error: " << error.what() << '\n';
        return 1;
    }
}
