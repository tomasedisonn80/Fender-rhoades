# W4-73 contact usage audit

Date: 2026-11-24. Agent: `/root/w4_73_contact_usage_audit`.

## Finding

No material mismatch found between the contact guide, README, DEVELOPMENT, current CLI help, schema and oracle implementation.

The guide correctly separates native generation from validation. Its subprocess example preserves stdout bytes and separate stderr, enables located candidates and retains failed output. Input requires BOM-free UTF-8 and exactly one run. Supported comparison fixtures are exactly `conservative_impact`, `damped_impact`, `forced_clamp`, and `preload_equilibrium`. Output creation is exclusive and requires an existing parent. The declared producer is hashed, never executed; its identity does not attest candidate production.

Default references need only the standard library. Requested damped timing uses the bounded optional SciPy worker; missing SciPy is `not_available`, worker failure/exhaustion is `inconclusive_reference`. Unrequested timing and unresolved positive preload maximizing times do not alone block exit 0. Positive preload value enclosures remain distinct from exact zero plateaus and disabled contacts.

A structurally valid unsupported fixture or absent peaks yields exit 2; failed producer status takes precedence with exit 1. Malformed inputs and byte-limit violations also yield 1; operational failures yield 3. Aggregate precedence is correctly documented as `3 > 1 > 2 > 0`. Limits are 64 files, 32 MiB each and 128 MiB collectively. Partial retention records readable prefix identity or unknown identity, never refunds consumed bytes, and does not turn an earlier manifest into completion.

The current oracle explicitly decodes private-worker output as UTF-8 and matches the later UTF-8 transport record. Historical Wave 2 replay used oracle `74030851ce9511cd50c6952a11b43bfdf5e8da5d7fd10428c009be0210060789`; that replay is not current-source coverage.

## Actual checks and limits

Passed: `python3 tools/project.py validate`, `sync --check`, `next --json`, and `python3 tools/validate_contact_peaks.py --help`. Documentation links and report whitespace were checked after writing. Inspected sources and retained records; all hashes below were unchanged across review. No test suite, native producer, replay, build, Git or canonical-state operation ran.

This review does not establish numerical correctness, Windows native Unicode paths, general continuous extrema, physical fidelity or PHY-002 completion. Next step: retain these boundaries and bind future execution evidence to its actual source identities.

## Source SHA-256

| Source | SHA-256 |
| --- | --- |
| `README.md` | `b4c3f07eb1c8fcc01527ceedab19b770635f64715e9a157a4e2e3f16a078b038` |
| `docs/DEVELOPMENT.md` | `7761d929fff1b9f3d5c7f4956c869f9b806dd0793ebc100f44cd8eb71b91b419` |
| `docs/CONTACT_PEAK_DIAGNOSTIC.md` | `37825a75518ddc7baa212b302af4fe403666f055deb54bb3d92dd2b9d6c3cde4` |
| `tools/validate_contact_peaks.py` | `5bfc879d0a4a42d48667cc2370bafc85a2bd6c6b08c18071c1d60ab7b85be717` |
| `tools/contact_peak_schema.py` | `af5cbf87d155ecb40c176b78a7527b2a7bd686c7fd515be621af93d910db86c4` |
| `tools/contact_peak_oracles.py` | `e691093a11ef34ea68cbbe25614d42a2dbd4f4576535c2c35b289133e8fc2c50` |
| `tools/validate_contact_reference.py` | `c910da5da6b6ad957b709a414cef39a08ed6e8b75c6e36c9d73ef535a064dcdc` |
| `docs/specs/CONTACT_ANALYTIC_PEAK_SLICE_20261123.md` | `56e5caefaebc03596b2ed566d50578b2be2e4dc54e37bf7fdce31dab31243f03` |
| `docs/validation/contact-worker-utf8-20261124.md` | `8448e425d4d8f930c0d09a6f19ea3408696ada6108128c8e43fb5a93f3d3c931` |
| `docs/validation/contact-worker-utf8-20261124.json` | `9b1e313ade2bb2bca127d7f312690c6be030ab6c7ecc1b1454b2590b3d2b1ffd` |
| `docs/validation/contact-peaks-final-replay-20261124.json` | `dfbe13724511c0437e2e6a266d1815360df35791608b6b0be5e4f0f29b60a879` |
