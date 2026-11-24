#!/usr/bin/env python3
"""Bounded, retained four-case stationary primitive alias diagnostic.

Only Python's standard library and the frozen independent oracle are used.
Success concerns this synthetic stationary primitive experiment, never alias
acceptability, whole-engine convergence, C03, fidelity, or realtime deadlines.

Public test seams are plan(), validate_probe_document(raw, fixture),
run_probe(probe, fixture, output, deadline), and execute(probe, output_dir,
build_record=None). ArtifactStore owns a NEW directory. No helper silently
overwrites an existing artifact. run_probe performs one case for focused tests;
the execution CLI always plans the same four cases and 6,144 samples.
"""

from __future__ import annotations

import argparse
from contextlib import contextmanager
import csv
import hashlib
import io
import json
import math
import os
from pathlib import Path
import queue
import signal
import stat
import struct
import subprocess
import sys
import threading
import time
import traceback
from typing import Any, Iterator

import direct_alias_oracle as oracle


ROOT = Path(__file__).resolve().parents[1]
ORACLE_SHA256 = "2bfed9f0c3e75fce9fed9a866756f9caa4d45728a7150912790b4caf2a71ff6c"
PRODUCER_SHA256 = "e1cba5cd3b31e07954afae6b6740ab6fd22914becea50eede959506c2a41c808"
MAX_STREAM_BYTES = 1024 * 1024
MAX_ARTIFACT_BYTES = 16 * 1024 * 1024
FINAL_RESERVE_BYTES = 1024 * 1024
MAX_MEMORY_BYTES = 256 * 1024 * 1024
WALL_SECONDS = 30.0
CLEANUP_RESERVE_SECONDS = 3.0
MAX_JSON_DEPTH = 12
MAX_JSON_TOKENS = 65536
MAX_JSON_STRING = 256
MAX_ARTIFACTS = 48
MAX_RECEIPT_BYTES = 64 * 1024
MAX_SOURCE_BYTES = 2 * 1024 * 1024
MAX_EXECUTABLE_BYTES = 128 * 1024 * 1024
IO_CHUNK = 16384
CASE_IDS = ("one_mode_48000", "one_mode_96000", "two_modes_48000", "two_modes_96000")
EXPECTED_SAMPLES = 6144
RECEIPT_SOURCES = (
    "apps/alias_primitive_probe.cpp", "src/research_engine.cpp",
    "include/rhodes/research_engine.hpp", "include/rhodes/control_value.hpp",
    "docs/agents/reports/20261124b-w2-025-alias-diagnostic-design.md",
)
SOURCE_PATHS = RECEIPT_SOURCES + (
    "tools/direct_alias_oracle.py", "tools/validate_direct_alias_toy.py",
    "docs/agents/reports/20261124b-w2-028-alias-oracle.md",
    "docs/agents/reports/20261124b-w2-029-alias-primitive-probe.md",
)


class DiagnosticError(ValueError):
    """A bounded input, resource, retention, or diagnostic failure."""


class DeadlineExceeded(DiagnosticError):
    """The experiment deadline expired; subsequent cases must not run."""


def _check_deadline(deadline: float | None) -> None:
    if deadline is not None and time.monotonic() >= deadline:
        raise DeadlineExceeded("bounded experiment phase deadline exceeded")


def _number(value: Any, label: str) -> float:
    if type(value) not in (int, float):
        raise DiagnosticError(f"{label}: require a finite number, not a boolean")
    try:
        result = float(value)
    except (OverflowError, ValueError) as exc:
        raise DiagnosticError(f"{label}: cannot represent a finite binary64 value") from exc
    if not math.isfinite(result):
        raise DiagnosticError(f"{label}: nonfinite value")
    return result


def _object(value: Any, keys: set[str], label: str) -> dict:
    if type(value) is not dict or set(value) != keys:
        raise DiagnosticError(f"{label}: wrong object type or exact key set")
    return value


def _exact_number(value: Any, expected: float, label: str) -> None:
    if _number(value, label) != expected:
        raise DiagnosticError(f"{label}: differs from frozen fixture")


def _exact_int(value: Any, expected: int, label: str) -> None:
    if type(value) is not int or value != expected:
        raise DiagnosticError(f"{label}: require exact integer {expected}")


def _fixed_fixtures() -> tuple:
    result = oracle.fixtures()
    if type(result) is not tuple or len(result) != 4:
        raise DiagnosticError("oracle fixture count/type changed")
    for index, fixture in enumerate(result):
        rate = 48000 if index % 2 == 0 else 96000
        modes = ((13500, 1e-4, math.pi / 7),) if index < 2 else (
            (7500, 1e-4, math.pi / 7), (13500, 6e-5, -math.pi / 5))
        expected = (CASE_IDS[index], rate, rate * 1024 // 48000, modes,
                    (0.4, 800.0, 400000.0), 0.001, 0.0)
        actual = (fixture.case_id, fixture.sample_rate_hz, fixture.sample_count,
                  tuple((m.frequency_hz, m.amplitude_m, m.phase_rad) for m in fixture.modes),
                  fixture.coefficients, fixture.mass_kg, fixture.decay_per_s)
        if type(fixture) is not oracle.Fixture or actual != expected:
            raise DiagnosticError("oracle fixture differs from independently frozen constants")
    if sum(f.sample_count for f in result) != EXPECTED_SAMPLES:
        raise DiagnosticError("total sample counter differs from 6144")
    return result


def plan() -> dict[str, Any]:
    """A deterministic standard-library-only plan; no subprocess or file writes."""
    _fixed_fixtures()
    result = oracle.plan()
    result.update({
        "driver_schema": "rhodes.direct_alias_driver.plan.v1",
        "total_samples": EXPECTED_SAMPLES,
        "case_order": list(CASE_IDS),
        "source_bindings": {"tools/direct_alias_oracle.py": ORACLE_SHA256,
                            "apps/alias_primitive_probe.cpp": PRODUCER_SHA256},
        "decomposition": "P-B=(P-S)+(S-B), sample by sample; retain closure residual",
        "record_origin": "n=0 is stationary phase origin; retain every sample; no strike or trimming",
        "fit_or_alignment": "none; no gain, phase, timing, magnitude-spectrum or onset fitting",
        "retention": {
            "directory": "must not exist; every artifact is created exclusively",
            "raw_stream_bytes_per_case_per_stream": MAX_STREAM_BYTES,
            "stream_cap_behavior": "kill/reap; keep exact prefix and explicit incomplete capture",
            "array_encoding": "JSON round-trip binary64 numbers; row-major SHA-256 of struct.pack('<d', value)",
            "line_encoding": "CSV signed integer bins and real/imaginary complex voltage coefficients",
            "failure_behavior": "retain streams/argv/exit/errors; stop later cases; final failure dominates",
            "compiler": "optional explicit --build-record; missing provenance is unavailable, never inferred",
        },
        "implementation_bounds": {
            "json_depth": MAX_JSON_DEPTH, "json_tokens": MAX_JSON_TOKENS,
            "probe_json_string_characters": MAX_JSON_STRING,
            "json_number_characters": 64, "artifacts": MAX_ARTIFACTS,
            "build_record_bytes": MAX_RECEIPT_BYTES,
            "final_metadata_reserved_bytes": FINAL_RESERVE_BYTES,
            "pipe_queue_chunks": 4, "pipe_chunk_bytes": IO_CHUNK,
            "workers": 1, "pipe_reader_threads": 2,
        },
        "enforcement": {
            "wall": "30-second envelope: 27 seconds diagnostic work and 3 seconds kill/reap/input-finalization reserve; SIGALRM where supported",
            "memory": "Linux RLIMIT_AS 256 MiB per process, inherited by probe; unsupported platforms explicitly recorded",
            "cleanup": "no diagnostic work after phase deadline; final emergency metadata writes are byte-bounded, not an OS file-I/O latency guarantee",
            "process_tree": "POSIX new-session process-group kill; direct-child kill elsewhere, capability recorded",
        },
        "scope_exclusions": ["alias magnitude acceptability", "damping", "strike/onset/retrigger/release",
                             "finite-window leakage", "finite-kernel response", "whole-engine direct convergence",
                             "RT-001 completion", "C03 promotion", "physical fidelity", "audibility",
                             "realtime deadline validation"],
    })
    return result


def _json_bytes(value: Any) -> bytes:
    return (json.dumps(value, sort_keys=True, indent=2, allow_nan=False) + "\n").encode("utf-8")


def _pairs(pairs: list[tuple[str, Any]]) -> dict:
    if len(pairs) > 32:
        raise DiagnosticError("JSON object member cap exceeded")
    result = {}
    for key, value in pairs:
        if key in result:
            raise DiagnosticError(f"duplicate JSON key: {key[:80]}")
        result[key] = value
    return result


def _parse_int(token: str) -> int | float:
    if len(token) > 20:
        raise DiagnosticError("JSON integer token too long")
    value = int(token)
    if not -(2 ** 63) <= value < 2 ** 63:
        raise DiagnosticError("JSON integer outside signed 64-bit bound")
    # C++ max_digits10 can serialize a negative binary64 zero as '-0'. Keep
    # its sign in real-valued arrays; exact integer schema/counter fields reject it.
    if token == "-0":
        return -0.0
    return value


def _parse_float(token: str) -> float:
    if len(token) > 64:
        raise DiagnosticError("JSON number token too long")
    return _number(float(token), "JSON number")


def _reject_constant(token: str) -> None:
    raise DiagnosticError(f"nonstandard/nonfinite JSON constant: {token}")


def _bounded_json(raw: bytes, *, maximum: int = MAX_STREAM_BYTES,
                  string_cap: int = MAX_JSON_STRING) -> Any:
    """Bound allocations before json.loads; reject duplicate keys and nonfinite values."""
    if type(raw) is not bytes or not 0 < len(raw) <= maximum:
        raise DiagnosticError("JSON byte count/type is outside its explicit bound")
    try:
        text = raw.decode("utf-8", errors="strict")
    except UnicodeError as exc:
        raise DiagnosticError("JSON is not strict UTF-8") from exc
    # A lexical prepass limits nesting, atom sizes and constructed values before
    # the stdlib decoder creates containers. Syntax remains json.loads' job.
    depth = tokens = index = 0
    while index < len(text):
        char = text[index]
        if char in " \t\r\n,:":
            index += 1
            continue
        if char in "[{":
            depth += 1
            tokens += 1
            if depth > MAX_JSON_DEPTH:
                raise DiagnosticError("JSON depth cap exceeded")
            index += 1
        elif char in "]}":
            depth -= 1
            if depth < 0:
                raise DiagnosticError("JSON has unmatched closing delimiter")
            index += 1
        elif char == '"':
            tokens += 1
            start = index
            index += 1
            while index < len(text):
                if index - start > string_cap:
                    raise DiagnosticError("JSON string cap exceeded")
                if text[index] == '"':
                    index += 1
                    break
                index += 2 if text[index] == "\\" else 1
            else:
                raise DiagnosticError("unterminated JSON string")
        else:
            tokens += 1
            start = index
            while index < len(text) and text[index] not in " \t\r\n,]}:":
                index += 1
                if index - start > 64:
                    raise DiagnosticError("JSON atom cap exceeded")
            if index == start:
                raise DiagnosticError("invalid JSON token")
        if tokens > MAX_JSON_TOKENS:
            raise DiagnosticError("JSON token cap exceeded")
    if depth != 0:
        raise DiagnosticError("JSON has unclosed containers")
    try:
        return json.loads(text, object_pairs_hook=_pairs, parse_int=_parse_int,
                          parse_float=_parse_float, parse_constant=_reject_constant)
    except (ValueError, RecursionError) as exc:
        if isinstance(exc, DiagnosticError):
            raise
        raise DiagnosticError(f"invalid complete JSON document: {str(exc)[:200]}") from exc


def validate_probe_document(raw: bytes, fixture: oracle.Fixture) -> dict[str, Any]:
    """Validate exact v1 schema, fixed settings, counters, dimensions and SI sums.

    This deliberately does not accept an exit code. The caller MUST separately
    require process success. A valid JSON success flag never repairs a failure.
    """
    if type(fixture) is not oracle.Fixture or fixture not in _fixed_fixtures():
        raise DiagnosticError("validator requires one of the four frozen fixtures")
    doc = _object(_bounded_json(raw), {
        "schema_version", "schema", "provenance", "case_id", "settings", "modes",
        "frames", "status", "success"}, "producer document")
    _exact_int(doc["schema_version"], 1, "schema_version")
    expected_strings = {
        "schema": "rhodes.alias_primitive_probe.v1",
        "provenance": "synthetic_development_fixture", "case_id": fixture.case_id,
        "status": "ok",
    }
    for key, expected in expected_strings.items():
        if type(doc[key]) is not str or doc[key] != expected:
            raise DiagnosticError(f"{key}: incorrect producer declaration")
    if doc["success"] is not True:
        raise DiagnosticError("success must be JSON true")
    settings = _object(doc["settings"], {
        "sample_rate_hz", "frame_count", "modal_mass_kg", "decay_rate_per_second",
        "observation", "boundary", "pickup_coefficients"}, "settings")
    _exact_int(settings["sample_rate_hz"], fixture.sample_rate_hz, "sample_rate_hz")
    _exact_int(settings["frame_count"], fixture.sample_count, "frame_count")
    _exact_number(settings["modal_mass_kg"], 0.001, "modal_mass_kg")
    _exact_number(settings["decay_rate_per_second"], 0.0, "decay_rate_per_second")
    if settings["observation"] != "before_advance" or settings["boundary"] != "single_pickup_voltage":
        raise DiagnosticError("wrong observation or voltage boundary")
    coeff_keys = ("a1_wb_turn_per_m", "a2_wb_turn_per_m2", "a3_wb_turn_per_m3")
    coeffs = _object(settings["pickup_coefficients"], set(coeff_keys), "pickup_coefficients")
    for key, value in zip(coeff_keys, (0.4, 800.0, 400000.0)):
        _exact_number(coeffs[key], value, key)
    modes = doc["modes"]
    if type(modes) is not list or len(modes) != len(fixture.modes):
        raise DiagnosticError("wrong modal dimension")
    for index, (mode, expected) in enumerate(zip(modes, fixture.modes)):
        _object(mode, {"frequency_hz", "amplitude_m", "phase_rad"}, f"mode {index}")
        for key in ("frequency_hz", "amplitude_m", "phase_rad"):
            _exact_number(mode[key], getattr(expected, key), f"mode {index} {key}")
    frames = doc["frames"]
    if type(frames) is not list or len(frames) != fixture.sample_count:
        raise DiagnosticError("wrong frame dimension")
    for index, frame in enumerate(frames):
        _object(frame, {"index", "modal_q_m", "modal_v_m_per_s", "q_m", "v_m_per_s", "P_volts"},
                f"frame {index}")
        _exact_int(frame["index"], index, f"frame {index} index")
        for modal_key, total_key in (("modal_q_m", "q_m"), ("modal_v_m_per_s", "v_m_per_s")):
            values = frame[modal_key]
            if type(values) is not list or len(values) != len(fixture.modes):
                raise DiagnosticError(f"frame {index}: wrong {modal_key} dimension")
            total = 0.0
            for value in values:
                total = _number(total + _number(value, modal_key), f"frame {index} modal sum")
            if total != _number(frame[total_key], total_key):
                raise DiagnosticError(f"frame {index}: {total_key} differs from ordered modal sum")
        _number(frame["P_volts"], f"frame {index} P_volts")
    return doc


def _path(value: str | Path) -> Path:
    path = Path(value).expanduser().absolute()
    if len(os.fsencode(path)) > 4096:
        raise DiagnosticError("path exceeds 4096-byte bound")
    return path


@contextmanager
def _open_regular(path: Path, maximum: int) -> Iterator[tuple[Path, Any, os.stat_result]]:
    resolved = path.resolve(strict=True)
    metadata = resolved.stat()
    if not stat.S_ISREG(metadata.st_mode) or metadata.st_size > maximum:
        raise DiagnosticError(f"file exceeds bound or is not regular: {path}")
    # A regular-file check before open prevents FIFO/device blocking. Nonblocking
    # and no-follow flags also close the final-component replacement race on POSIX.
    flags = (os.O_RDONLY | getattr(os, "O_NONBLOCK", 0) | getattr(os, "O_NOFOLLOW", 0)
             | getattr(os, "O_BINARY", 0))
    descriptor = os.open(resolved, flags)
    try:
        opened = os.fstat(descriptor)
        if (not stat.S_ISREG(opened.st_mode) or opened.st_size > maximum
                or (opened.st_dev, opened.st_ino) != (metadata.st_dev, metadata.st_ino)):
            raise DiagnosticError(f"file identity/type changed before opening: {path}")
        stream = os.fdopen(descriptor, "rb")
        descriptor = -1
        with stream:
            yield resolved, stream, opened
    finally:
        if descriptor != -1:
            os.close(descriptor)


def _read_bounded(path: Path, maximum: int, deadline: float | None = None) -> bytes:
    with _open_regular(path, maximum) as (_resolved, stream, before):
        data = stream.read(maximum + 1)
        after = os.fstat(stream.fileno())
    _check_deadline(deadline)
    if len(data) > maximum:
        raise DiagnosticError(f"file grew beyond bound: {path}")
    if (before.st_size, before.st_mtime_ns) != (after.st_size, after.st_mtime_ns):
        raise DiagnosticError(f"file changed while reading: {path}")
    return data


def _hash_file(path: Path, maximum: int, deadline: float | None = None) -> dict[str, Any]:
    digest = hashlib.sha256()
    count = 0
    with _open_regular(path, maximum) as (resolved, stream, before):
        while True:
            _check_deadline(deadline)
            chunk = stream.read(IO_CHUNK)
            if not chunk:
                break
            count += len(chunk)
            if count > maximum:
                raise DiagnosticError(f"hash input grew beyond bound: {path}")
            digest.update(chunk)
        after = os.fstat(stream.fileno())
    if (before.st_size, before.st_mtime_ns) != (after.st_size, after.st_mtime_ns) or count != after.st_size:
        raise DiagnosticError(f"input changed while hashing: {path}")
    return {"path": str(resolved), "bytes": count, "sha256": digest.hexdigest()}


class _Artifact:
    def __init__(self, owner: "ArtifactStore", name: str, limit: int, final: bool):
        self.owner, self.name, self.limit, self.final = owner, name, limit, final
        self.count = 0
        self.digest = hashlib.sha256()
        self.file = (owner.path / name).open("xb")

    def write(self, data: bytes) -> None:
        if type(data) is not bytes:
            raise DiagnosticError("artifact writes require bytes")
        budget = MAX_ARTIFACT_BYTES if self.final else MAX_ARTIFACT_BYTES - FINAL_RESERVE_BYTES
        if self.count + len(data) > self.limit or self.owner.total + len(data) > budget:
            raise DiagnosticError(f"artifact byte cap exceeded: {self.name}")
        written = self.file.write(data)
        if written != len(data):
            raise DiagnosticError(f"short artifact write: {self.name}")
        self.count += written
        self.owner.total += written
        self.digest.update(data)

    def close(self, complete: bool) -> None:
        try:
            self.file.flush()
        except BaseException:
            complete = False
            raise
        finally:
            self.file.close()
            self.owner.entries[self.name] = {
                "bytes": self.count, "sha256": self.digest.hexdigest(), "write_complete": complete,
            }


class ArtifactStore:
    """Exclusive flat output files with cumulative and per-file write bounds."""
    def __init__(self, path: str | Path):
        self.path = _path(path)
        self.path.mkdir(mode=0o700, parents=False, exist_ok=False)
        self.path = self.path.resolve(strict=True)
        self.entries: dict[str, dict] = {}
        self.names: set[str] = set()
        self.total = 0

    @contextmanager
    def open(self, name: str, limit: int, *, final: bool = False) -> Iterator[_Artifact]:
        if (type(name) is not str or not name or len(name) > 96
                or any(c not in "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-" for c in name)
                or name in (".", "..") or name in self.names or len(self.names) >= MAX_ARTIFACTS):
            raise DiagnosticError("artifact name/count is invalid or already used")
        self.names.add(name)
        stream = _Artifact(self, name, limit, final)
        complete = False
        try:
            yield stream
            complete = True
        finally:
            stream.close(complete)

    def write_bytes(self, name: str, data: bytes, limit: int, *, final: bool = False) -> None:
        with self.open(name, limit, final=final) as stream:
            for start in range(0, len(data), IO_CHUNK):
                stream.write(data[start:start + IO_CHUNK])

    def write_json(self, name: str, value: Any, limit: int = 128 * 1024,
                   *, final: bool = False) -> None:
        encoder = json.JSONEncoder(sort_keys=True, separators=(",", ":"), allow_nan=False)
        with self.open(name, limit, final=final) as stream:
            for chunk in encoder.iterencode(value):
                encoded = chunk.encode("utf-8")
                for start in range(0, len(encoded), IO_CHUNK):
                    stream.write(encoded[start:start + IO_CHUNK])
            stream.write(b"\n")

    def verify(self, deadline: float, names: tuple[str, ...] | None = None) -> dict[str, Any]:
        """Rehash bounded retained bytes; a manifest must expose every mismatch."""
        checked: dict[str, Any] = {}
        for name in tuple(self.entries) if names is None else names:
            expected = self.entries[name]
            try:
                actual = _hash_file(self.path / name, expected["bytes"], deadline)
                matched = (actual["bytes"] == expected["bytes"] and actual["sha256"] == expected["sha256"])
                checked[name] = {"matched": matched, "sha256": actual["sha256"], "bytes": actual["bytes"]}
            except (OSError, DiagnosticError) as exc:
                checked[name] = {"matched": False, "error": str(exc)[:1024]}
        return checked


def _error(phase: str, exc: BaseException) -> dict[str, str]:
    return {"phase": phase, "type": type(exc).__name__, "message": str(exc)[:2048],
            "traceback": "".join(traceback.format_exception(type(exc), exc, exc.__traceback__, limit=6))[-8192:]}


class _Limits:
    """Restore caller limits; use actual OS capability, never assert unsupported enforcement."""
    def __init__(self, start: float):
        self.overall_deadline = start + WALL_SECONDS
        self.deadline = self.overall_deadline - CLEANUP_RESERVE_SECONDS
        self.resource = None
        self.previous_memory = None
        self.previous_alarm = None
        self.alarm_active = False
        self.can_preempt = False
        self.record = {
            "workers": 1,
            "wall": {"seconds": WALL_SECONDS, "diagnostic_seconds": WALL_SECONDS - CLEANUP_RESERVE_SECONDS,
                     "cleanup_reserve_seconds": CLEANUP_RESERVE_SECONDS, "preemptive": False,
                     "mechanism": "monotonic checks and bounded probe wait",
                     "scope": "diagnostic and input-finalization phases; final emergency metadata is byte-bounded"},
            "memory": {"requested_bytes": MAX_MEMORY_BYTES, "enforced": False,
                       "scope": "per-process virtual address space; not total RSS or a sandbox"},
            "process_tree_kill": "POSIX process group" if os.name == "posix" else "direct child only",
        }

    def __enter__(self) -> "_Limits":
        if sys.platform.startswith("linux"):
            try:
                import resource
                old = resource.getrlimit(resource.RLIMIT_AS)
                target = min(MAX_MEMORY_BYTES, old[0]) if old[0] != resource.RLIM_INFINITY else MAX_MEMORY_BYTES
                resource.setrlimit(resource.RLIMIT_AS, (target, old[1]))
                observed = resource.getrlimit(resource.RLIMIT_AS)
                if observed[0] != target:
                    raise OSError("RLIMIT_AS readback mismatch")
                self.resource, self.previous_memory = resource, old
                self.record["memory"].update(enforced=True, mechanism="Linux RLIMIT_AS soft limit, inherited by probe",
                                              effective_bytes=target)
            except (ImportError, AttributeError, OSError, ValueError) as exc:
                self.record["memory"]["unsupported_reason"] = str(exc)[:512]
        else:
            self.record["memory"]["unsupported_reason"] = "this driver verifies RLIMIT_AS enforcement only on Linux"
        if (hasattr(signal, "setitimer") and hasattr(signal, "SIGALRM")
                and threading.current_thread() is threading.main_thread()):
            try:
                if signal.getitimer(signal.ITIMER_REAL) == (0.0, 0.0):
                    self.previous_alarm = signal.getsignal(signal.SIGALRM)
                    self.can_preempt = True
                    self.arm(self.deadline)
                    self.record["wall"].update(preemptive=True, mechanism="SIGALRM plus monotonic subprocess deadlines")
                else:
                    self.record["wall"]["unsupported_reason"] = "caller already owns an active real-time timer"
            except (OSError, ValueError) as exc:
                self.stop_alarm()
                self.can_preempt = False
                self.record["wall"]["unsupported_reason"] = str(exc)[:512]
        else:
            self.record["wall"]["unsupported_reason"] = "SIGALRM main-thread preemption unavailable; bounded-phase checks only"
        return self

    def arm(self, deadline: float) -> None:
        if self.can_preempt:
            def expired(_signum: int, _frame: Any) -> None:
                raise DeadlineExceeded("bounded experiment phase deadline exceeded")
            signal.signal(signal.SIGALRM, expired)
            self.alarm_active = True
            signal.setitimer(signal.ITIMER_REAL, max(0.000001, deadline - time.monotonic()))

    def stop_alarm(self) -> None:
        if self.alarm_active:
            signal.setitimer(signal.ITIMER_REAL, 0.0)
            signal.signal(signal.SIGALRM, self.previous_alarm)
            self.alarm_active = False

    def __exit__(self, _kind: Any, _value: Any, _tb: Any) -> None:
        self.stop_alarm()
        if self.resource is not None:
            self.resource.setrlimit(self.resource.RLIMIT_AS, self.previous_memory)


def _kill_reap(child: subprocess.Popen, record: dict[str, Any]) -> None:
    record["kill_requested"] = True
    try:
        if os.name == "posix":
            os.killpg(child.pid, signal.SIGKILL)
        elif child.poll() is None:
            child.kill()
    except ProcessLookupError:
        pass
    except OSError as exc:
        record["errors"].append(_error("kill", exc))
        try:
            child.kill()
        except OSError:
            pass
    try:
        child.wait(timeout=1.0)
    except subprocess.TimeoutExpired as exc:
        record["errors"].append(_error("reap", exc))
    record["reaped"] = child.poll() is not None


def run_probe(probe: str | Path, fixture: oracle.Fixture, output: ArtifactStore,
              deadline: float) -> dict[str, Any]:
    """Capture one sequential child with bounded queues/files; kill and reap on failure."""
    if type(fixture) is not oracle.Fixture or fixture not in _fixed_fixtures():
        raise DiagnosticError("probe helper requires a frozen fixture")
    command = [str(_path(probe)), "--case", fixture.case_id]
    record: dict[str, Any] = {
        "case_id": fixture.case_id, "argv": command, "cwd": str(ROOT), "shell": False,
        "stdin": "DEVNULL", "environment_override": {"LC_ALL": "C", "LANG": "C"},
        "returncode": None, "reaped": False, "kill_requested": False,
        "stdout_file": fixture.case_id + ".stdout", "stderr_file": fixture.case_id + ".stderr",
        "stdout_bytes_observed": 0, "stderr_bytes_observed": 0,
        "stdout_bytes_retained": 0, "stderr_bytes_retained": 0,
        "stdout_capture_complete": False, "stderr_capture_complete": False,
        "stream_byte_limit": MAX_STREAM_BYTES, "errors": [], "succeeded": False,
    }
    started = time.monotonic()
    messages: queue.Queue = queue.Queue(maxsize=4)
    stopped = threading.Event()
    threads: list[threading.Thread] = []
    child = None

    def pump(pipe: Any, label: str) -> None:
        try:
            while not stopped.is_set():
                chunk = os.read(pipe.fileno(), IO_CHUNK)
                item = (label, chunk, None)
                while not stopped.is_set():
                    try:
                        messages.put(item, timeout=0.05)
                        break
                    except queue.Full:
                        continue
                if not chunk:
                    return
        except (OSError, ValueError) as exc:
            if not stopped.is_set():
                try:
                    messages.put((label, b"", str(exc)[:512]), timeout=0.1)
                except queue.Full:
                    pass

    try:
        with output.open(record["stdout_file"], MAX_STREAM_BYTES) as stdout, \
                output.open(record["stderr_file"], MAX_STREAM_BYTES) as stderr:
            sinks = {"stdout": stdout, "stderr": stderr}
            try:
                _check_deadline(deadline)
                child = subprocess.Popen(command, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                                         stderr=subprocess.PIPE, cwd=ROOT, shell=False, bufsize=0,
                                         close_fds=True, start_new_session=(os.name == "posix"),
                                         env={**os.environ, "LC_ALL": "C", "LANG": "C"})
                record["pid"] = child.pid
                for label, pipe in (("stdout", child.stdout), ("stderr", child.stderr)):
                    thread = threading.Thread(target=pump, args=(pipe, label), daemon=True)
                    thread.start()
                    threads.append(thread)
                eof: set[str] = set()
                while len(eof) < 2 or child.poll() is None:
                    _check_deadline(deadline)
                    try:
                        label, chunk, error = messages.get(timeout=min(0.05, max(0.001, deadline - time.monotonic())))
                    except queue.Empty:
                        continue
                    if error is not None:
                        raise DiagnosticError(f"{label} reader: {error}")
                    if not chunk:
                        eof.add(label)
                        record[label + "_capture_complete"] = True
                        continue
                    record[label + "_bytes_observed"] += len(chunk)
                    available = MAX_STREAM_BYTES - record[label + "_bytes_retained"]
                    kept = chunk[:available]
                    sinks[label].write(kept)
                    record[label + "_bytes_retained"] += len(kept)
                    if len(chunk) > available:
                        record[label + "_capture_complete"] = False
                        raise DiagnosticError(f"{label} exceeds 1 MiB cap; retained prefix only")
                record["returncode"] = child.wait(timeout=max(0.001, min(1.0, deadline - time.monotonic())))
                record["reaped"] = True
                if record["returncode"] != 0:
                    raise DiagnosticError(f"probe exited {record['returncode']}")
            except (Exception, KeyboardInterrupt) as exc:
                record["errors"].append(_error("probe", exc))
                if child is not None:
                    _kill_reap(child, record)
            finally:
                stopped.set()
                for thread in threads:
                    thread.join(timeout=0.25)
                if any(thread.is_alive() for thread in threads):
                    record["errors"].append({"phase": "pipe_cleanup", "type": "DiagnosticError",
                                               "message": "pipe reader did not finish after bounded kill/reap", "traceback": ""})
                if child is not None:
                    record["returncode"] = child.poll()
                    for pipe in (child.stdout, child.stderr):
                        if pipe is not None:
                            pipe.close()
    except (Exception, KeyboardInterrupt) as exc:
        record["errors"].append(_error("capture_retention", exc))
        if child is not None and child.poll() is None:
            _kill_reap(child, record)
    record["wall_seconds"] = time.monotonic() - started
    record["succeeded"] = (record["returncode"] == 0 and record["reaped"] and not record["errors"]
                           and record["stdout_capture_complete"] and record["stderr_capture_complete"])
    output.write_json(fixture.case_id + ".process.json", record, 32 * 1024, final=True)
    return record


def _array(values: Any, count: int, units: str, columns: int | None = None) -> dict[str, Any]:
    if type(values) not in (tuple, list) or len(values) != count or count > 2048:
        raise DiagnosticError("array row dimension exceeds frozen bound")
    digest = hashlib.sha256()
    for row in values:
        numbers = (row,) if columns is None else row
        if type(numbers) not in (tuple, list) or len(numbers) != (1 if columns is None else columns):
            raise DiagnosticError("array column dimension differs from frozen fixture")
        for value in numbers:
            digest.update(struct.pack("<d", _number(value, "retained array")))
    return {"shape": [count] if columns is None else [count, columns], "units": units,
            "encoding": "IEEE-754 binary64, little-endian, row-major; JSON numbers round-trip",
            "sha256_f64le": digest.hexdigest(), "values": values}


def _write_lines(output: ArtifactStore, fixture: oracle.Fixture,
                 spectra: dict[str, dict[int, complex]]) -> None:
    with output.open(fixture.case_id + ".signed_lines.csv", 128 * 1024) as target:
        buffer = io.StringIO(newline="")
        writer = csv.writer(buffer, lineterminator="\n")
        writer.writerow(["spectrum", "signed_bin", "frequency_hz", "folded_bin", "folded_frequency_hz",
                         "coefficient_real_v", "coefficient_imag_v", "coefficient_magnitude_v", "phase_rad"])
        target.write(buffer.getvalue().encode("ascii"))
        for name, lines in spectra.items():
            if len(lines) > oracle.MAX_LINES:
                raise DiagnosticError("line allocation cap exceeded")
            for key, coefficient in sorted(lines.items()):
                buffer.seek(0)
                buffer.truncate(0)
                folded = oracle.fold_bin(key, fixture.sample_count)
                writer.writerow([name, key, repr(key * fixture.sample_rate_hz / fixture.sample_count), folded,
                                 repr(folded * fixture.sample_rate_hz / fixture.sample_count),
                                 repr(coefficient.real), repr(coefficient.imag), repr(abs(coefficient)),
                                 repr(math.atan2(coefficient.imag, coefficient.real))])
                target.write(buffer.getvalue().encode("ascii"))


def _evaluate_case(output: ArtifactStore, fixture: oracle.Fixture, doc: dict,
                   deadline: float) -> dict[str, Any]:
    _check_deadline(deadline)
    count, modes = fixture.sample_count, len(fixture.modes)
    frames = doc["frames"]
    primitive = tuple(float(frame["P_volts"]) for frame in frames)
    arrays = oracle.oracle_arrays(fixture)
    errors = oracle.error_arrays(primitive, arrays["s_v"], arrays["b_v"])
    metrics = oracle.metrics(fixture, primitive)
    _check_deadline(deadline)
    pickup_residual = tuple(p + (0.4 + f["q_m"] * (1600.0 + 1200000.0 * f["q_m"])) * f["v_m_per_s"]
                            for p, f in zip(primitive, frames))
    direct_residual = tuple(d - s for d, s in zip(arrays["direct_v"], arrays["s_v"]))
    stored = {
        "time_s": _array(arrays["time_s"], count, "s"),
        "primitive_modal_q_m": _array(tuple(f["modal_q_m"] for f in frames), count, "m", modes),
        "primitive_modal_v_m_per_s": _array(tuple(f["modal_v_m_per_s"] for f in frames), count, "m/s", modes),
        "primitive_q_m": _array(tuple(f["q_m"] for f in frames), count, "m"),
        "primitive_v_m_per_s": _array(tuple(f["v_m_per_s"] for f in frames), count, "m/s"),
        "analytic_modal_q_m": _array(arrays["q_m"], count, "m", modes),
        "analytic_modal_v_m_per_s": _array(arrays["v_m_per_s"], count, "m/s", modes),
        "analytic_x_m": _array(arrays["x_m"], count, "m"),
        "analytic_v_total_m_per_s": _array(arrays["v_total_m_per_s"], count, "m/s"),
    }
    voltages = {"P_v": primitive, "S_v": arrays["s_v"], "B_v": arrays["b_v"],
                "direct_v": arrays["direct_v"], "analytic_alias_v": arrays["analytic_alias_v"],
                "direct_harmonic_residual_v": direct_residual,
                "pickup_chain_residual_v": pickup_residual, **errors}
    stored.update({key: _array(values, count, "V") for key, values in voltages.items()})
    output.write_json(fixture.case_id + ".arrays.json", {
        "schema": "rhodes.direct_alias_driver.arrays.v1", "case_id": fixture.case_id,
        "sample_count": count, "first_index": 0, "last_index": count - 1,
        "samples_discarded": 0, "array_hash_format": "SHA-256 of concatenated little-endian binary64 bytes",
        "arrays": stored}, 2 * 1024 * 1024)
    lines = oracle.continuous_lines(fixture)
    band = oracle.bandlimited_lines(lines, count)
    alias = oracle.alias_lines(lines, count)
    folded = oracle.folded_lines(lines, count)
    bins = sorted(set(folded) | set(alias) | {0, -count // 2, -1, 1})
    direct_dft = oracle.dft_at_bins(arrays["direct_v"], bins)
    alias_dft = oracle.dft_at_bins(errors["alias_v"], bins)
    _write_lines(output, fixture, {
        "continuous_all": lines, "continuous_retained": band,
        "continuous_removed": {k: v for k, v in lines.items() if k not in band},
        "sampled_all": folded, "sampled_alias": alias,
        "dft_direct": direct_dft, "dft_alias": alias_dft})
    coefficient_error = max((abs(alias_dft[k] - alias.get(k, 0j)) for k in bins), default=0.0)
    metrics["alias_dft_max_coefficient_error_v"] = coefficient_error
    metrics["pickup_chain_residual"] = oracle.signal_metrics(pickup_residual)
    metrics["checks"]["emitted_pickup_matches_emitted_motion"] = (
        metrics["pickup_chain_residual"]["sample_peak_v"] <= oracle.BOOKKEEPING_PEAK_V)
    metrics["checks"]["alias_dft_matches_removed_folded_lines"] = coefficient_error <= oracle.BOOKKEEPING_PEAK_V
    metrics["parseval_all_signals"] = {}
    for name, samples, spectrum in (("S", arrays["s_v"], lines), ("B", arrays["b_v"], band),
                                     ("S_minus_B", errors["alias_v"], alias)):
        sampled_rms = oracle.signal_metrics(samples)["rms_v"]
        line_rms = oracle.line_rms(spectrum, count)
        gap = abs(sampled_rms - line_rms)
        metrics["parseval_all_signals"][name] = {
            "sample_rms_v": sampled_rms, "coalesced_line_rms_v": line_rms, "absolute_gap_v": gap}
        metrics["checks"]["parseval_" + name] = gap <= oracle.BOOKKEEPING_PEAK_V
    metrics["modal_motion_residuals"] = []
    for index in range(modes):
        item: dict[str, Any] = {"mode_index": index, "acceptance_threshold": None}
        for key, analytic, unit in (("modal_q_m", "q_m", "m"), ("modal_v_m_per_s", "v_m_per_s", "m/s")):
            residual = tuple(float(f[key][index]) - row[index] for f, row in zip(frames, arrays[analytic]))
            item[key] = {"units": unit, "sample_peak": max(abs(v) for v in residual),
                         "rms": math.hypot(*residual) / math.sqrt(count)}
        metrics["modal_motion_residuals"].append(item)
    metrics.update({"sample_count": count, "mode_count": modes, "samples_discarded": 0,
                    "first_index": 0, "last_index": count - 1, "alias_acceptability_threshold": None,
                    "array_hashes": {key: {k: v for k, v in value.items() if k != "values"}
                                     for key, value in stored.items()}})
    metrics["failures"] = [name for name, passed in metrics["checks"].items() if not passed]
    metrics["passed"] = not metrics["failures"]
    _check_deadline(deadline)
    return metrics


def _snapshot(paths: dict[str, tuple[Path, int]], deadline: float | None) -> dict:
    result = {}
    for name, (path, maximum) in paths.items():
        try:
            result[name] = _hash_file(path, maximum, deadline)
        except (OSError, DiagnosticError) as exc:
            result[name] = {"path": str(path), "error": str(exc)[:2048]}
    return result


def _receipt(raw: bytes, inputs: dict, probe: Path) -> tuple[dict, Path]:
    receipt = _object(_bounded_json(raw, maximum=MAX_RECEIPT_BYTES, string_cap=8192), {
        "compiler_path", "compiler_sha256", "compiler_version", "source_sha256_before",
        "compile_command", "compile_returncode", "compile_stdout", "compile_stderr",
        "binary_sha256_before"}, "build record")
    _exact_int(receipt["compile_returncode"], 0, "build record compile_returncode")
    for key in ("compiler_path", "compiler_version", "compile_stdout", "compile_stderr"):
        if type(receipt[key]) is not str or len(receipt[key]) > 8192 or "\x00" in receipt[key]:
            raise DiagnosticError(f"build record {key}: bounded string required")
    for key in ("compiler_sha256", "binary_sha256_before"):
        if (type(receipt[key]) is not str or len(receipt[key]) != 64
                or any(c not in "0123456789abcdef" for c in receipt[key])):
            raise DiagnosticError(f"build record {key}: invalid SHA-256")
    if receipt["binary_sha256_before"] != inputs["probe"].get("sha256"):
        raise DiagnosticError("build record binary hash does not match observed executable")
    sources = _object(receipt["source_sha256_before"], set(RECEIPT_SOURCES), "build record source paths")
    for key in RECEIPT_SOURCES:
        if type(sources[key]) is not str or sources[key] != inputs[key].get("sha256"):
            raise DiagnosticError(f"build record source hash mismatch: {key}")
    compiler = Path(receipt["compiler_path"])
    if not compiler.is_absolute():
        raise DiagnosticError("build record compiler path must be absolute")
    compiler = _path(compiler).resolve(strict=True)
    command = receipt["compile_command"]
    if (type(command) is not list or not 3 <= len(command) <= 64
            or any(type(arg) is not str or not arg or len(arg) > 4096 or "\x00" in arg for arg in command)):
        raise DiagnosticError("build record command is not a bounded argv array")
    if (Path(command[0]).resolve() != compiler or command[-2] != "-o"
            or _path(command[-1]).resolve() != probe.resolve()
            or command.count("apps/alias_primitive_probe.cpp") != 1
            or command.count("src/research_engine.cpp") != 1):
        raise DiagnosticError("build record command does not identify expected compiler, source pair and output")
    for arg in command[1:-2]:
        if arg.startswith("@") or (Path(arg).suffix in (".c", ".cc", ".cpp", ".cxx", ".h", ".hpp", ".hh")
                                  and arg not in RECEIPT_SOURCES[:2]):
            raise DiagnosticError("build record command references an untracked source or response file")
    return receipt, compiler


def execute(probe: str | Path, output_dir: str | Path,
            build_record: str | Path | None = None) -> dict[str, Any]:
    """Run exactly the frozen plan, retaining incomplete/failing cases and final errors.

    A missing build receipt makes reproducibility incomplete but leaves numerical
    results visible. Receipt consistency is NOT proof that compilation occurred.
    Return the final manifest; CLI exits nonzero unless every required part passes.
    """
    started = time.monotonic()
    output = ArtifactStore(output_dir)
    failures: list[dict] = []
    cases = [{"case_id": case_id, "status": "not_run", "process": None, "metrics": None,
              "errors": []} for case_id in CASE_IDS]
    inputs_before: dict = {}
    inputs_after: dict = {}
    paths = {name: (ROOT / name, MAX_SOURCE_BYTES) for name in SOURCE_PATHS}
    receipt_info: dict[str, Any] = {
        "available": build_record is not None, "byte_consistent": False,
        "compiler_provenance": "unavailable" if build_record is None else "supplied receipt, not independently witnessed",
        "compilation_proven": False,
    }
    initial_plan = None
    plan_before = None
    plan_after = None
    plan_regenerated = None
    artifact_integrity: dict[str, Any] = {}
    phase = "preflight"
    limits = _Limits(started)
    with limits:
        try:
            initial_plan = plan()
            plan_data = _json_bytes(initial_plan)
            plan_before = hashlib.sha256(plan_data).hexdigest()
            output.write_bytes("plan.json", plan_data, 32 * 1024)
            probe_path = _path(probe)
            paths["probe"] = (probe_path, MAX_EXECUTABLE_BYTES)
            output.write_json("commands.json", {
                "cwd": str(ROOT), "shell": False,
                "commands": [[str(probe_path), "--case", case_id] for case_id in CASE_IDS]}, 32 * 1024)
            inputs_before = _snapshot(paths, limits.deadline)
            if any("error" in value for value in inputs_before.values()):
                raise DiagnosticError("input hashing failed; see inputs.before.json")
            for name, expected in (("tools/direct_alias_oracle.py", ORACLE_SHA256),
                                   ("apps/alias_primitive_probe.cpp", PRODUCER_SHA256)):
                if inputs_before[name]["sha256"] != expected:
                    raise DiagnosticError(f"frozen source hash mismatch: {name}")
            if os.name == "posix" and not os.access(probe_path, os.X_OK):
                raise DiagnosticError("probe is not executable")
            if build_record is not None:
                record_path = _path(build_record)
                paths["build_record"] = (record_path, MAX_RECEIPT_BYTES)
                raw = _read_bounded(record_path, MAX_RECEIPT_BYTES, limits.deadline)
                output.write_bytes("build_record.raw.json", raw, MAX_RECEIPT_BYTES)
                receipt, compiler = _receipt(raw, inputs_before, probe_path)
                paths["compiler"] = (compiler, MAX_EXECUTABLE_BYTES)
                for name in ("build_record", "compiler"):
                    inputs_before[name] = _hash_file(*paths[name], limits.deadline)
                if inputs_before["compiler"]["sha256"] != receipt["compiler_sha256"]:
                    raise DiagnosticError("build record compiler hash differs from observed file")
                receipt_info.update(byte_consistent=True, compiler_path=str(compiler),
                                    compiler_version_claim=receipt["compiler_version"],
                                    compile_command_claim=receipt["compile_command"],
                                    semantics="matches observed bytes and stated paths; no claim that this driver observed compilation")
            output.write_json("inputs.before.json", inputs_before, 64 * 1024, final=True)
            for fixture, case in zip(_fixed_fixtures(), cases):
                phase = fixture.case_id
                _check_deadline(limits.deadline)
                case["status"] = "running"
                case["process"] = run_probe(probe_path, fixture, output, limits.deadline)
                if not case["process"]["succeeded"]:
                    raise DiagnosticError(f"{fixture.case_id}: process failed; raw streams and process record retained")
                raw = _read_bounded(output.path / case["process"]["stdout_file"], MAX_STREAM_BYTES, limits.deadline)
                doc = validate_probe_document(raw, fixture)
                case["validated_frame_count"] = len(doc["frames"])
                case["metrics"] = _evaluate_case(output, fixture, doc, limits.deadline)
                case["status"] = "passed" if case["metrics"]["passed"] else "failed"
                if not case["metrics"]["passed"]:
                    raise DiagnosticError(f"{fixture.case_id}: new diagnostic numerical/bookkeeping check failed")
            _check_deadline(limits.deadline)
        except (Exception, KeyboardInterrupt) as exc:
            failure = _error(phase, exc)
            failures.append(failure)
            for case in cases:
                if case["case_id"] == phase:
                    case["status"] = "failed"
                    case["errors"].append(failure)
        finally:
            # Failure-dominant finalization has a separate, finite byte/file budget.
            # No further numerical calculation or producer execution occurs here.
            limits.stop_alarm()
            if time.monotonic() >= limits.deadline and not any(
                    error["type"] == "DeadlineExceeded" for error in failures):
                failures.append(_error("deadline", DeadlineExceeded("experiment reached its wall deadline")))
            try:
                limits.arm(limits.overall_deadline)
                if "inputs.before.json" not in output.names:
                    output.write_json("inputs.before.json", inputs_before, 64 * 1024, final=True)
                inputs_after = _snapshot(paths, limits.overall_deadline)
                output.write_json("inputs.after.json", inputs_after, 64 * 1024, final=True)
                if inputs_before != inputs_after:
                    failures.append(_error("input_integrity", DiagnosticError("input hashes/paths/sizes changed or hashing was incomplete")))
                if "plan.json" in output.entries:
                    plan_after = _hash_file(output.path / "plan.json", 32 * 1024, limits.overall_deadline)["sha256"]
                    plan_regenerated = hashlib.sha256(_json_bytes(plan())).hexdigest()
                    if plan_before != plan_after or plan_before != plan_regenerated:
                        failures.append(_error("plan_integrity", DiagnosticError("retained or regenerated plan hash changed")))
                artifact_integrity = output.verify(limits.overall_deadline)
                if any(not value["matched"] for value in artifact_integrity.values()):
                    failures.append(_error("artifact_integrity", DiagnosticError("retained artifact hash/size mismatch or verification failure")))
            except (Exception, KeyboardInterrupt) as exc:
                failures.append(_error("integrity_finalization", exc))
            finally:
                limits.stop_alarm()

        completed = sum(case["metrics"] is not None for case in cases)
        evaluated_samples = sum(case["metrics"]["sample_count"] for case in cases if case["metrics"] is not None)
        numerical_passed = (completed == 4 and evaluated_samples == EXPECTED_SAMPLES
                            and all(case["metrics"]["passed"] for case in cases))
        reproducibility_complete = (receipt_info["byte_consistent"] and inputs_before == inputs_after
                                    and plan_before is not None and plan_before == plan_after == plan_regenerated
                                    and set(artifact_integrity) == set(output.entries)
                                    and all(value["matched"] for value in artifact_integrity.values()))
        result: dict[str, Any] = {
            "schema": "rhodes.direct_alias_driver.metrics.v1", "case_count_planned": 4,
            "sample_count_planned": EXPECTED_SAMPLES, "case_count_evaluated": completed,
            "sample_count_evaluated": evaluated_samples, "numerical_passed": numerical_passed,
            "reproducibility_complete": reproducibility_complete,
            "new_diagnostic_budgets": initial_plan["new_diagnostic_budgets"] if initial_plan else {
                "bookkeeping_peak_v": 1e-12, "primitive_numerical_peak_v": 1e-10,
                "primitive_numerical_rms_v": 1e-11, "alias_acceptability_threshold": None,
                "changes_historical_budgets_or_acceptance": False},
            "alias_acceptability_threshold": None, "historical_acceptance_changed": False,
            "final_status_authority": "manifest.json includes later retention/integrity failures",
            "cases": cases, "failures": failures,
            "incomplete_reasons": [] if build_record is not None else ["compiler/build provenance unavailable: no --build-record supplied"],
        }
        try:
            output.write_json("metrics.json", result, 256 * 1024, final=True)
            artifact_integrity.update(output.verify(limits.overall_deadline, ("metrics.json",)))
            if not artifact_integrity["metrics.json"]["matched"]:
                raise DiagnosticError("final metrics bytes failed integrity verification")
        except (Exception, KeyboardInterrupt) as exc:
            failures.append(_error("metrics_retention", exc))
            reproducibility_complete = False
        passed = numerical_passed and reproducibility_complete and not failures
        manifest: dict[str, Any] = {
            "schema": "rhodes.direct_alias_driver.manifest.v1", "passed": passed,
            "status": "passed" if passed else ("failed" if failures else "incomplete"),
            "scope": "four synthetic stationary primitive cases only; no alias acceptability or acceptance promotion",
            "output_dir": str(output.path), "case_order": list(CASE_IDS),
            "case_count_evaluated": completed, "sample_count_planned": EXPECTED_SAMPLES,
            "sample_count_evaluated": evaluated_samples, "numerical_passed": numerical_passed,
            "reproducibility_complete": reproducibility_complete, "build_receipt": receipt_info,
            "plan_sha256_before": plan_before, "plan_sha256_after": plan_after,
            "regenerated_plan_sha256_after": plan_regenerated,
            "inputs_before": inputs_before, "inputs_after": inputs_after,
            "limits": limits.record, "failures": failures,
            "incomplete_reasons": result["incomplete_reasons"],
            "wall_seconds_before_manifest": time.monotonic() - started,
            "python": {"version": sys.version, "executable": sys.executable, "platform": sys.platform},
            "artifacts": dict(output.entries), "artifact_bytes_before_manifest": output.total,
            "artifact_integrity": artifact_integrity,
            "artifact_bytes_cap": MAX_ARTIFACT_BYTES,
            "manifest_hash_note": "manifest excludes its own hash; all other retained files are inventoried",
        }
        encoded = _json_bytes(manifest)
        manifest["artifact_bytes_including_manifest"] = output.total + len(encoded)
        for _ in range(4):
            encoded = _json_bytes(manifest)
            total = output.total + len(encoded)
            if total == manifest["artifact_bytes_including_manifest"]:
                break
            manifest["artifact_bytes_including_manifest"] = total
        output.write_bytes("manifest.json", encoded, 256 * 1024, final=True)
    return manifest


def main(argv: list[str] | None = None) -> int:
    arguments = list(sys.argv[1:] if argv is None else argv)
    parser = argparse.ArgumentParser(description=__doc__, allow_abbrev=False)
    parser.add_argument("--plan", action="store_true", help="print the fixed plan; no producer execution or files")
    parser.add_argument("--probe", type=Path, help="path to the bounded native primitive producer")
    parser.add_argument("--output-dir", type=Path, help="NEW directory for exclusively retained artifacts")
    parser.add_argument("--build-record", type=Path, help="optional explicit worker-029-format build receipt")
    options = [arg.split("=", 1)[0] for arg in arguments if arg.startswith("-")]
    if len(options) != len(set(options)):
        parser.error("repeated options are not accepted")
    if any(arg in ("--help", "-h") for arg in arguments) and len(arguments) != 1:
        parser.error("--help must be used alone")
    args = parser.parse_args(arguments)
    if args.plan:
        if len(arguments) != 1:
            parser.error("--plan must be used alone")
        sys.stdout.write(_json_bytes(plan()).decode("utf-8"))
        return 0
    if args.probe is None or args.output_dir is None:
        parser.error("execution requires --probe PATH and --output-dir NEW")
    try:
        manifest = execute(args.probe, args.output_dir, args.build_record)
    except (Exception, KeyboardInterrupt) as exc:
        print(f"validate_direct_alias_toy: {type(exc).__name__}: {str(exc)[:2048]}", file=sys.stderr)
        return 2
    print(json.dumps({key: manifest[key] for key in (
        "status", "passed", "numerical_passed", "reproducibility_complete", "output_dir",
        "case_count_evaluated", "sample_count_evaluated", "artifact_bytes_including_manifest")}, allow_nan=False))
    return 0 if manifest["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
