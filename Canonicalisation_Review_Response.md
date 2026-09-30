# Response to the canonicalisation reviews

**30 September 2026 · specification v2.0**

Reviewed baseline: v1.0 at commit `fb014c9`, SHA-256 `924699142d622de142e63f1145c91b436612aa88653e77311da8ecb2d2cfb431`. The [referee report](Canonicalisation_Referee_Report.md) is unchanged. Section references below are to the [revised specification](Canonicalisation_C_Architecture_Specification.md); milestone references are to the [implementation plan](Canonicalisation_Implementation_Plan.md). All findings and T1–T14 receive a design decision; the remaining implementation/proof/evidence obligations are explicit rather than claimed complete.

## Referee findings

| Finding | Revised sections and decision | Remaining scoped obligation |
|---|---|---|
| **1. Executable profile/wire** | §§4, 7, 17, 20: assigned P1/CDAG-2/order/type/trace tags; exact stages, colours, sorting, framing and hand-checked vectors; metadata excluded from canonical bytes. | M0 blind independent agreement and corruption sensitivity; each new public wrapper/profile needs its own vectors. |
| **2. Representation independence** | §§5–7, 16: decode factorisation is separate from equivariance; group presentation, DAG sharing and auxiliary numbering cannot choose semantics. | M1 interface theorem and M3 concrete adapter proofs; metamorphic evidence is insufficient alone. |
| **3. Labeling witnesses** | §3.1: typed λ=ρt∈Gρ, c=x^λ, complete Aλ and explicit renamed-coordinate/group reconstruction. | M1 formal proof and M4 public API tests; small finite reconstruction/representative checks pass. |
| **4. Termination** | §§6–7, 10: fixed P1 domain, strict splits plus final stable sweep, bounded branch depth; auxiliary profiles require a node-wide bound/rank. | Total custom callbacks remain a trust assumption; future generation profiles need their own proofs. |
| **5. Complete objectives** | §8: explicit coset enumerator and consumers for minima, transporter, stabiliser, labeling coset and general constraints; internal intersection/normaliser/conjugacy reference predicates. | M1/M4 coverage; specialised public services deferred until their gates. Image-only pruning never proves full A. |
| **6. Hardness** | §§4.4, 12.1: fixed SIMPLE-UPPER-1 Independent Set reduction; restricted IR theorem scope preserved. | No universal exponential or arbitrary-canonical-image hardness claim. Prove profile hypotheses before applying the restricted IR lower bound. |
| **7. Subgroup output** | §9.4: canonical greedy generators with deterministic sparse/symbolic priority; O(n² log n) entries; cubic format diagnostic-only. | M2 serializer/kernel proofs and output-budget tests; ordered normalisation does not solve conjugacy. |
| **8. Shared objects** | §4.2: exact bottom-up normalisation and canonical DAG references; induction establishes injectivity/sharing independence. | M3 executable parser/normaliser proofs, including native algebraic leaves; no cyclic-object support. |
| **9. Cost ledger** | §§9.2, 10, 12.1, 19: explicit input/group/DAG/bit/output parameters and complete cost sums, including wrappers and batch setup. | M5 instrumentation; no end-to-end sparse-refinement bound is inferred from a local kernel bound. |
| **10. Lower bounds** | §12.2 and [appendix](Canonicalisation_Performance_Lower_Bounds.md): U/A/H distinct; E estimates separate; resource maxima and justified sequential sums. | Validate demand/residency/clock premises for each future result; no calibration was run. |
| **11. Reproducible deltas** | §§12.4, 19: Δ, ρ, δ, undefined ratios, negative-gap investigation, cold/warm/residency and censored comparisons. | M5 retained per-result ledger and measurements, matched competitors. |
| **12. Memory/capacity** | §11: all live state, replay fallback, deterministic admission, exact arithmetic, one-task capacity failure and external-allocation distinction. | M4/M5 deterministic budget planner and fault evidence; no progress promise beyond available resources. |
| **13. Group kernel** | §9: Schreier reference, explicit verifier conditions and provenance DAG; build/rebase/verification costs; independent witness minimisation. | M2 practical closure policy, executable bounds/proofs and performance gates. Unsigned bytes need not pay for full stabiliser. |
| **14. Concurrency** | §§14, 18: mutex-linearised ownership/split/link/complete protocol, retained acyclic representatives, helper joins, reclamation and trusted resume. | M6 abstract coverage proof/tests; M7 concrete atomics refinement. Untrusted checkpoints require independently checked coverage. |
| **15. CPU/GPU** | §§1, 13, 21: dependency-free CPU release; optional explicit GPU capability/buffer/event boundary. | M8 plugin, residency/transfer and scalar-equivalence evidence; GPU floors do not set CPU acceptance. |
| **16. API/errors** | §§3.2, 11, 17: orthogonal validity/completeness flags, sign/status separation, ownership/callback/allocator/output-resume contracts. | M4 public/FFI, error/lifetime and overflow tests; arbitrary adapter laws remain declared assumptions until proved. |
| **17. Proof scope** | §§20–23 and plan: semantic, group, adapter, checker, concurrent and C/runtime layers separate; opposite-group bridge; certificate boundary. | M1–M8 are future work. H0/H1 audit possible external components before reuse; no end-to-end verification claim. |
| **18. Acceptance gates** | §§20–21 and plan: evidence-bearing gates, blind references, viable representations, honest performance and admissible optimisation. | Gates are specified, not passed by this document revision. |

## TensorGR learnings

The [learnings](Canonicalisation_TensorGR_Learnings.md) retain their evidence limits. No TensorGR code was changed and no laptop timings or web-only theorem claims were imported as established facts.

| Item | Revised sections and decision | Remaining scoped obligation |
|---|---|---|
| **T1** | §§3, 7.4, 17, 19: source-to-target/product vectors, typed labeling coordinates and client-level wrapper tests; xperm free/dummy/double-coset convention gate. | M3c/M4 tensor wrapper proof and vectors before release/benchmarking. |
| **T2** | §§4.4, 19: keep canonical image and prescribed minimum separate; add identical-factor products and Riemann chains. | Local pinned correct competitors and measured results; one family is not a general factorial theorem. |
| **T3** | §§20–21, M0: two blind retained implementations, exact agreement beyond oracle range and planted-corruption sensitivity. | Future M0 acceptance test; TensorGR's unavailable implementations cannot satisfy it. |
| **T4** | §7.3: node-restricted verified automorphisms, including implicit candidates, with objective-specific coverage. | Prove each prune; no prototype speedup or arbitrary-G completeness theorem is imported. |
| **T5** | §§12, 19 and appendix §6.7: wrapper/build/sort/group costs; model actual group structure; pin clock/power and report work gaps. | M5 per-regime instrumentation/calibration; identical factor count does not imply an S_k stabiliser. |
| **T6** | §§9.3, 12.3, 16–17: warm group reuse, exact chain/skeleton keys and coordinate transport, preallocated workspaces. | M5 transport/cache proofs/tests and cold/warm accounting. |
| **T7** | §12.3 and appendix §6.7: cache-resident small-batch regime with compute, branch-recovery, streaming and collection floors. | Establish actual residency and demand; no imported 80n-byte or hardware constants. |
| **T8** | §14: normative R1–R3; caller-owned threads, batch-first parallelism and verified-only helper contributions. | M6 scheduler and M8 backend evidence; random discovery cannot certify nonzero/completeness. |
| **T9** | §§3, 8.4: explicit χ, validated lift comparison, one-sided odd certificate and full-stabiliser nonzero route; coefficient convention proved. | SIGN-COVER remains open for arbitrary G; baseline correctness does not depend on it. xperm reduction/profile equivalence is separate. |
| **T10** | §2.2: multi-term identities, rational collection and Clifford normal forms live in higher layers. | No implementation of those layers in this library scope. |
| **T11** | §§7.3, 12.3, 19: discrete-root fast path with required trace and separate signed completeness; standard root-discrete metric. | M5 workload measurement; no assumed fraction or performance conclusion. |
| **T12** | §11.1: deterministic capacity/admission and exact arithmetic; distinguish external allocation failure/cancellation. | M4/M5 planner tests; higher-layer rational collectors must meet their own exact/final-range or conservative-bound policy. |
| **T13** | §23, formalisation-review update, H0/H1: hex repositories are unverified external leads, not disproved by a mathlib-only search. | Future local retrieval at full pinned commits, SHA-256 provenance and theorem/executable scope audit before any dependency. No retrieval in this task. |
| **T14** | §§14.1, 17: immutable registry, per-thread workspace, no hot-path allocation after admission, capacities/status separate from signs, output valid as input. | M4/M5 API/lifetime/FFI tests; surviving header is design evidence, not a validated reusable implementation. |

## Validation and remaining limits

The expanded `Canonicalisation_Review_Checks.py` remains a finite sanity checker, not a proof or production test. Exact command: `nice -n 19 python3 Canonicalisation_Review_Checks.py`. It passes in under one second on this run, well below the approximately five-second cap. The retained original checks cover 40 subgroups at degrees 0–4, 3,536 leaf identities, 2,997 coset partitions, 2,059 normalised-orbit cases and 361 graph-prefix cases, plus diagnostic output arithmetic.

The v2 additions check P1 subset/graph transport invariance at small degrees, hand-derived traces/bytes/witnesses, DAG sharing and a depth-60 compressed chain, noncommuting products, typed labeling-coset reconstruction/representative/source-coordinate changes, all small-group characters and signed lifts, odd-stabiliser equivalence and nonzero sign covariance, inconsistent character rejection examples, and greedy-generator size/generation. Exact final counts and link/diff validation are recorded in [HANDOFF.md](HANDOFF.md).

All deliverables are document/checker changes. No Lean, Julia, compiler, build, benchmark, network download or archive extraction ran. No state-changing Git command ran. Remaining limitations are the future gates above: independent implementations, formal/executable proofs, capacity/runtime implementation and measurements, adapter/competitor acquisition and SIGN-COVER if the faster signed route is pursued. They are not unanswered review decisions.
