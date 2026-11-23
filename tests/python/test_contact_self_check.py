"""The actual oracle self-check must reject bad results under -O and -OO.

Only the scalar prefix is exercised with standard-library fakes. A sentinel
stops the independent matrix-exponential phase; these are validation-flow
regressions, not substitutes for the optional real numerical self-check.
"""
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
SENTINEL = "TEST SENTINEL: reached independent preload phase"
MODES = ((), ("-O",), ("-OO",))
FAULTS = {
    "conservative_impulse": "conservative impact hammer impulse",
    "conservative_hammer_velocity": "conservative impact hammer velocity",
    "conservative_modal_velocity": "conservative impact modal velocity",
    "conservative_energy": "conservative impact energy",
    "damped_rebound": "damped impact rebound",
    "damped_loss": "damped impact loss",
    "clamp_impulse": "forced clamp",
    "clamp_loss": "forced clamp",
    "nonfinite_energy": "conservative impact energy",
    "nonfinite_impulse": "conservative impact hammer impulse",
}


def fault_probe(case, source, output=None):
    """Run the production main() with controlled numerical observations."""
    spec = importlib.util.spec_from_file_location("self_check_probe_target", source)
    if spec is None or spec.loader is None:
        raise RuntimeError("Cannot load contact auditor")
    audit = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = audit
    spec.loader.exec_module(audit)

    def stop_at_preload(_):
        raise RuntimeError(SENTINEL)

    # Numerical library imports are deliberately unavailable/unnecessary here.
    # A fixed scalar-root result lets us inject inconsistent independent-oracle
    # outputs. Real root accuracy is checked by the optional numerical tool.
    scipy = ModuleType("scipy")
    optimize = ModuleType("scipy.optimize")
    optimize.brentq = lambda *_args, **_kwargs: -0.3
    scipy.optimize = optimize
    sys.modules["scipy"] = scipy
    sys.modules["scipy.optimize"] = optimize
    audit.numerics = lambda: (SimpleNamespace(diag=stop_at_preload), None, None, None)

    def exact_free(fixture):
        if fixture.name == "conservative_impact":
            values = {"hammer_impulse_ns": 0.0032,
                      "hammer_velocity_m_per_s": 0.6,
                      "v_m_per_s": [1.6, 0.0], "energy_j": 0.004}
            if case == "conservative_impulse":
                values["hammer_impulse_ns"] = 0.0
            elif case == "conservative_hammer_velocity":
                values["hammer_velocity_m_per_s"] = 0.0
            elif case == "conservative_modal_velocity":
                values["v_m_per_s"] = [0.0, 0.0]
            elif case == "conservative_energy":
                values["energy_j"] = 0.0
            elif case == "nonfinite_energy":
                values["energy_j"] = math.nan
            elif case == "nonfinite_impulse":
                values["hammer_impulse_ns"] = math.inf
        elif fixture.name == "forced_clamp":
            values = {"hammer_impulse_ns": 0.0, "hammer_loss_j": 0.0001}
            if case == "clamp_impulse":
                values["hammer_impulse_ns"] = 1.0
            elif case == "clamp_loss":
                values["hammer_loss_j"] = 0.0
        else:
            raise RuntimeError("Unexpected exact fixture in scalar-prefix test")
        return SimpleNamespace(sample=lambda _time: values)

    def radau_oracle(fixture):
        if fixture.name != "damped_impact":
            raise RuntimeError("Unexpected Radau fixture in scalar-prefix test")
        values = {"hammer_velocity_m_per_s": -0.3,
                  "v_m_per_s": [0.0, 0.0], "hammer_loss_j": 0.000728}
        if case == "damped_rebound":
            values["hammer_velocity_m_per_s"] = 0.0
        elif case == "damped_loss":
            values["hammer_loss_j"] = 0.0
        return SimpleNamespace(fixture=fixture, sample=lambda _time: values,
                               evaluation_count=0)

    audit.exact_free = exact_free
    audit.radau_oracle = radau_oracle
    sys.argv = [str(source), "--self-check"]
    if output is not None:
        sys.argv += ["--output", str(output)]
    return audit.main()


class ContactSelfCheckTests(unittest.TestCase):
    def probe(self, case, mode, output=None):
        argv = [sys.executable, *mode, "-S", str(THIS_FILE),
                "--fault-probe", case, str(SCRIPT)]
        if output is not None:
            argv.append(str(output))
        completed = subprocess.run(argv, capture_output=True, text=True, timeout=15)
        self.assertEqual(completed.returncode, 1, completed.stderr)
        self.assertEqual(completed.stderr, "")
        report = json.loads(completed.stdout)
        self.assertEqual(report["status"], "fail", report)
        if output is not None:
            self.assertEqual(report["report"], str(output))
            report = json.loads(output.read_text(encoding="utf-8"))
        self.assertEqual(report["status"], "fail", report)
        self.assertIn("error", report)
        return report

    def check_faults(self, cases):
        for case in cases:
            for mode in MODES:
                with self.subTest(case=case, mode=mode):
                    report = self.probe(case, mode)
                    self.assertIn("Oracle self-check failed:", report["error"])
                    self.assertIn(FAULTS[case], report["error"])
                    self.assertNotIn(SENTINEL, report["error"])

    def test_valid_scalar_prefix_reaches_independent_preload_phase(self):
        for mode in MODES:
            with self.subTest(mode=mode):
                self.assertEqual(self.probe("valid", mode)["error"], SENTINEL)

    def test_bad_conservative_oracle_results_reject(self):
        self.check_faults(("conservative_impulse", "conservative_hammer_velocity",
                           "conservative_modal_velocity", "conservative_energy"))

    def test_bad_damped_oracle_results_reject(self):
        self.check_faults(("damped_rebound", "damped_loss"))

    def test_bad_clamp_oracle_results_reject(self):
        self.check_faults(("clamp_impulse", "clamp_loss"))

    def test_nonfinite_oracle_results_reject(self):
        self.check_faults(("nonfinite_energy", "nonfinite_impulse"))

    def test_optimized_failure_is_written_as_structured_json(self):
        with tempfile.TemporaryDirectory() as directory:
            for index, mode in enumerate(MODES):
                with self.subTest(mode=mode):
                    output = Path(directory) / f"failure-{index}.json"
                    report = self.probe("conservative_energy", mode, output)
                    self.assertIn(FAULTS["conservative_energy"], report["error"])
                    self.assertNotIn(SENTINEL, report["error"])


if __name__ == "__main__":
    if len(sys.argv) >= 4 and sys.argv[1] == "--fault-probe":
        raise SystemExit(fault_probe(sys.argv[2], Path(sys.argv[3]),
                                     Path(sys.argv[4]) if len(sys.argv) == 5 else None))
    unittest.main()
