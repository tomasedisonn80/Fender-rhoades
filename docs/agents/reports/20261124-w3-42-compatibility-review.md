# W3-42 — Exact audio replay failure retention

Assignment 42 was performed by `/root/w3_42_compatibility_review` within the
root-owned RT-001 review. No child agents, Git mutations, canonical-state changes,
DSP edits, or historical-evidence edits were made. The inspected checkout advanced
to `18738833a778c8dec7652e224fec9621ff39f30f`; the exact changed-file identities below
bind this report independently of that moving branch.

## Findings and changes

`tools/verify_audio_compatibility.py` previously performed its final source and
binary reads outside error handling. A source or executable disappearing after
an attempted probe could therefore prevent `result.json` from being written.
Final integrity errors now set the unchanged flag false, preserve the original
execution error, and produce an operational-failure report where output remains
writable. Child-directory creation errors receive the same reporting treatment.
Git revision lookup now completes before output creation, and its subprocess
failure is converted to CLI exit 3.

Renderer timeouts previously discarded the partial captured streams. The renderer
wrapper now retains them before propagating the timeout. Nonzero renderer exits
and missing/nonregular WAVs now produce operational exit 3 after stream retention;
they previously could count as completed comparison mismatches. A successful
regular output still follows the unchanged SHA-256 comparison. Reported WAV frame
totals are now available only when all 24 WAV identities match.

The historical legacy fixture count alone did not enforce distinct coverage or
safe output names. The exact original 24 IDs, filenames, path/rate/key combinations,
velocity, duration, gain, frame counts, and aggregate count are now checked before
execution. This rejects duplicate substitutions and traversal/collision names.
Baseline reads have a 16 MiB ceiling; duplicate JSON keys, nonfinite or overflowing
JSON numbers, and nonobject documents are rejected. Q6 column names must be unique,
and metadata indices must be nonnegative integers in range. All original 2,097
generated input identities still pass preflight in the focused replay tests.

## Executed verification

The required initial state commands passed: `python3 tools/project.py validate`,
`python3 tools/project.py sync --check`, and `python3 tools/project.py next --json`.
They reported revision 27, three root-owned claims, and fidelity `unvalidated`.

```sh
python3 -m unittest discover -s tests/python -p test_audio_compatibility.py -v
git diff --check -- tools/verify_audio_compatibility.py tests/python/test_audio_compatibility.py
```

**18 tests passed, no skips, in 4.728 seconds.** Real Python child processes exercised
successful output, a nonzero exit with binary streams and a partial file, a missing
output, and a three-second timeout with flushed stdout/stderr prefixes. Controlled
fault injection exercised probe failure, invalid JSON, source disappearance,
executable deletion, directory-creation failure, and Git failure. Tests confirmed
retained bytes, valid incomplete reports, error precedence, refusal of existing
output, and rejection before launching an executable. The diff check passed.

| Artifact | SHA-256 |
| --- | --- |
| `tools/verify_audio_compatibility.py` | `af44c7c02cca4464a4816fe82253f13a6d44f401bcd89868e69167c118153df0` |
| `tests/python/test_audio_compatibility.py` | `9241dd793bc340e42222bf66fb02ff2ffd84819d9386ab8ed76183e4e889e086` |
| Scratch log `build/audio-compatibility-review-final-20261124.log` | `ef56c92b8b113234fb87c35153718857ef4443a678e1198f55c60ede247b7e69` |

## Evidence boundary and handoff

No 2,097-case numerical replay, 24-WAV native render replay, continuous-reference
calculation, build, or native-platform job was rerun. Exact waveform/metadata
comparison semantics are unchanged. This is failure-path and input-contract
verification, not new audio-compatibility or hardware-fidelity evidence. Byte
identity does not establish acquisition authenticity; writable output is required
for successful retention, and before/after hashes cannot detect a transient change
that is restored between observations.

The historical compatibility report remains unchanged at SHA-256
`2ecedc02e1fb11628cfa98017035f0e3e92b7a428e72ed6ec281ce6f1b6b40d4`.
Its original Q6 and WAV baseline files remain unchanged at
`d1d13dc3468fff4962d3bdc03a61a35a03284bc2ae38835c9783525903c15278`
and `436244553d4462eab4452a3b318864840f31b41fcf06cf3c8f0978d9a841e914`.
Root can publish these three owned files and run the normal Python CI. Future
audio-source changes still require an actual compatibility replay.
