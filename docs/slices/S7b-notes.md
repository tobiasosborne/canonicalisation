# Slice S7b implementation notes

Brief: [`S7b.md`](S7b.md). Normative text: `docs/specification.md` v2.1 (cited as §x). Prerequisite: S7 ([`S7-notes.md`](S7-notes.md)). Rules: [`docs/pruning-rules.md`](../pruning-rules.md). Implemented 13 October 2026 by one agent on the maintainer's instruction, as S7 was; self-review below.

## Files changed

- `src/symmetry/symmetry.h`, `symmetry.c`: the `epoch` counter (incremented on every insertion); `canon_symmetry_cell_orbits` takes optional representative flags (given representatives are marked first, their orbits grown by BFS, points reached by none stay their own representative); the BFS factored into `grow_orbit`.
- `src/search/p1_tree.h`, `p1_tree.c`: run option `implicit`; the per-node child loop decides each child when it is reached (`decide_before`: recompute `A_S` when the epoch changed, mark the orbits of the explored children and of the child about to be explored, prune the undecided members reached, with their transporters written at once); `implicit_automorphism` at a leaf whose key equals the incumbent's (`a = t_L t_best⁻¹`, membership by `ops->contains`, action by `canon_symmetry_insert`, listed in the certificate when inserted); counters `implicit_candidates`, `implicit_inserted`; two more per-point arrays (`inv`, `roots`).
- `include/canon/canon.h`, `src/api/api.c`: `CANON_WORK_POLICY_PRUNE_2` (`0x0003`), the new default; validation accepts the three IDs; the run options from the policy.
- `checker/cc_check.c`: the header accepts `0x0003`. No other checker change.
- `tools/canon-cli.c`: `--prune on|explicit|off`.
- Tests: `tests/c/test_prune.c` (three policies; the `{0}` under `Sym(4)` case), `test_symmetry.c` (given representatives), `test_certificate.c` (the new default), `checker/tests/test_checker.py` (three policies on every tier; the implicit case and its mutations), `tests/python/test_e2e.py` (tier P1-prune under three policies; quota boundaries of the S7b case).
- Docs: `docs/pruning-rules.md` (the incremental form of the rule, source ii, the three policies), READMEs, `HANDOFF.md`, `CLAUDE.md`, this file.

## Readings

1. **Where a discovered automorphism prunes.** `a = t_L t_best⁻¹` maps the incumbent's path onto `L`'s path; at the node where the two paths diverge it maps one explored child onto another, so it never prunes there. It prunes the *other* children of every ancestor that lie in the orbit, under the enlarged `A_S`, of an explored child. Hence the incremental form: decisions when a child is reached, recomputation when `A_known` grew. Taking every decision at node entry (S7's form) would use a discovered automorphism only at nodes entered later; the S7b case `{0}` under `Sym(4)` shows the difference (7 nodes against 10).
2. **A new work-policy ID.** §11.1 v2.1: "a pruning policy that changes the count has its own work-policy ID". The incremental rule with source (ii) changes the count, so it is `0x0003`; `0x0002` keeps S7's semantics exactly (every S7 count is reproduced by the tests). The default moves to `0x0003` (the strongest sound prune); the model tiers keep comparing under `0x0001`.
3. **Membership check of an implicit automorphism.** `a` is a product of two elements of `G`, so the check cannot fail mathematically; it is performed anyway (§7.3 says "exact membership and object equality") and a failure is `INTERNAL_ERROR`. On the chain backend it is one sift; on the explicit backend one table lookup.
4. **Verification on insertion.** Each inserted implicit automorphism costs a verified rebuild of `A_known` (`canon_bsgs_insert_verified`), at most log₂|A| of them per solve; the candidates that are already members cost one sift.
5. **The checker needs no change beyond the header.** It verifies every listed automorphism by its own sift and action and every pruned child by one named automorphism; where the automorphism came from is irrelevant to the lemma. What it cannot tell is which pruning policy produced a pruned tree (a certificate pruned with implicit automorphisms and relabelled `0x0002` is accepted); stated in `checker/README.md` and tested.

## Deviations from the brief

None. The brief was written in this session for the implementation that followed; the counts it predicts (7 against 10) were derived by hand before the test ran and confirmed by it.

## Measured counters

`test_prune` (both backends): 1 546 cases agree on trace and bytes under both pruning policies, 1 234 in deterministic mode on the witness too.

| Case | Unpruned | `0x0002` | `0x0003` | Implicit automorphisms | `|A_known|` at the end |
|---|---|---|---|---|---|
| empty subset, `Sym(2)` | 3 | 2 | 2 | 0 of 0 candidates | 2 |
| empty subset, `Sym(6)` | 1237 | 6 | 6 | 0 of 0 | 720 |
| `{0}`, `Sym(4)` | 10 | 10 | 7 | 2 of 3 | 6 |

`checker/tests/test_checker.py`, T1 subsets under the three policies: 1 617 certificates, 2 593 175 bytes, largest 23 772, 3 661 nodes, 780 pruned children, 2.8 s of `canon-check` time; the G1 and random tiers scale likewise (every certificate accepted).

## Self-review

1. The decision slot of a node is reset to UNDECIDED for every member before the loop, and an explored member is never reached by `decide_before` as a pruned one (it is marked as a root first).
2. The transporter of a pruned child is computed before any child visit, so the Schreier tree it reads is the one of the decision; the per-depth slots survive the child visits; the stacks are re-read after every call that may grow them.
3. `implicit_automorphism` runs before the witness comparison of a leaf and uses `s->image` (free after the byte comparison) and `s->aut`/`s->inv` (free at a leaf); it never changes the leaf's key or the incumbent.
4. Under `0x0002` the epoch never changes (no implicit insertion), so a node's `A_S` is computed once, as in S7; S7's counts are reproduced on every tier.
5. gcc and clang `-Werror` clean; ASan/UBSan clean; the checker harness and the e2e tiers pass.

## Left out

- Pruning for the other objectives (§8.2 forbids image-only pruning there), trace-prefix domination and bound pruning (§7.3), certificate v1.
- Using an implicit automorphism to prune the *current* node's remaining children when it is found at a leaf below the current child: it is used, at the latest, before the next undecided child of that node is explored, which is the same moment.
