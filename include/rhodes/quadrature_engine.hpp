#pragma once

#include "rhodes/quadrature_fir.hpp"
#include "rhodes/research_engine.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <optional>
#include <span>

namespace rhodes {

// A separate synthetic audio approximation. Each host cell uses six prepared
// fractional observations of the same mechanical state, followed by the
// unchanged core's one-frame boundary advance. These are nonuniform quadrature
// nodes, not a uniformly oversampled signal. No physical fidelity is implied.
// Static engine substitution permits deterministic observation/advance faults.
template<class Engine>
class BasicQuadratureEngine {
public:
    BasicQuadratureEngine() noexcept { (void)prepare(48000.0); }

    [[nodiscard]] bool prepare(double host_rate) noexcept {
        if (!std::isfinite(host_rate) || host_rate < minimum_sample_rate_hz ||
            host_rate > maximum_sample_rate_hz) {
            return false;
        }
        Engine candidate;
        if (!candidate.prepare(host_rate)) { return false; }
        FractionalObservationPlan candidate_plan;
        if (!candidate.prepare_observation_plan(quadrature_filter::nodes, candidate_plan)) {
            return false;
        }
        // All fallible preparation precedes publication. Resetting the fixed
        // FIR cannot fail, and rejected input retains every previous cell.
        engine_ = candidate;
        plan_ = candidate_plan;
        filter_.reset();
        host_rate_hz_ = host_rate;
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
        // The prepared, rate-bound observation plan is immutable during audio
        // processing and survives explicit reset and host configuration recall.
    }

    // Events occur at boundary n before this call. y[n] uses only completed
    // cells. Observe n+a[j], advance to n+1, and retain that cell for later
    // outputs. Even y[n] stays provisional until every operation succeeds.
    // On fault, earlier frames remain valid; the failed frame and suffix are
    // zero. Partial mechanical advance requires explicit reset before resuming,
    // as enforced by BasicEventProcessor when this engine is used in a host.
    [[nodiscard]] bool process(std::span<double> output_volts) noexcept {
        QuadratureFir::Cell cell{};
        std::array<double, 1> discarded_endpoint{};
        for (std::size_t frame = 0; frame < output_volts.size(); ++frame) {
            const auto value = filter_.output();
            bool ok = value.has_value();
            if (ok) {
                const auto observations = engine_.observe_cell(plan_);
                ok = observations.has_value();
                if (ok) {
                    cell = *observations;
                    for (const double observation : cell) {
                        if (!std::isfinite(observation)) { ok = false; break; }
                    }
                }
            }
            if (ok) {
                ok = engine_.process(discarded_endpoint) &&
                     std::isfinite(discarded_endpoint[0]);
            }
            if (ok) { ok = filter_.push(cell); }
            if (!ok) {
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
    [[nodiscard]] static constexpr std::size_t node_count() noexcept {
        return quadrature_filter::node_count;
    }
    [[nodiscard]] static constexpr std::size_t latency_frames() noexcept {
        return quadrature_filter::latency_frames;
    }

private:
    static_assert(observation_node_count == quadrature_filter::node_count);
    Engine engine_{};
    FractionalObservationPlan plan_{};
    QuadratureFir filter_{};
    double host_rate_hz_ = 48000.0;
};

using QuadratureEngine = BasicQuadratureEngine<ResearchEngine>;

} // namespace rhodes
