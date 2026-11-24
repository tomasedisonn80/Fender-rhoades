#!/usr/bin/env python3
"""Strict native input for the frozen two-fixture, depth-one contact matrix.

``parse_candidate(raw: bytes)`` returns the existing immutable ParsedCandidate
only after validating the supported-impact fixture semantics. Its availability
describes optional peak data, never producer success or numerical acceptance.
``validate_collection(candidates)`` returns deterministic, JSON-serializable
matrix diagnostics; missing and duplicate slots are retained, not selected away.

The original four-fixture parser and its defaults are unchanged. It supplies
bounded JSON/envelope/SI-field parsing here, but its unsupported-fixture path is
not sufficient validation by itself. This module needs only the standard
library and does not execute a producer, numerical solver, or peak reference.
"""
from __future__ import annotations

from collections import defaultdict
from dataclasses import replace
import hashlib
import json
from typing import Any, Iterable

import contact_peak_schema as _native
from contact_peak_schema import (CandidateFormatError, MAX_COLLECTION_BYTES,
                                 MAX_FILE_BYTES, MAX_FILES, PEAK_NAMES,
                                 ParsedCandidate)
from validate_contact_reference import (RUN_SETTING_FIELDS, fixtures, observe,
                                        refinement_slot_id, refinement_slots)


SUPPORTED_FIXTURES = frozenset({"supported_impact", "supported_impact_scaled"})
EXTRA_STEP_LEVELS = 1
EXPECTED_SETTINGS = tuple(sorted(refinement_slots(EXTRA_STEP_LEVELS)))
EXPECTED_COUNT = 26
MATRIX_ID = "contact-refinement-20261123-extra-1"
MATRIX_SETTINGS_SHA256 = "802f018edbad9396df0af0099cd1d0bf6b0d0a9621b0a5ae95688a283d31e17b"
_EXPECTED_KEYS = tuple((name, settings) for name in sorted(SUPPORTED_FIXTURES)
                       for settings in EXPECTED_SETTINGS)
_EXPECTED_KEY_SET = frozenset(_EXPECTED_KEYS)
_DEFINITIONS = {name: fixtures()[name] for name in SUPPORTED_FIXTURES}
# include/rhodes/contact_reference.hpp: Failure::none through unresolved_event.
_KNOWN_FAILURE_CODES = frozenset(range(22))
_ZERO_SAMPLE_FIELDS = (
    "damper_force_n", "damper_impulse_ns", "damper_loss_j",
    "damper_clipped_loss_j", "hammer_loss_j", "hammer_clipped_loss_j",
    "actuator_work_j", "actuator_power_w", "launch_work_j",
)


def _fail(message: str) -> None:
    raise CandidateFormatError(message[:384])


def _check_frozen_matrix() -> None:
    """Fail closed if a shared future policy silently changes this experiment."""
    settings = [{"fixture": name, "settings": dict(zip(RUN_SETTING_FIELDS, slot))}
                for name, slot in _EXPECTED_KEYS]
    payload = json.dumps(settings, sort_keys=True, separators=(",", ":"),
                         allow_nan=False).encode("utf-8")
    if len(EXPECTED_SETTINGS) != 13 or len(_EXPECTED_KEYS) != EXPECTED_COUNT or \
            hashlib.sha256(payload).hexdigest() != MATRIX_SETTINGS_SHA256:
        raise RuntimeError("supported-impact depth-one settings differ from the frozen matrix")


_check_frozen_matrix()


def _settings(candidate: ParsedCandidate) -> tuple[float, ...]:
    return tuple(value for _, value in candidate.settings)


def _validate_supported(candidate: ParsedCandidate) -> None:
    """Add every fixture invariant skipped by the old unsupported-name path."""
    name, run = candidate.fixture, candidate.run
    if name not in SUPPORTED_FIXTURES:
        _fail("run.fixture is outside the two supported-impact fixtures")
    if tuple(field for field, _ in candidate.settings) != RUN_SETTING_FIELDS or \
            _settings(candidate) not in EXPECTED_SETTINGS:
        _fail("run settings do not match an exact frozen depth-one slot")
    if run["failure_code"] not in _KNOWN_FAILURE_CODES:
        _fail("run.failure_code is not a known native Failure enum")
    fixture = _DEFINITIONS[name]
    if fixture.relaunch or fixture.kh <= 0 or fixture.ch != 0 or \
            fixture.kd != 0 or fixture.cd != 0 or fixture.duration != 0.020 or \
            fixture.motion(0.0) != (0.0, 0.0) or fixture.face != "constant":
        _fail("independent supported-impact fixture definition changed")

    samples = run["samples"]
    # observe() is pure standard-library arithmetic; no oracle/backend is loaded.
    initial = (*fixture.q, *fixture.v, fixture.x, fixture.vh, *([0.0] * 7))
    expected = observe(fixture, 0.0, initial)
    expected.update(hammer_clipped_loss_j=0.0, damper_clipped_loss_j=0.0)
    for field, target in expected.items():
        actual = samples[0][field]
        if isinstance(target, list):
            actual, target = tuple(actual), tuple(target)
        if actual != target:
            _fail("initial " + field + " does not exactly match the independent fixture")

    if any(sample["time_s"] > fixture.duration for sample in samples):
        _fail("sample time exceeds the exact supported-impact duration")
    if run["success"] and samples[-1]["time_s"] != fixture.duration:
        _fail("successful trajectory must end at the exact supported-impact duration")
    if run["relaunch_snapshots"]:
        _fail("supported-impact fixtures do not contain action snapshots")
    for index, sample in enumerate(samples):
        for field in _ZERO_SAMPLE_FIELDS:
            if sample[field] != 0:
                _fail("samples[" + str(index) + "]." + field +
                      " contradicts the disabled damper or action-free undamped contact")

    for index, event in enumerate(run["events"]):
        label = "events[" + str(index) + "]"
        if event["contact"] != "hammer":
            _fail(label + " reports an event for the disabled damper")
        if event["kind"] not in {"onset", "separation", "grazing"}:
            _fail(label + " reports an action or clamp absent from the fixture")
        if event["work_j"] != 0:
            _fail(label + " reports action work for an action-free fixture")
        if event["bracket_hi_s"] - event["bracket_lo_s"] > \
                run["event_tolerance_s"] * (1 + 1e-9):
            _fail(label + " bracket exceeds event tolerance")

    diagnostics = run["diagnostics"]
    if diagnostics["initial_energy_j"] != expected["energy_j"]:
        _fail("diagnostics.initial_energy_j differs from the exact initial state")
    sampled = diagnostics["sampled_maxima"]
    for field in ("damper_compression_m", "damper_compression_time_s",
                  "damper_force_n", "damper_force_time_s"):
        if sampled[field] != 0:
            _fail("disabled damper sampled maxima must have zero value and time")

    if candidate.peaks is None:
        return
    for name in PEAK_NAMES:
        peak = candidate.peaks[name]
        if peak["origin"] in {"action_before", "action_after"}:
            _fail("peak." + name + " reports an action absent from the fixture")
        if (peak["origin"] == "disabled") != name.startswith("damper_"):
            _fail("peak." + name + " disabled semantics disagree with the fixture")
        # The native parser has already checked exact unit keys, finite and
        # nonnegative values, duration bounds, bracket containment/width, and
        # all-zero disabled records. Do not infer extrema acceptance here.


def parse_candidate(raw: bytes) -> ParsedCandidate:
    """Parse one frozen-slot run, preserving failed producers and absent peaks.

    Successful runs must cover the exact full duration. A failed native run may
    retain a partial trajectory; ``run['success']`` remains False. Peak
    availability is ``available`` or ``not_available`` independently of that
    status. Any malformed supplied peak object raises CandidateFormatError.
    Other fixture names and off-matrix settings are rejected, not promoted.
    """
    candidate = _native.parse_candidate(raw)
    _validate_supported(candidate)
    return replace(candidate, availability=("available" if candidate.peaks is not None
                                            else "not_available"))


def validate_collection(candidates: Iterable[ParsedCandidate]) -> dict[str, Any]:
    """Describe exact 26-slot coverage without granting an acceptance pass.

    Inputs must be results of this module's parse_candidate. At most MAX_FILES
    are consumed. Byte-identical repetitions are duplicates too; no run wins a
    slot by input order. Diagnostics and rows are stable under input permutation.
    ``matrix_complete`` checks one occurrence of each expected slot only.
    Producer failures and missing optional peaks have separate retained lists.
    The caller must enforce MAX_COLLECTION_BYTES while reading raw files because
    ParsedCandidate retains the byte hash, not the original byte length.
    """
    groups: dict[tuple[str, tuple[float, ...]], list[ParsedCandidate]] = defaultdict(list)
    unexpected = []
    count = 0
    for candidate in candidates:
        count += 1
        if count > MAX_FILES:
            _fail("candidate collection exceeds the 64-file limit")
        if type(candidate) is not ParsedCandidate:
            _fail("collection entries must be ParsedCandidate records")
        if tuple(field for field, _ in candidate.settings) != RUN_SETTING_FIELDS or \
                candidate.fixture != candidate.run["fixture"] or \
                any(value != candidate.run[field] for field, value in candidate.settings):
            _fail("collection candidate metadata differs from its parsed run")
        key = (candidate.fixture, _settings(candidate))
        if key not in _EXPECTED_KEY_SET:
            unexpected.append({"fixture": candidate.fixture, "settings": dict(candidate.settings),
                               "raw_sha256": candidate.raw_sha256})
            continue
        _validate_supported(candidate)
        expected_availability = "available" if candidate.peaks is not None else "not_available"
        if candidate.availability != expected_availability:
            _fail("collection candidate must come from the supported-impact parser")
        groups[key].append(candidate)

    missing = [refinement_slot_id(*key) for key in _EXPECTED_KEYS if key not in groups]
    duplicate = []
    failed = []
    unavailable = []
    for key in sorted(groups):
        rows = sorted(groups[key], key=lambda row: row.raw_sha256)
        slot = refinement_slot_id(*key)
        if len(rows) > 1:
            hashes = [row.raw_sha256 for row in rows]
            duplicate.append({"slot": slot, "count": len(rows), "raw_sha256": hashes,
                              "byte_identical": len(set(hashes)) == 1})
        for candidate in rows:
            if candidate.run["success"] is not True or candidate.run["failure_code"] != 0:
                failed.append({"slot": slot, "raw_sha256": candidate.raw_sha256,
                               "failure_code": candidate.run["failure_code"]})
            if candidate.availability != "available":
                unavailable.append({"slot": slot, "raw_sha256": candidate.raw_sha256,
                                    "availability": candidate.availability})
    unexpected.sort(key=lambda row: (row["fixture"],
                                    tuple(row["settings"][field] for field in RUN_SETTING_FIELDS),
                                    row["raw_sha256"]))
    diagnostics = ["missing slot: " + slot for slot in missing]
    diagnostics.extend("duplicate slot: " + row["slot"] + " (" + str(row["count"]) + " records)"
                       for row in duplicate)
    diagnostics.extend("unexpected slot: " + row["fixture"] + " sha256=" + row["raw_sha256"]
                       for row in unexpected)
    diagnostics.extend("producer failure: " + row["slot"] + " code=" + str(row["failure_code"])
                       for row in failed)
    diagnostics.extend("peak diagnostics not available: " + row["slot"] for row in unavailable)
    return {
        "scope": "supported-impact-depth-one-input-matrix", "matrix_id": MATRIX_ID,
        "settings_sha256": MATRIX_SETTINGS_SHA256, "expected_count": EXPECTED_COUNT,
        "actual_count": count, "distinct_expected_slots": len(groups),
        "matrix_complete": not missing and not duplicate and not unexpected,
        "missing_slots": missing, "duplicate_slots": duplicate, "unexpected_slots": unexpected,
        "failed_runs": failed, "unavailable_peaks": unavailable, "diagnostics": diagnostics,
        "numerical_acceptance": "not_evaluated",
    }
