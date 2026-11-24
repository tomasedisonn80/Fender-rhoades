# W3-54 — Independent compatibility failure boundaries

**Agent:** `/root/w3_54_compatibility_adversarial`  
**Assignment:** 54, wave 3  
**Result:** one reproduced preflight defect repaired with root authorization.

Read AGENTS, README, roadmap, generated state and canonical task definitions.
`project.py validate`, `sync --check` and `next --json` passed at revision 27
with 21 registered evidence records. This bounded review belongs to root's
existing runtime work; no claim, ledger, state or Git mutation was performed.

## Reproduced defect and repair

The repaired W3-42 verifier checked Q6 input identities and metadata indices
before execution, but did not check its consumed output-comparison hashes.
Replacing the first historical row's `raw_array_sha256` with an empty list
passed preflight. Feeding that case's actual retained probe bytes through the
real parser then raised uncaught `IndexError: list index out of range` after
the first probe boundary. Raw bytes existed, but `result.json` did not. The
reproducer supplied retained bytes through the subprocess boundary; it executed
neither native probe nor renderer.

Root authorized a narrow production change. Preflight now validates the two
consumed stdout/stderr SHA-256 values and the frozen five-entry array-hash
list, including digest syntax. Malformed comparisons return operational exit
3 before Git metadata collection, output creation or probe execution. Historical
valid inputs still pass preflight. No waveform comparison, output tolerance,
case generation, numerical formula or successful-result interpretation changed.

Changed `tools/verify_audio_compatibility.py`; added only the independent test
module and this report. The original 18-test module, W3-42 report and historical
numerical evidence remain unchanged.

## Focused verification

```sh
python3 -m unittest discover -s tests/python -p test_audio_compatibility_adversarial.py -v
```

Final run: **5 tests passed, no skips, exit 0, 3.129 seconds**. Production and
new test hashes were equal before/after this run. The initial five-test run
also passed; the final run followed narrowing the unsupported-symlink capability
guard so an unrelated `EINVAL` cannot become a skip.

The new tests cover eight malformed digest/list variants with no process or
output creation; valid JSON carrying an unexpected probe model; a later probe
failure preserving an earlier mismatch and both attempted raw prefixes; refusal
to replace an existing output file; and rejection of a symlink WAV after
retaining real Python-child stdout/stderr. The latter may explicitly skip where
symlink creation is unavailable. Probe/parser substitutions isolate orchestration
in the partial-attempt test; they are not numerical verification.

No full 2,097-case replay, build, reference integration or benchmark was rerun.
The original exact-output evidence remains historical. These tests establish
the exercised preflight/retention behavior, not acquisition authenticity,
arbitrary hostile executable containment, realtime deadlines or physical fidelity.
No further material defect was identified within this bounded review.

## Exact identities

| File | SHA-256 |
| --- | --- |
| `tools/verify_audio_compatibility.py` | `bf688ea37c6d49a3d388d47c8f47ad996bf60229fd56873ada1fba5a804dd1e4` |
| `tests/python/test_audio_compatibility_adversarial.py` | `e958930693f060a42fd779481ee0b5e9916e878abd82d080487c103ee5e1eb0f` |
| Existing `tests/python/test_audio_compatibility.py` | `9241dd793bc340e42222bf66fb02ff2ffd84819d9386ab8ed76183e4e889e086` |
| Original Q6 full-study JSON | `d1d13dc3468fff4962d3bdc03a61a35a03284bc2ae38835c9783525903c15278` |
| Original legacy WAV baseline JSON | `436244553d4462eab4452a3b318864840f31b41fcf06cf3c8f0978d9a841e914` |
| Retained batch-compatibility JSON | `2ecedc02e1fb11628cfa98017035f0e3e92b7a428e72ed6ec281ce6f1b6b40d4` |
| Local `build/w3-54-final-tests.log` | `22d9d143a56b5127a4134d29368889d166085ddd8ad2b75437bc8eb519d0fe0c` |

Next: root can integrate this change and obtain native Python CI confirmation.
RT-001 remains incomplete and measured fidelity remains unvalidated.
