# Lane E-perf-refine: deep performance review of refinement and partition layer

Review pass November 2026. Common brief: `docs/review-2026-11/BRIEF.md`.
No test suites, reference runs, or benchmarks were run. No repository file
other than this report was edited. Every cost claim below is *reasoned*
from code inspection (loop/counting derivations), not measured. Where a
number is a byte count it is computed from the declared struct layouts.

## 1. Scope

Read in full:

- `CLAUDE.md` (via session rules), `README.md`, `HANDOFF.md`,
  `docs/review-2026-11/BRIEF.md`.
- Specification (`docs/specification.md`, normative v2.2): §7.1 (P1 stages,
  fixed-point rule, target cell), §7.2 (trace bytes, leaf map), §7.3
  (pruning, only as it constrains refinement), §10 (refinement
  implementation), §11.1–11.3 (capacity, live memory, layout), §12.1–12.3
  (ledger, bound categories, small batches), §15 (stronger refiners),
  §16 (exact caches). §7.4 golden cases read for trace shape only.
- `docs/performance-lower-bounds.md` in full (U/A/H/E taxonomy §1,
  search §4, work/span §5, kernels §6.4–6.7, memory §7, gates §11).
- Code, the lane proper: `src/refine/p1.h`, `src/refine/p1.c`,
  `src/refine/README.md`, `src/partition/partition.h`,
  `src/partition/partition.c`, `src/partition/README.md`.
- Code, immediate boundary (read because refinement drives it every
  sweep, or it drives refinement): `src/util/sort.h`, `src/util/sort.c`,
  `src/encoding/wire.h`, `src/encoding/wire.c` (trace buffer),
  `src/object/graph.h` (CSR/CSC layout the O stage reads),
  `src/bsgs/group.h` (`tuple_min` contract), `src/search/p1_tree.c`
  (sole caller of `canon_p1_refine_node`; snapshot/restore/individualise
  per child), `src/search/certificate.c` (the observer), the header
  comment and `tuple_min` forwarding in `src/bsgs/chain_backend.c`.

Not covered: internals of `canon_bsgs_tuple_min`
(`src/bsgs/chain.c`), the explicit backend, coset/objective search,
encoding of images, the checker (`checker/`), slice notes beyond what
the module READMEs cite, and anything about node-count pruning policy
(other lanes / `docs/pruning-rules.md`). `tuple_min` cost is therefore
carried below as an opaque per-sweep term `T_GM(f)` (spec §12.1 charges
it as `T_group_node`, separately from `T_refine`); only its call
frequency and allocation behaviour at the interface are reviewed here.

### Notation and the current cost model (reasoned, per refinement call)

`n` degree; `e` distinct arcs of a graph root (each arc contributes one
out-entry and one in-entry, so `2e` pre-merge signature entries);
`L` distinct arc labels; `k` cells at a sweep's entry; `d_v` incident
arc count of `v` (out+in, loop counted twice), `sum d_v = 2e`;
`l_v <= min(d_v, 2Lk)` merged signature length of `v`; `s_i` cell sizes;
`W` sweeps in one `canon_p1_refine_node` call. Because a continuing
sweep strictly increases the cell count (`src/refine/p1.c:366`,
spec §7.1 "if number of cells == c0: break"), `W <= n - k_entry + 1
<= n + 1`, including the mandatory final no-change sweep (spec §7.1:
"One complete no-change sweep is always recorded, even at a discrete
root").

Current cost of one sweep, graph root (`src/refine/p1.c:310-368`):

| Step | Code | Cost (reasoned) |
|---|---|---|
| Build all signatures | `p1.c:200-249` | `O(2e)` entry writes with a 64-bit multiply each, plus per-vertex generic stable sort `sum_v O(d_v log d_v)` comparisons and 16-byte `memcpy` moves, plus `O(2e)` merge |
| Rank all vertices globally | `p1.c:284-296` | `O(n log n)` signature comparisons, each `O(l)` worst, plus a second adjacent-comparison pass `O(n·l)` |
| Split by rank | `partition.c:101-125` | `sum_i O(s_i log s_i)` comparisons/moves, plus `O(n)` `rebuild_index` |
| Emit `STAGE_O` | `p1.c:18-35` | `Theta(k)` bytes, one buffer call chain per U32 |
| Collect `F`, `tuple_min`, pull back orbits | `p1.c:336-351` | `O(k) + T_GM(f) + O(n)` gather |
| Split by orbit id, emit `STAGE_G` | `p1.c:352-353` | as split/emit above |

So one `canon_p1_refine_node` call costs
`W · ( O(e log d_max + n log n · l + n log s_max + k) + T_GM )` worst
case, before the per-child snapshot/restore/individualise `Theta(n)`
charged by the search (`src/search/p1_tree.c:409,468-469`). For a
non-graph root the `e` terms vanish but every sweep still pays the
G-stage split, pull-back, both trace tokens and `T_GM`:
`W · (O(n log s_max + k) + T_GM)`, i.e. `O(n^2 log n)` of partition work
per search node in the worst case. Multiplied by the visited node count
`V` this is the `sum_nodes(T_refine + T_group_node +
T_split_restore_replay)` term of spec §12.1. `V` itself is an
algorithm (**A**) quantity, not a problem bound
(`docs/performance-lower-bounds.md` §4); nothing below claims a **U**
improvement. All proposals keep the specified computation DAG's
*outputs* (partition sequence, trace bytes, leaf order) and reduce its
**A**-cost realisation, except where explicitly flagged as needing a
new profile ID.

### What "preserves P1 exactly" must mean here

Profile `0x0001` is frozen by trace and bytes: spec §11.1, "The profile
ID names the output semantics only (a pruned run that reproduces P1's
trace, bytes and witness reports profile `0x0001`)". For this layer the
observable sequence per node is exactly: `NODE(depth)`, then per sweep
the cell-size vector after O and after G (spec §7.2 token table), the
final ordered partition (its singleton order is the leaf list `L`,
§7.2: "At a leaf extract L from the partition's singleton order"), the
target cell "minimising (cell size, cell position), among size > 1"
(§7.1), and, in certificate mode, the observer records
(`src/refine/p1.h:94-104`). Member order inside a non-singleton cell is
declared non-semantic (§7.1: "Internal order of members within a cell
is not semantic"; "Raw atom IDs may choose visitation order only"), so
in principle it may change without changing trace, bytes or witness of
a *complete unpruned* run. In practice it also fixes child visitation
order (`src/search/p1_tree.c:429-435`), hence certificate child order
and the representatives chosen by the orbit prune; a change there is
not a profile change but can be a work-policy-count and certificate
change. Every proposal below therefore either (a) preserves the exact
member order the current stable sorts produce, making equivalence a
one-line argument, or (b) is flagged where it does not.

## 2. Findings

Ordered by expected gain within this layer, most valuable first. All
are category `performance` unless stated. Each proposal states why the
trace order is preserved, or that it is not.

---

### E-perf-refine-01 — O stage ranks every vertex globally, including vertices whose signature cannot affect the split

- Severity: high. Category: performance.

**Evidence (reasoned).** `canon_p1_stage_o` (`src/refine/p1.c:265-299`)
always (i) builds signatures for *all* `n` vertices
(`p1.c:214`, loop over `g->n`), (ii) stable-sorts the identity array
`order[0..n)` by full signature comparison (`p1.c:289`; comparator
`p1.c:256-263` walking sparse lists via `canon_p1_sig_compare`,
`p1.c:157-177`), (iii) re-compares every adjacent pair to assign ranks
(`p1.c:292` — a second `O(n·l)` pass over comparisons the sort already
performed in effect), then (iv) calls the generic
`canon_partition_split`, which sorts again, this time by rank, inside
every cell (`src/partition/partition.c:111`).

But `split(P, sig)` is defined cell-locally. Spec §7.1: "`split(P,
sig)` replaces each old cell, in its old position, by its nonempty
signature classes in increasing lexicographic signature order. It
never reorders old cells or merges distinctions." Three consequences
the code does not use:

1. Only the relative signature order *inside one entry cell* matters.
   The global sort establishes order between vertices in different
   cells that is then thrown away.
2. A vertex in a singleton entry cell cannot change anything whatever
   its signature: its cell is replaced by itself. Yet its signature is
   built (`O(d_v log d_v)`), it participates in `O(n log n)`
   comparisons, and in a discrete partition (`k = n`) the whole
   `O(e log d + n log n · l)` O stage is a provable identity that is
   paid in full — including on the mandatory final sweep of every
   leaf node, where the partition is discrete by definition. The same
   holds for the G-stage pull-back and split over singletons
   (`p1.c:349-352`).
3. Sorting members of a cell by signature *is* the split for that
   cell: the sorted order, cut where the signature changes, is
   exactly the new `lab` segment and its boundaries. The rank array
   and the second (rank) sort are a redundant round-trip.

Worst-case illustration (derivation, not a run): regular graph, `L=1`,
late sweep with `k = Theta(n)`, each `l_v = Theta(n)` distinct
neighbour cells. Current ranking is `Theta(n log n · n) = Theta(n^2
log n)` signature-entry steps per sweep, although almost every cell
is already a singleton and the correct O work is `O(n)`.

**Recommendation.** Fuse ranking into a per-cell split for the O
stage, and compute signatures only for vertices in non-singleton
entry cells:

- Add `canon_partition_split_by_cmp` (or an O-stage-private routine
  in `p1.c` operating on the partition arrays): for each entry cell
  of size `> 1`, stable-sort its members by `canon_p1_sig_compare`,
  write them back in that order, cut a new boundary exactly where
  adjacent signatures differ, and maintain `pos`/`cell_of` for the
  moved members (see finding 02). Cells of size 1 are copied through
  untouched.
- In `canon_p1_graph_signatures`, build signatures only for `v` with
  `cell_size(cell_of[v]) > 1`. The entries array is indexed by
  `sig_offset(g,v) = out_start[v] + in_start[v]` (`p1.c:188-191`),
  a per-vertex slot independent of which other vertices are built,
  so skipping is a pure guard, no repacking.
- Apply the same restriction to the G-stage pull-back: iterate the
  `lab` segments of non-singleton cells instead of `v = 0..n`
  (`p1.c:349-351`); singletons need no `sig` value if split also
  skips size-1 cells (finding 02). `tuple_min` itself is still
  called (its `u`/observer contract is finding 05).

*Why the trace is preserved.* For each entry cell, the classes and
their order are "nonempty signature classes in increasing
lexicographic signature order" (§7.1). The current code obtains them
via global ranks that "respect the global signature order"
(`p1.c:279-283` comment); restricted to one cell, increasing global
rank and increasing signature are the same order, and equal-rank
(resp. equal-signature) members form one class. Sorting the cell by
signature directly yields the identical class sequence, hence
identical cell sizes in identical semantic positions, hence byte-
identical `STAGE_O`. Member order inside a class: both the current
path and the fused path are compositions of stable sorts keyed first
by signature; the current path's intra-class order is the entry
`lab` order (global sort is stable over `order = 0..n`, ranks are
then split by a stable sort over the cell's `lab` segment — note the
global sort's stability does *not* feed the split, which re-reads
`lab`; the fused stable sort over the same `lab` segment gives the
same intra-class order). Skipped singleton signatures are unread by
any consumer: `canon_p1_signature` is only called from
`vertex_cmp` (`p1.c:260-261`) and tests; the observer receives only
the partition after O (`p1.h:101`, `p1.c:327-332`). One behavioural
difference to decide explicitly: a count overflow
(`p1.c:239-241`) inside a skipped singleton's signature would no
longer be detected. The header already argues it cannot occur for an
imported graph ("whose total multiplicity fits uint64",
`p1.h:72-74`; every signature count is a partial sum of that total),
so no admitted input changes status; the remediation must restate
that argument at the guard, per the spec-citation rule.

**Remediation plan.** Files: `src/refine/p1.c` (`stage_o`,
`graph_signatures`, G pull-back), `src/partition/partition.{h,c}`
(new split-by-comparator or a callback-free variant taking
pre-sorted segments), `src/refine/README.md`. Delete `pt.order`/
`pt.order_tmp` if no other user remains (`p1.h:38-39`). Cost after:
per O sweep `O( sum_{v in fat cells} d_v log d_v  +
sum_{fat cells} s_i log s_i · l  + n )`, collapsing to `O(n + k)`
(trace-bound) when discrete. Tests that would prove it: the existing
golden/trace tiers under work policy `0x0001` must be byte-identical
(traces, bytes, witnesses, node counts); `tests/c/test_signature.c`
extended to cell-restricted signature building; a differential test
old-vs-new split on random partitions/signatures asserting identical
`lab`, `start`, `pos`, `cell_of` (not just sizes). Risk: medium —
touches the hottest invariant (`lab[pos[v]] = v`, §10); the
differential test is the mitigation. Effort: ~2–4 days including
tests. This is the single largest PROFILE-EQUIV win in the layer.

---

### E-perf-refine-02 — `canon_partition_split` comparison-sorts dense integer keys, then rebuilds both indexes in a separate scattered pass

- Severity: high. Category: performance.

**Evidence (reasoned).** `src/partition/partition.c:101-125`: for
every cell, gather `(sig[lab[j]], lab[j])` pairs, stable merge sort
them through the generic `canon_stable_sort`
(`src/util/sort.c:6-48`: `void*` elements, indirect comparator call
per comparison at `sort.c:23`, `memcpy` per element per pass), write
`lab` back, then `rebuild_index` (`partition.c:18-27`) walks all
cells again writing `pos[lab[j]]` and `cell_of[lab[j]]` — two
scattered writes per point, in a second pass over data the split
just touched. The same `rebuild_index` is paid by
`canon_partition_individualise` (`partition.c:142`) and
`canon_partition_restore` (`partition.c:171`), i.e. `Theta(n)`
scattered work per explored child edge of the search
(`src/search/p1_tree.c:468-469`), however small the child's subtree.

Every key ever handed to split in this codebase is a dense bounded
integer: initial keys are subset membership `0/1` or colour ranks
(`src/object/graph.h:113-116`, at most `n` distinct); O-stage keys
are ranks `0..R-1`, `R <= n` (`p1.c:290-295`); G-stage keys are orbit
ids, dense by construction of the §7.1 ordering
(`src/bsgs/group.h:40-44`). Spec §10 explicitly licenses the fix:
"Use flat `lab`, `pos`, `cell_of`, cell spans/order, count/stamp
arrays and touched lists" and "Signature sorting may use fixed-width
radix or comparison sort, but hashes never order semantic classes."
`docs/performance-lower-bounds.md` §6.6 makes the same point from
below: the `Omega(q log q)` comparison bound "cannot forbid
fixed-width radix sorting, direct binary splits, bucket counting,
exact interned keys or trie reuse."

**Recommendation.**

- Replace the per-cell merge sort with a stable counting (bucket)
  split for dense keys, in one pass over the whole partition:
  (1) sequential pass computing `key_at_pos[j] = sig[lab[j]]` (the
  one unavoidable gather; the current pairs array already pays it,
  `partition.c:107`), counting per-cell key frequencies with a
  stamp/epoch array so no `O(n)` clearing per cell; (2) prefix sums
  giving each (cell, key) class its destination segment, in cell
  order then key order — which *is* the new `start[]`; (3) stable
  scatter `lab -> lab2` in `lab` order, writing `pos[member]` and
  `cell_of[member]` at placement time; swap `lab`/`lab2` pointers.
  Detect the no-split case during counting (one distinct key per
  cell) and skip the scatter entirely — the common final-sweep case
  becomes one sequential read pass.
- Keep a generic fallback only if a future caller has non-dense
  keys: either an LSD radix sort (stable, fixed passes over
  `uint32`) inside the same routine, or keep the merge-sort path
  behind a `key_bound > n` branch. State the bound in the API:
  `canon_partition_split_dense(p, sig, key_bound)`; audit the three
  call sites (`p1.c:131,297,352`) for the bound each supplies
  (after finding 01, only two remain).
- Fold `rebuild_index` into split as above. For
  `individualise`: update `pos` only for the shifted members of the
  affected cell (the shift loop `partition.c:132-135` already visits
  exactly them) and `cell_of` only for that cell's members, by
  storing a cell *handle* in `cell_of` plus a handle→semantic-
  position array, so renumbering later cells costs `O(k)` small
  writes instead of `O(n)` per-point writes. Spec §10 sanctions the
  distinction: "Semantic cell position differs from allocation
  handle." `cell_of` is read as a semantic position in exactly two
  places in this layer: signature indexes (`p1.c:221,229`) and the
  search's branching logic; both can take `pos_of_handle[handle]`
  at one extra dependent load per arc — or `cell_of` can keep
  storing semantic positions and individualise updates the suffix
  cells' members, which is `O(n)` worst case but with sequential
  writes and only when a non-last cell is individualised. Pick the
  handle form; it also prepares finding 06.
- For `restore`: either keep snapshots as now and rebuild (the
  scatter remains), or widen the snapshot to include `pos` and the
  handle array so restore is pure sequential `memcpy` (more bytes
  per depth: `+8n` bytes against `Theta(n)` scatter; see finding
  06 for the memory trade).

*Why the trace is preserved.* Stable counting sort and stable merge
sort of the same sequence by the same key produce the *unique*
ordering by `(key, input position)`; the resulting `lab` array is
bit-identical, not merely size-identical, so every downstream
quantity (sizes, `F`, target cell, child order, leaf `L`) is
unchanged. Class boundaries are placed between distinct keys in
increasing key order within each old cell's segment, which is §7.1's
rule verbatim. Writing `pos`/`cell_of` at placement maintains the
§10 invariant `lab[pos[v]] = v` with the same final values as
`rebuild_index`.

**Remediation plan.** Files: `src/partition/partition.{h,c}` (layout:
replace `pairs/pairs_tmp` with `lab2`, `key_at_pos`, count/stamp
arrays — net bytes per point drop, finding 07), call sites in
`src/refine/p1.c`, snapshot layout if widened
(`partition.c:11-16,145-177`) plus `src/search/p1_tree.c` snapshot
sizing. Tests: `tests/c/test_partition.c` differential against a
retained merge-sort reference split on adversarial key patterns
(all equal, already sorted, reverse, singletons, `n=0,1`); full
trace/byte identity tiers. Cost after: split `O(n + distinct keys
touched)`, no comparator calls, no second index pass; no-split
sweep `O(n)` sequential. Risk: medium (core invariant, snapshot
format is private to `partition.c` so blast radius is contained).
Effort: ~3–5 days. Do this before or together with finding 01 —
the fused O split wants the same placement-time index maintenance.

---

### E-perf-refine-03 — Per-vertex signature sorting uses the generic merge sort `n` times per sweep

- Severity: medium. Category: performance.

**Evidence (reasoned).** `src/refine/p1.c:235` calls
`canon_stable_sort` once per vertex per O sweep to sort `d_v`
entries by index before merging duplicates (`p1.c:236-246`). The
sort is type-erased (`void*`, element size, indirect `entry_cmp`
`p1.c:179-185`), copies 16-byte entries with `memcpy` on every merge
pass (`sort.c:24-28`), and is invoked even for `d_v` of 0, 1, 2
(the early return at `sort.c:9-11` still costs the call and setup).
In bounded-degree graphs — the common case for sparse refinement,
cf. `docs/performance-lower-bounds.md` §6.5 — essentially all of
this is overhead per arc. The generation order is also needlessly
unsorted: CSR arcs are sorted by `(source, target, label)`
(`src/object/graph.h:61`), but the signature index orders by
`(label, cell, dir)` (`p1.c:221,229`), so neither the out-half nor
the in-half arrives near-sorted and no run detection could help as
laid out.

**Recommendation**, in increasing ambition; all produce the same
merged entry list (sorted distinct indexes, summed counts), which is
a pure function of the generated multiset, so `canon_p1_sig_compare`
results, ranks and the trace are unchanged. Checked-add overflow
behaviour is preserved under any merge order: all counts are
positive, so every partial sum is `<=` the total and the checked add
(`p1.c:239`) fails iff the true total exceeds `uint64`:

1. *Small-degree specialisation (do first, trivial).* Inline paths
   for `len <= 1` (skip), `len <= K` (`K ~ 8–16`, tune later by
   measurement under milestone M5, not now): insertion sort on the
   typed array, no `tmp`, no indirect call. Medium `len`: a typed
   merge/introsort on `canon_p1_sig_entry` directly instead of the
   `void*` sort.
2. *One global radix pass instead of `n` sorts.* Generate all `2e`
   entries as `(vertex, index, count)` records (two per arc: for
   arc `(s,t,l,m)`, `(s, ((l·k)+cell_of[t])·2, m)` and
   `(t, ((l·k)+cell_of[s])·2+1, m)`), LSD-radix sort by
   `(vertex, index)`, then one linear pass merges equal
   `(vertex, index)` and writes per-vertex segments + `sig_len`.
   Cost `O(e)` with a fixed number of passes (index is `< 2Lk <=
   2^64`; in practice radix on the significant bytes present, or on
   a packed 64-bit key when `vertex` and `index` jointly fit —
   they need not: sort by index within vertex blocks that the
   generation already creates in vertex order if arcs are emitted
   from CSR then CSC per vertex as now, reducing the key to
   `index` per block; a counting radix per large block only).
   Trade-off to state honestly: for very sparse graphs option 1's
   `sum d_v log d_v` is already near-linear with tiny constants;
   the radix wins for high-degree vertices (star-like, `d_max =
   Theta(e)`) where it removes the `log` factor and all
   comparisons. A degree threshold selecting per-vertex strategy
   keeps both.
3. *Secondary arc order at import (later, needs ledger room).* A
   second CSR/CSC order sorted by `(source, label, target)` /
   `(target, label, source)` makes each vertex's entries arrive
   grouped by label, leaving only within-label ordering by
   neighbour cell. This shrinks sort keys and enables counting by
   cell within small groups, at `+O(e)` stored indexes charged to
   `M_input` (§11.2) and import time. Only worth it if profiling
   (M5) shows signature building dominant after 1–2.

**Remediation plan.** Files: `src/refine/p1.c` only (option 1–2);
option 3 touches `src/object/graph.{h,c}` import and the §11.1
admission plan. Tests: `tests/c/test_signature.c` already checks
against a dense implementation — keep it as the oracle; add
high-degree and duplicate-heavy cases (parallel arcs into one cell,
loops). Cost after option 1+2: signature building
`O(e + sum_{small} d_v^2)` bounded by `O(e·K)` worst, resp. `O(e)`
radix. Risk: low. Effort: 1 day (option 1), 2–3 days (option 2).

---

### E-perf-refine-04 — Every sweep rescans every arc and every point; no touched-set or cross-sweep reuse (and what Hopcroft-style queues may and may not do here)

- Severity: high (as a design finding; the safe subset is partly
  subsumed by finding 01). Category: performance.

**Evidence (reasoned).** The sweep loop (`src/refine/p1.c:310-369`)
recomputes both stages from scratch `W` times per node, and the
search calls it fresh at every node (`src/search/p1_tree.c:379`)
starting from the parent's refined partition plus one
individualisation (`p1_tree.c:468-471`). A child therefore re-derives
signatures over all `2e` entries although, relative to its parent,
exactly one cell was split before refinement began. Spec §10 names
the missing machinery ("count/stamp arrays and touched lists") and
immediately fences it: "Sparse adjacency and dense bitplanes must
yield exactly P1's simultaneous signature vectors and stage
results. An asynchronous splitter implementation cannot silently
emit its own intermediate cell ordering into the trace." And: "A
smaller-fragment O((N+m)log N) local algorithm may be used only
after proving its scheduling, fixed-relation, key-cost and
P1-equivalence hypotheses; no such bound is claimed for P1's
repeated full sweeps or arbitrary callbacks."
`docs/performance-lower-bounds.md` §6.5 agrees from below: the
`O((N+m) log N)` smaller-fragment analysis "is an upper/amortised
local-work result under its hypotheses. It is not a lower bound,
and must not be multiplied blindly by node count when inherited
partitions and reusable summaries change per-node work."

**What is PROFILE-EQUIV (recommendation, safe subset).** P1's O
sweep is a *simultaneous* (Jacobi) update against the entry snapshot
(§7.1: "Within O all signatures refer to its entry snapshot"), but
nothing requires computing signature *components that cannot
distinguish anyone*:

1. *Fat-cell restriction* (finding 01): signatures only for members
   of non-singleton entry cells. The arc work per sweep drops from
   `2e` to the arcs incident to those vertices,
   `E_fat = sum_{v in fat cells} d_v`. Trace argument as in
   finding 01.
2. *Coordinate skipping within a cell.* A signature coordinate
   `(l, j, dir)` that is constant (usually 0) over a whole entry
   cell cannot split it. Computing full signatures then comparing
   re-discovers this per comparison. The fused per-cell split can
   instead collect, per fat cell, the union of its members'
   touched coordinates (from their incident arcs) and compare
   signatures — same `canon_p1_sig_compare` semantics, since
   untouched coordinates are zero for every compared pair in the
   cell and the sparse comparison already elides joint zeros
   (`p1.c:136-156`). This is a representation change of the same
   vectors, not a scheduling change; trace identical by the
   elision lemma already proved in the `p1.c` comment.
3. *F-unchanged G reuse across sweeps of one node.* If the O stage
   of a sweep changed nothing (`cells` and the partition are the
   post-G partition of the previous sweep), then `F` (a function of
   the partition, `p1.c:336-341`) is the previous sweep's `F`, the
   previous `tuple_min` outputs `(u, orbit)` remain valid answers
   for the same input (`src/bsgs/group.h:38-47` contract is a pure
   function of `(G, F)` up to the "any `u`" freedom, and §7.1
   proves the pulled-back orbit signature is independent of that
   choice: "if F^u=F^v=M, u⁻¹v∈G_M… so the pulled-back ordered
   orbits agree"), and the G split will again change nothing. The
   engine may therefore skip `T_GM` and the split, but must still
   emit both stage tokens (sizes unchanged) and still invoke the
   observer with the cached `F, u`. This saves one `T_GM` — often
   the dominant per-sweep term — on every node's mandatory final
   sweep whenever its O stage is stable. Care: this is *not*
   available when O split something (new singletons may extend
   `F`), and in certificate mode the observer's rebase work
   (finding 05) must still happen or be shown reproducible from
   the cached chain — flag as a checker-format question, do not
   assume.
4. *Exact `tuple_min` memoisation keyed by `(G, F)`* across nodes,
   as spec §16 anticipates verbatim: "Normalised group-refiner
   keys include G, ordered fixed tuple F (or M with its verified
   transport), ordered orbit convention and profile stage. A hit
   must produce exactly the same logical stage/trace as
   recomputation. Eviction changes time only." Symmetric siblings
   frequently (conjecture — measure at M5) present the same `F` up
   to the group's own action; the §16 key as written only licences
   literal-`F` hits. Store `(u, orbit)` in a workspace-local table
   with exact key comparison (hash for bucketing only, collisions
   resolved exactly, per §16 and §10). Trace identical because the
   stage's logical output is a function of the key.

**What is NOT PROFILE-EQUIV (do not do under `0x0001`).**

- *Hopcroft / Paige–Tarjan style sequential splitter queues*
  ("process the smaller half"; cited here as an external lead —
  the repository's documents do not cite these algorithms by name;
  §10's "smaller-fragment" sentence and lower-bounds §6.5 are the
  in-repo pointers). Such algorithms compute a *stable fixed
  point* by Gauss–Seidel updates. P1 does not refine O to a fixed
  point: it alternates single Jacobi O sweeps with G sweeps and
  records every intermediate size vector. Running O to its own
  fixed point before the first G changes what the first G reads
  (more singletons, larger `F`), deletes recorded sweeps, and so
  changes trace bytes even where the final joint fixed point
  coincides — and the leaf key compares trace *first* (§7.2), so
  the canonical output itself can change. The one sequential form
  that *does* reproduce a single Jacobi sweep is coordinate-wise
  splitting against the *fixed entry cells* in most-significant-
  coordinate-first order (fibres of successive coordinate splits
  are the fibres of the full vector; subcell order by ascending
  count at each step yields lexicographic order) — but that is
  just an MSD-radix formulation of finding 01/02's split, with the
  smaller-half arc accounting applied to *coordinates of one
  sweep*, and it must never let a subcell created inside the sweep
  become a splitter of the same sweep. Amdahl reality: per-sweep
  Jacobi semantics forbid the cross-sweep `O((n+e) log n)` amortisation
  that makes the queue algorithms attractive, because P1 *re-enters*
  the snapshot discipline after every G stage. Any genuine
  queue-to-fixed-point refiner is a stronger refiner under §15
  ("future profiles or proved P1-equivalent shortcuts… An
  explicitly selected weaker profile is a different problem
  identity") and needs a new assigned profile ID (next free after
  `0x0001`, allocated in `include/canon/canon.h` beside
  `CANON_PROFILE_P1`), new golden vectors, and checker rules —
  exactly the gate §7.1 states: "Additional useful profiles need
  assigned IDs and equivalent precision before release".
- Omitting the final no-change sweep, merging the last O and G
  tokens, or splitting G before O: trace changes; new profile ID.
- Orbital/lookahead/higher-dimensional refiners intertwined with
  the sweep: §15 names these as future profiles. New profile ID.

**Remediation plan.** Stage it: (a) fat-cell restriction with
finding 01; (b) F-unchanged reuse inside `canon_p1_refine_node`
(needs the previous sweep's `F`/partition-stability bit; observer
contract decision first); (c) §16 `(G,F)` cache in the workspace
(`src/search` owns the workspace lifetime; the refine scratch
`canon_p1_scratch` is the natural home, `p1.h:44-53`); (d) the
coordinate-skip comparison only if (a) leaves signature comparison
dominant. Tests: trace/byte/witness identity on all tiers; a
forced-collision test for the cache (§16: "Randomised hashing
resolves every collision exactly; forced-collision tests must
still pass"); a test where O is stable but G is not, and vice
versa, pinning sweep counts. Risk: (a) low, (b) medium (observer
semantics), (c) medium (key completeness — reading `tuple_min`'s
contract again for *least-t* determinism: `group.h:39-40` pins
"the least such t as an image array", so cached `(u, orbit)` are
well-defined values, not merely valid ones). Effort: (b) 2 days,
(c) 3–5 days. The queue refiner, if ever wanted, is a profile
project (weeks, spec work) — recommend against spending it inside
P1.

---

### E-perf-refine-05 — `tuple_min` is invoked (and, in certificate mode, a chain rebased and serialised) on every sweep, including provably redundant ones

- Severity: medium. Category: performance (boundary of this lane).

**Evidence (reasoned).** `src/refine/p1.c:344` calls
`g->ops->tuple_min` unconditionally per sweep. The chain backend
forwards with `NULL` scratch (`src/bsgs/chain_backend.c:62-67`) and
its header states "every operation allocates its own scratch
(tuple_min)" (`chain_backend.c:3-4`): per-sweep heap traffic through
`canon_mem_*` inside the hottest loop of the search, against spec
§12.3's small-batch requirement that "no per-call chain rebuild or
allocator is required on an admitted fixed-capacity hot path" and
§17's "no callbacks/heap allocation after successful admission" for
hot entry points. (Whether `canon_bsgs_tuple_min` in fact allocates
on every call was not verified in `src/bsgs/chain.c` — out of scope;
the interface as documented permits it and provides no
workspace-scratch channel, unlike `character`, which does:
`src/bsgs/group.h:68-69`.) The G stage also runs when `F` is empty:
then `M` is empty, `G_M = G`, and the orbit signature is the orbit
partition of `G` itself — a per-group constant recomputed at every
such sweep of every node.

In certificate mode the observer multiplies this: `observe_g`
(`src/search/certificate.c:256-292`) rebases the root chain onto
`M` (`certificate.c:282`, `canon_bsgs_rebase`, freeing the previous
rebased chain each time) and serialises the whole rebased chain
(`certificate.c:286`) **per sweep**, plus `observe_o` re-walks the
partition writing the same sizes `append_stage` just wrote
(`certificate.c:234-241,253` duplicating `p1.c:31-33`).

**Recommendation.** (i) Give `tuple_min` an optional caller scratch
parameter modelled on `character`'s (`group.h:68-74`), owned by
`canon_p1_scratch` (grow-only, workspace lifetime), so the refine
loop allocates nothing per sweep. Pure interface change; the
returned `(t, orbit)` are contractually identical, trace trivially
preserved. (ii) Cache the `F = ∅` result per group handle lazily
(orbit ids of `G`, `u` = least element attaining the empty minimum
— the contract's "least such t" at `group.h:39-40` makes the value
well-defined); general `(G,F)` memoisation is finding 04 item 4.
(iii) Do **not** skip `tuple_min` for a discrete partition in
certificate mode: the checker consumes `F`, `u`, `M` and the
rebased chain per sweep (`certificate.c:264-290`), so skipping
changes the certificate, which needs a checker rule change first
(spec §7.3's pattern for shortcuts: "A caller requesting a trace
certificate receives the prescribed trace or a checker rule for
this shortcut"). Without a certificate, skipping both stage
computations (not the tokens) when discrete *is* trace-preserving:
every cell is a singleton, so both splits are identities for any
signature — but note `canon_p1_refine_node`'s documented post-state
"`s->pt.fixed` and `s->pt.u` hold the last sweep's F and u"
(`p1.h:109-110`); a repo-wide search in this review found no reader
of those fields outside `p1.c`, so the contract can be narrowed,
but the status under allocation-fault injection would change (a
call that today can return `RESOURCE_LIMIT` from `tuple_min` would
succeed). §11.1 only promises schedule-independence for
`CAPACITY_LIMIT`, not for allocation failure, so this is legal;
record it as a deliberate behaviour note, and check the S8 fault
tests' expectations before landing. (iv) Fuse the observer's size
writing with `append_stage`: pass the observer the just-emitted
token's payload slice (trace buffer offset/length) so
`observe_o`/`put_sizes` become one `memcpy` of bytes already
computed (certificate payload is `U32(k) || sizes`, the trace token
minus its tag byte — compare `certificate.c:236-240` with
`p1.c:29-33`). Certificate bytes identical by construction.

**Remediation plan.** Files: `src/bsgs/group.h` + both backends
(scratch parameter; this crosses into D-perf/group lanes — flag,
do not unilaterally widen), `src/refine/p1.{h,c}`,
`src/search/certificate.c` + `p1.h` observer signature. Tests:
trace/byte identity; certificate acceptance tiers unchanged
byte-for-byte for (iv); fault-injection tier reviewed for (iii).
Risk: low for (i)/(iv), medium for (iii). Effort: (i) 2–3 days
across backends, (iv) half a day, (iii) gated on decisions above.

---

### E-perf-refine-06 — Snapshot/restore is `Theta(n)` scattered work per child; the snapshot stack is `Theta(n · depth)`

- Severity: medium. Category: performance.

**Evidence (reasoned).** Per internal node the search saves one
snapshot (`src/search/p1_tree.c:409`, `2n+2` words,
`src/partition/partition.c:145-161`), and per explored child
restores it and re-individualises (`p1_tree.c:468-469`). Restore
copies `lab`+`start` sequentially (`partition.c:164-172`) but then
pays `rebuild_index`'s `Theta(n)` scatter (finding 02), and
individualise pays it again (`partition.c:142`) — two full
scattered index rebuilds per tree edge, before the child spends
anything on refinement. Retained snapshot memory along a DFS path
is `depth · (2n+2)` words with `depth <= n-1` (§7.2 termination),
i.e. `Theta(n^2)` words worst case inside `sum_workers(…
snapshots …)` of the §11.2 live-memory equation; §11.2 itself
warns of exactly this shape ("A full trail of Θ(N) entries at d
frames can be Θ(Nd); at N=d=100,000 … 80 GB") and prescribes the
strict fallback: "one mutable node state and O(n) branch recipes:
rebuild from the immutable root to advance a sibling". The
partition header acknowledges the debt: "The trail/replay
variants of spec 11.2 arrive in slice S8" (`partition.h:10-11`) —
S8 has landed (HANDOFF) without them; the comment is stale
(documentation, low).

**Recommendation.** Two PROFILE-EQUIV steps, in order. (a) Remove
the second rebuild: after `restore`, `individualise` with
placement-time index maintenance (finding 02) makes the edge cost
one sequential copy plus `O(cell + k)` updates; widening the
snapshot to carry `pos` (+handle map) removes restore's rebuild
too, trading `+4`–`8n` bytes/depth of sequential copies for
`Theta(n)` scatter — usually a win because `memcpy` streams at a
fraction of scatter cost per element (qualitative hardware
statement, to be confirmed at M5; the lower-bounds doc §6.6 notes
rollback bytes are chosen-DAG cost, not a universal floor).
Trace preserved trivially: snapshots are internal state; restore
must (and by construction does) reproduce the saved `(cells,
start, lab)` exactly, and widened fields are deterministic
functions of them. (b) Bound the quadratic term: keep snapshots
only to a fixed depth/checkpoint stride within the admission
plan's slack and replay individualisations from the nearest
retained ancestor beyond it (§11.2: "Optional fixed-depth
checkpoints/trails reduce recomputation within the reserved
slack; eviction never removes coverage"). Replay must reproduce
the identical partition — it does, since refinement from a given
partition is a deterministic function (the whole P1 argument);
visitation order and node counts are unchanged because replay is
not a tree visit and must not emit tokens or consume quota
(§11.1: quota counts `NODE` tokens of the named traversal).
Admission plan (`M_live`, §11.2) must be updated whichever form is
chosen — flag to the S8-plan owner; a snapshot widening that the
plan under-charges would repeat the defect class HANDOFF records
the S8 review finding (plan < measured peak).

**Remediation plan.** Files: `src/partition/partition.{h,c}`,
`src/search/p1_tree.c` (snapshot stack, `prepare`), admission
accounting wherever snapshot words are charged. Tests: deep-path
stress (pathological depth `n-1`) asserting identical traces and
`peak <= plan` (the S8 invariant); partition differential tests.
Risk: medium (memory-plan coupling). Effort: (a) 1–2 days with
finding 02, (b) ~1 week including plan surgery.

---

### E-perf-refine-07 — Working set per point/arc is larger than the algorithms need; layout fights the cache in the small-batch regime

- Severity: low–medium. Category: performance.

**Evidence (computed from layouts).** Live refine/partition arrays:
partition `lab,pos,cell_of,start,new_start` ≈ `20n` B +
`pairs,pairs_tmp` `16n` B (`partition.h:32-38`); P1 scratch
`sig_len` `8n` + six `uint32` arrays `24n` (`p1.h:31-40`); signature
entries: two halves of `2e` entries × 16 B = `64e` B
(`p1.c:99-115`, `p1.h:23-26`) — the sort-scratch half is as large
as the data half although only the largest single vertex's segment
is ever sorted at once (`p1.c:235`). Total ≈ `68n + 64e` bytes
before trace, snapshots, group and object state, against §11.2's
"40–64 bytes/vertex/worker is only an array planning estimate"
and §12.3/§6.7's requirement to *measure* the live set in the
small-batch (≤128 atoms) regime rather than assume a model.
Access patterns: `rebuild_index` writes two separate arrays
(`pos`, `cell_of`) at the same scattered index per point (two
cache lines per point where one 8-byte `{pos,cell}` record would
do); the global vertex sort (finding 01) dereferences signatures
in random vertex order over a `32e`-byte region; `append_stage`
performs `k+2` nested calls `put_u32 -> put_bytes -> reserve`
(`p1.c:29-33`, `src/encoding/wire.c:55-87`) although it already
reserved the exact token size (`p1.c:22-28`) — in the §12.3
cache-resident regime this is pure instruction/branch overhead per
sweep, the regime where "computation can be dominated by
instructions, dependencies and branch recovery" (§12.3).

**Recommendation** (all byte-output-identical; internal layout
only). (a) Size `entries_tmp` to the maximum vertex degree, not
`2e` (or drop it under finding 03's radix, which needs its own
ping-pong buffer — then charge honestly); saves up to `32e` B.
(b) Findings 01+02 delete `pt.order/order_tmp` (`8n` B) and
`pairs/pairs_tmp` (`16n` B), replacing them with one `lab2` +
`key_at_pos` (`8n` B). (c) Interleave `pos`/`cell_of` (or the
handle form of finding 02) into one per-point record. (d) Add an
internal unchecked writer to `wire.h` for use after a successful
`canon_buf_reserve` (the reserve already performed the §11.1 size
checks; the unchecked path writes the same big-endian bytes and
advances `len`), and use it in `append_stage` and the
certificate's dense writers. Trace bytes are definitionally the
same sequence of `U32` encodings (§7.2). (e) Keep the one-block
carving of `canon_p1_points` (`p1.c:54-79`) — it is the right
pattern — and extend it: allocate the partition's arrays as one
block too (currently seven separate `canon_mem_malloc` calls,
`partition.c:60-66`), reducing ledger entries and improving the
chance the arrays share pages. None of this changes any semantic
value; equivalence is by "same values, different addresses".

**Remediation plan.** Files as above; update §11.1 admission
formulas for scratch sizes in the same commit. Tests: ledger
tests (`peak <= plan`), trace identity. Risk: low, except
admission-formula drift. Effort: 1–3 days total, mostly mechanical.

---

### E-perf-refine-08 — No demand instrumentation exists in this layer, so none of the above can be prioritised by evidence yet

- Severity: note. Category: performance / maintainability.

**Evidence (reasoned).** The refine loop exposes no counters for
sweeps per node, arcs scanned, signature comparisons, or
`tuple_min` calls; the S8 metrics (`canon_result_metrics`,
HANDOFF) count nodes/leaves/pruned/automorphisms but nothing
inside `T_refine`. `docs/performance-lower-bounds.md` §11 gate 3
requires a "reproducible demand ledger… arcs/words examined…
Distinguish logical byte counts from measured cache/DRAM"
before performance claims, and §9's envelope is per-node-class.
Every gain estimate in this report is therefore asymptotic and
structural, labelled **A**/**E**, never seconds.

**Recommendation.** Add cheap always-on counters to
`canon_p1_scratch` (sweeps, O entries generated, signature
comparisons and entries compared, split scatter elements,
`tuple_min` calls and total `f`), surfaced through the existing
metrics struct. Counters must not influence any decision (they
are not quota; §11.1). Then rank findings 01–05 empirically at M5
on the §11-gate instance families before committing to the
larger items (04c cache, 03 radix, 06b replay).

**Remediation plan.** Files: `src/refine/p1.{h,c}`,
`src/partition/partition.c`, metrics plumbing in `src/api`
(outside lane; coordinate). Tests: metrics determinism (same
counts across backends where the contract demands it). Risk:
none semantic. Effort: 1 day.

---

### Ranked implementation plan (gain/effort)

| # | Change (finding) | Proposed cost effect | Trace argument | Effort |
|---|---|---|---|---|
| 1 | Dense counting split + placement-time indexes (02) | split `O(n log s) -> O(n)`, one rebuild pass deleted | identical `lab` (unique stable order) | 3–5 d |
| 2 | Per-cell fused O split, fat-cell-only signatures (01, 04a) | kills global `O(n log n · l)` ranking + rank round-trip; discrete sweep `O(n+k)` | same classes/order per §7.1 cell-local rule | 2–4 d |
| 3 | Small-degree typed signature sort (03.1) | removes `n` generic sort calls/sweep | same merged list (pure function) | 1 d |
| 4 | Unchecked trace writer + observer payload reuse (07d, 05iv) | `Theta(k)` mandatory bytes at `memcpy` cost, not per-U32 calls | same bytes by construction | 0.5–1 d |
| 5 | `entries_tmp` sizing, array diet (07a–c,e) | up to `-(32e + 16n)` B live set | addresses only | 1–2 d |
| 6 | `tuple_min` workspace scratch; `F=∅` cache (05i–ii) | zero allocs/sweep; one `T_GM` saved on empty-`F` sweeps | same contract values | 2–3 d |
| 7 | F-unchanged sweep reuse (04.3) | one `T_GM` + split saved per stable-O final sweep | §7.1 orbit-independence of `u` choice; tokens still emitted | 2 d |
| 8 | Edge-cost fix: widened snapshot, no double rebuild (06a) | per child: scatter `Theta(n)` → sequential copy | snapshot is internal, exact state | 1–2 d |
| 9 | Global radix signature build (03.2); `(G,F)` cache (04.4); checkpoint/replay bound (06b) | instance-dependent; removes `log` factors / repeated `T_GM` / `Theta(n^2)` snapshots | as argued per item; §16 exact-key licence | days–week each |
| — | Queue-to-fixed-point / stronger refiner (04) | potentially largest, **not** P1 | **changes trace ⇒ new profile ID** (§7.1, §15), new goldens + checker rules | profile project |

A pruning-style change that kept trace/bytes/witness but changed
explored-node counts would need a new *work-policy* ID, not a
profile ID (§11.1); none of items 1–9 changes node counts, child
order, or member order, so neither ID moves. Any variant that
permits a different intra-cell member order (e.g. an unstable
split) keeps profile `0x0001` for complete unpruned runs but must
be re-examined against work policies `0x0002/0x0003` counts and
certificate bytes before it can claim the same — recommend simply
requiring stability everywhere, as the table does.

## 3. Spec ambiguities

None that block this lane's proposals were found. For the record,
two sentences whose reading the remediation must pin down in code
comments rather than leave implicit (quoted, not resolved here
beyond the readings already argued in the findings):

1. §10: "A smaller-fragment O((N+m)log N) local algorithm may be
   used only after proving its scheduling, fixed-relation,
   key-cost and P1-equivalence hypotheses; no such bound is
   claimed for P1's repeated full sweeps or arbitrary callbacks."
   The spec does not define "local algorithm" or list the
   hypotheses; finding 04 takes the conservative reading that
   only within-sweep, snapshot-faithful formulations qualify.
2. §7.1: "Raw atom IDs may choose visitation order only." Read
   together with §11.1's work-policy counts, it is not stated in
   §7 whether a visitation-order change that alters a *pruned*
   policy's explored representatives (not its count) is a
   work-policy change; `docs/pruning-rules.md` (not re-read for
   this lane) presumably owns that. All proposals above sidestep
   the question by preserving member order exactly.

## 4. Things checked and found sound

- Sparse signature comparison (`p1.c:157-177`) against §7.1's
  dense lexicographic order: the elision argument in the comment
  (`p1.c:136-156`) is correct, including the asymmetric case
  (smaller next index wins) and loop double-counting
  (`p1.c:217-232` vs §7.1 "A graph loop contributes once to each
  incoming/outgoing count"). Reasoned; the dense-model test
  exists (`tests/c/test_signature.c`, not run per brief).
- Entry-snapshot discipline: signatures for all vertices are
  fully built from the unmodified partition before any split
  (`p1.c:200-249` writes only scratch), satisfying §7.1/§10's
  simultaneity requirement; G reads the post-O partition
  (`p1.c:336-341`).
- Fixed-point rule: exactly one final no-change sweep is emitted,
  including `n=0` and discrete roots (`p1.c:364-368`); `NODE`
  precedes the first sweep (`p1.c:305-309`); token bytes and
  `U32` framing match §7.2 (`p1.c:13-35`).
- G stage pull-back `sig[v] = orbit[u[v]]` (`p1.c:349-351`)
  matches §7.1 "sig(v) = position of the orbit containing u[v]"
  and the interface's target-label convention
  (`src/bsgs/group.h:44-45`); `F` is collected in partition
  (cell) order, not branch order (`p1.c:337-341`).
- `split` never reorders old cells and emits only nonempty
  classes in increasing key order (`partition.c:104-122`);
  stability keeps intra-class member order deterministic.
- Overflow discipline in this layer: signature index range
  `2·L·k` checked in `uint64` (`p1.c:209-213`), count accumulation
  checked (`p1.c:239`), trace token size checked before reserve
  (`p1.c:22-24`), scratch sizes checked via `canon_size_*`
  (`p1.c:61-65,100-103`) — consistent with §11.1. Scratch is
  grow-only and reused across solves (`p1.h:42-63`), and
  refinement itself performs no allocation except trace growth
  and whatever `tuple_min` does internally (finding 05).
- Termination: cell count strictly increases across continuing
  sweeps and is bounded by `n`, matching §7.2's argument.

*End of lane E-perf-refine report.*

