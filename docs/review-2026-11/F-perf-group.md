# F-perf-group: deep performance review of the permutation-group algorithms

## 1. Scope

Read in full: `docs/review-2026-11/BRIEF.md`, `CLAUDE.md`, `README.md`, `HANDOFF.md`
(incl. "Costs accepted for now", HANDOFF.md:94); spec `docs/specification.md` §§3, 7.1–7.3,
8.1–8.4, 9.1–9.4, 11.1–11.3, 12.1–12.3; `src/perm/perm.{h,c}`;
`src/bsgs/chain.{h,c}`, `verify.c`, `provenance.{h,c}`, `chain_backend.c`, `group.{h,c}`,
`reference.c` (test-only); `src/coset/coset.h`, `least.c`, `enumerate.c`;
`src/encoding/group_stream.{h,c}` (the §9.4 writers only); `src/search/objectives.c` in
full (signed route §8.4 at objectives.c:154–226, 542–771). Call-site level only: the
rebase/insert consumers in `src/symmetry/symmetry.c`, `src/search/certificate.c`,
`src/object/dag.c`, the admission-plan chain term in `src/api/api.c:580–731`, the two
`tuple_min` calls (`src/refine/p1.c:344`, `src/search/p1_tree.c:205`).

Not covered: `src/bsgs/explicit.c` internals (test oracle; noted only where the shared
contract binds it), the rest of `src/encoding/`, `checker/`, benchmarks (none exist/run).
No test suite, benchmark or reference run was executed (brief). No build was run either:
**every complexity claim below is reasoned from the code, not measured.** Literature is
labelled **(lead)** unless the repo already cites it; the only group-algorithm source the
repo cites is the canonical-generators lemma for §9.4 (`reviews/algorithm-literature-review.md:51`,
spec §9.4 ¶3). Seress/Holt-style BSGS practice (base swap, Sims/Jerrum filters, random
Schreier–Sims, product replacement) is **(lead)** throughout.

Notation: `n` degree; `b` base length; `r_i` level orbit sizes; `s_i=|S_i|` (inclusive,
chain.h:12–13); `S` total strong generators; `D` max Schreier-tree path length; `P`
provenance nodes; `C_build` one deterministic closure build (chain.c `build_run`/`closure`):
it processes `Σ_i r_i·s_i` Schreier candidates (spec §9.2), each sift costing
`O(Σ_j D_j · n)` dense work, so `C_build = O((Σ r_i s_i)·b·D·n)` plus provenance.
`C_verify` is of the same order (verify.c re-sifts every residue and re-derives `P`).

Ranking summary (gain/effort): F-02 (drop G-stage least-minimiser tail) and F-08
(cheap χ, one sift) are small; F-01 (one ordered base per descent/tuple_min/enumeration,
zero per-step rebuilds) is the largest gain at moderate effort; F-03 (incremental insert,
verify once) next; then F-04/F-05 (transporter and memory constants), F-06 (strong-generator
filter), F-07 (rule-2 `level_in` witness), F-10 (caches), F-09 (randomised constructor) last.

## 2. Findings

### F-perf-group-01 — One full chain rebuild per step of `tuple_min`, per step of the coset descent, per enumerator node; one ordered base makes all three rebuild-free after a single rebase
Severity: high. Category: performance.

**Evidence (reasoned).** Three loops each pay `canon_bsgs_rebase` = copy the level's strong
generators flat (`s`×`n` words) + full `canon_bsgs_build` (chain.c:680–716, cost `C_build`):

- `tuple_run` (chain.c:997–1011): at each of `len+n` positions whose target `b=min(a^H)` is
  not the current base point, one rebase. Called once per G-stage sweep of every P1 node
  (`src/refine/p1.c:344`) and once per leaf (`src/search/p1_tree.c:205`), so the §12.1 ledger
  term is `Σ_nodes sweeps · O(n · C_build)` — multiplicatively on top of the search tree.
- Coset descent `step`/`ensure_base` (least.c:144–164, 203): one rebase per point `v` moved
  by `J'` that is not the current base point; ≤ `log2|J|` strict descents per call
  (least.c:13–17), each `C_build` of a shrinking group.
- Enumerator `visit` (enumerate.c:125–141): one rebase per internal node whose least moved
  point `a` is not the base point, **plus** one full `canon_coset_least` descent (with its
  own rebases) per child `b` to get the least `t_b`. Internal nodes are `Θ(|G|)` for
  branching ≥2, so enumeration is `O(|G| · C_build)` before any consumer work — this, not
  the `|G|` leaves, is why T3 cases stall (HANDOFF open item 4 is about P1, but every
  §8.2 objective and every deterministic witness pays this enumerator).

**Recommendation / algorithm.** In all three loops the points being stabilised are visited
in a *known order*, so a single chain whose base is a subsequence of that order serves the
whole loop with `level += 1` only:

- *Coset descent.* `descend` (least.c:221–243) stabilises constrained points in increasing
  `v`, then all points in increasing `v`. One rebase to base order
  *(constrained points increasing, then the rest increasing)* aligns both phases: in phase 2
  a constrained `v` is already fixed (orbit length 1, `step` returns at least.c:200–201) and
  unconstrained points arrive in base order. Rebuilds per descent: `≤log2|J|` → **1**.
- *Enumerator.* §8.1's `a` is the *smallest atom moved by H*. With one chain rebased to the
  increasing order `(0,1,…,n−1)` (subsequence form below), at any suffix `H=K_lv` the least
  moved point is the first later base point with `orbit_len>1` (levels with `orbit_len=1`
  satisfy `K_{i+1}=K_i`, since every element fixes `b_i`), so `visit` advances `lv` past
  singleton levels instead of rebasing (enumerate.c:125–131 deleted), `H_a` is the next
  level, and each `t_b` descent inherits base order `(a, then increasing)` — rebuild-free by
  the bullet above. Enumeration: `O(|G|·C_build)` → `O(C_build + |G| · descent)`, descent
  `O(n · D · n)` worst (≤ n steps × transporter) with no closure work.
- *Tuple minimum.* Current form stabilises *image* points `b_i`, whose order is not known
  ahead. Reformulate symmetrically (derivation): rebase once to base order
  *(entries of `L` in order, deduplicated, then remaining points increasing)*. Maintain
  `g = h·t` with `h` ranging over the suffix `G_(L[0..i−1])`: `L[i]` is the level-`i` base
  point, its possible images are `{t[o] : o ∈ orbit_i}` (the stored orbit — `orbit_min`'s
  BFS at chain.c:914–948 disappears), choose `o` minimising `t[o]`, take the level transporter
  `w` with `L[i]^w=o`, set `t ← w t`, `level += 1`. This is entry-by-entry minimisation of
  `L^g`, hence the same `M` and the same unique/least `t` as §7.2 ("Intermediate transporter
  choices cannot change t", spec §7.2 ¶2); `G_M` for the orbit ids is still read at step
  `len` exactly as now (chain.c:983–986). Rebuilds per `tuple_min`: ≤ n → **1**.

Memory caveat and fix: forcing prefix `(0,…,n−1)` through today's `build_run` creates `n`
levels × `5n` words (append_base_point, chain.c:104–117) = `20n²` bytes — a regression for
short-base groups (e.g. a cycle `C_n`: current depth 1). So parameterise policy 2 (chain.c:7–8,
395–408): base extension picks the least moved point *in a prescribed order* instead of the
numerically least. Depth then stays the true base length (each appended level at least
doubles the represented subgroup bound as now), the base is an increasing subsequence of the
prescribed order, and the loops align levels by scanning base points, not by counting.
`canon_bsgs_bytes`/admission (api.c:601–603 charges 4× group bytes) is then unchanged in form.

Determinism/bytes: §7.2's `t_L` is unique; §8.1's traversal is defined by `H`, sorted orbits
and least `t_b`, all representation-independent; §9.4 descents return least coset elements.
Chain base is representation only (§5, §9.2 "reuse is optional"), so **all output bytes,
traces, quota visit counts and provenance obligations are preserved**; transient chains stay
unverified exactly as today (S3 review item 2). No new work-policy ID: P1/enumerator node
counts do not change (§11.1 counts traversal nodes, not group work).

**Remediation plan.** (1) chain.c: add an `order` parameter (permutation or NULL=numeric) to
`canon_bsgs_build`/`rebase` used only by policy-2 base extension; keep the verifier unchanged
(it checks any base). (2) least.c: build the ordered chain once in `canon_coset_least`/
`least_outside`; `step` advances `lv` to the level based at `v` (skipping fixed/singleton
levels) — delete `ensure_base` from the hot path. (3) enumerate.c: rebase once at
`canon_coset_enumerate`, thread `(chain, lv)` only. (4) chain.c `tuple_run`: rewrite in the
`g=h·t` form. Tests that prove it: existing byte-identity tiers must be unchanged (chain vs
explicit backend on `tuple_min` t and orbit ids; enumerator leaf sequence and visit counts;
S4-notes counters `rebuilds` should drop to ≤1 per descent / 1 per enumeration — assert in
`test_enumerate.c`, `test_coset_least.c`); add a short-base large-`n` case (e.g. `C_n`, n a few
hundred) asserting depth stays small. Risk: medium (three loops restructured; the tuple_min
reformulation needs the derivation above re-checked against both backends). Effort: ~1 slice.

### F-perf-group-02 — The G stage pays for the *least* minimiser although §7.1 needs any minimiser
Severity: medium. Category: performance.

**Evidence (reasoned).** `canon_group_ops.tuple_min` contracts "the least such t as an image
array" (group.h:38–46), so `tuple_run` processes `L` followed by all of `0..n−1`
(chain.c:967–982) — up to `n` extra steps, today each a possible rebase. The only callers are
the G stage (`src/refine/p1.c:344`, `len=f≤n`) and leaves (`src/search/p1_tree.c:205`,
`len=n`, where the minimiser is unique and the tail is already a no-op once `H` is trivial).
Spec §7.1 says "choose any u in G with F^u=M" and proves the pulled-back orbits independent of
the choice (§7.1 last ¶: `u⁻¹v∈G_M` "preserves every one of its orbits"); the G stage consumes
only `sig(v)=orbit[t[v]]` (p1.c:348–350), invariant under that freedom. The deterministic
witness is computed separately via stabiliser + coset descent (objectives.c:773–798), not from
this tail.

**Recommendation.** Split the op: `tuple_min_any` (deterministic procedure, any minimiser +
orbit ids) for the G stage; keep the least form only where a caller needs it (today: none
outside tests). With F-01 this is up to `n` cheap steps saved per G sweep; without F-01 it is
up to `n` rebuilds saved per sweep. Output bytes/traces unchanged (sig identical); the only
observable change is the backend-internal `t` in tests that compare it directly.

**Remediation plan.** group.h contract split; chain.c `tuple_run` stops at `len` for the
`_any` form (after computing orbit ids); explicit.c keeps returning its scan's first least —
tests must then compare `L^t` and orbit ids across backends, not `t`, for `len<n`
(S3-notes:158 currently compares `t`). Risk: low. Effort: small. Do before/with F-01.

### F-perf-group-03 — `canon_bsgs_insert_verified` rebuilds *and fully verifies* on every insertion (stabiliser, §9.4 rule 2, `A_known`)
Severity: high. Category: performance.

**Evidence (reasoned).** chain.c:737–759: each non-member insertion appends to `gens` and
calls `canon_bsgs_build_verified` = from-scratch closure over all generators so far + full
`canon_bsgs_verify` (all Schreier residues re-sifted, provenance re-derived: `C_build+C_verify`).
Insertions are ≤ `log2|A|` (doubling), but costs grow with `k`, so a stabiliser run pays
`Σ_{k≤log|A|}(C_build(k)+C_verify)`. Callers: §8.2 stabiliser consumer (objectives.c:175, 220 —
hence STABILISER, both transporters' deterministic modes, labeling, signed, deterministic
witness), rule-2 `K` (group_stream.c:151, plus its initial verified `K=1` at :142), and the
search's `A_known` (symmetry.c:98, on the P1 hot path under policies 0x0002/0x0003).

**Recommendation.** The closure is already incremental inside `build_run` (sift → insert residue
→ `closure` restart, chain.c:615–643). Expose it: `canon_bsgs_insert(chain, g)` appends `g` to
`chain.inputs` (so provenance INPUT indices and the verifier's `inputs_match`,
verify.c:234–242, stay meaningful), sifts, inserts the residue with provenance and runs closure
— maintaining by the same argument as `build` that the chain represents exactly
`<inputs>`. Verify **once**, when the chain becomes evidence (before `subgroup_verified`,
Group bytes, or certificate use), not per insertion. Membership answers during enumeration
then rest on the constructor, exactly as unverified transient rebases already do today
(chain.h:137–144). Complexity: total insertion work `O(C_build(final))` amortised instead of
`log|A|` rebuilds; one `C_verify` per completed subgroup instead of `log|A|`.

Determinism/bytes: the resulting chain (SGS, tree edges) will generally differ from the
rebuild-from-scratch chain. No CDAG output depends on it: Group(H) depends only on `H`
(§9.4), orbit pruning on the group `A_known` (orbits, docs/pruning-rules.md), node counts
unchanged — no new work-policy ID. **Certificate bytes may change** (the automorphism table
reads transporters off `A_known`'s Schreier tree, symmetry.c:237–260; `canon-check` verifies
each by membership+action, so validity is preserved, byte-identity of certificates is not —
nothing in the spec freezes certificate bytes, but re-sealed fixtures would need regeneration).
Spec tension to record, not resolve: §9.2 "base change rebuilds/**certifies**" and §9.1 "exact
verification is mandatory" — see Ambiguities; the proposal keeps mandatory verification at
every point a chain certifies an answer, matching the existing transient-chain reading.

**Remediation plan.** chain.c: factor the per-generator loop body of `build_run` into
`insert_one`; `build_run` and new `canon_bsgs_insert` share it. objectives.c/group_stream.c/
symmetry.c: replace `insert_verified` with insert + one final `canon_bsgs_verify` against the
accumulated gens (failure ⇒ `INTERNAL_ERROR`, as now). Keep `insert_verified` as a wrapper
during transition. Tests: `stab_builds`/rule-2 `k_builds` counters become "verifications ≤ 1";
byte-identity of all objective outputs on existing tiers; certificate accept/reject harness
unchanged in verdicts. Risk: medium (provenance/input bookkeeping). Effort: ~half a slice.
Independent of F-01; combined they remove essentially all rebuild traffic except group creation.

### F-perf-group-04 — Transporters are rebuilt densely `O(D·n)` at every use; several callers need only pointwise or in-place application
Severity: medium. Category: performance.

**Evidence (reasoned).** `transporter_into` (chain.c:229–245) does `D` dense composes + a memcpy
each. Uses: closure per orbit point per batch (chain.c:476 — rebuilt again after every restart
that appends generators to that level, since only `(p,gi)` pairs are memoised via
`schreier_done`, not `t_b`); tuple_min per step then *inverts and composes again*
(chain.c:1014–1017: `D·n + 3n` where applying the inverse path directly to `t` — the existing
`times_inverse_path` pattern, chain.c:273–287 — is `D·n`); descent step similarly
(least.c:208–210); verifier reconstructs every `t_b` twice: `transversal_images`
(verify.c:393–405) then again inside `schreier_residues` (verify.c:414–415). Also BFS trees are
only breadth-first at first build: `level_extend` keeps old edges (chain.c:143–148), so a later
generator offering a shorter path to an old point is never taken — `D` can exceed the BFS depth
for the final `S_i`. Sift cost `O(b·D·n)` inherits this.

**Recommendation.** (a) Ping-pong buffers instead of compose+memcpy (the verifier already does,
verify.c:165–170). (b) Apply paths in place where the dense transporter is immediately composed
(tuple_min, descent, symmetry transporter) — saves the inverse+compose+copy passes, same
asymptotics, ~`(D+3)/D` constant. (c) Fuse verifier conditions 6+7 to share each `t_b`, or make
condition 6 pointwise: `t_b[b_i]` needs only the path applied to one point, `O(D)`;
condition 7 still exercises full transporters and rejects a wrong product order
(verify.c:421 checks the residue fixes `b_i`), so the proof sketch (verify.c:19–28) survives
with its wording adjusted. (d) Bounded dense-transporter cache per workspace for hot levels —
explicitly sanctioned ("bounded word/jump/dense-transporter caches", §9.2; charge §11.2
`M_caches`, bound in the admission plan; §9.3's 400 MB warning for 1000 transversals at
n=100 000 is why it must be bounded and keyed by immutable chain identity). (e) Shallow-tree
refresh **(lead)**: periodically recompute a level's tree by BFS over final `S_i` during build
only — changes tree edges, hence transporters, hence certificate bytes; outputs unaffected.
All of (a)–(d) preserve every output byte and every verifier verdict ((c) needs the proof-sketch
note). Effort: (a)(b) trivial, (c) small, (d) medium. Rank after F-01/F-03, which remove most
transporter traffic anyway.

### F-perf-group-05 — Chain memory: `5n` words per level forever, gens+inverses+inputs triple storage
Severity: medium. Category: performance (memory).

**Evidence (reasoned).** Each level allocates one `5n`-word block (chain.c:104–117):
orbit, orbit_pos, parent_point, parent_gen, **schreier_done** — the last is construction-only
(chain.h:44–48) yet lives for the chain's lifetime and is charged by `canon_bsgs_bytes`
(chain.c:1075: `5·n·4` per level), inflating the admission plan. `parent_point` is derivable:
with the classic Schreier vector over *points*, parent of `y` is `invs[gen][y]` (one O(1) lookup
in the already-stored inverse table). Strong generators are stored three times at group level:
`gens`+`invs` (chain.h:67–68) plus `inputs`, and the handle copies inputs again
(group.c:55–77), plus a third copy for signed groups (group.c:79–112). At `S` strong generators
that is `≥ 2·S·n·4` bytes before levels; spec §9.3's arithmetic (400 kB/dense perm at n=100 000)
makes this the binding constraint long before time at large `n`.

**Recommendation.** (a) Allocate `schreier_done` separately, free at end of build (conjugate
already tolerates NULL, chain.c:848): −20% level memory, plan shrinks honestly. (b) Drop
`parent_point` (−20% more), deriving parents via inverses in `transporter_into`/
`times_inverse_path`/provenance walks — same complexity, one extra table read per edge.
(c) Sparse moved-support rows for strong generators (§9.3 invites "sparse support"): residues
fix a base prefix but support is not bounded in general, so keep dense as the fallback and store
support lists only where `σ ≪ n`; gains are in point/tuple application and memory, **not** dense
composition (still `O(n)` writes) — be honest that this is a memory/locality play. (d) Narrower
dense width at small `n` would halve bandwidth but §9.3 states "Dense permutations use uint32
images" — see Ambiguities; do not do silently. (a)(b) preserve all bytes and the verifier
(it reads `parent_point` today, verify.c:346–352 — update it to derive the parent the same way;
its independence is about *not sharing code*, the layout is shared by definition, chain.h).
Effort: (a) trivial, (b) small-medium (touches verifier + conjugate + certificate chain
serialisation — check `src/search/certificate.c` chain embedding before landing).

### F-perf-group-06 — No strong-generator filter: `S` (hence `Σ r_i s_i`, memory, sift width) is unbounded by anything but subgroup-chain length
Severity: medium. Category: performance.

**Evidence (reasoned).** Every non-identity Schreier residue inserts a new strong generator
(chain.c:496–526); nothing ever removes one. S8-notes:32 already records the only bound is
`depth·log2|G|`, "uselessly loose". `S` multiplies closure candidates (`Σ r_i s_i`), orbit BFS
(`level_extend` iterates all `s_i` per point), `orbit_ids` (`O(S·n)` union-find, chain.c:882–892),
`level_in` (F-07) and memory (F-05).

**Recommendation (lead).** A Sims/Jerrum-style filter keeps at most one generator per
`(level, first moved pair)`-type key, bounding `S = O(n²)` total in the worst formulation and
far less in practice; filtered combinations are recorded as provenance PRODUCTs so §9.1
derivations still verify. The verifier accepts any SGS meeting its listed conditions, so no
verifier change is needed; the filter policy must be a deterministic function of the insertion
sequence (it is, if keys/order are fixed) — chain representation changes, outputs do not (same
certificate-bytes caveat as F-03). This is the standard cure for intermediate-generator blow-up
in deterministic Schreier–Sims **(lead — no repo citation; the proof that filtering preserves
`<S_i>` at each level must be written into the code comments/slice notes, not imported)**.
Effort: medium; do after F-03 (the filter hooks into the same `insert_one`).

### F-perf-group-07 — §9.4 rule 2: `level_in` re-sifts every generator of `J'` at every point; descent sorts by comparison
Severity: low. Category: performance.

**Evidence (reasoned).** `outside_descend` calls `level_in(cur,lv,k)` at every `v`
(least.c:351) and once for the child group (:375); each call sifts **all** `s_lv` generators
through `K` (least.c:314–323), i.e. `O(n · s · sift_K)` per `least_outside`, times `k≤log2|H|`
rule-2 steps (group_stream.c:144–156). Orbit candidates are comparison-sorted by image
(least.c:367–374, enumerate.c:136) at `O(r log r)` with an `O(1)` key that is a permutation
image — a counting/radix pass is `O(n+r)`.

**Recommendation.** Cache a *witness*: when `level_in` fails, remember the generator `g∉K`;
at the next (smaller) `J''`, test that one generator first — while it survives in `J''` the
answer stays "not contained" in one sift. `K` is fixed during the descent, and `J'` is nested
decreasing, so this is sound and typically makes `level_in` `O(1)` sifts per level. Also
short-circuit by order: `|J'|>|K|` ⇒ not contained (suffix orders are `O(b)`, chain.c:217–221)
— check before sifting. Radix/counting sort for the two orbit sorts. Bytes unchanged (the
discard rule "C⊆K iff r∈K and J≤K" itself is untouched, §9.4). Effort: small.

### F-perf-group-08 — Signed route: χ costs one–two sifts through the *(n+2)*-point lift per non-member hit, plus a repeated sift inside insertion
Severity: medium. Category: performance.

**Evidence (reasoned).** Per stabiliser hit (objectives.c:198–225): sift through `A_known`; if
non-member, `character` sifts `lift(r,+1)` through the lift chain and, for odd `r`, the whole
sift again for `lift(r,−1)` (group.c:198–205); if even, `insert_verified` sifts through
`A_known` **again** (objectives.c:218–220 admits this) and rebuilds+verifies. In deterministic
(exhaust) zero mode every odd element of `A` is a non-member forever (odd hits are never
inserted), so `|A|/2` hits pay A-sift + up-to-2 lift sifts on degree `n+2`. The lift also
doubles signed-group creation (build+verify on `n+2`, chain_backend.c:166–191) and memory.

**Recommendation.** Precompute signs on `G`'s own chain once per signed group: χ of each strong
generator (one lift evaluation each at creation, `O(S)`), then χ of every tree transporter
`t_b` by DP over the Schreier trees (`O(Σ r_i)` scalar work). χ(g) for any later `g` is then
*one sift through G's chain on degree n* recording `(level,pos)` — `sift_record` already
records exactly this (chain.c:291–322) — times `O(b)` sign multiplies; residue identity
re-proves membership. This replaces 1–2 lift sifts with 1 narrower sift everywhere `character`
is used (signed consumer, `signed_nonzero` objectives.c:568, witness checks :921, api). It is
exact because χ is a homomorphism — which is what creation-time lift validation certifies
(§8.4, chain_backend.c:167–184); that validation is **kept**. Going further, §8.4 explicitly
permits the alternative certification: "A complete chain/kernel calculation certifies this, **or
complete relation/provenance checking against a certified presentation may do so**" — signs
propagated through the verified provenance DAG of `G`'s chain plus a sign check on every
verified Schreier relation would let the lift chain be dropped entirely (creation ~halved).
That step needs its own proof that the checked relations present `G` (the verifier's conditions
do give both inclusions, verify.c:19–28, but the sign-presentation argument must be written
down); treat as a second, separately-gated step, with the lift kept as a differential test.
Also merge the redundant re-sift: an insert API that consumes the residue/stop-level from the
caller's sift (natural under F-03) removes one A-sift per even non-member. All sign values,
zero/nonzero verdicts and bytes unchanged; `characters` metric semantics (count of χ
evaluations) unchanged if the counter stays at the call sites. Effort: sign tables small;
provenance-validation replacement medium with a proof obligation.

### F-perf-group-09 — Randomised Schreier–Sims front end (spec-permitted) is unexplored; do the deterministic fixes first
Severity: note. Category: performance.

**Evidence.** Spec §9.1 ¶2: "A randomised constructor may propose the same data but exact
verification is mandatory." The deterministic closure's cost driver is the `Σ r_i s_i` candidate
product; randomised construction with random subproducts/product replacement **(lead)** usually
reaches a complete chain after few candidates, leaving `canon_bsgs_verify` — already independent
— as the certificate, with deterministic closure as fallback on rejection.

**Constraints before anyone builds it.** (i) RNG must be a deterministic function of the input
generators if certificate bytes (which embed chains, certificate.c) are to stay reproducible —
the spec does not state this; see Ambiguities. (ii) Provenance: random elements must carry
straight-line words over the inputs (product-replacement maintains these **(lead)**), else
§9.1 derivation checks fail. (iii) `|G|≥2^64` refusal must stay exact — the verifier's order
product (verify.c:449–458) covers it only if the proposal is complete; an incomplete proposal
must fall back, never refuse. (iv) Quota/status semantics unaffected (construction is outside
§11.1's traversal quota). Expected gain is real but unquantified without benchmarks; F-01/F-03/
F-06 shrink the deterministic cost first and may change the trade-off. Rank: last.

### F-perf-group-10 — No reuse of verified group work across objectives in a workspace (stabilisers, conjugates, rebased chains)
Severity: low. Category: performance.

**Evidence (reasoned).** The complete stabiliser of the same `(G,x)` is rebuilt from enumeration
by TRANSPORTER_ONE-deterministic (objectives.c:318), TRANSPORTER_COSET (:362), labeling per `ρ`
(:489; S6-notes:153 admits "recomputed for every ρ"), signed (:741), deterministic witness
(:788); `G'=ρ⁻¹Gρ` is re-conjugated per labeling solve (:462, cost `O(chain)` via
chain.c:783–859 — already rebuild-free); `tuple_min` rebases are per-call (F-01 makes them
per-call single). §12.3's small-batch regime ("Reuse validated immutable groups…no per-call
chain rebuild") is exactly where this bites.

**Recommendation.** Workspace-level exact caches keyed as §9.3/§16 require ("exact semantic
group, ordered base/fixed tuple, coordinate maps and operation version, never just its order or
generator hash"): in practice `(group handle identity, object canonical stream bytes) → verified
A chain`, `(group, ρ array) → conjugate handle`, `(group, prescribed base order) → rebased
chain`. Bounded (e.g. last-one or small LRU) and charged in §11.2 `M_caches` + admission plan,
or `peak ≤ plan` breaks. Note what does **not** work: seeding `A_known` from P1-discovered
automorphisms cannot shorten the §8.2 enumeration, because completeness *is* the exhaustion and
its visits are the quota — savings would be only the `≤log|A|` insertions (already cheap after
F-03), and completeness-from-P1 is the open SIGN-COVER-style obligation (§8.4), not a shortcut.
Bytes/statuses unchanged (groups are unique as sets). Effort: medium (key/lifetime discipline
in the workspace). Rank after F-01/F-03.

### F-perf-group-11 — Sift is dense from the first level; failing sifts pay full `O(b·D·n)`
Severity: low. Category: performance.

**Evidence (reasoned).** `sift_record` (chain.c:291–322) applies each level's inverse transporter
to the full `n`-array before knowing whether a later level fails. Membership-heavy paths with
many *failing* sifts: `level_in` (F-07), A_known pre-sift of early stabiliser hits, closure
residues that insert. A point-image first pass — track only the images of remaining base points,
applying transporter paths to single points, `O(b²·D)` point ops — detects failure at level `j`
without any `O(n)` work, then materialises the dense residue by replaying the recorded steps
only when it is needed (insertion, terminal identity). Honest bound: this wins iff `b ≪ n`
(both are `O(b·D·n)`-vs-`O(b²·D)`; for `Sym(n)`, `b=n−1`, it does not win) and only for sifts
that fail or stop early; successful membership still needs the terminal identity check over all
`n` points. Recommend only as a measured follow-up for short-base workloads; no semantic change
(same recorded steps, same residue). Effort: medium. Conjecture-level benefit until profiled —
labelled as such.

## 3. Spec ambiguities (quoted, not resolved)

1. §9.3: "**Dense permutations use uint32 images**; tagged identities, sparse support and exact
   symbolic products avoid unnecessary n-entry materialisation." Is a narrower dense width
   (uint16 when `n<65536`) a violation, or is the sentence describing the current physical
   choice? This gates F-05(d).
2. §9.1: "**A randomised constructor may propose the same data but exact verification is
   mandatory.**" Must the proposal be a deterministic function of the input (affecting
   certificate bytes, which embed rebased chains), or is any verified proposal acceptable run
   to run? This gates F-09's seeding rule.
3. §9.2: "base change **rebuilds/certifies** for the requested ordered base (reuse is optional)"
   together with §9.1 "exact verification is mandatory". The code's standing reading (S3 review
   item 2; chain.h:137–144) is that transient chains feeding tuple minima/descents need not be
   verified. F-03 extends that reading to incremental `A_known`/rule-2 chains verified once at
   completion. Whether "certifies" demands verification of *every* intermediate chain whose
   membership answers steer a result is not stated.
4. §8.1: "**Any transporter t_b suffices for coverage; the least choice freezes reference
   traversal for budgets/certificates.**" May a production enumerator use Schreier-tree
   transporters (cheap, F-04) when no certificate is requested? Node *counts* are unchanged,
   but leaf order, first-hit ANY-mode witnesses, and which leaves precede a `CAPACITY_LIMIT`
   all change. F-01 deliberately keeps least `t_b`; anything cheaper needs this answered.

## 4. Things checked and found sound (by reading; nothing was run)

- Product/inverse conventions (spec §3) are implemented independently in chain.c
  (`transporter_into`, `times_inverse_path`) and verify.c (`transporter`,
  `times_transporter_inverse`) with mirror-image derivations; sift/membership is a single
  rule (`canon_bsgs_contains_scratch`) everywhere including the lift.
- Incremental Schreier checking via `schreier_done` (chain.c:456–463): sound because
  `level_extend` never changes a stored tree edge, so previously checked residues keep their
  value and their target subgroup only grows.
- Insertion counts: `insert_verified`/stabiliser/rule-2 insertions at least double the subgroup
  order, so rebuild *counts* are `≤log2|·|` (the cost complaint in F-03 is per-rebuild, not count).
- Coset descent constraints-first order (least.c:215–220) and the `J'_v t_o r'` update justify
  least-element correctness; `least_outside`'s discard rule is exactly §9.4's iff.
- `canon_bsgs_conjugate` is `O(chain)` relabelling with the verified flag carried by a correct
  isomorphism argument (chain.h:163–176) — already the right algorithm; no finding.
- Rule-1 factorial-overflow decision (group_stream.c:39–45) is exact given this release's
  uint64 orders; `canon_group_bytes_bound` matches §9.4's `k≤⌊log2|H|⌋`.
- Signed `A_known`-before-χ (objectives.c:179–197): the "A_known stayseven" invariant is
  correctly argued (generators checked even before insertion; χ a homomorphism), so skipping χ
  on members loses no odd hit.
- `orbit_ids` ranking by least point (chain.c:863–904): the single-pass relabelling invariant
  `parent[x]≤x` is correctly maintained; cost remark is F-06 only.
- Order-overflow refusal during construction (chain.c:31–35, 449–451): the orbit product is a
  lower bound on `|<inserted>|` throughout (inclusive nesting), so `CAPACITY_LIMIT` fires iff
  `|G|>2^64−1` — and usefully bounds work on huge groups.
