# Lane A-search — correctness review of the P1 canonical search core

No test suites were run, per `docs/review-2026-11/BRIEF.md`. Evidence below
is by reading unless a finding says it was run; the one run made
(A-search-01) was two one-second `canon-cli` invocations under `nice -n 19`
against the already-built tree (`make` reported "Nothing to be done"),
output kept at `/tmp/canon-review-A-search/genset-quota.txt`.

## 1. Scope

Read in full:

- Spec (normative, `docs/specification.md` v2.2): §2.1, §3 (incl. §3.1, §3.2,
  deterministic witness incl. decision D1), §4.1–4.4 (as they bear on leaf
  keys/orders), §5 (semantic/representation independence), §6, §7 (profile
  P1, trace bytes, leaf map and proof, §7.3 pruning/root shortcuts, §7.4
  golden cases), §8 (§8.1 enumerator, §8.2 consumers, §8.3, §8.4 signed),
  §9.1–9.4 (as used by the G stage, `Group` bytes, coset least element),
  §10, §11.1 (work policies/quota), §14.3 R2 (as cited by the prune).
- Code, every file: `src/partition/partition.{h,c}`,
  `src/refine/p1.{h,c}`, `src/search/p1_tree.{h,c}`,
  `src/search/objectives.{h,c}`, `src/search/certificate.{h,c}`,
  `src/symmetry/symmetry.{h,c}`.
- Supporting code the lane depends on (read in the relevant parts): the
  `tuple_min`/orbit-id and rebase paths of `src/bsgs/chain.c` and
  `src/bsgs/explicit.c`, `src/object/object.c` (root kinds, initial keys,
  action), `src/coset/coset.h`, and the solve routing in `src/api/api.c`
  (`solve_canonical`, `solve_p1_objective`, work-policy and quota wiring).
- Briefs/notes: `docs/pruning-rules.md` (rule P1-ORBIT-1, policies
  `0x0002`/`0x0003`), `docs/slices/S7-notes.md`, `docs/slices/S7b-notes.md`,
  `docs/slices/M0a-notes.md` (incl. their 19 October 2026 independent-review
  sections and decision D1), `README.md`, `HANDOFF.md`, `CLAUDE.md`.

Not covered: the checker (`checker/`) beyond what the certificate writer
promises it; the CDAG/DAG internals of `src/object/dag.c` and the encoders
beyond the dispatch in `src/object/object.c`; the full BSGS constructor and
verifier (only the suffix/rebase/orbit/tuple-min contracts this lane
consumes); `src/coset/least.c`'s descent body (its least-element contract
was taken from `src/coset/coset.h` and its callers); tests
(not run, per the brief); performance except where a work item changes a
counted traversal.

## 2. Findings

### A-search-01 — Pruned quota counts (hence `CAPACITY_LIMIT`) depend on the generating set as given, not only on the semantic input

- Severity: **high** (as a spec-conformance question; no wrong trace/bytes
  claimed). Category: spec-conformance / soundness of the capacity contract.
- Evidence (**ran**: the two presentations below through `canon-cli`
  `p1-subset --prune explicit --max-nodes 5`, output in
  `/tmp/canon-review-A-search/genset-quota.txt`; plus code reading):
  - Source (i) of `A_known` is "the input generators of G that fix x"
    (`src/search/p1_tree.c:486-519`, `root_automorphisms`; insertion through
    `src/symmetry/symmetry.c:79-105`). Which subgroup this generates is a
    function of the *presentation*, not of `(G, x)`: for `G = Sym(4)`,
    `x = {0}` (`A = Sym{1,2,3}`), the documented generating set
    `⟨(0 1 2 3), (0 1)⟩` has no generator fixing `x`, so `A_known = 1` and
    policy `0x0002` explores all 10 nodes (`docs/pruning-rules.md:57`). The
    generating set `⟨(1 2), (2 3), (0 1)⟩` of the same `G` has two generators
    fixing `x` generating all of `A`, so `A_known = A` before the root is
    visited and the root's children are pruned by their `A`-orbits: strictly
    fewer explored nodes for the same semantic input, objective and
    descriptor. A quota between the two counts therefore returns
    `CAPACITY_LIMIT` for one presentation and `COMPLETE` for the other.
    Observed exactly that at quota 5, policy `0x0002` (`--prune explicit`):
    gens `1,2,3,0;1,0,2,3` → `CAPACITY_LIMIT` (exit 3); gens
    `0,2,1,3;0,1,3,2;1,0,2,3` (the Coxeter transpositions, also generating
    `Sym(4)`) → `COMPLETE`, canonical bytes for `{3}`, witness `3,0,1,2`
    (exit 0). Trace/bytes are presentation-independent in both runs; only
    the status at this quota differs.
    Policy `0x0003` inherits the same dependence (it starts from the same
    source-(i) subgroup), and the `witness_mode = ANY` witness (least `t`
    among *explored* leaves, `src/search/p1_tree.c:230-248`) likewise varies
    with the presentation — the latter is explicitly allowed by spec §3
    ("Without the deterministic mode any verified witness may be returned"),
    so this finding is only about the *status/count*, not the witness.
  - `docs/pruning-rules.md:55` states the dependence openly: the count "is
    determined by `A_known` (a function of the generators as given and `x`)".
  - Against that, spec §5 (`docs/specification.md:164-166`): "if
    decode(R)=decode(S), each semantic stage returns equal interpreted
    results … This excludes SGS/base choice, generator order, … from
    mathematical choices"; and §11.1 (`docs/specification.md:365`):
    "`CAPACITY_LIMIT`, a function of semantic input, objective and
    descriptor". The §11.1 quota paragraph itself says only "a function of
    input, objective and descriptor" (`docs/specification.md:367`), which is
    the ambiguity: "input" as presentation vs. as semantic value.
  - This is the quota analogue of the already-open maintainer question for
    the admission *plan* (HANDOFF open item 6(i): the plan "reads the
    generator list and the backend, so two generating sets of one group …
    can give different verdicts at a budget between their plans"). The notes
    I read do not record the same question being decided for the work-policy
    *node count*; decision D9/§11.1 v2.1 settled that the count is the
    explored traversal under a named policy, not that the policy may be
    presentation-dependent.
- Recommendation: this needs a maintainer/spec decision, not a silent code
  change (spec-ambiguity item SA-1 below). If "semantic input" wins, source
  (i) as defined cannot define a work policy's count.
- Remediation plan:
  - Option A (spec change, cheapest): amend §11.1 to say the count under
    `0x0002`/`0x0003` is a function of the presentation (generator list as
    given), and state that only `0x0001` has a presentation-independent
    count; align §5 by carving capacity *statuses under pruning policies*
    out of "interpreted results". Files: `docs/specification.md` §5, §11.1;
    `docs/pruning-rules.md` §2 (already accurate). Effort: editorial, but it
    weakens the §5 contract and must be a recorded maintainer decision.
  - Option B (code change): make source (i) presentation-independent, e.g.
    seed `A_known` from a canonical generating set of `A ∩ ⟨input gens⟩`
    derived from `G` and `x` alone — in the limit the complete `A`, which is
    exactly the §8.2 stabiliser enumeration the prune exists to avoid, so
    any cheaper canonical seed (e.g. the strong generators of the verified
    chain of `G` that fix `x`: the chain is a deterministic function of the
    *sorted normalised input*, `src/bsgs/chain.c` §9.1 policies — still
    presentation-dependent unless the chain itself is canonical for `G`)
    needs its own proof of presentation independence. This likely needs a
    new work-policy ID (counts change, §11.1). Effort: large; do not attempt
    without the decision.
  - Tests that would pin whichever reading is chosen: a metamorphic C test
    solving `{0}` under `Sym(4)` from both generating sets above under
    `0x0002`/`0x0003` at quotas bracketing the two counts, asserting the
    decided behaviour (equal statuses, or explicitly documented unequal
    counts); the same pair in deterministic-witness mode asserting identical
    trace/bytes/witness (already the tiers' claim).

### A-search-02 — Internal contract comments for the signed objective still state the pre-D1 deterministic zero witness

- Severity: **low**. Category: documentation / maintainability.
- Evidence (read): `src/search/objectives.h:114-127` describes
  `canon_obj_signed` saying, for `deterministic`, "a zero certificate is
  unchanged", and the fast path as "the first odd generator (input order) is
  the zero certificate" without the mode qualification. Since decision D1
  (spec v2.2 §3; `docs/slices/M0a-notes.md:76-83`) the deterministic witness
  of a certified zero is the *least odd element of A*, and the code does
  exactly that (`src/search/objectives.c:698-704` fast path via
  `least_odd_of_group`; `:741-744` enumeration route exhausted with
  `least_odd` over `A+`). The header — the contract a future caller or
  reviewer reads first — asserts the superseded M0a reading.
- Recommendation: update the header comment to the v2.2 rule for both routes
  (least element of `A+a`; on the singleton route `A+ = ker χ` by Schreier
  generators over the transversal `{1, a}`), keeping "first odd" only for
  the ANY mode.
- Remediation plan: comment-only edit in `src/search/objectives.h` (the
  `canon_obj_signed` block and, for symmetry, check the `canon_obj_labeling`
  block's deterministic sentence, which is current). No test can prove a
  comment; the behaviour is already pinned by `tests/c/test_signed.c` per
  M0a-notes. Effort: minutes. Risk: none.

### A-search-03 — Certificate writer carries a stale comment claiming a failed certificate is silently dropped

- Severity: **low**. Category: documentation.
- Evidence (read): `src/search/certificate.c:129` (orphaned comment above
  `fail`): "Any failure ends the certificate: the search goes on, the result
  carries no certificate." The implemented and documented contract is the
  opposite — S7 decision 6 ("A requested certificate that cannot be written
  fails the solve; it is never dropped silently"), restated in
  `src/search/certificate.h:48-50`, and the code propagates every writer
  status (`fail` deactivates *and* returns the status; every call site in
  `src/search/p1_tree.c` and the refinement observer returns it, e.g.
  `p1_tree.c:371-385`, `:440-467`; `src/api/api.c:1200-1204` even turns a
  complete run without a finished certificate into `INTERNAL_ERROR`). The
  several `canon_cert_*` entry points that return `CANON_COMPLETE` when
  `!c->active` (`certificate.c:209-213`, `:300-304`, etc.) are reachable
  after a failure only if a caller ignored the first failing status, which
  no current caller does — but the stale comment tells a future maintainer
  that ignoring it is the design.
- Recommendation: delete or correct the line-129 comment; optionally make
  the `!active` early returns assert/return the stored failure instead of
  `CANON_COMPLETE` so the invariant does not depend on every caller checking
  the first status.
- Remediation plan: edit `src/search/certificate.c:129`; if hardening is
  wanted, store the failing status in `canon_cert` (in `fail`) and return it
  from the `!active` paths. Test: `tests/c/test_certificate.c` already
  covers writer failures failing the solve per S7-notes item 9; a harden
  change should keep those passing. Effort: small. Risk: low.

## 3. Spec ambiguities

- **SA-1 (§11.1 quota count).** "The count is a function of input, objective
  and descriptor, which names the work policy." (`docs/specification.md:367`)
  vs. "Exceeding that policy yields `CAPACITY_LIMIT`, a function of
  *semantic* input, objective and descriptor" (`:365`) and §5's exclusion of
  "generator order" from mathematical choices (`:166`). Whether a
  presentation-dependent pruned count (A-search-01) conforms cannot be
  settled from the text; quoted here, not resolved.

## 4. Things checked and found sound

(Brief; all by reading, with the derivations re-done, not by running tests.)

- **§7.1 refinement loop shape**: `canon_p1_refine_node`
  (`src/refine/p1.c:301-370`) appends `NODE(depth)`, then per sweep records
  `c0`, runs O against the entry snapshot (signatures fully computed from
  the unmodified partition before any split, `:200-249`), appends
  `STAGE_O`, builds `F` from the post-O partition in partition order, runs
  the G stage, appends `STAGE_G`, and breaks exactly when the cell count
  equals `c0`. One complete no-change sweep is always recorded, including
  `n = 0`. Target-cell choice (`src/search/p1_tree.c:395-403`) is the first
  cell minimising `(size, position)` among size > 1; individualisation
  replaces `C` by `[{a}, C\{a}]` in place (`src/partition/partition.c:127-143`)
  and every child gets a fresh refinement loop on the restored snapshot.
- **O-stage signatures**: index order `((label·k)+j)·2+dir` with labels in
  `B(label)` rank order, loops contributing once out and once in
  (`p1.c:214-232`), duplicates accumulated exactly with overflow →
  `CAPACITY_LIMIT`, sparse comparison equal to the dense lexicographic
  comparison (argument at `p1.c:136-156` re-derived and correct: at the
  first differing sparse index the other dense vector has 0 against a
  positive count). Non-graph roots get the identity split but still record
  `STAGE_O`; an arc-less graph's signature is the empty vector, correctly.
- **G-stage orbit ordering**: both backends rank orbits of `G_M` by least
  point, which is the lexicographic order of the increasingly-sorted orbit
  lists for disjoint orbits (first elements already differ)
  (`src/bsgs/chain.c:873-904`, `src/bsgs/explicit.c:214-247`; the chain
  version's single increasing pass is justified by the `parent[x] ≤ x`
  invariant). `sig(v) = orbit[u[v]]` is the spec's pull-back (`p1.c:348-352`).
  `tuple_min` returns the least minimiser on both backends (explicit: first
  row of the sorted table; chain: §7.2 procedure continued over
  `0..n-1` inside `t·G_M`, `chain.c:960-1027`) — stronger than §7.1's "any u",
  and the pulled-back orbits are the same for any minimiser by the spec's
  own argument, so this choice cannot change traces.
- **Leaf key and witness**: key is `(complete trace, CDAG-2 bytes)`, trace
  first, proper prefix smaller (`src/search/p1_tree.c:186-264`); the
  trace-greater shortcut skips only work that cannot change the minimum or
  produce an implicit automorphism (the incumbent trace only decreases, so
  a skipped leaf can never later tie the final key); equal-key leaves keep
  the lexicographically least explored `t_L`. With a certificate, `t_L` is
  computed and recorded at every leaf, including trace-losing ones.
- **P1-ORBIT-1 realisation (the corrected form)**: decisions are per child
  in cell order; `A_S` is recomputed when the epoch changed *or* the shared
  transient stabiliser no longer belongs to this depth
  (`p1_tree.c:307-315`) — the two defects of the 19 October review are both
  fixed as documented, and I re-derived that between two decisions of one
  node only deeper descendants can run, so `stab_depth == depth` cannot then
  name a different same-depth prefix. Pass 1 prunes by explored siblings'
  orbits (possibly pruning `j` itself), pass 2 by `j`'s own orbit
  (`:316-350`); every pruned child's representative is an explored sibling,
  which is exactly the lemma's hypothesis in `docs/pruning-rules.md:17-23`.
  Under `0x0002` (epoch fixed) the sequential form is decision-for-decision
  the S7 "first member of each orbit" rule. The Schreier-tree transporter
  product order (`src/symmetry/symmetry.c:237-260`) is `s_1…s_k` with `s_1`
  acting first, giving `rep^a = b` with every factor fixing the prefix —
  direction re-derived against `(pq)[v] = q[p[v]]`.
- **Implicit automorphisms**: formed only on equal trace *and* equal bytes,
  `a = t_L t_best⁻¹` with the product convention checked
  (`p1_tree.c:150-181`), verified by the group's membership test and by exact
  action inside `canon_symmetry_insert`, either failure →
  `INTERNAL_ERROR` (never a silent skip), inserted into the verified chain
  before any use (spec §7.3, R2). Insertion bumps the epoch and invalidates
  the transient stabiliser (`symmetry.c:99-103`), so no ancestor decision
  can use a stale `A_S` after growth.
- **Objective scope of the prune**: only `CANONICAL_IMAGE` runs with run
  options; the labeling and signed P1 runs pass `NULL` options (unpruned
  reference traversal under every policy,
  `src/search/objectives.c:440-445`), and all §8.1 enumerations are
  unpruned, as §8.2 requires.
- **Quota assembly per solve**: one quota split across phases —
  canonical image then deterministic witness with `quota - nodes`
  (`src/api/api.c:1181-1185`); labeling P1 then stabiliser with
  `remaining(quota, p1_nodes)` (`objectives.c:486`); signed enumeration then
  P1 with `remaining(quota, v.nodes)` (`:748`); multi-enumeration objectives
  share one accumulating visitor (`src/coset/coset.h:87-95`).
  `visit` refuses the node that would exceed the quota before counting it
  (`p1_tree.c:363-366`), matching "past max_nodes … `CANON_CAPACITY_LIMIT`".
- **Deterministic witnesses (§3/D1)**: canonical image `min A t` by complete
  stabiliser + coset descent (`objectives.c:773-798`); `TRANSPORTER_ONE`
  `r₀ = min A g` (`:310-333`); labeling `λ₀ = min Aλ` read from the payload's
  `Perm(r₀)` with `t₀ = ρ⁻¹λ₀` — composition order re-derived:
  `(ρt₀)[v] = t₀[ρ[v]] = λ₀[v]` (`:500-507`); signed nonzero `min A t` with
  the sign then *computed* from the replaced witness, not inferred
  (`:554-574`); signed zero least odd element via exhausted enumeration
  giving exactly `A+` (odd hits never inserted; membership pre-check against
  the even `A_known` cannot skip an odd hit, `:179-226`) and via Schreier
  generators of `ker χ` over `{1, a}` on the fast path — the four generator
  forms re-derived and correct for a right transversal (`:613-660`).
- **Signed routes**: fast path only when every generator fixes `x` by exact
  action (then `A = G`); any-mode zero witness is a checked generator;
  nonzero always goes through complete-`A` evidence (SIGN-COVER honoured —
  no shortcut from P1 automorphisms); `signed_nonzero` asserts
  `χ(t) = +1` on the all-even fast path rather than assuming it (`:572-574`).
- **Certificate writer mechanics**: sweep-count patching happens at
  `node_refined`, before any child `node_begin` can overwrite `sweeps_at`
  (`p1_tree.c:379-385`); children are written in cell order as explored
  (followed immediately by the nested node) or pruned with
  `(b, b', aut index)`, the transporter materialised into the automorphism
  table at decision time, before descendant visits reuse the Schreier
  scratch; the final length accounting in `canon_cert_finish`
  (`certificate.c:378-393`) matches the assembled fields
  (`4 + (4+trace) + (4+bytes) + 4n + 8 = 20 + 4n + …`); the running limit
  check in `fail` makes an over-limit certificate `CAPACITY_LIMIT` as a
  function of the input, not of when it is noticed.
- **Snapshot/decision-stack aliasing**: both stacks may move during a child
  visit and are re-read after `decide_before` and at every loop iteration
  (`p1_tree.c:435-447`); the snapshot's `lab` is read only through
  `canon_partition_snapshot_lab`; rollback restores `(cells, start, lab)`
  exactly and rebuilds `pos`/`cell_of` (`partition.c:154-172`).
- **Root-kind determination feeding §7.1 initial keys**: an imported DAG
  whose root is a set of atom nodes (incl. empty) becomes the subset root
  and a root graph record becomes the graph root
  (`src/object/object.c:214-259`), so the same extensional value gets the
  same initial keys and O signatures whether built or imported; every other
  root gets the empty initial key and empty O signatures, per spec.

### A-search-04 — P1 `visit` recurses to depth `n − 1` on the C stack with no depth guard

- Severity: **low** (note-level robustness; conditions below). Category:
  memory-safety.
- Evidence (reasoned from code; not run — a reproducer would need a large
  degree and a raised memory budget, against the brief's light-load rule):
  `visit` (`src/search/p1_tree.c:357-478`) recurses once per
  individualisation; spec §7.2 bounds that by `n − 1`. The only depth check
  is `depth == UINT32_MAX` (`:411-413`, unreachable for `n ≤ 2³²−1`). At the
  default 4 GiB budget the admission plan's quadratic snapshot charge
  (HANDOFF decision D16) rejects the degrees that could recurse tens of
  thousands of frames, and failing that, the geometric snapshot growth hits
  `RESOURCE_LIMIT` early; but with a caller-raised `max_live_bytes` and
  enough RAM (e.g. `n ≈ 5·10⁴`, trivial group, empty subset: a first path of
  `≈ n` frames against a default quota of `2²⁰` nodes, snapshot heap
  `≈ depth·8n` bytes still comfortably allocatable at the depth where a
  typical 8 MiB stack is exhausted), the outcome would be a stack-overflow
  crash, not a status — against the spirit of "allocation failure is
  `RESOURCE_LIMIT`, never a crash" (HANDOFF, S8). The §8.1 enumerator does
  not have this issue (depth `≤ log₂|G|`). Labelled a reasoned conjecture
  about reachability, not an observed crash.
- Recommendation: either charge a C-stack term in the admission plan
  (depth `≤ n` frames of a measured frame size vs. a stated stack budget,
  `CAPACITY_LIMIT` beyond it) or convert the DFS to the explicit-frame /
  rebuild form of spec §11.2's strict fallback. Record which is chosen;
  until then, state the admitted-degree caveat near `visit`.
- Remediation plan: cheapest correct step is the plan charge
  (`src/api/api.c` `plan_bytes` + a frame-size constant measured for
  `visit`), with a C test at a small artificial stack budget if the plan
  gains such a field; the iterative conversion is M5-scale work. Risk of
  the plan charge: some currently-admitted large sparse cases become
  `CAPACITY_LIMIT` (a deliberate, documented change).

## 5. Further checks resolved sound after the first write

- **Base-prefix levels for `A_S`.** `build_run` appends one base level per
  requested prefix point, validated distinct and in range, before any
  generator is inserted (`src/bsgs/chain.c:592-606` via
  `append_base_point`, `:97-130`), including points the whole group fixes
  (orbit `{b}`). So `levels[d]` in a rebased chain is the suffix fixing the
  first `d` base points pointwise, as `canon_symmetry_stabiliser`
  (`src/symmetry/symmetry.c:112-146`), the certificate's rebased chains and
  `tuple_run`'s `H_b` step assume; a missing level would have made the
  prune unsound, and it cannot occur.
- **Terminal level indexing.** `ensure_levels` always maintains a terminal
  level at `levels[depth]` (base `NONE`, no generators), and
  `append_base_point` opens a new one (`chain.c:90-100`), so the accesses
  `levels[level]` with `level == depth` in `canon_bsgs_orbit_ids`
  (`:873-876`), `orbit_min` (`:917`) and `tuple_run` (`:987`) read that
  trivial level, never out of bounds; `orbit_min`'s BFS over zero
  generators correctly reports a singleton orbit there.
- **Shared §8.1 visitor across enumerations.** Both enumerators reset
  `v->stopped = false` at entry (`src/coset/enumerate.c:172`,
  `src/bsgs/explicit.c:343`) while `nodes`/`leaves` accumulate
  (`src/coset/coset.h:87-95`), so a transporter hit in a first enumeration
  cannot truncate the stabiliser enumeration that follows it in
  `TRANSPORTER_ONE` (deterministic) or `TRANSPORTER_COSET`, and the quota
  is spent across both. The chain enumerator body
  (`src/coset/enumerate.c:98-180`) rebases `H` to base point
  `a` = least point moved by `H`, iterates `b` over the *sorted* orbit, takes `t_b` as
  the constrained least element with `a^t_b = b`, recurses on `H_a` = next
  level with `t_b r` (t_b first), and counts every `visit` through
  `canon_coset_visit_enter` before doing work — the §8.1 reference
  traversal and its frozen order, with recursion depth `≤ log₂|G|` since
  each child orbit has size ≥ 2.
- **Graph tables are in `B(·)` order.** `intern` sorts distinct colour/label
  strings by `(length, unsigned bytes)` — which is `B(s)` order, spec §4.3
  ("a B field compares length first") — and assigns ids in that order
  (`src/object/graph.c:48-125`), and arcs are combined and sorted by
  `(source, target, B(label))` with label ids as those ranks (`:129ff`,
  `src/object/graph.h:7-13`). So the O stage's "label rank" index order and
  the initial key `B(vertex_colour[a])` rank are the spec's orders, on built
  and imported graphs alike (images borrow the same tables,
  `graph.c:443-452`, so an image's ranks cannot drift mid-search).
