# Pruning rules of the P1 canonical-image search

Status: slice S7 (12 October 2026). Normative text: `docs/specification.md` v2.1, §7.1–7.3, §8.2, §11.1, §14.3 (R2), §20. This file states every prune the engine applies to profile P1, with its coverage lemma, its objective scope and its quota rule. The unpruned evaluator of §7.1 is normative (§7.3 "The unpruned evaluator above is normative"); a prune is admissible only with a lemma showing that the key minimum of §7.2 is unchanged.

## 1. Definitions

Let `x` be a root object on the atom domain Ω = {0, …, n−1} and `G ≤ Sym(Ω)` given by generators. The P1 tree of `(x, G)` (§7.1): a node is an ordered sequence `S = (a_1, …, a_d)` of individualised atoms; its ordered partition `π_S` is the result of the §7.1 refinement loop after individualising `a_1, …, a_d` in order from the root partition; its trace `τ_S` is the concatenation of the tokens of the nodes on the path from the root. If `π_S` is discrete the node is a leaf with full list `L_S` (the singletons in partition order), transporter `t_S = t_{L_S}` (the unique element of `G` minimising `L_S^G`, §7.2) and key `κ_S = (τ_S ‖ 00, bytes(x^{t_S}))`; otherwise its target cell is `C_S`, the `(size, position)`-least cell of size > 1, and its children are `S·b` for `b ∈ C_S`. The canonical image is the least key over all leaves, traces compared first (§7.2).

For `h ∈ G` write `S^h = (a_1^h, …, a_d^h)`.

**Tree equivariance (§7.1 last paragraph, §7.2).** For `h ∈ G`: `π_{S^h}(x^h) = π_S(x)^h` cellwise in order, `τ_{S^h}(x^h) = τ_S(x)`, `C_{S^h}(x^h) = C_S(x)^h`, and at a leaf `t_{L^h} = h^{-1} t_L`, so `(x^h)^{t_{L^h}} = x^{t_L}`. (Every stage of §7.1 is representation-independent and equivariant: the initial keys, the O-stage counts, the G-stage orbits of `G_M` pulled back through `u`, the target-cell rule and individualisation; the leaf identity is §7.2's uniqueness argument. Checked on every tier since S1.)

## 2. Rule P1-ORBIT-1: orbit pruning by prefix-fixing automorphisms

### Lemma

Let `a ∈ A = Aut_G(x)` and let `S = (a_1, …, a_d)` be a node with `a_i^a = a_i` for all `i ≤ d`. Then for every `b ∈ C_S`, the subtrees rooted at `S·b` and `S·b^a` have the same multiset of leaf keys, and for every leaf `L` below `S·b` the leaf `L^a` below `S·b^a` satisfies `t_{L^a} = a^{-1} t_L` and `x^{t_{L^a}} = x^{t_L}`.

*Proof.* Since `x^a = x`, equivariance with `h = a` maps the tree of `x` to itself: node `T ↦ T^a` with equal traces. `S^a = S` because `a` fixes the prefix pointwise, so `C_S^a = C_{S^a} = C_S`: the target cell is mapped to itself as a set and `b^a ∈ C_S`. The child `S·b` maps to `S·b^a` and, by induction on depth, every descendant of `S·b` maps to a descendant of `S·b^a` with the same trace; the map is a bijection (apply `a^{-1}`). At a leaf `L` below `S·b`, the leaf identity gives `t_{L^a} = a^{-1} t_L` and `x^{t_{L^a}} = (x^a)^{t_{L^a}} = x^{t_L}`, so the keys agree. ∎

### Rule

At a node `S`, let `A_known ≤ A` be the verified subgroup of known automorphisms (§7.3) and `A_S ≤ A_known` the pointwise stabiliser of `(a_1, …, a_d)` in `A_known` (§7.3 "At each node intersect with the stabiliser of its constraints before sibling pruning"). Partition `C_S` into `A_S`-orbits. In cell order (the order of the members inside `C_S`, which is not semantic, §7.1), a member `b` is **explored** if no earlier member of `C_S` lies in its orbit, and **pruned** otherwise, with the earlier member `b'` of its orbit as its representative. By the lemma (applied to any `a ∈ A_S` with `b'^a = b`), the explored children's leaf keys together cover every leaf key of the node's subtree, so the minimum over the explored children equals the minimum over all children. Induction over the tree gives: **the least key over the explored leaves equals the least key over all leaves of the unpruned tree.**

The engine's realisation (`src/symmetry/`, `src/search/p1_tree.c`): `A_known` is a verified stabiliser chain; `A_S` is obtained by rebasing it so that its base starts with the prefix (§9.2 base change, without verification, as a transient chain), whose level-`d` strong generators generate `A_S`; the orbits on `C_S` are a breadth-first search over those generators with a Schreier tree, from which an explicit `a ∈ A_S` with `b'^a = b` is read off as a product of generators (certificate, §4 below).

### Sources of A_known (S7)

Only **source (i)**: the input generators of `G` that fix `x`, each checked by exact action (`x^g = x` by `canon_root_act_into` and extensional equality), inserted into the verified chain unless already a member (membership by sifting; a non-member rebuilds and re-verifies the chain, §9.1 "exact verification is mandatory"). The lemma does not require `A_known = A`; any verified subgroup is sound. R2 (§14.3) forbids any contribution that is not a verified automorphism.

**Source (ii), deferred to slice S7b (not implemented).** When a leaf `L` has `κ_L = κ_best` (equal trace and equal bytes), `x^{t_L} = x^{t_best}`, so `a = t_L t_best^{-1}` satisfies `x^a = x` (§3: if `x^p = x^q` then `p q^{-1}` fixes `x`); `a` is a product of two elements of `G`, hence in `G`. After exact verification (membership and object equality, §7.3 "Verify implicit automorphisms by exact membership and object equality before publication") it may be inserted into `A_known`, and the lemma applies unchanged to the enlarged subgroup. Soundness therefore needs no new argument; what S7b must settle is bookkeeping (when inserted automorphisms may be used at the current node, and the certificate's representation of them).

### Objective scope

The rule is applied to **`CANONICAL_IMAGE` only**. It preserves the key minimum, nothing more:

- **Witness.** The least `t` over all leaves attaining the minimum key is not preserved: pruned leaves can carry smaller `t`. With `witness_mode = DETERMINISTIC` the witness is computed from the complete stabiliser (§3, S4) and is unaffected. With `witness_mode = ANY` the pruned run returns the least `t` among the **explored** leaves attaining the key, a valid witness (`x^t = c`, `t ∈ G`) that may differ from the unpruned run's; the tests compare traces and bytes in that mode and witnesses only in deterministic mode.
- **Complete stabiliser, enumeration objectives, labeling coset, signed image.** §8.2: "Image-preserving pruning need not preserve complete-group coverage"; "Keeping one best image is insufficient for full A or an enumeration stream." The §8.1 enumerations stay unpruned. The P1 runs inside `CANONICAL_LABELING_COSET` and `SIGNED_CANONICAL_IMAGE` also stay unpruned in S7 (their `t` is part of the answer and is compared with the model), under every work policy.

### Quota (§11.1 v2.1, work-policy ID)

The quota counts "the nodes of the traversal fixed by the profile's policy for the objective", selected by the **work-policy ID** of the capacity descriptor (`canon_capacity.work_policy`):

| ID | Traversal counted | Engine behaviour |
|---|---|---|
| `0x0001` `UNPRUNED` | every NODE token of the reference tree (plus §8.1 visits) | runs unpruned |
| `0x0002` `PRUNE_1` (default) | the nodes **explored** under rule P1-ORBIT-1 (plus §8.1 visits) | prunes `CANONICAL_IMAGE`; every other objective runs its reference traversal |

No count-only traversal of pruned subtrees is made (decision D9). The count is a function of the input, the objective and the descriptor: the orbits, hence the explored children, are determined by `A_known` (a function of the generators as given and `x`) and by the cell order, and by equivariance the subtrees of the children in one orbit have equal node counts, so the count does not depend on which representative is explored. Parallel modes must reproduce the sequential count (S8). Both policies report profile `0x0001`: the outputs are P1's (PROFILE-EQUIV). Unknown IDs are `UNSUPPORTED_ACTION`.

Measured on the empty subset (every generator fixes `x`, so `A_known = G`): under `Sym(2)` 3 nodes unpruned, 2 pruned; under `Sym(6)` 1237 unpruned (1 + 6 + 30 + 120 + 360 + 720), 6 pruned (one child per level, the orbit count), see `tests/c/test_prune.c`.

## 3. Rules not applied

Trace-prefix domination, bound pruning and exact state equality with transport (§7.3) are not implemented. The leaf shortcut of S2 (a leaf whose trace exceeds the incumbent's does not materialise its image) is not a prune: the leaf is visited and counted; only the byte comparison it cannot win is skipped (§7.2: trace compared first).

## 4. Checker rule

Certificate v0 (`src/search/certificate.h`, `checker/README.md`) records, for every pruned child `b`, its representative `b'` and an index into the certificate's automorphism table. The checker (`checker/cc_check.c`, rule 5) verifies that `b'` is an explored child of the same node, that the named automorphism `a` is a member of `G` fixing `x` (rule 2, by its own sift and action), that `a` fixes the node's prefix pointwise and that `b'^a = b`. The lemma then applies with that `a`, so the checker's minimum over the explored leaves is the canonical image. The checker does not need `A_known`, the orbit computation or the chain of `A_S`: one automorphism per pruned child suffices.
