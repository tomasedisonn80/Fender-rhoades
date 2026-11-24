# Stationary pickup alias diagnostic

[`validate_direct_alias_toy.py`](../tools/validate_direct_alias_toy.py) measures
primitive numerical error and stationary alias contribution separately for four
fixed synthetic cases. It combines the C++
[`rhodes_alias_primitive_probe`](../apps/alias_primitive_probe.cpp) with an
[independent analytic oracle](../tools/direct_alias_oracle.py). The boundary is
one pickup's voltage before the harp network or gain stage. There is no strike,
damper, filter, host callback, or instrument recording in this experiment.

**Passing this diagnostic does not complete RT-001 or C03.** The constants are
development fixtures, and alias magnitude has no acceptability threshold.
The [retained experiment](validation/direct-alias-initial-20261124b.json) and
[measurement report](agents/reports/20261124b-w2-036-direct-alias-capture.md)
provide the observed results and their source/build identities.

## What the three signals mean

| Signal | Definition at sample `n` |
| --- | --- |
| `P` | Voltage emitted by the native modal and pickup primitives. |
| `S` | Full analytically differentiated harmonic polynomial sampled at `n/Fs`. |
| `B` | The same continuous spectrum with only original frequencies satisfying `abs(F) < Fs/2` retained, then sampled at `n/Fs`. |

The exact algebraic identity is

\[
P-B=(P-S)+(S-B).
\]

`P-S` measures numerical error, `S-B` measures the stationary alias contribution,
and `P-B` measures total error against this ideal strict-band target. The tool
also retains the floating-point closure residual of this identity. It applies
no gain, phase, timing, onset, or magnitude-spectrum fit. Peak means the largest
absolute **sample** value; it is not a continuous-time peak certificate.

For a flux term `q*cos(2*pi*F*t+alpha)`, Faraday's sign gives voltage
`2*pi*F*q*sin(2*pi*F*t+alpha)`. Differentiation uses the **original signed
frequency** `F`. The oracle then folds exact integer bins to `[-N/2, N/2)` and
sums complex coefficients at coincident bins before taking magnitudes or powers.
This preserves phase cancellation and reinforcement; differentiating at the
folded frequency or subtracting magnitude spectra would describe a different
signal. The target excludes original Nyquist endpoints. DC/Nyquist conjugates
coalesce once, with no additional doubling. See the
[derivation and public references](agents/reports/20261124b-w2-025-alias-diagnostic-design.md)
and [implemented conventions](agents/reports/20261124b-w2-028-alias-oracle.md).

## Frozen cases and sampling

| Case ID | Rate | Samples | Mode frequencies | Amplitudes | Phases |
| --- | ---: | ---: | --- | --- | --- |
| `one_mode_48000` | 48,000 Hz | 1,024 | 13,500 Hz | 0.0001 m | pi/7 |
| `one_mode_96000` | 96,000 Hz | 2,048 | 13,500 Hz | 0.0001 m | pi/7 |
| `two_modes_48000` | 48,000 Hz | 1,024 | 7,500; 13,500 Hz | 0.0001; 0.00006 m | pi/7; -pi/5 |
| `two_modes_96000` | 96,000 Hz | 2,048 | 7,500; 13,500 Hz | 0.0001; 0.00006 m | pi/7; -pi/5 |

Every mode has mass 0.001 kg and zero decay. The flux polynomial is
`Phi(x)=0.4*x + 800*x*x + 400000*x*x*x`, with coefficient units Wb-turn/m,
Wb-turn/m² and Wb-turn/m³. Modal displacement and velocity are summed before
one nonlinear pickup conversion. The
[producer contract](agents/reports/20261124b-w2-029-alias-primitive-probe.md)
defines the exact JSON fields and observation order.

Every record retains indices **0 through N-1**, observing before each advance;
the endpoint `N/Fs` is not appended. The common coherent record duration is
`N/Fs = 1024/48000` seconds, with bin spacing 46.875 Hz. Index zero selects a
stationary phase origin, rather than a strike. In both 96 kHz fixtures all
generated lines lie strictly below Nyquist, so the full and retained spectra
are identical: `S-B` is exactly zero, without rounding a small alias estimate
to zero. This property is specific to these fixtures.

## Build, inspect, and run

Use the repository's [C++20/CMake requirements](DEVELOPMENT.md) and Python 3.11
or later. The native target does not require the VST3 SDK. The Python oracle and
driver use only the standard library; NumPy and SciPy are not required.
From the repository root, a separate build directory can be prepared with:

```sh
cmake -S . -B build/alias -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build build/alias --config Release --target rhodes_alias_primitive_probe --parallel 2
```

For a single-configuration build the executable is normally
`build/alias/rhodes_alias_primitive_probe`; a Visual Studio build normally uses
`build/alias/Release/rhodes_alias_primitive_probe.exe`. Select the executable
actually produced by your generator. These CMake commands do not automatically
create the specialized build receipt described below.

The producer itself accepts `--help` or `--case ID`, using one of the four exact
IDs above. Help emits text; a successful case emits one JSON document on stdout
and exits 0. Invalid arguments or computation/output failures exit 2 with a
stderr diagnostic. A failed output write can leave partial stdout, which is why
the retained driver checks both the process exit and complete bytes.

Read the fixed plan before execution:

```sh
python3 -S -B tools/direct_alias_oracle.py --help
python3 -S -B tools/direct_alias_oracle.py --plan
python3 -S -B tools/validate_direct_alias_toy.py --help
python3 -S -B tools/validate_direct_alias_toy.py --plan
```

Help and plan write stdout only and create no artifacts. The driver requires
each of these options to stand alone. Execution always uses all four cases in
the table's order, totaling 6,144 samples; it has no case, rate, physical-parameter,
resource-limit, or fitting override.

For numerical inspection of a fresh CMake build without a matching receipt:

```sh
python3 -B tools/validate_direct_alias_toy.py \
  --probe build/alias/rhodes_alias_primitive_probe \
  --output-dir build/alias-result-001
```

The output directory must be **new**, with its parent already present. An
existing directory is refused; choose a new name for each attempt. Without
`--build-record`, valid numerical results remain available, but
`reproducibility_complete` is false and the CLI returns **1**, including when
all numerical checks pass. Inspect the manifest rather than treating that exit
as an unexplained numerical failure.

When an actual matching build receipt is available, substitute its paths:

```sh
python3 -B tools/validate_direct_alias_toy.py \
  --probe PATH_TO_MATCHING_PROBE \
  --output-dir build/alias-result-002 \
  --build-record PATH_TO_OBSERVED_BUILD_RECORD
```

### Build receipt requirements

The accepted receipt is the bounded worker-029 `build.json` format, documented
by the [observed producer build](agents/reports/20261124b-w2-029-alias-primitive-probe.md)
and checked by the driver's `_receipt` function. It contains exactly these
fields:

| Fields | Required content |
| --- | --- |
| `compiler_path`, `compiler_sha256`, `compiler_version` | An absolute compiler path, its lowercase SHA-256, and the recorded version string. The compiler file must remain accessible and match. |
| `source_sha256_before` | Exact hashes for the five paths listed below. |
| `compile_command` | The actual bounded argument array identifying the compiler, the two expected C++ source files, and final `-o` plus the supplied executable path. |
| `compile_returncode`, `compile_stdout`, `compile_stderr` | The observed successful compilation exit code `0` and captured streams. |
| `binary_sha256_before` | SHA-256 matching the executable supplied to the driver. |

The exact source-map keys are `apps/alias_primitive_probe.cpp`,
`src/research_engine.cpp`, `include/rhodes/research_engine.hpp`,
`include/rhodes/control_value.hpp`, and
`docs/agents/reports/20261124b-w2-025-alias-diagnostic-design.md`.
The command must name the two C++ inputs once each using those repository-relative
names. Response-file arguments and additional source/header arguments are
rejected. This format describes the recorded direct compiler invocation; it
does not accept an arbitrary `cmake --build` command as an equivalent receipt.

Record these facts while building. Do not fill in guessed flags or reuse the
historical receipt for a different executable. The receipt from the retained
study matches worker 029's particular compiler, paths, sources and binary;
copying its JSON does not make a fresh build match. The driver hashes the
available compiler, source and executable bytes, and retains the claimed command
and version. This establishes **byte consistency**, not independent proof that
the stated compilation happened. The manifest keeps `compilation_proven=false`.

## Read results and retained evidence

On a completed attempt, driver stdout is a small JSON summary. Native stdout
and stderr remain separate byte-for-byte files, alongside each process record.
Argument or outer setup/retention errors are printed to driver stderr. Native
`success: true` describes completed primitive calculation; it cannot override
a failing process exit or a diagnostic/provenance failure.

| Driver exit | Meaning |
| --- | --- |
| `0` | Standalone help/plan succeeded, or the four-case retained diagnostic passed its numerical and reproducibility checks. |
| `1` | A retained diagnostic failed or remained incomplete, including an otherwise passing run without a build receipt. |
| `2` | Invalid invocation or an outer setup/retention error, such as an existing output directory. A complete final manifest may be unavailable. |

`manifest.json` is the final authority. Read `status`, `passed`,
`numerical_passed`, `reproducibility_complete`, `failures`, `incomplete_reasons`,
case/sample counts, and actual `limits`. It includes source/binary identities
before and after, plan identity, artifact hashes and integrity checks. It excludes
its own hash to avoid self-reference, while including its bytes in the artifact
total. A later integrity or retention failure overrides earlier successful
numerical checks. The first execution/comparison failure stops later cases,
which remain explicitly `not_run`; there is no automatic retry.

The retained files include:

| Files | Use |
| --- | --- |
| `plan.json`, `commands.json`, `inputs.before.json`, `inputs.after.json` | Fixed definitions, invoked commands and input identities. |
| `build_record.raw.json`, when supplied | Exact receipt bytes. |
| `CASE.stdout`, `CASE.stderr`, `CASE.process.json` | Native streams, exit, capture completeness, timeout/kill/reap information. |
| `CASE.arrays.json` | Modal motion, P/S/B, error arrays, residuals and units for every sample. |
| `CASE.signed_lines.csv` | Continuous, retained, removed and folded complex lines, plus direct DFT checks at the declared bins. |
| `metrics.json`, `manifest.json` | Comparisons and final retention/provenance outcome. |

Array values are JSON numbers that round-trip as binary64. Array hashes use
row-major concatenated little-endian `struct.pack('<d', value)` bytes. They
are different from hashes of their JSON containers. Retain signed zeros and all
sample indices when reconstructing them. DFT checks evaluate selected exact
line and guard bins; they do not claim a full FFT was performed.

The new diagnostic budgets are **1e-10 V peak and 1e-11 V RMS for P-S**, with
**1e-12 V bookkeeping** checks. Historical budgets remain unchanged.
`alias_acceptability_threshold` is null: a nonzero `S-B` is a measured quantity,
not an automatic pass/fail verdict on an instrument.

## Resource and platform limits

The driver schedules one sequential numerical worker, four cases and at most
2,048 samples per case. Each native stream has a 1 MiB cap. The total artifact
limit is 16 MiB across at most 48 files, including a 1 MiB reserve for final
metadata; receipts are limited to 64 KiB. JSON shape, nesting and token limits
are declared in `--plan`. On stream overflow it kills/reaps the child and keeps
only the exact retained prefix, explicitly marked incomplete.

The 30-second envelope reserves 27 seconds for diagnostic work and three for
cleanup/finalization. Supported main-thread POSIX runs use SIGALRM preemption;
monotonic checks and bounded child waits also apply. Linux applies and reads
back a 256 MiB RLIMIT_AS soft ceiling, or a lower existing soft limit, inherited
by the child. This is per-process virtual address space, not combined RSS.
The manifest records unavailable enforcement. POSIX uses process-group kill;
other platforms have the narrower direct-child capability. Final emergency
metadata is byte-bounded, without a guaranteed filesystem latency. The retained
native experiment establishes Linux behavior only; this guide does not add
Windows/macOS execution evidence.

## Retained four-case observation

The [24 November 2026 capture report](agents/reports/20261124b-w2-036-direct-alias-capture.md)
records one Linux/GCC-built-probe attempt with all four native exits zero,
6,144 samples, complete reproducibility and stable input bytes. Values below
are rounded from its retained metrics, in volts; each cell is **peak / RMS**.

| Case | Numerical P-S | Alias S-B | Total P-B |
| --- | ---: | ---: | ---: |
| One mode, 48 kHz | 3.61045e-13 / 1.15662e-13 | 0.695070716 / 0.480168620 | 0.695070716 / 0.480168620 |
| One mode, 96 kHz | 2.30926e-14 / 1.23060e-14 | 0 / 0 | 2.30926e-14 / 1.23060e-14 |
| Two modes, 48 kHz | 4.04787e-13 / 1.03583e-13 | 0.277885043 / 0.175064227 | 0.277885043 / 0.175064227 |
| Two modes, 96 kHz | 1.83409e-13 / 5.60811e-14 | 0 / 0 | 1.83409e-13 / 5.60811e-14 |

The largest closure residual was 2.7755575615628914e-17 V. Both 96 kHz cases
retained byte-identical S/B arrays and exactly zero alias arrays. The full
[archive](validation/direct-alias-initial-20261124b.json) contains all 27 driver
outputs plus capture/verification material, 36 exact text artifacts in total.
For each archive entry, UTF-8 encode its `text` verbatim and verify the recorded
byte count and SHA-256 before use. This reconstructs the saved evidence without
executing the producer.

The [independent oracle tests](agents/reports/20261124b-w2-033-alias-oracle-tests.md)
and [driver tests](agents/reports/20261124b-w2-034-alias-driver-tests.md) cover the
algebra, thresholds and protocol/failure behavior separately. Damping,
strike/retrigger/release, finite-window leakage, finite-kernel response,
whole-engine convergence, calibrated fidelity, audibility and realtime deadlines
remain outside this diagnostic. Use the separate
[filtered-path decomposition](validation/filtered-decomposition-20261124b.md)
and [runtime methodology](BENCHMARKING.md) for their respective boundaries.
