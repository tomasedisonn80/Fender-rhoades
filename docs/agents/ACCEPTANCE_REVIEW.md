# Completion review contract

This is the integrator's required review before completing a task or promoting
fidelity. It addresses A072's finding that a passing evidence kind alone does not
establish relevance to each acceptance criterion. The CLI checks declared kinds,
dependencies, hashes and typed metadata; it does **not** evaluate the scientific
meaning of a report or execute the command recorded in it.

## Freeze what was evaluated

Save an immutable report under `docs/validation/` with the task ID, exact acceptance
text, dependencies, required kinds and target used for the decision. Include its
definition digest, tested commit or explicit file-hash manifest, model/asset hashes,
input hashes and evaluator version as applicable. A Git ID's syntax alone does not
prove that it contains the tested source: inspect the actual revision and local
diff. Do not claim a clean committed baseline when the test used later edits.

For a reproducible definition digest, serialize this projection as UTF-8 JSON
with sorted keys, no extra separators/whitespace and `ensure_ascii=False`, then
compute SHA-256:

```text
{
  "task_id": task.id,
  "acceptance": task.acceptance,
  "dependencies": task.dependencies,
  "required_evidence": task.required_evidence,
  "target": state.target
}
```

Within that frozen definition, use criterion IDs `TASK_ID/C01`, `TASK_ID/C02`, and
so on, together with exact criterion text. The pair of definition digest and
criterion ID is stable; a changed or reordered definition requires a new review.

## Map every criterion

Use one row per acceptance clause. Shared evidence is permitted only with an
explicit explanation of the behavior or condition it covers for this task.

| Criterion and exact text | Supporting evidence/report | Observed coverage and source | Remaining limitation | Decision |
|---|---|---|---|---|
| `TASK_ID/C01`: exact clause | Actual immutable path/ID | Executed check, relevant condition, units/tolerance and tested snapshot | Exact excluded behavior or none | Pass / fail / incomplete / inconclusive |

Do not map oscillator-only tests to finite-mass contact, or one-note measurements
to keyboard-wide coverage. Each physical boundary, construction zone, operating
condition and interaction included in the task needs relevant evidence. Explain
which rows use synthetic, analytic, measured, performance or perceptual evidence.
The same report may support several clauses when it actually tests them.

Record command/procedure, environment, input/model provenance, actual result,
failure analysis and unrun conditions. Verify predeclared thresholds and partitions
where required. Separate code/reference correctness from correctness of physical
assumptions, and byte integrity from authenticity of acquisition.

## Decide and preserve

Complete only when every required clause passes, dependencies are complete and all
required evidence is registered. Any incomplete or inconclusive required clause
prevents completion. Preserve failures and narrowed scope explicitly; do not
rewrite acceptance criteria to disguise a failed condition.

For a regression, reopen downstream completed tasks in reverse dependency order,
retain the old evidence, and create a new review of the repair and affected
conditions. New reports must describe actual new observations; changing an ID or
copying a prior report does not satisfy scientific revalidation. The task CLI's
freshness rules improve record consistency but cannot certify that tests ran.

Register the review as documentary evidence when appropriate, alongside the
required numerical/measured/performance records. The software does not currently
parse this table or verify criterion relevance, source applicability or threshold
justification automatically. The named integrator remains responsible for those
decisions. A later structured review schema should preserve these semantics and
must not present a populated form as an authenticity guarantee.
