"""Independent tiny pixel fixtures and adversarial ZIP/manifest mutations."""

from contextlib import redirect_stderr, redirect_stdout
import io
import json
from pathlib import Path
import stat
import sys
import tempfile
import unittest
import warnings
import zipfile

try:
    from PIL import Image, PngImagePlugin
except ModuleNotFoundError as error:
    raise unittest.SkipTest("photo evidence checks require the optional developer Pillow installation") from error

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import pack_photo_evidence as packer


def file_snapshot(root):
    return {str(path.relative_to(root)): (path.read_bytes(), path.stat().st_mtime_ns)
            for path in root.rglob("*") if path.is_file()}


class PhotoEvidenceTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.source = self.root / "source"
        self.source.mkdir()
        (self.source / "run01").mkdir()
        (self.source / "run02").mkdir()
        self.base = bytes([0, 1, 254, 255, 10, 120, 60, 99, 110, 128, 220, 250])
        self.held = bytes([255, 0, 1, 0, 200, 2, 59, 100, 110, 255, 0, 249])
        image = Image.frombytes("RGB", (2, 2), self.base)
        image.save(self.source / "reference.png")
        metadata = PngImagePlugin.PngInfo()
        metadata.add_text("fixture", "original metadata is deliberately not reconstructed")
        for run in ("run01", "run02"):
            image.save(self.source / run / "idle-maximum-2x2.png", pnginfo=metadata)
            Image.frombytes("RGB", (2, 2), self.held).save(
                self.source / run / "white-held-midi-028.png", pnginfo=metadata)
            Image.new("L", (2, 2), 255).save(self.source / run / "black-interior-mask.png")
            (self.source / run / "outcome.json").write_bytes(b'{"result":"synthetic fixture"}\n')
            (self.source / run / "editorhost.log").write_bytes(b"raw\r\nlog\x00\xff")
        (self.source / "provenance.json").write_bytes(b'{"source_hash":"fixture"}\n')
        self.archive = self.root / "evidence.zip"
        self.sidecar = self.root / "evidence.manifest.json"

    def pack(self):
        return packer.pack(self.source, self.archive, ["run01", "run02"],
                           ["provenance.json"], "reference.png")

    def mutate(self, callback):
        with zipfile.ZipFile(self.archive) as archive:
            entries = [(item, archive.read(item)) for item in archive.infolist()]
        manifest = json.loads(next(raw for item, raw in entries if item.filename == "manifest.json"))
        callback(manifest, entries)
        target = self.root / "mutated.zip"
        with warnings.catch_warnings():
            warnings.simplefilter("ignore", UserWarning)
            with zipfile.ZipFile(target, "w") as archive:
                for item, raw in entries:
                    archive.writestr(item, packer.manifest_bytes(manifest)
                                     if item.filename == "manifest.json" else raw)
        return target

    def assert_rejected(self, archive, pattern):
        output = self.root / "restored"
        with self.assertRaisesRegex((packer.EvidenceError, KeyError), pattern):
            packer.unpack(archive, output)
        self.assertFalse(output.exists())
        self.assertEqual([], list(self.root.glob("photo-unpack-*")))

    def test_roundtrip_is_bytewise_rgb_exact_modulo_wrap_and_inputs_unchanged(self):
        before = file_snapshot(self.source)
        result = self.pack()
        self.assertEqual(6, result["images"])
        self.assertEqual(5, result["raw_files"])
        self.assertEqual(2, result["baselines"])
        self.assertTrue(result["originals_compared_bytewise"])
        with zipfile.ZipFile(self.archive) as archive:
            manifest_raw = archive.read("manifest.json")
            self.assertEqual(manifest_raw, self.sidecar.read_bytes())
            manifest = json.loads(manifest_raw)
            image = next(item for item in manifest["images"] if "white-held" in item["path"])
            with Image.open(io.BytesIO(archive.read(image["delta"]["member"]))) as delta:
                self.assertEqual(bytes((value - base) % 256 for value, base in
                                       zip(self.held, self.base)), delta.tobytes())
            self.assertEqual("reference.png", next(item["source_path"] for item in
                             manifest["baselines"] if item["source_mode"] == "RGB"))
        output = self.root / "restored"
        packer.unpack(self.archive, output, self.source, self.sidecar)
        packer.verify(self.archive, self.source, self.sidecar)
        for name, (raw, _) in before.items():
            if name == "reference.png":
                continue
            if name.endswith(".png"):
                with Image.open(io.BytesIO(raw)) as original, Image.open(output / name) as restored:
                    self.assertEqual(original.size, restored.size)
                    self.assertEqual(original.convert("RGB").tobytes(), restored.tobytes())
            else:
                self.assertEqual(raw, (output / name).read_bytes())
        self.assertNotEqual((self.source / "run01/white-held-midi-028.png").read_bytes(),
                            (output / "run01/white-held-midi-028.png").read_bytes())
        self.assertEqual(before, file_snapshot(self.source))

    def test_changed_delta_payload_is_rejected(self):
        self.pack()
        def corrupt(manifest, entries):
            name = manifest["images"][0]["delta"]["member"]
            for index, (item, raw) in enumerate(entries):
                if item.filename == name:
                    entries[index] = item, raw[:-1] + bytes([raw[-1] ^ 1])
        self.assert_rejected(self.mutate(corrupt), "payload SHA-256 mismatch")

    def test_forged_reconstruction_hash_is_rejected(self):
        self.pack()
        self.assert_rejected(self.mutate(lambda manifest, entries:
                             manifest["images"][0].update(rgb_sha256="0" * 64)),
                             "decoded RGB SHA-256 mismatch")

    def test_changed_baseline_dimensions_are_rejected(self):
        self.pack()
        self.assert_rejected(self.mutate(lambda manifest, entries:
                             manifest["baselines"][0].update(width=3)),
                             "image dimension mismatch")

    def test_png_cannot_be_reclassified_to_bypass_rgb_checks(self):
        self.pack()
        def reclassify(manifest, entries):
            manifest["files"].append(manifest["images"].pop())
        self.assert_rejected(self.mutate(reclassify), "image/raw path classification mismatch")

    def test_changed_raw_attachment_binding_is_rejected(self):
        self.pack()
        self.assert_rejected(self.mutate(lambda manifest, entries:
                             manifest["files"][0].update(original_file_sha256="0" * 64)),
                             "raw file binding mismatch")

    def test_changed_original_file_is_detected(self):
        self.pack()
        (self.source / "run01/editorhost.log").write_bytes(b"changed")
        with self.assertRaisesRegex(packer.EvidenceError, "original file hash/size mismatch"):
            packer.verify(self.archive, self.source)

    def test_traversal_absolute_drive_and_backslash_paths_are_rejected(self):
        self.pack()
        for path in ("../escaped.png", "/escaped.png", "C:/escaped.png", "run01\\escaped.png",
                     "run01/../escaped.png", "run01//escaped.png"):
            with self.subTest(path=path):
                self.assert_rejected(self.mutate(lambda manifest, entries:
                                     manifest["images"][0].update(path=path)), "unsafe relative path")
        self.assertFalse((self.root / "escaped.png").exists())

    def test_extra_archive_traversal_member_is_rejected(self):
        self.pack()
        self.assert_rejected(self.mutate(lambda manifest, entries:
                             entries.append((zipfile.ZipInfo("../escaped"), b"escape"))),
                             "unsafe relative path")

    def test_zip_symlink_is_rejected(self):
        self.pack()
        def symlink(manifest, entries):
            item = zipfile.ZipInfo("symlink")
            item.external_attr = (stat.S_IFLNK | 0o777) << 16
            entries.append((item, b"/tmp"))
        self.assert_rejected(self.mutate(symlink), "non-regular ZIP member")

    def test_duplicate_and_unreferenced_members_are_rejected(self):
        self.pack()
        self.assert_rejected(self.mutate(lambda manifest, entries: entries.append(entries[0])),
                             "duplicate or truncated ZIP name")
        self.assert_rejected(self.mutate(lambda manifest, entries:
                             entries.append((zipfile.ZipInfo("unreferenced"), b"unused"))),
                             "unreferenced archive members")

    def test_output_path_collisions_are_rejected(self):
        self.pack()
        for replacement in ("RUN01/BLACK-INTERIOR-MASK.PNG", "run01/black-interior-mask.png/child.png"):
            with self.subTest(replacement=replacement):
                self.assert_rejected(self.mutate(lambda manifest, entries:
                                     manifest["images"][1].update(path=replacement)),
                                     "duplicate output path|path conflict")

    def test_refuses_existing_outputs_and_symlink_input(self):
        self.pack()
        before = self.archive.read_bytes()
        with self.assertRaisesRegex(packer.EvidenceError, "output already exists"):
            self.pack()
        self.assertEqual(before, self.archive.read_bytes())
        output = self.root / "existing"
        output.mkdir()
        with self.assertRaisesRegex(packer.EvidenceError, "output already exists"):
            packer.unpack(self.archive, output)
        (self.source / "run01/link.log").symlink_to(self.source / "run01/editorhost.log")
        with self.assertRaisesRegex(packer.EvidenceError, "symlink input refused"):
            packer.pack(self.source, self.root / "second.zip", ["run01"])

    def test_refuses_nonmatching_reference_and_alpha(self):
        Image.new("RGB", (2, 2), (1, 2, 3)).save(self.source / "reference.png")
        with self.assertRaisesRegex(packer.EvidenceError, "not RGB-identical"):
            self.pack()
        Image.new("RGBA", (2, 2), (1, 2, 3, 128)).save(self.source / "run01/alpha.png")
        with self.assertRaisesRegex(packer.EvidenceError, "alpha/transparency"):
            packer.pack(self.source, self.archive, ["run01"])

    def test_external_manifest_mismatch_is_rejected(self):
        self.pack()
        self.sidecar.write_bytes(b"{}\n")
        with self.assertRaisesRegex(packer.EvidenceError, "independent manifest differs"):
            packer.verify(self.archive, independent_manifest=self.sidecar)

    def test_cli_reports_malformed_archive_without_traceback(self):
        self.archive.write_bytes(b"broken ZIP")
        error = io.StringIO()
        with redirect_stderr(error), redirect_stdout(io.StringIO()):
            result = packer.main(["verify", str(self.archive)])
        self.assertEqual(1, result)
        self.assertIn("photo evidence error:", error.getvalue())
        self.assertNotIn("Traceback", error.getvalue())


if __name__ == "__main__":
    unittest.main()
