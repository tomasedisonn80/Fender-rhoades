#!/usr/bin/env python3
"""Check the owner-supplied reference and lossless PNG without build dependencies.

The default check uses only Python's standard library: manifest hashes, byte
counts, and image-header dimensions. --decode additionally requires an existing
developer installation of Pillow with WebP support and compares decoded RGB
pixels and ICC profiles. This script reads assets; it never regenerates them.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys


ROOT = Path(__file__).resolve().parents[1]
REFERENCE_SHA256 = "782b4c83a2bf827ed0d363bc567cdc2211485da6e650db4ae99448e78b7da24b"
ASSETS = {
    "reference": ("assets/reference/rhodes-suitcase-reference.webp", "WEBP"),
    "editor": ("plugins/vst3/resources/rhodes-suitcase.png", "PNG"),
}
SIZE = (1536, 1152)


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError(message)


def dimensions(data: bytes, image_format: str) -> tuple[int, int]:
    """Read dimensions without decoding pixels; hashes cover the entire file."""
    if image_format == "PNG":
        require(len(data) >= 33 and data[:8] == b"\x89PNG\r\n\x1a\n"
                and data[8:16] == b"\x00\x00\x00\rIHDR", "invalid PNG header")
        return struct.unpack(">II", data[16:24])
    require(len(data) >= 20 and data[:4] == b"RIFF" and data[8:12] == b"WEBP",
            "invalid WebP header")
    require(int.from_bytes(data[4:8], "little") + 8 == len(data),
            "WebP RIFF length differs from file length")
    offset = 12
    while offset + 8 <= len(data):
        kind = data[offset:offset + 4]
        size = int.from_bytes(data[offset + 4:offset + 8], "little")
        payload = data[offset + 8:offset + 8 + size]
        require(len(payload) == size, "truncated WebP chunk")
        if kind == b"VP8X" and size == 10:
            return (1 + int.from_bytes(payload[4:7], "little"),
                    1 + int.from_bytes(payload[7:10], "little"))
        if kind == b"VP8 " and size >= 10 and payload[3:6] == b"\x9d\x01\x2a":
            return tuple(value & 0x3fff for value in struct.unpack("<HH", payload[6:10]))
        if kind == b"VP8L" and size >= 5 and payload[0] == 0x2f:
            bits = int.from_bytes(payload[1:5], "little")
            return (1 + (bits & 0x3fff), 1 + ((bits >> 14) & 0x3fff))
        offset += 8 + size + (size & 1)
    raise ValueError("no supported WebP dimensions found")


def verify(root: Path, decode: bool = False) -> None:
    manifest = json.loads((root / "assets/reference/manifest.json").read_text(encoding="utf-8"))
    require(manifest["schema_version"] == 1, "unsupported manifest version")
    entries = manifest["assets"]
    require(isinstance(entries, list) and len(entries) == len(ASSETS),
            "manifest must contain exactly the reference and editor assets")
    seen = set()
    for entry in entries:
        role = entry["role"]
        require(role in ASSETS and role not in seen, f"unexpected or duplicate asset role: {role}")
        seen.add(role)
        path, image_format = ASSETS[role]
        require((entry["path"], entry["format"]) == (path, image_format),
                f"unexpected {role} asset path or format")
        data = (root / path).read_bytes()
        digest = hashlib.sha256(data).hexdigest()
        require(digest == entry["sha256"], f"SHA-256 mismatch: {path}")
        if role == "reference":
            require(digest == REFERENCE_SHA256, "reference differs from the owner's exact attachment")
        require(len(data) == entry["bytes"], f"byte count mismatch: {path}")
        require(dimensions(data, image_format) == (entry["width_px"], entry["height_px"]) == SIZE,
                f"dimension mismatch: {path}")
        require(entry["pixel_mode"] == "RGB", f"unexpected manifest pixel mode: {path}")
        print(f"PASS {path}: SHA-256, {len(data)} bytes, {SIZE[0]} x {SIZE[1]}")

    if decode:
        try:
            from PIL import Image
        except ImportError as exc:
            raise ValueError("--decode requires Pillow with WebP support (developer-only check)") from exc
        with Image.open(root / ASSETS["reference"][0]) as reference, \
                Image.open(root / ASSETS["editor"][0]) as editor:
            reference.load()
            editor.load()
            require(reference.size == editor.size == SIZE, "decoded dimensions differ")
            require(reference.mode == editor.mode == manifest["decoded_pixels"]["mode"] == "RGB",
                    "decoded pixel modes differ")
            require(getattr(reference, "n_frames", 1) == getattr(editor, "n_frames", 1) == 1,
                    "assets must each contain one frame")
            pixels = reference.tobytes()
            require(pixels == editor.tobytes(), "decoded RGB pixels differ")
            require(hashlib.sha256(pixels).hexdigest() == manifest["decoded_pixels"]["sha256"],
                    "decoded RGB hash differs from manifest")
            profile = reference.info.get("icc_profile", b"")
            require(profile == editor.info.get("icc_profile", b""), "embedded ICC profiles differ")
            require(manifest["icc_profile"]["preserved_in_png"] is True
                    and len(profile) == manifest["icc_profile"]["bytes"]
                    and hashlib.sha256(profile).hexdigest() == manifest["icc_profile"]["sha256"],
                    "embedded ICC profile differs from manifest")
        print("PASS decoded RGB pixels and embedded ICC profile are exactly identical")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=ROOT, help="repository root (defaults to this checkout)")
    parser.add_argument("--decode", action="store_true", help="also compare pixels and ICC profiles using Pillow")
    args = parser.parse_args()
    try:
        verify(args.root, args.decode)
    except (OSError, ValueError, KeyError, TypeError, struct.error) as exc:
        print(f"FAIL editor assets: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
