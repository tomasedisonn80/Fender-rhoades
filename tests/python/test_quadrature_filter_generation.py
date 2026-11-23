"""Independent Q6 rule/kernel checks and generator publication regressions.

Base checks use the standard library. NumPy/SciPy are optional offline oracles;
they are not dependencies of the generator, C++ build or audio callback. The
rule's polynomial moments do not imply exactness for the audio integrand.
"""
from __future__ import annotations

from contextlib import redirect_stderr, redirect_stdout
from decimal import Inexact, ROUND_DOWN, localcontext
from fractions import Fraction
import importlib.util
import io
import math
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
GENERATOR = ROOT / "tools/generate_quadrature_filter.py"
SPEC = importlib.util.spec_from_file_location("quadrature_filter_generator", GENERATOR)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError("Cannot load quadrature coefficient generator")
generator = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(generator)
HAVE_SCIPY = (importlib.util.find_spec("numpy") is not None and
              importlib.util.find_spec("scipy") is not None)


def parse_header(text: str):
    """Read emitted binary64 literals, independently of generator return values."""
    def numbers(body):
        return tuple(float.fromhex(value.strip())
                     for value in body.split(",") if value.strip())

    arrays = {}
    for name in ("nodes", "weights"):
        match = re.search(r"\b" + name + r"\s*\{\s*(.*?)\s*\};", text, re.DOTALL)
        if match is None:
            raise ValueError(f"Missing {name} initializer")
        arrays[name] = numbers(match.group(1))
    match = re.search(r"\bcoefficients\s*\{\{(.*?)\}\};", text, re.DOTALL)
    if match is None:
        raise ValueError("Missing phase coefficients")
    phases = tuple(numbers(body) for body in re.findall(
        r"\{\{(.*?)\}\}", match.group(1), re.DOTALL))
    normalizer = re.search(r"\bnormalizer\s*=\s*([^;]+);", text)
    if normalizer is None:
        raise ValueError("Missing continuous-kernel quadrature normalizer")
    return arrays["nodes"], arrays["weights"], phases, float.fromhex(normalizer.group(1))


class QuadratureFilterGenerationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.header = generator.generate_header()
        cls.nodes, cls.weights, cls.phases, cls.normalizer = parse_header(cls.header)

    def run_main(self, *arguments):
        output, errors = io.StringIO(), io.StringIO()
        with redirect_stdout(output), redirect_stderr(errors):
            code = generator.main(list(arguments))
        return code, output.getvalue(), errors.getvalue()

    def test_serialized_rule_integrates_degree_zero_through_eleven(self):
        self.assertEqual(len(self.nodes), 6)
        self.assertEqual(len(self.weights), 6)
        self.assertEqual(tuple(sorted(self.nodes)), self.nodes)
        self.assertTrue(all(0.0 < value < 1.0 for value in self.nodes))
        self.assertTrue(all(math.isfinite(value) and value > 0.0 for value in self.weights))
        # This independent integral of t**degree checks the rule, including
        # its mapping from [-1,1] to [0,1], without a second root algorithm.
        for degree in range(12):
            with self.subTest(degree=degree):
                observed = math.fsum(weight * node ** degree
                                     for node, weight in zip(self.nodes, self.weights))
                self.assertAlmostEqual(observed, 1.0 / (degree + 1), delta=4e-16)

    def test_binary_complements_and_kernel_mirrors_center_the_delay(self):
        self.assertEqual(len(self.phases), 6)
        self.assertTrue(all(len(phase) == 96 for phase in self.phases))
        self.assertTrue(math.isfinite(self.normalizer) and self.normalizer > 0.0)
        for index in range(6):
            with self.subTest(node=index):
                # Ordinary float addition can round an inexact pair to 1.0;
                # exact rational conversion detects that otherwise hidden defect.
                self.assertEqual(Fraction.from_float(self.nodes[index]) +
                                 Fraction.from_float(self.nodes[5 - index]), Fraction(1))
                self.assertEqual(self.weights[index].hex(), self.weights[5 - index].hex())
                self.assertEqual(tuple(value.hex() for value in self.phases[index]),
                                 tuple(value.hex() for value in self.phases[5 - index][::-1]))
        taps = [value for phase in self.phases for value in phase]
        self.assertTrue(all(math.isfinite(value) for value in taps))
        self.assertAlmostEqual(math.fsum(taps), 1.0, delta=5e-15)
        # The physical lag is k+1-a[j], not k or k+a[j]. This first moment
        # checks the causal cell indexing and the 48-frame symmetry center.
        mean_lag = math.fsum(value * (age + 1.0 - self.nodes[node])
                             for node, phase in enumerate(self.phases)
                             for age, value in enumerate(phase))
        self.assertAlmostEqual(mean_lag, 48.0, delta=1e-13)

    def test_committed_header_matches_deterministic_generator(self):
        self.assertEqual(generator.DEFAULT_OUTPUT.read_bytes(), self.header.encode("ascii"))
        code, output, errors = self.run_main("--check")
        self.assertEqual((code, errors), (0, ""))
        self.assertIn("is current", output)
        with localcontext() as context:
            context.prec = 7
            context.rounding = ROUND_DOWN
            context.traps[Inexact] = True
            self.assertEqual(generator.generate_header(), self.header)

    def test_generation_and_default_check_work_without_site_packages_from_other_cwd(self):
        with tempfile.TemporaryDirectory(prefix="rhodes quadrature ") as directory:
            root = Path(directory)
            output = root / "generated.hpp"
            process = subprocess.run(
                [sys.executable, "-S", str(GENERATOR), "--output", str(output)],
                cwd=root, capture_output=True, text=True, check=False)
            self.assertEqual(process.returncode, 0, process.stderr)
            self.assertEqual(output.read_bytes(), self.header.encode("ascii"))
            # No output override: the script must locate its own repository.
            process = subprocess.run([sys.executable, "-S", str(GENERATOR), "--check"],
                                     cwd=root, capture_output=True, text=True, check=False)
            self.assertEqual(process.returncode, 0, process.stderr)
            self.assertEqual(set(root.iterdir()), {output})

    def test_stale_and_missing_checks_leave_files_and_directories_untouched(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            stale = root / "stale.hpp"
            original = b"// Preserve this stale file.\n"
            stale.write_bytes(original)
            before = stale.stat()
            code, output, errors = self.run_main("--check", "--output", str(stale))
            after = stale.stat()
            self.assertEqual((code, output), (1, ""))
            self.assertIn("is stale", errors)
            self.assertEqual(stale.read_bytes(), original)
            self.assertEqual((before.st_mtime_ns, before.st_ino),
                             (after.st_mtime_ns, after.st_ino))
            missing = root / "missing-parent" / "missing.hpp"
            code, output, errors = self.run_main("--check", "--output", str(missing))
            self.assertEqual((code, output), (1, ""))
            self.assertIn("failed", errors)
            self.assertEqual(set(root.iterdir()), {stale})

    def test_generation_rejects_live_and_dangling_symlinks_without_mutation(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for live in (True, False):
                with self.subTest(live=live):
                    target = root / ("live-target.hpp" if live else "absent-target.hpp")
                    if live:
                        target.write_bytes(b"keep target bytes\n")
                    link = root / ("live-link.hpp" if live else "dangling-link.hpp")
                    try:
                        link.symlink_to(target)
                    except (OSError, NotImplementedError):
                        self.skipTest("Host cannot create symlinks")
                    original_link = link.readlink()
                    before = set(root.iterdir())
                    code, output, errors = self.run_main("--output", str(link))
                    self.assertEqual((code, output), (1, ""))
                    self.assertIn("Refusing symlink", errors)
                    self.assertTrue(link.is_symlink())
                    self.assertEqual(link.readlink(), original_link)
                    if live:
                        self.assertEqual(target.read_bytes(), b"keep target bytes\n")
                    else:
                        self.assertFalse(target.exists())
                    self.assertEqual(set(root.iterdir()), before)

    def test_failed_publication_does_not_create_parent_or_replace_directory(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            occupied = root / "occupied.hpp"
            occupied.mkdir()
            marker = occupied / "keep.txt"
            marker.write_bytes(b"preserve directory\n")
            for output_path in (root / "missing" / "output.hpp", occupied):
                with self.subTest(path=output_path):
                    code, output, errors = self.run_main("--output", str(output_path))
                    self.assertEqual((code, output), (1, ""))
                    self.assertIn("failed", errors)
                    self.assertEqual(set(root.iterdir()), {occupied})
                    self.assertEqual(marker.read_bytes(), b"preserve directory\n")


@unittest.skipUnless(HAVE_SCIPY, "Optional independent Q6 audit needs NumPy/SciPy")
class IndependentQuadratureFilterTests(unittest.TestCase):
    def test_scipy_rule_and_independent_continuous_kernel_recipe(self):
        import numpy as np
        from scipy.special import roots_legendre

        nodes, weights, phases, normalizer = parse_header(
            generator.DEFAULT_OUTPUT.read_text(encoding="ascii"))
        roots, gauss_weights = roots_legendre(6)
        reference_nodes, reference_weights = (roots + 1.0) / 2.0, gauss_weights / 2.0
        self.assertLessEqual(float(np.max(np.abs(nodes - reference_nodes))), 2e-15)
        self.assertLessEqual(float(np.max(np.abs(weights - reference_weights))), 2e-15)
        # Uses SciPy roots and NumPy's independent I0/sinc implementations;
        # neither the production Decimal kernel nor serialized nodes is reused.
        centered = np.arange(96)[None, :] + 1.0 - reference_nodes[:, None] - 48.0
        window = np.i0(9.0 * np.sqrt(1.0 - (centered / 48.0) ** 2)) / np.i0(9.0)
        raw = reference_weights[:, None] * 0.94 * np.sinc(0.94 * centered) * window
        reference_normalizer = math.fsum(float(value) for value in raw.reshape(-1))
        reference = raw / reference_normalizer
        self.assertAlmostEqual(normalizer, reference_normalizer, delta=5e-15)
        self.assertLessEqual(float(np.max(np.abs(np.asarray(phases) - reference))), 5e-15)


if __name__ == "__main__":
    unittest.main()
