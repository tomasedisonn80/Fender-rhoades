# W4-74 — Current documentation claims

Agent: `/root/w4_74_current_claims`  
Date: 2026-11-24  
Scope: current README, DEVELOPMENT, VST3, BENCHMARKING, ARCHITECTURE, NUMERICS and campaign index; read-only review except this report.

## Findings

No material stale status or unsupported completion claim was found in the inspected snapshot. Required project instructions, generated state, roadmap and canonical state were read. State revision 27 retains unvalidated fidelity and incomplete PHY-002, RT-001 and HOST-001.

| Area | Observed boundary |
| --- | --- |
| Native platforms | README records Linux/Windows/macOS execution. DEVELOPMENT and VST3 bind observations to identified revisions and preserve skipped cases. VST3's cancelled macOS job is explicitly in the historical 23 November subsection, not a current untested-platform claim. |
| Runtime work | BENCHMARKING distinguishes the frozen plan from three completed 72-process captures. It preserves the worse first batch and larger final maximum. The 7.2% CPU observation is neither a causal speedup nor a device deadline guarantee. |
| Numerical evidence | README and BENCHMARKING separate exact retained output replay from the original independent reference integration. Synthetic/toolchain limits remain explicit. |
| Physics and product | Four-fixture contact results remain offline and partial. ARCHITECTURE describes a target; NUMERICS scopes absent mechanisms to the direct research engine. Neither declares calibrated realism, completed PHY/RT acceptance, native DAW qualification or a release. |
| Campaign | The index correctly marks waves 1–3 completed, wave 4 underway and wave 5 planned. It distinguishes ledger integrity from execution/scientific evidence. |

Optional clarity improvement for the integrator: `docs/NUMERICS.md:174–191` explains the filtered `AudioEngine` alongside the direct engine but offers no Q6/`QuadratureEngine` cross-link. Add a short pointer to the frozen quadrature contract if this guide should orient readers to all three paths. Its opening scope at lines 3–13 makes this a discoverability omission, not an incorrect claim that every variant lacks antialiasing.

No correction to dated evidence is warranted. No builds, tests, Git operations, ledger edits or canonical-state edits were performed. All nine inspected hashes remained unchanged at the final source check.

## Executed checks

| Command | Actual result |
| --- | --- |
| `python3 tools/project.py validate` | Exit 0; 14 tasks, 22 evidence records; fidelity unvalidated. |
| `python3 tools/project.py sync --check` | Exit 0; generated state current. |
| `python3 tools/project.py next --json` | Exit 0; revision 27, no available tasks; specimen/data blockers retained. |
| `python3 tools/check_docs.py` | Exit 0; documentation integrity passed, once. |

## Inspected SHA-256 identities

| Path | SHA-256 |
| --- | --- |
| `README.md` | `b4c3f07eb1c8fcc01527ceedab19b770635f64715e9a157a4e2e3f16a078b038` |
| `docs/DEVELOPMENT.md` | `7761d929fff1b9f3d5c7f4956c869f9b806dd0793ebc100f44cd8eb71b91b419` |
| `docs/VST3.md` | `9ac3bf761fc536eade3fdee70eb77117338c6dc879b493081ff22be22ff79dba` |
| `docs/BENCHMARKING.md` | `02439bddb473c9abeb46714ee48c84030f2e06bafe8144e70a2b61214b82a4e8` |
| `docs/ARCHITECTURE.md` | `65116bc34c4beea134acbff8094f1b2c7dca5949ad33334d966e23e332abce51` |
| `docs/NUMERICS.md` | `5307150655f5044d19a4ceae2aa357254cc0209130f0a1a6a4a553a4a054bc86` |
| `docs/agents/DEVELOPMENT_WAVES_20261124.md` | `4ee13849fc38c2b4c6eaa5624e28cdad8efbf64e68b69f695d06a179d387703e` |
| `project/state.json` | `fd69ef9e6c59a436636f96a7a9295b1a42903ea0c853b51350b5f6ce29eba1e9` |
| `docs/PROJECT_STATE.md` | `12f0ac868d8465761fd0fb8ba2a30e8e5da9a6b813d8b366b3dcfa15515f5725` |
