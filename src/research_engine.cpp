#include "rhodes/research_engine.hpp"

#include <cmath>
#include <numbers>

namespace rhodes {
namespace {
bool valid_sample_rate(double rate) noexcept {
    return std::isfinite(rate) && rate >= minimum_sample_rate_hz &&
           rate <= maximum_sample_rate_hz;
}

constexpr std::array<double, modes_per_key> frequency_ratios{1.0, 2.73, 5.41};
constexpr std::array<double, modes_per_key> free_decay{0.8, 3.0, 7.0};
constexpr std::array<double, modes_per_key> damped_decay{30.0, 35.0, 45.0};
constexpr std::array<double, modes_per_key> strike_coupling{1.0, 0.45, 0.2};
constexpr std::array<double, modes_per_key> pickup_observation{1.0, 0.3, 0.08};
constexpr double synthetic_modal_mass_kg = 0.001;
constexpr double synthetic_max_velocity_change_m_per_s = 0.06;
}

bool ModalOscillator::configure(ModalParameters p, double rate) noexcept {
    if (!valid_sample_rate(rate) || !std::isfinite(p.frequency_hz) ||
        p.frequency_hz <= 0.0 || p.frequency_hz >= 0.45 * rate ||
        !std::isfinite(p.mass_kg) || p.mass_kg <= 0.0 ||
        !std::isfinite(p.decay_per_s) || p.decay_per_s < 0.0) {
        return false;
    }
    ModalOscillator candidate;
    candidate.parameters_ = p;
    candidate.sample_rate_hz_ = rate;
    candidate.omega_0_ = 2.0 * std::numbers::pi * p.frequency_hz;
    if (!candidate.update_transition(p.decay_per_s)) {
        return false;
    }
    candidate.configured_ = true;
    *this = candidate;
    return true;
}

bool ModalOscillator::update_transition(double decay) noexcept {
    if (!std::isfinite(decay) || decay < 0.0 || decay >= omega_0_) {
        return false; // Only the underdamped regime is represented.
    }
    const double omega_d = std::sqrt((omega_0_ - decay) * (omega_0_ + decay));
    const double angle = omega_d / sample_rate_hz_;
    const double sinc = std::abs(angle) < 1.0e-6
        ? 1.0 - angle * angle / 6.0
        : std::sin(angle) / angle;
    const double sine_over_omega_d = sinc / sample_rate_hz_;
    const double cosine = std::cos(angle);
    const double envelope = std::exp(-decay / sample_rate_hz_);
    const std::array<double, 4> next{
        envelope * (cosine + decay * sine_over_omega_d),
        envelope * sine_over_omega_d,
        -envelope * omega_0_ * omega_0_ * sine_over_omega_d,
        envelope * (cosine - decay * sine_over_omega_d)};
    for (const double value : next) {
        if (!std::isfinite(value)) {
            return false;
        }
    }
    transitions_[alternate_active_] = next;
    parameters_.decay_per_s = decay;
    return true;
}

bool ModalOscillator::set_decay(double decay) noexcept {
    if (!configured_) { return false; }
    if (decay == parameters_.decay_per_s) { return true; }
    if (alternate_transition_valid_ && decay == alternate_decay_per_s_) {
        alternate_active_ = !alternate_active_;
        alternate_decay_per_s_ = parameters_.decay_per_s;
        parameters_.decay_per_s = decay;
        return true;
    }
    // A cache miss retains the original calculation and validation. Store the
    // old transition only after success, so invalid updates leave both entries
    // and the mechanical state unchanged. No allocation or retry is involved.
    const auto previous_transition = transitions_[alternate_active_];
    const double previous_decay = parameters_.decay_per_s;
    if (!update_transition(decay)) { return false; }
    transitions_[!alternate_active_] = previous_transition;
    alternate_decay_per_s_ = previous_decay;
    alternate_transition_valid_ = true;
    return true;
}

bool ModalOscillator::valid_state(ModalState s) const noexcept {
    if (!std::isfinite(s.displacement_m) || !std::isfinite(s.velocity_m_per_s)) {
        return false;
    }
    const double potential_velocity = omega_0_ * s.displacement_m;
    return std::isfinite(0.5 * parameters_.mass_kg *
        (s.velocity_m_per_s * s.velocity_m_per_s +
         potential_velocity * potential_velocity));
}

bool ModalOscillator::set_state(ModalState s) noexcept {
    if (!configured_ || !valid_state(s)) {
        return false;
    }
    state_ = s;
    return true;
}

bool ModalOscillator::add_impulse(double impulse) noexcept {
    if (!configured_ || !std::isfinite(impulse)) {
        return false;
    }
    return set_state({state_.displacement_m,
                      state_.velocity_m_per_s + impulse / parameters_.mass_kg});
}

bool ModalOscillator::advance() noexcept {
    if (!configured_) {
        return false;
    }
    const auto& transition = transitions_[alternate_active_];
    return set_state({transition[0] * state_.displacement_m +
                          transition[1] * state_.velocity_m_per_s,
                      transition[2] * state_.displacement_m +
                          transition[3] * state_.velocity_m_per_s});
}

void ModalOscillator::reset() noexcept { state_ = {}; }
ModalState ModalOscillator::state() const noexcept { return state_; }

double ModalOscillator::energy_joules() const noexcept {
    const double potential_velocity = omega_0_ * state_.displacement_m;
    return 0.5 * parameters_.mass_kg *
        (state_.velocity_m_per_s * state_.velocity_m_per_s +
         potential_velocity * potential_velocity);
}

bool PolynomialPickup::configure(double a1, double a2, double a3) noexcept {
    if (!std::isfinite(a1) || !std::isfinite(a2) || !std::isfinite(a3)) {
        return false;
    }
    a1_ = a1;
    a2_ = a2;
    a3_ = a3;
    return true;
}

std::optional<double> PolynomialPickup::flux_linkage(double x) const noexcept {
    if (!std::isfinite(x)) {
        return std::nullopt;
    }
    const double value = ((a3_ * x + a2_) * x + a1_) * x;
    return std::isfinite(value) ? std::optional<double>{value} : std::nullopt;
}

std::optional<double> PolynomialPickup::voltage(double x, double velocity) const noexcept {
    if (!std::isfinite(x) || !std::isfinite(velocity)) {
        return std::nullopt;
    }
    const double value = -(a1_ + x * (2.0 * a2_ + 3.0 * a3_ * x)) * velocity;
    return std::isfinite(value) ? std::optional<double>{value} : std::nullopt;
}

bool SyntheticResistiveHarp::configure(double branch_r, double load_r) noexcept {
    const double source_r = branch_r * (0.25 + 23.0 / 3.0);
    if (!std::isfinite(branch_r) || branch_r <= 0.0 ||
        !std::isfinite(source_r) || !std::isfinite(load_r) || load_r <= 0.0) {
        return false;
    }
    source_resistance_ohm_ = source_r;
    load_resistance_ohm_ = load_r;
    return true;
}

std::optional<double> SyntheticResistiveHarp::voltage(
    const std::array<double, key_count>& sources) const noexcept {
    double source_voltage = 0.0;
    for (std::size_t i = 0; i < sources.size(); ++i) {
        if (!std::isfinite(sources[i])) {
            return std::nullopt;
        }
        source_voltage += sources[i] / (i < 4 ? 4.0 : 3.0);
    }
    if (!std::isfinite(source_voltage)) {
        return std::nullopt;
    }
    double value = 0.0;
    if (source_resistance_ohm_ <= load_resistance_ohm_) {
        value = source_voltage / (1.0 + source_resistance_ohm_ / load_resistance_ohm_);
    } else {
        // Compute V*Rload/Rsource without overflowing a resistance ratio or
        // underflowing it before multiplication by a large source voltage.
        // Mantissas stay bounded; scalbn applies only the final exponent.
        int voltage_exponent = 0;
        int load_exponent = 0;
        int source_exponent = 0;
        const double voltage_fraction = std::frexp(source_voltage, &voltage_exponent);
        const double load_fraction = std::frexp(load_resistance_ohm_, &load_exponent);
        const double source_fraction = std::frexp(source_resistance_ohm_, &source_exponent);
        const double fraction = voltage_fraction * load_fraction / source_fraction /
            (1.0 + load_resistance_ohm_ / source_resistance_ohm_);
        value = std::scalbn(fraction, voltage_exponent + load_exponent - source_exponent);
    }
    return std::isfinite(value) ? std::optional<double>{value} : std::nullopt;
}

double SyntheticResistiveHarp::source_resistance_ohm() const noexcept {
    return source_resistance_ohm_;
}

ResearchEngine::ResearchEngine() noexcept { (void)prepare(48000.0); }

bool ResearchEngine::prepare(double rate) noexcept {
    if (!valid_sample_rate(rate)) {
        return false;
    }
    std::array<Key, key_count> candidate{};
    for (std::size_t k = 0; k < key_count; ++k) {
        const double midi = static_cast<double>(k) + 28.0;
        const double frequency = 440.0 * std::exp2((midi - 69.0) / 12.0);
        for (std::size_t m = 0; m < modes_per_key; ++m) {
            auto& mode = candidate[k].modes[m];
            if (!mode.configure(
                    {frequency * frequency_ratios[m], damped_decay[m],
                     synthetic_modal_mass_kg}, rate) ||
                !mode.set_decay(free_decay[m]) || !mode.set_decay(damped_decay[m])) {
                return false;
            }
            // Both loss regimes are prepared before publishing the new engine.
            // Later note/pedal changes only select fixed validated coefficients.
        }
    }
    keys_ = candidate;
    sample_rate_hz_ = rate;
    sustain_ = false;
    return true;
}

void ResearchEngine::update_damping(Key& key) noexcept {
    // Deliberate surrogate: instantaneous viscous-loss change, NOT contact.
    const auto& decay = (key.held || sustain_) ? free_decay : damped_decay;
    for (std::size_t m = 0; m < modes_per_key; ++m) {
        (void)key.modes[m].set_decay(decay[m]);
    }
}

bool ResearchEngine::note_on(std::size_t key, double intensity) noexcept {
    if (key >= key_count || !std::isfinite(intensity) || intensity < 0.0 || intensity > 1.0) {
        return false;
    }
    if (intensity == 0.0) {
        return note_off(key);
    }
    Key candidate = keys_[key];
    candidate.held = true;
    update_damping(candidate);
    for (std::size_t m = 0; m < modes_per_key; ++m) {
        const double impulse = synthetic_modal_mass_kg *
            synthetic_max_velocity_change_m_per_s * intensity * strike_coupling[m];
        if (!candidate.modes[m].add_impulse(impulse)) {
            return false;
        }
    }
    keys_[key] = candidate;
    return true;
}

bool ResearchEngine::note_off(std::size_t key) noexcept {
    if (key >= key_count) {
        return false;
    }
    keys_[key].held = false;
    update_damping(keys_[key]);
    return true;
}

void ResearchEngine::set_sustain(bool down) noexcept {
    if (sustain_ == down) { return; }
    sustain_ = down;
    for (auto& key : keys_) {
        update_damping(key);
    }
}

void ResearchEngine::all_notes_off() noexcept {
    for (auto& key : keys_) {
        key.held = false;
        update_damping(key);
    }
}

void ResearchEngine::reset() noexcept {
    sustain_ = false;
    for (auto& key : keys_) {
        key.held = false;
        for (auto& mode : key.modes) {
            mode.reset();
        }
        update_damping(key);
    }
}

bool ResearchEngine::sample(double& volts) noexcept {
    std::array<double, key_count> sources{};
    for (std::size_t k = 0; k < key_count; ++k) {
        double x = 0.0;
        double velocity = 0.0;
        for (std::size_t m = 0; m < modes_per_key; ++m) {
            // Observe state at t; then advance to t + dt. A strike at t therefore
            // has its first electrical sample at the same timestamp.
            const auto state = keys_[k].modes[m].state();
            x += pickup_observation[m] * state.displacement_m;
            velocity += pickup_observation[m] * state.velocity_m_per_s;
            if (!keys_[k].modes[m].advance()) {
                volts = 0.0;
                return false;
            }
        }
        const auto voltage = pickup_.voltage(x, velocity);
        if (!voltage) {
            volts = 0.0;
            return false;
        }
        sources[k] = *voltage;
    }
    const auto voltage = harp_.voltage(sources);
    volts = voltage.value_or(0.0);
    return voltage.has_value();
}

bool ResearchEngine::process(std::span<double> output) noexcept {
    for (std::size_t i = 0; i < output.size(); ++i) {
        if (!sample(output[i])) {
            // Report the fault; fill the remaining caller buffer, never leave
            // uninitialized audio. State is not rolled back after an arithmetic fault.
            for (std::size_t j = i; j < output.size(); ++j) {
                output[j] = 0.0;
            }
            return false;
        }
    }
    return true;
}

std::optional<ModalState> ResearchEngine::modal_state(std::size_t key,
                                                    std::size_t mode) const noexcept {
    if (key >= key_count || mode >= modes_per_key) {
        return std::nullopt;
    }
    return keys_[key].modes[mode].state();
}

double ResearchEngine::mechanical_energy_joules() const noexcept {
    double energy = 0.0;
    for (const auto& key : keys_) {
        for (const auto& mode : key.modes) {
            energy += mode.energy_joules();
        }
    }
    return energy;
}

double ResearchEngine::sample_rate_hz() const noexcept { return sample_rate_hz_; }

} // namespace rhodes
