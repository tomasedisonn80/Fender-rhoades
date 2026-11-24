# W4-75 — Local Linux binary surface

Inspected the three existing Linux VST3 modules on 2026-11-24 using GNU
Binutils 2.44. Each is an ELF64, little-endian, x86-64 shared object.
For each basename below, the exact module path is
`build/vst3/VST3/Release/<basename>.vst3/Contents/x86_64-linux/<basename>.so`.
Python `hashlib.sha256(path.read_bytes()).hexdigest()` produced identical
digests immediately before and after inspection.

| Basename | Bytes | Defined exports / Rhodes-named | SHA-256 |
|---|---:|---:|---|
| RhodesResearch | 334984 | 14 / 0 | `7e1ef2adbe48dbcfd567cb19fae66620b8c6b84e64e5fb232f29b5ad5952dfc9` |
| RhodesResearchFiltered | 343176 | 14 / 0 | `caa527c269192210e3a080427ff1c92b3da2a547f9be1eaf1dd33d26720babc0` |
| RhodesResearchQuadrature | 339080 | 14 / 0 | `efe89d4d9241115222d4661362d33011c9d3484392d439ca40c0bf536f49e20d` |

All three export `GetPluginFactory`, `ModuleEntry`, and `ModuleExit`.
Their eleven remaining defined dynamic exports are standard-library weak
template functions or type-information/vtable symbols. No demangled export
contains `Rhodes` or `rhodes`; this is a dynamic-symbol observation, not
absence of implementation code inside the modules.

Each module declares the same four `DT_NEEDED` entries: `libstdc++.so.6`,
`libm.so.6`, `libgcc_s.so.1`, and `libc.so.6`. Numerically comparing version
suffixes on undefined dynamic symbols gives highest required versions
`GLIBC_2.38`, `GLIBCXX_3.4.29`, and `CXXABI_1.3.9` in every module.
Dependency resolution was not exercised.

## Commands and interpretation

Python subprocesses executed these commands for each exact module path
(`MODULE` below denotes that expanded path); every command exited zero
without stderr:

```sh
readelf --file-header --wide "$MODULE"
readelf --dynamic --wide "$MODULE"
readelf --dyn-syms --wide "$MODULE"
nm -D --defined-only --demangle "$MODULE"
```

`readelf --version` and `nm --version` identified the tools. The
[guide](../../VST3.md) correctly attributes its 137→14 exports, 120→0
Rhodes exports, and runtime versions to historical
[A062](../development-20261123/W4-A062.md) and
[A058](../development-20261123/W3-A058.md) observations. The current local
results agree with those final counts and versions, but these are different
binary hashes and sizes; this inspection does not replay the historical
before/after experiment.

Required project `validate`, `sync --check`, and `next --json` preflights
passed at revision 27. No build, benchmark, installation, `ldd`, module
loading, or validator rerun occurred. These local bytes do not identify
remote artifacts, select a minimum distribution, establish portable
binaries, or validate other platform ABIs.
