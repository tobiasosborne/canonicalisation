# Review lane G-perf-search: search tree, pruning, enumeration objectives, deterministic parallelism

## 1. Scope

Read in full:

- `docs/review-2026-11/BRIEF.md`, `README.md`, `HANDOFF.md`, `CLAUDE.md`.
- Spec (`docs/specification.md` v2.2): §7 (P1, leaf map, §7.3 pruning), §8 (§8.1 enumerator,
  §8.2 consumers, §8.3 bounds, §8.4 signed), §11.1–11.2 (work-policy IDs, quota, live memory),
  §12.1/12.3 (cost ledger, small batches), §13, §14 (modes, coordinator protocol, R1–R3),
  §15–16 (stronger refiners, exact caches), §18 (checkpoints), §20.
- `docs/pruning-rules.md` (rule P1-ORBIT-1, policies `0x0001`–`0x0003`, checker rule 5).
- Code: `src/search/p1_tree.{h,c}`, `src/search/objectives.c`, `src/search/certificate.{h,c}`,
  `src/symmetry/symmetry.{h,c}`, `src/refine/p1.c`, `src/coset/enumerate.c`, `src/coset/least.c`,
  `src/coset/coset.h`, `src/bsgs/chain.c` (`canon_bsgs_tuple_min`, rebases),
  `src/bsgs/chain_backend.c`, the solve dispatch in `src/api/api.c:1085-1264`,
  `src/scheduler/README.md`, `src/checkpoint/README.md` (both placeholders).
- Recorded evidence in repo: `HANDOFF.md` open item 4 and decision 8 under "Decisions taken
  during the slices"; `refs/compare/runs/2026-10-19-m0/README.md` (T3 stall);
  `docs/slices/S7-notes.md`, `S7b-notes.md` (cost notes); `checker/cc_check.c` rule headers.

Not covered: `src/bsgs` internals beyond `tuple_min`/rebase cost, object/encoding hot paths
(lane D-perf), checker soundness in depth (only what a new prune would need from it), the
explicit backend except as the §8.1 oracle, Lean, benchmarks.

No tests, reference runs, or benchmarks were run (per the brief). Every timing cited is a
recorded value in the repository; every other claim is reasoned from the code and stated as such.

**Headline.** The search is slowed less by tree size than by *per-node* cost: every sweep of
every node calls a `tuple_min` that performs up to O(n) full stabiliser-chain rebuilds and five
heap allocations (G-01). That, not the prune policy, is why ref-a stalls ≈17 min on a case
ref-b answers `CAPACITY_LIMIT` in 0.27 s. The enumeration objectives are Θ(|G|) by construction
with an expensive per-element descent (G-02, G-07). One cheap, mathematically free win is being
left on the table: the labeling and signed objectives run their P1 trees unpruned although orbit
pruning provably cannot change their final bytes or deterministic witnesses (G-07a). Parallelism
does not exist yet; §14 plus the §11.1 quota make the `0x0003` policy the hard case, and G-08
gives a schedule-independent reformulation.

Conventions used below: "count change" means the §11.1 quota count of the traversal changes, so
by spec §11.1 a new work-policy ID is required; "P1 bytes" means profile `0x0001` trace and
CDAG-2 bytes (PROFILE-EQUIV). Work-policy IDs in use: `0x0001` unpruned, `0x0002` orbit prune by
input fixing generators, `0x0003` plus implicit automorphisms (`docs/pruning-rules.md` §"Quota").

---

## 2. Findings

### G-01 — Per-node cost: `tuple_min` per sweep, O(n) chain rebuilds and 5 allocations per call

- Severity: critical. Category: performance.

**Evidence** (reasoned from code; timing is recorded, not mine).

1. Every refinement sweep of every node calls the group `tuple_min` once, for F = the singletons
   so far: [p1.c](/home/tobias/Projects/canonicalisation/src/refine/p1.c:344)
   `st = g->ops->tuple_min(g, s->pt.fixed, f, s->pt.u, s->pt.orbit);` inside the loop at
   [p1.c](/home/tobias/Projects/canonicalisation/src/refine/p1.c:310). A node runs at least one
   sweep (spec §7.1, the mandatory no-change sweep) and up to O(n) sweeps.
2. Each `tuple_min` call minimises L followed by all of `0..n-1` ([chain.c](/home/tobias/Projects/canonicalisation/src/bsgs/chain.c:973),
   `tuple_run`), and whenever the orbit minimum `b` is not the current base point it does a full
   `canon_bsgs_rebase` — a Schreier–Sims rebuild of the suffix chain:
   [chain.c](/home/tobias/Projects/canonicalisation/src/bsgs/chain.c:997). The header comment
   concedes "At most O(n) rebuilds per call, the known S3 cost (M5 work)"
   ([chain.c](/home/tobias/Projects/canonicalisation/src/bsgs/chain.c:964)); HANDOFF records it
   as accepted cost 8 ("one chain rebuild per step of `tuple_min`", `HANDOFF.md:94`).
3. Each call also heap-allocates five n-word arrays and frees them
   ([chain.c](/home/tobias/Projects/canonicalisation/src/bsgs/chain.c:1042)), through the ledger
   allocator — even though the workspace already owns grow-only scratch of the same shape
   ([p1.c](/home/tobias/Projects/canonicalisation/src/refine/p1.c:54)). `chain_tuple_min` passes
   `NULL` stats ([chain_backend.c](/home/tobias/Projects/canonicalisation/src/bsgs/chain_backend.c:66)),
   so this dominant cost is invisible in `canon_metrics` (which does expose `coset_rebuilds`,
   [api.c](/home/tobias/Projects/canonicalisation/src/api/api.c:1133)).
4. At a leaf the work is done twice: the last G stage has F = L and its `u` is already the unique
   minimiser `t_L` (S7-notes reading 4 relies on `t = u` in the checker), yet
   `evaluate_leaf` calls `tuple_min` again on the full list
   ([p1_tree.c](/home/tobias/Projects/canonicalisation/src/search/p1_tree.c:205)).
5. Consequence, recorded: `t3-s20261013-000077-canonical_image` (n = 32, subset) had not
   returned after ≈17 min under ref-a while ref-b answers `CAPACITY_LIMIT` in 0.27 s
   (`refs/compare/runs/2026-10-19-m0/README.md:38`, `HANDOFF.md:155`). Both implementations must
   stop at the same 10⁶-node quota; the difference can only be per-node cost. A quota is a node
   bound, not a time bound, so today a `CAPACITY_LIMIT` verdict can cost 10⁶ × O(n) chain
   rebuilds. This is the single largest known performance defect in this lane and it blocks the
   M0 T3 run (HANDOFF open item 4).

**Recommendation.** Attack the G stage, in this order; none of it changes any node count, trace
byte, or policy:

- (a) Reuse leaf `u` as `t_L` (item 4). The equality is already a checker rule; the engine merely
  needs the last sweep's `u` retained per node.
- (b) No allocation in `tuple_min`: take caller scratch (the group op needs a scratch argument
  or a chain-backend entry that receives the workspace arrays), matching the pattern already
  used by `canon_coset_scratch`.
- (c) Replace "rebase per step" with sift-with-orbits on one chain: `tuple_run` needs, per step,
  `min(a^H)` and a transporter in the *current suffix* H, then H ← H_b. That is exactly one
  orbit BFS over the suffix level's strong generators (already implemented as `orbit_min`) plus
  descending to the stabiliser of b. A chain whose base order is the order in which points get
  minimised would make each descent a level step; when the needed base point differs, a
  *single-point* base change (swap/Insert one base point, recomputing only the affected levels'
  orbits and Schreier generators) is O(one level), not a rebuild of the whole suffix from the
  generator table. The current `canon_bsgs_rebase` always rebuilds from generators
  ([symmetry.c](/home/tobias/Projects/canonicalisation/src/symmetry/symmetry.c:128) uses it the
  same way, see G-05). Budget: per sweep O(depth × orbit BFS), depth ≤ number of base points,
  instead of O(n × rebuild).
- (d) Cache G-stage orbit ids by the exact key spec §16 names: "Normalised group-refiner keys
  include G, ordered fixed tuple F … A hit must produce exactly the same logical stage/trace as
  recomputation" (§16). F values recur heavily across sibling subtrees (singletons in partition
  order). A workspace-local exact cache (full F array as key, hash only as bucket index with
  exact compare — §7.3 "Never use a hash/quotient match as exact state equality") turns repeated
  sweeps into lookups. Eviction changes time only (§16).

**Lemma/policy/checker.** Pure PROFILE-EQUIV engineering: the G stage's *output* (M, orbit ids,
split, trace token) is a pinned function of (G, F) by §7.1; computing it faster needs no coverage
lemma, no new work-policy ID (node count identical), P1 bytes preserved by construction, and no
checker change (the certificate records F, u, M and a chain; the chain the producer embeds can
stay as today — see G-09 for making that cheaper).

**Remediation plan.** Files: `src/bsgs/chain.c` (`tuple_run`, single-point base change),
`src/bsgs/group.h`/`chain_backend.c` (scratch-taking op, real stats), `src/refine/p1.c`
(retain last `u`; optional F-cache in `canon_p1_scratch`), `src/search/p1_tree.c` (leaf reuse).
Tests: byte-identity tiers as today (pruned/unpruned/Python model) are the proof; add a counter
assertion (rebuilds per solve, exposed in metrics) and a regression case at T3 case 77's shape
(n = 32 subset, quota-bound) asserting only the *status* and count, not wall time. Risk: the
base-change code is the subtlest part of the chain; keep the verifier over the *stored* chain
unchanged and treat all of this as transient (as today, S3 review item 2). Effort: (a)+(b) days;
(c) 1–2 weeks with tests; (d) days after (b). Estimated gain: the recorded ref-b figure suggests
one to two orders of magnitude on quota-bound cases; labelled an estimate, not a measurement.


### G-02 — Enumeration objectives: Θ(|G|) leaves, each reached by repeated coset descents

- Severity: critical (for those objectives). Category: performance.

**Evidence** (reasoned). Why `STABILISER`, `TRANSPORTER_COSET`, `LEX_MIN_IMAGE` and the
deterministic/labeling/signed routes are slow already at n ≤ 48:

1. The only traversal is spec §8.1, "with zero pruning": "Singleton leaves partition G with |G|
   leaves" (§8.1). §8.2 requires exhaustion for stabiliser, transporter-coset (after the hit) and
   minimum; negative transporter likewise. At n = 48 almost every interesting G has
   |G| ≫ 10⁶ quota, so most large cases can only ever end in `CAPACITY_LIMIT` — the question is
   only how much each of the first 10⁶ visits costs.
2. Each visit is expensive. In [enumerate.c](/home/tobias/Projects/canonicalisation/src/coset/enumerate.c:98)
   a node rebases H to its least moved point if needed (line 127), then *for every child b*
   computes `t_b` by a full constrained least-element descent
   ([enumerate.c](/home/tobias/Projects/canonicalisation/src/coset/enumerate.c:141) →
   `canon_coset_least`). That descent fixes points 0..n−1
   ([least.c](/home/tobias/Projects/canonicalisation/src/coset/least.c:235)), and each step that
   moves a non-base point rebases again ([least.c](/home/tobias/Projects/canonicalisation/src/coset/least.c:144)).
   So per §8.1 edge: O(n) descent steps with up to O(depth) rebuilds; per leaf this multiplies
   by the path length. `t_b` could be read off the *already computed* Schreier tree of the
   rebased level's orbit at O(n · word length) — any transporter suffices for coverage (§8.1:
   "Any transporter t_b suffices for coverage; the least choice freezes reference traversal").
   The least choice is currently bought at descent price on every edge, even in runs where no
   certificate and no budget-boundary case can observe which transporter was used — but the
   *visit order* of leaves (hence first-hit witnesses in ANY mode and which partial tree is
   explored before quota) does depend on it, so this cannot be silently changed under the
   existing counted traversal; it needs either proof the count is unchanged (it is: the tree
   shape of §8.1 depends only on the orbit partition, not on the transversal representatives —
   the children are indexed by b ∈ a^H regardless of t_b; only the element labelling of leaves
   changes) plus acceptance that ANY-mode first hits may change, or a new work-policy ID.
3. Each leaf consumer pays full object work with no pre-filter: stabiliser/transporter do a full
   `canon_root_act_into` plus full equality per element
   ([objectives.c](/home/tobias/Projects/canonicalisation/src/search/objectives.c:138),
   [objectives.c](/home/tobias/Projects/canonicalisation/src/search/objectives.c:165));
   `LEX_MIN_IMAGE` additionally writes the full CDAG-2 stream (or SIMPLE-UPPER-1 key) for
   *every* element before comparing
   ([objectives.c](/home/tobias/Projects/canonicalisation/src/search/objectives.c:97)). No
   invariant (colour class sizes, degree sequences, P1 cell sizes) is ever used to reject a
   whole coset region Hr without visiting its elements.
4. Compounding: deterministic `TRANSPORTER_ONE` enumerates to a hit, then enumerates *all* of G
   again for the complete stabiliser ([objectives.c](/home/tobias/Projects/canonicalisation/src/search/objectives.c:318));
   deterministic `CANONICAL_IMAGE` runs the P1 tree, then a full stabiliser enumeration for the
   witness ([api.c](/home/tobias/Projects/canonicalisation/src/api/api.c:1183) →
   `canon_obj_deterministic_witness`); labeling and signed-nonzero do P1 (unpruned, G-07a) plus
   the same full enumeration. HANDOFF records the result: "the unpruned enumeration objectives
   under that quota take minutes per case on n ≤ 48" (`HANDOFF.md:111`).

**Recommendation.** Two stages. Stage 1 (same §8.1 tree, cheaper edges/leaves): Schreier-tree
transporters instead of per-edge descents where the count-invariance argument above is accepted
and recorded; a streaming key comparison for `LEX_MIN_IMAGE` (G-06 applies verbatim: compare the
stream against the incumbent while generating, abort on first greater byte, materialise only on
a win); invariant pre-filters per element only (no coverage change). Stage 2 replaces the
traversal for stabiliser/transporter by partition backtrack (G-07), which is the real fix.

**Lemma/policy/checker.** Stage 1 transporter change: tree shape and leaf *set* are unchanged
(the disjoint-union identity `H r = ⨆_b H_a t_b r` holds for any t_b with a^{t_b}=b, §8.1), so the
quota count is unchanged and P1 is not involved (profile NO_TREE). What changes is leaf *order*
within the fixed tree indexing? No — children are still iterated by sorted b; the element at a
leaf is the product of the edge transporters, so leaf elements are permuted among leaf
positions. First-hit ANY witnesses and stabiliser hit *order* (hence the sequence of verified
rebuilds, not the final group) can change. Certificates do not exist for these objectives (v0 is
canonical image only), so no checker change; but because ref-a/M0 records compare ANY witnesses
only where the schema asks for deterministic ones, the maintainers must decide whether this is a
realisation change under the same policy (like D2) or a new ID. My recommendation: treat as the
same count, new *traversal-order* note in `docs/pruning-rules.md`-equivalent for §8; flag as a
maintainer decision, do not silently ship.

**Remediation plan.** `src/coset/enumerate.c` (transporters from `canon_bsgs_transporter_scratch`
on the rebased level, already used in `least.c:208`), `src/search/objectives.c` (streaming
compare), metrics (`coset.descents` should fall to ~descent-free per edge). Tests: enumerator
visits exactly G once each (existing tiers), byte identity of Group/coset payloads and
deterministic witnesses on every tier; ANY-witness validity via `verify_witness` rather than
equality. Effort: days. Gain: removes the O(n)-descents factor from every enumeration edge;
does not change the Θ(|G|) leaf count — hence G-07.

### G-03 — Trace-prefix domination (§7.3) is not implemented; only a leaf-level shortcut exists

- Severity: high. Category: performance.

**Evidence.** `docs/pruning-rules.md` §3: "Trace-prefix domination, bound pruning and exact state
equality with transport (§7.3) are not implemented." The only trace use is at leaves: a leaf
whose complete trace exceeds the incumbent's skips `t_L`, image and stream
([p1_tree.c](/home/tobias/Projects/canonicalisation/src/search/p1_tree.c:196)) — but the whole
subtree above it (all its NODE visits and refinements) has already been paid. In a tree whose
incumbent is found early (DFS explores the first child first), entire later subtrees frequently
have prefixes that already lose.

**The sound rule and its lemma (sketch).** At any node S, the bytes `trace[0..len)` accumulated
on the root path are a *prefix of the complete trace of every descendant leaf* — tokens are only
appended ([p1.c](/home/tobias/Projects/canonicalisation/src/refine/p1.c:306),
[p1_tree.c](/home/tobias/Projects/canonicalisation/src/search/p1_tree.c:388); §7.2 "Concatenate
tokens from root to leaf"). Let B be the incumbent's complete trace and P the node's prefix.
If P and B first differ at a position i < min(|P|,|B|) with P[i] > B[i], then every extension
P‖σ satisfies P‖σ > B in §4.3 byte order, so every descendant leaf key exceeds the incumbent key
in its first component and none can attain or beat the minimum. If P is a proper prefix of B, no
conclusion: the next byte may be `00` (LEAF) < B[|P|] or `10`/`20`/`21` > — exactly §7.3's
warnings: "An equal shorter prefix is not worse; compare full framed tokens and preserve the
possibility of a smaller next byte." Pruning on the first-difference condition alone therefore
removes only leaves with *strictly greater* trace. Two consequences worth recording: (i) unlike
orbit pruning, this preserves not only the minimum key but the *set of attaining leaves*, hence
also the least attaining `t` (the ANY witness) and every objective whose answer is derived from
the attaining leaf (labeling `t`, signed `t`) — coverage is "finalised trace-prefix domination"
in §7.3's own list; (ii) it is incumbent-dependent, so the explored set depends on DFS/cell order
exactly as `0x0003` already does (`docs/pruning-rules.md`, Quota).

**Policy/P1/checker.** Node count **changes** ⇒ **new work-policy ID** (suggest `0x0004` =
`0x0003` + trace domination, keeping the IDs cumulative). P1 trace/bytes/witness preserved.
Checker: this is the expensive part. CERT-0 has no way to record "this subtree was cut because
its prefix lost to the incumbent *at that moment*"; the incumbent is order-dependent state the
checker would have to track by replaying leaves in recorded order (it already replays the tree
in DFS order and recomputes each leaf key, so it *can* maintain the running minimum) and a new
CHILD kind `PRUNED_TRACE` carrying no automorphism, verified by: recompute the child's would-be
prefix — but the child node's refinement tokens are exactly what was not explored. The cut can
only be made *after* the node's own NODE/STAGE tokens are appended, i.e. the producer must still
refine the cut node (recording its sweeps) and then not branch. That preserves most of the win
(the subtree below is cut) and gives the checker the full prefix to compare against its running
incumbent trace. This needs a CERT-0 extension or CERT-1: node kind "branch suppressed by trace
domination" + checker rule (prefix > running best trace at first difference; running best is
over *explored* leaves seen so far in the record). Effort is mostly here, not in the search.

**Remediation plan.** Search: in `visit()` after `canon_p1_refine_node`, before branching,
compare `s->trace` with `s->best_trace` under the first-difference rule (careful: at a leaf the
LEAF byte has not been appended yet — cut only non-leaf nodes, or append-then-decide with the
proper-prefix case excluded). Files: `src/search/p1_tree.c`, `include/canon/canon.h` (new ID),
`docs/pruning-rules.md` (lemma above), `src/search/certificate.{h,c}` + `checker/cc_check.c`
(new record/rule), metrics (nodes cut by trace). Tests: byte/witness identity vs `0x0001` on all
tiers *including ANY witnesses* (stronger than orbit prune tests), node-count regressions pinned
per policy, checker mutation (a trace-pruned node whose prefix does not dominate must be
rejected). Risk: medium — the proper-prefix trap is the whole lemma; a unit test with an
incumbent whose trace has the node's as proper prefix and a winning LEAF descendant is the
regression that matters. Effort: ~1 week including checker. Gain: conjecture — largest on
asymmetric instances where the first leaf is already optimal or near it; zero on highly
symmetric ones (orbit prune's home ground). Complementary, not competing.

### G-04 — Automorphism discovery uses only two of the available sources

- Severity: high. Category: performance.

**Evidence.** `A_known` is fed by (i) input generators fixing x
([p1_tree.c](/home/tobias/Projects/canonicalisation/src/search/p1_tree.c:486)) and (ii) implicit
automorphisms `t_L t_best⁻¹` only when a leaf's key *equals the incumbent's*
([p1_tree.c](/home/tobias/Projects/canonicalisation/src/search/p1_tree.c:230): the call sits
inside `c == 0` for trace *and* bytes vs best). Everything else the search learns is discarded:

1. Two leaves with equal *bytes* but different traces give an automorphism (`x^p = x^q ⇒ pq⁻¹`
   fixes x, spec §3) regardless of trace; source (ii) misses all such pairs not involving the
   incumbent. Leaves that lose on bytes to the incumbent but tie an earlier non-incumbent leaf
   are the common case in symmetric trees.
2. Leaves with trace > best never materialise their image (line 199), so no pairing is even
   attempted there (rightly for speed — see trade-off below).
3. No use is made of automorphism *search* as a separate computation (partition backtrack for
   Aut_G(x), G-07, or randomised Schreier–Sims-style sampling of G elements tested against x),
   which §14.3 R2 and §8.4 both contemplate ("Randomised discovery may use this rule").
4. Minor waste inside source (ii): every candidate pays a full G-membership test plus a full
   action check ([p1_tree.c](/home/tobias/Projects/canonicalisation/src/search/p1_tree.c:158),
   `canon_symmetry_insert`), even when the candidate is already in `A_known`. The objectives'
   signed consumer already demonstrates the cheaper order — sift in `A_known` first, skip if
   member ([objectives.c](/home/tobias/Projects/canonicalisation/src/search/objectives.c:198)) —
   the search path does not. Candidates that enlarge `A_known` are ≤ log₂|A| (each at least
   doubles it); candidates that do not can be numerous (every best-tying leaf).

**Recommendation, with lemma.** The orbit lemma (`docs/pruning-rules.md` §2) needs only a
*verified* subgroup `A_known ≤ A`; provenance is irrelevant to soundness, and insertion already
verifies by action ([symmetry.c](/home/tobias/Projects/canonicalisation/src/symmetry/symmetry.c:86)).
So every source below is sound for CANONICAL_IMAGE under the *same* lemma; they differ in cost
and in *when* they fire (hence counts):

- (a) *Image table for materialised leaves.* Keep an exact map from leaf bytes → first `t`
  attaining them, for every leaf whose image was materialised (trace ≤ incumbent at arrival).
  On a repeat, offer `t_L t_first⁻¹`. Hashing may index buckets; equality is decided on the full
  bytes (§7.3). Cost: one hash + occasional verify per materialised leaf; memory O(distinct
  images × bytes) — must be bounded by the admission plan or capped (a full table stops
  offering candidates; soundness unaffected since `A_known ≤ A` is all the lemma needs, but a
  capacity-dependent table makes the *count* depend on the cap — so the cap must be a function
  of the descriptor, which it is if derived from `max_live_bytes`/quota, and stated in the
  policy definition).
- (b) *Pre-search automorphism computation.* Run the G-07 backtrack stabiliser search (or a
  bounded randomised sampler: random words in the generators / Schreier-tree random elements of
  G, test x^g = x exactly) before the P1 tree, insert everything verified. With A (or most of
  it) known up front, orbit pruning is maximal from the root and — crucial for G-08 — the
  explored tree becomes order-independent in the `0x0002` sense. Randomised discovery must never
  be *counted* on: the policy definition must fix the sampler deterministically (seeded by the
  input only) or the count stops being a function of input+descriptor (§11.1). A deterministic
  backtrack stabiliser has no such problem: its output A is the same however computed.
- (c) Sift-before-verify ordering (item 4): pure cost fix, no decision changes.

**Policy/P1/checker.** Any change to *which* automorphisms are known *when* changes explored
counts ⇒ **new work-policy ID** for (a) and (b) (e.g. `0x0005`); (c) changes nothing observable ⇒
same IDs. P1 bytes preserved (orbit lemma). Checker: **no change needed** — this is the pleasant
consequence of the CERT-0 design: the checker verifies each listed automorphism itself and "does
not distinguish the sources" and does not re-derive the policy (`docs/pruning-rules.md` §4).
Every pruned child still names one explicit automorphism, which the producer already reconstructs
from the Schreier tree ([p1_tree.c](/home/tobias/Projects/canonicalisation/src/search/p1_tree.c:340)).

**Remediation plan.** (c) trivial in `implicit_automorphism` (sift via
`canon_bsgs_contains_scratch` on `sym.a` before `g->ops->contains`). (a) in
`canon_p1_search` (table hanging off the search state, sized in `prepare`), hooked in
`evaluate_leaf` after the bytes are known. (b) depends on G-07. Tests: for each policy, counts
pinned on the `test_prune.c` fixtures plus byte identity vs `0x0001`; a fixture where the only
useful automorphism comes from a non-incumbent leaf pair (e.g. two symmetric "wrong" branches)
showing the count drop under the new ID only. Effort: (c) hours, (a) days, (b) = G-07.

### G-05 — The orbit-prune machinery itself is quadratic-ish and rebase-heavy

- Severity: medium-high. Category: performance.

**Evidence** (reasoned). Three compounding costs in `decide_before`:

1. `A_S` is materialised by *rebuilding* the chain of `A_known` on the node's prefix
   ([symmetry.c](/home/tobias/Projects/canonicalisation/src/symmetry/symmetry.c:128)), and because
   there is a *single* transient stabiliser chain shared by all depths, it is recomputed not only
   when `A_known` grew (epoch) but after **every child visit**
   ([p1_tree.c](/home/tobias/Projects/canonicalisation/src/search/p1_tree.c:307):
   `s->sym.stab_depth != depth`). A branching node with k explored children therefore pays up to
   k full rebases of `A_known` per level of the tree. (This recomputation is a *correctness* fix
   from the 19 October 2026 review, S7b-notes item 1 — using the child's longer-prefix stabiliser
   was sound but changed the counts; the fix is right, the cost model is the problem.)
2. For each undecided child reached, `decide_before` runs *two* full orbit BFS passes
   ([p1_tree.c](/home/tobias/Projects/canonicalisation/src/search/p1_tree.c:321)), each call to
   `canon_symmetry_cell_orbits` growing orbits from *all* explored representatives from scratch
   over the level generators ([symmetry.c](/home/tobias/Projects/canonicalisation/src/symmetry/symmetry.c:162)).
   Over a cell of size k that is O(k) decisions × O(k · |gens_S|) BFS work, plus, for every
   pruned child *with a certificate*, an O(path · n) transporter reconstruction
   ([symmetry.c](/home/tobias/Projects/canonicalisation/src/symmetry/symmetry.c:237)).
3. `A_known` insertion rebuilds and *verifies* the whole chain per enlarging generator
   ([symmetry.c](/home/tobias/Projects/canonicalisation/src/symmetry/symmetry.c:98)) — ≤ log₂|A|
   rebuilds, acceptable, but each is a verified Schreier–Sims from the generator table.

**Recommendation.** This is where "orbit pruning at every level via a Schreier tree of
`A_known`" should land — it already is at every level; what is missing is *incrementality*:

- Keep a *stack* of prefix-stabiliser chains, one per depth along the current path (depth ≤ n),
  invalidated lazily by epoch: on returning to depth d, the chain for d is still there unless
  `A_known` grew. Rebase cost per (node, epoch) instead of per child. Memory: O(depth × chain) —
  charge it in the §11.2 plan (the plan already budgets quadratic stacks, S8 D16).
- Compute the orbit partition of the target cell **once** per (node, epoch): one BFS forest
  (Schreier tree per orbit) over the level-d generators, then answer every decision from the
  partition: an undecided member is pruned iff its orbit already contains an explored member.
  The two-pass dance in `decide_before` is then a table lookup; transporters come from the same
  forest. When the epoch changes mid-node, recompute the partition (orbits only merge) but keep
  existing decisions — exactly the `0x0003` rule as written in `docs/pruning-rules.md`.
- The decisions must be *bit-identical* to today's (representative = first explored member of
  the orbit in cell order at decision time). The orbit partition is a function of `A_S` and the
  cell, not of BFS order, so this is provable; the chosen *transporter word* may differ, which
  the checker tolerates (it checks `b'^a = b`, membership, prefix-fixity — not the word).

**Policy/P1/checker.** No count change (same decisions) ⇒ same IDs `0x0002`/`0x0003`; P1 bytes
preserved; certificate *bytes* may change (different transporter words) while remaining valid —
existing checker tests must assert acceptance, not byte equality of certificates (they do:
accept/mutate harness). No checker change.

**Remediation plan.** `src/symmetry/symmetry.{h,c}`: per-depth stabiliser cache + `cell_orbits`
returning a reusable partition keyed by (epoch, depth, cell identity); `src/search/p1_tree.c`:
replace the two-pass loop. Tests: `test_prune.c` node counts unchanged across the refactor on
every fixture (the strongest possible regression), `stats.rebases`/`orbits` counters shown to
drop. Risk: subtle deviation in representative choice would silently become a new policy —
guard with the 700-case count comparison the S7b review used. Effort: ~1 week.

### G-06 — Leaf comparison materialises full image + full stream + copies, even to lose

- Severity: medium. Category: performance.

**Evidence.** A competitive leaf pays: full action into image storage, full
`canon_root_stream_write` into `leaf_bytes`, full compare, and on a win two full buffer copies
([p1_tree.c](/home/tobias/Projects/canonicalisation/src/search/p1_tree.c:220)). The same pattern
is in `LEX_MIN_IMAGE` per element ([objectives.c](/home/tobias/Projects/canonicalisation/src/search/objectives.c:91)),
though there the buffers are at least swapped, not copied (line 122). For graph/nested images
the stream is the expensive part, and most competitive leaves lose in the first few bytes.

**Recommendation ("cheaper leaf-certificate comparison").** Stream-compare: generate the image's
CDAG-2 bytes through a sink that compares against the incumbent byte-by-byte and aborts on the
first greater byte (or on length divergence under §4.3 rules), materialising into `leaf_bytes`
only while the outcome is still "could win/tie" — on abort-lose, nothing is retained; on win,
swap buffers as `consume_min` already does. Two caveats are the whole correctness content:
(i) the bytes compared must be *exactly* the §4.2 stream of the fully normalised image — the
image root must still be fully acted and normalised first (`canon_root_act_into`); only the
*serialisation* can early-abort, since CDAG numbering is a function of the finished DAG, not of
the prefix written so far; (ii) a hash pre-filter may be used to *find candidate ties* for G-04(a)
but never to decide equality (§7.3). For subsets and graphs, a cheaper exact pre-compare exists:
the stream of a subset is order-equivalent to its sorted member list, so comparing images as
objects first and only encoding the survivor is valid **if proved order-equivalent to the byte
order for that root kind** — that proof obligation is per-kind and must be written down
(PROFILE-EQUIV); absent the proof, use the streaming encoder, which needs none.

**Certificate angle.** With a certificate, every leaf currently forces `t_L` computation even
when its trace loses ([p1_tree.c](/home/tobias/Projects/canonicalisation/src/search/p1_tree.c:199)),
because CERT-0 records `t_L` per leaf. If G-01(a) lands (leaf `t_L` = last sweep's `u`, already
computed and already recorded via the observer), the certificate's per-leaf cost drops to writing
n words. A CERT-1 could go further and omit `t_L` at leaves whose trace provably loses, letting
the checker ignore them — but the checker needs every leaf's *key* only to take the minimum; a
leaf with trace > final best trace cannot be the minimum. The checker would need the final claim
before judging leaves (it reads the claim last but can buffer). Record as a CERT-1 option in G-09.

**Policy/P1/checker.** Streaming compare: no count change, bytes identical, no checker change.
Object-order pre-compare: bytes identical *iff* the order-equivalence holds — treat as
PROFILE-EQUIV proof work, not a policy change. **Remediation**: a compare-sink variant of
`canon_root_stream_write` (or a writer that takes a limit buffer), used by both `evaluate_leaf`
and `consume_min`; tests are the existing byte-identity tiers plus a forced-tie/forced-prefix
unit test of the comparator against `canon_bytes_compare` on random streams. Effort: days.

### G-07a — Labeling and signed P1 runs are unpruned by test convenience, not by mathematics

- Severity: high. Category: performance (with a spec-conformance decision attached).

**Evidence.** `run_p1` always passes `NULL` options — the unpruned traversal under every work
policy ([objectives.c](/home/tobias/Projects/canonicalisation/src/search/objectives.c:443));
S7 decision 4 records why: "their `t` is part of the answer and is compared with the model"
(`docs/pruning-rules.md`, "Objective scope"). But trace what `t` is actually used for:

- *Labeling, deterministic*: the payload encodes λ₀ = least of Aλ, and the reported witness is
  recomputed as t₀ = ρ⁻¹λ₀ ([objectives.c](/home/tobias/Projects/canonicalisation/src/search/objectives.c:500)).
  Any attaining t gives the same coset Aλ (A complete), hence the same payload and same t₀.
- *Labeling, ANY*: the witness λ = ρt must merely satisfy x^λ = c, t ∈ G′ — any attaining leaf's
  t qualifies.
- *Signed, nonzero*: the sign s = χ(t) is the same for every attaining t (spec §8.4: "If p and q
  reach c, p q⁻¹∈A makes their signs equal", A even on this route), and deterministic mode
  replaces t by the least of A t by descent
  ([objectives.c](/home/tobias/Projects/canonicalisation/src/search/objectives.c:559)).
- *Signed, zero*: does not use the P1 t at all.

Orbit pruning (lemma in `docs/pruning-rules.md` §2) preserves the minimum *key*; the cases above
need exactly the key (c's bytes) plus *any* attaining t, with determinism restored afterwards by
coset descent over the complete A. So both objectives' P1 stages can run under `0x0002`/`0x0003`
semantics with bit-identical payloads, signs and deterministic witnesses, and valid ANY
witnesses. G-03's trace domination also applies and does not even change the ANY t.
What pruning must **not** be used for here: certifying signed *nonzero* from the automorphisms
the pruned tree happened to expose — that is open obligation SIGN-COVER (§8.4), and nothing in
this finding touches it; the complete-stabiliser requirement stands (made cheaper by G-07b).

**Policy/P1/checker.** P1 bytes preserved; complete-answer bytes preserved as argued. Node count
for these objectives **changes**: the policy table in `docs/pruning-rules.md` currently defines
`0x0002`/`0x0003` as pruning CANONICAL_IMAGE only, "every other objective runs its reference
traversal". Extending pruning to these two objectives under the same IDs changes their counts
retroactively — spec §11.1 says "a pruning policy that changes the count has its own work-policy
ID", so either issue a new ID (recommended; `0x0002`/`0x0003` keep their documented counts for
every objective) or take a D2-style maintainer decision that the IDs' definitions are extended.
Certificates do not exist for these objectives; nothing in the checker changes.

**Remediation plan.** Thread run options + policy through `run_p1`/`canon_obj_labeling`/
`canon_obj_signed` (they already receive the quota; the API must pass the descriptor's policy —
today `solve_p1_objective` discards it, [api.c](/home/tobias/Projects/canonicalisation/src/api/api.c:1211)).
Update the Python-model test tiers to compare payloads/signs/deterministic witnesses under the
new policy, ANY witnesses by `verify_witness`. Add the objective-scope section to
`docs/pruning-rules.md` with the attaining-t argument above as the coverage lemma. Effort: days
of code, the decision on IDs is the long pole. Gain: whatever orbit pruning gives on canonical
image (up to 1237→6 on the recorded Sym(6) fixture) now also on two more objectives — but note
the *dominant* cost of both objectives is the subsequent stabiliser enumeration, i.e. G-07b.

### G-07b — Better algorithms for stabiliser / transporter / lex-min: partition backtrack instead of element enumeration

- Severity: high. Category: performance. (Design finding; the counterpart of G-02.)

**Why the reference is slow** is G-02. The standard faster family (Leon's partition backtrack;
Jefferson–Waldecker–Wilson-style search for arbitrary G — named in `README.md` as prior art) fits
this codebase unusually well because P1 already supplies an equivariant refinement. Design, per
objective. In all cases the *answer objects* (Group(A) bytes, coset bytes, witnesses, statuses)
are the spec's; only the search changes, so each needs a §8.2-style coverage argument:
"Optimised pruning must either prove every omitted region has no relevant solution or map its
solutions to retained coverage appropriate to the requested result" (§8.2).

**Common skeleton (stabiliser of x in G; transporter x→y is the same with two objects).**
Maintain a partial map φ: base points β₁..β_d ↦ images γ₁..γ_d, with the invariant that some
g ∈ G extends φ. Data per node: (i) the subgroup H = G_(β₁..β_d → γ..) — represented exactly as
now: a suffix of a chain of G rebased so its base starts with the β's, times a coset
representative r with β_i^r = γ_i (region H r, the §8.1 node object — the design is a *pruned
§8.1 tree*, which makes the coverage argument a subtree argument); (ii) ordered partitions
Π_x (refinement of x with β's individualised) and Π_y (of y with γ's), refined by the P1 stages
(O stage on both objects; the G stage is *not* directly reusable — it is defined relative to one
group action on one domain; use O + initial keys + individualisation, i.e. the graph-backtrack
refiner, and lean on H's orbits for the group side).
Branch: choose the first non-singleton cell C of Π_x and the corresponding cell C′ of Π_y (must
have equal size and, all along, equal cell-size sequences, else the region is *empty*: any
g extending φ with x^g = y maps refinement cells to refinement cells by equivariance of every
stage — this is the "no relevant solution" prune, lemma = P1 tree equivariance restricted to the
O stage and initial keys). Children: β = first point of C, γ ranging over
C′ ∩ (β^{H})^r (the group-feasible images). Recurse with H ← H_β-conjugate bookkeeping via one
descent per child (canon_coset machinery exists). At a discrete leaf, r is a candidate: verify
x^r = y exactly (never trust the refinement); on a hit, act per objective.
Prune 2 (orbit pruning for the *stabiliser* objective, the delicate one): if γ, γ′ ∈ C′ lie in
one orbit of the pointwise stabiliser of (γ₁..γ_d) in A_known′ (= r⁻¹ A_known r transported;
for the pure stabiliser with y = x and the search formulated on images, of
(A_known)_(β..) on the γ side), then every solution through γ′ is an A_known-translate of a
solution through γ: precisely, if a ∈ A_known fixes the previous images and sends γ to γ′, and
g′ is a solution in the γ′ child, then a⁻¹g′ (with the convention-checked side) is a solution in
the γ child and g′ ∈ ⟨A_known, γ-child solutions⟩. This is exactly the licensed form: "A
stabiliser prune may omit solutions only with proof they lie in the final generated subgroup"
(§8.2) — induction on the tree with A_known only growing gives: at exhaustion, A_known contains
a generator set whose closure includes every pruned hit, and every generator inserted is a
verified hit, so A_known = A. (I am sketching, not proving, the side/convention details; they
are where this lemma will live or die, and the repo's convention tests are the tool.)
Termination/completeness is exhaustion of the pruned tree; *no order argument* (|A_known| vs an
upper bound) is available in general and none should be claimed.

- **STABILISER**: as above with y = x. Inserts use the existing verified-chain insertion
  ([objectives.c](/home/tobias/Projects/canonicalisation/src/search/objectives.c:175)). Expected
  gain: on rigid/near-rigid objects the tree collapses to near a path (refinement usually
  discretises), replacing |G| leaves by O(n · refinement); on empty/complete objects orbit
  pruning by a quickly-complete A_known collapses each level to one child. Worst case remains
  exponential (set-stabiliser is hard) — but the *constant* is the P1 node's, not |G|'s.
- **TRANSPORTER_ONE**: y ≠ x, stop at first verified leaf (any-hit needs no coverage, §3.2);
  empty only on exhaustion of the pruned tree — note the A_known orbit prune above is *not*
  available before a first hit (A_known of x still is: automorphisms of x map transporters to
  transporters, g ↦ a g, so the same prune is sound for *existence* with A_known from e.g.
  generators fixing x; state it separately). Deterministic mode unchanged afterwards (complete A
  by the stabiliser search, descent for r₀ — but now both searches are backtracks).
- **TRANSPORTER_COSET**: one transporter search + one stabiliser search, payload as today
  ([objectives.c](/home/tobias/Projects/canonicalisation/src/search/objectives.c:350)).
- **Labeling / signed (deterministic routes)**: their stabiliser phase is the stabiliser search
  above (signed: evaluate χ on each *generator that enlarges A_known*, stop on odd in ANY mode —
  the existing even-invariant argument at
  [objectives.c](/home/tobias/Projects/canonicalisation/src/search/objectives.c:183) carries
  over verbatim since it only needs "every element of A is generated by inserted hits by
  exhaustion"; deterministic zero exhausts and descends as today). Their P1 phase is G-07a.
- **LEX_MIN_IMAGE**: be honest — NP-hard in general (§3, Independent Set); no algorithm removes
  that. The right improvement is branch-and-bound *on the §8.1 coset tree* with §8.3 region
  bounds, since the consumer needs exhaustion: for a region H r and an order key, a bound must
  "lower-bound the selected order key for every permutation in H r" (§8.3). Constructible today
  for the two implemented orders: for each output position whose *preimage set* under H r is
  already a singleton or confined to points of x with a determined contribution, the key byte is
  forced; the bound is the key with forced positions fixed and the rest filled by the
  order-minimal possibility (for SIMPLE-UPPER-1, entries (i,j) with both preimages confined to
  H-orbits of size 1 or inside known non-edges are forced 0/1; for subset CDAG-BYTE-1, sorted
  member list of x^{Hr} is bounded below by greedy least-possible members given orbit sizes and
  |x|). Prune when bound > incumbent; when bound == incumbent, prune only in ANY-witness
  image-only mode (§8.3: "Equality with an incumbent can be pruned for image-only minima");
  deterministic witness mode must keep the tie regions or recover the least attaining r
  afterwards via A (which needs the stabiliser — for LEX_MIN that is a *different* stabiliser,
  of the minimal image, computable by the backtrack above; then least r by descent — this is
  cleaner than tie-tracking during the search and mirrors TRANSPORTER_ONE's structure).
  Additionally children of an §8.1 node whose regions have identical forced-prefix bounds can be
  visited best-bound-first to raise the incumbent early — order changes counts → policy.

**Policy/P1/checker.** These are profile-NO_TREE objectives; P1 is untouched. The quota counted
traversal changes completely: **§11.1's text fixes the count to "P1 NODE tokens plus §8.1
enumerator visits" with the work policy selecting the traversal — a backtrack traversal needs a
new work-policy ID and arguably a spec sentence generalising "§8.1 enumerator visits" to "visits
of the traversal the work policy names" (flagged in §3 ambiguities below).** Certificates: none
exist for these objectives today; spec §8.2 says certificates (v1) "record splits and exact
empty/bound/representative justifications" — the backtrack tree is *more* certifiable than the
enumeration (each empty region carries a refinement-mismatch justification the checker can
re-derive like CERT-0 sweeps; each orbit-pruned child carries one automorphism as in rule 5).
Design CERT-1 around the backtrack from the start; do not certificate the enumerator.

**Remediation plan.** New module `src/search/backtrack.{h,c}` sharing `canon_partition`, the O
stage (`canon_p1_stage_o` is already separable, [p1.c](/home/tobias/Projects/canonicalisation/src/refine/p1.c:265)),
chain suffix/transporters, and the consumers rewritten over it in `objectives.c`; keep the §8.1
enumerator as the `0x0001`-policy reference and the test oracle (exactly the role the explicit
backend plays for the chain). Tests: exhaustive agreement tiers as today (every subgroup n ≤ 4,
randoms to 7, both traversals), plus the orbit-prune-stabiliser lemma tested by targeted
symmetric cases where a wrong side/convention loses generators. Effort: this is a slice-scale
project (weeks), the largest single win for everything except CANONICAL_IMAGE.


### G-08 — Deterministic parallelism: nothing exists; the `0x0003` count is schedule-defined, so a new mathematically-defined policy is needed

- Severity: high (design gap). Category: performance / spec-conformance (of any future design).

**Evidence.** `src/scheduler/` and `src/checkpoint/` are README placeholders "Filled by M6…
Nothing is implemented here yet". `canon_solve_batch` is sequential by design statement
([api.c](/home/tobias/Projects/canonicalisation/src/api/api.c:1310) and `api/README.md` S8:
"batch cancellation and pool execution are not offered"). The process-wide ledger (S8 D17,
`HANDOFF.md:117`: "the per-solve peak is exact when one solve runs at a time") and grow-only
per-workspace search states ([p1_tree.h](/home/tobias/Projects/canonicalisation/src/search/p1_tree.h:38))
are the current blockers to simply running solves concurrently.

**The core difficulty, stated precisely.** Spec §11.1: the quota count "must give the same count
in every execution mode and worker count" and be "a function of input, objective and descriptor".
For `0x0002` that is already true mathematically (`A_known` fixed before search; explored
children are a function of orbits). For `0x0003` it is true *only definitionally through the
sequential DFS order*: `A_known` grows when a best-tying leaf is met, so which later children
are pruned depends on which leaves were met *first* (`docs/pruning-rules.md`, Quota: "under
`0x0003` the fixed sequential order also fixes which leaves are met first"). A parallel search
meets leaves in timing order. Therefore *reproducing the `0x0003` count in parallel* (the S7
decision: "parallel modes reproduce the count") cannot be achieved by counting what workers
actually explored; speculative nodes and count-charged nodes come apart.

**Concrete design (R1–R3, §14.1–14.3), in three staged policies.**

*Stage 0 — batch pool (do first; §14.1 "Batch parallelism is the default small-instance
policy").* `CORE_POOL` executes `solve_batch` entries on P worker-owned workspaces; each entry is
an ordinary sequential solve (R1 trivially: same pinned function; R2/R3 nothing to share).
Statuses/bytes per entry are what `solve` returns (§17), order preserved by construction.
Prerequisites, both already on the books as M5/M6 work: per-workspace memory ledger replacing
the process-wide one for per-solve peaks (admission must reserve `sum_workers(...)` per §11.2's
equation, and "Never let race-dependent transient peaks cause a purported input-capacity failure"
— so the pool must admit at most as many concurrent solves as the *plan* reserves, queuing the
rest; queueing is not a capacity verdict), and a thread abstraction (none exists; C17 `<threads.h>`
or OS shim — a portability decision for the maintainer). Quota determinism: untouched (per-solve
sequential). This stage captures the §12.3 small-instance regime, which is the stated batch use
case, at the lowest risk. Effort: medium; gain ≈ cores on batches.

*Stage 1 — subtree pool for `0x0001`/`0x0002` canonical image.* Region = P1 subtree named by its
recipe (the individualisation prefix S; §14.1 "Recipes name actual branch choices"). Coordinator
state machine mapping (§14.2): Admit root → one READY task with recipe "()"; a RUNNING worker
either *splits* (reserve all child records under the mutex, parent → WAIT_CHILDREN, children
READY — children are only the *explored* ones under `0x0002`'s fixed `A_known`; pruned children
are recorded as LINK to their representative child's task ID, with the same explicit automorphism
the certificate would name — §14.2 LINK requires "objective-specific equivalence… never link
merely because images coincide", and the orbit lemma is exactly that equivalence for
CANONICAL_IMAGE) or *completes* a subtree serially and commits (RUNNING → COVERED) its least key
and least attaining t as one immutable bundle ("Publish incumbent trace/bytes/witness as one
immutable bundle after exact comparison"). The root is COVERED when all children/links discharge
("COMPLETE requires root discharge… not empty queues"). Which subtrees to split is a *physical*
decision (split while task slots free and depth < limit; otherwise solve serially) — it affects
time only, because under `0x0001`/`0x0002` the explored tree is fixed in advance: total certified
node count = number of nodes in that fixed tree, computable as sum over tasks of locally counted
explored nodes, independent of who explored what. **`CAPACITY_LIMIT` semantics**: the verdict
must be "fixed-tree count > quota". Workers reaching a *global speculative* counter must not
fabricate that verdict early (a worker may have explored nodes the sequential run would also
explore — the tree is the same — so a shared counter of *distinct* explored nodes is exact here;
abandonment at quota is legitimate because the counter is a lower bound reaching quota+1 implies
the sequential count exceeds quota). This exactness is special to fixed-tree policies and is the
reason to stage them first. ANY-mode witness: least t over *attaining explored leaves* is a
minimum over a fixed set, computable order-independently by taking per-subtree minima — even the
witness is schedule-independent here (stronger than §14.3 requires). Helpers per R2: idle
workers may run automorphism discovery (G-04(b) deterministic backtrack is *not* R2-racing, it
is a separate objective; randomised discovery contributes verified automorphisms which under
`0x0002` semantics **cannot be used** mid-search without changing the policy — they can only be
banked for a *later* solve or for the Stage-2 policy. R1 racers (same pinned function, e.g. a
second kernel) have no current use. R3 shortcuts: the §7.3 singleton-orbit root shortcut, if
ever taken, is an R3 candidate needing its trace rule (see ambiguities).

*Stage 2 — a schedule-independent successor to `0x0003` (required; do not parallelise `0x0003`
by counting explored nodes).* Define the policy by the tree, not by time: for a node S, let
`A(S)` be the group generated by (i) the input generators fixing x, and (ii) **all** products
`t_L t_L'^{-1}` over equal-key leaf pairs (L, L') with L in the subtrees of the *earlier siblings
of the ancestors of S* (DFS-predecessor leaves), taking t over the *unpruned* leaves of those
subtrees… — the italicised gap is the definitional work: predecessor subtrees are themselves
explored under this rule, so define by mutual recursion on DFS order over the *reference* tree:
process leaves in DFS order (cell order fixed as today); `A_known` before leaf L = group from
(i) + all equal-key pairs among *explored* predecessor leaves; a child is explored iff no
predecessor-explored sibling is in its A_S-orbit under that group. This is a purely mathematical
function of (x, G, generator list, cell-order convention) — sequentially computable, and in
parallel computable because each subtree can return, as its summary, (least key, least t, and a
generating set for the group of all equal-key-pair products *inside* it — the G-04(a) image table
is precisely this data, compressed by keeping one t per distinct image). The coordinator, knowing
sibling order, can then decide for later siblings exactly what the sequential rule decides;
workers exploring later siblings speculatively under a *snapshot* subgroup (stale ⇒ possibly
explores extra children, §14.1 "A stale candidate/subgroup can lose pruning, never coverage")
produce results that remain valid: their least keys are over supersets of the policy's leaves,
hence equal minima (orbit lemma), and their node counts are **not** charged — the charged count
is computed at commit time in sibling order by *recounting what the policy would have explored
given the returned summaries*… and here is the honest cost: exact charged counting without
re-exploration requires each task to return its explored-node count *as a function of the
automorphism group snapshot it is given*, which in general it cannot. Two implementable
resolutions: (ii-a) *ordered commit with re-decision at boundaries only*: split the tree at a
fixed depth frontier; explore frontier subtrees in parallel under snapshots; the *policy*
(fixed in advance) declares that within-subtree counts are charged as explored under the snapshot
the subtree was assigned, where snapshots are assigned from *completed earlier frontier
subtrees in order* at claim time under the mutex (a deterministic function of the claim order —
but claim order is timing… no). (ii-b) — recommended — **make the frontier the policy**: define
the new policy's explored tree level-wise: automorphism knowledge propagates only *between
whole levels* (or between frontier blocks of a fixed size in DFS order). Knowledge used at depth
d = group generated by (i) + all equal-key pairs among leaves of completed blocks before the
block in DFS order, where blocks are subtrees cut at a descriptor-fixed depth/block-size. Within
a block the rule is `0x0002`-like (fixed group) possibly plus intra-block `0x0003` sequential
semantics if the block is run by one worker (then its internal count is the sequential one,
charged verbatim). This is schedule-independent (blocks and their assigned knowledge are fixed
by DFS order), parallel (blocks run concurrently; their internal searches are ordinary
sequential policy runs under a *larger starting* `A_known` — which needs the orbit lemma only,
since starting `A_known` is an input to the policy), and exactly countable (sum of per-block
sequential counts). It discovers slightly less/faster-or-slower than `0x0003` depending on block
size — it is a different policy and gets a **new work-policy ID** regardless. Checkpointing
(§18) then maps cleanly: coherent state = block coverage graph + per-block summaries + recipes,
captured under the coordinator mutex at safe points between block commits.

What not to do: publish implicit automorphisms into a shared `A_known` mid-search and charge
whatever resulted (breaks §11.1 — the same input becomes COMPLETE on a fast machine and
CAPACITY_LIMIT on a slow one); or let workers' best-trace publications trigger G-03 cuts whose
charged count is then timing-dependent (same defect; trace domination inside one worker's serial
block is fine, across blocks only via block summaries in order).

**Remediation plan.** Order: per-workspace ledger + thread shim → batch pool (Stage 0) →
coordinator + recipe reconstruction for `0x0002` subtrees (needs: rebuild node state from prefix
— replay refinement from the root, reusing `visit` logic; snapshot stacks exist) → Stage-2
policy definition written into `docs/pruning-rules.md` and `include/canon/canon.h` before code.
Tests: §20's "Repeat across schedules" — byte/status/count identity of every tier at 1, 2, 4,
8 workers, including quota-boundary fixtures (count exactly quota, quota+1) which are where all
of the above goes wrong; a chaos test with injected sleeps is worth more than core-count tests.
Effort: Stage 0 weeks; Stage 1 a slice; Stage 2 a slice plus the definitional review. This is
M6-sized, as the plan already assumes.

### G-09 — Certificate v0 cost: one rebased verified chain per sweep per node

- Severity: medium. Category: performance.

**Evidence** (reasoned + recorded). The observer writes, for *every* G stage of *every* node,
F, u, M and a complete serialised chain of G rebased to M
([certificate.c](/home/tobias/Projects/canonicalisation/src/search/certificate.c:256)); the
chain serialisation is O(depth·n + gens·n + prov) words
([certificate.c](/home/tobias/Projects/canonicalisation/src/search/certificate.c:75)), and the
checker verifies every such chain (§9.1). Recorded: "The size is dominated by the rebased chain
per G stage per node (D10); the 189 kB certificate is a random subset under a group on 6 points
with 73 nodes" (S7-notes) ≈ 2.6 kB/node at n = 6, growing ~linearly in n and chain size. At the
node counts where certificates matter most (10⁵–10⁶ quota-bound trees), the certificate is
gigabytes or `CAPACITY_LIMIT` by the `max_output_bytes` bound (D3) — i.e. the checked mode is
unavailable exactly on hard instances. Certificate mode also disables the leaf trace shortcut
(G-06) and adds a per-sweep rebase on the producer side (line 282) on top of G-01's rebases.

**Recommendation (CERT-1 design points).** (1) Deduplicate chains: consecutive sweeps/nodes
rebase to related M (F only grows along a path); emit each *distinct* (by M) chain once into a
table and reference by index — the G-01(d) F-cache key is again the natural key. (2) Or offload:
let the checker derive G_M orbits and M-minimality from the (single, verified) root chain by its
own orbit/stabiliser computations given F, u, M — it already contains a chain implementation
(`checker/cc_chain.c`); it would verify u ∈ G by sift, M = F^u, and minimality by computing
orbits of successive stabilisers of M from the root chain's generators (Schreier BFS), trading
certificate bytes for checker recomputation (which is G-01-priced, but checker time is the
scarcer trust resource per §20's "Count certificate generation/checking and size" — this trade
must be measured, not assumed; dedup (1) is unambiguously good). (3) Per-leaf `t_L` only where
the checker needs the key (G-06). (4) Any new prune (G-03) needs its record kind designed in the
same revision — bundle these into CERT-1 with enumeration/backtrack coverage (G-07b) rather than
patching CERT-0 per prune.

**Policy/P1/checker.** No count or byte change to *answers*; certificate bytes are not frozen
semantics (they are evidence), but format changes are breaking for `canon-check` pairing — version
the magic (`CNC0` → CERT-1) and keep the checker accepting both during transition (checker is
maintainer-owned-trust, not a wire contract with third parties yet). Effort: weeks, mostly
checker; do after G-01/G-03 so the format is cut once.

### G-10 — Invariant/state pruning for P1 beyond automorphisms: what is and is not available

- Severity: low-medium. Category: performance (with soundness fences).

**Analysis** (reasoned; partly a "do not attempt" finding so later work does not re-derive it).

1. *Stronger refinement as a prune is profile change, not pruning.* Any invariant that discards
   children because a *finer* invariant distinguishes them changes the trace of retained leaves
   only if it changes splits; if it changes only *which* subtrees are visited while keeping
   P1's own cells, it must still map visited onto pruned by an x-preserving equivariance — that
   is the automorphism lemma or state-equality lemma below. §15 is explicit: stronger refiners
   are "future profiles or proved P1-equivalent shortcuts". There is no third route within P1.
2. *Bounds on descendant keys* (§7.3 "or a bound for every descendant key"): since trace compares
   first and the node's trace is a prefix of every descendant's, a byte-bound can only bite when
   the trace bound already ties the incumbent — i.e. byte-bounds are subordinate to G-03, and
   §7.3 forbids the converse ("A bound on image bytes cannot override a better trace"). The only
   general trace bound is G-03's first-difference test; finer token-wise bounds (e.g. bounding
   the next STAGE token from current cell sizes, since splits only refine) can strengthen G-03
   in one narrow case: when the node's prefix is a proper prefix of the incumbent trace, the
   next token is NODE(d+1) or LEAF — both known — followed by a STAGE_O whose U32(k) satisfies
   k ≥ current cells. If even the *least* such continuation exceeds the incumbent's byte at that
   position, prune. Correct, but a small refinement of G-03; implement only if measurements show
   proper-prefix nodes are common. Lemma = token monotonicity under split; new ID if folded into
   G-03's policy from the start (do that).
3. *Exact state equality with transport* (§7.3, §16): if two nodes S, T have the *same ordered
   partition cell-for-cell* and the *same trace prefix*, their descendant key multisets are
   identical (the future of the search is a function of (π, x, G); L extraction and traces agree),
   so exploring one covers the other. Transport version: π_T = π_S^h, τ equal, x^h = x reduces to
   an automorphism not necessarily fixing either prefix — strictly more general than sibling
   orbit pruning (it can identify nodes in different subtrees), sound by the same equivariance
   with leaf keys equal (as in the orbit lemma proof). Checker: a LINK-like child/node entry
   naming the retained node by its DFS index plus h; checker verifies h ∈ G, x^h = x, π and trace
   equality by its own replay — needs node indexing in CERT-1. Value: *conjecture* — duplicate
   states with equal traces but no prefix-fixing automorphism are probably rare beyond what
   orbit pruning captures; the state table costs O(nodes × n) memory. Implement only the cheap
   half with no count change: **skeleton memoisation** (§16) — cache the refinement *result*
   (cell sizes, split) keyed by exact (π-cells, stage inputs) to skip recomputation while still
   visiting and counting the node. That is G-01(d)-adjacent and policy-neutral.
4. *Child ordering* (visiting promising children first to raise the incumbent for G-03/G-04):
   spec §7.1 makes cell *member* order non-semantic ("Internal order of members within a cell is
   not semantic"), and the key minimum is order-invariant — but every incumbent-sensitive prune's
   *count* is defined through the implemented order (partition `lab` order today). Reordering is
   therefore a new work policy, not a free optimisation; with G-03 it's a real lever (order by a
   cheap heuristic, e.g. child partition's cell-count after one sweep). Bundle into the G-03
   policy definition if adopted; verify count determinism (heuristic must be a pure function of
   node state, ties by current cell order).

### G-11 — Quota counts nodes, but node cost varies by orders of magnitude and the dominant term is unmeasured

- Severity: low (note with teeth). Category: contract / maintainability.

**Evidence.** §11.1 deliberately makes the quota logical ("Wall-time/observed-node cutoffs are
cancellation policies, not semantic capacity"). Within that design, per-unit cost today spans:
a subset node that hits `tuple_min`'s trivial-H early exit vs one paying O(n) rebases (G-01); an
enumerator visit paying a full descent (G-02); a certificate node paying chains (G-09). The
metrics surface exposes `stabiliser_rebases` and `coset_rebuilds` but *not* `tuple_min` rebuilds
(stats discarded, G-01 item 3), so the biggest term in §12.1's ledger
(`sum_nodes(T_refine + T_group_node + …)`) cannot be attributed from metrics today.
**Recommendation**: do not touch the quota semantics; do expose `tuple_min` calls/rebases and
G-stage cache hits in `canon_metrics`, and state in the certificate/`solve` docs that
`CAPACITY_LIMIT` bounds nodes, not seconds. Effort: hours. This is also the instrumentation
G-01/G-02 fixes should be judged by (node counts identical, rebuild counts collapsed).

---

## Ranking by gain/effort (implementation order I recommend)

| # | Item | Gain | Effort | New policy ID? | Checker work |
|---|---|---|---|---|---|
| 1 | G-11 metrics for tuple_min | enabler | hours | no | none |
| 2 | G-01(a,b) leaf-u reuse, scratch, no allocs | high | days | no | none |
| 3 | G-04(c) sift-before-verify; G-06 streaming leaf compare | medium | days | no | none |
| 4 | G-01(c,d) base-change + F-cache | very high (quota-bound cases) | 1–2 wks | no | none |
| 5 | G-05 incremental A_S stack + once-per-epoch orbits | high when pruning | ~1 wk | no | none |
| 6 | G-07a prune labeling/signed P1 stages | high for those objectives | days + ID decision | **yes** (or D2-style extension) | none |
| 7 | G-02 stage 1 Schreier transporters | high (all enumerations) | days + decision | no (count); order note | none |
| 8 | G-03 trace-prefix domination (+G-10.2/10.4 folded in) | high on asymmetric trees | ~1 wk | **yes `0x0004`** | new record+rule |
| 9 | G-04(a) image-table implicit automorphisms | variable, sym. cases | days | **yes** | none |
| 10 | G-07b partition backtrack (stab/transp/lex-min B&B) | transformative for §8 | weeks (slice+) | **yes** + §11.1 sentence | CERT-1 design |
| 11 | G-09 CERT-1 (dedup/derived chains, leaf economy, new prune records) | enables checked mode at scale | weeks | n/a (format) | major |
| 12 | G-08 stage 0 batch pool → stage 1 `0x0002` subtrees → stage 2 block policy | ~cores where applicable | M6-sized | stage 2 **yes** | cert. from summaries |

Items 2–5 change no counts and no bytes and should land before any new policy is defined, so the
new policies' counts are measured on a non-pathological baseline.

---

## 3. Spec ambiguities (quoted; not resolved here)

1. **Quota text vs new traversals (§11.1).** "A logical work quota … counts the nodes of the
   traversal fixed by the profile's policy for the objective: P1 `NODE` tokens plus §8.1
   enumerator visits". The second clause names one concrete traversal for non-P1 work. Whether a
   work-policy ID may substitute a different traversal (G-07b backtrack) whose *visits* are not
   §8.1 visits — and then what one "visit" is — is not stated. (For P1 the text is clear because
   it continues "which traversal is counted is selected by a work-policy ID".)
2. **"Finalised" trace prefix (§7.3).** "An optimisation needs a coverage lemma for this exact
   key: finalised trace-prefix domination…" — *finalised* is used once, undefined. G-03 reads it
   as "bytes already appended on the root path", which the concatenation definition supports but
   the spec never says in so many words that a node's trace is a prefix of descendant traces.
3. **Stabiliser prune licence (§8.2).** "A stabiliser prune may omit solutions only with proof
   they lie in the final generated subgroup." *Final* generated subgroup is defined by the run
   that the prune itself shapes; the induction principle that makes this non-circular (e.g.
   G-07b's orbit argument) is left to the lemma author. Acceptable as stated, but any reviewer
   of G-07b must be told which invariant discharges it.
4. **Parallel count accounting (§14.2 vs §11.1).** §11.1 demands identical counts across worker
   counts; §14.2's protocol never says how LINKed or speculatively explored regions are charged,
   nor whether a region completed by a stale snapshot (superset of the sequential leaves) can be
   *committed* at its speculative count. G-08 stage 2(ii-b) is one consistent reading; the spec
   as written does not choose one.
5. **Trace under the root shortcut (§7.3).** "If every generator fixes x, the unsigned orbit is a
   singleton and returning x is equivalent to P1 regardless of trace." A result also *reports* a
   trace (S1 result shape); which trace a shortcut run reports is unstated ("A caller requesting
   a trace certificate receives the prescribed trace or a checker rule for this shortcut" covers
   certificates, not the ordinary result field). The engine currently never takes the shortcut in
   the P1 search (signed mode still runs P1), so this is latent.
6. **Work-policy ID granularity (§11.1).** IDs live in the capacity descriptor once per problem,
   but traversal choices differ per objective (definitionally in `docs/pruning-rules.md`, e.g.
   "`0x0002` prunes CANONICAL_IMAGE; every other objective runs its reference traversal"). The
   spec does not say whether the *definition* of an ID is per-(ID, objective) pairs that may be
   extended objective-by-objective (G-07a) or a single bundle that any count change anywhere
   retires. Precedent D2 (realisation defects keep the ID) does not cover *intended* extensions.

## 4. Things checked and found sound

- **Orbit lemma and its realisation.** The proof in `docs/pruning-rules.md` §2 is correct; the
  pointwise-prefix hypothesis is exactly what level-d strong generators supply
  ([symmetry.c](/home/tobias/Projects/canonicalisation/src/symmetry/symmetry.c:112)), the 19 Oct
  fix (recompute when `stab_depth != depth`) restores the rule as defined, and the checker
  re-verifies the three facts it needs per pruned child (rep explored+same node, a ∈ G fixing x
  and the prefix, b'^a = b). I found no gap; G-05 is purely about its cost.
- **Implicit automorphisms.** Derivation (`t_L t_best⁻¹`, injective encoding) is right, and the
  redundant membership+action verification turning failure into `INTERNAL_ERROR` rather than a
  silent skip ([p1_tree.c](/home/tobias/Projects/canonicalisation/src/search/p1_tree.c:150)) is
  the correct failure semantics.
- **Leaf trace shortcut.** Skipping image/stream when trace already loses is sound (trace-first
  key, §7.2); the certificate exception is handled (line 199) and documented as not a prune.
- **§8.1 coverage + consumers.** The disjoint coset split, stabiliser-by-exhaustion
  ([objectives.c](/home/tobias/Projects/canonicalisation/src/search/objectives.c:154) comment is a
  correct completeness argument), `A g` for transporter cosets, deterministic witnesses by descent
  over a *complete* A, and the signed consumer's sift-before-χ optimisation (even `A_known`
  invariant, odd hit can't be a member — argument at line 183–190 is correct) all check out.
  These are slow references being used as production algorithms (G-02), not wrong ones.
- **Quota plumbing.** One quota per solve, decremented across phases
  ([objectives.c](/home/tobias/Projects/canonicalisation/src/search/objectives.c:434),
  [api.c](/home/tobias/Projects/canonicalisation/src/api/api.c:1184)), counted at visit entry in
  both traversals ([p1_tree.c](/home/tobias/Projects/canonicalisation/src/search/p1_tree.c:363),
  [enumerate.c](/home/tobias/Projects/canonicalisation/src/coset/enumerate.c:38)); cancellation is
  kept separate from capacity as §11.1 requires. Nothing in the current code violates R1–R3:
  there is no parallelism and `solve_batch` matches CALLER_THREADS.
- **No hidden count coupling.** Pruned runs report profile `0x0001` with the work policy recorded
  separately in results and certificate headers, as §11.1 requires; unknown policy IDs are
  `UNSUPPORTED_ACTION` at problem creation ([api.c](/home/tobias/Projects/canonicalisation/src/api/api.c:773)).

*End of report. No unfinished items were carried beyond this file.*
