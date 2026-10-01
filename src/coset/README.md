# src/coset

**Purpose.** Disjoint coset enumeration and the constrained least-element descents built on the stabiliser chain.

**Governing spec sections** (`docs/specification.md`, normative): §8.1 (disjoint coset enumerator), §9.4 (least elements by successive point constraints; the rule-2 descent), §11.1 (logical work quota), §3 (product convention).

**Status.** Implemented in S4 (`docs/slices/S4.md`, notes in `docs/slices/S4-notes.md`):
- `coset.h`: the interface. A subgroup is the suffix of a chain (`src/bsgs/chain.h`) from a level; a coset `J r` is `{j r}` with `j` acting first (spec §3).
- `least.c`: `canon_coset_least`, the least image array in `J r` subject to point constraints `v ↦ c` (constrained points are fixed first, then the image array is minimised point by point; each step `r' ← t_o r'`, `J' ← J'_v`, with one transient unverified rebuild when `v` is moved by `J'` and is not its base point), and `canon_coset_least_outside`, the least element of `H \ K` for §9.4 rule 2 (a branch `C = J r` is discarded iff `r ∈ K` and `J ≤ K`).
- `enumerate.c`: the §8.1 enumerator `visit(H, r)` with zero pruning over a chain, consumer callbacks (stop on request), a cancellation poll per node, and the logical work quota counted per visit call (`canon_coset_visit_enter`, shared with the explicit backend's oracle enumeration in `src/bsgs/explicit.c`). `t_b` comes from `canon_coset_least` with the constraint `a ↦ b`; children are `visit(H_a, t_b r)`.
Both backends implement `canon_group_ops.enumerate` and visit the same nodes in the same order. Counters: `canon_coset_stats` (descents, transient rebuilds). Performance work (level reuse instead of one rebuild per step, per-node allocation) is **M5**. Tests: `tests/c/test_coset_least.c`, `test_enumerate.c`, and through the objectives `test_group_stream.c`, `test_objectives.c`, `tests/python/test_e2e.py`.

**What may live here.** C17 sources and internal headers for this module only, no third-party code and no dependencies beyond the C standard library. Each rule implemented must cite its spec section in a comment (see `CLAUDE.md`). Golden constants must be derived from the rules, never copied (spec §20).
