# Pruning rules of the P1 canonical-image search

Status: slices S7 (12 October 2026) and S7b (13 October 2026). Normative text: `docs/specification.md` v2.1, §7.1–7.3, §8.2, §11.1, §14.3 (R2), §20. This file states every prune the engine applies to profile P1, with its coverage lemma, its objective scope and its quota rule. The unpruned evaluator of §7.1 is normative (§7.3 "The unpruned evaluator above is normative"); a prune is admissible only with a lemma showing that the key minimum of §7.2 is unchanged.

## 1. Definitions

Let `x` be a root object on the atom domain Ω = {0, …, n−1} and `G ≤ Sym(Ω)` given by generators. The P1 tree of `(x, G)` (§7.1): a node is an ordered sequence `S = (a_1, …, a_d)` of individualised atoms; its ordered partition `π_S` is the result of the §7.1 refinement loop after individualising `a_1, …, a_d` in order from the root partition; its trace `τ_S` is the concatenation of the tokens of the nodes on the path from the root. If `π_S` is discrete the node is a leaf with full list `L_S` (the singletons in partition order), transporter `t_S = t_{L_S}` (the unique element of `G` minimising `L_S^G`, §7.2) and key `κ_S = (τ_S ‖ 00, bytes(x^{t_S}))`; otherwise its target cell is `C_S`, the `(size, position)`-least cell of size > 1, and its children are `S·b` for `b ∈ C_S`. The canonical image is the least key over all leaves, traces compared first (§7.2).

For `h ∈ G` write `S^h = (a_1^h, …, a_d^h)`.

**Tree equivariance (§7.1 last paragraph, §7.2).** For `h ∈ G`: `π_{S^h}(x^h) = π_S(x)^h` cellwise in order, `τ_{S^h}(x^h) = τ_S(x)`, `C_{S^h}(x^h) = C_S(x)^h`, and at a leaf `t_{L^h} = h^{-1} t_L`, so `(x^h)^{t_{L^h}} = x^{t_L}`. (Every stage of §7.1 is representation-independent and equivariant: the initial keys, the O-stage counts, the G-stage orbits of `G_M` pulled back through `u`, the target-cell rule and individualisation; the leaf identity is §7.2's uniqueness argument. Checked on every tier since S1.)

## 2. Rule P1-ORBIT-1: orbit pruning by prefix-fixing automorphisms

### Lemma

Let `a ∈ A = Aut_G(x)` and let `S = (a_1, …, a_d)` be a node with `a_i^a = a_i` for all `i ≤ d`. Then for every `b ∈ C_S`, the subtrees rooted at `S·b` and `S·b^a` have the same multiset of leaf keys, and for every leaf `L` below `S·b` the leaf `L^a` below `S·b^a` satisfies `t_{L^a} = a^{-1} t_L` and `x^{t_{L^a}} = x^{t_L}`.

*Proof.* Since `x^a = x`, equivariance with `h = a` maps the tree of `x` to itself: node `T ↦ T^a` with equal traces. `S^a = S` because `a` fixes the prefix pointwise, so `C_S^a = C_{S^a} = C_S`: the target cell is mapped to itself as a set and `b^a ∈ C_S`. The child `S·b` maps to `S·b^a` and, by induction on depth, every descendant of `S·b` maps to a descendant of `S·b^a` with the same trace; the map is a bijection (apply `a^{-1}`). At a leaf `L` below `S·b`, the leaf identity gives `t_{L^a} = a^{-1} t_L` and `x^{t_{L^a}} = (x^a)^{t_{L^a}} = x^{t_L}`, so the keys agree. ∎

### Rule

At a node `S`, let `A_known ≤ A` be the verified subgroup of known automorphisms (§7.3) and `A_S ≤ A_known` the pointwise stabiliser of `(a_1, …, a_d)` in `A_known` (§7.3 "At each node intersect with the stabiliser of its constraints before sibling pruning"). The children are decided in cell order (the order of the members inside `C_S`, which is not semantic, §7.1). A member `b` is **pruned** if, at the time it is decided, some already **explored** child `b'` of the same node lies in its `A_S`-orbit (then `b'` is its representative); otherwise it is explored. By the lemma (applied to any `a ∈ A_S` with `b'^a = b`), the pruned child's leaf keys are those of its representative's subtree, so the explored children's leaf keys together cover every leaf key of the node's subtree and the minimum over the explored children equals the minimum over all children. Induction over the tree gives: **the least key over the explored leaves equals the least key over all leaves of the unpruned tree.**

Two forms of the rule are in use, distinguished by work-policy ID (below):

- **P1-ORBIT-1 (policy `0x0002`, S7):** `A_known` is fixed before the search (source (i) only), so `A_S` is fixed at a node and every decision can be taken at once: the first member of each orbit in cell order is explored, the others are pruned with it as representative.
- **P1-ORBIT-1 incremental (policy `0x0003`, S7b):** `A_known` may grow during the search (sources (i) and (ii)). When an undecided member `j` is reached, `A_S` is recomputed if `A_known` grew since the node's last orbit computation; then, first, the orbits of every explored child under the current `A_S` are marked and every undecided member reached, `j` included, is pruned with that child as representative; if `j` is still undecided it is explored, and the members of its orbit that are still undecided are pruned with `j` as representative. (Review of 19 October 2026: the S7b realisation marked `j` as a representative before the explored children's orbits, so a `j` that the enlarged `A_S` had moved into an explored sibling's orbit was explored, duplicating that sibling's subtree; and it read the orbits from the transient stabiliser of the last descendant's longer prefix, a subgroup of `A_S`. Both made the explored tree larger than this rule names; `docs/slices/S7b-notes.md`.) Children pruned earlier keep their representative (an explored child of the same node, so the lemma still applies); `A_S` only grows at a node. An automorphism found at a leaf below a child therefore prunes the later children of every ancestor node.

The engine's realisation (`src/symmetry/`, `src/search/p1_tree.c`): `A_known` is a verified stabiliser chain; `A_S` is obtained by rebasing it so that its base starts with the prefix (§9.2 base change, without verification, as a transient chain), whose level-`d` strong generators generate `A_S` (one transient chain serves every depth, so a node recomputes `A_S` before a decision whenever a child visit has replaced it, not only when `A_known` grew); the orbits on `C_S` are a breadth-first search over those generators from the representatives with a Schreier tree, from which an explicit `a ∈ A_S` with `b'^a = b` is read off as a product of generators at the moment of the decision (certificate, §4 below).

### Sources of A_known

**Source (i) (S7):** the input generators of `G` that fix `x`, each checked by exact action (`x^g = x` by `canon_root_act_into` and extensional equality), inserted into the verified chain unless already a member (membership by sifting; a non-member rebuilds and re-verifies the chain, §9.1 "exact verification is mandatory"). The lemma does not require `A_known = A`; any verified subgroup is sound. R2 (§14.3) forbids any contribution that is not a verified automorphism.

**Source (ii) (S7b, policy `0x0003` only):** when a leaf `L` has `κ_L = κ_best` (equal trace and equal bytes), `x^{t_L} = x^{t_best}` because the encoding is injective (§4.2), so `a = t_L t_best^{-1}` satisfies `x^a = x` (§3: if `x^p = x^q` then `p q^{-1}` fixes `x`); `a` is a product of two elements of `G`, hence in `G`. Following §7.3 "Verify implicit automorphisms by exact membership and object equality before publication", the engine nevertheless verifies both (the group's membership test; exact action and extensional equality) before inserting `a` into `A_known`; a failure of either would be an engine error and is reported as `INTERNAL_ERROR`, never skipped. The lemma applies unchanged to the enlarged subgroup. The certificate lists the inserted implicit automorphisms like the explicit ones; the checker verifies every listed automorphism itself, so it does not distinguish the sources.

### Objective scope

The rule is applied to **`CANONICAL_IMAGE` only**. It preserves the key minimum, nothing more:

- **Witness.** The least `t` over all leaves attaining the minimum key is not preserved: pruned leaves can carry smaller `t`. With `witness_mode = DETERMINISTIC` the witness is computed from the complete stabiliser (§3, S4) and is unaffected. With `witness_mode = ANY` the pruned run returns the least `t` among the **explored** leaves attaining the key, a valid witness (`x^t = c`, `t ∈ G`) that may differ from the unpruned run's; the tests compare traces and bytes in that mode and witnesses only in deterministic mode.
- **Complete stabiliser, enumeration objectives, labeling coset, signed image.** §8.2: "Image-preserving pruning need not preserve complete-group coverage"; "Keeping one best image is insufficient for full A or an enumeration stream." The §8.1 enumerations stay unpruned. The P1 runs inside `CANONICAL_LABELING_COSET` and `SIGNED_CANONICAL_IMAGE` also stay unpruned in S7 (their `t` is part of the answer and is compared with the model), under every work policy.

### Quota (§11.1 v2.1, work-policy ID)

The quota counts "the nodes of the traversal fixed by the profile's policy for the objective", selected by the **work-policy ID** of the capacity descriptor (`canon_capacity.work_policy`):

| ID | Traversal counted | Engine behaviour |
|---|---|---|
| `0x0001` `UNPRUNED` | every NODE token of the reference tree (plus §8.1 visits) | runs unpruned |
| `0x0002` `PRUNE_1` | the nodes **explored** under rule P1-ORBIT-1 with source (i) (plus §8.1 visits) | prunes `CANONICAL_IMAGE`; every other objective runs its reference traversal |
| `0x0003` `PRUNE_2` (default since S7b) | the nodes explored under the incremental rule with sources (i) and (ii) (plus §8.1 visits) | as `0x0002`, with implicit automorphisms inserted and used |

No count-only traversal of pruned subtrees is made (decision D9). The count is a function of the input, the objective and the descriptor: under `0x0002` the orbits, hence the explored children, are determined by `A_known` (a function of the generators as given and `x`) and by the cell order, and by equivariance the subtrees of the children in one orbit have equal node counts, so the count does not depend on which representative is explored; under `0x0003` the fixed sequential order also fixes which leaves are met first and hence which automorphisms are discovered when. Parallel modes must reproduce the sequential count (S8). All policies report profile `0x0001`: the outputs are P1's (PROFILE-EQUIV). Unknown IDs are `UNSUPPORTED_ACTION`.

Measured (`tests/c/test_prune.c`): on the empty subset every generator fixes `x`, so `A_known = G` from the start and the two prunes agree: under `Sym(2)` 3 nodes unpruned, 2 pruned; under `Sym(6)` 1237 unpruned (1 + 6 + 30 + 120 + 360 + 720), 6 pruned (one child per level, the orbit count). On `{0}` under `Sym(4) = ⟨(0 1 2 3), (0 1)⟩` no generator fixes `x`: 10 nodes unpruned and under `0x0002`; under `0x0003` the second leaf below the root's first child yields `(2 3)`, which prunes the root's third child, and the first leaf below the second child yields `(1 2)`, which puts that node's second child `3` into the orbit of its explored child `1` under `A_S = ⟨(1 3)⟩`: 6 nodes, `A_known = Sym({1, 2, 3})` (7 before the review of 19 October 2026). On `{0, 1, 3, 4}` under `⟨(1 3 4), (0 3 4 2)⟩ = Sym(5)`, where `A_known = ⟨(1 3 4)⟩` under `0x0002`: 41 nodes unpruned, 15 under `0x0002` (the root's orbits `{0}` and `{1, 3, 4}`), 8 under `0x0003`.

## 3. Rules not applied

Trace-prefix domination, bound pruning and exact state equality with transport (§7.3) are not implemented. The leaf shortcut of S2 (a leaf whose trace exceeds the incumbent's does not materialise its image) is not a prune: the leaf is visited and counted; only the byte comparison it cannot win is skipped (§7.2: trace compared first).

## 4. Checker rule

Certificate v0 (`src/search/certificate.h`, `checker/README.md`) records, for every pruned child `b`, its representative `b'` and an index into the certificate's automorphism table. The checker (`checker/cc_check.c`, rule 5) verifies that `b'` is an explored child of the same node, that the named automorphism `a` is a member of `G` fixing `x` (rule 2, by its own sift and action), that `a` fixes the node's prefix pointwise and that `b'^a = b`. The lemma then applies with that `a`, so the checker's minimum over the explored leaves is the canonical image. The checker does not need `A_known`, the orbit computation, the chain of `A_S` or the source of an automorphism: one automorphism per pruned child suffices, under either pruning policy. What the checker does not re-derive is which of the two pruning policies produced the tree: it accepts a header naming `0x0002` or `0x0003` for any correctly justified pruned tree, and checks the node count against the records and, for `0x0001`, that nothing was pruned.
