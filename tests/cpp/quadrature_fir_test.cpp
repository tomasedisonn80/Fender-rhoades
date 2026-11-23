#include "rhodes/quadrature_fir.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <numbers>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
namespace filter = rhodes::quadrature_filter;
using Cell = rhodes::QuadratureFir::Cell;

void require(bool condition, std::string_view message) {
    if (!condition) { throw std::runtime_error(std::string{message}); }
}

double output(const rhodes::QuadratureFir& fir) {
    const auto result = fir.output();
    require(result.has_value(), "ordinary quadrature FIR stream faulted");
    return *result;
}

// Ordinary chronological convolution, accumulated in a different order and
// precision from the implementation. completed excludes the current cell:
// output for frame n sees observations from frames 0 through n-1 only.
long double direct(std::span<const Cell> completed) {
    const std::size_t count = std::min(completed.size(), filter::history_frames);
    long double sum = 0.0L;
    for (std::size_t frame = completed.size() - count; frame < completed.size(); ++frame) {
        const std::size_t age = completed.size() - 1 - frame;
        for (std::size_t node = 0; node < filter::node_count; ++node) {
            sum += static_cast<long double>(completed[frame][node]) *
                   static_cast<long double>(filter::coefficients[node][age]);
        }
    }
    return sum;
}

Cell distinct_cell(std::size_t frame) {
    Cell cell{};
    for (std::size_t node = 0; node < cell.size(); ++node) {
        const double time = static_cast<double>(frame);
        const double phase = static_cast<double>(node);
        cell[node] = 0.4 * std::sin(0.113 * time + 0.37 * phase) +
                     0.15 * std::cos(0.031 * time - 0.29 * phase);
    }
    return cell;
}

void qfir_convolution() {
    rhodes::QuadratureFir fir;
    constexpr std::size_t active = 5 * filter::history_frames + 17;
    std::vector<Cell> cells(active + filter::history_frames + 3);
    std::uint32_t seed = 0x51783bafU;
    for (std::size_t frame = 0; frame < active; ++frame) {
        cells[frame] = distinct_cell(frame);
        for (double& value : cells[frame]) {
            seed = seed * 1664525U + 1013904223U;
            value += 0.3 * (static_cast<double>(seed >> 8U) / 16777216.0 - 0.5);
        }
    }
    long double maximum_error = 0.0L;
    for (std::size_t frame = 0; frame < cells.size(); ++frame) {
        const double actual = output(fir);
        const auto completed = std::span<const Cell>{cells}.first(frame);
        const long double error = std::abs(static_cast<long double>(actual) - direct(completed));
        maximum_error = std::max(maximum_error, error);
        require(error <= 2e-14L, "quadrature FIR differs from chronological convolution");
        if (frame == 0 || frame >= active + filter::history_frames) {
            require(actual == 0.0, "quadrature FIR startup or expired tail is nonzero");
        }
        require(fir.push(cells[frame]), "finite convolution cell rejected");
    }

    // Uniform observations must settle to DC itself, including after wraps.
    for (const double level : {1.0, -0.75}) {
        fir.reset();
        Cell cell{};
        cell.fill(level);
        for (std::size_t frame = 0; frame < 4 * filter::history_frames; ++frame) {
            require(fir.push(cell), "finite DC cell rejected");
            if (frame + 1 >= filter::history_frames) {
                require(std::abs(output(fir) - level) <= 5e-15,
                        "settled quadrature FIR DC output differs from its input");
            }
        }
    }
    std::cout << "maximum chronological-convolution absolute error="
              << static_cast<double>(maximum_error) << '\n';
}

void qfir_impulses() {
    // Exercise every node at every circular insertion position independently.
    // A branch impulse verifies orientation/support, not combined group delay.
    for (std::size_t node = 0; node < filter::node_count; ++node) {
        for (std::size_t onset = 0; onset < filter::history_frames; ++onset) {
            rhodes::QuadratureFir fir;
            require(output(fir) == 0.0, "new quadrature FIR is not silent");
            for (std::size_t frame = 0; frame < onset; ++frame) {
                require(fir.push(Cell{}), "zero warmup cell rejected");
                require(output(fir) == 0.0, "zero warmup changed FIR output");
            }
            Cell impulse{};
            impulse[node] = 1.0;
            require(fir.push(impulse), "unit phase impulse rejected");
            for (std::size_t age = 0; age < filter::history_frames + 2; ++age) {
                const double expected = age < filter::history_frames ?
                    filter::coefficients[node][age] : 0.0;
                require(output(fir) == expected,
                        "phase impulse tap, ring position, or finite tail differs");
                require(fir.push(Cell{}), "zero impulse-tail cell rejected");
            }
            require(output(fir) == 0.0, "phase impulse survived finite support");
        }
    }
}

void qfir_delay() {
    require(filter::latency_frames == 48, "quadrature FIR latency contract changed");
    constexpr long double delay = 48.0L;
    constexpr long double initial_phase = 0.237L;
    long double maximum_error = 0.0L;
    for (const long double cycles : {0.013L, 0.071L, 0.217L, 0.401L}) {
        const long double omega = 2.0L * std::numbers::pi_v<long double> * cycles;
        // Mirror taps at physical lags k+1-a[j] give a real response around
        // 48 frames. Compute its amplitude directly; fit neither phase nor gain.
        long double gain = 0.0L;
        long double phase_residual = 0.0L;
        for (std::size_t node = 0; node < filter::node_count; ++node) {
            for (std::size_t age = 0; age < filter::history_frames; ++age) {
                const long double centered_lag = static_cast<long double>(age) + 1.0L -
                    static_cast<long double>(filter::nodes[node]) - delay;
                const long double coefficient = filter::coefficients[node][age];
                gain += coefficient * std::cos(omega * centered_lag);
                phase_residual += coefficient * std::sin(omega * centered_lag);
            }
        }
        require(std::abs(phase_residual) <= 2e-14L,
                "combined fractional-node response lacks 48-frame symmetry");
        rhodes::QuadratureFir fir;
        long double wrong_delay_error = 0.0L;
        for (std::size_t frame = 0; frame < 5 * filter::history_frames; ++frame) {
            const long double time = static_cast<long double>(frame);
            if (frame >= filter::history_frames) {
                const long double actual = output(fir);
                const long double expected = gain * std::sin(omega * (time - delay) + initial_phase);
                const long double error = std::abs(actual - expected);
                maximum_error = std::max(maximum_error, error);
                require(error <= 2e-13L,
                        "fractional-node sinusoid does not retain 48-frame phase");
                const long double wrong = gain * std::sin(omega * (time - delay - 1.0L) + initial_phase);
                wrong_delay_error = std::max(wrong_delay_error, std::abs(actual - wrong));
            }
            Cell cell{};
            for (std::size_t node = 0; node < cell.size(); ++node) {
                cell[node] = static_cast<double>(std::sin(omega *
                    (time + static_cast<long double>(filter::nodes[node])) + initial_phase));
            }
            require(fir.push(cell), "finite fractional-node sinusoid rejected");
        }
        require(wrong_delay_error > 0.05L,
                "sinusoid fixture cannot distinguish an extra frame of delay");
    }
    std::cout << "maximum fixed-48-frame sinusoid absolute error="
              << static_cast<double>(maximum_error) << '\n';
}

void qfir_rejection() {
    // Test before and at a wrap, with each invalid node late enough in the cell
    // to expose partially committed earlier branches. Check the entire future
    // tail as well as immediate output, so a changed cursor cannot escape.
    for (const std::size_t warmup : {2 * filter::history_frames - 1,
                                    2 * filter::history_frames}) {
        rhodes::QuadratureFir prepared;
        for (std::size_t frame = 0; frame < warmup; ++frame) {
            require(prepared.push(distinct_cell(frame)), "rejection warmup failed");
        }
        for (std::size_t node = 0; node < filter::node_count; ++node) {
            for (const double invalid : {std::numeric_limits<double>::quiet_NaN(),
                                         std::numeric_limits<double>::infinity(),
                                         -std::numeric_limits<double>::infinity()}) {
                auto actual = prepared;
                auto unchanged = prepared;
                Cell rejected{};
                rejected.fill(123.0);
                rejected[node] = invalid;
                require(!actual.push(rejected), "nonfinite quadrature cell accepted");
                require(output(actual) == output(unchanged),
                        "rejected cell changed immediate quadrature output");
                for (std::size_t frame = 0; frame < 2 * filter::history_frames + 1; ++frame) {
                    const Cell cell = distinct_cell(warmup + frame);
                    require(actual.push(cell) && unchanged.push(cell),
                            "finite continuation after rejection failed");
                    require(output(actual) == output(unchanged),
                            "rejected cell changed branch history or insertion cursor");
                }
            }
        }
    }
}

void qfir_overflow() {
    rhodes::QuadratureFir fir;
    std::vector<Cell> cells(filter::history_frames);
    // Align each historical value with its coefficient sign. Every individual
    // input is finite, while the final positive sum exceeds double range.
    const double large = 0.8 * std::numeric_limits<double>::max();
    long double absolute_gain = 0.0L;
    for (std::size_t frame = 0; frame < cells.size(); ++frame) {
        const std::size_t age = cells.size() - 1 - frame;
        for (std::size_t node = 0; node < filter::node_count; ++node) {
            const double coefficient = filter::coefficients[node][age];
            absolute_gain += std::abs(static_cast<long double>(coefficient));
            cells[frame][node] = std::copysign(large, coefficient);
        }
        require(fir.push(cells[frame]), "finite extreme quadrature cell rejected");
    }
    // This dimensionless check also works where long double shares double's
    // exponent range, without relying on an overflowing reference calculation.
    require(absolute_gain > static_cast<long double>(std::numeric_limits<double>::max()) / large,
            "overflow fixture does not exceed finite output range");
    require(!fir.output().has_value(), "quadrature overflow returned a finite value");
    require(!fir.output().has_value(), "reading overflow output changed its history");
    Cell rejected{};
    rejected.back() = std::numeric_limits<double>::quiet_NaN();
    require(!fir.push(rejected), "nonfinite cell accepted after overflow");
    require(!fir.output().has_value(), "invalid cell erased overflow history");

    fir.reset();
    require(output(fir) == 0.0, "reset failed to clear quadrature overflow history");
    for (std::size_t frame = 0; frame < cells.size(); ++frame) {
        cells[frame] = distinct_cell(frame);
        require(fir.push(cells[frame]), "finite recovery cell rejected");
        const auto completed = std::span<const Cell>{cells}.first(frame + 1);
        require(std::abs(static_cast<long double>(output(fir)) - direct(completed)) <= 2e-14L,
                "quadrature reset did not recover ordinary convolution");
    }
}
} // namespace

int main(int argc, char** argv) {
    try {
        const std::string_view selected = argc > 1 ? argv[1] : "all";
        bool found = false;
        const auto run = [&](std::string_view name, auto test) {
            if (selected == "all" || selected == name) {
                test();
                found = true;
                std::cout << "PASS " << name << '\n';
            }
        };
        run("qfir_convolution", qfir_convolution);
        run("qfir_impulses", qfir_impulses);
        run("qfir_delay", qfir_delay);
        run("qfir_rejection", qfir_rejection);
        run("qfir_overflow", qfir_overflow);
        require(found, "unknown quadrature FIR fixture");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
