"""Fault injection through the actual matrix/refinement self-check and CLI.

These standard-library fakes test rejection and optimization behavior, not
numerical accuracy. Matrix construction/solving returns deliberate placeholders;
exponentiation is identity and quadrature is zero. Scalar vector operations and
NaN-propagating maxima keep production comparison expressions active. Optional
real NumPy/SciPy self-checks remain necessary to validate the mathematics.
"""
from dataclasses import replace
import importlib.util
import json
import math
from pathlib import Path
import subprocess
import sys
import tempfile
from types import ModuleType, SimpleNamespace
import unittest


ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "tools/validate_contact_reference.py"
THIS_FILE = Path(__file__).resolve()
MODES = ((), ("-O",), ("-OO",))
FAULTS = {
    "position": "preload position",
    "position_nan": "preload position",
    "velocity": "preload velocity",
    "velocity_inf": "preload velocity",
    "loss": "preload modal loss",
    "loss_nan": "preload modal loss",
    "compression_zero": "strictly positive compression",
    "compression_negative": "strictly positive compression",
    "refinement_middle": "independent Radau refinement",
    "refinement_last": "independent Radau refinement",
    "refinement_nan_first": "independent Radau refinement",
    "refinement_nan_middle": "independent Radau refinement",
    "refinement_nan_last": "independent Radau refinement",
    "refinement_inf_middle": "independent Radau refinement",
}


class Vector(list):
    def __add__(self, other):
        return Vector(a + b for a, b in zip(self, other))

    def __sub__(self, other):
        return Vector(a - b for a, b in zip(self, other))

    def __mul__(self, scalar):
        return Vector(value * scalar for value in self)

    __rmul__ = __mul__

    def __getitem__(self, item):
        value = super().__getitem__(item)
        return Vector(value) if isinstance(item, slice) else value

    def __matmul__(self, other):
        if isinstance(other, Matrix):
            return Vector(self)
        return sum(a * b for a, b in zip(self, other))

    def tolist(self):
        return list(self)


class Matrix:
    """Opaque setup object; multiplication on vectors acts as identity."""
    def __add__(self, _other):
        return self

    def __mul__(self, _scalar):
        return self

    __rmul__ = __mul__

    def __neg__(self):
        return self

    def __matmul__(self, vector):
        return Vector(vector)


class Concatenate:
    def __getitem__(self, vectors):
        return Vector(value for vector in vectors for value in vector)


def maximum(values):
    """Match numpy.max's relevant NaN propagation, unlike Python's max."""
    values = list(values)
    return math.nan if any(math.isnan(value) for value in values) else max(values)


def fault_probe(case, source, output=None):
    spec = importlib.util.spec_from_file_location("matrix_self_check_target", source)
    if spec is None or spec.loader is None:
        raise RuntimeError("Cannot load contact auditor")
    audit = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = audit
    spec.loader.exec_module(audit)

    # No external numerical library is imported by this test process.
    scipy = ModuleType("scipy")
    optimize = ModuleType("scipy.optimize")
    optimize.brentq = lambda *_args, **_kwargs: -0.3
    scipy.optimize = optimize
    sys.modules["scipy"] = scipy
    sys.modules["scipy.optimize"] = optimize
    np = SimpleNamespace(
        diag=lambda _values: Matrix(), array=Vector,
        outer=lambda *_args: Matrix(), block=lambda _parts: Matrix(),
        zeros=lambda _shape: Matrix(), eye=lambda _size: Matrix(),
        linalg=SimpleNamespace(solve=lambda _matrix, rhs:
                               Matrix() if isinstance(rhs, Matrix)
                               else Vector(0.0 for _value in rhs)),
        r_=Concatenate(), max=maximum,
        abs=lambda values: Vector(abs(value) for value in values),
        linspace=lambda start, stop, count:
            [start + (stop - start) * index / (count - 1)
             for index in range(count)])
    audit.numerics = lambda: (np, None, lambda _matrix: Matrix(),
                              lambda *_args, **_kwargs: (0.0, 0.0))
    original_fixtures = audit.fixtures

    def fixtures():
        result = original_fixtures()
        if case.startswith("compression_"):
            result["preload_linear_plus"] = replace(
                result["preload_linear_plus"],
                face_offset=0.0 if case == "compression_zero" else 1.0)
        return result

    audit.fixtures = fixtures

    def exact_free(fixture):
        if fixture.name == "conservative_impact":
            values = {"hammer_impulse_ns": 0.0032,
                      "hammer_velocity_m_per_s": 0.6,
                      "v_m_per_s": [1.6, 0.0], "energy_j": 0.004}
        elif fixture.name == "forced_clamp":
            values = {"hammer_impulse_ns": 0.0, "hammer_loss_j": 0.0001}
        else:
            raise RuntimeError("Unexpected exact fixture in matrix-phase test")
        return SimpleNamespace(sample=lambda _time: values)

    def radau_oracle(fixture, tight=False):
        if fixture.name == "damped_impact":
            values = {"hammer_velocity_m_per_s": -0.3,
                      "v_m_per_s": [0.0, 0.0], "hammer_loss_j": 0.000728}
            return SimpleNamespace(fixture=fixture, sample=lambda _time: values,
                                   evaluation_count=0)
        if fixture.name == "preload_linear_plus":
            values = {"q_m": list(fixture.q), "v_m_per_s": list(fixture.v),
                      "modal_loss_j": 0.0}
            if case in ("position", "position_nan"):
                values["q_m"][0] += 1.0 if case == "position" else math.nan
            elif case in ("velocity", "velocity_inf"):
                values["v_m_per_s"][1] += 1.0 if case == "velocity" else math.inf
            elif case in ("loss", "loss_nan"):
                values["modal_loss_j"] = 1.0 if case == "loss" else math.nan
            return SimpleNamespace(sample=lambda _time: values)
        if fixture.name != "preload_plus":
            raise RuntimeError("Unexpected Radau fixture in matrix-phase test")

        def state(time):
            values = Vector([0.0] * 13)
            if tight or not case.startswith("refinement_"):
                return values
            index = round(time / fixture.duration * 1000)
            target = 0 if case.endswith("first") else 1000 if case.endswith("last") else 500
            if index == target:
                # Last ledger component ensures the whole state is compared.
                values[-1] = math.nan if "_nan_" in case else math.inf if "_inf_" in case else 1.0
            return values

        return SimpleNamespace(state=state)

    audit.exact_free = exact_free
    audit.radau_oracle = radau_oracle
    sys.argv = [str(source), "--self-check"]
    if output is not None:
        sys.argv += ["--output", str(output)]
    return audit.main()


class ContactMatrixSelfCheckTests(unittest.TestCase):
    def probe(self, case, mode, output=None):
        argv = [sys.executable, *mode, "-S", str(THIS_FILE),
                "--fault-probe", case, str(SCRIPT)]
        if output is not None:
            argv.append(str(output))
        completed = subprocess.run(argv, capture_output=True, text=True, timeout=15)
        self.assertEqual(completed.stderr, "")
        self.assertEqual(completed.returncode, 0 if case == "valid" else 1,
                         completed.stdout)
        report = json.loads(completed.stdout)
        if output is not None:
            self.assertEqual(report["report"], str(output))
            report = json.loads(output.read_text(encoding="utf-8"))
        self.assertEqual(report["status"], "pass" if case == "valid" else "fail")
        return report

    def check_faults(self, cases):
        for case in cases:
            for mode in MODES:
                with self.subTest(case=case, mode=mode):
                    error = self.probe(case, mode)["error"]
                    self.assertIn("Oracle self-check failed:", error)
                    self.assertIn(FAULTS[case], error)

    def test_consistent_controls_reach_success(self):
        for mode in MODES:
            with self.subTest(mode=mode):
                checks = self.probe("valid", mode)["oracle_self_check"]["checks"]
                self.assertIn("preload_expm_vs_radau", checks)
                self.assertIn("independent_radau_refinement", checks)

    def test_matrix_position_and_velocity_disagreement_reject(self):
        self.check_faults(("position", "velocity"))

    def test_matrix_nonfinite_comparisons_reject(self):
        self.check_faults(("position_nan", "velocity_inf", "loss_nan"))

    def test_quadrature_loss_disagreement_rejects(self):
        self.check_faults(("loss",))

    def test_uncertified_compression_rejects(self):
        self.check_faults(("compression_zero", "compression_negative"))

    def test_refinement_middle_and_final_ledger_disagreement_reject(self):
        self.check_faults(("refinement_middle", "refinement_last"))

    def test_refinement_nonfinite_first_middle_final_samples_reject(self):
        self.check_faults(("refinement_nan_first", "refinement_nan_middle",
                           "refinement_nan_last", "refinement_inf_middle"))

    def test_nonfinite_refinement_failure_is_written_as_json(self):
        with tempfile.TemporaryDirectory() as directory:
            for index, mode in enumerate(MODES):
                with self.subTest(mode=mode):
                    output = Path(directory) / f"failure-{index}.json"
                    error = self.probe("refinement_nan_last", mode, output)["error"]
                    self.assertIn("independent Radau refinement", error)


if __name__ == "__main__":
    if len(sys.argv) >= 4 and sys.argv[1] == "--fault-probe":
        raise SystemExit(fault_probe(sys.argv[2], Path(sys.argv[3]),
                                     Path(sys.argv[4]) if len(sys.argv) == 5 else None))
    unittest.main()
