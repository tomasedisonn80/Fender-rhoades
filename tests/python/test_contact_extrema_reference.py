"""Independent helper and retention checks for the supported-contact reference.

Constructed cubics and refinement records below are test inputs, not integrated
trajectories or evidence of PHY-002 acceptance. Only the cap tests invoke Radau;
no test generates a complete R0/R1/R2 reference.
"""
from __future__ import annotations

import copy
from dataclasses import replace
import hashlib
import importlib.util
import json
import math
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[2]
TOOLS = ROOT / "tools"
sys.path.insert(0, str(TOOLS))
import contact_extrema_reference as reference


HAS_NUMPY = importlib.util.find_spec("numpy") is not None
HAS_SCIPY = importlib.util.find_spec("scipy") is not None
BASE_ATOL = [1e-16, 1e-16, 1e-13, 1e-13, 1e-16, 1e-13,
             1e-16, 1e-16, 1e-16, 1e-16, 1e-16, 1e-16, 1e-16]
LEVEL_SETTINGS = {
    name: {"maximum_step_s": step, "relative_tolerance": rtol,
           "absolute_vector_multiplier": multiplier,
           "absolute_tolerance_vector": [value * multiplier for value in BASE_ATOL]}
    for name, step, rtol, multiplier in (
        ("R0", 5e-6, 1e-10, 10.0), ("R1", 2.5e-6, 1e-12, 1.0),
        ("R2", 1.25e-6, 1e-13, 0.1))
}
COVERAGE = "uncertified_independent_search"


def refinement_records():
    """Minimal synthetic comparison inputs, with independently stated settings."""
    records = []
    for level in ("R0", "R1", "R2"):
        peaks = {}
        for key, field in (("hammer_compression", "value_m"), ("hammer_force", "value_n"),
                           ("damper_compression", "value_m"), ("damper_force", "value_n")):
            disabled = key.startswith("damper")
            peaks[key] = {field: 0.0, "classification": "disabled" if disabled else "stationary",
                          "maximizer_intervals_s": [[0.0, 0.020]] if disabled else [[0.004, 0.004]]}
        records.append({
            "fixture": "supported_impact", "fixture_sha256": "f" * 64,
            "source_sha256": {"synthetic-test-source": "a" * 64}, "level": level,
            "settings": copy.deepcopy(LEVEL_SETTINGS[level]), "status": "completed",
            "complete_duration": True, "peaks": peaks, "unresolved": [],
            "branches": [{"branch": name} for name in
                         ("separated", "positive_compression", "separated")],
            "resolved_peak_clusters": [{"time_s": t, "crossing": "positive_to_negative"}
                                       for t in (0.004, 0.011)],
            "search_statistics": {"maximum_physical_dense_derivative_discrepancy_m_per_s": 1e-9},
            "counts": {"accepted_steps": 0},
        })
    return records


def retained_cubics(coefficients, boundaries=(0.0, 0.004), fixture="supported_impact"):
    """Build a hash-bound test artifact without any production serialization helper."""
    import numpy as np

    coefficients = np.asarray(coefficients, dtype="<f8")
    mesh_y = [coefficients[0, :, 0]]
    # Evaluation at u=1 is independently just the coefficient sum.
    mesh_y.extend(np.sum(segment, axis=1) for segment in coefficients)
    arrays = {
        "mesh_t_s": np.asarray(boundaries, dtype="<f8"),
        "mesh_y": np.asarray(mesh_y, dtype="<f8"),
        "dense_t0_s": np.asarray(boundaries[:-1], dtype="<f8"),
        "dense_t1_s": np.asarray(boundaries[1:], dtype="<f8"),
        "dense_coefficients": coefficients,
        "reconstruction_residual": np.zeros((len(coefficients), 13), dtype="<f8"),
        "reconstruction_limit": np.full((len(coefficients), 13), 1e-14, dtype="<f8"),
        "candidate_time_s": np.asarray(boundaries, dtype="<f8"),
        "candidate_y": np.asarray(mesh_y, dtype="<f8"),
        "candidate_segment_id": np.asarray([-1, *range(len(coefficients))], dtype="<i8"),
        "candidate_origin_bits": np.ones(len(boundaries), dtype="<u8"),
    }
    manifest = {name: {"shape": list(array.shape), "dtype": array.dtype.str,
                       "sha256": hashlib.sha256(array.tobytes(order="C")).hexdigest()}
                for name, array in arrays.items()}
    return {"fixture": fixture, "raw": {"arrays": manifest},
            "raw_arrays": {name: array.tolist() for name, array in arrays.items()}}, arrays


def diagnostic_coefficients():
    """Positive gap: .001+.003u-.002u²; physical speed: -.4+.8u."""
    import numpy as np

    coefficients = np.zeros((1, 13, 4))
    coefficients[0, 0, 0], coefficients[0, 1, 0] = 1e-5, -2e-5
    coefficients[0, 2, 0], coefficients[0, 3, 0] = 0.2, -0.4
    coefficients[0, 4] = [0.001003, 0.003, -0.002, 0.0]
    coefficients[0, 5, :2] = [-0.34, 0.8]
    return coefficients


class ContactExtremaStdlibTests(unittest.TestCase):
    def test_import_and_read_only_plan_do_not_import_numerical_backends(self):
        script = """
import builtins, json, sys
sys.path.insert(0, sys.argv[1])
original = builtins.__import__
def guarded(name, *args, **kwargs):
    if name.split('.')[0] in {'numpy', 'scipy'}:
        raise AssertionError('Numerical import during stdlib declaration: ' + name)
    return original(name, *args, **kwargs)
builtins.__import__ = guarded
import contact_extrema_reference
import validate_contact_extrema
raise SystemExit(validate_contact_extrema.main(['--plan']))
"""
        with tempfile.TemporaryDirectory() as directory:
            process = subprocess.run([sys.executable, "-S", "-c", script, str(TOOLS)],
                                     cwd=directory, capture_output=True, text=True, timeout=10)
            self.assertEqual(process.returncode, 0, process.stderr)
            self.assertEqual(process.stderr, "")
            self.assertEqual(list(Path(directory).iterdir()), [])
        declared = json.loads(process.stdout)
        self.assertEqual(declared["expected_candidate_count"], 26)
        self.assertEqual(declared["reference_count"], 6)
        self.assertEqual(declared["coverage"], COVERAGE)
        self.assertEqual(declared["phy002_acceptance"], "incomplete")
        self.assertEqual({(row["fixture"], row["level"]) for row in declared["references"]},
                         {(name, level) for name in ("supported_impact", "supported_impact_scaled")
                          for level in ("R0", "R1", "R2")})
        for row in declared["references"]:
            self.assertEqual(row["settings"], LEVEL_SETTINGS[row["level"]])

    def test_settings_and_value_budgets_keep_the_declared_constants(self):
        self.assertEqual(reference.SUPPORTED_FIXTURES,
                         ("supported_impact", "supported_impact_scaled"))
        for level, settings in LEVEL_SETTINGS.items():
            self.assertEqual(reference.LEVELS[level].settings(), settings)
        for value in (0.0, 0.0003, -0.0003, 7.0):
            for key in ("hammer_compression", "damper_compression"):
                self.assertEqual(reference.value_budget(key, value), 1e-8 + 1e-4 * abs(value))
            for key in ("hammer_force", "damper_force"):
                self.assertEqual(reference.value_budget(key, value), 1e-4 + 2e-4 * abs(value))

    def test_unsupported_or_modified_fixtures_and_levels_fail_before_backend(self):
        frozen = reference.Fixture("supported_impact")
        invalid = ["conservative_impact", "supported_conservative", "unknown",
                   replace(frozen, kh=19999.0), replace(frozen, duration=0.019),
                   replace(frozen, ch=1.0), replace(frozen, q=(1e-12, 0.0))]
        with patch.object(reference, "_numpy", side_effect=AssertionError("backend reached")):
            for fixture in invalid:
                with self.subTest(fixture=fixture), self.assertRaises(ValueError):
                    reference.generate_reference(fixture, "R0")
            with self.assertRaisesRegex(ValueError, "Unknown reference level"):
                reference.generate_reference(frozen, "R3")

    def test_invalid_or_raised_caps_are_rejected_before_backend(self):
        cases = [{"accepted_steps": -1}, {"accepted_steps": True}, {"accepted_steps": 1.0},
                 {"accepted_steps": 200001}, {"rhs_evaluations": 5000001},
                 {"search_evaluations": 1000001}, {"candidates": 100001},
                 {"wall_seconds": 301}, {"wall_seconds": 0}, {"wall_seconds": math.inf},
                 {"wall_seconds": math.nan}, {"unknown": 1}, []]
        with patch.object(reference, "_numpy", side_effect=AssertionError("backend reached")):
            for limits in cases:
                with self.subTest(limits=limits), self.assertRaises(ValueError):
                    reference.generate_reference("supported_impact", limits=limits)

    def test_existing_raw_artifact_is_preserved_before_backend(self):
        with tempfile.TemporaryDirectory() as directory:
            destination = Path(directory) / "existing.npz"
            destination.write_bytes(b"preserve original bytes")
            with patch.object(reference, "_numpy", side_effect=AssertionError("backend reached")):
                with self.assertRaises(FileExistsError):
                    reference.generate_reference("supported_impact", raw_path=destination)
            self.assertEqual(destination.read_bytes(), b"preserve original bytes")


class ContactExtremaBisectionTests(unittest.TestCase):
    def test_bracket_retains_independent_root_residuals_and_width(self):
        result = reference.retained_bisection(lambda t: t * t - 2.0, 1.0, 2.0)
        lo, hi = result["bracket_lo_s"], result["bracket_hi_s"]
        self.assertLessEqual(lo, math.sqrt(2))
        self.assertGreaterEqual(hi, math.sqrt(2))
        self.assertLessEqual(hi - lo, 1e-12)
        self.assertEqual(result["bracket_residuals"], [lo * lo - 2.0, hi * hi - 2.0])
        self.assertLessEqual(result["bracket_residuals"][0], 0.0)
        self.assertGreaterEqual(result["bracket_residuals"][1], 0.0)
        self.assertLessEqual(result["iterations"], 40)
        self.assertEqual(result["initial_signs"], [-1, 1])
        self.assertEqual(result["status"], "resolved_numerical")

    def test_endpoint_zero_retains_exact_location_without_iteration(self):
        result = reference.retained_bisection(lambda t: t - 0.25, 0.25, 1.0, iterations=0)
        self.assertEqual((result["bracket_lo_s"], result["bracket_hi_s"]), (0.25, 0.25))
        self.assertEqual(result["iterations"], 0)
        self.assertEqual(result["isolation"], "endpoint_zero")

    def test_iteration_limit_stops_after_only_endpoint_evaluations(self):
        visited = []
        def function(t):
            visited.append(t)
            return t * t - 2.0
        with self.assertRaisesRegex(reference.ReferenceFailure, "iteration budget exhausted"):
            reference.retained_bisection(function, 1.0, 2.0, iterations=0)
        self.assertEqual(visited, [1.0, 2.0])

    def test_unrepresentable_midpoint_is_an_explicit_failure(self):
        lo = float(2 ** 53)
        hi = math.nextafter(lo, math.inf)
        self.assertEqual(hi - lo, 2.0)
        with self.assertRaisesRegex(reference.ReferenceFailure, "midpoint is not representable"):
            reference.retained_bisection(lambda t: -1.0 if t == lo else 1.0,
                                         lo, hi, width_s=0.5)

    def test_nonbrackets_nonfinite_values_and_excessive_iterations_are_rejected(self):
        with self.assertRaisesRegex(reference.ReferenceFailure, "strictly bracket"):
            reference.retained_bisection(lambda t: (t - 0.5) ** 2, 0.0, 1.0)
        with self.assertRaisesRegex(reference.ReferenceFailure, "Nonfinite"):
            reference.retained_bisection(lambda t: math.nan, 0.0, 1.0)
        for iterations in (-1, 81, True):
            with self.subTest(iterations=iterations), self.assertRaises(ValueError):
                reference.retained_bisection(lambda t: t - 0.5, 0.0, 1.0, iterations=iterations)

    def test_search_budget_stops_before_next_function_call(self):
        budget = reference.WorkBudget({"search_evaluations": 2})
        visited = []
        def function(t):
            visited.append(t)
            return t * t - 2.0
        with self.assertRaisesRegex(reference.BudgetExceeded, "search_evaluations"):
            reference.retained_bisection(function, 1.0, 2.0, budget=budget)
        self.assertEqual(visited, [1.0, 2.0])
        self.assertEqual(budget.counts["search_evaluations"], 2)

    def test_all_counters_and_exact_wall_deadline_stop_before_extra_work(self):
        for name in ("accepted_steps", "rhs_evaluations", "search_evaluations", "candidates"):
            with self.subTest(counter=name):
                budget = reference.WorkBudget({name: 1})
                budget.charge(name)
                with self.assertRaisesRegex(reference.BudgetExceeded, name):
                    budget.charge(name)
                self.assertEqual(budget.counts[name], 1)
        with patch.object(reference.time, "monotonic", return_value=100.0) as clock:
            budget = reference.WorkBudget({"wall_seconds": 0.25})
            clock.return_value = 100.25
            with self.assertRaisesRegex(reference.BudgetExceeded, "wall_seconds"):
                budget.charge("candidates")
            self.assertEqual(budget.counts["candidates"], 0)


@unittest.skipUnless(HAS_NUMPY, "optional NumPy not installed")
class ContactExtremaPolynomialTests(unittest.TestCase):
    def check_reconstruction(self, t0, t1):
        import numpy as np

        expected = np.array([[0.125, -0.75, 2.0, -0.5], [1.5, 0.0, 0.0, 0.0],
                             [-3e-7, 2e-7, -1e-7, 4e-7]])
        calls = []
        class PublicOnly:
            def __call__(self, t):
                calls.append(t)
                u = (t - t0) / (t1 - t0)
                return np.array([0.125 - 0.75*u + 2*u**2 - 0.5*u**3,
                                 1.5, -3e-7 + 2e-7*u - 1e-7*u**2 + 4e-7*u**3])
            def __getattr__(self, name):
                raise AssertionError("Only public calls are allowed: " + name)
        segment = reference.reconstruct_cubic(PublicOnly(), t0, t1)
        scale = np.max(np.abs(expected), axis=1)
        self.assertTrue(np.all(np.max(np.abs(segment.coefficients - expected), axis=1) <= 2e-13*scale))
        self.assertEqual(len(calls), 7)
        self.assertTrue(all(t0 <= t <= t1 for t in calls))
        for fraction in (0.0, 0.13, 0.63, 1.0):
            t = t0 + fraction*(t1 - t0)
            u = (t - t0) / (t1 - t0)
            expected_value = np.sum(expected * np.array([1, u, u*u, u*u*u]), axis=1)
            np.testing.assert_allclose(segment.state(t), expected_value, rtol=2e-13, atol=1e-20)
        self.assertTrue(np.all(segment.residual_by_component <= segment.validation_limit_by_component))
        with self.assertRaisesRegex(ValueError, "extrapolate"):
            segment.state(math.nextafter(t0, -math.inf))

    def test_vector_cubic_reconstructs_known_coefficients_from_public_calls(self):
        self.check_reconstruction(2.0, 5.0)

    def test_tiny_interval_uses_actual_representable_abscissae(self):
        t0 = 0.004
        t1 = t0 + 7*math.ulp(t0)
        self.assertNotEqual(((t0 + (t1-t0)/3) - t0)/(t1-t0), 1/3)
        self.check_reconstruction(t0, t1)

    def test_noncubic_nonvector_and_collapsed_sample_times_are_rejected(self):
        import numpy as np

        for evaluator in (lambda t: np.array([t**4]), lambda t: np.array(t),
                          lambda t: np.array([math.nan])):
            with self.subTest(evaluator=evaluator), self.assertRaises(reference.ReferenceFailure):
                reference.reconstruct_cubic(evaluator, 0.0, 1.0)
        with self.assertRaisesRegex(reference.ReferenceFailure, "sample times are not representable"):
            reference.reconstruct_cubic(lambda t: np.array([t]), 1.0, math.nextafter(1.0, math.inf))

    def test_close_roots_hidden_from_quarter_scan_keep_separate_brackets(self):
        a, b = 25/64, 25/64 + 2**-12
        # Exact dyadic coefficients of (u-a)(u-b), derived without polyfromroots.
        result = reference.isolate_polynomial_roots([a*b, -(a+b), 1.0])
        self.assertEqual(result["quarter_scan_sign_changes"], 0)
        self.assertEqual(result["polynomial_roots_without_scan_sign_change"], 2)
        self.assertEqual(result["unresolved"], [])
        self.assertEqual(len(result["roots"]), 2)
        for root, expected in zip(result["roots"], (a, b)):
            self.assertEqual(root["status"], "resolved_numerical")
            self.assertLessEqual(root["bracket_lo_s"], expected)
            self.assertGreaterEqual(root["bracket_hi_s"], expected)
            self.assertLessEqual(root["bracket_hi_s"] - root["bracket_lo_s"], 1e-12)
            self.assertIn("polynomial_cell_bisection", root["methods"])
        self.assertLess(result["roots"][0]["bracket_hi_s"], result["roots"][1]["bracket_lo_s"])
        self.assertEqual([root["crossing"] for root in result["roots"]],
                         ["positive_to_negative", "negative_to_positive"])

    def test_even_and_triple_roots_keep_conditioning_or_tangency_unresolved(self):
        # (u-1/4)^2 and (u-1/4)^3; an odd multiple root can have a sign bracket.
        for coefficients, even in (([1/16, -1/2, 1.0], True),
                                   ([-1/64, 3/16, -3/4, 1.0], False)):
            with self.subTest(even=even):
                result = reference.isolate_polynomial_roots(coefficients)
                self.assertFalse(result["plateau"])
                self.assertTrue(result["roots"])
                reasons = {item["reason"] for item in result["unresolved"]}
                self.assertIn("ill_conditioned_or_multiple_root", reasons)
                self.assertTrue(any(abs(root["time_s"] - 0.25) < 1e-7 for root in result["roots"]))
                if even:
                    self.assertIn("unbracketed_root_or_tangency", reasons)
                    self.assertTrue(all(root["status"] == "unresolved" for root in result["roots"]))

    def test_zero_plateau_near_flat_nonzero_and_endpoint_labels_are_distinct(self):
        zero = reference.isolate_polynomial_roots([0.0, 0.0, 0.0], 2.0, 6.0,
                                                  interval_u=(0.25, 0.75))
        self.assertTrue(zero["plateau"])
        self.assertEqual(zero["plateau_interval_s"], [3.0, 5.0])
        self.assertEqual(zero["plateau_basis"], "validated_polynomial_identity_only")
        self.assertEqual(zero["roots"], [])
        for constant in (1e-20, -1e-20):
            flat = reference.isolate_polynomial_roots([constant], flat_threshold=1e-18)
            self.assertFalse(flat["plateau"])
            self.assertEqual(flat["roots"], [])
            self.assertIn("numerically_flat_unresolved", {item["reason"] for item in flat["unresolved"]})
        endpoints = reference.isolate_polynomial_roots([0.0, -1.0, 1.0], 2.0, 6.0)
        self.assertEqual([root["time_s"] for root in endpoints["roots"]], [2.0, 6.0])
        self.assertTrue(all(root["isolation"] == "endpoint_zero" for root in endpoints["roots"]))

    def test_normalized_derivative_includes_physical_time_scale(self):
        import numpy as np

        derivative = reference.polynomial_derivative([[1.0, -2.0, 3.0, -4.0], [7.0, 0.0, 0.0, 0.0]],
                                                       time_scale_s=0.25)
        np.testing.assert_array_equal(derivative, [[-8.0, 24.0, -48.0], [0.0, 0.0, 0.0]])
        self.assertEqual(float(reference.evaluate_polynomial(derivative[0], 0.5)), -8.0)


@unittest.skipUnless(HAS_NUMPY, "optional NumPy not installed")
class ContactExtremaRetainedEvaluationTests(unittest.TestCase):
    def test_physical_velocity_and_dense_derivative_have_distinct_stationary_times(self):
        retained, _ = retained_cubics(diagnostic_coefficients())
        for u in (0.25, 0.5, 0.75):
            with self.subTest(u=u):
                result = reference.evaluate_reference(retained, 0.004*u)
                gap, physical, dense = 0.001 + 0.003*u - 0.002*u*u, -0.4 + 0.8*u, (0.003 - 0.004*u)/0.004
                self.assertAlmostEqual(result["hammer_compression_m"], gap, delta=1e-18)
                self.assertAlmostEqual(result["hammer_force_n"], 20000*gap, delta=3e-14)
                self.assertAlmostEqual(result["physical_rhs_derivative_m_per_s"], physical, delta=2e-16)
                self.assertAlmostEqual(result["dense_derivative_m_per_s"], dense, delta=2e-16)
                self.assertAlmostEqual(result["physical_force_derivative_n_per_s"], 20000*physical, delta=5e-12)
                self.assertAlmostEqual(result["physical_dense_derivative_discrepancy_m_per_s"],
                                       abs(physical-dense), delta=3e-16)
                self.assertEqual(result["branch"], "positive_compression")
                self.assertGreater(result["state"]["damper_gap_m"], 0.0)
                self.assertEqual((result["damper_compression_m"], result["damper_force_n"]), (0.0, 0.0))
                self.assertEqual(result["coverage"], COVERAGE)
        # Analytically, physical speed vanishes at u=1/2 and dense slope at u=3/4.
        physical_root = reference.isolate_polynomial_roots([-0.4, 0.8], 0.0, 0.004)["roots"][0]
        dense_root = reference.isolate_polynomial_roots([0.75, -1.0], 0.0, 0.004)["roots"][0]
        self.assertAlmostEqual(physical_root["time_s"], 0.002, delta=1e-12)
        self.assertAlmostEqual(dense_root["time_s"], 0.003, delta=1e-12)

    def test_signed_coordinate_transform_preserves_physical_observables(self):
        coefficients = diagnostic_coefficients()
        transformed = coefficients.copy()
        transformed[:, 0] *= 2.0
        transformed[:, 2] *= 2.0
        transformed[:, 1] *= -3.0
        transformed[:, 3] *= -3.0
        base, _ = retained_cubics(coefficients)
        scaled, _ = retained_cubics(transformed, fixture="supported_impact_scaled")
        for t in (0.0, 0.001, 0.003, 0.004):
            left = reference.evaluate_reference(base, t)
            right = reference.evaluate_reference(scaled, t)
            for key in ("hammer_compression_m", "hammer_force_n", "damper_compression_m",
                        "damper_force_n", "physical_rhs_derivative_m_per_s", "dense_derivative_m_per_s"):
                self.assertAlmostEqual(left[key], right[key], delta=2e-14)
            for key in ("energy_j", "pickup_voltage_v", "hammer_acceleration_m_per_s2"):
                self.assertAlmostEqual(left["state"][key], right["state"][key], delta=2e-14)
            for key in ("q_m", "v_m_per_s", "modal_acceleration_m_per_s2"):
                for index, scale in enumerate((2.0, -3.0)):
                    self.assertAlmostEqual(left["state"][key][index], right["state"][key][index]/scale,
                                           delta=1e-11)

    def test_exact_mesh_endpoint_side_selection_and_no_extrapolation(self):
        import numpy as np

        coefficients = np.zeros((2, 13, 4))
        coefficients[:, 4, 0] = [-0.001, 0.002]
        coefficients[:, 5, 0] = [2.0, 3.0]
        retained, _ = retained_cubics(coefficients, (0.0, 0.010, 0.020))
        left = reference.evaluate_reference(retained, 0.010, side="left")
        right = reference.evaluate_reference(retained, 0.010, side="right")
        self.assertEqual((left["segment_id"], right["segment_id"]), (0, 1))
        self.assertEqual((left["branch"], right["branch"]), ("separated", "positive_compression"))
        self.assertEqual((left["hammer_force_n"], right["hammer_force_n"]), (0.0, 40.0))
        self.assertEqual((left["physical_force_derivative_n_per_s"], right["physical_force_derivative_n_per_s"]),
                         (0.0, 60000.0))
        self.assertEqual(reference.evaluate_reference(retained, 0.0)["segment_id"], 0)
        self.assertEqual(reference.evaluate_reference(retained, 0.020, side="left")["segment_id"], 1)
        for time in (-1e-12, 0.0200001, math.nan, math.inf, True):
            with self.subTest(time=time), self.assertRaises(ValueError):
                reference.evaluate_reference(retained, time)
        with self.assertRaisesRegex(ValueError, "side must be"):
            reference.evaluate_reference(retained, 0.010, side="nearest")
        partial, _ = retained_cubics(diagnostic_coefficients())
        with self.assertRaisesRegex(ValueError, "no complete dense coverage"):
            reference.evaluate_reference(partial, 0.005)

    def test_inline_array_mutation_is_rejected_by_independent_manifest(self):
        retained, _ = retained_cubics(diagnostic_coefficients())
        retained["raw_arrays"]["dense_coefficients"][0][4][0] += 1e-6
        with self.assertRaisesRegex(ValueError, "per-array hash/shape/dtype mismatch"):
            reference.evaluate_reference(retained, 0.002)

    def test_npz_byte_hash_rejects_trailing_bytes(self):
        import numpy as np

        retained, arrays = retained_cubics(diagnostic_coefficients())
        del retained["raw_arrays"]
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "retained.npz"
            np.savez_compressed(path, **arrays)
            retained["raw"].update(path=str(path), sha256=hashlib.sha256(path.read_bytes()).hexdigest())
            self.assertAlmostEqual(reference.evaluate_reference(retained, 0.002)["hammer_force_n"], 40.0)
            with path.open("ab") as stream:
                stream.write(b"changed bytes after valid ZIP archive")
            with self.assertRaisesRegex(ValueError, "byte hash mismatch"):
                reference.evaluate_reference(retained, 0.002)

    def test_npz_array_hash_shape_and_dtype_survive_recomputed_file_hash(self):
        import numpy as np

        for mutation in ("value", "shape", "dtype", "missing"):
            with self.subTest(mutation=mutation), tempfile.TemporaryDirectory() as directory:
                retained, arrays = retained_cubics(diagnostic_coefficients())
                del retained["raw_arrays"]
                name = "dense_coefficients"
                if mutation == "value":
                    arrays[name][0, 4, 0] += 1e-6
                elif mutation == "shape":
                    arrays[name] = arrays[name].reshape((13, 4))
                elif mutation == "dtype":
                    arrays[name] = arrays[name].view("<u8")
                else:
                    del arrays[name]
                path = Path(directory) / "retained.npz"
                np.savez_compressed(path, **arrays)
                retained["raw"].update(path=str(path), sha256=hashlib.sha256(path.read_bytes()).hexdigest())
                with self.assertRaisesRegex(ValueError, "per-array hash/shape/dtype mismatch"):
                    reference.evaluate_reference(retained, 0.002)


class ContactExtremaRefinementTests(unittest.TestCase):
    def test_exact_tenth_budget_is_inclusive_for_each_successive_pair(self):
        for key, field, budget in (("hammer_compression", "value_m", 1e-8),
                                  ("hammer_force", "value_n", 1e-4)):
            threshold = 0.1*budget  # R2 is exactly zero: no subtractive rounding.
            for difference, resolved in ((math.nextafter(threshold, 0.0), True),
                                          (threshold, True),
                                          (math.nextafter(threshold, math.inf), False)):
                for pair in (0, 1):
                    with self.subTest(key=key, difference=difference, pair=pair):
                        records = refinement_records()
                        for index in range(pair + 1):
                            records[index]["peaks"][key][field] = difference
                        result = reference.compare_refinement(list(reversed(records)))
                        peak = result["peaks"][key]
                        self.assertEqual(peak["required_maximum_difference"], threshold)
                        self.assertEqual(peak["successive_absolute_differences"][pair], difference)
                        self.assertEqual(peak["value_status"], "resolved_under_declared_refinement"
                                         if resolved else "inconclusive_reference")
                        self.assertEqual(peak["time_status"], "resolved_under_declared_refinement")
                        self.assertEqual(result["coverage"], COVERAGE)

    def test_budget_is_based_on_R2_and_preserves_both_value_differences(self):
        records = refinement_records()
        for record, value in zip(records, (0.0005, 0.0004, 0.0003)):
            record["peaks"]["hammer_compression"]["value_m"] = value
        result = reference.compare_refinement(records)
        peak = result["peaks"]["hammer_compression"]
        self.assertEqual(peak["budget_at_R2"], 1e-8 + 1e-4*0.0003)
        self.assertEqual(peak["successive_absolute_differences"], [abs(0.0004-0.0005), abs(0.0003-0.0004)])
        self.assertEqual(peak["value_status"], "inconclusive_reference")
        self.assertEqual(peak["time_status"], "resolved_under_declared_refinement")

    def test_lost_or_ambiguous_physical_cluster_blocks_value_refinement(self):
        for mode in ("lost", "ambiguous"):
            with self.subTest(mode=mode):
                records = refinement_records()
                if mode == "lost":
                    records[1]["resolved_peak_clusters"].pop()
                else:
                    for record in records:
                        record["resolved_peak_clusters"][1]["time_s"] = 0.00400001
                result = reference.compare_refinement(records)
                self.assertEqual(result["status"], "inconclusive_reference")
                self.assertTrue(all(not row["stable"] for row in result["peak_cluster_refinements"]))
                self.assertTrue(any("lost or ambiguous physical peak cluster" in issue for issue in result["issues"]))
                self.assertEqual(result["peaks"]["hammer_force"]["value_status"], "inconclusive_reference")
                self.assertEqual(result["peaks"]["damper_force"]["value_status"], "analytic_disabled_identity")

    def test_branch_classification_unresolved_and_incomplete_results_block_values(self):
        for mode in ("branch", "classification", "unresolved", "incomplete"):
            with self.subTest(mode=mode):
                records = refinement_records()
                if mode == "branch":
                    records[0]["branches"].pop()
                elif mode == "classification":
                    records[1]["peaks"]["hammer_force"]["classification"] = "endpoint"
                elif mode == "unresolved":
                    records[1]["unresolved"].append({"reason": "ill_conditioned_or_multiple_root"})
                else:
                    records[0].update(status="inconclusive_reference", complete_duration=False)
                result = reference.compare_refinement(records)
                self.assertEqual(result["status"], "inconclusive_reference")
                self.assertEqual(result["peaks"]["hammer_force"]["value_status"], "inconclusive_reference")
                self.assertEqual(result["peaks"]["damper_compression"]["time_status"], "disabled")

    def test_missing_levels_identity_drift_and_changed_settings_are_rejected(self):
        for mode in ("missing", "duplicate", "fixture", "definition", "source", "settings"):
            with self.subTest(mode=mode):
                records = refinement_records()
                if mode == "missing":
                    records.pop()
                elif mode == "duplicate":
                    records[0]["level"] = "R1"
                elif mode == "fixture":
                    records[0]["fixture"] = "supported_impact_scaled"
                elif mode == "definition":
                    records[0]["fixture_sha256"] = "b" * 64
                elif mode == "source":
                    records[0]["source_sha256"]["synthetic-test-source"] = "b" * 64
                else:
                    records[0]["settings"]["absolute_tolerance_vector"][1] *= 2.0
                with self.assertRaises(ValueError):
                    reference.compare_refinement(records)


@unittest.skipUnless(HAS_NUMPY and HAS_SCIPY, "optional NumPy/SciPy not installed")
class ContactExtremaLiveCapTests(unittest.TestCase):
    def test_one_accepted_step_retains_partial_arrays_checkpoints_and_explicit_failure(self):
        import numpy as np

        with tempfile.TemporaryDirectory() as directory:
            raw = Path(directory) / "one-step.npz"
            checkpoint = Path(directory) / "one-step.partial.npz"
            result = reference.generate_reference("supported_impact", "R0", limits={"accepted_steps": 1},
                                                  raw_path=raw, checkpoint_path=checkpoint)
            self.assertEqual(result["status"], "inconclusive_reference")
            self.assertEqual(result["failure"]["type"], "BudgetExceeded")
            self.assertIn("accepted_steps", result["failure"]["reason"])
            self.assertEqual(result["counts"]["accepted_steps"], 1)
            self.assertGreater(result["counts"]["rhs_evaluations"], result["solver"]["nfev"])
            self.assertFalse(result["complete_duration"])
            self.assertGreater(result["final_time_s"], 0.0)
            self.assertLessEqual(result["final_time_s"], 5e-6)
            self.assertEqual(result["settings"], LEVEL_SETTINGS["R0"])
            self.assertEqual(result["raw"]["sha256"], hashlib.sha256(raw.read_bytes()).hexdigest())
            for name, digest in result["source_sha256"].items():
                self.assertEqual(digest, hashlib.sha256((TOOLS / name).read_bytes()).hexdigest())
            with np.load(raw, allow_pickle=False) as arrays:
                self.assertEqual(arrays["mesh_y"].shape, (2, 13))
                self.assertEqual(arrays["dense_coefficients"].shape, (1, 13, 4))
                np.testing.assert_array_equal(arrays["mesh_y"][0],
                                              [0.0, 0.0, 0.0, 0.0, -0.0002, 1.0, 0.0, 0.0,
                                               0.0, 0.0, 0.0, 0.0, 0.0])
                for name in arrays.files:
                    array = arrays[name]
                    self.assertEqual(result["raw"]["arrays"][name], {
                        "shape": list(array.shape), "dtype": array.dtype.str,
                        "sha256": hashlib.sha256(array.tobytes(order="C")).hexdigest()})
            retained = reference.evaluate_reference(result, result["final_time_s"]/2)
            self.assertEqual(retained["hammer_force_n"], 0.0)
            self.assertEqual(retained["branch"], "separated")
            for key in ("hammer_compression", "hammer_force"):
                self.assertEqual(result["peaks"][key]["value_status"], "inconclusive_reference")
            for key in ("damper_compression", "damper_force"):
                self.assertEqual(result["peaks"][key]["maximizer_intervals_s"], [[0.0, 0.020]])
                self.assertEqual(result["peaks"][key]["value_status"], "analytic_disabled_identity")
            progress = json.loads(checkpoint.with_suffix(".json").read_text(encoding="utf-8"))
            self.assertEqual(progress["status"], "inconclusive_reference")
            self.assertEqual(progress["counts"]["accepted_steps"], 1)
            self.assertEqual(progress["checkpoint"]["sha256"], hashlib.sha256(checkpoint.read_bytes()).hexdigest())
            with np.load(checkpoint, allow_pickle=False) as arrays:
                embedded = json.loads(arrays["checkpoint_metadata_utf8"].tobytes())
                self.assertEqual(embedded["mesh_final_time_s"], result["final_time_s"])
                self.assertEqual(embedded["arrays"], result["raw"]["arrays"])

    def test_rhs_search_and_candidate_caps_stop_at_one_and_keep_initial_mesh(self):
        for counter in ("rhs_evaluations", "search_evaluations", "candidates"):
            with self.subTest(counter=counter):
                result = reference.generate_reference("supported_impact_scaled", "R1", limits={counter: 1})
                self.assertEqual(result["failure"]["type"], "BudgetExceeded")
                self.assertIn(counter, result["failure"]["reason"])
                self.assertEqual(result["counts"][counter], 1)
                self.assertLessEqual(result["counts"]["accepted_steps"], 1)
                self.assertEqual(result["raw_arrays"]["mesh_t_s"][0], 0.0)
                self.assertEqual(len(result["raw_arrays"]["mesh_y"][0]), 13)
                self.assertEqual(result["status"], "inconclusive_reference")
                self.assertFalse(result["complete_duration"])
                self.assertEqual(result["settings"], LEVEL_SETTINGS["R1"])
                self.assertEqual(result["coverage"], COVERAGE)
                json.dumps(result, allow_nan=False)


if __name__ == "__main__":
    unittest.main()
