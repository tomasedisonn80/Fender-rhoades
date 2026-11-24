#pragma once

#include "rhodes/control_value.hpp"
#include "rhodes/front_panel_state.hpp"
#include "rhodes/research_engine.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numbers>

namespace rhodes {

struct StereoSample {
    double left = 0.0;
    double right = 0.0;
};

// Synthetic digital tone and stereo amplitude modulation. The photographed
// "Vibrato" label names this amplitude effect; it does not modulate pitch.
// These choices are not an identified or calibrated Peterson/Janus circuit.
//
// Numerical recipe (fs in Hz, u a normalized control):
//   shelf gain G(u) = 10^((24*u - 12)/20), exactly unity at u = 0.5;
//   g = tan(pi*fc/fs), a = g/(1+g), with fc = 200 or 2500 Hz;
//   low[n] = a*x[n] + (1-a)*s[n];
//   s[n+1] = 2*a*x[n] + (1-2*a)*s[n];
//   bass: y = x + (G_bass-1)*low_200(x);
//   treble, after bass: y = x + (G_treble-1)*(x-low_2500(x)).
// Each low-pass has transfer g*(1+z^-1)/((1+g)+(g-1)*z^-1).
// Thus bass has its requested gain at DC and unity at Nyquist; treble has
// unity at DC and its requested gain at Nyquist. The chosen frequencies are
// the low-pass split frequencies, not gain-independent shelf midpoints.
// Bilinear/prewarping background (no third-party code is incorporated):
// https://www.mathworks.com/help/signal/ref/bilinear.html
//
//   speed = 0.5 + 11.5*u Hz; depth = vibrato ? intensity : 0;
//   left gain  = 1 - depth*(1 + sin(2*pi*phase))/2;
//   right gain = 1 - depth*(1 - sin(2*pi*phase))/2.
// Phase is observed before advancing by speed/fs and runs while bypassed.
// The mono fold-down therefore has gain 1-depth/2 without cyclic modulation.
//
// Gain, depth and speed changes ramp linearly from their current values over
// ceil(0.010*fs) samples, including the first changed sample. The last sample
// snaps to the exact target. A new target restarts only its own ramp. Reset
// applies controls immediately. At settled neutral tone and zero effective
// depth, process returns the original input bits, including either zero sign.
// Neutral shelf histories are cleared; enabling a shelf starts its history
// from zero while its gain ramps. There is no extra frame delay or lookahead.
//
// Fixed storage, no allocation, locks or I/O. All processing paths have a
// constant operation bound. Standard-library sin is used for active modulation;
// pow is evaluated only when a tone target changes, tan only in prepare.
// Native floating-point rounding/underflow applies to audio arithmetic.
class FrontPanelEffects {
public:
    FrontPanelEffects() noexcept { (void)prepare(48000.0); }

    // Same 32000..192000 Hz domain as the synthesis engine. Success resets to
    // default controls; any rejected rate leaves every live member unchanged.
    [[nodiscard]] bool prepare(double sample_rate_hz) noexcept {
        if (!std::isfinite(sample_rate_hz) ||
            sample_rate_hz < minimum_sample_rate_hz ||
            sample_rate_hz > maximum_sample_rate_hz) {
            return false;
        }
        const double bass = split_coefficient(200.0, sample_rate_hz);
        const double treble = split_coefficient(2500.0, sample_rate_hz);
        if (!valid_split(bass) || !valid_split(treble)) { return false; }
        const auto frames = static_cast<std::uint32_t>(std::ceil(0.010 * sample_rate_hz));
        sample_rate_hz_ = sample_rate_hz;
        bass_coefficient_ = bass;
        treble_coefficient_ = treble;
        ramp_frames_ = frames;
        reset();
        return true;
    }

    // Clears history, ramps, phase and the fault latch. Invalid controls latch
    // a fault instead of silently substituting a usable configuration.
    void reset(FrontPanelState state = {}) noexcept {
        bass_history_ = 0.0;
        treble_history_ = 0.0;
        phase_ = 0.0;
        faulted_ = !valid_front_panel_state(state);
        if (faulted_) { return; }
        controls_ = state;
        bass_gain_.reset(shelf_gain(state.bass));
        treble_gain_.reset(shelf_gain(state.treble));
        depth_.reset(effective_depth(state));
        speed_.reset(speed_hz(state.speed));
    }

    // Invalid controls, nonfinite input, or nonfinite arithmetic return NaN in
    // both channels and latch failure until valid reset/successful prepare.
    // Callers must expose that failure through their existing audio-fault path.
    // There is no clipping, zero substitution or continued healthy-looking
    // bypass after a fault; an invalid prepare never clears the latch.
    [[nodiscard]] StereoSample process(double input, FrontPanelState state) noexcept {
        if (faulted_ || !std::isfinite(input) || !valid_front_panel_state(state)) {
            return fail();
        }
        update_targets(state);
        const double bass_gain = bass_gain_.advance();
        const double treble_gain = treble_gain_.advance();
        const double depth = depth_.advance();
        const double speed = speed_.advance();

        double tone = input;
        if (bass_gain != 1.0 || bass_gain_.remaining != 0) {
            const double low = split(tone, bass_coefficient_, bass_history_);
            tone = std::fma(bass_gain - 1.0, low, tone);
        } else {
            bass_history_ = 0.0;
        }
        if (treble_gain != 1.0 || treble_gain_.remaining != 0) {
            const double low = split(tone, treble_coefficient_, treble_history_);
            tone = std::fma(treble_gain - 1.0, tone - low, tone);
        } else {
            treble_history_ = 0.0;
        }

        StereoSample output{tone, tone};
        if (depth != 0.0) {
            const double wave = std::sin(2.0 * std::numbers::pi * phase_);
            output.left *= 1.0 - 0.5 * depth * (1.0 + wave);
            output.right *= 1.0 - 0.5 * depth * (1.0 - wave);
        }
        phase_ += speed / sample_rate_hz_;
        if (phase_ >= 1.0) { phase_ -= 1.0; }

        if (!std::isfinite(bass_history_) || !std::isfinite(treble_history_) ||
            !std::isfinite(output.left) || !std::isfinite(output.right)) {
            return fail();
        }
        return output;
    }

    [[nodiscard]] double sample_rate_hz() const noexcept { return sample_rate_hz_; }
    [[nodiscard]] std::uint32_t smoothing_frames() const noexcept { return ramp_frames_; }
    [[nodiscard]] bool requires_reset() const noexcept { return faulted_; }
    [[nodiscard]] static constexpr std::size_t latency_frames() noexcept { return 0; }

private:
    struct Ramp {
        double value = 0.0;
        double target = 0.0;
        double increment = 0.0;
        std::uint32_t remaining = 0;

        void reset(double next) noexcept {
            value = next;
            target = next;
            increment = 0.0;
            remaining = 0;
        }
        void set_target(double next, std::uint32_t frames) noexcept {
            if (next == target) { return; }
            target = next;
            increment = (next - value) / static_cast<double>(frames);
            remaining = frames;
        }
        [[nodiscard]] double advance() noexcept {
            if (remaining != 0) {
                --remaining;
                value = remaining == 0 ? target : value + increment;
            }
            return value;
        }
    };

    [[nodiscard]] static bool same_control(float first, float second) noexcept {
        return front_panel_detail::control_bits(first) == front_panel_detail::control_bits(second);
    }
    [[nodiscard]] static double shelf_gain(float normalized) noexcept {
        if (front_panel_detail::control_bits(normalized) == 0x3f000000U) { return 1.0; }
        return std::pow(10.0, (24.0 * *control::widen(normalized) - 12.0) / 20.0);
    }
    [[nodiscard]] static double speed_hz(float normalized) noexcept {
        return 0.5 + 11.5 * *control::widen(normalized);
    }
    [[nodiscard]] static double effective_depth(FrontPanelState state) noexcept {
        return state.vibrato ? *control::widen(state.intensity) : 0.0;
    }
    [[nodiscard]] static double split_coefficient(double hz, double fs) noexcept {
        const double g = std::tan(std::numbers::pi * hz / fs);
        return g / (1.0 + g);
    }
    [[nodiscard]] static bool valid_split(double coefficient) noexcept {
        // This domain also makes both state-update weights nonnegative.
        return std::isfinite(coefficient) && coefficient > 0.0 && coefficient < 0.5;
    }
    [[nodiscard]] static double split(double input, double coefficient,
                                      double& history) noexcept {
        const double low = coefficient * input + (1.0 - coefficient) * history;
        history = (2.0 * coefficient) * input + (1.0 - 2.0 * coefficient) * history;
        return low;
    }
    void update_targets(FrontPanelState state) noexcept {
        if (!same_control(state.bass, controls_.bass)) {
            bass_gain_.set_target(shelf_gain(state.bass), ramp_frames_);
        }
        if (!same_control(state.treble, controls_.treble)) {
            treble_gain_.set_target(shelf_gain(state.treble), ramp_frames_);
        }
        if (!same_control(state.intensity, controls_.intensity) ||
            state.vibrato != controls_.vibrato) {
            depth_.set_target(effective_depth(state), ramp_frames_);
        }
        if (!same_control(state.speed, controls_.speed)) {
            speed_.set_target(speed_hz(state.speed), ramp_frames_);
        }
        controls_ = state;
    }
    [[nodiscard]] StereoSample fail() noexcept {
        faulted_ = true;
        const double nan = std::numeric_limits<double>::quiet_NaN();
        return {nan, nan};
    }

    double sample_rate_hz_ = 48000.0;
    double bass_coefficient_ = 0.0;
    double treble_coefficient_ = 0.0;
    double bass_history_ = 0.0;
    double treble_history_ = 0.0;
    double phase_ = 0.0;
    Ramp bass_gain_{};
    Ramp treble_gain_{};
    Ramp depth_{};
    Ramp speed_{};
    FrontPanelState controls_{};
    std::uint32_t ramp_frames_ = 480;
    bool faulted_ = false;
};

} // namespace rhodes
