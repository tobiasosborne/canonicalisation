# Fix round, 8 November 2026: common brief for every fix agent

You are fixing findings from the review pass in this directory. Read `CLAUDE.md`, then
`docs/review-2026-11/SUMMARY.md` (especially §1 "Recommendations taken"), then the report
sections named in your task, then the code and spec sections they cite. The spec
(`docs/specification.md`) is normative; cite the spec section in a comment at each rule you
implement. Treat the review findings as leads: **confirm each one in the code before fixing it**;
if a finding is wrong, say so in your notes and do not "fix" it.

## Rules
- You work in your own git worktree on your own branch. **Commit early and often** with explicit
  paths (`git add <paths>`, never `git add -A`), one commit per finding or coherent step, so that
  nothing is lost if your session is cut off by quota. Do not push, do not merge, do not touch
  `main`. End each commit message with your own model's attribution line.
- Do not edit `docs/specification.md`, `docs/implementation-plan.md`,
  `docs/performance-lower-bounds.md`, `reviews/*`, `review_sources/*`, `checks/review_checks.py`,
  `LICENSE`, or the review reports. Do not edit files outside your lane's list unless unavoidable;
  say why in the commit message.
- **Output must not change.** Every fix must reproduce P1 traces, bytes, statuses, witnesses and
  node counts exactly (PROFILE-EQUIV), unless your task says otherwise.
- **Testing, light only** (the machine has CPU contention; run everything under `nice -n 19`):
  `nice -n 19 make` and the C test programs you touch or add; `nice -n 19 make test` at most twice
  in total (once before your final commit). Lanes that touch memory handling may run
  `nice -n 19 make SANITIZE=1 BUILD=build/san test` once. **Do not run** `make check`, `ctest`,
  `make corpus`, `checks/review_checks.py`, the Python e2e tiers, `refs/` runners, the oracle,
  comparisons, T1/T2/T3 corpora, benchmarks or long fuzzing. A single Python unittest class you
  changed may be run once.
- All allocations in `src/` go through `src/arena/mem.h`. C17, strict warnings, `-Werror`.
- The checker (`checker/`) never includes `src/` or `include/` and copies no producer code.
- No subagents, no background jobs, no downloads.

## Deliverable
Commits on your branch, plus `docs/review-2026-11/fix-<lane>.md` (committed, updated as you go):
for each finding, what you confirmed, what you changed (files, commits), the tests you added and
ran with their result, and what is left undone. Your final chat message: 5–10 lines with the
branch name, the commits, test results, and anything unfinished.
