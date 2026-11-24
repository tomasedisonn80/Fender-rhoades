#!/usr/bin/env python3
"""Retain source-bound CI diagnostics without hiding a child command's failure.

Only the standard library is required. The workflows upload this tool's output
with ``always()``; a missing command/result remains missing evidence, not a pass.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import subprocess
import sys
import time


LEGACY_COMMIT = "6c91dab00855b67a559e92edb79ae49e567be469"
SDK_REVISIONS = {
    ".": "3cdf9ca5d1f5b1b21e0a86832aa4abe55607bd96",
    "base": "fcf9da0bd27a16f7f03773a3a39822f28f5c8477",
    "cmake": "054c9143cbb8d47fc4694e473f2ee3b4d951a8f5",
    "pluginterfaces": "4f547e8e102b47de4a8b8aaf343c73b700786372",
    "public.sdk": "586dc5e6c8012c3e4b01c79389375cbe96bdb1da",
}
BUNDLE_NAMES = ("RhodesResearch", "RhodesResearchFiltered", "RhodesResearchQuadrature")
ENVIRONMENT_KEYS = (
    "GITHUB_SHA", "GITHUB_REF", "GITHUB_REPOSITORY", "GITHUB_WORKFLOW",
    "GITHUB_WORKFLOW_REF", "GITHUB_WORKFLOW_SHA", "GITHUB_EVENT_NAME",
    "GITHUB_RUN_ID", "GITHUB_RUN_NUMBER", "GITHUB_RUN_ATTEMPT", "GITHUB_JOB",
    "GITHUB_SERVER_URL", "RUNNER_OS", "RUNNER_ARCH", "RUNNER_ENVIRONMENT",
    "ImageOS", "ImageVersion", "CI_JOB_STATUS",
)
TOOLCHAIN_KEYS = (
    "CMAKE_GENERATOR", "CMAKE_GENERATOR_PLATFORM", "CMAKE_GENERATOR_TOOLSET",
    "CMAKE_BUILD_TYPE", "CMAKE_CXX_COMPILER", "CMAKE_CXX_COMPILER_ID",
    "CMAKE_CXX_COMPILER_VERSION", "CMAKE_CXX_COMPILER_TARGET",
    "CMAKE_CXX_COMPILER_ARCHITECTURE_ID", "CMAKE_CXX_SIZEOF_DATA_PTR",
    "CMAKE_C_COMPILER", "CMAKE_C_COMPILER_ID", "CMAKE_C_COMPILER_VERSION",
    "CMAKE_SYSTEM_NAME", "CMAKE_SYSTEM_PROCESSOR", "CMAKE_OSX_ARCHITECTURES",
    "CMAKE_OSX_DEPLOYMENT_TARGET", "CMAKE_CXX_FLAGS", "CMAKE_CXX_FLAGS_RELEASE",
)


def utc_now() -> str:
    return datetime.now(timezone.utc).isoformat()


def sha256(path: Path) -> str:
    with path.open("rb") as source:
        return hashlib.file_digest(source, "sha256").hexdigest()


def write_json(path: Path, value: object, *, update: bool = False) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    payload = json.dumps(value, indent=2, sort_keys=True, ensure_ascii=True) + "\n"
    if update:
        temporary = path.with_name(path.name + ".tmp")
        temporary.write_text(payload, encoding="utf-8")
        os.replace(temporary, path)
    else:
        with path.open("x", encoding="utf-8") as output:
            output.write(payload)


def capture(command: list[str]) -> dict:
    try:
        result = subprocess.run(command, stdin=subprocess.DEVNULL, capture_output=True,
                                timeout=30, check=False)
        return {"command": command, "returncode": result.returncode,
                "stdout": result.stdout.decode("utf-8", "replace"),
                "stderr": result.stderr.decode("utf-8", "replace")}
    except (OSError, subprocess.TimeoutExpired) as error:
        return {"command": command, "returncode": None, "error": str(error)}


def git(root: Path, *arguments: str) -> str:
    result = capture(["git", "-C", str(root), *arguments])
    if result["returncode"] != 0:
        raise ValueError(f"Git inspection failed for {root}: {result}")
    return result["stdout"].rstrip("\n")


def repository_snapshot(root: Path, expected: str | None, errors: list[str]) -> dict:
    root = root.resolve()
    result = {"root": str(root), "expected_commit": expected}
    try:
        # Missing nested checkouts must not inherit a parent repository.
        if Path(git(root, "rev-parse", "--show-toplevel")).resolve() != root:
            raise ValueError(f"Not a repository root: {root}")
        result["commit"] = git(root, "rev-parse", "HEAD")
        result["tree"] = git(root, "rev-parse", "HEAD^{tree}")
        result["tracked_status"] = git(
            root, "status", "--porcelain=v1", "--untracked-files=no", "--ignore-submodules=all")
        result["tracked_clean"] = not result["tracked_status"]
        if expected and result["commit"] != expected:
            errors.append(f"Source commit mismatch for {root}: expected {expected}, "
                          f"found {result['commit']}")
        if not result["tracked_clean"]:
            errors.append(f"Tracked source differs from its commit: {root}")
        files = []
        for entry in git(root, "ls-files", "--stage", "-z").split("\0"):
            if not entry:
                continue
            index, relative = entry.split("\t", 1)
            mode, object_id, stage = index.split()
            record = {"path": relative, "git_mode": mode, "git_object": object_id,
                      "git_stage": stage}
            path = root / relative
            if stage != "0":
                errors.append(f"Unmerged source path: {path}")
            if mode == "160000":
                record["kind"] = "gitlink"
            elif path.is_symlink():
                target = os.readlink(path)
                record.update(kind="symlink", target=target,
                              sha256=hashlib.sha256(os.fsencode(target)).hexdigest())
            elif path.is_file():
                record.update(kind="file", bytes=path.stat().st_size, sha256=sha256(path))
            else:
                record["kind"] = "missing"
                errors.append(f"Missing tracked source path: {path}")
            files.append(record)
        result["files"] = files
        result["files_sha256"] = hashlib.sha256(json.dumps(
            files, sort_keys=True, separators=(",", ":")).encode("utf-8")).hexdigest()
    except (OSError, ValueError) as error:
        result["error"] = str(error)
        errors.append(str(error))
    return result


def sdk_snapshot(root: Path | None, errors: list[str]) -> dict | None:
    if root is None:
        return None
    result = {relative: repository_snapshot(root / relative, expected, errors)
              for relative, expected in SDK_REVISIONS.items()}
    # Headless legacy builds still need only the original four submodules.
    # When the editor checkout is present, bind its actual tracked contents too.
    if (root / "vstgui4" / ".git").exists():
        result["vstgui4"] = repository_snapshot(
            root / "vstgui4", "5db272256172557818b6158cf0bb2c4410bddb25", errors)
    return result


def environment() -> dict:
    return {
        "platform": platform.platform(), "system": platform.system(),
        "release": platform.release(), "machine": platform.machine(),
        "python": sys.version, "python_executable": sys.executable,
        "process_bits": 64 if sys.maxsize > 2**32 else 32,
        "github": {key: os.environ[key] for key in ENVIRONMENT_KEYS if key in os.environ},
        "tools": {name: capture([name, "--version"]) for name in ("git", "cmake", "ctest")},
    }


def build_snapshot(root: Path) -> dict:
    root = root.resolve()
    result = {"root": str(root), "configured": False, "settings": {}, "files": []}
    cache = root / "CMakeCache.txt"
    if cache.is_file():
        result["configured"] = True
        result["files"].append({"path": "CMakeCache.txt", "sha256": sha256(cache)})
        for line in cache.read_text(encoding="utf-8", errors="replace").splitlines():
            if not line or line.startswith(("//", "#")) or "=" not in line:
                continue
            key, value = line.split("=", 1)
            key = key.split(":", 1)[0]
            if key.startswith(("CMAKE_", "RHODES_", "SMTG_", "BUILD_TESTING")):
                result["settings"][key] = value
    for pattern in ("CMakeFiles/*/CMake*Compiler.cmake", "CMakeFiles/*/CMakeSystem.cmake"):
        for path in sorted(root.glob(pattern)):
            result["files"].append({"path": path.relative_to(root).as_posix(),
                                    "sha256": sha256(path)})
            contents = path.read_text(encoding="utf-8", errors="replace")
            for key, value in re.findall(r'^set\((CMAKE_\w+) "([^"\n]*)"\)',
                                         contents, re.MULTILINE):
                if key in TOOLCHAIN_KEYS:
                    result["settings"][key] = value
    result["toolchain"] = {key: result["settings"].get(key, "") for key in TOOLCHAIN_KEYS}
    return result


def module_snapshot(root: Path, errors: list[str]) -> dict:
    root = root.resolve()
    modules = []
    suffix = {"Linux": ".so", "Windows": ".vst3", "Darwin": ""}.get(platform.system())
    if suffix is None:
        errors.append(f"Unsupported native module platform: {platform.system()}")
    else:
        for name in BUNDLE_NAMES:
            bundle = root / f"{name}.vst3"
            paths = list(bundle.glob(f"Contents/*/{name}{suffix}"))
            if bundle.is_symlink() or len(paths) != 1 or not paths[0].is_file() \
                    or paths[0].is_symlink() or paths[0].stat().st_size == 0:
                errors.append(f"Expected one nonempty native module in {bundle}")
                continue
            path = paths[0]
            modules.append({"path": path.relative_to(root).as_posix(),
                            "sha256": sha256(path), "bytes": path.stat().st_size})
    result = {"root": str(root), "modules": modules}
    manifest = root / "manifest.json"
    if manifest.is_file():
        result["manifest_sha256"] = sha256(manifest)
    return result


def metadata(args: argparse.Namespace) -> int:
    errors: list[str] = []
    result = {
        "schema_version": 1, "phase": args.phase, "captured_at": utc_now(),
        "source": repository_snapshot(args.source_root, args.expected_sha, errors),
        "sdk": sdk_snapshot(args.sdk_root, errors),
        "environment": environment(),
        "builds": [build_snapshot(path) for path in args.build_dir],
        "bundles": [module_snapshot(path, errors) for path in args.bundle_root],
    }
    if args.baseline_source:
        result["baseline_source"] = repository_snapshot(
            args.baseline_source, LEGACY_COMMIT, errors)
    if args.phase == "after":
        before_path = args.output_dir / "metadata-before.json"
        if before_path.is_file():
            before = json.loads(before_path.read_text(encoding="utf-8"))
            for group in ("source", "baseline_source", "sdk"):
                old, new = before.get(group), result.get(group)
                old_repositories = old if group == "sdk" else {".": old}
                new_repositories = new if group == "sdk" else {".": new}
                for key in set(old_repositories or {}) | set(new_repositories or {}):
                    old_repository = (old_repositories or {}).get(key)
                    new_repository = (new_repositories or {}).get(key)
                    if old_repository is None and new_repository is None:
                        continue
                    if not old_repository or not new_repository or any(
                            old_repository.get(field) != new_repository.get(field)
                            for field in ("commit", "tree", "files_sha256")):
                        errors.append(f"Source snapshot changed during the job: {group}/{key}")
            result["before_snapshot_sha256"] = sha256(before_path)
        else:
            errors.append("Before snapshot is missing; source-bound execution is incomplete")
    result["errors"] = errors
    result["valid"] = not errors
    destination = args.output_dir / f"metadata-{args.phase}.json"
    write_json(destination, result)
    print(f"Retained metadata: {destination}; valid={result['valid']}")
    for error in errors:
        print(error, file=sys.stderr)
    return 1 if errors else 0


def emit(data: bytes) -> None:
    # Keep native output bytes in the file even when the terminal uses another
    # encoding. Python's binary stream is available in the actual CLI.
    if hasattr(sys.stdout, "buffer"):
        sys.stdout.buffer.write(data)
        sys.stdout.buffer.flush()
    else:
        sys.stdout.write(data.decode("utf-8", "replace"))
        sys.stdout.flush()


def recorded_command(command: list[str], output_dir: Path, label: str,
                     *, inventory: bool = False) -> int:
    output_dir.mkdir(parents=True, exist_ok=True)
    log_path = output_dir / (f"{label}.json" if inventory else f"{label}.log")
    record_path = output_dir / f"{label}.command.json"
    record = {"command": command, "cwd": str(Path.cwd()), "started_at": utc_now(),
              "status": "running", "returncode": None, "log": log_path.name}
    # Refuse duplicate labels so an accidental retry cannot replace first-failure evidence.
    if log_path.exists() or record_path.exists():
        raise ValueError(f"Diagnostics already exist for command label {label}")
    write_json(record_path, record)
    started = time.monotonic()
    with log_path.open("xb") as log:
        try:
            if inventory:
                result = subprocess.run(command, stdin=subprocess.DEVNULL, capture_output=True,
                                        check=False)
                log.write(result.stdout)
                stderr = output_dir / f"{label}.stderr.log"
                stderr.write_bytes(result.stderr)
                record["stderr"] = stderr.name
                emit(result.stderr)
                returncode = result.returncode
            else:
                with subprocess.Popen(command, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                                      stderr=subprocess.STDOUT) as process:
                    try:
                        while block := process.stdout.read1(65536):
                            log.write(block)
                            log.flush()
                            emit(block)
                        returncode = process.wait()
                    except KeyboardInterrupt:
                        process.kill()
                        returncode = process.wait()
                        record["interrupted"] = True
            record["status"] = "completed"
        except OSError as error:
            returncode = 127
            record.update(status="could_not_start", error=str(error))
            log.write((str(error) + "\n").encode("utf-8"))
            print(error, file=sys.stderr)
    wrapper_returncode = 128 - returncode if returncode < 0 else returncode
    if record.get("interrupted"):
        wrapper_returncode = 130
    record.update(returncode=returncode, wrapper_returncode=wrapper_returncode,
                  ended_at=utc_now(), elapsed_seconds=time.monotonic() - started,
                  log_bytes=log_path.stat().st_size, log_sha256=sha256(log_path))
    write_json(record_path, record, update=True)
    return wrapper_returncode


def run(args: argparse.Namespace) -> int:
    return recorded_command(args.command, args.output_dir, args.label)


def ctest(args: argparse.Namespace) -> int:
    # Use the exact same selection/configuration for inventory and execution.
    # Absolute output paths work for both --test-dir and source-relative presets.
    output = args.output_dir.resolve()
    inventory_label = f"{args.label}-inventory"
    code = recorded_command(args.command + ["--show-only=json-v1"], output,
                            inventory_label, inventory=True)
    if code:
        return code
    inventory = json.loads((output / f"{inventory_label}.json").read_text(encoding="utf-8"))
    if not isinstance(inventory.get("tests"), list):
        raise ValueError("CTest inventory does not contain a tests array")
    return recorded_command(args.command + [
        "--output-log", str(output / f"{args.label}-output.log"),
        "--output-junit", str(output / f"{args.label}-junit.xml"),
    ], output, args.label)


def verify_toolchains(args: argparse.Namespace) -> int:
    baseline = build_snapshot(args.baseline_build)
    current = build_snapshot(args.current_build)
    errors = []
    for name, build in (("baseline", baseline), ("current", current)):
        for key in ("CMAKE_GENERATOR", "CMAKE_CXX_COMPILER", "CMAKE_CXX_COMPILER_ID",
                    "CMAKE_CXX_COMPILER_VERSION"):
            if not build["toolchain"][key]:
                errors.append(f"Missing {name} toolchain setting: {key}")
    for key in TOOLCHAIN_KEYS:
        if baseline["toolchain"][key] != current["toolchain"][key]:
            errors.append(f"Baseline/current toolchain mismatch: {key}")
    result = {"baseline": baseline, "current": current, "valid": not errors, "errors": errors}
    write_json(args.output_dir / "toolchain-comparison.json", result)
    for error in errors:
        print(error, file=sys.stderr)
    return 1 if errors else 0


def baseline_manifest(args: argparse.Namespace) -> int:
    errors: list[str] = []
    source = repository_snapshot(args.source_root, LEGACY_COMMIT, errors)
    modules = module_snapshot(args.bundle_root, errors)
    result = {"schema_version": 1, "source_commit": source.get("commit"),
              "modules": modules["modules"], "source": source,
              "sdk": sdk_snapshot(args.sdk_root, errors),
              "build": build_snapshot(args.build_dir), "environment": environment(),
              "created_at": utc_now(), "errors": errors}
    if not result["build"]["configured"]:
        errors.append("Legacy build cache is missing")
    result["valid"] = not errors
    # Even a rejected baseline retains the diagnostic attempt. It is never
    # published as a valid comparator manifest or silently relabeled.
    write_json(args.output_dir / "baseline-manifest.json", result)
    if errors:
        for error in errors:
            print(error, file=sys.stderr)
        return 1
    write_json(args.bundle_root / "manifest.json", result)
    return 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="action", required=True)
    meta = commands.add_parser("metadata")
    meta.add_argument("--source-root", type=Path, default=Path("."))
    meta.add_argument("--expected-sha", default=os.environ.get("GITHUB_SHA"))
    meta.add_argument("--phase", choices=("before", "after"), required=True)
    meta.add_argument("--sdk-root", type=Path)
    meta.add_argument("--baseline-source", type=Path)
    meta.add_argument("--build-dir", type=Path, action="append", default=[])
    meta.add_argument("--bundle-root", type=Path, action="append", default=[])
    meta.set_defaults(handler=metadata)
    for name, handler in (("run", run), ("ctest", ctest)):
        command = commands.add_parser(name)
        command.add_argument("--label", required=True)
        command.add_argument("command", nargs=argparse.REMAINDER)
        command.set_defaults(handler=handler)
    toolchains = commands.add_parser("verify-toolchains")
    toolchains.add_argument("--baseline-build", type=Path, required=True)
    toolchains.add_argument("--current-build", type=Path, required=True)
    toolchains.set_defaults(handler=verify_toolchains)
    baseline = commands.add_parser("baseline-manifest")
    baseline.add_argument("--source-root", type=Path, required=True)
    baseline.add_argument("--bundle-root", type=Path, required=True)
    baseline.add_argument("--build-dir", type=Path, required=True)
    baseline.add_argument("--sdk-root", type=Path, required=True)
    baseline.set_defaults(handler=baseline_manifest)
    for command in commands.choices.values():
        command.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args(argv)
    if hasattr(args, "command"):
        if args.command[:1] == ["--"]:
            args.command = args.command[1:]
        if not args.command or not re.fullmatch(r"[a-z0-9][a-z0-9_-]*", args.label):
            parser.error("A command and a simple lowercase command label are required")
    try:
        return args.handler(args)
    except (OSError, ValueError) as error:
        print(f"CI diagnostics error: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
