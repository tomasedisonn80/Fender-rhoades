# W2-020 follow-up — cold first Volume drag fails

Parent: HOST-004. 2026-11-24. **FAIL: 19/20 original checks pass; added PointerRoot baseline passes (20/21 total).** The final native gate remains open.

W2-020 made **two actual REAPER launches**: the first stopped before checks due to startup-dialog focus stealing; this authorized follow-up completed after fixing only that startup handling. The original report, raw result and captures remain hash-identical. Dialog dismissal now requires observed unmapping, retries while mapped within the unchanged 35-second deadline, and respects the normal evaluation countdown. No license settings changed. Assertion-call ASTs are identical between attempts.

Executed `with-xvfb.py python3 w2-020-followup/probe_reaper.py` after sourcing `gui-env.sh`, then `w2-020-followup/analyze_capture.py`, all beneath `/home/agent/rhodes-7a7c13ab7da3`. Compilation passed. REAPER reported `7.86/linux-x86_64`; Filtered loaded at FX 0 with 27 parameters and a 1024×768 editor. Runner exit 0 denotes completion, not assertion success.

**Observed failure:** before the first Volume gesture, native focus was PointerRoot (`1`). The unchanged 36-source-pixel drag expected `0.5 → 0.7`; held/released host values stayed `0.5`, with no changed parameter or visible feedback. Native events show child `FocusOut(detail=5, NotifyPointer)`, `FocusIn(detail=3)`, then `FocusOut(detail=0, Ancestor)` while explicit focus ended on floating parent `4194377`. The first-drag cancellation remains reproducible in this bare-Xvfb host sequence.

All seven identities and the other six controls passed, changing only their intended indices. A later Volume drag reached `0.6000000238418579`; genuine child-to-REAPER-main FocusOut stopped further held movement at that value. Both Volume and MIDI-60 highlights cleared. After-loss and final images exactly matched idle across 786,432 RGB pixels; four held/cleared images and the failed first-Volume image were visually inspected.

All three module and eight source records stayed stable. Filtered SHA-256: `f8e62098528353b7d90b1df08ea0b4fafc710ba8d3f493b820831a72bde72adc`. Full hashes, native events, 17 untouched PNGs and comparisons are in the follow-up directory’s `run.json`, `analysis.json`, and `outcome.json`.

No rebuild/repository/Git/state changes. Audio remained closed; direct edit-callback balance and processor note-off were unobserved. Next: distinguish cold focus assignment to the floating ancestor from genuine editor departure without weakening the passing main-window cancellation checks.
