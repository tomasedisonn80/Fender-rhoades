"""Fail-closed evidence checks for the standalone native legacy comparator."""

from copy import deepcopy
import importlib.util
import io
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch
from contextlib import redirect_stderr, redirect_stdout


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("compare_vst3_legacy", ROOT / "tools/compare_vst3_legacy.py")
comparison = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(comparison)


def fixture(*, variant="RhodesResearch", format_bits=32, partition="whole", mode="baseline"):
    model, processor, controller = comparison.IDENTITIES[variant]
    options = {"capture_id": "a" * 32, "mode": mode, "variant": variant, "format_bits": format_bits,
               "rate": 48000, "partition": partition, "workload": "dense"}
    points = comparison.expected_checkpoints(model, partition)
    for point in points:
        point["silence_flags"] = 3 if point["name"] == "reset_silence" else 0
    parameter_ids = [0, 1] + list(range(0x1000, 0x1010))
    metadata = {
        "capture_schema": 1, "fixture_version": 1, "complete": True,
        "capture_id": options["capture_id"], "mode": mode, "format_bits": format_bits,
        "sample_rate_hz": 48000, "partition": partition, "gain_workload": "dense", "byte_order": sys.byteorder,
        "processor_id": processor, "controller_id": controller, "model": model,
        "latency": 0 if model == 1 else 48, "tail_samples": 0xFFFFFFFF,
        "parameter_count": 18 if mode == "baseline" else 19,
        "legacy_parameters": [{"id": value, "title": "parameter", "units": "", "flags": 0,
                               "step_count": 0, "default_normalized": 0.5} for value in parameter_ids],
        "initial_state": comparison.state_hex(model), "checkpoints": points,
        "callbacks": 2 * len(comparison.blocks(partition)) + 3, "nonzero_left_samples": 4096,
        "output_frames": comparison.TOTAL_FRAMES, "note_events": 44, "pressure_events": 16,
        "command_points": 48,
        "expression_points": (4 + 2 * sum(1 if count == 1 else 2 for _, count in comparison.blocks(partition)))
        if mode == "explicit1" else 0,
        "epoch_nonzero_left_samples": [2048, 2048], "epoch_peak_abs": [0.25, 0.25],
    }
    sample = struct.Struct(("<" if sys.byteorder == "little" else ">") + ("ff" if format_bits == 32 else "dd"))
    audio = sample.pack(0.25, 0.25) * 4096 + sample.pack(0.0, 0.0) * 128
    return metadata, audio, options


class CaptureValidationTests(unittest.TestCase):
    def test_complete_records_for_all_variants_formats_partitions_and_modes(self):
        for variant in comparison.VARIANTS:
            for format_bits in comparison.FORMATS:
                for partition in comparison.PARTITIONS:
                    for mode in comparison.MODES:
                        with self.subTest(variant=variant, format=format_bits, partition=partition, mode=mode):
                            metadata, audio, options = fixture(variant=variant, format_bits=format_bits,
                                                               partition=partition, mode=mode)
                            result = comparison.validate_capture(metadata, audio, **options)
                            self.assertEqual(result["output_frames"], 4224)
                            self.assertEqual(result["nonzero_left_samples"], 4096)

    def test_missing_partial_and_oversized_audio_fail(self):
        metadata, audio, options = fixture()
        for invalid in (b"", audio[:-1], audio + b"\0"):
            with self.subTest(size=len(invalid)), self.assertRaises(comparison.ComparisonError):
                comparison.validate_capture(metadata, invalid, **options)

    def test_stale_identity_and_incomplete_metadata_fail(self):
        original, audio, options = fixture()
        for key, value in (("capture_id", "b" * 32), ("complete", False), ("parameter_count", 19),
                           ("output_frames", True), ("note_events", 0), ("expression_points", 1),
                           ("sample_rate_hz", 192000), ("processor_id", "0" * 32)):
            metadata = deepcopy(original)
            metadata[key] = value
            with self.subTest(key=key), self.assertRaises(comparison.ComparisonError):
                comparison.validate_capture(metadata, audio, **options)
        for missing in ("checkpoints", "nonzero_left_samples", "initial_state"):
            metadata = deepcopy(original)
            del metadata[missing]
            with self.subTest(missing=missing), self.assertRaises(comparison.ComparisonError):
                comparison.validate_capture(metadata, audio, **options)

    def test_silence_cannot_pass_forged_nonzero_counts(self):
        metadata, audio, options = fixture()
        with self.assertRaisesRegex(comparison.ComparisonError, "nonvacuous"):
            comparison.validate_capture(metadata, bytes(len(audio)), **options)

    def test_one_silent_epoch_cannot_pass(self):
        metadata, audio, options = fixture()
        audio = bytes(2048 * 8) + audio[2048 * 8:]
        with self.assertRaisesRegex(comparison.ComparisonError, "nonvacuous"):
            comparison.validate_capture(metadata, audio, **options)

    def test_raw_audio_recomputes_nonzero_peak_and_silence_evidence(self):
        original, audio, options = fixture()
        mutations = [lambda value: value.update(nonzero_left_samples=4095),
                     lambda value: value.update(epoch_peak_abs=[0.5, 0.5]),
                     lambda value: value.update(epoch_nonzero_left_samples=[2047, 2048]),
                     lambda value: value["checkpoints"][2].update(silence_flags=3)]
        for mutate in mutations:
            metadata = deepcopy(original)
            mutate(metadata)
            with self.assertRaises(comparison.ComparisonError):
                comparison.validate_capture(metadata, audio, **options)

    def test_every_state_and_checkpoint_extent_are_independently_required(self):
        original, audio, options = fixture(partition="ragged")
        mutations = [lambda value: value["checkpoints"].pop(),
                     lambda value: value["checkpoints"][3].update(frame=999),
                     lambda value: value["checkpoints"][3].update(state=comparison.state_hex(1)),
                     lambda value: value["checkpoints"][3].update(state=value["checkpoints"][3]["state"] + "00000000"),
                     lambda value: value["checkpoints"][3].update(frames=8193)]
        for mutate in mutations:
            metadata = deepcopy(original)
            mutate(metadata)
            with self.assertRaises(comparison.ComparisonError):
                comparison.validate_capture(metadata, audio, **options)

    def test_stereo_and_nonfinite_samples_fail(self):
        metadata, audio, options = fixture()
        for pair in ((0.25, 0.5), (float("nan"), float("nan")), (float("inf"), float("inf"))):
            invalid = struct.pack("=ff", *pair) + audio[8:]
            with self.subTest(pair=pair), self.assertRaises(comparison.ComparisonError):
                comparison.validate_capture(metadata, invalid, **options)

    def test_comparison_allows_only_declared_mode_differences(self):
        old_metadata, audio, options = fixture()
        new_metadata, _, _ = fixture(mode="explicit1")
        old = ({"passed": True, "mode": "baseline", "capture_id": "a" * 32}, old_metadata, audio)
        new = ({"passed": True, "mode": "explicit1", "capture_id": "b" * 32}, new_metadata, audio)
        self.assertTrue(comparison.compare_pair(old, new)["passed"])
        changed = deepcopy(new_metadata)
        changed["legacy_parameters"][0]["title"] = "changed"
        self.assertFalse(comparison.compare_pair(old, (new[0], changed, audio))["passed"])
        # Signed zero is valid audio but must remain bit-identical at unity.
        changed_audio = audio[:-8] + struct.pack("=ff", -0.0, -0.0)
        self.assertFalse(comparison.compare_pair(old, (new[0], new_metadata, changed_audio))["passed"])
        self.assertFalse(comparison.compare_pair(old, ({**new[0], "passed": False}, None, None))["passed"])

    def test_json_rejects_duplicates_nonfinite_and_trailing_records(self):
        for invalid in (b'{"complete":true,"complete":false}', b'{"peak":NaN}', b'{}{}', b'{'):
            with self.subTest(invalid=invalid), self.assertRaises(ValueError):
                comparison.strict_json(invalid)

    def test_retained_manifest_rejects_changed_missing_and_unexpected_files(self):
        record = {}
        files = {"commands.json": {"bytes": 1, "sha256": "commands"}}
        for kind in ("stdout", "stderr", "trace"):
            record.update({kind + "_path": kind, kind + "_bytes": 4, kind + "_sha256": kind})
            files[kind] = {"bytes": 4, "sha256": kind}
        manifest = {"files": files}
        self.assertTrue(comparison.raw_records_match_manifest([record], manifest))
        for mutate in (lambda value: value["files"].pop("trace"),
                       lambda value: value["files"]["trace"].update(bytes=3),
                       lambda value: value["files"]["trace"].update(sha256="changed"),
                       lambda value: value["files"].update(stale={"bytes": 0, "sha256": "stale"})):
            modified = deepcopy(manifest)
            mutate(modified)
            self.assertFalse(comparison.raw_records_match_manifest([record], modified))


class BundleManifestTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.addCleanup(self.temporary.cleanup)

    def make_bundles(self, target):
        for variant in comparison.VARIANTS:
            if target == "win32":
                relative = Path(variant + ".vst3") / "Contents" / "x86_64-win" / (variant + ".vst3")
            elif target == "darwin":
                relative = Path(variant + ".vst3") / "Contents" / "MacOS" / variant
            else:
                relative = Path(variant + ".vst3") / "Contents" / "x86_64-linux" / (variant + ".so")
            path = self.root / relative
            path.parent.mkdir(parents=True)
            path.write_bytes(("synthetic inventory fixture " + variant).encode())
        inventory = comparison.bundle_inventory(self.root)
        manifest = {"source_commit": comparison.BASELINE_REVISION, "modules": [
            {"path": item["native_binary"], "sha256": item["files"][item["native_binary"]]["sha256"]}
            for item in inventory.values()]}
        (self.root / "manifest.json").write_text(json.dumps(manifest))
        return inventory, manifest

    def test_native_inventory_layouts_are_supported(self):
        for target in ("linux", "win32", "darwin"):
            with self.subTest(target=target), tempfile.TemporaryDirectory() as directory:
                self.root = Path(directory)
                with patch.object(comparison.sys, "platform", target):
                    inventory, manifest = self.make_bundles(target)
                    self.assertEqual(comparison.validate_baseline_manifest(self.root, inventory), manifest)

    def test_missing_wrong_revision_duplicate_and_changed_module_manifest_fail(self):
        inventory, original = self.make_bundles(sys.platform)
        mutations = [lambda value: value.update(source_commit="0" * 40),
                     lambda value: value["modules"].pop(),
                     lambda value: value["modules"][0].update(sha256="0" * 64),
                     lambda value: value["modules"].__setitem__(1, value["modules"][0])]
        for mutate in mutations:
            manifest = deepcopy(original)
            mutate(manifest)
            (self.root / "manifest.json").write_text(json.dumps(manifest))
            with self.assertRaises(comparison.ComparisonError):
                comparison.validate_baseline_manifest(self.root, inventory)
        (self.root / "manifest.json").unlink()
        with self.assertRaises(comparison.ComparisonError):
            comparison.validate_baseline_manifest(self.root, inventory)

    def test_missing_native_binary_fails(self):
        inventory, _ = self.make_bundles(sys.platform)
        (self.root / inventory[comparison.VARIANTS[0]]["native_binary"]).unlink()
        with self.assertRaises(comparison.ComparisonError):
            comparison.bundle_inventory(self.root)


class DriverFailureTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.addCleanup(self.temporary.cleanup)

    def test_existing_capture_files_are_never_reused(self):
        output, diagnostics, trace = (self.root / name for name in ("stdout", "stderr", "trace"))
        output.write_bytes(b'{"complete":true}')
        with self.assertRaisesRegex(comparison.ComparisonError, "pre-existing"):
            comparison.run_bounded([sys.executable, "-c", "pass"], output, diagnostics, trace, 10, 1)

    def test_oversized_output_is_failed_and_retained_only_within_bound(self):
        output, diagnostics, trace = (self.root / name for name in ("stdout", "stderr", "trace"))
        code, error = comparison.run_bounded(
            [sys.executable, "-c", f"import sys; sys.stdout.write('x'*{comparison.MAX_METADATA_BYTES + 1})"],
            output, diagnostics, trace, 10, 5)
        self.assertIsNotNone(error)
        self.assertLessEqual(output.stat().st_size, comparison.MAX_METADATA_BYTES)

    def test_timeout_is_failure(self):
        output, diagnostics, trace = (self.root / name for name in ("stdout", "stderr", "trace"))
        code, error = comparison.run_bounded([sys.executable, "-c", "import time; time.sleep(2)"],
                                             output, diagnostics, trace, 10, 0.02)
        self.assertNotEqual(code, 0)
        self.assertIn("timeout", error)

    def check_raw_mutation_after_validation(self, kind):
        metadata, audio, _ = fixture()
        paths, snapshots = {}, {}
        validate = comparison.validate_capture

        def captured(command, stdout, stderr, trace, audio_limit, timeout):
            metadata["capture_id"] = command[-1]
            snapshots.update(stdout=json.dumps(metadata).encode(), trace=audio)
            paths.update(stdout=stdout, trace=trace)
            stdout.write_bytes(snapshots["stdout"])
            stderr.write_bytes(b"")
            trace.write_bytes(audio)
            return 0, None

        def validate_then_replace(metadata, audio, **options):
            result = validate(metadata, audio, **options)
            paths[kind].write_bytes(bytes(len(audio)) if kind == "trace" else b'{"complete":true}\n')
            return result

        with patch.object(comparison, "run_bounded", side_effect=captured), \
                patch.object(comparison, "validate_capture", side_effect=validate_then_replace):
            record, _, _ = comparison.one_capture(
                Path(sys.executable), self.root / "bundles", self.root,
                ("RhodesResearch", 32, 48000, "whole", "dense"), "baseline", 1)
        self.assertTrue(record["passed"])
        self.assertEqual(record[kind + "_sha256"], comparison.sha256(snapshots[kind]).hexdigest())
        self.assertEqual(record[kind + "_bytes"], len(snapshots[kind]))
        self.assertNotEqual(record[kind + "_sha256"], comparison.file_hash(paths[kind]))
        (self.root / "commands.json").write_text(json.dumps([record]))
        manifest = {"files": {path.name: {"bytes": path.stat().st_size, "sha256": comparison.file_hash(path)}
                              for path in self.root.iterdir()}}
        # main's final publication gate must reject the changed retained file,
        # even though its earlier byte snapshot was a valid complete capture.
        self.assertFalse(comparison.raw_records_match_manifest([record], manifest))

    def test_retained_trace_is_bound_to_compared_bytes(self):
        self.check_raw_mutation_after_validation("trace")

    def test_retained_metadata_is_bound_to_parsed_bytes(self):
        self.check_raw_mutation_after_validation("stdout")

    def test_invalid_timeout_replaces_stale_pass_without_launching_children(self):
        output = self.root / "results"
        previous = output / "run-previous"
        previous.mkdir(parents=True)
        old_report = b'{"status":"passed","run_id":"previous-success"}'
        (previous / "report.json").write_bytes(old_report)
        run_ids = set()
        for invalid in ("0", "61", "nan", "inf", "-inf"):
            with self.subTest(timeout=invalid):
                (output / "report.json").write_bytes(old_report)
                with patch.object(comparison.subprocess, "Popen") as process, \
                        patch.object(comparison, "one_capture") as capture, \
                        redirect_stdout(io.StringIO()), redirect_stderr(io.StringIO()):
                    code = comparison.main([
                        "--capture", sys.executable, "--baseline-root", str(self.root / "old"),
                        "--current-root", str(self.root / "new"), "--output-dir", str(output),
                        "--timeout=" + invalid])
                process.assert_not_called()
                capture.assert_not_called()
                report = json.loads((output / "report.json").read_text())
                self.assertEqual(code, 1)
                self.assertEqual(report["status"], "failed")
                self.assertIn("timeout", report["error"])
                self.assertFalse(report["summary"]["passed"])
                self.assertFalse(report["summary"]["complete"])
                self.assertEqual(report["captures"], [])
                self.assertNotIn(report["run_id"], run_ids | {"previous-success"})
                run_ids.add(report["run_id"])
                raw = output / report["raw_directory"]
                self.assertEqual(json.loads((raw / "report.json").read_text()), report)
                self.assertTrue((raw / "manifest.json").is_file())
                self.assertEqual((previous / "report.json").read_bytes(), old_report)

    def test_missing_inputs_replace_stale_pass_with_fresh_failure(self):
        output = self.root / "results"
        output.mkdir()
        (output / "report.json").write_text('{"status":"passed","run_id":"stale"}')
        with redirect_stdout(io.StringIO()), redirect_stderr(io.StringIO()):
            code = comparison.main(["--capture", sys.executable, "--baseline-root", str(self.root / "missing-baseline"),
                                    "--current-root", str(self.root / "missing-current"), "--output-dir", str(output)])
        report = json.loads((output / "report.json").read_text())
        self.assertEqual(code, 1)
        self.assertEqual(report["status"], "failed")
        self.assertNotEqual(report["run_id"], "stale")
        self.assertFalse(report["summary"]["passed"])
        self.assertFalse(report["summary"]["complete"])
        self.assertEqual(report["captures"], [])
        self.assertTrue((output / report["raw_directory"] / "manifest.json").is_file())


if __name__ == "__main__":
    unittest.main()
