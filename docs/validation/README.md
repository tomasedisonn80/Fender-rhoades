# Validation evidence

Store compact, immutable result reports here. Keep large raw captures in the
approved data location and bind them through manifests and verification reports.
No physical specimen or calibrated instrument measurements have been supplied.

Each result identifies the actual procedure/command, source revision or tested
file hashes, environment, inputs, numerical tolerances, observed outcomes and
limitations. Separate local checks, hosted CI, payload integrity and physical
validation. A proposed test belongs in a specification until it has run.

Register completed reports with `tools/project.py evidence-add`, using the correct
kind and typed profile/session links when required. Once a report is registered by
hash, append a new correction or follow-up instead of editing the old report.
Generated dashboards and canonical registries are mutable and cannot serve as
immutable evidence artifacts.

For scientific acceptance, follow [VALIDATION](../VALIDATION.md) and the
[acquisition contract](../specs/DATA_INGESTION.md). For actual local payload bytes,
use the [opt-in verifier](../../data/README.md). Its output is byte-integrity
evidence only. Future public reports must not disclose private operator/owner
information, credentials or unauthorized recordings.
