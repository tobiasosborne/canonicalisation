# Review pass, November 2026: common brief for every review agent

You are one of several independent reviewers of `canon`, an exact C17 library for
canonical forms under finite permutation groups (repository root: this checkout).
Read `CLAUDE.md`, `README.md` and `HANDOFF.md` first, then the parts of
`docs/specification.md` (v2.2, **normative**) and the code relevant to your lane.

## Goal
A review and correctness pass (and, for performance lanes, a performance review).
Find real defects, spec non-conformance, unsound reasoning, missing coverage,
and (performance lanes) refactorings or new algorithms that would give the best
performance. Depth over breadth: a few well-evidenced findings beat many vague ones.

## Hard rules
- **Do not run test suites or reference runs.** No `make test`, `make check`,
  `make fuzz`, `make corpus`, `ctest`, `checks/review_checks.py`, the Python test
  suites, `refs/ref-a/run_cases.py`, `refs/ref-b`, `refs/oracle/oracle.py`,
  `refs/compare/*`, golden-vector checks, T1/T2/T3 corpora, benchmarks or fuzzing.
- You **may** build once (`nice -n 19 make`) and compile/run a tiny snippet or one
  `canon-cli` invocation (seconds, under `nice -n 19`) **only** to produce evidence
  for a specific finding. Put such snippets under `/tmp/canon-review-<lane>/`, not
  in the repository. Never anything long-running.
- **Read-only on the repository.** Do not edit `src/`, `include/`, `checker/`,
  `tests/`, `docs/` (except your own report), `refs/`, `reviews/`, `checks/`.
  Do not commit, stage, push, or change branches. No downloads.
- The machine has CPU contention: keep load light, no broad filesystem scans
  outside this repository.

## Deliverable
Write exactly one file: `docs/review-2026-11/<your-lane-id>.md`. **Create it early
and append each finding as soon as it is evidenced** (your session may be cut off
by quota at any time; unwritten findings are lost). Structure:

1. **Scope** — what you read (files, spec sections), what you did not cover.
2. **Findings**, most severe first. For each:
   - ID (`<lane>-NN`), title, severity (critical / high / medium / low / note),
     category (correctness, spec-conformance, soundness, memory-safety, contract,
     performance, maintainability, documentation).
   - **Evidence**: `path:line` references, quoted spec sentences (with §), and
     where possible a concrete counterexample input, a minimal reproduction, or a
     short derivation. State plainly whether you *ran* the evidence or reasoned it.
   - **Recommendation**: what should change.
   - **Remediation plan**: concrete steps (files, functions, spec sections), the
     tests that would prove the fix, risk, and rough effort.
3. **Spec ambiguities** — quote the sentence; do not resolve it by guessing.
4. **Things checked and found sound** — brief, so later reviewers do not repeat them.

Never claim a defect you cannot evidence; label conjectures as conjectures. Your
final chat message should be a 5–10 line summary of the top findings and the path
of your report.
