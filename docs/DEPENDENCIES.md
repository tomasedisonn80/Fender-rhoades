# Dependency provenance

## Default research core

The default core has no third-party runtime library dependencies and downloads
no packages during its build. Its build requirements remain a C++20 compiler
and CMake 3.24 or later; repository tooling uses Python 3.11 or later. See
[development instructions](DEVELOPMENT.md) and the [root build](../CMakeLists.txt).

No third-party source is vendored into this repository by the optional SDK integration
below. The project-wide software license remains an owner decision; recording
the SDK's license does not select a license for this project's original code.
See [rights and attribution](../RIGHTS.md).

## Optional official VST3 SDK

The experimental VST3 adapter uses a separately supplied local checkout
of Steinberg's official SDK. It is optional and must remain outside the default
core's dependency graph. This document records the inspected dependency and
packaging requirements. Actual build and validator observations, along with the
remaining platform and release limits, are recorded in [VST3.md](VST3.md).

| Item | Pinned value |
|---|---|
| Official repository | [steinbergmedia/vst3sdk](https://github.com/steinbergmedia/vst3sdk) |
| Release tag observed locally | [`v3.8.1_build_84`](https://github.com/steinbergmedia/vst3sdk/tree/v3.8.1_build_84) |
| Exact root commit | [`3cdf9ca5d1f5b1b21e0a86832aa4abe55607bd96`](https://github.com/steinbergmedia/vst3sdk/tree/3cdf9ca5d1f5b1b21e0a86832aa4abe55607bd96) |
| SDK build requirement | CMake 3.25.0 or later, as specified in the pinned [SDK CMakeLists.txt](https://github.com/steinbergmedia/vst3sdk/blob/3cdf9ca5d1f5b1b21e0a86832aa4abe55607bd96/CMakeLists.txt) |
| Inspection date | 2026-11-23 |

The headless configuration disables `SMTG_ENABLE_VSTGUI_SUPPORT`,
`SMTG_ENABLE_VST3_PLUGIN_EXAMPLES`, and `SMTG_ENABLE_VST3_HOSTING_EXAMPLES`.
The SDK validator remains a distinct verification requirement for the adapter.
This configuration needs the `base`, `cmake`, `pluginterfaces`, and `public.sdk`
submodules; it does not need the VSTGUI, documentation, or tutorial checkouts.

### Exact submodule revisions

The following gitlinks were read from the pinned root commit. The four entries
marked **inspected** were initialized locally and their checked-out commits
matched these gitlinks. The other entries were uninitialized: their revisions
are recorded for completeness, without a claim that their contents or notices
were inspected. Repository names follow the pinned
[.gitmodules](https://github.com/steinbergmedia/vst3sdk/blob/3cdf9ca5d1f5b1b21e0a86832aa4abe55607bd96/.gitmodules).

| SDK path | Official source at pinned commit | Local inspection |
|---|---|---|
| `base` | [vst3_base · `fcf9da0bd27a16f7f03773a3a39822f28f5c8477`](https://github.com/steinbergmedia/vst3_base/tree/fcf9da0bd27a16f7f03773a3a39822f28f5c8477) | Inspected |
| `cmake` | [vst3_cmake · `054c9143cbb8d47fc4694e473f2ee3b4d951a8f5`](https://github.com/steinbergmedia/vst3_cmake/tree/054c9143cbb8d47fc4694e473f2ee3b4d951a8f5) | Inspected |
| `pluginterfaces` | [vst3_pluginterfaces · `4f547e8e102b47de4a8b8aaf343c73b700786372`](https://github.com/steinbergmedia/vst3_pluginterfaces/tree/4f547e8e102b47de4a8b8aaf343c73b700786372) | Inspected |
| `public.sdk` | [vst3_public_sdk · `586dc5e6c8012c3e4b01c79389375cbe96bdb1da`](https://github.com/steinbergmedia/vst3_public_sdk/tree/586dc5e6c8012c3e4b01c79389375cbe96bdb1da) | Inspected |
| `doc` | [vst3_doc · `8bfca19d3b76a61d093951ba9297047f544caea1`](https://github.com/steinbergmedia/vst3_doc/tree/8bfca19d3b76a61d093951ba9297047f544caea1) | Gitlink only |
| `tutorials` | [vst3_tutorials · `33b73dfbb87f3fde3bce8c0a10cae934dc66ad34`](https://github.com/steinbergmedia/vst3_tutorials/tree/33b73dfbb87f3fde3bce8c0a10cae934dc66ad34) | Gitlink only |
| `vstgui4` | [vstgui · `5db272256172557818b6158cf0bb2c4410bddb25`](https://github.com/steinbergmedia/vstgui/tree/5db272256172557818b6158cf0bb2c4410bddb25) | Gitlink only |

### Observed official license notices

The five files below were read from the local pinned checkout. Each identifies
the **MIT License**, with copyright **2026, Steinberg Media Technologies GmbH**.
All five files have the same SHA-256:
`e86d79e19e4a33ebc442d2e0d083887dfc75a2a8de9fe17eeddedb84810ad74e`.

| SDK-relative file | Official pinned source |
|---|---|
| `LICENSE.txt` | [SDK root notice](https://github.com/steinbergmedia/vst3sdk/blob/3cdf9ca5d1f5b1b21e0a86832aa4abe55607bd96/LICENSE.txt) |
| `base/LICENSE.txt` | [Base notice](https://github.com/steinbergmedia/vst3_base/blob/fcf9da0bd27a16f7f03773a3a39822f28f5c8477/LICENSE.txt) |
| `cmake/LICENSE.txt` | [CMake notice](https://github.com/steinbergmedia/vst3_cmake/blob/054c9143cbb8d47fc4694e473f2ee3b4d951a8f5/LICENSE.txt) |
| `pluginterfaces/LICENSE.txt` | [Plug-in interfaces notice](https://github.com/steinbergmedia/vst3_pluginterfaces/blob/4f547e8e102b47de4a8b8aaf343c73b700786372/LICENSE.txt) |
| `public.sdk/LICENSE.txt` | [Public SDK notice](https://github.com/steinbergmedia/vst3_public_sdk/blob/586dc5e6c8012c3e4b01c79389375cbe96bdb1da/LICENSE.txt) |

Every binary bundle built with this SDK must include these unmodified
official copyright and permission notices in its third-party notice resources,
with their source paths identifiable. Packaging verification must compare the
bundled notices against the pinned files. The optional target currently copies
the common SDK notice into its resources; the five inspected notices are byte
identical. A distributable package still requires its own packaging review.

The observations above apply to the named files and headless integration scope.
Optional GUI components and example-specific third-party code are outside that
scope; adding them requires recording their own sources and notices. The SDK
pin and notice inventory must be updated together if the dependency changes.

The [resource notice index](../plugins/vst3/THIRD_PARTY_NOTICES.md) identifies all
five source paths and revisions for packaging beside the common `LICENSE.txt`.
On 2026-11-23 the existing Linux bundle's
`build/vst3/VST3/Release/RhodesResearch.vst3/Contents/Resources/LICENSE.txt`
was compared byte-for-byte with all five pinned source files: all six copies
were identical, 1094 bytes, with the SHA-256 above. The optional target also
copies `THIRD_PARTY_NOTICES.md` through its resource list. The subsequent
[Linux bundle audit](agents/development-20261123/W3-A058.md) verified that the
bundled index exactly matches the project source; the
[visibility update](agents/development-20261123/W4-A062.md) preserved the bundle
metadata and required entry points. These are local bundle observations, not
verification of a distributable package on every platform.

## Optional independent Python audit

[`tools/validate_contact_reference.py`](../tools/validate_contact_reference.py)
uses NumPy arrays and SciPy's independent Radau, matrix-exponential, root and
quadrature routines. These are developer-side numerical audit dependencies.
They are not dependencies of the C++ core, offline C++ reference, VST3 audio
callback, ordinary CTests, or standard-library candidate-sweep runner. No
Python package or third-party source is vendored by this documentation change.

The following installed versions and metadata were inspected in the Linux
Python 3.12 environment on 2026-11-23. These are observed audit versions, not a
complete environment lock or a claim about other available releases.

| Distribution | Observed version | Primary source | Upstream project license |
| --- | --- | --- | --- |
| NumPy | `2.3.5` | [numpy/numpy](https://github.com/numpy/numpy), [official license](https://numpy.org/doc/stable/license.html) | BSD 3-Clause |
| SciPy | `1.17.0` | [scipy/scipy](https://github.com/scipy/scipy), [official project](https://scipy.org/) | BSD 3-Clause |

Installed `METADATA` identifies both distributions as BSD licensed and points
to those upstream repositories. Their installed license files retain the
project notices and additional bundled-component notices. The inspected files
list OpenBLAS, LAPACK, GCC runtime and libquadmath notices; NumPy also lists
source/build components. Do not reduce a redistributed binary distribution's
complete notice set to the project-level BSD label. These Python distributions
are not packaged in the VST3 bundle.

| Installed file, relative to `site-packages` | Bytes | SHA-256 of complete installed notice file |
| --- | --- | --- |
| `numpy-2.3.5.dist-info/LICENSE.txt` | 47768 | `2046a3130e50b11c01659b3a0d963e6ae0b7436ff8e89cbcfd9e87bc6112d595` |
| `scipy-1.17.0.dist-info/LICENSE.txt` | 46838 | `4daf14e37432e7026165979ecb7a39930707a3e89bef4876cfaf9dad36ed364a` |

These hashes identify the observed notice bytes, not a wheel, source archive or
all transitive dependencies. A new audit environment should record its actual
versions and retain its distribution notices. The repository's original-code
license remains a separate owner decision.
