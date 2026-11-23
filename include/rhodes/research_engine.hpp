#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <span>

namespace rhodes {

inline constexpr double minimum_sample_rate_hz = 32000.0;
inline constexpr double maximum_sample_rate_hz = 192000.0;
inline constexpr std::size_t key_count = 73;
inline constexpr std::size_t modes_per_key = 3;

struct ModalState {
    double displacement_m = 0.0;
    double velocity_m_per_s = 0.0;
};

struct ModalParameters {
    double frequency_hz = 440.0; // Undamped natural frequency.
    double decay_per_s = 1.0;   // beta in q'' + 2 beta q' + omega_0^2 q = 0.
    double mass_kg = 0.001;
};

// Exact state transition of one linear underdamped oscillator. This primitive
// has no hammer/contact model. An impulse is an explicit research excitation.
class ModalOscillator {
public:
    // Successful configuration resets state; rejected input leaves it unchanged.
    [[nodiscard]] bool configure(ModalParameters parameters, double sample_rate_hz) noexcept;
    [[nodiscard]] bool set_decay(double decay_per_s) noexcept; // Preserves q and v.
    [[nodiscard]] bool set_state(ModalState state) noexcept;
    [[nodiscard]] bool add_impulse(double impulse_newton_seconds) noexcept;
    [[nodiscard]] bool advance() noexcept;
    void reset() noexcept;
    [[nodiscard]] ModalState state() const noexcept;
    [[nodiscard]] double energy_joules() const noexcept;

private:
    [[nodiscard]] bool update_transition(double decay_per_s) noexcept;
    [[nodiscard]] bool valid_state(ModalState state) const noexcept;
    ModalParameters parameters_{};
    ModalState state_{};
    double sample_rate_hz_ = 48000.0;
    double omega_0_ = 0.0;
    std::array<double, 4> transition_{};
    bool configured_ = false;
};

// lambda(x) = a1*x + a2*x^2 + a3*x^3, in weber-turns (turns already included).
// This local polynomial is synthetic, with no extrapolation/fidelity guarantee.
class PolynomialPickup {
public:
    [[nodiscard]] bool configure(double a1_weber_per_m, double a2_weber_per_m2,
                                 double a3_weber_per_m3) noexcept;
    [[nodiscard]] std::optional<double> flux_linkage(double displacement_m) const noexcept;
    [[nodiscard]] std::optional<double> voltage(double displacement_m,
                                               double velocity_m_per_s) const noexcept;
private:
    double a1_ = 0.4;
    double a2_ = 800.0;
    double a3_ = 400000.0;
};

// Research topology: one four-pickup parallel group and 23 three-pickup groups
// connected in series. All 73 resistances remain present when sources are zero.
// Purely resistive; no coil inductance, capacitance, controls or back reaction.
// This candidate topology is not yet verified against a selected specimen.
class SyntheticResistiveHarp {
public:
    [[nodiscard]] bool configure(double branch_resistance_ohm,
                                 double load_resistance_ohm) noexcept;
    [[nodiscard]] std::optional<double> voltage(
        const std::array<double, key_count>& source_volts) const noexcept;
    [[nodiscard]] double source_resistance_ohm() const noexcept;
private:
    double source_resistance_ohm_ = 200.0 * (0.25 + 23.0 / 3.0);
    double load_resistance_ohm_ = 1000000.0;
};

// Uncalibrated development engine; constants below are NOT specimen data.
// Fixed 73-key storage, no allocation/locking/I/O in any core operation.
// Sample output is the surrogate loaded harp voltage, in volts.
// Frequency-dependent pickup impedance, antialiasing, reciprocal forces,
// finite-mass hammer contact, shared structure and physical dampers are absent.
class ResearchEngine {
public:
    ResearchEngine() noexcept;
    // Successful prepare resets all mechanics/pedal. Invalid input is atomic.
    [[nodiscard]] bool prepare(double sample_rate_hz) noexcept;
    // key index 0..72 maps to MIDI 28..100 for synthetic equal temperament.
    // intensity 0..1 drives an impulse surrogate, NOT measured hammer velocity.
    // Zero intensity is release; retriggers preserve displacement and velocity.
    [[nodiscard]] bool note_on(std::size_t key, double intensity) noexcept;
    [[nodiscard]] bool note_off(std::size_t key) noexcept;
    void set_sustain(bool down) noexcept;
    void all_notes_off() noexcept; // Release keys; sustain still applies.
    void reset() noexcept;        // Explicit hard reset, never used by note-on/off.
    // Observe at t, then advance to t+dt. On arithmetic failure, returns false
    // and zeros the failed sample plus the remaining buffer. Earlier samples
    // and partially advanced state are retained; reset before resuming after
    // such a fault. No rollback or silent clipping is performed.
    [[nodiscard]] bool process(std::span<double> output_volts) noexcept;
    [[nodiscard]] std::optional<ModalState> modal_state(std::size_t key,
                                                      std::size_t mode) const noexcept;
    [[nodiscard]] double mechanical_energy_joules() const noexcept;
    [[nodiscard]] double sample_rate_hz() const noexcept;

private:
    struct Key {
        std::array<ModalOscillator, modes_per_key> modes{};
        bool held = false;
    };
    void update_damping(Key& key) noexcept;
    [[nodiscard]] bool sample(double& volts) noexcept;
    std::array<Key, key_count> keys_{};
    PolynomialPickup pickup_{};
    SyntheticResistiveHarp harp_{};
    double sample_rate_hz_ = 48000.0;
    bool sustain_ = false;
};

} // namespace rhodes
