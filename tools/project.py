#!/usr/bin/env python3
"""Deterministic, evidence-gated project state. Python standard library only.

This tool coordinates work; it neither launches agents nor proves that submitted
evidence is truthful. See docs/agents/STATE_FORMAT.md for the trust boundary.
"""
from __future__ import annotations

import argparse
from contextlib import contextmanager
from datetime import datetime, timedelta, timezone
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import secrets
import sys
import tempfile
import time
from typing import Any

VERSION = 1
TASK_ID = re.compile(r"^[A-Z][A-Z0-9]{1,11}-[0-9]{3}$")
EVIDENCE_ID = re.compile(r"^[A-Za-z0-9][A-Za-z0-9_.-]{0,95}$")
STATUSES = {"todo", "in_progress", "blocked", "done"}
KINDS = {"documentary", "synthetic", "numerical", "specimen_identity", "measurement", "hardware_validation", "performance"}
SPECIMEN_KINDS = {"specimen_identity", "measurement", "hardware_validation"}
RESULTS = {"pass", "fail", "inconclusive"}
STATE_KEYS = {"schema_version", "project", "goal", "target", "fidelity_status", "revision", "updated_at", "tasks", "history"}
TASK_KEYS = {"id", "title", "milestone", "status", "priority", "dependencies", "acceptance", "required_evidence", "evidence", "owner", "claim", "blockers", "paths"}
RECORD_KEYS = {"id", "kind", "result", "summary", "command", "created_at", "source_commit", "artifacts", "specimen_id", "profile_path", "session_paths", "independent", "reference_evidence", "limitations"}
UTC = timezone.utc

# Load the sibling contract implementation both for CLI and file-based imports.
# Do not call validate_repository: it reads canonical task state independently.
DATA_SPEC = importlib.util.spec_from_file_location("rhodes_data_contracts", Path(__file__).with_name("validate_data.py"))
assert DATA_SPEC and DATA_SPEC.loader
data_contracts = importlib.util.module_from_spec(DATA_SPEC)
DATA_SPEC.loader.exec_module(data_contracts)


class StateError(ValueError):
    """User-actionable validation or coordination failure."""


def require(condition: bool, message: str) -> None:
    if not condition:
        raise StateError(message)


def exact_keys(value: Any, keys: set[str], context: str) -> None:
    require(isinstance(value, dict), f"{context}: expected an object")
    require(set(value) == keys, f"{context}: fields differ; missing={sorted(keys-set(value))}, unknown={sorted(set(value)-keys)}")


def nonempty(value: Any, context: str) -> None:
    require(isinstance(value, str) and bool(value.strip()), f"{context}: expected a nonempty string")


def strings(value: Any, context: str, *, unique: bool = True) -> None:
    require(isinstance(value, list), f"{context}: expected an array")
    for item in value:
        nonempty(item, context)
    require(not unique or len(value) == len(set(value)), f"{context}: duplicate entries")


def timestamp(value: Any, context: str) -> datetime:
    require(isinstance(value, str) and re.fullmatch(r"\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}Z", value) is not None,
            f"{context}: expected UTC timestamp YYYY-MM-DDTHH:MM:SSZ")
    try:
        return datetime.strptime(value, "%Y-%m-%dT%H:%M:%SZ").replace(tzinfo=UTC)
    except ValueError as error:
        raise StateError(f"{context}: invalid timestamp") from error


def now() -> str:
    return datetime.now(UTC).replace(microsecond=0).strftime("%Y-%m-%dT%H:%M:%SZ")


def relative_path(root: Path, value: Any, context: str, *, exists: bool = False) -> Path:
    nonempty(value, context)
    require("\\" not in value and ":" not in value, f"{context}: use a portable repository-relative path")
    path = Path(value)
    require(not path.is_absolute() and all(part not in {"..", ".git"} for part in path.parts), f"{context}: unsafe path {value!r}")
    resolved = (root / path).resolve()
    require(resolved.is_relative_to(root.resolve()) and resolved != root.resolve(), f"{context}: path escapes repository")
    if exists:
        require(resolved.is_file(), f"{context}: artifact missing or not a regular file: {value}")
    return resolved


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def artifact_path(root: Path, value: Any, context: str) -> Path:
    path = relative_path(root, value, context, exists=True)
    mutable = {"project/state.json", "project/evidence.json", "docs/PROJECT_STATE.md",
               "project/agent-campaign.json", "docs/agents/CAMPAIGN.md"}
    require(path not in {(root / item).resolve() for item in mutable},
            f"{context}: tool-managed mutable files cannot be immutable evidence; write a separate report")
    require(not path.is_relative_to((root / "project/.project.lock").resolve()),
            f"{context}: transient lock files cannot be evidence")
    return path


def load_json(path: Path) -> Any:
    def pairs(entries: list[tuple[str, Any]]) -> dict[str, Any]:
        result: dict[str, Any] = {}
        for key, value in entries:
            require(key not in result, f"{path}: duplicate JSON key {key!r}")
            result[key] = value
        return result

    try:
        return json.loads(path.read_text(encoding="utf-8"), object_pairs_hook=pairs,
                          parse_constant=lambda value: (_ for _ in ()).throw(StateError(f"{path}: nonfinite number {value}")))
    except (OSError, json.JSONDecodeError) as error:
        raise StateError(f"Cannot read {path}: {error}") from error


def encode(value: Any) -> str:
    return json.dumps(value, ensure_ascii=False, indent=2, allow_nan=False) + "\n"


def atomic_write(path: Path, content: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary = tempfile.mkstemp(prefix=f".{path.name}.", dir=path.parent)
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8", newline="\n") as stream:
            stream.write(content)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


@contextmanager
def locked(root: Path):
    """Cross-platform cooperative lock; never steal a possibly live lock."""
    lock = relative_path(root, "project/.project.lock", "state lock")
    lock.parent.mkdir(parents=True, exist_ok=True)
    deadline = time.monotonic() + 2.0
    while True:
        try:
            lock.mkdir()
            break
        except FileExistsError:
            if time.monotonic() >= deadline:
                raise StateError("Project state is locked. Wait for the writer. After a crash, inspect project/.project.lock/owner.json and verify no writer is active before removing the lock directory.")
            time.sleep(0.05)
    try:
        (lock / "owner.json").write_text(encode({"pid": os.getpid(), "created_at": now()}), encoding="utf-8")
        yield
    finally:
        owner = lock / "owner.json"
        if owner.exists():
            owner.unlink()
        lock.rmdir()


def validate_specimen_links(root: Path, evidence: dict[str, dict[str, Any]]) -> None:
    """Cross-check only explicitly linked metadata, without asserting authenticity.

    Each linked path has already passed the immutable artifact checks. Session
    identities include their manifest hash so a reference cannot silently cover
    another version of an observation. Raw payloads are not opened here.
    """
    records = [record for record in evidence.values() if record["kind"] in SPECIMEN_KINDS]
    if not records:
        return
    profile_hashes: dict[str, str] = {}
    session_bindings: dict[str, set[tuple[str, str]]] = {}
    observed_sessions: dict[tuple[str, str], dict[str, Any]] = {}
    try:
        sources = data_contracts.strict_load(relative_path(root, "docs/research/sources.json", "source registry", exists=True))
        require(isinstance(sources.get("sources"), list), "source registry: sources must be an array")
        source_ids: set[str] = set()
        for source in sources["sources"]:
            require(isinstance(source, dict) and "id" in source, "source registry: each source requires an ID")
            data_contracts.identifier(source["id"], "source ID")
            require(source["id"] not in source_ids, "source registry: duplicate source ID")
            source_ids.add(source["id"])
        for record in records:
            label = f"evidence {record['id']}"
            artifacts = {item["path"]: item["sha256"] for item in record["artifacts"]}
            profile = data_contracts.strict_load(root / record["profile_path"])
            data_contracts.validate_profile(profile, source_ids)
            require(profile["identity_status"] == "identified", f"{label}: requires an identified specimen profile")
            require(profile["profile_id"] == record["specimen_id"], f"{label}: specimen_id must match the linked profile_id")
            profile_hashes[record["id"]] = artifacts[record["profile_path"]]
            session_bindings[record["id"]] = set()
            for path in record["session_paths"]:
                session = data_contracts.strict_load(root / path)
                data_contracts.validate_session(session, {profile["profile_id"]: profile})
                require(session["kind"] == "measured", f"{label}: linked session {path} must declare kind measured; synthetic sessions cannot support specimen observations")
                if record["kind"] == "hardware_validation":
                    require(session["split"] in {"validation", "test"}, f"{label}: hardware validation requires validation/test sessions, not fit sessions")
                binding = (session["session_id"], artifacts[path])
                require(binding not in session_bindings[record["id"]], f"{label}: duplicate linked session")
                session_bindings[record["id"]].add(binding)
                observed_sessions[binding] = session
        # Repeated references to the same immutable manifest are one observation.
        # Different manifests that reuse an ID, condition, or cross-split payload
        # still pass through the existing split-leakage checks.
        data_contracts.validate_splits(list(observed_sessions.values()))
    except (data_contracts.DataError, KeyError, TypeError) as error:
        raise StateError(f"Evidence data contracts: {error}") from error

    for record in records:
        if record["kind"] != "hardware_validation":
            continue
        covered: set[tuple[str, str]] = set()
        for ref in record["reference_evidence"]:
            measurement = evidence[ref]
            if (measurement["kind"] == "measurement" and measurement["result"] == "pass"
                    and measurement["specimen_id"] == record["specimen_id"]
                    and profile_hashes[ref] == profile_hashes[record["id"]]):
                covered.update(session_bindings[ref])
        require(session_bindings[record["id"]] <= covered,
                f"evidence {record['id']}: requires passing measurement references from the same specimen and profile covering every observed session")


def validate(root: Path, state: Any, registry: Any) -> None:
    exact_keys(state, STATE_KEYS, "state")
    require(type(state["schema_version"]) is int and state["schema_version"] == VERSION, "state: unsupported schema_version")
    require(type(state["revision"]) is int and state["revision"] >= 0, "state.revision: expected nonnegative integer")
    for field in ("project", "goal", "target"):
        nonempty(state[field], f"state.{field}")
    updated = timestamp(state["updated_at"], "state.updated_at")
    require(isinstance(state["fidelity_status"], str) and state["fidelity_status"] in {"unvalidated", "measured_one_note", "measured_keyboard"}, "state: unknown fidelity_status")
    exact_keys(registry, {"schema_version", "records"}, "evidence")
    require(type(registry["schema_version"]) is int and registry["schema_version"] == VERSION, "evidence: unsupported schema_version")
    require(isinstance(registry["records"], list), "evidence.records: expected array")
    evidence: dict[str, dict[str, Any]] = {}
    for record in registry["records"]:
        exact_keys(record, RECORD_KEYS, "evidence record")
        require(isinstance(record["id"], str) and EVIDENCE_ID.fullmatch(record["id"]) is not None, "evidence: invalid ID")
        label = f"evidence {record['id']}"
        require(record["id"] not in evidence, f"{label}: duplicate ID")
        require(isinstance(record["kind"], str) and record["kind"] in KINDS, f"{label}: unknown kind")
        require(isinstance(record["result"], str) and record["result"] in RESULTS, f"{label}: unknown result")
        nonempty(record["summary"], f"{label}.summary")
        require(record["command"] is None or isinstance(record["command"], str) and bool(record["command"].strip()), f"{label}: invalid command")
        timestamp(record["created_at"], f"{label}.created_at")
        commit = record["source_commit"]
        require(commit is None or isinstance(commit, str) and re.fullmatch(r"[0-9a-f]{40}|[0-9a-f]{64}", commit) is not None, f"{label}: source_commit must be a full Git object ID or null")
        require(type(record["independent"]) is bool, f"{label}: independent must be boolean")
        require(record["specimen_id"] is None or isinstance(record["specimen_id"], str) and bool(record["specimen_id"].strip()), f"{label}: invalid specimen_id")
        require(record["profile_path"] is None or isinstance(record["profile_path"], str) and bool(record["profile_path"].strip()), f"{label}: invalid profile_path")
        for field in ("session_paths", "reference_evidence", "limitations"):
            strings(record[field], f"{label}.{field}")
        require(isinstance(record["artifacts"], list) and bool(record["artifacts"]), f"{label}: at least one immutable artifact is required")
        paths: set[str] = set()
        for artifact in record["artifacts"]:
            exact_keys(artifact, {"path", "sha256"}, f"{label} artifact")
            path = artifact_path(root, artifact["path"], f"{label} artifact")
            require(artifact["path"] not in paths, f"{label}: duplicate artifact path")
            paths.add(artifact["path"])
            require(isinstance(artifact["sha256"], str) and re.fullmatch(r"[0-9a-f]{64}", artifact["sha256"]) is not None, f"{label}: invalid artifact SHA256")
            require(sha256(path) == artifact["sha256"], f"{label}: artifact hash changed: {artifact['path']}; preserve evidence and append a new record")
        linked_paths = ([] if record["profile_path"] is None else [record["profile_path"]]) + record["session_paths"]
        for path in linked_paths:
            require(path in paths, f"{label}: typed linked file must also be an immutable artifact: {path}")
        if record["kind"] in SPECIMEN_KINDS:
            nonempty(record["specimen_id"], f"{label}.specimen_id")
            nonempty(record["profile_path"], f"{label}.profile_path")
            if record["kind"] == "specimen_identity":
                require(not record["session_paths"], f"{label}: specimen identity documents a profile without acquisition sessions")
            else:
                require(bool(record["session_paths"]), f"{label}: measurement/hardware evidence requires session_paths")
        else:
            require(record["profile_path"] is None and not record["session_paths"], f"{label}: typed profile/session links require specimen_identity, measurement, or hardware_validation kind")
        if record["kind"] == "hardware_validation":
            require(record["independent"], f"{label}: hardware validation requires held-out independent conditions")
            require(bool(record["reference_evidence"]), f"{label}: hardware validation requires measurement references")
        if record["kind"] in {"synthetic", "numerical"}:
            require(record["specimen_id"] is None, f"{label}: synthetic/numerical evidence cannot claim a measured specimen")
        evidence[record["id"]] = record
    for record in evidence.values():
        for ref in record["reference_evidence"]:
            require(ref in evidence and ref != record["id"], f"evidence {record['id']}: unknown or self reference {ref}")
    validate_specimen_links(root, evidence)

    require(isinstance(state["tasks"], list) and bool(state["tasks"]), "state.tasks: expected a nonempty array")
    tasks: dict[str, dict[str, Any]] = {}
    for task in state["tasks"]:
        exact_keys(task, TASK_KEYS, "task")
        require(isinstance(task["id"], str) and TASK_ID.fullmatch(task["id"]) is not None, "task: invalid ID")
        label = task["id"]
        require(label not in tasks, f"{label}: duplicate task ID")
        for field in ("title", "milestone"):
            nonempty(task[field], f"{label}.{field}")
        require(isinstance(task["status"], str) and task["status"] in STATUSES, f"{label}: unknown status")
        require(type(task["priority"]) is int and 0 <= task["priority"] <= 1000, f"{label}: priority must be integer 0..1000")
        for field in ("dependencies", "acceptance", "required_evidence", "evidence", "blockers", "paths"):
            strings(task[field], f"{label}.{field}")
        require(bool(task["acceptance"]) and bool(task["required_evidence"]), f"{label}: acceptance and required_evidence cannot be empty")
        require(set(task["required_evidence"]) <= KINDS, f"{label}: unknown required evidence kind")
        for path in task["paths"]:
            relative_path(root, path, f"{label}.paths")
        require(task["owner"] is None or isinstance(task["owner"], str) and bool(task["owner"].strip()), f"{label}: invalid owner")
        claim = task["claim"]
        if claim is not None:
            exact_keys(claim, {"owner", "token", "claimed_at", "expires_at"}, f"{label}.claim")
            nonempty(claim["owner"], f"{label}.claim.owner")
            nonempty(claim["token"], f"{label}.claim.token")
            start = timestamp(claim["claimed_at"], f"{label}.claim.claimed_at")
            end = timestamp(claim["expires_at"], f"{label}.claim.expires_at")
            require(start < end, f"{label}: claim expiry must follow creation")
            require(task["status"] == "in_progress" and task["owner"] == claim["owner"], f"{label}: claim requires matching in_progress owner")
        require(task["status"] != "blocked" or bool(task["blockers"]), f"{label}: blocked task requires a reason")
        require(task["status"] == "blocked" or not task["blockers"], f"{label}: clear blockers before changing status")
        for identifier in task["evidence"]:
            require(identifier in evidence, f"{label}: unknown evidence {identifier}")
        if task["status"] == "done":
            require(bool(task["evidence"]), f"{label}: done task requires evidence")
            require(all(evidence[identifier]["result"] == "pass" for identifier in task["evidence"]), f"{label}: completion evidence must pass")
            actual_kinds = {evidence[identifier]["kind"] for identifier in task["evidence"]}
            require(set(task["required_evidence"]) <= actual_kinds, f"{label}: missing evidence kinds {sorted(set(task['required_evidence'])-actual_kinds)}")
        tasks[label] = task
    for task in tasks.values():
        for dependency in task["dependencies"]:
            require(dependency in tasks, f"{task['id']}: unknown dependency {dependency}")
            if task["status"] == "done":
                require(tasks[dependency]["status"] == "done", f"{task['id']}: dependency {dependency} is not done")
    visited: set[str] = set()
    visiting: set[str] = set()

    def visit(identifier: str) -> None:
        require(identifier not in visiting, f"dependency cycle includes {identifier}")
        if identifier in visited:
            return
        visiting.add(identifier)
        for dependency in tasks[identifier]["dependencies"]:
            visit(dependency)
        visiting.remove(identifier)
        visited.add(identifier)
    for identifier in tasks:
        visit(identifier)
    for status, required in (("measured_one_note", "VAL-001"), ("measured_keyboard", "KEY-001")):
        if state["fidelity_status"] == status:
            require(required in tasks and tasks[required]["status"] == "done", f"{status} requires completed {required}")
            require(any(evidence[ref]["kind"] == "hardware_validation" for ref in tasks[required]["evidence"]), f"{status} requires hardware validation evidence")
    require(isinstance(state["history"], list), "state.history: expected array")
    require(len(state["history"]) == state["revision"], "state.history: one event per revision is required")
    previous: datetime | None = None
    for index, event in enumerate(state["history"], start=1):
        exact_keys(event, {"revision", "at", "action", "task", "owner", "detail"}, "history event")
        require(type(event["revision"]) is int and event["revision"] == index, "history revisions must be sequential")
        moment = timestamp(event["at"], "history.at")
        require(previous is None or moment >= previous, "history timestamps must be chronological")
        require(moment <= updated, "history timestamp exceeds updated_at")
        require(isinstance(event["task"], str) and event["task"] in tasks, "history: unknown task")
        for field in ("action", "owner", "detail"):
            nonempty(event[field], f"history.{field}")
        previous = moment


def read_project(root: Path) -> tuple[dict[str, Any], dict[str, Any]]:
    state = load_json(relative_path(root, "project/state.json", "canonical state", exists=True))
    registry = load_json(relative_path(root, "project/evidence.json", "canonical evidence", exists=True))
    validate(root, state, registry)
    return state, registry


def task_by_id(state: dict[str, Any], identifier: str) -> dict[str, Any]:
    for task in state["tasks"]:
        if task["id"] == identifier:
            return task
    raise StateError(f"Unknown task: {identifier}")


def active_claim(task: dict[str, Any], moment: datetime | None = None) -> bool:
    return task["claim"] is not None and timestamp(task["claim"]["expires_at"], "claim expiry") > (moment or datetime.now(UTC))


def unmet_dependencies(state: dict[str, Any], task: dict[str, Any]) -> list[str]:
    return [identifier for identifier in task["dependencies"] if task_by_id(state, identifier)["status"] != "done"]


def ready_tasks(state: dict[str, Any], *, include_claimed: bool = False) -> list[dict[str, Any]]:
    return sorted([task for task in state["tasks"] if task["status"] in {"todo", "in_progress"} and not unmet_dependencies(state, task)
                   and (include_claimed or not active_claim(task))], key=lambda task: (task["priority"], task["id"]))


def markdown(value: str) -> str:
    return value.replace("|", "\\|").replace("\n", " ").replace("\r", " ")


def render_state(state: dict[str, Any]) -> str:
    # No current clock: generated content must remain byte-identical across runs.
    counts = {status: sum(task["status"] == status for task in state["tasks"]) for status in sorted(STATUSES)}
    lines = ["# Project state", "", "<!-- Generated by tools/project.py sync; edit project/state.json through the CLI. -->", "",
             f"**Target:** {state['target']}", "", f"**Fidelity status:** `{state['fidelity_status']}`. Task completion is not a realism percentage.", "",
             f"**State revision:** {state['revision']} · **Last state change:** {state['updated_at']}", "",
             " | ".join(f"{status}: **{count}**" for status, count in counts.items()), "",
             "## Task ledger", "", "| ID | Task | Status | Dependencies | Evidence |", "| --- | --- | --- | --- | --- |"]
    for task in sorted(state["tasks"], key=lambda item: (item["priority"], item["id"])):
        lines.append(f"| {task['id']} | {markdown(task['title'])} | {task['status']} | {', '.join(task['dependencies']) or '—'} | {', '.join(task['evidence']) or '—'} |")
    lines += ["", "## Work selection", "", "Run `python3 tools/project.py next` for lease-aware available work. The list below includes claimed work and is deterministic:", ""]
    candidates = ready_tasks(state, include_claimed=True)
    for task in candidates:
        claim = task["claim"]
        suffix = f" Claimed by `{claim['owner']}` until `{claim['expires_at']}`; check current lease before starting." if claim else ""
        lines.append(f"- **{task['id']} — {task['title']}**.{suffix}")
    if not candidates:
        lines.append("No dependency-ready task. Inspect blockers and active work; do not invent completion.")
    lines += ["", "## Explicit blockers", ""]
    blocked = [task for task in state["tasks"] if task["status"] == "blocked"]
    for task in blocked:
        lines.append(f"- **{task['id']}:** {'; '.join(task['blockers'])}")
    if not blocked:
        lines.append("None recorded. This does not certify hardware fidelity.")
    lines += ["", "## Completion discipline", "", "Canonical inputs are `project/state.json` and `project/evidence.json`. Generated documentation never marks tasks done. Evidence is append-only by workflow and artifact hashes detect accidental changes; it is not a cryptographic attestation that the measurements or reported commands are truthful.", "", "Continue with [the precise agent prompt](agents/CONTINUE.md) and [the coordination workflow](agents/WORKFLOW.md).", ""]
    return "\n".join(lines)


def sync(root: Path, state: dict[str, Any], check: bool = False) -> None:
    path = relative_path(root, "docs/PROJECT_STATE.md", "generated state")
    content = render_state(state)
    if check:
        require(path.exists() and path.read_text(encoding="utf-8") == content, "docs/PROJECT_STATE.md is stale; run python3 tools/project.py sync")
    else:
        atomic_write(path, content)


def event(state: dict[str, Any], task: dict[str, Any], action: str, owner: str, detail: str) -> None:
    state["revision"] += 1
    state["updated_at"] = now()
    state["history"].append({"revision": state["revision"], "at": state["updated_at"], "action": action, "task": task["id"], "owner": owner, "detail": detail})


def check_owner(task: dict[str, Any], owner: str, token: str) -> None:
    require(active_claim(task), f"{task['id']}: no active claim; claim it before updating")
    require(task["claim"]["owner"] == owner and
            secrets.compare_digest(task["claim"]["token"].encode("utf-8"), token.encode("utf-8")),
            f"{task['id']}: claim owner/token mismatch")


def parser() -> argparse.ArgumentParser:
    result = argparse.ArgumentParser(description=__doc__)
    result.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1], help="Repository root (primarily for isolated tests)")
    commands = result.add_subparsers(dest="command_name", required=True)
    commands.add_parser("validate", help="Validate state, dependency DAG, evidence and artifact hashes")
    command = commands.add_parser("sync", help="Regenerate the state document; does not modify task status")
    command.add_argument("--check", action="store_true")
    command = commands.add_parser("next", help="Show available dependency-ready work")
    command.add_argument("--json", action="store_true")
    command = commands.add_parser("claim", help="Acquire/reacquire a task lease; expired claims can be replaced")
    command.add_argument("task")
    command.add_argument("--owner", required=True)
    command.add_argument("--minutes", type=int, default=120)
    for action in ("complete", "release", "block", "renew"):
        command = commands.add_parser(action)
        command.add_argument("task")
        command.add_argument("--owner", required=True)
        command.add_argument("--token", required=True)
        if action == "complete":
            command.add_argument("--evidence", action="append", required=True, help="Repeat for each evidence ID")
        elif action == "renew":
            command.add_argument("--minutes", type=int, default=120)
        else:
            command.add_argument("--reason", required=True)
    for action, help_text in (("unblock", "Record verified blocker resolution; dependencies still apply"),
                              ("reopen", "Reopen completed work after a regression; reopen completed dependents first")):
        command = commands.add_parser(action, help=help_text)
        command.add_argument("task")
        command.add_argument("--owner", required=True)
        command.add_argument("--reason", required=True)
    command = commands.add_parser("evidence-add", help="Register an already-observed result; this does not run the supplied command")
    command.add_argument("identifier")
    command.add_argument("--kind", choices=sorted(KINDS), required=True)
    command.add_argument("--result", choices=sorted(RESULTS), required=True)
    command.add_argument("--summary", required=True)
    command.add_argument("--command", dest="observed_command")
    command.add_argument("--artifact", action="append", required=True)
    command.add_argument("--source-commit")
    command.add_argument("--specimen")
    command.add_argument("--profile", help="Identified specimen profile path; also list it with --artifact")
    command.add_argument("--session", action="append", default=[], help="Measured session manifest path; repeat as needed and also list each with --artifact")
    command.add_argument("--independent", action="store_true")
    command.add_argument("--reference-evidence", action="append", default=[])
    command.add_argument("--limitation", action="append", default=[])
    return result


def run(args: argparse.Namespace) -> int:
    root = args.root.resolve()
    require((root / "project/state.json").is_file(), f"No canonical project state under {root}")
    with locked(root):
        state, registry = read_project(root)
        action = args.command_name
        if action == "validate":
            print(f"Valid: {len(state['tasks'])} tasks, {len(registry['records'])} evidence records; fidelity={state['fidelity_status']}")
            return 0
        if action == "sync":
            sync(root, state, args.check)
            print("Generated state is current." if args.check else "Updated docs/PROJECT_STATE.md.")
            return 0
        if action == "next":
            available = ready_tasks(state)
            if args.json:
                print(encode({"revision": state["revision"], "available": available, "blocked": [{"id": task["id"], "reasons": task["blockers"]} for task in state["tasks"] if task["status"] == "blocked"]}), end="")
            elif available:
                task = available[0]
                print(f"{task['id']}: {task['title']}\nStatus: {task['status']}\nRequired evidence: {', '.join(task['required_evidence'])}\nAcceptance:")
                for criterion in task["acceptance"]:
                    print(f"- {criterion}")
                print(f"Claim: python3 tools/project.py claim {task['id']} --owner YOUR_AGENT_ID")
            else:
                print("No unclaimed dependency-ready task. Inspect current leases and explicit blockers in docs/PROJECT_STATE.md.")
            return 0
        if action == "evidence-add":
            require(not any(record["id"] == args.identifier for record in registry["records"]), "Evidence ID already exists; append a new record instead")
            artifacts = [{"path": path, "sha256": sha256(artifact_path(root, path, "artifact"))} for path in args.artifact]
            record = {"id": args.identifier, "kind": args.kind, "result": args.result, "summary": args.summary, "command": args.observed_command,
                      "created_at": now(), "source_commit": args.source_commit, "artifacts": artifacts, "specimen_id": args.specimen,
                      "profile_path": args.profile, "session_paths": args.session, "independent": args.independent,
                      "reference_evidence": args.reference_evidence, "limitations": args.limitation}
            registry["records"].append(record)
            validate(root, state, registry)
            atomic_write(relative_path(root, "project/evidence.json", "canonical evidence"), encode(registry))
            print(f"Registered {args.identifier}. This records supplied evidence; it does not execute or certify the reported observation.")
            return 0
        task = task_by_id(state, args.task)
        nonempty(args.owner, "owner")
        if action == "claim":
            require(task["status"] in {"todo", "in_progress"}, f"{task['id']}: cannot claim a {task['status']} task")
            require(not active_claim(task), f"{task['id']}: already claimed until {task['claim']['expires_at'] if task['claim'] else '?'}")
            require(not unmet_dependencies(state, task), f"{task['id']}: unfinished dependencies {unmet_dependencies(state, task)}")
            require(1 <= args.minutes <= 1440, "Lease must be 1..1440 minutes")
            stamp = now()
            task["claim"] = {"owner": args.owner, "token": secrets.token_hex(16), "claimed_at": stamp,
                             "expires_at": (timestamp(stamp, "now") + timedelta(minutes=args.minutes)).strftime("%Y-%m-%dT%H:%M:%SZ")}
            task["owner"] = args.owner
            task["status"] = "in_progress"
            event(state, task, action, args.owner, f"Lease until {task['claim']['expires_at']}")
        elif action in {"unblock", "reopen"}:
            expected = "blocked" if action == "unblock" else "done"
            require(task["status"] == expected, f"{task['id']}: task is not {expected}")
            nonempty(args.reason, "reason")
            if action == "reopen":
                completed_dependents = [other["id"] for other in state["tasks"] if task["id"] in other["dependencies"] and other["status"] == "done"]
                require(not completed_dependents, f"Reopen completed dependent tasks first: {completed_dependents}")
                if (task["id"] == "VAL-001" and state["fidelity_status"] == "measured_one_note") or (task["id"] == "KEY-001" and state["fidelity_status"] == "measured_keyboard"):
                    state["fidelity_status"] = "unvalidated"
            task["blockers"] = []
            task["status"] = "todo"
            task["owner"] = None
            event(state, task, action, args.owner, args.reason)
        else:
            check_owner(task, args.owner, args.token)
            if action == "complete":
                task["evidence"] = args.evidence
                task["status"] = "done"
                task["claim"] = None
                event(state, task, action, args.owner, f"Evidence: {', '.join(args.evidence)}")
            elif action == "renew":
                require(1 <= args.minutes <= 1440, "Lease must be 1..1440 minutes")
                task["claim"]["expires_at"] = (datetime.now(UTC).replace(microsecond=0) + timedelta(minutes=args.minutes)).strftime("%Y-%m-%dT%H:%M:%SZ")
                event(state, task, action, args.owner, f"Lease until {task['claim']['expires_at']}")
            else:
                nonempty(args.reason, "reason")
                task["status"] = "blocked" if action == "block" else "todo"
                task["blockers"] = [args.reason] if action == "block" else []
                task["claim"] = None
                task["owner"] = None
                event(state, task, action, args.owner, args.reason)
        validate(root, state, registry)
        atomic_write(relative_path(root, "project/state.json", "canonical state"), encode(state))
        sync(root, state)
        if action == "claim":
            print(encode({"task": task["id"], **task["claim"]}), end="")
        else:
            print(f"{task['id']}: {task['status']}; revision {state['revision']}. State document regenerated.")
    return 0


def main(argv: list[str] | None = None) -> int:
    try:
        return run(parser().parse_args(argv))
    except (StateError, OSError) as error:
        print(f"project: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
