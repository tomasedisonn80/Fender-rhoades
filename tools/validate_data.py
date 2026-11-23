#!/usr/bin/env python3
"""Validate committed profile and acquisition contracts; no external packages.

This checks metadata integrity, not whether an observation is scientifically true.
No files named in a manifest are opened by this command.
"""
from __future__ import annotations

import argparse
import json
import math
import re
import sys
from datetime import datetime
from pathlib import Path, PurePosixPath

ROOT = Path(__file__).resolve().parents[1]
SI_UNITS = {"1", "kg", "m", "m/s", "m/s^2", "s", "Hz", "s^-1", "N", "N/m",
            "N*s/m", "V", "A", "ohm", "F", "H", "Wb_turn", "Pa", "J", "K"}
CHANNEL_UNITS = {"V", "A", "m", "m/s", "N", "Pa"}
IDENTIFIER = re.compile(r"^[A-Za-z0-9][A-Za-z0-9_.-]*$")


class DataError(ValueError):
    pass


def require(condition: bool, message: str) -> None:
    if not condition:
        raise DataError(message)


def number(value: object, label: str, minimum: float | None = None,
           positive: bool = False) -> None:
    try:
        finite = type(value) in (int, float) and math.isfinite(value)
    except OverflowError:
        finite = False
    require(finite, f"{label}: expected a finite number")
    if minimum is not None:
        require(value >= minimum, f"{label}: must be >= {minimum}")
    if positive:
        require(value > 0, f"{label}: must be positive")


def keys(obj: object, required: set[str], label: str) -> None:
    require(isinstance(obj, dict), f"{label}: expected object")
    require(set(obj) == required,
            f"{label}: missing {sorted(required - set(obj))}; unknown {sorted(set(obj) - required)}")


def text(value: object, label: str) -> None:
    require(isinstance(value, str) and bool(value.strip()), f"{label}: nonempty text required")


def identifier(value: object, label: str) -> None:
    text(value, label)
    require(IDENTIFIER.fullmatch(value) is not None, f"{label}: invalid identifier")


def references(value: object, label: str) -> None:
    require(isinstance(value, list), f"{label}: list required")
    for item in value:
        identifier(item, label)
    require(len(value) == len(set(value)), f"{label}: duplicate reference")


def strict_load(path: Path) -> dict:
    def pairs(items):
        result = {}
        for key, value in items:
            if key in result:
                raise DataError(f"duplicate JSON key: {key}")
            result[key] = value
        return result
    def invalid(value):
        raise DataError(f"nonfinite JSON constant: {value}")
    try:
        data = json.loads(path.read_text(encoding="utf-8"), object_pairs_hook=pairs,
                          parse_constant=invalid)
    except (OSError, UnicodeError, ValueError) as exc:
        raise DataError(f"{path}: {exc}") from exc
    require(isinstance(data, dict), f"{path}: object required")
    def check_finite(value):
        if type(value) in (float, int):
            number(value, "JSON number")
        elif isinstance(value, dict):
            for child in value.values():
                check_finite(child)
        elif isinstance(value, list):
            for child in value:
                check_finite(child)
    check_finite(data)
    return data


def validate_profile(profile: dict, source_ids: set[str]) -> None:
    keys(profile, {"schema_version", "profile_id", "kind", "identity_status", "model_family",
                   "serial_number", "production_date", "action_revision", "tine_revision",
                   "pickup_revision", "wiring_verified", "output_boundary", "key_count",
                   "midi_low", "midi_high", "key_mapping_source", "reference_pitch_hz",
                   "reference_pitch_status", "instrument_evidence", "load", "notes"}, "profile")
    require(type(profile["schema_version"]) is int and profile["schema_version"] == 1, "profile: schema version must be 1")
    require(profile["kind"] == "specimen", "profile: wrong kind")
    require(profile["model_family"] == "mark_i_stage_73", "profile: wrong target family")
    require(profile["identity_status"] in ("unidentified", "identified"), "profile: identity status invalid")
    identifier(profile["profile_id"], "profile_id")
    text(profile["notes"], "notes")
    for field in ("serial_number", "production_date", "action_revision", "tine_revision", "pickup_revision"):
        require(profile[field] is None or isinstance(profile[field], str) and bool(profile[field].strip()), f"{field}: string or null required")
    require(type(profile["wiring_verified"]) is bool, "wiring_verified: boolean required")
    require(profile["output_boundary"] in ("loaded_harp", "stage_jack"), "profile: invalid Stage output boundary")
    require(all(type(profile[x]) is int for x in ("key_count", "midi_low", "midi_high")), "key mapping: integers required")
    require((profile["key_count"], profile["midi_low"], profile["midi_high"]) == (73, 28, 100), "key mapping: expected 73 keys, MIDI 28..100")
    identifier(profile["key_mapping_source"], "key_mapping_source")
    require(profile["key_mapping_source"] in source_ids, "key mapping: unknown source")
    number(profile["reference_pitch_hz"], "reference_pitch_hz", positive=True)
    require(profile["reference_pitch_status"] in ("assumed", "measured"), "reference pitch provenance invalid")
    require(isinstance(profile["instrument_evidence"], list), "instrument_evidence: list required")
    for evidence in profile["instrument_evidence"]:
        text(evidence, "instrument evidence")
    require(len(profile["instrument_evidence"]) == len(set(profile["instrument_evidence"])),
            "instrument evidence: duplicate entry")
    keys(profile["load"], {"resistance_ohm", "capacitance_farad"}, "profile load")
    for field, value in profile["load"].items():
        if value is not None:
            number(value, field, minimum=0, positive=field == "resistance_ohm")
    if profile["identity_status"] == "identified":
        require(all(profile[x] for x in ("serial_number", "action_revision", "tine_revision", "pickup_revision", "instrument_evidence")), "identified specimen: missing identity/revision evidence")
        require(profile["wiring_verified"], "identified specimen: wiring must be verified")
        require(all(x is not None for x in profile["load"].values()), "identified specimen: load must be recorded")
    else:
        require(not profile["wiring_verified"] and profile["reference_pitch_status"] == "assumed", "unidentified profile cannot claim verified wiring or measured tuning")


def safe_relative(value: object) -> None:
    text(value, "artifact path")
    path = PurePosixPath(value)
    require(not path.is_absolute() and ".." not in path.parts and ".git" not in path.parts
            and "\\" not in value and ":" not in value and path.parts and str(path) == value
            and not any(ord(character) < 32 or ord(character) == 127 for character in value),
            "artifact path must be a canonical relative POSIX path without traversal or control characters")


def validate_session(session: dict, profiles: dict[str, dict]) -> None:
    keys(session, {"schema_version", "session_id", "kind", "specimen_profile", "output_boundary", "split",
                   "condition_id", "acquired_at", "operator", "temperature_celsius", "sample_rate_hz",
                   "channels", "excitation", "load", "pickup_geometry_m", "calibration", "artifacts", "rights", "notes"}, "session")
    require(type(session["schema_version"]) is int and session["schema_version"] == 1, "session: schema version must be 1")
    for field in ("session_id", "condition_id", "specimen_profile"):
        identifier(session[field], field)
    text(session["notes"], "notes")
    require(session["kind"] in ("synthetic", "measured"), "session: invalid kind")
    require(session["specimen_profile"] in profiles, "session: unknown specimen profile")
    profile = profiles[session["specimen_profile"]]
    require(session["output_boundary"] == profile["output_boundary"], "session/profile boundary mismatch")
    require(session["split"] in ("development", "fit", "validation", "test"), "session: invalid split")
    number(session["sample_rate_hz"], "sample rate", positive=True)
    require(isinstance(session["channels"], list) and session["channels"], "channels: nonempty list required")
    names = set()
    for channel in session["channels"]:
        keys(channel, {"name", "unit", "calibration_id"}, "channel")
        text(channel["name"], "channel name")
        require(channel["name"] not in names, "channel names must be unique")
        names.add(channel["name"])
        text(channel["unit"], "channel unit")
        require(channel["unit"] in CHANNEL_UNITS, "channel: unsupported SI unit")
        if channel["calibration_id"] is not None:
            identifier(channel["calibration_id"], "channel calibration ID")
    keys(session["excitation"], {"type", "key_midi", "hammer_velocity_m_per_s"}, "excitation")
    require(type(session["excitation"]["key_midi"]) is int and 28 <= session["excitation"]["key_midi"] <= 100, "excitation: key outside target range")
    require(session["excitation"]["type"] in ("synthetic_impulse", "native_hammer", "controlled_hammer", "pluck", "electrical_sweep"), "excitation type invalid")
    if session["excitation"]["hammer_velocity_m_per_s"] is not None:
        number(session["excitation"]["hammer_velocity_m_per_s"], "hammer velocity", minimum=0)
    keys(session["load"], {"resistance_ohm", "capacitance_farad"}, "session load")
    number(session["load"]["resistance_ohm"], "load resistance", positive=True)
    number(session["load"]["capacitance_farad"], "load capacitance", minimum=0)
    require(isinstance(session["calibration"], list), "calibration: list required")
    calibration_ids = {}
    for calibration in session["calibration"]:
        keys(calibration, {"id", "method", "reference", "uncertainty", "unit"}, "calibration")
        for field in ("id", "method", "reference", "unit"):
            text(calibration[field], f"calibration {field}")
        identifier(calibration["id"], "calibration ID")
        require(calibration["unit"] in CHANNEL_UNITS, "calibration: unsupported SI unit")
        number(calibration["uncertainty"], "calibration uncertainty", minimum=0)
        require(calibration["id"] not in calibration_ids, "duplicate calibration ID")
        calibration_ids[calibration["id"]] = calibration["unit"]
    for channel in session["channels"]:
        require(channel["calibration_id"] is None or channel["calibration_id"] in calibration_ids, "channel: unknown calibration")
        if channel["calibration_id"] is not None:
            require(calibration_ids[channel["calibration_id"]] == channel["unit"],
                    "channel/calibration unit mismatch")
    require(isinstance(session["artifacts"], list), "artifacts: list required")
    seen = set()
    for artifact in session["artifacts"]:
        keys(artifact, {"path", "sha256", "bytes"}, "artifact")
        safe_relative(artifact["path"])
        require(artifact["path"] not in seen, "duplicate artifact path")
        seen.add(artifact["path"])
        require(isinstance(artifact["sha256"], str) and re.fullmatch(r"[0-9a-f]{64}", artifact["sha256"]), "artifact: invalid SHA-256")
        require(type(artifact["bytes"]) is int and artifact["bytes"] > 0, "artifact: positive byte count required")
    keys(session["rights"], {"status", "permission_reference"}, "rights")
    require(session["rights"]["status"] in ("synthetic_original", "owned", "permission_granted"), "rights: unresolved permission")
    if session["temperature_celsius"] is not None:
        number(session["temperature_celsius"], "temperature", minimum=-273.15)
    if session["pickup_geometry_m"] is not None:
        keys(session["pickup_geometry_m"], {"gap", "vertical_offset"}, "pickup geometry")
        number(session["pickup_geometry_m"]["gap"], "pickup gap", positive=True)
        number(session["pickup_geometry_m"]["vertical_offset"], "pickup offset")
    if session["kind"] == "synthetic":
        require(session["split"] == "development", "synthetic data cannot be a held-out hardware validation set")
        require(session["acquired_at"] is None and session["operator"] is None, "synthetic fixture must not claim acquisition")
        require(session["rights"]["status"] == "synthetic_original", "synthetic fixture rights mismatch")
        require(session["rights"]["permission_reference"] is None, "synthetic fixture must not claim external permission")
        require(not session["calibration"] and all(c["calibration_id"] is None for c in session["channels"]),
                "synthetic fixture cannot claim acquisition calibration")
    else:
        require(profile["identity_status"] == "identified", "measured session needs identified specimen")
        require(session["split"] != "development", "measured session needs an explicit fit/validation/test split")
        require(session["excitation"]["type"] != "synthetic_impulse", "measured session cannot use synthetic excitation")
        text(session["operator"], "operator")
        text(session["acquired_at"], "acquisition timestamp")
        require(re.fullmatch(r"\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}(?:\.\d+)?(?:Z|[+-]\d{2}:\d{2})", session["acquired_at"]),
                "acquired_at: complete ISO timestamp with timezone required")
        try:
            stamp = datetime.fromisoformat(session["acquired_at"].replace("Z", "+00:00"))
        except ValueError as exc:
            raise DataError("acquired_at: invalid ISO timestamp") from exc
        require(stamp.tzinfo is not None, "acquired_at: timezone required")
        number(session["temperature_celsius"], "temperature", minimum=-273.15)
        require(session["artifacts"] and session["calibration"], "measured data requires artifacts and calibration")
        require(all(c["calibration_id"] for c in session["channels"]), "measured channels require calibration")
        require(session["pickup_geometry_m"] is not None, "measured session requires pickup geometry")
        if session["excitation"]["type"] in ("native_hammer", "controlled_hammer"):
            number(session["excitation"]["hammer_velocity_m_per_s"], "measured hammer velocity", positive=True)
        require(session["rights"]["status"] != "synthetic_original", "measured data rights mismatch")
        text(session["rights"]["permission_reference"], "rights reference")


def validate_components(inventory: dict, profiles: dict[str, dict], source_ids: set[str],
                        task_ids: set[str], sessions: dict[str, dict]) -> None:
    """Check a mechanism inventory, not a verified circuit netlist or full BOM."""
    keys(inventory, {"schema_version", "profile_id", "inventory_kind", "components", "notes"}, "components")
    require(type(inventory["schema_version"]) is int and inventory["schema_version"] == 1, "components: schema version must be 1")
    identifier(inventory["profile_id"], "component profile")
    require(inventory["profile_id"] in profiles, "components: unknown profile")
    require(inventory["inventory_kind"] == "mechanism_requirements_not_bom", "components: schema does not represent a verified circuit BOM")
    text(inventory["notes"], "components notes")
    require(isinstance(inventory["components"], list) and inventory["components"], "components: nonempty list required")
    seen = set()
    for component in inventory["components"]:
        keys(component, {"id", "domain", "mechanism", "unit", "value", "provenance", "source_ids",
                         "measurement_ids", "uncertainty", "implementation_status", "task_id"}, "component")
        identifier(component["id"], "component ID")
        require(component["id"] not in seen, "duplicate component ID")
        seen.add(component["id"])
        require(component["domain"] in ("mechanical", "magnetic", "electrical"), "component: invalid domain")
        text(component["mechanism"], "component mechanism")
        text(component["unit"], "component unit")
        require(component["unit"] in SI_UNITS | {"mixed_SI"}, "component: unsupported unit")
        require(component["provenance"] in ("unknown", "documentary", "assumed", "synthetic", "measured", "fitted"), "component: invalid provenance")
        require(component["implementation_status"] in ("planned", "research_primitive", "reference_implemented", "runtime_implemented"),
                "component: unsupported implementation status; hardware validation is a separate evidence gate")
        references(component["source_ids"], "component source IDs")
        references(component["measurement_ids"], "component measurement IDs")
        require(set(component["source_ids"]) <= source_ids, "component: unknown source reference")
        identifier(component["task_id"], "component task ID")
        require(component["task_id"] in task_ids, "component: unknown task reference")
        if component["value"] is not None:
            number(component["value"], "component value")
            require(component["unit"] != "mixed_SI", "mixed_SI is only a placeholder; split numeric parameters into explicit units")
        if component["uncertainty"] is not None:
            number(component["uncertainty"], "component uncertainty", minimum=0)
            require(component["value"] is not None, "component uncertainty needs a value")
        if component["provenance"] == "unknown":
            require(component["value"] is None and component["uncertainty"] is None and not component["measurement_ids"],
                    "unknown component must retain null value/uncertainty and no measurement claims")
        else:
            require(component["value"] is not None, "known component provenance requires a value")
        if component["provenance"] == "documentary":
            require(component["source_ids"], "documentary value requires a source")
        if component["provenance"] in ("measured", "fitted"):
            require(profiles[inventory["profile_id"]]["identity_status"] == "identified", "measured component requires identified specimen")
            require(component["measurement_ids"] and component["uncertainty"] is not None,
                    "measured/fitted component requires measurement references and uncertainty in the stated unit")
            for ref in component["measurement_ids"]:
                require(ref in sessions, "component: unknown measurement reference")
                session = sessions[ref]
                require(session["kind"] == "measured" and session["specimen_profile"] == inventory["profile_id"],
                        "component measurement must be acquired from the same specimen")
                require(session["split"] == "fit", "component parameter estimation cannot consume held-out validation/test data")
        else:
            require(not component["measurement_ids"], "unmeasured component cannot claim measurement references")


def validate_splits(sessions: list[dict]) -> None:
    """Detect declared condition leakage and identical raw data across splits."""
    seen_ids, splits, hashes = set(), {}, {}
    for session in sessions:
        require(session["session_id"] not in seen_ids, "duplicate session ID")
        seen_ids.add(session["session_id"])
        key = (session["specimen_profile"], session["condition_id"])
        require(key not in splits or splits[key] == session["split"], "condition leaks between fit/validation/test splits")
        splits[key] = session["split"]
        for artifact in session["artifacts"]:
            digest = artifact["sha256"]
            assignment = (session["kind"], session["split"])
            require(digest not in hashes or hashes[digest] == assignment,
                    "identical artifact is relabeled across provenance kinds or data splits")
            hashes[digest] = assignment


def validate_repository(root: Path = ROOT) -> tuple[int, int]:
    sources = strict_load(root / "docs/research/sources.json")
    require(isinstance(sources.get("sources"), list), "sources: list required")
    ids = [source["id"] for source in sources["sources"]]
    for source_id in ids:
        identifier(source_id, "source ID")
    require(len(ids) == len(set(ids)), "duplicate source ID")
    profiles = {}
    for path in sorted((root / "profiles").glob("*/specimen.json")):
        profile = strict_load(path)
        validate_profile(profile, set(ids))
        require(profile["profile_id"] not in profiles, "duplicate profile ID")
        profiles[profile["profile_id"]] = profile
    require(profiles, "no profiles found")
    sessions = []
    for path in sorted((root / "data").rglob("*.json")):
        if "raw" in path.relative_to(root / "data").parts:
            continue
        session = strict_load(path)
        validate_session(session, profiles)
        sessions.append(session)
    validate_splits(sessions)
    state = strict_load(root / "project/state.json")
    require(isinstance(state.get("tasks"), list), "project tasks: list required")
    task_ids = {task["id"] for task in state["tasks"]}
    inventories = set()
    for path in sorted((root / "profiles").glob("*/components.json")):
        inventory = strict_load(path)
        validate_components(inventory, profiles, set(ids), task_ids, {s["session_id"]: s for s in sessions})
        require(inventory["profile_id"] not in inventories, "duplicate component inventory for profile")
        sibling = strict_load(path.with_name("specimen.json"))
        require(sibling["profile_id"] == inventory["profile_id"], "component inventory/profile directory mismatch")
        inventories.add(inventory["profile_id"])
    require(inventories == set(profiles), "each specimen profile requires a component inventory")
    return len(profiles), len(sessions)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=ROOT)
    args = parser.parse_args()
    try:
        profiles, sessions = validate_repository(args.root)
        print(f"Data contracts valid: {profiles} profile(s), {sessions} session manifest(s). Metadata checks do not establish physical accuracy.")
        return 0
    except (DataError, KeyError, TypeError) as exc:
        print(f"Data validation failed: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
