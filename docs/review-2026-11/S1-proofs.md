# S1-proofs: analytic correctness review

## 1. Scope

Read `docs/review-2026-11/BRIEF.md`, `CLAUDE.md`, `README.md`, and `HANDOFF.md`; reviewed normative `docs/specification.md` v2.2 §§3–4, 5–8, relevant parts of §§9, 11.1, 14 and 20; read all of `docs/pruning-rules.md`. Consulted `docs/slices/S4.md`, `S4-notes.md`, the D1 discussion in `M0a-notes.md`, relevant portions of `docs/detailed-implementation-plan.md`, and `checker/README.md` to identify the claims being used.

Glanced at `src/coset/{coset.h,least.c}`, `src/search/{p1_tree.c,objectives.c}`, `src/symmetry/symmetry.c`, `src/partition/partition.c`, and relevant checker policy/count conditions solely to confirm the mathematical interpretation. This is not an implementation, memory-safety, or performance audit. Exact group construction, chain verification, adapter action/equality, and canonical encoding are hypotheses here, not independently proved C implementations. The encoding argument is used subject to §4.2's stated group-leaf hypothesis.

**Evidence method:** all examples and proofs below are written derivations. No builds, snippets, CLI invocations, test suites, reference runs, benchmarks, downloads, or background jobs were run. The only file written is this report. No subagents were launched. Following the maintainer's time-box instruction, no further investigation was started after that instruction; this report records the completed derivations.

**Overall result:** the core P1 canonical-image proof, objective-specific coverage proofs, automorphism pruning lemma, signed zero/nonzero criterion, lifted-character validation, and D1 kernel/coset construction are sound under the hypotheses made explicit below. The main gaps concern the stronger conclusions attached to work-policy counts and their certification, not membership of the returned image in the orbit or invariance of complete image bytes.

## 2. Findings

### S1-proofs-01 — Source-(i) pruning gives presentation-dependent counts and quota outcomes

**Severity:** medium. **Category:** contract.

**Evidence.** `docs/pruning-rules.md:34` defines source (i) as the input generators fixing `x`. Its count argument at `docs/pruning-rules.md:55` explicitly calls the resulting subgroup “a function of the generators as given and x.” This is enough for repeatability on one generator list, but cannot establish representation independence of the quota outcome. Compare §5's “This excludes SGS/base choice, generator order … from mathematical choices” (`docs/specification.md:164–166`) and §11.1's “CAPACITY_LIMIT, a function of semantic input, objective and descriptor” (`docs/specification.md:365`). The work-quota paragraph itself says “input” rather than “semantic input” (`docs/specification.md:367`); the intended scope of representation independence therefore needs an explicit decision, as recorded also in §3 of this report.

Here is a complete analytic counterexample to a presentation-independent work-count claim. Use degree 3, right actions, and

```
x = {0},  G = Sym(3),
c = [1,2,0] = (0 1 2),
s = [1,0,2] = (0 1),
q = [0,2,1] = (1 2) = c^-1 s c.
S = [c,s],        S' = [c,s,q].
```

Both lists generate exactly the same group. Neither `c` nor `s` fixes `x`, so source (i) gives `B=1` for `S`. For `S'` it gives `B=<q>`. Neither presentation qualifies for the all-generators-fix shortcut.

P1's root partition is `[{1,2},{0}]`. O does nothing. In G, `F=(0)`, `M=(0)`, and the point stabiliser of 0 has orbits `{0}` and `{1,2}`, so no split occurs. There are exactly two children, individualising 1 and 2; each is discrete. The unpruned tree has three nodes. Its leaf lists are `(1,2,0)` and `(2,1,0)`, with respective transporters `[2,0,1]` and `[2,1,0]`; both give image `{2}` and equal traces.

Under policy `0x0002`, `S` explores all three nodes. For `S'`, `q` identifies the root's two children, so exactly two nodes are explored. With `witness_mode=ANY`, quota 2, and other capacities sufficiently generous, these are respectively `CAPACITY_LIMIT` and `COMPLETE`. Policy `0x0003` has the same difference on this example: for `S`, the implicit `q` is discovered only at the second and final leaf, too late to remove either visit; for `S'` it is available initially.

The code confirms the stated reliance rather than supplying this proof: `src/search/p1_tree.c:480–518` takes the fixing input generators; `src/search/p1_tree.c:360–366` charges explored nodes. This example was **reasoned, not run**.

This does **not** refute PROFILE-EQUIV for complete canonical bytes. It refutes extending that result to presentation-independent explored counts or quota statuses. It also extends beyond HANDOFF's existing memory-admission question: the difference here persists with unlimited memory and comes from the search-count definition itself.

**Recommendation.** Decide whether pruned quota outcomes may depend on the supplied presentation. If yes, explicitly distinguish presentation-dependent work quotas from semantic capacity guarantees. If no, use a presentation-independent source of pruning automorphisms, or charge a separately specified semantic traversal. Merely sorting the supplied generators cannot fix redundancy dependence.

**Remediation plan.** Amend §5/§11.1 and `docs/pruning-rules.md` §§2/Quota together after the contract decision; update public capacity documentation and `root_automorphisms`/quota accounting only if semantic invariance is required. Future regression evidence should pin the above two presentations at quota boundaries 2 and 3 under both pruned policies, and verify identical complete bytes with generous quotas. No such tests were run here. Documentation-only clarification is low risk, hours; changing the count/source is higher risk, days or more, and requires deciding whether a new work-policy ID is necessary.

### S1-proofs-02 — CERT-0 proves image coverage and record count, not the named policy's count

**Severity:** medium. **Category:** contract.

**Evidence.** `docs/pruning-rules.md:65` expressly says that the checker accepts either pruned policy for any correctly justified pruned tree. `checker/README.md:62,66–68` specifies the same restriction. This is compatible with the image-preservation lemma, but weaker than §11.1's “nodes of the traversal fixed by the profile's policy” and “must give the same count in every execution mode and worker count” (`docs/specification.md:367`). Counting the records is not proving that those are exactly the records that the named policy would produce.

Use the presentation `S=[c,s]` from finding 01. Policy `0x0002` has `B=1` and must explore three nodes. Nevertheless, the checker's stated rules permit a two-node tree that explores child 1 and prunes child 2 with the automorphism `q=c^-1 s c`. All image-coverage obligations hold: `q` is in `G`, fixes `x`, fixes the empty root prefix, and maps 1 to 2. The automorphism table need not prove that `q` came from source (i). The final count 2 equals the number of records. Thus those rules cannot distinguish this derivation from the prescribed two-node tree for presentation `S'`.

Conversely, a fully unpruned three-node proof can name policy `0x0002` even for `S'`, where the prescribed count is two. Both trees establish the same canonical image; neither example is a false image certificate. These are **analytic countermodels to the stronger count-certification claim**, not generated or executed certificate files. The limited code glance agrees: `checker/cc_check.c:639–647` compares the final count with `node_records` and separately forbids pruning only under `0x0001`.

**Recommendation.** State explicitly that CERT-0 validates the canonical image, explored witness, and record count, while the policy label and exact policy count remain producer assertions. If the policy count is intended to be checked, add a separate obligation: reconstruct source (i) and its orbit representatives for `0x0002`; for `0x0003`, also pin/replay child order and the chronology of eligible implicit discoveries. A final table of valid automorphisms alone is insufficient.

**Remediation plan.** Clarify `checker/README.md`, `docs/pruning-rules.md:65`, and the result/evidence documentation first. A stronger certificate version would change producer records and `checker/cc_check.c` and needs mutation cases that introduce an unavailable automorphism, skip a required representative, or relabel a valid tree with an incompatible policy. No tests were run. Documentation clarification is low risk, hours; exact policy replay is moderate/high risk, several days or more, particularly for incremental discovery. The existing image soundness rule should be retained.

## 3. Spec ambiguities and limits of the statements

1. **Semantic versus presented input for quota acceptance.** §11.1 says “CAPACITY_LIMIT, a function of semantic input, objective and descriptor” (`docs/specification.md:365`), then “The count is a function of input, objective and descriptor” (`:367`). `docs/pruning-rules.md:55` expressly includes the generators “as given.” These can describe different acceptance contracts; finding 01 shows a concrete consequence. This report does not choose the intended contract.

2. **Fixed sequential order for policy `0x0003`.** §7.1 says “Internal order of members within a cell is not semantic” (`docs/specification.md:182`), and “Raw atom IDs may choose visitation order only” (`:215`). The incremental count argument says “the fixed sequential order also fixes which leaves are met first” (`docs/pruning-rules.md:55`). The latter needs an actual rule fixing that order, or an explicit dependence on implementation/presentation order. The image proof allows every cell-member order; that does not by itself freeze the chronology needed for a cross-implementation count. The current implementation retains member order during individualisation (`src/partition/partition.c:128–142`), but code is not a normative mathematical definition of this policy.

3. **Shortcuts and which traversal is charged.** §7.3 says returning `x` for a singleton orbit “is equivalent to P1 regardless of trace” (`docs/specification.md:242`), while §11.1 says “the §7.3 root shortcuts are part of profile P1's policy and visit no tree nodes” (`:367`). The orbit-pruning examples include an actual P1 traversal of the empty subset under `Sym(6)` (`docs/pruning-rules.md:57`), although that input has singleton unsigned orbit. The image equivalence is unambiguous; whether a shortcut must be taken, may be taken, or has an objective/evidence-dependent trigger needs a precise work-policy rule before a universal count claim follows. I have not assumed one reading to reject those example counts.

4. **Transformed labeling representative versus deterministic least representative.** §3.1 says, for a source rename `μ`, “output μ⁻¹λ” (`docs/specification.md:88`), while §3 explicitly warns “Witness choice is not equivariant under input automorphisms” (`:78`) and defines deterministic `λ₀=min Aλ` (`:80`). The whole coset always transforms as `μ⁻¹(Aλ)`, and `μ⁻¹λ` is a valid representative. Taking the image-array least element is not generally equivariant under reordering source coordinates. The wording should distinguish transporting a valid representative from promising that the newly minimised representative is its transport; no such stronger promise is used in the proofs below.

5. **“Complete A” on a deterministic zero.** §3 says “The deterministic mode thus needs the complete stabiliser A” (`docs/specification.md:80`). D1's enumeration stores the complete even subgroup `A⁺` plus an odd element `a`, not necessarily a separately materialised chain for `A` (`HANDOFF.md:100`; `src/search/objectives.c:192–197`). Mathematically this is complete information because `A=A⁺ ⊔ A⁺a`; it must not be described as a chain for `A⁺` alone establishing completeness. The derivation below makes the sufficiency precise.

## 4. Things checked and found sound: claims and written proofs

Throughout, permutations act on the right, `v^(pq)=(v^p)^q`, and `Hr={hr:h∈H}`. All minima use the named order, exact finite groups, exact actions/equality, and deterministic injective canonical encodings where bytes are compared. “Complete” means exhaustion or a separately sufficient certificate, never merely a verified chain for discovered generators.

### 4.1 P1 G-stage normalisation, equivariance, and representation independence

**Claim** (§7.1, `docs/specification.md:197–215`). For ordered singleton list `F`, let `M=min F^G`. Any `u∈G` with `F^u=M` gives the same ordered orbit signature after pulling back the ordered orbits of the pointwise stabiliser `G_M`. This construction is equivariant under `h∈G`.

**Proof.** If `F^u=F^v=M`, then

`M^(u^-1 v)=F^v=M`,

so `k=u^-1v∈G_M` and `v=uk`. For every atom `z`, `z^v=(z^u)^k`, so `z^u` and `z^v` belong to the same `G_M` orbit. Every element of a group preserves each of its orbits setwise. The target orbit sets, sorted as increasing lists and then lexicographically, therefore give identical signature indices for `z`, independently of `u`.

For input transported by `h`, the singleton list is `F^h`. Since `hG=G`, its least orbit image is the same `M`. A valid transporter is `h^-1u`, and

`(z^h)^(h^-1u)=z^u`.

Thus signatures on transported atoms coincide. O-stage signatures likewise agree: the action maps arcs, colours and ordered cell sets bijectively, so every incoming/outgoing multiplicity into the corresponding cell is unchanged. Their entry snapshots matter: all signatures refer to the same cell sets. Initial membership/colour keys, ordered splitting, target `(size,position)`, and individualisation all commute with transport. Singleton order must be partition order as specified, not branch order.

Every mathematical quantity here depends on the semantic group, graph and ordered cell sets. Generating sets, their order, SGS base, hash iteration, and within-cell storage order do not choose signatures or cell order. A partial tuple can have several minimising transporters, but the preceding argument removes this choice. This proves independence of the *unpruned semantic tree*, traces and leaf-key multiset. It does not prove independence of a traversal's encounter order or the pruned node counts in findings 01–02.

### 4.2 Leaf map, constructive minimisation, and canonical-image correctness

**Claim** (§7.2, `docs/specification.md:230–234`). A full singleton list `L` has a unique `t_L∈G` minimising `L^G`; `t_(L^h)=h^-1t_L`. The least leaf key returns an in-orbit, G-invariant canonical image.

**Proof of the greedy leaf procedure.** Maintain the possible permutations giving the already minimised prefix as the right coset `tH`, with `H` fixing those attained target values pointwise. Initially this is `G`. At the next entry set `a=L[i]^t`; possible next values are `a^H`. Choose their minimum `b` and `u∈H` sending `a` to `b`. Then

`{t h : h∈H, a^h=b} = t H_a u = t u H_b`.

The second equality uses `u^-1H_a u=H_b`. Thus the update `t←tu`, `H←H_b` preserves exactly the possible completions of the new minimum prefix. Induction proves lexicographic optimality. At the end `L` contains every atom once, so its image determines the permutation uniquely; all intermediate choices produce the same `t_L`.

For `h∈G`, `(L^h)^G=L^G`. The map `g↦h^-1g` is a bijection between transporters from `L` and from `L^h` to the same least full list, giving the identity by uniqueness. Tree equivariance from §4.1 matches leaves with the same complete trace, and

`(x^h)^(h^-1t_L)=x^t_L`.

Hence corresponding keys agree, so their minima agree. Each selected image is explicitly `x^t` with `t∈G`, proving orbit membership. If the least key is attained several times, its image bytes are identical; injectivity gives one extensional image. Idempotence follows from orbit membership and invariance: `C(C(x))=C(x)`. Within one fixed action/group/context, equal canonical bytes imply orbit equivalence since `x^p=y^q` gives `y=x^(p q^-1)`; the converse is invariance.

**Termination and size.** Splits never merge cells. At a node there are at most `n-c_initial` net cell-count increases and one final no-change sweep. Along a path every individualisation isolates a new atom; for `n>0` at most `n-1` are needed. Each branch fixes another distinct atom, and the completion permutations of different leaf prefixes are disjoint nonempty sets, bounding the leaves by `n!`. For `n=0`, the empty partition is discrete, the full-list action is uniquely the identity, and there is one leaf. All these statements assume the group/object primitives themselves terminate.

### 4.3 Coset splitting and every §8 consumer

**Claim** (§8.1, `docs/specification.md:272–281`). If `a` is moved by nontrivial `H`, and `a^t_b=b` with `t_b∈H`, then

`Hr = disjoint_union_(b∈a^H) H_a t_b r`.

**Proof.** For `h∈H`, put `b=a^h`. Then `h t_b^-1∈H_a`, so `h∈H_a t_b`. Conversely every element of this coset sends `a` to `b`. Different `b` give disjoint cosets, and multiplying on the right by `r` preserves disjointness and coverage. The orbit has at least two points, so `|H_a|<|H|`. Each successive moved point is distinct from those already fixed, bounding recursion depth by `n`. Trivial subgroups give singleton regions. Starting at `G` therefore consumes each element of `G` exactly once.

Coverage needs no least-transporter assumption. Choosing least `t_b` fixes the specified reference traversal and budgets independently of group presentation, provided those least elements are actually computed exactly. Every nonleaf has at least two children, so a fully exhausted enumeration with `|G|` leaves has at most `2|G|-1` visits. This count includes internal visits, not just `consume` calls.

**Consumers** (§8.2, `docs/specification.md:287–296`). Exhausting all elements and taking the least exact key proves the minimum. A positive transporter is certified by one exact membership/action witness; a negative one needs exhaustion. Stabiliser hits are exactly `A`: the generated subgroup is contained in `A` because all hits fix `x`, and contains all of `A` after exhaustion because every element was a hit or already in the generated subgroup. A verified chain before exhaustion proves only its generated subgroup.

If `x^g=y`, then

`x^r=y  iff  x^(r g^-1)=x  iff  r∈Ag`.

Thus one hit plus complete `A` proves the entire transporter coset. An arbitrary Boolean predicate has no closure property; exact once-only enumeration and exhaustion establish its solution stream, and stopping at a hit establishes only existence. Different root shapes cannot become equal under the stated shape-preserving action, so they correctly give an empty transporter result on exhaustion.

The additional internal services follow the same coverage argument: enumerate `H` for intersection with `K`; enumerate ambient `G` and test exact conjugation equality for normalisers/conjugacy; when `H^g=K`, every other solution `r` has `r g^-1∈N_G(H)`, giving `N_G(H)g`. These are sound reference semantics, not fast algorithms.

### 4.4 Constrained least-element descent and least outside element

**Claim** (§9.4, `docs/specification.md:345–347`; explicit procedure confirmed at `src/coset/least.c:6–16,215–240`). For `C=Jr`, applying all prescribed point images first and then greedily minimising remaining images gives exactly the least admissible element, or proves emptiness.

**Proof.** At any step with current coset `J'r'`, possible images of `v` are `(v^J')^r'`. For requested image `c`, set `o=c^(r'^-1)`. If `o∉v^J'`, there is no solution. Otherwise choose `t∈J'` with `v^t=o`. Then

`{h r' : h∈J', v^(h r')=c} = J'_v t r'`.

Indeed `h r'` has the requested image exactly when `h t^-1` fixes `v`. Updating `J'←J'_v`, `r'←t r'` therefore retains exactly the satisfying permutations. Previously constrained source points are fixed by `J'`, so their images remain fixed. Repeating for every constraint either proves emptiness at a step or obtains exactly the nonempty admissible subcoset.

Only then choose the least possible image successively at source points `0,1,…,n-1`. Each branch is nonempty by its orbit test, and all original constraints are already enforced. Lexicographic induction proves the least element; faithfulness of the full point list makes the final representative unique. Different transporters for a restriction give the same restricted coset and final minimum. This is representation-independent for exact group primitives.

**Necessary caution.** The interleaved procedure still printed in `docs/slices/S4.md:50` is false for general constraints. For `J=Sym(3)`, `r=id`, constraint `1→0`, first greedily choosing `0→0` eliminates all admissible permutations; `[1,0,2]` is in fact admissible and least. `S4-notes.md:116–123` already acknowledges and repairs this, and the current two-pass procedure above is sound. This is an already-known stale brief statement, not a newly established implementation defect. The normative “successive point constraints” needs the invariant above to remove any ambiguity for future implementations.

For §9.4's least-outside descent, the key test is also correct:

`Jr ⊆ K  iff  r∈K and J≤K`.

The forward implication uses `r=1r∈Jr` and `(jr)r^-1=j∈K`; the reverse is closure. Exact point-image branches partition the coset, and discarding exactly the contained branches retains every element outside `K`. Selecting the least surviving image at each position therefore gives the least outside element. This justifies the canonical greedy generator sequence and its dependence only on `H`, conditional on the exact containment operations.

### 4.5 Labeling cosets and deterministic image witnesses

**Claim** (§3.1 and §8.2, `docs/specification.md:86–88,291`). With `Λ=Gρ`, `G'=ρ^-1Gρ` and `x'=x^ρ`, a target solution `t∈G'` reconstructs to `λ=ρt∈Λ`, `c=x^λ`; all admissible labelings to `c` form precisely `Aλ`.

**Proof.** Write `t=ρ^-1gρ`. Then `ρt=gρ`, so the typing and admissibility hold. For any other admissible `κ` reaching `c`, `κλ^-1∈G` and `x^(κλ^-1)=x`, so `κ∈Aλ`. Every `aλ`, `a∈A`, conversely reaches `c`. On target coordinates `A'=ρ^-1Aρ`, so source reconstruction is `A=ρA'ρ^-1`, as specified.

For `ρ_new=kρ`, `k∈G`, the target group is unchanged and `x^ρ_new=(x^ρ)^(ρ^-1kρ)` lies in the same target orbit. P1 invariance gives the same `c`, and `Aλ` is the same set because it is characterised as all admissible labelings reaching that `c`. Under source rename `μ`, the complete set is `μ^-1(Aλ)` with source stabiliser `μ^-1Aμ`. This proves equivariance of the complete coset, with the representative qualification in §3 above.

The identical transporter argument proves that all image witnesses for any chosen `c=x^t` form `At`. Complete `A` plus exact coset minimisation therefore gives the deterministic witness. There is also a useful missing explicit lemma connecting this to P1 leaves: fix one minimum-key leaf `L`. For every `a∈A`, the leaf `L^a` has the same key and witness `a^-1t_L`. Since `a^-1` ranges over `A`, these leaves realise every element of `At_L`. Every other witness reaching `c` lies in that coset. Thus the least witness over all minimum-key leaves of the **unpruned** tree really equals `min At_L`; it is not merely an empirical coincidence. Pruning need not retain all those leaves.

### 4.6 P1-ORBIT-1: exact scope of the coverage lemma

**Claim** (`docs/pruning-rules.md:17–23`). If `a∈Aut_G(x)` fixes the node prefix `S` pointwise, the unpruned subtrees at `S·b` and `S·b^a` have identical multisets of complete leaf keys.

**Proof.** Equivariance identifies the tree of `x^a=x` with itself by `T↦T^a`. Since `S^a=S`, it preserves the refined partition at `S` cellwise and its target cell set. It maps the first child to the second and maps all descendants recursively, preserving traces. Its inverse is the map induced by `a^-1`, so this is a bijection with multiplicity. At each leaf,

`t_(L^a)=a^-1t_L`, and `x^(a^-1t_L)=x^t_L`.

Thus full keys agree. Fixing the prefix is sufficient even when refinement adds singleton atoms: equivariance forces preservation of the entire refined partition and hence of every singleton. Fixing the prefix is essential; an arbitrary root automorphism can move the node to a different node and does not justify a prune within that node.

For any verified subgroup `B≤A`, use the true pointwise stabiliser `B_S`. The statement needs neither `B=A` nor a complete stabiliser of `x`. Input generators individually checked to fix `x` generate such a subgroup because stabilisers are subgroups. Taking only the *generators* that fix the prefix is a potentially smaller, still sound subgroup, but can miss products fixing the prefix and therefore fails to reproduce a count policy requiring the full `B_S`. A correct rebase generates the actual pointwise stabiliser.

The explored children need only cover each **key value**, not preserve multiplicities of the original leaf multiset. For each omitted child, retain a direct representative child and a valid prefix-fixing map. Structural induction on remaining tree height gives equality of the minimum: explored children's recursive searches preserve their unpruned minima, and every pruned child's unpruned minimum is one of these retained minima. This proves image canonicality. It proves neither full stabiliser discovery nor all witnesses nor an enumeration stream.

### 4.7 Implicit automorphisms and incremental reuse at ancestors

**Claim** (`docs/pruning-rules.md:28,36`). Equal leaf keys justify adding `a=t_L t_best^-1` to the known automorphism subgroup, and the enlarged subgroup can be used at every ancestor after intersecting with that ancestor's prefix stabiliser.

**Proof.** Equal bytes, by injectivity, give `x^t_L=x^t_best`. Acting by `t_best^-1` yields `x^(t_L t_best^-1)=x`. Both factors lie in `G`, so their product lies in `A`. The multiplication order is correct in this right action; the reversed product is not justified. Equal traces are unnecessary for the automorphism identity itself, although the policy deliberately requires equality of the whole key. Exact membership/action verification is a valid additional implementation check.

Old automorphisms remain valid even after the incumbent changes. If the known subgroups grow `B≤B'`, then for every fixed prefix `S`, `B_S≤B'_S`. Earlier prune witnesses remain valid; the ancestor's current orbits can only merge. A newly found automorphism can affect an ancestor's later children via the new pointwise stabiliser, possibly through products with previously known automorphisms. It does **not** necessarily fix that ancestor's prefix itself, nor necessarily merge any new child orbits. The sentence “therefore prunes the later children of every ancestor node” (`docs/pruning-rules.md:28`) needs this qualification: it enables reconsideration at every ancestor; actual pruning is conditional on an orbit connection.

Incremental coverage does not require an explored representative to have used the same subgroup as the skipped child would have used. It requires only that the representative's completed recursive search preserves its own unpruned minimum. Induction on subtree height supplies that property even if discoveries occurred inside that recursive search. Representatives must remain retained, and a failed/interrupted exploration cannot be treated as complete coverage. Pruned children point directly to explored children, so no cycle of mutually discarded siblings is introduced.

The two-pass incremental rule is consequently sound: first cover undecided children in current orbits of already explored representatives; then, if needed, select the next child and cover its current orbit. Recompute the true ancestor stabiliser after subgroup growth or replacement of shared stabiliser scratch. The code's comments at `src/search/p1_tree.c:301–320` correctly identify the stale-deeper-stabiliser case as weaker pruning/count nonconformance, rather than an image-soundness failure.

### 4.8 What the node-count arguments do and do not prove

**Fixed subgroup, policy `0x0002`.** The conclusion that the total count is independent of the representative chosen within an orbit is true, but equal *unpruned* subtree sizes alone, cited at `docs/pruning-rules.md:55`, is not a sufficient proof about recursively pruned sizes. The needed stronger lemma is:

> For fixed `B≤A` and `a∈B`, the recursively B-orbit-pruned node count at `T` equals that at `T^a`, regardless of the representatives chosen at lower nodes.

Proof by remaining height: `B_(T^a)=a^-1 B_T a`, and equivariance carries target cells and these stabiliser orbits bijectively to each other. For leaves the count is 1. For a nonleaf, define

`N_B(T)=1 + sum_(O in C_T/B_T) N_B(T·b_O)`.

The induction hypothesis makes each summand independent of `b_O` and identifies corresponding sums under `a`. This proves the lemma and representative independence. It fixes the logical gap in the abbreviated argument; it does not remove the dependence on the initial `B` shown in finding 01.

**Incremental subgroup, policy `0x0003`.** The same key-coverage proof remains true, but the preceding fixed-subgroup recurrence does not describe discovery chronology. With an explicitly fixed sequential member order, exact leaf maps, incumbent updates and immediate insertion rule, the whole sequential state transition is deterministic, so its count is a function of that fully specified starting state. This is the precise conditional content of `docs/pruning-rules.md:55`. The key-preservation lemma alone does not show order independence, schedule independence, or independence from which extra verified automorphisms a helper contributes. Parallel execution would need to reproduce the stipulated sequential discovery/decision state, or separately compute its charged count; arbitrary timely R2 contributions preserve images but do not prove equal counts. No current parallel implementation was reviewed here.

**Numerical examples.** If the empty-subset P1 tree under `Sym(n)` is actually traversed, no refinement splits the remaining nonsingleton cell before it is individualised. Its level sizes are `n!/(n-d)!` for `0≤d≤n-1`; for `n=6`, their sum is `1+6+30+120+360+720=1237`. With `B=G`, there is one stabiliser orbit on the remaining cell at each level, giving a single path of `n=6` nodes, agreeing with `docs/pruning-rules.md:57`. These are traversal counts conditional on not replacing the traversal by a root shortcut. The three-atom derivation in finding 01 separately pins a nonsingleton-orbit example without that shortcut ambiguity.

**Checker.** A verified map per omitted child is sufficient for image coverage by §4.6. No orbit calculation is needed by the checker for that conclusion. Exact named-policy counts require more, as finding 02 establishes. Deterministic witness recovery is a separate complete-stabiliser/coset computation; valid explored leaf witnesses suffice only for ANY mode. A node quota is a quota on the specified visits, not on every group calculation, rebase, verification, or Schreier generation operation.

### 4.9 Signed zero/nonzero and the §8.4 lift

**Claim** (`docs/specification.md:304–312`). For a valid character `χ:G→{±1}`, the signed class of `x` over Q is zero exactly when `A` contains an element with character -1.

**Proof.** An odd automorphism `a` gives `[x]=[x^a]=χ(a)[x]=-[x]`, hence `2[x]=0`, hence `[x]=0` over Q. Membership, action, and sign must all be checked; no search coverage is needed for this one-sided certificate.

Conversely assume `χ|_A=+1`. Define `f(x^g)=χ(g)`. If `x^g=x^h`, then `g h^-1∈A`, so `χ(g)=χ(h)`; `f` is well-defined. For any orbit element `z=x^g`,

`f(z^k)=χ(gk)=χ(k)f(z)`,

so `f` annihilates the defining relation vectors `[z^k]-χ(k)[z]` and descends to the signed quotient. Since `f([x])=1`, `[x]` cannot be zero. This proves the converse and explains the characteristic-not-two hypothesis. A complete generating set of `A` with all signs +1 is sufficient by homomorphism; a generating set for a discovered proper subgroup is not.

If `c=x^t`, then `[c]=χ(t)[x]`, equivalently `[x]=χ(t)[c]`. Any two such transporters differ on the left by an element of `A`, so in the nonzero case their signs agree. For `x^h`, the transporter `h^-1t` gives sign `χ(h^-1t)=χ(h)χ(t)`, proving covariance. Full stabiliser enumeration supplies the missing coverage before nonzero completion; SIGN-COVER is correctly left open rather than assumed.

**Lift-validation claim.** Let `L` be generated on `Ω ⊔ {+,-}` by the supplied signed generators, and let `π:L→G` restrict to Ω. Each generator preserves the two blocks, so `π` is a surjective homomorphism. Its kernel consists exactly of elements fixing every point of Ω and is a subgroup of the two-element sign-swap group. If that kernel is trivial, `π` is an isomorphism; composing `π^-1` with the sign action gives the required character and the prescribed generator signs. Conversely a valid character makes `L` its graph `{(g,χ(g)):g∈G}`, whose projection kernel is trivial. Thus the iff in §8.4 is correct. Merely checking some relations or checking a guessed subgroup's kernel is insufficient; the lifted generated group/kernel must be complete.

For a validated character, `g↦g_tilde` preserves products because the Ω action does and the two-point swaps multiply as signs; it is injective because restriction recovers `g`. Acting on `(x,marker)` therefore models unsigned action plus character. In particular,

`(x,+)^(a_tilde)=(x,-) iff a∈A and χ(a)=-1`.

This establishes the zero-transporter interpretation. Ordinary canonicalisation of the lifted pair merely selects a representative; it does not on its own establish this transporter's existence or its absence, nor equality with P1's unsigned representative. The warning at `docs/specification.md:312` is necessary and correct.

### 4.10 D1: least odd automorphism and Schreier kernel generators

**Claim** (`HANDOFF.md:100`; `docs/specification.md:80`; `docs/slices/M0a-notes.md:78–80`). If an odd automorphism exists, the deterministic zero witness is precisely the least element of `A⁺a`, where `A⁺=A∩kerχ` and `a` is any odd element of `A`. The singleton-orbit route can construct `A⁺` from the transversal `{1,a}`.

**Coset proof.** `χ|_A` is surjective, so `A⁺` is a normal subgroup of index two. For any odd `b∈A`, `χ(b a^-1)=+1`, hence `b∈A⁺a`; every element of `A⁺a` is conversely odd and fixes `x`. Thus the set is exactly the set D1 minimises, independently of the chosen `a`. Finiteness and the total image-array order give a unique minimum. Complete even stabiliser coverage plus one odd element suffices: `A=A⁺ ⊔ A⁺a`. A complete chain for `A⁺` without coverage evidence is insufficient.

On the enumeration route, every even automorphism is encountered and inserted or already generated, while odd hits are not inserted. Exhaustion therefore gives exactly `A⁺`. The first retained odd `a` need not be minimal; the exact coset descent of §4.4 computes the required minimum. Stopping on the first odd hit would suffice only for ANY mode or an independent minimum proof.

**Kernel-generation proof on the singleton-orbit route.** Here `A=G=<S>` is known because every input generator fixes `x`. Choose any odd generator `a`; one exists whenever `χ` is nontrivial on `G`. Put `K=kerχ`, and let `r(w)=1` for even `w`, `a` for odd `w`. These are representatives of the right cosets `Kw`. For `t∈{1,a}` and `s∈S`, Schreier's element is

`k(t,s)=t s r(ts)^-1 ∈ K`.

In the stated convention its four possibilities are

| Character of `s` | `t=1` | `t=a` |
|---|---|---|
| +1 | `s` | `a s a^-1` |
| -1 | `s a^-1` | `a s` |

They have the correct signs and product order. To prove they generate all of `K`, write a kernel element as a word `w=s_1…s_m` in the input generators. This positive-word choice is legitimate for a finite group: each inverse is a positive power. Let `r_i=r(s_1…s_i)`, with `r_0=r_m=1`. Then

`w = product_(i=1..m) (r_(i-1) s_i r_i^-1)`.

The product telescopes, and each factor is one of the displayed Schreier elements because the sign of `r_(i-1)s_i` is the sign of the corresponding prefix. Thus they generate exactly `K`, even if the supplied generator list is not inverse-closed. Equivalently, with inverse letters allowed, their Schreier factors are inverses of forward-edge factors. A verified chain constructed from these generators can therefore feed the same coset descent.

**No involution assumption.** In general `a^-1` is not `a`, and the odd row really contains `a s`, not `a s a^-1`. For example if `G=<a>` is cyclic of order four with `χ(a)=-1`, the two entries for `s=a` are `1` and `a²`, generating the correct even kernel. This directly checks the case an unjustified replacement of `a^-1` by `a` could mishandle. The formulas at `src/search/objectives.c:613–647` match the derivation.

**Boundary/coverage qualifications.** At degree zero or for the trivial group, a validated character is trivial, so no certified-zero branch with an odd element is reachable. On the singleton-orbit route the kernel calculation can be substantial despite charging no enumeration visits: this follows the traversal-based quota definition, not a constant-work theorem. On other routes deterministic exhaustion counts against the solve quota; an early odd witness still certifies zero mathematically but does not yet fulfil D1's minimum-witness objective.

**Do not infer lexicographic enumeration.** The historical assertion that §8.1 “visits G in image-array order” (`docs/slices/M0a-notes.md:65`) is false in general and is unnecessary for the current D1 algorithm. A small analytic counterexample is `G=Alt(4)`. At the root, the least permutation sending 0 to 1 is `r=[1,0,3,2]`. Its stabiliser child is `H=G_0=<u>`, `u=[0,2,3,1]`. The next split at 1 visits images 1, 2, 3 in that order; since `H_1=1`, its consecutive leaves are

```
r       = [1,0,3,2]
u r     = [1,3,2,0]
u^-1 r  = [1,2,0,3].
```

The last is lexicographically smaller than the preceding leaf. Sorted source orbit points cease to be sorted final images after the right multiplier `r`. This refutes the general ordering assertion; it does not claim that this particular group has an odd character or supplies a signed-zero counterexample. The current exhaustive `A⁺` construction followed by exact coset minimisation avoids relying on that historical argument. This stale explanatory sentence should be corrected if the historical notes are annotated.

