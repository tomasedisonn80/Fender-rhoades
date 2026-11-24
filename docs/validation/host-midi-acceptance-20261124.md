# HOST-002 criterion review — 24 November 2026

**Decision: incomplete. C01–C03 pass within their frozen synthetic scope; C04
remains incomplete at the latest retained native-status cutoff, 2026-11-24T11:09:46.569Z.**
The complete numerical/native results retain W5-081's earlier 10:57:30.673Z
cutoff; W5-093 subsequently observed all three remaining macOS jobs still queued
without runners or steps. No successful retry execution or native macOS pass is
inferred. This review records the incomplete outcome and does not authorize
completion of HOST-002.

Evaluator: **continuation-c-root**, integrator; evaluated **2026-11-24T11:14:28+00:00**.
This review incorporates worker088's synthesis, worker092's independent review,
and the source/evidence checks identified below. The integrator rechecked all
99 current C++ input hashes and all 27 artifact references in the four registered
HOST-002 numerical records, and found no runtime/build/test/tool drift from d4.
No new build, test, numerical experiment or physical observation is claimed by
this documentary review. The completion contract is
`docs/agents/ACCEPTANCE_REVIEW.md`, SHA-256
`11bbc5feef1017fb99a60ae1967d8c8e775ddf029d981b6af37bed22b75eee85`.
Paths are repository-relative. W-prefixed report names resolve under
`docs/agents/reports/continuation-c/`. Later native outcomes require a new
immutable supplement and a new completion decision; preserve this observation.

## Frozen definition and registration

Task: **HOST-002 — Improve VST3 MIDI interoperability and generic control
validation**. Exact target: **One documented 73-key Mark I Stage specimen at its specified loaded electrical output; identity and measurements pending.**

Dependency: `HOST-001`, observed `done` in state revision 39, with
`HOST-20261124-current-native` and `HOST-20261124-final-review`. That dependency's
historical evidence does not validate HOST-002. Required evidence kinds, in
canonical order: `numerical`, `documentary`.

Definition SHA-256:
`eefa42abc22e4a677e76f1968e9c1760650b1031496e795a2fd7d6219b59701c`.
Recomputed from UTF-8 JSON of `task_id`, canonical `acceptance`, `dependencies`,
`required_evidence` and `state.target`, with sorted keys, `ensure_ascii=False`
and `separators=(',', ':')`. The four exact clauses appear below. The design
document's five acceptance bullets do not replace or renumber them.

Three passing numerical records are registered:
`HOST-002-20261124-linux-integration`,
`HOST-002-20261124-legacy-compatibility`, and
`HOST-002-20261124-native-linux-windows`. During this campaign the integrator registered
`HOST-002-20261124-native-platform-observation` as numerical/**inconclusive**,
incorporating W5-081's consolidation/log/report and W5-087's independent audit.
Worker088 and the integrator verified all 27 artifacts across these four records.
At evaluation, before this documentary record's registration, HOST-002's task
evidence list was empty. Registration and any later handoff state transition
do not change the observed executions or this incomplete criterion decision.
Registry kind/status/count checks cannot determine criterion relevance.

## Criterion decisions

| Criterion and exact text | Supporting evidence and relevant condition | Remaining limitation | Decision |
| --- | --- | --- | --- |
| **HOST-002/C01:** Deliver bounded channel-specific CC123 commands with chronological ownership preflight, retained sustain/tails, zero-frame handling and unchanged saved configuration. | Registered Linux integration and native Linux/Windows records; W5-082; command bounds, atomicity, partition, release-port and repaired loaded-MIDI execution. Independent ownership/scalar comparison checks exact audio, identities, mechanics, cursor, configuration and continuation. | Exact commuting channel/sustain dispatch order requires source review. Generic release fault injection does not execute combined host-publication failure. No foreign-getter or WCRT guarantee. | **Pass, scoped synthetic behavior.** |
| **HOST-002/C02:** Accept validated polyphonic pressure without dropping co-batched note events; reject malformed inputs atomically. | Same integration/native records; W5-083; pressure-present/omitted pairs, malformed-field/retrieval/order/capacity cases, inactive outputs and pending-recall retry. State and subsequent audio are checked after rejection. | Pressure remains a synthesis no-op. Finite fixtures do not establish arbitrary host memory/getter behavior or DAW delivery. | **Pass, scoped synthetic behavior.** |
| **HOST-002/C03:** Reject malformed generic-control text and nonfinite conversions without changing output values or saved state. | Registered Linux/native records; W5-084; independent parser vectors, all public controller variants, loaded text rejection, and d4 Windows core execution. Both conversion outputs and saved bytes are checked. | Domain checks apply after binary64 rounding; SDK buffer preconditions remain. Rounding/flush controls are preserved, not all sticky status flags. | **Pass, scoped synthetic behavior.** |
| **HOST-002/C04:** Pass independent ownership/audio comparisons, compatibility checks and native SDK/module validation for the claimed platforms; document remaining DAW-specific verification. | Three passing numerical records plus registered inconclusive native-platform observation; W5-085/087; W3-041/048/050; W2-033/036; W5-081's consolidation; `docs/DAW_VALIDATION.md`. Finite independent ownership/audio and legacy compatibility checks pass; Linux/Windows native validators and loaded execution pass. | Required applicable macOS VST3/core completion is absent at cutoff. Retry is queued; interrupted Python lacks a terminal summary. Historical three-platform results cannot fill the gap. | **Incomplete. HOST-002 cannot complete.** |

## Source, binary, model and input binding

Published HEAD observed here is `4cf0de7089e5f622916cc5f6a25bb4034715d781`;
runtime/test code corresponds to `d4c8a6b2668e478f48facd2eeb146aff0f551506`.
The read-only d4-to-4cf diff contains documentation, evidence and campaign records,
not runtime/test changes. Current uncommitted documentary work is not a clean
source/build claim.

The local integration actually used an existing incremental Release build at
dirty `ce59b0c254ab99b399f03e63094b10ce10933e71`, with the W4-064 generated fixture
header/generator repair. Its 99-file before-build, after-build and after-CTest
manifest is unchanged:
`e3d94b5f9c8d3bb259311ca43e79164f1e65bdd3f6409a5af2746c2eea6786c1`.
Worker088 and the integrator matched all 99 current files. W4-073 independently checked equality to
d4 and the registered 31,865-byte source-bridge log, SHA-256
`6cc32d506d8b43533f9e24b219ddb2355affd7dd3bd430cda360af3f151cedef`.
This establishes source applicability, not a clean d4 build or independent
incremental-binary provenance. Python's 89-file d4 binding is after-run readback,
without a before/after freeze or complete non-Python input binding.

W5-081 separately binds ce59 VST3's 91 applicable source/build files to current
bytes (manifest `4346f55625331bb05221ae298241960f18adbf9805f068dbe90fd0b0e3d92eb6`)
and d4 Foundation's 195 files
(`03d031c839285781f593787cd0d7311db6ce6712d03ab9eff9e96228b3414867`).
Worker088 rechecked both maps and the selected-log companion. These scoped maps
do not promote current documentation to remotely tested content.

The Linux record's `binaries` section binds the actual three modules, 85 project
executables and core archive. The loaded compatibility record binds all six
baseline/current module and metadata pairs, harness executable/source and driver;
its explicit event/recall schedule and raw-stream manifest are retained. The WAV
record binds renderer `44d32bd09440090aeff07ccb4784bdc3c7dd6db6e71fc3ca69b85f50dbedd9e2`,
driver/helper hashes and per-case inputs/outputs. These existing manifests are
incorporated through the exact artifact hashes below, rather than duplicated.

Models remain synthetic direct/filtered/quadrature IDs 1/2/3, distinct preserved
FUIDs, gain/sustain IDs 0/1, RHST-v1 16-byte configuration and reported latency
0/48/48 frames. Configuration excludes commands, mechanics, ownership and tails.
The source freeze binds DSP coefficients, state definitions and fixed tests;
there are no calibrated specimen/model assets. The 400-vector parser fixture
`tests/fixtures/control_text_vectors.json` has SHA-256
`da183a9b183f10d86a258b5aeee1ce4daae0af03099bab7e87c5ec1fecdb75e0`.
Independent Decimal/Fraction expectations check rounded bits; the W4-064 C++
literal repair preserves those expected bytes.

Loaded033 matched 23/23 files at d4; current comparison is 22/23 solely because
the specification gained W4-079's 428-byte Roland reference bullet. Removing that
bullet reproduces the original specification hash exactly. WAV036 remains 32/33
because `cmake/Vst3.cmake` later added test registration/loader support. W3-057
reviews that difference. Neither mismatch changes a runtime result. The Roland
citation supports manufacturer practice; shared sustain/mechanics/tail behavior
remains the project's explicit contract, not a universal MIDI normative claim.

## Behavioral evidence and limits

**C01.** W3-041's map/multiset ownership oracle directly calls scalar engines,
bypassing production NoteTracker translation/dispatch. Its 769-frame timeline
uses whole, one-frame, fixed-64 and ragged partitions at 32/48/96/192 kHz, both
sample formats, all variants, with a 129-frame continuation. Comparisons require
bit-identical stereo and exact state/identity/configuration/cursor agreement.
The oracle shares production DSP equations and does not validate physical
acoustics. Separate bounds fixtures cover 18 queues, 256 raw commands, 585
expanded note actions, and 256 accepted versus 257 rejected actions per sample,
including sustain; a partition pass alone would not establish these endpoints.

Atomicity fixtures cover chronological identity reuse, shared-holder survival,
selected-recall abort/retry, inactive outputs and zero-frame nonadvancement.
Ordinary release preserves mechanical state and ringing history; selected recall
intentionally resets them. Staging/preflight precedes persistent mutation.
Processor source establishes ascending-channel and sustain-first tie order:
W3-050's commuting-setter permutations demonstrate why audio alone cannot prove
that exact call order. W3-048 repaired the loaded restrike oracle by lifting
sustain at offset 8 before the command at 17; its counterfactual fails and the
repaired production fixture passes. Accepted later faults retain partial state
and fence processing; they are not preflight rollback. Allocation interception
and bounded source review do not establish device deadlines or exhaustive malloc
and foreign-host behavior.

**C02.** Pressure validation covers bus 0, nondecreasing in-block offsets, finite
PPQ, channels 0–15, pitches 0–127 and raw binary32 pressure in [0,1]. Every signed
note ID is legal. Pressure consumes the input budget and ordering cursor without
emitting an action. Per-variant retained results include 288 calls, 110 paired
comparisons and 68 rejections across both formats and baseline/FTZ/DAZ/combined
modes. Cases include 256/257-event boundaries, 255 pressures plus note/pedal,
late invalid pressure, failed retrieval and zero-frame rejection before getters.
State snapshots, 129-frame cloned continuations and audible reactivation check
that rejection preserves live/published/saved configuration, holders, mechanics,
cursor and pending recall. Rejection may silence valid supplied audio buffers;
that differs from C03's unchanged conversion-output requirement.

**C03.** Baseline `nan`/`16junk` acceptance is reproduced historically and rejected
by current tests. Complete ASCII input needs NUL within 128 UTF-16 units; grammar,
non-ASCII, junk, nonfinite values and nonzero underflow reject. Both outputs stage
until success. Gain uses parsed binary64 [0,32], then binary32 normalization;
commands use [0,1] with binary64 round trips. Accordingly
`32.000000000000000001` may round to 32 and pass. Negative subnormals reject,
signed zero canonicalizes, and sustain keeps exact Off/On labels with threshold
0.5. Text conversion does not execute commands or mutate saved configuration.
Six core selectors exercise 400 independent vectors; public variants each check
41,752 conversions across 16 floating environments, and loaded modules retain
1,002 rejected texts. Final integration covers the portable helper; the earlier
pre-helper controller run alone would be insufficient.

**C04 comparisons.** Loaded033 records 72 baseline/current pairs, 304,128 exact
stereo frames and 2,952 configuration checkpoints at 48/192 kHz, float/double and
three partitions. Dense automation supports partition comparison; sparse ramps
compare only identical partitions. Baseline is
`20a77e8b2033d435839df594c783837befb07938`, current capture label a06d2cd. WAV036
uses the distinct `5aac654f8569aaa782e7bce58e82b3213b57da23` fingerprint baseline:
24 direct/filtered fixtures, 44,160 frames, four rates, keys 0/36/72, velocity 0.7,
gain 8 and 20 ms duration. Historical raw WAVs are absent; equality is fingerprint
based. These finite legacy inputs exclude CC123, pressure, malformed/saturated
inputs and new text behavior. The Q6 numerical-reference study was not rerun.
Baseline/build provenance is integrator-supplied. Raw archive integrity preserves
bytes; it neither authenticates execution nor supplies a reproducible rebuild.

## Commands, environments and native cutoff

Recorded local commands were `cmake --build build-host -j 2`,
`ctest --test-dir build-host --output-on-failure -j 2`, and
`python3 -m unittest discover -s tests/python -v`. Environment: Linux 6.18.57,
glibc 2.41, x86_64, GCC 14.2.0, CMake 4.4.5, Python 3.12.14, Release C++20.
Results: 327/327 CTests, zero skips; Python 888 reported, 887 successful, one
optional `RHODES_AUDIO_PROBE` skip. Each SDK validator's 47 internal checks is
inside its CTest selector. Thirty retained SDK `ERROR` lines concern unsupported
sample-rate rejection; all three validators conclude zero failures.

Compatibility commands were `python3 .agent-work/worker033/run_comparison.py`
and `python3 -B .agent-work/worker036/replay_legacy_wav.py`. SDK root pin is
`3cdf9ca5d1f5b1b21e0a86832aa4abe55607bd96`; the Linux environment manifest and
`.github/workflows/vst3.yml` freeze all four required submodule revisions.

Native VST3 uses Release configuration with the pinned headless SDK, then
`cmake --build build/vst3 --config Release --target rhodes_vst3_checks --parallel 2`
and `ctest --test-dir build/vst3 -C Release -R '^vst3\.' --output-on-failure --no-tests=error`.
Foundation uses `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release`,
`cmake --build build --config Release --parallel 2`, and
`ctest --test-dir build -C Release --output-on-failure --no-tests=error`.
Sanitizers use `cmake --preset sanitize`,
`cmake --build --preset sanitize --parallel 2`, and
`ctest --preset sanitize --no-tests=error`. Provider job identities and commands
are retained by the native artifacts/workflows.

| Executed native scope | Actual source/environment | Retained result |
| --- | --- | --- |
| VST3 Linux, job 138374005076 | ce59; Ubuntu 26.04, GNU 15.2.0, x86_64 | 157/157; 46.96 s; zero skips |
| VST3 Windows, job 138374004751 | ce59; Windows 2025 VS2026, MSVC 19.52.37029.0, x64 | 157/157; 43.83 s; zero skips |
| Foundation Linux/Windows | d4; GNU 15.2.0 / MSVC 19.52.37029.0 | 170/170, 5.10 s / 167/167, 7.93 s |
| Linux sanitizer | d4; GNU 15.2.0 | 170/170; 83.24 s |
| Foundation Python Linux/Windows | d4; Python 3.11.18 / 3.11.9, x64 | 888 reported per platform: 838 successful/50 skips; 830 successful/58 skips |

Both VST3 jobs in run 45373649775 pass all three SDK and three module-info
validators, loaded MIDI and dual/triple coexistence checks. Successful internal
stdout is suppressed by `--output-on-failure`; retained pass lines are not full
native log/binary archives. Linux core/sanitizer logs do not explicitly identify
architecture. The three Foundation count differences are Linux `/dev/full`
registrations, not Windows failures/skips. Windows mandatory identity/callback
regressions pass; actual open-file replacement retains its WinError 5 skip.

VST3 attempt 1 ended workflow failure with macOS cancelled without execution.
Retry job **138393683256** was queued with no runner/steps. Re-listed
Linux/Windows successes retain original execution times and are not new runs.
Foundation run **45376430320** ended cancelled: macOS core never ran; arm64
Python emitted some regression passes but no terminal suite summary.
Replacement Foundation **45379468043**, at 4cf, had seven queued jobs. None is
evidence of a macOS pass.

The later [W5-093 status record](host-midi-native-latest-20261124.json) ends at
**2026-11-24T11:09:46.569Z**. VST3 retry 138393683256, Foundation core
138394544167 and Foundation project/Python 138394543837 were all still queued,
each with runner ID 0 and no actual steps. Replacement Foundation had successful
Linux project, Windows core and sanitizer jobs; its other four jobs remained
queued. These API statuses supply no new macOS compiler, architecture, test or
skip result and do not replace the earlier retained execution logs.

`docs/DAW_VALIDATION.md` documents first/default-zero and repeated unchanged
CC123, restrikes within/across blocks, live/recorded routes, pressure/releases,
recall and processor queue traces as unrun host checks. A plug-in cannot recover
points omitted by its host. This satisfies documentation, not DAW qualification.
Physical fidelity, PHY-002/RT-001, WCRT, device deadlines and release remain
separate gates.

## Evidence anchors and continuation requirements

The following hashes incorporate their contained manifests, commands and inputs.
The 27 registered artifact/report digests remain in `project/evidence.json`;
W4-073's source-bridge log is already included in the Linux numerical record.

| Artifact under `docs/validation/` | SHA-256 |
| --- | --- |
| `host-midi-linux-final-20261124.json` | `d1e662ee01f59e947c6391248b787c0d62e3a622983a8a43e933f96df4f960e3` |
| `host-midi-python-final-20261124.json` | `8d62352d334217dcf6b1788af73d140b6dd99631370f6324558e2a5c16a0a928` |
| `host-midi-loaded-compatibility-20261124.json` | `8fadfb8558f77d815a3a54ea7990cfeee7de559c04082d0e71b9770808528a14` |
| `host-midi-legacy-wav-20261124.json` | `f4758fc1ab4bcb8bf00c517b877b9f2eea78d7976f3847190af1659ff77e3b13` |
| `host-midi-source-binding-20261124.json` | `43d82ec639f8bcaa7af0c6692f323bec36169e1c6ce50627bf2040f7c7de1fde` |
| `host-midi-native-fix-snapshot-20261124.json` | `a98c233b04af89308a79b6e78f12b764067b07c8f7f9209be54b6db82370b97a` |
| `host-midi-native-final-20261124.json` | `00baf5673293c3aba43e1ae059fa0870fa9587f374d77adafaead73896f8443d` |
| `host-midi-native-final-selected-20261124.log` | `708032a6cf10a431f00a8042148e59f5b03b3eb793b08109be89456c623e22ed` |
| `host-midi-native-latest-20261124.json` | `c4c1ded20ae0c092d0f642b83c8a37ba58cf661bf566ca9663889aa33a9779b2` |

Stable scoped review hashes: W5-082
`d96ff96d58a5f5cae305db21c137a314e6d75f5d9bd10a7943b572b90103076f`;
W5-083 `fc91be878609230571b3c6fe1dc70cc131f4a5f13f88f7b93918192a479c2505`;
W5-084 `f355ce2bcd134c3d32eb2fde7ec201168f7e33cc2b97c826f6d158e667a00e20`;
W5-085 `524f5e9386e9fc7daefd665e896ae98034b65582c607a04c8f511032b1396d20`.
W5-081 is `e2127f255c4b7c4f3c25d9b72e2084690189be354401f26016a16df427788f55`;
W5-087 is `f91f453f6d9ed03c2cc508d977f35c2cf3b21d2bed0a4f5ad0f870ecca3fc774`;
W4-079 is `2869093b958a62dc0362b7d7b8662bd7bbe45bbd9562e762b0d1beb1542f0f2e`.
The preserved predecessor `.agent-work/worker062/acceptance-draft.md` is
`9926cc64b1914d715de0131dbb175b2afc2384ebfb316b7bde02844e4db22a40`;
its earlier registration/native-pending statements are superseded explicitly here.

Before completion, obtain applicable completed macOS SDK/module, loaded-host and
core/model-state/control-text results with source, environment, exact skips and
failure history; inspect the replacement workflow's actual terminal outcomes.
Append and review new immutable native evidence beyond the registered cutoff,
register applicable further numerical and final documentary records, then
reassess all four clauses. Do not
silently relabel older builds or edit registered snapshots. **At this cutoff,
C04 is incomplete and the task must remain incomplete.**

This final copy incorporates the scoped reviewer findings without changing the
canonical definition. The [continuation handoff](host-midi-handoff-20261124.md)
identifies the next native checks and existing evidence. Canonical state and the
campaign ledger separately record task and agent progress.
