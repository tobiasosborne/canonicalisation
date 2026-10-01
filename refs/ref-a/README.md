# refs/ref-a

**Purpose.** Blind reference implementation A of the frozen semantics (P1, CDAG-2, objectives, status), per `refs/README.md` and spec §20 / plan M0.

**Rules.**
- Written from `docs/specification.md` alone, by an author who does not see `ref-b/`, shares no code with it and exchanges no implementation choices before comparison.
- Any language; may use the standard library of that language but no existing canonicalisation software. Do not copy `checks/review_checks.py` or the expected constants in `refs/vectors/golden.json`: derive results from the rules.
- Reads cases from the JSON inputs in `refs/vectors/` (and generated case files named by seed) and **emits one record per case in the format of `refs/compare/FORMAT.md`**.
- Keep sources, build instructions and seeds in this directory. Seal it by recording the commit hash in `refs/SEALS.md` when complete.

**Governing spec sections.** §3, §3.1, §3.2, §4, §7, §8, §11, §17, §20.
