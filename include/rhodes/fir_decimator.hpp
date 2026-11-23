#pragma once

#include "rhodes/audio_filter_coefficients.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <optional>
#include <span>

namespace rhodes {

// Fixed-storage FIR. The caller owns decimation phase: ring wrap is not a
// phase clock because the odd tap count is not divisible by the rate factor.
class AudioFir {
public:
    [[nodiscard]] bool prepare(std::size_t factor) noexcept {
        std::span<const double> candidate;
        switch (factor) {
        case 1: candidate = audio_filter::coefficients_l1; break;
        case 2: candidate = audio_filter::coefficients_l2; break;
        case 4: candidate = audio_filter::coefficients_l4; break;
        default: return false;
        }
        coefficients_ = candidate;
        reset();
        return true;
    }

    void reset() noexcept {
        history_.fill(0.0);
        next_ = 0;
    }

    [[nodiscard]] bool push(double sample) noexcept {
        if (!std::isfinite(sample)) { return false; }
        history_[next_] = sample;
        if (++next_ == coefficients_.size()) { next_ = 0; }
        return true;
    }

    [[nodiscard]] std::optional<double> output() const noexcept {
        const std::size_t count = coefficients_.size();
        const std::size_t middle = count / 2;
        double sum = coefficients_[middle] * past(middle);
        for (std::size_t i = 0; i < middle; ++i) {
            sum += coefficients_[i] * (past(i) + past(count - 1 - i));
        }
        return std::isfinite(sum) ? std::optional<double>{sum} : std::nullopt;
    }

    [[nodiscard]] std::span<const double> coefficients() const noexcept {
        return coefficients_;
    }

private:
    [[nodiscard]] double past(std::size_t age) const noexcept {
        const std::size_t count = coefficients_.size();
        const std::size_t newest = next_ == 0 ? count - 1 : next_ - 1;
        const std::size_t index = newest >= age ? newest - age : count - (age - newest);
        return history_[index];
    }

    std::array<double, 385> history_{};
    std::span<const double> coefficients_{audio_filter::coefficients_l4};
    std::size_t next_ = 0;
};

} // namespace rhodes
