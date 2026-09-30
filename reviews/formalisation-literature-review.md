# Formalisation literature review

The mathematical core of the architecture is a realistic Lean target. An efficient canoniser with arbitrary generated groups, general adapters, C17 storage, concurrency and ISA kernels is a substantially larger verification project. The local prior art establishes a useful proof architecture; it does not supply a verified implementation of that complete scope.

This review was prepared against v1.0; section references below describe that baseline unless stated otherwise. A 30 September 2026 update adds the unverified external leads below; the [implementation plan](../docs/implementation-plan.md) reconciles its effort ranges with the referee report.

This review concerns [the architecture specification](../docs/specification.md), especially §§3, 6–10, 14, 17–18 and 20. Its ground truth is the locally retained TeX, Isabelle theories and pinned mathlib source described in [the provenance record](../review_sources/formalisation/PROVENANCE.md). No Lean/Isabelle build, C/C++ execution or performance experiment was conducted.

## What the formal graph literature establishes

Banković, Drecun and Marić, *A proof system for graph (non)-isomorphism verification* (LMCS 2023; arXiv:2112.14303v5), formalise graph canonisation and certificate checking in Isabelle/HOL. The following distinctions matter:

| Layer | Locally supported conclusion | Exact evidence |
|---|---|---|
| Abstract graph search | Canonical-form theorem under refiner, target-selector and invariant contracts; pruning correctness | [McKayPiperno.thy:51](../review_sources/formalisation/code/isocert/thy/McKayPiperno.thy#L51), locales at 51–63, 194–206 and 923–942; `canon_form` theorem at 1321; `pruneACs_canon_form` at 1638 |
| Concrete mathematical choices | Their refinement, target selector and node invariant satisfy the required contracts | [Paper §4:517](../review_sources/formalisation/papers/isocert-2112.14303v5/rules.tex#L517), 517–522; concrete theory sublocale proofs at 2521, 3177, 3450 and 3494 |
| Proof system | Soundness is formalised; completeness is proved in the paper | [Paper §4.5:1367](../review_sources/formalisation/papers/isocert-2112.14303v5/rules.tex#L1367), 1367–1381; completeness subsection starts at 1422 |
| Abstract checker | Rule checking preserves valid facts; accepted final canonical fact identifies the specified canonical form | [ProofFormat.thy:551](../review_sources/formalisation/code/isocert/thy/ProofFormat.thy#L551), rule lemma; `check_proof` definition at 736, correctness lemma at 798, `soundness` theorem at 818–830 |
| Search executable | Optimised C++ search is outside the formal correctness claim | [Paper §4:525](../review_sources/formalisation/papers/isocert-2112.14303v5/rules.tex#L525), 525–534 |
| Checker executable | C++ prototype inspected/tested; verified refinement to executable code remains future work | [Paper §7:2050](../review_sources/formalisation/papers/isocert-2112.14303v5/rules.tex#L2050), 2050–2059; implementation discussion at 1569–1589 |

The Isabelle development is useful proof-engineering precedent, not a Lean library import. Its locales retain assumptions about invariant/encoding functions: see [McKayPiperno.thy:3484](../review_sources/formalisation/code/isocert/thy/McKayPiperno.thy#L3484), 3484–3492. These assumptions must be discharged for each concrete instantiation. The source was inspected but not rebuilt here; this audit does not independently certify its current Isabelle compatibility.

## What local mathlib supplies

The inspected mathlib snapshot is pinned to `ed72f1faaeee9bea5805932ae309af92264cd8d1`. The local extraction now retains only eight complete source files needed for this review, plus license/toolchain metadata; the pinned compressed archive remains available to recover the full corpus. It supplies substantial mathematical infrastructure:

| Needed concept | Actual local source support | Remaining implementation work |
|---|---|---|
| Finite permutations and group laws | [Algebra/Group/End.lean:72](../review_sources/formalisation/mathlib/mathlib4/Mathlib/Algebra/Group/End.lean#L72), `Equiv.Perm` group; multiplication application at 101 | Prove bounded array representation, bijectivity checks, indexing and composition agree with this model |
| Group actions, orbits and stabilisers | [GroupAction/Defs.lean:49](../review_sources/formalisation/mathlib/mathlib4/Mathlib/GroupTheory/GroupAction/Defs.lean#L49), orbit and membership; stabiliser at 515–524. [GroupAction/Basic.lean:64](../review_sources/formalisation/mathlib/mathlib4/Mathlib/GroupTheory/GroupAction/Basic.lean#L64), finite orbit; conjugate stabiliser at 264 | Constructive orbit traversal and transporter production with complete coverage, not merely set-level definitions |
| Cosets | [Coset/Basic.lean:18](../review_sources/formalisation/mathlib/mathlib4/Mathlib/GroupTheory/Coset/Basic.lean#L18), left/right conventions; coset cover at 548 | Relate concrete `H r` node payloads and transversal choices to these sets; prove disjoint splitting |
| Schreier subgroup generation | [Schreier.lean:96](../review_sources/formalisation/mathlib/mathlib4/Mathlib/GroupTheory/Schreier.lean#L96), `Subgroup.closure_mul_image_eq`; finite-set version at 131–134 | BSGS data, transversal validation, sifting, chain construction/termination and completeness certificates |
| Finite ordered minima | [Data/Finset/Max.lean:127](../review_sources/formalisation/mathlib/mathlib4/Mathlib/Data/Finset/Max.lean#L127), definitions and `to_dual` generation of `min'`, membership and bounds | Prove orbit/tree nonemptiness, exact total order, encoder agreement and executable enumeration |

The existing Schreier lemma is a subgroup-generation theorem, not a verified efficient Schreier–Sims implementation. The initial named search used the full pinned Mathlib Lean source tree before the extraction was pruned, and found no BSGS, stabiliser-chain or graph-canonisation implementation under the terms recorded in the provenance file. This supports planning new algorithmic work, not a universal absence claim about Lean projects. In particular, it searched mathlib, not all external Lean repositories.

**Unverified external leads (30 September 2026).** TensorGR identifies `leanprover/hex-graph-iso` and `leanprover/hex-perm-group`, reportedly covering pinned nauty configurations for coloured simple undirected graphs and certified stabiliser-chain checking, respectively. Those claims came from web README/API inspection, not locally retained primary sources; neither repository has been retrieved or audited here. They may supply reusable components, but no ready verified implementation from them is established by this audit. Implementation-plan H0 must retrieve both at full pinned commits with SHA-256 provenance and inspect theorem, executable, configuration and trust scope before any dependent milestone. H1 then decides reuse. Even if the leads are confirmed, arbitrary-group P1, directed/multi/hypergraphs, signed completeness, adapters and concrete C/runtime refinement remain separate obligations. The independent proof route does not depend on these leads.

Resolve the permutation convention before other proofs. The specification uses `(pq)[v] = q[p[v]]`; mathlib has `(f * g) x = f (g x)` at `End.lean:101`. A wrapper based on `MulOpposite (Equiv.Perm (Fin n))` can preserve the specification convention: [Algebra/Opposites.lean:177](../review_sources/formalisation/mathlib/mathlib4/Mathlib/Algebra/Opposites.lean#L177) reverses multiplication, with `unop_mul` at 224. An abstract right action can be represented as a left action of the opposite group. These are representation choices to prove correct, not grounds for changing the specification. Test the bridge with noncommuting permutations.

## Transfer to this architecture

**Abstract canonisation is the easiest useful target.** Formalise a finite atom universe, subgroup, exact action and injective ordered encoding. Prove orbit-minimum invariance first, then the §7 tree theorem assuming equivariant refinement, complete splitting, finite progress and leaf-normalisation correctness. The full atom list makes the leaf transporter unique. The short mathematical argument in §7.2 needs precise definitions of tree relabelling, traces and minima, but its dependencies are present at the mathematical level. A pure exhaustive reference implementation can be proved separately from later optimisations.

**Restricted groups and cosets add genuinely new obligations.** The prior graph proof ranges over graph relabellings; it does not automatically prove this specification's arbitrary-subgroup leaf map, normalised group-orbit refiners, lexicographic coset minima, or presentation-independent subgroup/coset encodings. Prove §8's coset split and finite descent as a separate theorem. Distinguish finding a witness from exhausting a transporter set and distinguish valid generator membership from completeness of a returned stabiliser. BSGS correctness is a major reusable subproject.

**Auxiliary encodings and adapters need individual theorems.** For every adapter, prove action compatibility, exact equality, injective serialization and refiner equivariance. Extended graphs additionally need extension/restriction lemmas, freedom from auxiliary-numbering dependence, and a proof that stopping after base atoms become discrete preserves leaf keys. Native subgroup and labelling-coset atoms need presentation-independence proofs; a general abstract `MulAction` does not supply them. These obligations follow directly from [specification §§6–7](https://github.com/tobiasosborne/canonicalisation/blob/7ad98cb7ba778ba3599f2eca0de05ab76c6c954c/Canonicalisation_C_Architecture_Specification.md?plain=1#L136).

**Pruning and certificates are a useful boundary.** One can prove a Lean checker for explicit splitting, bounds and verified symmetries while permitting an unverified producer to search aggressively. The checker must reject missing coverage and check mathematical claims, including group membership and node stabilisation. An orbit witness alone cannot certify canonicity. Generalising the graph certificate rules to arbitrary groups/adapters remains design work; the graph paper does not provide those rules ready made.

**C17 and scheduling are separate refinement layers.** A Lean theorem about a pure search does not establish array bounds, integer-overflow safety, aliasing, lifetime management, memory reclamation or C atomics. Those require an explicit concrete-state relation and a chosen formal operational/memory model. For [specification §14.7](https://github.com/tobiasosborne/canonicalisation/blob/7ad98cb7ba778ba3599f2eca0de05ab76c6c954c/Canonicalisation_C_Architecture_Specification.md?plain=1#L549), first prove an abstract task protocol preserves coverage and a retained representative under symmetry cancellation; separately prove actual counters, queues, transfers and C17 memory ordering implement it. Liveness also needs stated fairness/resource assumptions. Checkpoint reconstruction and crash-safe publication require their own semantics.

**ISA correctness does not follow from scalar correctness.** SIMD kernels need a theorem relating word/vector operations to the scalar mathematical operation, including tail handling and overflow, followed by a bridge to the selected intrinsics/instruction semantics. CPU dispatch needs to respect its feature preconditions. The audited local sources establish no end-to-end Lean-to-C17-to-ISA verification pipeline for this engine. Compiler correctness and operating-system behaviour must remain explicit trust assumptions unless separately proved.

## Suggested scope and effort

The estimates below are my planning judgment, not measured results or literature claims. They assume an experienced Lean/mathlib developer working with a permutation-group specialist, frozen definitions and a small initial graph/subset adapter scope. Ranges are additional expert person-weeks; work can overlap, and calendar time depends on staffing. Re-estimate after the first executable/proof milestone.

| Milestone | Reviewable exit condition | Tentative effort |
|---|---|---|
| 1. Semantics and reference | Convention bridge, finite orbit minimum, coset coverage, §7 canonicality theorem, proved pure exhaustive reference | 8–16 weeks |
| 2. Constructive group kernel | Permutation arrays, orbit/transversal operations, chain invariants, sifting and independently checked BSGS completeness | 12–36 weeks |
| 3. Useful serial proof/checker | One graph/subset refiner, target/trace rules, pruning soundness and serial certificate checker | 12–32 weeks |
| 4. General adapters | A selected bounded auxiliary encoding and native subgroup/coset atom semantics, with presentation independence | 12–32 weeks; adapter-dependent |
| 5. Concurrent abstraction | Task ownership, cancellation, incumbent/generator publication and restart coverage as transition-system proofs | 8–24 weeks after protocol is fixed |
| 6. Concrete implementation | Verified chosen C subset/state representation, concurrency primitives and selected ISA kernels | Separate systems-verification programme; plausibly 1–3+ expert person-years for substantial coverage, highly dependent on chosen semantics/tools |

A narrow abstract theorem and reference implementation are attainable within months. A verified efficient arbitrary-group engine should be budgeted as a sustained project, with the BSGS kernel and certificate boundary decided early. Full C17/multicore/ISA correctness is not a routine final port of a mathematical Lean proof. The most informative first release would state exactly which semantic and executable layers are proved, checked, tested, or trusted.
