# Measurement data contract

There are **no measurements of a physical Rhodes in this repository yet**.
`examples/synthetic-session.json` is a metadata-validation fixture, not an acquired
session or a calibration source. No measured fidelity gate can use it.

Raw recordings and motion/impedance captures stay in `data/raw/` (ignored) or an
owner-approved immutable store. Commit compact manifests containing exact hashes,
units, calibration, instrument identity, rights, acquisition conditions, and split
assignment. Do not rename synthetic fixtures to make them appear measured.

Read [the measurement plan](../docs/MEASUREMENT_PLAN.md). Fit/validation splits
separate whole acquisition conditions and sessions, not adjacent windows from one
recording. Loading, pickup geometry, mechanics, temperature, and calibration form
part of the observation. Hashes establish identity; they do not establish accuracy.

Run `python3 tools/validate_data.py` for committed contracts and fixtures. An
unidentified profile may contain null values. A measured manifest requires an
identified specimen and complete acquisition/calibration metadata.

The current version-1 checks are structural. They do not verify raw payload bytes,
resolve calibration certificates, enforce calibration validity dates, or discover
derived-data ancestry. The [specimen and acquisition acceptance contract](../docs/specs/DATA_INGESTION.md)
specifies the coordinated schema upgrade and review gates required before actual
measurement tasks can be completed.

## Verify actual local payload bytes

For a real local capture, run the separate opt-in verifier after metadata checks:

```sh
python3 tools/verify_payloads.py --manifest data/manifests/SESSION.json \
  --payload-root data/raw --report docs/validation/SESSION-payloads.json
```

`SESSION` names files you have actually acquired; this is a usage template, not a
command that can verify the metadata-only synthetic example. The report's parent
directory must already exist. Paths in `artifacts` are relative to the explicit
payload root: `session/take.wav` above means `data/raw/session/take.wav`. Do not
repeat `data/raw` in the artifact path.

The verifier binds the manifest hash to the exact bytes parsed, then reads regular
files and compares actual counts and SHA-256 values. It rejects symlink/reparse
components, including root ancestors, and checks ordinary concurrent changes.
Use trusted, quiescent local directories. It is not hardened against adversarial
directory races on every platform and cannot guarantee bytes remain unchanged
after inspection.

Reports must be new files outside the payload root. Publication requires local
hard-link support and never overwrites an existing report. Exit `0` means byte
agreement; `1` means a failure report was published; `2` means invocation, setup
or publication failed. It checks the artifact list only: full session/profile
validation, calibration validity, rights and scientific authenticity remain
separate. It imports nothing and fetches no external data.

Keep verifier reports under `docs/validation`, outside session discovery. Version 1
treats JSON beneath `data` as session manifests unless a path component is `raw`.
Calibration records and immutable configuration archives need the coordinated
discovery/schema upgrade described in the acquisition contract.

A temporary-file cleanup warning after publication does not reverse the result;
the complete final report remains available. A reported leftover temporary path
may be removed after confirming no writer is active. Publication does not provide
a power-loss durability guarantee for the containing directory.
