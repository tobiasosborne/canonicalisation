# Review pass of November 2026: collated summary

**Date:** 8 November 2026. **Base:** `main` at `fb0d8e9`, unchanged by the review (no code edited).
**Brief:** [BRIEF.md](BRIEF.md) (read-only, no test suites or reference runs; one report per lane;
each finding with evidence, recommendation and remediation plan).

One round only, stopped on the maintainer's instruction when the Muse quota ran out; no further
agents were launched. The reports are the reviewers' own and have **not been independently
verified** except where noted under "Coordinator spot checks". Treat every finding as a lead to
confirm before a fix, as `CLAUDE.md` requires for delegated output.

| Lane | Model | Report | State |
|---|---|---|---|
| A-search: P1 search core, pruning, witnesses | Muse Wonder Full 1.0 (high) | [A-search.md](A-search.md) | complete |
| B-kernel: perm, chain, coset, encoding, objects, ledger | Muse Wonder Full 1.0 (high) | [B-kernel.md](B-kernel.md) | complete |
| C-checker-api: checker soundness, API, spec gaps | Muse Wonder Full 1.0 (high) | [C-checker-api.md](C-checker-api.md) | five cross-checks marked IN PROGRESS |
| D-perf: system-wide performance | Muse Wonder Full 1.0 (high) | [D-perf.md](D-perf.md) | partial (its §6 lists what is unfinished) |
| E-perf-refine: refinement and partition | Muse Wonder Full 1.0 (high) | [E-perf-refine.md](E-perf-refine.md) | complete |
| F-perf-group: group algorithms | Muse Wonder Full 1.0 (high) | [F-perf-group.md](F-perf-group.md) | complete |
| G-perf-search: search, pruning, parallelism | Muse Wonder Full 1.0 (high) | [G-perf-search.md](G-perf-search.md) | complete |
| H-tests: adequacy of the test base (by reading) | Muse Wonder Full 1.0 (high) | [H-tests.md](H-tests.md) | complete |
| S1-proofs: analytic derivations (§7, §8, pruning lemmas, D1) | GPT-6.1 Sol (xhigh) | [S1-proofs.md](S1-proofs.md) | complete |

Not done (planned, cancelled by the stop): a second Sol lane deriving bounds for the performance
proposals; deeper passes on nested DAG/labeling semantics and an adversarial checker attack.

## 1. Decisions the maintainer must take first (spec-level; no code fix without them)

1. **`CAPACITY_LIMIT` depends on the presentation.** Under work policies `0x0002`/`0x0003` the
   explored-node count depends on the given generating set (source (i) of `A_known`), so the
   quota verdict does too, against §5/§11.1 ("a function of semantic input, objective and
   descriptor"). A-search-01 *ran* a counterexample: `{0}` under `Sym(4)`, quota 5:
   `CAPACITY_LIMIT` from `⟨(0 1 2 3),(0 1)⟩`, `COMPLETE` from `⟨(1 2),(2 3),(0 1)⟩`;
   C-checker-api-02 derives one on `Sym(3)`. The admission plan has the same property
   (C-checker-api-03 = HANDOFF open item 6(i)). Options: declare the descriptor/presentation part
   of the input in §11.1, or define the count from presentation-free data.
2. **Pruned runs and the profile claim.** C-checker-api-01: pruned runs keep profile `0x0001`, but
   in `ANY` witness mode the reported witness is an explored-leaf witness that need not be the
   unpruned one, while §11.1 conditions the profile on "trace, bytes and witness". Either the
   spec's sentence is read as "deterministic witness only", or pruned ANY-mode runs need a
   different claim. The checker certifies neither §7.4 nor the deterministic witness.
3. **Unverified base changes.** B-kernel-01: every production `canon_bsgs_rebase` passes
   `verify=false` (coordinator confirmed: `src/coset/enumerate.c:127`, `src/coset/least.c:152`,
   `src/bsgs/chain.c:1002`, `src/symmetry/symmetry.c:128`, `src/search/certificate.c:282`), while
   spec §9.2 says "base change rebuilds/certifies". Whether a deterministic rebuild from a
   verified chain's generators counts as "certified" is the question. This interacts with the
   main performance fix (one ordered base, item 3.1 below): decide it together.
4. **Quota counting at the boundaries** (C-checker-api-09; ref-b questions Q1–Q3, Q10) and the
   remaining quoted ambiguities in each report's §3 (C-checker-api lists fourteen).
5. **Labeling and signed searches unpruned.** G-07a argues orbit pruning provably cannot change
   their payloads, signs or deterministic witnesses; enabling it is a per-policy-ID decision.

### Recommendations taken (8 November 2026; the maintainer delegated these to the coordinator)

These are readings recorded for the next spec revision; `docs/specification.md` is unchanged.

1. **1.1 → presentation is part of the pruned input.** Under work policies other than `0x0001`,
   the count, and hence `CAPACITY_LIMIT`, is a function of the input *as presented* (generator
   list, order) together with objective and descriptor, as `docs/pruning-rules.md` already says
   ("as given"). `0x0001` stays presentation-free and is the reference for determinism claims.
   The same reading covers admission (open item 6(i)): the plan may read the backend and the
   given generators. Rationale: a presentation-free count would forbid pruning by the input
   generators, the main prune. Spec v2.3 should state it in §11.1. No code change.
2. **1.2 → the profile claims trace, bytes and the deterministic witness.** An `ANY`-mode witness
   is by definition any element of the solution coset, so a pruned run may report a different one
   without changing profile; `DETERMINISTIC` witnesses must agree across policies (tested since
   S7). Spec v2.3 should say "deterministic witness" in §11.1. No code change.
3. **1.3 → certify every base change by order equality.** A rebuilt chain is generated by elements
   of G, and its transversal product counts distinct elements of ⟨S⟩ ≤ G; if that product equals
   the verified |G| (< 2^64 by decision 2), the chain is a complete BSGS of G. This O(depth) check
   satisfies §9.2 "rebuilds/certifies" at no real cost; failure is `INTERNAL_ERROR`. Code change
   (fix lane K1).
4. **1.4 → deferred** to the T3 comparison and the ref-b question review (HANDOFF open item 4).
5. **1.5 → deferred** to the performance slice; enabling pruning of the labeling and signed
   searches needs its own coverage lemma in `docs/pruning-rules.md` and a policy decision.

**Priorities for the fix round.** P0: B-kernel-01 (decision 1.3); the dominant performance defect
(chain rebuild per step of `tuple_min`, which blocks the T3 run of the M0 gate); CI and test gaps
H-tests-02/03. P1: the remaining kernel robustness lows (B-kernel-03…06), checker robustness on
hostile input (C-checker-api-06), stale contract text (A-search-02/03, C-checker-api-08).

## 2. Correctness, soundness and evidence findings (ranked)

| ID | Sev. | Finding |
|---|---|---|
| H-tests-01 | high | P1 trace and the choice of canonical orbit element rest on one shared reading (`checks/review_checks.py` model); the oracle checks neither; ref-b runs in no routine gate. A shared misreading would pass every test. |
| H-tests-02 | high | §3 "transporter target of another shape" (flipped once) and other cross-shape rules have no routine C/e2e test. |
| H-tests-03 | high | CI and `ctest` do not run what the HANDOFF validation record implies; the CI Python job builds no `canon-check`, so `test_e2e.Pruning` cannot pass as written (reasoned, not run). |
| C-checker-api-04 | medium | The checker does not certify the named work policy's traversal; the quota is absent from CERT-0. |
| C-checker-api-05 | medium | The checker accepts embedded object streams weaker than §4.1/§4.2 legality (non-canonical, partly unreachable); not a false-image hole. |
| C-checker-api-06 | medium | Checker on hostile input: unbounded total memory, super-quadratic work, `CAPACITY` reported as `REJECT`. |
| C-checker-api-07 | medium | "Written from the specification only" overstates independence: CERT-0 is restated from the producer side by the same single agent (S7). |
| C-checker-api-08 | medium | `include/canon/canon.h` contradicts itself and the code on deterministic witnesses; stale on evidence mode and object kinds. |
| B-kernel-02 | medium | §11.2 ledger counts payload bytes only, not block headers or allocator overhead; `peak ≤ plan` understates real memory. |
| H-tests-04/05 | med-high | Semantic coverage mostly n ≤ 4 exhaustive / n ≤ 8 random; admission tests only where the plan was calibrated, no budget-boundary or presentation-determinism tests. |
| H-tests-06/07/08 | medium | Checker mutations single-field only, several REJECT reasons never elicited; some circular expectations; §17 lifetime/ownership/re-entrancy contracts thinly tested. |
| A-search-02, -03 | low | Stale comments: pre-D1 zero witness in `src/search/objectives.h:114`; "certificate silently dropped" in `src/search/certificate.c:129`. |
| A-search-04 | low | `visit` recurses to depth n−1 with no guard (conjecture, matters only with a raised budget). |
| B-kernel-03…08 | low | Verifier trusts unchecked capacities; `least_outside` shortcut trusts its precondition; `set_signs` leaks on a second call; chain `insert()` not failure-atomic; decoder allocates O(n) bit maps up front; `canon_dag_normalise` trusts payload well-formedness. |
| C-checker-api-10…13 | low | Sink/result lifetime during the callback; process-global metrics peak under concurrency; stubbed surface; CLI rough edges. |

**Found sound** (see each report's §4; do not repeat): P1 refinement and G-stage ordering;
corrected P1-ORBIT-1 (both 19 October defects verified fixed); implicit automorphisms; D1 on every
route; quota assembly; prefix stabilisers; §8.1 enumerator; permutation convention; verifier
conditions including nesting; coset partitioning; Group bytes; decoder strictness; DAG height
numbering; subset/graph actions; the checker's chain verifier, G-stage minimality, child/prune/
leaf replay and integer discipline. No golden constants found in `src/` or `tools/`. No wrong
trace or bytes, no unsound prune, no wrong witness was found.

## 3. Performance (ranked across D, E, F, G; all claimed to preserve P1 bytes and node counts unless marked)

1. **Chain rebuild per step in `tuple_min`, the coset descent and the enumerator** (D-perf-01,
   G-01, F-perf-group-01, E-perf-refine-05; four lanes agree). Every sweep of every node rebuilds
   Schreier–Sims chains, with 5 heap allocations per call. D-perf *measured* 1.3–1.8 ms per sweep at
   n = 32 (≈ 22–30 min for the 10⁶-node quota), which explains the T3-000077 stall; ref-b's
   memoised stabiliser trie is why it answers in 0.27 s. Fix: one ordered base per descent, a
   stabiliser cache (D-perf cites §16 as licensing it), fast paths (empty-F sweep measured
   1 774 µs → 2.1 µs). Related: F-perf-group-02 / D-perf-01 say §7.1 needs *any* minimiser, not the
   least; **check that against spec §7.2 ("the unique t_L minimising L^G") before relying on it**.
2. **Enumeration objectives Θ(|G|) with a full descent per tree edge** (D-perf-02, G-02, G-07b):
   Schreier transporters short term; partition backtrack for stabiliser/transporter and
   branch-and-bound for `LEX_MIN_IMAGE` (coverage lemmas sketched; new work-policy IDs needed for
   the region prunes, D-perf-02b).
3. **`insert_verified` rebuilds and re-verifies from scratch per insertion** (D-perf-03,
   F-perf-group-03): incremental insertion, verify once.
4. **Refinement** (E-perf-refine-01/02/04, D-perf-07): per-cell split over non-singleton cells only;
   stable counting split instead of the generic merge sort. Hopcroft/Paige–Tarjan queues are
   **not** PROFILE-EQUIV (P1 traces every intermediate sweep) and would need a new profile ID.
5. **Group constants** (F-perf-group-04…11, D-perf-04/05/08): provenance DAGs for transient chains
   nobody verifies; dense transporters where point images suffice; signed-route χ via sign tables on
   G's own chain (F-08); strong-generator filter; §9.4 rule-2 caching.
6. **Search** (G-03…06, G-09, D-perf-06): trace-prefix domination (new policy + checker record),
   more implicit-automorphism sources, quadratic orbit-prune overhead, leaf comparison copies,
   per-sweep certificate rebases.
7. **Parallelism** (G-08, D-perf-09): none exists; `0x0003`'s count is defined by sequential
   discovery order, so stage it: batch pool, then `0x0002` subtrees, then a schedule-independent
   policy.

Note the tension between 1.3 (verify base changes) and 3.1 (fewer rebuilds): one verified ordered
base per operation satisfies both.

## 4. S1-proofs (GPT-6.1 Sol)

Complete ([S1-proofs.md](S1-proofs.md), no builds or tests). Written proofs, under stated
hypotheses, that the core arguments hold: P1 G-stage normalisation, equivariance and
representation independence (§4.1); leaf map and constructive minimisation (§4.2); coset splitting
and every §8 consumer (§4.3); constrained least-element descent and least outside element (§4.4);
labeling cosets and deterministic witnesses (§4.5); the exact scope of P1-ORBIT-1 (§4.6);
implicit automorphisms and their reuse at ancestors (§4.7); the signed zero/nonzero rule and the
§8.4 lift (§4.9); D1 and the Schreier kernel generators (§4.10). Two medium findings, both
independent confirmations of Muse findings:

- **S1-proofs-01**: source-(i) pruning makes node counts and quota outcomes presentation-dependent
  (= decision 1.1; A-search-01, C-checker-api-02).
- **S1-proofs-02**: CERT-0 proves image coverage and record count, not the named policy's count
  (= C-checker-api-04).

Five quoted ambiguities (its §3): semantic versus presented input for quota acceptance; no rule
fixes the "fixed sequential order" of `0x0003` given that cell member order "is not semantic"
(§7.1); which traversal is charged when a §7.3 shortcut applies; `μ⁻¹λ` (§3.1) versus the
deterministic `λ₀ = min Aλ` (§3); "complete A" on a deterministic zero when D1 stores `A⁺` and `a`.
Section 4.8 states what the node-count arguments do and do not prove.

## 5. Coordinator spot checks

- B-kernel-01: confirmed that every production `canon_bsgs_rebase` call passes `verify=false`
  (five call sites above). Whether this violates §9.2 is decision 1.3.
- The working tree was clean apart from `docs/review-2026-11/`; no lane edited the repository.
- Nothing else was re-verified; the A-search-01 counterexample and the D-perf timings are the
  reviewers' own runs.

## 6. Suggested next session (not started)

1. Take decisions 1.1–1.5.
2. Cheap, high-value fixes: the stale comments and header text (A-search-02/03, C-checker-api-08),
   the CI wiring (H-tests-03), the cross-shape transporter test (H-tests-02), B-kernel-05.
3. Performance item 3.1 as a slice with a brief (it also unblocks the T3 run of HANDOFF open item 4).
4. An independent check of the high findings (and, where a fix relies on them, the S1-proofs
   derivations) before any fix.
