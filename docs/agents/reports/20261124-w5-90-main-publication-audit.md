# W5-90 — Main publication and repository-scope audit

Evaluator: `/root/w5_90_main_publication_audit`; 24 November 2026.

**No material publication or tracked-content issue was found in the inspected
checkpoint.** Local HEAD, `refs/heads/main`, `refs/remotes/origin/main`, and the
GitHub `git/ref/heads/main` response all identify
`766dd11c73cf9d1dcf6c1960b4333767356f9937`. The active branch is `main`.
This is an observed checkpoint, not a claim about later integration commits.

## Published scope

Starting after baseline `a0f2b46da3a48879cacddc9f03d66217056467bc`, the first
session commit is `807e078b3670a7eb3b874dfb9e28980e29a209f9`. The inspected range
contains **21 commits**, each with exactly one parent equal to the preceding
commit. The net change is **165 paths: 143 additions and 22 modifications**,
with no deletions. Changes comprise batched observation and scoped Clang fixes,
macOS linking and metadata validators, offline diagnostic/verification tools,
targeted tests, documentation, evidence and campaign/state records. They fit the
user's authorized main-branch development and publication work.

The entire `plugins/vst3` directory is unchanged from that baseline. Explicit
byte comparisons also preserve the state codec and both coefficient headers.
The six class IDs, model tags 1/2/3, RHST v1 sixteen-byte format and declared
0/48/48-frame latencies remain intact. SDK pins, optional VST3 workflow,
dependency documentation and rights notice are unchanged. All fourteen tasks'
acceptance clauses, dependencies and required evidence kinds also match the
session baseline; the inspected state changes renew claims and extend assigned
paths rather than award scientific completion.

The tip contains **688 tracked files** and no Git links, SDK/vendor checkout,
build-directory files or native object/module/executable paths. I inspected all
**201 changed blob/path versions** across the range, including transient versions.
Selected private-key, GitHub-token, AWS-key-ID and literal bearer-token patterns
had no matches. This bounded pattern/path inspection cannot prove arbitrary
credentials are absent.

Eight new compressed files are deliberate evidence: seven tar archives contain
**1,120 regular members**, and one gzip file retains the Python log. Bounded
in-memory reads found no native executable/archive magic signatures in changed
blobs or these members; no archived code was executed or extracted to disk.
The contents are retained reports, inputs and logs, not committed plug-in builds.
The read-only GitHub releases endpoint returned an empty list. This observation
does not prove that a release or branch rewrite never existed historically.

## Procedure and remaining integration check

Required `project.py validate`, `sync --check` and `next --json` passed at revision
27 with 23 evidence records and unvalidated fidelity. Read-only `git rev-list
--reverse --parents`, `git diff`, `git ls-tree`, `git show`/`cat-file`, connector
GET requests and Python JSON/hash/archive inspection supplied this audit.
No build, test suite, Git mutation, release or canonical-state edit occurred.
Only this report was written.

The working tree deliberately contained root-owned campaign updates and current
peer reports. After all final publication, root still needs to verify a clean
worktree, equal local/remote main heads and the final applicable CI result.
The observed linear graph does not establish the server's historical push flags;
expected-SHA nonforce publication remains the integrator's separate procedure.

## Focused identities

File bytes below match both baseline and inspected tip. Graph digest covers the
21 LF-terminated `commit parent` lines in chronological order.

| Object or path | Identity |
| --- | --- |
| Inspected Git tree | `9a11df74b5ddad667e432af8a87cca6c12ee1bb1` |
| Commit-parent graph SHA-256 | `44f5679ca0ee71f804a1871dd08296963818c2aa9f205fc2d473da9246d791e9` |
| `plugins/vst3/plugin_ids.hpp` SHA-256 | `1b5744731384e100c2f32cfed42fccf9fe2168d39df5cc29f1ac1108b9d94a81` |
| `include/rhodes/instrument_state.hpp` SHA-256 | `c11c9d982887f4228c76c3ee9e10fe5627c2dc0f1053ad531cfd316285a65616` |
| `include/rhodes/audio_filter_coefficients.hpp` SHA-256 | `c1ab39356d7e44348f9dc56f9120ff2598889d1fa144e5294b084585655eb76d` |
| `include/rhodes/quadrature_filter_coefficients.hpp` SHA-256 | `3cca4ea6553ceb01810a6230f2144491364fa48af7d6cfbc43a8e3dcf5d08b51` |
| `.github/workflows/vst3.yml` SHA-256 | `6cf2593353d05a6b122c99736399132b4c813ec8fba5af03b39ed4d5fdd8e3f3` |
