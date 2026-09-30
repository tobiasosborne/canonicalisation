# Learnings from TensorGR.jl canonicalisation work

30 September 2026. This note distils the TensorGR.jl session-20 canonicalisation work for use in the pending revision of the specification and the implementation plan (see [HANDOFF.md](HANDOFF.md)). It is an input to that revision, not itself a revision.

## Source and evidence status

Inspected: `../TensorGR.jl` at commit `b4928910790e6bafd07884b2f956fbaa42348a88`, specifically `reviews/07_julia_best_practices_review.md` §1, `reviews/08_canonicalization_literature_survey.md`, `reviews/08b_parallel_racing_pareto.md`, `reviews/09_canonicalization_perf_bound.md`, `reviews/10_blind_prototype_comparison.md`, and `reviews/10_probes/` (`o_canonir.h`, `s_canonir.h`, `xcheck.c`, `hard.c` and raw timing outputs).

Evidence limits:

- **Literature claims in TensorGR 08/08b were retrieved from the web, not from local primary TeX.** Under this project's evidence rule they are discovery leads. Any of them used normatively (McKay–Piperno Theorem 5, Niehoff §4.4, SeQuant scaling, `hex-graph-iso`/`hex-perm-group` scope) must first be retrieved locally with provenance under `review_sources/`.
- **The two C prototypes cannot currently be rebuilt.** The branches `proto/canon-ir-baseline` and `proto/canon-ir-baseline-opus` named in TensorGR's `HANDOFF.md` no longer exist in that repository; only the public headers, the cross-check harness and raw outputs survive in `reviews/10_probes/`. Their results below are reported measurements that cannot be re-run from the surviving files.
- Timings were taken on a laptop (i7-1365U) whose effective clock drifted between 0.7 and 2.3 GHz; the report rates absolute times as ±30 % and only same-session interleaved ratios as reliable. None of these numbers transfers to this project's Ryzen 9 9950X model.
- The TensorGR prototypes are nauty-style individualisation–refinement for tensor monomials (symmetric group on a coloured graph plus slot-symmetry gadgets). They are not implementations of this specification's canonical-image search under arbitrary `G`.

## What was found

| # | TensorGR finding | Evidence | Status |
|---|---|---|---|
| T1 | Calling `xperm.c` with every index declared free silently discarded the dummy-relabelling half of the double coset. It produced wrong "canonical" forms (`A^b_a + A_a^b` not reduced to zero; free-index positions changed). The xperm argument convention (`PERM[name] = slot`, dummies as (up, down) slot pairs) had to be found empirically. | 07 §1b, 10 §5 | Verified runtime probes in TensorGR |
| T2 | Butler–Portugal/xperm grows factorially with identical exchangeable factors: `(R_abcd R^abcd)^m` at m = 3 (24 slots) took 5.48 s per xperm call, versus 28.6 µs and 60.4 µs for the two IR prototypes. The step from m = 1 to 2 was ×38, and from 2 to 3 was ×1,460. | 10 §5, `hard_m3.txt` | Single instance family, one run per point, default xperm base |
| T3 | Two blind, independently written C implementations of the same brief agreed on about 2.33 M checks (up to 128 slots, far beyond either brute-force oracle). Planting 1 sign error in 500 was detected, and each suite also ran clean under ASan/UBSan. The checks included an absolute-sign cross-feed: `s_O(canon_S(x)) · s_O(x) = s_S(x)`. | 10 §1 | Reported; harness survives, implementations do not |
| T4 | The faster prototype's first design had a factorial blow-up that its invariance tests exposed. The fix was automorphism pruning with stored automorphisms at **every** node, plus "implicit" automorphisms detected without descending to a leaf. It then visited 3 leaves on Riemann chains versus k + 3 without implicit detection, and was 1.9–3.2× faster on symmetric products. | 10 §2 | Reported |
| T5 | Measured instruction counts were 19–35× the analytic µop model. The gap to the model floor was mostly fixed per-instance overhead (graph build, sorting, certificate handling), not search. The model's search term also wrongly assumed that identical factors are permuted by `S_k`: a closed Riemann chain has a dihedral-like group of order about k + 2. | 10 §3, 09 §11 | Reported; the model was flagged as needing revision |
| T6 | The old TensorGR path cost about 438 µs per `Riem³` term. xperm itself was not the main cost: per-call Schreier–Sims rebuilding, allocation and wrapper overhead dominated. | 09 §9 | Measured on a throttled laptop |
| T7 | The realistic TensorGR workload is 10⁵–10⁷ independent small instances (≤ 128 points, working set about 80n bytes, L1-resident). In this regime the per-term floor is compute- and branch-misprediction-bound, not bandwidth-bound; branch mispredictions are about half of the modelled practical floor. | 09 §0, §5–6 | Model only |
| T8 | Parallelise across instances, not within one, except for rare giant instances. Racing is safe only if all racers compute one pinned function (R1), contribute only verified automorphisms to one deterministic search (R2), or are proved-equal fast paths or pure equality tests (R3). Monte Carlo automorphism search (dejavu) gives groups, not canonical forms. | 08b §4, (b) | Web-sourced literature plus analysis |
| T9 | Monoterm canonicalisation needs a **sign character**. Antisymmetric slots, antisymmetric (spinor) metrics and Grassmann parity make the output `±canon(x)`, and the output is 0 iff an automorphism has sign −1. A missed odd automorphism is a wrong answer, not a slowdown. | 08 §1, §7; 08b D3; `o_canonir.h` | Standard in xperm/SymPy; the prototypes implement it |
| T10 | Multi-term identities (Bianchi, cyclic, dimension-dependent) are not canonical-labelling problems. They belong in a linear-algebra layer over canonical monomials. Clifford products need an algebraic normal form (antisymmetrised Γ basis) before canonicalisation. | 08 §6–7 | Literature synthesis |
| T11 | A discrete refinement result at the root settles the instance without search. The TensorGR plan identifies the fraction of real instances that hit this path as the number that determines the value of the whole design. | 08b §3, (d)1 | Open measurement |
| T12 | Exact rational collection with checked `Int` overflow can fail under one summation order and succeed under another. Capacity failure can therefore be schedule-dependent. | 08b D4 | Analysis |
| T13 | Leads on verified components: `leanprover/hex-graph-iso` (verified Lean 4 implementations of pinned nauty 2.9.3 dense/sparse configurations for coloured *simple undirected* graphs, with certificate checking) and `leanprover/hex-perm-group` (verified stabiliser chains, `checkChain`). The claims were checked against GitHub API and README only. | 08 §9, 08b §1 | **Not retrieved locally; unverified here** |
| T14 | The surviving prototype API is plain C structs with no callbacks. It has a read-only shared registry, a per-thread workspace, no allocation on the hot path, compile-time capacity limits returning a distinct error, error codes disjoint from sign values, and output that is valid input (idempotence testable). | `o_canonir.h` | Header only |

## What to incorporate, by referee finding

**Finding 1 (freeze profile and wire grammar).**
- T3 gives the acceptance test for "frozen": at least two blind, independently written implementations of the reference profile must agree byte-for-byte on generated instances beyond oracle range. The harness should prove its own sensitivity by planted corruption.
- Disagreement between careful independent implementations is direct evidence that the profile still leaves a choice open.
- Add this to the "Semantics frozen" gate and to the plan.

**Finding 5 (complete stabiliser, transporter) and finding 13 (separate output costs).** T9 adds an objective the specification lacks: a **signed canonical image**.
- *Setting.* The group carries a character `χ: G → {±1}`. The result is `(c, s)` with `x = s · c`, or zero.
- *One-sided zero test.* One verified automorphism `a ∈ Aut_G(x)` with `χ(a) = −1` is a complete, checkable certificate of zero. A nonzero claim requires completeness evidence: either `χ` trivial on a proved-complete `Aut_G(x)`, or a proved lemma that the canonical search's covered leaves expose every odd automorphism.
- *Unverified lemma.* The McKay–Piperno Theorem 5(b) style statement (pruning generators plus all best leaves generate `Aut`) is a candidate for that lemma. It must be retrieved locally and re-proved for this specification's tree under arbitrary `G` before use.
- *Encodings.* Two equivalent encodings deserve comparison:
  - an explicit character in the problem;
  - xperm's signed permutations on `Ω ⊔ {+, −}`, which reduces the sign to an ordinary canonical image of `(x, sign marker)` under `G̃ ≤ Sym(Ω ⊔ {±})`.
- *Cost.* Callers requesting signed output necessarily pay for either a zero certificate or completeness evidence; unsigned callers must not.
- *Scheduling.* T8 and T9 together forbid probabilistic completion: a Monte Carlo helper may contribute verified automorphisms but can never certify "nonzero" or "complete".

**Finding 6 (NP-hard lexicographic minimum).**
- T2 is concrete evidence of the practical cost of a minimum-type objective on block-exchange groups. xperm's double-coset representative is a prescribed minimum; refinement-based canonical images avoid the enumeration that the minimum requires.
- Keep canonical image as the default objective and lexicographic minimum as an explicitly slow, separately specified mode.
- Add identical-factor products (e.g. `(R_abcd R^abcd)^m`, Riemann chains) to the workload families and to the regression set.

**Finding 9 (cost ledger) and findings 10–11 (lower-bound discipline).**
- *Fixed costs.* T5–T7 show that for small instances the fixed per-call terms (`T_validate`, `T_group`, graph build, sort, output) can dominate and exceed a µop model by more than an order of magnitude. The ledger must expose them separately, and the lower-bound appendix needs a **small-instance batch regime** alongside its `N = 100,000` movement bounds. In that regime:
  - the per-instance working set is cache-resident;
  - the floors come from compute, branch recovery, streaming input/output and collection-table insertion latency amortised by memory-level parallelism;
  - bandwidth is not a floor.
- *Search model.* The search-cost model must be parametrised by the actual automorphism-group structure, not the number of identical factors (T5).
- *Measurement host.* T5 reinforces the requirement to pin clock and power policy: a laptop measurement's absolute times are unusable.

**Finding 2 (representation independence) and §16 (exact caches).**
- T6: a warm, reused group context is the largest single lever for repeated small solves. Recomputing Schreier–Sims per call cost more than the canonical search it served.
- *Batch reuse.* Make `group_create` reuse, verified-chain caching keyed by exact group identity, and cold versus warm reporting explicit for batch mode.
- *Skeleton cache.* The TensorGR "skeleton cache" (canonicalise the contraction skeleton once with its automorphism data, then reuse it across instances sharing it) is a coordinate-transported memo in §16's terms. It needs an exact key and transport map, and must not become a dominance cache.

**Finding 12 (capacity and live memory).**
- T12: capacity errors must not depend on schedule. Validate integer ranges before search, or use exact arbitrary precision, so that `RESOURCE_LIMIT`/capacity status is a function of the input and budget, not of thread interleaving.
- T14 illustrates the capacity-limited first release that the report accepts: compile-time or problem-time limits returning a distinct error, with `CANONIR_ELIMIT`-style codes disjoint from mathematical results.

**Finding 14 (concurrency) and §14.1 (parallelism hierarchy).**
- T8 matches §14.1's ordering and McKay–Piperno-style determinism (pruning from any thread only prunes). Add R1–R3 as normative rules for fast paths and helpers.
- Add a **caller-owned-threads mode**: a reentrant, single-threaded solve on a per-thread workspace, with no internal pool. It avoids two competing thread pools when the host language or application already parallelises across instances (Julia, TFORM-style collectors).
- The core-owned pool remains for single hard instances.

**Finding 16 (API, ownership, callbacks).**
- T1 is the motivating failure: a correct kernel was used with the wrong action, silently, and the argument convention was undocumented. Beyond making the action explicit (spec §2.1), require:
  - golden vectors for every argument convention of every public entry point, including action direction and which coordinates are fixed;
  - tests of *adapter/wrapper* layers against the reference semantics, not only kernel tests;
  - idempotence (`canon(canon(x)) = canon(x)`) and invariance properties run through the public API exactly as a client would call it.
- T14 supplies a concrete small-instance API shape for the batch path.

**Finding 17 (Lean scope).**
- T13, if confirmed locally, changes the formalisation audit's conclusion that no ready verified BSGS or canoniser was found. That audit searched mathlib only; the `hex` projects are outside mathlib.
- *What it could enable.*
  - A narrow **graph-only profile pinned to a `hex-graph-iso`-verified nauty configuration** could give Lean-checked canonicity for the `Sym(V)` coloured-simple-graph case.
  - `hex-perm-group`'s `checkChain` matches the recommended certificate-checker boundary for BSGS certificates.
- *What it cannot do.* Neither covers this specification's canonical-image objective under arbitrary `G`, directed/multi/hypergraph adapters, or signed objectives. Encodings into simple undirected graphs would need their own faithfulness proofs.
- **Action:** retrieve both repositories at pinned commits with SHA-256 provenance before any plan milestone depends on them.

**Non-goals (spec §2.1).** State T10 explicitly: multi-term identities and Clifford algebra are layers above monoterm canonicalisation. The engine supplies canonical monomials, signs and automorphism data to them.

**Workloads and competitors (spec §19).** Add:
- tensor monomials with slot symmetries, dummy relabelling, metric/spinor-metric signs and Grassmann parity, including identical-factor products;
- xperm used **correctly** in full double-coset mode, with SGS-cached and per-call timings reported separately;
- SeQuant/bliss, Symbolica `graphica` and GraphCombinations.jl as small-instance competitors, subject to local pinning.

Report the fraction of instances settled by root refinement (T11) as a standard metric.

## What not to import

- TensorGR's hardware numbers (generic 8-core desktop, RTX 4060 class, laptop measurements). This project's hardware model is fixed and differently sourced.
- The TensorGR recommendation to make nauty-style IR on `Sym(V)` the defining engine. That fits a tensor-monomial product; this project's scope is canonical images under arbitrary `G`. The evidence supports a pinned, possibly verified, graph-only profile as one fast, separately versioned profile, not a replacement for the general objective.
- Any claim of polynomial practical behaviour for IR on tensor inputs. TensorGR labels it synthesis, and Neuen–Schweitzer-type lower bounds remain in force for WL-realisable profiles.
