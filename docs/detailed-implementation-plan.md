# Detailed implementation plan

**1 October 2026 · aligned with specification v2.0 and the milestone plan**

The [milestone plan](implementation-plan.md) owns gates, dependencies and effort judgments. This document breaks each milestone into work packages (WPs) with concrete files, interfaces, algorithms, tests and exit criteria, against the repository scaffold introduced on 1 October 2026. Where the two disagree the milestone plan and the [specification](specification.md) win; this document must then be corrected. Nothing here authorises an optimisation that bypasses a gate.

Status words used below: **frozen** (spec v2.0 fixes it), **decided** (chosen here, revisable before the relevant gate), **open** (needs a decision at the named gate).

## 0. Ground rules for all work

1. **Convention.** Arrays store `p[v] = v^p`; `(pq)[v] = q[p[v]]`; `(x^p)^q = x^(pq)` (spec §3). Every function that composes, inverts or applies a permutation carries a comment stating which side it acts on. Tests include the §7.4 vectors `p=[1,0,2]`, `q=[0,2,1]`, `pq=[2,0,1]`, `qp=[1,2,0]`.
2. **Spec citations in code.** Each rule implemented in C, Lean or a reference evaluator cites the governing spec section in a comment at the point of implementation. A reviewer must be able to go from any branch in the code to the sentence that mandates it.
3. **No golden constants in implementations** (spec §20). The expected hex in `refs/vectors/golden.json` is used by tests only. An implementation that embeds an expected byte string fails review.
4. **Representation independence is tested, then proved** (spec §5). Every semantic function gets a metamorphic test over: generator reorder, inverse and redundant generators; DAG sharing versus duplication; graph arc insertion order; coordinate renaming. Passing these does not discharge the §5 obligation; M1/M3 proofs do.
5. **Machine load.** Builds and tests are small. Anything non-trivial runs under `nice -n 19`. No benchmark is run before M5, and no dependency is fetched without the maintainer's go-ahead (HANDOFF).
6. **Evidence.** Literature claims cite local primary sources only (`review_sources/SOURCES.json`). TensorGR numbers are leads, never measurements of this engine.
7. **Git.** Explicit paths only; never `git add -A`. Push to `main` only when asked. Blind reference implementations are sealed by recorded commit hash and never edited afterwards except through the divergence protocol (§3.7 below).
8. **Status and flags.** Every result carries `canon_status` and the nine independent flags of spec §3.2. A false flag means unproved. No code path may set `image_canonical` without the coverage evidence its objective requires.

## 0a. Delivery process: vertical slices (decided 1 October 2026)

The milestones above are gates. Delivery is organised as **vertical slices**: each slice carries one thin, end-to-end path from public API to canonical bytes and tests, across however many `src/` modules it needs, rather than completing one module at a time. Rules:

1. **Foundation before proofs.** Lean work (M1, §4 below) is deferred until the foundation slices S1–S6 have landed. Theorem statements stay recorded in the `lean/` docstrings; nothing in S1–S6 waits on a proof. The M1 gate still precedes the "mathematical reference complete" review.
2. **Implementer and reviewer are separate.** An Opus agent implements each slice from a written brief. The slice ends with a code review by a different agent or session against the brief, the cited spec sections and the tests; findings are fixed before the next slice starts. A slice is not landed until its review is closed.
3. **Every slice is green end to end.** `make test`, `make check`, the CMake build with sanitizers and CI all pass at the end of each slice. No slice leaves a failing or skipped test.
4. **Backends are swappable behind interfaces.** Early slices may use deliberately simple backends (explicit group enumeration, no pruning) behind the interfaces of §2; later slices replace the backend and must reproduce identical bytes on the accumulated test corpus. Capacity limits make the simple backends deterministic (§11.1), never approximate.
5. **End-to-end oracle from S1.** Each slice extends `tools/canon-cli` (emits `refs/compare/FORMAT.md` records) and the Python driver `tests/python/test_e2e.py`, which compares the C path against the Python model in `checks/review_checks.py` over the exhaustive T1 tier and against `refs/vectors/golden.json`. This is the running regression corpus for backend swaps.
6. **M0 adjustment.** The production C path is not blind with respect to `checks/review_checks.py`. The blind reference for the M0 gate is therefore **ref-b in Julia**, commissioned after S6 from an author who has not seen `src/`, `checks/` or `tools/`. A sealed snapshot of the unpruned C path serves as ref-a. Until ref-b exists, the Python model is a cross-check, not a blind reference, and semantics are not frozen.

### Slice schedule

| Slice | End-to-end path | Modules touched | Backend simplifications allowed | Lands plan WPs |
|---|---|---|---|---|
| **S1** | Subset under a small group → P1 canonical image → CDAG-2 bytes, trace, witness, status and flags, through the public API and `canon-cli`; T1 agreement with the Python model on all 40 subgroups of `Sym(n)`, `n ≤ 4`, all subsets | perm, group (explicit backend), object (subset), encoding (wire primitives, subset stream), partition, refine (initial key, G stage), search (unpruned tree, leaf map), api, tools, tests | Groups as explicit element lists under a capacity on order; no decoder; no pruning | 2.1, parts of 3.1, 3.5, 4.1–4.3, 4.6 |
| **S2** | Coloured directed multigraph → P1 with the O stage → graph stream; the §7.4 one-arc case; T1 digraphs with multiplicities `≤ 2` for `n ≤ 2`; `SIMPLE-UPPER-1` key | object (graph, CSR/CSC, labels), refine (O stage), encoding (graph record, Nat) | as S1 | 3.4, 4.2 |
| **S3** | Verified stabiliser chain replaces explicit enumeration: build, independent verifier, sift, order, rebase, tuple minimum, pointwise-stabiliser orbits; byte identity with S1/S2 corpus; T2 random groups up to `n ≤ 8` | bsgs | Provenance DAG may start as input/inverse/product records without compaction | 2.2–2.5 |
| **S4** | Coset enumerator and the enumeration objectives: `LEX_MIN_IMAGE` (both orders), `TRANSPORTER_ONE`, `STABILISER`, `TRANSPORTER_COSET`; oracle agreement on T1/T2; canonical `Group(H)` bytes for the stabiliser answer | coset, search (consumers), encoding (Group/Perm records) | Greedy generators by constrained descent; no pruning | 2.6, 2.7, 4.4 (part) |
| **S5** | Nested objects: tuples, sets, multisets, literals, permutation/subgroup/coset leaves; extensional normalisation; CDAG-2 decoder and validator; `canon_object_create` from a stream; sharing tests to depth 60 | object (dag), encoding (decoder, validator) | none | 3.2, 3.3, 3.6 |
| **S6** | Labeling cosets and signed images: typed λ = ρt, complete `Aλ`, χ validation by the lifted group, zero certificate, nonzero via complete stabiliser, sign covariance tests, cross-feed `s_A(C_B(x))·s_A(x)=s_B(x)` | search (objectives 0x0005, 0x0007), api | none | 4.4 (rest) |
| **S7** | Certificate v0 and the independent checker; first prune (node-stabilising automorphism) with its checker rule; mutation rejection | symmetry, checker | checker uses its own slow refinement | 4.5, 4.7 |
| **S8** | Capacity admission, live-memory ledger, metrics, fault injection, fuzz targets; `solve_batch`; output sink pause/fail | arena, metrics, api | none | 4.8, 5.1, 5.2 |

After S8 the M0 gate (ref-b commissioning, corpus tiers T2/T3, planted corruption) and M1 follow; M5 benchmarks and M6 concurrency are scheduled only after M0 closes.

## 1. Layout to milestone map

| Path | Milestone | Governing spec sections | Contents when complete |
|---|---|---|---|
| `refs/vectors/`, `refs/oracle/`, `refs/compare/` | M0 | §§7.4, 20 | Golden corpus, exhaustive oracle, generated-case corpus, comparison harness |
| `refs/ref-a/`, `refs/ref-b/` | M0 | §§3, 4, 7, 8, 9.4, 11.1 | Two sealed blind evaluators |
| `lean/Canon/` | M1 | §§3, 4.2, 7.2, 8.1, 8.4, 20 | Convention bridge, tree canonicality, coset coverage, signed theorem, encoding injectivity, pure reference |
| `src/perm/`, `src/bsgs/`, `src/coset/` | M2 | §§8.1, 9 | Permutation representations, deterministic chain construction, independent verifier, provenance, tuple minimum, Group(H) bytes |
| `src/object/`, `src/encoding/` | M3 | §§2, 4, 6 | Object model, extensional normalisation, CDAG-2 encoder/decoder/validator, graph/subset/tuple adapters |
| `src/partition/`, `src/refine/`, `src/search/`, `src/symmetry/`, `src/api/` | M4 | §§3.2, 7, 8, 10, 11.1, 17 | P1 refinement, objective-specific search, verified automorphism subgroup, public API |
| `checker/` | M4 | §20 | Independent certificate checker; shares no code with `src/` |
| `src/arena/`, `src/metrics/`, `bench/` | M5 | §§11, 12, 19 | Admission planner, live-memory ledger, benchmark harness, Δ/ρ/δ ledgers |
| `src/scheduler/`, `src/checkpoint/` | M6 | §§14, 18 | Coordinator state machine, helpers, trusted resume |
| `src/cpu_dispatch/` | M8 | §13 | Feature-checked SIMD kernels with scalar equality tests |
| `tools/` | all | §4.1 | Stream dumper, corpus generators, ledger tooling |

## 2. Shared internal interfaces (decided; frozen at the M2/M3 gates)

These are the C-level contracts that later modules build on. They are internal (`src/**/*.h`), not public API.

### 2.1 Integers and capacities

- Atom IDs, counts and wire list lengths are `uint32_t` (spec §11.1). Byte offsets are `uint64_t`, checked against `SIZE_MAX`.
- Group orders and multiplicities use a small multi-limb type `canon_nat` (array of `uint32_t` limbs, little-endian limb order internally; the wire `Nat` is big-endian shortest form per §4.1). Operations needed: multiply by `uint32_t`, add, compare, encode to `Nat`, decode from `Nat` with leading-zero rejection.
- The capacity descriptor (§11.1) is a plain struct: `max_n`, `max_nodes`, `max_refs`, `max_literal_bytes`, `max_output_bytes`, `max_count_bits`, `max_search_nodes` (the logical work quota, counting the fixed reference traversal). **Decided:** the first release sets `max_count_bits = 64`, so a multiplicity or order that does not fit is `CAPACITY_LIMIT`, which §11.1 permits. The multi-limb type still exists so that the limit is a descriptor value, not a design assumption.

### 2.2 Permutations (`src/perm/perm.h`)

```c
typedef struct { uint32_t n; uint32_t *img; } canon_perm;        /* dense, img[v] = v^p */
typedef struct { uint32_t n, k; uint32_t *src, *dst; } canon_sperm; /* sparse moved support, src increasing */
```

Operations: `compose(p, q, out)` with `out[v] = q[p[v]]`; `inverse`; `apply_tuple`; `lex_compare` on image arrays; `is_identity`; sparse to dense and back; `validate` (bijection, range). Identity is a tagged value so that no `n`-entry array is materialised for it (§9.3).

### 2.3 Stabiliser chain (`src/bsgs/bsgs.h`)

```c
typedef struct {
    uint32_t base_point;
    uint32_t orbit_len; uint32_t *orbit;        /* discovered order, orbit[0] = base_point */
    uint32_t *parent_gen;                       /* Schreier vector: index into level generators, or NONE for root */
    uint32_t *parent_point;
    uint32_t gen_count; uint32_t *gen_ids;      /* indices into the chain's generator table */
} canon_bsgs_level;
typedef struct {
    uint32_t n, depth;
    canon_bsgs_level *levels;                   /* levels[i] generates G_(b_0..b_{i-1}) */
    canon_perm_table gens;                      /* all strong generators, with provenance records */
    canon_nat order;
    bool verified;                              /* set only by canon_bsgs_verify */
} canon_bsgs;
```

Operations and the spec sections they implement: `build` (§9.1 practical deterministic closure: sift residues, insert with provenance, rebuild affected levels, repeat until all Schreier residues sift to identity); `verify` (§9.1 independent verifier: every condition listed there, including input-generator membership at the root and terminal triviality); `sift`/`contains`; `order`; `point_stabiliser_chain` (suffix view); `rebase(ordered prefix)`; `transporter(level, point)` reconstructed from the Schreier vector with an optional bounded dense cache; `tuple_min(L)` (§7.2 procedure, returns `t` and `H = G_M` as a chain suffix after rebase to `L`'s order); `orbits_of_pointwise_stabiliser(M)`.

Provenance records (§9.2) are `{INPUT i | INVERSE j | PRODUCT j k}` nodes in a DAG; a generator's dense array is recomputed from provenance on demand in tests and compared bit for bit with the stored array.

### 2.4 Coset enumerator (`src/coset/coset.h`)

Implements §8.1 exactly, with a consumer callback and a cancellation poll. `visit(H, r)` chooses `a` = smallest atom moved by `H`, and for each `b` in sorted `a^H` the least image-array element `t_b` with `a^{t_b} = b`. **Decided:** `t_b` is found by the same constrained least-element primitive used for §9.4, `canon_coset_least`: the point constraints are applied **first** (§9.4 "successive point constraints"), reducing the coset to the sub-coset of elements satisfying them, and only then is the image array minimised point by point in increasing order. Interleaving constraints with minimisation is wrong: for `J = Sym(3)`, `r = id` and the constraint `1 ↦ 0`, minimising point 0 first fixes `0 ↦ 0` and then no element satisfies the constraint, although `[1,0,2]` does (found in S4). The enumerator, the coset representative `r₀` and the greedy generator descent share this one audited primitive.

### 2.5 Objects (`src/object/object.h`)

A read-only, normalised object is an arena of records `{tag, payload, child_ids[]}` numbered by the canonical DAG order of §4.2. Decoded views exist for the three first-delivery kinds: subset (bitset plus sorted list), atom tuple, coloured directed multigraph (CSR and CSC, labels interned, multiplicities as `uint64_t` under the §2.1 capacity decision). Every view keeps a reference to its arena and its `n`.

### 2.6 Partition and refinement (`src/partition/`, `src/refine/`)

Flat arrays `lab`, `pos`, `cell_of`, cell start/end, per spec §10, plus a trail of cell-split records for rollback. The O stage computes, per vertex, the signature vector `(out_count[label][cell], in_count[label][cell])` in the frozen order of §7.1 using CSR/CSC traversal over the entry snapshot of cells; signatures are sorted by exact lexicographic comparison, never by hash. The G stage asks `src/bsgs` for `tuple_min(F)` and the orbits of `G_M`, pulls them back through `u⁻¹` and splits by orbit index.

## 3. M0: executable semantics and blind references

Exit evidence (milestone plan): hand-derived vectors reproduced; trace/byte/status/sign agreement including generated cases beyond oracle range; deterministic-witness agreement when requested; a planted error is detected; both implementations retained.

### WP0.1 Golden corpus (`refs/vectors/golden.json`)

Machine-readable transcription of every §7.4 case, verified against `checks/review_checks.py` by `tests/python/test_golden_vectors.py`. Done by the scaffold: six P1 cases, four group and coset payloads, the convention vectors, the labeling example, the three signed examples and the CDAG-BYTE-1 versus P1 minimum case. The transcription recorded four readings of the spec text that a v2.1 editorial pass should make explicit: `Group(1)` has no stated degree; the DAG case's trace and witness are only stated by reference; `(0,2)^(pq)=(2,1)` is read as conjugation of a cycle; and the eight status names have no numeric values (the header assigns 0–7 in listed order). None changes a byte.

### WP0.2 Case schema and interchange format

The case input schema (JSON, one object per case) is: `case_id`, `n`, `generators` (list of image arrays), `character` (optional list of ±1 per generator), `object` (`{"kind": "subset"|"tuple"|"graph"|"dag", ...}`), `objective` (hex tag), `order` (for minimum), `labeling` (optional `rho` image array), `witness_mode` (`"any"|"deterministic"`), `capacity` (the descriptor; at least `max_search_nodes`). The output format is `refs/compare/FORMAT.md`, which already carries the sign as a `;sign=±1` suffix on the witness field. **Decided:** before any reference is written, FORMAT.md and `compare.py` gain one trailing field `group_hex` (canonical `Group(H)` bytes for the stabiliser objective, `Group(H) || Perm(r₀)` for coset objectives, `-` otherwise), so that group and coset answers are compared as canonical bytes rather than as generator lists.

### WP0.3 Exhaustive oracle (`refs/oracle/`)

Python, standard library. For a case with `|G| ≤ 2·10⁵` it enumerates `G` explicitly (closure under generators) and checks each reference output against the definitions, not against the other reference: `c ∈ x^G`; `C(x^h) = C(x)` for every `h ∈ G` by calling the reference on transported inputs; minimum under the named order equals the brute-force minimum; the returned transporter satisfies `x^g = y`, and emptiness matches brute force; the returned stabiliser generators generate exactly `{g : x^g = x}`; the labeling coset reconstructs to `Aut_G(x)λ`; the signed answer is zero iff some stabiliser element is χ-odd, and otherwise `s = χ(t)`. It may reuse `closure`, `mul`, `inverse` from `checks/review_checks.py`; it must not call `p1`, `dag_bytes`, `group_bytes` or `graph_bytes` from that file, which encode implementation choices. Traces are not oracle-checkable (they are defined by P1, not by the orbit); only the two references check each other's traces.

### WP0.4 Blind reference A and WP0.5 blind reference B

**Decided:** ref-a in C17 (so that, after sealing, its unpruned consumers can be reviewed as a candidate for the M4 reference path); ref-b in Julia. Each author receives the same brief:

- Inputs: the specification (§§3, 4, 7, 8, 9.4, 11.1 are normative for M0), `refs/compare/FORMAT.md`, the case schema, and the golden corpus **inputs only**; the expected outputs are withheld until the first comparison.
- Forbidden before sealing: reading `checks/review_checks.py`, the `expected_*` and `payload_hex` fields of the golden corpus, the other reference, or any M2–M4 code (`refs/README.md`).
- Scope: objectives `0x0001` to `0x0007` on subsets, atom tuples, coloured directed multigraphs and nested tuple/set/multiset DAGs with literal leaves; `Group(H)` and `Perm` encodings per §9.4 for the stabiliser and coset objectives; status and all nine flags; the logical work quota `max_search_nodes` applied to the unpruned P1 tree and to the §8.1 enumeration so that `CAPACITY_LIMIT` is a function of the input (§11.1).
- Group operations may be naive (explicit element lists for small groups; for larger groups a simple Schreier–Sims of the author's own writing). Nothing is shared.
- Deterministic witness mode: minimise the image array over `Aut_G(x)·t` after a complete stabiliser enumeration (§3).
- Sealing: author records the commit hash, language and toolchain in `refs/SEALS.md`. The implementation is then read-only.

### WP0.6 Generated corpus (`refs/vectors/generated/`)

A seeded generator (`tools/gen_cases.py`) writes three tiers:

| Tier | Groups | Objects | Oracle | Count |
|---|---|---|---|---|
| T1 | every subgroup of `Sym(n)`, `n ≤ 4` (40 groups) | all subsets; all digraphs with multiplicities `≤ 2` for `n ≤ 2`; all 3-tuples | yes | exhaustive |
| T2 | random 1–3 generators, `n ≤ 8`, `|G| ≤ 2·10⁵` | random subsets, tuples, labelled digraphs, DAGs of depth `≤ 4` with sharing | yes | 2,000 per objective |
| T3 | structured families, `n ≤ 48`: cyclic, dihedral, direct products, wreath products `Sym(a) ≀ Sym(b)`, grid `C_a × C_b`, intransitive sums | as T2 plus identical-factor tensor skeletons encoded as labelled digraphs | no | 500 per objective |

T3 cases are filtered by both references returning `COMPLETE` under a fixed `max_search_nodes` of 10⁶; cases where both return `CAPACITY_LIMIT` are retained as status-agreement cases. Deterministic witnesses are requested only where `|G| ≤ 10⁶`.

### WP0.7 Comparison and corruption sensitivity

`refs/compare/compare.py` reports per-case agreement across ref-a, ref-b and the oracle. Corruption planting goes beyond the nibble flip: the harness runs ref-a under three environment switches implemented for this purpose only, `CANON_PLANT=sign` (negate `χ` on one generator), `=action` (compose on the wrong side in one leaf action), `=encoding` (omit the DAG height ordering). Each must produce at least one disagreement on T1. The switches are deleted before sealing; the run logs are retained under `refs/compare/runs/`.

### WP0.8 Divergence protocol

Every disagreement is logged as an issue with the case, both outputs, and a classification: **spec ambiguity** (the sentence admits both readings; fix the spec, bump to v2.1, re-run), **implementation bug** (one output violates a stated rule; fix with a unit test), **oracle bug**. A spec change after sealing reopens both references for that rule only, with a new seal.

### M0 exit checklist

- [ ] All §7.4 cases reproduced by ref-a, ref-b and (where applicable) the oracle.
- [ ] T1 and T2: zero disagreements; T3: zero disagreements on traces, bytes, signs, statuses, group bytes.
- [ ] Deterministic witnesses agree where requested.
- [ ] Three planted corruptions detected; logs retained.
- [ ] `refs/SEALS.md` records both seals; divergence log closed.
- [ ] Spec v2.x tagged as **semantics frozen**.

## 4. M1: Lean semantic proofs and pure reference

Lean 4 against the pinned mathlib snapshot (`ed72f1fa…`), fetched only after the maintainer's go-ahead. Modules and the theorems they hold:

| Module | Definitions | Theorems (prose statements) |
|---|---|---|
| `Convention.lean` | `ArrPerm n := Fin n ≃ Fin n` wrapped in `MulOpposite` so that `(p * q) v = q (p v)`; `act` for tuples, subsets, finite relations | The array convention is a group isomorphic to `(Equiv.Perm (Fin n))ᵐᵒᵖ`; `(x^p)^q = x^(pq)`; inverse of a product reverses factors |
| `Action.lean` | Objects as an inductive type `Obj` over atoms, literals, tuples, finsets, multisets, perms, subgroups, cosets, graphs, relations; `ATOM-TRANSPORT-1` as a `MulAction` | Action laws; `Aut_G(x)` is a subgroup; orbit/stabiliser facts imported from mathlib |
| `Coset.lean` | `H r` as a set; `t_b` as any element with `a^{t_b}=b` | `H r = ⨆_b H_a t_b r`, disjoint; descent terminates (orbit size > 1 strictly reduces order); exhaustive consumers are complete for minimum, transporter, stabiliser, transporter coset (§8.2) |
| `Tree.lean` | Abstract tree with equivariant refiner, equivariant target selection, framed trace; full-list leaf `L`; `t_L` via tuple minimum | Uniqueness of `t_L`; `t_(L^h) = h⁻¹ t_L`; leaf-key sets of `x` and `x^h` coincide; minimum is canonical; termination from strict splits (§7.2) |
| `Profile.lean` | P1's initial colours, O and G stages, target rule, as functions on ordered partitions | Each stage is equivariant and representation-independent (§7.1 arguments); P1 instantiates `Tree` |
| `Signed.lean` | Character `χ : G →* ℤˣ`; lifted group on `Ω ⊔ {+,−}` | `[x] = 0` in `ℚ[orbit]/relations` iff `Aut_G(x)` has a χ-odd element; nonzero sign covariance `s(x^h) = χ(h) s(x)`; the lift is an injective homomorphism and χ exists iff the projection kernel is trivial (§8.4) |
| `Labeling.lean` | `Λ = Gρ`, `λ = ρt` | `λ ∈ Λ`, `c = x^λ`, complete set `Aut_G(x) λ`; coordinate-rename and representative-change rules (§3.1) |
| `Encoding.lean` | Canonical DAG numbering by height then record bytes | Equal extensional values give equal streams; decoding inverts encoding (injectivity, §4.2); no complete trace is a proper prefix of another |
| `Reference.lean` | Executable (slow) `canonImage`, `lexMin`, `transporter`, `stabiliser`, `signed` by explicit enumeration over `Finset` | Each returns what its spec contract states; termination by well-founded recursion on group order and partition cell count |

The assumptions ledger (`lean/ASSUMPTIONS.md`) lists every hypothesis a theorem takes as a parameter (refiner equivariance for custom callbacks, totality of actions) and marks each as discharged for built-ins or declared for externals. No `axiom`, no `sorry` at the gate.

## 5. M2: certified constructive group operations

### WP2.1 Permutations (`src/perm/`)

Dense and sparse forms, tagged identity, validation, composition, inverse, tuple application, lexicographic compare, FNV-style hash used only as a lookup aid. Tests: convention vectors; random round trips sparse↔dense; `compose(p, inverse(p))` is identity; `inverse(compose(p,q)) == compose(inverse(q), inverse(p))`.

### WP2.2 Reference Schreier construction (`src/bsgs/reference.c`)

The direct recursion of §9.1: at each level, close generators under inverses, dedupe, sort; orbit by queue in increasing discovered label; Schreier generators `t_b s t_{b^s}⁻¹`; recurse. Exponential in general; used by tests up to `n ≤ 8` as the correctness anchor for WP2.3.

### WP2.3 Practical deterministic constructor (`src/bsgs/build.c`)

Sift-and-insert closure with provenance. **Decided policies** (the plan requires them fixed before any performance claim):

- Base extension: when a residue fixes all current base points but is not identity, append its least moved point.
- Insertion level: the deepest level at which the residue still fixes all earlier base points.
- Rebuild: after insertion at level `i`, recompute orbits and Schreier vectors for levels `≥ i`; re-sift all Schreier generators of those levels.
- Termination: each insertion strictly enlarges the subgroup represented at its level (checked by `sift` failing before insertion), so the number of insertions is bounded by the chain length of subgroup growth.

Operation counts (candidates considered, sifts, dense compositions) are recorded in a `canon_bsgs_stats` struct for M5 ledgers.

### WP2.4 Independent verifier (`src/bsgs/verify.c`)

Written against §9.1's list without reading `build.c` (a second author or a second session). Checks: all generator arrays are bijections; each generator's provenance re-derives its array; level `i` generators fix `b_0..b_{i-1}`; orbit reachability via stored tree edges; orbit closure under level generators; stored transversal images; every Schreier residue sifts to identity in the certified next level; every input generator sifts at the root; the last level is trivial; **and each level's generator set contains the next level's (nesting)**. The nesting condition is absent from §9.1's list and is necessary: for `Sym(3)` from `(0 1)`, `(1 2)`, the chain with level 0 generated by `(0 1)` alone (orbit `{0,1}`) and level 1 by `(1 2)` (orbit `{1,2}`) satisfies every listed condition yet has order product 4, because the Schreier-residue condition gives only the inclusion `Stab_{⟨S_i⟩}(b_i) ⊆ ⟨S_{i+1}⟩` and the reverse inclusion needs `S_{i+1} ⊆ ⟨S_i⟩`. Found in S3; to be added to §9.1 in spec v2.1. Sets `verified = true` only on full success. Tests: mutate one stored entry in a verified chain in each of ten ways and require rejection (certificate mutation, milestone exit evidence).

### WP2.5 Operations on verified chains

`sift`, `contains`, `order` (multi-limb product of orbit lengths), `point_stabiliser_chain`, `rebase` (rebuild from strong generators with the requested base prefix; reuse is optional), `tuple_min` (§7.2 procedure; returns `t` and the chain suffix for `G_M`), `orbits_of_pointwise_stabiliser`, `equal` and `subgroup_of` by mutual sifting of generators.

### WP2.6 Coset enumerator and constrained least element (`src/coset/`)

§8.1 enumerator with consumers for minimum, transporter, stabiliser, transporter coset and constraint predicate (§8.2); the `least_element_with_constraints` descent (§9.4): at each level choose the least admissible image of the next base point whose constrained coset is not contained in `K` (using `C = Jr ⊆ K iff r ∈ K and J ≤ K`). Tests: against brute-force enumeration for all T1 groups; against the reference Schreier construction for T2 groups.

### WP2.7 Canonical `Group(H)` and labeling-coset bytes (`src/encoding/group.c`, depends on WP3.1 for byte helpers)

Rule 1: orbits, multi-limb product of factorials, exact equality with `order` → symbolic encoding. Rule 2: greedy sequence `g_i = least element of H \ K_i`, `K_{i+1} = ⟨K_i, g_i⟩` (rebuild and verify after each insertion). Labeling coset: least element `r₀` of `H r` by constrained descent, then `Group(H) || Perm(r₀)`. Tests: the four §7.4 payloads; presentation independence over random generating sets of the same group; `2^k ≤ |H|`.

### M2 exit checklist

- [ ] Oracle agreement on membership, order and transporters for T1/T2 groups.
- [ ] Ten certificate mutations rejected; both inclusions and terminal triviality asserted by the verifier.
- [ ] Group bytes identical across generator presentations, orderings and bases.
- [ ] Build, verify, rebase, sift, tuple-min counters reported separately on a small suite (no timing claims).
- [ ] Long Schreier paths (`n`-cycles), redundant generators, changing bases, small support exercised.

## 6. M3: concrete adapters and encoding

### WP3.1 Wire primitives (`src/encoding/wire.c`)

`U16`, `U32`, `B`, `Nat` encode/decode with strict rejection of leading zeros, overflow and truncation (§4.1). Property tests: round trip; decoded-then-encoded equals input bytes; every malformed form rejected.

### WP3.2 Object import and extensional normalisation (`src/object/normalise.c`)

Bounded acyclicity traversal; discard unreachable; bottom-up intern by `(tag, payload, child ids)`; sets dedupe, multisets merge counts; height numbering; within-height sort by record bytes (§4.2). Tests: `x_{i+1} = (x_i, x_i)` to depth 60 gives 61 records; shared versus duplicated storage give identical streams; unreachable records vanish; insertion order has no effect.

### WP3.3 CDAG-2 encoder, decoder and validator (`src/encoding/cdag.c`)

Encode from a normalised arena; decode into an arena with strict checks (child index smaller than parent, counts determine boundaries, no trailing bytes, root is last); validate by re-normalising and requiring byte identity (§4.2). Tests: the six §7.4 streams; every invalid-encoding category listed in §4.2 rejected; fuzz the decoder with a sanitizer build (from M4 onwards in CI).

### WP3.4 Graph adapter (`src/object/graph.c`)

Import with duplicate-arc combination, zero-multiplicity rejection, sort by `(source, target, B(label))`, CSR and CSC, label interning with `(length, bytes)` order; action by relabelling; exact equality; the simple-undirected wrapper (reject loops, coalesce, emit two unit arcs, §4.1); `SIMPLE-UPPER-1` key (§4.4). Tests: loops count once in each direction; duplicate arcs equal a count-two arc; insertion order; the §7.4 one-arc case.

### WP3.5 Subset and tuple adapters (`src/object/subset.c`, `tuple.c`)

Bitset plus sorted list for subsets; tuple with repeated positions. Action, equality, encoding. Initial P1 keys (membership or empty) per §7.1.

### WP3.6 Nested DAG and algebraic leaves (`src/object/dag.c`)

Tuples, sets, multisets over the above, plus permutation, subgroup and labeling-coset leaves encoded through WP2.7. Action on a permutation leaf is conjugation `g⁻¹pg`; on a subgroup `g⁻¹Hg`; on a coset `g⁻¹Hρ` (§2.1). Tests: conjugation convention with noncommuting `p, q`; coset action preserves the set of labelings.

### WP3.7 Adapter proof bundles (Lean, after M1)

One bundle per adapter: decode factorisation, action, injectivity, equivariance, termination of the P1 initial key, and every bound the C code uses. Tensor reductions (M3c) are out of scope until a wrapper brief exists (SOURCE-GATE and ADAPTER-FAITHFUL).

### M3 exit checklist

- [ ] Streams for all T1/T2 objects validate by byte identity after decode and re-encode.
- [ ] Sharing, duplication and insertion-order tests pass for every adapter.
- [ ] Output-size evidence: encoded size equals the §4.2 charge (records, references, literal bytes, group payloads) on a logged suite.
- [ ] One adapter theorem bundle (subset) complete in Lean; graph and DAG bundles scheduled.

## 7. M4: trustworthy scalar C and the certificate checker

### WP4.1 Ordered partition with trail (`src/partition/`)

`lab/pos/cell_of`, cell spans in semantic order, split-in-place preserving old cell position (§7.1 `split`), a trail that records `(cell, old_end, new_boundary)` triples for rollback, and an explicit recompute-from-recipe fallback (§11.2). Tests: `split` never reorders old cells; rollback restores exact arrays; recipe replay equals trail rollback.

### WP4.2 P1 refiner (`src/refine/p1.c`)

Exactly the loop of §7.1: `NODE(d)`; repeat `{O stage; STAGE_O; G stage; STAGE_G}` until no strict split; a final no-change sweep always recorded. The O stage uses the entry snapshot of cells; the G stage reads the post-O partition and extracts `F` in partition order. Signature sort by exact comparison (radix on fixed-width counts is permitted; hashes never order). Tests: the §7.4 traces; metamorphic invariance under generator presentation and arc insertion order; cross-check against ref-a and ref-b on T1–T3 (the sealed references are now oracles for the production path).

### WP4.3 Canonical-image search (`src/search/p1_tree.c`)

Unpruned DFS first: target cell `min(size, position)` among non-singletons; individualise each `a` in cell order; at a leaf compute `L`, `t_L` by `tuple_min`, the image and its CDAG-2 bytes; compare `(trace, bytes)` lexicographically; keep the least, with `t` as metadata. The logical work quota is checked per node (§11.1). Status and flags set per §3.2. Tests: byte identity with the sealed references on all tiers.

### WP4.4 Objective-specific consumers (`src/search/coset_objectives.c`)

Minimum (`CDAG-BYTE-1` and `SIMPLE-UPPER-1`), transporter one, stabiliser, transporter coset, labeling coset (§3.1 reconstruction with explicit source/target handles), signed image (§8.4: stabiliser enumeration with early odd-witness exit, then P1), constraint one/enum with a registered total predicate. Tests: oracle on T1/T2; sign cross-feed `s_A(C_B(x))·s_A(x) = s_B(x)` between the production path and ref-a (§20).

### WP4.5 Verified automorphisms and the first prune (`src/symmetry/`)

`A_known` as a verified chain; insertion only after exact membership and `x^a = x` checks (§7.3). First pruning rule, implemented only with its coverage lemma written in `docs/pruning-rules.md` and a checker rule: **node-stabilising automorphism, retained equivalent subtree** (if `a ∈ A_known` fixes the current node's individualised sequence setwise and maps sibling branch `b` to an already-explored branch `b'`, then the subtree at `b` has the same leaf-key set as the subtree at `b'`). Objective scope: canonical image only; stabiliser and enumeration objectives keep full coverage (§8.2). Tests: pruned and unpruned paths give identical `(trace, bytes)` on all tiers; node counts logged.

### WP4.6 Public API (`src/api/`)

The §17 entry points over opaque handles with retain/release, copying builders, borrowed buffers with release callbacks, `solve_batch` with order-preserving per-input statuses, the output sink with `PAUSE`/`FAIL` semantics, `result_verify_witness` (membership plus action only), `result_encode` (canonical bytes only when the image is complete, `00` for certified zero). Capacity admission precedes allocation (§11.1). Tests: lifetime vectors (release in every order, release of failed handles), error vectors for every status, FFI-style idempotence `canon(canon(x)) = canon(x)` through the public API, output-as-input.

### WP4.7 Certificate format and checker (`checker/`)

Certificate v0 (binary, versioned) for the canonical-image objective: the capacity descriptor; the input stream and group generators; for each visited node its individualised sequence and the trace tokens emitted; for each pruned sibling the automorphism and the retained branch it maps to; for each leaf the witness `t_L` and the image bytes; the final claim. The checker (no code shared with `src/`) recomputes P1 refinement at each listed node with its own simple partition code, verifies each automorphism by membership (its own sift) and action, verifies that the listed children of each node are exactly its target cell, and verifies the final minimum. It rejects any omitted child. Stabiliser and signed-nonzero certificates (v1) additionally carry the coset-enumeration regions and are scheduled after v0 passes mutation tests. Tests: every accepted certificate from the production path checks; mutate each field and require rejection; certificate size and check time logged.

### WP4.8 Fault and sanitizer gates

ASan/UBSan builds in CI; fuzz targets for the CDAG-2 decoder, `Perm`/`Group` decoders and the certificate parser (libFuzzer under clang, bounded corpus); allocation fault injection through a test allocator that fails the `k`-th allocation for every `k` on a small suite, requiring either a releasable partial state or a clean status.

### M4 exit checklist

- [ ] Production path agrees with both sealed references on T1–T3 for every objective.
- [ ] Oracle agreement on T1/T2.
- [ ] Checker accepts all production certificates and rejects every planted mutation.
- [ ] Sanitizer, fuzz and fault-injection suites pass in CI.
- [ ] Trusted-code ledger (`docs/trusted-code.md`) lists what is proved, checked, tested and trusted, including whether checked execution trusts compiled Lean.

## 8. M5: serial and small-batch performance

### WP5.1 Arena and admission (`src/arena/`)

Worker-local arenas; reservation before every component; the live-state equation of §11.2 implemented as a ledger updated at phase boundaries; deterministic admission that reserves the complete serial workspace plus bounded output/verification buffers; `CAPACITY_LIMIT` before search when the one-task requirement exceeds the descriptor. Tests: the same input under different schedules and cache policies yields the same status; peak ledger equals measured RSS within an explained margin.

### WP5.2 Metrics (`src/metrics/`)

Counters for every §19 metric that is observable in scalar code: nodes, leaves, prunes, root-discrete and singleton-orbit fractions, arcs visited, point queries, dense compositions, provenance bytes, replay/undo, output and certificate bytes, per-stage times (import, group build, verify, rebase, wrapper, refine, leaf, output, check).

### WP5.3 Benchmark harness (`bench/`)

Instance families of §19; pinned competitors recorded with version and exact invocation; cold/warm/amortised separation; every record in `bench/ledger-schema.json` form with `L`, its category and equation, and `Δ, ρ, δ`; censored cases retained. **No target such as "within 2× of the floor" is set.** First measurements require the maintainer's machine or a documented host; this environment is not a benchmark host.

### WP5.4 Small-batch regime

Caller-owned workspaces, immutable shared group contexts keyed per §16, no allocation after admission on the hot path, root-discrete fraction measured on the TensorGR-style skeleton family. Compute, branch-recovery, stream and collection floors reported separately (appendix §6.7).

## 9. M6: concurrent coverage and runtime

### WP6.1 Abstract protocol model

Before C: a transition-system model of §14.2 (states `READY, RUNNING, SUSPENDED, WAIT_CHILDREN, COVERED, LINK`) in Lean (`lean/Canon/Coverage.lean`) with the invariant "each root obligation has exactly one accounting route to a live task, a completed proof or a justified representative link" and the theorem that `COMPLETE` implies root discharge. Link IDs strictly decrease, so acyclicity is by construction.

### WP6.2 Coordinator runtime (`src/scheduler/`)

Single coordinator mutex; the transition table of §14.2 as one function per row; helpers under R1–R3 with disjoint output ranges; cancellation as an atomic release store polled with acquire; `CALLER_THREADS` and `CORE_POOL` modes (§14.1). Tests: forced interleavings via a deterministic scheduler shim; stealing, failure, cancellation and reclamation; identical complete bytes and capacity policy to the serial path; work inflation `W_p/W_1` logged.

### WP6.3 Trusted checkpoint and resume (`src/checkpoint/`)

Safe-point suspension, coherent coverage-graph capture under the metadata lock, payload write after release, new file plus manifest replace (§18). Untrusted resumption stays disabled. Tests: corruption detection; resume with changed worker count yields identical bytes.

## 10. M7 and M8 (outline only)

- **M7:** choose the concrete C model (candidates: a verified subset via Lean-generated C, or a separate refinement proof in a C verifier) at the M4 gate; the choice determines which `src/` modules are rewritten in the verified subset. No module is described as verified until that programme reports.
- **M8:** AVX2 and AVX-512 kernels for the O-stage count accumulation and bitset operations, dispatched after CPUID and OS-state checks; conflict-safe accumulation for duplicate scatter; scalar equality tests on every pinned profile before any timing. GPU plugin only after the CPU release, with buffer/event ownership tests.

## 11. H0 and H1

H0 is a network task requiring the maintainer's go-ahead: retrieve `leanprover/hex-graph-iso` and `leanprover/hex-perm-group` at full commit hashes, record SHA-256 of the archives and inspected files in `review_sources/formalisation/`, audit theorem and executable scope against code. H1 decides reuse for a bounded part of M2 or a separately versioned graph-only profile. Neither is on the critical path.

## 12. Sequencing

```
WP0.1–0.3 ──► WP0.4 ∥ WP0.5 ──► WP0.6–0.8 ──► [semantics frozen]
                                                   │
          ┌────────────────────────────────────────┼─────────────────────┐
          ▼                                        ▼                     ▼
       M1 (Lean)                      WP2.1 ─► WP2.2 ─► WP2.3 ─► WP2.4 ─► WP2.5 ─► WP2.6 ─► WP2.7
                                                   │                                    │
                                      WP3.1 ─► WP3.2 ─► WP3.3 ─► WP3.4/3.5 ─► WP3.6 ◄───┘
                                                                   │
                                      WP4.1 ─► WP4.2 ─► WP4.3 ─► WP4.4 ─► WP4.5 ─► WP4.6 ─► WP4.7 ─► WP4.8
                                                                                                │
                                                                     WP5.1 ─► WP5.2 ─► WP5.3 ─► WP5.4
                                                                                                │
                                                                     WP6.1 ─► WP6.2 ─► WP6.3 ──► M8
```

The critical path is M0 → WP2.1–2.5 → WP3.1–3.4 → WP4.1–4.3 → WP4.7. WP3.1–3.3 depend only on WP2.1 and can start as soon as M0 freezes the encoding; WP2.6–2.7 and WP3.4–3.6 run in parallel. M1 proceeds alongside M2/M3 once M0 has frozen the definitions; its results gate the "mathematical reference complete" review, not the start of M4 construction.

## 13. Effort allocation within the milestone plan's judgments

The milestone plan's ranges are not re-estimated here. Within them, the decided split is: M2's 12–36 weeks allocate about a third each to WP2.3 (constructor and policies), WP2.4–2.5 (verifier and operations) and WP2.6–2.7 (enumeration and canonical bytes). M3a (WP3.1–3.5) is the smaller half of the shared M3a/M4 range; WP4.7 (certificate checker) is the larger single item in M4. M0 is re-estimated at its entry gate after the briefs are written; a working assumption of 2–4 expert weeks per blind reference plus 1–2 weeks for corpus and harness is a judgment, not a commitment.

## 14. Risk register

| Risk | Consequence | Mitigation | Owner milestone |
|---|---|---|---|
| Spec ambiguity found by blind references | Semantics cannot freeze | WP0.8 protocol; spec version bump; re-seal | M0 |
| Unpruned P1 tree is factorial on symmetric inputs (empty subset under `Sym(n)` has `n!` leaves) | References time out; corpus bias toward easy cases | Logical work quota makes `CAPACITY_LIMIT` deterministic and comparable; T3 includes retained capacity-agreement cases; prune only in M4 with a checker rule | M0, M4 |
| G-stage cost: `tuple_min` needs a chain along `F`'s order at every sweep | Refinement dominated by rebase | Cache keyed by `(G, F)` per §16; measure before optimising; never change the stage | M4, M5 |
| Chain constructor policies produce huge intermediate generator sets | Build time dominates small instances | Fixed policies in WP2.3, counters, warm reuse of verified groups | M2, M5 |
| Checker needs its own refinement implementation | Checker is not small | Keep it simple and slow; it is the Lean target for M1/M3, not the engine | M4 |
| SIGN-COVER unproved | Signed nonzero requires full stabiliser enumeration | Accept the cost; no shortcut without the lemma | M4 onwards |
| Count-bit capacity decision (64 bits) rejects legitimate inputs | Unsupported large multiplicities | Descriptor value, multi-limb type retained; raise at M5 if a workload needs it | M3 |
| CI environment lacks Lean and the benchmark host | Proof and performance gates cannot run in CI | Lean CI job added when the toolchain is approved; benchmarks are maintainer-run with ledgers committed | M1, M5 |

## 15. Immediate next actions

1. Implement slice S1 from its brief (`docs/slices/S1.md`); review; land.
2. S2 through S8 in order, each with its brief under `docs/slices/` and a closing review.
3. After S6: extend `refs/compare/FORMAT.md` and `compare.py` with the `group_hex` field (WP0.2), write the case input schema and the blind brief `refs/BRIEF.md`, and commission ref-b in Julia.
4. After S8 and the M0 gate: `lean/ASSUMPTIONS.md` and M1.
