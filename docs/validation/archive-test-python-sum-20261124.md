# Archive verifier fixture across Python versions — 24 November 2026

Foundation CI at `1b2e4fc04e08cff41edcbe1ca420e690c9845ff4` passed the native
C++ jobs but its three Python 3.11 jobs exposed the same test-fixture assumption:
[Linux](https://github.com/baabi-boo/Fender-rhoades/actions/runs/45338025815/job/138254638905),
[Windows](https://github.com/baabi-boo/Fender-rhoades/actions/runs/45338025815/job/138254638602),
and [macOS](https://github.com/baabi-boo/Fender-rhoades/actions/runs/45338025815/job/138254638756).
Each discovered 560 tests and failed only
`test_historical_python_sum_difference_is_explicit`.

The archive verifier correctly reports differences in the auxiliary historical
Python input-order sum separately from the exact normative native summary and
`math.fsum` checks. The fixture expected exactly one reported difference after
injecting one, but Python 3.11 already differed from the retained Python 3.12
auxiliary values in additional runs. The native summary contract was not failing.

The repaired fixture first sets each auxiliary input-order mean to the executing
interpreter's actual sum, then changes exactly one by 1 microsecond. It still
requires exactly that run to be reported. Native summaries, raw durations,
`math.fsum` audits, production verifier code, and original archives are untouched.

```sh
python3 -m unittest discover -s tests/python -p test_vst3_matrix_archive.py
```

All 20 focused tests passed locally after this test-only correction. Native
Python 3.11 confirmation remains a separate CI result. The original W3-41 report
continues to describe its historical local source and verification; it is not
rewritten to hide this later fixture finding.
