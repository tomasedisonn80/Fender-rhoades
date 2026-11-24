# HOST-MIDI verification and evidence access

This guide separates testing a build from inspecting retained HOST-002 evidence.
The [MIDI contract](specs/HOST_MIDI_20261124.md) defines the behavior;
[DAW validation](DAW_VALIDATION.md) remains a separate manual protocol.

## Test the implementation

Start at the repository root. Follow [development prerequisites](DEVELOPMENT.md)
and [VST3 setup](VST3.md#build-the-optional-adapter): C++20, CMake 3.25+,
Python 3.11+, and the pinned SDK with its four required submodules. The complete
test build needs `std::barrier`/`std::jthread` support. For Linux/macOS with the
SDK in `../vst3sdk`, use a fresh build directory:

```sh
cmake -S . -B build/host-midi -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release \
  -DRHODES_BUILD_VST3=ON -DBUILD_TESTING=ON \
  -DRHODES_VST3_SDK_ROOT="${PWD}/../vst3sdk"
cmake --build build/host-midi --config Release --parallel 2
ctest --test-dir build/host-midi -C Release --output-on-failure --no-tests=error \
  -R '^(control_text\.|events\.release_port$|vst3\.)'
python3 -B tools/generate_control_text_vectors.py --check
```

Use the Windows generator instructions in [VST3 setup](VST3.md); substitute
`python` where appropriate and put wrapped commands on one PowerShell line.
These are instructions, not new platform results.

The selection includes the six `control_text.*` cases, `events.release_port`,
and the new `vst3.channel_commands`, `command_partition`, `command_bounds`,
`command_atomicity`, `pressure`, and `control_text` suites with their
`filtered_`/`quadrature_` counterparts. It also runs existing host regressions
and validators. [Core](../CMakeLists.txt) and
[adapter registration](../cmake/Vst3.cmake) identify each target;
most adapter suites compile processor code into their executables.
[vst3.loaded_midi](../tests/cpp/vst3_loaded_midi_test.cpp) instead reaches all
three instruments through actual module factories and SDK-only host linkage.
Inspect skips and failures; an empty selection is an error.

The [vector checker](../tools/generate_control_text_vectors.py) uses Python's
standard library and IEEE binary64 floats to compare regenerated expectations
with both committed fixtures without writing. Its raw-parser oracle is separate
from [controller](../plugins/vst3/controller.cpp) range/unit handling. Relevant
implementation also includes [control_text.hpp](../include/rhodes/control_text.hpp)
and [processor.cpp](../plugins/vst3/processor.cpp).

## Identify the frozen observations

The [pre-change baseline](validation/host-midi-baseline-20261124.md) is
`20a77e8b2033d435839df594c783837befb07938`; the W2 captures declare current source
`a06d2cd60bc53e3939e89f64254aed7ed5586e7c`. These are historical contexts,
not aliases for today's checkout.

| Retained artifact | Experiment and boundary |
| --- | --- |
| [W033 loaded compatibility](validation/host-midi-loaded-compatibility-20261124.json) | 72 baseline/current pairs, 144 loaded processes; 304,128 paired stereo frames and 2,952 state checkpoints matched. Legacy valid-input coverage; parameter count intentionally changed 2→18. |
| [W036 WAV replay](validation/host-midi-legacy-wav-20261124.json) | 24 direct/filtered WAVs, 44,160 frames, compared with frozen fingerprints. Historical WAV baseline is separately `5aac654f8569aaa782e7bce58e82b3213b57da23`; original historical WAVs are absent. No Q6 study rerun. |
| [W038 command cost](validation/host-midi-command-cost-20261124.json) | 360 timed callbacks in freshly compiled adapters linked to static core/SDK archives; no shared-bundle loading. Linux/GCC, 48 kHz, 64 frames, binary64, shared scheduling. |

Read each artifact's source/worktree, build-input and binary hashes together.
W033 binds directly to recorded `.so` bytes; baseline build provenance was
integrator-supplied. W036 used an existing renderer, without rebuilding; its
source context records later test/documentation/CI changes and CMake test
registration. W038 records its compiled sources and linked archives. A commit
label or unchanged worktree hash does not independently prove which source
produced a binary, and a fresh build need not reproduce historical binary bytes.

None of these captures establishes physical fidelity, named-DAW delivery,
portability of changed source, WCRT, or host/device deadlines. Observed timing
maxima are descriptive; see [benchmark methodology](BENCHMARKING.md).

## Verify and inspect retained bytes

Only Python's standard library is needed:

```sh
python3 -B tools/verify_host_midi_archive.py
```

The [verifier](../tools/verify_host_midi_archive.py) checks bounded archive
structure, hashes, sizes, inventory consistency and referenced report bytes
against the [manifest](validation/host-midi-raw-20261124-manifest.json).
Exit 0 reports `verified`; exit 1 reports `rejected`. It extracts nothing,
executes no archived command and does not recompute experiment claims.
Hashes establish byte consistency, not execution authenticity or a reproducible
build. Use `--root /path/to/checkout` to select another complete checkout.

The [archive](validation/host-midi-raw-20261124.tar.gz) maps 612 original paths
to 191 deduplicated `blobs/sha256/<digest>` members. It retains workers
033/036/038/039 outputs, sources and logs; binaries, libraries and build trees
are excluded. After verification, this read-only example inspects one original
stdout path by streaming its member, without extraction or code execution:

```sh
python3 -B - <<'PY'
import hashlib, json, tarfile
from pathlib import Path

base = Path("docs/validation")
manifest = json.loads((base / "host-midi-raw-20261124-manifest.json").read_text())
original = ".agent-work/worker036/legacy-wav-replay/wav/direct-r192000-k00.stdout.txt"
entry = next(e for e in manifest["entries"] if e["original_path"] == original)
if (entry["archive_member"] != "blobs/sha256/" + entry["sha256"]
        or not 0 <= entry["size_bytes"] <= 4096):
    raise ValueError("Unexpected text entry")
with tarfile.open(base / "host-midi-raw-20261124.tar.gz", "r|gz") as archive:
    for member in archive:
        if member.name != entry["archive_member"]:
            continue
        if not member.isfile() or member.size != entry["size_bytes"]:
            raise ValueError("Unexpected member type or size")
        stream = archive.extractfile(member)  # Returns a reader; writes no file.
        if stream is None:
            raise ValueError("Missing member reader")
        with stream:
            raw = stream.read(4097)
        if (len(raw) != entry["size_bytes"]
                or hashlib.sha256(raw).hexdigest() != entry["sha256"]):
            raise ValueError("Member integrity mismatch")
        print(original, len(raw), entry["sha256"])
        print(repr(raw.decode("utf-8")))
        break
    else:
        raise ValueError("Member absent")
PY
```

## Reproduce an experiment deliberately

A new experiment needs the matching source snapshots, SDK pins, compiler/flags,
complete bundles or renderer, static archives, fixtures and repository helpers
listed in its artifact. The archive alone is insufficient. Inspect retained
driver/harness source as text first; embedded `restore` instructions are evidence,
not commands to execute automatically. Original drivers are
`.agent-work/worker033/run_comparison.py`,
`.agent-work/worker036/replay_legacy_wav.py`, and
`.agent-work/worker038/run_probe.py`. Review and adapt absolute SDK paths,
baseline/current bundle locations, build/archive paths and output destinations
in a separate working copy. Retain the adaptation and new hashes/logs; never
overwrite frozen evidence. Preserve the original workload and comparison rule.
New timing samples describe their new environment; reconstructing missing
historical WAVs cannot turn fingerprint comparison into original-file comparison.
