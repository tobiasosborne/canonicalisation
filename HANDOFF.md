# Handoff: canonicalisation, foundation slices S1–S6 on `main`

**State on 2 October 2026 (after a short follow-up session; the first implementation session ended 1 October).** Specification v2.1 (2 October 2026; v2.0 plus the editorial decisions listed under "Decisions taken on 2 October 2026" below, no golden value changed) is the normative document. The repository is scaffolded, has a [milestone plan](docs/implementation-plan.md) and a [detailed work-package plan](docs/detailed-implementation-plan.md), and is being implemented in **vertical slices** (detailed plan §0a): a written brief under `docs/slices/`, an Opus implementer, a separate high-effort code review, fixes, then landing on the working branch and fast-forwarding `main`. Slices **S1–S6 are landed, reviewed and on `main`**: every objective of spec §3 except the constraint predicates works end to end with oracle agreement. The **S7 brief is a draft for the maintainer's review** (`docs/slices/S7.md`): it carries the first pruning lemma and the certificate format and must not be implemented before that review. No Lean proofs, benchmarks or performance work exist, by decision. Read [README.md](README.md) first for what the project is; this file records state, decisions, constraints and next steps.

## What works now (all through the public API and `tools/canon-cli`)

| Capability | Slice | Evidence |
|---|---|---|
| P1 canonical image of a subset | S1 | 539 cases: every subgroup of Sym(n), n ≤ 4, every subset, two generating sets, byte-identical with the Python model |
| P1 canonical image of a coloured directed multigraph (O stage, Nat encoding, simple-undirected wrapper, SIMPLE-UPPER-1 key) | S2 | 664 exhaustive and 2880 sampled digraphs; every stream parsed by the strict dumper |
| Deterministic Schreier–Sims chain with fixed policies, independent verifier (written before the constructor), provenance, rebase, §7.2 tuple minimum; explicit-enumeration backend kept as oracle | S3 | Byte identity between backends on every tier; 200 random groups; Sym(12), a 64-cycle; ten certificate mutations rejected |
| §8.1 coset enumerator, constrained least-element descent, `LEX_MIN_IMAGE` (both orders), `TRANSPORTER_ONE`, `STABILISER`, `TRANSPORTER_COSET`, canonical `Group(H)` and coset bytes, deterministic witness, `verify_witness` | S4 | Every objective against brute force on T1 and random groups to degree 7, both backends; enumerator visits exactly G, each element once |
| Nested objects: arena, extensional normalisation and height numbering, strict CDAG-2 decoder, canonical-form validator, action on nested objects with permutation, subgroup and coset leaves (stored verified chains, conjugated), import from streams | S5 | 280 random nested objects against a Python oracle, both backends; imported subsets and graphs identical to built ones; every decoder rule tested |

| Typed labeling coset (λ = ρt, complete `Aλ` payload) and signed canonical image (characters validated by the §8.4 lift on both backends, §7.3 generator fast path, one-sided zero certificate with the odd automorphism as witness, nonzero via complete stabiliser, `00` payload, extended `verify_witness`) | S6 | Every ρ and every character on the T1 tier against the model, both backends; covariance `s(x^h) = χ(h)s(x)`; representative change and coordinate rename invariance |

Still stubs: `CONSTRAINT_*`, `solve_batch`, checkpoints; the checker prints a placeholder until S7.

## Process (follow it; it has caught an error in every brief so far)

1. Write or revise the brief `docs/slices/Sk.md` (scope, API, design with spec citations, tests, definition of done).
2. Launch an Opus implementer with the brief, the earlier briefs and notes, the spec sections, and the standing rules (below). It commits with explicit paths on the working branch and does not push.
3. Verify independently: `rm -rf build && make && make SANITIZE=1 BUILD=build/san test && make check && cmake -S . -B build/cm -DCANON_SANITIZE=ON && cmake --build build/cm && ctest --test-dir build/cm`.
4. Run `/code-review high <base>..HEAD`; send every finding to the implementer; verify the fix commit the same way.
5. Record the brief's errors the implementer found in the detailed plan; update README, HANDOFF and CLAUDE.md; commit; push the branch and fast-forward `main` (`git push origin HEAD:main`).

Each slice has cost about 0.5–0.75 M subagent tokens and 40–60 minutes wall-clock including review and fixes. Reviews have found no mathematical defect so far; findings have been contract gaps, hot-path waste, lifetime hazards and duplication, all fixed within the slice.

## Standing constraints

- **Goal.** An exact, native C canonicalisation library with broad finite permutation-group coverage and excellent performance; correctness and complexity first; performance reported as gaps to justified lower bounds.
- **Evidence.** Local primary sources with provenance (`review_sources/SOURCES.json`); web summaries are leads only; nothing third-party is redistributed.
- **Machine load.** Light: `nice -n 19` for anything non-trivial; no downloads, dependency builds or mathlib fetches without the user's go-ahead. The sanitizer `ctest` run now takes about five minutes because of the Python end-to-end tiers; keep sample sizes in check.
- **Code rules** (`CLAUDE.md`, `CONTRIBUTING.md`): C17, no dependencies, the strict warning set with `-Werror`, sanitizer-clean; cite the spec section at every implemented rule; no golden constants in `src/` or `tools/`; one checked allocator and growth helper (`src/arena/alloc.h`), checked arithmetic (`src/arena/checked.h`), the shared stable sort (`src/util/sort.{h,c}`), atomic refcounts, grow-only workspace scratch, no borrowed pointers after a run, immutable contexts, backend-agnostic API through `canon_group_ops`.
- **Git.** Explicit paths only; never `git add -A`. The user has asked (1 October 2026) that everything be merged to `main`: fast-forward `main` after each landing. Implementers sign their commits with their own model's attribution.

## Repository layout

| Path | Content |
|---|---|
| [docs/specification.md](docs/specification.md) | Architecture/implementation specification v2.0 (normative) |
| [docs/implementation-plan.md](docs/implementation-plan.md) | Milestones M0–M8, H0/H1, gates, obligations ledger, effort judgments |
| [docs/detailed-implementation-plan.md](docs/detailed-implementation-plan.md) | Work packages, internal interfaces, decided policies, §0a slice process and schedule, WP0.9 external oracle tier, corrections found during slices |
| `docs/slices/` | Briefs `S1.md`–`S7.md` (S7 is a draft) and notes `S1-notes.md`–`S5-notes.md` (spec readings, deviations, review fixes, counters, what was left out) |
| [docs/performance-lower-bounds.md](docs/performance-lower-bounds.md) | Hardware model, U/A/H bound taxonomy, envelopes |
| `reviews/` | Referee report on v1.0, response, literature audits, TensorGR learnings |
| `include/canon/canon.h` | Public header: statuses, objective tags, frozen IDs, opaque handles, the S1–S5 API (provisional until M4) |
| `src/<module>/` | perm, bsgs (chain, verify, provenance, conjugate, explicit), coset (descent, enumerator), object (subset, graph, dag, root dispatch), encoding (wire, Nat, streams, Group/Perm, SIMPLE-UPPER-1, CDAG-2 encoder/decoder/validator), partition, refine (P1 stages), search (P1 tree, objectives), api, arena, util; scheduler, checkpoint, metrics, symmetry, cpu_dispatch are READMEs only |
| `checker/` | Independent checker stub (shares no code with `src/`; S7 fills it) |
| `tools/` | `canon-cli` (every objective, both backends, FORMAT records, `validate`), `hexdump_stream.py` (strict CDAG-2 dumper) |
| `tests/c/`, `tests/python/` | 26 C test programs; `test_e2e.py` tiers T1, G1, G2, S4 objectives, D1, under both backends; golden vectors; dumper and compare tests |
| [refs/](refs/README.md) | M0 blind protocol, `SEALS.md` (empty), seven-field `FORMAT.md`, `compare.py` with `--plant`, `vectors/golden.json` (spec §7.4, test-verified) |
| [checks/review_checks.py](checks/review_checks.py) | Finite sanity checks and the Python P1 model the e2e tiers compare against (not a blind reference) |
| `review_sources/` | Provenance only |
| `bench/`, `lean/` | Ledger schema placeholder; Lean skeleton (docstrings only, not built) |
| `CLAUDE.md`, `CONTRIBUTING.md`, `.github/workflows/ci.yml` | Agent and contributor rules; CI: gcc and clang with and without sanitizers, ctest, finite checks, Python tests |

## History and baselines

| Commit | Content |
|---|---|
| `7ad98cb` | Reviewed **v1.0 baseline**; spec SHA-256 `924699142d622de142e63f1145c91b436612aa88653e77311da8ecb2d2cfb431`; referee permalinks point here (pre-publication `fb014c9`) |
| `59cb67c` | v2.0 revision (pre-publication `3ec26b1`) |
| `8896edc` | Reorganisation, provenance inventory, README, LICENSE |
| `c473d49`, `e45e5b2`, `07dace4` | Scaffold; detailed plan; vertical-slice process and S1 brief |
| `ce4bfa2`, `b794f23`, `d31e38e`, `880a052`, `0407fcd` | Review-fix commits closing S1, S2, S3, S4, S5 |
| `9c07db7`, `24c715f`, `e2a0fb3`, `b127a86`, `5d7aa51` | "Land slice" doc commits for S1–S5 |
| `563d799`, `82e3567` | S7 draft brief; HANDOFF rewrite |
| S6 commits and the "Land slice S6" commit | See `git log`; the latest on `main` at the time of writing |
| 2 October 2026 commits | Signed-route sift (`cd7a5eb`), blind brief and schema, batch reading, spec v2.1 (SHA-256 `b4e23af1a9acb820e5bb42abc68ed34e39142033fd44e76879c21c4ee0e65b33`), S7/S8 decisions, maintainer answers |

The pre-publication history with third-party files is in `~/Projects/canonicalisation-pre-publication-2026-09-30.bundle` on the maintainer's machine; never push it. Local copies of the sources remain git-ignored under `review_sources/`.

## Decisions taken during the slices (details in the notes and the detailed plan)

1. **Backends.** The chain is the default; the explicit table is a creation-time context option and the oracle in C tests; `max_group_order` applies to the explicit backend only.
2. **Orders are `uint64`.** The chain refuses `|G| ≥ 2^64` with `CAPACITY_LIMIT`; §9.4 rule 1 is decided with overflow-detecting factorial products; `canon_nat` (multi-limb) is deferred to the slice that lifts the limit.
3. **Quota.** `max_search_nodes` counts P1 `NODE` tokens plus enumeration `visit` calls, one quota per solve, so `CAPACITY_LIMIT` is a function of the input and descriptor.
4. **Witness.** For the unpruned tree the least leaf witness attaining the key equals the §3 deterministic witness (checked on every tier, not assumed); `witness_mode = DETERMINISTIC` computes it from the complete stabiliser.
5. **Constrained descent.** Constraints first, then minimisation (the brief had it interleaved; S4 showed the counterexample).
6. **Output size.** Exact stream length for subsets, graphs and nested objects with tags 01–06; the §9.4 bound for subgroup and coset leaves, whose payload length is not invariant under conjugation (S5 counterexample: C₄ on 4 points).
7. **Group leaves.** Verified chains are stored in the arena and conjugated with `canon_bsgs_conjugate` on the action path, never rebuilt per tree node.
8. **Costs accepted for now (M5 work):** one chain rebuild per step of `tuple_min` and of the coset descent; transient chains in the §9.4 writers; on the signed route each hit is sifted through `A_known` before χ (2 October 2026 follow-up: 264 verified rebuilds instead of 476 on T1, outputs identical) at one extra sift per even non-member; the sanitizer e2e runtime is kept near four minutes by `CANON_E2E_FAST` seeded subsets, while `make check` runs every tier in full (about 105 s).

## Decisions taken on 2 October 2026 (maintainer, on the session's recommendations)

1. The signed-route membership test before χ stays: byte-identical outputs, fewer verified rebuilds; not performance work.
2. ref-a is the sealed snapshot of the unpruned C path, recorded as not blind with respect to `checks/` and `tools/`; ref-b (Julia) is the blind implementation; spec §20's "two blind" is read as "two independently written".
3. Spec readings the brief raised are settled by stating them in spec v2.1 (open item 6 below), never by telling the blind author what the C path does.
4. Quota unit: one per P1 `NODE` token and one per §8.1 enumerator visit, one quota per solve; default 2^20 (`include/canon/canon.h`).
5. Case schema: the `golden.json` field names are the schema; `refs/CASES.md` Part B is definitive; generated files carry a top-level `cases` array.
6. Every M0 case requests the deterministic witness and `compare.py` compares it; the nine flags are out of M0 scope.
7. ref-b carries its own minimal JSON parser; no package download.
8. `refs/SEALS.md` gains `Toolchain` and `Blind w.r.t.` columns.
9. S7: quota under pruning counts the explored nodes under the fixed sequential policy, selected by a work-policy ID in the capacity descriptor (`0x0001` unpruned reference, `0x0002` the S7 policy, default `0x0002`; the profile ID stays `0x0001`; the certificate header gains `U16(work_policy)`; parallel modes reproduce the count); certificate v0 embeds a rebased chain per stage; implicit automorphism discovery is deferred to S7b.
10. `solve_batch` contract as now stated in spec §17 v2.1 (open item 6).
11. The specification is v2.1, SHA-256 `b4e23af1a9acb820e5bb42abc68ed34e39142033fd44e76879c21c4ee0e65b33` at the landing commit; v2.0 (commit `cb76c0d`) is named in its header as the revised text and v1.0 remains the reviewed baseline.

## Open items, in order

1. **S7 (decided 2 October 2026, ready to implement after the S8 batch reading is folded in).** The three open points are settled in `docs/slices/S7.md` §6: the quota under pruning counts the nodes explored under the fixed sequential prune policy, selected by a work-policy ID in the capacity descriptor (`0x0001` unpruned reference, `0x0002` the S7 policy; the profile ID stays `0x0001` because the outputs are P1's), and parallel modes must reproduce that count (spec §11.1 v2.1); certificate v0 embeds a rebased chain per G stage; implicit automorphism discovery from equal leaf keys is deferred to its own slice S7b.
2. **S8:** capacity admission and live-memory ledger, metrics, `solve_batch`, sink pause/resume, fault injection and fuzzing (detailed plan §0a schedule). `docs/slices/S8-batch-reading.md` (2 October 2026) quotes every spec sentence on batches: §17 pins per-input statuses and input order; the overall status, stop-or-continue, call-level errors, admission scope and execution mode are open and must be decided in the S8 brief.
3. **External oracle tier (detailed plan WP0.9), needs the user's go-ahead for acquisition:** GAP with the `images` package first, SymPy second, nauty third, xperm with the tensor wrapper; each pinned in `SOURCES.json`; none checks P1 traces or bytes.
4. **M0 blind reference (brief answered 2 October 2026, ready to commission).** `refs/BRIEF.md` carries the maintainer's answers to its fourteen questions and `refs/CASES.md` is the definitive case schema (golden.json field names; WP0.2 amended). Decisions: ref-a is the sealed snapshot of the unpruned C path, recorded in `refs/SEALS.md` as not blind with respect to `checks/` and `tools/`; ref-b in Julia carries its own minimal JSON parser (no downloads); every M0 case requests the deterministic witness; the nine flags are out of M0 scope; the spec readings the brief asked about are now stated in spec v2.1 rather than told to the authors. Next: commission ref-b from an author who has not seen `src/`, `checks/`, `tools/` or `tests/`.
5. **Deferred features:** `CONSTRAINT_ONE/ENUM`; the signed labeling orientation σ_ρ (§3.1); relations record `0a` and nested graph records (`UNSUPPORTED_ACTION` today); `canon_nat`; public cancellation; partial results on interruption.
6. **Spec v2.1 (applied 2 October 2026).** Items (a), (b), (d), (e), (f), (g), (h) and (i) of the earlier list are now stated in the spec: §9.1 verifier nesting condition; `Group(1)` is `01 00000000` for every degree; cycle conjugation for the convention vector; status values 0–7 in listed order; `SIMPLE-UPPER-1` for n ≤ 1 is `U32(n)` alone; rule-1 Group blocks are disjoint and ordered; the labeling stabiliser is reported on Ω; the quota counts the traversal named by the descriptor's work-policy ID (root shortcuts visit no nodes; `0x0001` is the unpruned reference; a count-changing prune policy has its own ID and the profile ID names output semantics only). §17 now carries the `solve_batch` contract (call-level `INVALID_INPUT` only for NULL workspace or arrays or size overflow; otherwise every input attempted in order with its own status; `COMPLETE` iff every input was attempted; admission per input). Still open: (c) the DAG golden case's trace and witness are stated only by reference; the signed labeling orientation σ_ρ remains deferred (an implementation may return `UNSUPPORTED_ACTION`). `docs/implementation-plan.md` still says "aligned with v2.0"; nothing in it changes.
7. **Earlier open items unchanged:** the engineering-appendix decision (restore v1.0's engineering guidance or record it as dropped); H0 (retrieve the `hex` repositories at pinned commits, needs network); named obligations SIGN-COVER, PROFILE-EQUIV, ADAPTER-FAITHFUL, GROUP-CERT, RUNTIME-REFINE, SOURCE-GATE; Lean (M1) after the foundation slices.

Do not start S7 or a new research cycle without the user's direction.

## Validation record

- **Finite checks.** `nice -n 19 python3 checks/review_checks.py` passes (40 subgroups at degrees 0–4; 3,536 leaf identities; 2,997 coset partitions; 2,059 normalisation cases; 361 graph-prefix cases; 143 subset and 494 digraph P1 transports; 307 labeling cases; 95 signed cases; 16 golden checks; 10 greedy sequences). Sanity checks, not proofs; the file is unmodified since publication.
- **Slices (state after S6).** `make` and `make CC=clang`: zero diagnostics under `-Werror`. `make SANITIZE=1 test`: 29 C tests clean under ASan/UBSan (gcc; the local clang has no ASan runtime, CI covers it). `make check`: finite checks plus 52 Python tests under the chain backend and 32 under the explicit backend, about 100 s. CMake with `CANON_SANITIZE=ON` and `ctest`: 32 of 32; the sanitizer e2e tiers run reduced seeded subsets under `CANON_E2E_FAST` to keep that run near five minutes (see `tests/python/README.md`). Every tier is byte-identical with the Python model under both backends; pruned paths do not exist yet.
- **Hand verification and provenance.** As recorded at publication: every §7.4 golden case re-derived by hand; `fetch_sources.py` reports 150/150 OK on the maintainer's machine; all relative links resolve.
