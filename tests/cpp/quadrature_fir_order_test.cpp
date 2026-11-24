#include "rhodes/quadrature_fir.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <optional>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
namespace filter = rhodes::quadrature_filter;
using Cell = rhodes::QuadratureFir::Cell;

static_assert(std::numeric_limits<double>::is_iec559 &&
              std::numeric_limits<double>::digits == 53 &&
              std::numeric_limits<double>::max_exponent == 1024);
static_assert(filter::history_frames == 96 && filter::node_count == 6);

void require(bool condition, std::string_view message) {
    if (!condition) { throw std::runtime_error(std::string{message}); }
}

// A chronological history has no circular cursor or split-index expression.
// Visit every tap, including startup zeros, in the specified node/age order.
// Volatile stores force separate binary64 product/add roundings in this oracle.
// Build this arithmetic-order test with contraction disabled for the production
// header too (for example, target-only -ffp-contract=off on GCC). Compatibility
// under native/default contraction requires a separate original-traversal replay;
// an oracle mismatch alone does not establish a new production regression.
std::optional<double> chronological_output(std::span<const Cell> completed) {
    double sum = 0.0;
    for (std::size_t node = 0; node < filter::node_count; ++node) {
        for (std::size_t age = 0; age < filter::history_frames; ++age) {
            const double value = age < completed.size() ?
                completed[completed.size() - 1 - age][node] : 0.0;
            volatile double product = filter::coefficients[node][age] * value;
            volatile double rounded = sum + product;
            sum = rounded;
        }
    }
    return std::isfinite(sum) ? std::optional<double>{sum} : std::nullopt;
}

bool same_output(std::optional<double> actual, std::optional<double> expected) {
    return actual.has_value() == expected.has_value() &&
        (!actual || std::bit_cast<std::uint64_t>(*actual) ==
                    std::bit_cast<std::uint64_t>(*expected));
}

void print_output(std::ostream& stream, std::optional<double> value) {
    if (value) {
        stream << std::hexfloat << *value << " [0x" << std::hex
               << std::bit_cast<std::uint64_t>(*value) << ']';
    } else {
        stream << "nullopt";
    }
}

void check_output(const rhodes::QuadratureFir& fir, std::optional<double> expected,
                  std::string_view phase, std::size_t rotation, std::size_t frame,
                  std::size_t& reads) {
    for (std::size_t read = 0; read < 2; ++read) {
        const auto actual = fir.output();
        ++reads;
        if (!same_output(actual, expected)) {
            std::ostringstream message;
            message << phase << " rotation=" << rotation << " frame=" << frame
                    << " read=" << read << " actual=";
            print_output(message, actual);
            message << " expected=";
            print_output(message, expected);
            throw std::runtime_error(message.str());
        }
    }
}

// Construct finite values by their binary64 bits so fixture generation itself
// cannot change with multiply/add contraction or the host's transcendental math.
Cell distinct_cell(std::size_t frame) {
    Cell cell{};
    for (std::size_t node = 0; node < cell.size(); ++node) {
        std::uint64_t bits = 0x9e3779b97f4a7c15ULL *
            (static_cast<std::uint64_t>(frame) * filter::node_count + node + 1);
        bits = (bits ^ (bits >> 30U)) * 0xbf58476d1ce4e5b9ULL;
        bits = (bits ^ (bits >> 27U)) * 0x94d049bb133111ebULL;
        bits ^= bits >> 31U;
        const std::uint64_t exponent = 1013U + ((bits >> 52U) & 15U);
        cell[node] = std::bit_cast<double>((bits & 0x800fffffffffffffULL) |
                                          (exponent << 52U));
    }
    return cell;
}

std::array<Cell, filter::history_frames> cancelling_history() {
    std::array<Cell, filter::history_frames> cells{};
    for (std::size_t frame = 0; frame < cells.size() / 2; ++frame) {
        cells[frame] = distinct_cell(frame);
        for (std::size_t node = 0; node < filter::node_count; ++node) {
            cells[cells.size() - 1 - frame][filter::node_count - 1 - node] =
                -cells[frame][node];
        }
    }
    return cells;
}

void qfir_ordered_history() {
    constexpr auto count = filter::history_frames;
    std::array<rhodes::QuadratureFir, count> rotated{};
    std::vector<Cell> completed;
    completed.reserve(2 * count + 18);
    std::size_t reads = 0;
    for (std::size_t rotation = 0; rotation < rotated.size(); ++rotation) {
        for (std::size_t frame = 0; frame < rotation; ++frame) {
            require(rotated[rotation].push(Cell{}), "zero rotation prelude rejected");
        }
        check_output(rotated[rotation], chronological_output(completed),
                     "startup", rotation, 0, reads);
    }

    // The different zero preludes put the same logical history at every ring
    // cursor. Mirror-opposite cells add cancellation without sharing ring logic.
    const auto common = cancelling_history();
    const auto append = [&](const Cell& cell, std::string_view phase, std::size_t frame) {
        completed.push_back(cell);
        const auto expected = chronological_output(completed);
        require(expected.has_value(), "ordinary ordered fixture overflowed");
        for (std::size_t rotation = 0; rotation < rotated.size(); ++rotation) {
            require(rotated[rotation].push(cell), "finite ordered-history cell rejected");
            check_output(rotated[rotation], expected, phase, rotation, frame, reads);
        }
    };
    for (std::size_t frame = 0; frame < common.size(); ++frame) {
        append(common[frame], "common history", frame);
    }

    const auto before_rejection = chronological_output(completed);
    constexpr std::array invalid_values{
        std::numeric_limits<double>::quiet_NaN(),
        std::numeric_limits<double>::infinity(),
        -std::numeric_limits<double>::infinity()};
    for (std::size_t rotation = 0; rotation < rotated.size(); ++rotation) {
        Cell invalid{};
        invalid.fill(123.0);
        invalid.back() = invalid_values[rotation % invalid_values.size()];
        require(!rotated[rotation].push(invalid), "invalid ordered-history cell accepted");
        check_output(rotated[rotation], before_rejection, "rejected cell",
                     rotation, 0, reads);
    }

    // Continue past a complete ring turn after rejection, including a fully
    // expired zero tail. A partially written cell or advanced cursor must fail.
    for (std::size_t frame = 0; frame < 17; ++frame) {
        append(distinct_cell(count + frame), "live continuation", frame);
    }
    for (std::size_t frame = 0; frame <= count; ++frame) {
        append(Cell{}, "zero continuation", frame);
    }
    require(same_output(chronological_output(completed), 0.0),
            "ordered fixture did not expire to positive zero");
    std::cout << "ordered history: cursors=" << rotated.size()
              << " common_cells=" << common.size() << " continuation_cells=" << count + 18
              << " exact_reads=" << reads << '\n';
}

void qfir_prefix_overflow() {
    constexpr auto count = filter::history_frames;
    constexpr auto half_nodes = filter::node_count / 2;
    const double magnitude = 0.99 * std::numeric_limits<double>::max();
    std::vector<Cell> extreme(count);
    long double prefix_gain = 0.0L;
    for (std::size_t node = 0; node < filter::node_count; ++node) {
        long double node_gain = 0.0L;
        for (std::size_t age = 0; age < count; ++age) {
            const double coefficient = filter::coefficients[node][age];
            const double aligned = std::copysign(magnitude, coefficient);
            extreme[count - 1 - age][node] = node < half_nodes ? aligned : -aligned;
            node_gain += std::abs(static_cast<long double>(coefficient));
            require(std::isfinite(coefficient * aligned),
                    "prefix fixture has a nonfinite individual product");
            // Every positive-prefix term has an exactly opposite mirror term.
            // This proves mathematical cancellation without an overflowing sum
            // or requiring long double to have a wider exponent than double.
            require(coefficient == filter::coefficients[filter::node_count - 1 - node]
                                                      [count - 1 - age],
                    "prefix fixture lost exact coefficient mirror symmetry");
        }
        require(node_gain < 1.0L, "prefix fixture has an overflowing node subtotal");
        if (node < half_nodes) { prefix_gain += node_gain; }
    }
    require(prefix_gain > static_cast<long double>(std::numeric_limits<double>::max()) /
                              magnitude,
            "positive prefix cannot overflow before its cancelling suffix");
    require(!chronological_output(extreme), "ordered prefix oracle did not overflow");

    // Empty second range, one-element first range, interior split, and
    // one-element second range. Ordinary histories cover the remaining cursors.
    constexpr std::array rotations{std::size_t{0}, std::size_t{1}, count / 2, count - 1};
    std::size_t reads = 0;
    for (const auto rotation : rotations) {
        rhodes::QuadratureFir fir;
        for (std::size_t frame = 0; frame < rotation; ++frame) {
            require(fir.push(Cell{}), "prefix rotation prelude rejected");
        }
        for (const auto& cell : extreme) {
            require(fir.push(cell), "finite prefix-overflow cell rejected");
        }
        check_output(fir, std::nullopt, "cancelling prefix overflow", rotation, 0, reads);
        Cell invalid{};
        invalid.fill(123.0);
        invalid.back() = std::numeric_limits<double>::quiet_NaN();
        require(!fir.push(invalid), "invalid cell accepted after prefix overflow");
        check_output(fir, std::nullopt, "prefix rejection", rotation, 0, reads);

        auto completed = extreme;
        completed.reserve(2 * count + 1);
        for (std::size_t frame = 0; frame <= count; ++frame) {
            completed.push_back(Cell{});
            require(fir.push(Cell{}), "finite prefix tail rejected");
            check_output(fir, chronological_output(completed), "prefix continuation",
                         rotation, frame, reads);
        }

        // Reset a faulted history again, then compare fresh startup continuation.
        for (const auto& cell : extreme) {
            require(fir.push(cell), "finite prefix refill rejected");
        }
        check_output(fir, std::nullopt, "prefix refill", rotation, 0, reads);
        fir.reset();
        completed.clear();
        check_output(fir, chronological_output(completed), "prefix reset", rotation, 0, reads);
        for (std::size_t frame = 0; frame < 17; ++frame) {
            completed.push_back(distinct_cell(count + frame));
            require(fir.push(completed.back()), "finite reset continuation rejected");
            check_output(fir, chronological_output(completed), "reset continuation",
                         rotation, frame, reads);
        }
    }
    std::cout << "prefix overflow: cancelling_total=0 prefix_gain=" << prefix_gain
              << " cursors=" << rotations.size() << " exact_reads=" << reads << '\n';
}
} // namespace

int main(int argc, char** argv) {
    const std::string_view selected = argc > 1 ? argv[1] : "all";
    bool found = false;
    bool passed = true;
    const auto run = [&](std::string_view name, auto test) {
        if (selected != "all" && selected != name) { return; }
        found = true;
        try {
            test();
            std::cout << "PASS " << name << '\n';
        } catch (const std::exception& error) {
            std::cerr << "FAIL " << name << ": " << error.what() << '\n';
            passed = false;
        }
    };
    run("qfir_ordered_history", qfir_ordered_history);
    run("qfir_prefix_overflow", qfir_prefix_overflow);
    if (!found) { std::cerr << "unknown quadrature FIR order fixture\n"; }
    return found && passed ? 0 : 1;
}
