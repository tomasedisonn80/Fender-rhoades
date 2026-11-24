# Host interoperability continuation — 24 November 2026

This is the separately claimed **HOST-002** continuation on `main`, starting at
`20a77e8b2033d435839df594c783837befb07938`. It preserves the concurrently owned
physical-reference and runtime-research work. The instruments remain synthetic
and unvalidated against a measured specimen.

The user requested **100 actual workers in five waves of 20**. The environment
admits one integrator and three workers concurrently, so each wave uses smaller
batches. A later wave starts only after the preceding 20 workers finish. The
[assignment ledger](../../project/continuation-c-20261124.json) records actual
spawned task names and immutable report hashes; counts do not establish scientific
acceptance. `python3 tools/validate_continuation_c.py` checks ledger integrity.

## Progress

| Wave | Assignments | Status | Work |
| --- | --- | --- | --- |
| 1 | 001–020 | Complete | Recover context, reproduce host failures, review contracts and bounds |
| 2 | 021–040 | Complete | Implement CC123, validated pressure and portable text; independent regression and compatibility checks |
| 3 | 041–060 | Active | Close integration gaps and establish portable, reproducible verification |
| 4 | 061–080 | Pending | Review retained evidence and remaining implementation risks |
| 5 | 081–100 | Pending | Complete acceptance review and source-bound continuation notes |

The [MIDI contract](../specs/HOST_MIDI_20261124.md) defines the behavior and
acceptance scope. [VST3 controls](../VST3.md) and the
[proposed DAW protocol](../DAW_VALIDATION.md) explain use and unrun host checks.
Canonical completion remains in [project state](../PROJECT_STATE.md).

## Verified implementation checkpoint

Commit [a06d2cd](https://github.com/baabi-boo/Fender-rhoades/commit/a06d2cd60bc53e3939e89f64254aed7ed5586e7c)
adds channel-specific CC123 commands, accepts well-formed polyphonic pressure as
a no-op, and replaces permissive text parsing with bounded integer conversion.
The local Linux Release suite passed 317 tests, followed by six new control-text
selectors. The expanded allocation guard passed for all three variants.

Retained results cover [72 actual loaded-module legacy pairs](../validation/host-midi-loaded-compatibility-20261124.json),
[24 legacy WAV fixtures](../validation/host-midi-legacy-wav-20261124.json), and
[bounded command-path observations](../validation/host-midi-command-cost-20261124.json).
They describe distinct compatibility and performance scopes, with no measured
fidelity, native DAW, device deadline or worst-case execution-time claim.

[Native CI run 45367133323](https://github.com/baabi-boo/Fender-rhoades/actions/runs/45367133323)
passed all 153 Linux VST3 checks. Windows rejected a shadowed test-helper argument
under MSVC's warning-as-error policy; renaming it preserves that policy and passes
the nine affected local suites. At the corrected
[75a444d checkpoint](https://github.com/baabi-boo/Fender-rhoades/commit/75a444df6e68b2d2ced14a4bd0772a6f7b1b914c),
[native CI run 45369327428](https://github.com/baabi-boo/Fender-rhoades/actions/runs/45369327428)
passed Linux and Windows; macOS execution remains pending.

Wave 2's final review identified new-feature partition coverage and inactive
output interactions as the next permanent regression work. The latter also has
a passing 24-scenario temporary probe in worker 039's report. At that checkpoint,
durable raw replay data and final criterion review were also outstanding.

Wave 3 adds permanent scalar-oracle partition and inactive-output checks, plus
an SDK-only loaded-MIDI test. All pass locally. A review found that sustain masked
one loaded-test ordering assertion; ending sustain between the two commands
makes the error audible, and a counterfactual oracle verifies that sensitivity.
Loaded-module CMake coverage now includes Windows and macOS; those new branches
still require their own native results.

The [raw evidence archive](../validation/host-midi-raw-20261124.tar.gz) and
[manifest](../validation/host-midi-raw-20261124-manifest.json) preserve 612 original
files as 191 content blobs. `python3 tools/verify_host_midi_archive.py` verifies
the archive, mappings and referenced reports without extracting or executing
content. It checks byte integrity, not execution authenticity or fidelity. The
[verification guide](../HOST_MIDI_VERIFICATION.md) explains the commands, frozen
inputs and limits on reproduction.
