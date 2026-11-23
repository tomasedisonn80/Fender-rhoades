# Canonical project and evidence format

## Version and validation

Both canonical JSON files use integer `schema_version: 1`. The structural task
schema is `schemas/project-state.schema.json`. The executable validator in
`tools/project.py` additionally checks dependency cycles, cross-record references,
completion gates, artifact contents, typed profile/session links, history ordering,
and path safety. It rejects
unknown object fields, duplicate JSON keys, nonfinite JSON constants, invalid UTC
timestamps, unsupported statuses, and unsafe repository-relative paths.

Use Python 3.11 or newer. The tool is standard-library-only. It does not download
schemas or dependencies. Changes to the format require synchronized schema,
validator, fixtures, documentation and migration review. Do not reinterpret a
field silently under the same schema version.

The foundation added `profile_path`, `session_paths`, and `specimen_identity`
before its first evidence record was registered. All version 1 records now require
the complete field set below; missing fields are rejected rather than inferred
from artifact names. No populated registry was migrated by this change.

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
| `kind` | `documentary`, `synthetic`, `numerical`, `specimen_identity`, `measurement`, `hardware_validation`, or `performance` |
| `result` | `pass`, `fail`, or `inconclusive` |
| `summary` | What was observed and which claim it supports |
| `command` | Already-executed command text or `null`; never executed by this tool |
| `created_at` | UTC registration timestamp |
| `source_commit` | Full lowercase Git SHA-1/SHA-256 object-ID syntax or `null`; object existence and association with tested source are not checked |
| `artifacts` | Nonempty array of `{path, sha256}` objects referencing immutable local evidence |
| `specimen_id` | For the three specimen evidence kinds, exactly the linked profile's `profile_id`; this is not the serial-number field |
| `profile_path` | Explicit repository-relative specimen-profile path for `specimen_identity`, `measurement`, and `hardware_validation`; `null` for other kinds |
| `session_paths` | Unique explicit acquisition-manifest paths for `measurement` and `hardware_validation`; `[]` for other kinds |
| `independent` | Whether validation conditions are held out; human-reviewed declaration |
| `reference_evidence` | Supporting registry IDs; hardware validation requires passing measurements covering every linked observation and the same specimen/profile snapshot |
| `limitations` | Explicit scope and uncertainty statements |

`synthetic` and `numerical` records cannot claim a measured specimen. The explicit
profile/session fields are reserved for the three specimen evidence kinds; other
kinds can include metadata files as ordinary opaque artifacts. The validator does
not guess a JSON file's role from its extension, name, or contents.

| Kind | Required declared metadata |
| --- | --- |
| `specimen_identity` | A valid `identified` profile whose `profile_id` equals `specimen_id`; no acquisition sessions. Identity documentation has its own gate and does not require completed acquisition. |
| `measurement` | The same identified-profile requirement plus at least one valid `measured` session that references that profile. A measurement may have `fit`, `validation`, or `test` split; registration alone does not authorize using held-out data to estimate parameters. |
| `hardware_validation` | The same profile/session checks, every session in `validation` or `test`, `independent: true`, and passing measurement references that collectively cover all linked sessions. |

Every `profile_path` and entry of `session_paths` must also occur in the same
record's immutable `artifacts` list, using the same path spelling. Profiles and
sessions are checked by the existing data-contract functions with a strictly read
source registry. Profile identity, revisions, wiring declaration, output boundary,
calibration, acquisition fields, rights declarations, and session/profile agreement
must satisfy those contracts. A manifest explicitly declaring `synthetic` is
rejected as measurement or hardware-validation evidence even when its hash is
valid and its filename sounds like an acquisition.

Hardware reference coverage uses the profile's SHA-256 and each session's
`session_id` plus manifest SHA-256, along with the same `specimen_id`. A measurement
of another session or a different profile snapshot does not cover a validation
observation. Identical immutable manifests may be referenced by several records;
they count once when checking duplicate session IDs, declared condition leakage,
and identical payload hashes across splits. Those split checks cover explicitly
linked sessions. Run `tools/validate_data.py` separately for the repository's full
profile, session, and component inventory contracts.

Artifacts use portable relative paths, must exist, and must match SHA-256 on every
validation run. Paths cannot leave the checkout, enter `.git`, or follow a symlink
outside the checkout. Hashes detect changed files; they do not prove when a file
was produced or what generated it. Store large measurement payloads using the
project's data policy and commit an immutable manifest/report that identifies and
hashes the external payload. The state tool does not fetch external data or open
the raw payload paths named inside a session manifest. Tool-managed mutable state,
evidence registries, generated dashboards, and transient locks cannot serve as
immutable artifacts. Freeze linked profile/session snapshots; a correction needs
a new artifact path and record instead of changing already registered bytes.

## Trust boundary and maintenance

The CLI enforces consistent declarations and honest bookkeeping when used as
documented. A repository editor can still falsify an artifact, label an observation
incorrectly, or rewrite JSON and its matching hash. Peer review, acquisition
records, CI provenance and hardware inspection supply the evidentiary trust that
the state validator cannot. Do not describe this as tamper-proof certification.
Typed links check agreement between declarations. They do not establish that the
instrument exists, an acquisition occurred, calibration/rights references are
authentic, raw data match the manifest, or held-out conditions are statistically
independent. Reviewers still assess relevance to acceptance criteria. Likewise,
`source_commit` is format-checked only; reviewers verify the actual source object
and which changes were tested.

Adding new tasks requires a reviewed JSON edit with stable IDs, complete
acceptance, dependencies and evidence kinds. Validate, regenerate the dashboard,
and update the roadmap in the same change. Preserve completed history and
evidence. When a historical result no longer applies, append a new result and
reopen the affected tasks instead of erasing the earlier observation.

Schema version 1 intentionally has no model provider secrets, scheduling,
automatic code generation, automatic commits, or network-side state. Those would
be separate integrations with explicit authorization and their own validation.
