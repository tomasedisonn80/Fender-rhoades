"""Structural algebra checks; these do not measure rendered aliasing error."""
from __future__ import annotations

from contextlib import redirect_stderr, redirect_stdout
import copy
import hashlib
import importlib.util
import io
import json
import math
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "rhodes_aliasing_test_target", ROOT / "tools/audit_aliasing.py")
if SPEC is None or SPEC.loader is None:
    raise RuntimeError("Cannot load aliasing auditor")
audit = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(audit)
SOURCE_PATHS = ("src/research_engine.cpp", "include/rhodes/research_engine.hpp")


class AliasingAuditTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        # A stable local snapshot also avoids mixing concurrent source edits.
        cls.directory = tempfile.TemporaryDirectory(prefix="rhodes alias audit ")
        cls.addClassCleanup(cls.directory.cleanup)
        cls.snapshot = Path(cls.directory.name)
        cls.blobs = {path: (ROOT / path).read_bytes() for path in SOURCE_PATHS}
        for relative, blob in cls.blobs.items():
            target = cls.snapshot / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(blob)
        cls.model = audit.read_model(cls.snapshot)
        cls.catalog = audit.products(cls.model["pickup_observation"],
                                     cls.model["pickup_coefficients"])

    def cli(self, *arguments, root=None):
        output, errors = io.StringIO(), io.StringIO()
        argv = ["audit_aliasing.py", "--repo-root", str(root or self.snapshot),
                "--compact", *arguments]
        with patch.object(sys, "argv", argv), redirect_stdout(output), redirect_stderr(errors):
            try:
                code = audit.main()
            except SystemExit as error:
                code = error.code
        return code, output.getvalue(), errors.getvalue()

    def test_single_cosine_cubic_coefficients_and_voltage(self):
        # x=2*A*cos(theta), lambda=5*x^3. Independently use
        # cos(theta)^3=(3*cos(theta)+cos(3*theta))/4.
        amplitude, weight, coefficient, omega = 0.125, 2.0, 5.0, 7.0
        terms = audit.products([weight], [0.0, 0.0, coefficient])
        self.assertEqual(set(audit.support(terms)), {(1,), (3,)})
        effective = weight * amplitude
        expected = {1: 3 * coefficient * effective**3 / 4,
                    3: coefficient * effective**3 / 4}
        actual = {
            harmonic: 2 * sum(term["linkage_structural_coefficient"] *
                              (amplitude / 2)**term["order"] for term in terms
                              if term["signed_mode_sum"] == [harmonic])
            for harmonic in (1, 3)
        }
        self.assertEqual(actual, expected)  # All inputs are exactly dyadic.
        for theta in (0.0, math.pi / 6, math.pi / 3, math.pi / 2, 1.234):
            x = effective * math.cos(theta)
            velocity = -effective * omega * math.sin(theta)
            direct = -3 * coefficient * x * x * velocity
            harmonic = sum(k * omega * value * math.sin(k * theta)
                           for k, value in actual.items())
            self.assertAlmostEqual(harmonic, direct, delta=2e-15)

    def test_mixed_products_keep_signed_locations_and_coefficients(self):
        terms = audit.products([2.0, -3.0], [0.0, 5.0, 7.0])
        by_counts = {tuple(term["positive_negative_counts"]): term for term in terms}
        for counts in ((1, 0, 1, 0), (1, 0, 0, 1)):
            term = by_counts[counts]
            self.assertEqual(term["multinomial"], 2)
            self.assertEqual(term["linkage_structural_coefficient"], -60.0)
        cubic = by_counts[(2, 0, 0, 1)]
        self.assertEqual(cubic["multinomial"], 3)
        self.assertEqual(cubic["linkage_structural_coefficient"], -252.0)
        self.assertEqual(cubic["signed_mode_sum"], [2, -1])
        self.assertEqual(cubic["mode_powers"], [2, 1])
        locations = audit.support(terms)
        self.assertIn((1, -1), locations)
        self.assertIn((1, 1), locations)
        self.assertIn((2, -1), locations)
        self.assertNotIn((-2, 1), locations)  # Conjugate location grouped once.
        self.assertEqual(audit.canonical([0, -2, 1]), (0, 2, -1))
        self.assertEqual(audit.canonical([0, 0, 0]), (0, 0, 0))

    def test_folding_matches_sampled_cosine_equivalence(self):
        for frequency, rate, expected in (
            (1000, 6000, 1000), (3000, 6000, 3000), (5000, 6000, 1000),
            (12000, 6000, 0), (42900, 48000, 5100), (2 * 1300 - 2300, 6000, 300),
        ):
            for sign in (-1, 1):
                for multiple in (-3, 0, 4):
                    original = sign * frequency + multiple * rate
                    folded = audit.fold(original, rate)
                    self.assertEqual(folded, expected)
                    self.assertGreaterEqual(folded, 0)
                    self.assertLessEqual(folded, rate / 2)
                    for sample in range(8):
                        self.assertAlmostEqual(math.cos(2 * math.pi * original * sample / rate),
                                               math.cos(2 * math.pi * folded * sample / rate),
                                               delta=5e-14)

    def test_catalog_and_damped_zero_carrier_limit(self):
        self.assertEqual(len(self.catalog), 83)
        self.assertEqual(len(audit.support(self.catalog)), 31)
        for regime, beta in (("held_or_sustained", self.model["free_decay"]),
                             ("released", self.model["damped_decay"])):
            row = audit.key_audit(self.model, self.catalog, 0, regime, [48000.0])
            zero = {tuple(term["mode_powers"]): term
                    for term in row["zero_carrier_envelope_terms"]}
            self.assertEqual(set(zero), {(2, 0, 0), (0, 2, 0), (0, 0, 2)})
            for j, weight in enumerate(self.model["pickup_observation"]):
                powers = tuple(2 if k == j else 0 for k in range(3))
                self.assertEqual(zero[powers]["envelope_decay_per_s"], 2 * beta[j])
                expected = 4 * self.model["pickup_coefficients"][1] * weight**2 * beta[j]
                self.assertAlmostEqual(zero[powers]["voltage_structural_coefficient"], expected)
        stationary = copy.deepcopy(self.model)
        stationary["free_decay"] = [0.0, 0.0, 0.0]
        row = audit.key_audit(stationary, self.catalog, 0, "held_or_sustained", [48000.0])
        self.assertTrue(all(term["voltage_structural_coefficient"] == 0.0
                            and term["envelope_decay_per_s"] == 0.0
                            for term in row["zero_carrier_envelope_terms"]))

    def test_endpoint_keys_and_structural_rate_limits(self):
        low = audit.key_audit(self.model, self.catalog, 0, "held_or_sustained", [48000.0, 96000.0])
        high = audit.key_audit(self.model, self.catalog, 72, "released", [48000.0, 96000.0])
        self.assertEqual((low["midi_pitch"], high["midi_pitch"]), (28, 100))
        self.assertAlmostEqual(low["natural_frequencies_hz"][0], 41.20344461410875, places=11)
        self.assertAlmostEqual(high["natural_frequencies_hz"][0], 2637.02045530296, places=9)
        self.assertTrue(all(a < b for a, b in zip(high["damped_carriers_hz"],
                                                high["natural_frequencies_hz"])))
        self.assertTrue(all(rate["above_nyquist_count"] == 0 for rate in low["rates"]))
        self.assertGreater(high["rates"][0]["above_nyquist_count"], 0)
        self.assertEqual(high["rates"][1]["above_nyquist_count"], 0)

    def test_source_drift_and_nonfinite_constants_fail_closed(self):
        mutations = (
            (SOURCE_PATHS[0], "3.0 * a3_ * x", "4.0 * a3_ * x"),
            (SOURCE_PATHS[1], "modes_per_key = 3;", "modes_per_key = 4;"),
            (SOURCE_PATHS[1], "double a1_ = 0.4;", "double a1_ = nan;"),
        )
        for relative, old, new in mutations:
            with self.subTest(replacement=new), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                for name, blob in self.blobs.items():
                    target = root / name
                    target.parent.mkdir(parents=True, exist_ok=True)
                    target.write_bytes(blob)
                target = root / relative
                text = target.read_text(encoding="utf-8")
                self.assertIn(old, text)
                target.write_text(text.replace(old, new, 1), encoding="utf-8")
                with self.assertRaises(ValueError):
                    audit.read_model(root)
                code, output, errors = self.cli("--keys", "0", root=root)
                self.assertEqual(code, 1)
                self.assertEqual(output, "")
                self.assertIn("Aliasing audit failed:", errors)

    def test_cli_rejects_invalid_keys_rates_and_regime(self):
        for arguments in (("--keys", "-1"), ("--keys", "73"),
                          ("--keys", "0", "--sample-rates", "nan"),
                          ("--keys", "0", "--sample-rates", "inf"),
                          ("--keys", "0", "--sample-rates", "31999"),
                          ("--keys", "0", "--sample-rates", "192001"),
                          ("--regime", "unknown")):
            with self.subTest(arguments=arguments):
                code, output, errors = self.cli(*arguments)
                self.assertNotEqual(code, 0)
                self.assertEqual(output, "")
                self.assertNotEqual(errors, "")

    def test_small_json_has_finite_values_deduplication_and_source_hashes(self):
        code, output, errors = self.cli("--keys", "72", "0", "72", "--sample-rates",
                                        "96000", "48000", "48000", "--regime", "held")
        self.assertEqual((code, errors), (0, ""))
        def reject_constant(token):
            raise ValueError("Nonfinite JSON token: " + token)
        value = json.loads(output, parse_constant=reject_constant)
        self.assertEqual(value["schema_version"], 1)
        self.assertEqual([row["key"] for row in value["keys"]], [0, 72])
        self.assertEqual([row["sample_rate_hz"] for row in value["summary"]], [48000.0, 96000.0])
        self.assertEqual(value["model"]["source_sha256"],
                         {name: hashlib.sha256(blob).hexdigest() for name, blob in self.blobs.items()})
        def check_finite(item):
            if isinstance(item, dict):
                for child in item.values():
                    check_finite(child)
            elif isinstance(item, list):
                for child in item:
                    check_finite(child)
            elif isinstance(item, (int, float)):
                self.assertTrue(math.isfinite(item))
        check_finite(value)
        self.assertIn("not an audio-error measurement", value["scope"])
        self.assertTrue(any("audibility" in limit for limit in value["limits"]))

    def test_self_check_guards_remain_active_under_python_optimization(self):
        result = audit.self_check(self.model)
        self.assertEqual(result["status"], "pass")
        self.assertEqual(result["complex_derivative_cases"], 6)
        broken = copy.deepcopy(self.model)
        broken["pickup_observation"][0] = 0.0
        with self.assertRaises(AssertionError):
            audit.self_check(broken)
        code, output, errors = self.cli("--self-check")
        self.assertEqual((code, errors), (0, ""))
        self.assertEqual(json.loads(output)["status"], "pass")


if __name__ == "__main__":
    unittest.main()
