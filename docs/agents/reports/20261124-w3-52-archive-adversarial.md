# W3-52 — Independent archive adversarial review

**Agent:** `/root/w3_52_archive_adversarial`  
**Assignment:** 52, wave 3  
**Result:** one concrete failure-retention defect repaired; five focused tests pass.

## Finding and authorized repair

The archive reader rejected many malformed gzip cases, but its local exception
handler omitted `zlib.error`. The 19-byte fixture
`1f8b0800000000000003070000000000000000` contains a gzip envelope and a reserved
DEFLATE block type. Calling `archive_members()` raised the raw decompression
exception. With a summary rebound to that fixture's real size and SHA-256, the
CLI likewise escaped before emitting its result collection or checking the next
report. This was a failure-reporting defect, not a demonstrated file write,
command execution, or archive extraction.

Root explicitly authorized the two-line production repair: import `zlib` and
normalize `zlib.error` to the existing `VerificationError` at the compressed-tar
boundary. No validity rules, numerical summaries, hash comparisons, limits, or
historical evidence changed. W3-41 remains immutable and describes its earlier
source. The previously inspected verifier SHA-256 was `1cd002d895caf86a456bd5ed67ade854251de00406d5a5a2f604dd8f12ab4e68`.

## Independent regression coverage

Added `tests/python/test_vst3_archive_adversarial.py` with five tests:

- Invalid DEFLATE is an ordinary verifier rejection.
- The CLI retains that rejection, continues to the next valid report, verifies
  its 72 runs, and returns failure for the collection.
- A second gzip member containing another tar cannot hide behind the first
  archive's end markers.
- PAX path, advertised-size, and sparse-map overrides cannot bypass member
  safety or size rules.
- Rebound execution records cannot overlap monotonic intervals or use a Boolean
  timestamp, even after member/archive hashes and manifest records are updated.

The first run produced **two errors and three passes** in 0.759 seconds, with
both errors exposing the uncaught decompression exception. After repair, the
same module passed **5/5 tests, no skips, in 1.024 seconds**, exit 0:

```sh
python3 -m unittest discover -s tests/python -p test_vst3_archive_adversarial.py -v
```

The green run checked the four identities below before and after execution;
all were unchanged. The tests read original archive members without extracting
files and create manipulated fixtures only in temporary directories. No native
benchmark, build, or numerical reference was run.

## Source and retained evidence

Git HEAD when this report was prepared: `df3a46153e9fe6b6e693f5564b8538ddabaf09a2`. The new test and repair were
local edits at verification; the explicit file hashes below identify them.

| Examined file | SHA-256 |
| --- | --- |
| `tools/verify_vst3_matrix_archive.py` | `4fc824ebe5038fcda4170415b002dd844c53a0ea167705cdbee03a9f23a32162` |
| `tests/python/test_vst3_archive_adversarial.py` | `a02a3863004da46d0f085c79c474b76d9ca77bcd6d0b243707382ece35bb6acc` |
| `docs/validation/vst3-matrix-20261124.json` | `021648832aa392fc68b1c7235db79c6489da93798a48c68321459985ac57328d` |
| `docs/validation/vst3-matrix-20261124.tar.gz` | `366c6935c98f143c1fa3d6d6014a83058391d73391fb6bf0047629650783fad4` |

The red and green logs remain under `build/w3-52-archive-{red,green}.log`.
Their SHA-256 values are respectively
`9097da400b95b204cebfd305638bc657c37092fbb5b33d5a6813595cd087019c` and
`10d7b566c0a4c90d0bba8d5394f074ee98717863f00fb653a792d915d082ff4f`. The green run's exact argv, return code, and
before/after identities are in `build/w3-52-checks.json`. The first shell wrapper
printed the red log after the failed unittest command; its outer exit was 0,
so the red outcome above is taken from the unittest failure report, not that
wrapper status.

Required project validation, generated-state check, and work selection passed
at revision 27 with 21 evidence records. Root owns publication and native CI.
This local Python 3.12 result is not a native Windows/macOS observation. Archive
integrity cannot authenticate acquisition, reconstruct historical binaries,
prove runtime deadlines, or establish measured Rhodes fidelity. RT-001 and
physical-fidelity status are unchanged.
