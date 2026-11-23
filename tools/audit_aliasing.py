#!/usr/bin/env python3
"""Audit possible cubic-pickup carrier folding in the current synthetic core.

This standard-library tool expands symbolic complex modal monomials. It does
not render audio, estimate line amplitudes, measure aliasing error, or establish
an antialiasing acceptance threshold. Source constants and supported statement
forms are read and checked on every run; unsupported source changes fail closed.
"""
from __future__ import annotations

import argparse
import cmath
import hashlib
import itertools
import json
import math
from pathlib import Path
import re
import sys
from typing import Any

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_RATES = (32000.0, 44100.0, 48000.0, 96000.0, 192000.0)


def capture(pattern: str, source: str, label: str) -> str:
    match = re.search(pattern, source)
    if match is None:
        raise ValueError(f"Unsupported source form for {label}; review this audit")
    return match.group(1)


def read_model(root: Path) -> dict[str, Any]:
    paths = ("src/research_engine.cpp", "include/rhodes/research_engine.hpp")
    blobs = {path: (root / path).read_bytes() for path in paths}
    source, header = (blobs[path].decode("utf-8") for path in paths)
    compact = re.sub(r"\s+", "", source)
    required = (
        "std::sqrt((omega_0_-decay)*(omega_0_+decay))",
        "-(a1_+x*(2.0*a2_+3.0*a3_*x))*velocity",
        "x+=pickup_observation[m]*state.displacement_m;",
        "velocity+=pickup_observation[m]*state.velocity_m_per_s;",
        "pickup_.voltage(x,velocity)", "frequency*frequency_ratios[m]",
        "(key.held||sustain_)?free_decay:damped_decay",
    )
    if any(form not in compact for form in required):
        raise ValueError("Core formula changed; review the analytical audit before use")
    arrays = {}
    for name in ("frequency_ratios", "free_decay", "damped_decay", "pickup_observation"):
        raw = capture(r"constexpr\s+std::array<double,\s*modes_per_key>\s+" +
                      name + r"\s*\{([^}]+)\}", source, name)
        arrays[name] = [float(value.strip()) for value in raw.split(",")]
    key_count = int(capture(r"key_count\s*=\s*(\d+)\s*;", header, "key count"))
    modes = int(capture(r"modes_per_key\s*=\s*(\d+)\s*;", header, "mode count"))
    if modes != 3 or any(len(values) != modes for values in arrays.values()):
        raise ValueError("This audit requires the current three-mode model")
    coefficients = [float(capture(r"double\s+a" + str(order) + r"_\s*=\s*([^;]+);",
                                  header, f"pickup order {order}")) for order in (1, 2, 3)]
    midi_start = float(capture(r"const\s+double\s+midi\s*=\s*static_cast<double>\(k\)\s*\+\s*([\d.]+)",
                               source, "MIDI origin"))
    frequency = re.search(r"([\d.]+)\s*\*\s*std::exp2\(\(midi\s*-\s*([\d.]+)\)\s*/\s*([\d.]+)\)", source)
    if frequency is None:
        raise ValueError("Unsupported equal-temperament source expression")
    minimum = float(capture(r"minimum_sample_rate_hz\s*=\s*([^;]+);", header, "minimum rate"))
    maximum = float(capture(r"maximum_sample_rate_hz\s*=\s*([^;]+);", header, "maximum rate"))
    guard = float(capture(r"p.frequency_hz\s*>=\s*([\d.]+)\s*\*\s*rate", source, "modal frequency guard"))
    numbers = [*coefficients, *frequency.groups(), midi_start, minimum, maximum,
               guard, *itertools.chain.from_iterable(arrays.values())]
    if not all(math.isfinite(float(value)) for value in numbers):
        raise ValueError("Nonfinite source model constant")
    return {**arrays, "pickup_coefficients": coefficients, "key_count": key_count,
            "midi_start": midi_start, "reference_hz": float(frequency[1]),
            "reference_midi": float(frequency[2]), "semitones_per_octave": float(frequency[3]),
            "minimum_rate_hz": minimum, "maximum_rate_hz": maximum,
            "modal_guard_fraction": guard,
            "source_sha256": {path: hashlib.sha256(blob).hexdigest() for path, blob in blobs.items()}}


def products(weights: list[float], coefficients: list[float]) -> list[dict[str, Any]]:
    """Expand x=sum_j w_j(z_j exp(s_j t)+conj(z_j) exp(conj(s_j)t))."""
    result = []
    components = 2 * len(weights)
    for order, polynomial in enumerate(coefficients, 1):
        if polynomial == 0:
            continue
        for chosen in itertools.combinations_with_replacement(range(components), order):
            counts = [chosen.count(index) for index in range(components)]
            total = [counts[2*j] + counts[2*j+1] for j in range(len(weights))]
            signed = [counts[2*j] - counts[2*j+1] for j in range(len(weights))]
            multiplicity = math.factorial(order) // math.prod(math.factorial(n) for n in counts)
            coefficient = polynomial * multiplicity * math.prod(w**n for w, n in zip(weights, total))
            if coefficient == 0:
                continue
            result.append({"order": order, "positive_negative_counts": counts,
                           "mode_powers": total, "signed_mode_sum": signed,
                           "multinomial": multiplicity,
                           "linkage_structural_coefficient": coefficient})
    return result


def canonical(vector: list[int]) -> tuple[int, ...]:
    first = next((value for value in vector if value), 0)
    return tuple(-value if first < 0 else value for value in vector)


def support(catalog: list[dict[str, Any]]) -> dict[tuple[int, ...], list[int]]:
    rows: dict[tuple[int, ...], set[int]] = {}
    for term in catalog:
        vector = canonical(term["signed_mode_sum"])
        if any(vector):
            rows.setdefault(vector, set()).add(term["order"])
    return {vector: sorted(orders) for vector, orders in sorted(rows.items())}


def fold(frequency: float, rate: float) -> float:
    return abs((frequency + rate / 2) % rate - rate / 2)


def key_audit(model: dict[str, Any], catalog: list[dict[str, Any]], key: int,
              regime: str, rates: list[float]) -> dict[str, Any]:
    fundamental = model["reference_hz"] * 2**((key + model["midi_start"] - model["reference_midi"])
                                            / model["semitones_per_octave"])
    natural = [fundamental * ratio for ratio in model["frequency_ratios"]]
    beta = model["free_decay" if regime == "held_or_sustained" else "damped_decay"]
    angular = [2 * math.pi * value for value in natural]
    if any(b < 0 or b >= omega for b, omega in zip(beta, angular)):
        raise ValueError("Core model is outside its underdamped domain")
    damped = [math.sqrt((omega-b)*(omega+b)) / (2*math.pi) for omega, b in zip(angular, beta)]
    lines = [{"signed_mode_sum": vector, "polynomial_orders": orders,
              "carrier_hz": abs(math.fsum(n*f for n, f in zip(vector, damped)))}
             for vector, orders in support(catalog).items()]
    highest = max((line["carrier_hz"] for line in lines), default=0.0)
    comparisons = []
    for rate in rates:
        above = [{**line, "folded_hz": fold(line["carrier_hz"], rate)}
                 for line in lines if line["carrier_hz"] > rate/2]
        at = [{**line, "folded_hz": fold(line["carrier_hz"], rate)}
              for line in lines if line["carrier_hz"] == rate/2]
        comparisons.append({"sample_rate_hz": rate, "nyquist_hz": rate/2,
                            "modal_guard_passes": all(f < model["modal_guard_fraction"]*rate for f in natural),
                            "above_nyquist_count": len(above), "above_nyquist": above,
                            "exactly_at_nyquist": at,
                            "highest_carrier_folded_hz": fold(highest, rate)})
    # Decaying zero-carrier monomials are not stationary DC and can have voltage.
    zero = [{"order": term["order"], "mode_powers": term["mode_powers"],
             "envelope_decay_per_s": math.fsum(n*b for n, b in zip(term["mode_powers"], beta)),
             "voltage_structural_coefficient": term["linkage_structural_coefficient"] *
                 math.fsum(n*b for n, b in zip(term["mode_powers"], beta))}
            for term in catalog if not any(term["signed_mode_sum"])]
    return {"key": key, "midi_pitch": key+model["midi_start"], "decay_regime": regime,
            "natural_frequencies_hz": natural, "damped_carriers_hz": damped,
            "beta_per_s": beta, "possible_nonzero_vector_locations": len(lines),
            "highest_possible_carrier_hz": highest,
            "rate_strictly_above_for_ideal_carrier_support_hz": 2*highest,
            "zero_carrier_envelope_terms": zero, "rates": comparisons}


def self_check(model: dict[str, Any]) -> dict[str, Any]:
    def check(condition: bool, message: str) -> None:
        if not condition:
            raise AssertionError(message)

    # For x=A*cos(wt), z=A/2: lambda contains a1*A+3*a3*A^3/4 at w,
    # a2*A^2/2 at 2w, and a3*A^3/4 at 3w. Differentiation multiplies by k*w.
    one = products([1.0], [2.0, 3.0, 5.0])
    amplitude = 0.2
    cosine = {harmonic: 2*sum(term["linkage_structural_coefficient"] *
                             (amplitude/2)**term["order"] for term in one
                             if term["signed_mode_sum"] == [harmonic]) for harmonic in (1, 2, 3)}
    expected = {1: 2*amplitude+3*5*amplitude**3/4,
                2: 3*amplitude**2/2, 3: 5*amplitude**3/4}
    for harmonic in expected:
        if not math.isclose(cosine[harmonic], expected[harmonic], rel_tol=1e-14, abs_tol=1e-16):
            raise AssertionError("Single-sinusoid polynomial expansion differs from closed form")
    check(set(support(one)) == {(1,), (2,), (3,)}, "Single-sinusoid support")
    check(fold(1000, 6000) == 1000 and fold(3000, 6000) == 3000, "In-band/Nyquist folds")
    check(fold(5000, 6000) == 1000 and fold(12000, 6000) == 0, "Out-of-band folds")
    catalog = products(model["pickup_observation"], model["pickup_coefficients"])
    check(len(catalog) == 83 and len(support(catalog)) == 31, "Current three-mode catalog")
    low = key_audit(model, catalog, 0, "held_or_sustained", list(DEFAULT_RATES))
    high = key_audit(model, catalog, model["key_count"]-1, "held_or_sustained", list(DEFAULT_RATES))
    check(low["midi_pitch"] == 28 and high["midi_pitch"] == 100, "Current MIDI endpoints")
    check(math.isclose(low["natural_frequencies_hz"][0], 41.20344461410875, rel_tol=1e-13), "Lowest fundamental")
    check(math.isclose(high["natural_frequencies_hz"][0], 2637.02045530296, rel_tol=1e-13), "Highest fundamental")
    check(all(row["above_nyquist_count"] == 0 for row in low["rates"]), "Lowest-key carrier support")
    check([row["above_nyquist_count"] > 0 for row in high["rates"]] == [True, True, True, False, False],
          "Highest-key carrier support")
    check(all(row["modal_guard_passes"] for row in high["rates"]), "Natural-frequency core guard")
    # Independently evaluate the expanded complex monomials against the direct
    # real polynomial and its time derivative, with unequal amplitudes/phases.
    maximum_error = 0.0
    z = [complex(1e-4, 3e-5), complex(-2e-5, 1e-5), complex(4e-6, -8e-6)]
    for beta in (model["free_decay"], model["damped_decay"]):
        exponents = [complex(-b, math.sqrt((2*math.pi*f-b)*(2*math.pi*f+b)))
                     for b, f in zip(beta, high["natural_frequencies_hz"])]
        for time in (0.0, 0.001, 0.05):
            positive = [amplitude*cmath.exp(s*time) for amplitude, s in zip(z, exponents)]
            components = [value for a in positive for value in (a, a.conjugate())]
            x = sum(w*(a+a.conjugate()) for w, a in zip(model["pickup_observation"], positive)).real
            speed = sum(w*(s*a+(s*a).conjugate())
                        for w, s, a in zip(model["pickup_observation"], exponents, positive)).real
            a1, a2, a3 = model["pickup_coefficients"]
            direct = ((a3*x+a2)*x+a1)*x
            direct_voltage = -(a1+2*a2*x+3*a3*x*x)*speed
            expanded = 0j
            expanded_voltage = 0j
            for term in catalog:
                value = term["linkage_structural_coefficient"] * math.prod(
                    component**n for component, n in zip(components, term["positive_negative_counts"]))
                multiplier = complex(math.fsum(n*b for n, b in zip(term["mode_powers"], beta)),
                                     -math.fsum(n*s.imag for n, s in zip(term["signed_mode_sum"], exponents)))
                expanded += value
                expanded_voltage += multiplier*value
            check(abs(expanded-direct) <= 1e-16, "Complex linkage expansion")
            check(abs(expanded_voltage-direct_voltage) <= 1e-12, "Complex voltage derivative expansion")
            maximum_error = max(maximum_error, abs(expanded_voltage-direct_voltage))
    return {"status": "pass", "scope": "algebra and current-source arithmetic only",
            "single_cosine_linkage_coefficients": cosine,
            "single_cosine_voltage_sine_coefficients_per_omega": {k: k*v for k, v in cosine.items()},
            "catalog_terms": len(catalog), "nonzero_vector_locations": len(support(catalog)),
            "complex_derivative_cases": 6, "maximum_complex_voltage_discrepancy_v": maximum_error,
            "lowest_key_maximum_carrier_hz": low["highest_possible_carrier_hz"],
            "highest_key_maximum_carrier_hz": high["highest_possible_carrier_hz"]}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo-root", type=Path, default=ROOT)
    parser.add_argument("--keys", type=int, nargs="+", help="physical key indices; default all")
    parser.add_argument("--sample-rates", type=float, nargs="+", default=list(DEFAULT_RATES))
    parser.add_argument("--regime", choices=("both", "held", "released"), default="both")
    parser.add_argument("--self-check", action="store_true")
    parser.add_argument("--compact", action="store_true")
    args = parser.parse_args()
    try:
        model = read_model(args.repo_root)
        keys = sorted(set(range(model["key_count"]) if args.keys is None else args.keys))
        rates = sorted(set(args.sample_rates))
        if any(key < 0 or key >= model["key_count"] for key in keys):
            raise ValueError("Key outside the current instrument range")
        if any(not math.isfinite(rate) or not model["minimum_rate_hz"] <= rate <= model["maximum_rate_hz"] for rate in rates):
            raise ValueError("Sample rate outside the supported core domain")
        if args.self_check:
            output = self_check(model)
        else:
            catalog = products(model["pickup_observation"], model["pickup_coefficients"])
            regimes = ["held_or_sustained", "released"] if args.regime == "both" else [
                "held_or_sustained" if args.regime == "held" else "released"]
            rows = [key_audit(model, catalog, key, regime, rates) for key in keys for regime in regimes]
            summaries = []
            for regime in regimes:
                for index, rate in enumerate(rates):
                    affected = [row["midi_pitch"] for row in rows if row["decay_regime"] == regime
                                and row["rates"][index]["above_nyquist_count"]]
                    summaries.append({"decay_regime": regime, "sample_rate_hz": rate,
                                      "keys_with_possible_folded_carriers": len(affected),
                                      "affected_midi_pitches": affected})
            output = {"schema_version": 1, "scope": "synthetic polynomial carrier support; not an audio-error measurement",
                      "model": model, "product_convention": {
                          "displacement": "x=sum w_j*(z_j*exp((-beta_j+i*2*pi*f_j)*t)+conj(z_j)*exp((-beta_j-i*2*pi*f_j)*t))",
                          "counts": "per mode: positive exponent, negative exponent; z_j is half a cosine amplitude",
                          "linkage_coefficient": "a_p*p!/product(count!)*product(w_j^mode_power); multiplies the indicated z/conj(z) monomial",
                          "voltage_multiplier": "sum(mode_power_j*beta_j)-i*2*pi*sum(signed_mode_sum_j*f_j)",
                          "coefficient_units": "linkage structural coefficient: weber-turns/metre^order; voltage multiplier: 1/second",
                          "fold": "abs((frequency+sample_rate/2) modulo sample_rate-sample_rate/2)"},
                      "structural_products": catalog, "summary": summaries, "keys": rows,
                      "limits": [
                          "Catalog entries are symbolic monomials, not equal-strength spectral lines; amplitudes, phases and cancellations are unspecified.",
                          "Conjugate locations and repeated vectors are grouped for counts; distinct vectors are not merged by a frequency tolerance.",
                          "Constant damping is assumed within each regime. Exponential envelopes and strike/release/retrigger boundaries make actual trajectories non-bandlimited.",
                          "Zero-carrier damped envelopes can contribute voltage; only stationary undamped DC differentiates to zero.",
                          "No audio render, FFT, amplitude error, audibility, filter, transient error or accepted antialiasing bound is measured.",
                          "Rates clearing ideal carrier support still require independent reconstruction/filter and transient convergence checks.",
                          "The current resistive linear harp creates no extra mixing products or antialiasing filter; future nonlinear shared circuits require a new audit.",
                          "Source extraction recognizes the current C++ statements; hashes bind inputs but do not constitute a general C++ semantics proof."]}
        print(json.dumps(output, indent=None if args.compact else 2, allow_nan=False))
        return 0
    except (OSError, UnicodeError, ValueError, AssertionError, OverflowError) as exc:
        print(f"Aliasing audit failed: {exc or type(exc).__name__}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
