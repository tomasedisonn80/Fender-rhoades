#pragma once

#include "rhodes/quadrature_filter_coefficients.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <optional>

namespace rhodes {

// Six fixed FIR branches for completed host-frame cells. A cell contains the
// source at forward fractions a[j]. Its age-k contribution uses K(k+1-a[j]).
// The caller emits output before pushing the current cell; this preserves the
// continuous kernel's 48-frame center without introducing another frame.
class QuadratureFir {
public:
    using Cell = std::array<double, quadrature_filter::node_count>;

    void reset() noexcept {
        for (auto& phase : history_) { phase.fill(0.0); }
        next_ = 0;
    }

    // Validate the complete cell before replacing any branch or ring position.
    [[nodiscard]] bool push(const Cell& cell) noexcept {
        for (const double value : cell) {
            if (!std::isfinite(value)) { return false; }
        }
        for (std::size_t node = 0; node < quadrature_filter::node_count; ++node) {
            history_[node][next_] = cell[node];
        }
        if (++next_ == quadrature_filter::history_frames) { next_ = 0; }
        return true;
    }

    [[nodiscard]] std::optional<double> output() const noexcept {
#if defined(__clang__)
        // Keep the fixed reduction identical at different inlined call sites.
        // Optional multiply/add fusion must not change the continuation bits
        // of a cloned engine. Node and tap order remain unchanged.
#pragma clang fp contract(off)
#endif
        constexpr auto count = quadrature_filter::history_frames;
        const std::size_t newest = next_ == 0 ? count - 1 : next_ - 1;
        double sum = 0.0;
        for (std::size_t node = 0; node < quadrature_filter::node_count; ++node) {
            for (std::size_t age = 0; age < count; ++age) {
                const std::size_t index = newest >= age ? newest - age :
                    count - (age - newest);
                sum += quadrature_filter::coefficients[node][age] * history_[node][index];
            }
        }
        return std::isfinite(sum) ? std::optional<double>{sum} : std::nullopt;
    }

private:
    std::array<std::array<double, quadrature_filter::history_frames>,
               quadrature_filter::node_count> history_{};
    std::size_t next_ = 0;
};

} // namespace rhodes
