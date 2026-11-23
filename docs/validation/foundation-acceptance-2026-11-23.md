# Foundation acceptance — 2026-11-23

Evaluator: `integration-root`, with independent scoped reviews and a separate
clean-checkout verifier. This is a software/documentary acceptance decision for
**FND-001**, not physical-instrument validation. The two required registry kinds
are `documentary` and `numerical`; there are no task dependencies.

## Frozen definition and source

Target: **One documented 73-key Mark I Stage specimen at its specified loaded
electrical output; identity and measurements pending.**

Definition SHA-256:
`3a6d38f2371bbb535f601c17fb31e1f50e8704693b9f062f45b942303f4a6d24`.
The [source manifest](foundation-source-manifest-2026-11-23.json) includes the
exact definition projection, source-file hashes, scope and exclusions. Its
serialization follows [ACCEPTANCE_REVIEW](../agents/ACCEPTANCE_REVIEW.md).

Tested source commit:
`6cf5f3af25a6c989eb244db070681be069e7fd78`.
Tree: `5950625afe680dcb9b107e5be793a249a681c51f`.
The integrator compared the remotely created tree with the local Git index,
published with a non-forced expected-head update, fetched the result and verified
both IDs. The working tree was clean at that source freeze. Subsequent acceptance
reports, campaign/evidence records and milestone transitions do not pretend to
have existed in the source commit.

## Criterion-by-criterion decision

| Criterion and exact acceptance text | Supporting evidence | Observed coverage and source | Remaining limitation | Decision |
|---|---|---|---|---|
| `FND-001/C01`: Buildable dependency-free research kernel, developer setup and platform CI configuration exist. | CMake targets, README/DEVELOPMENT, pinned workflow; [numerical report](analytic-primitives-2026-11-23.md); [A099](../agents/reviews/A099.md) | Original C++20 core, renderer and benchmark build without downloaded runtime packages; Release and sanitizer suites each passed 25 tests locally. CI declares C++ and Python checks on three operating systems plus Linux sanitizers. | Platform configuration is distinct from observed execution; no plugin, installer, minimum-version toolchain execution or physical-model fidelity is inferred. | Pass within the stated software criterion |
| `FND-001/C02`: Canonical state, evidence validation, generated status and agent continuation workflow pass their tests. | 171-test Python run; state/data/document/campaign validators; [A075](../agents/reviews/A075.md), [A089](../agents/reviews/A089.md), [A099](../agents/reviews/A099.md) | Dependency and lease enforcement, immutable artifact checks, typed specimen/session links, regression revalidation, deterministic document generation, malformed-input rejection, payload verification and campaign integrity exercised. Role-specific continuation, scoped ownership and integrator handoff reviewed. | Hashes/types/fresh IDs cannot certify that an experiment occurred or that evidence is scientifically relevant. Cooperative locking and trusted directories are assumed; this is not a worker launcher or unattended development service. | Pass within the stated software criterion |
| `FND-001/C03`: Research scope, synthetic constants, missing measurements and licensing uncertainty are documented. | README, NUMERICS, RIGHTS, research archive/manifest, unidentified profile, detailed specifications; [A097](../agents/reviews/A097.md), [A098](../agents/reviews/A098.md) | Current modal/impulse/switched-loss/polynomial/resistive approximations and future physical interactions are distinguished. Unknown specimen/components remain unknown. The separate referenced PDF was unavailable; source-review status and reuse limits are recorded. Action-noise and runtime-handoff gaps were corrected before source freeze. | No calibrated captures, authenticated specimen, owner-selected project license, or measured fidelity. Many linked leads remain explicitly unreviewed. | Pass within the stated documentary criterion |

## Observed checks and environment

Local environment: Linux x86_64, GNU C++ 14.2.0, CMake 4.4.5, Ninja and Python
3.12.14. Commands actually executed on the integrated source before freezing:

```sh
cmake --preset release
cmake --build --preset release --parallel 2
ctest --preset release --output-on-failure
cmake --preset sanitize
cmake --build --preset sanitize --parallel 2
ASAN_OPTIONS=detect_leaks=0 ctest --preset sanitize --output-on-failure
python -m unittest discover -s tests/python -v
python tools/project.py validate
python tools/project.py sync --check
python tools/validate_data.py
python tools/check_docs.py
python tools/campaign.py validate
python tools/campaign.py sync --check
```

Release **25/25**, sanitizer **25/25**, and Python **171/171** passed. Local leak
detection was disabled for the workspace; leak freedom is not established. UBSan
halt/stack-trace options came from the sanitizer test preset. The final Python
run followed all metadata/code changes; later source-freeze edits were scoped
documentation and canonical task paths. The independent frozen-checkout result
below checks the committed combination.

State/data/document validators passed with 13 tasks, one unidentified profile,
one explicitly synthetic session template and `unvalidated` fidelity. The
partial campaign was valid with 98 completed reports at the source freeze; its
final completion is recorded separately after the last two real workers return.
Synthetic test fixtures, including simulated campaign ledgers, are not counted
as actual agent assignments or measurements.

### Independent frozen-checkout result

[A099](../agents/reviews/A099.md) cloned the public remote into a new directory,
verified the exact frozen commit/tree and ran the documented generic Release
configure/build/CTest flow. All **25 CTest cases** and **171 Python tests** passed;
the Python run had **zero skips**. All six project/state/data/docs/campaign
integrity commands passed. Live and dangling renderer symlink cases ran for both
default and forced publication. The checkout remained clean ignoring its build
and Python-cache artifacts. A100 independently inspected the verifier's logs;
no local preexisting build tree was needed for this result.

### Hosted execution boundary

At the 2026-11-23 review, the earlier source commit
`b66bd5ce2e8b1df5d43db03330b6b9a08629cb61` had successful hosted Linux C++, Windows
C++, Linux sanitizer and project/provenance jobs in
[run 45173529599](https://github.com/baabi-boo/Fender-rhoades/actions/runs/45173529599).
Its macOS job was queued at inspection; the run was subsequently superseded and
cancelled when the frozen source was pushed. These are observations of that earlier commit, not
claims that the expanded seven-job matrix or the frozen source passed all hosts.
The current workflow's existence satisfies the configuration clause; inspect
the exact later commit's Actions run for updated platform evidence.

## Integration decision and limits

All three foundation criteria pass in their declared software/documentary scope,
including the independent frozen-checkout result above. Register this
criterion review as documentary evidence and the numerical report separately;
complete FND-001 only with both records and its valid integrator lease. The
evidence CLI records commands without executing them. The named integrator owns
the relevance decision; [A100](../agents/reviews/A100.md) independently reviews it.

Keep SPEC-001 and DATA-001 blocked. Completion neither changes the fidelity status
nor completes finite contact, magnetic identification, reactive loading, Stage
controls, runtime deadlines, full-keyboard calibration or a host product. The
owner's software-license choice is a future release prerequisite, not an invented
approval. Report discrepancies through new immutable records instead of editing
this report after registration.
