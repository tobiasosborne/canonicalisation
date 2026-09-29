# A native C engine for canonicalisation under permutation groups

**Architecture and implementation specification · 29 September 2026 · version 1.0**

Prepared for Tobias J. Osborne. This document specifies an implementation; it does not implement it. Performance choices below are engineering proposals and testable hypotheses, not measured results. “Bare metal C” means a native, allocation-controlled C library with direct CPU intrinsics and a thin operating-system layer, running on an ordinary desktop OS. It does not mean a freestanding kernel or bypassing the OS.

## 1. Architectural decision

Build one exact search infrastructure with a permutation-group kernel, reversible ordered partitions, graph-stack refinement, typed object adapters, and a locality-aware multicore runtime. Specialise its hot kernels for common representations without creating unrelated graph and minimal-image solvers.

Use the Jefferson–Waldecker–Wilson canonical-image framework as the practical algorithmic foundation. Use Schweitzer–Wiebking to define the breadth of objects and the need to retain labeling cosets during composition. These are complementary ingredients, not interchangeable algorithms. The proposed backtracker does **not** inherit Schweitzer–Wiebking's asymptotic bound merely by supporting the same object types. [R1–R4]

The principal design decisions are:

1. **Exact semantics first.** Canonical images, lexicographic minima, transporter searches, and full stabiliser computations are distinct operations with explicit contracts.
2. **Native groups.** Support every finite permutation group supplied by generators; do not require its expansion into a graph or enumeration of its elements.
3. **Logical graph stacks, physical shared data.** A refinement step appends descriptors or deltas, not another copy of the input graph.
4. **Data movement is a first-class cost.** Optimise cache lines touched, dependent loads, undo traffic, and task-state migration alongside search-node count.
5. **Multicore primarily across subtrees and independent inputs.** Each worker owns its mutable state. Shared information is immutable or published in batches.
6. **Fixed mathematical choices, flexible execution.** CPU dispatch, thread count, scheduling, and cache eviction cannot change the answer under a fixed canonicalisation profile.
7. **A slow complete fallback.** Weak refinement may cost enormous time, but unsupported accelerators must never cause an incorrect result or a silent loss of generality.

No claim of universally best performance is made. Refinement can save an exponential amount of search, or spend most of the runtime proving almost nothing. The architecture must measure that trade-off.

## 2. Scope and non-goals

### 2.1 Required mathematical coverage

The domain is a finite set of movable atoms Ω, usually indexed internally by 0,…,n−1. The admissible group is G ≤ Sym(Ω), given by permutations generating G, or by a supported exact structured representation. An object has a computable right action of G and exact equality under that action.

Built-in adapters must cover:

| Object | Exact interpretation | Preferred representation |
|---|---|---|
| Subset of Ω | Unordered, without repetitions | Sorted IDs, bitset, or hybrid |
| Tuple of atoms | Positions distinguished; repetitions allowed | Contiguous IDs |
| Set or multiset of tuples | Tuple positions distinguished; outer order ignored | Flat tuple store plus offsets/multiplicities |
| Coloured directed graph | Vertex/arc colours are semantic labels | CSR/CSC or dense bitplanes |
| Hypergraph | Unordered hyperedges; explicitly defined duplicate policy | Incidence arrays or incidence refinement graph |
| Finite relational structure | Ordered relation names and tuple positions | Columnar relation tables |
| Nested finite objects | Atoms, typed tuples, sets, multisets, immutable literals | Semantic DAG with flat child arrays |
| Permutation as an object | Usually conjugation, explicitly specified | Image array; directed-cycle relation |
| Subgroup as an object | Equality of generated groups; usually conjugation | Verified stabiliser chain and generators |
| Labeling coset | A set of bijections, not a chosen generator list | Group handle plus a representative |
| Explicit code | Set of explicit words with declared coordinate/symbol action | Word table and native coordinate action |
| Custom object | Exact action, comparison and encoding supplied by adapter | Opaque immutable payload plus adapter |

The action is part of the problem. Independent permutations of matrix rows and columns, simultaneous permutation of both, conjugation of a group, and permutation of symbols are different equivalence relations. The API must make this impossible to overlook.

For the built-in algebraic atoms, fix the actions explicitly: a permutation object p transforms to g⁻¹pg; a subgroup H transforms to g⁻¹Hg; a labeling coset Hρ transforms by precomposition to g⁻¹Hρ=(g⁻¹Hg)(g⁻¹ρ). Its ordered target labels stay fixed. A general permutation coset under conjugation is a different object type from a labeling coset. Native refiners and serializers must implement this distinction.

Implicitly represented linear codes, enormous induced actions, infinite structures, approximate numeric equality, tensor identities involving sums, continuous changes of basis, and antisymmetric signs are not automatically solved by finite permutation canonicalisation. They require an exact reduction or a separate adapter with a precise action. For a general black-box action, the fallback can enumerate G through a verified chain; there is no promised useful runtime.

### 2.2 Delivery target

Primary target: x86-64 Linux and Windows, ordinary desktop memory, one socket, multiple physical cores; also account for shared-cache clusters, chiplets, heterogeneous cores, and SMT. Portable scalar C is mandatory. ARM64 scalar support is architecturally straightforward; NEON/SVE kernels are a later performance port.

Runtime dependencies: C runtime, OS threading/virtual-memory services, optional CPU intrinsics. No Rust, GAP, Python, C++ runtime, GPU runtime, or external graph package is required in production. Existing packages are development oracles and benchmark competitors. Use a small internal unsigned multi-limb integer facility for exact group orders if avoiding a big-integer dependency; a group order can exceed 64 bits even for modest n.

## 3. Mathematical contracts and permutation conventions

Fix the convention before designing data structures:

- A permutation array stores `p[v] = v^p`.
- Products act left to right: v^(pq) = (v^p)^q; hence `(pq)[v] = q[p[v]]`.
- Object actions obey (x^p)^q = x^(pq).
- The pointwise stabiliser of a list L is G_L. The object stabiliser is A = {g∈G : x^g=x}.
- H r denotes {h r : h∈H}; do not use ambiguous “coset” arguments without side information.

The public operations are:

| Operation | Required answer |
|---|---|
| `CANONICAL_IMAGE(profile)` | c and g∈G, with c=x^g and C_G(x^h)=C_G(x) for every h∈G |
| `LEX_MIN_IMAGE(order)` | min{enc(x^g):g∈G} under the explicitly named total order, plus a witness |
| `TRANSPORTER(x,y)` | A g∈G satisfying x^g=y, or proof of exhaustion |
| `STABILISER(x)` | Generators for exactly A, with a verified complete chain |
| `CANONICAL_LABELING_COSET` | A g, where g sends x to its canonical image and A is complete |
| `INTERSECTION/CONSTRAINT_SEARCH` | A group, coset or witness only when the constraint family guarantees that result type |

For equal canonical images x^p=y^q, a transporter from x to y is p q⁻¹. For two images of the same x with x^p=x^q, p q⁻¹ is an automorphism of x. Check these identities in tests with noncommuting permutations.

A single canonising permutation is generally not unique. Return any valid witness unless deterministic-witness mode is requested. For a deterministic witness, first obtain complete A and choose the lexicographically least element of A g. Do not claim that a chosen witness itself is equivariant under all input automorphisms; the natural equivariant output is the coset.

### 3.1 Fixed group versus a renamed universe

The basic canonical-image operation compares inputs for the same G acting on the same indexed Ω. Relabeling x by an arbitrary permutation outside G does not in general preserve its G-orbit.

For coordinate-independent composition, also accept an admissible labeling coset Λ=Gρ, where ρ:Ω→{0,…,n−1}. Under a change of names μ:Ω→Ω′ the admissible labelings become μ⁻¹Λ. Internally convert x to x^ρ and G to ρ⁻¹Gρ on the ordered target domain, solve there, and map witnesses back. This avoids conflating an automorphism of the data with a change of its coordinate system.

### 3.2 Completion and interruption

Every result has a completion status: `COMPLETE`, `CANCELLED`, `RESOURCE_LIMIT`, `INVALID_INPUT`, `UNSUPPORTED_ACTION`, or `INTERNAL_ERROR`. An interrupted search may return a valid image, witness, lower bound, and verified subgroup found so far. It must not label an unproved incumbent “canonical”, a subgroup “the full automorphism group”, or an unfinished transporter search “no solution”.

## 4. Exact output and versioning

Canonicalisation is useful for databases only if its byte representation is specified.

Define an injective typed encoding with explicit lengths and tags. Atom labels are integers in the ordered target domain. Tuple order is retained. Set children are compared by their complete normalised encodings, sorted, and deduplicated by exact equality. Multisets retain explicit multiplicities. Literal strings are byte strings with an explicit text-normalisation policy; no locale-dependent ordering. Floating-point literals, if admitted, must have a declared bitwise policy for NaNs and signed zero; approximate equality is not an equivalence relation suitable for this engine.

Use a fixed-endian wire format with an explicit numeric comparator. Do not assume that `memcmp` of little-endian integers implements numeric lexicographic order. The encoding and comparison specification must state whether tuple/set lengths precede contents in the order. Streaming comparisons and stored encodings must agree exactly.

Each persisted result records:

- object-schema version, action identifier and domain size;
- admissible-group/labeling context identity, independent of the particular generator list;
- canonicalisation profile and objective mode;
- encoding version and canonical bytes;
- witness and optional complete labeling coset;
- optional digest used for indexing, never as the only equality test.

Group presentation, worker count, ISA, memory addresses, hash-table iteration, queue order and elapsed time are not semantic inputs. Different generator lists for the same G must give identical canonical bytes for a fixed profile. Algorithmic changes to the canonical tree may change a valid canonical form and require a profile version change. Lexicographic minimum mode is independent of tree heuristics once its encoding order is fixed.

A canonical-byte equality is an equivalence test only within the same declared action and admissible-labeling context. Databases mixing different groups must include that context in their keys. A user-supplied context identifier can avoid normalising the ambient group merely for metadata; it must not be silently derived from a presentation-dependent generator hash.

## 5. Component architecture

| Component | Responsibility | Mutable ownership |
|---|---|---|
| Problem builder | Validate types, actions, generators; freeze input | Single construction thread |
| Object adapters | Action, exact comparison, serialisation, structural refinement | Immutable data; worker scratch |
| Group kernel | Chains, orbits, membership, cosets, transporters | Shared verified roots; local descendants |
| Logical graph stack | Relational facts, individualisations, derived invariants | Immutable layers plus local append-only descriptors |
| Partition kernel | Ordered cells, splitting, worklist, rollback | Worker-private |
| Search engine | Frames, objective, coverage, pruning and leaf verification | Worker-private |
| Symmetry service | Verified automorphism publication and subgroup snapshots | Batched coordinator updates |
| Scheduler | Task ownership, stealing, completion and cancellation | Padded queues and atomic task states |
| ISA dispatch | Scalar/AVX2/AVX-512 kernel selection | Immutable per worker class |
| Measurement layer | Counters, timing, allocation and traffic estimates | Local counters, periodic reduction |
| Persistence layer | Checkpoints, format validation, result encoding | Explicit I/O boundary |

Public handles are opaque; hot data is flat. External adapters use a versioned vtable called at operation or refinement-batch boundaries. Built-in adapters are statically dispatched inside tight loops. There must be no indirect callback per edge, point, or bitset word.

The common engine has objective-specific node policies. A canonical-image node is an equivariant individualisation/refinement state. A minimum-image or transporter node can be an exact coset search state. These share frames, partitions, group operations, rollback, workers and verification; they need not pretend to have identical pruning semantics.

## 6. Objects, graph stacks, and auxiliary vertices

### 6.1 Semantic objects versus graph encodings

Give objects a native exact representation. A graph representation is a refiner and, where proved, a faithful encoding. It is not automatically the semantic object.

For nested objects, distinguish movable atom vertices, set nodes, tuple nodes, tuple-position ports, literal-value nodes, and root markers. Duplicate tuple entries need distinct position ports. Repeated multiset entries need multiplicities or distinct occurrence gadgets. Set semantics must not accidentally become list semantics through child storage order.

DAG sharing is storage-only unless reference identity is explicitly part of the schema. Either use a fully extensional, exact hash-consed DAG, or make occurrence encodings insensitive to storage sharing. An input with two equal subobjects stored separately must canonicalise like one with shared storage.

For an exact extended graph encoding E, require:

“g maps object x to object y if and only if g extends to a colour-preserving isomorphism E(x)→E(y).”

Keep base atoms Ω separate from auxiliary vertices Δ. All returned permutations act on Ω. Internal automorphisms acting only on Δ are encoding symmetries and must be discarded when projecting the group. Auxiliary IDs cannot enter semantic colours, ordered traces, or tie-breaks.

### 6.2 Generality without graph blow-up

Extended graph backtracking provides more expressive refiners than graphs confined to Ω, but an existence result for a perfect refiner is not a guarantee of a small or inexpensive representation. [R3]

In particular, do not encode a generator list as a distinguished set of permutation gadgets and claim to have encoded its generated subgroup. Another generating set can describe the same subgroup. Conversely, do not enumerate all elements just to make that encoding presentation-independent. Preserve subgroup and labeling-coset atoms in the native group layer; supply safe structural summaries and exact leaf tests.

### 6.3 Physical stack layout

Use immutable relation handles plus a local descriptor stack. A descriptor contains a relation ID, vertex-label layer, active restriction, coordinate-map handle and provenance. Singleton facts are short records. Sparse derived relations use compact edge slabs. Implicit relations such as “same G-orbit” remain orbit-ID arrays rather than cliques.

Several logical relations can share one physical adjacency structure. Distinguish their ordered relation identity and arc labels exactly. Merge only under an injective combination of relation labels. A bitwise OR that forgets which relation supplied an edge is not a faithful merge.

For a base-only canonical search, stop when all base atoms are singleton cells; residual auxiliary permutations need not be enumerated. The adapter must prove that all refinement and trace information read before that point is independent of auxiliary numbering. If it cannot, use a weaker refiner or an explicit auxiliary search. A base witness is always checked against the native object.

## 7. Canonical-image search specification

This section fixes a concrete canonical objective rather than leaving “choose the best leaf” undefined. It is a proposed engineering realisation of the canonical-tree principle; its permutation convention is the one in §3.

### 7.1 Tree semantics

At every node:

1. Append the branch's individualisation fact.
2. Run a fixed sequence of object and group refiners to the profile's fixed-point condition.
3. Produce an ordered partition P of the working domain and an exact node invariant I.
4. If the base partition is discrete, evaluate a leaf.
5. Otherwise select a non-singleton base cell by a fixed, equivariant rule and create one child for each atom in it.

The ordered partition is an over-approximation: every actual automorphism of the current constrained object preserves its cells. A cell need not be a true orbit. The initial fallback refiner is trivial; individualising atoms still terminates.

Select the target cell by a fixed tuple such as `(size, relation-degree signature, canonical cell position)`. Every component must be invariant under allowed renaming. Visit children in any order. Raw atom IDs may control visitation, but may not eliminate tied branches or decide which structural cell exists.

Object refinement must satisfy R_(x^h)(S^h)=R_x(S)^h for h∈G, with the corresponding extension on auxiliary vertices. Group refinement must be G-equivariant. These contracts are stronger than “seems to split the correct vertices on one instance”.

### 7.2 Leaf map and objective

Read the ordered singleton base partition as L=(l₀,…,l_(n−1)). Let t_L be the unique element of G sending L to its lexicographically least image L^G. Compute this by successive point-orbit minimisation and exact stabilisers. The full list makes the minimising permutation unique, even if intermediate transporters are not unique.

Let τ(L) be the concatenation of exact, framed node invariants along the path. The baseline invariant records the ordered cell-size vector after each prescribed refinement stage; richer invariants require a versioned profile. Define the leaf key as

**K(L) = (τ(L), enc(x^t_L)).**

Choose the lexicographically least key among all leaves. Return its second component and t_L. An explicit terminal marker makes variable-length traces unambiguous. Group normalisation is performed on the ordered target domain when a labeling coset is supplied.

Here is the short correctness argument. For h∈G, equivariance identifies the tree for x with the tree for x^h and sends L to L^h, preserving τ. The two lists have the same least G-image, so uniqueness gives t_(L^h)=h⁻¹t_L. Therefore (x^h)^t_(L^h)=x^t_L, and the sets of leaf keys agree. Every leaf image is in x^G. Taking the minimum consequently gives a canonical image. This argument also explains why scheduling must not define the tree or its invariant.

### 7.3 Group-refiner normalisation

A concrete baseline uses the ordered list F of currently fixed base atoms. Send F to its least G-image M using a transporter u. Compute the orbits of G_M, order those orbit descriptions on the fixed ordered target domain, and transport the result back by u⁻¹. The result is independent of the choice of u because two choices differ by a stabiliser of M. This normalisation avoids a first-encountered representative. More powerful orbital refiners use the same discipline. [R1, Appendix 7.1]

Cache the normalised fixed list, verified stabiliser and orbit result with exact keys. A cache hit changes cost only. It may not change which refiner is logically invoked, the ordering of its output or whether a fact enters the canonical trace.

### 7.4 Sound pruning

There are four initially approved pruning mechanisms:

| Mechanism | Justification required |
|---|---|
| Trace prefix worse than complete incumbent | First unequal, finalised token is larger; no descendant can change that token |
| Proven automorphism identifies siblings | Automorphism belongs to G, fixes x and the current branch constraints, and maps the whole subtree semantics |
| Exact duplicate state | Full state equivalence plus coordinate transport is known; equal hash or quotient is insufficient |
| Adapter-specific bound | Bound is valid for every remaining leaf and for the selected objective |

A shorter equal trace prefix cannot be discarded merely because an incumbent has more tokens. Tentative local traces are not complete incumbents. A lower bound on object bytes cannot override a better trace: compare the lexicographic pair in the specified order.

Use exact leaf equality to derive automorphisms. Maintain A_known≤Aut_G(x). Incomplete knowledge only reduces pruning. Before orbit pruning at a node, restrict A_known to the pointwise stabiliser of its branch choices, or verify preservation of the complete node constraints directly. Root orbits alone are not a valid pruning rule at descendants.

### 7.5 Generality and complexity

The scalar fallback refines nothing and explores full individualisations of Ω. It terminates for every finite input with exact action/encoding, potentially after factorially many leaves. If a refiner makes this tree impractical, a complete enumeration of G remains available under a separate minimum-image profile. No polynomial, quasipolynomial or singly exponential worst-case bound is promised for this practical engine.

For an external action adapter with no structural refiner, make this expense visible at problem construction. Generic completeness is a guarantee of a correct eventual answer under unbounded resources, not evidence that all object types receive equally effective pruning.

## 8. True minimum-image and transporter policies

### 8.1 Minimum-image coverage

A canonical-tree search can retain only a structured subset of orbit representatives. Therefore disabling its trace comparison does **not**, by itself, establish that the result is the global lexicographic minimum. Minimum-image mode must have a separate coverage proof.

Use nodes representing exact cosets C=H r. At a node choose a point a moved by H. For each b in a^H choose t_b∈H with a^t_b=b. Then

H r = disjoint union over b∈a^H of H_a t_b r.

The children cover every permutation exactly once at that split. Repeating until H is trivial enumerates G at worst. At a singleton coset, compare enc(x^r). This construction shares the group, stack, frame and scheduler components with canonical-image search.

Structural refinement can tighten safe candidate domains and lower bounds, but it must not discard a coset merely because its images have a nonpreferred canonical-tree invariant. The objective is the object encoding, not the canonical trace.

### 8.2 Bounds

Adapters can emit an exact forced prefix and an optimistic lower bound on the remaining encoding. If the bound is no smaller than the best complete image, prune for image-only mode. If all minimising witnesses or a full group are required, equality needs a separate coverage argument.

For subsets, use the declared bitstring or sorted-list order, not an implicit mixture. For graphs, a canonical fixed-length adjacency encoding permits bounds from assigned target endpoints; a sorted edge-list encoding requires different bounds. For general nested sets, child reordering can invalidate a naively forced prefix. Until a bound is proved, return “unknown” and continue.

The initial complete version may use only the trivial bound. Faster Linton-style set-specific logic is an adapter optimisation whose contract is checked against coset coverage, not a replacement for it.

### 8.3 Transporter and stabiliser search

Paired-stack search maintains source and target structures. An approximator must contain every true transporter; partition incompatibility can prove emptiness. At a discrete candidate, check bijectivity, membership in G and exact native-object equality.

Canonical-image mode may harvest useful automorphisms without proving that they generate the full stabiliser. The default implementation of `CANONICAL_LABELING_COSET` runs a complete stabiliser search seeded with these generators if completeness is not already proved. This uses the same engine with a different objective. A future fused traversal is allowed only with a group-completeness proof.

Arbitrary Boolean constraints can produce solution sets that are not cosets. Such inputs must request enumeration or one witness, unless closure has been established. Intersections of subgroup/coset/transporter constraints have the expected structure only when their mathematical hypotheses hold.

## 9. Permutation-group kernel

This is a major subsystem, not an incidental utility. Removing GAP from the runtime means implementing the required exact group operations, not merely rewriting the backtracking loop in C.

### 9.1 Required operations

Construct and verify a base and strong generating set (BSGS); sift permutations; compute point orbits and transporters; obtain pointwise stabilisers; change/extend a base; minimise an ordered tuple; enumerate a coset lazily; insert verified generators; compare groups and cosets exactly; and compute exact group order. Add intersections and normaliser/conjugacy services using the common search infrastructure where direct chain operations do not suffice.

Use deterministic Schreier–Sims as the reference constructor. A randomised constructor may propose a chain, but must finish with an exact verification that proves completeness. Checking only that input generators sift to identity is insufficient if the claimed stabiliser levels have not been verified. Require Schreier closure/strongness verification and provenance showing that chain generators lie in the input group. Never interpret a probabilistic chain as an exact proof of nonmembership or exhaustion.

### 9.2 Representation choices

Store dense permutations in contiguous image arrays, normally 32-bit entries. Detect identity, transpositions, small support and structured product elements as tagged forms where they save materialisation. Materialise a dense array only when the expected reuse pays for n writes and additional cache occupancy.

A chain level stores a base point, generator-index span, orbit list, point-to-orbit lookup and a compact Schreier tree. Tree entries identify a parent and generator edge; do not allocate a full n-entry transversal permutation for every orbit point. Inverse generators are explicit handles or cached arrays with clear lifetime rules.

The shared root group is immutable after verification. Worker changes use local chain fragments, generator-index overlays and bounded caches. Do not clone the entire chain at every search node or every steal. Structured Sym(n) and products of symmetric groups have exact specialised providers; they need not manufacture a generic enormous SGS.

### 9.3 Latency-aware transversals

Compact Schreier trees trade memory for dependent loads. Provide three levels:

1. Reconstruct rarely used transporters from short generator words.
2. Cache short words and selected jump ancestors for hot orbit points.
3. Materialise dense transporters only for hot entries or batch application.

Every cache has a byte budget and measured hit/reconstruction statistics. Flattening every transversal can turn a manageable chain into O(n times the sum of orbit sizes) storage; this is precisely the trade-off to avoid.

Do tuple minimisation incrementally. When only a few points are queried, apply generator words to those points instead of composing full permutations. If a word becomes long or most of the domain will be scanned, materialise once. Interleave independent point applications to expose memory-level parallelism, but do not expect SIMD to remove the dependency in p[q[v]].

### 9.4 Group objects and presentation-independent bytes

Subgroups occurring inside objects need exact normal forms on an already ordered domain. Sorting the input generators is invalid.

A complete baseline normal form uses the fixed base (0,…,n−1). At level i, let H_i fix 0,…,i−1. For each j in the orbit i^H_i, select the lexicographically least permutation in {h∈H_i : i^h=j}, obtained by successive constrained orbit minimisation. Emit these permutations in increasing (i,j) order, omitting identities by a fixed rule. They form canonical transversals and generate H. For a coset H r, additionally emit its lexicographically least representative. Thus the representation depends on the group/coset, not the supplied generators.

This deliberately conservative scheme may emit O(n³) point entries. Stream it and cache comparisons; do not describe it as a compact practical solution for all large group objects. A proven canonical-generating-set algorithm is the planned replacement if this dominates. Group-object normal forms and subgroup-conjugacy search are separate costs; normalising on an ordered domain does not solve conjugacy for free. Schweitzer–Wiebking explicitly treats canonical group representations and labeling-coset atoms. [R2, §9]

### 9.5 Working-set controls for group operations

A dense 32-bit permutation on 100,000 points consumes 400 KB. One thousand cached dense transporters consume 400 MB before indexing. This is large enough to dominate both group computation and the rest of the search working set. Count transporter bytes separately from generator bytes in every report.

Store generator-major contiguous arrays for full composition and scans. Benchmark small point-major tiles only for batches that repeatedly query the same points across several generators; keeping a full transposed copy doubles the footprint and is not the default. Reuse invariant ambient-group contexts across a batch. Cache only a bounded number of rebased chains, and key them by the exact ordered fixed tuple rather than an unordered set.

Short-circuit the trivial group, already fixed tuple entries, and symbolic full-symmetric cases. Drop globally fixed points from group storage only with an exact support map, retaining their object incidences and output positions. A small moved support can make restricted-group problems much cheaper than their full object size suggests.

If every input generator fixes x under its exact action, then the whole group fixes x: its orbit is a singleton. Return x immediately, with G itself as the complete stabiliser if requested. This shortcut is independent of the canonical profile. Other direct closed-form shortcuts must prove agreement with that profile, or advertise their own versioned profile.

## 10. Partition refinement and rollback

### 10.1 Required arrays

For N working vertices, maintain flat arrays:

| Array | Typical element | Purpose |
|---|---|---|
| `lab[N]` | uint32 | Vertices concatenated by ordered cell |
| `pos[N]` | uint32 | Inverse position |
| `cell_of[N]` | uint32 | Current cell handle |
| `cell_start`, `cell_len` | uint32 or uint64 | Active cell spans |
| `cell_order` | uint32 | Semantic cell order independent of allocation handle |
| `count[N]` | uint32/uint64 | Current splitter counts |
| `stamp[N]` | uint32/uint64 | Lazy clearing generations |
| `touched[]` | uint32 | Vertices/cells changed by current splitter |
| `worklist[]` | Compact descriptors | Pending relation/cell splitters |

Maintain `lab[pos[v]]=v` and exact coverage of the active domain. Integer widths are selected for a complete kernel family at input construction, not checked inside every edge loop. Use 32-bit IDs when N fits; independently use 64-bit offsets when the edge/incidence count requires them. If 64-bit IDs are unsupported in the first release, return a capacity error explicitly.

### 10.2 Sparse refinement

For splitter cell C and relation r, accumulate exact counts of appropriate incoming/outgoing arcs between each vertex and C. Traverse adjacency of C through the matching orientation, update only touched vertices, collect touched cells, and split them by exact signatures. Distinguish incoming/outgoing direction, arc type and multiplicity. Repeated updates to the same vertex are expected.

Use epoch stamps or touched-list reset to avoid clearing O(N) arrays for a tiny splitter. Handle epoch wrap with a complete reset at a safe boundary. For a simple unweighted relation, integer counts suffice. For many labels, use compressed per-vertex signature records and exact comparison; do not allocate a dense N×number-of-labels table by default.

Use a worklist with an amortised smaller-fragment rule where its standard partition-refinement preconditions hold. Retain a splitter already pending in the correct way when its cell splits. The resulting equitable partition can be computed near O((N+m) log N) for suitable sparse fixed-relation routines; do not promote that local bound to a bound on the whole search or arbitrary derived refiners.

### 10.3 Dense refinement

Represent an adjacency row as packed 64-bit words. Counts into a splitter mask use AND plus population count. Tile rows and word ranges so that the splitter mask and small count blocks remain cached. A singleton splitter often reduces to testing a single bit in each row; dispatch to it rather than scanning whole rows.

Keep sparse and dense relation kernels behind one exact signature interface. A relation may store sparse low-degree rows and bitset high-degree rows. Conversion decisions use storage and time estimates, expected reuse, and a fixed budget. They cannot change the semantic trace. For logical operation order, either produce the same prescribed splitter events or emit only canonical stage results whose ordering is independent of execution order.

### 10.4 Signature sorting

Use direct special cases for all-equal, binary and tiny-count splits. For small cells use in-cache insertion/small-array sorting; for larger fixed-width keys benchmark radix sorting against comparison sorting. Integer accumulation is exact, so traversal order cannot introduce floating-point nondeterminism.

Hashing may select buckets. Equal hashes must be checked by exact signature comparison before equality is asserted. Never use an incidental random hash seed to order canonical cells. A deliberately lossy invariant can be sound if it only weakens refinement and is fixed by the profile, but the baseline keeps exact signatures for auditability.

### 10.5 Reversible state

Use worker-local bump arenas and explicit frame watermarks. The baseline undo trail logs swaps/changed ranges, old cell metadata, stack lengths and group-overlay changes. Rolling back a frame restores all semantic state exactly. Scratch counts may be discarded by epoch rather than restored.

For a heavily changed contiguous region, saving the old region once can beat logging each mutation. The strategy is a physical choice with identical restored state. Trail bandwidth, inverse-map writes and write-allocate traffic must be measured. No per-node `malloc/free`; large allocator calls occur at problem creation, arena growth, task snapshots or output construction.

An iterative DFS frame machine avoids C stack overflow and enables suspension. Each frame holds branch progress, arena/trail watermarks, invariant length, group-view handle and objective state. Set/multiset object recursion also needs depth bounds or an explicit stack.

### 10.6 Refinement termination and semantic work order

The baseline profile fixes the logical order of relations, splitter cells, split signatures and stage boundaries. Within an unordered bucket, allocation or input order cannot assign a semantic colour. If two splitters tie, use their canonical relation/cell positions; do not break the tie by the smallest original vertex ID. Hardware execution may run independent counts in parallel, but commits their ordered results according to this specification.

Allocate a finite bounded auxiliary domain for each refiner invocation, or require a proved terminating generation scheme with a declared bound. Deduplicate already-appended facts by exact identity. After each complete scheduled sweep, continue only if the profile's progress measure strictly improves. The simplest baseline uses strict refinement of the finite working partition; stop after a full sweep without such progress. A stronger profile may also track a strictly shrinking exact group bound, provided the measure and termination proof are explicit. Appending another copy of the same graph is not progress.

This stopping rule can forgo useful refinement without harming completeness. What is forbidden is an unbounded loop that creates fresh auxiliary IDs or numerically fresh colour IDs for unchanged mathematical information.

## 11. Physical memory architecture

### 11.1 Four lifetime regions

1. **Problem region:** immutable atoms, relations, object DAG, normalised labels and verified ambient group.
2. **Worker region:** partitions, undo trail, counts, active group overlays, DFS frames and scratch encoding.
3. **Task region:** bounded snapshots and branch recipes for queued/stolen work.
4. **Publication region:** immutable incumbents and batches of verified generators, reclaimed by epochs.

Use structure-of-arrays where loops stream selected fields; use small packed records when every field is consumed together. Separate hot headers from cold diagnostics. Internal 32-bit indices/relative offsets reduce footprint where capacity allows. Arena slabs larger than the offset range need explicit segmented handles rather than silent truncation.

### 11.2 Sparse versus dense storage arithmetic

For a single simple directed relation with N vertices and m arcs:

- CSR with 32-bit destinations and 64-bit offsets costs approximately 4m+8(N+1) bytes before labels.
- Keeping CSR and CSC costs approximately 8m+16(N+1) bytes before labels.
- A row bit matrix costs N²/8 bytes, rounded to row alignment; storing its transpose doubles this.
- Per-arc 32-bit labels add 4m bytes per orientation unless an exact shared label representation is used.

Ignoring offsets and alignment, one bit matrix crosses one unlabelled CSR at density m/N²≈1/32. With equal numbers of stored orientations the same rough storage crossover applies. It is **not** a speed threshold: small splitters, degree skew, cache residency, bitplane multiplicity and conversion cost determine runtime.

Example: N=100,000 requires about 1.25 GB for one unpadded bit matrix. At m=1,000,000, bidirectional unlabelled CSR/CSC is about 9.6 MB. This makes indiscriminate “vectorise by making it dense” unacceptable.

### 11.3 Worker footprint

A practical starting budget for partition/counter/scratch arrays is 40–64 bytes per working vertex per worker, excluding undo history, subgroup data and snapshots. It is a planning estimate, to be replaced by exact accounting during implementation.

At N=100,000 and 48 bytes/vertex, mutable base state is 4.8 MB per worker. Sixteen workers consume 76.8 MB before trails or group caches. This can exceed the useful shared-cache capacity even when one worker behaves well. Auxiliary expansion increases N, sometimes dramatically.

The admission equation is

M_total = M_immutable + p·M_worker + M_tasks + M_group_caches + M_publications + M_output.

Apply hard byte budgets to every term. Reserve room for a complete rollback, generator verification and final output. If a budget is exhausted, evict reconstructible caches or reduce active concurrency; never delete constraints, pending coverage or exact comparison information. Oversized unavoidable state produces `RESOURCE_LIMIT`.

### 11.4 Cache and TLB policy

Prefer contiguous forward scans and compact vertex IDs. Keep relation data immutable and shared. Align bulk bitsets and queue metadata suitably; pad independent writers to avoid cache-line sharing. Do not pad every vertex record to a cache line.

Allocate worker arenas on their owning workers. Where the OS exposes NUMA, use first-touch placement and node-aware affinity. Shared-cache clusters on a single-socket chiplet processor are not necessarily separate NUMA nodes; query actual topology. Test per-cluster replication only for small, frequently read group metadata or relation indexes, never duplicate the whole object by default.

Large pages are optional for large long-lived arrays after measuring dTLB misses and allocation behaviour. They are not a default remedy for poor locality. Avoid per-node page tricks, copy-on-write mappings and page faults in the hot path.

### 11.5 Physical renumbering and blocking

Consider a one-time physical vertex ordering to cluster adjacency and counter accesses for very large sparse inputs. Keep a bijection between physical slots and semantic atoms. Permutations, incidence data and output maps must all use the correct coordinate view; tuple minimisation still compares the specified semantic target labels. A locality heuristic is not allowed to change the canonical ordering.

Evaluate preprocessing cost and the extra translation arrays against expected reuse. For a tiny/easy instance, leave numbering alone. For repeated hard searches on the same object/group, block adjacency by destination ranges so a counter tile fits in private cache. This may require a secondary edge index and a second pass. Admit it only when the saved random traffic exceeds the sorting/index memory cost. Immutable per-relation tiles can be shared across workers.

## 12. Bandwidth and latency model

Track bytes and dependency chains separately. An edge scan can have sequential adjacency traffic yet be latency-limited by random updates to vertex counters. A group operation can move few bytes overall yet stall on a chain of dependent generator accesses.

For a measured workload, let B_k be traffic through cache/memory level k, β_k its attainable bandwidth, D the longest relevant dependent-miss chain, ℓ its effective latency, and T_compute the execution-port/instruction lower bound. Use the diagnostic bound

T ≥ max(T_compute, max_k B_k/β_k, D·ℓ).

For Q random cache-line requests with average latency ℓ and at most q independent outstanding requests, Qℓ/q is another useful throughput bound. Real throughput may be worse because of TLB misses, branch recovery, bank conflicts, coherency, dependencies and scheduling. These are bounds for the implemented access pattern, not universal lower bounds for canonicalisation.

Do not derive bandwidth from DIMM marketing alone. Measure sustained read-only, read/write, random-update and loaded-latency behaviour for the actual machine and active core set. Vendor performance tools and optimisation manuals support this calibration approach. [R7–R10]

### 12.1 Per-kernel accounting

| Kernel | Useful data traffic | Hidden risk | Architectural response |
|---|---|---|---|
| Sparse splitter | Arc IDs plus count/stamp accesses | A 4-byte update can fetch/dirty a whole line | Touched sets, locality, tile large frontiers |
| Dense count | Bitset rows, masks, count output | Repeated full rows for many small masks | Singleton/sparse-mask paths, batching |
| Permutation composition | Two input arrays and one output | Gather misses; dependent indices | Lazy words, independent queries, cached compositions |
| Schreier traversal | Parent/edge records and generator lookups | Serial pointer-like dependency | Compact trees, short words, selective materialisation |
| Cell split/undo | Partition and inverse-map writes | Trail plus restoration multiplies traffic | Region snapshots versus delta logs |
| Leaf comparison | Image/encoding stream | Full graph rewrite for a losing leaf | Lazy transformed view and early exact mismatch |
| Task steal | Recipe or mutable-state snapshot | Cache-cold restart and large memcpy | Coarse tasks, checkpoint reuse, local stealing |

Example model, not a hardware measurement: if a node requires 200 MB of irreducible DRAM traffic and the active core set sustains 60 GB/s for that mix, the bandwidth floor is roughly 3.3 ms per node. Additional workers share that budget. Conversely, a dependent path of 100,000 last-level misses at an assumed 80 ns has an 8 ms serial latency floor. Wide SIMD cannot remove either floor without changing the access pattern.

### 12.2 Refinement value model

Estimate benefit as avoided downstream work, including bytes moved and group cost, against refinement cost and retained memory. A useful refiner can be slower per node while winning overall. Report both nodes and time; selecting refiners solely by nanoseconds per node is misleading.

For a fixed canonical profile, online timings may choose equivalent kernels and storage representations. They may not arbitrarily enable/disable semantic refiners or change target-cell selection. An adaptive semantic policy must itself be a fixed, equivariant rule based on mathematical features and versioned as part of the profile. Memory pressure cannot silently select a different canonical form.

### 12.3 Calibration and dispatch table

An optional short calibration command records a hardware profile; startup must not always run a long benchmark. At minimum measure:

| Measurement | Sweep | Decision it informs |
|---|---|---|
| Sequential read and read/write bandwidth | Array sizes across private cache, shared cache and DRAM; core counts | Copy/snapshot policy and concurrency cap |
| Random counter updates | Working-set size, skew and duplicate frequency | Sparse scalar versus blocked accumulation |
| Dependent and independent gathers | Chain length and independent streams | Word reconstruction versus dense transporter |
| AND/popcount | Row length, splitter density and ISA | Dense/hybrid dispatch |
| Task replay versus snapshot | Partition size, trail depth and destination cluster | Steal payload and grain size |
| Cache-to-cache publication | Batch size and cluster placement | Generator/incumbent polling intervals |

Persist these as performance hints keyed by CPU/topology, build and memory configuration. They do not identify a canonicalisation profile. If calibration is absent, use conservative scalar/sparse choices and collect low-overhead counters for the next explicit tuning run.

## 13. SIMD and microarchitectural specialisation

### 13.1 Dispatch

Ship a portable scalar baseline plus separately compiled ISA kernels. On x86, verify CPU features and OS extended-state support before using AVX-family instructions. If workers can migrate among heterogeneous cores, use a safe common feature set or bind each worker and dispatch for that verified core class. Do not assume AVX-512 exists on every recent Intel/AMD desktop.

Dispatch once per worker or large operation. Keep baseline translation units free of unsupported instructions. `-march=native` is acceptable for an explicitly local build, not a portable distributed binary.

### 13.2 Useful vector kernels

- AND, OR, AND-NOT, equality and first-difference scans on bitsets.
- Dense count reductions: scalar POPCNT, AVX2 lookup/bit-sliced techniques, and AVX-512 vector population count where the exact feature is supported.
- Fixed-width signature comparisons, radix-key construction, zeroing and bulk copies.
- Batched permutation queries or graph-label gathers when measured profitable.

AVX2 does not provide a general vector population-count instruction. AVX-512 vector-popcount features must be checked separately. Integer masks and exact reductions make scalar/vector equivalence testable.

### 13.3 Where vectorisation is conditional

Sparse counter scatter can contain duplicate destinations. A vector gather/add/scatter sequence may lose increments if duplicates are not combined; conflict handling can cost more than scalar code. Begin with unrolled scalar scatter and benchmark blocked aggregation for large frontiers. SIMD width does not repair poor cache locality.

Test AVX-512 against AVX2/scalar on whole workloads, including frequency behaviour and concurrency. Dense kernels may win while permutation-heavy mixed workloads lose. Keep thresholds calibrated by CPU class and array size.

### 13.4 Other techniques

| Technique | Decision |
|---|---|
| LTO and PGO | Use after a diverse training corpus; verify compiler/ISA reproducibility |
| Software prefetch | Experiment on long predictable adjacency/word walks; keep only measured wins |
| Loop unrolling | Expose independent work without exhausting registers or instruction cache |
| Non-temporal stores | Only large cold outputs not reused soon; avoid partition/trail arrays |
| Huge pages | Optional for measured TLB pressure on long-lived large regions |
| Branchless operations | Use when unpredictable branch cost exceeds extra work; retain early exits |
| Compact adjacency compression | Test decode cost versus reduced bandwidth; preserve random-access path |
| Hardware transactional memory | Not a required synchronisation mechanism |
| Manual assembly | Only for a proven bottleneck after intrinsic/compiler inspection |
| GPU | Optional future bulk preprocessing/batch backend, not the default irregular search engine |

An accelerator proposal must include transfer, launch, memory-residency and exactness costs. A desktop GPU's nominal bandwidth is not bandwidth available to a CPU DFS.

## 14. Multicore search architecture

### 14.1 Parallelism hierarchy

Use parallelism in this order, subject to workload:

1. Independent canonicalisation requests sharing an immutable group context.
2. Coarse independent subtrees of one hard search.
3. Large refinement kernels where outer search exposes too little work.

Never run all three at unrestricted concurrency. A global worker budget controls nested work. For many small objects, object-level parallelism usually avoids synchronisation and amortises group setup better than splitting each tiny tree.

### 14.2 Seed and frontier

Start a worker down one deterministic visitation path to obtain an early complete incumbent and initial symmetries. Other workers may start immediately on expensive problems, or wait for a short configured seed phase. Seed duration is an execution choice, not part of the canonical objective.

Expand to a bounded frontier of tasks. Estimate task cost from remaining cells, recent refinement/group costs and observed subtree work; estimates only choose scheduling. Use depth-first execution locally, expose older sibling tasks for theft, and preserve hot path state.

### 14.3 Task payloads

A task contains problem/profile IDs, an immutable checkpoint reference, a branch recipe, objective data and ownership state. The recipe identifies actual branch choices in the existing input domain; it need not itself be invariant because it is an execution record. Replay recreates the prescribed mathematical state exactly.

Two delivery formats are permitted:

- **Replay recipe:** few bytes transferred, repeated computation on the receiving worker.
- **Local snapshot:** flat mutable arrays and compact group-overlay data copied once, faster start but substantial traffic.

Choose between them using measured replay time versus snapshot copy plus cache-cold reconstruction time. Do not equate memcpy bandwidth with task-start cost. Checkpoint at coarse depths and cap queued snapshot bytes. A stolen task never holds pointers into the donor's mutable arena.

### 14.4 Scheduling and topology

Give each worker a padded deque and a private arena. Prefer stealing within the same shared-cache cluster, then across clusters, then across exposed NUMA nodes. This is a preference, not a restriction that leaves cores idle indefinitely. Large tasks justify more distant steals.

Use physical cores first. Benchmark SMT separately; it can hide latency but also competes for cache, execution resources and bandwidth. Heterogeneous cores receive tasks proportional to measured throughput; long critical tasks should not be stranded on a much slower core. Keep a configurable cap on simultaneously bandwidth-heavy tasks.

When bandwidth is saturated, more workers may increase elapsed time by evicting hot state and increasing loaded latency. Reduce active workers or mix compute-heavy and bandwidth-heavy tasks. Controller decisions may alter search order, never legal coverage or the canonical profile.

### 14.5 Incumbent publication

Publish an immutable bundle containing a complete trace, canonical encoding/view with stable lifetime, witness, profile ID and generation number. A coordinator or short publication lock compares candidates exactly and atomically publishes the winning bundle; multiword payloads are not updated piecemeal.

Workers poll at coarse safe points and keep local snapshots. A stale incumbent only misses pruning; it cannot cause an incorrect answer, because it is still a valid complete candidate and newer incumbents only improve the objective. Reclaim bundles through epochs or another explicit safe-lifetime scheme.

Do not send the whole best graph to every worker on every improvement. Publish a shared immutable encoding, small trace prefix and a handle. Compare locally and touch later bytes only when necessary.

### 14.6 Automorphism publication

Workers verify candidate automorphisms before submitting them. Batch generators to a coordinator or cluster owner; eliminate redundancy by exact membership tests and publish immutable subgroup snapshots. Workers may use any verified subgroup of the full stabiliser. Delayed updates only reduce pruning.

Generators are expressed on the original input domain. A worker storing a transformed coordinate view must conjugate them consistently before use. Restrict the subgroup to the current node before computing sibling orbits.

Avoid a shared mutable BSGS and a global union-find write on every leaf. A union-find of root orbits is not a substitute for a stabiliser chain at deeper nodes.

### 14.7 Task correctness and termination

Every task has one owner and a terminal state. Accepted states are pending, running, completed, and pruned with a valid justification; cancellation is separate. A parent is complete only when all child coverage is discharged.

If new symmetry merges queued branches, retain at least one live/completed representative. Use an ownership protocol with representative links pointing monotonically to a retained task; do not let two workers cancel each other's equivalent tasks. An in-flight equivalent task can safely finish even if it becomes redundant.

Completion detection must account for active workers, queued tasks, tasks in transfer, and child creation. Use an epoch-aware outstanding-task counter or a proven termination protocol. Observing empty queues alone is insufficient.

### 14.8 Determinism modes

Default: deterministic canonical bytes for a fixed profile, nondeterministic schedule, any valid witness. Strict mode additionally gives a deterministic witness after complete stabiliser computation. Debug replay records task choices and subgroup versions to reproduce execution; exact wall-clock interleaving is not part of the mathematical contract.

### 14.9 Intra-node parallel refinement

Enable cooperative refinement only when a single node has enough work and the pool lacks useful outer tasks. Borrow workers from the same scheduler; do not create a second OpenMP-style pool. One owner retains the node, partition and undo trail.

Dense counting partitions output rows among helpers, so each count has one writer. Helpers share read-only masks, use private accumulation and publish count blocks. The owner performs or commits cell splits in the prescribed semantic order. For bitset intersections over long rows, reduce partial counts exactly with explicit overflow bounds.

Sparse source-frontier traversal naturally scatters to the same counters. Do not put an atomic increment on every edge. Choose one of three measured strategies: output-owner traversal using the reverse index; bounded private accumulators followed by exact reduction; or destination-tiled edge batches with disjoint counter ownership. Private dense arrays cost O(pN) additional memory and clearing/reduction bandwidth, so they are inappropriate for a tiny touched set. For small splitters, remain serial.

Synchronise at refinement-stage boundaries, not per edge or cell. Cancellation waits for outstanding helpers before restoring a frame. A helper cannot mutate a node that its owner has rolled back. Barrier and reduction cost must appear in the dispatch threshold.

### 14.10 Grain size and concurrency admission

Let C_task be expected useful task time and C_transfer include queue, replay/copy, cold-cache and publication overhead. A starting target is C_task at least 20 times C_transfer, giving roughly a 5% transfer overhead budget before search duplication. This is a tuning target, not a universal constant; irregular tails require smaller late-stage steals.

Choose active p subject to memory capacity, exposed tasks and measured throughput. For fixed work with one-core compute time T_comp and total DRAM traffic B, a diagnostic scaling bound is T_p ≥ max(T_comp/p, B/β(p), T_critical). Here β(p) is measured aggregate bandwidth and T_critical includes serial dependency/coordination paths. Real search changes B and node count with p, so log those changes rather than fitting an unjustified linear speedup curve.

When the run becomes bandwidth-limited, test parking surplus workers between coarse tasks instead of busy-spinning. Maintain a short bounded spin for imminent local work and use OS blocking for longer waits. This preserves desktop responsiveness and reduces shared-cache/coherency traffic without changing the search result.

## 15. Expensive refiners and decomposition

Baseline refiners are cheap exact type/colour, incidence, equitable graph and normalised group-orbit refiners. Optional stages include:

- selected orbital relations;
- bounded lookahead/individualisation probes;
- stronger relational signatures;
- specialised subset/tuple incidence refiners;
- exact subsearch on a small auxiliary structure;
- bounded higher-dimensional Weisfeiler–Leman refinement;
- decomposition into components or blocks where mathematically justified.

Each stage declares peak temporary bytes, retained bytes, logical output order, termination measure, and its proof of equivariance. All-pairs orbital generation and 2-WL can require quadratic storage; higher dimensions are worse. Reject configurations that exceed budget before construction, or use an exact streaming/weaker profile explicitly selected before the run.

For fixed-profile portability, specify whether an expensive stage is logically mandatory. If mandatory and resources are insufficient, fail or checkpoint; do not silently omit it. A separate “economy” profile may deliberately use weaker refiners.

Disconnected graph components do not imply independent canonicalisation under arbitrary G. G can couple their permutations. Even for unrestricted graph canonicalisation, repeated isomorphic components bring a component-permutation group. Decomposition must retain the induced action, admissible cosets and reconstruction maps.

Likewise, independently canonicalising children and sorting their images can destroy shared-atom correlations. When replacing a subobject, retain its labeling coset and compatibility on overlaps. This is where the general-object theory is architecturally relevant. A full implementation of its recursive asymptotic algorithm is a distinct future backend, not an unnoticed consequence of this decomposition interface.

## 16. Exactness of caches and fingerprints

There are three different cache uses:

| Cache | Safe key/value contract |
|---|---|
| Computational memo | Exact same operation on exact same immutable state; recomputation gives identical result |
| Coordinate-transported memo | Verified isomorphism/transport map accompanies the reused result |
| Search dominance | Proof that all solutions of discarded state are represented or cannot improve objective |

Do not implement the third as the first with a larger hash. Two states with the same cell sizes or quotient graph can have very different descendants. A BSGS cache key must include group identity, base/fixed tuple and coordinate convention, not just subgroup order.

Use keyed/randomised hashes for adversarial hash-table resistance if desired, but resolve collisions exactly and keep hash iteration out of canonical order. Persistent digests are identifiers for lookup, not mathematical certificates. A test mode forcing all hashes to collide must still return the same canonical answer.

Use bounded per-worker caches first. Shared caches need enough reuse to justify locking and coherency. Batch immutable cache entries across workers only for common root/group computations. Eviction may cost time but cannot change output.

## 17. API, module boundaries and lifecycle

### 17.1 Public API specification

Provide opaque handles for context, frozen group, frozen object, problem, result and checkpoint. API names below are interface intentions, not implementation code.

| Operation | Inputs | Output/lifetime |
|---|---|---|
| `context_create` | Allocator, topology/thread policy, budgets | Context owning worker pool |
| `group_create` | Domain and generators/structured descriptor | Verified immutable reusable group |
| `object_create` | Schema/action and validated payload | Immutable native object |
| `problem_create` | Object, group/labeling coset, objective, profile | Frozen semantic problem |
| `solve` | Problem, cancellation/progress hooks | Status plus owned result |
| `solve_batch` | Problems, shared context | Per-problem statuses/results |
| `result_verify_witness` | Original problem and result | Exact witness validity; not independent canonicity proof |
| `checkpoint_write/read` | Problem and scheduler state | Versioned restart data |
| `result_encode` | Complete result, encoding version | Canonical byte stream |

Provide explicit retain/release or move ownership; no hidden global state. Callbacks may not recursively mutate the active problem. A context cannot be used concurrently unless the entry point documents it. Batch solves share immutable inputs safely.

### 17.2 Adapter contract

An adapter supplies action, exact equality, ordered encoding, leaf comparison, validation and optionally a refinement producer and lower-bound producer. Refiners write into engine-owned builders. They cannot alter worker scheduling, access an evolving global best to choose semantic cell order, or use timing in a canonical invariant.

Each adapter documents the group action, duplicate semantics, auxiliary extension if any, encoding proof, equivariance proof and bound proof. Unsupported refinement returns no additional information; unsupported exact action/encoding is an error. Built-in graph/set/group adapters have no runtime virtual dispatch inside their kernels.

### 17.3 Proposed source modules

`api`, `object`, `encoding`, `perm`, `bsgs`, `coset`, `partition`, `relation_sparse`, `relation_dense`, `refine`, `search`, `symmetry`, `scheduler`, `arena`, `checkpoint`, `cpu_dispatch`, `platform`, `metrics`, and independent verification/test executables.

Keep proof-sensitive semantics in small scalar modules. SIMD kernels implement explicitly specified array operations. The scheduler cannot invent pruning; it accepts pruning decisions and associated metadata from the search/objective layer.

### 17.4 C engineering rules

Use C17 with a documented atomics/OS abstraction. Audit size arithmetic before allocation; validate permutations as bijections and domain-consistent generators. Check multiplicity/count overflow. Avoid shifts by word width, unaligned type punning and assumptions about signed overflow. `restrict` applies only where non-aliasing is guaranteed. Wire formats never dump padded C structs.

Separate trusted frozen internal data from untrusted import parsing. Use sanitizers, fuzzing and strict warning builds in development. Expensive invariant checks are a diagnostic build option. Release code retains checks needed to prevent invalid inputs, corrupted checkpoints or exhausted budgets from producing a mathematical answer.

## 18. Checkpoint and restart

Checkpoint only at coherent safe points. Record immutable problem identity, schema/profile/encoding versions, complete incumbent, verified generators or reconstructible verified group data, queued branch recipes, running-task suspension state and coverage ownership.

Prefer portable recipes and compact data over raw address snapshots. Derived caches can be omitted and recomputed. On restart validate version compatibility and witnesses; reconstruct chains exactly. A checkpoint may resume with a different number of workers or ISA and must return the same canonical bytes.

For a large problem, first write new checkpoint contents and a checksum, then atomically publish its manifest. A crashed write must not replace the last complete checkpoint. Checksums detect corruption; they do not establish correctness of an incumbent or full search coverage.

## 19. Benchmark and measurement programme

This project succeeds on end-to-end performance, not a synthetic bitset loop. Benchmark graph-only and restricted-group problems separately, and report preprocessing honestly.

### 19.1 Workload families

| Family | What it exposes |
|---|---|
| Small random coloured graphs | Fixed overhead and easy discrete refinement |
| Sparse regular graphs and graph benchmark suites | Sparse refinement, difficult symmetries |
| Strongly regular and other weak-refinement graphs | Search explosion; lookahead value |
| Complete/empty graphs and repeated components | Large automorphism groups, orbit pruning |
| Directed, coloured, looped and multigraph cases | Adapter/encoding correctness and label traffic |
| Grid groups acting on subsets | Restricted-group search and group setup |
| Intransitive, imprimitive and wreath/product actions | Block structure and coupled components |
| Cyclic, dihedral, alternating and symmetric groups | Distinct chain/orbit regimes |
| Nested sets/tuples, large hyperedges | Auxiliary growth and object comparison |
| Subgroup/coset objects with varied generators | Presentation independence and normal-form cost |
| Huge sparse easy instances | Bandwidth/footprint rather than backtracking |
| Many small instances sharing one group | Batch throughput and amortisation |

Use standard hard graph families, including CFI-style constructions, with controlled sizes. Avoid presenting random-graph results as evidence for all canonicalisation.

### 19.2 Baselines and fair comparison

Compare graph cases against nauty, Traces and a suitable additional graph canoniser; compare general canonical images against Vole; compare set minima against an established minimal-image implementation; use GAP as an algebraic oracle. Pin versions/commits, build flags and exact settings. Vole's documented interface and inspected code demonstrate that native Rust search can still interact with GAP for group operations, so distinguish search time, setup and process/interface overhead. [R5–R6]

Different canonisers need not return identical canonical graphs. Compare equivalence decisions, valid witnesses and independently established automorphism groups. Byte-for-byte comparison is appropriate only for the same declared canonical profile, or for a shared explicit lexicographic-minimum objective.

Report both cold total time, including chain construction/encoding, and warm repeated solves with preprocessing amortised. For timeouts, report solved count and a defined penalised/censored statistic; never compare only averages over each solver's different set of solved cases.

### 19.3 Required metrics

Elapsed and CPU time; peak RSS and allocated bytes by region; visited/pruned nodes by reason; refinement rounds and arcs/words scanned; undo bytes; group orbit/sift/transversal counts; chain-build time; materialised permutation bytes; leaf comparison bytes; incumbent and generator publications; task sizes; steals; replay/copy time; worker idle time; and time to first complete candidate.

Use available hardware counters for cycles/instructions, branch misses, LLC traffic/misses, dTLB behaviour and memory-controller traffic. Counter availability and multiplexing differ by platform; record that limitation. Counter-estimated bytes and software-counted logical bytes are different quantities.

### 19.4 Core scaling

Test 1, 2, 4, … physical cores, all physical cores, then SMT; include per-cluster and heterogeneous-core configurations. Record S_p=T_1/T_p, parallel efficiency, and work inflation W_p/W_1. Parallel search may visit more or fewer nodes depending on incumbent/symmetry discovery, so speedup alone does not identify hardware scaling.

Add fixed-work kernel replay to separate bandwidth scaling from search-tree changes. Measure loaded latency while other cores run representative refinement, not just an unloaded pointer chase. Plot runtime and bytes/node against core count and active working set. Use medians plus dispersion/tail measures from repeated trials, pinned when appropriate, and record thermal/power conditions.

### 19.5 Optimisation acceptance

Every proposed optimisation provides: correctness argument, regression coverage, before/after wall time, memory impact, changes in node count and hardware-counter evidence where relevant. A kernel enters default dispatch only when it improves a declared regime without unacceptable regressions there.

Track a Pareto frontier of elapsed time, memory and robustness across instance families. Do not force one configuration to win all workloads. Semantic profile selection happens explicitly; physical kernel selection may be automatic within a profile.

## 20. Correctness validation and proof obligations

The following is a required future validation programme, not a claim that an implementation has been tested in this work.

### 20.1 Independent small-instance oracle

Enumerate all group elements for small degrees. Establish exact orbits, lexicographic minima, stabilisers and transporter sets using a simple independent program. Exhaust all manageable graphs/sets at the smallest degrees; sample more diverse objects and groups at larger small degrees. For the chosen canonical-tree profile, also write a slow exhaustive scalar tree evaluator so that pruning and parallelism can be compared against identical semantics.

A valid witness proves only that an output lies in the orbit. It does not prove canonicity or minimality. Independent enumeration, tree coverage and proof of pruning are necessary to validate those stronger claims.

### 20.2 Metamorphic properties

- C_G(x^h)=C_G(x) for every tested h∈G; C_G(C_G(x))=C_G(x).
- All returned g lie in G and satisfy x^g=c.
- For min mode, compare with the whole small orbit's explicit minimum.
- Reorder generators, invert them, add redundant generators and vary SGS construction.
- Permute edge/tuple/set insertion order and allocation order; vary DAG sharing.
- Vary worker count, task split depth, stealing, SIMD backend and cache budgets.
- Force hash collisions, epoch wrap, checkpoint/resume, and frequent generator publication.
- Rename the universe together with its labeling coset and verify coordinate-independent output.
- Compare full returned stabiliser order and membership with the oracle, not only generator validity.

### 20.3 Mandatory adversarial regressions

1. Empty and singleton domains; empty objects and trivial groups.
2. Repeated tuple entries and repeated multiset members.
3. Nontrivial subgroup restrictions where unrestricted graph relabeling gives a false equivalence.
4. The same subgroup with very different generating sets.
5. Auxiliary-only automorphisms and duplicate gadget occurrences.
6. Equal refinement traces from nonisomorphic graphs.
7. Root automorphisms that do not stabilise a deeper branch.
8. Two workers discovering competing incumbents while another prunes.
9. Newly equivalent queued tasks attempting simultaneous cancellation.
10. Disconnected objects with group-coupled components.
11. Noncommuting permutations revealing composition/inverse mistakes.
12. Graph colour renaming accidentally treated as permitted or forbidden contrary to the action.
13. Deliberate integer/size overflow, corrupted permutations and malformed checkpoints.
14. A profile where the canonical answer differs from the lexicographic minimum.

### 20.4 Proof ledger

Before release, every pruning/refinement module has a written lemma and a test mapping. The core obligations are: action consistency; injective encoding; refiner equivariance; approximator over-inclusion; complete splitting; well-founded progress; leaf-map correctness; trace-bound correctness; verified automorphism membership and node stabilisation; group completeness when claimed; and scheduler coverage under interruption and restart.

The short proof in §7 supplies the canonicality argument for the specified tree. Optimisations must preserve that argument or supply a replacement. A successful benchmark does not discharge a proof obligation.

## 21. Implementation sequence and release gates

| Phase | Deliverable | Exit condition |
|---|---|---|
| 0. Semantics | Conventions, encoding, profiles, adapters and proof ledger | Reviewable mathematical/API specification |
| 1. Exact scalar group core | Permutations, deterministic verified BSGS, orbits, cosets | Small-group oracle agreement |
| 2. Reference search | Trivial-refinement canonical tree and complete coset enumeration | Canonicality/minimum/witness properties |
| 3. Practical serial engine | Native set/graph adapters, sparse refinement, rollback, trace pruning | Agreement with reference; profiling baseline |
| 4. General objects | Extended incidence, native subgroup/coset atoms, complete stabilisers | Presentation/auxiliary/composition tests |
| 5. Memory optimisation | Flat storage, lazy clearing, compressed chains, bounded caches | Reduced measured traffic/latency and footprint |
| 6. Multicore | Coarse tasks, replay/snapshots, publications, checkpoints | Schedule-independent bytes; coverage/race tests |
| 7. ISA kernels | Dense/hybrid storage, SIMD and PGO | Exact scalar agreement and measured regime wins |
| 8. Advanced refinement | Orbitals, lookahead, decomposition, stronger signatures | Proof and Pareto benefit per feature |

Do not postpone BSGS engineering until after writing the search. Do not parallelise an unvalidated scalar traversal. Do not start by materialising every object as a dense graph. SIMD work follows measurement of actual hot kernels.

This is a substantial algorithmic-systems project. A prototype for graphs/subsets is much smaller than a production engine with exact arbitrary groups, group-valued atoms and deterministic multicore search. Calendar estimates should follow the phase-1/phase-3 measurements and the agreed adapter scope; a credible architecture cannot promise a fixed speedup or a short implementation schedule without them.

## 22. Concrete initial defaults

The first performance release should start with these defaults, all subject to the stated acceptance gates:

| Area | Default |
|---|---|
| Canonical objective | Fixed equivariant trace plus exact object encoding |
| Ambient group | Verified native BSGS; symbolic Sym/product shortcuts |
| Graph input | CSR/CSC; bitsets for measured dense/high-degree regimes |
| Refinement | Exact ordered partition refinement and normalised group orbits |
| Expensive refiners | Off in baseline profile; explicit stronger profiles |
| Integer widths | 32-bit IDs when valid; independently selected 64-bit offsets |
| Rollback | Local trail with contiguous-region snapshot escape hatch |
| Worker policy | Physical cores up to measured memory/throughput cap |
| Tasks | DFS locally; bounded coarse frontier; recipes plus checkpoints |
| Sharing | Immutable input, complete incumbents, batched verified generators |
| SIMD | Scalar and AVX2; AVX-512 selected only after capability/performance checks |
| Hashing | Acceleration with exact collision resolution |
| Full automorphism group | Complete stabiliser objective; never inferred from a partial harvest |
| Resource exhaustion | Evict optional physical caches, reduce concurrency, checkpoint or report limit |

The highest-leverage work is likely to be: preserving strong pruning, reducing repeated chain construction, keeping refinement/rollback state local, and avoiding migration of large mutable snapshots. Wider instructions help the regular kernels; they are not the central architecture.

## 23. Source basis and limits of the investigation

The architectural proposals, memory arithmetic, API, parallel protocol and validation plan are original design recommendations for this specification. References establish the algorithm families and hardware engineering basis. No existing implementation was benchmarked during this task, and no production C code was written.

**R1.** Christopher Jefferson, Rebecca Waldecker, Wilf A. Wilson, *Computing canonical images in permutation groups with Graph Backtracking*, arXiv:2209.02534, v4, 11 September 2023. Practical canonical-image framework; in particular §§2–4 and Appendix 7. [Paper](https://arxiv.org/abs/2209.02534) · [PDF](https://arxiv.org/pdf/2209.02534)

**R2.** Pascal Schweitzer, Daniel Wiebking, *A unifying method for the design of algorithms canonizing combinatorial objects*, STOC 2019; arXiv:1806.07466. General objects, labeling cosets, composition and canonical group representations. [Paper](https://arxiv.org/abs/1806.07466) · [PDF](https://arxiv.org/pdf/1806.07466)

**R3.** Christopher Jefferson, Rebecca Waldecker, Wilf A. Wilson, *Perfect refiners for permutation group backtracking algorithms*, Journal of Symbolic Computation 114 (2023), 18–36; arXiv:2112.05065. Extended graphs and expressiveness/limitations of refiners. [Paper](https://arxiv.org/abs/2112.05065)

**R4.** Christopher Jefferson, Markus Pfeiffer, Rebecca Waldecker, Wilf A. Wilson, *Permutation group algorithms based on directed graphs*, Journal of Algebra 585 (2021), 723–758; extended version arXiv:1911.04783. Graph-stack transporters, approximators and backtracking. [Extended paper](https://arxiv.org/abs/1911.04783)

**R5.** Vole project and manual, consulted 29 September 2026. The exposed manual identifies version 0.6.0 and describes canonicalisation under arbitrary permutation groups. [Project](https://peal.github.io/vole/) · [Introduction](https://peal.github.io/vole/doc/chap1.html) · [Native interface](https://peal.github.io/vole/doc/chap5.html)

**R6.** Vole source snapshot inspected at commit `37df59049f4e40ce9b1dde3ca4a0544984dba2dc`. Selected files: `rust/src/vole/search/mod.rs`, `search/checkers.rs`, `partition_stack.rs`, `trace.rs`, `solutions.rs`, `datastructures/digraph.rs`, and `perm/stabchain.rs`. Inspection showed reversible partition arrays, trace-based search, base/extended domains and GAP-related canonical-minimum handling. This was a selected-file architectural inspection, not a full correctness audit. [Pinned source tree](https://github.com/peal/vole/tree/37df59049f4e40ce9b1dde3ca4a0544984dba2dc)

**R7.** Intel, *Intel 64 and IA-32 Architectures Optimization Reference Manual*, current documentation landing page. [Documentation](https://www.intel.com/content/www/us/en/developer/articles/technical/intel64-and-ia32-architectures-optimization.html)

**R8.** AMD, *Software Optimization Guide for the AMD Zen5 Microarchitecture*, publication 58455. Consult for target-specific execution/cache guidance; no fixed latency/cache constants have been assumed in this spec. [Documentation](https://docs.amd.com/v/u/en-US/58455_1.00)

**R9.** Intel, *Memory Latency Checker*, documentation consulted 29 September 2026. Describes bandwidth, cache-to-cache and loaded-latency measurements. [Documentation](https://www.intel.com/content/www/us/en/developer/articles/tool/intelr-memory-latency-checker.html)

**R10.** AMD, *μProf*, platform performance measurement tools. [Documentation](https://www.amd.com/en/developer/uprof.html)

**R11.** nauty and Traces project documentation, search-tree and user-guide material. Used as a reference family and planned benchmark baseline, not as a promise of matching its canonical byte representation. [Project](https://pallini.di.uniroma1.it/) · [Search tree](https://pallini.di.uniroma1.it/SearchTree.html)

### Source interpretation

The initial premise is sound as an architectural direction: graph canonicalisation and arbitrary-group canonical images can share an engine. The necessary qualifications are that exact minimum image is a stronger specified objective, succinct group objects need native treatment, complete automorphism output needs a completeness argument, and stronger general theory does not transfer its complexity bound automatically. Those distinctions drive the interfaces and correctness conditions throughout this specification.
