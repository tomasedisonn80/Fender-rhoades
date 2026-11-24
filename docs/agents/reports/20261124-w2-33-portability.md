# W2-33 — Contact peak CLI portability review

Date: 2026-11-24. Assignment: 33/100, bounded review under
`/root/w2_contact_implementation`. **PHY-002 remains incomplete.**

## Scope and result

Added three focused regressions in
[`test_contact_peak_portability.py`](../../../tests/python/test_contact_peak_portability.py).
All three passed on the actual Linux host. The Windows findings below are static
source review, not a native Windows run. No new blocking implementation defect
was reproduced. The existing `ensure_ascii=True` correction successfully retains
POSIX surrogate-escaped paths through the complete CLI attempt.

The only assigned files changed are the new test file and this report. No
diagnostic implementation, old-auditor, production, canonical-state, or CI files
were edited; no Git operations were performed. No supplied producer binary was
launched. Tests use temporary directories and the existing
schema `payload`/`encode` helpers; the forced-clamp transport fixture explicitly
sets its initial and reported maximum hammer compression to `1e-4 m`. These
constructed payloads establish transport behavior, not physical trajectories,
continuous-extrema certification, or fidelity.

## Executed verification

Actual environment: CPython 3.12.14, Linux 6.18.57 x86_64, glibc 2.41.

```sh
python3 -m unittest discover -s tests/python -p 'test_contact_peak_portability.py' -v
```

Result: **3 tests passed, 0 skipped, 0 failures, 0 errors**; unittest reported
`Ran 3 tests in 0.288s`. The command exited successfully.

| New regression | Observed behavior |
| --- | --- |
| Help with optional imports explicitly blocked | A child Python process with `-S` and an import guard rejecting NumPy/SciPy reached user-visible `--help`, exited 0, listed all four public options, and created no evidence files. |
| Unicode and space paths | The complete CLI accepted Unicode/spaced candidate, output, and declared-producer paths from an unrelated working directory. With `PYTHONIOENCODING=ascii:strict`, stdout and retained JSON remained ASCII while parsed path strings exactly matched their inputs. |
| POSIX surrogate-escaped paths | Candidate/output/producer names containing undecodable filename bytes survived argument transport, output creation, ASCII JSON retention, and path decoding. The retained candidate and producer strings round-tripped through `os.fsencode`. This case explicitly skips on non-POSIX hosts. |

Both path tests ran the CLI with `-S`, verified exact raw candidate bytes including
CR/LF, verified candidate and plan SHA-256 values, and checked a declared-producer
byte blob containing every byte value across the 64 KiB hashing boundary. The
blob includes Ctrl-Z, CR/LF, and non-text bytes. It was only hashed. Successful
final manifests and absence of sibling temporary manifests exercised the
replacement path; operational errors remained empty and acceptance stayed
`incomplete`.

The existing schema import, oracle `-S` missing-backend, exclusive-output-refusal,
and blocked-worker timeout regressions were inspected but **not rerun or
duplicated**. No C++ producer, full test suite, native Windows/macOS check,
installed-SciPy timing run, or remote Actions run was performed. Temporary test
artifacts were removed by `TemporaryDirectory` cleanup.

## Static Windows and optional-backend review

| Surface | Source finding and limit |
| --- | --- |
| Python CLI paths | `argparse` uses `Path`; candidate and producer paths remain strings in evidence. Internal child launches use argument lists without a shell. The new Unicode test is portable to native Windows, but its passing Linux result does not verify Windows path behavior, UNC paths, drive-relative paths, long paths, or filesystem normalization. |
| Binary hashing | `_regular_reader` combines `os.O_RDONLY` with `getattr(os, "O_BINARY", 0)` and returns an `rb` stream. Raw candidate storage uses `xb`; SHA-256 receives bytes. The Windows binary-mode intent is explicit, avoiding text newline/Ctrl-Z conversion. Native Windows file-identity and filesystem behavior remain unexecuted. |
| Exclusive creation and manifest replacement | `output.mkdir(exist_ok=False)` reserves the attempt directory; individual evidence files use `xb`. `_replace_manifest` creates a sibling temporary file, writes and flushes it, calls `fsync`, closes its descriptor, and then calls `os.replace`. Closing the temporary file before replacement avoids relying on POSIX replacement of an open source handle. This review does not establish Windows sharing-mode, antivirus, network-filesystem, or power-loss behavior. |
| Optional imports | Schema/oracle imports reach only standard-library modules and the fixture definitions. SciPy import is delayed to `_time_worker`; CLI version reporting uses distribution metadata rather than importing NumPy/SciPy. The new CLI help check complements the pre-existing schema and value-only oracle checks. |
| Worker deadline | `_run_damped_time` launches this module's own Python worker with `subprocess.run(..., timeout=WALL_SECONDS)` and no shell; `WALL_SECONDS` is 10.0. `TimeoutExpired` produces incomplete timing with termination metadata while independent values remain available. This is portable API use by inspection; no new timeout experiment was run. The existing real blocked-worker regression is in `test_contact_peak_oracles.py`. |
| Worker text transport | Worker success JSON uses the default ASCII escaping. Parent `text=True` uses the host text encoding; malformed/undecodable worker output is caught as incomplete evidence. This is not a claim that all third-party stderr is rendered identically across Windows code pages. |
| C++ command-line boundary | `docs/DEVELOPMENT.md` already states that C++ command-line arguments are narrow and arbitrary Unicode Windows paths remain unvalidated. The Python diagnostic path test does not change that separate producer boundary. |

## Windows shell stdout encoding documentation gap

[`docs/CONTACT_REFERENCE.md`](../../CONTACT_REFERENCE.md) explains native JSON
stdout and the Visual Studio executable location, but does not warn that Windows
shell capture can alter its encoding. The strict peak parser decodes raw bytes as
UTF-8 before `json.loads`; UTF-16LE output and an added UTF-8 BOM do not meet this
parser contract. This is a static inference from `parse_candidate`, not a native
PowerShell experiment.

Microsoft documents that Windows PowerShell 5.1 `Out-File`, `>`, and `>>` use
UTF-16LE, while its `UTF8` encoding choice includes a BOM. Therefore suggesting
`Out-File -Encoding utf8` is not sufficient for this strict candidate parser.
See [Microsoft: character encoding in Windows PowerShell](https://learn.microsoft.com/en-us/powershell/module/microsoft.powershell.core/about/about_character_encoding?view=powershell-7.4).

Microsoft also documents that PowerShell 7.4 and later preserve native stdout
bytes when redirecting to a file, with a separate caveat that combining stderr
with stdout changes the streams to string data. See
[Microsoft: native-command redirection](https://learn.microsoft.com/en-us/powershell/module/microsoft.powershell.core/about/about_redirection?view=powershell-7).
Both official pages were consulted on 2026-11-24; no native shell behavior was
executed for this assignment.

Actionable documentation handoff: specify BOM-free UTF-8 candidate bytes, retain
stderr separately, and recommend byte-preserving capture through the existing
Python sweep runner or a Python subprocess opened with binary output files.
Any PowerShell direct-redirection example should identify its version requirement.
Do not weaken strict parsing or rewrite original candidate bytes to accommodate
an accidental shell transcode. This finding was sent to the parent integrator;
source documentation remains outside this worker's edit scope.

## Actual configured CI coverage

The inspected `.github/workflows/ci.yml` has a `contracts` matrix for
`ubuntu-latest`, `windows-latest`, and `macos-latest`, with `actions/setup-python`
pinned to **Python 3.11** and `python -m unittest discover -s tests/python -v`.
Its separate C++ matrix uses the same three OS labels; sanitizers use Ubuntu
only. The workflow does not install optional NumPy/SciPy packages. This is CI
configuration, not evidence that these jobs have run or passed for the reviewed
source. This assignment ran Python 3.12.14 on Linux, not the configured 3.11
Windows/macOS matrix. No CI edits are proposed by this bounded review.

## Source identities

SHA-256 values read immediately after the passing test command at
`2026-11-24T05:41:29.084257+00:00`. The successful CLI attempts also enforce their
own source-before/source-after identity checks. The report does not bind future
concurrent edits to these results.

| Path | SHA-256 |
| --- | --- |
| `tools/validate_contact_peaks.py` | `ba25e476be93aecc1ccdbedfa020f457369bdad62c0ce16d5f749f79fc42e3ef` |
| `tools/contact_peak_schema.py` | `af5cbf87d155ecb40c176b78a7527b2a7bd686c7fd515be621af93d910db86c4` |
| `tools/contact_peak_oracles.py` | `74030851ce9511cd50c6952a11b43bfdf5e8da5d7fd10428c009be0210060789` |
| `tools/validate_contact_reference.py` | `c910da5da6b6ad957b709a414cef39a08ed6e8b75c6e36c9d73ef535a064dcdc` |
| `tests/python/test_contact_peak_portability.py` | `df60f3cb649d19a5d16f6a63aedb301b7b3001e680b6023f08af20f2efaa6a32` |
| `tests/python/test_contact_peak_schema.py` | `8556d104c5492257c2be33e022040c66429dd447b832ea116d51dd571ac2edec` |
| `tests/python/test_contact_peak_oracles.py` | `9de8d7907b7a68fccf77788d9f04c93d4059d343bb4e6b1bacdc1b26bc2be1e6` |
| `docs/specs/CONTACT_ANALYTIC_PEAK_SLICE_20261123.md` | `56e5caefaebc03596b2ed566d50578b2be2e4dc54e37bf7fdce31dab31243f03` |
| `docs/CONTACT_REFERENCE.md` | `8b5595a58ecfa1b0ceac1868f29757b17522ee5b7ca29faade394fb63d260580` |
| `docs/DEVELOPMENT.md` | `f8ab34e85dd7f90dc0ac3fc52bf57f07491cb166a39d2d512afd1e14c59b6406` |
| `.github/workflows/ci.yml` | `a1b203c3d6df6b56fbf7cbe3759865e7ffaff10a0a22d256235d40e3ba461a3e` |

Next action: integrator review and registration of this new report, source-doc
clarification for Windows capture, and actual configured native CI execution
before any Windows/macOS coverage claim. No task completion, fidelity promotion,
or amendment to the frozen contact acceptance follows from these transport tests.
