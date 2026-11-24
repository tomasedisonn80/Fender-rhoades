# W5-93 — Python verification coverage and skips

Evaluator: `/root/w5_93_python_coverage`; 24 November 2026.

**No material coverage contradiction was found in the final claims.** The
remaining production-probe test is explicitly unrun, not counted as a pass.
This review inspects retained results and source conditions; it executes no test.

## Observed execution

The [local record](../../validation/python-final-20261124.json) captures
`python3 -m unittest discover -s tests/python -v` at observed HEAD `420c4c7`,
with Python 3.12.14, NumPy 2.3.5 and SciPy 1.17.0. The
[native record](../../validation/native-final-20261124.json) retains three
separate Python 3.11 executions at `2401706`.

| Execution | Reported tests | Unskipped passes | Skips |
| --- | ---: | ---: | ---: |
| Local Linux, Python 3.12.14 | 584 | 583 | 1 |
| Native Linux, Python 3.11.18 | 584 | 564 | 20 |
| Native Windows, Python 3.11.9 | 584 | 561 | 23 |
| Native macOS, Python 3.11.9 | 584 | 563 | 21 |

All four summaries report success. Independent log recounts agree; the Windows
log includes one standalone `ok` after intervening output, alongside 560 ordinary
verbose success lines. It must not be lost by a parser requiring `... ok` on
every line.

## Meaning of the skipped checks

Twenty optional numerical tests are common to all three native skip lists:
14 filtered-reference tests, three Q6 assessment/array-retention tests, two
independent filter-recipe checks and one damped-contact timing check. Their
source decorators require available NumPy/SciPy, or SciPy for contact timing.
The local log confirms 19 of these passed, including genuine matrix-exponential,
adaptive-integral, filter-recipe and damped-time refinement checks. Some other
checks deliberately exercise controlled/malformed probes; availability does not
turn every unit fixture into a production numerical experiment.

The remaining local skip is
`FilteredAudioNumericalTests.test_three_small_production_comparisons_keep_exact_internal_phase`.
Its method additionally requires `RHODES_AUDIO_PROBE`, which was unset. It would
compare three 160-frame production/reference cases: key 0 at 32 kHz and key 72
immediately above the binary64 48/96 kHz boundaries. It checks comparison A and
reference refinement, not a full matrix or general comparison-B acceptance.
Native runs skip the entire containing class before reaching this extra condition.

Windows adds three capability exclusions:
`test_posix_surrogate_escaped_paths_survive_retention` requires POSIX;
`test_fifo_and_final_symlink_are_refused_before_opening` requires `os.mkfifo`;
`test_path_replacement_after_final_fstat_is_detected` encountered the documented
open-file replacement restriction. macOS adds only the surrogate-filename test:
its filesystem rejected undecodable bytes with the explicitly handled `EILSEQ`.
These three capability tests passed locally. Other unrelated I/O errors are not
silently converted to that filename skip.

## Applicability and limits

All **65 current Python tool/test source files** match both retained execution
manifests exactly. The local 544-entry stable inventory includes 543 tracked
inputs at its commit and one then-untracked ledger; later integration metadata
is not relabelled as part of that execution.

Plan-count assertions and small numerical self-checks do not execute the full
2,097-case reference study or later byte replay. Those experiments retain their
separate input, source and binary bindings. Python success also does not replace
native C++/SDK host validation, close the remaining RT/PHY convergence gates or
establish physical fidelity. The disclosed probe omission does not undermine
the precisely stated Python totals or the separate current HOST evidence.
This inspection identifies no reason for a broad repeat.

Required project validation, generated-state checking and work selection passed
at revision 28 with 25 evidence records. Read-only JSON/gzip/tar inspection,
status recounting, source-condition review and SHA-256 comparisons produced these
findings. No archive extraction/execution, build, test rerun, Git mutation or
shared-state edit occurred. Only this report was written.

## Examined byte identities

| Path | SHA-256 |
| --- | --- |
| `docs/validation/python-final-20261124.json` | `a1c5634bd91b1e8a59c2f63190753efa9d498153c1fcdbc54f4880466feec347` |
| `docs/validation/python-final-20261124.log.gz` | `eec045f340d3ede9f260e69874445233d34eb539b4fbb8e5263c6d4618e25e86` |
| `docs/validation/native-final-20261124.json` | `6a31a715bdd9590f9ff789636df1bb948639120bed0811ec43b313e3a96833e2` |
| `docs/validation/native-final-20261124.tar.gz` | `77c2f598b07d971d74a7f986a3230bf378e51a710ad652dc8382082943c902a5` |
| `tests/python/test_filtered_audio_reference.py` | `562d6b8860f286153addcb18cfc1f25ed2b467898ada1a2a64b354b899dd2ae5` |
| `tests/python/test_contact_peak_oracles.py` | `9de8d7907b7a68fccf77788d9f04c93d4059d343bb4e6b1bacdc1b26bc2be1e6` |
| `tests/python/test_contact_peak_portability.py` | `14004d860445a1a6075228f641d72fca16ff8b24c55bed9702a01bec21922388` |
| `tests/python/test_contact_peak_retention_errors.py` | `9e899b5a5d5c4d19dca7f99cb7a0dc99c3067b3620d3c7dd3c127b78954911e9` |
| `tests/python/test_payloads.py` | `e1cd213e7461f77b974f13e2e5c86cf27aea5279d0f882a879c627c0e9606846` |
| `tests/python/test_quadrature_audio_validation.py` | `b34d6271a252517803802c0920821ed64bb207f292e8b396ff272c330823ca78` |
| `tests/python/test_audio_filter_generation.py` | `846fb70c0f1df30f45a20a889f18ff4ad4ff43b508d12c28af24f537157f1b33` |
| `tests/python/test_quadrature_filter_generation.py` | `4f058e06a65a3f2084039a0087858e06141b81b21c78e014771e0cf08f5e51c4` |
