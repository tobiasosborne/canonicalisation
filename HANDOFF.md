# Handoff: revise the canonicalisation plan and specification, commit, stop

**Next agent's task:** revise the implementation plan and architecture specification using the completed referee review, track the resulting documents with Git, commit, and stop. Do not begin implementing the C library or a Lean development. The current handoff turn establishes a Git baseline for the review materials; inspect repository state before making further changes.

## User intent and working constraints

The goal is an exact, native C canonicalisation library with broad finite permutation-group coverage and excellent performance. Correctness and complexity take priority. Performance must be assessed against explicitly justified lower bounds with reported absolute/relative gaps, using realistic commodity CPU/GPU hardware and the best legal data representations.

The user explicitly requested GPT-6.1 Sol at xhigh reasoning for the performance analysis and authorised Sol subagents for literature review and other useful delegation. Those reviews are complete; reuse their results rather than restarting the research. New delegation is allowed if useful.

The user only accepts literature ground truth from **local primary paper sources, preferably TeX**. The relevant papers, formalisation sources, vendor documents and provenance manifests are already local. Web summaries are discovery aids, not evidence. Source code and manufacturer documents support their respective implementation/hardware claims; they do not establish measured performance.

The user reported local CPU contention. Keep processing light. Do not run calibration benchmarks, build dependencies, unpack all of mathlib, or launch broad scans. A previous extraction of 8,554 mathlib files was reduced to ten retained files to avoid unnecessary filesystem/indexing work. The full compressed archive remains available locally if a specific additional file is genuinely needed. Use targeted reads and bounded checks. No research subprocess is left running.

Proceed autonomously on routine design choices within this document task. Do not request repeated permission for authorised work. Git commits are requested; remote publication/push is not requested.

## Current state and reading order

Workspace: `/home/tobiasosborne/Projects/canonicalisation`.

1. [Canonicalisation_Referee_Report.md](Canonicalisation_Referee_Report.md): main report, 18 findings and release gates. Read this first.
2. [Canonicalisation_C_Architecture_Specification.md](Canonicalisation_C_Architecture_Specification.md): original v1.0, 824 lines, dated 29 September 2026. It has not yet been revised.
3. [Canonicalisation_Performance_Lower_Bounds.md](Canonicalisation_Performance_Lower_Bounds.md): hardware model, lower-bound taxonomy, kernel/search/output costs, numeric examples and performance-gap metrics.
4. [Canonicalisation_Algorithm_Literature_Review.md](Canonicalisation_Algorithm_Literature_Review.md): exact local TeX references, theorem scope, constructive canonical generators and complexity qualifications.
5. [Canonicalisation_Formalisation_Literature_Review.md](Canonicalisation_Formalisation_Literature_Review.md): local Isabelle/mathlib evidence, proof boundaries and staged Lean effort estimates.

There is currently no separate implementation-plan document. The existing plan is primarily specification §21, with defaults in §22. Create `Canonicalisation_Implementation_Plan.md` and make the revised specification's roadmap agree with it. Create a concise `Canonicalisation_Review_Response.md` mapping all 18 findings to revised sections, decisions and remaining explicitly scoped obligations.

The original specification SHA-256 is `924699142d622de142e63f1145c91b436612aa88653e77311da8ecb2d2cfb431`. The review started outside Git; the current handoff turn is to initialise and commit a baseline. Inspect `git status` and history rather than assuming a particular commit hash. Local Git author identity is already configured. No `AGENTS.md` was found in the workspace or its ancestors during the review; recheck for newly added guidance.

The referee reports describe v1.0 and should remain historical review records. Their original-spec line references may become stale after revision; preserve the reviewed version through the baseline commit, record that commit in the response, and use section/finding identifiers to map the revision.

## Required revision decisions

Address every finding constructively. The following are the highest-priority changes, not a replacement for reading the complete report.

1. **Freeze one executable canonical profile and wire grammar.** Specify initial colours/types, refiner sequence, fixed-point/stage boundaries, signature ordering, cell ordering, target selection, individualisation placement, singleton extraction, trace tokens/framing/terminal ordering, and exact leaf comparison. Assign actual type tags, integer/length encoding and comparison rules. Add small golden cases. Requirements to “choose a fixed rule” are not enough.
2. **Separate semantic and representation invariance.** Refiners, traces and encoders must factor through the semantic interpretation, independently of generator presentation, SGS/base choices, storage sharing, insertion order and auxiliary numbering. Equivariance under atom permutations alone does not establish this.
3. **Complete the labeling-coset witness contract.** With `Λ=Gρ`, solve on `G′=ρ⁻¹Gρ`; return labeling `λ=ρt ∈ Λ`, with `c=x^λ` and complete labeling coset `Aut_G(x) λ`. Distinguish this bijection from an ordinary witness in `G`. State source/target domains and coordinate reconstruction explicitly.
4. **Make termination precise.** Use a node-wide finite auxiliary-domain bound or a proved well-founded generation rank. Per-invocation finite allocation is insufficient. Refiners preserve existing distinctions and individualisations. State totality assumptions on custom callbacks.
5. **Specify complete objective-specific algorithms.** Give reference pseudocode and coverage proofs for canonical image, lexicographic minimum, transporter, complete stabiliser and requested constraint services. A BSGS for discovered automorphisms does not prove the full object stabiliser. Pruning needs separate image-only and full-group coverage obligations.
6. **Replace the cubic subgroup encoding as production default.** The current full-transversal format emits `2n²(n−1)` bytes with uint32 entries for `Sym(n)`: approximately 2 GB at degree 1,000, 2 TB at 10,000, and 2 PB at 100,000. The constructive canonical-generating-sequence lemma gives at most `n log n` coset elements and `O(n² log n)` dense point entries, with trivial-domain cases handled explicitly. Introduce it early, still budget output, and retain the full-transversal format only as a small reference/diagnostic option. Symbolic/sparse forms need deterministic semantic encoding rules.
7. **Preserve sharing in extensional nested-object encoding.** `x₀=literal`, `xᵢ₊₁=(xᵢ,xᵢ)` has linear stored DAG size but exponentially expanded tree output. Define canonical DAG references, exact normalisation, acyclicity, stored/output sizes and comparison costs; avoid accidental occurrence expansion.
8. **State honest complexity parameters.** Include `n`, working vertices `N`, incidences, total tuple arity, relations/labels, literals, multiplicity bit lengths, generator/support sizes, chain/orbit/word lengths, search nodes/leaves and output bytes. Charge import, group construction/verification, preprocessing, every refiner, rollback, comparison and final encoding. A local sparse-refinement upper bound is not an end-to-end bound.
9. **Incorporate the lower-bound discipline below.** Preserve the distinction between universal/contract bounds, algorithm-specific work bounds, hardware-model bounds and engineering estimates. Give per-result delta reporting and cold/warm/residency contracts. Do not turn observed avoidable work into a universal performance floor.
10. **Bound all live memory.** Include trails, snapshots, group/provenance caches, in-flight tasks/publications, verification scratch and output. Base worker arrays alone are not a memory bound. Specify recomputation/streaming and single-task resource-limit behaviour.
11. **Specify the concurrent coverage state machine.** Define ownership and transitions, task creation/stealing, symmetry representative retention, cancellation/helpers, reclamation, completion detection and checkpoint coverage. Distinguish trusted resumptions from independently verified checkpoints. Keep scheduler correctness separate from mathematical search correctness.
12. **Scope APIs and verification honestly.** Define partial-result validity/completeness, capacity limits, ownership, callbacks and output failures. Keep the CPU core and optional GPU backend contracts distinct. Put Lean semantic proofs, certified group operations, adapter proofs and C/runtime verification into separate milestones.

The core mathematical constructions should be retained unless a revision supplies a reason and replacement proof. Under the stated right action, full-list minimisers satisfy `t_(L^h)=h⁻¹t_L`, and `Hr` splits disjointly as `⋃_b H_a t_b r`. The main report received an independent Sol mathematical audit with no material error found. Do not copy ambiguous permutation formulas from a paper without checking conventions.

## Complexity and performance facts to preserve

- The permitted `LEX_MIN_IMAGE` interface includes NP-hard cases. The report gives an explicit Independent Set reduction using upper-triangle adjacency ordered by increasing larger endpoint with `0<1`. This does not establish hardness of arbitrary canonical-image output, an unconditional exponential bound, or the same reduction for every encoding.
- Neuen–Schweitzer's exponential IR bound applies to fixed-dimensional WL-realizable operators in their model, even with automorphism knowledge. Prove a profile meets those hypotheses before invoking it. Arbitrary custom refiners are not automatically covered.
- The canonical-generator lemma concerns ordered-domain representations. It does not solve subgroup conjugacy. R2's cited subgroup-conjugacy corollary depends polynomially on group order, not just generator input length.
- The hardware model is Ryzen 9 9950X, 64 GiB dual-channel DDR5-5600, RTX 5080 with nominal 16 GiB VRAM and PCIe 5.0 ×16. Fixed-configuration interface ceilings used are 89.6 GB/s DDR, 960 GB/s GDDR and 63.015 GB/s per PCIe direction. Latency, sustained bandwidth, cache-topology details and launch costs marked as estimates remain unmeasured assumptions.
- A mandatory 1.2504 GB broad dense-count row read gives a 13.96 ms host-DRAM floor or a 1.303 ms resident-GPU-memory floor. Upload alone gives 19.84 ms. These are conditional movement bounds, not full solve times or a GPU speedup proof.
- Report `Δ=T−L`, `ρ=T/L`, and fractional gap `(T−L)/L` where applicable. A loose lower bound does not make all time above it avoidable. A fixed-work bound cannot establish optimality among algorithms doing different work.
- Optimal legal packing, symbolic forms, caching and reuse matter. Distinguish logical traffic from mandatory transfers across a hierarchy boundary; combine overlapping resource bounds with a maximum and sequential dependencies with justified sums.

## Local evidence and proof scope

Use the local links in the literature audits. Important entry points:

- Algorithm provenance: `review_sources/algorithms/manifest.json` and `manifest_supplemental.json`.
- Canonical-image TeX: `review_sources/algorithms/2209.02534v4/paper.tex`; full-list minimisation around lines 1226–1239, normalised refiners around 1273–1300.
- Constructive canonical generators: `review_sources/algorithms/1803.06858v1/articles/canonization.tex`, lines 140–175 (Lemma 21).
- IR lower bound: `review_sources/algorithms/1705.03283v1/ir-analysis.tex`, hypotheses around 298–307 and theorem at 311–319.
- Karp primary scan and selected page images: `review_sources/algorithms/Karp_1972.pdf` and `Karp_1972_page9.png` / `Karp_1972_page10.png`. The PDF has no text layer; do not treat empty extracted text as evidence.
- Formalisation provenance: `review_sources/formalisation/PROVENANCE.md`; paper TeX under `papers/isocert-2112.14303v5`, pinned Isabelle/C++ under `code/isocert`, selected pinned mathlib files under `mathlib/mathlib4`.
- Hardware provenance: `review_sources/hardware/README.md` and `manifest.json`. AMD JavaScript portal responses are explicitly not the requested optimisation guides; they do not substantiate cache-latency or execution-port claims.

The Isabelle prior art proves the abstract graph scheme, concrete mathematical contracts, proof-system soundness and an abstract checker. Executable C++ checker refinement remains future work in that paper. Mathlib supplies groups/actions/cosets and Schreier's lemma, not a ready efficient verified BSGS/canoniser found by this audit. The spec's permutation product is opposite to usual `Equiv.Perm` multiplication; specify and prove a convention bridge.

A narrow Lean theorem/reference implementation is manageable. An efficient arbitrary-group kernel, full adapter scope, mutable C17 storage, atomics, SIMD/GPU and checkpoints are additional verification projects. The two review documents' effort ranges refer to different milestone scopes; reconcile them when writing the plan and label estimates as judgments. A certificate checker is the recommended initial assurance boundary. No Lean proofs or production C code have been written or built in this review.

## Validation, Git and stopping condition

The existing `Canonicalisation_Review_Checks.py` already passed in approximately 0.26 seconds: 40 subgroups across degrees 0–4, 3,536 leaf identities, 2,997 coset partitions, 2,059 normalised-orbit cases and 361 graph/prefix cases. These are finite sanity checks, not formal proofs or production tests. Do not rerun or expand them absent a relevant mathematical change. No hardware benchmark ran.

For this document revision, check consistency of action conventions, all operation/completion contracts, complexity quantifiers, profile/encoding decisions, references, numeric units, memory budgets and plan dependencies. Keep validation proportional. A final targeted read-through and link/diff checks are appropriate. Do not build Lean or implement C merely to demonstrate progress on this task.

Git procedure:

1. Inspect status and existing baseline; preserve unrelated changes. Initialise Git only if the handoff baseline is unexpectedly absent.
2. Revise the spec, create the implementation plan and review-response mapping, and update supporting documents only where necessary for consistency. Record new version/date and the reviewed baseline commit.
3. Retain primary local evidence and provenance. `.gitignore` excludes re-downloadable source archives and interpreter/build scratch; those ignored archives stay available locally. Do not re-extract large trees or discard evidence.
4. Stage explicit intended paths, inspect the exact staged set and diff, and commit with a description of the actual revision. Avoid `git add -A`, remote pushes and unrelated cleanup. If the environment restricts `.git` writes, use the authorised escalation path; the user's instruction already authorises this local commit.
5. Verify commit success and repository status. Report the commit hash, revised files, key decisions and any material remaining limitation.
6. **Stop after the commit and concise report. Do not start implementation or a further research cycle.**
