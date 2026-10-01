# Handoff: canonicalisation specification v2.0, public repository

**State on 1 October 2026.** The design documents are complete at specification v2.0 and published as [tobiasosborne/canonicalisation](https://github.com/tobiasosborne/canonicalisation) under AGPL-3.0-or-later. The repository is now scaffolded (build, public header, module layout, blind-reference harness, golden vectors, Lean skeleton, CI) and has a [detailed implementation plan](docs/detailed-implementation-plan.md). Delivery is by vertical slices with an Opus implementer and a closing code review per slice (detailed plan §0a); Lean work is deferred until the foundation slices land. Slice S1 (subset canonical image, explicit group backend, public API, CLI, end-to-end test against the Python model) has landed and been reviewed; see `docs/slices/S1.md` and `S1-notes.md`. No Lean proofs or benchmarks exist. Read [README.md](README.md) first for what the project is. This file records state, decisions, constraints and the next steps for whoever continues.

## User intent and standing constraints

- **Goal.** An exact, native C canonicalisation library with broad finite permutation-group coverage and excellent performance. Correctness and complexity come first. Performance is reported as gaps to explicitly justified lower bounds, on realistic commodity hardware (the model is a Ryzen 9 9950X, DDR5-5600 and an RTX 5080), with the best legal data representations.
- **Evidence.** Literature ground truth is **local primary sources, preferably TeX**, with provenance. Web summaries, including TensorGR's web-sourced literature claims, are leads only. Source code and manufacturer documents support implementation and hardware claims respectively; they are not measurements.
- **Machine load.** The user's machine has CPU contention. Keep processing light: no calibration benchmarks, dependency builds, mathlib unpacking or broad filesystem scans. Use targeted reads and bounded checks, at `nice -n 19` when running anything non-trivial.
- **Delegation.** The user authorises subagents, including other model families (GPT/Codex via `codex exec`, model `gpt-6-astra` at `xhigh` is the configured default). Review delegated output skeptically before committing it.
- **Git.** Commit when a task is complete. Stage explicit paths and never use `git add -A` at the root. Push to `origin/main` only when the user asks, or when the task is itself publication.

## Repository layout

| Path | Content |
|---|---|
| [docs/specification.md](docs/specification.md) | Architecture/implementation specification v2.0 (the normative document) |
| [docs/implementation-plan.md](docs/implementation-plan.md) | Milestones M0–M8, H0/H1, gates, obligations ledger, effort judgments |
| [docs/performance-lower-bounds.md](docs/performance-lower-bounds.md) | Hardware model, U/A/H bound taxonomy, envelopes, small-instance regime (§6.7) |
| [reviews/referee-report.md](reviews/referee-report.md) | Referee report on v1.0: 18 findings and release gates (historical) |
| [reviews/review-response.md](reviews/review-response.md) | Maps findings 1–18 and TensorGR T1–T14 to v2.0 decisions and open obligations |
| [reviews/algorithm-literature-review.md](reviews/algorithm-literature-review.md), [reviews/formalisation-literature-review.md](reviews/formalisation-literature-review.md) | Primary-source audits |
| [reviews/tensorgr-learnings.md](reviews/tensorgr-learnings.md) | Lessons from [TensorGR.jl](https://github.com/tobiasosborne/TensorGR.jl) (commit `b492891`) |
| [docs/detailed-implementation-plan.md](docs/detailed-implementation-plan.md) | Work packages per milestone, internal C interfaces, decided policies, test matrices, sequencing, risks |
| [checks/review_checks.py](checks/review_checks.py) | Finite sanity checks (about 0.6 s) |
| [review_sources/](review_sources/README.md) | Provenance only: `SOURCES.json` inventory plus `fetch_sources.py` verify/rebuild |
| `include/canon/canon.h`, `src/`, `checker/` | Public header with §3.2 statuses, §3 objective tags, frozen IDs, §17 opaque handles and the S1 API (context, capacity descriptor, subset builder, result accessors; signatures provisional until M4); one `src/` directory per spec §5 module, S1 code in perm, bsgs/explicit, object/subset, encoding, partition, refine, search, api; independent checker stub |
| `docs/slices/` | Per-slice briefs (`S1.md`) and implementation notes (`S1-notes.md`: spec readings taken, deviations, what was left out) |
| [refs/](refs/README.md) | M0 blind protocol, `SEALS.md`, `compare/FORMAT.md`, `compare/compare.py` (with `--plant`), `vectors/golden.json` (spec §7.4 transcribed and test-verified) |
| `tests/`, `tools/`, `bench/`, `lean/` | C and Python tests; strict CDAG-2 dumper; ledger schema; Lean skeleton (docstrings only, not built) |
| [CLAUDE.md](CLAUDE.md), [CONTRIBUTING.md](CONTRIBUTING.md), `.github/workflows/ci.yml` | Rules for agents and contributors; CI runs gcc and clang builds with and without sanitizers, ctest, the finite checks and the Python tests |

## History and baselines

The Git history was rewritten before publication so that third-party files are not redistributed. The rewrite removed 141 third-party files (papers, TeX, a scan, vendor documents, isocert code, mathlib files) from both earlier commits. It changed nothing else.

| Commit | Content |
|---|---|
| `7ad98cb` | Reviewed **v1.0 baseline**; spec SHA-256 `924699142d622de142e63f1145c91b436612aa88653e77311da8ecb2d2cfb431`. Referee line links are permalinks to this commit. Its pre-publication hash was `fb014c9`. |
| `59cb67c` | v2.0 revision (pre-publication hash `3ec26b1`); referee report SHA-256 there `3b061b846e454957a8c7857e3a79b7552fdcb7c09bded7d9d2c4af186a2f38b8` |
| `8896edc` | Reorganisation, provenance inventory, README, LICENSE, this handoff |
| next | Repository scaffold and detailed implementation plan (branch `claude/jolly-lamport-jvbe9f`) |

- **Backup.** The full pre-publication history, including the removed files, is kept locally in `~/Projects/canonicalisation-pre-publication-2026-09-30.bundle` (verified with `git bundle verify`). Never push it.
- **Local copies.** The removed files remain on the local disk under `review_sources/`, git-ignored; `python3 review_sources/fetch_sources.py` reports 150/150 OK.
- **Rewritten links.** In the reorganisation, the referee report's link targets were rewritten: absolute paths became relative ones, and v1.0 line links became baseline permalinks. Its text is otherwise unchanged, so its file hash now differs from the historical value above.

## v2.0 decisions (details in the review response)

1. **Profile P1 and wire grammar `CDAG-2`.** P1 (`0x0001`) is executable. `CDAG-2` (`0x0002`) is concrete, with a canonical DAG numbering that preserves sharing. Golden vectors are hand-verified.
2. **Typed labeling cosets.** λ = ρt ∈ Gρ, with complete coset Aλ and explicit coordinate reconstruction.
3. **Objective-specific completeness.** Each objective has its own complete reference algorithm built on disjoint coset enumeration. Image-only pruning never proves a full stabiliser.
4. **Group encoding.** Exact symmetric-product detection comes first, otherwise greedy canonical generators (O(n² log n) entries). The cubic transversal format is diagnostic only.
5. **Signed objective.** A verified odd automorphism certifies zero; a nonzero result needs complete-stabiliser evidence.
6. **Runtime.** Deterministic capacity admission, a full live-memory ledger, the mutex-linearised coverage state machine, R1–R3, and the `CALLER_THREADS` / `CORE_POOL` modes.
7. **Accounting and proof layers.** The U/A/H versus E bound discipline, with Δ/ρ/δ reporting. Assurance layers are separate milestones.

## Open items and next steps

1. **Engineering appendix decision (user's call).** v2.0 compressed some v1.0 engineering guidance: the per-kernel traffic table, the calibration/dispatch table, task payload formats, the 20× grain-size target, physical renumbering, and the refinement value model. Either restore it as an appendix to the spec, adapted to v2.0 contracts, or record that it is deliberately dropped. v1.0 at `7ad98cb` holds the text.
2. **H0.** Retrieve `leanprover/hex-graph-iso` and `leanprover/hex-perm-group` at full pinned commits, with SHA-256 manifests, and audit their scope, before any plan item depends on them. This needs network access and the user's go-ahead.
3. **Next slice: S2** (graphs and the O stage), per the schedule in the detailed plan §0a. Each slice: brief under `docs/slices/`, Opus implements, separate review, fixes, land.
4. **M0 (first implementation milestone).** Brief two blind, independent reference evaluators of P1/CDAG-2/API rules. They must share no code, and must show agreement beyond oracle range plus planted-corruption sensitivity. Retain both. The protocol, interchange format, comparison script and golden corpus are in `refs/`; per the detailed plan §0a item 6, the blind reference is ref-b in Julia, commissioned after S6 from an author who has not seen `src/`, `checks/` or `tools/`; a sealed snapshot of the unpruned C path serves as ref-a. Remaining steps: add the `group_hex` field to the format, write the case schema, the blind brief, the oracle and the case generator (detailed plan §3, §15).
5. **Scaffold readings to confirm.** The scaffold made four readings of the spec that a v2.1 editorial pass should make explicit (detailed plan WP0.1): `Group(1)` has no stated degree; the DAG golden case's trace and witness are stated only by reference; `(0,2)^(pq)=(2,1)` is read as cycle conjugation; status names have no numeric values (the header assigns 0–7 in listed order). The §17 argument lists in `include/canon/canon.h` are provisional until M4.
6. **Named obligations.** These stay open: SIGN-COVER, PROFILE-EQUIV, ADAPTER-FAITHFUL, GROUP-CERT, RUNTIME-REFINE and SOURCE-GATE (defined in the plan). McKay–Piperno Thm 5, Niehoff, SeQuant and dejavu remain unverified leads.

Do not start implementation or a new research cycle without the user's direction.

## Validation record

- **Finite checks.** `nice -n 19 python3 checks/review_checks.py` passes in about 0.6 s. It covers 40 subgroups at degrees 0–4 (3,536 leaf identities, 2,997 coset partitions, 2,059 normalisation cases, 361 graph-prefix cases). The v2 additions cover:
  - 143 P1 subset transports and 494 P1 digraph transports;
  - 307 labeling-coset cases;
  - 95 signed cases;
  - 16 golden/encoding/convention checks;
  - 10 greedy-group sequences.

  These are sanity checks, not proofs.
- **Hand verification.** The coordinating review re-derived every golden case in spec §7.4 by hand: traces, bytes, witnesses, group and coset payloads, product conventions, labeling and sign examples. It also read the key proofs (coset splitting, zero iff odd stabiliser element, greedy-generator bound).
- **Provenance tooling.** `python3 review_sources/fetch_sources.py` reports all 150 inventory entries OK locally. An offline test re-extracted all 100 archive members from the retained archives with matching SHA-256 values in 0.8 s. The network `--fetch` path for downloads has not been exercised.
- **Link check.** All relative links in the documents resolve to tracked files or to inventory entries rebuilt by the fetch script.
- **Scaffold (1 October 2026).** `make`, `make test`, `make SANITIZE=1 test`, and `cmake` with `CANON_SANITIZE=ON` plus `ctest` (3 tests) all pass with zero warnings under gcc 13; 17 Python unit tests pass; `compare.py --plant` detects a planted nibble flip. Lean files were not built (no toolchain here). clang with sanitizers was not run locally (no ASan runtime in this environment); CI covers it.
