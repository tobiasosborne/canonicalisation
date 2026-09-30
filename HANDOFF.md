# Handoff: v2.0 document revision committed

**State on 30 September 2026:** the v2.0 specification and planning revision was drafted by a Codex agent (gpt-6-astra, xhigh) that ran no state-changing Git commands. The coordinating Claude session then reviewed it and committed it on `main`, on top of baseline `fb014c9`; see `git log`. The review re-derived every golden case in spec §7.4 by hand and re-ran the finite checks. Do not infer permission to implement C/Lean, retrieve sources or run heavy checks from this handoff.

## User intent and constraints

Build toward an exact native C canonicalisation library for broad finite permutation groups, with correctness and honest complexity/performance accounting first. The present task is documentation and bounded mathematical sanity checking only. Primary literature evidence must be local, preferably TeX, with provenance under `review_sources/`. TensorGR web-sourced claims are leads, not established evidence.

During this revision: no network/download, archive extraction, builds, compilers, benchmarks, Lean or Julia; keep CPU use light and reads targeted. Never modify `../TensorGR.jl`. No state-changing Git commands (`add`, `commit`, `stash`, `reset`, `checkout`, `branch`, etc.). These current instructions override the previous handoff's commit procedure. Preserve unrelated work, including `.claude/` and the pre-existing uncommitted TensorGR learnings. No AGENTS.md was present in the workspace or the explicitly checked ancestors.

## Baseline and files

Workspace: `/home/tobiasosborne/Projects/canonicalisation`, branch `main`, baseline `fb014c9`.

Reviewed v1.0 SHA-256: `924699142d622de142e63f1145c91b436612aa88653e77311da8ecb2d2cfb431`. The revised document records this commit/hash/date explicitly. Historical line references in the review documents refer to that baseline, not current line numbers.

Created:

- [Canonicalisation_Implementation_Plan.md](Canonicalisation_Implementation_Plan.md): dependencies, evidence gates, effort reconciliation and optional hex acquisition/reuse gates.
- [Canonicalisation_Review_Response.md](Canonicalisation_Review_Response.md): all 18 findings and T1–T14 mapped to decisions and remaining obligations.

Modified:

- [Canonicalisation_C_Architecture_Specification.md](Canonicalisation_C_Architecture_Specification.md): v2.0, dated 30 September 2026.
- [Canonicalisation_Performance_Lower_Bounds.md](Canonicalisation_Performance_Lower_Bounds.md): small-instance batch regime, v2 consistency and fractional gaps; retained hardware arithmetic/evidence.
- [Canonicalisation_Formalisation_Literature_Review.md](Canonicalisation_Formalisation_Literature_Review.md): scope of the mathlib search and unverified external hex leads.
- [Canonicalisation_Algorithm_Literature_Review.md](Canonicalisation_Algorithm_Literature_Review.md): only a baseline-reference notice/link for consistency.
- [Canonicalisation_Review_Checks.py](Canonicalisation_Review_Checks.py): modest v2 finite checks; runs below one second here.
- This handoff.

Unchanged historical inputs: [Canonicalisation_Referee_Report.md](Canonicalisation_Referee_Report.md) and [Canonicalisation_TensorGR_Learnings.md](Canonicalisation_TensorGR_Learnings.md). Referee SHA-256 remains `3b061b846e454957a8c7857e3a79b7552fdcb7c09bded7d9d2c4af186a2f38b8`. Primary source files and the sibling TensorGR tree were not modified.

## Main decisions now recorded

1. **P1 (`0x0001`) is executable:** initial subset/graph colours, simultaneous graph-count stage then normalised group-orbit stage, fixed-point rule, target cell, singleton extraction, trace framing and leaf comparison all fixed. Non-graph/DAG roots intentionally start with weak refinement.
2. **CDAG-2 (`0x0002`) is concrete:** big-endian lengths/IDs, assigned tags, exact multiplicities, loop/duplicate/relation policies and canonical bottom-up DAG numbering. It preserves extensional sharing instead of expanding occurrences.
3. **Group(H) has deterministic production rules:** exact symmetric-product detection first, otherwise least-outside greedy generators with sparse supports. Full dense transversals are diagnostic-only.
4. **Operations have distinct completion proofs:** canonical tree versus exact coset enumeration consumers for minimum, transporter, stabiliser and general predicates. Complete labeling output uses λ=ρt and Aλ, with typed coordinate reconstruction.
5. **Signed objective:** valid χ, comparison/proof of the two-point lift, checked odd automorphism as a one-sided zero certificate; nonzero uses complete stabiliser evidence. Signed labeling representatives also carry an orientation convention.
6. **Runtime contracts:** caller-owned threads, normative R1–R3, a concrete mutex-based coverage state machine, bounded live state/replay and trusted checkpoint resumptions. Input/budget capacity is distinct from external allocation failure and cancellation.
7. **Performance:** U/A/H bounds remain distinct from E estimates, with Δ/ρ/δ and cold/warm/residency boundaries. Cache-resident batch compute/branch/stream/collection costs supplement large-instance movement bounds; no TensorGR hardware numbers transferred.
8. **Acceptance and proof layers:** blind independent references before semantics acceptance; Lean semantics, certified groups, adapters, checker, concurrency and C/runtime verification are separate milestones. Estimates are judgments with overlapping scopes reconciled.

## Remaining obligations and next step

No finding or TensorGR item was omitted. Document-level decisions do not complete the future implementation/proof gates. In particular:

- **SIGN-COVER** remains open for arbitrary-G canonical search: no claim that its best leaves/pruning expose every odd automorphism. The complete-stabiliser baseline does not depend on this optimisation theorem.
- M0 still requires two blind retained reference implementations and corruption-sensitive agreement. The local Python checker is not that acceptance test and is not a Lean proof.
- M2/M3/M4 need executable group/adapter/checker proofs and public wrapper tests; M5 needs real measurements and budget evidence; M6/M7 need abstract/concrete runtime verification respectively.
- Tensor, auxiliary and graph-only alternative backends require faithful reductions and explicit profiles. Multi-term identities, rational collection and Clifford normal forms are higher layers.
- `leanprover/hex-graph-iso` and `leanprover/hex-perm-group` have not been retrieved. **H0 must obtain local sources at full pinned commits with SHA-256 manifests and audit scope before H1 or any reuse depends on them.** Commit hashes are unresolved, not fabricated. No network use is permitted in the completed revision task; acquisition needs a subsequent authorised task.
- McKay–Piperno Theorem 5, Niehoff, SeQuant and dejavu remain unverified leads where only TensorGR's web-sourced reports are available. The spec makes no normative claim from them.

Known review note: v2.0 compresses some v1.0 engineering guidance. Examples are the per-kernel traffic table, the calibration/dispatch table, task payload formats, the 20× grain-size target, physical renumbering and the refinement value model. v1.0 at `fb014c9` remains the reference for that detail until it is restored as an appendix or deliberately dropped.

Next: await the user's direction. If implementation is later requested, begin with a bounded M0 brief and retained blind evaluators; do not restart broad literature research or run builds merely to finish the documentation task.

## Validation record

Exact mathematical check command:

```
nice -n 19 python3 Canonicalisation_Review_Checks.py
```

Final run passed in about 0.53 seconds. Original coverage: 40 subgroups at degrees 0–4; 3,536 full-list identities; 2,997 coset partitions; 2,059 normalised-orbit cases; 361 graph/prefix cases; full-transversal output arithmetic.

V2 additions passed: 143 P1 subset transports; 494 P1 directed-graph transports (degrees 0–2, loops and multiplicities 0–2, equal/distinct vertex colours); 307 labeling-coset cases (degrees 0–3, reconstruction, representative changes and source renaming); 95 signed subset/character cases; 16 golden/encoding/convention checks; 10 greedy-group sequences. Additional assertions cover a malformed signed lift, graph duplicate aggregation, set allocation/insertion differences and a shared depth-60 tuple DAG. The P1/sign/labeling loops use small groups only; these checks are not general proofs, a full wire-parser test, production validation or a benchmark.

Hand verification covered every included golden case: empty/singleton subsets, the canonical/minimum distinction, branching trace, graph split plus final stable sweep, repeated literal sharing, sparse/symbolic group payloads, coset payload, noncommuting products, typed labeling and zero/nonzero sign examples.

Final targeted read-through covered action/labeling/sign conventions, profile/wire decisions, objective/completion flags, capacity/runtime accounting, lower-bound categories and plan dependencies. Final read-only checks passed:

- `python3 /tmp/check_canonicalisation_docs.py`: 9 explicitly named documents, 154 local link targets (70 relative), no missing targets; changed-document whitespace/table columns/fences valid; all 18 findings and T1–T14 mapped; version/baseline and 23 numbered sections checked. External URLs were not contacted; historical line numbers were deliberately not treated as current locations.
- `git diff --check`: exit 0, no whitespace errors.
- `git diff --exit-code -- Canonicalisation_Referee_Report.md`: exit 0, unchanged.
- `sha256sum Canonicalisation_Referee_Report.md`: matched the historical hash recorded above.
- `git show fb014c9:Canonicalisation_C_Architecture_Specification.md | sha256sum`: matched the reviewed v1.0 hash.
- `git status --short` and `git diff --stat`: only intended tracked modifications, two new plan/response files, and the pre-existing untracked learnings/`.claude/`; nothing staged or committed by this task.

The temporary link checker uses only the nine listed Markdown documents and directly referenced paths; it performs no recursive scan. No research subprocess or build is running.
