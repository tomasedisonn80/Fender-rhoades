# Canonical project and evidence format

## Version and validation

Both canonical JSON files use integer `schema_version: 1`. The structural task
schema is `schemas/project-state.schema.json`. The executable validator in
`tools/project.py` additionally checks dependency cycles, cross-record references,
completion gates, artifact contents, history ordering, and path safety. It rejects
unknown object fields, duplicate JSON keys, nonfinite JSON constants, invalid UTC
timestamps, unsupported statuses, and unsafe repository-relative paths.

Use Python 3.11 or newer. The tool is standard-library-only. It does not download
schemas or dependencies. Changes to the format require synchronized schema,
validator, fixtures, documentation and migration review. Do not reinterpret a
field silently under the same schema version.

## State object

| Field | Meaning |
| --- | --- |
| `schema_version` | Exactly `1` |
| `project`, `goal`, `target` | Nonempty project identity and scientific scope |
| `fidelity_status` | `unvalidated`, `measured_one_note`, or `measured_keyboard` |
| `revision` | Nonnegative integer; one task mutation event per revision |
| `updated_at` | UTC `YYYY-MM-DDTHH:MM:SSZ` of the last task mutation |
| `tasks` | Nonempty list of task objects with stable unique IDs |
| `history` | Sequential task mutation events; retain previous entries |

`fidelity_status` is a scoped scientific statement, not a percentage. It never
increases automatically. A reviewed promotion to `measured_one_note` requires
`VAL-001` done with passing hardware validation evidence; `measured_keyboard`
requires the corresponding `KEY-001` gate. The validator also checks all completed
task dependencies. Reopening a corresponding gate removes its fidelity claim.
These consistency rules cannot establish the truth or adequacy of the evidence.

## Task object

| Field | Meaning |
| --- | --- |
| `id` | Stable identifier such as `PHY-002`; uppercase prefix plus three digits |
| `title`, `milestone` | Nonempty human-readable scope and roadmap grouping |
| `status` | `todo`, `in_progress`, `blocked`, or `done` |
| `priority` | Integer 0–1000; lower values are selected first, then ID order |
| `dependencies` | Unique prerequisite task IDs; the graph must be acyclic |
| `acceptance` | Nonempty array of concrete criteria; reviewers check actual satisfaction |
| `required_evidence` | Nonempty array of required evidence kinds |
| `evidence` | Evidence IDs attached to the task; done tasks require passing evidence of every required kind |
| `owner` | Agent/integrator identifier or `null` |
| `claim` | Active/expired lease record or `null` |
| `blockers` | Nonempty explanations only when status is `blocked` |
| `paths` | Repository-relative scope hints; integrator assigns disjoint writable files |

A claim contains `owner`, a coordination `token`, `claimed_at`, and `expires_at`.
Its owner must match the task owner and the task must be `in_progress`. An
unclaimed `in_progress` task is allowed as an explicit initial/imported integration
assignment; acquire a claim before using mutation commands. Lease expiry does not
silently alter the canonical record. A new claim replaces an expired one and
records a history event. A token is not an authorization mechanism against a
contributor who can edit the repository.

History events contain `revision`, `at`, `action`, `task`, `owner`, and `detail`.
Revisions are sequential and timestamps cannot run backward. `claim`, `renew`,
`complete`, `release`, `block`, `unblock`, and `reopen` append events. A completed
task can be reopened after a discovered regression; completed dependents must be
reopened first. Prior evidence stays in the registry.

## Evidence object

The registry contains `schema_version` and `records`. Each record has:

| Field | Meaning |
| --- | --- |
| `id` | Unique stable evidence ID; 1–96 letters/digits/dot/underscore/hyphen |
| `kind` | `documentary`, `synthetic`, `numerical`, `measurement`, `hardware_validation`, or `performance` |
| `result` | `pass`, `fail`, or `inconclusive` |
| `summary` | What was observed and which claim it supports |
| `command` | Already-executed command text or `null`; never executed by this tool |
| `created_at` | UTC registration timestamp |
| `source_commit` | Full lowercase Git SHA-1/SHA-256 object ID or `null` |
| `artifacts` | Nonempty array of `{path, sha256}` objects referencing immutable local evidence |
| `specimen_id` | Actual specimen identity for measurement/hardware evidence; otherwise `null` when inapplicable |
| `independent` | Whether validation conditions are held out; human-reviewed declaration |
| `reference_evidence` | Supporting registry IDs; hardware validation must reference passing same-specimen measurement |
| `limitations` | Explicit scope and uncertainty statements |

`synthetic` and `numerical` records cannot claim a measured specimen. Hardware
validation requires `independent: true` and at least one passing measurement
reference for that specimen. A synthetic render cannot satisfy a measurement
gate by being assigned a convincing filename.

Artifacts use portable relative paths, must exist, and must match SHA-256 on every
validation run. Paths cannot leave the checkout, enter `.git`, or follow a symlink
outside the checkout. Hashes detect changed files; they do not prove when a file
was produced or what generated it. Store large measurement payloads using the
project's data policy and commit an immutable manifest/report that identifies and
hashes the external payload. The state tool does not fetch external data.

## Trust boundary and maintenance

The CLI enforces consistent declarations and honest bookkeeping when used as
documented. A repository editor can still falsify an artifact, label an observation
incorrectly, or rewrite JSON and its matching hash. Peer review, acquisition
records, CI provenance and hardware inspection supply the evidentiary trust that
the state validator cannot. Do not describe this as tamper-proof certification.

Adding new tasks requires a reviewed JSON edit with stable IDs, complete
acceptance, dependencies and evidence kinds. Validate, regenerate the dashboard,
and update the roadmap in the same change. Preserve completed history and
evidence. When a historical result no longer applies, append a new result and
reopen the affected tasks instead of erasing the earlier observation.

Schema version 1 intentionally has no model provider secrets, scheduling,
automatic code generation, automatic commits, or network-side state. Those would
be separate integrations with explicit authorization and their own validation.
