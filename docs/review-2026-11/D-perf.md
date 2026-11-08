# Review lane D-perf: performance (refactorings and algorithms, P1-exact)

Status: **partial — items marked IN PROGRESS are unfinished.** All other findings are evidenced as stated.

## 1. Scope

Read: `docs/review-2026-11/BRIEF.md`, `CLAUDE.md`, `README.md`, `HANDOFF.md` (incl. "Costs accepted for
now", decision 8), `docs/performance-lower-bounds.md` (whole), `docs/specification.md` §§7, 8, 9, 10, 11,
12, 13, 14, 15, 16 (and §3/§17 as referenced), `docs/pruning-rules.md`,
`refs/compare/runs/2026-10-19-m0/README.md`, `refs/ref-a/run_cases.py` (work-policy handling),
`refs/ref-b/QUESTIONS.md`, `refs/ref-b/src/p1.jl` and `refs/ref-b/src/group.jl` (only to explain the
0.27 s vs ≥ 17 min discrepancy on T3 case 000077; ref-b was **not run**).

Code read in full: `src/refine/p1.c`, `src/search/p1_tree.c`, `src/partition/partition.c`,
`src/perm/perm.c`, `src/symmetry/symmetry.c`, `src/coset/enumerate.c`, `src/coset/least.c`,
`src/encoding/group_stream.c`, `src/object/object.c`, `src/bsgs/chain_backend.c`, `src/bsgs/chain.h`,
`src/search/objectives.c` (through the labeling run). `src/bsgs/chain.c` read for transporter/sift
(240–358), insert/closure/build/rebase (387–730), orbit ids and tuple minimum (860–1066).
`src/bsgs/explicit.c` first 200 lines.

Not covered / IN PROGRESS: `src/object/dag.c` subgroup/coset-leaf action and stream costs;
`src/object/graph.c` action cost per leaf; `src/search/certificate.c` emission cost; `src/api/api.c`
admission-plan cost; checker performance (out of lane except where a producer proposal changes
certificate contents); `src/bsgs/verify.c` cost split inside `build_verified` (cited only via
`canon_bsgs_insert_verified` call structure).

Evidence runs (permitted by the brief: one build, tiny snippets under `/tmp/canon-review-D-perf/`,
seconds each, `nice -n 19`; **no test suite, corpus or reference run was executed**):
`nice -n 19 make -j4` once; snippets `tm.c`, `en.c`, `en2.c` linking `build/make/libcanon.a`, on two
groups I constructed from T3's documented families (`tools/gen_cases.py:19`): the wreath product
Sym(4) wr Sym(8) on 32 points (4 generators, order 4 438 236 667 576 320, chain depth 24, 31 strong
generators) and Sym(20) on 32 points (order 20!, depth 19, 33 strong generators). These are
representative hot-path measurements, **not** case `t3-s20261013-000077` itself (not run, per instructions).

Conventions for every proposal: **P1** = profile `0x0001` output (trace, CDAG-2 bytes, witness under the
mode in force). "Preserves counts" = same work-policy node/visit count, so no new work-policy ID and no
`CAPACITY_LIMIT` boundary moves. Anything that changes counts is flagged as needing a new ID plus a
coverage lemma in `docs/pruning-rules.md` and a checker rule (spec §11.1, §7.3; `CLAUDE.md` rules).

---

## 2. Findings (ranked by expected gain / effort at the end, §3)

### D-perf-01 — The G stage and leaf map rebuild whole stabiliser chains from scratch, per step, per node, with no reuse; this is why T3-000077 stalls

- Severity: **critical** (performance). Category: performance.

**Evidence (reasoned from code + measured on my snippets).**

The P1 loop calls the group backend's `tuple_min` once per refinement sweep of every tree node
(`src/refine/p1.c:344`, inside the sweep loop at `src/refine/p1.c:310-369`), and once per competitive
leaf (`src/search/p1_tree.c:205`). The chain backend's tuple minimum
(`src/bsgs/chain.c:973-1027`, `tuple_run`) implements spec §7.2's constructive procedure
(`t←tu`, `H←H_b`) by, at almost every list position where the current base point is not the required
`b`, calling `canon_bsgs_rebase` (`src/bsgs/chain.c:1002`), which is a **complete deterministic
Schreier–Sims closure from the level's strong generators** (`canon_bsgs_build`,
`src/bsgs/chain.c:648-676, 680-716`), then freeing the previous transient chain (`chain.c:1007`).
The code itself names this: "That is one rebuild per such step… At most O(n) rebuilds per call, the
known S3 cost (M5 work)" (`chain.c:960-965`); `HANDOFF.md` decision 8 lists it under "Costs accepted
for now". Three multipliers make this far worse than "one rebuild per step of tuple_min" suggests:

1. **The list processed is `L` followed by `0,1,…,n−1`** (`chain.c:981, 990`) so that the returned `t`
   is the *least image-array minimiser* (contract in `src/bsgs/group.h:40`). For the G stage, spec §7.1
   requires only "*choose any u in G with F^u=M*" — the pulled-back orbits are independent of the
   choice (§7.1, paragraph after the algorithm: "if F^u=F^v=M, u⁻¹v∈G_M, which preserves every one of
   its orbits, so the pulled-back ordered orbits agree"). The `0..n−1` tail is therefore up to `n`
   extra stabilisation steps — each a potential full rebuild — buying a property P1 does not need at
   internal nodes. (At leaves `L` is a full list and `t_L` is unique by §7.2, so the tail is also
   unnecessary there; it exits only when the suffix generator list happens to be empty,
   `chain.c:987-988`.) Callers of `tuple_min` are exactly the G stage and the leaf map (grep:
   `src/refine/p1.c:344`, `src/search/p1_tree.c:205`); the deterministic witness is computed separately
   from the complete stabiliser (`src/search/objectives.c:310-326`), so nothing semantic consumes
   "least minimiser" from this entry point.
2. **No reuse across calls.** Every sweep of every node re-derives the stabiliser chain sequence
   `G, G_{b_1}, G_{b_1 b_2}, …` from the immutable root chain and frees it. Sibling nodes, and the
   repeated sweeps of one node (F only grows by the newly singleton points), recompute identical
   prefixes. The root sweep of a subset case typically has `F = ∅` (no singleton cells): the required
   result is just the orbit ids of `G` itself with `u = id` admissible, yet the code walks the whole
   `0..n−1` tail.
3. **Each rebuild is a full closure**, cost `O(Σ_i r_i s_i)` Schreier candidates (spec §9.2's own
   ledger), each candidate a dense `O(n)` composition plus a sift whose transversal walks apply a
   dense permutation per Schreier-tree edge (`chain.c:273-287`), plus provenance-DAG nodes per product
   (see D-perf-04) and per-level allocations through the ledger allocator.

Measured (`/tmp/canon-review-D-perf/tm.c`, Sym(4) wr Sym(8), n = 32; one `build_verified` of the root
chain costs 3 713 candidates / 31 insertions / 0.9 ms):

| call | time | candidates | sifts | dense compositions | insertions |
|---|---|---|---|---|---|
| `tuple_min`, F empty (root G stage) | 1774 µs | 15 100 | 14 158 | 74 187 | 279 |
| `tuple_min`, F = 1 singleton | 1780 µs | 15 100 | 14 158 | 74 190 | 279 |
| `tuple_min`, F = 8 singletons | 1309 µs | 11 315 | 10 689 | 51 926 | 264 |
| `tuple_min`, leaf (full L) | 1296 µs | 11 209 | 10 604 | 51 373 | 266 |
| `canon_bsgs_orbit_ids` level 0 (all F=∅ needs) | **2.1 µs** | — | — | — | — |

Sym(20) on 32 points gives the same picture (F empty: 1584 µs, 13 054 candidates;
`/tmp/canon-review-D-perf/en2.c`). Note F=1 costs the same as F=∅: the least-minimiser tail dominates,
not the semantics.

**The T3-000077 arithmetic.** The case is `canonical_image`, n = 32, 3 generators, a subset
(`refs/compare/runs/2026-10-19-m0/README.md:37-39`); ref-a runs work policy `0x0001` (unpruned,
`refs/ref-a/run_cases.py:18-19, 272, 307`) under the T3 quota of 10⁶ nodes
(`tools/gen_cases.py:508`). The quota check is at node entry (`src/search/p1_tree.c:363-366`), so the C
path *will* answer `CAPACITY_LIMIT` — after doing the per-node work for 10⁶ nodes. At ≥ 1 sweep per
node and 1.3–1.8 ms per sweep's G stage on a group of this shape, that is **≈ 1 300–1 800 s ≈ 22–30
min**, consistent with "had not returned after about 17 minutes" (the case's actual group differs from
mine; this is an order-of-magnitude derivation, labelled as such). Leaves add another tuple_min each.

**Why the Julia reference answers in 0.27 s (read, not run).** ref-b's P1 uses a *stabiliser trie*:
`GNode` caches `children[b] = H_b` in a `Dict` shared for the whole search
(`refs/ref-b/src/p1.jl:6-16, 49-55`), so each distinct pointwise stabiliser along a minimised prefix is
built **once per solve**, not once per node; its `lexmin_tuple` stops after the entries of `L` (no
least-minimiser tail, `p1.jl:71-89`); and its per-step work is point queries on a Schreier tree plus
one dense product, with orbit minima read off a BFS computed once per trie node (`p1.jl:18-47`). Its
quota readings (Q1–Q3, `refs/ref-b/QUESTIONS.md`) differ from ref-a's only at the boundary (discrete
root counted 0, etc.) and cannot explain a ≥ 10³ factor. The gap is (1)+(2)+(3) above.

**Recommendation.** Replace the rebuild-per-step tuple minimum with a *cached stabiliser-trie*
realisation of exactly the §7.2 procedure, and drop the least-minimiser tail from the P1 call sites:

- Introduce a per-solve (workspace-owned) cache keyed by the **minimised prefix** `M` prefix
  `(b_1,…,b_k)` — equivalently a trie whose node at depth k holds a verified-quality chain (or a
  Schreier-tree + generator set) for `G_(b_1..b_k)`, its orbit minima, and its orbit ids. This is the
  "normalised group-refiner key" cache spec §16 already licenses verbatim: "Normalised group-refiner
  keys include G, ordered fixed tuple F (or M with its verified transport), ordered orbit convention
  and profile stage. A hit must produce exactly the same logical stage/trace as recomputation.
  Eviction changes time only." (spec §16, second cache paragraph).
- `tuple_run` then becomes: walk/create trie children; per step one orbit-min lookup (BFS tree of the
  trie node) and one transporter walk (point queries, composing `t` once per step — or lazily, see
  D-perf-05); orbit ids read from the trie node representing `G_M`.
- Drop the `0..n−1` tail for the two P1 call sites (keep a least-minimiser variant only if a real
  consumer appears). The G-stage `u` recorded in certificates may then differ from today's: spec §7.1
  permits any `u`; the certificate checker must be confirmed to accept any `u` with `F^u = M`
  (IN PROGRESS: checker rule not yet re-read for this report). P1 trace/bytes/witness are unchanged by
  the §7.1 argument quoted above and by §7.2 uniqueness at leaves.
- Cheapest first increment, before the trie: special-case `F = ∅` (and generally, reuse
  `canon_bsgs_orbit_ids` on an existing chain when the current `H` is already a suffix of a chain whose
  base starts with the minimised prefix — the common case when minimisation follows the chain's own
  base order). Measured headroom for the F=∅ sweep alone: 1774 µs → 2.1 µs.

**Expected gain.** Per G-stage sweep: from `O(n)` full closures (≈ 1.3–1.8 ms at n = 32) to
`O(|F| · (orbit query + word application))` with trie hits, i.e. single-digit µs after warm-up; total
trie construction per solve is bounded by the number of *distinct* minimised prefixes actually met.
This is the difference between the current ≥ 17 min and ref-b's 0.27 s on quota-bound canonical-image
cases; expect ≥ 10²–10³× on exactly the T3 regime (n ≤ 48, weakly refining roots, 10⁶-node quota),
and a large constant-factor win on every canonical-image/labeling/signed P1 run.

**P1/counts.** Preserves P1 bytes, trace and node counts under every existing work policy (the §7.1/§7.2
arguments above; spec §16 cache contract). No new profile or work-policy ID. Certificate *contents*
(the recorded `u` per G stage) may change while remaining valid — confirm checker acceptance (above)
and expect certificate-byte changes in tests that compare them literally.

**Remediation plan.** (i) `src/bsgs/chain.c`: split `tuple_run` into "any minimiser" (new default for
P1) and keep the current least-minimiser code path only if a caller needs it; add the F=∅/suffix
fast path. (ii) New workspace-owned trie in `src/bsgs/` (or extend `canon_group_ops.tuple_min` with a
scratch/cache parameter — the group is immutable and shared, spec §17, so the cache must live in the
workspace/search state, threaded through `src/refine/p1.c` and `src/search/p1_tree.c`; the explicit
backend can ignore it). Trie node chains are transient/unverified as today's rebases are (S3 review
item 2), built by the same deterministic closure, so correctness rests on the same footing; each built
node should assert by construction (orbit product = suffix order) in tests. (iii) Memory: bound the
trie by the admission plan (§11.2 `M_caches`); eviction of whole subtrees is legal (time only, §16) —
start unbounded-per-solve but charged, since depth ≤ n and branching ≤ n per level is still a large
worst case; record trie nodes/bytes in `canon_metrics`. (iv) Tests that would prove it: byte-identity
of trace/bytes/witness against the current implementation on the existing T1/G1/G2/random tiers under
both backends and all three work policies (the harness already compares exactly this); a stats test
asserting rebuild counts collapse (e.g. `canon_bsgs_stats` candidates for the root sweep of a wreath
case); a quota-boundary case at n = 32 answering `CAPACITY_LIMIT` in seconds. Effort: medium (the
trie is the same shape as ref-b's `GNode`, whose existence in-repo de-risks the design); risk: cache
key must be the *minimised* prefix `M` (semantic), never raw `F` order variants or generator identity
(§16; representation independence, spec §5).

**Literature.** The stabiliser-chain machinery and tuple minimisation are spec §7.2/§9.2's own; the
repo's algorithm audit cites Jefferson–Waldecker–Wilson graph backtracking (2209.02534v4) for
canonical images under arbitrary G with group "refiners" of exactly this orbit-of-tuple-minimum shape
(`reviews/algorithm-literature-review.md` R1/R3). Caching pointwise stabilisers along minimised
prefixes is standard practice in that family — cited here as a **lead**, not re-derived from the local
TeX in this review. The in-repo existence proof that the trie realises the same P1 semantics is ref-b
itself (agreeing on all golden/T1/T2 records).

---

### D-perf-02 — §8.1 enumerator pays a full constrained descent per tree edge (each descent itself rebuild-per-step); enumeration objectives inherit this multiplied by per-leaf object work

- Severity: **high**. Category: performance.

**Evidence (code + measured).** In `src/coset/enumerate.c:98-154`, each internal `visit` (a) rebases
`H` so its base starts at the smallest moved atom unless it already does (`enumerate.c:125-131`: a
full `canon_bsgs_build`), and (b) for **every** child `b ∈ a^H` computes `t_b` — spec §8.1's "least
image-array element of H with a^t_b = b" — by an independent `canon_coset_least` descent
(`enumerate.c:141`). The descent (`src/coset/least.c:221-343`) fixes points `v = 0..n−1` in order and
calls `ensure_base` → `canon_bsgs_rebase` (full build) whenever the next point to fix is not the
current base point (`least.c:144-164, 203`). The file comment concedes the model: "one rebuild …
whenever v is moved by J'… reusing levels is M5 work" (`least.c:13-17`).

Measured (`/tmp/canon-review-D-perf/en.c`, same wreath group): one top-level `canon_coset_least`
= **1.76–2.19 ms with exactly 18.0 rebuilds per call**. A quota-3000 enumeration averaged 5.1 µs/visit
(3 000 descents = one per tree edge, 332 visit-level rebases) because deep `H` are small — but the
visits are *not* uniformly deep: the number of internal nodes at depth d is `Π_{i<d} r_i` over the
orbit sizes, so for groups whose early orbits are large and shrink slowly, a substantial share of the
10⁶-visit quota is spent at depths where each edge still costs a descent through a large `H`. That,
times per-leaf consumer cost (below), is the mechanism behind `HANDOFF.md` M0a decision 5 ("the
unpruned enumeration objectives under that quota take minutes per case on n ≤ 48"). (Sym(20) run:
5.0 µs/visit over the first 2 000 visits, 737 rebases — the deep-node share dominates there too; both
measurements are evidence for the *structure* — one descent per edge, rebuilds per descent — not a
claim about any specific T3 case's orbit profile. **Reasoned, not run, for the minutes regime.**)

Consumers multiply the leaf term (`src/search/objectives.c`): `consume_min` materialises `x^r` and
writes its **complete CDAG-2 stream** (or SIMPLE-UPPER-1 key) at *every* leaf before comparing
(`objectives.c:97-116`); `consume_transporter`/`consume_stabiliser` materialise `x^r` and
extensional-compare per leaf (`objectives.c:138-171`). For DAG roots `canon_root_act_into` re-normalises
the whole arena per leaf (`src/object/object.c:113-124`; subgroup/coset-leaf conjugation cost IN
PROGRESS). Spec §8.2 mandates the *semantics* (compare exact keys, exhaust), not this materialisation
strategy; spec §8.3 explicitly contemplates bounds ("A minimum bound must lower-bound the selected
order key for every permutation in H r") but any pruning use changes visit counts → new work-policy ID
 territory (see recommendation split below).

**Recommendations, split by whether counts change.**

(a) *Count-preserving (do first).* Share D-perf-01's stabiliser trie with the descent and the
enumerator: a descent is a walk down pointwise stabilisers `J' ⊇ J'_v ⊇ …`; with cached children each
step is an orbit query + transporter instead of a rebuild (18 builds → 18 trie steps per top-level
descent). In the enumerator, compute all sibling `t_b` from **one** descent skeleton: the children of
`H r` share the constraint structure (they differ only in the image chosen at `a`); equivalently,
descend once with the "least element of H with a↦b" computed by the trie child's own least-element
walk. Also, the enumerator's per-visit rebase disappears when `H` is a trie node (its base *is* `a`
by construction — choose trie nodes keyed by the enumerator's own stabilisation sequence: smallest
moved atom first, which is exactly a trie path). For consumers: compare `x^r` keys *lazily* — e.g. for
subset roots compare the image bitset against the incumbent's without writing a stream; for graphs
compare the SIMPLE-UPPER-1 key by streaming comparison with early exit (spec §6.6 of the bounds
appendix: "Losing leaves can compare transformed views and stop early. There is no universal
'rewrite the whole graph at every leaf' lower bound."). Keep the incumbent's materialised image, not
just its key, where equality tests can reuse it.

(b) *Count-changing (needs new work-policy IDs + lemmas, do second).* Region bounds for
`LEX_MIN_IMAGE` (§8.3) and stabiliser-aware skipping for `STABILISER` (once `A_known`'s order equals a
proved upper bound… there is none in general — do **not**; §8.2: "A stabiliser prune may omit
solutions only with proof they lie in the final generated subgroup", which *is* available in one form:
if a whole region `H r` satisfies `H ≤ A_known` and `r ∈ A_known`, every hit in it is already in
`A_known` — the exact analogue of §9.4's "C=Jr, C⊆K iff r∈K and J≤K". That prune preserves the final
*group* and bytes; it changes visit counts, so it needs its own work-policy ID, a coverage lemma in
`docs/pruning-rules.md`, and (for certificate v1, not yet existent for enumerations) a checker rule.
Same shape for transporter coset's second phase. Do not fold these into `0x0001..0x0003`.

**Expected gain.** (a) removes the per-edge rebuild factor (measured 18 builds/top-level descent;
expect 10–100× on enumerator-internal work at n ≈ 32–48, more where descents are repeated across
siblings). (b) is instance-dependent, potentially exponential-to-polynomial on symmetric cases, but is
a policy change, not a refactor.

**P1/counts.** (a) preserves bytes and counts (`t_b` is *defined* as the least element — the trie must
return the identical element, which the §9.4 descent semantics fixes; test by byte-identity).
(b) preserves output bytes by its lemma but changes counts → new ID, per §11.1.

**Remediation plan.** After D-perf-01's trie lands: re-express `src/coset/least.c` descent steps as
trie child walks (keep `canon_coset_least` API and its constraint-first order — S4's counterexample in
`least.c:215-220` shows order matters); re-express `src/coset/enumerate.c` visit children via the
node's cached orbit + per-child least walks; add lazy key comparison in `objectives.c` consumers
per root kind. Tests: enumerator visits exactly G once (existing S4 test), byte-identity of all four
objectives on existing tiers both backends, rebuild/descent counters (`canon_coset_stats`) collapse.
Effort: medium; risk: the "least" element must stay bit-identical — the descent's greedy invariant is
the spec's, so any correct trie walk gives the same `t_b`.

---

### D-perf-03 — Verified-subgroup insertion rebuilds **and re-verifies from scratch, from the full generator list, on every insertion** (stabiliser consumer, A_known, §9.4 rule 2)

- Severity: **high**. Category: performance.

**Evidence (code).** `canon_bsgs_insert_verified` (`src/bsgs/chain.c:737-759`): on a non-member it
appends `g` to the generator table and calls `canon_bsgs_build_verified` over **all generators so
far**, i.e. a full closure plus a full independent verification pass, discarding the previous chain.
Callers: the §8.2 stabiliser consumer (`objectives.c:175, 220`), `A_known` insertion for explicit and
implicit automorphisms (`src/symmetry/symmetry.c:98`), and §9.4 rule 2's `K ← ⟨K,g⟩`
(`src/encoding/group_stream.c:151`). The doubling argument bounds insertions by `log2|A| ≤ n log n`,
but the *cost* is `Σ_i (build+verify)(K_i)` with `K_i` growing to the final group — and the stabiliser
consumer pays a membership sift for every hit on top (fine) — so total insertion work is
`Θ(log|A|)` times the final build+verify, with large constants (verification re-derives provenance for
every strong generator). For `A_known` under work policy `0x0003` this sits **inside** the P1 search:
every equal-key leaf that enlarges `A_known` triggers a full verified rebuild
(`src/search/p1_tree.c:150-181` → `symmetry.c:79-105`), in addition to the membership sift and exact
action check (those two are spec-mandated, §7.3).

**Recommendation.** Use the chain builder's existing incremental machinery (`insert` + `closure`,
`chain.c:390-531`) as an *incremental* insert: sift `g` through the current chain; if the residue is
non-identity, insert the residue at the stopping level and run closure only over the newly affected
Schreier pairs — which is precisely what `schreier_done` already tracks (`chain.c:456-463` comment:
"After an insertion of s at level j the new checks are exactly the pairs (new point, any generator)
and (any point, s)"). The verification story needs care, not a shortcut: spec §9.1 "exact verification
is mandatory" is about the chain *being* a correct BSGS for its inputs. Two honest options:
(i) verify **once at the end** (when the chain becomes an answer: complete stabiliser, rule-2 `K` at
`K = H`, `A_known`… but `A_known` is *used* for pruning before the end, so its soundness during the
search is load-bearing — the pruning lemma needs `A_known ≤ A`, which incremental closure by the same
deterministic constructor gives by the same invariant the transient rebases already rely on (S3 review
item 2); membership answers from an *incomplete* chain would be the risk, and closure-until-fixed-point
is exactly completeness for the inserted generators); (ii) incremental verification of only the new
Schreier residues (the verifier's per-level checks are local to changed levels). Either must be argued
in the slice brief against §9.1's verifier list; do not silently downgrade `A_known` to unverified
without recording that the constructor invariant is what the prune then rests on (it already does for
transient `A_S`, `symmetry.c:120-128`).

**Expected gain.** Removes the `log|A|` multiplier on build+verify and the repeated re-sifting of old
generators (`kept_inputs` sort `O(s log s · n)` per rebuild included, `chain.c:550-573`); one
incremental insertion is one sift + closure over new pairs. In stabiliser-heavy enumerations and
symmetry-rich searches this is a 5–30× reduction of the insertion term (estimate from the multiplier,
not measured separately — labelled as such).

**P1/counts.** Output-identical: `A_known` is the same subgroup at every point *if* insertion order
and the closure policy produce the same "inserted or not" answers — membership in `⟨gens⟩` is a
semantic predicate, so yes. One caveat: the *chain* (base/SGS) may differ from today's rebuild chain,
which changes **which** explicit transporter the certificate reads off the Schreier tree for a pruned
child (`p1_tree.c:337-345`) and the orbit *representative discovery order* is unaffected (cell order
decides), so node counts under `0x0002/0x0003` are preserved; certificate bytes may change (validity
unchanged). §9.4 rule 2 depends only on `K` as a set (least outside element), so Group bytes are
preserved.

**Remediation plan.** Expose `canon_bsgs_insert_incremental` reusing `build_ctx` across calls (workspace
scratch, not per-call allocation); keep `insert_verified` as build+verify for the final-answer sites or
replace per option (i)/(ii) decision in the slice. Tests: randomised agreement of incremental vs
rebuild chains (order, membership on random words, same final Group/coset bytes — the S4/S7 harnesses),
mutation tests for the verifier unchanged. Effort: medium; risk concentrated in the verification
argument, not the code.

---

### D-perf-04 — Transient chains pay for full provenance DAGs that no consumer reads

- Severity: **medium**. Category: performance.

**Evidence (code).** Every `canon_bsgs_build` — including the per-step rebases of D-perf-01/02, the
enumerator's rebases, `canon_symmetry_stabiliser`'s rebase (`symmetry.c:128`) and the §9.4 writers'
descents — records straight-line provenance: `canon_prov_input` per input (`chain.c:622`),
`canon_prov_product` per Schreier-tree edge per non-identity candidate (`chain.c:499-517`,
`prov_times_recorded` `chain.c:372-385`), `canon_prov_inverse` per insertion (`chain.c:430`). These
chains are explicitly "transient, not verified" (S3 review item 2; `chain.h:137-144`); the verifier —
the only reader that re-derives derivations — never runs on them. Per measured tuple_min call above:
15 100 candidates and 279 insertions, i.e. tens of thousands of provenance node allocations (through
the ledger allocator) per G-stage sweep, freed immediately after. Even after D-perf-01/02, every trie
node build pays this unless it is made switchable.

**Recommendation.** Add a build flag `provenance: off` used by all transient builds (rebase with
`verify=false`, descents, trie nodes); keep it on for chains that will be verified or emitted (group
creation, complete stabilisers, rule-2 `K` if it stays verified-per-insertion). The `insert` path
already separates the permutation bookkeeping from `node` threading; make `node` a no-op token when off.

**Expected gain.** Removes one ledger allocation (plus DAG growth/memcpy) per Schreier candidate factor
in transient builds; from the candidate counts above this is plausibly 20–40 % of a transient build's
wall time (estimate — not isolated by measurement; the snippet counters bound the allocation *count*,
74 187 compositions ↔ ≥ 10⁴ prov operations per tuple_min).

**P1/counts.** No semantic effect (provenance is not read by construction/sift/orbit code paths; it is
write-only during builds). `canon_bsgs_bytes` for admission changes (smaller) — the §11.1 plan must be
charged for what is actually built; since plans are computed from the same code's formulas, keep
`canon_bsgs_bytes` honest per flag or the S8 "peak ≤ plan" tests will catch a mismatch in the safe
direction only if over-charged. Flag this in the slice.

**Remediation plan.** `chain.c`: thread a `bool prov` through `build_run/insert/closure`;
`canon_bsgs_rebase(..., verify=false)` ⇒ prov off; audit every `canon_bsgs_build` caller. Tests:
existing chain/verifier suites unchanged for prov-on chains; a differential test that prov-off chains
have identical levels/generators/order. Effort: small.

---

### D-perf-05 — Sift and transporter walks materialise dense permutations per Schreier-tree edge when only point images are needed

- Severity: **medium**. Category: performance.

**Evidence (code).** `times_inverse_path` (`chain.c:273-287`) applies a full dense permutation
(`g[v] = inv[g[v]]` for all v) per edge of a Schreier path; `transporter_into` likewise composes dense
arrays per edge (`chain.c:230-245`, as read). A sift therefore costs `O(n · Σ path lengths)` entry
operations where the *decision* at each level needs only the single point image `g[b_j]`
(`chain.c:298`). The bounds appendix anticipates this exactly: "Dense sifting that rewrites a residue
at every level can incur Θ(nb) entry work per sift. Sifting using only required base-point images,
words and cached actions can be cheaper, so the dense rewrite is not a general BSGS lower bound"
(`performance-lower-bounds.md` §6.3). In the measured runs the *closure candidates* dominate counts
(74 187 dense compositions per tuple_min), and every composition is charged `O(n)` with a dependent
gather — this is the inner constant of D-perf-01/02/03, and it remains after them (trie node builds,
final chain builds, stabiliser consumer sifts: one sift per enumeration hit,
`objectives.c:198`).

**Recommendation.** Two-tier sift: (1) *point sift* — determine the sequence of (level, orbit position)
steps by chasing only base-point images through the *word* (each step needs the image of one point
under the accumulated transporter inverse: maintain the images of the ≤ b base points, `O(b)` point
queries per edge instead of `O(n)`); (2) materialise the dense residue once, by composing the recorded
word — or never, for pure membership (`canon_bsgs_contains_scratch` only needs "residue = id", which
the word + a final `O(n · word)`… no: membership needs the full residue checked against identity, but
that is one dense composition chain of the recorded steps applied to `g` once, not per level per edge
with intermediate full writes; better: apply the step permutations to `g` in walk order exactly as now
but *skip levels whose step is the identity and fuse consecutive steps* — the real saving is in
*closure candidates*, where most residues are identity: a point-sift that stops at the first level
whose residue is already determined… honesty check: identity of the residue cannot be decided from
base-point images alone. The correct cheap form, standard in BSGS practice (labelled **lead** here;
the repo docs cite no sift-optimisation source I found): first sift the *images of the base points
only* to obtain the step record; then evaluate the residue as a straight-line word applied to points
on demand, materialising densely only when a consumer needs the array (insertion does; "is identity"
needs all points but can early-exit on the first moved point — most non-identity residues move an
early point, while identity residues — the majority in closure — still cost a full check; there the
saving is replacing `path_length` dense rewrites by `path_length` point chases per level for the step
search plus one fused application).

**Expected gain.** Constant-factor on all chain work: step search drops from `O(n)` to `O(b)` entry ops
per tree edge (b ≤ depth ≤ n, typically ≪ n for the T3 families; measured depths 19–24 at n = 32 —
modest there, large when n ≫ depth, e.g. sparse-support groups at n = 10³+). Rank below D-perf-01..04
because at n ≤ 48 the rebuild *count*, not the per-sift constant, is the binding term.

**P1/counts.** Pure kernel change; identical residues, identical insertion decisions (the deterministic
builder's policies key on residues and stop levels, both preserved). No ID changes. Careful: the
recorded sift steps feed provenance (`sift_record`) — keep the record shape.

**Remediation plan.** Implement `sift_record` variant returning the step record from point chasing,
then a fused "apply record to array" used where the dense residue is required; keep the current dense
path as the reference in tests (differential sift tests on random chains/words). Effort: medium.
Also cheap and immediate: `canon_bsgs_contains` allocates scratch per call (`chain.c:345-357`) — route
hot callers (implicit-automorphism check, `p1_tree.c:158`) to the existing scratch variant.

---

### D-perf-06 — A_S (prefix stabiliser for orbit pruning) is a full rebase, recomputed on a shared transient that child visits keep invalidating

- Severity: **medium**. Category: performance.

**Evidence (code).** Under the default work policy `0x0003`, every node decision needs
`A_S = (A_known)_(a_1..a_d)`. It is obtained by `canon_symmetry_stabiliser` = a full
`canon_bsgs_rebase` of `A_known` onto the prefix (`symmetry.c:112-135`) — a complete closure build.
There is **one** shared transient (`s->stab`); a child visit rebases it to the child's longer prefix,
so on return the parent recomputes its own `A_S` before its next decision even if `A_known` did not
grow (`src/search/p1_tree.c:301-315`, the `stab_depth != depth` condition and its comment). Pattern
per internal node: up to one full `A_known` rebuild **per explored child**, plus one per
`A_known` growth (epoch change). When `A_known` is large (symmetric objects, where pruning matters
most), each such rebase is a chain build over its strong generators — the same cost class as D-perf-01's
rebuilds, paid inside the pruned search that is supposed to be the fast path. Note the count semantics
are *defined* by this recomputation discipline (`docs/pruning-rules.md` rule statement and the
19-October review note), so any optimisation must reproduce the same `A_S` at each decision — a
*cache* does (same group, same orbits, same decisions), a stale subgroup does not.

**Recommendation.** Cache `A_S` per (epoch, prefix) in a per-depth slot stack (the search already keeps
per-depth snapshot/decision stacks, `p1_tree.c:107-142, 282-289`): one transient chain per depth,
invalidated only by epoch growth, not by child visits. With D-perf-01's trie, `A_S` is the trie node
of `A_known` at the prefix — but `A_known` *changes* (epoch), so the trie must be rebuilt/extended on
insertion; the per-depth epoch-stamped slot is the simpler correct form. Orbit BFS itself
(`symmetry.c:162-230`) is `O(|orbit| · s)` point queries and fine.

**Expected gain.** Removes up to (branching − 1) full `A_known` rebases per node under `0x0003`;
on symmetry-rich cases (where pruned node counts are tiny but `A_known` is big) this can dominate the
whole solve — e.g. the `Sym(6)` empty-subset showcase (6 nodes) still pays root-to-leaf rebases of
`A_known = Sym(6)` per level transition. Unmeasured in isolation (the snippets did not drive the
search); bounded above by "one `A_known` build per explored edge" today vs "one per (depth, epoch)".

**P1/counts.** Decisions identical by the argument in `p1_tree.c:301-306` (the recomputation exists to
make the *same* `A_S` available); counts under `0x0002/0x0003` preserved. The explicit transporter
recorded per pruned child is read off the Schreier tree of `A_S`'s chain — a cached chain keeps the
same tree within an epoch, so certificate bytes are also preserved if the cache returns the identical
chain object the uncached code would have built at that decision (it does: same builder, same inputs;
today's code would rebuild the identical chain because the builder is deterministic given
`A_known`'s generator list and the prefix).

**Remediation plan.** `src/symmetry/`: replace single `stab` with a grow-only per-depth array of
(chain, epoch) slots; `canon_symmetry_stabiliser` consults depth slot. Memory: d transient chains of
`A_known` — charge in the admission plan (S8 formulas count one; update). Tests: node counts under
`0x0002/0x0003` on existing prune tiers identical; rebase counter (`stats.rebases`) drops. Effort: small.

---

### D-perf-07 — Partition refinement does comparison sorts and full re-indexing per split, and computes signatures for vertices that cannot split

- Severity: **medium** (rises to high for graph roots at large n). Category: performance.

**Evidence (code).** `canon_partition_split` (`src/partition/partition.c:101-125`) stable-sorts
*every* cell's `(key, member)` pairs (`O(m log m)` per cell) and then `rebuild_index` rewrites `pos`
and `cell_of` for all n points — on every split, including no-op splits (the G stage calls split
unconditionally, `p1.c:352`; the O stage for non-graphs returns before, `p1.c:267-269`, but graph
stage O always splits). `canon_partition_individualise` and `restore` also `rebuild_index`
(`partition.c:142, 171`): each tree edge costs `O(n)` index writes plus snapshot save/restore copies
`O(n)` (`partition.c:154-172`). Spec §10 prescribes the data layout (flat `lab/pos/cell_of`) but
explicitly permits the faster realisations: "Signature sorting may use fixed-width radix or comparison
sort" and the split output is a function of keys only. All current keys are small integers: initial
keys ∈ {0,1} or colour ranks, G-stage keys are orbit ranks `< n`, O-stage keys are signature ranks
`< n` after `p1.c:284-296` ranking. A counting/radix split is `O(n + maxkey)` per split with the
timestamps the code already uses elsewhere.

For graph stage O (`p1.c:200-299`): every sweep rebuilds every vertex's sparse signature by walking
all incident arcs and **sorting each vertex's entry list** (`p1.c:235`), then sorts all n vertices by
signature with comparisons that walk both sparse lists (`p1.c:256-263, 289`). Total per sweep
`O(Σ_v deg(v) log deg(v) + n log n · sigcmp)`. But a vertex in a **singleton cell** never needs its
signature computed or ranked: `split` replaces a singleton cell by itself regardless of key. Late in
refinement (most sweeps of most nodes) most cells are singletons, yet all signatures are rebuilt.
Similarly, vertices whose incident arcs touch only cells unchanged since the last sweep have unchanged
signatures *relative to the snapshot* — but the snapshot's cell *indices* `j` enter the signature
index arithmetic (`p1.c:221, 229`), so index renumbering after a split invalidates naive reuse; the
safe incremental form is: recompute signatures only for vertices in non-singleton cells, and rank
only those (singletons get a dummy key). That preserves `split`'s output bit-for-bit because ranks
are only ever compared **within** a cell (`split` sorts per cell; global rank consistency is needed
only among vertices that share a cell — non-singleton members get true global ranks among themselves,
which induces the same per-cell order as ranking over all vertices, since adding other vertices
(singletons) to the ranking pool does not change the relative order of the ranked subset). Stage
tokens record only cell sizes: preserved.

**Expected gain.** Partition ops: log factor removal per split (`O(n log n)` → `O(n)`), and elimination
of `rebuild_index` when the split changed nothing (detect via the existing `bool` return — currently
ignored at both call sites, `p1.c:297, 352` cast to void). Graph O stage: per-sweep work proportional
to arcs incident to non-singleton cells' members instead of all arcs — the standard refinement
profile; on deep nodes this is the difference between `O(m)` and `O(m_sub)` per sweep. These are the
dominant terms *after* D-perf-01 for graph roots (for subset roots stage O is empty and D-perf-01 is
nearly everything).

**P1/counts.** Preserved: same splits, same traces, same counts; only the *computation* of the same
pinned function changes (spec §10 license; R1 in §14.3 states the same principle). The one care point
is within-cell member order after a radix split: "Internal order of members within a cell is not
semantic" (§7.1) **but** the *child visitation order* derives from it, which under policy `0x0003`
affects which leaves are met first, hence implicit-automorphism discovery order, hence **explored
node counts** (`docs/pruning-rules.md`: "under 0x0003 the fixed sequential order also fixes which
leaves are met first"). A stable radix split (stable counting sort by key within the cell, scanning
members in current order) reproduces today's stable-sort member order exactly. Keep it stable, or
`0x0003` counts legitimately change and need a new ID. Flag this as the slice's main correctness trap.

**Remediation plan.** `partition.c`: stable counting-sort split when `maxkey ≤ 2n` (all current uses),
fallback to comparison sort otherwise; skip `rebuild_index` if no cell boundary changed and `lab`
untouched (radix split can detect identity permutation per cell); `p1.c` stage O: compute signatures
and ranks only over non-singleton members. Tests: golden traces (they pin member-order-independent
sizes), plus *node-count identity* under all three policies on prune tiers (this catches the stability
trap), plus the existing dense-vs-sparse signature test (`tests/c/test_signature.c` per `p1.c:155`).
Effort: small–medium.

---

### D-perf-08 — §9.4 writers: rule 2 alternates full descents with full verified rebuilds, and re-tests subgroup containment from scratch per point

- Severity: **medium**. Category: performance.

**Evidence (code).** `rule2` (`src/encoding/group_stream.c:130-173`): per greedy generator
(`≤ log2|H|` of them) one `canon_coset_least_outside` descent plus one `canon_bsgs_insert_verified`
(full build + verify, D-perf-03). Inside `outside_descend` (`src/coset/least.c:344-391`),
`level_in(cur, lv, k)` — sift **every** strong generator of the current level through `K` — is
evaluated at the top of the loop for *every* point `v` (`least.c:351`), although `lv` only changes
when a point is actually fixed; for runs of fixed points (`len == 1`, `least.c:364-366`) the same
containment is re-sifted repeatedly. `level_in(cur, lv+1, k)` is likewise recomputed per branching
point (`least.c:375`) with no memoisation across the (up to `n`) sibling descents of one solve. The
descents themselves inherit D-perf-02's rebuilds (18 per top-level descent measured; `least_outside`
may then invoke a nested `descend`, `least.c:358`). HANDOFF decision 8's "transient chains in the
§9.4 writers" is this item plus D-perf-04.

**Recommendation.** Cache `level_in` results keyed by (chain identity, level, K epoch) within a solve;
evaluate containment once per *level change*, not per point; share the D-perf-01/02 trie so the
descents stop rebuilding; apply D-perf-03's incremental insertion to `K`. The greedy sequence itself
is canonical (§9.4) and untouched: same `g_i`, same bytes.

**Expected gain.** Group/coset byte emission is `O(log|H| · (descent + build + verify))` today;
expect the same 10–50× class of reduction as D-perf-02/03 on this path. It matters beyond `STABILISER`
objectives because subgroup/coset **leaves of nested objects** are re-encoded per image
(IN PROGRESS: exact frequency in `dag.c` unchecked).

**P1/counts.** Bytes preserved by construction (the greedy sequence depends only on the subgroups as
sets; containment is a predicate). No counts involved (writers are outside the quota traversals —
confirm: descents inside objectives *are* charged in stats but the quota counts visits/nodes, not
rebuilds; spec §11.1 — preserved).

**Remediation plan.** Rides on D-perf-01..04; the only writer-local changes are the `level_in` cache
and hoisting it out of the per-point loop. Tests: golden Group payloads (§7.4) and existing
group-stream suites byte-identical; `stats->k_builds`/rebuild counters drop. Effort: small after
D-perf-01..03.

---

### D-perf-09 — No parallelism anywhere: `solve_batch` is sequential and the §14 pool does not exist

- Severity: **medium** (high for wall-clock on batch workloads, but last by gain/effort and risk).
  Category: performance.

**Evidence (code/docs).** `src/scheduler/` is a README only (per `HANDOFF.md` layout table); spec §14's
coordinator protocol, coverage state machine and R1–R3 are unimplemented; `solve_batch` runs
sequentially under `CALLER_THREADS` semantics (spec §17 statement of current behaviour; S8 decision 5:
"batch cancellation and pool execution are not offered"). The bounds appendix's small-instance batch
regime (§6.7) and §12.3 both assume caller-owned parallel workspaces as the throughput path for the
tensor-style workload; today the only way to use 16 cores is caller-side threading around the
single-threaded API (which the immutable-context design permits, but nothing in the public surface
helps or measures it).

**Recommendation.** Sequence it **after** D-perf-01..03 (parallelising a 10³×-wasteful per-node cost
buys 16× of the wrong thing, and the trie's sharing design must be settled first: per-workspace tries
duplicate build work across workers; a shared immutable trie published incrementally needs the §14.2
publication discipline). Then: (i) batch-level parallelism first (independent problems, §14.1 "Batch
parallelism is the default small-instance policy") — no coverage machine needed, counts are per-solve
by construction; (ii) intra-search parallelism only with the full §14.2 protocol and the sequential
count-reproduction rule for `0x0003` (`docs/pruning-rules.md`: "Parallel modes must reproduce the
sequential count") — this is M6-sized and should not be smuggled into a performance slice.

**Expected gain.** (i) near-linear in physical cores for batch throughput until DRAM/LLC limits
(appendix §6.7 model); (ii) instance-dependent, bounded by span (appendix §5).

**P1/counts.** (i) preserves everything trivially. (ii) preserves bytes under R1–R3; counts only under
the stated reproduction rule.

**Remediation plan.** M5/M6 slices as already sequenced in `docs/implementation-plan.md`; record as a
finding here only so the ranking is explicit that it is *not* the next thing to do. Effort: large.

---

### D-perf-10 — Minor constant-factor items (each small, all count/byte-preserving)

- Severity: **low**. Category: performance.

1. `canon_bsgs_tuple_min` allocates five fresh n-word arrays per call through the ledger allocator
   (`chain.c:1042-1046`) — at one-per-sweep frequency this is allocator traffic on the hottest path;
   move to workspace scratch (as `canon_coset_least` already does, `least.c:27-28`). Subsumed by
   D-perf-01's signature change but trivially doable alone.
2. `canon_bsgs_contains` allocates per call (`chain.c:350`); hot caller: implicit-automorphism check
   per equal-key leaf (`p1_tree.c:158`). Use `canon_bsgs_contains_scratch` with search scratch.
3. The explicit backend's `tuple_min` is an `O(|G| · len)` table scan per call (`explicit.c:183-200`)
   — acceptable for an oracle, but it is also reachable as a production backend choice; document or
   leave (it is labelled oracle in `HANDOFF.md` decision 1). No action recommended beyond noting that
   any "explicit is faster for tiny groups" intuition is false for the G stage at `|G|` beyond a few
   hundred, since it pays `|G|` per sweep where the chain pays (after D-perf-01) `O(|F| · word)`.
4. Trace handling: each node appends tokens to a per-search buffer and truncates on exit
   (`p1_tree.c:367, 392, 476`) — already amortised; leaf comparison is a full memcmp of the trace
   (`p1_tree.c:197-198`) even when the incumbent shares a long prefix with the current path. A
   depth-indexed "first differing token vs incumbent" cache could shave this, but trace length is
   `O(nodes-on-path · cells)` words and dwarfed by D-perf-01 today. Revisit after D-perf-01 using the
   metrics (`p1_images`) rather than now.
5. `root_automorphisms` in non-insert mode still materialises `x^g` per generator just to count
   fixing generators (`p1_tree.c:494-500`) — required for the §7.3 singleton-orbit frequency record;
   for subset roots this is a bitset image per generator and cheap; for DAG roots it is a full
   re-normalisation per generator (IN PROGRESS: dag cost). Consider whether the frequency record is
   needed on the unpruned path at all when no certificate is requested — spec §7.3 says "Record
   root-discrete and singleton-orbit frequencies separately", which is a metrics obligation, so any
   change must keep the counter; cheaper equality via action-on-the-fly comparison instead of
   materialisation is the fix if profiles later indict it.

---

## 3. Ranking by expected gain / effort

| Rank | Item | Headline gain | Effort | New ID / lemma? |
|---|---|---|---|---|
| 1 | D-perf-01 fast paths first (F=∅ / suffix reuse; drop least-minimiser tail at P1 sites) | up to ~10³× on root-ish sweeps (1774 µs → 2.1 µs measured for F=∅) | small | no |
| 2 | D-perf-01 stabiliser trie (§16 cache) shared by refine/search | the remaining 10²–10³× to ref-b parity on quota-bound cases | medium | no |
| 3 | D-perf-04 provenance off for transient builds | est. 20–40 % of every remaining build | small | no |
| 4 | D-perf-06 per-depth A_S cache | removes ≤ (branching−1) A_known rebuilds/node under 0x0003 | small | no |
| 5 | D-perf-02(a) descent/enumerator on the trie; lazy leaf-key comparison | 10–100× on enumeration-internal work | medium | no |
| 6 | D-perf-03 incremental verified insertion | removes log\|A\|×(build+verify) multiplier | medium | no (verification argument must be written) |
| 7 | D-perf-08 writer containment cache (+ rides on 2/5/6) | 10–50× on Group/coset emission | small | no |
| 8 | D-perf-07 radix split + singleton-skip signatures | log-factor and m→m_sub per sweep (graphs) | small–medium | no, **iff** split stays stable |
| 9 | D-perf-05 point-sift kernel | constant factor, grows with n/depth | medium | no |
| 10 | D-perf-02(b) region prunes for enumeration objectives | potentially largest on symmetric cases | medium + policy work | **yes**: new work-policy ID(s), coverage lemmas, future checker rule |
| 11 | D-perf-09 batch parallelism, then §14 pool | ≤ #cores throughput | large | counts preserved only under §14 discipline |

Explicitly **not** recommended: stronger refiners / different target-cell heuristics / node-invariant
pruning beyond §7.3's listed forms — these change the tree and hence traces; they are new profiles
(spec §15), not P1 optimisations. Likewise SIMD/AVX kernels (§13) before the algorithmic terms above:
the appendix's kernel floors (§6.4) are irrelevant while the per-node work is dominated by redundant
chain closures; revisit under M8 with profiles.

## 4. Spec ambiguities (quoted; not resolved here)

1. §11.1 quota boundary/unit: "*Exceeding that policy yields `CAPACITY_LIMIT`*" with the quota
   counting "*P1 `NODE` tokens plus §8.1 enumerator visits; the §7.3 root shortcuts are part of
   profile P1's policy and visit no tree nodes*". Whether a discrete root's `NODE(0)` token counts,
   whether "visit" includes internal §8.1 regions, and whether equality with the quota completes are
   exactly ref-b's Q1–Q3 (`refs/ref-b/QUESTIONS.md`). Performance relevance: none of the readings
   changes any proposal above, but T3's `CAPACITY_LIMIT` cases are the first that can distinguish
   them, so D-perf-01's acceptance test ("answers CAPACITY_LIMIT in seconds") must not be read as
   settling the boundary.
2. §7.1 "*choose any u in G with F^u=M*" vs the internal `canon_group_ops.tuple_min` contract "the
   least such t as an image array" (`src/bsgs/group.h:40`): not a spec ambiguity — the spec is clear
   and the internal contract is stricter than needed (D-perf-01). Recorded here because the G-stage
   `u` enters certificates; whether `canon-check` re-derives minimality of `u` or merely `F^u = M`
   is IN PROGRESS (checker rule not re-read in this lane yet).
3. §9.2 "*base change rebuilds/certifies for the requested ordered base (reuse is optional)*" — the
   parenthetical is the license for D-perf-01/02/06's caches; no ambiguity, noted so the proposals are
   not mistaken for spec deviations.

## 5. Things checked and found sound (performance-wise)

- The leaf shortcut (skip image/stream when the trace already loses, `p1_tree.c:195-202`) is correctly
  placed and is *not* a prune (`docs/pruning-rules.md` §3 agrees); certificate mode correctly disables
  it.
- Scratch discipline in the search/refine/coset layers is grow-only with no per-node allocation
  (`p1.c:81-117`, `p1_tree.c:55-105`, `least.c:63-88`, enumerator node blocks `enumerate.c:59-96`) —
  the allocator hot spots are specifically the chain builds and D-perf-10.1/10.2, not the traversal.
- Graph O-stage signatures are already sparse with an exact elided-zero comparison (`p1.c:136-177`)
  and never materialise the dense `2Lk` vector; the remaining waste is per-sweep recomputation
  (D-perf-07), not representation.
- The closure builder is genuinely incremental *within* one build (`schreier_done` prefix tracking,
  `chain.c:456-531`) — the waste is the number of builds, not their inner algorithm; D-perf-03 reuses
  exactly this machinery.
- Quota checks are at visit/node entry (`p1_tree.c:363`, `enumerate.c:38-50`), so no proposal above
  needs to move a `CAPACITY_LIMIT` boundary to obtain its speedup; the work is *inside* the counted
  units. (This also confirms the C path does terminate on 000077 with `CAPACITY_LIMIT`, eventually.)
- `canon_bsgs_orbit_ids` (union-find by least point, `chain.c:873-904`) is already the right kernel
  for orbit ranking; it is simply unreachable from the G stage without going through `tuple_min`'s
  rebuild walk today.

## 6. IN PROGRESS (to be appended)

- Checker G-stage rule for recorded `u` (does it require the least minimiser?) — affects only how
  D-perf-01 is phased, not its validity.
- `src/object/dag.c`: cost of `canon_dag_act` per enumeration leaf and of subgroup/coset-leaf
  conjugation + Group-byte emission per image (would sharpen D-perf-02/D-perf-08 magnitudes for
  nested roots).
- `src/object/graph.c`: `canon_graph_act_into` per-leaf sort cost for the transporter/stabiliser
  consumers.
- One measured end-to-end point: time `canon-cli` on a synthetic n = 32 subset case under a small
  quota to confirm ms/node through the public path (snippets so far measure the internal kernels).
