# W3-56 — Native CI attribution and coverage audit

**Agent:** `/root/w3_56_native_ci_audit`  
**Decision:** no material platform, denominator or attribution error found.

The retained [native record](../../validation/native-platforms-20261124.json)
binds commit `015767fee5635ff85a717739188ff4af60fe84f2`. I verified its archive
size/hash, all ten safe regular-member hashes, and all 74 listed source hashes
against that Git revision and the working files at observed HEAD
`3175e3dc8c01d1bd57c80d9684168a408cee66a2`; none differed. Reading the retained
CTest lines independently gives:

| Retained checkpoint | Passed | Skipped | Registered |
| --- | ---: | ---: | ---: |
| Linux core | 150 | 0 | 150 |
| Windows core | 147 | 0 | 147 |
| macOS core | 141 | 6 | 147 |
| Linux VST3 | 138 | 0 | 138 |
| Windows VST3 | 134 | 0 | 134 |
| macOS VST3 | 125 | 9 | 134 |
| Linux ASan/UBSan | 150 | 0 | 150 |
| Local SDK-integrated Linux | 288 | 0 | 288 |

All three SDK-validator and three moduleinfo-validator selectors pass on each
native platform. macOS skips are exactly the listed x86 FTZ/DAZ modes. Linux-only
module-coexistence coverage is not transferred to Windows/macOS. My first audit
regex missed CTest's adjacent `...***Skipped` formatting; correcting that parser
reproduced every registered denominator and skipped name without changing evidence.

## Later Python confirmation

GitHub API job/step reads independently confirmed all seven successful jobs in
both [the UTF-8 repair run](https://github.com/baabi-boo/Fender-rhoades/actions/runs/45337574098)
at `1873883` and [the summation-fixture repair run](https://github.com/baabi-boo/Fender-rhoades/actions/runs/45338423626)
at `df3a461`. Retrieved logs for the latter explicitly pass both
`test_worker_utf8_survives_ascii_parent_locale` and
`test_historical_python_sum_difference_is_explicit`:

| Python job at `df3a461` | CPython | Passed | Skipped | Discovered |
| --- | --- | ---: | ---: | ---: |
| [Linux](https://github.com/baabi-boo/Fender-rhoades/actions/runs/45338423626/job/138255928745) | 3.11.18 | 540 | 20 | 560 |
| [Windows](https://github.com/baabi-boo/Fender-rhoades/actions/runs/45338423626/job/138255928572) | 3.11.9 | 537 | 23 | 560 |
| [macOS](https://github.com/baabi-boo/Fender-rhoades/actions/runs/45338423626/job/138255928801) | 3.11.9 | 539 | 21 | 560 |

Twenty skips concern optional NumPy/SciPy audits; Windows additionally skips
POSIX filename/FIFO and open-file replacement cases, while macOS additionally
skips its unsupported invalid-byte filename fixture. These are recorded
coverage exclusions. The earlier Windows UTF-8 job independently reports
498 discovered, 23 skipped, and the locale regression passing.

## Procedure, limits and handoff

Required `project.py validate`, `sync --check` and `next --json` passed at revision
27. Read-only Python `hashlib`/`tarfile`/regex inspection and `git show` verified
the identities above; no archive extraction, build, test rerun or numerical
experiment occurred. GitHub reads used run job lists and four specific Python
job logs; no new raw-log archive was created.

The later W3-52–54 tool repairs and new workflow checks are outside these Python
checkpoints. Root must inspect their own subsequent CI result. None of these
observations establishes native DAW/device behavior, signing, release readiness,
or measured fidelity. Only this report was created; task decisions remain with
the integrator.

| Examined immutable input | SHA-256 |
| --- | --- |
| `native-platforms-20261124.md` | `433aafeb47acedb77853394e99e11ab11ad56a83404b36b5804ea74fd1cfd1e2` |
| `native-platforms-20261124.json` | `3b28ae2ee831786e1f69c8a340f07eb888e6be23743a155f59a014621dbc5f7c` |
| `native-platforms-20261124.tar.gz` | `99655044dd9815379b892f3fddd0e9f5e37496cff794bbbd2bf9bd205b091694` |
