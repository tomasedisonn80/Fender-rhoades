"""Adversarial retained-reference protocol tests, not trajectory evidence.

The NPZ/NPY fixtures are written independently with the standard library. Their
constant states are deliberately synthetic, and no test integrates a reference,
runs a real worker, or creates the 26-candidate experiment. Oversized declarations
contain headers only; guards forbid numerical loading before their rejection.
Only the dense replay/cache tests require the optional NumPy installation.
"""
from __future__ import annotations

from contextlib import ExitStack
import copy
from dataclasses import asdict, dataclass
import hashlib
import importlib.util
import io
import json
import math
import os
from pathlib import Path
import struct
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, patch
import warnings
import zipfile


ROOT = Path(__file__).resolve().parents[2]
TOOLS = ROOT / "tools"
with patch.object(sys, "path", [str(TOOLS), *sys.path]):
    import contact_extrema_reference as reference
    import validate_contact_extrema as driver


HAS_NUMPY = importlib.util.find_spec("numpy") is not None
COVERAGE = "uncertified_independent_search"
FIXTURE_HASHES = {
    "supported_impact": "7a8778c66a118885c001d3d7c11c343dc1b93e555bc3cb17a485d084af7033b8",
    "supported_impact_scaled": "f4c3599e63f34854d6d67a04dcd0ed76554fd128a9fe6f52e6026b4e9bb483fe",
}
ARRAY_NAMES = (
    "mesh_t_s", "mesh_y", "dense_t0_s", "dense_t1_s", "dense_coefficients",
    "reconstruction_residual", "reconstruction_limit", "candidate_time_s",
    "candidate_y", "candidate_segment_id", "candidate_origin_bits",
)
BASE_ATOL = [1e-16, 1e-16, 1e-13, 1e-13, 1e-16, 1e-13,
             1e-16, 1e-16, 1e-16, 1e-16, 1e-16, 1e-16, 1e-16]
SETTINGS = {
    name: {"maximum_step_s": step, "relative_tolerance": relative,
           "absolute_vector_multiplier": multiplier,
           "absolute_tolerance_vector": [value * multiplier for value in BASE_ATOL]}
    for name, step, relative, multiplier in (
        ("R0", 5e-6, 1e-10, 10.0), ("R1", 2.5e-6, 1e-12, 1.0),
        ("R2", 1.25e-6, 1e-13, 0.1))
}


def sha(raw):
    return hashlib.sha256(raw).hexdigest()


@dataclass(frozen=True)
class ArrayBytes:
    dtype: str
    shape: tuple
    payload: bytes


def packed(dtype, shape, values):
    code = {"<f8": "d", "<i8": "q", "<u8": "Q"}[dtype]
    values = tuple(values)
    return ArrayBytes(dtype, tuple(shape), struct.pack("<" + code * len(values), *values))


def tiny_arrays(*, mesh=2, segments=1, candidates=2, compression=0.001):
    """One constant 13-state cubic by default: 1,120 payload bytes, 20 N."""
    state = [0.0] * 13
    state[4] = compression
    mesh_times = [0.020 * i / max(1, mesh - 1) for i in range(mesh)]
    segment_starts = [0.020 * i / max(1, mesh - 1) for i in range(segments)]
    segment_ends = [0.020 * (i + 1) / max(1, mesh - 1) for i in range(segments)]
    last_dense = segment_ends[-1] if segment_ends else 0.0
    candidate_times = [last_dense * i / max(1, candidates - 1) for i in range(candidates)]
    coefficient = [value for component in state for value in (component, 0.0, 0.0, 0.0)]
    return {
        "mesh_t_s": packed("<f8", (mesh,), mesh_times),
        "mesh_y": packed("<f8", (mesh, 13), state * mesh),
        "dense_t0_s": packed("<f8", (segments,), segment_starts),
        "dense_t1_s": packed("<f8", (segments,), segment_ends),
        "dense_coefficients": packed("<f8", (segments, 13, 4), coefficient * segments),
        "reconstruction_residual": packed("<f8", (segments, 13), [0.0] * (segments * 13)),
        "reconstruction_limit": packed("<f8", (segments, 13), [1e-14] * (segments * 13)),
        "candidate_time_s": packed("<f8", (candidates,), candidate_times),
        "candidate_y": packed("<f8", (candidates, 13), state * candidates),
        "candidate_segment_id": packed("<i8", (candidates,),
                                       [-1] + [max(-1, segments - 1)] * (candidates - 1)
                                       if candidates else []),
        "candidate_origin_bits": packed("<u8", (candidates,), [1] * candidates),
    }


def npy_bytes(array, *, version=1, header=None, fortran=False):
    """Encode the documented NPY container; never ask NumPy to serialize."""
    text = repr({"descr": array.dtype, "fortran_order": fortran, "shape": array.shape}) \
        if header is None else header
    length_bytes = 2 if version == 1 else 4
    prefix_size = 8 + length_bytes
    encoded = text.encode("latin1")
    padding = (-(prefix_size + len(encoded) + 1)) % 64
    encoded += b" " * padding + b"\n"
    return (b"\x93NUMPY" + bytes((version, 0))
            + struct.pack("<H" if version == 1 else "<I", len(encoded))
            + encoded + array.payload)


def archive_bytes(arrays, *, members=None, version=1, compression=zipfile.ZIP_DEFLATED,
                  comment=b""):
    buffer = io.BytesIO()
    rows = list(members) if members is not None else [
        (name + ".npy", npy_bytes(value, version=version)) for name, value in arrays.items()]
    with zipfile.ZipFile(buffer, "w", compression=compression) as archive:
        for name, raw in rows:
            archive.writestr(name, raw)
        archive.comment = comment
    return buffer.getvalue()


def protocol_record(path, arrays=None, *, fixture="supported_impact", level="R0",
                    complete=True, archive=None):
    arrays = tiny_arrays() if arrays is None else arrays
    path.write_bytes(archive_bytes(arrays) if archive is None else archive)
    mesh = arrays["mesh_t_s"].shape[0]
    candidate_count = arrays["candidate_time_s"].shape[0]
    peaks = {}
    for name in ("hammer_compression", "hammer_force", "damper_compression", "damper_force"):
        disabled = name.startswith("damper_")
        unit = "m" if name.endswith("compression") else "n"
        value = 0.0 if disabled else 0.001 if unit == "m" else 20.0
        peaks[name] = {"value_" + unit: value, "classification": "disabled" if disabled else "stationary",
                       "kind": "disabled" if disabled else "isolated",
                       "maximizer_intervals_s": [[0.0, 0.020]] if disabled else [[0.004, 0.004]]}
    return {
        "schema_version": 1, "reference_kind": "independent_supported_contact_extrema",
        "fixture": fixture, "fixture_sha256": FIXTURE_HASHES[fixture], "level": level,
        "settings": copy.deepcopy(SETTINGS[level]), "coverage": COVERAGE,
        "source_sha256": {name: sha((TOOLS / name).read_bytes()) for name in
                          ("contact_extrema_reference.py", "validate_contact_reference.py")},
        "status": "completed" if complete else "inconclusive_reference",
        "value_status": "requires_refinement" if complete else "inconclusive_reference",
        "complete_duration": complete, "duration_s": 0.020,
        "final_time_s": 0.020 if mesh > 1 else 0.0 if mesh else None,
        "counts": {"accepted_steps": max(0, mesh - 1), "candidates": candidate_count,
                   "rhs_evaluations": 0, "search_evaluations": 0, "elapsed_s": 0.0},
        "peaks": peaks, "failure": None if complete else {"reason": "synthetic cap"},
        "unresolved": [], "branches": [], "resolved_peak_clusters": [], "root_candidates": [],
        "search_statistics": {"maximum_physical_dense_derivative_discrepancy_m_per_s": 0.0},
        "raw": {"path": str(path), "sha256": sha(path.read_bytes()), "arrays": {
            name: {"shape": list(value.shape), "dtype": value.dtype, "sha256": sha(value.payload)}
            for name, value in arrays.items()}},
    }


def worker_record(path, *, fixture="supported_impact", level="R0", complete=True,
                  arrays=None, source_sha256=None):
    """Write a tiny stdlib NPZ and return a valid, synthetic worker protocol.

    Source identities may be supplied for an isolated fake worker directory.
    The existing independent fixture definitions are read without integrating
    them; NPZ bytes, shapes, counters, settings and peak records are constructed
    here, independently of production serialization and reference generation.
    """
    arrays = tiny_arrays() if arrays is None else arrays
    result = protocol_record(path, arrays, fixture=fixture, level=level, complete=complete)
    independent_fixture = reference.fixtures()[fixture]
    result["fixture_definition"] = {
        **asdict(independent_fixture),
        "evaluated_stiffness_n_per_m": list(independent_fixture.stiffness()),
        "evaluated_damping_ns_per_m": list(independent_fixture.damping()),
    }
    result["limits"] = {"accepted_steps": 200000, "rhs_evaluations": 5000000,
                        "search_evaluations": 1000000, "candidates": 100000, "wall_seconds": 300.0}
    if source_sha256 is not None:
        result["source_sha256"] = dict(source_sha256)
    result["raw"].update(
        basis="ascending normalized u=(t-t0)/(t1-t0)",
        state_coordinates=["q0_m", "q1_m", "v0_m_per_s", "v1_m_per_s",
                           "hammer_position_m", "hammer_velocity_m_per_s",
                           "modal_loss_j", "hammer_loss_j", "damper_loss_j",
                           "actuator_work_j", "launch_work_j", "hammer_impulse_ns", "damper_impulse_ns"],
        candidate_origin_bits={"mesh_endpoint": 1, "dense_endpoint": 2, "branch_boundary": 4,
                               "physical_rhs_derivative": 8, "dense_derivative": 16, "plateau_endpoint": 32},
    )
    for name, peak in result["peaks"].items():
        disabled = name.startswith("damper_")
        peak.update(
            unit="m" if name.endswith("compression") else "N", coverage=COVERAGE,
            kind="disabled" if disabled else "isolated_candidate", maximizing_candidates=[],
            value_status="analytic_disabled_identity" if disabled else
                         "requires_refinement" if complete else "inconclusive_reference",
            time_status="disabled" if disabled else
                        "numerical_candidates" if complete else "inconclusive_reference",
        )
        if not disabled:
            peak["maximizer_intervals_s"] = []
            peak["classification"] = "endpoint"
            if result["counts"]["candidates"]:
                peak["maximizer_intervals_s"] = [[0.0, 0.0]]
                peak["maximizing_candidates"] = [
                    {"candidate_id": 0, "time_s": 0.0, "segment_id": -1,
                     "origin_bits": 1, "gap_m": 0.001, "details": []}]
    return result


class RawFilesCase(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.directory = Path(temporary.name).resolve()
        self.path = self.directory / "retained.npz"

    def record(self, arrays=None, **kwargs):
        return protocol_record(self.path, arrays, **kwargs)

    def assert_inspection_rejected(self, record):
        with patch.object(reference, "_numpy", side_effect=AssertionError("numerical backend reached")):
            with self.assertRaises((ValueError, OSError)):
                reference.inspect_retained_raw(record)

    def replace_member(self, arrays, target, raw):
        return archive_bytes(arrays, members=[
            (name + ".npy", raw if name == target else npy_bytes(value))
            for name, value in arrays.items()])


class ContactExtremaRawStorageTests(RawFilesCase):
    def test_storage_caps_derive_from_existing_work_limits_without_backend(self):
        with patch.object(reference, "_numpy", side_effect=AssertionError("numerical backend reached")):
            default = reference.raw_storage_limits()
            zero = reference.raw_storage_limits({"accepted_steps": 0, "candidates": 0})
            tightened = reference.raw_storage_limits({"accepted_steps": 2, "candidates": 3})
        self.assertEqual(default["array_bytes"], 163_200_112)
        self.assertEqual(zero["array_bytes"], 112)
        self.assertEqual(tightened["array_bytes"], 2_000)
        for item in (zero, tightened, default):
            self.assertLessEqual(item["array_bytes"], item["uncompressed_bytes"])
            self.assertLessEqual(item["uncompressed_bytes"], item["array_bytes"] + 11 * 4096)
            self.assertGreaterEqual(item["compressed_bytes"], item["uncompressed_bytes"])
        for key in ("array_bytes", "uncompressed_bytes", "compressed_bytes"):
            self.assertLess(zero[key], tightened[key])
            self.assertLess(tightened[key], default[key])

    def test_storage_limits_cannot_raise_or_weaken_work_caps(self):
        invalid = [{"accepted_steps": 200001}, {"candidates": 100001}, {"accepted_steps": 2**80},
                   {"accepted_steps": True}, {"candidates": -1}, {"candidates": 1.0},
                   {"wall_seconds": math.inf}, {"unknown": 1}, []]
        for limits in invalid:
            with self.subTest(limits=limits), self.assertRaises(ValueError):
                reference.raw_storage_limits(limits)

    def test_valid_compressed_and_stored_npy_versions_inspect_without_numpy(self):
        arrays = tiny_arrays()
        self.assertEqual(set(arrays), set(ARRAY_NAMES))
        self.assertEqual(sum(len(value.payload) for value in arrays.values()), 1120)
        for version in (1, 2):
            for compression in (zipfile.ZIP_STORED, zipfile.ZIP_DEFLATED):
                with self.subTest(version=version, compression=compression):
                    record = self.record(arrays, archive=archive_bytes(arrays, version=version,
                                                                      compression=compression))
                    with patch.object(reference, "_numpy", side_effect=AssertionError("backend reached")), \
                            patch.object(Path, "read_bytes", side_effect=AssertionError("unbounded read")):
                        result = reference.inspect_retained_raw(record)
                    self.assertIsInstance(result, dict)
                    self.assertEqual(result["array_bytes"], 1120)
                    self.assertEqual(result["sha256"], record["raw"]["sha256"])

    def test_existing_synthetic_record_without_optional_counters_or_limits_is_accepted(self):
        record = self.record()
        for name in ("counts", "status", "complete_duration", "final_time_s"):
            record.pop(name)
        inspected = reference.inspect_retained_raw(record)
        self.assertEqual(inspected["array_bytes"], 1120)

    def test_complete_and_capped_partial_mesh_relations_are_accepted(self):
        cases = [(2, 1, 2, True), (0, 0, 0, False), (1, 0, 0, False),
                 (1, 0, 1, False), (2, 0, 1, False), (3, 1, 2, False)]
        for mesh, segments, candidates, complete in cases:
            with self.subTest(mesh=mesh, segments=segments, candidates=candidates, complete=complete):
                arrays = tiny_arrays(mesh=mesh, segments=segments, candidates=candidates)
                record = self.record(arrays, complete=complete)
                record["limits"] = {"accepted_steps": max(0, mesh - 1), "candidates": candidates}
                result = reference.inspect_retained_raw(record)
                self.assertEqual(result["array_bytes"], sum(len(row.payload) for row in arrays.values()))

    def test_invalid_mesh_dense_candidate_count_relations_are_rejected(self):
        cases = [(0, 0, 0, True), (1, 0, 1, True), (2, 1, 0, True),
                 (3, 1, 2, True), (1, 1, 1, False), (4, 1, 2, False), (0, 1, 0, False)]
        for mesh, segments, candidates, complete in cases:
            with self.subTest(mesh=mesh, segments=segments, candidates=candidates, complete=complete):
                record = self.record(tiny_arrays(mesh=mesh, segments=segments, candidates=candidates),
                                     complete=complete)
                self.assert_inspection_rejected(record)

    def test_present_counters_and_per_record_limits_must_match_retained_shapes(self):
        cases = [({"counts": {"accepted_steps": 0}}, "accepted_steps"),
                 ({"counts": {"candidates": 1}}, "candidates"),
                 ({"counts": {"accepted_steps": True}}, "boolean count"),
                 ({"counts": {"candidates": 2.0}}, "noninteger count"),
                 ({"limits": {"accepted_steps": 0}}, "step limit"),
                 ({"limits": {"candidates": 1}}, "candidate limit")]
        for changed, label in cases:
            with self.subTest(case=label):
                record = self.record()
                for key, value in changed.items():
                    record.setdefault(key, {}).update(value)
                self.assert_inspection_rejected(record)


class ContactExtremaRawSchemaTests(RawFilesCase):
    def test_exact_eleven_array_names_are_required_in_manifest_and_zip(self):
        arrays = tiny_arrays()
        extra = {**arrays, "unexpected": packed("<f8", (1,), [0.0])}
        record = self.record(extra)
        self.assert_inspection_rejected(record)
        record = self.record()
        del record["raw"]["arrays"]["mesh_y"]
        self.assert_inspection_rejected(record)
        members = [(name + ".npy", npy_bytes(value)) for name, value in arrays.items()]
        variants = [members[:-1], members + [("README.txt", b"not a retained array")],
                    [("../mesh_t_s.npy", members[0][1]), *members[1:]],
                    [("mesh_t_s.NPY", members[0][1]), *members[1:]]]
        with warnings.catch_warnings():
            warnings.simplefilter("ignore", UserWarning)
            variants.append(members + [members[0]])
            for index, rows in enumerate(variants):
                with self.subTest(variant=index):
                    self.assert_inspection_rejected(self.record(arrays, archive=archive_bytes(arrays, members=rows)))

    def test_exact_dtypes_reject_float_width_endian_object_and_integer_sign_changes(self):
        cases = [("mesh_y", dtype) for dtype in (">f8", "<f4", "|O", "<c16", "|b1", "<M8[ns]")]
        cases += [("candidate_segment_id", "<u8"), ("candidate_origin_bits", "<i8")]
        for name, dtype in cases:
            with self.subTest(array=name, dtype=dtype):
                arrays = tiny_arrays()
                old = arrays[name]
                arrays[name] = ArrayBytes(dtype, old.shape, old.payload)
                self.assert_inspection_rejected(self.record(arrays))

    def test_every_array_shape_is_validated_before_loading(self):
        shapes = {
            "mesh_t_s": (2, 1), "mesh_y": (2, 12), "dense_t0_s": (2,), "dense_t1_s": (0,),
            "dense_coefficients": (1, 13, 5), "reconstruction_residual": (1, 12),
            "reconstruction_limit": (1, 13, 1), "candidate_time_s": (1,), "candidate_y": (0, 13),
            "candidate_segment_id": (2, 1), "candidate_origin_bits": (1,),
        }
        for name, shape in shapes.items():
            with self.subTest(array=name, shape=shape):
                arrays = tiny_arrays()
                original = arrays[name]
                count = math.prod(shape)
                arrays[name] = ArrayBytes(original.dtype, shape, b"\x00" * (count * 8))
                self.assert_inspection_rejected(self.record(arrays))

    def test_malformed_manifest_shapes_and_hashes_are_rejected(self):
        fields = [("shape", [-1]), ("shape", [True]), ("shape", [2.0]), ("shape", "2"),
                  ("shape", [2**80]), ("dtype", None), ("sha256", "0" * 63),
                  ("sha256", "g" * 64)]
        for key, value in fields:
            with self.subTest(field=key, value=value):
                record = self.record()
                record["raw"]["arrays"]["mesh_t_s"][key] = value
                self.assert_inspection_rejected(record)

    def test_header_and_manifest_must_agree_even_when_archive_hash_matches(self):
        arrays = tiny_arrays()
        for header in ("{'descr': '<f8', 'fortran_order': False, 'shape': (1099511627776,)}",
                       "{'descr': '>f8', 'fortran_order': False, 'shape': (1, 13, 4)}",
                       "{'descr': '<f8', 'fortran_order': True, 'shape': (1, 13, 4)}"):
            with self.subTest(header=header):
                raw = npy_bytes(ArrayBytes("<f8", (1, 13, 4), b""), header=header)
                record = self.record(arrays, archive=self.replace_member(arrays, "dense_coefficients", raw))
                self.assert_inspection_rejected(record)

    def test_hashes_bind_archive_and_each_array_payload(self):
        record = self.record()
        self.path.write_bytes(self.path.read_bytes() + b"changed archive")
        self.assert_inspection_rejected(record)
        arrays = tiny_arrays()
        payload = bytearray(arrays["mesh_y"].payload)
        payload[0] ^= 1
        old = arrays["mesh_y"]
        changed = npy_bytes(ArrayBytes(old.dtype, old.shape, bytes(payload)))
        record = self.record(arrays, archive=self.replace_member(arrays, "mesh_y", changed))
        self.assert_inspection_rejected(record)


class ContactExtremaRawHeaderTests(RawFilesCase):
    def test_tiny_oversized_shape_header_never_reaches_numerical_loading(self):
        arrays = tiny_arrays()
        raw = npy_bytes(ArrayBytes("<f8", (2**40,), b""))
        self.assertLess(len(raw), 4096)
        record = self.record(arrays, archive=self.replace_member(arrays, "dense_coefficients", raw))
        self.assert_inspection_rejected(record)
        if HAS_NUMPY:
            import numpy as np
            with patch.object(np, "load", side_effect=AssertionError("array allocation path reached")) as load:
                with self.assertRaises(ValueError):
                    reference._load_arrays(record)
            load.assert_not_called()

    def test_declared_large_header_is_rejected_before_large_zip_read(self):
        arrays = tiny_arrays()
        malformed = b"\x93NUMPY\x02\x00" + struct.pack("<I", 2**30) + b"\x00" * 16
        record = self.record(arrays, archive=self.replace_member(arrays, "mesh_t_s", malformed))
        original = zipfile.ZipExtFile.read
        requests = []
        def bounded_read(handle, size=-1):
            requests.append(size)
            if size < 0 or size > 4096:
                raise AssertionError("unbounded header read requested: " + str(size))
            return original(handle, size)
        with patch.object(zipfile.ZipExtFile, "read", bounded_read):
            self.assert_inspection_rejected(record)
        self.assertTrue(requests)
        self.assertLessEqual(max(requests), 4096)

    def test_header_length_cap_applies_even_to_well_formed_literal(self):
        arrays = tiny_arrays()
        header = "{'descr': '<f8', 'fortran_order': False, 'shape': (2,)}" + " " * 4096
        raw = npy_bytes(arrays["mesh_t_s"], version=2, header=header)
        self.assert_inspection_rejected(self.record(arrays, archive=self.replace_member(arrays, "mesh_t_s", raw)))

    def test_invalid_header_literals_magic_versions_and_payload_lengths_are_rejected(self):
        arrays = tiny_arrays()
        item = arrays["mesh_t_s"]
        headers = ["{'descr': '<f8', 'fortran_order': False, 'shape': (-1,)}",
                   "{'descr': '<f8', 'fortran_order': False, 'shape': (True,)}",
                   "{'descr': '<f8', 'fortran_order': False, 'shape': (2.0,)}",
                   "{'descr': '<f8', 'fortran_order': False, 'shape': [2]}",
                   "{'descr': '<f8', 'fortran_order': False, 'shape': (2,), 'extra': 1}",
                   "{'descr': '<f8', 'fortran_order': False, 'shape': (__import__('math').sqrt(4),)}"]
        valid = npy_bytes(item)
        malformed = [npy_bytes(item, header=header) for header in headers]
        malformed += [b"BADMAGIC" + valid[8:], valid[:6] + b"\x63\x00" + valid[8:],
                      valid[:9], valid[:-1], valid + b"\x00"]
        for index, raw in enumerate(malformed):
            with self.subTest(case=index):
                self.assert_inspection_rejected(self.record(arrays, archive=self.replace_member(arrays, "mesh_t_s", raw)))

    def test_uncompressed_zip_size_cap_is_checked_before_member_read(self):
        arrays = tiny_arrays()
        raw = bytearray(archive_bytes(arrays))
        offset = raw.index(b"PK\x01\x02")
        struct.pack_into("<I", raw, offset + 24, 2**31)
        record = self.record(arrays, archive=bytes(raw))
        with patch.object(zipfile.ZipFile, "open", side_effect=AssertionError("oversized member opened")) as opened:
            self.assert_inspection_rejected(record)
        opened.assert_not_called()


class ContactExtremaRawFileTests(RawFilesCase):
    def test_compressed_snapshot_cap_rejects_file_before_open(self):
        record = self.record(tiny_arrays(mesh=1, segments=0, candidates=0), complete=False)
        record["limits"] = {"accepted_steps": 0, "candidates": 0}
        cap = reference.raw_storage_limits(record["limits"])["compressed_bytes"]
        self.assertLess(cap, 128 * 1024)
        self.path.write_bytes(b"x" * (cap + 1))
        with patch.object(reference.os, "open", side_effect=AssertionError("oversized file opened")) as opened:
            self.assert_inspection_rejected(record)
        opened.assert_not_called()

    def test_snapshot_reads_are_bounded_without_sentinel_or_unbounded_read(self):
        arrays = tiny_arrays(candidates=512)
        record = self.record(arrays, archive=archive_bytes(arrays, compression=zipfile.ZIP_STORED))
        expected = self.path.read_bytes()
        self.assertGreater(len(expected), 65536)
        self.assertLess(len(expected), 128 * 1024)
        original = os.read
        requests, returned = [], []
        def read(descriptor, size):
            self.assertGreater(size, 0)
            self.assertLessEqual(size, min(65536, len(expected) - sum(returned)))
            requests.append(size)
            raw = original(descriptor, size)
            returned.append(len(raw))
            return raw
        with patch.object(reference.os, "read", side_effect=read):
            inspected = reference.inspect_retained_raw(record)
        self.assertEqual(sum(returned), len(expected))
        self.assertEqual(inspected["bytes"], len(expected))
        self.assertEqual(inspected["sha256"], sha(expected))
        self.assertEqual(len(requests), 2)
        self.assertEqual(requests[0], 65536)

    def test_replacing_path_during_snapshot_read_is_rejected(self):
        record = self.record()
        replacement = self.directory / "replacement.npz"
        replacement.write_bytes(self.path.read_bytes())
        original = os.read
        replaced = []
        def read_then_replace(descriptor, size):
            block = original(descriptor, size)
            if not replaced:
                os.replace(replacement, self.path)
                replaced.append(True)
            return block
        with patch.object(reference.os, "read", side_effect=read_then_replace):
            self.assert_inspection_rejected(record)
        self.assertEqual(replaced, [True])

    def test_symlink_leaf_and_ancestor_are_rejected_before_open(self):
        record = self.record()
        leaf = self.directory / "leaf.npz"
        ancestor = self.directory / "linked-directory"
        try:
            leaf.symlink_to(self.path)
            ancestor.symlink_to(self.directory, target_is_directory=True)
        except (OSError, NotImplementedError) as exc:
            self.skipTest("symlinks unavailable: " + str(exc))
        for path in (leaf, ancestor / self.path.name):
            with self.subTest(path=str(path)):
                changed = copy.deepcopy(record)
                changed["raw"]["path"] = str(path)
                with patch.object(reference.os, "open", side_effect=AssertionError("symlink was opened")) as opened:
                    self.assert_inspection_rejected(changed)
                opened.assert_not_called()

    @unittest.skipUnless(hasattr(os, "mkfifo"), "FIFO creation requires a POSIX-like platform")
    def test_fifo_is_rejected_without_opening_or_reading_it(self):
        record = self.record()
        fifo = self.directory / "retained.fifo"
        os.mkfifo(fifo)
        record["raw"]["path"] = str(fifo)
        with patch.object(reference.os, "open", side_effect=AssertionError("FIFO was opened")) as opened:
            self.assert_inspection_rejected(record)
        opened.assert_not_called()

    def test_directory_and_absent_path_are_rejected(self):
        record = self.record()
        for path in (self.directory, self.directory / "absent.npz"):
            with self.subTest(path=str(path)):
                record["raw"]["path"] = str(path)
                self.assert_inspection_rejected(record)


@unittest.skipUnless(HAS_NUMPY, "optional NumPy not installed; stdlib raw inspection remains covered")
class ContactExtremaRawReplayTests(RawFilesCase):
    def test_independent_complete_and_partial_cubics_load_and_evaluate(self):
        for mesh, complete, requested in ((2, True, 0.010), (3, False, 0.005)):
            with self.subTest(mesh=mesh, complete=complete):
                arrays = tiny_arrays(mesh=mesh)
                record = self.record(arrays, complete=complete)
                loaded = reference._load_arrays(record)
                self.assertEqual(set(loaded), set(ARRAY_NAMES))
                for name, item in arrays.items():
                    self.assertEqual(loaded[name].dtype.str, item.dtype)
                    self.assertEqual(loaded[name].shape, item.shape)
                    self.assertEqual(loaded[name].tobytes(order="C"), item.payload)
                evaluated = reference.evaluate_reference(record, requested)
                self.assertEqual(evaluated["hammer_compression_m"], 0.001)
                self.assertEqual(evaluated["hammer_force_n"], 20.0)
                self.assertEqual(evaluated["damper_force_n"], 0.0)
                if not complete:
                    with self.assertRaises(ValueError):
                        reference.evaluate_reference(record, 0.015)

    def test_loader_parses_exact_hashed_snapshot_when_path_changes_before_numpy_load(self):
        import numpy as np

        arrays = tiny_arrays()
        record = self.record(arrays)
        expected = self.path.read_bytes()
        changed = archive_bytes(arrays, comment=b"changed after the bounded snapshot was validated")
        self.assertNotEqual(sha(expected), sha(changed))
        original_load = np.load
        snapshots = []
        def replace_then_load(file, *args, **kwargs):
            self.assertFalse(isinstance(file, (str, bytes, os.PathLike)), "loader reopened the mutable pathname")
            self.assertTrue(hasattr(file, "getvalue"), "loader did not retain an in-memory snapshot")
            snapshots.append(file.getvalue())
            self.path.write_bytes(changed)
            return original_load(file, *args, **kwargs)
        with patch.object(np, "load", side_effect=replace_then_load):
            result = reference.evaluate_reference(record, 0.010)
        self.assertEqual(snapshots, [expected])
        self.assertEqual(result["hammer_force_n"], 20.0)
        self.assertEqual(record["raw"]["sha256"], sha(expected))
        self.assertNotEqual(sha(self.path.read_bytes()), record["raw"]["sha256"])
        with self.assertRaises(ValueError):
            reference.evaluate_reference(record, 0.010)

    def test_changed_raw_archive_cannot_reuse_successful_pointwise_cache(self):
        records = {level: protocol_record(self.directory / (level + ".npz"), level=level)
                   for level in ("R0", "R1", "R2")}
        evaluator = Mock(wraps=reference.evaluate_reference)
        cache = {}
        before = driver._at_candidate_time("hammer_force", 20.0, 0.010, records, evaluator, cache)
        self.assertEqual(before["status"], "pass")
        self.assertEqual(evaluator.call_count, 6)
        victim = Path(records["R0"]["raw"]["path"])
        victim.write_bytes(victim.read_bytes() + b"archive provenance changed")
        after = driver._at_candidate_time("hammer_force", 20.0, 0.010, records, evaluator, cache)
        self.assertNotEqual(after["status"], "pass")
        self.assertTrue(after["errors"])

    def test_replacing_reference_identity_invalidates_previous_pointwise_cache(self):
        records = {level: protocol_record(self.directory / (level + ".npz"), level=level)
                   for level in ("R0", "R1", "R2")}
        evaluator = Mock(wraps=reference.evaluate_reference)
        cache = {}
        before = driver._at_candidate_time("hammer_force", 20.0, 0.010, records, evaluator, cache)
        self.assertEqual(before["status"], "pass")
        records = {level: protocol_record(self.directory / (level + "-changed.npz"),
                                          tiny_arrays(compression=0.002), level=level)
                   for level in ("R0", "R1", "R2")}
        after = driver._at_candidate_time("hammer_force", 20.0, 0.010, records, evaluator, cache)
        self.assertEqual(after["status"], "fail")
        self.assertEqual(evaluator.call_count, 12)
        for side in ("left", "right"):
            self.assertEqual(after["sides"][side]["values"], {"R0": 40.0, "R1": 40.0, "R2": 40.0})


class RawWorkerCase(RawFilesCase):
    def setUp(self):
        super().setUp()
        stack = ExitStack()
        self.addCleanup(stack.close)
        (self.directory / "tools").mkdir()
        self.source_hashes = {}
        for name in ("contact_extrema_reference.py", "validate_contact_reference.py"):
            content = ("# isolated synthetic source identity for " + name + "\n").encode()
            (self.directory / "tools" / name).write_bytes(content)
            self.source_hashes[name] = sha(content)
        stack.enter_context(patch.object(driver, "ROOT", self.directory))
        stack.enter_context(patch.object(driver, "SOURCE_PATHS", tuple("tools/" + name for name in self.source_hashes)))
        stack.enter_context(patch.object(driver, "_git_identity", return_value={"test": "not queried"}))
        stack.enter_context(patch.object(driver, "_versions", return_value={"test": "stdlib protocol only"}))
        stack.enter_context(patch.object(reference, "_numpy", side_effect=AssertionError("numerical backend reached")))

    def launcher(self, *, mutate=None, complete=True, arrays=None):
        def launch(command, **kwargs):
            arguments = dict(zip(command[2::2], command[3::2]))
            record = worker_record(Path(arguments["--raw"]), fixture=arguments["--fixture"],
                                   level=arguments["--level"], complete=complete, arrays=arrays,
                                   source_sha256=self.source_hashes)
            if mutate is not None:
                mutate(record)
            # Deliberately allow malformed NaN in adversarial worker JSON; the
            # receiver must reject it rather than certify a completed worker.
            Path(arguments["--output"]).write_text(json.dumps(record, allow_nan=True), encoding="utf-8")
            kwargs["stdout"].write(b"synthetic worker protocol; no process launched\n")
            process = Mock(returncode=0)
            process.poll.return_value = 0
            return process
        return launch

    def one_worker(self, name, **kwargs):
        with patch.object(driver.subprocess, "Popen", side_effect=self.launcher(**kwargs)) as process:
            result = driver.run_reference_worker("supported_impact", "R0", self.directory / name)
        process.assert_called_once()
        return result


class ContactExtremaRawWorkerTests(RawWorkerCase):
    def test_complete_worker_and_capped_partial_record_both_retain_valid_npz(self):
        execution, result = self.one_worker("complete")
        self.assertEqual(execution["status"], "completed", execution["errors"])
        self.assertIsNotNone(result)
        self.assertEqual(execution["raw_validation"]["sha256"], result["raw"]["sha256"])
        self.assertEqual(execution["raw_validation"]["array_bytes"], 1120)
        execution, result = self.one_worker("partial", complete=False,
                                            arrays=tiny_arrays(mesh=2, segments=0, candidates=1))
        self.assertEqual(execution["status"], "inconclusive_reference")
        self.assertIsNotNone(result)
        self.assertFalse(result["complete_duration"])
        self.assertTrue(execution["errors"])
        self.assertEqual(execution["raw_validation"]["sha256"], result["raw"]["sha256"])

    def test_worker_schema_and_kind_mutations_cannot_complete(self):
        mutations = {
            "wrong_schema": lambda row: row.update(schema_version=999),
            "boolean_schema": lambda row: row.update(schema_version=True),
            "missing_schema": lambda row: row.pop("schema_version"),
            "wrong_kind": lambda row: row.update(reference_kind="unrelated_reference"),
        }
        for name, mutate in mutations.items():
            with self.subTest(case=name):
                execution, result = self.one_worker(name, mutate=mutate)
                self.assertEqual(execution["status"], "inconclusive_reference")
                self.assertIsNone(result)
                self.assertTrue(execution["errors"])

    def test_worker_source_map_must_exactly_match_recorded_source_identities(self):
        mutations = {
            "unrelated": lambda row: row.update(source_sha256={"unrelated.py": "a" * 64}),
            "missing": lambda row: row["source_sha256"].pop("validate_contact_reference.py"),
            "extra": lambda row: row["source_sha256"].update({"unrelated.py": "a" * 64}),
            "wrong_digest": lambda row: row["source_sha256"].update({"contact_extrema_reference.py": "0" * 64}),
        }
        for name, mutate in mutations.items():
            with self.subTest(case=name):
                execution, result = self.one_worker(name, mutate=mutate)
                self.assertEqual(execution["status"], "inconclusive_reference")
                self.assertIsNone(result)
                self.assertTrue(execution["errors"])

    def test_source_changed_during_worker_prevents_completion(self):
        source = self.directory / "tools" / "validate_contact_reference.py"
        def mutate_source(_):
            source.write_bytes(source.read_bytes() + b"# changed while fake worker ran\n")
        execution, result = self.one_worker("source-changed", mutate=mutate_source)
        self.assertEqual(execution["status"], "inconclusive_reference")
        self.assertIsNone(result)
        self.assertNotEqual(execution["reference_sources_before"][source.name]["sha256"],
                            execution["reference_sources_after"][source.name]["sha256"])

    def test_worker_missing_or_invalid_peak_records_cannot_complete(self):
        mutations = {
            "missing_peaks": lambda row: row.pop("peaks"),
            "missing_damper": lambda row: row["peaks"].pop("damper_force"),
            "nonfinite_force": lambda row: row["peaks"]["hammer_force"].update(value_n=math.nan),
            "negative_force": lambda row: row["peaks"]["hammer_force"].update(value_n=-1.0),
            "wrong_unit": lambda row: row["peaks"]["hammer_force"].update(unit="m"),
            "claimed_coverage": lambda row: row["peaks"]["hammer_force"].update(coverage="certified"),
            "backwards_interval": lambda row: row["peaks"]["hammer_force"].update(maximizer_intervals_s=[[0.01, 0.0]]),
            "missing_maximizer": lambda row: row["peaks"]["hammer_force"].update(maximizing_candidates=[]),
            "invalid_candidate": lambda row: row["peaks"]["hammer_force"]["maximizing_candidates"][0].update(candidate_id=2),
            "active_damper": lambda row: row["peaks"]["damper_force"].update(value_n=1.0),
        }
        for name, mutate in mutations.items():
            with self.subTest(case=name):
                execution, result = self.one_worker(name, mutate=mutate)
                self.assertEqual(execution["status"], "inconclusive_reference")
                self.assertIsNone(result)
                self.assertTrue(execution["errors"])

    def test_worker_matching_raw_digest_does_not_bypass_npy_header_validation(self):
        arrays = tiny_arrays()
        bad = self.replace_member(arrays, "dense_coefficients", npy_bytes(ArrayBytes("<f8", (2**40,), b"")))
        def mutate_raw(row):
            Path(row["raw"]["path"]).write_bytes(bad)
            row["raw"]["sha256"] = sha(bad)
        execution, result = self.one_worker("malformed-npy", mutate=mutate_raw)
        self.assertEqual(execution["status"], "inconclusive_reference")
        self.assertIsNone(result)
        self.assertTrue(execution["errors"])


class ContactExtremaRawFinalProvenanceTests(RawWorkerCase):
    def experiment(self, name, *, target=None, inventory_mutation=False):
        """One mocked parsed input; inspect real worker/filesystem provenance.

        Numerical comparison, collection and scaling are stubbed, so these
        tests claim only artifact-identity handling, never scientific success.
        The actual six worker protocol checks each use a tiny synthetic NPZ.
        """
        output = self.directory / name
        input_path = self.directory / (name + "-candidate.json")
        raw_input = b'{"synthetic_protocol_test":true}\n'
        input_path.write_bytes(raw_input)
        parsed = SimpleNamespace(fixture="supported_impact", settings=(), availability={}, peaks={},
                                 raw_sha256=sha(raw_input), run={"success": True, "failure_code": 0})
        paths = {
            "raw": output / "references/supported_impact-R0/reference.npz",
            "json": output / "references/supported_impact-R0/reference.json",
            "candidate": output / "raw/candidate-0001.raw",
            "missing": output / "references/supported_impact-R0/reference.npz",
        }
        changed = {}
        def change(path, *, remove=False):
            before = path.read_bytes()
            changed.update(path=str(path), expected_sha256=sha(before))
            if remove:
                path.unlink()
                changed["observed_sha256"] = None
            else:
                path.write_bytes(before + b"\n")
                changed["observed_sha256"] = sha(before + b"\n")
        def scaling(*_):
            if target is not None and not inventory_mutation:
                change(paths[target], remove=target == "missing")
            return {"status": "pass"}
        original_identity = driver._identity
        def identity(path):
            if inventory_mutation and str(path) == str(paths["raw"]) and not changed:
                change(Path(path))
            return original_identity(path)
        with ExitStack() as stack:
            process = stack.enter_context(patch.object(driver.subprocess, "Popen", side_effect=self.launcher()))
            stack.enter_context(patch.object(driver, "parse_candidate", return_value=parsed))
            stack.enter_context(patch.object(driver, "refinement_slot_id", return_value="synthetic-test-slot"))
            stack.enter_context(patch.object(driver, "validate_collection", return_value={
                "matrix_complete": False, "duplicate_slots": [], "failed_runs": [], "unavailable_peaks": []}))
            stack.enter_context(patch.object(reference, "compare_refinement", side_effect=lambda rows: {
                "fixture": rows[0]["fixture"], "status": "resolved_under_declared_refinement", "peaks": {}}))
            stack.enter_context(patch.object(driver, "compare_candidate", return_value={
                "fixture": parsed.fixture, "status": "pass", "value_status": "pass", "time_status": "pass",
                "raw_sha256": parsed.raw_sha256, "peaks": {}}))
            stack.enter_context(patch.object(driver, "coordinate_pairs", return_value={"status": "pass"}))
            stack.enter_context(patch.object(driver, "_reference_scaling", side_effect=scaling))
            if inventory_mutation:
                stack.enter_context(patch.object(driver, "_identity", side_effect=identity))
            result = driver.run_experiment([input_path], output)
        self.assertEqual(process.call_count, 6)
        if inventory_mutation:
            changed_workers = [row for row in result["references"] if row["status"] != "completed"]
            self.assertEqual(len(changed_workers), 1)
            self.assertEqual(changed_workers[0]["status"], "inconclusive_reference")
            self.assertTrue(changed_workers[0]["errors"])
        else:
            self.assertTrue(all(row["status"] == "completed" for row in result["references"]),
                            [row["errors"] for row in result["references"]])
        return result, changed, output

    def test_unchanged_artifact_inventory_retains_original_digests(self):
        result, changed, output = self.experiment("unchanged")
        self.assertEqual(changed, {})
        provenance = result["provenance"]
        self.assertEqual(provenance["status"], "pass")
        self.assertTrue(provenance["retained_artifact_identity_stable"])
        self.assertEqual(provenance["changed_or_unavailable_artifacts"], [])
        self.assertTrue(provenance["expected_retained_sha256"])
        for path, digest in provenance["expected_retained_sha256"].items():
            self.assertEqual(sha(Path(path).read_bytes()), digest)
        self.assertEqual(result["phy002_acceptance"], "incomplete")
        self.assertEqual(result["input_status"], "incomplete")
        self.assertTrue((output / "manifest.json").is_file())

    def check_changed_artifact(self, result, changed, output):
        provenance = result["provenance"]
        self.assertEqual(provenance["status"], "fail")
        self.assertFalse(provenance["retained_artifact_identity_stable"])
        self.assertEqual(provenance["expected_retained_sha256"][changed["path"]], changed["expected_sha256"])
        rows = [row for row in provenance["changed_or_unavailable_artifacts"] if row["path"] == changed["path"]]
        self.assertEqual(len(rows), 1)
        self.assertEqual(rows[0]["expected_sha256"], changed["expected_sha256"])
        self.assertEqual(rows[0]["observed_sha256"], changed["observed_sha256"])
        self.assertEqual(result["diagnostic_status"], "fail")
        self.assertNotEqual(result["exit_code"], 0)
        self.assertEqual(result["phy002_acceptance"], "incomplete")
        on_disk = json.loads((output / "report.json").read_bytes())
        self.assertEqual(on_disk["provenance"], provenance)
        manifest = json.loads((output / "manifest.json").read_bytes())
        self.assertEqual(manifest["diagnostic_status"], "fail")
        inventory = [row for row in manifest["artifacts"] if row.get("path") == changed["path"]]
        if changed["observed_sha256"] is not None:
            self.assertEqual(len(inventory), 1)
            self.assertEqual(inventory[0]["sha256"], changed["observed_sha256"])

    def test_final_mutation_or_removal_of_original_raw_json_or_candidate_bytes_fails_provenance(self):
        for target in ("raw", "json", "candidate", "missing"):
            with self.subTest(target=target):
                self.check_changed_artifact(*self.experiment("changed-" + target, target=target))

    def test_worker_inventory_cannot_overwrite_previously_validated_raw_digest(self):
        self.check_changed_artifact(*self.experiment("inventory-change", target="raw", inventory_mutation=True))


if __name__ == "__main__":
    unittest.main()
