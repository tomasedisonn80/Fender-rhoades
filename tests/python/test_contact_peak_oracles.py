"""Focused checks for independent four-fixture peak references and bounded timing."""
from decimal import Decimal, localcontext
from fractions import Fraction
import hashlib
import importlib.util
import json
import math
from pathlib import Path
import subprocess
import sys
import tempfile
import time
from types import ModuleType
import unittest
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[2]
TOOLS = ROOT / "tools"
sys.path.insert(0, str(TOOLS))
import contact_peak_oracles as oracle


def fake_backend(quad):
    scipy = ModuleType("scipy")
    scipy.__version__ = "controlled-test-backend"
    integrate = ModuleType("scipy.integrate")
    integrate.quad = quad
    scipy.integrate = integrate
    return patch.dict(sys.modules, {"scipy": scipy, "scipy.integrate": integrate})


def worker_parameters():
    return {"mu": 0.0016, "k": 20000.0, "c": 40000.0,
            "w0": 1.0, "force_velocity": 0.5}


class ContactPeakOracleTests(unittest.TestCase):
    def test_complete_finite_records_and_independent_definition_hash(self):
        required = {"kind", "unit", "value_interval", "value_status",
                    "maximizer_intervals_s", "time_status", "coverage", "details"}
        for name in oracle.SUPPORTED_FIXTURES:
            with self.subTest(fixture=name):
                result = oracle.reference_peaks(name)
                self.assertEqual(set(result["peaks"]), set(oracle.PEAK_KEYS))
                for record in result["peaks"].values():
                    self.assertEqual(set(record), required)
                self.assertFalse(result["reference_settings"]["damped_time_requested"])
                encoded = json.dumps(result["definition"], sort_keys=True,
                                     separators=(",", ":"), allow_nan=False).encode()
                self.assertEqual(result["definition_sha256"], hashlib.sha256(encoded).hexdigest())
                self.assertEqual(result["definition"], oracle.reference_peaks(name)["definition"])
                json.dumps(result, allow_nan=False)

    def test_conservative_closed_form_with_inactive_second_mode(self):
        result = oracle.reference_peaks("conservative_impact")
        # Independent decimal reduced-mass arithmetic checks the binary64
        # implementation, without sampling a candidate trajectory.
        with localcontext() as context:
            context.prec = 70
            m = Decimal.from_float(0.002)
            mh = Decimal.from_float(0.008)
            maximum = ((m * mh / (m + mh)) / Decimal(20000)).sqrt()
        compression = result["peaks"]["hammer_compression"]
        force = result["peaks"]["hammer_force"]
        self.assertAlmostEqual(compression["value_interval"][0], float(maximum), delta=2e-19)
        self.assertAlmostEqual(force["value_interval"][0], float(maximum * 20000), delta=4e-15)
        expected_time = math.pi * float(maximum) / 2
        self.assertAlmostEqual(compression["maximizer_intervals_s"][0][0], expected_time, delta=2e-19)
        self.assertEqual(compression["maximizer_intervals_s"], force["maximizer_intervals_s"])
        self.assertLess(compression["details"]["separation_time_s"], result["duration_s"])
        self.assertEqual(result["definition"]["bh"], (1.0, 0.0))

    def test_clamp_enabled_zero_force_and_disabled_damper_are_distinct(self):
        result = oracle.reference_peaks("forced_clamp")
        peaks = result["peaks"]
        self.assertEqual(peaks["hammer_compression"]["value_interval"], [0.0001, 0.0001])
        self.assertEqual(peaks["hammer_compression"]["maximizer_intervals_s"], [[0.0, 0.0]])
        self.assertEqual(peaks["hammer_force"]["kind"], "plateau")
        self.assertEqual(peaks["hammer_force"]["time_status"], "analytic")
        self.assertTrue(peaks["hammer_force"]["details"]["enabled"])
        for key in ("hammer_force", "damper_compression", "damper_force"):
            self.assertEqual(peaks[key]["value_interval"], [0.0, 0.0])
            self.assertEqual(peaks[key]["maximizer_intervals_s"], [[0.0, result["duration_s"]]])
        self.assertEqual(peaks["damper_force"]["kind"], "disabled")
        self.assertEqual(peaks["damper_force"]["time_status"], "disabled")

    def test_damped_values_match_independent_high_precision_newton_root(self):
        with localcontext() as context:
            context.prec = 85
            two, one = Decimal(2), Decimal(1)
            invariant = two - Decimal(3).ln()
            y = one
            # Solve directly in y=(c/k)w, using Newton rather than the
            # reference's bracketed velocity bisection.
            for _ in range(20):
                y -= (Decimal("1.5") * y - (1 + y).ln() - invariant) / (
                    Decimal("1.5") - one / (1 + y))
            m, mh = Decimal.from_float(0.002), Decimal.from_float(0.008)
            mu = m * mh / (m + mh)
            scale = two * mu * Decimal(20000) / Decimal(40000) ** 2
            compression = (scale * invariant).sqrt()
            force = (scale * (invariant - y + (1 + y).ln())).sqrt() * Decimal(20000) * (1 + y)
        with patch.object(oracle, "_run_damped_time", side_effect=AssertionError("unexpected backend")):
            result = oracle.reference_peaks("damped_impact")
        comp, force_record = (result["peaks"][key] for key in ("hammer_compression", "hammer_force"))
        self.assertAlmostEqual(comp["value_interval"][0], float(compression), delta=1e-18)
        self.assertAlmostEqual(force_record["value_interval"][0], float(force), delta=2e-13)
        self.assertGreater(force_record["value_interval"][0], 20000 * comp["value_interval"][0])
        refinements = force_record["details"]["root_refinements"]
        self.assertEqual(len(refinements), 3)
        for level, target in zip(refinements, (1e-10, 1e-12, 1e-14)):
            lo, hi = map(Decimal, level["bracket_decimal_m_per_s"])
            residual_lo, residual_hi = map(Decimal, level["residual_decimal"])
            self.assertLessEqual(hi - lo, Decimal(str(target)))
            self.assertLessEqual(level["iterations"], 80)
            self.assertLess(residual_lo, 0)
            self.assertGreater(residual_hi, 0)
            self.assertLess(level["loading_time_upper_bound_s"], result["duration_s"])
        self.assertEqual(comp["time_status"], "not_available")
        self.assertEqual(comp["maximizer_intervals_s"], [])

    def test_preload_energy_and_outward_bounds_use_exact_rounded_definition(self):
        result = oracle.reference_peaks("preload_equilibrium")
        f = oracle.fixtures()["preload_equilibrium"]
        k, b, h = (tuple(map(Fraction, values)) for values in (f.stiffness(), f.bd, f.bh))
        kd, cd, ell = map(Fraction, (f.kd, f.cd, f.face_offset))
        s = sum(bi * bi / ki for bi, ki in zip(b, k))
        delta = -ell / (1 + kd * s)
        qstar = tuple(-bi * kd * delta / ki for bi, ki in zip(b, k))
        error = tuple(Fraction(q) - qs for q, qs in zip(f.q, qstar))
        energy = (sum(ki * ei * ei for ki, ei in zip(k, error))
                  + kd * sum(bi * ei for bi, ei in zip(b, error)) ** 2) / 2
        # Sherman-Morrison gives an independent quadratic-form computation,
        # distinct from the implementation's explicit two-by-two inverse.
        def inverse_form(vector):
            return (sum(zi * zi / ki for zi, ki in zip(vector, k))
                    - kd * sum(zi * bi / ki for zi, bi, ki in zip(vector, b, k)) ** 2 / (1 + kd * s))

        dcap, wcap, hcap, fcap = map(Fraction, ("1e-20", "3e-17", "1.1e-20", "1.3e-16"))
        self.assertGreater(energy, 0)  # Rounded preload is not an exact plateau.
        self.assertLessEqual(2 * energy * inverse_form(b), dcap ** 2)
        self.assertLessEqual(2 * energy * inverse_form(h), hcap ** 2)
        self.assertLessEqual(2 * energy * sum(bi * bi / Fraction(mi) for bi, mi in zip(b, f.mass)), wcap ** 2)
        self.assertLessEqual(kd * dcap + cd * (delta + dcap) * wcap, fcap)
        self.assertGreater(delta - dcap, 0)
        self.assertGreater(kd - cd * wcap, 0)
        self.assertLess(Fraction(f.x) - sum(hi * qi for hi, qi in zip(h, qstar)) + hcap, 0)
        for key, center, cap in (("damper_compression", delta, dcap),
                                 ("damper_force", kd * delta, fcap)):
            record = result["peaks"][key]
            self.assertEqual(record["kind"], "enclosure")
            self.assertEqual(record["value_status"], "verified_enclosure")
            self.assertEqual(record["time_status"], "unresolved_near_equilibrium")
            self.assertEqual(record["maximizer_intervals_s"], [])
            lo, hi = map(Fraction, record["value_interval"])
            self.assertLessEqual(lo, center - cap)
            self.assertGreaterEqual(hi, center + cap)
            self.assertEqual(list(map(Fraction, record["details"]["outward_targets_exact"])),
                             [center - cap, center + cap])
            self.assertEqual(Fraction(record["details"]["exact_rational"]["perturbation_energy_j"]), energy)
            self.assertTrue(all(item["passed"] for item in record["details"]["rational_checks"]))
        for key in ("hammer_compression", "hammer_force"):
            self.assertEqual(result["peaks"][key]["kind"], "plateau")
            self.assertEqual(result["peaks"][key]["maximizer_intervals_s"], [[0.0, f.duration]])

    def test_values_work_without_site_packages_and_worker_reports_missing_scipy(self):
        code = """
import json, sys
sys.path.insert(0, sys.argv[1])
import contact_peak_oracles as o
results = [o.reference_peaks(name) for name in o.SUPPORTED_FIXTURES]
assert 'scipy' not in sys.modules and 'numpy' not in sys.modules
missing = o._time_worker({'mu': .0016, 'k': 20000., 'c': 40000., 'w0': 1., 'force_velocity': .5})
print(json.dumps({'results': results, 'missing': missing}, allow_nan=False))
"""
        process = subprocess.run([sys.executable, "-S", "-c", code, str(TOOLS)],
                                 capture_output=True, text=True, timeout=5)
        self.assertEqual(process.returncode, 0, process.stderr)
        output = json.loads(process.stdout)
        self.assertEqual(len(output["results"]), 4)
        self.assertEqual(output["missing"]["status"], "not_available")
        self.assertIn("SciPy", output["missing"]["reason"])

    def test_missing_requested_backend_preserves_value_references(self):
        completed = subprocess.CompletedProcess([], 0, json.dumps({
            "status": "not_available", "reason": "Optional SciPy backend is unavailable"}), "")
        with patch.object(oracle.subprocess, "run", return_value=completed) as run:
            result = oracle.reference_peaks("damped_impact", damped_time=True)
        self.assertEqual(run.call_args.kwargs["timeout"], 10.0)
        self.assertTrue(result["reference_settings"]["damped_time_requested"])
        for key in ("hammer_compression", "hammer_force"):
            self.assertEqual(result["peaks"][key]["value_status"], "resolved_under_declared_refinement")
            self.assertEqual(result["peaks"][key]["time_status"], "not_available")
            self.assertIsNotNone(result["peaks"][key]["value_interval"])

    @unittest.skipUnless(importlib.util.find_spec("scipy") is not None, "optional SciPy not installed")
    def test_optional_time_refinement_separates_force_and_compression(self):
        result = oracle.reference_peaks("damped_impact", damped_time=True)
        comp, force = (result["peaks"][key] for key in ("hammer_compression", "hammer_force"))
        self.assertEqual(comp["time_status"], "resolved_under_declared_refinement", comp["details"])
        self.assertEqual(force["time_status"], "resolved_under_declared_refinement", force["details"])
        tc = comp["maximizer_intervals_s"][0][0]
        tf = force["maximizer_intervals_s"][0][0]
        self.assertAlmostEqual(tc, 0.00034505973795999833, delta=1e-14)
        self.assertAlmostEqual(tf, 0.0001776905622574621, delta=1e-14)
        self.assertLess(tf, tc)
        timing = comp["details"]["timing"]
        self.assertLessEqual(timing["evaluations"], 10000)
        self.assertEqual(timing["enforced_wall_cap_s"], 10.0)
        self.assertIn("not rigorous certificates", timing["error_interpretation"])
        for levels in timing["refinements"].values():
            self.assertEqual([(level["epsabs"], level["epsrel"]) for level in levels],
                             [(1e-11, 1e-9), (1e-13, 1e-11), (1e-15, 1e-13)])
            self.assertTrue(all(level["limit"] == 128 and not level["convergence_message"] for level in levels))

    def test_active_parent_timeout_terminates_a_blocked_own_worker(self):
        with tempfile.TemporaryDirectory() as directory:
            worker = Path(directory) / "blocked_reference.py"
            worker.write_text("import time\ntime.sleep(5)\n", encoding="utf-8")
            started = time.monotonic()
            with patch.object(oracle, "__file__", str(worker)), patch.object(oracle, "WALL_SECONDS", 0.05):
                result = oracle._run_damped_time(worker_parameters())
            elapsed = time.monotonic() - started
        self.assertLess(elapsed, 2.0)
        self.assertEqual(result["status"], "inconclusive_reference")
        self.assertTrue(result["worker_terminated"])
        self.assertEqual(result["enforced_wall_cap_s"], 0.05)

    def test_evaluation_cap_stops_before_an_extra_integrand_evaluation(self):
        def quad(function, *_args, **_kwargs):
            for _ in range(4):
                function(0.1)
            raise AssertionError("evaluation cap did not stop integration")

        with fake_backend(quad), patch.object(oracle, "MAX_EVALUATIONS", 3):
            result = oracle._time_worker(worker_parameters())
        self.assertEqual(result["status"], "inconclusive_reference")
        self.assertEqual(result["evaluations"], 3)
        self.assertIn("evaluation budget exhausted", result["reason"])

    def test_nonfinite_and_convergence_failures_retain_json_safe_evidence(self):
        for value, error, message in ((math.nan, 0.0, ""), (0.001, math.inf, ""),
                                       (0.001, 0.0, "convergence failed")):
            with self.subTest(value=value, error=error, message=message):
                def quad(_function, *_args, **_kwargs):
                    output = (value, error, {"neval": 0, "last": 1})
                    return output + ((message,) if message else ())

                with fake_backend(quad):
                    result = oracle._time_worker(worker_parameters())
                self.assertEqual(result["status"], "inconclusive_reference")
                self.assertEqual(len(result["refinements"]["hammer_compression"]), 1)
                json.dumps(result, allow_nan=False)

    def test_malformed_worker_success_does_not_destroy_values(self):
        for payload in ({"status": "resolved_under_declared_refinement"},
                        {"status": "resolved_under_declared_refinement", "refinements": []},
                        {"status": "resolved_under_declared_refinement", "refinements": {
                            key: [{"value_s": 0.0}, {"value_s": 0.0}, {"value_s": math.nan}]
                            for key in ("hammer_compression", "hammer_force")}}):
            with self.subTest(payload=payload):
                completed = subprocess.CompletedProcess([], 0, json.dumps(payload), "")
                with patch.object(oracle.subprocess, "run", return_value=completed):
                    result = oracle.reference_peaks("damped_impact", damped_time=True)
                for key in ("hammer_compression", "hammer_force"):
                    self.assertEqual(result["peaks"][key]["value_status"], "resolved_under_declared_refinement")
                    self.assertEqual(result["peaks"][key]["time_status"], "inconclusive_reference")
                json.dumps(result, allow_nan=False)

    def test_out_of_duration_optional_time_preserves_resolved_values(self):
        timing = {"status": "resolved_under_declared_refinement", "refinements": {
            key: [{"value_s": 1.0}] * 3 for key in ("hammer_compression", "hammer_force")}}
        with patch.object(oracle, "_run_damped_time", return_value=timing):
            result = oracle.reference_peaks("damped_impact", damped_time=True)
        for key in ("hammer_compression", "hammer_force"):
            self.assertEqual(result["peaks"][key]["value_status"], "resolved_under_declared_refinement")
            self.assertEqual(result["peaks"][key]["time_status"], "inconclusive_reference")
            self.assertEqual(result["peaks"][key]["maximizer_intervals_s"], [])

    def test_scalar_budget_exhaustion_is_explicit_and_never_passes(self):
        with patch.object(oracle, "ROOT_ITERATIONS", 1):
            result = oracle.reference_peaks("damped_impact")
        record = result["peaks"]["hammer_force"]
        self.assertEqual(record["value_status"], "inconclusive_reference")
        self.assertIsNone(record["value_interval"])
        self.assertIn("iteration budget exhausted", record["details"]["reason"])

    def test_unsupported_fixture_and_nonboolean_option_are_rejected(self):
        with self.assertRaises(ValueError):
            oracle.reference_peaks("moving_face")
        with self.assertRaises(TypeError):
            oracle.reference_peaks("damped_impact", damped_time=1)


if __name__ == "__main__":
    unittest.main()
