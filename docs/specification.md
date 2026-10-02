# A native C engine for canonicalisation under permutation groups

**Architecture and implementation specification · 2 October 2026 · version 2.1**

Prepared for Tobias J. Osborne. Reviewed baseline: **v1.0**, 29 September 2026, commit **`7ad98cb`**, SHA-256 **`924699142d622de142e63f1145c91b436612aa88653e77311da8ecb2d2cfb431`**. Revision of v2.0 (30 September 2026, commit **`cb76c0d`**). The [referee report](../reviews/referee-report.md) remains a historical record. The [response](../reviews/review-response.md) maps its 18 findings and TensorGR T1–T14 to this revision; the [implementation plan](implementation-plan.md) defines future gates.

**Revision v2.1** (2 October 2026):

- §3.1: the reported complete stabiliser of a labeling is A on Ω, not A′; the signed labeling orientation σ_ρ is deferred (`UNSUPPORTED_ACTION` permitted).
- §3.2: status numeric values 0…7 in the listed order, frozen for the C ABI.
- §4.4: for n≤1 the `SIMPLE-UPPER-1` key is exactly `U32(n)`.
- §7.4: (0,2)^(pq)=(2,1) is stated as cycle conjugation.
- §8.2: the labeling objective computes and reports A on the source domain Ω.
- §9.1: the verifier checks level nesting of strong generator sets.
- §9.4: rule 1 blocks are disjoint and ordered (decoders reject otherwise); Group(1)=`01 00000000` at every degree n.
- §11.1: the logical work quota counts the profile-fixed traversal; default 2²⁰.
- §17: the `solve_batch` contract.

This is an executable design specification, not a claim that C code, Lean proofs, benchmarks or the release gates already exist. Estimates below are engineering judgments. Native C means a C17 library with controlled allocation and ordinary OS services, not a freestanding kernel.

## 1. Architectural decision

Share exact permutation/group operations, immutable objects, ordered partitions, search frames and verification infrastructure. Use objective-specific traversals: an equivariant tree for canonical images; disjoint coset enumeration for minima, transporters and complete stabilisers. Do not infer one objective's completeness from another's pruning.

The local canonical-image papers support the tree construction and group normalisation; the general-object papers motivate labeling cosets and composition. Supporting the same types does not transfer their asymptotic bounds to this engine (§23). Broad finite-group semantics and excellent measured performance on named regimes are separate goals. A complete slow fallback is mandatory; universally small runtime gaps are not promised.

The initial CPU core has no GAP, Python, Rust, C++ or GPU-runtime dependency. Portable scalar C is mandatory, with x86-64 Linux/Windows the first performance targets. ARM64 and an optional GPU plugin are separate ports. Small independent requests sharing immutable group contexts are a first-class regime, alongside a single large search.

## 2. Scope and semantic objects

### 2.1 Supported mathematical model and delivery scope

A finite indexed atom domain Ω has size n. An exact group G ≤ Sym(Ω) acts on immutable objects. Built-in schema `EXT-DAG-1` includes atoms, opaque byte literals, tuples, sets, multisets, permutations, subgroups, labeling cosets, coloured directed multigraphs and finite named relations. Subsets are sets of atom nodes; hypergraphs are sets/multisets of such sets; explicit codes are sets of tuples with a declared coordinate/symbol action. These interpretations must be selected explicitly, not guessed from data.

The first scalar delivery covers subsets, atom tuples and coloured directed multigraphs. General DAGs and algebraic atoms follow separate adapter gates. A recognised but unavailable type returns `UNSUPPORTED_ACTION`, not an approximation. Mathematical coverage of every finite object with a total exact action is provided by the reference model, subject to the finite machine capacities in §11 for an implementation.

Action `0x0001`, `ATOM-TRANSPORT-1`, maps atom a to g[a], fixes literal bytes and relation names, preserves tuple positions and multiplicities, and acts recursively. A permutation object p becomes g⁻¹pg; a subgroup H becomes g⁻¹Hg. A labeling-coset object Hρ (source Ω, fixed ordered target D_n) becomes g⁻¹Hρ = (g⁻¹Hg)(g⁻¹ρ). A permutation coset under conjugation would be a different action and is not this type. Graph colours and arc labels are fixed byte strings; they are never freely renamed.

Different row/column actions, symbol actions and tensor slot/dummy conventions require registered action/schema IDs and wrapper proofs. No custom callback may claim the built-in ID without proving it implements the same semantics.

### 2.2 Explicit non-goals

Approximate equality, infinite structures, continuous basis changes and implicit linear-code algorithms are outside this release. Signed **monoterm** symmetries are supported by §8.4. Multi-term Bianchi/cyclic/dimension-dependent identities need a linear-algebra layer over canonical monomials. Clifford multiplication needs an algebraic normal form, such as a declared antisymmetrised gamma basis, before this service. The engine does not supply these higher layers or assume that their coefficient arithmetic fits machine integers.

## 3. Actions, witnesses and results

Arrays store `p[v] = v^p`; products act left to right:

```
(pq)[v] = q[p[v]]
(x^p)^q = x^(pq)
H r = {h r : h in H}
A = Aut_G(x) = {g in G : x^g = x}.
```

Permutation arrays are source-to-target maps, not inverse images or lists of sources at target positions. If x^p=y^q then p q⁻¹ transports x to y. If x^p=x^q then p q⁻¹ fixes x. Inversion of a product reverses its factors.

Objective tags are uint16:

| Tag / operation | Complete mathematical result |
|---|---|
| `0x0001 CANONICAL_IMAGE` | §7 profile image c=x^g, g∈G; C_G(x^h)=C_G(x) for h∈G |
| `0x0002 LEX_MIN_IMAGE` | Least image in the named order (§§4.3–4.4), with g∈G |
| `0x0003 TRANSPORTER_ONE` | One g∈G with x^g=y, or complete empty result |
| `0x0004 STABILISER` | Generators for exactly A and a verified complete chain |
| `0x0005 CANONICAL_LABELING_COSET` | Canonical c and complete Aλ, with typed λ as below |
| `0x0006 TRANSPORTER_COSET` | Empty, or complete A g, x^g=y |
| `0x0007 SIGNED_CANONICAL_IMAGE` | Certified zero, or canonical c, sign s∈{−1,+1}, and proved absence of odd stabilisers |
| `0x0008 CONSTRAINT_ONE`, `0x0009 CONSTRAINT_ENUM` | One solution or exhaustion; respectively an exact streamed general solution set with completion flag |

A deterministic witness is optional metadata: minimise the image array among all solutions sending x to the selected c, either by complete enumeration or by minimising A g after A is proved complete. Canonical bytes alone do not require this cost. Witness choice is not equivariant under input automorphisms; the complete coset is the natural equivariant output.

### 3.1 Typed labeling-coset contract

Let Ω be the source, D_n={0,…,n−1} the ordered target, ρ:Ω→D_n a bijection, and Λ=Gρ. Compute x′=x^ρ and G′=ρ⁻¹Gρ ≤ Sym(D_n). Solve on D_n for t∈G′; return **λ=ρt:Ω→D_n**, c=(x^ρ)^t=x^λ. Since t=ρ⁻¹gρ, λ=gρ∈Λ. λ is not generally an element of G. The complete set of labelings taking x to c is **Aλ**: if κ∈Λ and x^κ=c, κλ⁻¹∈A, and conversely every aλ works.

For μ:Ω→Ω′, use x^μ, Λ_new=μ⁻¹Λ and output μ⁻¹λ, with A_new=μ⁻¹Aμ. If ρ_new=kρ, k∈G, G′ is unchanged and x^ρ_new is in the same G′ orbit, hence the canonical target bytes are unchanged. A′ on the target reconstructs to A=ρA′ρ⁻¹; the reported complete stabiliser is A on Ω, never A′ (§8.2). Each view retains its source/target domain handle and explicit bijection; never cast a labeling as an endomorphism.

The same reconstruction applies to signed problems using χ′(ρ⁻¹gρ)=χ(g). Supply an orientation σ_ρ∈{±1} with the convention [x]=σ_ρ[x^ρ]; return s=σ_ρχ′(t). On replacing ρ by kρ, set σ_(kρ)=χ(k)σ_ρ. On a pure coordinate rename μ use the same orientation for μ⁻¹ρ. These rules make the reconstructed sign independent of the labeling representative. An arbitrary bijection has no intrinsic χ value outside G.

The signed labeling orientation σ_ρ is deferred: an implementation may return `UNSUPPORTED_ACTION` for a signed labeling problem until it supports σ_ρ; M0 references return `UNSUPPORTED_ACTION`.

### 3.2 Completion versus validity

Status is a separate enum: `COMPLETE`, `CANCELLED`, `CAPACITY_LIMIT`, `RESOURCE_LIMIT`, `INVALID_INPUT`, `UNSUPPORTED_ACTION`, `OUTPUT_ERROR`, `INTERNAL_ERROR`. Their numeric values are 0…7 in this listed order (`COMPLETE`=0 … `INTERNAL_ERROR`=7) and are frozen for the C ABI. It is never encoded as a sign. Each result independently records `witness_valid`, `image_canonical`, `minimum_proved`, `subgroup_verified`, `stabiliser_complete`, `transport_exhausted`, `zero_certified`, `nonzero_certified`, and `encoding_complete`. False means unproved, not mathematically false.

Interrupted results may contain a valid candidate or verified subgroup. `COMPLETE` is relative to the requested objective: a positive transporter witness needs no negative-search coverage; an empty transporter, minimum, canonical image, nonzero sign or complete stabiliser needs its applicable coverage evidence. A zero certificate can complete signed search early without a canonical monomial. Incomplete bytes cannot be used as a canonical database key.

Evidence mode is explicit: `TRUSTED_ENGINE` records exhaustion asserted by the implementation; `CHECKED_CERTIFICATE` requires an independent checker of the objective's coverage and rules (§20). A valid witness alone never upgrades one to the other. Bounds carry an objective/order ID and region: a byte bound is not a trace-plus-byte bound.

## 4. Frozen encoding, orders and keys

### 4.1 Wire grammar `CDAG-2` (encoding ID `0x0002`)

All integers are unsigned. `U16`/`U32` are exactly 2/4 bytes, big endian. `B(s)=U32(len(s)) || s` for raw bytes. `Nat(k)=U32(b) || big_endian_bytes(k,b)` uses the shortest b, with b=0 for k=0 and no leading zero otherwise. Positive multiplicities require k>0. Lengths and counts must fit U32; overflow is a capacity error. No locale, UTF normalisation, implicit numeric coercion or floating-point interpretation occurs; a literal is its bytes.

A complete stream is:

```
43 4e 02 | U16(schema=1) | U16(action=1) | U32(n) |
U32(q) | record[0] ... record[q-1] | U32(root)
```

Unknown type tags, out-of-domain atom IDs and malformed fields are invalid; unknown schema/action/profile/encoding versions are unsupported, never reinterpreted. Here q≥1, all records are reachable from root, and record indices are implicit. Counts determine all variable boundaries; no padding or trailing bytes are permitted. Every child reference is a U32 index smaller than the parent index. Primitive record payloads are:

| Byte tag | Payload after tag | Meaning |
|---|---|---|
| `01` | U32(a), a<n | Movable atom |
| `02` | B(s) | Literal bytes |
| `03` | U32(k), k child references | Ordered tuple |
| `04` | U32(k), k increasing distinct child references | Set |
| `05` | U32(k), k pairs (child reference, Nat(m)), references increasing | Multiset, m>0 |
| `06` | Perm(p) | Permutation under conjugation |
| `07` | Group(H) | Subgroup under conjugation |
| `08` | Group(H), Perm(r) | Labeling coset H r, r its least element |
| `09` | n values B(vertex_colour), U32(e), e arc records | Coloured directed multigraph |
| `0a` | U32(r), r relation records | Named finite relations |

`Perm(p)=U32(s)` followed by s pairs `U32(i),U32(p[i])` in increasing i, exactly the moved support; unlisted points are fixed. Reject duplicate sources, fixed pairs, out-of-range targets or a nonbijection. This code also represents source-to-target labelings in their declared indexed coordinates. `Group(H)` is specified in §9.4.

An arc record is `U32(source),U32(target),B(label),Nat(multiplicity)`. Loops are permitted. Combine duplicate (source,target,label) arcs by exact addition, delete none with positive multiplicity, and sort by (source,target,B(label)); input zero multiplicities are invalid. Graphs with no arcs remain valid and retain all vertex colours. Labels compare by (byte length, unsigned bytes), including the empty label.

A relation record is `B(name),U32(arity),U32(k)`, followed by k records of `arity` atom IDs and `Nat(multiplicity)`. Names are unique, sorted by B(name); each name has one arity. Preserve empty named relations and arity-zero tuples. Combine duplicate tuples by summing multiplicities and sort tuples numerically lexicographically. Set-relation imports require multiplicity one and deduplicate instead. That import choice is resolved into the exact multirelation before solving, so a multiset count of two remains distinct from a set entry. Schema-specific wrappers for simple graphs reject loops and coalesce duplicate undirected edges before translating to two opposite unit arcs. The core never silently makes a directed graph undirected.

### 4.2 Extensional normalisation and canonical DAG references

Validate acyclicity with a bounded explicit traversal before interning. Source sharing, unreachable allocation records and insertion order have no meaning. Discard unreachable input nodes; bottom-up normalise reachable ones by exact type/payload/normalised children. Sets deduplicate equal children; multisets combine equal children and add positive counts. Hashes are lookup aids with exact collision resolution.

For each distinct normalised node define height 0 if it has no child references, otherwise 1+maximum child height. Number nodes by increasing height; within one height sort their exact record bytes using the already assigned smaller-height child indices. Equal records are one node. For sets/multisets sort by these child indices. This algorithm fixes numbering without expanding occurrences. The root is the last record: every proper descendant has smaller height. Validate imported canonical streams by reconstructing this normal form and requiring byte identity; repeated equal nodes, redundant references, leading zeroes, noncanonical orders or unreachable records are invalid canonical encodings.

Induction on height proves correctness: equal extensional nodes have equal child identities and payloads, hence equal records; unequal nodes have a differing tag, payload or child identity. The root's reachable closure is determined by its value, so representation-equivalent inputs give the same sequence and root. Decoding the sequence reconstructs the value, proving injectivity. This proof assumes exact equality/normal forms for group leaves (§9.4).

For x₀=literal, xᵢ₊₁=(xᵢ,xᵢ), the output has i+1 records and 2i references, rather than 2^i occurrences. Charge stored nodes D, references E_D, literal bytes, group payloads and actual output Z. Sorting and comparing records costs their actual lengths/prefixes and exact group-normalisation work; do not recursively re-expand shared children. Comparison may need two separately normalised closures; IDs from unrelated DAGs are not globally comparable.

### 4.3 Comparison and persistent keys

`CDAG-BYTE-1` (order `0x0001`) compares complete canonical streams by unsigned byte lexicographic order, with a proper prefix smaller. Fixed-width numeric fields thus compare numerically; a B field compares length first. The DAG numbering above, rather than an unspecified recursive tree order, defines this order. Comparing streamed or lazy views must reproduce precisely these bytes.

Profile tag `0x0000` means `NO_TREE` for coset-enumeration objectives; canonical and signed image objectives use P1 (`0x0001`). A persistent canonical key is the typed tuple `(schema, action, n, admissible_context, objective, profile, encoding, order, canonical_payload)`. Use exact canonical Group bytes for fixed-G context. A labeling context uses its declared source domain and canonical H r descriptor; coordinate-renamed contexts must be transported to a common domain before comparison. An application may instead supply an exact external context ID whose injectivity it guarantees. A generator hash alone is not a context ID. Signed contexts additionally include the exact character, e.g. its canonical lifted-group descriptor (§8.4).

For a nonzero signed result the payload is the canonical monomial bytes; the returned coefficient sign belongs to result metadata and to any higher-layer term key requiring coefficients. A certified zero has distinguished payload byte `00` under the signed objective and no monomial stream; ordinary CDAG streams begin `43`. Witnesses, trace certificates, subgroup harvests, capacities, scheduling and diagnostics are never appended to canonical object bytes. Comparing objects across objectives or contexts requires an explicit mathematical comparison, not byte coincidence.

### 4.4 A second frozen minimum order

`SIMPLE-UPPER-1` (order `0x0002`) is available only for uncoloured simple undirected graphs (empty vertex/arc labels, no loops, and exactly one arc in each direction for each edge): `U32(n)` followed by upper-triangle bits in order (0,1),(0,2),(1,2),(0,3),…; pack most significant bit first, pad the final byte with zero low bits. For n≤1 there are no triangle bits and no padding byte: the key is exactly `U32(n)`. Compare unsigned bytes. It is the minimum-search order; return the selected graph in CDAG-2 plus its order key. The fixed n header and padding cannot change comparisons within an orbit.

Its first k(k−1)/2 bits are zero in some relabeling exactly when an independent k-set exists. An all-zero prefix beats every prefix containing one; thus computing this minimum for G=Sym(n) decides Independent Set. This proves the public minimum interface includes NP-hard cases using the local Karp source (§23). It proves neither that arbitrary canonical-image output is NP-hard, nor an unconditional exponential bound, nor the same reduction for CDAG-BYTE-1.

## 5. Components and semantic independence

Use opaque immutable group/object/problem handles and worker-owned mutable workspaces. Modules: `api`, `object`, `encoding`, `perm`, `bsgs`, `coset`, `partition`, `refine`, `search`, `symmetry`, `scheduler`, `arena`, `checkpoint`, `cpu_dispatch`, `metrics`, plus an independent checker. Built-in adapters are statically dispatched in hot loops; callbacks run at bounded operation/stage boundaries.

Every storage representation R has a semantic interpretation decode(R). Validation, action, equality, normalisation, refinement, target selection, traces and encoding must factor through decode, up to declared coordinate transport. Formally, if decode(R)=decode(S), each semantic stage returns equal interpreted results, not merely isomorphic implementation tables. Separately require equivariance under the action. Equivariance alone does not prevent a redundant generator or duplicated DAG node from changing a trace.

This excludes SGS/base choice, generator order, allocation IDs, auxiliary numbering, edge insertion order and hash iteration from mathematical choices. Representation-equivalence is an explicit Lean interface hypothesis to discharge for built-ins. Metamorphic tests support but do not replace it.

## 6. Native objects, auxiliaries and composition

Profile P1 below allocates no auxiliary vertices: N=n. DAG/group/relational adapters initially use exact leaf semantics with weak refinement. Future incidence profiles may use atom/type/tuple-position/literal/root vertices, but must assign new profile IDs and prove their construction and ordering. Tuple-position ports distinguish repeated positions; set storage order does not create positions.

For an extended graph encoding E require: g carries x to y iff it extends to a colour-preserving isomorphism E(x)→E(y). All intermediate base partitions and traces, not just final isomorphism existence, must be invariant under auxiliary-only renaming. Project automorphisms to Ω; discard kernel actions on auxiliaries. Stop at discrete base atoms only if this invariant has been proved. No generator-list graph can stand for the generated subgroup without a presentation-independence proof.

At each node fix one finite auxiliary universe of at most N_max vertices before iteration, or supply a well-founded generation rank that covers both allocations and refinements. Per-invocation finite allocation is insufficient. Refiners never merge old cells or erase individualisations. A fixed universe permits at most N−initial_cell_count strict splits; every stage itself must terminate.

Disconnected graphs and independently canonicalised children can still share atoms or have G-coupled component actions. Decomposition must retain overlap constraints, induced groups, complete labeling cosets and reconstruction maps. Replacing children by bare canonical images and sorting is not authorised. Perfect-refiner existence and the recursive general-object theory do not establish small auxiliary size or cheap search for these implementations.

## 7. Frozen canonical profile P1

### 7.1 Exact scalar rules (`profile=0x0001`, `BASE-ORBIT-GRAPH-1`)

P1 uses N=n base vertices, all internal type tag `0x00`. On the top-level subset (a set consisting only of atom nodes), the initial key of a is membership 0/1; on a top-level graph it is B(vertex_colour[a]); on every other root it is the empty key. Empty sets qualify as subsets. Partition by equal keys and order cells by increasing key. All atoms are retained, including unused/fixed ones. n=0 has an empty ordered partition. Internal order of members within a cell is not semantic.

`split(P, sig)` replaces each old cell, in its old position, by its nonempty signature classes in increasing lexicographic signature order. It never reorders old cells or merges distinctions. Refine each node as follows, taking snapshots where specified:

```
append NODE(depth)
repeat:
    c0 = number of cells
    O: snapshot current ordered cells C[0..k-1]
       for a graph, sig(v) consists of exact counts, in order:
           for arc labels sorted by B(label), then cell index j:
               outgoing multiplicity from v into C[j],
               incoming multiplicity from C[j] into v
       for every other root, sig(v) is the empty vector
       P = split(P, sig); append STAGE_O(cell sizes of P)
    G: F = singleton atoms in current partition order (not branch order)
       M = lexicographically least F^G
       choose any u in G with F^u=M
       compute G_M orbits; sort each orbit's target labels increasingly,
           then sort the orbit lists lexicographically
       sig(v) = position of the orbit containing u[v]
       P = split(P, sig); append STAGE_G(cell sizes of P)
    if number of cells == c0: break
if all cells singleton: append LEAF; evaluate §7.2
else:
    choose cell minimising (cell size, cell position), among size > 1
    for EACH a in that cell:
        replace cell C in place by [{a}, C minus {a}]
        recurse at depth+1, with a fresh node refinement loop
```

A graph loop contributes once to each incoming/outgoing count. Count signatures compare lexicographically using the ordinary numerical order on mathematical naturals; machine acceptance uses §11. One complete no-change sweep is always recorded, even at a discrete root. Within O all signatures refer to its entry snapshot; G reads the post-O partition. Continue only after a strict split in the preceding sweep. No timing-dependent refiner, lookahead, auxiliary extension, extra graph layer or optional stage is part of P1. Additional useful profiles need assigned IDs and equivalent precision before release; no such future ID is silently substituted.

For O, counts and labels depend only on the semantic graph and ordered cell sets. For G, if F^u=F^v=M, u⁻¹v∈G_M, which preserves every one of its orbits, so the pulled-back ordered orbits agree. Under h∈G, h⁻¹u sends F^h to the same M and gives the transported partition. Thus both stages are representation-independent and equivariant, and preserve constrained automorphisms. Initial colours and target/individualisation rules have the same properties. Raw atom IDs may choose visitation order only.

### 7.2 Trace bytes, leaf map and proof

Trace tokens have assigned byte encodings:

| Token | Bytes |
|---|---|
| `NODE(d)` | `10 \|\| U32(d)` |
| `STAGE_O(sizes)` | `20 \|\| U32(k) \|\| U32(s_0) ... U32(s_(k-1))` |
| `STAGE_G(sizes)` | `21 \|\| U32(k) \|\| U32(s_0) ... U32(s_(k-1))` |
| `LEAF` | `00` |

Concatenate tokens from root to leaf. Compare unsigned byte lexicographically, with a proper prefix smaller; `00 < 10 < 20 < 21`. Framing is parsed, and no cell members or transporter choices occur in the trace. The objective is the lexicographic pair **(complete trace, CDAG-2 bytes of x^t_L)**, with trace compared first.

At a leaf extract L from the partition's singleton order. Find the unique t_L∈G minimising L^G numerically lexicographically. A constructive procedure starts t=id, H=G; at list position i set a=t[L[i]], choose b=min(a^H) and u∈H with a^u=b, then t←t u and H←H_b. This preserves already minimised entries; the final full list determines t uniquely. Intermediate transporter choices cannot change t.

For h∈G, tree equivariance maps L to L^h with unchanged trace. The lists have the same least image and uniqueness gives t_(L^h)=h⁻¹t_L. Therefore (x^h)^t_(L^h)=x^t_L. Leaf-key sets, and hence minima, coincide. The returned object belongs to x^G. Injective encoding then gives idempotence and orbit-equivalence detection within the same context. For n=0 the empty list has the unique identity action and the root is a leaf.

Finite termination follows from at most n strict cell splits per node, one last no-change sweep, total group/object operations, and at most n−1 individualisations for n>0. The unpruned tree has at most n! leaves (one for n=0). This is an upper bound on this traversal, not a problem lower bound.

### 7.3 Pruning and root fast paths

The unpruned evaluator above is normative. An optimisation needs a coverage lemma for this exact key: finalised trace-prefix domination, a checked node-stabilising automorphism and retained equivalent subtree, exact state equality with transport, or a bound for every descendant key. An equal shorter prefix is not worse; compare full framed tokens and preserve the possibility of a smaller next byte. A bound on image bytes cannot override a better trace. Never use a hash/quotient match as exact state equality.

Known automorphisms form a verified subgroup A_known≤A. At each node intersect with the stabiliser of its constraints before sibling pruning. Root orbits alone cannot justify deeper pruning. Verify implicit automorphisms by exact membership and object equality before publication. Image-preserving pruning need not preserve complete-group coverage (§8).

A discrete root returns its one leaf after the required final sweep; signed mode must still certify zero/nonzero. If every generator fixes x, the unsigned orbit is a singleton and returning x is equivalent to P1 regardless of trace. A caller requesting a trace certificate receives the prescribed trace or a checker rule for this shortcut. Signed mode in this case tests χ on generators: any odd one proves zero, otherwise all of G=A is even. Record root-discrete and singleton-orbit frequencies separately.

### 7.4 Hand-checked golden cases

The following abbreviations expand **exactly** via §§4.1 and 7.2: `H(n)=43 4e 02 00 01 00 01 || U32(n)`; `A(a)=01||U32(a)`; `S(ids)=04||U32(len(ids))||U32(ids...)`; `B0=02 00 00 00 00`; `T(ids)=03||U32(len(ids))||U32(ids...)`. Hex groups below separated by spaces are concatenated, not alternative encodings. Where identity attains the minimum key it is the least witness; elsewhere minimise among the witnesses that actually attain that key.

| Case | P1 derivation and complete stream |
|---|---|
| n=0, empty subset, G=1 | Empty partition; trace `10 00000000 20 00000000 21 00000000 00`. Witness empty. Stream `H(0) 00000001 04 00000000 00000000`. |
| n=2, x={0}, G=⟨[1,0]⟩ | Initial cells [{1},{0}]; no stage splits; L=(1,0), t=[1,0], c={1}. Trace `10 00000000 20 00000002 00000001 00000001 21 00000002 00000001 00000001 00`. Stream `H(2) 00000002 01 00000001 04 00000001 00000000 00000001`. |
| n=2, x={0}, G=1 | Initial cells [{1},{0}] remain in place although G-orbit signatures differ. L=(1,0), t=id, c={0}. Same trace as above; stream `H(2) 00000002 01 00000000 04 00000001 00000000 00000001`. |
| n=2, empty subset, G=Sym(2) | Root stages both [2]; two children both stages [1,1]. Least-witness leaf selects 0 first, t=id. Trace `10 00000000 20 00000001 00000002 21 00000001 00000002 10 00000001 20 00000002 00000001 00000001 21 00000002 00000001 00000001 00`. Stream `H(2) 00000001 04 00000000 00000000`. |
| n=2, one unit arc 0→1, all labels empty, G=Sym(2) | O signatures are (1,0) at 0 and (0,1) at 1, so cells become [{1},{0}]. G does not split; one further stable sweep is required. Trace `10 00000000 20 00000002 00000001 00000001 21 00000002 00000001 00000001 20 00000002 00000001 00000001 21 00000002 00000001 00000001 00`. Witness [1,0], output arc 1→0. Stream `H(2) 00000001 09 00000000 00000000 00000001 00000001 00000000 00000000 00000001 01 00000000`. |
| n=0, x=(b,b), b=empty literal | One height-0 literal, one height-1 tuple. Stream `H(0) 00000002 02 00000000 03 00000002 00000000 00000000 00000001`. Shared and duplicate b storage give these same bytes. Trace equals n=0 case above. |

For the second case CDAG-BYTE-1 minimum is {0}, with identity witness, whereas P1 returns {1}; the objectives differ deliberately. For n=2 empty subset with transposition character −1, [1,0] fixes x and certifies signed zero. For x={0} in the same signed group, A=1, P1 returns {1} with sign −1; {1} returns itself with sign +1. These sign cases are hand checked by listing the two group elements.

Hand-checked group payloads: `Group(1)=01 00000000`; on degree two, `Group(Sym(2))=01 00000001 00000002 00000000 00000001`. On degree three, C₃ generated by [1,2,0] is not the full symmetric group on its orbit; its greedy sequence has just [1,2,0], so `Group(C₃)=00 00000001 00000003 00000000 00000001 00000001 00000002 00000002 00000000`. For a labeling-coset payload on degree two, H=1 and r=[1,0] give `01 00000000 00000002 00000000 00000001 00000001 00000000` (Group then Perm). Each support pair lists source then target.

Public convention vectors use p=[1,0,2], q=[0,2,1]: pq=[2,0,1], qp=[1,2,0], p⁻¹=p, and (0,2)^(pq)=(2,1). The last is cycle conjugation: the cycle (a b) maps to (a^(pq) b^(pq)). If Ω=(a,b), ρ=[1,0], G=1, x={a}, then t=id on D_2 and λ=ρ, c={1}, Aλ={ρ}; returning id as the source labeling is wrong. Under μ swapping source coordinates, μ⁻¹ρ=id and the same target c results.

Wrapper gates must add graph loops/duplicate arcs, multiset counts, subgroup/coset presentations, free versus dummy tensor indices, error/status and ownership vectors before exposing those entry points. The vectors here pin the core, not the correctness of an unimplemented tensor reduction. Blind implementations must derive them from the rules, not copy expected constants (§20).

## 8. Complete objective-specific reference algorithms

### 8.1 Disjoint coset enumerator

All group enumeration services use the following exact reference, with zero pruning:

```
visit(H, r):                         # region H r
    if H == {id}: consume(r); return
    a = smallest atom moved by H
    for b in sorted(a^H):
        t_b = least image-array element of H with a^t_b=b
        visit(H_a, t_b r)
start visit(G, id)
```

Any transporter t_b suffices for coverage; the least choice freezes reference traversal for budgets/certificates. An element h∈H sends a to b iff h t_b⁻¹∈H_a, proving **H r = ⨆_b H_a t_b r**. Orbits have size>1, so each child stabiliser has smaller order; the faithful degree-n action bounds depth by n. Singleton leaves partition G with |G| leaves. This is a reference cost, not an unavoidable lower bound.

### 8.2 Consumers and completeness

| Objective | `consume(r)` and termination rule |
|---|---|
| Minimum | Compare the exact selected order key of x^r; retain the least. Exhaust all leaves for completion. |
| Transporter one | Test exact x^r=y; stop successfully on one hit. Declare empty only after exhaustion. |
| Stabiliser | Test x^r=x; insert every hit into a verified subgroup. Exhaustion proves every member of A was inserted and no other one was. |
| Transporter coset | Find one g, then run complete stabiliser consumer for x; return A g. All solutions r satisfy r g⁻¹∈A. If no g, return exhausted empty. |
| Canonical labeling coset | Run §7 on target coordinates, then complete stabiliser; reconstruct §3.1 and return Aλ. A is computed and reported on the source domain Ω (A=Aut_G(x)≤Sym(Ω)); its conjugate A′=ρ⁻¹Aρ on D_n is not the reported group. |
| Constraint one/enumeration | Evaluate the registered total Boolean predicate P(r); stop on first hit, or emit each hit exactly once and exhaust. No closure or coset claim follows from an arbitrary P. |

For internal subgroup intersection H∩K, enumerate H and test K-membership. For a normaliser inside ambient G enumerate G and test g⁻¹Hg=H. For subgroup conjugacy to K enumerate G and test g⁻¹Hg=K; the complete solution, when nonempty, is N_G(H)g. Coset intersections can use a known ambient enumeration with exact membership in both. These deliberately slow services specify reference semantics; specialised public APIs beyond `CONSTRAINT_*` are deferred until their cost/certificate gates. Predicate enumeration remains available within capacities. Callback nontermination is not repaired by finite group coverage.

Optimised pruning must either prove every omitted region has no relevant solution or map its solutions to retained coverage appropriate to the requested result. Keeping one best image is insufficient for full A or an enumeration stream. A stabiliser prune may omit solutions only with proof they lie in the final generated subgroup. A chain for discovered generators certifies that subgroup, not discovery completeness. Certificates record splits and exact empty/bound/representative justifications; trusted-engine exhaustion is a separate assurance mode.

### 8.3 Bounds and deterministic witnesses

The reference uses the bottom bound (unknown). A minimum bound must lower-bound the selected order key for every permutation in H r. Graph forced prefixes use the named adjacency order; sorted DAG records may move when children change, so unproved prefixes are forbidden. Equality with an incumbent can be pruned for image-only minima; all minimising witnesses/full groups need a separate solution-coverage argument. Deterministic witness minimisation adds a distinct search or coset-minimum cost and must appear in metrics.

### 8.4 Signed canonical images

The mathematical signed domain is the vector space over Q on unsigned orbit objects, with relations `[x^g]=χ(g)[x]`, where **χ:G→{±1} is a homomorphism**. More generally a coefficient field of characteristic not two works. Characteristic two and rings with 2-torsion require another contract. Arbitrary signs attached to generators are not automatically a well-defined character.

Input may supply signed generators. Validate χ by constructing the lifted generated group on Ω ⊔ {+,−}: each generator acts on Ω as given and swaps the last two points iff its sign is −1. Projection onto G is onto. χ exists exactly when the subgroup fixing every point of Ω in this lift is trivial: a kernel sign swap would give two signs for the same g. A complete chain/kernel calculation certifies this, or complete relation/provenance checking against a certified presentation may do so. Reject inconsistent signs as `INVALID_INPUT`.

If a∈A and χ(a)=−1, then [x]=−[x], hence [x]=0 over Q. One checked membership/action/sign witness is a complete **one-sided zero certificate**. If every a∈A is even, define a functional on the orbit by f(x^g)=χ(g). It is well-defined: equal images imply g h⁻¹∈A, hence χ(g)=χ(h). It respects the relations and takes f(x)=1, proving nonzero. Thus zero iff A contains an odd element.

Reference algorithm: enumerate the stabiliser as in §8.2, stopping immediately if an odd witness is verified. Otherwise exhaustion gives complete A; check χ=+1 on its generators, run/finish P1 for c=x^t, and return s=χ(t), so [x]=s[c]. If p and q reach c, p q⁻¹∈A makes their signs equal. Under x→x^h the nonzero sign changes to χ(h)s while c stays fixed; this covariance is the signed equivalence law, not invariance of the bare coefficient. Unsigned callers do not pay for this completeness search.

The lift g↦g̃ preserves products because swaps compose by sign multiplication, is injective, and projects back to g. Its action on (x,marker) therefore exactly models unsigned transport plus sign. This is the mathematical comparison with xperm-style signed permutations; an xperm wrapper's argument conventions and double-coset reduction still require independent proofs/tests. Ordinary canonisation of the lifted pair does **not** alone report algebraic zero: zero requires that (x,+) transports to (x,−), or equivalent odd-stabiliser evidence. A lifted profile can choose a different base canonical representative, so P1 equivalence must be proved before replacing the explicit-character path.

**Open obligation SIGN-COVER:** whether this arbitrary-G canonical tree, with proposed pruning, exposes enough automorphisms to detect every odd one without separate stabiliser enumeration. No such theorem is claimed. McKay–Piperno Theorem 5, Niehoff and TensorGR's graph prototypes are unverified leads for this purpose, not evidence for arbitrary G. Until proved in this convention, signed nonzero completion uses the exhaustive/complete-stabiliser route. Randomised helpers can find verified odd witnesses but cannot certify nonzero.

## 9. Exact group kernel and group encoding

### 9.1 Reference construction and certification

For ordered base (0,…,n−1), use a deterministic Schreier construction. At a level with generators S for K fixing preceding base points: include inverses, remove identities/duplicates, sort image arrays; build the orbit of a by queue traversal in increasing discovered-label order and sorted generator order, retaining t_b with a^t_b=b. For each b and s form **t_b s t_(b^s)⁻¹**, which fixes a. Recurse with the deduplicated nonidentity Schreier generators, finishing with a trivial subgroup. This direct recursion is finite and exact by the Schreier generation lemma. It can generate enormous intermediate sets; it is a correctness reference, not a claim of an efficient constructor.

The practical deterministic closure constructor sifts residues into lower levels, inserts failed residues with straight-line provenance, recomputes affected orbits/Schreier checks, and repeats until all checks pass. Each insertion must strictly enlarge the represented subgroup at a deficient level; finite subgroup growth bounds termination. A randomised constructor may propose the same data but exact verification is mandatory. The implementation milestone must fix insertion/rebuild policies and their operation bounds before performance claims.

The independent verifier checks input bijections, each generator's derivation from the original input, base-prefix fixation, orbit reachability via stored tree edges, orbit closure under level generators, transversal images, all Schreier residues' membership in the certified next subgroup, input-generator membership at the root, and terminal triviality. It also checks nesting: the strong generators certified at level i+1 are a subset of those at level i (as sets of image arrays), so K_(i+1)≤K_i is part of the certificate; without it a chain for Sym(3) of order 4 passes every other listed check. Induction gives both inclusions at every level and completeness. Testing only input generators against a guessed chain is insufficient.

### 9.2 Costs, provenance and operations

Let b be chain length, r_i orbit sizes, s_i active generators, w_i reconstructed word lengths, Q point queries, C dense compositions, and P provenance DAG nodes/edges. A direct closure pass processes Σ_i r_i s_i candidates; dense products cost O(n) entries each and naive dense sifting O(nb). Construction may need many passes; report their sum. Space includes stored generator support/dense bytes, Σ_i r_i tree records, lookups, residues, P and verification scratch. These are selected-algorithm bounds/ledgers, not universal lower bounds or a promised polynomial bound for the naive recursion.

Store shared straight-line derivations (`input`, `inverse`, `product`) with child indices and exact verification; do not expand long words repeatedly. Use compact Schreier trees, bounded word/jump/dense-transporter caches and exact immutable root chains. Charge verification as well as candidate generation. Group order is an exact multi-limb product ∏r_i; it is not uint64 in general.

Membership sifts g by repeatedly mapping the base image back with the corresponding transversal inverse and checking terminal identity. Point stabilisers use Schreier steps; tuple minimisation is §7.2; base change rebuilds/certifies for the requested ordered base (reuse is optional). Group equality checks mutual generator membership in complete chains; containment checks all generators. Enumerators and transporters use §8.1. Tests must include symbolic symmetric/product groups, small support, long Schreier paths, redundant generators and changing bases; benchmark build, verification, sift, rebase, tuple minimum and output separately.

### 9.3 Physical group representations

Dense permutations use uint32 images; tagged identities, sparse support and exact symbolic products avoid unnecessary n-entry materialisation. At n=100,000 one dense permutation is 400 kB, so 1,000 cached transversals cost 400 MB. Tree words trade space for dependent accesses; cache only within explicit budgets. Exact support maps must preserve unused atoms' object incidences and output positions. A group cache key includes the exact semantic group, ordered base/fixed tuple, coordinate maps and operation version, never just its order or generator hash.

### 9.4 Production `Group(H)` grammar and size proof

Use this deterministic priority, never whichever representation the caller supplied:

1. Compute the H-orbits on the ordered domain. H embeds in the product of symmetric groups on those orbits. If its exact order equals the product of orbit factorials, equality follows by finite containment. Encode `01 || U32(k)` followed by each non-singleton orbit as `U32(size), U32(points...)`; points increase, blocks order by least point. Omit singleton orbits. This includes trivial H as `01 00000000`, including n=0,1. Blocks are pairwise disjoint and ordered by least point; a decoder rejects overlapping or unordered blocks as `INVALID_INPUT`. Group(1) is `01 00000000` for every degree n (rule 1 with zero non-singleton orbits); the payload does not carry n, and decoders take n from context.
2. Otherwise encode `00 || U32(k) || Perm(g_1)...Perm(g_k)`. Start K=1; repeatedly choose the lexicographically least image-array g∈H\K and set K←⟨K,g⟩ until K=H. This canonical sequence depends only on H. Each step at least doubles |K|, so k≤floor(log₂|H|)≤log₂(n!)≤n log₂ n for n≥2; n=0,1 use rule 1. Sparse Perm records have at most n pairs, hence O(n² log n) point entries in the worst case.

To find the least outside element without enumerating H, descend in point-image lexicographic order through exact constrained cosets. For C=Jr, C⊆K iff r∈K and J≤K; discard exactly these branches and choose the least surviving point-image branch. Every level fixes another point; complete membership/containment and stabiliser operations make this constructive. The [local canonical-generators lemma](../review_sources/algorithms/1803.06858v1/articles/canonization.tex), lines 140–175, supports this approach; the proof above fixes our product convention and subgroup variant.

For a labeling coset H r, first choose its least image-array element r₀ by successive point constraints, then encode the canonical Group(H) and Perm(r₀). The left difference group of the coset is H, so both are determined by the set of labelings. This avoids confusing canonical representation on an ordered domain with subgroup-conjugacy canonicalisation. R2's subgroup-conjugacy corollary has a factor polynomial in group order, not in succinct generator length (§23).

The full fixed-base canonical-transversal stream survives only as diagnostic format `TRANSVERSAL-1`, never CDAG-2. For Sym(n) dense uint32 output is exactly 2n²(n−1) bytes: 1,998,000,000 at n=1,000; 1,999,800,000,000 at 10,000; 1,999,980,000,000,000 at 100,000, excluding framing. Production sparse/symbolic rules improve this contract but still require output budgets. A streaming sink saves memory, not emitted bytes; it must implement §17 backpressure and errors.

## 10. Refinement implementation and termination

Use flat `lab`, `pos`, `cell_of`, cell spans/order, count/stamp arrays and touched lists; maintain lab[pos[v]]=v and exact partition coverage. Semantic cell position differs from allocation handle. Mutable arrays are worker-private. Sparse adjacency and dense bitplanes must yield exactly P1's simultaneous signature vectors and stage results. An asynchronous splitter implementation cannot silently emit its own intermediate cell ordering into the trace.

CSR/CSC traversal visits only relevant incidences; duplicate updates accumulate exactly. Dense rows count AND/popcount against cell masks, with singleton/sparse-mask shortcuts. Signature sorting may use fixed-width radix or comparison sort, but hashes never order semantic classes. Epoch wrap triggers a full safe reset. A smaller-fragment O((N+m)log N) local algorithm may be used only after proving its scheduling, fixed-relation, key-cost and P1-equivalence hypotheses; no such bound is claimed for P1's repeated full sweeps or arbitrary callbacks.

P1 has a fixed domain and strictly increasing cell count between continuing sweeps. New auxiliary profiles obey §6's node-wide bound/rank and must preserve old distinctions. A custom callback is assumed total, deterministic, action-compatible and within its declared cooperative cancellation contract; registration cannot prove these universal facts. Noncooperative callbacks can delay cancellation indefinitely, which must be declared.

## 11. Capacity, rollback and all live memory

### 11.1 Numeric acceptance and schedule-independent capacity

The initial machine API uses 0≤n,N≤2³²−1, uint32 IDs/counts for wire list lengths, uint64 byte offsets checked against SIZE_MAX, and exact multi-limb counts/orders/multiplicities. Wire Nat byte length must fit U32. The initial lifted-character validator additionally requires n+2≤2³²−1, checked before constructing its two sign points; a future direct-character validator needs its own admitted bound. Built-in graph counts cannot exceed the total positive input multiplicity; compute that bound exactly during import. All sizes/products are checked before allocation; no wraparound or saturated arithmetic has semantic meaning.

A problem-time capacity descriptor fixes degree/node/reference/literal/output/count-bit limits. Validation is deterministic over the normalised input. For data-dependent output size, use a count-only canonical traversal/encoder or a conservative input-derived bound; the same policy must be used across executions. Exceeding that policy yields `CAPACITY_LIMIT`, a function of semantic input, objective and descriptor, independent of worker count or lucky early discovery. An API promising this property may conservatively reject an instance with a smaller actual output.

Requested managed-memory budgets use a deterministic admission plan: reserve a complete serial reference workspace plus bounded output/verification buffers; run optional work only from separate reserved slack. Spill/recompute/serialise before declaring a budget failure. A logical work quota, if offered, counts the nodes of the traversal fixed by the profile's policy for the objective: P1 `NODE` tokens plus §8.1 enumerator visits; the §7.3 root shortcuts are part of profile P1's policy and visit no tree nodes. A pruning policy that changes the count carries its own profile ID (PROFILE-EQUIV) and must give the same count in every execution mode and worker count. The count is a function of input, objective, descriptor and profile. The default quota when none is given is 2²⁰ (matching `include/canon/canon.h`). Wall-time/observed-node cutoffs are cancellation policies, not semantic capacity. A remaining unforeseen allocation failure is `RESOURCE_LIMIT` with cause `EXTERNAL_ALLOCATION`, and no claim of schedule-independent OS availability. Never let race-dependent transient peaks cause a purported input-capacity failure.

For a higher-layer rational collector, either exact arbitrary precision plus canonical final-range validation, or a deterministic conservative bound on all intermediate numerators/denominators, is required for schedule-independent capacity. Checking machine overflow in the arrival order is disallowed for that guarantee. Exact rational collection remains a higher-layer operation (§2.2), with its own budget contract.

### 11.2 Live-state equation and recomputation policy

At every phase account for:

```
M_live = M_input + M_normalised_DAG + M_root_group + M_provenance
       + sum_workers(arrays + frames + trace_history + trail + snapshots + group_overlays
                     + comparisons + verification_scratch + helper_buffers)
       + M_queued_and_in_transfer + M_publications_retired_or_live
       + M_caches + M_certificate_buffers + M_output_buffers + M_allocator_overhead.
```

Reserve bytes before creating every component, including replacement objects that coexist with old ones. Bound task/coverage-record slots and publications and drain or block producers when full; include retained link targets and proof summaries. When optional task slots are full, keep a single lazy remainder recipe and continue local serial DFS rather than creating an unbounded frontier. Reclamation lag counts. Host RAM and GPU VRAM have separate ledgers. Output/certificates can stream to a bounded sink; permanent sink storage is charged separately.

The strict memory fallback uses one mutable node state and O(n) branch recipes: rebuild from the immutable root to advance a sibling, discarding deeper arrays rather than retaining an O(Nd) trail. Reference group operations may similarly recompute and stream candidates. Deterministic size planning must bound their largest scratch state; if that one-task requirement exceeds the descriptor, return `CAPACITY_LIMIT` before search. Optional fixed-depth checkpoints/trails reduce recomputation within the reserved slack; eviction never removes coverage. The time tradeoff is replay up to the current depth per reconstruction, including group/refinement work.

A full trail of Θ(N) entries at d frames can be Θ(Nd); at N=d=100,000 and eight bytes/entry it is 80 GB. The earlier 40–64 bytes/vertex/worker is only an array planning estimate, not the budget. At 48 bytes and N=100,000 it is 4.8 MB/worker, 76.8 MB for sixteen, before every other term. Reducing concurrency cannot repair one oversized mandatory task.

### 11.3 Layout and locality

For uint32 destinations/uint64 offsets, CSR ≈4m+8(N+1) bytes and CSR+CSC ≈8m+16(N+1), excluding labels/multiplicity. One padded dense orientation costs 8N ceil(N/64) bytes. At N=100,000,m=10⁶ these are about 9.6 MB for the sparse pair and 1.2504 GB for one dense matrix. Storage crossover around density 1/32 is not a speed threshold.

Use contiguous regions, worker-local arenas and physically local allocation, with explicit maps for any locality renumbering. Query actual cache/NUMA topology. Huge pages, compression, transposed indexes, region snapshots and extra dense transporters are measured choices charged to the ledger, not assumptions of free storage.

## 12. Complexity ledger and performance bounds

### 12.1 Parameters and total cost

Report n base degree; N working vertices; m incidences; relation/label counts; total tuple arity; D/E_D stored DAG nodes/references; literal bytes; multiplicity bit lengths; input generator count/support/dense sizes; chain levels/orbits/word/provenance lengths; evaluated nodes V and leaves L; maximum depth; output/certificate bytes Z. Expanded semantic tree size is separate from stored DAG size. Signed character validation and complete stabiliser costs are separate terms.

A serial ledger is:

```
T = T_import_validate + T_group_construct_verify + T_normalise
  + sum_nodes(T_refine + T_group_node + T_split_restore_replay)
  + sum_leaves(T_tuple_min + T_action + T_compare)
  + T_signed_completeness + T_output + T_certificate_check.
```

Add wrapper conversion, batch setup, allocator and collection costs to an end-to-end application boundary. Charge actual relation work across all sweeps/layers, not just unique stored adjacency. Shared storage does not eliminate repeated scans. Custom callbacks carry their own input-size/runtime assumptions; computability supplies no useful uniform time bound. n! and |G| are reference traversal sizes, not universal demands. Neuen–Schweitzer's fixed-k WL-realizable IR lower bound applies only after proving a particular profile satisfies its hypotheses; native G refiners and arbitrary adapters are not automatically covered.

### 12.2 Three lower-bound categories and estimates

Use the [performance appendix](performance-lower-bounds.md) normatively: **U** problem/contract bounds, **A** specified-algorithm/work bounds, **H** hardware-model bounds conditional on justified demand and service ceilings. H can translate U or A into time; it is not a claim that the demand is universal. **E** calibrated/assumed engineering targets are estimates, never a fourth kind of proved lower bound. Observed unnecessary nodes, misses or undo writes cannot define U.

For a required work DAG take `max(max_j U_j/R_j^max, max_h B_h/beta_h^max, critical_path_min)`. B_h is mandatory transfer across a named boundary after optimal legal packing/reuse, not logical byte reads. Sum only stages proved sequential without overlap. A latency average or measured STREAM rate is an E parameter, not an absolute maximum service rate/minimum latency. Independent requests can overlap; count span and outstanding-request capacity.

The example is Ryzen 9 9950X, 64 GiB dual-channel DDR5-5600, RTX 5080 nominal 16 GiB VRAM, PCIe 5.0 ×16. Fixed-configuration interface ceilings are 89.6 GB/s DDR, 960 GB/s GDDR and 63.015 GB/s per PCIe direction. A mandatory 1.2504 GB broad row read has conditional floors 13.96 ms host DRAM or 1.303 ms resident GDDR; upload alone is 19.84 ms. These are not solve times or a GPU speedup proof. Local source provenance, clock qualifications, and unmeasured latency/cache/launch estimates remain in the appendix. Decimal GB and seconds are used for rates; GiB/MiB denote binary capacities.

### 12.3 Small-instance batches

Include a regime of many independent small objects (a workload choice, e.g. up to 128 base atoms), whose **measured live working set**, including group/adapter/output scratch, fits the relevant private/shared cache. Do not assume an 80n-byte model from another project. Reuse validated immutable groups, registries and workspaces; no per-call chain rebuild or allocator is required on an admitted fixed-capacity hot path.

Cache-resident computation can be dominated by instructions, dependencies and branch recovery while the batch still streams input/output and touches a collection table. Report compute-resource, fixed-work branch-recovery, mandatory stream and collection-insertion work/span bounds separately; branch misses and hash probes are conditional algorithm/hardware quantities, not universal canonicalisation floors. Collection latency is amortised by available independent requests, and end-to-end throughput includes setup and drain. Measure root-discrete fraction, wrapper/build/sort/encode time and actual automorphism structure. A count of identical factors does not establish an S_k action on the whole contraction structure. The appendix §6.7 specifies these equations without importing TensorGR hardware numbers.

### 12.4 Reproducible gaps

Every timed result with applicable L>0 reports Δ=T−L, ρ=T/L, δ=(T−L)/L, bound category/equation/assumptions and parameter ranges. Ratios are undefined for zero/unavailable L. Investigate T<L rather than clamping. A loose floor does not make all excess time avoidable; a fixed-DAG floor cannot establish optimality across algorithms with different work.

Pin clock/power policy, memory population, topology, compiler/ISA, versions, schema/action/profile/order, output mode and initial/final residency. Separate cold import/setup/solve/emission from warm cached contexts and amortised batches. Report CPU and GPU floors separately, fixed-work replay separately from full solves, all censored cases, work inflation and uncertainty. Engineering targets (e.g. transfer overhead below 5% or a kernel reaching 70% of a matching measured stream rate) require a versioned benchmark policy; they are judgments, not guarantees.

## 13. CPU kernels and optional GPU boundary

Dispatch scalar/AVX2/AVX-512 only after CPU and OS-state feature checks; heterogeneous workers use a verified common subset or pinned per-core dispatch. AVX2 has no general vector popcount; AVX-512 popcount is a separate feature. Duplicate scatter destinations require conflict-safe exact accumulation. Tail handling, overflow and exact stage ordering must match scalar semantics. LTO/PGO, prefetch, unrolling, non-temporal output and compression require measured whole-regime improvement.

The GPU is an optional capability `GPU_BULK`, with runtime/driver dependencies and a separate plugin API. Freeze immutable input buffers, device ownership, pinned-host staging, upload/download sizes, residency lifetimes and completion events before admission; the owner retains them until the final event. A CPU commit waits for all required exact count blocks. GPU resource exhaustion falls back only to a proved equivalent CPU kernel within P1, using reserved CPU capacity; otherwise return the applicable status. Initial release acceptance is CPU-only; GPU floors are future-backend opportunities. No floating-point TFLOPs denominator estimates integer group/refinement speed.

## 14. Parallel runtime and coverage state machine

### 14.1 Modes and ownership

`CALLER_THREADS` is reentrant single-threaded solving with an immutable shared context and one exclusively owned workspace per caller thread, no internal pool or hidden thread creation. `CORE_POOL` owns a bounded pool for independent requests or a hard search. Never nest unrestricted pools. Batch parallelism is the default small-instance policy; coarse subtrees precede cooperative intra-node work for large instances.

Recipes name actual branch choices and reconstruct the exact semantic node. Snapshot/copy/replay choices are physical, within budgets. Prefer local-cache tasks and physical cores; tune SMT and cross-cluster stealing with measurements. Sharing includes immutable complete candidates and verified subgroup snapshots only. A stale candidate/subgroup can lose pruning, never coverage.

### 14.2 Normative coordinator protocol

The first parallel implementation uses a single coordinator mutex for coverage metadata, queues, ownership and publication. Unlock is release; lock is acquire under the C17/OS abstraction. No lock-free deque or epoch algorithm is presumed verified. Immutable problem data is published before workers start. A cancellation request is an atomic release store and polled with acquire; mathematical state changes still occur under the mutex.

Each task has unique ID, region, objective, optional owner, recipe and state `READY`, `RUNNING`, `SUSPENDED`, `WAIT_CHILDREN`, `COVERED`, or `LINK`. A region denotes a subtree's relevant leaf keys or a coset's solution obligations. The invariant is that each root obligation has exactly one accounting route to a live task, a completed proof or a justified representative link. Mathematical search splits are independent of this accounting proof.

| Transition under mutex | Required action / linearisation point |
|---|---|
| Admit root | Reserve its record and serial workspace; insert READY before exposing work. |
| Claim/steal READY→RUNNING | Remove queue entry and set unique owner in one locked transaction. There is no unaccounted in-transfer interval. |
| RUNNING→WAIT_CHILDREN | First reserve/build all child records or a lazy remaining-region continuation; atomically attach their complete disjoint coverage and publish READY records. Parent waits; failed reservation leaves parent unchanged. |
| RUNNING→COVERED | Commit a checked leaf, valid objective bound/emptiness proof, or finished consumer contribution. Owner release is part of commit. |
| READY/SUSPENDED→LINK | Verify objective-specific equivalence and map to an existing retained lower-ID representative. Transfer any group/enumeration obligations; never link merely because images coincide. |
| Children all discharged | WAIT_CHILDREN→COVERED with child/link proof references; a LINK is discharged only after its target coverage is discharged. |
| Cancel/fail/suspend | RUNNING→SUSPENDED after helpers join; preserve region and recipe. It is not COVERED. Pending READY tasks remain covered by accounting but unfinished. |
| Resume | SUSPENDED→READY after reconstruction validation. |

Link IDs strictly decrease, so cycles are impossible. Retain link targets and proofs until all dependants and readers release them. New symmetries never cancel two representatives in favour of each other. Running redundant tasks may finish; do not revoke an owner asynchronously. A positive `TRANSPORTER_ONE` or zero-certified signed request may close the objective at the root by its sufficient certificate; abandoned regions then remain irrelevant to that objective, not falsely exhausted.

All references/publications are retained/released under the mutex; workers acquire a reference before unlocking. Retired payloads are freed only at reference count zero and with no helper access. Publish incumbent trace/bytes/witness as one immutable bundle after exact comparison; publish group snapshots only after verification. Parent records retain coverage summaries even when task workspaces are reclaimed. Collapse discharged children into their parent as soon as no representative link/reader needs them; stream any required proof records before reclamation. A summary retains the objective contribution and a durable proof reference where certificates are requested, not every completed task record forever. `COMPLETE` requires root discharge, no unresolved child/link obligations for that objective and committed result evidence, not empty queues or a zero ad hoc work counter.

### 14.3 Helpers, cancellation and R1–R3

Helpers receive immutable stage input, generation ID and disjoint output ranges/private accumulators. Only the owner commits partition changes in P1 order. Helper completion is counted under the coordinator; rollback, arena reclamation and suspension wait for all helpers. No per-edge atomic scatter is required. Cancellation latency includes the current bounded kernel/callback and verification stage; totality alone supplies no practical latency bound. Fair scheduling and available resources are assumptions for liveness.

The admissible fast-path/racing rules are:

- **R1:** every racer computes the same pinned function (action, objective, profile, encoding and sign/completion contract). Only a complete verified result can win.
- **R2:** a helper contributes only exactly verified automorphisms to the deterministic search. It cannot choose a new trace, discard coverage without a rule, or certify nonzero/completeness. Randomised discovery may use this rule.
- **R3:** a shortcut has a proof of equality to the pinned result, or answers only a separately requested pure equality test with its own contract. A fast isomorphism test does not return a canonical label.

Stopping a helper cannot lose mathematical coverage. Schedule, thread count, cache pressure and physical kernel selection cannot change complete bytes or the capacity policy (§11). Deterministic witness mode imposes its additional minimum; ordinary mode may return different valid witnesses. Stronger semantic refiners require a new profile, even if benchmark timing favours them.

## 15. Stronger refiners and decomposition gates

Orbitals, lookahead, higher-dimensional WL, component/block reductions and tensor incidence refiners are future profiles or proved P1-equivalent shortcuts. Each declares exact mathematical output/order, representation and action invariance, node-wide termination, peak temporary/retained size and objective-specific pruning theorem. Resource pressure cannot silently omit a mandatory semantic stage. An explicitly selected weaker profile is a different problem identity.

Tensor slot symmetries, identical-factor exchanges, dummy relabeling, metric/spinor-metric signs and Grassmann parity require a faithful reduction with fixed/free-coordinate rules and valid χ. Do not import the TensorGR prototypes' Sym(V) graph scope as a replacement for arbitrary G. Any narrower pinned simple-undirected-graph backend gets a separate profile and proof boundary; adapters into it need extension/projection proofs.

## 16. Exact caches and transported skeletons

Computational memoisation requires an exact immutable operation/state key. Coordinate-transported memoisation requires a verified bijection and action-compatible transport of the value. Search dominance requires a separate coverage theorem. These are distinct contracts.

A tensor skeleton cache key includes schema/action/profile/encoding, exact normalised contraction incidence, factor/slot types, fixed/free labels, variance and sign/metric/parity policy, admissible group/character, and the operation requested. Literal coefficients may be excluded only after proving they do not affect that operation. The cache must store exact key content plus source/target maps and proved-complete status of any reused group. Renamed skeleton reuse verifies the map and transports witnesses, stabilisers by conjugation and character coordinates; an unverified topology hash is insufficient.

Normalised group-refiner keys include G, ordered fixed tuple F (or M with its verified transport), ordered orbit convention and profile stage. A hit must produce exactly the same logical stage/trace as recomputation. Eviction changes time only. Randomised hashing resolves every collision exactly; forced-collision tests must still pass. Shared bounded caches require measured benefit over workspace-local ones.

## 17. API, wrappers, ownership and failures

Opaque retain/release handles: context, registry, group, object, problem, workspace, result, checkpoint. Input builders copy data by default. Explicit borrowed immutable buffers require a release callback and remain unchanged/alive until the last referencing handle is released. Transformed views retain their source and coordinate map. A workspace has one active owner; immutable contexts/registries/groups can be shared. `solve_batch` returns per-input statuses and preserves caller input order regardless of scheduling. It rejects the whole batch with `INVALID_INPUT`, writing nothing, only for a NULL workspace, NULL problem/result/status arrays with count>0, or a count whose byte size overflows; otherwise every input is attempted in caller order, and statuses[i]/results[i] are exactly what `solve` would return for that input alone (a NULL problem entry gives `INVALID_INPUT` for that entry only). The call returns `COMPLETE` iff every input was attempted; per-input failures appear only in statuses[]. Capacity admission and the quota are per input. Under `CALLER_THREADS` the batch runs sequentially on the one workspace; pool execution (§14.1) must reproduce the same statuses and bytes.

`group_create`, `object_create`, `problem_create`, `workspace_create`, `solve`, `solve_batch`, `result_verify_witness`, `result_encode`, `checkpoint_write/read` specify their domain/action/mode arguments explicitly. `result_verify_witness` checks membership and exact action, not canonicity. Hot small-instance entry points use plain fixed-layout descriptors and preallocated workspace, with no callbacks/heap allocation after successful admission; the general API may use bounded stage callbacks.

External adapters must supply total exact action, equality, comparator, canonical encoder and validation; optional refiner/bound callbacks need the stated invariance, monotonicity, auxiliary and coverage proofs. Adapter registration records assumptions, proof IDs and cancellation behaviour, not a claim the engine verified arbitrary code. Callbacks cannot mutate the active problem, choose semantics from scheduler state, or recursively enter the same workspace. Built-ins validate their declared schemas and have separate proof/test obligations.

Constraint-enumeration streams have unspecified arrival order and contain each solution exactly once on completion; they are not canonical byte streams. A result owns its immutable candidate/evidence; releasing a failed or partial handle is always valid. Allocator failure leaves the old state or a releasable partial state, never a fabricated completion flag. `result_encode` returns canonical bytes only when the corresponding image is complete, or the signed `00` payload when zero is certified; an explicitly named candidate-export API can export unproved objects. The sink receives ordered borrowed chunks valid only during its callback, with accepted byte count; `PAUSE` retains the offset for resume, `FAIL` returns `OUTPUT_ERROR`. No bytes are skipped or duplicated on resume. A final commit marker/length is required before a consumer treats a stream as a complete key. Sink failure may leave `image_canonical=true` and `encoding_complete=false`; it cannot alter the mathematical answer.

Every public entry point gets client-level golden/error/lifetime vectors before release. Test array direction, product order, inverses, source/target domains, fixed coordinates, character signs, duplicate policy and output-as-input idempotence through wrappers/FFI, not only internal kernels. An xperm benchmark wrapper must explicitly test name-to-slot versus slot-to-name, both halves of the double coset, and up/down dummy pairing; incorrect free-index declarations invalidate the comparison. These tests are obligations, not claims based on the surviving TensorGR header.

C17 code must check sizes, offsets, shifts, overflow, aliasing, ownership and feature preconditions; wire streams never dump structs. Sanitizers/fuzzing and exact invariants are future validation gates. Compiler/runtime/OS assumptions remain outside a pure Lean semantic theorem.

## 18. Checkpoints and restart

First delivery supports **trusted engine resumptions** only. Stop new claims/publications, request safe-point suspension, join helpers, and capture the coordinator's coherent root coverage graph, unfinished recipes, retained representative links, immutable problem/profile/action IDs, validated incumbent and verified group provenance. Hold the metadata lock while snapshot references are acquired; write payloads after releasing it. Save to a new file, complete its checksum/manifest, then atomically replace the manifest. A failed write cannot replace the prior complete checkpoint.

Restart validates versions, input identity, bounds, recipes, witnesses and group data, reconstructs workspace state and resumes unfinished coverage. Worker count/ISA can change under the same profile. Checksums detect corruption; witnesses do not prove retained coverage. An untrusted checkpoint requires an independently checked complete coverage certificate and parsing limits; until that milestone it is rejected as a resumable mathematical proof input. Crash safety, coverage correctness and independent certificate soundness are separate obligations.

## 19. Benchmark programme

Benchmark end-to-end operations with matched output guarantees and action scope. Families include small coloured graphs, sparse regular/strongly regular/CFI-style graphs, complete/empty graphs, repeated components, directed/looped/multigraphs, grid subset actions, intransitive/imprimitive/product/wreath groups, cyclic/dihedral/alternating/symmetric groups, deep DAGs/hyperedges, subgroup/coset atoms with varied presentations, huge sparse easy instances and many small requests sharing groups.

Add tensor monomials with signed slot symmetries, dummy relabeling, fixed free indices, metric/spinor signs and Grassmann parity; identical-factor products such as `(R_abcd R^abcd)^m`; Riemann chains; and cases where the actual stabiliser is much smaller than the permutation group suggested by factor count. TensorGR timings and missing prototype implementations are not measurements of this engine and imply no general complexity theorem.

Competitors: pinned nauty/Traces and Vole for matching graph/general-group tasks; established set-minimum software and GAP as algebraic oracles; correctly configured xperm full double-coset mode with both cached-SGS and per-call setup timings. SeQuant/bliss, Symbolica graphica and GraphCombinations.jl are **candidate competitors pending local source acquisition and scope audit**; no performance/scaling claim about them is established here. dejavu and Niehoff are likewise discovery leads, not verified canonical-result backends. Compare equivalence/witness/group results across different profiles, exact bytes only for an identical profile/order.

Metrics include import/group build/verification/rebase/wrapper/sort/refine/leaf/output/checker time; nodes/leaves/prunes; root-discrete and singleton-orbit fractions; arcs/words/point queries; permutation/provenance bytes; replay/undo; live memory by §11; queue/helpers/publication costs; cache hits; cycles/instructions/branch misses/LLC/dTLB/DRAM traffic where available. Distinguish logical from controller bytes and calibrations from ceilings. Report tails, dispersion, censored cases and solved counts over the same suite.

For scaling report speedup, efficiency and W_p/W_1 work inflation at 1,2,4,… physical cores, then SMT and cache-cluster configurations. Fixed-work replay separates hardware scaling from search changes. Optimisation acceptance requires a proof/certificate rule, regressions, before/after end-to-end time/memory/work evidence and declared regime. Aim for a measured Pareto frontier, not universal supremacy.

## 20. Proof and acceptance evidence

Before accepting semantics as frozen, require **two blind independently written reference implementations** using this document, agreeing on traces, bytes, signs, statuses under fixed capacity, and deterministic witnesses where requested. Cover oracle-sized cases and generated cases beyond oracle range. Plant an action/sign/encoding corruption and show the comparison catches it. Independent agreement can reveal ambiguity and bugs; it is not a proof of correctness. Implementations and seeds must be retained; unrebuildable prototype reports cannot satisfy this gate.

A separate tiny exhaustive oracle checks all orbit values, minima, complete stabilisers, transporters, characters and reference tree keys on small domains. Public API/FFI tests include noncommuting products, labeling-representative changes, renamed universes, graph duplicate/loop policies, DAG sharing changes, generator reorder/inverse/redundancy, forced collisions, capacity extremes and fault injection. Repeat across schedules, ISAs and checkpoint boundaries after those features exist. Signed cross-feed checks for nonzero inputs must include absolute coefficient conventions, e.g. s_A(C_B(x))·s_A(x)=s_B(x) when the two pinned functions agree on their canonical monomial and return +1 on it.

Proof layers are separate: Lean semantic action/tree/coset/termination theorems; certified constructive group operations; concrete adapter/encoding/refinement proofs; certificate-checker rules; concurrent coverage abstraction; C storage/memory/overflow/atomics and ISA refinement. Use an opposite-group/right-action wrapper to relate `(pq)[v]=q[p[v]]` to mathlib's `Equiv.Perm` multiplication, once, with a proved array correspondence. No axiom standing in for BSGS/adapter completeness can be described as a completed proof of that subsystem.

Initial assurance target: proved reference semantics and a small independently sound certificate checker. Producer certificates include coverage, group verification, branch/bound/symmetry rules and final objective claims; image witnesses alone are insufficient. Full stabilisers and signed nonzero need their stronger coverage rules. Count certificate generation/checking and size. Compiled Lean checker execution trusts its compiler/runtime unless separately justified; kernel-checked proof terms have a different boundary. No claim of C memory safety or liveness follows automatically.

The [finite review checks](../checks/review_checks.py) are sanity checks only. They do not run production C or Lean. Their exact coverage and revision results are recorded in the response; no benchmark or build is authorised as part of this document revision.

## 21. Roadmap and gates

The [implementation plan](implementation-plan.md) owns dependencies, effort judgments and exit evidence. This table must remain consistent with it:

| Milestone | Gate |
|---|---|
| M0 Specification and blind references | P1/wire/API vectors plus independent agreement and corruption sensitivity |
| M1 Lean semantic reference | Convention bridge, tree canonicality, coset coverage, termination and signed reference theorem |
| M2 Certified group kernel | Construction/verifier/provenance, tuple minimum, canonical group bytes and scoped operation bounds |
| M3 Concrete adapters | Graph/subset/DAG/algebraic encoding/action/refiner proofs, one adapter at a time |
| M4 Scalar C and certificate checker | Objective-specific complete reference behaviour, checked certificates, fault/fuzz/sanitizer evidence |
| M5 Batch and serial performance | Honest ledgers/floors, caller-owned workspaces, bounded memory and reuse, wrapper tests |
| M6 Concurrent abstraction and runtime | State-machine coverage proof, helper/reclamation/checkpoint tests, deterministic capacity |
| M7 C/runtime verification | Chosen C model/refinement, memory/overflow/atomics assurance stated independently of M1–M3 |
| M8 ISA and optional GPU | Scalar equality and measured regime wins, then optional residency/transfer-aware plugin |
| H0 Optional hex source audit | Local pinned commits and SHA-256 provenance before any hex-dependent work |
| H1 Optional reuse decision | Depends on H0 and the relevant semantic/group/adapter interfaces |

H0 is a future acquisition task, not performed here. It is not a prerequisite for the independent reference route. No optimisation bypasses an earlier relevant semantic/representation/scalar gate. Stop this revision after documents and light checks; implementation is a subsequent task.

## 22. Initial engineering defaults

Default objective P1 canonical image, CDAG-2 bytes, any valid witness; signed mode explicit. Use verified native groups with deterministic symbolic/product encoding priority. Use CSR/CSC and scalar signatures initially, exact bounded caches, compact provenance and replay-capable rollback. Small batches use caller-owned workspaces; a bounded core pool serves hard instances. Strict capacity admission precedes optional acceleration. Full groups, deterministic witnesses, certificates and streamed materialisation are explicit costs.

Live footprints, chain construction and fixed per-request overhead are initial performance risks. Wider vectors and more workers are considered only after profiling shows suitable work. These are engineering judgments to test, not findings from an implementation.

## 23. Local source basis and open evidence obligations

Literature ground truth is local primary material with provenance: [algorithm manifest](../review_sources/algorithms/manifest.json), [supplement](../review_sources/algorithms/manifest_supplemental.json), [formalisation provenance](../review_sources/formalisation/PROVENANCE.md), [hardware provenance](../review_sources/hardware/README.md). See the [algorithm audit](../reviews/algorithm-literature-review.md) and [formalisation audit](../reviews/formalisation-literature-review.md) for exact passages.

- Jefferson–Waldecker–Wilson, canonical images: [local TeX](../review_sources/algorithms/2209.02534v4/paper.tex), full-list minimum around 1226–1239; normalised refiner 1273–1300. §7 supplies the argument in this document's convention.
- Schweitzer–Wiebking, general objects: [definitions/DAG](../review_sources/algorithms/1806.07466v2/articles/heredit.tex), [replacement](../review_sources/algorithms/1806.07466v2/articles/objectReplacement.tex), [complexity/corollaries](../review_sources/algorithms/1806.07466v2/articles/hereditarilyFiniteObjects.tex). Its 2^{O(k)}N^{O(1)} algorithm is not implemented by this specification; its subgroup-conjugacy corollary depends polynomially on group order.
- Constructive generators: [local Lemma 21](../review_sources/algorithms/1803.06858v1/articles/canonization.tex), 140–175. §9.4 gives our ordered-domain subgroup/coset encoding, not conjugacy search.
- Refiner expressiveness: [local TeX](../review_sources/algorithms/2112.05065v2/perfect-refiners.tex), auxiliary encoding need not be small. Complete search: [local graph-backtracking TeX](../review_sources/algorithms/1911.04783v4/PermAlgoDigraphsJPWW_extended.tex).
- Neuen–Schweitzer: [local TeX](../review_sources/algorithms/1705.03283v1/ir-analysis.tex), hypotheses 298–307, theorem 311–319. Karp: [local primary scan](../review_sources/algorithms/Karp_1972.pdf), [p.93](../review_sources/algorithms/Karp_1972_page9.png), [p.94](../review_sources/algorithms/Karp_1972_page10.png); the scan has no text layer. §4.4 gives the additional reduction explicitly.
- Isabelle graph proof/checker precedent: [local paper](../review_sources/formalisation/papers/isocert-2112.14303v5/rules.tex) and pinned source in the formalisation audit. Its abstract checker proof is not end-to-end verification of the C++ executable. Pinned mathlib supplies groups/actions/Schreier's lemma; the local search did not find an efficient BSGS/canoniser there.

TensorGR's [learnings](../reviews/tensorgr-learnings.md) are design inputs and reported experiments with stated reproducibility limits. McKay–Piperno Theorem 5, Niehoff, SeQuant/dejavu and `leanprover/hex-graph-iso` / `leanprover/hex-perm-group` remain **unverified local-source dependencies/leads**. H0 must retrieve the hex repositories at exact commits, record hashes/licenses/toolchains, and inspect theorem/executable scope before reuse. Claimed pinned-nauty/simple-undirected and checkChain capabilities are hypotheses to audit, not established components. No network retrieval occurred in this revision. SIGN-COVER, adapter/backend faithfulness, optimisation coverage and concrete C/runtime refinement remain explicit future proof obligations.
