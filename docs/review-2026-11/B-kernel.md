# Review B-kernel: group kernel and encoding layer

## 1. Scope

Read so far:

- `docs/review-2026-11/BRIEF.md`, `CLAUDE.md`, `README.md`, `HANDOFF.md`.
- `docs/specification.md` v2.2: §3 (convention), §4 (4.1 CDAG-2 grammar,
  4.2 normalisation/height numbering, 4.3 orders, 4.4 SIMPLE-UPPER-1),
  §6, §8.1-8.4, §9 (9.1 verifier incl. nesting, 9.2, 9.4 Group bytes),
  §10, §11.1-11.2, §7.2 (tuple minimum, only as it touches the kernel).
- Code read in full: `src/perm/{perm.h,perm.c}`,
  `src/arena/{mem.h,mem.c,checked.h,alloc.h,refcount.h}`,
  `src/util/{sort.h,sort.c}`,
  `src/bsgs/{chain.h,chain.c,verify.h,verify.c,provenance.h,provenance.c,
  group.h,group.c,chain_backend.h,chain_backend.c,explicit.h,explicit.c,
  reference.h,reference.c}`,
  `src/coset/{coset.h,enumerate.c,least.c}`,
  `src/encoding/{wire.h,wire.c,subset_stream.c,graph_stream.c,
  simple_upper.h,simple_upper.c,group_stream.h,group_stream.c,
  cdag_encode.h,cdag_encode.c,cdag_decode.h,cdag_decode.c}`,
  `src/object/{dag,subset,graph,object}.{h,c}` in full.
- Spot-checked outside the lane only to resolve caller questions:
  rebase/set-signs/max_n call sites (`include/canon/canon.h:188-190`
  default `max_n = 4096`; `src/api/api.c:480` limits plumbing).
  The api/search/partition/refine/symmetry layers themselves are
  other lanes and were not reviewed.

Tests were not run (brief forbids test suites); nothing was built.
All evidence below is by reading and derivation unless a finding
says otherwise; no finding below was reproduced by execution.

Convention check (spec §3, `p[v]=v^p`, `(pq)[v]=q[p[v]]`): the primitive
implementations are directionally correct everywhere read so far —
`canon_perm_compose`/`canon_perm_inverse` (src/perm/perm.c:15-28),
chain transporter and sift (src/bsgs/chain.c:229-287), the verifier's
independent transporter/sift (src/bsgs/verify.c:157-202), provenance
PRODUCT evaluation (src/bsgs/provenance.c:172-174, "node a acts first"),
conjugation `q[g[v]]=g[p[v]]` i.e. `g^-1 p g`
(src/bsgs/chain.c:763-781, src/bsgs/explicit.c:429-435,
src/object/dag.c:372-389 coset `g^-1 r`), §8.1 enumerator `t_b r`
(src/coset/enumerate.c:146, src/bsgs/explicit.c:318), least-descent
`r' <- t_o r'` (src/coset/least.c:207-210), and the §8.4 lift in both
backends (orders compared, `|L|=|G|` iff kernel trivial;
src/bsgs/chain_backend.c:166-191, src/bsgs/explicit.c:613-630).

## 2. Findings

### B-kernel-01 — Every production base change is unverified; §9.2 "rebuilds/certifies" is not what the code does

- Severity: high. Category: soundness / spec-conformance.
- Evidence (reasoned, not run): spec §9.2: "base change
  rebuilds/certifies for the requested ordered base (reuse is
  optional)" (docs/specification.md:332), and §9.1: "exact
  verification is mandatory". `canon_bsgs_rebase` takes a `verify`
  flag (src/bsgs/chain.h:137-144, src/bsgs/chain.c:680-716), but a
  search of `src/` shows every call site passes `false`:
  tuple minimum src/bsgs/chain.c:1002, §8.1 enumerator
  src/coset/enumerate.c:127, coset descent src/coset/least.c:152
  (also src/symmetry/symmetry.c:128, src/search/certificate.c:282,
  outside this lane). The `verify=true` path is dead in production.
  Consequences by consumer: (a) `canon_bsgs_tuple_min` (P1 leaf map
  and G stage, §7.1/7.2) minimises through rebased suffix chains;
  (b) the §8.1 enumerator's coverage ("Singleton leaves partition G")
  depends on rebased `H` with first base point `a` and on `t_b` from
  the descent through further rebases — a constructor defect that
  only triggers for prefixed bases (e.g. prefix points that are not
  least-moved, or stabiliser suffixes of a verified chain) could
  drop or duplicate coset leaves and no verifier would catch it;
  the stabiliser consumer then builds a *verified* chain of the
  hits, which certifies the subgroup found, not completeness
  (spec §8.2 says exactly this). (c) §9.4 rule-2 Group bytes and
  deterministic witnesses come from the same descent machinery.
  The root chain of G is verified (build_verified), so this is not
  "nothing is verified"; it is that the completeness-bearing derived
  chains rest on constructor correctness plus cross-backend tests,
  which is weaker than the spec text and weaker than the verifier
  story told for the root chain. This is a documented design
  decision (S3 review item 2, cited at chain.h:140-142), so the
  defect is spec-vs-code drift: either the spec sentence or the
  code should change.
- Recommendation: either verify transient rebases at least in a
  debug/audit mode and state the exemption in the spec, or amend
  §9.2 to say transient internal rebases need not be certified and
  name what instead justifies completeness (constructor proof
  obligation). Do not leave "rebuilds/certifies" standing.
- Remediation plan: (1) Cheapest sound option: in
  `canon_coset_enumerate` and `canon_coset_least`, after a rebase,
  optionally (work-policy or build flag) call
  `canon_bsgs_verify(own, level generators)`; measure cost on T1.
  (2) Structural option: make `canon_bsgs_rebase` derive the new
  chain by a verified transformation (like conjugation) or check
  the cheap invariants that completeness uses (suffix order product
  equals the source suffix order; each source generator sifts to
  identity in the rebased chain and vice versa) — this is strictly
  weaker than full verify but catches dropped-generator defects.
  (3) Tests that would prove a fix: a mutation test that corrupts
  the constructor's prefix handling (e.g. drop the first prefix
  point) must make enumeration/stabiliser tests fail through the
  new check, not only through output disagreement. Risk: runtime
  cost of full verification per rebase is the known S3 cost model;
  effort small for option 2, medium for option 1 measurements.

### B-kernel-02 — The §11.2 ledger counts payload bytes only; header and allocator overhead are invisible

- Severity: medium. Category: spec-conformance (capacity accounting).
- Evidence (reasoned): spec §11.2's live-state equation ends in
  `M_allocator_overhead` (docs/specification.md:380) and §11.2
  requires accounting "at every phase". `canon_mem_*` adds only the
  requested payload to `mem_live`/`mem_peak`
  (src/arena/mem.c:60-63, 105-110, 120): each block also carries a
  `canon_mem_header` (a union with `max_align_t`, 32 bytes on
  x86-64 glibc) plus malloc's own chunk overhead/rounding, none of
  which is charged. A zero-byte allocation charges 0 while still
  allocating a header-sized block (mem.c:55). Allocation-heavy
  phases (provenance nodes, level arrays, per-depth enumerator
  blocks) therefore under-report by >= 32 bytes per live block, and
  "peak <= plan" evidence (HANDOFF S8) is about payload, not about
  the memory the process actually holds. `canon_bsgs_bytes`
  (src/bsgs/chain.c:1068-1081) has the same gap for the admission
  plan's chain term: it sums array capacities but not per-allocation
  headers or the `canon_bsgs` struct itself.
- Recommendation: charge `sizeof(canon_mem_header)` (and ideally a
  stated allocator rounding allowance) per block in the ledger, or
  rename/restate the counters as payload bytes everywhere they are
  presented as §11.2 `M_live`.
- Remediation plan: in `allocate`/`canon_mem_realloc`/`canon_mem_free`
  account `bytes + sizeof(canon_mem_header)`; keep the stored
  `h->size` as payload for realloc copy semantics. Update
  `canon_bsgs_bytes` to add header bytes per owned array. Tests:
  existing metrics test (`peak <= plan`) must still pass; add a
  unit test that N small allocations raise `live_bytes` by at
  least N*sizeof(header). Risk: plan constants may need raising;
  effort small.

### B-kernel-03 — The chain verifier trusts allocation capacities it never checks

- Severity: low. Category: memory-safety (verifier robustness).
- Evidence (reasoned): `structure_ok` is documented as "everything
  later reads only in-range indices" (src/bsgs/verify.c:88), but it
  checks value ranges and non-NULL only — never that
  `levels[i].orbit`/`parent_*` have >= orbit_len entries,
  `orbit_pos` has >= n, `gen_node`/`inv_node` have >= gens.count
  (`node_cap` is never read in verify.c), or that table `data`
  pointers own `count*n` words (verify.c:90-136). The later passes
  index all of these (e.g. verify.c:129-133, 334-349). In
  production this is not attacker-reachable today: chains come from
  the constructor, and CDAG group leaves are rebuilt via
  build_verified from parsed generators, never deserialised into a
  `canon_bsgs` struct. It does mean the verifier is not a safe
  checker for an arbitrary in-memory chain struct, and a future
  serialised-chain importer (certificates, checkpoints) that fed
  parsed fields into `canon_bsgs_verify` would inherit OOB reads.
- Recommendation: either state the precondition in verify.h
  ("capacities must be as allocated by the constructor; only field
  values are untrusted") or add the cap checks.
- Remediation plan: in `structure_ok`, require
  `node_cap >= gens.count`, each perm table `cap >= count`, and
  per-level allocations distinguishable — the level arrays are one
  5n block in the constructor (chain.c:104-117) with no stored
  capacity, so full checking needs a capacity field or a
  constructor-only contract. Effort small for the contract
  statement, medium for real checks. Test: hand-build a chain with
  a truncated orbit allocation under ASan; verifier must return
  BAD_STRUCTURE, not read OOB.

### B-kernel-04 — `canon_coset_least_outside` equality shortcut trusts an unchecked precondition and a 0-on-overflow order

- Severity: low. Category: contract / correctness.
- Evidence (reasoned): src/coset/least.c:401-404 returns
  "nothing outside" whenever
  `canon_bsgs_suffix_order(k,0) == canon_bsgs_suffix_order(h,0)`.
  That is valid only under the header precondition `K <= H`
  (src/coset/coset.h:66-71), which is not checked; and
  `canon_bsgs_suffix_order` returns 0 on uint64 overflow
  (src/bsgs/chain.c:217-221), so two overflowing suffix products
  would also compare "equal". The sole production caller
  (rule 2, src/encoding/group_stream.c:146) maintains K <= H by
  construction (K is rebuilt from elements the descent chose from
  H) and built chains have fitting orders, so no wrong output is
  known; but a descent defect that returned an element outside H
  would be masked exactly here on the next iteration.
- Recommendation: compare against the stored verified `order`
  fields instead of recomputing (and never treat 0 as an order),
  or assert `level_in(h,0,k)`-style containment before the shortcut
  in a checked build.
- Remediation plan: use `k->order == h->order` after the
  build_verified invariant, or make suffix_order's overflow
  distinguishable. Test: call least_outside with K not <= H of
  equal order (e.g. two distinct order-2 subgroups of Sym(3)) and
  document/assert the contract. Effort trivial.

### B-kernel-05 — `canon_group_set_signs` leaks a previous signs block if called twice

- Severity: low. Category: memory-safety / maintainability.
- Evidence (reasoned): src/bsgs/group.c:109-111 assigns
  `group->signs = s` without releasing any existing block, unlike
  `canon_group_set_generators` which frees the old table first
  (group.c:74-75). The header contract is "a handle that was just
  allocated" (group.h:126-131) and current callers comply, so this
  is latent only.
- Recommendation: free the old block (or return INTERNAL_ERROR if
  already set) to make the function safe under its own signature.
- Remediation plan: mirror set_generators. Effort trivial.

### B-kernel-06 — Chain `insert()` is not failure-atomic; a mid-insert allocation failure leaves gens/invs inconsistent

- Severity: low (note). Category: contract.
- Evidence (reasoned): src/bsgs/chain.c:425-443 pushes the
  generator row, then the inverse row, then the provenance inverse,
  then appends the id level by level; a failure partway leaves
  `gens.count != invs.count`, an uninitialised `gen_node[id]`
  slot (grow_array does not zero), and the id present in some
  levels only. Today every failure path funnels to
  `canon_bsgs_build`, which frees the whole chain
  (chain.c:672-675), so no inconsistent chain escapes. The hazard
  is only for a future caller that tries to reuse a chain after a
  failed insert.
- Recommendation: state "on failure the chain must be freed, not
  reused" at insert(), or roll back counts.
- Remediation plan: comment now; if insert is ever reused outside
  build, stage the pushes and commit counts last. Effort trivial.

### B-kernel-07 — Decoder allocates O(n) bit maps from the header degree before any record needs them

- Severity: low. Category: memory-safety (resource amplification),
  caller-dependent.
- Evidence (reasoned): `canon_cdag_decode` allocates
  `canon_cdag_bits_words(n)` words (~n/4 bytes) unconditionally
  after the header checks (src/encoding/cdag_decode.c:583-589),
  although the maps are only used by Perm/Group payload readers.
  A short stream (e.g. one literal record) with a large claimed
  `n` therefore forces an allocation proportional to n, bounded
  only by the caller's `max_n`. Import passes the problem degree,
  and the public default is `max_n = 4096`
  (include/canon/canon.h:188-190), so the exposure materialises
  only for a caller who deliberately raises `max_n` towards 2^32;
  admission then arguably accepted O(n) scratch. Hence low.
- Recommendation: allocate the maps lazily on first Perm/Group
  record, or bound n by a function of stream length when no
  degree is expected.
- Remediation plan: pass a lazy-bits holder into decode_records.
  Test: a 25-byte stream with n = 2^31 and one literal record must
  not allocate ~512 MB (check via canon_mem_stats). Effort small.

### B-kernel-08 — `canon_dag_normalise` trusts atom/literal/perm payloads to be well-formed; two readers would misbehave on a hand-built arena

- Severity: low (note). Category: contract / memory-safety.
- Evidence (reasoned): with `leaves_canonical=false` the normaliser
  canonicalises only group/coset/graph leaves; atom, literal and
  Perm payloads are taken as already strict
  (src/object/dag.c:497-510, comment "canonical as decoded"). Two
  downstream readers assume it: `summarise` computes a literal's
  `|s|` as `payload_len - 4u` in size_t (dag.c:729-731), which
  underflows for a literal payload shorter than 4 bytes, and
  `canon_dag_atom` reads payload bytes 0..3 unconditionally
  (dag.c:73-78). Both production feeds into normalise are safe —
  the strict decoder (validated) and `canon_dag_act`'s raw arena
  (built record-by-record from a normalised arena,
  `leaves_canonical=true`) — so this is an internal precondition,
  not a reachable defect. It is worth stating because `canon_dag_append`
  itself imposes no payload grammar, and the invariant is currently
  carried only by the comment at dag.c:499-501.
- Recommendation: state the precondition in dag.h at
  `canon_dag_normalise` ("in must come from the decoder or the
  action; leaf payloads must satisfy the §4.1 grammar"), or add the
  two cheap length guards (`payload_len >= 4` for atom/literal)
  in intern_node.
- Remediation plan: comment or guards; a unit test hand-building a
  2-byte literal payload must yield INVALID_INPUT (or be documented
  as contract violation). Effort trivial.

## 3. Spec ambiguities

- §9.2: "base change rebuilds/certifies for the requested ordered
  base (reuse is optional)" vs the S3 decision that transient
  rebases are unverified (see B-kernel-01). Quoted, not resolved:
  does "certifies" bind every internal rebase, or only base
  changes whose chain is itself an answer/certificate input?
- The status for SIMPLE-UPPER-1 on a graph outside the §4.4 class
  is not named in the spec (already open in HANDOFF item 6(ii));
  the internal key function returns CANON_INVALID_INPUT
  (src/encoding/simple_upper.c:71-73, documented in
  simple_upper.h:19-21). Whether the api lane maps that to
  UNSUPPORTED_ACTION at the boundary was not checked in this lane.
- §11.2's `M_allocator_overhead` (see B-kernel-02): the spec lists
  it as a term of M_live but never says whether the public
  `canon_memory_stats.live_bytes` must include it or may report
  payload bytes only. Quoted from docs/specification.md:373-381;
  the code currently reports payload only.

## 4. Things checked and found sound

- Permutation convention and inverse/product order in perm, chain,
  verifier (independently written), provenance evaluation,
  reference recursion (src/bsgs/reference.c:163-198, 234-237),
  explicit backend closure/tuple-min/enumerator, coset descent and
  enumerator, conjugation in all three implementations. The
  spec §7.4 convention vector directions (pq vs qp) match
  `canon_perm_compose(p,q)[v]=q[p[v]]`.
- Verifier (src/bsgs/verify.c): checks bijections, input identity
  with the originals, provenance re-derivation of every generator
  and inverse, inverse correctness by product, prefix fixation,
  the §9.1 v2.1 nesting condition `S_{i+1} subset S_i` (the Sym(3)
  order-4 counterexample in the file comment is genuinely
  excluded), orbit tree reachability with cycle bound, orbit
  closure, transversal images, all Schreier residues sifting to
  identity below, input membership, terminal triviality, and the
  order product with overflow-as-mismatch. No gap found that
  accepts a chain for a different group, given B-kernel-03's
  capacity precondition. Identity/duplicate strong generators
  would be accepted but are mathematically harmless (they do not
  change K_i, orbits, or the order product).
- Provenance checking evaluates only reachable nodes in topological
  order, range-checks INPUT rows, detects non-injective operands
  at INVERSE nodes, and rejects forward references
  (src/bsgs/provenance.c:73-98, 141-175). Note: an INPUT row that
  is range-valid but not bijective is not detected by
  `canon_prov_check` alone; the verifier's earlier `bijections`
  pass covers it in the only production use.
- §8.1 enumerator: partition argument implemented as specified;
  `t_b` is the constrained least element (not an arbitrary
  transporter), children are `H_a` of a chain rebased to `a`,
  orbit points are visited in sorted order, quota entry rule is
  shared with the explicit oracle (src/coset/enumerate.c,
  src/bsgs/explicit.c:277-326). Per-depth scratch blocks never
  move, so parent `r` pointers stay valid across recursion.
- Least-element descent: constraints are applied before
  minimisation (src/coset/least.c:221-243, with the counterexample
  reason stated), duplicate/conflicting constraints and duplicate
  images across points correctly yield "not found".
- §9.4 Group bytes: rule 1 decided by exact factorial product in
  uint64 with overflow meaning "does not apply" (sound because
  |H| <= 2^64-1 for built chains), blocks emitted ordered by least
  point with increasing points; Group(1) is `01 00000000` at every
  degree including 0; rule 2 greedy sequence via least-outside
  with verified K rebuilds; the size bound in
  `canon_group_bytes_bound` over-counts correctly
  (src/encoding/group_stream.c:234-254).
- Decoder strictness (src/encoding/cdag_decode.c): Nat shortest
  form and >2^64 as CAPACITY, Perm support exactly the moved
  points with bijectivity onto the source set, rule-1 blocks
  disjoint/ordered/size>=2, set/multiset references strictly
  increasing and < parent, positive counts, atom/graph endpoint
  ranges checked at read position, q>=1, root<q, no trailing
  bytes, unknown version/schema/action as UNSUPPORTED and unknown
  tag as INVALID. The bits-clearing contract is honoured on the
  failure paths read (perm_pairs/rule1_blocks touch-count logic).
- Checked arithmetic: `canon_size_*`/`canon_u64_*` used at the
  allocation sites read; grow helpers saturate capacities instead
  of wrapping; no raw malloc/calloc/realloc/free in the lane's
  `src/` outside `src/arena/mem.c` (searched; only `canon_mem_*`
  wrappers and same-named destructor functions matched).
- Wire writers: Nat shortest form, B() length must fit U32,
  buffer growth checked before allocation, all-or-nothing appends
  restore length on failure (src/encoding/wire.c).
- SIMPLE-UPPER-1: bit index `b(b-1)/2 + a` matches the §4.4 pair
  order (0,1),(0,2),(1,2),(0,3)...; MSB-first packing; n<=1 gives
  exactly U32(n); class check requires empty colours/labels, no
  loops, unit multiplicity and the reverse arc.
- DAG normalisation and numbering (src/object/dag.c:405-840):
  interning hashes (tag, canonical payload, normalised child ids,
  counts) with exact collision comparison; sets deduplicate and
  multisets add counts with uint64 overflow as CAPACITY; numbering
  is stable-sort by height, then per height by exact record bytes
  compared field-wise — the field-wise shortcut is sound because
  U32 BE fields compare numerically and shortest-form Nat byte
  order equals numeric order (argued at dag.c:589-596, verified
  here); sets/multisets re-sorted by final child index before
  comparison; equal records within a height are an INTERNAL_ERROR
  invariant, not silently numbered twice; root-last is asserted.
  Reachability is one descending pass (sound since children precede
  parents, checked in the same pass set); records after the root
  are discarded unexamined, which is safe only because the decoder
  already validated them (see B-kernel-08's contract point).
  `canon_dag_act`: atom/literal/perm/group/coset directions checked
  (see §1); coset representative read before generator conjugation
  uses disjoint scratch (perm vs perm2), so no aliasing bug;
  graph records inside an arena are refused on the action path.
  `canon_dag_image_bound`: the `bound - payload_len` step cannot
  underflow for arenas whose stored chain order matches the leaf
  (the §9.4 bound dominates that leaf's actual payload; argued
  above under Group bytes).
- Graph object (src/object/graph.c): (length, bytes) table order
  is B() order; duplicate arcs combined by exact addition after an
  exact total-multiplicity overflow check (combined sum cannot
  overflow once the total fits); invalid conditions take
  precedence over capacity ones as documented; simple-graph
  wrapper rejects loops, coalesces, emits two unit arcs; action
  `colour'[p[v]] = colour[v]`, arcs endpoint-wise, re-sorted, with
  borrowed tables and the borrowed-lifetime contract stated and
  enforced by `imported`/`image_clear`. CSR/CSC counting placement
  preserves arc order within each target group.
- Subset and root dispatch (src/object/subset.c, object.c):
  dedup via bitset, action elementwise through a cleared scratch
  bitset; imported set-of-atoms (including empty) becomes a subset
  root and a graph record a graph root exactly as §7.1 names them
  (object.c:214-259), with move semantics that do not double-free
  (checked each branch); `canon_root_equal`'s kind-first compare
  is safe because current constructors canonicalise the kind
  (DAG roots arise only from root_from_dag's third branch and
  kind-preserving images), so equal values cannot hide under two
  kinds today. One latent sharp edge, not counted as a finding:
  the failed-import placeholder subset `{degree,0,NULL,NULL}`
  (object.c:270) would make `canon_subset_contains` dereference a
  NULL bitset if a caller used the root after a failed import;
  current callers discard it on the error status.
