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
| `{0}`, `Sym(4)` | 10 | 10 | 7 (6 since the review below) | 2 of 3 (2 of 2) | 6 |

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

## Independent review (19 October 2026)

Review of the S7b commit `cb68610` (range `a2eef66..cb68610` with S7, whose findings are in [`S7-notes.md`](S7-notes.md#independent-review-19-october-2026)), every finding re-checked against the code at `1d0d83b`. The implicit automorphisms themselves are sound: `a = t_L t_best⁻¹` is formed only on equal trace and bytes, verified by the group's membership test and by exact action before insertion (a failure of either is `INTERNAL_ERROR`), inserted into the verified chain, and the epoch makes every ancestor recompute `A_S` before its next decision. Both findings below are in the realisation of the incremental rule; neither can make a pruned run unsound (every group used is a subgroup of `A_S`, and the checker accepted every certificate before and after), but both made the explored tree larger than `docs/pruning-rules.md` names, so the counts of the work policies, which are their definition (§11.1), were not the documented ones.

| # | Finding | Disposition |
|---|---|---|
| 1 | **Stale prefix stabiliser.** `A_S` lives in one transient chain (`canon_symmetry.stab`) shared by every depth. `decide_before` recomputed it only when the epoch changed, but a child visit in between rebases it to the child's longer prefix, so the next decision at the parent read the orbits of the pointwise stabiliser of the last descendant's prefix, in general a proper subgroup of `A_S`. S7 decided all children at node entry and was not affected; S7b's per-child decisions are. Under `0x0002` this broke S7b's own definition of done ("`0x0002` keeps S7's counts") and self-review item 4: on 700 random subsets of 4–7 points (generators: automorphisms of `x` plus random permutations) 37 counts differed from those of the S7 build `25fb621`; under `0x0003` 2 of 400. Smallest case: `{0, 1, 3, 4}` under `⟨(1 3 4), (0 3 4 2)⟩`, 35 nodes under `0x0002` instead of 15. | Fixed in `src/search/p1_tree.c`: `A_S` is recomputed also when the stabiliser in place is not this node's (`stab_depth != depth`; only descendants run between two decisions of a node, and they have greater depth). Afterwards `0x0002` equals the S7 build on all 700 cases. Regression case in `tests/c/test_prune.c` (`node_counts`: 41 unpruned, 15 under `0x0002`, 8 under `0x0003`, `|A_known| = 24`), which fails on the old code. |
| 2 | **The child about to be explored was never pruned.** `decide_before` marked `j` as a representative together with the explored children before growing their orbits, so when the enlarged `A_S` had moved `j` into the orbit of an explored sibling, `j` was explored anyway and its subtree duplicated the sibling's; only the members after `j` were pruned. This contradicts the rule's stated consequence ("An automorphism found at a leaf below a child therefore prunes the later children of every ancestor node"), the lemma allowing the prune. Example: `{0, 3}` under `⟨(0 3)(1 4), (0 4)(1 3 2)⟩`: below the root's child 0 the leaves under child 1 yield `(2 4)`, after which `A_S = Stab(0)` contains `(1 2)` and child 2 lies in the orbit of the explored child 1; it was explored (7 nodes), now it is pruned (5). On 800 random subsets 170 counts under `0x0003` fell, none rose; the bytes are unchanged, and trace, bytes and deterministic witness still agree with the unpruned run on every tier of `test_prune.c` and the e2e P1-prune tier. | Fixed in `src/search/p1_tree.c`: two passes, first the explored children's orbits (which may prune `j`), then `j`'s own if it survives. `docs/pruning-rules.md` states the rule in this form. The S7b case `{0}` under `Sym(4)` now takes 6 nodes under `0x0003` instead of 7 (below the root's child 2 the first leaf yields `(1 2)` and prunes child 3); `test_prune.c`, the e2e quota boundaries and the checker's `implicit_case` were updated. Checker harness, T1 tier under the three policies: 3 649 nodes and 788 pruned children (3 661 and 780 before). |

**Decision for the maintainer.** Both fixes change counts under existing work-policy IDs: finding 1 restores `0x0002` to S7's counts (the published ones), and both change some counts of `0x0003`. Spec §11.1 gives "a pruning policy that changes the count ... its own work-policy ID". The review reads the old `0x0003` counts as a defect of the realisation, not as a policy (they were not those of the rule the ID was registered for), and no frozen artefact depends on them: every M0 case uses `work_policy` 1 (`refs/CASES.md`), ref-a's seal is of the `0x0001` path, and no certificate or golden vector stores a `0x0003` count. If the maintainer prefers that a registered ID never change its counts, the corrected rule should become `0x0004` and `0x0003` be restored as it was.
