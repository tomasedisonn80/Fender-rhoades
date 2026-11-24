#!/usr/bin/env python3
"""Read-only integrity/recalculation check for the frozen VST3 matrix archives.

Nothing is extracted or executed. A pass establishes internal consistency of the
supplied bytes, not authenticity, current-source equivalence, realtime safety,
or a physical-instrument result. The archive's historical paths are metadata.
"""
from __future__ import annotations

import argparse
import gzip
import hashlib
import io
import json
import math
import os
from pathlib import Path, PurePosixPath
import re
import stat
import sys
import tarfile

import run_vst3_matrix as matrix

MAX_REPORT_BYTES = 2 * 1024 * 1024
MAX_COMPRESSED_BYTES = 8 * 1024 * 1024
MAX_TAR_BYTES = 32 * 1024 * 1024
MAX_MEMBER_BYTES = 4 * 1024 * 1024
MAX_PAYLOAD_BYTES = 16 * 1024 * 1024
MAX_MEMBERS = 512
MAX_JSON_DEPTH = 64
MAX_JSON_NODES = 250_000
SHA256 = re.compile(r"[0-9a-f]{64}\Z")
MEMBER_NAME = re.compile(r"[A-Za-z0-9][A-Za-z0-9_.-]*\Z")
METADATA_NAMES = {
    "CMakeCache.txt", "build-metadata.json", "build.ninja",
    "compile_commands.json", "input-hashes-after.json",
    "input-hashes-before.json", "machine-after.json", "machine-before.json",
    "manifest.json", "plan.json",
}
LIMITATION = (
    "Internal byte integrity and frozen-protocol recomputation only; no archived "
    "command is executed. Hashes do not authenticate acquisition, certify current "
    "binaries, establish realtime deadlines, or validate a physical instrument."
)


class VerificationError(ValueError):
    """A malformed, inconsistent, incomplete, or over-budget evidence bundle."""


def require(condition, message):
    if not condition:
        raise VerificationError(message)


def digest(raw):
    return hashlib.sha256(raw).hexdigest()


def canonical(value):
    return json.dumps(value, sort_keys=True, separators=(",", ":"),
                      ensure_ascii=True, allow_nan=False).encode("utf-8")


def same(actual, expected, label):
    # JSON bools must not compare equal to numbers through Python's == rules.
    require(canonical(actual) == canonical(expected), label + ": differs")


def bounded_read(path, limit):
    require(not path.is_symlink(), str(path) + ": symlink input refused")
    flags = (os.O_RDONLY | getattr(os, "O_BINARY", 0)
             | getattr(os, "O_NONBLOCK", 0) | getattr(os, "O_NOFOLLOW", 0))
    with os.fdopen(os.open(path, flags), "rb") as stream:
        require(stat.S_ISREG(os.fstat(stream.fileno()).st_mode),
                str(path) + ": regular file required")
        raw = stream.read(limit + 1)
    require(len(raw) <= limit, str(path) + ": byte limit exceeded")
    return raw


def strict_json(raw, label):
    """Bound nesting before decoding; reject duplicate keys and nonfinite numbers."""
    require(len(raw) <= MAX_MEMBER_BYTES, label + ": JSON byte limit exceeded")
    depth = 0
    quoted = escaped = False
    for byte in raw:
        if quoted:
            if escaped:
                escaped = False
            elif byte == 92:
                escaped = True
            elif byte == 34:
                quoted = False
        elif byte == 34:
            quoted = True
        elif byte in (91, 123):
            depth += 1
            require(depth <= MAX_JSON_DEPTH, label + ": JSON nesting limit exceeded")
        elif byte in (93, 125):
            depth -= 1

    def pairs(items):
        result = {}
        for key, value in items:
            require(key not in result, label + ": duplicate JSON key " + repr(key))
            result[key] = value
        return result

    def number(text):
        value = float(text)
        require(math.isfinite(value), label + ": nonfinite JSON number")
        return value

    def integer(text):
        require(len(text) <= 128, label + ": integer digit limit exceeded")
        return int(text)

    def constant(text):
        raise VerificationError(label + ": forbidden JSON constant " + text)

    try:
        result = json.loads(raw.decode("utf-8"), object_pairs_hook=pairs,
                            parse_float=number, parse_int=integer,
                            parse_constant=constant)
    except (UnicodeError, json.JSONDecodeError, RecursionError) as error:
        raise VerificationError(label + ": invalid JSON: " + str(error)) from error
    pending = [result]
    nodes = 0
    while pending:
        value = pending.pop()
        nodes += 1
        require(nodes <= MAX_JSON_NODES, label + ": JSON node limit exceeded")
        if isinstance(value, dict):
            pending.extend(value.values())
        elif isinstance(value, list):
            pending.extend(value)
    return result


def safe_name(name):
    # This frozen archive format is deliberately flat. No path normalization or
    # interpretation of Windows drive names, backslashes, or dot components.
    require(isinstance(name, str) and MEMBER_NAME.fullmatch(name) is not None
            and name not in (".", ".."), "unsafe archive member name: " + repr(name))
    return name


def archive_members(raw):
    require(len(raw) <= MAX_COMPRESSED_BYTES, "compressed archive byte limit exceeded")
    try:
        with gzip.GzipFile(fileobj=io.BytesIO(raw)) as compressed:
            unpacked = compressed.read(MAX_TAR_BYTES + 1)
        require(len(unpacked) <= MAX_TAR_BYTES, "decompressed tar byte limit exceeded")
        members = {}
        total = 0
        with tarfile.open(fileobj=io.BytesIO(unpacked), mode="r:") as archive:
            for member in archive:
                safe_name(member.name)
                require(member.name not in members, "duplicate tar member: " + member.name)
                require(len(members) < MAX_MEMBERS, "tar member count limit exceeded")
                require(member.type in (tarfile.REGTYPE, tarfile.AREGTYPE)
                        and member.sparse is None
                        and not any(k.startswith("GNU.sparse") for k in member.pax_headers),
                        "non-regular or sparse tar member: " + member.name)
                require(0 <= member.size <= MAX_MEMBER_BYTES,
                        "tar member byte limit exceeded: " + member.name)
                total += member.size
                require(total <= MAX_PAYLOAD_BYTES, "tar payload byte limit exceeded")
                source = archive.extractfile(member)
                require(source is not None, "unreadable tar member: " + member.name)
                with source:
                    payload = source.read(member.size + 1)
                require(len(payload) == member.size, "tar size mismatch: " + member.name)
                members[member.name] = payload
            require(not any(unpacked[archive.offset:]), "nonzero data after tar end")
        return members
    except (OSError, EOFError, tarfile.TarError) as error:
        raise VerificationError("invalid compressed tar: " + str(error)) from error


def hash_map(value, label):
    require(isinstance(value, dict) and bool(value), label + ": nonempty object required")
    for name, sha in value.items():
        require(isinstance(name, str) and isinstance(sha, str)
                and SHA256.fullmatch(sha) is not None, label + ": invalid hash entry")


def _verify(report_path):
    report_raw = bounded_read(report_path, MAX_REPORT_BYTES)
    report = strict_json(report_raw, "report")
    require(isinstance(report, dict), "report must be an object")
    same(report["schema_version"], 1, "report schema")
    same(report["status"], "complete", "report status")
    archive_name = safe_name(report["archive"])
    raw_archive = bounded_read(report_path.parent / archive_name, MAX_COMPRESSED_BYTES)
    same(report["archive_bytes"], len(raw_archive), "archive bytes")
    same(report["archive_sha256"], digest(raw_archive), "archive hash")
    payloads = archive_members(raw_archive)
    hash_map(report["member_sha256"], "member inventory")
    same(sorted(payloads), sorted(report["member_sha256"]), "member inventory")
    for name, raw in payloads.items():
        same(digest(raw), report["member_sha256"][name], "member hash " + name)
    documents = {name: strict_json(raw, name) for name, raw in payloads.items()
                 if name.endswith(".json")}
    plan = documents["plan.json"]
    unsigned = {key: value for key, value in plan.items() if key != "plan_sha256"}
    same(plan["plan_sha256"], matrix.PLAN_HASH, "frozen plan declaration")
    same(digest(canonical(unsigned)), matrix.PLAN_HASH, "frozen plan content")
    same(report["plan_canonical_sha256"], matrix.PLAN_HASH, "report plan hash")
    expected_names = set(METADATA_NAMES)
    for run in plan["runs"]:
        expected_names.update((run["stdout_file"], run["stderr_file"],
                               run["record_file"], run["id"] + ".affinity.json"))
    same(sorted(payloads), sorted(expected_names), "frozen member inventory")
    manifest = documents["manifest.json"]
    same(manifest["schema_version"], 1, "manifest schema")
    same(manifest["status"], "complete", "manifest status")
    same(manifest["plan_raw_sha256"], digest(payloads["plan.json"]), "raw plan hash")
    same(manifest["plan_canonical_sha256"], matrix.PLAN_HASH, "manifest plan hash")
    same(manifest["attempted_runs"], len(plan["runs"]), "attempted runs")
    same(manifest["unattempted_run_ids"], [], "unattempted runs")
    before, after = (documents["input-hashes-" + side + ".json"]
                     for side in ("before", "after"))
    hash_map(before, "input hashes before")
    hash_map(after, "input hashes after")
    same(after, before, "before/after input hashes")
    same(manifest["input_hashes_unchanged"], True, "manifest input equality")
    same(report["input_hashes_unchanged"], True, "report input equality")
    allowed, cpu = manifest["allowed_affinity"], manifest["chosen_cpu"]
    require(isinstance(allowed, list) and allowed
            and all(type(x) is int and x >= 0 for x in allowed), "invalid allowed affinity")
    same(allowed, sorted(set(allowed)), "allowed affinity ordering")
    same(cpu, allowed[0], "chosen lowest CPU")
    for side in ("before", "after"):
        same(documents["machine-" + side + ".json"]["affinity"], allowed,
             "machine " + side + " affinity")
    metadata = documents["build-metadata.json"]
    head = metadata["git_head"]
    same(head["returncode"], 0, "recorded git head status")
    source_commit = report["source_commit"]
    if isinstance(source_commit, str):
        same(source_commit, head["stdout"].strip(), "reported source commit")
    else:
        same(source_commit, head, "reported source commit record")
    profiles = {p["id"]: p for p in plan["profiles"]}
    variants = {v["id"]: v for v in plan["variants"]}
    summaries, records, raw_results = [], [], {}
    callbacks = frames = warmup = note_events = parameter_points = 0
    previous_end = -1
    input_order_mean_differences = []
    for run in plan["runs"]:
        name = run["id"]
        record = documents[run["record_file"]]
        for key in ("id", "sequence", "round", "profile_id", "variant"):
            same(record[key], run[key], name + " " + key)
        same(record["status"], "complete", name + " status")
        same(record["returncode"], 0, name + " returncode")
        same(record.get("timed_out", False), False, name + " timeout")
        cwd = record["cwd"]
        require(isinstance(cwd, str) and cwd.startswith("/") and "\\" not in cwd
                and str(PurePosixPath(cwd)) == cwd and ".." not in PurePosixPath(cwd).parts,
                name + ": invalid historical cwd")
        same(record["argv"], [str(PurePosixPath(cwd) / run["argv"][0]), *run["argv"][1:]],
             name + " frozen argv")
        for stream in ("stdout", "stderr"):
            file = run[stream + "_file"]
            same(record[stream], file, name + " " + stream + " path")
            same(record[stream + "_sha256"], digest(payloads[file]), name + " " + stream + " hash")
        affinity = documents[name + ".affinity.json"]
        same(record["child_affinity"], affinity, name + " child record")
        same(affinity["affinity"], [cpu], name + " child affinity")
        same(affinity["exec_argv"], record["argv"], name + " child argv")
        start, end = record["start_monotonic_ns"], record["end_monotonic_ns"]
        require(type(start) is int and type(end) is int and previous_end <= start <= end,
                name + ": nonsequential monotonic boundaries")
        previous_end = end
        raw = documents[run["stdout_file"]]
        verified = matrix.verify_result(raw, profiles[run["profile_id"]], variants[run["variant"]])
        require(verified["valid"], name + ": " + "; ".join(verified["errors"]))
        for key in ("valid", "errors", "summary", "mean_fsum_us"):
            same(record["verification"][key], verified[key], name + " verification " + key)
        # Python's built-in sum changed in 3.12. The native summary uses an
        # explicit sorted binary64 loop and is checked exactly above; preserve
        # this non-normative historical Python audit without a version gate.
        historical_mean = record["verification"]["mean_input_order_us"]
        require(type(historical_mean) in (int, float) and math.isfinite(historical_mean),
                name + ": invalid input-order audit mean")
        if historical_mean != verified["mean_input_order_us"]:
            input_order_mean_differences.append(name)
        summaries.append({key: record[key] for key in ("id", "round", "profile_id", "variant", "status")}
                         | verified["summary"])
        records.append(record)
        raw_results[name] = raw
        callbacks += raw["workload"]["measured_blocks"]
        frames += raw["integrity"]["frame_cursor"]
        warmup += raw["workload"]["completed_warmup_blocks"]
        note_events += raw["integrity"]["note_getter_reads"]
        parameter_points += raw["integrity"]["parameter_point_getter_reads"]
        same(before[str(PurePosixPath(cwd) / "tools/run_vst3_matrix.py")],
             manifest["launcher_sha256"], "recorded launcher hash")
        require(record["argv"][0] in before, name + ": executable absent from input hashes")
    same(manifest["results"], records, "manifest execution records")
    same(report["results"], summaries, "published timing summaries")
    pairs = []
    for profile in profiles:
        for variant in variants:
            left = raw_results[f"r1-{profile}-{variant}"]["integrity"]
            right = raw_results[f"r2-{profile}-{variant}"]["integrity"]
            mismatch = [field for field in plan["validation_and_analysis"]["repeat_checks"]
                        if canonical(left[field.split(".")[1]]) != canonical(right[field.split(".")[1]])]
            pairs.append({"profile_id": profile, "variant": variant, "mismatches": mismatch})
    same(manifest["repeat_pairs"], pairs, "manifest repeat pairs")
    same(report["repeat_pairs"], pairs, "reported repeat pairs")
    require(not any(p["mismatches"] for p in pairs), "repeat integrity mismatch")
    totals = {"process_runs": len(records), "same_variant_repeat_pairs": len(pairs),
              "measured_callbacks": callbacks, "measured_host_frames": frames,
              "warmup_callbacks": warmup, "note_events": note_events,
              "parameter_points": parameter_points}
    for key, value in totals.items():
        same(value, plan["expected_totals"][key], "total " + key)
    same(report["measured_callbacks"], callbacks, "published callback total")
    return {"report": str(report_path), "report_sha256": digest(report_raw),
            "archive_sha256": digest(raw_archive), "member_count": len(payloads),
            "payload_bytes": sum(map(len, payloads.values())), "status": "verified",
            **totals, "input_order_mean_differences": input_order_mean_differences,
            "limitation": LIMITATION}


def verify_report(path):
    try:
        return _verify(Path(path))
    except VerificationError:
        raise
    except (OSError, KeyError, TypeError, AttributeError, ValueError, OverflowError) as error:
        raise VerificationError("malformed or unreadable evidence: " + str(error)) from error


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("reports", type=Path, nargs="+", help="retained matrix summary JSON files")
    args = parser.parse_args(argv)
    results = []
    for path in args.reports:
        try:
            results.append(verify_report(path))
        except VerificationError as error:
            results.append({"report": str(path), "status": "rejected", "error": str(error)})
    print(json.dumps({"schema_version": 1, "results": results, "limitation": LIMITATION},
                     indent=2, ensure_ascii=True, allow_nan=False))
    return 0 if all(r["status"] == "verified" for r in results) else 1


if __name__ == "__main__":
    raise SystemExit(main())
