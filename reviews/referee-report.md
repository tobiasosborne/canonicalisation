# Constructive referee report: native C canonicalisation

29 September 2026. Reviewed: `Canonicalisation_C_Architecture_Specification.md`, version 1.0, 824 lines, SHA-256 `924699142d622de142e63f1145c91b436612aa88653e77311da8ecb2d2cfb431`.

**Recommendation: major revision before implementation freeze.** The central canonical-image construction is mathematically credible. The specification makes several important distinctions correctly, particularly canonical image versus lexicographic minimum, fixed groups versus coordinate changes, and valid automorphisms versus a complete stabiliser. I found no counterexample to its full-list leaf-map argument or coset splitting identity. However, it is an architectural design with outstanding semantic choices and proof obligations, not yet a complete executable specification. Its generality and performance ambition require substantial changes to representation, complexity accounting, and the verification plan.

The most consequential findings are: the public minimum-image scope contains NP-hard problems; subgroup serialisation can force terabytes of output from compact input; shared nested objects can expand exponentially; a canonical profile and wire format are still unspecified; and a Lean proof of the abstract algorithm will leave most of the C implementation outside its guarantee. These are actionable issues, rather than reasons to abandon the design.

This review includes [hardware and runtime lower bounds](../docs/performance-lower-bounds.md), an [algorithm literature audit](algorithm-literature-review.md), and a [formalisation literature audit](formalisation-literature-review.md), prepared by GPT-6.1 Sol subagents at xhigh reasoning. Literature claims are grounded in retained local primary sources, preferably TeX, under `review_sources/`; the audits supply exact locations and provenance. Hardware constants use retained manufacturer documentation. No hardware performance measurements or production implementation tests were performed. Effort estimates and proposed engineering policies below are referee judgments.

**1. Release blocker — freeze an actual canonical profile and encoding.** Relevant specification: [§4](https://github.com/tobiasosborne/canonicalisation/blob/7ad98cb7ba778ba3599f2eca0de05ab76c6c954c/Canonicalisation_C_Architecture_Specification.md?plain=1#L95), [§7](https://github.com/tobiasosborne/canonicalisation/blob/7ad98cb7ba778ba3599f2eca0de05ab76c6c954c/Canonicalisation_C_Architecture_Specification.md?plain=1#L166), [§10.6](https://github.com/tobiasosborne/canonicalisation/blob/7ad98cb7ba778ba3599f2eca0de05ab76c6c954c/Canonicalisation_C_Architecture_Specification.md?plain=1#L351).

The text frequently requires a fixed choice without making that choice: target cells use a tuple “such as” the example; the sequence of refiners, stage boundaries, initial ordered colours, signature order, individualisation placement, trace framing and terminal-marker order remain open. The wire format similarly requires tags, lengths and an ordering but does not assign them. Two careful implementations can satisfy the document and return different bytes under the purported same profile.

Publish one normative profile as deterministic pseudocode. Specify every comparison, the ordering of newly split cells, whether singleton facts follow branch order or partition order, and exactly which events enter the trace. Give the binary grammar and numeric/byte comparison relation separately. Specify graph loops, duplicate arcs, arc-colour ordering, empty relations, set versus multiset normalisation, literal limits, and invalid encodings. Include golden vectors whose trace, witness and bytes can be checked by hand. Treat all future profile extensions as new identifiers unless equivalence to an existing profile is proved.

The storage key must explicitly include schema, action, admissible context, objective, profile and encoding version. Witnesses and incomplete groups belong in result metadata; serialising the whole result does not make it a canonical byte string when witnesses are nondeterministic.

**2. Release blocker — representation independence needs its own proof obligation.** Relevant specification: [§4](https://github.com/tobiasosborne/canonicalisation/blob/7ad98cb7ba778ba3599f2eca0de05ab76c6c954c/Canonicalisation_C_Architecture_Specification.md?plain=1#L112), [§6](https://github.com/tobiasosborne/canonicalisation/blob/7ad98cb7ba778ba3599f2eca0de05ab76c6c954c/Canonicalisation_C_Architecture_Specification.md?plain=1#L136), [§17.2](https://github.com/tobiasosborne/canonicalisation/blob/7ad98cb7ba778ba3599f2eca0de05ab76c6c954c/Canonicalisation_C_Architecture_Specification.md?plain=1#L635).

Equivariance under atom permutations does not establish independence from the input representation. A refiner can be equivariant yet choose a different semantic refinement policy according to the number of redundant input generators, the presence of shared DAG nodes, or an insertion-order convention. Native equality at leaves will not repair different tree objectives.

Define a semantic interpretation `decode` of each representation. Require action, equality, encoding, refinement, target selection and trace construction to factor through this interpretation, modulo explicit coordinate transport. For example, equal generated subgroups with different SGS/base choices must induce the same ordered refinement result; shared and duplicated representations of the same extensional object must do likewise. Include representation equivalence as a separate hypothesis in the Lean interface. Metamorphic tests in §20 are appropriate evidence but are not this theorem.

For auxiliary vertices, retain the existing projection condition, but make the stronger operational requirement precise: all base partitions and traces used before termination must be invariant under auxiliary-only renaming. Mere existence of an extended graph isomorphism is insufficient to justify implementation tie-breaks.

**3. Release blocker — the labeling-coset API needs typed witnesses and explicit reconstruction equations.** Relevant specification: [§3.1](https://github.com/tobiasosborne/canonicalisation/blob/7ad98cb7ba778ba3599f2eca0de05ab76c6c954c/Canonicalisation_C_Architecture_Specification.md?plain=1#L85).

For the fixed-domain operation the witness is an element of `G`. For admissible labelings `Λ = Gρ`, the witness is a bijection in `Λ`; it is not generally an element of `G`. The current instruction to map witnesses back leaves this distinction out of the public result table and the universal witness tests.

With left-to-right products, let `G′ = ρ⁻¹Gρ`, let `t ∈ G′` canonise `x^ρ`, and return `λ = ρt`. Then `λ ∈ Gρ`, `c = x^λ`, and the complete labeling coset is `Aut_G(x) λ`. Under renaming by `μ`, the corresponding admissible coset is `μ⁻¹Λ`. These statements should be explicit API and test contracts, including unequal source and target domain types. Test replacement of `ρ` by another representative of the same labeling coset as well as changes of generators.

**4. Release blocker — refinement termination needs a bound across the whole node computation.** Relevant specification: [§10.6](https://github.com/tobiasosborne/canonicalisation/blob/7ad98cb7ba778ba3599f2eca0de05ab76c6c954c/Canonicalisation_C_Architecture_Specification.md?plain=1#L355).

The suggested strict-partition-refinement argument is valid on a fixed finite working domain. A finite auxiliary allocation on each invocation does not by itself bound a sequence of invocations that introduces fresh vertices. Exact deduplication does not stop genuinely new gadgets. Strict refinement of partitions on changing domains also needs a definition.

Require a node-wide finite domain bound and monotone refinement on that domain, or an explicit well-founded rank covering both generation and partition changes. Declare that refiners preserve prior individualisations and existing partition distinctions. For fixed `N`, the number of strict partition improvements is at most `N − initial_cell_count`; refiner sweeps and object actions must themselves terminate. Search depth is then bounded by base individualisations. If a custom callback is assumed total, state that assumption rather than claiming the engine can verify it.

**5. Major completeness gap — write the complete stabiliser and transporter algorithms.** Relevant specification: [§8.3](https://github.com/tobiasosborne/canonicalisation/blob/7ad98cb7ba778ba3599f2eca0de05ab76c6c954c/Canonicalisation_C_Architecture_Specification.md?plain=1#L247), [§9.1](https://github.com/tobiasosborne/canonicalisation/blob/7ad98cb7ba778ba3599f2eca0de05ab76c6c954c/Canonicalisation_C_Architecture_Specification.md?plain=1#L259).

“Run a complete stabiliser search” names a required result without specifying the traversal or its permitted pruning. The canonical-image proof does not prove group completeness. A verified BSGS for a subgroup found so far proves exactly that subgroup, not that every object automorphism has been found.

Provide a deliberately simple reference policy using §8.1's disjoint coset splitting. For transporter search, test `x^g = y` at every remaining singleton coset; for stabiliser search, accumulate every `g` satisfying `x^g = x`, or prove that each omitted solution lies in the generated subgroup. This proves completeness by exhaustion. Practical pruning then needs an objective-specific coverage lemma: preserving one best image is weaker than preserving all generators needed for the stabiliser. Specify empty/nonempty transporter results, complete transporter-coset output if supported, and the exact proof object or trusted-completion assertion meant by “proof of exhaustion.”

The same applies to intersections, normalisers and subgroup conjugacy services. They have different input and output contracts and substantial costs. Give each a reference algorithm and an explicit scope, or move it out of the first supported API. Arbitrary constraints must retain their documented general solution-set type.

**6. Major complexity omission — lexicographic minimisation includes NP-hard cases.** Relevant specification: [§8](https://github.com/tobiasosborne/canonicalisation/blob/7ad98cb7ba778ba3599f2eca0de05ab76c6c954c/Canonicalisation_C_Architecture_Specification.md?plain=1#L225).

Here is a direct reduction for an encoding permitted by the specification; it is a referee derivation, not an assertion that every encoding has the same complexity. Take a simple undirected graph on `n` vertices and `G = Sym(n)`. After a fixed header, encode upper-triangle adjacency bits in the order

`(0,1), (0,2), (1,2), (0,3), (1,3), (2,3), …`, with `0 < 1`.

The first `k(k−1)/2` bits describe exactly the induced graph on the first `k` labels. Some relabeling has this prefix entirely zero if and only if the original graph has an independent set of size `k`. Whenever such a relabeling exists, the lexicographically least encoding has that zero prefix. Consequently, computing this minimum solves Independent Set. Equivalently, apply complementation to the classical Clique problem. The algorithm literature audit retains the primary hardness source locally.

Thus the advertised general minimum-image operation cannot have a polynomial worst-case guarantee unless `P = NP`. This is stronger and more informative than merely observing that the proposed fallback enumerates a group. It does not establish an unconditional exponential runtime, a numeric instance bound, or hardness of arbitrary canonical-image output. Keep those claims distinct in both documentation and benchmarks.

There is also a relevant unconditional result for a restricted algorithm class: fixed-dimensional Weisfeiler–Leman-realizable individualisation/refinement schemes have exponential search-tree lower bounds, even with automorphism knowledge. The hypotheses must be checked against a particular profile; the theorem does not constrain arbitrary stronger adapters admitted by this architecture. See the [local TeX theorem](../review_sources/algorithms/1705.03283v1/ir-analysis.tex#L311) and its hypotheses at line 298.

**7. Major performance defect — the proposed subgroup wire representation is unsuitable as a general default.** Relevant specification: [§9.4](https://github.com/tobiasosborne/canonicalisation/blob/7ad98cb7ba778ba3599f2eca0de05ab76c6c954c/Canonicalisation_C_Architecture_Specification.md?plain=1#L285).

For `H = Sym(n)`, the fixed-base scheme has `n(n−1)/2` nonidentity transversal representatives. Emitting each as a full `n`-entry permutation therefore emits `n²(n−1)/2` entries. At four bytes per entry this is `2n²(n−1)` bytes:

| Degree | Bytes, excluding framing |
|---:|---:|
| 1,000 | 1,998,000,000, approximately 2 GB |
| 10,000 | 1,999,800,000,000, approximately 2 TB |
| 100,000 | 1,999,980,000,000,000, approximately 2 PB |

Streaming reduces peak storage but cannot reduce these output bytes. A bandwidth bound derived from them would certify the cost of an unnecessarily large representation. It is not a lower bound for representing the group, which in this example has a compact symbolic description. Bit-packing full representatives changes constants, not the cubic entry count.

Promote a compact, proven, presentation-independent generating representation to an early deliverable. The constructive lemma cited by R2 produces at most `n log n` coset elements using successive least elements outside the already generated coset, with polynomial-time group operations. Dense output is then `O(n² log n)` point entries. See the [original local lemma and proof](../review_sources/algorithms/1803.06858v1/articles/canonization.tex#L140). This is a concrete improvement, although still not a guarantee of small output for every large group. Content-defined sparse/symbolic encodings and output budgets remain necessary. Prove the serializer, including subgroup equality and coset-representative handling. Retain the full-transversal representation as a small-instance oracle or an explicitly selected diagnostic format, and define a streaming sink with backpressure/error semantics.

Do not confuse canonical representation on an ordered domain with solving subgroup conjugacy. The latter remains a search problem, and the source's general-object results must be read with their actual representation-size parameters.

**8. Major performance defect — extensional DAG semantics need a compressed canonical encoding.** Relevant specification: [§4](https://github.com/tobiasosborne/canonicalisation/blob/7ad98cb7ba778ba3599f2eca0de05ab76c6c954c/Canonicalisation_C_Architecture_Specification.md?plain=1#L95), [§6.1](https://github.com/tobiasosborne/canonicalisation/blob/7ad98cb7ba778ba3599f2eca0de05ab76c6c954c/Canonicalisation_C_Architecture_Specification.md?plain=1#L140).

Let `x₀` be a literal and `xᵢ₊₁ = (xᵢ, xᵢ)`. The shared input has `i+1` distinct nodes and two references per tuple, but recursive tree serialisation has `2^i` literal occurrences. At depth 60 this is already beyond desktop-scale output. Fully extensional hash-consing does not fix an encoder that expands every occurrence.

Define both stored size and expanded semantic size. Select a canonical DAG encoding with deterministic references to equal subobjects, or explicitly charge output expansion and refuse oversized serialisations. A feasible direction is exact bottom-up normalisation followed by deterministic numbering of distinct normal forms, with a proof that source sharing and insertion order do not affect the resulting bytes. Variable-length comparisons must avoid repeatedly expanding shared subobjects. Validate acyclicity if the schema promises a DAG; supporting cyclic objects would require different semantics and algorithms.

**9. Major complexity gap — provide a cost ledger with all input parameters.** Relevant specification: [§7.5](https://github.com/tobiasosborne/canonicalisation/blob/7ad98cb7ba778ba3599f2eca0de05ab76c6c954c/Canonicalisation_C_Architecture_Specification.md?plain=1#L219), [§9–12](https://github.com/tobiasosborne/canonicalisation/blob/7ad98cb7ba778ba3599f2eca0de05ab76c6c954c/Canonicalisation_C_Architecture_Specification.md?plain=1#L255).

The base degree `n` is not a sufficient input-size parameter. Include working vertices `N`, incidences `m`, relation/colour counts, total tuple arity, DAG nodes/references, literal bytes, multiplicity bit lengths, input-generator count and support, stabiliser-chain levels/orbit sizes/word lengths, and output bytes. State costs of the exact action, comparison, encoding and every custom refiner. A “computable” callback has no useful uniform runtime bound merely because its atom domain is small.

A useful serial accounting identity is

`T = T_validate + T_group + T_normalise + Σ_nodes(T_refine + T_group_node + T_split/undo) + Σ_leaves T_leaf + T_output`.

Then describe node count separately: the unpruned full individualisation tree has `n!` leaves for the trivial-refinement construction, while complete coset enumeration has `|G|` leaves and depth at most `n`. Neither is a universal lower bound, and automorphism or bound pruning can change both. Input generators all fixing the object is a valid shortcut already present in §9.5. Large differences between `|G|` and `n!` should inform explicitly selected, proved-equivalent backends where possible.

The sparse-refinement `O((N+m) log N)` statement needs a precise algorithm, fixed relation universe, exact sorting/key cost, smaller-fragment scheduling proof and a definition of `m`. If every sweep rescans all accumulated logical stack layers, or every singleton append creates another pass, physical sharing alone does not prevent quadratic cumulative work. Charge the sum of actual relation processing over all nodes. Full tuple minimisation and chain rebasing at every leaf can dominate refinement; expose their separate counters and reuse contracts.

**10. Major methodology correction — distinguish three kinds of lower bound.** Relevant specification: [§12](https://github.com/tobiasosborne/canonicalisation/blob/7ad98cb7ba778ba3599f2eca0de05ab76c6c954c/Canonicalisation_C_Architecture_Specification.md?plain=1#L409), [§14.10](https://github.com/tobiasosborne/canonicalisation/blob/7ad98cb7ba778ba3599f2eca0de05ab76c6c954c/Canonicalisation_C_Architecture_Specification.md?plain=1#L571).

The supplied [performance appendix](../docs/performance-lower-bounds.md) makes this distinction explicit. Adopt it normatively:

| Quantity | What it can establish |
|---|---|
| Problem/contract floor | Mandatory work or information transfer for the stated input/output and residency contract, allowing better legal representations and algorithms |
| Fixed-algorithm/work floor | Best possible execution of a specified work DAG or necessary passes; conditional on that work actually being required |
| Calibrated attainable target | An engineering target using measured bandwidth, latency, occupancy and launch costs; useful but not a mathematical lower bound |

The measured number of nodes, cache misses or undo writes of an implementation must not define its problem floor. Otherwise an inefficient design can appear near-optimal by doing needless work at high bandwidth. Conversely, an information-theoretic floor can be extremely loose on a hard search instance; a large ratio to it is not itself proof of poor engineering.

Use optimistic valid service ceilings for analytic bounds. Measured STREAM-like bandwidth is not a universal ceiling, and average pointer-chase latency is not the minimum latency of every dependency. For a fixed work DAG, take the maximum of competing resource and dependency bounds. Add costs only for stages that are proved sequential and cannot overlap. Do not multiply a serial latency by all requests when independent requests can overlap. Account for initial and final residency, preprocessing, compression/decompression, input validation, and whether output is materialised at all.

The appendix fixes a realistic example machine: Ryzen 9 9950X, 64 GiB dual-channel DDR5-5600, and RTX 5080 with nominal 16 GiB VRAM and PCIe 5.0 ×16. Its modeled interface ceilings are 89.6 GB/s for CPU DRAM, 960 GB/s for device memory, and 63.015 GB/s per PCIe direction. Selected consequences are:

| Precisely stated demand | Optimistic interface floor |
|---|---:|
| Read 1.2504 GB of padded rows for one broad dense count at `N=100,000`, from host DRAM | 13.96 ms |
| Same mandatory row read with data already resident in GPU memory | 1.303 ms |
| Upload that matrix over the selected PCIe link | 19.84 ms for upload alone |
| Emit the degree-100,000 full-transversal stream across host DRAM | 6.20 hours for approximately 2 PB |

These are conditional movement floors for the named operations and clock configuration, not complete-solve predictions. Cache-resident data, compressed or symbolic representations, different kernels, transfers and sequential dependencies alter their premises. Realistic bandwidth/latency scenarios, cache sizes and capacity constraints are tabulated separately in the appendix and labelled as estimates pending calibration. In particular, the smaller GPU read floor does not establish an end-to-end GPU speedup.

**11. Major measurement requirement — make deltas to lower bounds reproducible.** Relevant specification: [§19](https://github.com/tobiasosborne/canonicalisation/blob/7ad98cb7ba778ba3599f2eca0de05ab76c6c954c/Canonicalisation_C_Architecture_Specification.md?plain=1#L661).

For each measured result with a positive applicable bound `L`, report absolute gap `Δ = T − L`, ratio `ρ = T/L`, and fractional gap `δ = (T−L)/L`, together with the bound category, equation, assumptions and confidence/range of calibrated parameters. For a zero or unavailable floor, say the ratio is undefined. If a measurement falls below a claimed bound, investigate the bound, units or measurement contract; do not clamp the gap to zero.

Report CPU-only and CPU+GPU floors separately. Pin hardware configuration, clock/power policy, DRAM population, ISA, compiler, schema/profile, representation, output mode, residency and cold/warm boundary. A warm cached group must not be compared against a competitor whose setup is included. Use fixed-work replay for kernel efficiency and full solves for algorithmic efficiency. Preserve all censored/time-limited cases and report work inflation as the spec already proposes.

There can be a target to narrow these gaps for declared regimes. There cannot currently be a justified promise of a small constant factor from a tight optimal runtime for every supported instance. The paper-derived exponential bounds and NP-hardness distinction do not provide those numerical instance optima.

**12. Major memory gap — bound undo history, group caches and intermediate output, not just base arrays.** Relevant specification: [§10.5](https://github.com/tobiasosborne/canonicalisation/blob/7ad98cb7ba778ba3599f2eca0de05ab76c6c954c/Canonicalisation_C_Architecture_Specification.md?plain=1#L343), [§11.3](https://github.com/tobiasosborne/canonicalisation/blob/7ad98cb7ba778ba3599f2eca0de05ab76c6c954c/Canonicalisation_C_Architecture_Specification.md?plain=1#L383).

The stated 40–64 bytes per vertex per worker excludes precisely the terms that can dominate a difficult run. If each of `d` active frames modifies `Θ(N)` partition positions, a straightforward trail retains `Θ(Nd)` entries. At `N=d=100,000`, even eight bytes per entry means 80 GB before generators, snapshots and input. This is a conditional worst-case footprint, not a claim that every partition implementation incurs it; the specification currently gives no stronger bound.

Specify exact live-state accounting and admission checks for trails, temporary verification chains, in-flight publications, cached encodings and queued tasks. Choose recomputation checkpoints or another reversible representation with a stated time/space tradeoff. Give a policy for a single unavoidable task exceeding the budget; reducing concurrency cannot solve that case. Do not promise progress under finite memory solely from mathematical termination under unlimited resources.

Also state supported integer ranges. Large multiplicities and relation counts either require arbitrary-precision handling or explicit capacity errors. They are part of the input bit complexity. A capacity-limited first release remains useful, but “every finite object” then describes the mathematical model rather than the supported machine API.

**13. Major group-kernel risk — deterministic construction is a reference policy, not a performance argument.** Relevant specification: [§9](https://github.com/tobiasosborne/canonicalisation/blob/7ad98cb7ba778ba3599f2eca0de05ab76c6c954c/Canonicalisation_C_Architecture_Specification.md?plain=1#L255).

The requirements for exact Schreier closure and provenance are sound. However, the document needs pseudocode, operation/space bounds and performance gates for construction, verification, base change, sifting and transporter reconstruction. An implementation may spend most of its runtime constructing or certifying groups before entering the advertised fast search. Verification costs count even when a randomised constructor quickly finds a good candidate.

Represent provenance as shared straight-line derivations where appropriate; repeatedly expanding generator words can create another hidden size explosion. Verify the chosen certificate against the original generators, including terminal triviality and orbit/transversal conditions, rather than relying on mutual agreement of routines sharing the same bug. Plan separate tests for symbolic groups, small support, deep Schreier trees, many redundant generators, and changing bases.

The complete-stabiliser requirement for a deterministic witness is one sufficient method, not a necessity theorem. An alternative is to minimise the witness permutation directly among solutions taking `x` to the chosen canonical image. Keep the two output costs separate: callers requesting deterministic bytes alone should not accidentally pay for a full stabiliser.

**14. Major concurrency gap — specify a state machine that can be proved.** Relevant specification: [§14.5–14.9](https://github.com/tobiasosborne/canonicalisation/blob/7ad98cb7ba778ba3599f2eca0de05ab76c6c954c/Canonicalisation_C_Architecture_Specification.md?plain=1#L533), [§18](https://github.com/tobiasosborne/canonicalisation/blob/7ad98cb7ba778ba3599f2eca0de05ab76c6c954c/Canonicalisation_C_Architecture_Specification.md?plain=1#L653).

The stated principles are good, but “epoch-aware counter” and monotone representative links do not define linearisation points, release/acquire ordering, reclamation, or the interaction between task publication and completion. The formal invariant should assign each remaining mathematical region to exactly one live task, a completed certificate, or an acyclic justified representative link. A link must end at retained coverage, and its justification must be valid for the requested objective.

Specify transitions for child creation, stealing, failure, cancellation, helper completion, representative pruning and checkpoint capture. Prove that `COMPLETE` means the entire root coverage has been discharged. A checksum and witness checks cannot certify a checkpoint's retained search coverage. State whether checkpoints are trusted resumptions of engine-produced state or independently verifiable inputs; the latter needs coverage certificates. Include graceful cancellation while a callback, group verification or helper task is running, and distinguish cancellation latency from mathematical correctness.

**15. Important product decision — keep the CPU/GPU boundary explicit.** Relevant specification: [§2.2](https://github.com/tobiasosborne/canonicalisation/blob/7ad98cb7ba778ba3599f2eca0de05ab76c6c954c/Canonicalisation_C_Architecture_Specification.md?plain=1#L54), [§13.4](https://github.com/tobiasosborne/canonicalisation/blob/7ad98cb7ba778ba3599f2eca0de05ab76c6c954c/Canonicalisation_C_Architecture_Specification.md?plain=1#L479).

An optional GPU backend is compatible with a dependency-free CPU core, but the fastest GPU-resident floor is not a fair floor for the CPU-only implementation. Treat a GPU plugin and its runtime/driver dependency as an explicit build and API capability. Bulk dense refinement or batches may benefit; per-node irregular work can be dominated by transfer, launch, divergence, atomics and device memory limits. The appendix quantifies these regimes and uses integer/popcount throughput rather than advertised floating-point TFLOPs.

If GPU performance is a release objective, move the backend boundary, batched buffer ownership and residency contracts into the initial design. Otherwise label GPU numbers as future-backend opportunities and retain the CPU floor as the current acceptance reference.

**16. Important API gap — specify error, ownership and callback contracts concretely.** Relevant specification: [§3.2](https://github.com/tobiasosborne/canonicalisation/blob/7ad98cb7ba778ba3599f2eca0de05ab76c6c954c/Canonicalisation_C_Architecture_Specification.md?plain=1#L91), [§17](https://github.com/tobiasosborne/canonicalisation/blob/7ad98cb7ba778ba3599f2eca0de05ab76c6c954c/Canonicalisation_C_Architecture_Specification.md?plain=1#L615).

Give each result independently checkable validity/completeness flags: an interrupted search can have a valid witness without a canonical image, or a verified subgroup without a complete stabiliser. Define which objective a reported lower bound bounds; a byte bound and a trace-plus-byte bound are different. Define lifetime rules for transformed views, streamed output, borrowed input, callbacks and allocator failures. Budget errors must leave handles releasable and prevent partially populated results from masquerading as completed answers.

Custom adapters are a trust boundary. Document their required totality, action laws, exact semantic equality, comparator consistency, collision handling and refiner/bound proofs. The C engine cannot establish these universal properties of arbitrary callbacks at registration time. State validation guarantees for built-in adapters separately from assumptions on external adapters.

**17. Lean proof scope — the abstract theorem is manageable; the complete C guarantee is substantially harder.**

The closest retained prior art formalises the McKay–Piperno graph scheme, concrete refinement ingredients, pruning and proof-system soundness in Isabelle/HOL. It also formalises an abstract checker. The paper explicitly leaves refinement to an efficient executable checker as future work; its C++ checker is not an end-to-end verified implementation. See the [local paper source](../review_sources/formalisation/papers/isocert-2112.14303v5/rules.tex#L1917) and the [pinned-code audit](formalisation-literature-review.md). This is strong evidence for feasibility of the mathematical layer, with a clear boundary on what can be reused.

For this library, organise the proof into the following milestones. Effort ranges assume an experienced Lean/mathlib developer, a frozen specification, and ordinary supporting tooling; they are planning estimates, not measured delivery promises. They are not simply additive because interfaces and proof development overlap.

| Milestone | Required theorem / deliverable | Estimated difficulty |
|---|---|---|
| A. Abstract canonical image | Finite equivariant tree, invariant trace, full-list leaf map, minimum-key canonicality | Modest mathematical difficulty; roughly 2–6 expert weeks including usable definitions and review |
| B. Reference algorithms | Terminating unpruned tree and coset enumeration; exact image, minimum, transporter and stabiliser contracts | Several further weeks once A and group interfaces are stable |
| C. Certified group operations and one concrete profile | BSGS certificate soundness/completeness, tuple minimisation, graph/subset refinement and approved pruning | Several expert months; algebraic infrastructure helps but executable invariants remain substantial |
| D. General adapters and composition | Extensional DAG encoding, auxiliary projection, subgroup/coset atoms, overlap-compatible composition | Further months depending on scope; each adapter is an additional proof obligation |
| E. Fast implementation assurance | C memory/overflow model, mutable-array refinement, rollback, concurrency, checkpointing, ISA/GPU equivalence | A separate large verification project; plausibly person-years for the whole advertised surface |

The core proof is short on paper. For `h ∈ G`, tree equivariance sends leaf list `L` to `L^h` and preserves its trace. The least list image is the same, and a full list determines its acting permutation uniquely, so `t_(L^h) = h⁻¹t_L`. Hence `(x^h)^t_(L^h) = x^t_L`; the leaf-key sets coincide, and their minima coincide. Orbit membership follows from `t_L ∈ G`. Idempotence and equivalence detection follow from this theorem plus exact encoding. These arguments agree with the [local canonical-image source](../review_sources/algorithms/2209.02534v4/paper.tex#L801).

For minimum-image search, prove the disjoint decomposition `Hr = ⋃_b H_a t_b r` and induction over decreasing stabilisers. Every permitted bound and symmetry prune must preserve the objective's relevant solutions. For group-refiner normalisation, prove that two transporters from `F` to its least image differ by an element fixing that image, which preserves each orbit set. Representation independence and ordered orbit enumeration still need their own lemmas.

Use existing mathlib groups, cosets, stabilisers, finite orders and Schreier's lemma. Avoid forcing directed coloured multigraphs into `SimpleGraph`: a typed finite relation model matches the advertised semantics better. The spec's left-to-right permutation product differs from the usual `Equiv.Perm` composition convention; introduce a right-action wrapper or opposite group and prove the array correspondence once. The formalisation audit provides pinned local source locations. Do not axiomatise the hard BSGS or adapter obligations and then describe the resulting conditional theorem as verification of the implementation.

My recommended first assurance target is **a Lean theorem for the reference semantics plus a small executable certificate checker whose correctness is proved**. Let the optimised C search emit replayable branch, bound, automorphism and group certificates; verify final coverage independently. A certificate proving an image is canonical must include exhaustion/pruning evidence, not just its witness. Full stabiliser output additionally needs coverage and strong-generation evidence. Certificates can themselves be large, and custom refiners need checkable rules; include generation/checking cost in benchmarks.

This approach permits native C to remain the fast producer while limiting the trusted decision logic. It does not automatically prove memory safety or liveness of that producer. If the checker is executed as compiled Lean, its compiler/runtime assumptions must be declared; a kernel-checked proof term has a different trust boundary. End-to-end C verification requires an explicit C semantics and a refinement/translation argument, not just a similar functional Lean implementation.

**18. Acceptance plan — replace aspirations with reviewable gates.**

| Gate | Evidence required before moving on |
|---|---|
| Semantics frozen | One complete scalar profile, binary grammar, action/witness types, capacity policy and golden cases |
| Mathematical reference complete | Proofs of canonicality, coset coverage, termination and complete stabiliser output; explicit adapter assumptions |
| Representations viable | Compact subgroup encoding and shared-object encoding, size bounds, bounded live-state accounting |
| Scalar implementation trustworthy | Independent small-instance agreement, proof-obligation ledger, fault injection, C sanitizers/fuzzing, meaningful certificates where supported |
| Performance baseline honest | Cold/warm and residency contracts, bounds from the appendix, all costs included, declared workload families and competitors |
| Optimisations admissible | Semantic-equivalence proof or certificate rule plus regime-specific measured improvement |
| Parallel/accelerated release | Coverage state machine, rollback/helper safety, checkpoint protocol, fixed-profile equality, reported work inflation and transfer costs |

The key changes should be made before investing heavily in SIMD or GPU code. Preserve the common group/partition/runtime infrastructure, but allow objective-specific algorithms and compact representations with independently proved contracts. “Best in class” should mean a measured Pareto frontier over named workloads and output guarantees, with reported gaps to applicable bounds. General mathematical coverage is a separate guarantee and does not imply uniformly efficient execution.

**Checks performed for this report.** The included [finite-check script](../checks/review_checks.py) finished successfully in approximately 0.26 seconds. It exhausts the 40 subgroups across degrees 0–4, checking 3,536 full-list leaf-map identities, 2,997 coset partitions, 2,059 normalised-orbit cases (including transporter independence and equivariance), and 361 graph/prefix cases for the reduction above. It also checks the subgroup-output arithmetic. These are finite sanity checks supporting the review, not a Lean proof or validation of nonexistent production C code. No further CPU-intensive validation is needed for this report.
