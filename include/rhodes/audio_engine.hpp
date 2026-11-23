#pragma once

#include "rhodes/fir_decimator.hpp"
#include "rhodes/research_engine.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <optional>
#include <span>

namespace rhodes {

// The original synthetic physics runs at an integer multiple of host rate.
// Its nonlinear pickup is followed by a causal FIR and substep-zero decimation.
// This reduces sampled aliasing; it is not a measured instrument model or a
// guarantee covering every transient and internal-rate spectral component.
// Static substitution permits deterministic arithmetic-fault tests.
template<class Engine>
class BasicAudioEngine {
public:
    BasicAudioEngine() noexcept { (void)prepare(48000.0); }

    [[nodiscard]] bool prepare(double host_rate) noexcept {
        if (!std::isfinite(host_rate) || host_rate < minimum_sample_rate_hz ||
            host_rate > maximum_sample_rate_hz) {
            return false;
        }
        const std::size_t factor = host_rate <= 48000.0 ? 4U :
                                   (host_rate <= 96000.0 ? 2U : 1U);
        Engine candidate;
        if (!candidate.prepare(host_rate * static_cast<double>(factor))) {
            return false;
        }
        AudioFir candidate_filter;
        if (!candidate_filter.prepare(factor)) { return false; }
        engine_ = candidate;
        filter_ = candidate_filter;
        host_rate_hz_ = host_rate;
        factor_ = factor;
        return true;
    }

    [[nodiscard]] bool note_on(std::size_t key, double intensity) noexcept {
        return engine_.note_on(key, intensity);
    }
    [[nodiscard]] bool note_off(std::size_t key) noexcept { return engine_.note_off(key); }
    void set_sustain(bool down) noexcept { engine_.set_sustain(down); }
    void all_notes_off() noexcept { engine_.all_notes_off(); }
    void reset() noexcept {
        engine_.reset();
        filter_.reset();
    }

    // Events occur before internal sample L*n. Retain that substep's filtered
    // value, then consume the remaining L-1 samples before publishing it. The
    // mechanical state ends at host time n+1; output has 48 frames group delay.
    // On fault, the failed host sample and suffix are zero. Mechanics/history
    // may have advanced partially; explicit reset is required before resuming.
    [[nodiscard]] bool process(std::span<double> output_volts) noexcept {
        std::array<double, 4> internal{};
        for (std::size_t frame = 0; frame < output_volts.size(); ++frame) {
            bool ok = engine_.process(std::span<double>(internal.data(), factor_));
            std::optional<double> value;
            if (ok) {
                for (std::size_t substep = 0; substep < factor_; ++substep) {
                    if (!filter_.push(internal[substep])) { ok = false; break; }
                    if (substep == 0) {
                        value = filter_.output();
                        if (!value) { ok = false; break; }
                    }
                }
            }
            if (!ok || !value) {
                std::fill(output_volts.begin() + static_cast<std::ptrdiff_t>(frame),
                          output_volts.end(), 0.0);
                return false;
            }
            output_volts[frame] = *value;
        }
        return true;
    }

    [[nodiscard]] std::optional<ModalState> modal_state(std::size_t key,
                                                      std::size_t mode) const noexcept {
        return engine_.modal_state(key, mode);
    }
    [[nodiscard]] double mechanical_energy_joules() const noexcept {
        return engine_.mechanical_energy_joules();
    }
    [[nodiscard]] double sample_rate_hz() const noexcept { return host_rate_hz_; }
    [[nodiscard]] double internal_sample_rate_hz() const noexcept {
        return engine_.sample_rate_hz();
    }
    [[nodiscard]] std::size_t oversampling_factor() const noexcept { return factor_; }
    [[nodiscard]] static constexpr std::size_t latency_frames() noexcept { return 48; }

private:
    Engine engine_{};
    AudioFir filter_{};
    double host_rate_hz_ = 48000.0;
    std::size_t factor_ = 4;
};

using AudioEngine = BasicAudioEngine<ResearchEngine>;

} // namespace rhodes
