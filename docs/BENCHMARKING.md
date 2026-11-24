# Synthetic runtime benchmarks

Five executables expose three timing boundaries of the synthetic audio model.
The two core benchmarks are SDK-free; the direct, filtered and quadrature VST3
benchmarks require the optional pinned SDK. None includes the offline finite-mass
contact solver. Timing observations alone establish neither calibrated fidelity
nor complete `RT-001` acceptance.

| Executable | Timed operation | Controls outside the timed operation |
|---|---|---|
| `rhodes_runtime_benchmark` | One complete `EventProcessor::process()` call, including input preflight, event dispatch, sustain changes and sample rendering | Preparation, schedule generation and hashing, initial conditions, output checks and reporting |
| `rhodes_vst3_benchmark` | One complete concrete `RhodesProcessor::process()` call, including VST3 input access, ownership, automation and state publication | Setup, input schedules, output sentinel initialization, output checks, hashes and reporting |
| `rhodes_vst3_filtered_benchmark` | The same complete VST3 callback using the filtered signal path | Setup, input schedules, output sentinel initialization, output checks, hashes and reporting |
| `rhodes_vst3_quadrature_benchmark` | The same complete VST3 callback using the quadrature signal path | Setup, input schedules, output sentinel initialization, output checks, hashes and reporting |
| `rhodes_benchmark` | `ResearchEngine::process()` only | The preceding note-on calls, preparation, output checks and reporting |

Compare like boundaries. Subtracting results from separate runs does not isolate
event cost reliably when CPU frequency, cache state and scheduling differ.

## Event processor benchmark

Build an optimized configuration and retain its actual compile/link commands:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --target rhodes_runtime_benchmark
./build/rhodes_runtime_benchmark --blocks 1000 --warmup-blocks 32 \
  --block-size 64 --sample-rate 48000 --active-keys 73 \
  --workload retrigger256 --offsets distributed \
  --machine "documented-machine-label" > runtime-benchmark.json
```

For a Visual Studio multi-configuration build, use
`build/Release/rhodes_runtime_benchmark.exe`. The executable emits schema-version-2
JSON on success; `--help` emits text. Malformed arguments exit 2. An incomplete
measured workload emits its diagnostic JSON and exits 3. Inspect `complete` and
`integrity.status_counts` before interpreting timings.

| Option | Accepted values | Default |
|---|---|---|
| `--blocks` | Integer `1..1000000` measured calls | `1000` |
| `--block-size` | Integer `1..4096` frames | `128` |
| `--sample-rate` | Integer `32000..192000` Hz | `48000` |
| `--active-keys` | Integer `0..73`; `retrigger256` requires at least one | `73` |
| `--warmup-blocks` | Integer `0..10000` | `32` |
| `--workload` | One of the seven schedules below | `chord` |
| `--offsets` | `distributed` or `same`; controls storm-event offsets | `distributed` |
| `--machine` | Nonempty printable ASCII, at most 128 bytes | `unknown` |

The machine label is operator-supplied text. It does not discover hardware or
power policy. Large block counts also produce large raw-timing arrays.

### Schedules and excluded setup

Every workload traverses all 73 key slots and pickup branches. Selected keys
span indices `0..72` deterministically; one selected key uses index 36.
Intensity `0.7` is a synthetic impulse intensity, not measured hammer speed.

| Schedule | Measured behavior |
|---|---|
| `silence` | No input events; all slots still execute |
| `chord` | Retrigger selected keys at block zero and every `ceil(rate/(2*frames))` blocks; those note events are inside the timer |
| `retrigger256` | Exactly 128 note-off/note-on pairs per block, rotating through selected keys |
| `pedal256` | Exactly 256 alternating sustain events per block after a sustained-note initial condition |
| `tail-held` | Free evolution after a held-note initial condition, with no measured events |
| `tail-sustained` | Free evolution after notes are released under sustain, with no measured events |
| `tail-release` | Free evolution after an ordinary release, with no measured events |

Storm offsets are either all zero or `floor(event_index*frames/256)`. Explicit
sequence numbers preserve order when several events share an offset. Schedules
are generated before timing, but their validation and application are timed.

Tail/pedal initial conditions use one excluded sample with bounded events.
Warmup follows the same schedule, then the processor is reset and initialized
again for measurement. JSON distinguishes these excluded initial-condition
frames/events from measured events and records the actual warmup count. This
benchmark reset does not describe normal retrigger behavior.

### JSON interpretation and reproducibility

`timing.raw_callback_us` retains every measured call. `timing.summary` contains
observation count, mean, nearest-rank p50/p95/p99, maximum, nominal block duration
and the count strictly exceeding that duration. Four consecutive quarter-window
summaries expose changes over the run; an empty window has a null summary.
The period is `1e6*frames/rate` microseconds. With 1000 observations, p99 is the
990th sorted observation, not a confidence bound on extreme latency.

The executable times exactly one complete event-processor call using
`std::chrono::steady_clock`. It performs no overhead subtraction, affinity
selection, priority change or device-paced scheduling. Output/checksum checks,
block-end modal inspection, sorting, hashes and JSON serialization are outside
the timestamps. Observed elapsed time includes scheduling interruptions; the
report cannot attribute an outlier to DSP cost, contention or another cause.

Keep the raw JSON with the executable hash, relevant source-file hashes, Git
revision and relevant uncommitted changes, compiler/version, complete compile
and link flags, architecture, OS/kernel, visible CPU, container constraints,
affinity, scheduling policy, power policy and competing work. Record unavailable
metadata explicitly. The executable's `unrecorded` fields require this external
record; `NDEBUG`, a Release label or a nominal clock tick alone is insufficient.
A source/build change invalidates comparison with an earlier binary identity.

`integrity` records numerical status, finite output/checksum, peak synthetic
voltage and observed subnormal values. Modal subnormals are inspected only at
block ends. A short run without subnormals does not validate long-tail behavior,
flush-to-zero policy or denormal timing. These benchmark executables do not
detect allocations; separate allocation/locking checks are required.

The [A038 short matrix](agents/development-20261123/W2-A038.md) records one actual
Linux container observation with declared source/build identity and limitations.
It is not a performance threshold. These calls do not time MIDI ownership,
state-mailbox work, VST3 interface calls, an audio device, wake-up jitter or host
scheduling. Zero observed nominal misses cannot establish a real-time guarantee,
and an observed maximum is not a worst-case execution-time bound. Repeat with a
declared order and controlled or documented competing load before comparing
implementations. Keep long-tail, host/device and numerical-error acceptance
separate from this short synthetic matrix.

The later [matched core-cache experiment](agents/development-20261123/W3-A041.md)
compared the same `EventProcessor` boundary before and after precomputing the
free/damped transition matrices. For its 500-call, 64-frame, 73-key `pedal256`
case at 48 kHz, the observed median changed from 1400.686 to 229.495 µs and
nominal-period misses from 399 to 2. Four short storm renders retained identical
output/final-state hashes. The [baseline](validation/runtime-baseline-20261123.json)
and [cached-transition results](validation/runtime-transition-cache-20261123.json)
retain source identities and the uncontrolled-container limitations. These are
historical matched core measurements, not a before/after comparison with the
VST3 callback measurements below.

[A082](agents/development-20261123/W5-A082.md) subsequently preserved all sixteen
original A038/A041 timing JSONs in a
[self-contained historical archive](validation/runtime-historical-raw-20261123.json),
together with manifests, wrappers, reports and recorded trajectory comparisons.
An embedded standard-library verifier recomputes 14,000 durations and 64
quarter-window summaries without the original scratch files. Exact historical
payloads and their hashes are retained; this preservation work did not rerun
timings or DSP trajectories and adds no performance or numerical acceptance.

## Actual VST3 callback benchmark

Configure the optional SDK build as described in [VST3.md](VST3.md). Its
`rhodes_vst3_checks` aggregate builds all three VST3 benchmarks and all three
modules, the SDK validator and retained host tests when `BUILD_TESTING=ON`.
To build and run only the direct benchmark in that configured tree:

```sh
cmake --build build/vst3 --config Release --target rhodes_vst3_benchmark --parallel 2
./build/vst3/rhodes_vst3_benchmark --blocks 300 --warmup-blocks 32 \
  --block-size 64 --sample-rate 48000 --active-keys 73 \
  --workload pedal --sample-format 64 \
  --machine "documented-machine-label" > build/vst3-callback.json
```

With Visual Studio, use `build/vst3/Release/rhodes_vst3_benchmark.exe`.

Use target and executable `rhodes_vst3_filtered_benchmark` for the filtered
variant, or `rhodes_vst3_quadrature_benchmark` for quadrature, with the same
arguments. All three compile the [shared benchmark source](../apps/vst3_benchmark.cpp)
with their respective signal path selected at build time. Each `identity` object
records its own plug-in name and audio path:

| Variant | `state_model` | `latency_frames` | Additional `identity` fields |
|---|---:|---:|---|
| Direct | `1` | `0` | `oversampling_factor=1`; `internal_sample_rate_hz` equals the requested host rate |
| Filtered | `2` | `48` | Selected `oversampling_factor` and `internal_sample_rate_hz` |
| Quadrature | `3` | `48` | `quadrature_nodes=6`; `observation_scheme="gauss-legendre-host-cell"`; no `oversampling_factor` or `internal_sample_rate_hz` |

Quadrature observes six nonuniform positions within each host interval and
advances mechanics once per host frame. Its nodes do not define a uniform
internal sampling rate. Compare matching workloads and host rates, keeping the
reported filter delay separate from callback execution time.

For example, build and capture a quadrature observation in the configured tree:

```sh
cmake --build build/vst3 --config Release --target rhodes_vst3_quadrature_benchmark --parallel 2
./build/vst3/rhodes_vst3_quadrature_benchmark --blocks 512 --warmup-blocks 32 \
  --block-size 64 --sample-rate 48000 --active-keys 73 \
  --workload held --sample-format 64 \
  --machine "documented-machine-label" > build/vst3-quadrature-callback.json
```

With Visual Studio, use
`build/vst3/Release/rhodes_vst3_quadrature_benchmark.exe`.

| Option | Accepted values | Default |
| --- | --- | --- |
| `--blocks` | Integer `1..1000000` measured calls | `1000` |
| `--block-size` | Integer `1..8192` frames | `128` |
| `--sample-rate` | Integer `32000..192000` Hz | `48000` |
| `--active-keys` | Integer `0..73` | `73` |
| `--warmup-blocks` | Integer `0..10000` | `32` |
| `--workload` | `held`, `pedal` or `gain` | `held` |
| `--sample-format` | `32` or `64` | `64` |
| `--machine` | Nonempty printable ASCII, at most 128 bytes | `unknown` |

All workloads strike selected keys at block zero and every
`ceil(rate/(2*frames))` blocks, approximately every half-second. `held` retains
note ownership. `gain` adds one gain point per sample. `pedal` immediately
releases each struck key and flips sustain at every sample while the keys ring.
These are deterministic synthetic stress schedules, not recorded performances.

Large owners, buffers, schedules and the timing vector are allocated before
measurement. Warmup follows the selected schedule, then a lifecycle reset and
zero-frame flush restore gain `0.5` and sustain off before measurement. Only the
actual `RhodesProcessor::process()` call is timed: host getters, input preflight,
MIDI ownership, automation, output clearing, core rendering and state publication
are included. Output sentinel initialization, verification, hashes, sorting and
serialization are outside the timestamps. The executable calls the concrete
processor; it does not measure shared-module loading, a DAW, device scheduling
or wake-up jitter.

After the measurement reset, quadrature has no completed host cell for its first
output frame, which is exactly zero even when keys are struck. Consequently,
an excited `--blocks 1 --block-size 1` quadrature observation reports
`integrity.nonzero_output_expected=false`. Every longer excited measurement must
contain nonzero output; a zero-key workload remains silent in every variant.
The 48-frame latency describes the kernel delay, while the first completed cell
can contribute at the second output frame. Warmup does not remove this startup
behavior because the measurement phase always resets afterwards.

Success emits schema-version-1 JSON with `benchmark="synthetic_vst3_process"`.
Inspect `complete` and `integrity` before using timings: the report includes
success/failure counts, cursor and owner observations, host-getter counts,
finite/stereo/silence checks, nonzero output, checksum and FNV-1a hashes.
`timing.raw_callback_us` preserves every call; the summary reports mean,
nearest-rank p50/p95/p99, maximum, nominal period `1e6*frames/rate`, and durations
strictly greater than that period. Malformed CLI arguments exit `2`; an
incomplete measured run emits JSON and exits `3`; success and `--help` exit `0`.

The same external source/build/environment record described above is required;
the machine label does not discover hardware. Timing uses `steady_clock` without
overhead subtraction, thread affinity, priority changes or device pacing. This
executable does not instrument allocation or locking. Finite output and a short
run cannot establish long-tail denormal behavior, a worst-case bound or a
real-time guarantee.

### Frozen 72-process comparison

The [quadrature benchmark protocol](specs/QUADRATURE_BENCHMARK_20261123.md) and
[machine-readable run plan](validation/quadrature-benchmark-plan-20261123.json)
fix 12 profiles, three variants and two balanced rounds: **72 processes,
40,128 measured callbacks and 3,846,144 measured host frames**. The plan itself
contains no observations; the three completed captures are linked below.
Each capture compares 36 same-variant/profile repeat pairs. Different signal
paths retain different output and latency contracts.

#### Capture a new matrix

[`run_vst3_matrix.py`](../tools/run_vst3_matrix.py) currently supports this
**Linux-only protocol**, with working single-CPU affinity APIs and a nonempty
permitted CPU set. It requires an up-to-date, uninstrumented **Release Ninja**
build at the fixed repository path `build/vst3`, its `compile_commands.json`,
and the clean pinned SDK root plus four required submodules documented in
[Dependencies](DEPENDENCIES.md). The `--sdk` path must resolve to the SDK recorded
by that build. Native VST3 support on other operating systems does not make this
launcher a Windows/macOS timing protocol.

From the repository root, using the sibling SDK checkout from the
[VST3 guide](VST3.md), configure a new build directory or an existing Ninja one:

```sh
cmake -S . -B build/vst3 -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=ON -DRHODES_BUILD_VST3=ON \
  -DRHODES_VST3_SDK_ROOT="${PWD}/../vst3sdk" \
  -DRHODES_ENABLE_SANITIZERS=OFF -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build/vst3 --target rhodes_vst3_benchmark \
  rhodes_vst3_filtered_benchmark rhodes_vst3_quadrature_benchmark --parallel 2
```

A build directory already configured for another generator cannot be reused by
this configure command. The launcher does not build or repair a stale tree; it
checks freshness with Ninja before capture. Finish compilation and other
intensive work, then supply an honest account of remaining competing activity:

```sh
python3 tools/run_vst3_matrix.py --help
python3 tools/run_vst3_matrix.py \
  --sdk "${PWD}/../vst3sdk" \
  --output build/vst3-matrix-new \
  --competing-activity "Describe actual concurrent work and known background load"
```

Use a new output-directory name whose parent already exists. The resolved
directory must not already exist; creation is exclusive, with no overwrite or
retry option. The launcher verifies the frozen plan hash, pins each child to the lowest initially
permitted logical CPU, records the child's actual binding, and retains inherited
priority and power settings. It captures machine/load observations, source and
binary identities, build commands and raw streams, then rechecks inputs and
recomputes workload/timing integrity. Scheduling interruptions and every outlier
remain in the data.

Exit `0` means the declared workload and repeat-integrity checks completed;
`1` means the attempted collection has incomplete or inconsistent results;
`3` means an operational failure. Argument errors use argparse's exit `2`.
Inspect `manifest.json`, each execution record, `attempted_runs` and
`unattempted_run_ids`. A timeout cannot be marked complete even if the final
wait returns zero. Invalid child-affinity metadata stops later runs after
retaining the attempted record; final snapshot failures also prevent completion.
Storage failures can still leave partial evidence, so directory existence alone
is not success. Nominal-period misses are retained observations and do not
change a successfully verified collection into a timing failure.

#### Retained captures and their interpretation

The [batching report](validation/batched-quadrature-20261124.md) links the full
raw archives and source/binary bindings for all three captures:

| Capture | Q6 nominal-period misses | Largest Q6 callback |
| --- | ---: | ---: |
| [Original scalar traversal](validation/vst3-matrix-20261124.json) | 2,947 | 91,441.456 µs |
| [First batch](validation/vst3-matrix-batched-20261124.json) | 3,852 | 96,939.447 µs |
| [Final independent-node loops](validation/vst3-matrix-batch-final-20261124.json) | 1,716 | 99,882.967 µs |

Each capture contains 13,376 callbacks per variant, but mixes block sizes,
sample rates and stress workloads. These totals are inventories of observed
misses, not comparable population miss rates. The worse first-batch count and
larger final maximum remain visible. Scheduling, frequency and other work on
the shared host were uncontrolled; the final capture's possible startup overlap
with a short parser review is explicitly recorded. Compare individual matching
profiles and both rounds before drawing a limited observation.

The separate [16-process CPU experiment](validation/q6-cpu-paired-20261124.json)
uses a declared before/after/after/before order repeated four times. Whole-child
user CPU means were **1.532606 s before** and **1.422067 s after**, approximately
**7.2% lower in this observation**. These measurements include setup, warmup and
JSON output and exclude descheduled time; they are not callback wall durations.
They do not isolate cache/frequency effects, establish a causal or general
speedup, or guarantee device deadlines. The
[independent statistics audit](agents/reports/20261124-w3-51-timing-statistics.md)
recomputed the retained values without rerunning benchmarks.

The separate [Q6 numerical study](validation/quadrature-audio-full-20261123.json)
records 2,097 cases and 2,534,400 frames against independent Q6 and refined
continuous Q64 references. The
[batched replay](validation/batched-quadrature-compatibility-20261124.json)
retains exact output identity for those cases and all 24 legacy WAV fixtures;
it does not recompute the reference integration. This evidence remains specific
to its synthetic model, kernel, inputs and tested toolchain. The direct/filtered
aliasing and convergence findings, measured fidelity, allocation policy and
DAW/device timing retain separate gates; these captures do not complete RT-001.

#### Verify the retained archives without rerunning workloads

The standard-library-only
[`verify_vst3_matrix_archive.py`](../tools/verify_vst3_matrix_archive.py) reads
summary JSON files and the sibling `.tar.gz` files named by them:

```sh
python3 tools/verify_vst3_matrix_archive.py \
  docs/validation/vst3-matrix-20261124.json \
  docs/validation/vst3-matrix-batched-20261124.json \
  docs/validation/vst3-matrix-batch-final-20261124.json
```

It checks bounded, regular-file inputs; compressed and member hashes; safe
member names/types; the frozen inventory and plan; every raw workload/timing
summary; and all 36 repeat-pair findings per archive. It extracts no files and
executes no archived command or binary. JSON output preserves one result per
supplied report, including rejections, and processing continues to later inputs.
Exit `0` requires every report to verify; any rejected report returns `1`
(argument errors return `2`).

Native summaries and the `math.fsum` audit are checked exactly. The auxiliary
historical Python input-order mean can differ across Python versions; those
run IDs are reported separately without weakening the native summary check.
A verification pass establishes internal byte consistency, not acquisition
authenticity, current-source equivalence, a reconstructed binary, new timing
measurements or physical fidelity.

### Historical callback observations

The [A069 report](agents/development-20261123/W4-A069.md) and
[raw matrix with reproduction details](validation/vst3-callback-benchmark-20261123.json)
retain 12 shuffled cells: 48/192 kHz, 64 frames, 73 keys, all three workloads
and both sample formats, with 32 warmup and 300 measured calls per cell. All
3,600 measured calls succeeded and 433 independent accounting/statistics checks
passed. Two calls exceeded their nominal periods; the largest elapsed time was
1873.831 µs. These were one-pass observations on a shared Linux x86_64 container
with a visible Xeon Platinum 8573C and GCC 14.2. They do not isolate a causal
float32/float64 performance difference or establish host deadline compliance.

The [filtered comparison report](agents/continuation-20261123/w1-018.md) and
[retained callback data](validation/filtered-callback-observations-20261123.json)
add 24 paired direct/filtered observations across 32/48/96/192 kHz, 64/128-frame
blocks and the declared held/pedal/gain schedules. All 4,800 measured calls
succeeded, with zero nominal-period misses in that matrix and 1,008 independent
accounting checks. A preliminary ten-call smoke run with one nominal-period
miss is preserved separately in the same record. These shared-container
observations neither isolate scheduling causes nor establish a worst-case bound.
Allocation checks are a separate experiment in
[the expanded host suite](agents/continuation-20261123/w2-023.md): zero intercepted
C++ allocation attempts across 187 processor calls per variant, with calibrated
allocation hooks and explicitly bounded fixtures.

The separate [nonlinear aliasing diagnostic](agents/development-20261123/W4-A068.md)
and [retained observations](validation/aliasing-observations-20261123.json)
characterize selected synthetic renders and filtering limits. They do not
supply an alias-only error bound, an audibility conclusion or full `RT-001`
acceptance.

## Process-only kernel benchmark

`rhodes_benchmark` measures the elapsed wall-clock duration of the current
`ResearchEngine::process()` callback. This is an early runtime fixture using
synthetic development constants, not a calibrated Rhodes model or a completed
`RT-001` validation. It has no third-party dependencies beyond the project's
C++20 core and standard library.

### Build and run

Build an optimized configuration and retain the actual compiler command or build
log alongside any result intended for comparison:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --target rhodes_benchmark
./build/rhodes_benchmark --blocks 1000 --block-size 128 --sample-rate 48000 \
  --active-keys 73 --machine "describe CPU, OS and power policy" > benchmark.json
```

With a Visual Studio multi-configuration generator, use
`build/Release/rhodes_benchmark.exe`. Configuration flags and paths depend on the
chosen generator. The code has portable compiler identifiers for GCC, Clang and
MSVC; a platform is verified only after its own actual build and run. Build
configuration defaults to `unspecified` when the optional
`RHODES_BUILD_CONFIGURATION` compile definition is absent. The reported
`ndebug` flag states whether `NDEBUG` was defined; it does not prove optimization
or capture the complete compiler flags, sanitizer state, architecture, or LTO.

| Option | Accepted values | Default |
|---|---|---|
| `--blocks` | Integer `1..100000` measured callbacks | `1000` |
| `--block-size` | `64`, `128`, `256`, or `512` samples | `128` |
| `--sample-rate` | Integer `32000..192000` Hz | `48000` |
| `--active-keys` | Integer `0..73` | `73` |
| `--machine` | Nonempty UTF-8 label; quote shell spaces/metacharacters | `unknown` |
| `--help` | Print usage and exit | — |

Unknown flags, missing values, malformed numbers, invalid UTF-8 machine labels,
and out-of-range values exit with status 2 and an error on standard error. The
benchmark writes one JSON object to standard output on success; `--help` prints
human-readable text instead. Quotes, backslashes, and JSON control characters in
machine labels are escaped. A label is supplied by the operator, not detected or
verified hardware metadata. Runs at the maximum block count can take substantial
time, especially in unoptimized or instrumented builds.

### Workload and measurement boundary

The engine retains all **73 physical-key slots**, each with three synthetic
modes, and all pickup branches. `--active-keys 0` is a silent baseline with the
same fixed slot traversal. It does not disable the mechanical or electrical
loops, and active-key count must not be interpreted as an independently
allocated voice count or evidence of linear polyphony scaling.

For more than one active key, deterministic integer-spaced key indices span
`0..72`; a single active key uses index 36. The JSON includes the exact indices.
Each receives intensity `0.7` impulses, with keys held throughout the run. This
intensity is a dimensionless excitation surrogate, not measured hammer speed.
All selected keys are retriggered together at block zero and every
`ceil(sample_rate / (2 * block_size))` callbacks, maintaining a nonzero signal
while preserving the engine's accumulated mechanical state. The actual interval
is at least half a second of simulated audio and less than one additional block.

The output buffer and complete timing vector are allocated before measurement.
The fixture prepares the engine, runs 32 unrecorded warmup callbacks with the
same excitation policy, then explicitly resets its mechanical state before the
recorded workload. This reset is benchmark setup, not note-on behavior. Each
measurement places `std::chrono::steady_clock::now()` immediately before and
after `process()`. Note-on operations, output consumption, sorting, and JSON
formatting are outside those timestamps. Successful core processing has no
allocation, locking, or I/O by its present implementation; the fixture is not an
allocation detector or proof of future code's behavior.

Every output sample contributes its square to the reported checksum after its
callback ends, making generated signal externally observable and preventing an
unused render from being optimized away. The checksum and peak must be finite;
an active workload with no nonzero output is rejected. The zero-active baseline
correctly reports a zero checksum and peak. The checksum is a diagnostic
consumption mechanism, not a calibration, audio-quality score, or cross-platform
bit-exactness promise.

### Reading the JSON

The report identifies its schema and synthetic scope, supplied machine label,
compiler/version, build label, language level, sample rate, block count, block
size, warmup count, exact workload and measurement exclusions. Durations are in
microseconds:

| Field under `timing` | Meaning |
|---|---|
| `mean_callback_us` | Arithmetic mean of recorded callback durations |
| `p50_callback_us` | Nearest-rank median of sorted durations |
| `p99_callback_us` | Nearest-rank 99th percentile of sorted durations |
| `max_callback_us` | Largest observed duration in this run |
| `deadline_us` | Nominal block duration: `1e6 * block_size / sample_rate` |
| `deadline_misses` | Count of measured durations strictly greater than that duration |

Nearest rank uses sorted element `ceil(percentile * blocks / 100)`, with
one-based indexing. For one block, every reported quantile equals the maximum;
for fewer than 100 observations, p99 equals the maximum. The report supplies
finite-sample ranks, not confidence intervals for tail latency.
A very short run is suitable for a smoke check, not a credible tail estimate.
Timing includes elapsed scheduling delays and timer overhead; no estimated
overhead is subtracted. The loop runs offline as fast as possible, with no
real-time wake-up schedule or audio device. It measures callback body time, not
start-time jitter, complete host deadline compliance, or CPU utilization.

Microseconds are output units, not a measured timer resolution. Warmup and the
mechanical reset do not restore cache, processor-frequency or thermal state.
Use independent repeated runs and a declared ordering protocol when comparing
configurations, recording competing load and timer characteristics separately.

For a useful comparison, record repository revision and uncommitted changes,
complete build flags, compiler, operating system, CPU, power policy, competing
work, and any thread-affinity or priority setup outside this program. Repeat
under a declared environment and keep each raw JSON result. Compare like
workloads at several block sizes, sample rates, and active-key counts, including
the zero-active baseline. Sanitizer builds serve correctness investigation;
report their instrumentation explicitly when retaining timings. This initial
tool deliberately does not enforce a universal performance pass/fail threshold.

### What these results cannot establish

Observed maxima are finite-sample observations, not a worst-case execution-time
bound. Zero observed misses does not establish a real-time guarantee. No CPU
percentage is inferred from elapsed callback time. None of these measurements
establishes hardware fidelity, a production plug-in's polyphony, end-to-end
latency, host event cost, or performance on an untested platform.

The current synthetic core excludes finite-mass contact, coupled support
mechanics, complex pickup impedance, identified Stage controls, reciprocal
magnetic forces and antialiasing. This process-only boundary also excludes the
host adapter. Rebenchmark when these change. Completing `RT-001` also needs
representative stress conditions,
optimized-versus-reference errors, allocation/locking verification, and
timestep/antialiasing convergence; this harness alone does not satisfy those
gates. See [the roadmap](ROADMAP.md), [architecture](ARCHITECTURE.md), and
[validation specification](VALIDATION.md).
