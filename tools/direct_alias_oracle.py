#!/usr/bin/env python3
"""Independent, bounded stationary alias oracle for synthetic pickup primitives.

This module imports no production propagator or symbolic audit.  Its two
independent evaluations are (1) explicit one/two-mode cosine identities for the
cubic flux, differentiated at the ORIGINAL signed frequency, and (2) analytic
modal q/v inserted directly into -Phi'(sum(q))*sum(v).  Neither is a time stepper.

All line dictionaries map exact signed integer frequency bins to complex voltage
coefficients: y(t) = sum(c[k] * exp(2j*pi*k*Fs*t/N)).  Coefficients therefore have
the normalized-DFT convention, including both conjugate lines.  Folding sums
complex coefficients before taking powers; DC and Nyquist receive no extra factor
of two.  The ideal target retains only ORIGINAL bins with 2*abs(k) < N.

The four fixtures and new diagnostic tolerances implement W2-025's proposed
stationary experiment.  They change no engine identity, historical budget or
acceptance criterion.  These are synthetic volts at one pickup, without harp
gain; no fidelity, audibility, whole-engine or real-time claim follows.

Importable entry points: fixtures, plan, flux_cosine_terms, continuous_lines,
folded_lines, bandlimited_lines, sample_lines, analytic_motion, analytic_waveform,
oracle_arrays, error_arrays, signal_metrics, line_rms, dft_at_bins, metrics.
The CLI only prints --plan or --self-check JSON; it never writes a file.
"""

from __future__ import annotations

import argparse
import cmath
from collections.abc import Iterable, Mapping, Sequence
from dataclasses import dataclass, replace
import json
import math
from typing import Any


SCHEMA_VERSION = 1
MAX_CASES = 4
MAX_SAMPLES = 2048
MAX_MODES = 2
MAX_LINES = 128
MAX_DFT_BINS = 64
MAX_SAMPLE_RATE_HZ = 192000
MAX_MODE_FREQUENCY_HZ = 3 * MAX_SAMPLE_RATE_HZ
MAX_ABS_BIN = 9 * MAX_SAMPLES
COEFFICIENTS = (0.4, 800.0, 400000.0)
BOOKKEEPING_PEAK_V = 1.0e-12
PRIMITIVE_PEAK_V = 1.0e-10
PRIMITIVE_RMS_V = 1.0e-11


def _integer(value: Any, name: str, low: int, high: int) -> int:
    if type(value) is not int or not low <= value <= high:
        raise ValueError(f"{name} must be an integer in [{low}, {high}]")
    return value


def _real(value: Any, name: str) -> float:
    if type(value) not in (int, float):
        raise ValueError(f"{name} must be a finite real number, not a boolean")
    try:
        result = float(value)
    except (OverflowError, ValueError) as exc:
        raise ValueError(f"{name} must be finite") from exc
    if not math.isfinite(result):
        raise ValueError(f"{name} must be finite")
    return result


def _complex(value: Any, name: str) -> complex:
    if type(value) not in (int, float, complex):
        raise ValueError(f"{name} must be a finite numeric coefficient")
    try:
        result = complex(value)
    except (OverflowError, ValueError) as exc:
        raise ValueError(f"{name} must be finite") from exc
    if not (math.isfinite(result.real) and math.isfinite(result.imag)):
        raise ValueError(f"{name} must be finite")
    return result


def _sum(values: Iterable[float], name: str) -> float:
    try:
        return _real(math.fsum(values), name)
    except OverflowError as exc:
        raise ValueError(f"{name} overflowed") from exc


def _sample_count(value: Any) -> int:
    result = _integer(value, "sample_count", 2, MAX_SAMPLES)
    if result % 2:
        raise ValueError("sample_count must be even for the signed Nyquist convention")
    return result


def _samples(values: Sequence[float], name: str) -> tuple[float, ...]:
    if not isinstance(values, Sequence) or isinstance(values, (str, bytes, bytearray)):
        raise ValueError(f"{name} must be a bounded sequence")
    if not 1 <= len(values) <= MAX_SAMPLES:
        raise ValueError(f"{name} must contain 1..{MAX_SAMPLES} samples")
    return tuple(_real(value, name) for value in values)


@dataclass(frozen=True)
class Mode:
    """A prescribed cosine; integer Hz make coherence an exact divisibility test.

    Signed/zero frequencies are accepted for algebra checks.  The four frozen
    producer fixtures use positive frequencies below the primitive's limit.
    """

    frequency_hz: int
    amplitude_m: float
    phase_rad: float

    def __post_init__(self) -> None:
        _integer(self.frequency_hz, "frequency_hz", -MAX_MODE_FREQUENCY_HZ,
                 MAX_MODE_FREQUENCY_HZ)
        amplitude = _real(self.amplitude_m, "amplitude_m")
        if amplitude < 0.0:
            raise ValueError("amplitude_m must be nonnegative; use phase for polarity")
        object.__setattr__(self, "amplitude_m", amplitude)
        object.__setattr__(self, "phase_rad", _real(self.phase_rad, "phase_rad"))


@dataclass(frozen=True)
class Fixture:
    """Immutable bounded mathematical fixture, independent of engine parameters."""

    case_id: str
    sample_rate_hz: int
    sample_count: int
    modes: tuple[Mode, ...]
    coefficients: tuple[float, float, float] = COEFFICIENTS
    mass_kg: float = 0.001
    decay_per_s: float = 0.0

    def __post_init__(self) -> None:
        if (type(self.case_id) is not str or not 1 <= len(self.case_id) <= 80
                or any(c not in "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-"
                       for c in self.case_id)):
            raise ValueError("case_id must contain 1..80 ASCII letters, digits, '_' or '-'")
        _integer(self.sample_rate_hz, "sample_rate_hz", 1, MAX_SAMPLE_RATE_HZ)
        _sample_count(self.sample_count)
        if (type(self.modes) is not tuple or not 1 <= len(self.modes) <= MAX_MODES
                or any(type(mode) is not Mode for mode in self.modes)):
            raise ValueError("modes must be an immutable tuple of one or two Mode values")
        for mode in self.modes:
            numerator = mode.frequency_hz * self.sample_count
            if numerator % self.sample_rate_hz:
                raise ValueError("every frequency must occupy an exact integer record bin")
            if abs(numerator // self.sample_rate_hz) > 3 * self.sample_count:
                raise ValueError("mode frequency must not exceed three times the sample rate")
        if type(self.coefficients) is not tuple or len(self.coefficients) != 3:
            raise ValueError("coefficients must be an immutable three-number tuple")
        object.__setattr__(self, "coefficients", tuple(
            _real(value, "flux coefficient") for value in self.coefficients))
        mass = _real(self.mass_kg, "mass_kg")
        if mass <= 0.0:
            raise ValueError("mass_kg must be positive")
        object.__setattr__(self, "mass_kg", mass)
        decay = _real(self.decay_per_s, "decay_per_s")
        if decay != 0.0:
            raise ValueError("this stationary oracle supports undamped motion only")
        object.__setattr__(self, "decay_per_s", decay)

    @property
    def frequency_bins(self) -> tuple[int, ...]:
        return tuple(mode.frequency_hz * self.sample_count // self.sample_rate_hz
                     for mode in self.modes)

    @property
    def duration_s(self) -> float:
        return self.sample_count / self.sample_rate_hz


def _fixture(value: Any) -> Fixture:
    if type(value) is not Fixture:
        raise ValueError("fixture must be a Fixture")
    return value


_ONE_MODE = (Mode(13500, 1.0e-4, math.pi / 7.0),)
_TWO_MODES = (Mode(7500, 1.0e-4, math.pi / 7.0),
              Mode(13500, 6.0e-5, -math.pi / 5.0))
_FIXTURES = (
    Fixture("one_mode_48000", 48000, 1024, _ONE_MODE),
    Fixture("one_mode_96000", 96000, 2048, _ONE_MODE),
    Fixture("two_modes_48000", 48000, 1024, _TWO_MODES),
    Fixture("two_modes_96000", 96000, 2048, _TWO_MODES),
)


def fixtures() -> tuple[Fixture, ...]:
    """Return exactly the four frozen W2-025 fixtures, in stable order."""
    return _FIXTURES


def fixture_record(fixture: Fixture) -> dict[str, Any]:
    """Return a new JSON-compatible description; mutation cannot alter a fixture."""
    fixture = _fixture(fixture)
    return {
        "case_id": fixture.case_id,
        "sample_rate_hz": fixture.sample_rate_hz,
        "sample_count": fixture.sample_count,
        "duration_s": fixture.duration_s,
        "bin_width_hz": fixture.sample_rate_hz / fixture.sample_count,
        "coefficients_wb_turn_per_m_power": list(fixture.coefficients),
        "mass_kg": fixture.mass_kg,
        "decay_per_s": fixture.decay_per_s,
        "modes": [
            {"frequency_hz": mode.frequency_hz, "frequency_bin": bin_index,
             "amplitude_m": mode.amplitude_m, "phase_rad": mode.phase_rad}
            for mode, bin_index in zip(fixture.modes, fixture.frequency_bins)
        ],
    }


def plan() -> dict[str, Any]:
    """Fresh serializable plan.  Process/artifact caps are enforced by the driver."""
    return {
        "schema_version": SCHEMA_VERSION,
        "diagnostic": "direct-stationary-primitive-alias-v1",
        "provenance": "synthetic development constants; no specimen interpretation",
        "boundary": "one pickup voltage in volts, before harp gain",
        "observation": "observe q/v and voltage at n/Fs before advancing; no strike",
        "ideal_target": "retain original signed lines with 2*abs(bin) < sample_count",
        "line_convention": "c[k] in sum(c[k]*exp(2j*pi*k*n/N)); normalized DFT",
        "fold_interval": "[-N/2,N/2); sum complex coefficients before powers",
        "derivative": "-2j*pi*(original_bin*Fs/N)*flux_complex_coefficient",
        "sample_definitions": {
            "S": "full differentiated harmonic polynomial, sampled at n/Fs",
            "direct": "independent -Phi'(sum q)*sum v; compare to S",
            "B": "sum of retained original harmonics sampled at n/Fs",
            "alias": "S-B; also independently synthesize removed original harmonics",
            "numerical": "P-S", "total": "P-B",
        },
        "limits": {"cases": MAX_CASES, "samples_per_case": MAX_SAMPLES,
                   "workers": 1, "execution_seconds": 30,
                   "process_memory_bytes": 256 * 1024 * 1024,
                   "artifact_bytes": 16 * 1024 * 1024},
        "new_diagnostic_budgets": {
            "bookkeeping_peak_v": BOOKKEEPING_PEAK_V,
            "primitive_numerical_peak_v": PRIMITIVE_PEAK_V,
            "primitive_numerical_rms_v": PRIMITIVE_RMS_V,
            "alias_acceptability_threshold": None,
            "changes_historical_budgets_or_acceptance": False,
        },
        "fixtures": [fixture_record(fixture) for fixture in fixtures()],
    }


@dataclass(frozen=True)
class CosineTerm:
    """q*cos(2*pi*bin*Fs*t/N + phase); q is flux linkage in Wb-turn."""

    frequency_bin: int
    flux_coefficient_wb_turn: float
    phase_rad: float

    def __post_init__(self) -> None:
        _integer(self.frequency_bin, "frequency_bin", -MAX_ABS_BIN, MAX_ABS_BIN)
        object.__setattr__(self, "flux_coefficient_wb_turn",
                           _real(self.flux_coefficient_wb_turn, "flux cosine coefficient"))
        object.__setattr__(self, "phase_rad", _real(self.phase_rad, "cosine phase"))


def flux_cosine_terms(fixture: Fixture) -> tuple[CosineTerm, ...]:
    """Explicit cubic cosine identities, before frequency coalescence.

    This is the table derived in W2-025, not convolution from the existing audit.
    A missing second mode has B=0.  Signed difference frequencies are preserved.
    """
    fixture = _fixture(fixture)
    a1, a2, a3 = fixture.coefficients
    first = fixture.modes[0]
    A, p, k = first.amplitude_m, first.phase_rad, fixture.frequency_bins[0]
    if len(fixture.modes) == 2:
        second = fixture.modes[1]
        B, z, ell = second.amplitude_m, second.phase_rad, fixture.frequency_bins[1]
    else:
        B, z, ell = 0.0, 0.0, 0
    A2, B2 = A * A, B * B
    return (
        CosineTerm(0, a2 * (A2 + B2) / 2.0, 0.0),
        CosineTerm(k, a1 * A + 0.75 * a3 * A2 * A + 1.5 * a3 * A * B2, p),
        CosineTerm(ell, a1 * B + 0.75 * a3 * B2 * B + 1.5 * a3 * A2 * B, z),
        CosineTerm(2 * k, a2 * A2 / 2.0, 2.0 * p),
        CosineTerm(2 * ell, a2 * B2 / 2.0, 2.0 * z),
        CosineTerm(k + ell, a2 * A * B, p + z),
        CosineTerm(k - ell, a2 * A * B, p - z),
        CosineTerm(3 * k, a3 * A2 * A / 4.0, 3.0 * p),
        CosineTerm(3 * ell, a3 * B2 * B / 4.0, 3.0 * z),
        CosineTerm(2 * k + ell, 0.75 * a3 * A2 * B, 2.0 * p + z),
        CosineTerm(2 * k - ell, 0.75 * a3 * A2 * B, 2.0 * p - z),
        CosineTerm(k + 2 * ell, 0.75 * a3 * A * B2, p + 2.0 * z),
        CosineTerm(k - 2 * ell, 0.75 * a3 * A * B2, p - 2.0 * z),
    )


def coalesce_lines(terms: Iterable[tuple[int, complex]]) -> dict[int, complex]:
    """Sum matching exact bin keys; remove only exact zeros, never threshold lines."""
    if not isinstance(terms, Iterable) or isinstance(terms, (str, bytes, bytearray)):
        raise ValueError("line contributions must be an iterable of bounded numeric pairs")
    grouped: dict[int, list[complex]] = {}
    for index, term in enumerate(terms):
        if index >= MAX_LINES:
            raise ValueError(f"at most {MAX_LINES} line contributions are supported")
        if not isinstance(term, (tuple, list)) or len(term) != 2:
            raise ValueError("each line contribution must be a (bin, coefficient) pair")
        key = _integer(term[0], "line bin", -MAX_ABS_BIN, MAX_ABS_BIN)
        grouped.setdefault(key, []).append(_complex(term[1], "line coefficient"))
    result = {}
    for key in sorted(grouped):
        coefficient = complex(_sum((value.real for value in grouped[key]), "line real sum"),
                              _sum((value.imag for value in grouped[key]), "line imaginary sum"))
        if coefficient != 0j:
            result[key] = coefficient
    return result


def _lines(lines: Mapping[int, complex]) -> dict[int, complex]:
    if not isinstance(lines, Mapping) or len(lines) > MAX_LINES:
        raise ValueError(f"lines must be a mapping containing at most {MAX_LINES} bins")
    return coalesce_lines(lines.items())


def voltage_lines_from_cosines(terms: Sequence[CosineTerm], sample_rate_hz: int,
                               sample_count: int) -> dict[int, complex]:
    """Differentiate at signed ORIGINAL F: c_F=-i*pi*F*q*exp(i*phase)."""
    rate = _integer(sample_rate_hz, "sample_rate_hz", 1, MAX_SAMPLE_RATE_HZ)
    count = _sample_count(sample_count)
    if (not isinstance(terms, Sequence) or len(terms) > MAX_LINES // 2
            or any(type(term) is not CosineTerm for term in terms)):
        raise ValueError("terms must be a bounded sequence of CosineTerm values")
    contributions = []
    for term in terms:
        original_frequency = term.frequency_bin * rate / count
        multiplier = _real(math.pi * original_frequency * term.flux_coefficient_wb_turn,
                           "original-frequency derivative multiplier")
        coefficient = _complex(-1j * multiplier * cmath.exp(1j * term.phase_rad),
                               "differentiated coefficient")
        contributions.append((term.frequency_bin, coefficient))
        contributions.append((-term.frequency_bin, coefficient.conjugate()))
    return coalesce_lines(contributions)


def continuous_lines(fixture: Fixture) -> dict[int, complex]:
    """Coalesced continuous signed voltage lines; k=0 differentiates to zero."""
    fixture = _fixture(fixture)
    return voltage_lines_from_cosines(flux_cosine_terms(fixture), fixture.sample_rate_hz,
                                     fixture.sample_count)


def fold_bin(frequency_bin: int, sample_count: int) -> int:
    """Exact r(k) in [-N/2,N/2), including negative and multiple-period bins."""
    count = _sample_count(sample_count)
    key = _integer(frequency_bin, "frequency_bin", -MAX_ABS_BIN, MAX_ABS_BIN)
    return (key + count // 2) % count - count // 2


def folded_lines(lines: Mapping[int, complex], sample_count: int) -> dict[int, complex]:
    """Fold differentiated lines, coalescing both conjugates at DC/Nyquist."""
    count = _sample_count(sample_count)
    return coalesce_lines((fold_bin(key, count), coefficient)
                          for key, coefficient in _lines(lines).items())


def bandlimited_lines(lines: Mapping[int, complex], sample_count: int) -> dict[int, complex]:
    """Retain ORIGINAL frequencies strictly below Nyquist, before any folding."""
    count = _sample_count(sample_count)
    return {key: value for key, value in _lines(lines).items() if 2 * abs(key) < count}


def alias_lines(lines: Mapping[int, complex], sample_count: int) -> dict[int, complex]:
    """Fold only removed original lines, including original Nyquist endpoints."""
    count = _sample_count(sample_count)
    return folded_lines({key: value for key, value in _lines(lines).items()
                         if 2 * abs(key) >= count}, count)


def _real_spectrum(lines: Mapping[int, complex], count: int) -> dict[int, complex]:
    folded = folded_lines(lines, count)
    for key, value in folded.items():
        opposite = fold_bin(-key, count)
        if value != folded.get(opposite, 0j).conjugate():
            raise ValueError("voltage lines must be conjugate symmetric, including real DC/Nyquist")
    return folded


def sample_lines(lines: Mapping[int, complex], sample_count: int) -> tuple[float, ...]:
    """Sample a real coherent line spectrum; no interpolation, FFT or recurrence.

    Exact integer cycle reduction bounds trig arguments.  Each signed coefficient
    contributes once; no endpoint or positive-frequency doubling is applied.
    """
    count = _sample_count(sample_count)
    folded = _real_spectrum(lines, count)
    return tuple(_sum((coefficient.real * math.cos(math.tau * ((key * n) % count) / count)
                       - coefficient.imag * math.sin(math.tau * ((key * n) % count) / count)
                       for key, coefficient in folded.items()), "synthesized sample")
                 for n in range(count))


def analytic_motion(fixture: Fixture) -> dict[str, tuple]:
    """Direct prescribed q/v, with sample-major modal arrays and SI units."""
    fixture = _fixture(fixture)
    count = fixture.sample_count
    q_rows, v_rows = [], []
    for n in range(count):
        positions, velocities = [], []
        for mode, key in zip(fixture.modes, fixture.frequency_bins):
            angle = _real(math.tau * ((key * n) % count) / count + mode.phase_rad,
                          "analytic phase")
            positions.append(_real(mode.amplitude_m * math.cos(angle), "displacement"))
            velocities.append(_real(-math.tau * mode.frequency_hz * mode.amplitude_m
                                    * math.sin(angle), "velocity"))
        q_rows.append(tuple(positions))
        v_rows.append(tuple(velocities))
    return {"time_s": tuple(n / fixture.sample_rate_hz for n in range(count)),
            "q_m": tuple(q_rows), "v_m_per_s": tuple(v_rows),
            "x_m": tuple(_sum(row, "summed displacement") for row in q_rows),
            "v_total_m_per_s": tuple(_sum(row, "summed velocity") for row in v_rows)}


def _direct_voltage(fixture: Fixture, motion: Mapping[str, tuple]) -> tuple[float, ...]:
    a1, a2, a3 = fixture.coefficients
    # Deliberately direct chain rule, independently of flux_cosine_terms/lines.
    return tuple(_real(-(a1 + 2.0 * a2 * x + 3.0 * a3 * x * x) * velocity,
                       "direct polynomial voltage")
                 for x, velocity in zip(motion["x_m"], motion["v_total_m_per_s"]))


def analytic_waveform(fixture: Fixture) -> tuple[float, ...]:
    """Independent direct -Phi'(x)*v samples; sums modes before nonlinearity."""
    fixture = _fixture(fixture)
    return _direct_voltage(fixture, analytic_motion(fixture))


def oracle_arrays(fixture: Fixture) -> dict[str, tuple]:
    """Return q/v, direct polynomial, S, ideal B and two alias evaluations.

    S is the full analytic harmonic polynomial.  direct_v independently checks
    that expansion.  alias_v is S-B; analytic_alias_v independently synthesizes
    removed original harmonics.  At 96 kHz both alias arrays are exactly zero
    because no line is removed, without rounding or magnitude subtraction.
    """
    fixture = _fixture(fixture)
    lines = continuous_lines(fixture)
    motion = analytic_motion(fixture)
    sampled = sample_lines(lines, fixture.sample_count)
    band = sample_lines(bandlimited_lines(lines, fixture.sample_count), fixture.sample_count)
    return {**motion, "direct_v": _direct_voltage(fixture, motion), "s_v": sampled,
            "b_v": band, "alias_v": tuple(_real(s - b, "alias sample")
                                           for s, b in zip(sampled, band)),
            "analytic_alias_v": sample_lines(alias_lines(lines, fixture.sample_count),
                                              fixture.sample_count)}


def error_arrays(primitive_v: Sequence[float], s_v: Sequence[float],
                 b_v: Sequence[float]) -> dict[str, tuple[float, ...]]:
    """Separate numerical and alias error, retaining samplewise closure residual."""
    primitive = _samples(primitive_v, "primitive_v")
    sampled, band = _samples(s_v, "s_v"), _samples(b_v, "b_v")
    if len(primitive) != len(sampled) or len(sampled) != len(band):
        raise ValueError("primitive_v, s_v and b_v must have equal lengths")
    numerical = tuple(_real(p - s, "numerical error") for p, s in zip(primitive, sampled))
    alias = tuple(_real(s - b, "alias error") for s, b in zip(sampled, band))
    total = tuple(_real(p - b, "total error") for p, b in zip(primitive, band))
    return {"numerical_v": numerical, "alias_v": alias, "total_v": total,
            "closure_v": tuple(_real(t - (n + a), "decomposition closure")
                               for t, n, a in zip(total, numerical, alias))}


def signal_metrics(values: Sequence[float]) -> dict[str, float | int]:
    """Sample peak, RMS and mean; hypot avoids unnecessary squaring overflow."""
    samples = _samples(values, "metric samples")
    return {"sample_count": len(samples), "sample_peak_v": max(abs(value) for value in samples),
            "rms_v": _real(math.hypot(*samples) / math.sqrt(len(samples)), "sample RMS"),
            "mean_v": _sum((value / len(samples) for value in samples), "sample mean")}


def line_rms(lines: Mapping[int, complex], sample_count: int) -> float:
    """Parseval RMS = sqrt(sum |c[k]|^2), AFTER folding and complex coalescence."""
    count = _sample_count(sample_count)
    return _real(math.hypot(*(part for value in _real_spectrum(lines, count).values()
                             for part in (value.real, value.imag))),
                 "line RMS")


def dft_at_bins(values: Sequence[float], bins: Sequence[int]) -> dict[int, complex]:
    """Independent normalized direct DFT at at most 64 requested signed bins.

    Useful for checking the folded table without NumPy or a full quadratic DFT.
    A requested bin outside the principal interval is returned at its folded key.
    """
    samples = _samples(values, "DFT samples")
    count = _sample_count(len(samples))
    if not isinstance(bins, Sequence) or isinstance(bins, (str, bytes, bytearray)):
        raise ValueError("DFT bins must be a bounded sequence")
    if len(bins) > MAX_DFT_BINS:
        raise ValueError(f"at most {MAX_DFT_BINS} DFT bins may be requested")
    keys = sorted({fold_bin(key, count) for key in bins})
    result = {}
    for key in keys:
        real = _sum((value * math.cos(math.tau * ((key * n) % count) / count) / count
                     for n, value in enumerate(samples)), "DFT real part")
        imag = _sum((-value * math.sin(math.tau * ((key * n) % count) / count) / count
                     for n, value in enumerate(samples)), "DFT imaginary part")
        result[key] = complex(real, imag)
    return result


def metrics(fixture: Fixture, primitive_v: Sequence[float] | None = None) -> dict[str, Any]:
    """Independently checkable metrics and failures for the NEW diagnostic only.

    No alias magnitude has an acceptability threshold.  If P is omitted, only
    oracle bookkeeping is evaluated; primitive numerical success is not claimed.
    """
    fixture = _fixture(fixture)
    if primitive_v is not None:
        if not isinstance(primitive_v, Sequence) or len(primitive_v) != fixture.sample_count:
            raise ValueError("primitive_v must have exactly fixture.sample_count values")
        primitive_v = _samples(primitive_v, "primitive_v")
    arrays = oracle_arrays(fixture)
    line_alias = alias_lines(continuous_lines(fixture), fixture.sample_count)
    direct_residual = signal_metrics(tuple(d - s for d, s in zip(arrays["direct_v"], arrays["s_v"])))
    synthesis_residual = signal_metrics(tuple(a - h for a, h in
                                              zip(arrays["alias_v"], arrays["analytic_alias_v"])))
    alias_metric = signal_metrics(arrays["alias_v"])
    rms_lines = line_rms(line_alias, fixture.sample_count)
    rms_gap = abs(alias_metric["rms_v"] - rms_lines)
    folded = folded_lines(continuous_lines(fixture), fixture.sample_count)
    check_bins = sorted(set(folded) | {0, -fixture.sample_count // 2, 1, -1})
    observed = dft_at_bins(arrays["direct_v"], check_bins)
    dft_peak = max((abs(observed[key] - folded.get(key, 0j)) for key in observed), default=0.0)
    checks = {
        "direct_polynomial_matches_harmonics": direct_residual["sample_peak_v"] <= BOOKKEEPING_PEAK_V,
        "alias_synthesis_closes": synthesis_residual["sample_peak_v"] <= BOOKKEEPING_PEAK_V,
        "alias_parseval_closes": rms_gap <= BOOKKEEPING_PEAK_V,
        "direct_dft_matches_folded_lines": dft_peak <= BOOKKEEPING_PEAK_V,
    }
    if fixture in fixtures() and fixture.sample_rate_hz == 96000:
        checks["analytic_alias_is_exactly_zero_96000"] = (
            not line_alias and all(value == 0.0 for value in arrays["alias_v"])
            and all(value == 0.0 for value in arrays["analytic_alias_v"]))
    result: dict[str, Any] = {
        "schema_version": SCHEMA_VERSION, "case_id": fixture.case_id,
        "primitive_compared": primitive_v is not None,
        "scope": "new stationary primitive diagnostic; historical acceptance unchanged",
        "alias_error": alias_metric, "analytic_alias": signal_metrics(arrays["analytic_alias_v"]),
        "direct_polynomial_residual": direct_residual,
        "alias_synthesis_residual": synthesis_residual,
        "parseval": {"alias_rms_from_lines_v": rms_lines,
                     "alias_rms_from_samples_v": alias_metric["rms_v"], "absolute_rms_gap_v": rms_gap},
        "direct_dft_max_coefficient_error_v": dft_peak,
    }
    if primitive_v is not None:
        errors = error_arrays(primitive_v, arrays["s_v"], arrays["b_v"])
        result["numerical_error"] = signal_metrics(errors["numerical_v"])
        result["total_error"] = signal_metrics(errors["total_v"])
        result["decomposition_residual"] = signal_metrics(errors["closure_v"])
        checks.update({
            "primitive_numerical_peak": result["numerical_error"]["sample_peak_v"] <= PRIMITIVE_PEAK_V,
            "primitive_numerical_rms": result["numerical_error"]["rms_v"] <= PRIMITIVE_RMS_V,
            "decomposition_closes": result["decomposition_residual"]["sample_peak_v"] <= BOOKKEEPING_PEAK_V,
        })
    result["checks"] = checks
    result["failures"] = [name for name, passed in checks.items() if not passed]
    result["passed"] = not result["failures"]
    return result


def self_check() -> dict[str, Any]:
    """Small analytic checks only; no native process, build or performance capture."""
    checks: dict[str, bool] = {}
    cases = [metrics(fixture) for fixture in fixtures()]
    checks["four_frozen_fixture_bookkeeping"] = all(case["passed"] for case in cases)
    first = fixtures()[0]
    a1, a2, a3 = first.coefficients
    mode = first.modes[0]
    A, phase, frequency = mode.amplitude_m, mode.phase_rad, mode.frequency_hz
    expected = tuple(math.tau * frequency * (
        (a1 * A + 0.75 * a3 * A * A * A) * math.sin(math.tau * (288 * n % 1024) / 1024 + phase)
        + a2 * A * A * math.sin(2 * (math.tau * (288 * n % 1024) / 1024 + phase))
        + 0.75 * a3 * A * A * A * math.sin(3 * (math.tau * (288 * n % 1024) / 1024 + phase)))
        for n in range(first.sample_count))
    identity_peak = max(abs(a - b) for a, b in zip(expected, analytic_waveform(first)))
    checks["one_mode_closed_identity"] = identity_peak <= BOOKKEEPING_PEAK_V
    zero = replace(first, case_id="zero-coefficients", coefficients=(0.0, 0.0, 0.0))
    checks["zero_coefficients"] = not continuous_lines(zero) and not any(analytic_waveform(zero))
    negative = replace(first, case_id="negative-frequency", modes=(
        Mode(-mode.frequency_hz, mode.amplitude_m, -mode.phase_rad),))
    checks["signed_negative_frequency_identity"] = continuous_lines(negative) == continuous_lines(first)
    checks["conjugate_symmetry"] = all(value == lines.get(-key, 0j).conjugate()
                                     for fixture in fixtures() for lines in (continuous_lines(fixture),)
                                     for key, value in lines.items())
    # sin(w*n+alpha) + sin((2*pi-w)*n-alpha) = 0.  Equal coefficients
    # with a generic pi phase shift instead would generally reinforce, not cancel.
    coefficient = -0.5j * cmath.exp(1j * phase)
    high = -0.5j * cmath.exp(-1j * phase)
    cancelled = folded_lines({64: coefficient, -64: coefficient.conjugate(),
                              960: high, -960: high.conjugate()}, 1024)
    checks["opposite_phase_alias_cancellation"] = not cancelled
    checks["zero_phase_alias_cancellation"] = not folded_lines(
        {64: -0.5j, -64: 0.5j, 960: -0.5j, -960: 0.5j}, 1024)
    checks["dc_flux_differentiates_to_zero"] = not voltage_lines_from_cosines(
        (CosineTerm(0, 2.0, math.pi / 2),), 48000, 1024)
    endpoint_peaks = []
    for original_bin in (512, 1024):
        original_hz = original_bin * 48000 / 1024
        for angle in (0.0, math.pi / 2):
            lines = voltage_lines_from_cosines((CosineTerm(original_bin, 1.0, angle),),
                                               48000, 1024)
            endpoint = folded_lines(lines, 1024)
            amplitude = math.tau * original_hz * math.sin(angle)
            key = fold_bin(original_bin, 1024)
            endpoint_peaks.append(abs(endpoint.get(key, 0j) - amplitude))
            values = sample_lines(lines, 1024)
            phase_name = "zero_phase" if angle == 0.0 else "quarter_cycle_phase"
            checks[f"endpoint_bin_{original_bin}_{phase_name}"] = (
                len(endpoint) <= 1 and all(value.imag == 0.0 for value in endpoint.values())
                and all(abs(value - amplitude * ((-1.0) ** n if key else 1.0))
                        <= BOOKKEEPING_PEAK_V for n, value in enumerate(values)))
    checks["original_frequency_derivative"] = abs(
        abs(continuous_lines(first)[864]) - math.pi * 40500 * (a3 * A * A * A / 4)) <= BOOKKEEPING_PEAK_V
    # The nonzero starting phase detects a reversed Faraday sign.
    checks["voltage_derivative_sign"] = analytic_waveform(first)[0] > 0.0
    failures = [name for name, passed in checks.items() if not passed]
    return {"schema_version": SCHEMA_VERSION, "passed": not failures,
            "checks": checks, "failures": failures,
            "one_mode_identity_peak_v": identity_peak,
            "endpoint_coefficient_peak_error_v": max(endpoint_peaks), "cases": cases}


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    operation = parser.add_mutually_exclusive_group(required=True)
    operation.add_argument("--plan", action="store_true", help="print the four-case frozen plan")
    operation.add_argument("--self-check", action="store_true", help="run small analytic checks")
    arguments = parser.parse_args(argv)
    result = plan() if arguments.plan else self_check()
    print(json.dumps(result, indent=2, sort_keys=True, allow_nan=False))
    return 0 if arguments.plan or result["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
