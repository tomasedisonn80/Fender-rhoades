#!/usr/bin/env python3
"""Generate the fixed Q6 synthetic continuous-kernel observation coefficients.

This original standard-library implementation uses 80-digit Decimal arithmetic
and Newton iteration for the six-point Gauss-Legendre rule. It reuses only this
repository's original Decimal pi, sinc and I0 routines. No SciPy or third-party
source is copied or required. Public mathematical reference:
https://docs.scipy.org/doc/scipy/reference/generated/scipy.special.roots_legendre.html

Forward observation nodes a[j] lie in (0,1). A completed cell contributes

    c[j][k] = w[j] * K(k + 1 - a[j]) / N, k = 0,...,95,

where K is the 96-host-frame Kaiser/sinc kernel, centered at 48 frames, with
cutoff 0.47 of the host sample rate and beta 9. N is the complete quadrature
tap sum. The engine emits prior-cell history before observing the current cell.

Lower nodes are rounded to the nearest 2**-53 grid point; upper nodes are their
exact binary complements. This preserves literal pair symmetry, with at most
2**-54 perturbation from the mathematical nodes. The Decimal kernel uses these
serialized nodes. Weight pairs and coefficient mirror pairs are copied exactly.
All output uses hexadecimal binary64 literals, independent of libm or locale.

Synthetic engineering constants are not measured instrument parameters. Q6 is
an approximation for this nonpolynomial integrand, not an exact integral.
--check is read-only and fails for missing or stale output. Default output is
resolved from this file, independent of the working directory.
"""
from __future__ import annotations

import argparse
from decimal import Context, Decimal, ROUND_HALF_EVEN, localcontext
import importlib.util
from pathlib import Path
import sys


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_OUTPUT = ROOT / "include/rhodes/quadrature_filter_coefficients.hpp"
NODE_COUNT = 6
HISTORY_FRAMES = 96
LATENCY_FRAMES = 48
DECIMAL_PRECISION = 80
NEWTON_LIMIT = 64

# Load a sibling by its actual path so both CLI use and importlib-based tests
# work from arbitrary working directories without modifying sys.path.
_MATH_SPEC = importlib.util.spec_from_file_location(
    "rhodes_quadrature_decimal_math", Path(__file__).with_name("generate_audio_filter.py"))
assert _MATH_SPEC is not None and _MATH_SPEC.loader is not None
_MATH = importlib.util.module_from_spec(_MATH_SPEC)
_MATH_SPEC.loader.exec_module(_MATH)


def _legendre6(x: Decimal) -> tuple[Decimal, Decimal]:
    """P6 and its derivative via the standard Legendre three-term recurrence."""
    previous, current = Decimal(1), x
    for degree in range(2, NODE_COUNT + 1):
        following = ((2 * degree - 1) * x * current - (degree - 1) * previous) / degree
        previous, current = current, following
    derivative = NODE_COUNT * (x * current - previous) / (x * x - 1)
    return current, derivative


def _positive_root(seed: str) -> Decimal:
    value = Decimal(seed)
    # Stop well below binary64 needs, while avoiding last-digit Newton cycles
    # at the precision limit. The iteration count is always explicitly bounded.
    tolerance = Decimal(1).scaleb(-(DECIMAL_PRECISION - 8))
    for _ in range(NEWTON_LIMIT):
        polynomial, derivative = _legendre6(value)
        update = polynomial / derivative
        candidate = value - update
        if candidate == value or abs(update) < tolerance:
            residual, _ = _legendre6(candidate)
            if abs(residual) >= Decimal("1e-70"):
                raise ArithmeticError("Gauss-Legendre root residual exceeds its bound")
            return candidate
        value = candidate
    raise ArithmeticError("Gauss-Legendre root iteration did not converge")


def _rule() -> tuple[tuple[Decimal, ...], tuple[Decimal, ...]]:
    """Return symmetric forward nodes and mapped weights in the active context."""
    scale = Decimal(2) ** 53
    lower_nodes, lower_weights = [], []
    # Descending positive roots give ascending lower nodes on the unit cell.
    for seed in ("0.93", "0.65", "0.25"):
        root = _positive_root(seed)
        _, derivative = _legendre6(root)
        node = ((1 - root) * scale / 2).to_integral_value(rounding=ROUND_HALF_EVEN) / scale
        # Standard [-1,1] weight is 2/((1-r*r)*P6'(r)**2); mapping to
        # [0,1] divides by two. Weights correspond to mathematical roots.
        weight = 1 / ((1 - root * root) * derivative * derivative)
        lower_nodes.append(node)
        lower_weights.append(weight)
    nodes = tuple(lower_nodes) + tuple(1 - node for node in reversed(lower_nodes))
    weights = tuple(lower_weights) + tuple(reversed(lower_weights))
    return nodes, weights


def generate_coefficients() -> tuple[tuple[float, ...], tuple[float, ...],
                                     tuple[tuple[float, ...], ...], float]:
    """Return forward nodes, weights, normalized phase taps and DC normalizer."""
    context = Context(prec=DECIMAL_PRECISION, rounding=ROUND_HALF_EVEN)
    with localcontext(context):
        nodes, weights = _rule()
        pi = _MATH._pi()
        beta, width = Decimal(9), Decimal("0.94")
        i0_beta = _MATH._i0(beta)
        first_half = []
        for node, weight in zip(nodes[:NODE_COUNT // 2], weights[:NODE_COUNT // 2]):
            phase = []
            for age in range(HISTORY_FRAMES):
                centered = age + 1 - node - LATENCY_FRAMES
                position = centered / LATENCY_FRAMES
                window = _MATH._i0(beta * (1 - position * position).sqrt()) / i0_beta
                phase.append(weight * width * _MATH._sinc(width * centered, pi) * window)
            first_half.append(tuple(phase))
        normalizer = 2 * sum((sum(phase, Decimal(0)) for phase in first_half), Decimal(0))
        if normalizer <= 0:
            raise ArithmeticError("Quadrature kernel has nonpositive DC normalization")
        normalized = tuple(tuple(float(value / normalizer) for value in phase)
                           for phase in first_half)
        coefficients = normalized + tuple(tuple(reversed(phase)) for phase in reversed(normalized))
        return (tuple(float(value) for value in nodes),
                tuple(float(value) for value in weights), coefficients, float(normalizer))


def generate_header() -> str:
    nodes, weights, coefficients, normalizer = generate_coefficients()
    lines = [
        "// Generated by tools/generate_quadrature_filter.py; do not edit by hand.",
        "// Original stdlib Decimal Q6 rule; synthetic engineering parameters.",
        "// Forward nodes a[j]; taps w[j]*K(k+1-a[j])/N, k=0..95.",
        "// Emit completed-cell history before observing the current host cell.",
        "// Exact node/tap mirror pairs; kernel center and delay are 48 frames.",
        "#pragma once", "", "#include <array>", "#include <cstddef>",
        "#include <limits>", "", "namespace rhodes::quadrature_filter {", "",
        "static_assert(std::numeric_limits<double>::is_iec559 &&",
        "              std::numeric_limits<double>::radix == 2 &&",
        "              std::numeric_limits<double>::digits == 53 &&",
        "              std::numeric_limits<double>::max_exponent == 1024,",
        '              "Quadrature coefficients require IEEE binary64 double");', "",
        f"inline constexpr std::size_t node_count = {NODE_COUNT};",
        f"inline constexpr std::size_t history_frames = {HISTORY_FRAMES};",
        f"inline constexpr std::size_t latency_frames = {LATENCY_FRAMES};",
        "inline constexpr double kaiser_beta = 9.0;",
        "inline constexpr double cutoff_host_rate = 0.47;",
        f"inline constexpr double normalizer = {normalizer.hex()};", "",
    ]
    for name, values in (("nodes", nodes), ("weights", weights)):
        lines.append(f"inline constexpr std::array<double, node_count> {name} {{")
        for offset in range(0, len(values), 3):
            lines.append("    " + ", ".join(value.hex() for value in values[offset:offset + 3]) + ",")
        lines.extend(("};", ""))
    lines.append("inline constexpr std::array<std::array<double, history_frames>, node_count> coefficients {{")
    for phase in coefficients:
        lines.append("    {{")
        for offset in range(0, len(phase), 3):
            lines.append("        " + ", ".join(value.hex() for value in phase[offset:offset + 3]) + ",")
        lines.append("    }},")
    lines.extend(("}};", "", "} // namespace rhodes::quadrature_filter", ""))
    return "\n".join(lines)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT,
                        help="output header (default: repository quadrature header)")
    parser.add_argument("--check", action="store_true",
                        help="reject missing/stale output without writing files")
    args = parser.parse_args(argv)
    try:
        expected = generate_header().encode("ascii")
        if args.check:
            if args.output.read_bytes() != expected:
                print(f"Quadrature filter header is stale: {args.output}", file=sys.stderr)
                return 1
            print(f"Quadrature filter header is current: {args.output}")
        else:
            _MATH.write_header(args.output, expected)
            print(f"Generated quadrature filter header: {args.output}")
        return 0
    except (OSError, ValueError, ArithmeticError) as exc:
        print(f"Quadrature filter generation failed: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
