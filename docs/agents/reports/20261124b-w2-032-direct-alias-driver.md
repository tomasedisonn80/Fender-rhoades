# W2-032 — Bounded stationary primitive alias driver

2026-11-24; actual worker 32/100, wave 2; bounded RT-001 implementation
assignment from the integrator. **Implemented and froze the retained four-case
driver.** This worker ran help/plan checks and one small failing-process smoke;
the full native four-case numerical experiment has not run in this assignment.
No RT-001 completion, C03 promotion, alias acceptability, physical fidelity,
audibility, or realtime result is claimed.

## Scope and source binding

Changed only [tools/validate_direct_alias_toy.py](../../../tools/validate_direct_alias_toy.py)
and this report. Read `AGENTS.md`, `README.md`, `docs/PROJECT_STATE.md`,
`docs/ROADMAP.md`, `project/state.json`, and the complete
[W2-025 design](20261124b-w2-025-alias-diagnostic-design.md),
[W2-028 oracle interface](20261124b-w2-028-alias-oracle.md), and
[W2-029 producer interface](20261124b-w2-029-alias-primitive-probe.md).
Inspected the oracle and producer implementation plus the public pickup formula.
Historical reports, production primitives, CMake, Git, canonical state and
evidence registration remain the integrator's responsibility.

The frozen source and deterministic plan hashes are:

| Input | SHA-256 |
| --- | --- |
| Driver | `02733c24673d70f80734959a2a2aa1f143c33187d409c2b44605dd4cda6de4b2` |
| Imported independent oracle | `2bfed9f0c3e75fce9fed9a866756f9caa4d45728a7150912790b4caf2a71ff6c` |
| Native producer source | `e1cba5cd3b31e07954afae6b6740ab6fd22914becea50eede959506c2a41c808` |
| Final `--plan` stdout, including final newline | `62622fe2f1e004b13a4214c30497e213403bee2f4317d57f562e29e0fe1e9688` |

The driver rejects an oracle or producer source hash that differs from the
frozen binding. It independently checks all four fixture IDs, sample counts,
rates, modes, coefficients, mass and decay against its fixed constants.

## Frozen command and helper interfaces

```sh
python3 -B tools/validate_direct_alias_toy.py --help
python3 -B tools/validate_direct_alias_toy.py --plan
python3 -B tools/validate_direct_alias_toy.py \
  --probe /path/to/rhodes_alias_primitive_probe \
  --output-dir /path/to/NEW-directory \
  --build-record /path/to/build.json
```

`--build-record` is optional and explicit; the driver does not infer a compiler
from the binary, nearby files, environment, or source. Without a receipt, it can
report numerical results, but reproducibility remains incomplete and the CLI
returns nonzero. The supported receipt is the exact bounded W2-029 `build.json`
structure. Its source-hash keys are restricted to the expected producer, core,
two headers and design report. Its declared compiler, output and source pair
must agree with the supplied paths; untracked source and response-file arguments
are rejected. The raw receipt, claimed command and claimed compiler version are
retained. Compiler, binary and source bytes are hashed before and after.
Byte consistency does not prove that compilation occurred; the manifest states
this distinction explicitly.

`--help` and `--plan` are standalone forms. Repeated, unknown or abbreviated
options reject. Execution always plans this ordered matrix, with no case or
physical-parameter override:

| Case | Rate (Hz) | Samples | Modes (Hz) |
| --- | ---: | ---: | --- |
| `one_mode_48000` | 48000 | 1024 | 13500 |
| `one_mode_96000` | 96000 | 2048 | 13500 |
| `two_modes_48000` | 48000 | 1024 | 7500, 13500 |
| `two_modes_96000` | 96000 | 2048 | 7500, 13500 |

The successful matrix is exactly four cases and 6,144 samples. Every sample from
index zero through N−1 is retained. Index zero is a prescribed stationary phase
origin, not a strike or transient onset. No gain adjustment, time alignment,
phase fitting, onset trimming or magnitude-spectrum subtraction is performed.

| Importable helper | Contract |
| --- | --- |
| `plan()` | Fresh deterministic JSON-compatible plan; no subprocess or files. |
| `validate_probe_document(raw, fixture)` | Bounded raw UTF-8 bytes and one frozen oracle fixture; returns the strictly validated document or raises `DiagnosticError`. Process exit status must be checked separately. |
| `ArtifactStore(NEW)` | Creates one new directory and bounded exclusive artifacts; existing directories/files are never overwritten. |
| `run_probe(probe, fixture, output, deadline)` | One case for focused tests; absolute monotonic deadline; retains raw streams and a process record, including unsuccessful execution. |
| `execute(probe, output_dir, build_record=None)` | The fixed four-case plan, sequentially; returns the final manifest. First failure stops later cases, which remain explicitly `not_run`. |

The CLI returns zero only for a fully passed retained diagnostic, one for a
retained failure/incomplete result, and two for an argument or outer setup/
retention error. Final authority is `manifest.json`; subsequent integrity or
retention failures dominate previously passing numerical checks.

## Validation, numerical bookkeeping and retention

The parser rejects duplicate keys, nonstandard NaN/Infinity constants,
nonfinite conversion, excessive nesting, oversized tokens, wrong scalar types,
booleans used as numbers, unknown/missing schema keys, changed fixture metadata,
incorrect frame/modal dimensions, nonsequential counters and inconsistent ordered
modal sums. It limits a probe document to 1 MiB before decoding, 12 nesting
levels, 65,536 lexical tokens, 256-character strings and 64-character numeric
tokens. Signed integer tokens are bounded to 64 bits. The producer spelling
`-0` is preserved as negative binary64 zero in real arrays; it is rejected in
fields requiring exact integer counters.

The frozen oracle supplies independent analytic motion, direct chain-rule
voltage, signed harmonic voltage S, ideal strict-band target B, and independently
synthesized removed-line alias voltage. The driver retains P, S, B, direct
voltage, primitive and analytic modal q/v, summed motion, time, P−S, S−B, P−B,
and `(P−B)−((P−S)+(S−B))`. It retains additional direct/harmonic and emitted
pickup/motion residual arrays. Modal motion differences are reported with SI
units and without adding a modal acceptance threshold.

Signed-line CSV contains continuous all/retained/removed lines, coalesced sampled
all/alias lines, and normalized direct DFT coefficients of the direct polynomial
and S−B. Each row records the signed bin, original/current and folded frequency,
real and imaginary voltage coefficient, magnitude and phase. Complex coefficients
are coalesced before their powers are used. The driver preserves the oracle's
original-frequency differentiation, strict Nyquist rule, independent direct/
harmonic checks and exact zero analytic alias at 96 kHz.

The only numerical budgets are the design's separately named new limits:

| Check | New diagnostic limit |
| --- | ---: |
| Bookkeeping residuals, DFT coefficient agreement and Parseval gaps | 1e−12 V |
| Primitive P−S sample peak | 1e−10 V |
| Primitive P−S RMS | 1e−11 V |
| Alias magnitude acceptability | No threshold |

Additional emitted-pickup/motion, alias DFT and S/B/alias Parseval checks use the
same predeclared bookkeeping limit. Historical thresholds and gates are unchanged.

Artifacts include the pre-execution `plan.json`, planned command argv arrays,
optional raw build receipt, input snapshots before/after, each case's raw stdout
and stderr, process/exit/error record, arrays JSON, signed-line CSV, complete
metrics and final manifest. Arrays use portable round-trip JSON numeric values;
each array has shape, SI unit and a SHA-256 over concatenated little-endian
IEEE-754 binary64 bytes in row-major order. This preserves the raw floating-point
values without NumPy or a binary-container dependency.

Each retained artifact is exclusively created and has a size/hash inventory.
All existing artifacts are rehashed during finalization; metrics are separately
rehashed after writing. Input/plan changes, retained-file mutation, numerical
failure, process failure, timeout or retention failure prevent a passing final
status. The manifest excludes its own hash to avoid self-reference and includes
its byte count in the total artifact bound. The final status cannot be repaired
by a producer JSON `success: true` field or by later passing checks.

## Resource policy and platform boundary

There is one sequential numerical worker. Two bounded pipe-reader threads move
bytes only, using a four-chunk queue with 16 KiB chunks. Each case's stdout and
stderr has a separate 1 MiB cap. Overflow triggers kill/reap, preserves the exact
retained prefix, and explicitly marks capture incomplete; it does not pretend
the unseen remainder was retained. The total artifact cap is 16 MiB, including
final metadata, with a 1 MiB reserve for failure retention and at most 48 files.
Build records, source files, executable hashing, JSON construction and array/
line dimensions also have explicit bounds.

The 30-second envelope reserves 27 seconds for diagnostic work and three for
kill/reap and input/artifact finalization. SIGALRM preempts Python work on a
supported main-thread POSIX execution; the shared monotonic deadline also bounds
child waits. POSIX children start a new session and are killed as a process group;
other platforms use direct-child kill and record that narrower capability.
Kill/reap and reader joins are bounded. No later case runs after timeout.

On Linux the driver applies, reads back, and later restores a 256 MiB RLIMIT_AS
soft limit, inherited by the native child. This is per-process virtual address
space, not combined RSS. Unsupported memory enforcement or timer preemption is
reported explicitly; the driver never labels an unavailable mechanism enforced.
The file reader rejects nonregular inputs before opening and verifies the opened
descriptor's device/inode/type, using nonblocking/no-follow flags where present.
Final emergency metadata writes are byte-bounded, not a guarantee of filesystem
latency. No Windows/macOS enforcement or native run was performed by this worker.

## Actual verification and independent review

Initial read-only state commands returned zero:

```sh
python3 tools/project.py validate
python3 tools/project.py sync --check
python3 tools/project.py next --json
```

They reported revision 32, 14 tasks, 26 evidence records, fidelity `unvalidated`,
current generated state and no unclaimed available work. This worker stayed
within the integrator's existing RT-001 assignment.

The first `python3 -B tools/validate_direct_alias_toy.py --help` and `--plan`
commands passed. After source freeze the following standalone commands also
passed with empty stderr and a five-second subprocess timeout each:

```sh
python3 -S -B tools/validate_direct_alias_toy.py --help
python3 -S -B tools/validate_direct_alias_toy.py --plan
```

Help emitted 1,279 bytes; the final plan emitted 6,350 bytes. The plan was parsed
and checked for exactly four fixtures, 6,144 summed samples, no alias threshold
and no fitting/alignment. The final driver/oracle/producer hashes above were
rechecked. `-S` disables site-package initialization; no NumPy or third-party
runtime dependency is required.

One inline `python3 -B` failure smoke called
`execute(Path('/usr/bin/false'), NEW / 'failure')`. It asserted failed final
status, zero evaluated cases/samples, first case `failed` and remaining three
`not_run`, exact child exit 1, successful reap, identical before/after source/
binary/plan hashes, correct artifact size/hash inventory, and the total byte cap.
It passed and retained 24,362 bytes in the private scratch directory
`/workspace/scratch/cc294adc3e2f/w2-032-driver-smoke-5mgxia8c/failure`.
Recorded wall time before manifest was 0.006987966000451706 seconds, solely
execution bookkeeping. The smoke applied/read back Linux RLIMIT_AS and used
SIGALRM. Its pre-hardening driver SHA was
`ed595be96108f67411ae2671ebc082e57289af0fb40db8b57b3ed29d0c63b379`;
its `/usr/bin/false` SHA was
`d22a87e4d3bac34db5025ba813f9275244336880b297a5ae0bcbc19206454ddf`.
It is not presented as a native numerical or final-source regression result.

Separate worker 034 implemented
[test_direct_alias_driver.py](../../../tests/python/test_direct_alias_driver.py)
and reported **34/34 targeted tests passed normally in 1.039 s and under `-S` in
1.080 s**, without skips, against the final frozen driver hash. The test source
SHA was `274531b01cddb40608dc22547397d55882c005d3cf2480e78c8630dd009df154`.
Those tests used mocked subprocess/numerical work and actual temporary artifacts;
their own report owns the detailed commands and observations. Their review led
to the nonregular-file/identity checks, final artifact rehash and reproducibility
failure propagation before freeze. This report does not relabel their tests as
an experiment run by worker 032.

## Integration handoff

The driver API and source above are frozen. The integrator owns the shared CMake
target and registration of source-bound evidence. A later worker may run the
single approved full capture using worker 029's private producer and explicit
matching `build.json`, then retain/review all four cases and 6,144 samples.
That future result must remain separate from this implementation/failure-handling
record. Damping, transients, finite windows/kernels, whole-engine direct
convergence, RT-001 completion, C03, audibility, physical fidelity and realtime
deadlines retain their separate open scope.
