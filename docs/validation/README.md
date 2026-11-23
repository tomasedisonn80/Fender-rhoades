# Validation evidence

Store compact, immutable result reports here. Keep large raw captures in the
approved data location and bind them through manifests and verification reports.
No physical specimen or calibrated instrument measurements have been supplied.

## Development decisions — 2026-11-23

| Record | Scope and result |
|---|---|
| [Experimental VST3 acceptance](host-acceptance-20261123.md) | HOST-001 criterion review for the declared synthetic Linux prototype |
| [Final integration record](development-final-validation-20261123.json) | Source hashes, complete logs, 138 CTests, 47 SDK checks and 275 Python tests |
| [Open contact/runtime gates](research-gates-20261123.md) | PHY-002 and RT-001 remain incomplete; next experiments and historical bindings |
| [Contact failure examples](contact-failure-examples-20261123.json) | Six retained raw trajectories and a verifier reproducing seven failed refinements |
| [Historical runtime raw archive](runtime-historical-raw-20261123.json) | Sixteen exact raw timing files and a self-contained arithmetic replay |
| [Actual VST3 callback measurements](vst3-callback-benchmark-20261123.json) | 3,600 observed callbacks with two nominal-period overruns; no deadline guarantee |
| [Aliasing observations](aliasing-observations-20261123.json) | Limited synthetic diagnostic, not complete antialiasing acceptance |

The [development campaign](../agents/DEVELOPMENT_CAMPAIGN.md) identifies each
scoped worker and immutable report. These dated records retain their observed
source and result; live task status remains in [PROJECT_STATE](../PROJECT_STATE.md).

## Evidence discipline

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
