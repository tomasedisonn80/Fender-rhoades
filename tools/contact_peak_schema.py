#!/usr/bin/env python3
"""Strict, bounded native input for the four-fixture contact peak diagnostic.

Only the standard library is needed. Fixture names and initial conditions are
consistency checks, not authentication of a producer or measured parameters.
The independent fixture definitions are imported without loading NumPy/SciPy.
"""
from __future__ import annotations

from dataclasses import dataclass
import hashlib
import json
import math
from typing import Any, Mapping

from validate_contact_reference import RUN_SETTING_FIELDS, fixtures

MAX_FILE_BYTES = 32 * 1024 * 1024
MAX_COLLECTION_BYTES = 128 * 1024 * 1024
MAX_FILES = 64
MAX_DEPTH = 64
MAX_INTEGER = 2**64 - 1
MAX_SAMPLES = 30000
MAX_EVENTS = 100000
SUPPORTED_FIXTURES = frozenset({
    "conservative_impact", "damped_impact", "forced_clamp", "preload_equilibrium"})
PEAK_NAMES = ("hammer_compression", "hammer_force", "damper_compression", "damper_force")
SAMPLE_FIELDS = frozenset({
    "time_s", "q_m", "v_m_per_s", "hammer_position_m", "hammer_velocity_m_per_s",
    "hammer_force_n", "damper_force_n", "hammer_gap_m", "damper_gap_m",
    "hammer_relative_velocity_m_per_s", "damper_relative_velocity_m_per_s",
    "hammer_impulse_ns", "damper_impulse_ns", "modal_loss_j", "hammer_loss_j",
    "damper_loss_j", "hammer_clipped_loss_j", "damper_clipped_loss_j",
    "actuator_work_j", "launch_work_j", "energy_j", "modal_acceleration_m_per_s2",
    "hammer_acceleration_m_per_s2", "instantaneous_power_residual_w",
    "actuator_power_w", "pickup_voltage_v"})
ARRAY_FIELDS = frozenset({"q_m", "v_m_per_s", "modal_acceleration_m_per_s2"})
COUNTER_FIELDS = frozenset({"accepted_steps", "rejected_steps", "rhs_evaluations",
    "analytic_free_evaluations", "event_subdivisions", "root_iterations", "grazing_candidates"})
DIAGNOSTIC_SCALARS = frozenset({"max_abs_cumulative_residual_j",
    "sum_abs_interval_residual_j", "maximum_power_residual_w", "initial_energy_j",
    "minimum_accepted_step_s", "maximum_accepted_step_s"})
DIAGNOSTIC_FIELDS = COUNTER_FIELDS | DIAGNOSTIC_SCALARS | {"sampled_maxima"}
SAMPLED_SCOPE = "accepted endpoints and checked interior samples; not continuous-time extrema"
PEAK_COVERAGE = "uncertified_sign_change_search"
PEAK_METHOD = "fixed_interval_anchor_replay_two_rk4_half_steps_or_certified_free_motion"


class CandidateFormatError(ValueError):
    """A bounded malformed-input result; original bytes belong in CLI evidence."""


class _FrozenDict(dict):
    """JSON-serializable mapping with ordinary mutation operations disabled."""
    def _immutable(self, *args, **kwargs):
        raise TypeError("parsed candidate records are immutable")

    __setitem__ = __delitem__ = clear = pop = popitem = setdefault = update = __ior__ = _immutable

    def __copy__(self):
        return self

    def __deepcopy__(self, memo):
        return self


def _freeze(value):
    if isinstance(value, dict):
        return _FrozenDict({key: _freeze(item) for key, item in value.items()})
    if isinstance(value, list):
        return tuple(_freeze(item) for item in value)
    return value


@dataclass(frozen=True)
class ParsedCandidate:
    fixture: str
    settings: tuple[tuple[str, float], ...]
    run: Mapping[str, Any]
    peaks: Mapping[str, Any] | None
    availability: str
    raw_sha256: str


def _fail(message):
    raise CandidateFormatError(str(message)[:384])


def _object(value, fields, label, *, optional=()):
    if type(value) is not dict:
        _fail(label + " must be an object")
    actual = set(value)
    if not set(fields).issubset(actual) or actual - set(fields) - set(optional):
        _fail(label + " has missing or unknown fields")
    return value


def _number(value, label, *, nonnegative=False, positive=False):
    if type(value) not in (int, float):
        _fail(label + " must be a non-boolean finite number")
    try:
        value = float(value)
    except (ValueError, OverflowError):
        _fail(label + " cannot be converted to a finite number")
    if not math.isfinite(value) or (nonnegative and value < 0) or (positive and value <= 0):
        _fail(label + " is outside its finite numeric domain")
    return value


def _integer(value, label, *, maximum=MAX_INTEGER):
    if type(value) is not int or not 0 <= value <= maximum:
        _fail(label + " must be a bounded nonnegative integer")
    return value


def _status(value, failure, label):
    if type(value) is not bool:
        _fail(label + ".success must be a literal boolean")
    _integer(failure, label + ".failure_code")
    if value != (failure == 0):
        _fail(label + " success/failure_code are inconsistent")


def _sample(value, label):
    _object(value, SAMPLE_FIELDS, label)
    normalized = {}
    for name, item in value.items():
        if name in ARRAY_FIELDS:
            if type(item) is not list or len(item) != 2:
                _fail(label + "." + name + " must contain exactly two components")
            normalized[name] = [_number(x, label + "." + name) for x in item]
        else:
            normalized[name] = _number(item, label + "." + name)
    if normalized["time_s"] < 0:
        _fail(label + ".time_s must be nonnegative")
    return normalized


def _time(value, label, duration):
    value = _number(value, label, nonnegative=True)
    if value > duration:
        _fail(label + " exceeds the fixture duration")
    return value


def _bracket(record, label, duration):
    time = _time(record["time_s"], label + ".time_s", duration)
    lo = _time(record["bracket_lo_s"], label + ".bracket_lo_s", duration)
    hi = _time(record["bracket_hi_s"], label + ".bracket_hi_s", duration)
    if not lo <= time <= hi:
        _fail(label + " does not enclose its reported time")
    return time, lo, hi


def _initial_state(sample, fixture):
    expected = {"q_m": fixture.q, "v_m_per_s": fixture.v,
                "hammer_position_m": (fixture.x,), "hammer_velocity_m_per_s": (fixture.vh,)}
    for name, values in expected.items():
        actual = sample[name] if name in ARRAY_FIELDS else (sample[name],)
        for observed, target in zip(actual, values):
            if target == 0.0:
                matches = observed == 0.0
            else:
                matches = abs(observed - target) <= 64 * math.ulp(target)
            if not matches:
                _fail("initial " + name + " does not match the independent fixture")


def _diagnostics(raw, run, duration, supported, damper_enabled):
    _object(raw, DIAGNOSTIC_FIELDS, "diagnostics", optional=("located_peak_candidates",))
    normalized = {}
    for name in DIAGNOSTIC_SCALARS:
        normalized[name] = _number(raw[name], "diagnostics." + name, nonnegative=True)
    for name in COUNTER_FIELDS:
        normalized[name] = _integer(raw[name], "diagnostics." + name)
    lo, hi = normalized["minimum_accepted_step_s"], normalized["maximum_accepted_step_s"]
    if lo > hi:
        _fail("diagnostics accepted-step interval is reversed")
    # Analytic evaluations count trial/replay work, not accepted intervals.
    accepted = normalized["accepted_steps"]
    if accepted:
        if lo <= 0 or hi <= 0:
            _fail("accepted work requires positive accepted-step bounds")
    elif lo != 0 or hi != 0:
        _fail("zero accepted work requires zero accepted-step bounds")
    if run["success"] and accepted == 0:
        _fail("successful complete run requires accepted work")

    sampled_fields = {"scope"}
    for name in PEAK_NAMES:
        sampled_fields |= {name + ("_m" if name.endswith("compression") else "_n"), name + "_time_s"}
    sampled = _object(raw["sampled_maxima"], sampled_fields, "sampled_maxima")
    if sampled["scope"] != SAMPLED_SCOPE:
        _fail("sampled_maxima scope is not the native diagnostic scope")
    normalized_sampled = {"scope": SAMPLED_SCOPE}
    for name in PEAK_NAMES:
        unit = "_m" if name.endswith("compression") else "_n"
        normalized_sampled[name + unit] = _number(sampled[name + unit], "sampled_maxima." + name, nonnegative=True)
        normalized_sampled[name + "_time_s"] = _time(sampled[name + "_time_s"], "sampled_maxima." + name + "_time_s", duration)
        if supported and name.startswith("damper") and not damper_enabled:
            if normalized_sampled[name + unit] != 0 or normalized_sampled[name + "_time_s"] != 0:
                _fail("disabled damper sampled maxima must have zero value and time")
    normalized["sampled_maxima"] = normalized_sampled
    if "located_peak_candidates" not in raw:
        return normalized, None

    peaks = _object(raw["located_peak_candidates"], {"coverage", "evaluation_method", *PEAK_NAMES}, "located_peak_candidates")
    if peaks["coverage"] != PEAK_COVERAGE or peaks["evaluation_method"] != PEAK_METHOD:
        _fail("located_peak_candidates has invalid native coverage/method metadata")
    normalized_peaks = {"coverage": PEAK_COVERAGE, "evaluation_method": PEAK_METHOD}
    for name in PEAK_NAMES:
        unit = "value_m" if name.endswith("compression") else "value_n"
        peak = _object(peaks[name], {unit, "time_s", "bracket_lo_s", "bracket_hi_s", "origin"}, "peak." + name)
        value = _number(peak[unit], "peak." + name + "." + unit, nonnegative=True)
        time, low, high = _bracket(peak, "peak." + name, duration)
        origin = peak["origin"]
        allowed = {"sample", "stationary_candidate", "disabled"}
        if not supported:
            allowed |= {"action_before", "action_after"}
        if type(origin) is not str or origin not in allowed:
            _fail("peak." + name + " has an invalid origin")
        if origin == "stationary_candidate":
            if high - low > run["event_tolerance_s"] * (1 + 1e-9):
                _fail("stationary peak bracket exceeds event tolerance")
        elif low != high:
            _fail("sample/action/disabled peak brackets must be degenerate")
        if origin == "disabled" and any(x != 0 for x in (value, time, low, high)):
            _fail("disabled peak record must have all numeric fields zero")
        if supported:
            expected_disabled = name.startswith("damper") and not damper_enabled
            if (origin == "disabled") != expected_disabled:
                _fail("peak disabled semantics disagree with the independent fixture")
        normalized_peaks[name] = {unit: value, "time_s": time, "bracket_lo_s": low,
                                  "bracket_hi_s": high, "origin": origin}
    normalized["located_peak_candidates"] = normalized_peaks
    return normalized, normalized_peaks


def _parse_document(document, raw_hash):
    _object(document, {"schema_version", "model", "runs"}, "candidate")
    if type(document["schema_version"]) is not int or document["schema_version"] != 1:
        _fail("candidate schema_version must be integer 1")
    if document["model"] != "synthetic-contact-p1":
        _fail("candidate model is unsupported")
    if type(document["runs"]) is not list or len(document["runs"]) != 1:
        _fail("candidate must contain exactly one run")
    run = _object(document["runs"][0], {
        "fixture", "success", "failure_code", "samples", "relaunch_snapshots", "events", "diagnostics",
        *RUN_SETTING_FIELDS}, "run")
    name = run["fixture"]
    if type(name) is not str or not name:
        _fail("run.fixture must be a nonempty string")
    _status(run["success"], run["failure_code"], "run")
    normalized = {"fixture": name, "success": run["success"], "failure_code": run["failure_code"]}
    for setting in RUN_SETTING_FIELDS:
        normalized[setting] = _number(run[setting], setting, positive=True)
    samples = run["samples"]
    if type(samples) is not list or not 1 <= len(samples) <= MAX_SAMPLES:
        _fail("samples must contain 1..30000 observations")
    normalized_samples = [_sample(sample, "samples[" + str(index) + "]") for index, sample in enumerate(samples)]
    times = [sample["time_s"] for sample in normalized_samples]
    if times[0] != 0 or any(left >= right for left, right in zip(times, times[1:])):
        _fail("sample times must start at zero and increase strictly")
    normalized["samples"] = normalized_samples
    supported = name in SUPPORTED_FIXTURES
    fixture = fixtures().get(name)
    duration = fixture.duration if fixture is not None else times[-1]
    if any(time > duration + 1e-12 for time in times):
        _fail("sample time exceeds the named fixture duration")
    if supported:
        _initial_state(normalized_samples[0], fixture)
        if run["success"] and abs(times[-1] - duration) > 1e-12:
            _fail("successful supported trajectory does not cover the complete fixture duration")

    snapshots = run["relaunch_snapshots"]
    if type(snapshots) is not list or len(snapshots) > MAX_EVENTS:
        _fail("relaunch_snapshots must be a bounded list")
    if supported and snapshots:
        _fail("supported analytic fixtures do not contain actions")
    normalized_snapshots = []
    for index, snapshot in enumerate(snapshots):
        label = "relaunch_snapshots[" + str(index) + "]"
        _object(snapshot, {"time_s", "requested_velocity_m_per_s", "success", "failure_code", "work_j", "before", "after"}, label)
        _status(snapshot["success"], snapshot["failure_code"], label)
        time = _time(snapshot["time_s"], label + ".time_s", duration)
        before, after = _sample(snapshot["before"], label + ".before"), _sample(snapshot["after"], label + ".after")
        if before["time_s"] != time or after["time_s"] != time:
            _fail(label + " before/after times differ from its action time")
        normalized_snapshots.append({"time_s": time,
            "requested_velocity_m_per_s": _number(snapshot["requested_velocity_m_per_s"], label + ".requested_velocity_m_per_s"),
            "success": snapshot["success"], "failure_code": snapshot["failure_code"],
            "work_j": _number(snapshot["work_j"], label + ".work_j"), "before": before, "after": after})
    normalized["relaunch_snapshots"] = normalized_snapshots
    events = run["events"]
    if type(events) is not list or len(events) > MAX_EVENTS:
        _fail("events must contain at most 100000 rows")
    normalized_events = []
    previous_time = 0.0
    for index, event in enumerate(events):
        label = "events[" + str(index) + "]"
        _object(event, {"contact", "kind", "time_s", "bracket_lo_s", "bracket_hi_s", "gap_uncertainty_m", "work_j"}, label)
        if event["contact"] not in ("hammer", "damper"):
            _fail(label + " has invalid contact enum")
        kinds = {"onset", "separation", "clamp_on", "clamp_off", "grazing"}
        if not supported:
            kinds |= {"relaunch", "motion_change"}
        if type(event["kind"]) is not str or event["kind"] not in kinds:
            _fail(label + " has invalid event enum")
        time, low, high = _bracket(event, label, duration)
        if time < previous_time:
            _fail("events must be in chronological order")
        previous_time = time
        normalized_events.append({"contact": event["contact"], "kind": event["kind"],
            "time_s": time, "bracket_lo_s": low, "bracket_hi_s": high,
            "gap_uncertainty_m": _number(event["gap_uncertainty_m"], label + ".gap_uncertainty_m", nonnegative=True),
            "work_j": _number(event["work_j"], label + ".work_j")})
    normalized["events"] = normalized_events
    normalized["diagnostics"], peaks = _diagnostics(run["diagnostics"], normalized,
        duration, supported, name == "preload_equilibrium")
    immutable = _freeze(normalized)
    return ParsedCandidate(name, tuple((field, normalized[field]) for field in RUN_SETTING_FIELDS),
        immutable, immutable["diagnostics"].get("located_peak_candidates") if peaks is not None else None,
        "unsupported_fixture" if not supported else "available" if peaks is not None else "not_available", raw_hash)


def _pairs(pairs):
    result = {}
    for name, value in pairs:
        if name in result:
            _fail("duplicate JSON object key")
        result[name] = value
    return result


def _json_integer(text):
    if len(text.lstrip("-")) > 20:
        _fail("JSON integer exceeds 64-bit magnitude limit")
    value = int(text)
    if abs(value) > MAX_INTEGER:
        _fail("JSON integer exceeds 64-bit magnitude limit")
    return value


def _json_float(text):
    value = float(text)
    if not math.isfinite(value):
        _fail("JSON number is nonfinite or overflowed")
    return value


def _check_depth(text):
    depth = 0
    quoted = escaped = False
    for character in text:
        if quoted:
            if escaped:
                escaped = False
            elif character == "\\":
                escaped = True
            elif character == '"':
                quoted = False
        elif character == '"':
            quoted = True
        elif character in "[{":
            depth += 1
            if depth > MAX_DEPTH:
                _fail("JSON nesting exceeds depth 64")
        elif character in "]}":
            depth -= 1


def parse_candidate(raw: bytes) -> ParsedCandidate:
    """Parse exactly one strict native run without executing a producer/oracle."""
    if type(raw) is not bytes:
        _fail("candidate input must be bytes")
    if len(raw) > MAX_FILE_BYTES:
        _fail("candidate exceeds the 32 MiB byte limit")
    try:
        text = raw.decode("utf-8")
        _check_depth(text)
        document = json.loads(text, object_pairs_hook=_pairs, parse_int=_json_integer,
                              parse_float=_json_float,
                              parse_constant=lambda value: _fail("nonfinite JSON constant"))
        return _parse_document(document, hashlib.sha256(raw).hexdigest())
    except CandidateFormatError:
        raise
    except (UnicodeError, json.JSONDecodeError, OverflowError, RecursionError, ValueError, TypeError) as error:
        _fail("malformed native candidate: " + str(error))
