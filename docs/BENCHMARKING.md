# Synthetic kernel benchmarking

`rhodes_benchmark` measures the elapsed wall-clock duration of the current
`ResearchEngine::process()` callback. This is an early runtime fixture using
synthetic development constants, not a calibrated Rhodes model or a completed
`RT-001` validation. It has no third-party dependencies beyond the project's
C++20 core and standard library.

## Build and run

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

## Workload and measurement boundary

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

## Reading the JSON

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
one-based indexing. For one block, every reported quantile equals the maximum.
A very short run is suitable for a smoke check, not a credible tail estimate.
Timing includes elapsed scheduling delays and timer overhead; no estimated
overhead is subtracted. The loop runs offline as fast as possible, with no
real-time wake-up schedule or audio device. It measures callback body time, not
start-time jitter, complete host deadline compliance, or CPU utilization.

For a useful comparison, record repository revision and uncommitted changes,
complete build flags, compiler, operating system, CPU, power policy, competing
work, and any thread-affinity or priority setup outside this program. Repeat
under a declared environment and keep each raw JSON result. Compare like
workloads at several block sizes, sample rates, and active-key counts, including
the zero-active baseline. Sanitizer builds serve correctness investigation;
report their instrumentation explicitly when retaining timings. This initial
tool deliberately does not enforce a universal performance pass/fail threshold.

## What these results cannot establish

Observed maxima are finite-sample observations, not a worst-case execution-time
bound. Zero observed misses does not establish a real-time guarantee. No CPU
percentage is inferred from elapsed callback time. None of these measurements
establishes hardware fidelity, a production plug-in's polyphony, end-to-end
latency, host event cost, or performance on an untested platform.

The current synthetic core excludes finite-mass contact, coupled support
mechanics, complex pickup impedance, identified Stage controls, reciprocal
magnetic forces, antialiasing, and host integration. Rebenchmark when these
change. Completing `RT-001` also needs representative stress conditions,
optimized-versus-reference errors, allocation/locking verification, and
timestep/antialiasing convergence; this harness alone does not satisfy those
gates. See [the roadmap](ROADMAP.md), [architecture](ARCHITECTURE.md), and
[validation specification](VALIDATION.md).
