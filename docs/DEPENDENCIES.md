# Dependency provenance

## Default research core

The default core has no third-party runtime library dependencies and downloads
no packages during its build. Its build requirements remain a C++20 compiler
and CMake 3.24 or later; repository tooling uses Python 3.11 or later. See
[development instructions](DEVELOPMENT.md) and the [root build](../CMakeLists.txt).

No third-party source is vendored into this repository by the optional SDK plan
below. The project-wide software license remains an owner decision; recording
the SDK's license does not select a license for this project's original code.
See [rights and attribution](../RIGHTS.md).

## Optional official VST3 SDK

The planned experimental VST3 adapter uses a separately supplied local checkout
of Steinberg's official SDK. It is optional and must remain outside the default
core's dependency graph. This document records the inspected dependency and
packaging requirements; it does not claim that an adapter build, validator run,
or released binary has passed.

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

Any future binary bundle built with this SDK must include these unmodified
official copyright and permission notices in its third-party notice resources,
with their source paths identifiable. Packaging verification must compare the
bundled notices against the pinned files. This is a requirement for the future
bundle, not a claim that this documentation change creates one.

The observations above apply to the named files and headless integration scope.
Optional GUI components and example-specific third-party code are outside that
scope; adding them requires recording their own sources and notices. The SDK
pin and notice inventory must be updated together if the dependency changes.
