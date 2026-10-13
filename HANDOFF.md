# Handoff: canonicalisation, slices S1–S8 on `main`

**State on 13 October 2026 (after the S7, S7b and S8 sessions; the 2 October follow-up and the first implementation session, ended 1 October, precede them).** Specification v2.1 (2 October 2026; v2.0 plus the editorial decisions listed under "Decisions taken on 2 October 2026" below, no golden value changed) is the normative document. The repository is scaffolded, has a [milestone plan](docs/implementation-plan.md) and a [detailed work-package plan](docs/detailed-implementation-plan.md), and is being implemented in **vertical slices** (detailed plan §0a): a written brief under `docs/slices/`, an implementer, a code review, fixes, then landing on the working branch and fast-forwarding `main`. Slices **S1–S8 are landed and on `main`**: every objective of spec §3 except the constraint predicates works end to end with oracle agreement, the canonical image of a subset or graph can emit a certificate that the independent checker `canon-check` verifies, and the §11 capacity admission, the live-memory ledger, metrics, `solve_batch`, sink pause/resume, fault injection and fuzz targets exist. S7, S7b and S8 were implemented by a single agent on the maintainer's instruction ("work alone"), with self-reviews recorded in `docs/slices/S7-notes.md`, `S7b-notes.md` and `S8-notes.md` instead of the separate review of S1–S6. No Lean proofs, benchmarks or performance work exist, by decision. Read [README.md](README.md) first for what the project is; this file records state, decisions, constraints and next steps.

## What works now (all through the public API and `tools/canon-cli`)

| Capability | Slice | Evidence |
|---|---|---|
| P1 canonical image of a subset | S1 | 539 cases: every subgroup of Sym(n), n ≤ 4, every subset, two generating sets, byte-identical with the Python model |
| P1 canonical image of a coloured directed multigraph (O stage, Nat encoding, simple-undirected wrapper, SIMPLE-UPPER-1 key) | S2 | 664 exhaustive and 2880 sampled digraphs; every stream parsed by the strict dumper |
| Deterministic Schreier–Sims chain with fixed policies, independent verifier (written before the constructor), provenance, rebase, §7.2 tuple minimum; explicit-enumeration backend kept as oracle | S3 | Byte identity between backends on every tier; 200 random groups; Sym(12), a 64-cycle; ten certificate mutations rejected |
| §8.1 coset enumerator, constrained least-element descent, `LEX_MIN_IMAGE` (both orders), `TRANSPORTER_ONE`, `STABILISER`, `TRANSPORTER_COSET`, canonical `Group(H)` and coset bytes, deterministic witness, `verify_witness` | S4 | Every objective against brute force on T1 and random groups to degree 7, both backends; enumerator visits exactly G, each element once |
| Nested objects: arena, extensional normalisation and height numbering, strict CDAG-2 decoder, canonical-form validator, action on nested objects with permutation, subgroup and coset leaves (stored verified chains, conjugated), import from streams | S5 | 280 random nested objects against a Python oracle, both backends; imported subsets and graphs identical to built ones; every decoder rule tested |

| Typed labeling coset (λ = ρt, complete `Aλ` payload) and signed canonical image (characters validated by the §8.4 lift on both backends, §7.3 generator fast path, one-sided zero certificate with the odd automorphism as witness, nonzero via complete stabiliser, `00` payload, extended `verify_witness`) | S6 | Every ρ and every character on the T1 tier against the model, both backends; covariance `s(x^h) = χ(h)s(x)`; representative change and coordinate rename invariance |

| Orbit pruning of the canonical-image search by the input generators that fix `x` (rule P1-ORBIT-1, `docs/pruning-rules.md`), selected by the work-policy ID `0x0002` (default; `0x0001` is the unpruned reference), with the quota counting the explored nodes; certificate v0 (`CERT-0`) for the canonical image of a subset or graph; the independent checker `canon-check` (no shared code) replaying every refinement step from a verified chain per G stage, every automorphism and every child entry | S7 | Pruned and unpruned runs byte-identical on trace and bytes on every tier, both backends, identical deterministic witnesses; 1 822 certificates accepted (T1, G1, random groups on 5 and 6 points); 17 mutations rejected with the named reason; `Sym(6)` empty subset 1237 nodes unpruned against 6 pruned |

| Implicit automorphisms `t_L t_best⁻¹` from equal leaf keys, verified by membership and action, inserted into `A_known` and used incrementally at every ancestor (policy `0x0003`, the new default; `0x0002` keeps S7's counts) | S7b | Same agreement as S7 under both prunes; `{0}` under `Sym(4)` 10 nodes unpruned and explicit, 7 with implicit automorphisms; 1 617 T1 certificates accepted under the three policies |

| §11.1 deterministic admission (a plan from the input, objective and descriptor against `max_live_bytes`, default 4 GiB; `CAPACITY_LIMIT` at problem creation never depends on a measurement), the §11.2 live-memory ledger behind every allocation in `src/` (`canon_memory_get`; allocation failure is `RESOURCE_LIMIT`, never a crash), per-result metrics (`canon_result_metrics`: nodes, leaves, explored, pruned, automorphisms, enumeration visits, planned and peak bytes, allocations), `canon_solve_batch` per spec §17 v2.1, sink pause as `CANCELLED` with `canon_result_encode_resume`, fault injection (`canon_mem_fail_after`) and two fuzz targets with a deterministic replay driver | S8 | 1 253 batch entries identical to separate solves; 1 284 solves with measured peak ≤ plan (worst 5 288 / 30 386 bytes); 1 768 single-fault runs each ending in `RESOURCE_LIMIT` with the ledger back at baseline, under ASan; 16 008 fuzz replays, which found and fixed one checker allocation defect |

Still stubs: `CONSTRAINT_*`, checkpoints. Certificates exist for `CANONICAL_IMAGE` on subset and graph roots only (v0).

## Process (follow it; it has caught an error in every brief so far)

1. Write or revise the brief `docs/slices/Sk.md` (scope, API, design with spec citations, tests, definition of done).
2. Launch an Opus implementer with the brief, the earlier briefs and notes, the spec sections, and the standing rules (below). It commits with explicit paths on the working branch and does not push.
3. Verify independently: `rm -rf build && make && make SANITIZE=1 BUILD=build/san test && make check && cmake -S . -B build/cm -DCANON_SANITIZE=ON && cmake --build build/cm && ctest --test-dir build/cm`.
4. Run `/code-review high <base>..HEAD`; send every finding to the implementer; verify the fix commit the same way.
5. Record the brief's errors the implementer found in the detailed plan; update README, HANDOFF and CLAUDE.md; commit; push the branch and fast-forward `main` (`git push origin HEAD:main`).

Each slice has cost about 0.5–0.75 M subagent tokens and 40–60 minutes wall-clock including review and fixes. Reviews have found no mathematical defect so far; findings have been contract gaps, hot-path waste, lifetime hazards and duplication, all fixed within the slice. S7 departed from steps 2 and 4 on the maintainer's instruction (one agent, no subagents); its self-review is in `docs/slices/S7-notes.md`. A later independent review of S7 (`/code-review high` on the S7 commits) remains advisable before the M4 gate.

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
| `docs/slices/` | Briefs `S1.md`–`S8.md` and notes `S1-notes.md`–`S8-notes.md` (spec readings, deviations, review fixes, counters, what was left out); `S8-batch-reading.md` |
| [docs/pruning-rules.md](docs/pruning-rules.md) | Every prune of the P1 search with its coverage lemma, objective scope, quota rule and checker rule (S7: P1-ORBIT-1) |
| [docs/performance-lower-bounds.md](docs/performance-lower-bounds.md) | Hardware model, U/A/H bound taxonomy, envelopes |
| `reviews/` | Referee report on v1.0, response, literature audits, TensorGR learnings |
| `include/canon/canon.h` | Public header: statuses, objective tags, frozen IDs, opaque handles, the S1–S8 API (provisional until M4): memory statistics, metrics, batch, resume |
| `src/<module>/` | perm, bsgs (chain, verify, provenance, conjugate, explicit; stored generators), coset (descent, enumerator), object (subset, graph, dag, root dispatch), encoding (wire, Nat, streams, Group/Perm, SIMPLE-UPPER-1, CDAG-2 encoder/decoder/validator), partition, refine (P1 stages, observer), search (P1 tree with orbit pruning, certificate writer, objectives), symmetry (A_known, prefix stabilisers, cell orbits), api, arena (`mem.h`: the ledger allocator every `src/` allocation goes through), util; scheduler, checkpoint, cpu_dispatch are READMEs only (metrics live in `api`) |
| `checker/` | The independent checker `canon-check` (six C17 sources, no shared code with `src/`; `README.md` restates the `CERT-0` format and the rules) and its harness `tests/test_checker.py` |
| `tools/` | `canon-cli` (every objective, both backends, FORMAT records, `validate`, `--prune`, `--certificate`), `hexdump_stream.py` (strict CDAG-2 dumper) |
| `tests/c/`, `tests/python/` | 36 C test programs (41 ctest entries with the Python ones, the checker usage case and the fuzz replay); `fuzz/` holds the two targets and `run.sh`; `test_e2e.py` tiers T1, G1, G2, S4 objectives, D1, L1, Z1, P1-prune, under both backends; golden vectors; dumper and compare tests |
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
| 12–13 October 2026 commits | Slices S7, S7b and S8: pruning, certificates, checker, implicit automorphisms, admission, ledger, metrics, batch, faults, fuzzing (see `git log`) |

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

## Decisions taken in S8 (13 October 2026; `docs/slices/S8.md`, `S8-notes.md`; the implementer's, awaiting the maintainer's review)

1. **D15 Pause status.** A paused sink is reported as `CANON_CANCELLED` with the offset retained for `canon_result_encode_resume`, since §3.2's eight statuses are frozen; S1's `OUTPUT_ERROR` reading is withdrawn.
2. **D16 Plan versus measurement.** Admission is by a deliberately loose plan computed at problem creation (quadratic stacks for a depth-`n` path; a calibrated, not proven, chain estimate); the ledger measures and reports; no status depends on a measurement. `tests/c/test_metrics.c` checks `peak ≤ plan` on every tier.
3. **D17 Process-wide ledger.** One atomic counter set with size headers, no per-workspace arena yet (M5); the per-solve peak is exact when one solve runs at a time.
4. **D18 Fuzzing without libFuzzer.** The targets keep the `LLVMFuzzerTestOneInput` entry for machines with the runtime and are replayed here with deterministic mutations; nothing is downloaded.
5. Batch (D12, §17 v2.1) implemented literally; a batch of 0 is `COMPLETE` even with NULL arrays; batch cancellation and pool execution are not offered.

## Decisions taken in S7b (13 October 2026; `docs/slices/S7b-notes.md`)

1. The incremental form of rule P1-ORBIT-1 with source (ii) is work policy `0x0003` (`PRUNE_2`), the default; `0x0002` keeps S7's semantics and counts; the model tiers still compare under `0x0001`.
2. Implicit automorphisms are verified by membership and action although they are products of group elements; a failure is `INTERNAL_ERROR`.
3. The checker is unchanged except for accepting `0x0003`; it does not re-derive which pruning policy produced a pruned tree.

## Decisions taken in S7 (12 October 2026; details in `docs/slices/S7-notes.md`)

1. The certificate's node records carry a sweep count and explicit child points, so the checker never depends on the engine's internal cell member order; the final claim carries the explored-node count, checked against the policy.
2. The automorphism table lists the inserted input generators and one explicit transporter per pruned child (a product of `A_known`'s generators read off the Schreier tree); the checker verifies each by its own sift and action and needs no orbit computation.
3. Every group handle keeps a copy of its input generators (both backends).
4. Policy `0x0002` prunes `CANONICAL_IMAGE` only; the labeling and signed P1 runs and every enumeration stay unpruned under both policies.
5. A result with a certificate reports evidence mode `CHECKED_CERTIFICATE`, meaning "a certificate for the independent checker is attached"; only `canon-check`'s acceptance makes the claim checked.
6. A requested certificate that cannot be written fails the solve (`RESOURCE_LIMIT`/`CAPACITY_LIMIT`); it is never dropped silently.

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

1. **An independent review of S7, S7b and S8** (`/code-review high` over their commits), since none had one; the S8 decisions D15–D18 await the maintainer. **Certificate v1** (enumeration, labeling and signed objectives; nested roots) is scheduled with the coverage rules of spec §8.2.
2. **After S8 (M5/M6 work, not scheduled as a slice):** worker-local arenas and the §11.2 strict fallback; pool execution of batches (§14.1) and batch cancellation; checkpoints; a coverage-guided fuzzing run on a machine with a libFuzzer runtime (`fuzz/README.md`).
3. **External oracle tier (detailed plan WP0.9), needs the user's go-ahead for acquisition:** GAP with the `images` package first, SymPy second, nauty third, xperm with the tensor wrapper; each pinned in `SOURCES.json`; none checks P1 traces or bytes.
4. **M0 blind reference (brief answered 2 October 2026, ready to commission).** `refs/BRIEF.md` carries the maintainer's answers to its fourteen questions and `refs/CASES.md` is the definitive case schema (golden.json field names; WP0.2 amended). Decisions: ref-a is the sealed snapshot of the unpruned C path, recorded in `refs/SEALS.md` as not blind with respect to `checks/` and `tools/`; ref-b in Julia carries its own minimal JSON parser (no downloads); every M0 case requests the deterministic witness; the nine flags are out of M0 scope; the spec readings the brief asked about are now stated in spec v2.1 rather than told to the authors. Next: commission ref-b from an author who has not seen `src/`, `checks/`, `tools/` or `tests/`.
5. **Deferred features:** `CONSTRAINT_ONE/ENUM`; the signed labeling orientation σ_ρ (§3.1); relations record `0a` and nested graph records (`UNSUPPORTED_ACTION` today); `canon_nat`; public cancellation; partial results on interruption.
6. **Spec v2.1 (applied 2 October 2026).** Items (a), (b), (d), (e), (f), (g), (h) and (i) of the earlier list are now stated in the spec: §9.1 verifier nesting condition; `Group(1)` is `01 00000000` for every degree; cycle conjugation for the convention vector; status values 0–7 in listed order; `SIMPLE-UPPER-1` for n ≤ 1 is `U32(n)` alone; rule-1 Group blocks are disjoint and ordered; the labeling stabiliser is reported on Ω; the quota counts the traversal named by the descriptor's work-policy ID (root shortcuts visit no nodes; `0x0001` is the unpruned reference; a count-changing prune policy has its own ID and the profile ID names output semantics only). §17 now carries the `solve_batch` contract (call-level `INVALID_INPUT` only for NULL workspace or arrays or size overflow; otherwise every input attempted in order with its own status; `COMPLETE` iff every input was attempted; admission per input). Still open: (c) the DAG golden case's trace and witness are stated only by reference; the signed labeling orientation σ_ρ remains deferred (an implementation may return `UNSUPPORTED_ACTION`). `docs/implementation-plan.md` still says "aligned with v2.0"; nothing in it changes.
7. **Earlier open items unchanged:** the engineering-appendix decision (restore v1.0's engineering guidance or record it as dropped); H0 (retrieve the `hex` repositories at pinned commits, needs network); named obligations SIGN-COVER, PROFILE-EQUIV, ADAPTER-FAITHFUL, GROUP-CERT, RUNTIME-REFINE, SOURCE-GATE; Lean (M1) after the foundation slices.

Do not start a new slice or research cycle without the user's direction (the maintainer directed S7b and S8 on 13 October 2026; both are landed).

## Validation record

- **Finite checks.** `nice -n 19 python3 checks/review_checks.py` passes (40 subgroups at degrees 0–4; 3,536 leaf identities; 2,997 coset partitions; 2,059 normalisation cases; 361 graph-prefix cases; 143 subset and 494 digraph P1 transports; 307 labeling cases; 95 signed cases; 16 golden checks; 10 greedy sequences). Sanity checks, not proofs; the file is unmodified since publication.
- **Slices (state after S8).** `make` and `make CC=clang`: zero diagnostics under `-Werror`. `make SANITIZE=1 test`: 36 C tests and the fuzz replay (12 006 + 4 002 inputs) clean under ASan/UBSan (gcc). `make check`: finite checks, the Python tests under the chain backend and the e2e file under the explicit backend, then the checker harness (1 822 certificates accepted under three policies, 17 mutations rejected). CMake with `CANON_SANITIZE=ON` and `ctest`: 41 of 41 in about 345 s (`test_e2e` 145 s, `test_e2e_explicit` 140 s, `test_checker` 9 s); the sanitizer e2e and checker tiers run reduced seeded subsets under `CANON_E2E_FAST` (see `tests/python/README.md`). Every model tier is byte-identical with the Python model under both backends under work policy `0x0001`; the pruned policy `0x0002` agrees with it on trace and bytes on every tier and on the deterministic witness.
- **Hand verification and provenance.** As recorded at publication: every §7.4 golden case re-derived by hand; `fetch_sources.py` reports 150/150 OK on the maintainer's machine; all relative links resolve.
