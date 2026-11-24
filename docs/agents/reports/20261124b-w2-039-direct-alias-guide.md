# W2-039 — Stationary alias diagnostic usage guide

2026-11-24; actual worker 039/100, wave 2; bounded RT-001 documentation
assignment. Added the practical
[stationary pickup alias guide](../../DIRECT_ALIAS_DIAGNOSTIC.md) and this report.
No product, test, CMake, README, host-adapter, Git, or canonical-state file was
changed. The integrator retains task ownership and acceptance decisions; the
separate continuation-C host assignment was left untouched.

## Guide content and decisions

The guide documents the exact four-case matrix and every retained sample from
zero through N-1; the single-pickup voltage boundary; modal summation before
pickup; and the P/S/B definitions. It distinguishes the algebraic identity
`P-B=(P-S)+(S-B)` from its retained floating-point closure residual. Signed
differentiation precedes folding, complex coefficients coalesce before powers,
and the ideal target excludes original Nyquist endpoints. The 96 kHz zero alias
is explained as identical full/retained spectra for these specific fixtures.

Commands cover the existing SDK-free CMake target, standard-library help/plan,
a fresh numerical run without a receipt, and a run using an actually matching
receipt. The guide explicitly states that the CMake build does not create the
specialized receipt, that an absent receipt yields CLI exit 1 despite possible
numerical success, and that byte consistency cannot prove compilation. It lists
all nine receipt fields and the exact five source-map keys. A historical receipt
cannot be relabeled for an unrelated fresh build. No fabricated generic receipt
or guessed compiler flags are provided.

The guide also covers producer/driver stdout, stderr and exit semantics; the
final manifest's authority; first-failure stopping; partial streams; binary64
array hashes versus JSON-file hashes; memory/time/artifact caps and actual
platform enforcement. A four-row measured table is rounded from the retained
worker 036 results, with links to the full exact archive and report. The new
numerical/bookkeeping budgets remain separate from the null alias-acceptability
threshold. RT-001, C03, fidelity, audibility and realtime boundaries are retained.

Sources reviewed were `AGENTS.md`, README, roadmap, generated/current canonical
state, the oracle and driver implementation, CMake target registration, and
workers [025](20261124b-w2-025-alias-diagnostic-design.md),
[028](20261124b-w2-028-alias-oracle.md),
[029](20261124b-w2-029-alias-primitive-probe.md),
[032](20261124b-w2-032-direct-alias-driver.md),
[033](20261124b-w2-033-alias-oracle-tests.md),
[034](20261124b-w2-034-alias-driver-tests.md), and
[036](20261124b-w2-036-direct-alias-capture.md). Existing derivations and primary
references are linked through those reports; no additional physical facts or
measurement claims were introduced.

## Actual verification

Required `project.py validate`, `sync --check`, and `next --json` all returned
0. The observed current state was revision 34: 15 tasks, 26 evidence records,
no available unclaimed task, generated state current, fidelity `unvalidated`.

An inline Python subprocess wrapper ran these four read-only commands once,
with the current Python executable, absolute script paths, a temporary empty
working directory and a ten-second timeout per command:

```sh
python3 -S -B tools/direct_alias_oracle.py --help
python3 -S -B tools/direct_alias_oracle.py --plan
python3 -S -B tools/validate_direct_alias_toy.py --help
python3 -S -B tools/validate_direct_alias_toy.py --plan
```

Each returned 0 with empty stderr. The temporary working directory remained
empty. Both plans were parsed as JSON and their declared fields inspected.

| Read-only output | Bytes | SHA-256 |
| --- | ---: | --- |
| Oracle help | 1,601 | `b4c3091d15c0873e74453aa7239a6d52c0f64c7db5a5f11031cb68fd2d24738f` |
| Oracle plan | 3,768 | `a1e56b7fef464d1c3694481d109c2ba0b7ab438426190e5f8a017ad18ad7add5` |
| Driver help | 1,279 | `25dcc5ecc4a42913437bff7cf1eabe3b8abd402de4da6a81d692b6e43fd54c4a` |
| Driver plan | 6,350 | `62622fe2f1e004b13a4214c30497e213403bee2f4317d57f562e29e0fe1e9688` |

The CMake target and receipt-validator schema were inspected directly. The
retained archive's field names, receipt keys and 27 driver filenames were read
to check the documented interface. Numerical values were taken from the frozen
capture report, without re-evaluating arrays or launching a producer.

`python3 -B tools/check_docs.py` passed after the guide was created. The final
guide/report links and final newlines were checked again before freeze. These
checks validate documentation structure and existing local destinations; they
do not execute examples or establish platform behavior.

## Frozen bindings and handoff

| File | SHA-256 |
| --- | --- |
| New guide, 14,620 bytes | `97f0c3c7238376079597a09c459ae17cf69cd40851f76d197706639c938a8af3` |
| `tools/direct_alias_oracle.py` | `2bfed9f0c3e75fce9fed9a866756f9caa4d45728a7150912790b4caf2a71ff6c` |
| `tools/validate_direct_alias_toy.py` | `02733c24673d70f80734959a2a2aa1f143c33187d409c2b44605dd4cda6de4b2` |
| `apps/alias_primitive_probe.cpp` | `e1cba5cd3b31e07954afae6b6740ab6fd22914becea50eede959506c2a41c808` |
| Retained initial experiment | `2e6bbe63ae26579ad578bb545ea415e252620ce1668e87b886f8c6a8926d3a9d` |

The four pre-existing source/archive hashes matched before and after this
documentation work. No native process, build, unit/full test suite, numerical
experiment, performance capture, or historical-results recomputation ran.
The documented build/execution examples were checked against the implementation
and retained evidence, rather than executed in this assignment.

Next integrator action: link the guide from the appropriate developer index
after coordinating concurrent documentation ownership, and retain this report
with the campaign ledger. This documentation adds no acceptance evidence beyond
the specific interface and link checks above.
