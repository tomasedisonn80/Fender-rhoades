"""Reproducibility tests and an optional independent SciPy FIR audit.

The generator and the base tests use only Python's standard library. SciPy and
NumPy are optional offline test tools, never generator/build/runtime dependencies.
The dense grid is an observation, not a bound at every continuous frequency.
"""
from __future__ import annotations

from contextlib import redirect_stderr, redirect_stdout
from decimal import Inexact, ROUND_DOWN, localcontext
import importlib.util
import io
import math
from pathlib import Path
import re
import struct
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
GENERATOR = ROOT / "tools/generate_audio_filter.py"
SPEC = importlib.util.spec_from_file_location("audio_filter_generator", GENERATOR)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError("Cannot load audio filter coefficient generator")
generator = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(generator)
HAVE_SCIPY = (importlib.util.find_spec("numpy") is not None and
              importlib.util.find_spec("scipy") is not None)


def parse_header(text: str) -> dict[int, tuple[float, ...]]:
    arrays = {}
    for match in re.finditer(r"std::array<double, (\d+)> coefficients_l([124]) \{"
                             r"\n(.*?)\n\};", text, re.DOTALL):
        size, factor, body = match.groups()
        values = tuple(float.fromhex(token.strip())
                       for token in body.split(",") if token.strip())
        if len(values) != int(size):
            raise ValueError("Declared coefficient count differs from initializer")
        arrays[int(factor)] = values
    return arrays


class AudioFilterGenerationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.header = generator.generate_header()
        cls.arrays = parse_header(cls.header)

    def run_main(self, *arguments):
        output, errors = io.StringIO(), io.StringIO()
        with redirect_stdout(output), redirect_stderr(errors):
            code = generator.main(list(arguments))
        return code, output.getvalue(), errors.getvalue()

    def test_frozen_sizes_exact_symmetry_dc_and_binary64_serialization(self):
        self.assertEqual(set(self.arrays), {1, 2, 4})
        for factor, coefficients in self.arrays.items():
            with self.subTest(factor=factor):
                self.assertEqual(len(coefficients), 96 * factor + 1)
                self.assertEqual(coefficients, generator.generate_coefficients(factor))
                bits = [struct.pack("<d", value) for value in coefficients]
                self.assertEqual(bits, bits[::-1])
                self.assertTrue(all(math.isfinite(value) for value in coefficients))
                self.assertAlmostEqual(math.fsum(coefficients), 1.0, delta=3e-16)
                self.assertEqual((len(coefficients) - 1) // (2 * factor), 48)

    def test_repeat_generation_ignores_ambient_decimal_precision_rounding_and_traps(self):
        with localcontext() as context:
            context.prec = 9
            context.rounding = ROUND_DOWN
            context.traps[Inexact] = True
            self.assertEqual(generator.generate_header(), self.header)

    def test_stdlib_direct_response_grid_satisfies_frozen_gates(self):
        # A direct cosine sum is independent of coefficient construction and
        # retains a useful offline response check even without scientific tools.
        # The optional SciPy audit below covers the denser frozen-study grid.
        for factor, coefficients in self.arrays.items():
            with self.subTest(factor=factor):
                middle = len(coefficients) // 2
                pairs = tuple(enumerate(coefficients[middle + 1:], start=1))
                frequencies = [i * factor / 16384 for i in range(8193)]
                frequencies.extend((0.446, 0.5))
                passband, stopband = [], []
                for frequency in frequencies:
                    if 0.446 < frequency < 0.5:
                        continue
                    angle = 2 * math.pi * frequency / factor
                    magnitude = abs(coefficients[middle] + 2 * math.fsum(
                        coefficient * math.cos(angle * offset)
                        for offset, coefficient in pairs))
                    if frequency <= 0.446:
                        passband.append(magnitude)
                    if frequency >= 0.5:
                        stopband.append(magnitude)
                self.assertGreaterEqual(min(passband), 10 ** (-0.05 / 20))
                self.assertLessEqual(max(passband), 10 ** (0.05 / 20))
                self.assertLessEqual(max(stopband), 10 ** (-80 / 20))

    def test_rejects_unsupported_factors(self):
        for factor in (0, 3, 8, -1, 1.0, True, "2"):
            with self.subTest(factor=factor), self.assertRaises(ValueError):
                generator.generate_coefficients(factor)

    def test_committed_header_matches_generator(self):
        self.assertEqual(generator.DEFAULT_OUTPUT.read_bytes(), self.header.encode("ascii"))
        code, output, errors = self.run_main("--check")
        self.assertEqual((code, errors), (0, ""))
        self.assertIn("is current", output)

    def test_generate_and_check_do_not_depend_on_working_directory(self):
        with tempfile.TemporaryDirectory(prefix="rhodes fir ") as directory:
            root = Path(directory)
            output = root / "coefficients.hpp"
            code, _, errors = self.run_main("--output", str(output))
            self.assertEqual((code, errors), (0, ""))
            self.assertEqual(output.read_bytes(), self.header.encode("ascii"))
            # -S excludes site packages: generation/check requires no NumPy/SciPy.
            process = subprocess.run([sys.executable, "-S", str(GENERATOR), "--check"],
                                     cwd=root, capture_output=True, text=True, check=False)
            self.assertEqual(process.returncode, 0, process.stderr)
            self.assertEqual(set(root.iterdir()), {output})

    def test_stale_and_missing_checks_are_read_only(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            stale = root / "stale.hpp"
            original = b"// Deliberately stale, do not repair in --check.\n"
            stale.write_bytes(original)
            before = stale.stat()
            code, output, errors = self.run_main("--check", "--output", str(stale))
            after = stale.stat()
            self.assertEqual(code, 1)
            self.assertEqual(output, "")
            self.assertIn("is stale", errors)
            self.assertEqual(stale.read_bytes(), original)
            self.assertEqual((before.st_mtime_ns, before.st_ino),
                             (after.st_mtime_ns, after.st_ino))
            missing = root / "missing-parent" / "missing.hpp"
            code, output, errors = self.run_main("--check", "--output", str(missing))
            self.assertEqual(code, 1)
            self.assertEqual(output, "")
            self.assertIn("failed", errors)
            self.assertEqual(set(root.iterdir()), {stale})

    def test_generation_refuses_symlink_without_mutating_target(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            target = root / "target.hpp"
            target.write_bytes(b"keep this target\n")
            link = root / "link.hpp"
            try:
                link.symlink_to(target)
            except (OSError, NotImplementedError):
                self.skipTest("Symlink creation is not supported in this environment")
            code, output, errors = self.run_main("--output", str(link))
            self.assertEqual(code, 1)
            self.assertEqual(output, "")
            self.assertIn("Refusing symlink", errors)
            self.assertTrue(link.is_symlink())
            self.assertEqual(target.read_bytes(), b"keep this target\n")
            self.assertEqual(set(root.iterdir()), {target, link})


@unittest.skipUnless(HAVE_SCIPY, "Optional independent audit needs installed NumPy/SciPy")
class IndependentAudioFilterTests(unittest.TestCase):
    def test_scipy_coefficients_and_frozen_dense_frequency_gates(self):
        import numpy as np
        from scipy.signal import firwin, freqz

        arrays = parse_header(generator.DEFAULT_OUTPUT.read_text(encoding="ascii"))
        self.assertEqual(set(arrays), {1, 2, 4})
        for factor, values in arrays.items():
            with self.subTest(factor=factor):
                coefficients = np.asarray(values)
                reference = firwin(96 * factor + 1, 0.47, window=("kaiser", 9),
                                   fs=factor, scale=True)
                self.assertLessEqual(float(np.max(np.abs(coefficients - reference))), 5e-15)
                frequencies, response = freqz(coefficients, worN=1_048_577,
                                               include_nyquist=True, fs=factor)
                self.assertEqual(float(frequencies[0]), 0.0)
                self.assertEqual(float(frequencies[-1]), factor / 2)
                # Exact specification edges supplement the equally spaced grid.
                _, edges = freqz(coefficients, worN=[0.446, 0.5], fs=factor)
                magnitude = np.abs(response)
                passband = np.append(magnitude[frequencies <= 0.446], abs(edges[0]))
                stopband = np.append(magnitude[frequencies >= 0.5], abs(edges[1]))
                pass_db = 20 * np.log10(passband)
                self.assertGreaterEqual(float(pass_db.min()), -0.05)
                self.assertLessEqual(float(pass_db.max()), 0.05)
                self.assertLessEqual(float(stopband.max()), 10 ** (-80 / 20))


if __name__ == "__main__":
    unittest.main()
