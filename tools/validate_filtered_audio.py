#!/usr/bin/env python3
"""Independent, offline filtered-audio transient diagnostic (synthetic volts).

The fixed full matrix and the development-only pilot are specified in
docs/specs/FILTERED_TRANSIENT_DIAGNOSTIC_20261123.md and worker w2-025's report.
Comparison A independently samples analytic trajectories and generates a FIR.
Comparison B integrates the continuous source against the finite 96-frame
Kaiser/sinc prototype using separate Q16, Q32 and Q64 unit-cell quadratures.
Neither is an instrument-fidelity result or a separately isolated alias bound.

--help and --plan need only Python's standard library. Actual numerical runs
need installed NumPy and SciPy as optional OFFLINE tools, never runtime/build
dependencies. This original reference does not import the production engine,
its transition matrices, the coefficient generator or serialized FIR taps.
Existing output paths are refused. Each run retains its complete immutable
plan, inputs, probe stdout, binary/source hashes, raw float64 arrays and metrics.
Exit codes: 0 all selected gates pass; 1 a comparison fails; 2 unresolved B only;
3 an incomplete run/operational error. A pilot cannot establish full coverage.
"""
from __future__ import annotations

import argparse
from collections import Counter
from dataclasses import dataclass
from datetime import datetime, timezone
from functools import lru_cache
import hashlib
import json
import math
from pathlib import Path
import platform
import subprocess
import sys
from typing import Any, Callable


ROOT = Path(__file__).resolve().parents[1]
RATES = (32000.0, 44100.0, 48000.0, math.nextafter(48000.0, math.inf),
         64000.0, 88200.0, 96000.0, math.nextafter(96000.0, math.inf), 192000.0)
PILOT_RATE_INDICES = (0, 2, 3, 6, 7, 8)
INTENSITIES = (0.1, 0.7, 1.0)
MOVING_KEYS = (0, 3, 4, 36, 55, 62, 67, 72)
SPARSE_KEYS = (0, 12, 24, 36, 48, 60, 72)
ORDERS = (16, 32, 64)
SUPPORT_FRAMES = 96
LATENCY_FRAMES = 48
MODEL = {
    "identity": "uncalibrated-three-mode-cubic-loaded-harp-20261123",
    "key_count": 73, "frequency_reference_hz": 440.0,
    "reference_key": 41, "semitones_per_octave": 12,
    "frequency_ratios": (1.0, 2.73, 5.41),
    "free_decay_per_s": (0.8, 3.0, 7.0),
    "damped_decay_per_s": (30.0, 35.0, 45.0),
    "strike_coupling": (1.0, 0.45, 0.2),
    "maximum_velocity_change_m_per_s": 0.06,
    "pickup_observation": (1.0, 0.3, 0.08),
    "flux_polynomial_coefficients": (0.4, 800.0, 400000.0),
    "loaded_harp_gain_numerator": 12000, "loaded_harp_gain_denominator": 12019,
    "parallel_group_size_keys_0_to_3": 4, "parallel_group_size_other_keys": 3,
    "kernel_cutoff_host_fraction": 0.47, "kernel_kaiser_beta": 9.0,
    "kernel_support_host_frames": SUPPORT_FRAMES,
    "kernel_delay_host_frames": LATENCY_FRAMES,
}
GATES = {
    "comparison_a_peak_volts": 1e-12,
    "reference_refinement_peak_volts": 1e-9,
    "reference_refinement_rms_volts": 1e-10,
    "comparison_b_exploratory_peak_volts": 1e-4,
    "comparison_b_exploratory_rms_volts": 1e-5,
}
SCOPE = (
    "Exploratory synthetic loaded-harp voltage diagnostic; no gain, alignment, "
    "phase or latency fitting. B includes continuous-prototype/discrete-FIR "
    "differences and transient sampling effects; it is not an alias-only bound, "
    "audibility criterion, measured-instrument result or RT-001 acceptance."
)
SOURCE_PATHS = (
    "src/research_engine.cpp", "include/rhodes/research_engine.hpp",
    "include/rhodes/event_processor.hpp",
    "include/rhodes/audio_engine.hpp", "include/rhodes/fir_decimator.hpp",
    "include/rhodes/audio_filter_coefficients.hpp", "include/rhodes/control_value.hpp",
    "apps/audio_probe.cpp", "CMakeLists.txt", "tools/validate_filtered_audio.py",
    "tests/python/test_filtered_audio_reference.py",
    "docs/specs/FILTERED_AUDIO_20261123.md",
    "docs/specs/FILTERED_TRANSIENT_DIAGNOSTIC_20261123.md",
    "docs/agents/continuation-20261123/w2-025.md",
)


def canonical_bytes(value: Any) -> bytes:
    return json.dumps(value, sort_keys=True, separators=(",", ":"),
                      ensure_ascii=True, allow_nan=False).encode("ascii")


def object_hash(value: Any) -> str:
    return hashlib.sha256(canonical_bytes(value)).hexdigest()


def file_hash(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


@dataclass(frozen=True)
class Event:
    offset: int
    type: str
    key: int = 0
    value: float = 0.0

    def __post_init__(self) -> None:
        if type(self.offset) is not int or self.offset < 0:
            raise ValueError("Event offsets must be nonnegative integers")
        if type(self.key) is not int or not 0 <= self.key < 73:
            raise ValueError("Event keys must be integers in 0..72")
        if (type(self.value) not in (float, int) or not math.isfinite(self.value)
                or not 0.0 <= self.value <= 1.0):
            raise ValueError("Event values must be finite numbers in [0,1]")
        if self.type not in ("on", "off", "sustain", "all_off", "reset"):
            raise ValueError("Unknown physical event type")
        if self.type in ("sustain", "all_off", "reset") and self.key != 0:
            raise ValueError("Global events must use key 0")
        if self.type == "sustain" and self.value not in (0.0, 1.0):
            raise ValueError("Sustain value must be 0 or 1")
        if self.type in ("off", "all_off", "reset") and self.value != 0.0:
            raise ValueError("Release/all_off/reset value must be 0")

    def row(self) -> list[Any]:
        return [self.offset, self.type, self.key, float(self.value)]


@dataclass(frozen=True)
class Case:
    id: str
    family: str
    sample_rate_hz: float
    frames: int
    keys: tuple[int, ...]
    intensity: float
    events: tuple[Event, ...]

    def __post_init__(self) -> None:
        if (not math.isfinite(self.sample_rate_hz)
                or not 32000.0 <= self.sample_rate_hz <= 192000.0):
            raise ValueError("Host rate must be finite and in [32000,192000] Hz")
        if type(self.frames) is not int or not 1 <= self.frames <= 4096:
            raise ValueError("Frame count must be an integer in 1..4096")
        if (not self.keys or len(set(self.keys)) != len(self.keys)
                or any(type(key) is not int or not 0 <= key < 73 for key in self.keys)):
            raise ValueError("Case keys must be unique physical keys in 0..72")
        if not math.isfinite(self.intensity) or not 0.0 <= self.intensity <= 1.0:
            raise ValueError("Case intensity must be in [0,1]")
        previous = -1
        for event in self.events:
            if not previous <= event.offset < self.frames:
                raise ValueError("Events must be in producer order inside the record")
            if event.type in ("on", "off") and event.key not in self.keys:
                raise ValueError("Local event key is absent from the case")
            previous = event.offset
        if len(self.events) > 4096:
            raise ValueError("The probe accepts at most 4096 event rows")

    def document(self) -> dict[str, Any]:
        return {
            "case_id": self.id, "family": self.family,
            "sample_rate_hz": self.sample_rate_hz,
            "sample_rate_binary64_hex": self.sample_rate_hz.hex(),
            "frames": self.frames, "keys": list(self.keys), "intensity": self.intensity,
            "events": [event.row() for event in self.events],
            "event_columns": ["offset_host_frames", "type", "key", "value"],
        }


def event_script(keys: tuple[int, ...], intensity: float) -> tuple[Event, ...]:
    """Ten local operations per key plus six globals, each global exactly once."""
    events: list[Event] = []
    for offset, kind, value in (
        (16, "on", intensity), (17, "on", intensity), (191, "off", 0.0),
        (255, "sustain", 1.0), (256, "on", intensity), (383, "off", 0.0),
        (511, "sustain", 0.0), (768, "on", intensity), (769, "all_off", 0.0),
        (1023, "reset", 0.0), (1023, "on", intensity), (1536, "off", 0.0),
        (2048, "sustain", 1.0), (2049, "on", intensity), (2303, "on", 0.0),
        (2560, "sustain", 0.0),
    ):
        if kind in ("on", "off"):
            events.extend(Event(offset, kind, key, value) for key in keys)
        else:
            events.append(Event(offset, kind, 0, value))
    return tuple(events)


def make_case(rate_index: int, family: str, keys: tuple[int, ...], intensity: float) -> Case:
    tag = {0.1: "01", 0.7: "07", 1.0: "10"}[intensity]
    key_tag = f"k{keys[0]:02d}" if len(keys) == 1 else ("all73" if len(keys) == 73 else "sparse7")
    events = (tuple(Event(16, "on", key, intensity) for key in keys)
              if family == "attack" else event_script(keys, intensity))
    return Case(f"r{rate_index:02d}-{family}-{key_tag}-u{tag}", family,
                RATES[rate_index], 1024 if family == "attack" else 4096,
                keys, intensity, events)


def build_cases(suite: str) -> tuple[Case, ...]:
    """The CLI offers only these two predeclared matrices, with no case selectors."""
    if suite not in ("full", "pilot"):
        raise ValueError("Suite must be full or pilot")
    cases = []
    for index in (range(len(RATES)) if suite == "full" else PILOT_RATE_INDICES):
        for key in (range(73) if suite == "full" else (0, 36, 72)):
            for intensity in (INTENSITIES if suite == "full" else (1.0,)):
                cases.append(make_case(index, "attack", (key,), intensity))
        for key in (MOVING_KEYS if suite == "full" else (0, 72)):
            cases.append(make_case(index, "events", (key,), 1.0))
        for keys in ((tuple(range(73)), SPARSE_KEYS) if suite == "full" else (SPARSE_KEYS,)):
            for intensity in (INTENSITIES if suite == "full" else (1.0,)):
                cases.append(make_case(index, "chord", keys, intensity))
    expected = (2097, 2534400) if suite == "full" else (36, 92160)
    if (len(cases), sum(case.frames for case in cases)) != expected:
        raise RuntimeError("Fixed matrix count changed; review the declared protocol")
    return tuple(cases)


def make_plan(suite: str) -> dict[str, Any]:
    cases = build_cases(suite)
    plan = {
        "schema_version": 1, "suite": suite, "scope": SCOPE,
        "pilot_is_development_only": suite == "pilot",
        "case_count": len(cases), "host_frame_count": sum(case.frames for case in cases),
        "family_counts": dict(Counter(case.family for case in cases)),
        "quadrature_orders_per_unit_cell": list(ORDERS),
        "model_parameters": MODEL, "model_parameters_sha256": object_hash(MODEL),
        "gates": GATES, "event_response_window_inclusive": [0, SUPPORT_FRAMES],
        "cases": [case.document() for case in cases],
    }
    return {**plan, "plan_sha256": object_hash(plan)}


@lru_cache(maxsize=1)
def numerical_libraries() -> tuple[Any, Any]:
    try:
        import numpy as np
        import scipy
    except ImportError as exc:
        raise RuntimeError("Numerical runs require optional offline NumPy and SciPy; "
                           "--plan and --help require neither") from exc
    if sys.float_info.radix != 2 or sys.float_info.mant_dig != 53:
        raise RuntimeError("This experiment requires IEEE binary64 Python floats")
    return np, scipy


def oversampling_factor(rate: float) -> int:
    return 4 if rate <= 48000.0 else 2 if rate <= 96000.0 else 1


def modal_solution(q0: Any, v0: Any, omega0: Any, beta: Any, seconds: Any) -> tuple[Any, Any]:
    """Arbitrary-time closed form; no recurrence/production transition matrices."""
    np, _ = numerical_libraries()
    wd = np.sqrt(omega0 * omega0 - beta * beta)
    phase = seconds * wd
    envelope = np.exp(-seconds * beta)
    sine_over_wd, cosine = np.sin(phase) / wd, np.cos(phase)
    q = envelope * (q0 * cosine + (v0 + beta * q0) * sine_over_wd)
    v = envelope * (v0 * cosine - (beta * v0 + omega0 * omega0 * q0) * sine_over_wd)
    return q, v


@dataclass(frozen=True)
class Segment:
    start_frame: int
    q0: Any
    v0: Any
    beta: Any


class KeyTrajectory:
    """One independent physical key, with local pickup before electrical mixing."""
    def __init__(self, rate: float, key: int, events: tuple[Event, ...]):
        np, _ = numerical_libraries()
        self.rate, self.key = rate, key
        fundamental = MODEL["frequency_reference_hz"] * math.exp2(
            (key - MODEL["reference_key"]) / MODEL["semitones_per_octave"])
        self.omega0 = 2.0 * math.pi * fundamental * np.asarray(MODEL["frequency_ratios"])
        local = [event for event in events if event.type not in ("on", "off") or event.key == key]
        self.segments: list[Segment] = []
        q, v = np.zeros(3), np.zeros(3)
        held = sustain = False
        beta = np.asarray(MODEL["damped_decay_per_s"])
        previous = 0
        self.segments.append(Segment(0, q.copy(), v.copy(), beta.copy()))
        cursor = 0
        while cursor < len(local):
            offset = local[cursor].offset
            q, v = modal_solution(q, v, self.omega0, beta, (offset - previous) / rate)
            while cursor < len(local) and local[cursor].offset == offset:
                event = local[cursor]
                if event.type == "on" and event.value > 0.0:
                    held = True
                    v = v + (MODEL["maximum_velocity_change_m_per_s"] * event.value
                             * np.asarray(MODEL["strike_coupling"]))
                elif event.type in ("on", "off", "all_off"):
                    held = False
                elif event.type == "sustain":
                    sustain = bool(event.value)
                elif event.type == "reset":
                    q, v = np.zeros(3), np.zeros(3)
                    held = sustain = False
                cursor += 1
            beta = np.asarray(MODEL["free_decay_per_s"] if held or sustain
                              else MODEL["damped_decay_per_s"])
            segment = Segment(offset, q.copy(), v.copy(), beta.copy())
            if self.segments[-1].start_frame == offset:
                self.segments[-1] = segment
            else:
                self.segments.append(segment)
            previous = offset

    def states(self, host_frame_positions: Any) -> tuple[Any, Any]:
        np, _ = numerical_libraries()
        positions = np.asarray(host_frame_positions, dtype=np.float64)
        flat = positions.reshape(-1)
        q, v = np.zeros((flat.size, 3)), np.zeros((flat.size, 3))
        for index, segment in enumerate(self.segments):
            end = (self.segments[index + 1].start_frame
                   if index + 1 < len(self.segments) else math.inf)
            mask = (flat >= segment.start_frame) & (flat < end)
            if not np.any(mask):
                continue
            seconds = ((flat[mask] - segment.start_frame) / self.rate)[:, None]
            q[mask], v[mask] = modal_solution(segment.q0, segment.v0,
                                             self.omega0, segment.beta, seconds)
        return q.reshape((*positions.shape, 3)), v.reshape((*positions.shape, 3))

    def loaded_volts(self, host_frame_positions: Any) -> Any:
        np, _ = numerical_libraries()
        q, v = self.states(host_frame_positions)
        observation = np.asarray(MODEL["pickup_observation"])
        x, speed = np.sum(q * observation, axis=-1), np.sum(v * observation, axis=-1)
        a1, a2, a3 = MODEL["flux_polynomial_coefficients"]
        pickup = -(a1 + 2.0 * a2 * x + 3.0 * a3 * x * x) * speed
        group_size = 4 if self.key < 4 else 3
        return pickup * (MODEL["loaded_harp_gain_numerator"]
                         / MODEL["loaded_harp_gain_denominator"] / group_size)


def reset_epochs(frames: int, events: tuple[Event, ...]) -> tuple[tuple[int, int], ...]:
    boundaries = sorted({0, frames, *(e.offset for e in events if e.type == "reset")})
    return tuple(zip(boundaries[:-1], boundaries[1:]))


def kernel(positions: Any) -> Any:
    np, _ = numerical_libraries()
    s = np.asarray(positions, dtype=np.float64)
    centered = s - LATENCY_FRAMES
    window = np.i0(9.0 * np.sqrt(np.maximum(0.0, 1.0 - (centered / 48.0) ** 2))) / np.i0(9.0)
    value = 0.94 * np.sinc(0.94 * centered) * window
    return np.where((s >= 0.0) & (s <= SUPPORT_FRAMES), value, 0.0)


@lru_cache(maxsize=3)
def independent_fir(factor: int) -> Any:
    np, _ = numerical_libraries()
    if type(factor) is not int or factor not in (1, 2, 4):
        raise ValueError("FIR factor must be 1, 2 or 4")
    taps = kernel(np.arange(SUPPORT_FRAMES * factor + 1) / factor) / factor
    taps = taps / math.fsum(float(value) for value in taps)
    taps.setflags(write=False)
    return taps


@dataclass(frozen=True)
class Quadrature:
    order: int
    delta: Any
    taps: Any
    normalizer: float


@lru_cache(maxsize=3)
def quadrature(order: int) -> Quadrature:
    np, _ = numerical_libraries()
    if type(order) is not int or order not in ORDERS:
        raise ValueError("The predeclared unit-cell orders are 16, 32 and 64")
    # Standard independent offline library quadrature, not production DSP code.
    from scipy.special import roots_legendre
    nodes, weights = roots_legendre(order)
    delta, weights = (nodes + 1.0) / 2.0, weights / 2.0
    taps = weights[:, None] * kernel(np.arange(SUPPORT_FRAMES)[None, :] + delta[:, None])
    normalizer = math.fsum(float(value) for value in taps.reshape(-1))
    delta.setflags(write=False)
    taps.setflags(write=False)
    return Quadrature(order, delta, taps, normalizer)


def kernel_normalizer_report() -> dict[str, Any]:
    """A separately adaptive integral also audits the three unit-cell DC sums."""
    numerical_libraries()
    from scipy.integrate import quad
    from scipy.special import i0

    def scalar_kernel(s: float) -> float:
        centered = s - 48.0
        z = 0.94 * centered
        sinc = 1.0 if z == 0.0 else math.sin(math.pi * z) / (math.pi * z)
        return 0.94 * sinc * float(i0(9.0 * math.sqrt(max(0.0, 1.0 - (centered / 48.0) ** 2)))) / float(i0(9.0))

    cells = [quad(scalar_kernel, j, j + 1, epsabs=2e-15, epsrel=2e-13, limit=100)
             for j in range(SUPPORT_FRAMES)]
    adaptive = math.fsum(value for value, _ in cells)
    values = {str(order): quadrature(order).normalizer for order in ORDERS}
    return {
        "unit_cell_gauss_legendre": values, "adaptive_cell_quad": adaptive,
        "adaptive_reported_absolute_error_sum": math.fsum(error for _, error in cells),
        "absolute_differences": {
            "q16_q32": abs(values["16"] - values["32"]),
            "q32_q64": abs(values["32"] - values["64"]),
            **{f"q{order}_adaptive": abs(values[str(order)] - adaptive) for order in ORDERS},
        },
        "scope": "Normalizer agreement is reported, with no new post-hoc acceptance gate.",
    }


def sampled_reference(source: Any, factor: int, epochs: tuple[tuple[int, int], ...]) -> Any:
    np, _ = numerical_libraries()
    output = np.zeros(len(source) // factor)
    taps = independent_fir(factor)
    for start, stop in epochs:
        # Independent ordinary convolution, then exactly substep L*n.
        segment = source[start * factor:stop * factor]
        output[start:stop] = np.convolve(segment, taps, mode="full")[:len(segment):factor]
    return output


def continuous_reference(evaluate: Callable[[Any], Any], frames: int,
                         epochs: tuple[tuple[int, int], ...], rule: Quadrature) -> Any:
    np, _ = numerical_libraries()
    output = np.zeros(frames)
    for start, stop in epochs:
        length = stop - start
        relative = np.arange(length)[None, :] - rule.delta[:, None]
        source = np.asarray(evaluate(start + relative), dtype=np.float64)
        # A hard reset starts a new filter history, not merely a zero modal state.
        source = np.where(relative >= 0.0, source, 0.0)
        accumulated = np.zeros(length)
        for node in range(rule.order):
            accumulated += np.convolve(source[node], rule.taps[node], mode="full")[:length]
        output[start:stop] = accumulated / rule.normalizer
    return output


class ReferenceBank:
    """Reuse per-key loaded voltages across single-key/chord cases at one rate."""
    def __init__(self) -> None:
        self.rate: float | None = None
        self.cache: dict[Any, dict[str, Any]] = {}

    def curves(self, case: Case) -> dict[str, Any]:
        np, _ = numerical_libraries()
        if self.rate != case.sample_rate_hz:
            self.cache.clear()
            self.rate = case.sample_rate_hz
        factor = oversampling_factor(case.sample_rate_hz)
        epochs = reset_epochs(case.frames, case.events)
        total: dict[str, Any] = {}
        for key in case.keys:
            local = tuple(e for e in case.events if e.type not in ("on", "off") or e.key == key)
            signature = (key, case.frames, local)
            if signature not in self.cache:
                trajectory = KeyTrajectory(case.sample_rate_hz, key, local)
                source = trajectory.loaded_volts(np.arange(case.frames * factor) / factor)
                curves = {"source_internal_volts": source,
                          "same_rate_volts": sampled_reference(source, factor, epochs)}
                for order in ORDERS:
                    curves[f"continuous_q{order}_volts"] = continuous_reference(
                        trajectory.loaded_volts, case.frames, epochs, quadrature(order))
                for values in curves.values():
                    if not np.all(np.isfinite(values)):
                        raise ArithmeticError("Nonfinite independent reference")
                    values.setflags(write=False)
                self.cache[signature] = curves
            for name, values in self.cache[signature].items():
                if name not in total:
                    total[name] = values.copy()
                else:
                    total[name] += values
        return total


def probe_input(case: Case) -> bytes:
    return "".join(f"{e.offset} {e.type} {e.key} {float(e.value)!r}\n" for e in case.events).encode("ascii")


def parse_probe_output(raw: bytes, case: Case) -> tuple[Any, dict[str, Any]]:
    np, _ = numerical_libraries()

    def reject_constant(token: str) -> None:
        raise ValueError(f"Nonfinite JSON token: {token}")

    document = json.loads(raw, parse_constant=reject_constant)
    if not isinstance(document, dict):
        raise ValueError("Probe output must be one JSON object")
    factor = oversampling_factor(case.sample_rate_hz)
    integer_fields = {"schema_version": 1, "frames": case.frames,
                      "event_count": len(case.events), "oversampling_factor": factor,
                      "latency_frames": LATENCY_FRAMES}
    for name, expected in integer_fields.items():
        if type(document.get(name)) is not int or document[name] != expected:
            raise ValueError(f"Unexpected probe {name}")
    for name, expected in (("sample_rate_hz", case.sample_rate_hz),
                           ("internal_sample_rate_hz", case.sample_rate_hz * factor)):
        value = document.get(name)
        if type(value) not in (float, int) or float(value).hex() != expected.hex():
            raise ValueError(f"Unexpected probe {name}")
    if document.get("model") != "synthetic-filtered-audio":
        raise ValueError("Unexpected probe model identity")
    values = document.get("samples_volts")
    if (not isinstance(values, list) or len(values) != case.frames
            or any(type(value) not in (float, int) or not math.isfinite(value) for value in values)):
        raise ValueError("Probe samples must be exactly the finite float64 voltage record")
    return np.asarray(values, dtype=np.float64), {key: value for key, value in document.items()
                                                if key != "samples_volts"}


class ProbeFailure(RuntimeError):
    """An operational failure carries its raw output for immutable retention."""
    def __init__(self, message: str, stdout: bytes, stderr: bytes):
        super().__init__(message)
        self.stdout, self.stderr = stdout, stderr


def run_probe(probe: Path, case: Case) -> tuple[bytes, bytes]:
    try:
        process = subprocess.run([str(probe), "--sample-rate", repr(case.sample_rate_hz),
                                  "--frames", str(case.frames)], input=probe_input(case),
                                 capture_output=True, check=False, timeout=120)
    except subprocess.TimeoutExpired as exc:
        raise ProbeFailure("Probe exceeded its 120-second offline limit",
                           exc.stdout or b"", exc.stderr or b"") from exc
    if process.returncode:
        raise ProbeFailure(f"Probe failed ({process.returncode}): "
                           f"{process.stderr.decode('utf-8', errors='replace')}",
                           process.stdout, process.stderr)
    return process.stdout, process.stderr


def array_hash(values: Any) -> str:
    np, _ = numerical_libraries()
    return hashlib.sha256(np.asarray(values, dtype="<f8").tobytes(order="C")).hexdigest()


def error_metrics(output: Any, reference: Any, rate: float, indices: Any = None) -> dict[str, Any]:
    np, _ = numerical_libraries()
    if indices is None:
        indices = np.arange(len(output))
    indices = np.asarray(indices, dtype=np.int64)
    if indices.size == 0:
        return {"sample_count": 0, "maximum_absolute_error_volts": None,
                "maximum_error_frame": None, "maximum_error_time_seconds": None,
                "signed_error_at_maximum_volts": None, "rms_error_volts": None,
                "signed_mean_error_volts": None, "output_peak_absolute_volts": None,
                "reference_peak_absolute_volts": None}
    error = output[indices] - reference[indices]
    local = int(np.argmax(np.abs(error)))
    frame = int(indices[local])
    return {
        "sample_count": int(indices.size),
        "maximum_absolute_error_volts": float(abs(error[local])),
        "maximum_error_frame": frame, "maximum_error_time_seconds": frame / rate,
        "signed_error_at_maximum_volts": float(error[local]),
        "rms_error_volts": float(np.sqrt(np.mean(error * error))),
        "signed_mean_error_volts": float(np.mean(error)),
        "output_peak_absolute_volts": float(np.max(np.abs(output[indices]))),
        "reference_peak_absolute_volts": float(np.max(np.abs(reference[indices]))),
    }


def within_gate(metrics: dict[str, Any], peak: float, rms: float = math.inf) -> bool:
    return (metrics["maximum_absolute_error_volts"] <= peak
            and metrics["rms_error_volts"] <= rms)


def assess_case(case: Case, production: Any, curves: dict[str, Any]) -> dict[str, Any]:
    np, _ = numerical_libraries()
    a = error_metrics(production, curves["same_rate_volts"], case.sample_rate_hz)
    refinements = {
        "q16_q32": error_metrics(curves["continuous_q16_volts"], curves["continuous_q32_volts"], case.sample_rate_hz),
        "q32_q64": error_metrics(curves["continuous_q32_volts"], curves["continuous_q64_volts"], case.sample_rate_hz),
    }
    refinement_passes = {name: within_gate(value, GATES["reference_refinement_peak_volts"],
                                          GATES["reference_refinement_rms_volts"])
                          for name, value in refinements.items()}
    resolved = all(refinement_passes.values())
    b = (error_metrics(production, curves["continuous_q64_volts"], case.sample_rate_hz)
         if resolved else None)
    comparisons = {"comparison_a": (production, curves["same_rate_volts"]),
                   "q16_q32": (curves["continuous_q16_volts"], curves["continuous_q32_volts"]),
                   "q32_q64": (curves["continuous_q32_volts"], curves["continuous_q64_volts"])}
    if resolved:
        comparisons["comparison_b"] = (production, curves["continuous_q64_volts"])

    def region(indices: Any) -> dict[str, Any]:
        result = {name: error_metrics(out, ref, case.sample_rate_hz, indices)
                  for name, (out, ref) in comparisons.items()}
        if not resolved:
            result["comparison_b"] = None
        return result

    union = np.zeros(case.frames, dtype=bool)
    windows = []
    for offset in sorted({event.offset for event in case.events}):
        stop = min(case.frames, offset + SUPPORT_FRAMES + 1)
        union[offset:stop] = True
        windows.append({"start_frame": offset, "stop_frame_inclusive": stop - 1,
                        "event_indices": [i for i, event in enumerate(case.events) if event.offset == offset],
                        "metrics": region(np.arange(offset, stop))})
    return {
        "comparison_a": {"status": "pass" if within_gate(a, GATES["comparison_a_peak_volts"]) else "fail", "metrics": a},
        "reference_refinement": {"resolved": resolved, "passes": refinement_passes, "metrics": refinements},
        "comparison_b": {
            "status": ("inconclusive: reference not resolved" if not resolved else
                       "pass" if within_gate(b, GATES["comparison_b_exploratory_peak_volts"],
                                             GATES["comparison_b_exploratory_rms_volts"]) else "fail"),
            "metrics": b,
        },
        "event_windows": windows,
        "event_window_union": region(np.flatnonzero(union)),
        "outside_event_window_union": region(np.flatnonzero(~union)),
        "raw_array_sha256_little_endian_float64": {"production_volts": array_hash(production),
                                                   **{name: array_hash(value) for name, value in curves.items()}},
        "raw_array_peak_absolute_volts": {"production_volts": float(np.max(np.abs(production))),
                                           **{name: float(np.max(np.abs(value))) for name, value in curves.items()}},
    }


def write_exclusive(path: Path, content: bytes) -> None:
    with path.open("xb") as stream:
        stream.write(content)


def create_output(path: Path) -> None:
    """No --force path: a different directory is required to retain every run."""
    if path.exists() or path.is_symlink():
        raise ValueError(f"Refusing existing output path: {path}")
    path.mkdir()
    (path / "raw").mkdir()
    (path / "probe").mkdir()


def source_snapshot() -> dict[str, str]:
    return {name: file_hash(ROOT / name) for name in SOURCE_PATHS}


def execute_case(probe: Path, output: Path, case: Case, bank: ReferenceBank,
                 source_sha256: str) -> dict[str, Any]:
    np, _ = numerical_libraries()
    input_data = probe_input(case)
    write_exclusive(output / "probe" / f"{case.id}.stdin.txt", input_data)
    try:
        raw, stderr = run_probe(probe, case)
    except ProbeFailure as exc:
        write_exclusive(output / "probe" / f"{case.id}.stdout.json", exc.stdout)
        write_exclusive(output / "probe" / f"{case.id}.stderr.txt", exc.stderr)
        raise
    # Keep invalid probe stdout too: interpretation happens after publication.
    write_exclusive(output / "probe" / f"{case.id}.stdout.json", raw)
    write_exclusive(output / "probe" / f"{case.id}.stderr.txt", stderr)
    production, metadata = parse_probe_output(raw, case)
    curves = bank.curves(case)
    arrays_path = output / "raw" / f"{case.id}.npz"
    with arrays_path.open("xb") as stream:
        np.savez_compressed(stream, production_volts=production, **curves)
    result = {
        **case.document(), "input_sha256": object_hash(case.document()),
        "source_snapshot_sha256": source_sha256,
        "probe_stdin_sha256": hashlib.sha256(input_data).hexdigest(),
        "probe_stdout_sha256": hashlib.sha256(raw).hexdigest(),
        "probe_stderr_sha256": hashlib.sha256(stderr).hexdigest(),
        "probe_metadata": metadata, "raw_arrays_path": f"raw/{case.id}.npz",
        "raw_npz_file_sha256": file_hash(arrays_path),
        **assess_case(case, production, curves),
    }
    return result


def run_study(probe: Path, output: Path, suite: str) -> int:
    np, scipy = numerical_libraries()
    probe = probe.resolve(strict=True)
    if not probe.is_file():
        raise ValueError("Probe must be an executable regular file")
    plan = make_plan(suite)
    cases = build_cases(suite)
    sources = source_snapshot()
    binary_hash = file_hash(probe)
    normalizers = kernel_normalizer_report()
    create_output(output)
    write_exclusive(output / "plan.json", canonical_bytes(plan) + b"\n")
    manifest = {
        "schema_version": 1, "scope": SCOPE, "suite": suite,
        "started_at_utc": datetime.now(timezone.utc).isoformat(),
        "plan_sha256": plan["plan_sha256"], "source_sha256": sources,
        "source_snapshot_sha256": object_hash(sources),
        "probe_binary": str(probe), "probe_binary_sha256": binary_hash,
        "binary_source_binding": "Binary and current source identities are retained; the tool does not attest that the binary was built from these sources.",
        "precision": "IEEE binary64; raw array hashes use C-order little-endian float64 bytes",
        "versions": {"python": sys.version, "numpy": np.__version__, "scipy": scipy.__version__, "platform": platform.platform()},
        "kernel_dc_normalizers": normalizers,
        "reference_method": "Piecewise analytic local modal motion; local cubic flux derivative; loaded linear key sum; ordinary zero-padded causal convolution; no fitted alignment.",
        "normalizer_method": "Independent Q16/Q32/Q64 sums and separate adaptive scalar per-cell quadrature.",
    }
    write_exclusive(output / "manifest.json", canonical_bytes(manifest) + b"\n")
    counts: dict[str, Counter[str]] = {"comparison_a": Counter(), "comparison_b": Counter()}
    failures: dict[str, list[str]] = {"comparison_a": [], "comparison_b": [], "inconclusive": []}
    completed, error = 0, None
    bank = ReferenceBank()
    maxima = {"comparison_a": 0.0, "comparison_b_resolved": 0.0,
              "q16_q32": 0.0, "q32_q64": 0.0}
    try:
        with (output / "cases.jsonl").open("xb") as stream:
            for case in cases:
                row = execute_case(probe, output, case, bank, manifest["source_snapshot_sha256"])
                stream.write(canonical_bytes(row) + b"\n")
                stream.flush()
                completed += 1
                for name in counts:
                    status = row[name]["status"]
                    counts[name][status] += 1
                    if status == "fail":
                        failures[name].append(case.id)
                    elif status.startswith("inconclusive"):
                        failures["inconclusive"].append(case.id)
                maxima["comparison_a"] = max(maxima["comparison_a"], row["comparison_a"]["metrics"]["maximum_absolute_error_volts"])
                if row["comparison_b"]["metrics"] is not None:
                    maxima["comparison_b_resolved"] = max(maxima["comparison_b_resolved"], row["comparison_b"]["metrics"]["maximum_absolute_error_volts"])
                for name in ("q16_q32", "q32_q64"):
                    maxima[name] = max(maxima[name], row["reference_refinement"]["metrics"][name]["maximum_absolute_error_volts"])
                print(f"[{completed}/{len(cases)}] {case.id}: A={row['comparison_a']['status']}; B={row['comparison_b']['status']}", file=sys.stderr, flush=True)
    except (OSError, ValueError, ArithmeticError, RuntimeError, subprocess.SubprocessError, KeyboardInterrupt) as exc:
        error = f"{type(exc).__name__}: {exc}"
    try:
        final_sources, final_binary_hash = source_snapshot(), file_hash(probe)
        unchanged = final_sources == sources and final_binary_hash == binary_hash
    except OSError as exc:
        unchanged = False
        error = error or f"Source/binary recheck failed: {exc}"
    if not unchanged:
        error = error or "Source or binary bytes changed during this run"
    incomplete = completed != len(cases) or error is not None
    code = (3 if incomplete else 1 if failures["comparison_a"] or failures["comparison_b"]
            else 2 if failures["inconclusive"] else 0)
    summary = {
        "schema_version": 1, "scope": SCOPE, "suite": suite,
        "status": "incomplete" if incomplete else "complete", "exit_code": code,
        "plan_sha256": plan["plan_sha256"], "expected_cases": len(cases), "completed_cases": completed,
        "counts": counts, "case_ids_requiring_attention": failures,
        "maximum_observed_absolute_errors_volts": maxima,
        "source_and_binary_bytes_unchanged": unchanged, "operational_error": error,
        "cases_jsonl_sha256": file_hash(output / "cases.jsonl"),
        "finished_at_utc": datetime.now(timezone.utc).isoformat(),
    }
    write_exclusive(output / "summary.json", canonical_bytes(summary) + b"\n")
    print(json.dumps(summary, indent=2, allow_nan=False))
    return code


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--plan", action="store_true", help="print the complete deterministic plan without optional numerical dependencies or a probe")
    parser.add_argument("--suite", choices=("full", "pilot"), default="full", help="full is the fixed 2097-case matrix; pilot is the fixed development-only 36-case subset")
    parser.add_argument("--probe", type=Path, help="rhodes_audio_probe executable; required for a numerical run")
    parser.add_argument("--output", type=Path, help="new result directory with an existing parent; existing paths are refused")
    args = parser.parse_args(argv)
    if args.plan:
        if args.probe is not None or args.output is not None:
            parser.error("--plan writes only stdout; omit --probe and --output")
        print(json.dumps(make_plan(args.suite), indent=2, allow_nan=False))
        return 0
    if args.probe is None or args.output is None:
        parser.error("a numerical run requires --probe and --output (or use --plan)")
    try:
        return run_study(args.probe, args.output, args.suite)
    except (OSError, ValueError, ArithmeticError, RuntimeError, subprocess.SubprocessError) as exc:
        print(f"Filtered audio diagnostic failed: {exc}", file=sys.stderr)
        return 3


if __name__ == "__main__":
    sys.exit(main())
