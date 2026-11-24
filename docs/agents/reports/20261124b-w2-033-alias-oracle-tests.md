# W2-033 — Independent stationary alias oracle regression tests

2026-11-24; actual worker 033, wave 2. Bounded RT-001 test assignment.
**All 34 tests passed both normally and under `python3 -S`, without skips.**
No oracle implementation defect was found within this test scope. The test
module and this report are frozen at the handoff below.

## Changed paths and source binding

Added only [tests/python/test_direct_alias_oracle.py](../../../tests/python/test_direct_alias_oracle.py)
and this report. The test module has 487 lines and SHA-256:

```text
aaa7fd6ba53a6bdec9615ff4dda9ad1102828889507147a8069176d58ff9e1d3
```

The tested [tools/direct_alias_oracle.py](../../../tools/direct_alias_oracle.py)
was unchanged before and after the tests, with SHA-256:

```text
2bfed9f0c3e75fce9fed9a866756f9caa4d45728a7150912790b4caf2a71ff6c
```

Reviewed `AGENTS.md`, `README.md`, the current project-state documentation and
canonical JSON, the runtime roadmap, the [W2-025 diagnostic proposal](20261124b-w2-025-alias-diagnostic-design.md),
the [W2-028 implementation report](20261124b-w2-028-alias-oracle.md), and the
complete oracle module. Root retains the RT-001 claim and owns integration,
Git, canonical state and acceptance decisions. This worker did not claim a
task, edit source, stage, commit, push or change another worker's paths.

## Independent mathematical checks

Expected results use elementary cosine identities, independently evaluated
prescribed displacement/velocity, and a full direct DFT written in the test
module for records of at most 32 samples. The expected-answer helpers do not
call the oracle's harmonic expansion, DFT, self-check, production DSP or the
existing symbolic alias audit. No optional numerical library is imported.

| Coverage | Actual checks |
|---|---|
| One-mode polynomial | First, second and third harmonic complex coefficients from `cos²`/`cos³`; independently differentiated sine waveform; nonzero-phase Faraday sign |
| Combined modal motion | Quadratic cross-term differs from separately converted pickups; complete cubic two-mode signal against independent chain rule and tiny full DFT; coincident-mode phasor reduction; static displacement offset changes AC terms while voltage DC remains absent |
| Signed frequencies | Reversing frequency and phase preserves the motion; differentiation uses the original signed frequency before folding, with an explicit wrong-folded-frequency counterexample |
| Folding and phase | Exact half-open signed bin map across several periods; complex sums before powers; tiny nonzero coefficients retained; equal-voltage `f`/`Fs-f` cancellation for `beta=-alpha`; a pi shift at zero phase reinforces; equal flux amplitudes do not imply equal voltage amplitudes |
| DC and Nyquist | Constant flux differentiates to zero; phase-zero and quarter-cycle endpoint terms combine conjugates exactly once; sampled DC and alternating Nyquist values use no extra doubling; strict ideal-band selection uses original frequencies |
| DFT and Parseval | Negative-exponential sign and `1/N` normalization against a full tiny DFT; real DC/Nyquist terms; aliased requested bins share one signed key; line RMS uses coalesced power; N=2 endpoint case |
| Error accounting | Samplewise signed numerical, alias, total and closure components retain their original positions; no magnitude subtraction, alignment or gain fitting |
| Frozen fixtures | Exact four IDs/rate/count pairs, common duration and bins; expected continuous, retained and removed line sets; 48-kHz one-mode alias sample peak/RMS independently derived from the two removed sines; both 96-kHz alias arrays exactly zero |
| Evidence boundaries | Omitting primitive samples never claims primitive comparison; nonzero alias magnitude has no acceptance gate; plans retain unchanged historical acceptance and independent numerical budget names |
| Thresholds | Separate inclusive peak and RMS limits, plus `math.nextafter` values immediately above each; a synthetic harmonic discrepancy at and immediately above the bookkeeping limit |
| Invalid input | Nonfinite, boolean, incoherent, mutable, oversized, malformed and overflow inputs rejected; nonreal spectra rejected; mismatched arrays rejected; missing/conflicting CLI operations rejected |

The threshold tests use an analytically zero synthetic fixture, making the
peak/RMS boundary values independently controllable. The bookkeeping test injects
a known mismatch into the returned analytic arrays to test report failure policy.
These are deliberately synthetic tests, not primitive output or physical data.

The CLI tests start `--plan` and `--help` in separate temporary directories under
`-S`, with explicit import guards for NumPy, SciPy, the symbolic alias audit and
production engine module names. Both exit successfully, produce no files in the
temporary directory and return the declared plan/help. This does not test the
separate driver's process, memory, file-retention or provenance enforcement.

## Commands and observed results

Initial read-only workflow checks:

```sh
python3 tools/project.py validate
python3 tools/project.py sync --check
python3 tools/project.py next --json
```

All succeeded. State revision was 32, containing 14 tasks and 26 evidence records;
fidelity remained `unvalidated`. No task was available for an independent claim.

Focused normal suite:

```sh
python3 -m unittest discover -s tests/python -p test_direct_alias_oracle.py -v
```

Observed result: **34 tests, 0.329 seconds, OK**, no failures or skips.

Dependency-free suite:

```sh
python3 -S -m unittest discover -s tests/python -p test_direct_alias_oracle.py -v
```

Observed result: **34 tests, 0.308 seconds, OK**, no failures or skips.
These durations are unittest execution observations, not realtime measurements.
Both focused runs passed on their first execution; no production-code correction
or tolerance relaxation was needed. The recorded hashes were calculated from
complete file bytes after both runs.

Documentation check after writing this report:

```sh
python3 tools/check_docs.py
```

The check passed. No full Python/native matrix, native producer run, full
four-case retained experiment, new benchmark or external CI run was performed
by this worker.

## Handoff and limitations

The test module is discoverable by the existing Python unittest suite. Root may
integrate these two files with the frozen oracle and the separately owned driver
tests. No source patch is required by the findings here.

The tests cover a bounded stationary undamped algebra diagnostic at one pickup's
synthetic voltage boundary. They do not establish damped/transient behavior,
whole-engine direct convergence, filtered-path accuracy, C03, native primitive
accuracy, measured instrument fidelity, audibility or realtime deadlines.
PHY-002 and RT-001 acceptance remain the integrator's separate open criteria.
