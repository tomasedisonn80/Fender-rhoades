# Retained native photo-editor pixels

These archives retain actual Linux/X11 captures from the continuation campaign.
They are bound to the original run inputs and are not screenshots of every later
source revision. The manifests retain original file hashes and the source-bound
outcomes; failed observations remain failed.

| Archive | Observation | PNG images | Scope |
| --- | --- | ---: | --- |
| `native-replay-pixels.zip` | Worker 022 runs 01 and 02 | 131 | Before the visible-contour correction: respectively 96 passes / 36 failures / 1 incomplete and 127 passes / 9 failures. |
| `passing-native-pixels.zip` | Worker 036 run 01 | 67 | After the contour and help-text corrections: 136 passes, including 43 held white keys and exact full-size idle reference comparisons. |
| `final-native-pixels.zip` | Worker 061 run 01 | 67 | After the native lifecycle and X11 graphics repairs: 136 passes, actual host exit zero, exact full-size idle reference pixels and unchanged black-key interiors. |

The adjacent manifests are copies of each archive's internal manifest. Worker
029 and worker 046 performed original-to-reconstruction comparisons. The latter
audit's `roundtrip-summary.json` and `roundtrip-audit.json` are retained here.
Worker 066 independently archived the fresh final run with 134 raw records and
source/configuration files; `final-roundtrip-summary.json` records its exact RGB
and raw-byte comparison. `delivery-manifest.json` maps all 16 original producer
entries to their current repository-relative paths, byte counts and SHA-256 hashes.
`final-package-audit/producer-delivery-manifest.json` preserves worker 066's
producer manifest byte for byte; its hash is also retained in the current index.
`final-package-audit/roundtrip-audit.json` is worker 066's detailed audit, alongside
the eleven command, process, selection and input records formerly missing from
the delivery. The root-level `roundtrip-summary.json` and `roundtrip-audit.json`
remain the earlier worker 046 observations; the final summary remains
`final-roundtrip-summary.json`.

Absolute scratch paths in the producer records identify historical work locations;
they are not dependencies for locating the delivered files or unpacking the
archive. The historical commands describe their original environment, and the
archive is not a complete dependency checkout. The companion `VSTGUI-LICENSE.txt`
covers the retained upstream VSTGUI source and generated adaptations in that archive.

From the repository root, with Python and Pillow installed:

```sh
python3 tools/pack_photo_evidence.py verify \
  docs/validation/photo-continuation-20261124/captures/passing-native-pixels.zip
mkdir -p build
python3 tools/pack_photo_evidence.py unpack \
  docs/validation/photo-continuation-20261124/captures/passing-native-pixels.zip \
  build/passing-native-pixels
```

Use `--help` for the pack command and additional options. Extraction requires a
fresh output location and rejects path traversal or overwriting existing files.
Reconstruction preserves image dimensions and decoded RGB pixels exactly. It
does **not** reconstruct the original encoded PNG bytes, PNG metadata or original
image mode. Raw non-image records are preserved byte for byte. Neither archival
verification nor image matching establishes audio fidelity or named-DAW behavior.
