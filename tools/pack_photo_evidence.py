#!/usr/bin/env python3
"""Pack native screenshots as reversible RGB PNG deltas, without changing inputs.

Requires the already-used Pillow package; the container is ordinary ZIP. Example::

    python3 tools/pack_photo_evidence.py pack INPUT OUTPUT.zip \
        --run native-run01 --run native-run02 \
        --reference snapshot/Fender-rhoades/plugins/vst3/resources/rhodes-suitcase.png \
        --include snapshot-provenance.json --include snapshot02-provenance.json
    python3 tools/pack_photo_evidence.py verify OUTPUT.zip --originals INPUT
    python3 tools/pack_photo_evidence.py unpack OUTPUT.zip RESTORED --originals INPUT

OUTPUT.zip and its independent OUTPUT.manifest.json must be new files outside
INPUT. RESTORED must not exist. Runs and includes are relative to INPUT; includes
may be files or directories. All selected PNGs are retained. Other files (such as
outcome JSON, host logs and source-provenance manifests) are retained byte-exact.

For each size and original mode, use an actual idle capture as the baseline, or
the first image if no idle exists. A supplied reference replaces its group's
baseline only when its decoded RGB is identical to a selected idle capture.
Baselines are stored once, with original PNG bytes. Delta PNGs store
    delta = (image - baseline) mod 256
independently in every RGB byte. Reconstruction uses addition modulo 256.

The guarantee is exact decoded 8-bit RGB pixels and dimensions, NOT original PNG
bytes, encoding, metadata or original mode. Alpha/transparency inputs are refused.
Original PNG byte hashes remain in the manifest for auditing against originals.
Verification checks every stored payload and reconstructed RGB hash. With
--originals, it also checks each original file hash and compares every reconstructed
RGB byte directly. No archive members are extracted; only validated manifest
paths are written into a fresh temporary directory before publication. Hashes
establish integrity, not the truth of the recorded native-run claims.
"""

from __future__ import annotations

import argparse
from collections import defaultdict
import hashlib
import io
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import stat
import sys
import tempfile
import zipfile

from PIL import Image, ImageChops, __version__ as PILLOW_VERSION

KIND = "photo-evidence-rgb-mod256"
SCOPE = ("Exact decoded 8-bit RGB pixels and dimensions; reconstructed PNG bytes, "
         "metadata and original modes are not preserved. Non-PNG files are byte-exact.")
MAX_ENTRIES = 4096
MAX_MEMBER_BYTES = 64 * 1024 * 1024
MAX_MANIFEST_BYTES = 8 * 1024 * 1024
MAX_TOTAL_BYTES = 1024 * 1024 * 1024
MAX_PIXELS = 16 * 1024 * 1024
MAX_BASELINE_PIXELS = 32 * 1024 * 1024


class EvidenceError(ValueError):
    """Malformed, inconsistent, unsafe or changed evidence."""


def require(condition, message):
    if not condition:
        raise EvidenceError(message)


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def relative_path(value):
    require(isinstance(value, str)
            and re.fullmatch(r"[A-Za-z0-9_.-]+(?:/[A-Za-z0-9_.-]+)*", value)
            and all(part not in (".", "..") for part in value.split("/")),
            "unsafe relative path: " + repr(value))
    return PurePosixPath(value)


def input_path(root, name):
    path = root
    for part in relative_path(name).parts:
        path = path / part
        require(not path.is_symlink(), "symlink input refused: " + name)
    return path


def read_input(root, name):
    path = input_path(root, name)
    require(path.is_file() and path.stat().st_size <= MAX_MEMBER_BYTES,
            "input is not a bounded regular file: " + name)
    return path.read_bytes()


def rgb_image(raw, label):
    with Image.open(io.BytesIO(raw)) as image:
        require(image.format == "PNG", "not a PNG: " + label)
        require(not getattr(image, "is_animated", False), "animated PNG is outside screenshot scope: " + label)
        require(0 < image.width * image.height <= MAX_PIXELS,
                "image dimensions exceed limit: " + label)
        require("A" not in image.getbands() and "transparency" not in image.info,
                "alpha/transparency is outside RGB-only scope: " + label)
        mode = image.mode
        return image.convert("RGB"), mode


def png_bytes(image):
    buffer = io.BytesIO()
    image.save(buffer, format="PNG", compress_level=9)
    return buffer.getvalue()


def strict_json(raw):
    def pairs(items):
        result = {}
        for key, value in items:
            require(key not in result, "duplicate JSON key: " + key)
            result[key] = value
        return result

    def constant(value):
        raise EvidenceError("invalid JSON constant: " + value)

    return json.loads(raw, object_pairs_hook=pairs, parse_constant=constant)


def manifest_bytes(manifest):
    return (json.dumps(manifest, indent=2, sort_keys=True, allow_nan=False) + "\n").encode()


def payload_record(raw, prefix, suffix=""):
    digest = sha256(raw)
    return {"member": prefix + "/" + digest + suffix,
            "sha256": digest, "size_bytes": len(raw)}


def select_files(root, runs, includes):
    require(runs, "at least one --run directory is required")
    names = set()
    for name in [*runs, *includes]:
        path = input_path(root, name)
        require(path.exists(), "input does not exist: " + name)
        if name in runs:
            require(path.is_dir(), "run is not a directory: " + name)
        for entry in sorted(path.rglob("*")) if path.is_dir() else [path]:
            require(not entry.is_symlink(), "symlink input refused: " + str(entry))
            if entry.is_dir():
                continue
            logical = entry.relative_to(root).as_posix()
            relative_path(logical)
            names.add(logical)
    require(0 < len(names) <= MAX_ENTRIES, "selected file count is outside limits")
    return sorted(names)


def new_target(path):
    path = Path(os.path.abspath(path))
    require(not path.exists() and not path.is_symlink(),
            "output already exists: " + str(path))
    require(path.parent.is_dir(), "output parent does not exist: " + str(path.parent))
    for parent in path.parents:
        require(not parent.is_symlink(), "symlink output parent refused: " + str(parent))
    return path


def pack(source, archive, runs, includes=(), reference=None):
    root = Path(source).resolve(strict=True)
    require(root.is_dir(), "source must be a directory")
    output = new_target(archive)
    sidecar = new_target(output.with_suffix(".manifest.json"))
    require(not output.is_relative_to(root) and not sidecar.is_relative_to(root),
            "outputs must be outside the source directory")
    names = select_files(root, runs, includes)
    groups = defaultdict(list)
    images, files = [], []
    for name in names:
        raw = read_input(root, name)
        entry = {"path": name, "original_file_sha256": sha256(raw),
                 "original_file_size_bytes": len(raw)}
        if name.lower().endswith(".png"):
            image, mode = rgb_image(raw, name)
            entry.update(width=image.width, height=image.height, original_mode=mode,
                         rgb_sha256=sha256(image.tobytes()))
            groups[(image.width, image.height, mode)].append(entry)
            images.append(entry)
        else:
            files.append(entry)
    require(images, "no selected PNG images")
    reference_entry = None
    if reference is not None:
        raw = read_input(root, reference)
        image, mode = rgb_image(raw, reference)
        key = (image.width, image.height, mode)
        digest = sha256(image.tobytes())
        require(any(entry["rgb_sha256"] == digest
                    and PurePosixPath(entry["path"]).name.startswith("idle-")
                    for entry in groups.get(key, [])),
                "reference is not RGB-identical to a selected idle capture")
        reference_entry = {"key": key, "path": reference, "sha256": sha256(raw)}

    manifest = {"schema_version": 1, "kind": KIND, "scope": SCOPE,
                "encoding": "rgb = (baseline_rgb + delta_rgb) mod 256 per byte",
                "source_root": str(root), "runs": sorted(set(runs)),
                "producer": {"tool": "tools/pack_photo_evidence.py",
                             "sha256": sha256(Path(__file__).read_bytes()),
                             "pillow_version": PILLOW_VERSION,
                             "python_version": sys.version.split()[0]},
                "baselines": [], "images": images, "files": files}
    with tempfile.TemporaryDirectory(prefix="photo-pack-", dir=output.parent) as temporary:
        temporary = Path(temporary)
        stored = set()
        with zipfile.ZipFile(temporary / "archive", "w", zipfile.ZIP_DEFLATED,
                             compresslevel=9) as target:
            def write(name, raw):
                if name not in stored:
                    info = zipfile.ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
                    info.compress_type = zipfile.ZIP_DEFLATED
                    info.external_attr = (stat.S_IFREG | 0o644) << 16
                    target.writestr(info, raw, compresslevel=9)
                    stored.add(name)

            for number, (key, entries) in enumerate(sorted(groups.items())):
                source_name = next((item["path"] for item in entries
                                    if PurePosixPath(item["path"]).name.startswith("idle-")),
                                   entries[0]["path"])
                reason = "actual idle capture" if PurePosixPath(source_name).name.startswith("idle-") else "first image in dimension/mode group"
                if reference_entry and reference_entry["key"] == key:
                    source_name = reference_entry["path"]
                    reason = "reference verified RGB-identical to an actual idle capture"
                raw = read_input(root, source_name)
                if reference_entry and reference_entry["key"] == key:
                    require(sha256(raw) == reference_entry["sha256"], "reference changed while packing")
                baseline, mode = rgb_image(raw, source_name)
                baseline_record = {"id": "b" + str(number), "source_path": source_name,
                                   "selection": reason, "source_mode": mode,
                                   "source_file_sha256": sha256(raw),
                                   "width": baseline.width, "height": baseline.height,
                                   "rgb_sha256": sha256(baseline.tobytes()),
                                   "png": payload_record(raw, "png", ".png")}
                manifest["baselines"].append(baseline_record)
                write(baseline_record["png"]["member"], raw)
                for entry in entries:
                    original = read_input(root, entry["path"])
                    require(sha256(original) == entry["original_file_sha256"],
                            "input changed while packing: " + entry["path"])
                    image, _ = rgb_image(original, entry["path"])
                    delta = ImageChops.subtract_modulo(image, baseline)
                    raw_delta = png_bytes(delta)
                    entry.update(baseline_id=baseline_record["id"],
                                 baseline_rgb_sha256=baseline_record["rgb_sha256"],
                                 delta_rgb_sha256=sha256(delta.tobytes()),
                                 delta=payload_record(raw_delta, "png", ".png"))
                    write(entry["delta"]["member"], raw_delta)
            for entry in files:
                raw = read_input(root, entry["path"])
                require(sha256(raw) == entry["original_file_sha256"],
                        "input changed while packing: " + entry["path"])
                entry["payload"] = payload_record(raw, "raw")
                write(entry["payload"]["member"], raw)
            manifest_raw = manifest_bytes(manifest)
            write("manifest.json", manifest_raw)
        summary = verify(temporary / "archive", originals=root)
        (temporary / "manifest").write_bytes(manifest_raw)
        # Exclusive links publish complete files and never overwrite an input/output.
        os.link(temporary / "archive", output)
        try:
            os.link(temporary / "manifest", sidecar)
        except OSError:
            output.unlink()
            raise
    summary.update(archive=str(output), manifest=str(sidecar),
                   archive_sha256=sha256(output.read_bytes()), archive_bytes=output.stat().st_size)
    return summary


class Archive:
    def __init__(self, path, independent_manifest=None):
        self.zip = zipfile.ZipFile(path)
        try:
            members = self.zip.infolist()
            require(0 < len(members) <= MAX_ENTRIES * 3, "archive member count outside limits")
            self.members = {}
            total = 0
            for member in members:
                name = member.filename
                relative_path(name)
                require(member.orig_filename == name and name not in self.members,
                        "duplicate or truncated ZIP name: " + name)
                mode = stat.S_IFMT(member.external_attr >> 16)
                require(mode in (0, stat.S_IFREG) and not member.is_dir(),
                        "non-regular ZIP member: " + name)
                require(not member.flag_bits & 1 and member.compress_type in
                        (zipfile.ZIP_STORED, zipfile.ZIP_DEFLATED), "unsupported ZIP encoding")
                require(0 <= member.file_size <= MAX_MEMBER_BYTES, "ZIP member exceeds limit")
                total += member.file_size
                self.members[name] = member
            require(total <= MAX_TOTAL_BYTES, "archive decoded size exceeds limit")
            require("manifest.json" in self.members, "missing manifest.json")
            require(self.members["manifest.json"].file_size <= MAX_MANIFEST_BYTES,
                    "manifest exceeds limit")
            raw = self.zip.read("manifest.json")
            if independent_manifest is not None:
                require(Path(independent_manifest).read_bytes() == raw,
                        "independent manifest differs from archive manifest")
            self.manifest = strict_json(raw)
            m = self.manifest
            require(type(m) is dict and type(m.get("schema_version")) is int and m["schema_version"] == 1
                    and m.get("kind") == KIND and m.get("scope") == SCOPE,
                    "unsupported manifest schema or scope")
            for collection in ("baselines", "images", "files"):
                require(type(m.get(collection)) is list and len(m[collection]) <= MAX_ENTRIES,
                        "invalid manifest collection: " + collection)
            require(m["images"] and len(m["images"]) + len(m["files"]) <= MAX_ENTRIES,
                    "manifest file count outside limits")
            logical = set()
            for entry in m["images"] + m["files"]:
                name = str(relative_path(entry["path"]))
                require(name.casefold() not in logical, "duplicate output path: " + name)
                logical.add(name.casefold())
                require(type(entry["original_file_size_bytes"]) is int
                        and 0 <= entry["original_file_size_bytes"] <= MAX_MEMBER_BYTES,
                        "invalid original size: " + name)
                require(re.fullmatch("[0-9a-f]{64}", entry["original_file_sha256"]),
                        "invalid original file hash: " + name)
            for name in logical:
                require(not any(str(parent) in logical for parent in PurePosixPath(name).parents),
                        "output file/directory path conflict: " + name)
            self.used = {"manifest.json"}
            self.baselines = {}
            baseline_pixels = 0
            for entry in m["baselines"]:
                require(entry["id"] not in self.baselines, "duplicate baseline ID")
                require(type(entry["width"]) is int and type(entry["height"]) is int
                        and entry["width"] > 0 and entry["height"] > 0, "invalid baseline dimensions")
                baseline_pixels += entry["width"] * entry["height"]
                require(baseline_pixels <= MAX_BASELINE_PIXELS, "total baseline pixels exceed limit")
                image, mode = rgb_image(self.payload(entry["png"]), entry["id"])
                self.check_rgb(image, entry, "baseline " + entry["id"])
                require(mode == entry["source_mode"]
                        and entry["source_file_sha256"] == entry["png"]["sha256"],
                        "baseline source binding mismatch")
                self.baselines[entry["id"]] = (image, entry)
        except Exception:
            self.zip.close()
            raise

    def payload(self, record):
        name = str(relative_path(record["member"]))
        require(re.fullmatch(r"(?:png/[0-9a-f]{64}\.png|raw/[0-9a-f]{64})", name),
                "unexpected payload name: " + name)
        require(name in self.members, "missing payload: " + name)
        require(type(record["size_bytes"]) is int
                and record["size_bytes"] == self.members[name].file_size,
                "payload size mismatch: " + name)
        require(PurePosixPath(name).stem == record["sha256"], "payload name/hash mismatch")
        raw = self.zip.read(name)
        require(sha256(raw) == record["sha256"], "payload SHA-256 mismatch: " + name)
        self.used.add(name)
        return raw

    @staticmethod
    def check_rgb(image, entry, label):
        require(type(entry["width"]) is int and type(entry["height"]) is int
                and image.size == (entry["width"], entry["height"]),
                "image dimension mismatch: " + label)
        require(sha256(image.tobytes()) == entry["rgb_sha256"],
                "decoded RGB SHA-256 mismatch: " + label)

    def reconstruct(self, entry):
        require(entry["baseline_id"] in self.baselines, "missing baseline")
        baseline, record = self.baselines[entry["baseline_id"]]
        require(entry["baseline_rgb_sha256"] == record["rgb_sha256"],
                "image baseline hash mismatch")
        delta, mode = rgb_image(self.payload(entry["delta"]), entry["path"])
        require(mode == "RGB" and delta.size == baseline.size,
                "delta mode/dimensions mismatch: " + entry["path"])
        require(sha256(delta.tobytes()) == entry["delta_rgb_sha256"],
                "delta RGB SHA-256 mismatch: " + entry["path"])
        image = ImageChops.add_modulo(baseline, delta)
        self.check_rgb(image, entry, entry["path"])
        return image

    def process(self, originals=None, destination=None):
        root = Path(originals).resolve(strict=True) if originals is not None else None
        entries = [(entry, True) for entry in self.manifest["images"]]
        entries.extend((entry, False) for entry in self.manifest["files"])
        for entry, is_image in entries:
            require(entry["path"].lower().endswith(".png") == is_image,
                    "image/raw path classification mismatch: " + entry["path"])
            image = self.reconstruct(entry) if is_image else None
            raw = None if image is not None else self.payload(entry["payload"])
            if raw is not None:
                require(sha256(raw) == entry["original_file_sha256"]
                        and len(raw) == entry["original_file_size_bytes"],
                        "raw file binding mismatch: " + entry["path"])
            if root is not None:
                original = read_input(root, entry["path"])
                require(sha256(original) == entry["original_file_sha256"]
                        and len(original) == entry["original_file_size_bytes"],
                        "original file hash/size mismatch: " + entry["path"])
                if image is not None:
                    original_image, mode = rgb_image(original, entry["path"])
                    require(mode == entry["original_mode"] and original_image.size == image.size
                            and original_image.tobytes() == image.tobytes(),
                            "original RGB bytes differ: " + entry["path"])
                else:
                    require(original == raw, "original raw bytes differ: " + entry["path"])
            if destination is not None:
                path = destination.joinpath(*relative_path(entry["path"]).parts)
                path.parent.mkdir(parents=True, exist_ok=True)
                with path.open("xb") as target:
                    if image is not None:
                        image.save(target, format="PNG", compress_level=9)
                    else:
                        target.write(raw)
                if image is not None:
                    restored, _ = rgb_image(path.read_bytes(), entry["path"])
                    self.check_rgb(restored, entry, "written " + entry["path"])
        require(self.used == self.members.keys(), "unreferenced archive members")
        return {"result": "pass", "images": len(self.manifest["images"]),
                "raw_files": len(self.manifest["files"]), "baselines": len(self.baselines),
                "originals_compared_bytewise": root is not None,
                "original_file_bytes": sum(entry["original_file_size_bytes"] for entry in
                                           self.manifest["images"] + self.manifest["files"]),
                "scope": SCOPE}


def verify(archive, originals=None, independent_manifest=None):
    evidence = Archive(archive, independent_manifest)
    try:
        return evidence.process(originals=originals)
    finally:
        evidence.zip.close()


def unpack(archive, destination, originals=None, independent_manifest=None):
    output = new_target(destination)
    evidence = Archive(archive, independent_manifest)
    temporary = Path(tempfile.mkdtemp(prefix="photo-unpack-", dir=output.parent))
    try:
        summary = evidence.process(originals=originals, destination=temporary)
        require(not output.exists() and not output.is_symlink(), "output appeared during unpack")
        temporary.rename(output)
        summary["destination"] = str(output)
        return summary
    finally:
        evidence.zip.close()
        if temporary.exists():
            shutil.rmtree(temporary)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest="command", required=True)
    packing = commands.add_parser("pack", help="create ZIP and independent manifest; never overwrite inputs")
    packing.add_argument("source", type=Path)
    packing.add_argument("archive", type=Path)
    packing.add_argument("--run", action="append", required=True, help="relative run directory; repeatable")
    packing.add_argument("--include", action="append", default=[], help="relative additional file/directory; repeatable")
    packing.add_argument("--reference", help="relative PNG that exactly matches an idle capture")
    for command in ("verify", "unpack"):
        action = commands.add_parser(command, help="verify every reconstructed RGB hash" if command == "verify" else "reconstruct into a new directory and verify every RGB hash")
        action.add_argument("archive", type=Path)
        if command == "unpack":
            action.add_argument("destination", type=Path)
        action.add_argument("--originals", type=Path, help="also compare original hashes and exact RGB/raw bytes")
        action.add_argument("--manifest", type=Path, help="require byte-identical independent manifest")
    args = parser.parse_args(argv)
    try:
        if args.command == "pack":
            result = pack(args.source, args.archive, args.run, args.include, args.reference)
        elif args.command == "verify":
            result = verify(args.archive, args.originals, args.manifest)
        else:
            result = unpack(args.archive, args.destination, args.originals, args.manifest)
        print(json.dumps(result, indent=2, sort_keys=True))
        return 0
    except (EvidenceError, OSError, ValueError, KeyError, TypeError, AttributeError,
            zipfile.BadZipFile, Image.DecompressionBombError) as error:
        print("photo evidence error: " + str(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
