# W4-65 — Developer verification guide

**Agent:** `/root/w4_65_development_guide`  
**Assignment:** 65, wave 4  
**Result:** documentation updated; no implementation or acceptance change.

Updated only `docs/DEVELOPMENT.md` and this report. The guide now includes the
current-wave validator, all three retained timing-archive inputs, previous
campaign checks and coefficient freshness checks. It explains progress versus
`--require-complete`, byte integrity versus execution authenticity, and archive
recalculation versus a new benchmark or current-binary validation.

A dependency table separates standard-library checks and default contact peak
comparisons from optional SciPy timing and NumPy/SciPy numerical audits. Missing
dependency errors, unavailable requested references and explicit test skips
remain distinct from executed numerical coverage. The contact diagnostic guide
is linked without promoting its four-fixture result to PHY-002 completion.

The floating-point section follows the
[independent scope review](20261124-w3-47-clang-scope.md), current source and
official [Clang options](https://clang.llvm.org/docs/UsersManual.html#controlling-floating-point-behavior)
and [pragma documentation](https://clang.llvm.org/docs/LanguageExtensions.html#extensions-to-specify-floating-point-flags),
read on 2026-11-24. It distinguishes the tested non-Xcode macOS configuration
from external fast-math overrides and the pinned SDK's Xcode flags. Exact
assertions remain required; no general compiler-equivalence claim is added.
Core CMake 3.24, optional SDK CMake 3.25, C++20/Python requirements, sanitizer
limits and the narrow Windows native-argument boundary are preserved.

I fetched the job/step metadata for
[Foundation run 45340189549](https://github.com/baabi-boo/Fender-rhoades/actions/runs/45340189549)
at `510141e4b23969c0e51bbcd0e80167f0c2f4f8bf`: all seven jobs succeeded,
including ledger/archive checks on all three operating systems. The separate
retained `015767f` native VST3 checkpoint remains explicitly identified. Neither
checkpoint is presented as verification of later edits, DAWs or audio devices.

## Actual checks

Required project `validate`, `sync --check` and `next --json` passed at revision
27 with 21 evidence records. The three commands below returned help successfully:

```sh
python3 tools/validate_development_waves.py --help
python3 tools/verify_vst3_matrix_archive.py --help
python3 tools/validate_contact_peaks.py --help
```

Read-only source/import/workflow inspection verified the documented interfaces
and dependency boundaries. `python3 tools/check_docs.py` and scoped whitespace
checks passed. Fifteen supporting source/evidence hashes remained unchanged
during editing. No build, unit-test suite, benchmark, numerical replay, remote
job launch, Git mutation, ledger edit or canonical-state change occurred.

Next: integrate this documentation with the other Wave 4 changes and retain
revision-specific CI attribution when adding further results. RT-001, PHY-002
and measured fidelity remain incomplete/unvalidated.

## Selected exact identities

| File | SHA-256 |
| --- | --- |
| `docs/DEVELOPMENT.md` | `8a2f5270e66b6dbf7a5009a1dc91ff0b05a111b23c919a70356e7bd7636ed020` |
| `tools/validate_development_waves.py` | `5a93e12b045ddc7ee617de954f4a9cd8af625bca3bcc245e47d929bca4449e25` |
| `tools/verify_vst3_matrix_archive.py` | `4fc824ebe5038fcda4170415b002dd844c53a0ea167705cdbee03a9f23a32162` |
| `tools/validate_contact_peaks.py` | `5bfc879d0a4a42d48667cc2370bafc85a2bd6c6b08c18071c1d60ab7b85be717` |
| `.github/workflows/ci.yml` | `9a0d441f70dc60be6bdf7ec3b3ab5a3c5e046feed103a043633d1bd27911e94a` |
| `.github/workflows/vst3.yml` | `6cf2593353d05a6b122c99736399132b4c813ec8fba5af03b39ed4d5fdd8e3f3` |
| `include/rhodes/control_value.hpp` | `d7c282afdcffaef90f7872f97b37bfb34671448da64376d3439649c052d4d48d` |
| `include/rhodes/quadrature_fir.hpp` | `0ddcf691a5d251979a7adf1ea32d471c8fcf1fd6e5a7a2e203c8fb63974b7b6d` |
| `docs/validation/native-platforms-20261124.json` | `3b28ae2ee831786e1f69c8a340f07eb888e6be23743a155f59a014621dbc5f7c` |
