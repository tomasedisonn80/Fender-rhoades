# W4-68 — Local bundle and packaging audit

**Agent:** `/root/w4_68_bundle_audit`  
**Decision:** no material defect found in the inspected local bundles or their
declared packaging workflow.

## Observed scope

Read-only inspection covered `build/vst3/VST3/Release` and the workflow/CMake
packaging sources. Observed HEAD was `9f1b409f8dc5ab24997dee335773390556f84e49`;
the hashes below identify the inspected bytes, not a claim that these binaries
were freshly built at that HEAD.

Each bundle contains four directories and four nonempty regular files: twelve
directories and twelve files overall, with no symlinks or special files.
Directories and modules have mode `0755`; resources have `0644`. All three
modules have ELF64, little-endian, x86-64 headers. License bytes equal the pinned
SDK's `LICENSE.txt`; notices equal the repository source. That SDK notice does
not select a license for the project's original code.

Metadata names, versions and six distinct processor/controller IDs match
`plugin_ids.hpp`; every processor declares `Instrument`, `Synth`, `Stereo`.
The SDK-generated metadata contains trailing commas. I inspected its fields
without treating it as strict JSON and relied separately on retained SDK results.

The workflow requires complete nonsymlink bundle roots, modules and required
resources. Its PAX tar preserves link text and modes, then compares inventory,
regular-file hashes, modes and symlink text before upload. Interior symlink
handling was reviewed in source, not exercised by these link-free bundles;
the code assumes trusted generated contents and does not constrain link targets.

All ten retained native-log archive members match their indexed hashes. At
`015767f`, each OS log records all six SDK/metadata validators passing and complete
archive creation: Linux 24 entries, Windows 30, macOS 27. Names contain the
correct OS/architecture and full tested commit SHA. The current packaging
workflow is byte-identical to that revision. Remote bundle archives were not
downloaded; local module hashes below are **not remote artifact identities**.

## Checks and handoff

`project.py validate`, `sync --check` and `next --json` passed at revision 27,
with 22 evidence records. Python `pathlib`/`lstat`/`hashlib`/`struct` inspection,
metadata-field assertions, `tarfile` member reads and read-only `git show`
provided the observations above. Examined bytes remained unchanged.

No build, SDK validator rerun, archive creation/extraction, installation or DAW launch
occurred. Next distribution work needs explicit artifact acquisition/verification,
installation, deployment-baseline and signing/notarization decisions, followed
by native host evaluation. No release or measured-fidelity conclusion follows.

## Exact local identities

`D`, `F`, `Q` mean `RhodesResearch`, `RhodesResearchFiltered`, and
`RhodesResearchQuadrature`. Module paths are
`<name>.vst3/Contents/x86_64-linux/<name>.so`; resources are under each bundle's
`Contents/Resources`.

| File | Bytes | SHA-256 |
| --- | ---: | --- |
| D module | 334984 | `7e1ef2adbe48dbcfd567cb19fae66620b8c6b84e64e5fb232f29b5ad5952dfc9` |
| F module | 343176 | `caa527c269192210e3a080427ff1c92b3da2a547f9be1eaf1dd33d26720babc0` |
| Q module | 339080 | `efe89d4d9241115222d4661362d33011c9d3484392d439ca40c0bf536f49e20d` |
| D `moduleinfo.json` | 1083 | `992e019340507a84f7ba9c2f8928fcdaf466f917fc2fbff90912bfe7e1337d17` |
| F `moduleinfo.json` | 1090 | `6dff172b67ffa2811b3037640bfb692a819e092053c4a0812b7ae639f04fb6d2` |
| Q `moduleinfo.json` | 1096 | `67290a16b1d2982a867acfefacd1816fb2bbb99438a33ff8f5f1d74c4831ab1d` |
| Every `LICENSE.txt` | 1094 | `e86d79e19e4a33ebc442d2e0d083887dfc75a2a8de9fe17eeddedb84810ad74e` |
| Every `THIRD_PARTY_NOTICES.md` | 2089 | `171196ff6346500d42093d0aa0729d960944fc607cb83de415a92f1ee1a51570` |

| Supporting source/evidence | SHA-256 |
| --- | --- |
| `.github/workflows/vst3.yml` | `6cf2593353d05a6b122c99736399132b4c813ec8fba5af03b39ed4d5fdd8e3f3` |
| `cmake/Vst3.cmake` | `e779743e238c5b7ac2632d73764eb19974fcef3f4ae5c8699ac6f188fde783a6` |
| `plugins/vst3/plugin_ids.hpp` | `1b5744731384e100c2f32cfed42fccf9fe2168d39df5cc29f1ac1108b9d94a81` |
| [Native evidence JSON](../../validation/native-platforms-20261124.json) | `3b28ae2ee831786e1f69c8a340f07eb888e6be23743a155f59a014621dbc5f7c` |
| [Native log archive](../../validation/native-platforms-20261124.tar.gz) | `99655044dd9815379b892f3fddd0e9f5e37496cff794bbbd2bf9bd205b091694` |
