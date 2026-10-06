# Pruning rules of the canonical-image search

**Status.** Slice S7 step 1 (6 October 2026). This file records the one pruning rule the engine implements, its proof, its scope and its quota rule. It is not normative: `docs/specification.md` v2.1 is (cited as §x), and the brief is `docs/slices/S7.md` (§3.1, §3.2, §6.1 D9, §6.2 D14). Code: `src/symmetry/symmetry.{h,c}` (the subgroup `A_known` and the prefix stabilisers), `src/search/p1_tree.c` (the rule in the node loop), `src/api/api.c` (`solve_canonical`, the only caller that passes the descriptor's work policy). Tests: `tests/c/test_symmetry.c`, `tests/c/test_prune.c`, tier P1-prune of `tests/python/test_e2e.py`.

§7.3: "The unpruned evaluator above is normative. An optimisation needs a coverage lemma for this exact key". The rule below is "a checked node-stabilising automorphism and retained equivalent subtree" in the words of §7.3, made precise.

## 1. Notation

Permutations are image arrays, `p[v] = v^p`, and products act left to right: `(pq)[v] = q[p[v]]`, so `(x^p)^q = x^(pq)` (§3). `Aut_G(x) = {g ∈ G : x^g = x}` is the stabiliser `A` of §8.

The P1 tree of `(x, G)` (§7.1, brief §2): a node is a sequence `S = (a_1, …, a_d)` of individualised atoms; `π_S` is the ordered partition the §7.1 refinement loop reaches after individualising `a_1, …, a_d` in order; `τ_S` is the trace of the path; when `π_S` is discrete the node is a leaf with singleton list `L_S`, transporter `t_S`, the unique element of `G` with `L_S^(t_S)` the least element of `L_S^G` (§7.2), and key `κ_S = (τ_S, bytes(x^(t_S)))`; otherwise its target cell `C_S` is the non-singleton cell of least `(size, position)`, and its children are `S·b` for `b ∈ C_S`. For `h ∈ G`, `S^h = (a_1^h, …, a_d^h)`.

**Tree equivariance** (§7.1 last paragraph, §7.2 proof): for `h ∈ G`, refining `x^h` along `S^h` gives `π_S(x)^h` cell by cell in the same order, with the same trace; the target cell of `S^h` in the tree of `x^h` is `C_S^h`; and at leaves `t_(L^h) = h⁻¹ t_L`.

## 2. The lemma and its proof

**Lemma.** Let `a ∈ Aut_G(x)` and let `S = (a_1, …, a_d)` be a node of the tree of `x` with `a_i^a = a_i` for every `i ≤ d`. Then for every `b ∈ C_S`:

1. `b^a ∈ C_S`;
2. `T ↦ T^a` maps the subtree rooted at `S·b` bijectively onto the subtree rooted at `S·b^a`, with `τ_(T^a) = τ_T` for every node `T` of it;
3. for every leaf `L` below `S·b`, `t_(L^a) = a⁻¹ t_L` and `x^(t_(L^a)) = x^(t_L)`, so `κ_(L^a) = κ_L`;
4. hence the two subtrees have the same multiset of leaf keys.

*Proof.* Apply tree equivariance with `h = a`. Because `x^a = x`, the tree of `x^a` is the tree of `x`, so equivariance maps the tree of `x` to itself: node `T` goes to node `T^a`, the partition `π_T` to `π_T^a` and the trace is unchanged. The prefix is fixed pointwise, so `S^a = S` and `π_S^a = π_S` as ordered partitions; the target cell is chosen from cell sizes and positions only, so `C_S^a = C_S`, which is (1). Individualising `b^a` in the same position of `π_S` is the image of individualising `b`, so the child `S·b` maps to `S·b^a`, and every descendant `S·b·c·…` to `(S·b·c·…)^a`, a descendant of `S·b^a`; traces are equal because they are built from cell sizes, which the map preserves. `a⁻¹` gives the inverse map, so (2) holds. For a leaf `L`, `L^a` has the same `G`-orbit as `L` (`a ∈ G`), hence the same least image `M`; the transporter of `L^a` is the unique `t` with `(L^a)^t = M`, and `t = a⁻¹ t_L` qualifies: `(L^a)^(a⁻¹ t_L) = L^(t_L) = M`. Then `x^(a⁻¹ t_L) = (x^(a⁻¹))^(t_L) = x^(t_L)` because `x^(a⁻¹) = x`. Equal traces and equal images give equal keys, (3); (4) follows from the bijection. ∎

## 3. `A_known`: the automorphisms the rule may use

§7.3: "Known automorphisms form a verified subgroup A_known ≤ A. … Verify implicit automorphisms by exact membership and object equality before publication." §14.3 R2: only exactly verified automorphisms may contribute.

`A_known` is a verified stabiliser chain (`canon_bsgs_build_verified` from zero generators, then `canon_bsgs_insert_verified`, which re-runs the §9.1 verifier at each enlargement). An element `p` is inserted only after `canon_symmetry_insert` has checked, in this order: `p` is a bijection of `{0..n−1}`; `p ∈ G` by the group's own membership test; `x^p = x` by exact action and extensional equality (`canon_root_act_into`, `canon_root_equal`). An identity or an element already in `A_known` is not inserted. So `A_known ≤ Aut_G(x)` always; it need not be all of `A`, and neither the lemma nor the rule needs completeness.

**Source (i), implemented.** At the root, every input generator of `G` (the generators given to `canon_group_create` or `canon_group_create_signed`, in input order, identities and repeats included; both backends record them on the handle) goes through those checks; a generator that moves `x` is skipped. A group made by the `conjugate` op records no generators, so its `A_known` is trivial.

**Source (ii), for slice S7b (not implemented).** If two leaves `L` and `B` (the incumbent) have equal keys, then their images are equal, `x^(t_L) = x^(t_B)` (equal bytes suffice, since the encoding is injective, §7.2), and `a = t_L t_B⁻¹` satisfies `x^a = (x^(t_L))^(t_B⁻¹) = (x^(t_B))^(t_B⁻¹) = x`, with `a ∈ G` because `t_L, t_B ∈ G`. So `a ∈ Aut_G(x)` and inserting it into `A_known` is sound. It must still pass the same exact checks before insertion (§7.3 "verify … before publication"; R2): the argument relies on the key comparison and the transporters being right, and the checks make a defect there cost pruning, not correctness. Inserting during the search also changes which later children are pruned, so the explored-node count of a policy using source (ii) is that of a different work policy (§11.1) and needs its own ID.

**Nothing else** contributes (R2).

## 4. The rule

At a node `S = (a_1, …, a_d)` let `H_d` be the pointwise stabiliser of `a_1, …, a_d` in `A_known` (`H_0 = A_known`). Every `h ∈ H_d` satisfies the lemma's hypothesis, so `C_S` is a union of `H_d`-orbits. **Explore exactly one child per `H_d`-orbit on `C_S`: the numerically least atom of the orbit. Skip the others.**

*Coverage.* Claim: the pruned search of the subtree at any explored node `S` finds the least key of the whole (unpruned) subtree at `S`. By induction on height: at a leaf there is nothing to prune; at an internal node, every child `S·b` lies in the orbit of an explored child `S·r`, `b = r^h` with `h ∈ H_d`, so by the lemma the subtree at `S·b` has the same key multiset as the subtree at `S·r`, whose least key the pruned search of `S·r` finds by the induction hypothesis. The least key over the explored children is therefore the least over all children. At the root this is the P1 canonical image: the same trace and the same bytes (PROFILE-EQUIV; the result reports profile `0x0001`).

*Computation.* `H_{d+1}` is the stabiliser of `a_{d+1}` in `H_d`: `canon_symmetry_descend` rebuilds it once per explored child with `canon_bsgs_rebase(…, verify = false, …)` (S3: transient chains of a descent are not verified) from the strong generators of `H_d` with base prefix `(a_{d+1})`; level 1 of the result generates `H_{d+1}`. Orbits on `C_S` come from union-find over those generators (`canon_bsgs_orbit_ids`). Soundness needs only that every generator used lies in `A_known` and fixes the prefix: each strong generator of a chain built from a generator list is a word in that list (chain provenance), and one stored at level 1 of a chain with base prefix `(a)` fixes `a` (the S3 chain invariant stated in `src/bsgs/chain.h`: a strong generator inserted at level `j` appears in `S_0 … S_j` and fixes `b_0 … b_{j−1}`). An incomplete rebased chain could only split orbits further, losing pruning, never soundness. As a guard, every generator of `H_d` must map `C_S` into itself (the lemma's (1)); a violation is `CANON_INTERNAL_ERROR`.

*Order inside a cell.* §7.1: "Internal order of members within a cell is not semantic." The representative is therefore the numerically least atom of the orbit, not the least in the cell's internal (`lab`) order; the brief's "the least `b` of each orbit in cell order" is read this way (`docs/slices/S7-notes.md`). The explored children are visited in the cell's internal order; the answer does not depend on visiting order (it is a minimum), and neither does the explored set.

*No work when there is nothing to use.* When `A_known` is trivial (no input generator fixes `x`, or `n = 0`) the traversal is exactly the unpruned one: no rebase, no orbit computation, the same nodes in the same order and the same ANY witness. Once `H_d` is trivial on a path no further rebase happens below it. The only extra work of policy `0x0002` is building `A_known` at the root.

## 5. Scope

**Objectives.** Only `CANONICAL_IMAGE` prunes. §8.2: image-preserving pruning "need not preserve complete-group coverage", so the objectives whose answer includes `A` or `Aλ` run the reference tree: `canon_p1_search_run` stays the unpruned evaluator and is what the labeling and signed consumers call; only `solve_canonical` passes the descriptor's policy to `canon_p1_search_run_policy`. The enumeration objectives have no tree.

**Witness.** The lemma preserves the least key, not the least witness: a skipped leaf `L^a` can carry a smaller `t_(L^a) = a⁻¹ t_L` than every explored leaf attaining the key. With `CANON_WITNESS_DETERMINISTIC` the witness is the least element of `A t`, computed from the complete stabiliser as in S4 from any attaining `t`, so it is unchanged. With `CANON_WITNESS_ANY` the result is the least attaining witness among the explored leaves: valid (`t ∈ G`, `x^t = c`; `canon_result_verify_witness` accepts it) but possibly different (§14.3: "ordinary mode may return different valid witnesses"). On the T1 subsets 12 of 2156 runs return another witness (`test_prune.c`).

## 6. The quota under the work policy

§11.1 v2.1: "A logical work quota, if offered, counts the nodes of the traversal fixed by the profile's policy for the objective … which traversal is counted is selected by a work-policy ID in the capacity descriptor: `0x0001` is the unpruned reference traversal (the engine then runs unpruned), and a pruning policy that changes the count has its own work-policy ID, must give the same count in every execution mode and worker count, and is recorded in results and certificates beside the profile."

- `canon_capacity.work_policy` (brief §6.2 D14): `0x0001` the reference traversal, `0x0002` this rule; `0` selects the context default; the built-in default is `0x0002` (D14 (c)); any other value is `CANON_UNSUPPORTED_ACTION` at context and at problem creation.
- Under `0x0002`, `max_search_nodes` counts the `NODE` tokens of the nodes actually explored. A skipped child is never entered and never counted; no count-only traversal of pruned subtrees is made (D9). With the deterministic witness, the stabiliser enumeration gets the quota the tree left, as before. The quota counts `NODE` tokens (and enumeration visits) only: under `0x0002` the verified chain rebuilds of `canon_symmetry_from_inputs` at the root (at most `log₂|A_known|`, one membership test and one action per input generator) and the per-child rebases of `canon_symmetry_descend` are work the quota does not bound.
- Under `0x0002`, every other objective runs and counts exactly as under `0x0001`. The result records the effective policy (`canon_result_work_policy`: `0x0002` for a canonical image solved under `0x0002`, whether or not anything was pruned, `0x0001` otherwise).
- The M0 references and the e2e model tiers compare under `0x0001` (D14 (c)); `tests/python/test_e2e.py` passes `--work-policy 1` by default.

*The count is well defined.* The explored count does not depend on which member of an orbit is explored, nor on the visiting order: for `b' = b^h` with `h ∈ H_d`, the map `T ↦ T^h` carries the pruned subtree at `S·b` onto the one at `S·b'`, and the stabilisers along the two paths are conjugate (`Stab_(H_d)(b^h) = h⁻¹ Stab_(H_d)(b) h`), so their orbits on corresponding cells correspond and, by induction on height, the explored counts are equal. (This uses that each rebased chain generates the whole stabiliser; the deterministic Schreier-Sims closure guarantees it, and `test_symmetry.c` checks it against brute force on every T1 prefix, but the per-node rebases are not re-verified at run time.) The count is therefore a function of `x` and of `A_known` as a group. It is the same for both group backends (`A_known` is always built by the chain code; the tree does not depend on the backend), for any order of the generators, and with repeated or identity generators added (`test_prune.c` checks all four).

## 7. Open question for the maintainer: the count depends on the generating set

Under source (i), `A_known = ⟨g_i : g_i an input generator with x^(g_i) = x⟩`. This group depends on the generating set as presented, not only on `G`. Example (`test_prune.c`, `test_count_invariance`): `G = C_4` on 4 points and `x = {0, 2}`. With the greedy generator `c = (0 1 2 3)`, `c` moves `x`, so `A_known = 1` and 3 nodes are explored; with every element of `C_4` as generators, `c² = (0 2)(1 3)` fixes `x`, `A_known = ⟨c²⟩` and 2 nodes are explored. Trace and bytes agree, as they must.

So a policy-`0x0002` capacity verdict near the boundary can differ between two presentations of the same group. §11.1 v2.1 says the verdict is "a function of semantic input, objective and descriptor" and "the count is a function of input, objective and descriptor, which names the work policy". If the generator list counts as input, the implementation meets this; if "semantic input" means the group `G` as a set, it does not. Possible remedies, none implemented: compute `A_known` from a presentation-independent generating set of `G` (for example the canonical generating sequence behind §9.4's `Group(G)` bytes), or declare in the spec that the policy-`0x0002` count depends on the presentation. The `0x0001` count depends on `G` only. **Question: which reading does the maintainer want?** Until answered, cross-presentation comparisons of node quotas must use `0x0001` (the e2e metamorphic checks compare records, which carry no counts, so they are unaffected).

## 8. Counters (internal; read by tests, no public metrics yet)

`canon_p1_search`: `nodes` (explored, = `NODE` tokens counted), `leaves`, `images`, `pruned` (children skipped), `children` (children of explored internal nodes; on a complete run `nodes = 1 + children − pruned`), `work_policy` (of the last run). `canon_symmetry.stats`: `considered`, `rejected`, `not_fixing`, `inserted` (enlargements of `A_known`, at most `log₂|A_known|`), `rebases`, `orbit_calls`. Measured values are in `docs/slices/S7-notes.md`.

## 9. Not implemented

- The §7.3 root shortcut ("If every generator fixes x, the unsigned orbit is a singleton and returning x is equivalent to P1 regardless of trace"; §11.1: "the §7.3 root shortcuts are part of profile P1's policy and visit no tree nodes"). Neither policy takes it: `0x0001` runs the whole reference tree as since S1, and when every generator fixes `x`, `0x0002` has `A_known = G` and explores the tree pruned by it (at least the root and one node per further depth), not zero nodes. `0x0002` must not be read as that shortcut. How the shortcut relates to the `0x0001` count is recorded as an open point in `docs/slices/S7-notes.md`.
- Source (ii), implicit automorphisms from equal leaf keys (S7b).
- Certificates of pruned children and the independent checker (S7 step 2).
- Trace-prefix domination, bound pruning and exact state equality (§7.3, later).
- Parallel modes (S8 ties their count to the sequential one).
