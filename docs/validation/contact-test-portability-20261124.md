# Contact test-fixture portability — 24 November 2026

Foundation CI at `448c863f4a5396eac0ba7f0977206b6b84e6bc8d` exposed two fixture assumptions in the new Python tests. The production diagnostic modules and C++ tests were not the failing paths.

- [Windows job 138248024594](https://github.com/baabi-boo/Fender-rhoades/actions/runs/45335974810/job/138248024594) failed the explicit Unicode-stderr assertion. Its synthetic worker wrote through locale-dependent text stderr, which produced literal backslash escapes instead of the intended UTF-8 bytes. The fixture now writes encoded UTF-8 through `sys.stderr.buffer`; the original Unicode and scalar-preservation assertions remain exact.
- [macOS job 138248024810](https://github.com/baabi-boo/Fender-rhoades/actions/runs/45335974810/job/138248024810) could not create a surrogate-escaped filename: the filesystem returned `EILSEQ` before the validator ran. The fixture now probes that filesystem capability. Only `UnicodeEncodeError` and `EILSEQ` cause an explicit unsupported-fixture skip; all other I/O errors still fail. The real CLI retention/hash assertions remain intact wherever such filenames can be created. Ordinary Unicode/space-path checks continue on every platform.

After these test-only changes, the local command below passed **85 tests without skips** in 2.853 seconds: 74 new contact-diagnostic tests and 11 pre-existing peak-sweep compatibility tests. The sweep file was already present at `dfa82a5f1a877e4fa6c70e15fbea71a6770c4eb2`; it is not new work in this campaign.

```sh
python3 -m unittest discover -s tests/python -p 'test_contact_peak_*.py'
```

Final test SHA-256 identities:

| File | SHA-256 |
| --- | --- |
| `tests/python/test_contact_peak_backend_boundaries.py` | `63c684d569bdbef4bbad3ce6663e1a46a79786af18a4ef7a7e740bd5188049d1` |
| `tests/python/test_contact_peak_portability.py` | `14004d860445a1a6075228f641d72fca16ff8b24c55bed9702a01bec21922388` |

The module hashes remain those of the repaired oracle/schema/CLI. Native CI confirmation of these two fixture changes must be checked separately; this local run is not a Windows/macOS claim.
