"""Independent stationary spectral checks; no native or instrument evidence.

Expected values come from elementary trigonometric identities, direct prescribed
motion, or a full tiny DFT written here. No production DSP, symbolic alias audit,
NumPy, or oracle self-check is used to calculate the expected answers.
"""
from __future__ import annotations

from contextlib import redirect_stderr
import cmath
from dataclasses import replace
import io
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
with patch.object(sys, "path", [str(TOOLS), *sys.path]):
    import direct_alias_oracle as oracle


def tiny_fixture(*, modes=None, coefficients=(0.4, 3.0, 20.0), count=32):
    return oracle.Fixture("independent_tiny", 64, count,
                          (oracle.Mode(4, 0.02, 0.37),) if modes is None else modes,
                          coefficients=coefficients)


def prescribed_voltage(fixture):
    """Unreduced elementary q/v chain rule, evaluated only over tiny records."""
    a, b, c = fixture.coefficients
    samples = []
    for n in range(fixture.sample_count):
        t = n / fixture.sample_rate_hz
        q = sum(mode.amplitude_m * math.cos(2 * math.pi * mode.frequency_hz * t + mode.phase_rad)
                for mode in fixture.modes)
        v = sum(-2 * math.pi * mode.frequency_hz * mode.amplitude_m *
                math.sin(2 * math.pi * mode.frequency_hz * t + mode.phase_rad)
                for mode in fixture.modes)
        samples.append(-(a + 2 * b * q + 3 * c * q * q) * v)
    return tuple(samples)


def elementary_dft(samples):
    """All N bins by the defining complex sum, for N <= 32 in these tests."""
    count = len(samples)
    return {k: sum(value * cmath.exp(-2j * math.pi * k * n / count)
                   for n, value in enumerate(samples)) / count
            for k in range(-count // 2, count // 2)}


class SpectralCase(unittest.TestCase):
    def assertSamplesClose(self, actual, expected, tolerance=2e-13):
        self.assertEqual(len(actual), len(expected))
        self.assertLessEqual(max((abs(a - b) for a, b in zip(actual, expected)), default=0),
                             tolerance)

    def assertLinesClose(self, actual, expected, tolerance=2e-14):
        self.assertEqual(set(actual), set(expected))
        for key in expected:
            with self.subTest(bin=key):
                self.assertLessEqual(abs(actual[key] - expected[key]), tolerance)


class ClosedFormTests(SpectralCase):
    def test_one_mode_harmonics_and_faraday_sign(self):
        fixture = tiny_fixture()
        mode = fixture.modes[0]
        a, b, c = fixture.coefficients
        amplitude, phase, omega = mode.amplitude_m, mode.phase_rad, 2 * math.pi * mode.frequency_hz
        # cos²(theta)=(1+cos(2theta))/2; cos³(theta)=(3cos(theta)+cos(3theta))/4.
        voltage_amplitudes = (omega * (a * amplitude + 3 * c * amplitude**3 / 4),
                              omega * b * amplitude**2, 3 * omega * c * amplitude**3 / 4)
        expected_lines = {}
        for harmonic, voltage_amplitude in enumerate(voltage_amplitudes, 1):
            coefficient = voltage_amplitude * complex(math.sin(harmonic * phase),
                                                       -math.cos(harmonic * phase)) / 2
            expected_lines[2 * harmonic] = coefficient
            expected_lines[-2 * harmonic] = coefficient.conjugate()
        self.assertLinesClose(oracle.continuous_lines(fixture), expected_lines)
        expected = tuple(sum(v * math.sin(h * (omega * n / 64 + phase))
                             for h, v in enumerate(voltage_amplitudes, 1))
                         for n in range(32))
        self.assertSamplesClose(oracle.analytic_waveform(fixture), expected)
        self.assertSamplesClose(oracle.oracle_arrays(fixture)["s_v"], expected)
        self.assertGreater(expected[0], 0.0)  # A reversed Faraday sign cannot pass.

    def test_quadratic_cross_term_requires_sum_before_pickup(self):
        modes = (oracle.Mode(4, 0.02, 0.31), oracle.Mode(12, 0.03, -0.71))
        fixture = tiny_fixture(modes=modes, coefficients=(0.0, 1.7, 0.0))
        separately_converted = tuple(sum(row) for row in zip(*(
            prescribed_voltage(replace(fixture, modes=(mode,))) for mode in modes)))
        expected = prescribed_voltage(fixture)
        self.assertGreater(max(abs(a - b) for a, b in zip(expected, separately_converted)), 0.05)
        self.assertSamplesClose(oracle.oracle_arrays(fixture)["s_v"], expected)
        # Independent full tiny DFT also checks the difference-frequency sign.
        actual = oracle.folded_lines(oracle.continuous_lines(fixture), 32)
        for key, value in elementary_dft(expected).items():
            self.assertLessEqual(abs(actual.get(key, 0j) - value), 2e-14)

    def test_cubic_two_mode_table_matches_independent_chain_rule(self):
        fixture = tiny_fixture(modes=(oracle.Mode(6, 0.021, -0.4),
                                      oracle.Mode(22, 0.015, 0.9)))
        expected = prescribed_voltage(fixture)
        self.assertSamplesClose(oracle.analytic_waveform(fixture), expected)
        self.assertSamplesClose(oracle.oracle_arrays(fixture)["s_v"], expected)
        actual = oracle.folded_lines(oracle.continuous_lines(fixture), 32)
        for key, value in elementary_dft(expected).items():
            self.assertLessEqual(abs(actual.get(key, 0j) - value), 2e-14)

    def test_coincident_modes_reduce_to_one_phasor_before_nonlinearity(self):
        modes = (oracle.Mode(4, 0.02, 0.7), oracle.Mode(4, 0.03, -0.2))
        two = tiny_fixture(modes=modes)
        phasor = sum(mode.amplitude_m * cmath.exp(1j * mode.phase_rad) for mode in modes)
        one = replace(two, modes=(oracle.Mode(4, abs(phasor), cmath.phase(phasor)),))
        self.assertLinesClose(oracle.continuous_lines(two), oracle.continuous_lines(one))
        self.assertSamplesClose(oracle.oracle_arrays(two)["s_v"], prescribed_voltage(one))

    def test_static_offset_modifies_ac_harmonics_without_dc_voltage(self):
        fixture = tiny_fixture(modes=(oracle.Mode(0, 0.01, 0.2),
                                      oracle.Mode(4, 0.02, 0.37)))
        lines = oracle.continuous_lines(fixture)
        self.assertNotIn(0, lines)
        self.assertSamplesClose(oracle.oracle_arrays(fixture)["s_v"], prescribed_voltage(fixture))
        unshifted = replace(fixture, modes=(fixture.modes[1],))
        self.assertGreater(abs(lines[2] - oracle.continuous_lines(unshifted)[2]), 0.01)

    def test_negative_frequency_with_reversed_phase_is_same_motion(self):
        positive = tiny_fixture()
        mode = positive.modes[0]
        negative = replace(positive, modes=(oracle.Mode(-mode.frequency_hz, mode.amplitude_m,
                                                       -mode.phase_rad),))
        self.assertEqual(oracle.continuous_lines(negative), oracle.continuous_lines(positive))
        self.assertSamplesClose(oracle.analytic_waveform(negative), prescribed_voltage(positive))

    def test_zero_coefficients_and_zero_motion_give_exact_zero(self):
        for fixture in (tiny_fixture(coefficients=(0.0, 0.0, 0.0)),
                        tiny_fixture(modes=(oracle.Mode(4, 0.0, 0.37),))):
            with self.subTest(fixture=fixture):
                self.assertEqual(oracle.continuous_lines(fixture), {})
                arrays = oracle.oracle_arrays(fixture)
                for name in ("s_v", "direct_v", "b_v", "alias_v", "analytic_alias_v"):
                    self.assertEqual(arrays[name], (0.0,) * fixture.sample_count)


class FoldingTests(SpectralCase):
    def test_signed_half_open_bin_map_across_multiple_periods(self):
        expected = {-49: 15, -48: -16, -33: -1, -32: 0, -17: 15, -16: -16,
                    -1: -1, 0: 0, 15: 15, 16: -16, 17: -15, 31: -1,
                    32: 0, 33: 1, 48: -16, 49: -15}
        for original, folded in expected.items():
            with self.subTest(original=original):
                self.assertEqual(oracle.fold_bin(original, 32), folded)

    def test_differentiate_original_signed_frequency_before_folding(self):
        for key in (13, -13):
            with self.subTest(original=key):
                term = oracle.CosineTerm(key, 0.01, 0.2)
                lines = oracle.voltage_lines_from_cosines((term,), 32, 16)
                coefficient = -1j * math.pi * (2 * key) * 0.01 * cmath.exp(0.2j)
                self.assertEqual(lines[key], coefficient)
                folded = oracle.folded_lines(lines, 16)
                alias_key = -3 if key == 13 else 3
                self.assertEqual(folded[alias_key], coefficient)
                wrong = -1j * math.pi * (2 * alias_key) * 0.01 * cmath.exp(0.2j)
                self.assertGreater(abs(folded[alias_key] - wrong), 0.9)

    def test_phase_coalescence_cancels_equal_voltage_at_f_and_fs_minus_f(self):
        for alpha in (0.0, 0.37, math.pi / 2):
            with self.subTest(alpha=alpha):
                low = -0.5j * cmath.exp(1j * alpha)
                high = -0.5j * cmath.exp(-1j * alpha)
                lines = {3: low, -3: low.conjugate(), 29: high, -29: high.conjugate()}
                self.assertEqual(oracle.folded_lines(lines, 32), {})
                self.assertEqual(oracle.sample_lines(lines, 32), (0.0,) * 32)
                self.assertEqual(oracle.line_rms(lines, 32), 0.0)
        # A pi shift is different: at alpha=0 the sampled sines reinforce.
        reinforced = {3: -0.5j, -3: 0.5j, 29: 0.5j, -29: -0.5j}
        self.assertEqual(oracle.folded_lines(reinforced, 32), {-3: 1j, 3: -1j})
        self.assertAlmostEqual(oracle.line_rms(reinforced, 32), math.sqrt(2), delta=1e-15)

    def test_equal_flux_amplitudes_do_not_imply_equal_voltage_cancellation(self):
        terms = (oracle.CosineTerm(3, 0.01, 0.0), oracle.CosineTerm(29, 0.01, 0.0))
        lines = oracle.voltage_lines_from_cosines(terms, 32, 32)
        folded = oracle.folded_lines(lines, 32)
        self.assertAlmostEqual(folded[3].imag, math.pi * (29 - 3) * 0.01, delta=2e-16)
        self.assertGreater(oracle.line_rms(lines, 32), 1.0)

    def test_complex_sums_precede_magnitudes_and_no_small_lines_are_dropped(self):
        lines = {3: 0.25 + 0.5j, -3: 0.25 - 0.5j,
                 35: -0.25 + 0.125j, -35: -0.25 - 0.125j}
        self.assertEqual(oracle.folded_lines(lines, 32), {-3: -0.625j, 3: 0.625j})
        self.assertAlmostEqual(oracle.line_rms(lines, 32), 0.625 * math.sqrt(2), delta=2e-16)
        tiny = 2.0**-70
        self.assertEqual(oracle.coalesce_lines([(1, tiny), (2, -tiny), (3, 1), (3, -1)]),
                         {1: complex(tiny), 2: complex(-tiny)})

    def test_dc_and_nyquist_conjugates_coalesce_once_and_preserve_phase(self):
        for key in (8, 16):
            for phase in (0.0, math.pi / 2):
                with self.subTest(key=key, phase=phase):
                    original_hz = key * 2
                    lines = oracle.voltage_lines_from_cosines(
                        (oracle.CosineTerm(key, 0.01, phase),), 32, 16)
                    amplitude = 2 * math.pi * original_hz * 0.01 * math.sin(phase)
                    folded = oracle.folded_lines(lines, 16)
                    expected_key = -8 if key == 8 else 0
                    self.assertEqual(folded, {} if phase == 0 else {expected_key: complex(amplitude)})
                    expected = tuple(amplitude * ((-1.0)**n if key == 8 else 1.0) for n in range(16))
                    self.assertSamplesClose(oracle.sample_lines(lines, 16), expected, tolerance=1e-15)
                    self.assertAlmostEqual(oracle.line_rms(lines, 16), abs(amplitude), delta=1e-15)
                    self.assertEqual(oracle.bandlimited_lines(lines, 16), {})

    def test_original_dc_flux_always_differentiates_to_zero(self):
        for phase in (0.0, 0.37, math.pi / 2):
            self.assertEqual(oracle.voltage_lines_from_cosines(
                (oracle.CosineTerm(0, 17.0, phase),), 32, 16), {})

    def test_ideal_band_selection_precedes_folding_and_excludes_nyquist(self):
        lines = {key: complex(key) for key in (-16, -9, -8, -7, 0, 7, 8, 9, 16)}
        self.assertEqual(oracle.bandlimited_lines(lines, 16), {-7: -7 + 0j, 7: 7 + 0j})
        # An above-Nyquist source that folds inside the band must still be removed.
        self.assertNotIn(-7, oracle.bandlimited_lines({9: 1j}, 16))
        self.assertEqual(oracle.alias_lines({9: 1j, -9: -1j}, 16), {-7: 1j, 7: -1j})


class DftAndErrorTests(SpectralCase):
    def test_full_tiny_dft_sign_normalization_and_parseval_with_endpoints(self):
        count, phase = 16, 0.4
        samples = tuple(2 + 3 * math.cos(2 * math.pi * 3 * n / count + phase)
                        + 0.5 * math.sin(2 * math.pi * 5 * n / count - 0.2)
                        + 0.25 * (-1)**n for n in range(count))
        c3 = 1.5 * cmath.exp(1j * phase)
        c5 = -0.25j * cmath.exp(-0.2j)
        expected = {0: 2 + 0j, -8: 0.25 + 0j, 3: c3, -3: c3.conjugate(),
                    5: c5, -5: c5.conjugate()}
        direct = elementary_dft(samples)
        result = oracle.dft_at_bins(samples, tuple(range(-8, 8)))
        for key in direct:
            self.assertLessEqual(abs(result[key] - direct[key]), 5e-15)
            self.assertLessEqual(abs(result[key] - expected.get(key, 0j)), 5e-15)
        expected_rms = math.sqrt(2**2 + 3**2 / 2 + 0.5**2 / 2 + 0.25**2)
        self.assertAlmostEqual(oracle.signal_metrics(samples)["rms_v"], expected_rms, delta=1e-15)
        self.assertAlmostEqual(oracle.line_rms(expected, count), expected_rms, delta=1e-15)
        self.assertSamplesClose(oracle.sample_lines(expected, count), samples, tolerance=1e-14)

    def test_parseval_uses_folded_coalesced_power(self):
        same_phase = {3: 1.0, -3: 1.0, 19: 1.0, -19: 1.0}
        self.assertAlmostEqual(oracle.line_rms(same_phase, 16), math.sqrt(8), delta=1e-15)
        self.assertNotAlmostEqual(oracle.line_rms(same_phase, 16), math.sqrt(4))
        cancel = {3: 1.0, -3: 1.0, 19: -1.0, -19: -1.0}
        self.assertEqual(oracle.line_rms(cancel, 16), 0.0)

    def test_requested_dft_aliases_are_one_signed_key(self):
        samples = (1.0, 0.0, -1.0, 0.0)
        result = oracle.dft_at_bins(samples, (-5, -1, 3, 7, 1, 5, 0, 2))
        self.assertEqual(set(result), {-2, -1, 0, 1})
        self.assertAlmostEqual(result[-1].real, 0.5, delta=1e-15)
        self.assertAlmostEqual(result[1].real, 0.5, delta=1e-15)
        self.assertLessEqual(abs(result[-2]), 1e-15)
        self.assertLessEqual(abs(result[0]), 1e-15)

    def test_two_sample_record_keeps_dc_and_nyquist_without_doubling(self):
        lines = {0: 2.0, -1: 0.5}
        self.assertEqual(oracle.sample_lines(lines, 2), (2.5, 1.5))
        self.assertLinesClose(oracle.dft_at_bins((2.5, 1.5), (0, 1)), {0: 2 + 0j, -1: 0.5 + 0j})
        self.assertAlmostEqual(oracle.line_rms(lines, 2), math.sqrt(4.25), delta=1e-15)

    def test_error_components_keep_sample_positions_and_signed_values(self):
        result = oracle.error_arrays((1.5, -0.5, 2.0, -2.0),
                                     (1.0, -1.0, 1.0, -1.0),
                                     (0.25, -0.75, 0.75, -0.25))
        self.assertEqual(result, {
            "numerical_v": (0.5, 0.5, 1.0, -1.0),
            "alias_v": (0.75, -0.25, 0.25, -0.75),
            "total_v": (1.25, 0.25, 1.25, -1.75), "closure_v": (0.0,) * 4})
        self.assertEqual(oracle.signal_metrics(result["numerical_v"])["mean_v"], 0.25)


class FrozenPlanAndBudgetTests(SpectralCase):
    def test_frozen_rates_counts_modes_and_fresh_json_plan(self):
        fixtures = oracle.fixtures()
        self.assertEqual(tuple((f.case_id, f.sample_rate_hz, f.sample_count) for f in fixtures),
                         (("one_mode_48000", 48000, 1024), ("one_mode_96000", 96000, 2048),
                          ("two_modes_48000", 48000, 1024), ("two_modes_96000", 96000, 2048)))
        self.assertEqual({f.duration_s for f in fixtures}, {1024 / 48000})
        self.assertEqual(tuple(f.frequency_bins for f in fixtures), ((288,), (288,), (160, 288), (160, 288)))
        for fixture in fixtures:
            self.assertEqual(fixture.coefficients, (0.4, 800.0, 400000.0))
            self.assertEqual((fixture.mass_kg, fixture.decay_per_s), (0.001, 0.0))
        first = oracle.plan()
        self.assertEqual(json.loads(json.dumps(first, allow_nan=False)), first)
        first["fixtures"][0]["modes"][0]["frequency_hz"] = 1
        self.assertEqual(oracle.plan()["fixtures"][0]["modes"][0]["frequency_hz"], 13500)
        budgets = oracle.plan()["new_diagnostic_budgets"]
        self.assertEqual((budgets["bookkeeping_peak_v"], budgets["primitive_numerical_peak_v"],
                          budgets["primitive_numerical_rms_v"]), (1e-12, 1e-10, 1e-11))
        self.assertIsNone(budgets["alias_acceptability_threshold"])
        self.assertFalse(budgets["changes_historical_budgets_or_acceptance"])

    def test_frozen_continuous_retained_and_removed_bin_sets(self):
        for fixture in oracle.fixtures():
            with self.subTest(case=fixture.case_id):
                positives = {288, 576, 864} if len(fixture.modes) == 1 else {
                    32, 128, 160, 288, 320, 416, 448, 480, 576, 608, 736, 864}
                expected = positives | {-key for key in positives}
                lines = oracle.continuous_lines(fixture)
                self.assertEqual(set(lines), expected)
                retained = expected if fixture.sample_rate_hz == 96000 else {
                    key for key in expected if abs(key) < 512}
                self.assertEqual(set(oracle.bandlimited_lines(lines, fixture.sample_count)), retained)
                expected_alias_positive = set() if fixture.sample_rate_hz == 96000 else (
                    {160, 448} if len(fixture.modes) == 1 else {160, 288, 416, 448})
                self.assertEqual(set(oracle.alias_lines(lines, fixture.sample_count)),
                                 expected_alias_positive | {-key for key in expected_alias_positive})

    def test_one_mode_48000_alias_rms_and_sample_peak_from_removed_sines(self):
        fixture = oracle.fixtures()[0]
        amplitude, frequency, phase = 1e-4, 13500, math.pi / 7
        second = 2 * math.pi * frequency * 800 * amplitude**2
        third = 3 * (2 * math.pi * frequency) * 400000 * amplitude**3 / 4
        # The coherent sequence has period 32; no sampled-frequency derivative.
        expected_period = tuple(second * math.sin(2 * (2 * math.pi * (9 * n % 32) / 32 + phase))
                                + third * math.sin(3 * (2 * math.pi * (9 * n % 32) / 32 + phase))
                                for n in range(32))
        result = oracle.metrics(fixture)
        self.assertAlmostEqual(result["alias_error"]["rms_v"],
                               math.sqrt((second**2 + third**2) / 2), delta=2e-15)
        self.assertAlmostEqual(result["alias_error"]["sample_peak_v"],
                               max(map(abs, expected_period)), delta=2e-15)
        self.assertSamplesClose(oracle.oracle_arrays(fixture)["alias_v"], expected_period * 32, tolerance=3e-15)

    def test_96000_alias_is_exact_zero_without_primitive_success_claim(self):
        for fixture in oracle.fixtures()[1::2]:
            with self.subTest(case=fixture.case_id):
                arrays = oracle.oracle_arrays(fixture)
                self.assertEqual(arrays["s_v"], arrays["b_v"])
                self.assertEqual(arrays["alias_v"], (0.0,) * 2048)
                self.assertEqual(arrays["analytic_alias_v"], (0.0,) * 2048)
                result = oracle.metrics(fixture)
                self.assertTrue(result["passed"])
                self.assertFalse(result["primitive_compared"])
                self.assertNotIn("numerical_error", result)
                self.assertNotIn("primitive_numerical_peak", result["checks"])

    def test_nonzero_alias_has_no_acceptability_gate_when_primitive_matches_s(self):
        fixture = oracle.fixtures()[2]
        result = oracle.metrics(fixture, oracle.oracle_arrays(fixture)["s_v"])
        self.assertTrue(result["passed"])
        self.assertTrue(result["primitive_compared"])
        self.assertGreater(result["alias_error"]["sample_peak_v"], 0.2)
        self.assertEqual(result["numerical_error"]["sample_peak_v"], 0.0)
        self.assertEqual(result["total_error"], result["alias_error"])

    def test_peak_and_rms_limits_are_distinct_inclusive_and_fail_closed(self):
        fixture = tiny_fixture(coefficients=(0.0, 0.0, 0.0), count=128)
        for peak, expected in ((1e-10, True), (math.nextafter(1e-10, math.inf), False)):
            result = oracle.metrics(fixture, (peak,) + (0.0,) * 127)
            self.assertEqual(result["checks"]["primitive_numerical_peak"], expected)
            self.assertTrue(result["checks"]["primitive_numerical_rms"])
            self.assertEqual(result["failures"], [] if expected else ["primitive_numerical_peak"])
            self.assertEqual(result["passed"], expected)
        fixture = tiny_fixture(modes=(oracle.Mode(16, 0.0, 0.0),), coefficients=(0.0, 0.0, 0.0), count=4)
        for rms, expected in ((1e-11, True), (math.nextafter(1e-11, math.inf), False)):
            result = oracle.metrics(fixture, (rms,) * 4)
            self.assertTrue(result["checks"]["primitive_numerical_peak"])
            self.assertEqual(result["checks"]["primitive_numerical_rms"], expected)
            self.assertEqual(result["failures"], [] if expected else ["primitive_numerical_rms"])

    def test_bookkeeping_limit_rejects_a_known_harmonic_disagreement(self):
        fixture = tiny_fixture(coefficients=(0.0, 0.0, 0.0))
        for value, expected in ((1e-12, True), (math.nextafter(1e-12, math.inf), False)):
            arrays = oracle.oracle_arrays(fixture)
            arrays["direct_v"] = (value,) + (0.0,) * 31
            with patch.object(oracle, "oracle_arrays", return_value=arrays):
                result = oracle.metrics(fixture)
            self.assertEqual(result["checks"]["direct_polynomial_matches_harmonics"], expected)
            self.assertEqual(result["failures"], [] if expected else ["direct_polynomial_matches_harmonics"])
            self.assertEqual(result["passed"], expected)

    def test_cli_plan_and_help_work_without_optional_imports_or_artifacts(self):
        code = """
import builtins, sys
sys.dont_write_bytecode = True
sys.path.insert(0, sys.argv[1])
original = builtins.__import__
def guarded(name, *args, **kwargs):
    if name.split('.')[0] in {'numpy', 'scipy', 'audit_aliasing', 'research_engine'}:
        raise AssertionError('forbidden oracle dependency: ' + name)
    return original(name, *args, **kwargs)
builtins.__import__ = guarded
import direct_alias_oracle
raise SystemExit(direct_alias_oracle.main([sys.argv[2]]))
"""
        for argument in ("--plan", "--help"):
            with self.subTest(argument=argument), tempfile.TemporaryDirectory() as directory:
                result = subprocess.run([sys.executable, "-S", "-c", code, str(TOOLS), argument],
                                        cwd=directory, capture_output=True, text=True, timeout=10)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(result.stderr, "")
                self.assertEqual(list(Path(directory).iterdir()), [])
                if argument == "--plan":
                    self.assertEqual(json.loads(result.stdout), oracle.plan())
                else:
                    self.assertIn("--self-check", result.stdout)


class ValidationTests(unittest.TestCase):
    def test_modes_reject_nonfinite_ambiguous_and_out_of_range_values(self):
        invalid = [(True, 1.0, 0.0), (1.0, 1.0, 0.0), (576001, 1.0, 0.0),
                   (1, -1.0, 0.0), (1, True, 0.0), (1, math.nan, 0.0),
                   (1, math.inf, 0.0), (1, 10**400, 0.0), (1, 1.0, True),
                   (1, 1.0, math.nan), (1, 1.0, "0")]
        for arguments in invalid:
            with self.subTest(arguments=arguments), self.assertRaises(ValueError):
                oracle.Mode(*arguments)

    def test_fixtures_reject_incoherence_unbounded_and_mutable_inputs(self):
        base = {"case_id": "valid", "sample_rate_hz": 64, "sample_count": 32,
                "modes": (oracle.Mode(4, 0.02, 0.0),)}
        invalid = [{"case_id": ""}, {"case_id": "../escape"}, {"case_id": "x" * 81},
                   {"sample_rate_hz": True}, {"sample_rate_hz": 0}, {"sample_rate_hz": 192001},
                   {"sample_count": 1}, {"sample_count": 3}, {"sample_count": 2050},
                   {"sample_count": 32.0}, {"modes": ()}, {"modes": list(base["modes"])},
                   {"modes": base["modes"] * 3}, {"modes": (object(),)},
                   {"modes": (oracle.Mode(3, 0.02, 0.0),)},
                   {"modes": (oracle.Mode(194, 0.02, 0.0),)},
                   {"coefficients": [1, 2, 3]}, {"coefficients": (1, 2)},
                   {"coefficients": (1, 2, math.inf)}, {"coefficients": (1, True, 3)},
                   {"mass_kg": 0.0}, {"mass_kg": math.nan}, {"decay_per_s": 1e-300}]
        for override in invalid:
            with self.subTest(override=override), self.assertRaises(ValueError):
                oracle.Fixture(**{**base, **override})

    def test_line_algebra_rejects_bad_types_nonfinite_values_and_cap_overflow(self):
        invalid = [None, "text", [(True, 1)], [(1.0, 1)], [(18433, 1)], [(1, True)],
                   [(1, math.nan)], [(1, complex(0, math.inf))], [(1, 2, 3)],
                   [(1, 1)] * 129, [(1, 1e308), (1, 1e308)]]
        for terms in invalid:
            with self.subTest(terms=terms), self.assertRaises(ValueError):
                oracle.coalesce_lines(terms)
        for lines in ({0: 1j}, {-8: 1j}, {1: 1j}, {1: 1j, -1: 1j}):
            for function in (oracle.sample_lines, oracle.line_rms):
                with self.subTest(lines=lines, function=function.__name__), self.assertRaises(ValueError):
                    function(lines, 16)
        with self.assertRaises(ValueError):
            oracle.voltage_lines_from_cosines((oracle.CosineTerm(1, 1.0, 0.0),) * 65, 64, 32)

    def test_array_metrics_dft_and_decomposition_reject_invalid_data(self):
        for values in ([], [0.0] * 2049, "zero", (True,), (math.nan,), (math.inf,), iter([0.0])):
            with self.subTest(values=type(values).__name__), self.assertRaises(ValueError):
                oracle.signal_metrics(values)
        with self.assertRaises(ValueError):
            oracle.error_arrays((0, 1), (0,), (0, 1))
        with self.assertRaises(ValueError):
            oracle.error_arrays((1e308,), (-1e308,), (0,))
        for values, bins in (((0, 1, 0), (0,)), ((0, 0), [0] * 65),
                             ((0, 0), (True,)), ((0, 0), "zero")):
            with self.subTest(values=values, bins=bins), self.assertRaises(ValueError):
                oracle.dft_at_bins(values, bins)
        for samples in ((0.0,), (0.0,) * 31 + (math.nan,), "0" * 32):
            with self.subTest(samples=type(samples).__name__), self.assertRaises(ValueError):
                oracle.metrics(tiny_fixture(), samples)

    def test_overflow_is_rejected_instead_of_returning_nonfinite_samples(self):
        fixture = tiny_fixture(modes=(oracle.Mode(4, 1e308, 0.37),))
        for function in (oracle.continuous_lines, oracle.analytic_waveform, oracle.oracle_arrays):
            with self.subTest(function=function.__name__), self.assertRaises(ValueError):
                function(fixture)
        with self.assertRaises(ValueError):
            oracle.voltage_lines_from_cosines((oracle.CosineTerm(9, 1e308, 0.0),), 192000, 2)

    def test_cli_rejects_missing_conflicting_and_unknown_operations(self):
        for arguments in ([], ["--plan", "--self-check"], ["--output", "unused"]):
            with self.subTest(arguments=arguments), redirect_stderr(io.StringIO()), self.assertRaises(SystemExit) as caught:
                oracle.main(arguments)
            self.assertEqual(caught.exception.code, 2)


if __name__ == "__main__":
    unittest.main()
