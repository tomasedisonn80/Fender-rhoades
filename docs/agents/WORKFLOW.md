# Agent coordination and automatic state updates

## What this system does

`tools/project.py` maintains a dependency graph, task leases, completion evidence,
and an append-only-by-workflow event history. Every successful task mutation
regenerates `docs/PROJECT_STATE.md`. CI rejects stale generated state, malformed
records, changed evidence artifacts, unfinished completion dependencies, and
unsupported fidelity claims. It uses Python 3.11+ standard-library code and has no
service credentials, model provider, paid API, or network requirement.

It does **not** run agents, execute the command text in an evidence record, assess
the truth of a measurement, write DSP code automatically, commit, push, or create a
release. A human or an authorized agent does that work and records its observed
result. A passing governance check is a consistency check, not certification that
the emulator matches a Rhodes.

## Sources of truth

| Record | Role | Update rule |
| --- | --- | --- |
| `project/state.json` | Task DAG, acceptance, status, ownership, history | CLI for status changes; reviewed edits for new task definitions |
| `project/evidence.json` | Evidence metadata and artifact hashes | `evidence-add`; preserve old records and files |
| `docs/PROJECT_STATE.md` | Readable generated dashboard | `sync`; never edit directly |
| `docs/ROADMAP.md` | Scientific milestone rationale | Update when scope or gates change; preserve stable task IDs |
| `docs/agents/CONTINUE.md` | Agent startup and handoff prompt | Update when operating procedures change |
| Specimen profiles and data manifests | Actual parameter and measurement provenance | Validate with data tools; never substitute synthetic fixtures for measurements |

The dependency graph is authoritative for ordering. Read the acceptance criteria
of a task before choosing work. `SPEC-001` and `DATA-001` remain blocked until an
identified physical specimen and actual calibrated acquisition are available.
Schemas and fixture files are useful foundation work; they do not satisfy those
measurement tasks.

## Start a work session

From the repository root:

```sh
git status --short --branch
python3 tools/project.py validate
python3 tools/project.py sync --check
python3 tools/project.py next
```

Read `AGENTS.md`, `README.md`, `docs/PROJECT_STATE.md`, `docs/ROADMAP.md`, the
relevant architecture and validation sections, and the selected task in
`project/state.json`. Inspect current code and recent commits. A task's title is
not sufficient specification. Respect the user's requested branch and the
integrator's file assignments; preserve other workers' uncommitted changes.

Claim the task before making changes:

```sh
python3 tools/project.py claim PHY-002 --owner agent-unique-id --minutes 120
```

The JSON response contains the token and UTC expiry. The token coordinates state
updates; it is not a credential or access-control secret. Only a matching active
owner/token can complete, release, block, or renew that claim. A second claim on a
live lease fails. An expired lease may be reclaimed and receives a new token.

Renew before expiry if work continues:

```sh
python3 tools/project.py renew PHY-002 --owner agent-unique-id --token TOKEN --minutes 120
```

Leases last 1–1440 minutes. Stop modifying assigned files if you lose the lease.
The tool coordinates task records, not filesystem edits: an integrator must also
avoid overlapping file ownership and ensure an old worker has stopped before
reassigning its files.

## Parallel work contract

The integrator assigns each worker a stable identifier, narrow objective, exact
writable paths, required independent checks, and an expected report path. Workers
read shared context but edit only their assigned paths. One integrator owns
canonical state, Git staging, commits, and pushes. Workers report findings rather
than racing to mutate the shared JSON or branch.

Use only genuinely available concurrency. Record actual spawned assignments and
completed reports. Never report simulated perspectives, planned assignments, or
the attached report's earlier research agents as agents used in the current setup.

Every worker handoff must state:

1. Assignment ID, objective, owned paths, and inspected baseline.
2. What changed or was reviewed, including relevant equations and assumptions.
3. Commands actually run, exit outcomes, tolerances, and saved artifacts.
4. Findings, unresolved failures, and explicit untested conditions.
5. Exact next action and any parameter, measurement, or integration blockers.

## Evidence and completion

Save a durable report of the **already observed** check. Include the command,
configuration, relevant source revision or working-tree description, result,
limitations, and detailed output needed to evaluate the claim. Keep completed
evidence artifacts immutable. If a result is corrected, append a new artifact and
record; do not silently rewrite the old file.

```sh
python3 tools/project.py evidence-add PHY002-numerical-001 \
  --kind numerical --result pass \
  --summary "Contact energy and timestep convergence checks passed under the recorded synthetic conditions" \
  --command "ctest --test-dir build --output-on-failure" \
  --artifact docs/validation/PHY002-numerical-001.txt \
  --limitation "Synthetic contact constants; no hardware fidelity established"
```

This command hashes the artifact and records the supplied metadata. It never
executes `--command`. Use `--source-commit` with a full Git object ID when the
artifact refers to a committed source snapshot; otherwise leave it absent and
explain the exact working-tree state in the artifact. Do not claim a source commit
that excludes the tested changes.

Use `--kind measurement --specimen SPECIMEN_ID` for actual calibrated observations.
For held-out hardware validation use `--kind hardware_validation --specimen
SPECIMEN_ID --independent --reference-evidence MEASUREMENT_ID`. The tool requires a
passing measurement reference for the same specimen. It cannot establish the
statistical independence or authenticity of the underlying observations; reviewers
must inspect the manifest and acquisition method.

After every acceptance criterion is satisfied:

```sh
python3 tools/project.py complete PHY-002 \
  --owner agent-unique-id --token TOKEN \
  --evidence PHY002-numerical-001
python3 tools/project.py validate
python3 tools/project.py sync --check
```

Repeat `--evidence` when a task requires multiple kinds. Completion requires all
dependencies done, all listed evidence passing, every required evidence kind
present, and every artifact hash intact. Claiming a task or adding evidence does
not complete it. Completing a synthetic or numerical task never promotes the
scientific fidelity status.

## Blockers, handoff, and regressions

Record a concrete missing input or failed acceptance criterion:

```sh
python3 tools/project.py block PHY-002 --owner agent-unique-id --token TOKEN \
  --reason "Contact solver loses energy above the declared tolerance; failing fixture and next diagnosis are in the handoff report"
```

Release unfinished work so another agent can claim it:

```sh
python3 tools/project.py release PHY-002 --owner agent-unique-id --token TOKEN \
  --reason "Handoff: implementation complete in assigned paths; run the documented convergence sweep before completion"
```

`release` sets status to `todo`; `block` sets it to `blocked`. Both clear the claim
and preserve a reason in history. Resolve a blocker with a recorded explanation:

```sh
python3 tools/project.py unblock PHY-002 --owner integration-root \
  --reason "The convergence defect was resolved and the new artifact records the fix"
```

`unblock` does not bypass unfinished dependencies or complete the task. When a
regression invalidates completed work, use `reopen TASK --owner OWNER --reason
REASON`. Reopen completed dependents first, working backward through the DAG.
Previous evidence remains in the registry as historical evidence; create new
evidence for the corrected implementation. Reopening a measured-fidelity gate
also returns its corresponding fidelity claim to `unvalidated`.

## Recovery and deterministic checks

Writes use a temporary file, file flush/fsync, and atomic replacement. A directory
lock serializes cooperative writers across processes. The lock waits at most two
seconds and never steals a potentially live lock. After a process crash, inspect
`project/.project.lock/owner.json`, verify the writer is no longer running, then
remove that lock directory before retrying. Ignore this transient directory in
Git.

The canonical state and generated Markdown are separate atomic file writes. If a
crash occurs between them, the JSON is authoritative: run `validate`, then `sync`,
then `sync --check`. Never reconstruct state by scraping the Markdown. Evidence
registration writes only the evidence registry; a subsequent completion references
it, so a crash cannot create a done task that references an unwritten record.

Generated output contains the last recorded state timestamp, not the wall clock.
`sync` is therefore reproducible. `next` uses the current clock only to decide
which leases are available. The dashboard shows recorded lease expiries without
time-dependent regeneration.

## Integrator finish criteria

Run the relevant C++ tests, Python tests, data validation, state validation, and
generated-file check. Review the full diff, evidence consistency, and license/source
notes. Commit tested implementation and its state/evidence changes together.
Push only as authorized by the user. Verify the resulting remote commit and CI;
report pending or failed platform checks accurately. No tool in this workflow
grants release, publication, messaging, or fidelity authority on its own.
