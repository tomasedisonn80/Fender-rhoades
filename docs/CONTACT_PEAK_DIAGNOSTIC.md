# Offline four-fixture contact peak diagnostic

[`validate_contact_peaks.py`](../tools/validate_contact_peaks.py) compares native
reported compression and force maxima with independent references for exactly
`conservative_impact`, `damped_impact`, `forced_clamp`, and
`preload_equilibrium`. It follows the
[frozen analytic slice](specs/CONTACT_ANALYTIC_PEAK_SLICE_20261123.md).
**PHY-002 remains incomplete**, including after exit 0. These synthetic fixtures
do not establish hardware fidelity or certify arbitrary continuous trajectories.

## Generate native input separately

Build the native producer using [Development](DEVELOPMENT.md), then locate its
executable as described in [Run and inspect fixtures](CONTACT_REFERENCE.md#run-and-inspect-fixtures).
The native [C++ header](../include/rhodes/contact_reference.hpp) and
[peak-candidate contract](specs/CONTACT_PEAK_DIAGNOSTICS_20261123.md) describe the
producer API and its uncertified sign-change search. Enable
`--locate-peak-candidates`; the default producer output omits the required peaks.
Each candidate file must contain one complete native JSON run.

Capture stdout as bytes, with stderr separate. The parser expects BOM-free UTF-8;
Windows shell text redirection can transcode stdout. The following Python example
works without relying on PowerShell redirection semantics. Replace `producer`
with the executable from your build and run the snippet from the repository root:

```python
from pathlib import Path
import subprocess

producer = Path(r"REPLACE_WITH_NATIVE_EXECUTABLE").resolve()
inputs = Path("contact-peak-inputs")
inputs.mkdir(exist_ok=False)
for fixture in (
    "conservative_impact", "damped_impact", "forced_clamp", "preload_equilibrium"
):
    run = subprocess.run(
        [str(producer), "--fixture", fixture, "--locate-peak-candidates"],
        capture_output=True, check=False,
    )
    (inputs / f"{fixture}.json").write_bytes(run.stdout)
    (inputs / f"{fixture}.stderr").write_bytes(run.stderr)
    print(fixture, run.returncode)
```

Inspect the producer exit codes and stderr; the example preserves output even
when generation fails. The validator records a supplied native failed run as a
failure. Byte capture does not change the native Windows argument limitations in
[Development](DEVELOPMENT.md); the [portability review](agents/reports/20261124-w2-33-portability.md)
separates Linux observations from unexecuted Windows behavior.

## Validate retained files

Use Python 3.11 or later (`python3` may be the appropriate launcher on your host).
From the repository root:

```sh
python tools/validate_contact_peaks.py --help
python tools/validate_contact_peaks.py --candidate contact-peak-inputs/conservative_impact.json --output peak-run-001
```

Repeat `--candidate PATH` for each input, up to 64 files in one invocation. For
example, after substituting your captured filenames:

```sh
python tools/validate_contact_peaks.py --candidate first.json --candidate second.json --output peak-run-002
```

`--output` must name a **new directory whose parent already exists**. The CLI
creates it exclusively and refuses an existing directory; choose a new name for
each attempt. There is no overwrite or retry flag.

Add `--producer-binary PATH` to record an optional declared producer file's
SHA-256. **The validator only hashes that file; it never executes it.** This hash
does not attest that the file produced the candidates. Input generation above is
a separate operation.

### Default checks and optional damped timing

Help, schema parsing, value references, and available analytic timing checks use
the Python standard library. The native C++ reference also has no NumPy/SciPy
dependency. Add `--with-damped-time` to request the bounded SciPy timing reference
for damped hammer compression and force. See the public
[optional Python dependency notes](DEPENDENCIES.md#optional-independent-python-audit)
for package provenance and previously observed versions; those notes also cover
the separate general auditor, whose numerical dependency requirements differ.

Without the flag, unresolved damped timing is retained separately and does not
prevent a value-only exit 0 when the other required checks pass. With the flag,
missing SciPy yields `not_available`; failed convergence or exhausted timing
limits yields `inconclusive_reference`. Either makes requested timing incomplete.
Preload's positive maximizing times remain `unresolved_near_equilibrium` in both
modes and do not by themselves block exit 0. Read the separate value, time,
observation-consistency, and coverage fields even when the process succeeds.
Supplemental timing checks do not amend frozen acceptance.

## Interpret the result

| Exit | Meaning |
| --- | --- |
| `0` | All requested comparisons pass, including validity, observation consistency, values, and required timing. |
| `1` | Definite candidate/diagnostic failure, including malformed input, producer failure, or a byte-limit violation. |
| `2` | Missing peak diagnostics, unsupported fixture, or unavailable/inconclusive requested reference evidence. Command-line argument errors also use argparse's exit 2. |
| `3` | Operational failure, such as unreadable input, output creation/write failure, producer-hash failure, or changed diagnostic sources. |

Across attempted cases the precedence is **`3 > 1 > 2 > 0`**. A definite failure
therefore survives other incomplete comparisons. Inspect stderr and the retained
manifest to distinguish invocation errors from completed diagnostic attempts.

Zero has distinct meanings. The dampers are disabled in the first three fixtures,
so their zero records retain `kind=disabled`. The enabled forced-clamp hammer has
a zero-force plateau over the whole duration, while its compression maximum is
at time zero. The preload hammer is enabled but separated, with zero plateaus.
Preload's positive damper maxima are verified **value enclosures** for the rounded
initial state, not exact positive plateaus or located maximizing times. The
reference retains interval bounds and these distinctions explicitly.

## Evidence and incomplete writes

The output directory retains `plan.json`, `manifest.json`, numbered candidate
`.raw` and `.record.json` files, comparisons when available, and independent
`reference-<fixture>.json` files when computed. Evidence includes input bytes and
hashes, solver settings, independent fixture definitions and their hashes,
diagnostic source identities before/after the attempt, and Python/optional package
versions. Candidate names and initial-state checks do not authenticate arbitrary
input bytes: the native envelope does not include the full physical configuration.

Limits are 32 MiB per candidate and 128 MiB across the collection. An oversized
input retains only its bounded prefix, marked incomplete; its retained-prefix
hash is not a whole-input hash. A failed raw write records the partial artifact's
size/hash when readable, otherwise leaves them unknown. Bytes already read remain
charged to the collection limit even if retention fails. Later storage failures
can leave partial files or an earlier manifest; final-manifest failure returns 3
and reports to stderr. Inspect case errors, `raw_complete`, and unattempted inputs;
the existence of an output directory is not a completed result. The CLI does not
silently overwrite, omit failures, or retry an attempt.

The existing [general contact audit](CONTACT_REFERENCE.md#generate-and-independently-audit-a-sweep)
and [frozen acceptance](specs/CONTACT_ACCEPTANCE_20261123.md) retain their separate
scope and open gates.
