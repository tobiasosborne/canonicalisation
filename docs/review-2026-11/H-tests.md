# H-tests: adequacy of the test and evidence base (reading review)

Lane `H-tests`, November 2026 review pass. Per [BRIEF.md](BRIEF.md) **no test suites,
reference runs, corpora or fuzzing were run**; every statement below is reasoned from
reading. Line numbers are as read in this checkout.

## 1. Scope

Read in full or in large part:

- Governance: [CLAUDE.md](/home/tobias/Projects/canonicalisation/CLAUDE.md) (via session rules),
  [README.md](/home/tobias/Projects/canonicalisation/README.md),
  [HANDOFF.md](/home/tobias/Projects/canonicalisation/HANDOFF.md),
  [BRIEF.md](BRIEF.md).
- Spec ([docs/specification.md](/home/tobias/Projects/canonicalisation/docs/specification.md)):
  §3/3.1/3.2 (lines 52–101), §4.1–4.4 (102–165), §5–6 in part, §7.1–7.4 (178–264),
  §8.1–8.4 (265–315), §9.1–9.4 (316–358), §11.1–11.3 (359–393), §17 (493–524), §20 (525–537).
- Python tests: `tests/python/test_e2e.py` (1975 lines, all tiers), `test_golden_vectors.py`,
  `test_hexdump_stream.py`, `test_compare.py`, `test_refs.py`, `tests/python/README.md` heading data.
- C tests: headers/main structure and targeted bodies of all 36 files in `tests/c/`
  (incl. `check.h`, `t1_groups.h`); closer reading of `test_metrics.c`, `test_faults.c`,
  `test_sink.c`, `test_objectives.c`, `test_nat.c`, `test_certificate.c`, `test_chain.c`,
  `test_group_stream.c`, `test_enumerate.c`, `test_cdag_decode.c`.
- Checker harness: `checker/tests/test_checker.py` (592 lines), `checker/README.md` (rules 1–7,
  CERT-0 layout). Checker C sources themselves only via the README/rule list (other lane's code).
- Fuzz: `fuzz/fuzz_cdag_decode.c`, `fuzz/fuzz_checker.c`, `fuzz/fuzz_driver.h`, `fuzz/run.sh`, `fuzz/README.md`.
- Read-only models: `checks/review_checks.py` (449 lines, in full),
  `refs/oracle/oracle.py` (716 lines, in full) + `refs/oracle/README.md`,
  `refs/compare/README.md`, `refs/compare/runs/2026-10-19-m0/README.md`.
- Wiring: `Makefile`, `CMakeLists.txt`, `.github/workflows/ci.yml`.
- Golden-leak scan: scripted search (read-only, `/tmp` not needed) for every ≥16-hex-digit
  string in `refs/vectors/golden.json` inside `src/`, `include/`, `tools/` (normalised
  hex), plus a scan for long hex literals / `expected_` in `src/` and `tools/canon-cli.c`.

Not covered: correctness of `src/` itself, `checker/*.c` line-by-line, `refs/ref-a`/`ref-b`
internals, `tools/gen_cases.py` internals beyond what `test_refs.py` asserts, slice notes
except as quoted in HANDOFF, Lean/`bench/`. No snippet was compiled (none was needed;
all findings are coverage arguments, labelled *reasoned*).

**Ranking convention.** Findings are ordered by risk reduction per unit cost as the lane
brief requests, with severity stated per finding. Each finding names a concrete test:
inputs, expected property, and where it belongs.

---

## 2. Findings

### H-tests-01 — The P1 trace and *which* orbit element is canonical rest on one shared reading (`review_checks.p1`); the exhaustive oracle explicitly does not check either

- Severity: **high**. Category: soundness (test-oracle independence).
- Evidence (reasoned):
  - Every routine end-to-end tier compares the C library against
    `checks/review_checks.py:p1` (lines 229–280): T1 subsets
    (`tests/python/test_e2e.py:218`), G1/G2 graphs (`:264`), enumeration deterministic
    canonical image (`:621`), labeling trace/`t` (`:1462`), signed nonzero trace/`t`
    (`signed_expect`, line ~1607). The D1 tier's empty-key tree `p1_leaves`
    (`:1060–1101`) is explicitly "The structure follows review_checks.p1" (`:1063`),
    and its normaliser `normal_form` (`:944–1012`) re-implements §4.2 in the same file.
  - The one component marketed as definition-level, `refs/oracle/oracle.py`, states in
    its own header (lines 40–44): *"Not checked …: the trace beyond being non-empty and
    invariant across the file … which element of the orbit a canonical or signed image
    is"*. It checks orbit membership, `x^w = c`, least deterministic witness, and
    cross-case invariance (`:534–552`) — all necessary, none sufficient to pin P1.
    Any `c` in the orbit that is constant on the orbit passes, including e.g. the
    CDAG-BYTE-1 minimum.
  - Therefore a shared misreading of §7.1/§7.2 between `src/` and `review_checks.p1` —
    `split` order, O-stage label order `sorted({e[2]}, key=blob)` (`review_checks.py:251`),
    outgoing-before-incoming, G-stage orbit ranking via `normalized_orbits` (`:59–73`),
    target-cell `(size, position)` (`:274`), leaf minimiser `min(group, key=act_list)`
    (`:269`), trace framing `stage()` (`:243–244`) — passes `make`, `make check`,
    `ctest`, the checker acceptance tiers (certificates are producer-generated,
    `checker/tests/test_checker.py:208–221`, and the checker's own P1 reading is a
    third implementation but is only ever fed producer certificates), and the oracle.
  - The only routine pins against such drift are the six §7.4 golden cases
    (`review_checks.py:322–367`, `tests/python/test_golden_vectors.py:28–55`) — all at
    `n ≤ 2`, none with: two arc labels, a loop plus a non-loop arc, an O-stage split
    requiring a *second* sweep that actually splits, a G-stage split after
    individualisation at depth ≥ 2, or a target-cell tie broken by position.
  - Mitigation that exists but is not a regression guard: ref-b (blind) agreed on
    golden/T1/T2 (`refs/compare/runs/2026-10-19-m0/README.md`), but ref-b is run by
    neither `make check` (`Makefile:109–113`), nor `ctest` (`CMakeLists.txt:91–125`),
    nor CI (`.github/workflows/ci.yml` has no Julia/ref-b step), and its T3 run is
    unfinished (76/1900). A future commit that changes both `src/` and the Python model
    consistently (the normal way slices land) re-opens the hole silently.
- Risk: the library's central output (trace bytes, canonical bytes) could be
  self-consistently wrong w.r.t. spec P1 with a fully green routine suite. This is the
  single largest shared-reading exposure in the repo.
- Recommendation / concrete test (cheap, high yield): add a small set of **hand-derived
  trace vectors** — derived on paper from §7.1/§7.2, stored as test expectations only —
  in `tests/c/test_search_graph.c` (pattern already used: "Expected hex lives here"):
  1. `n=3`, `G=Sym(3)`, arcs `0→1` label `"a"` ×1, `0→1` label `""` ×2, loop `2→2` ×1,
     all colours empty. Hand-derive the root O signatures (label order `""` before
     `"a"` by `B(label)` length-first, §4.1/§7.1), first `STAGE_O` sizes, and assert
     the full trace + witness + bytes. A swapped label order or in/out swap changes
     the trace and must fail.
  2. `n=4`, empty graph, `G = ⟨(0 1), (2 3)⟩` (Vierergruppe, not Sym): root is
     non-discrete, G-stage must split by the orbit structure after individualising one
     point; assert the exact trace token sequence to the first leaf. This exercises
     §7.1-G beyond `n≤2` and the depth-1 `NODE` token.
  3. A subset case with a target-cell size tie at depth ≥ 1 (e.g. `n=5`, `x={0}`,
     `G=Sym(5)`), asserting the winning leaf's trace — pins `(size, position)`.
  Additionally, wire one **ref-b smoke** into `make check` if Julia is present
  (skip otherwise): run `refs/ref-b` on `refs/vectors/golden.json` only and
  `compare.py` against ref-a's records (6 cases, seconds) — not a suite substitute,
  but it makes blind drift visible per-commit.
- Remediation plan: 3 hand vectors, ~1–2 h including the paper derivation (which must
  be recorded in a comment citing §7.1/§7.2 sentences); ref-b smoke ~30 min of Makefile
  work. Risk of the change itself: none (test-only).

### H-tests-02 — §3's "transporter target of another shape" and several other cross-shape rules have no routine C/e2e test

- Severity: **high**. Category: correctness / spec-conformance coverage.
- Evidence (reasoned): spec §3 (line ~82) and §8.2: a target `y` of another shape on
  the same degree (tuple vs set of atoms, graph vs nested root) *"is an ordinary input,
  not INVALID_INPUT: … the complete empty result"*. `grep` for other-shape phrasing in
  `tests/` finds nothing. `tests/python/test_e2e.py` enumeration tiers always build
  `y` of the same kind (`check_case(..., kind, x, y)`, `:566`; D1 `check_objectives`
  target is `act_dag(records, h)` of the same records, `:1309`). The rule is exercised
  only out-of-band: the oracle demands it (`refs/oracle/oracle.py:465–468`) on M0
  corpus files, and HANDOFF records that the previous `INVALID_INPUT` reading was
  withdrawn on 13 Oct 2026 — i.e. this exact behaviour *already flipped once* and the
  routine suite would not have caught either direction.
- Risk: a regression to `INVALID_INPUT` (or worse, a crash/miscompare comparing a graph
  root against a set root in the §8.1 consumer) ships green. Same shape of gap, lower
  risk: `CONSTRAINT_ONE/ENUM` status is checked only as a stub tag
  (`tests/c/test_objectives.c:433`, `tests/python/test_refs.py:241`) with no matrix over
  profile/order/target combinations, and checkpoints only with all-NULL arguments
  (`tests/c/test_version.c:80–81`).
- Concrete test: in `tests/c/test_objectives.c`, table of cross-shape pairs at `n=2`
  and `n=3` under `Sym(n)` and under `G=1`: subset `{0}` → graph with one arc; subset
  → stream whose root is the tuple `(atom0, atom0)`; graph → subset; nested set of
  atoms `{0}` imported by stream → subset `{1}` (this last one *is* the same shape
  after §7.1 subset-root recognition and must be a *hit* — it pins the boundary).
  Expected: `CANON_COMPLETE`, empty result, `transport_exhausted=true`,
  `witness_valid=false`, no witness/group payload, for both `TRANSPORTER_ONE` and
  `TRANSPORTER_COSET`, both backends. ~40 lines reusing existing helpers.
- Remediation plan: as above; extend the stub matrix for `CONSTRAINT_ENUM` in the same
  file while there (each objective × {NULL target, non-NULL target} ⇒
  `UNSUPPORTED_ACTION`). Effort < 1 h.

### H-tests-03 — CI and `ctest` do not run what HANDOFF's validation record implies; the CI Python job as written cannot satisfy `test_e2e.Pruning`

- Severity: **high**. Category: contract (evidence/wiring).
- Evidence (reasoned):
  - `CMakeLists.txt:91–125` registers only `test_e2e`, `test_e2e_explicit`,
    `fuzz_replay` (500 iterations), `test_checker`, `canon_check_usage` and the C
    tests. `ctest` therefore never runs `checks/review_checks.py`,
    `test_golden_vectors.py`, `test_hexdump_stream.py`, `test_compare.py` or
    `test_refs.py` — although README's build section presents the CMake build as the
    "equivalent CMake definition". Only `make check` (`Makefile:109–113`) runs the full
    set, and **no CI job runs `make check` or `make corpus`** (`.github/workflows/ci.yml`:
    build-test = `ctest` + `make test` on gcc/no-sanitize only; python-checks =
    `review_checks.py` + `unittest discover -s tests/python`).
  - The CI python-checks job builds only the CLI (`ci.yml:38` `make build/make/canon-cli`)
    and runs discovery with `CANON_REQUIRE_CLI=1` (`ci.yml:40–42`). But
    `tests/python/test_e2e.py:1783–1788` (`Pruning.setUp`) *fails* (not skips) when
    `CANON_REQUIRE_CLI=1` and `canon-check` is absent, and nothing in that job builds
    `build/make/canon-check` (`Makefile:73–75` is a separate target, not a dependency of
    `$(CLI)`). As read, that job must go red on `Pruning` — or, worse, it has been red
    and ignored. (Reasoned from the files; not run, per brief. If `make
    build/make/canon-cli` somehow also produces the checker on the CI image, that is
    an accident of leftovers, not of the Makefile.)
  - Sanitizer CI runs the *reduced* e2e (`CANON_E2E_FAST=1`, `CMakeLists.txt:86–87`),
    so no CI job runs the full Python tiers under ASan/UBSan, and the full tiers under
    clang are never run in CI at all (clang jobs run `ctest`, whose e2e is full only
    in the non-sanitizer leg — that leg does exist, `ci.yml:14–15`; the gap is
    `make check`-only tests: refs/compare/hexdump/golden under any second compiler).
- Risk: the evidence that actually guards day-to-day commits is a proper subset of the
  evidence HANDOFF's "Validation record" describes (which was produced by local full
  runs). Contributors trusting green CI / `ctest` over-trust it.
- Concrete fix/test: (a) CI python-checks: `make build/make/canon-cli
  build/make/canon-check`. (b) Register the four missing Python files + `review_checks.py`
  as CTest tests (they need only `CANON_CLI` already in `CANON_E2E_ENV`), or add a CI
  job running `make check`. (c) Add `make corpus` to one CI leg (it is the only guard
  that the M0 manifest still reproduces; `test_refs.test_manifest_reproduced` skips T2,
  `test_refs.py:92–93`).
- Remediation plan: workflow + CMake edits only, ~30 min. No new test logic.

### H-tests-04 — Degree and size ceilings: almost all semantic coverage is `n ≤ 4` exhaustive / `n ≤ 8` random; several spec rules only "switch on" above that

- Severity: **medium-high**. Category: correctness coverage.
- Evidence (reasoned): T1 is every subgroup of `Sym(n)`, `n ≤ 4` (`tests/c/t1_groups.h:14–19`,
  masks over `n! ≤ 24`); Python tiers mirror it; random C tiers reach `n ≤ 7`
  (`test_enumerate.c:247`, `test_group_stream.c:8`) or `n ≤ 8` (`test_chain.c:9,215` —
  membership comparison is explicitly gated `if (n <= 8)`). Above that there are only
  point cases: Sym(12) and a 64-cycle (chain/provenance), Sym(20)/Sym(21) order
  boundary (`test_chain.c:484`), `C_2^17` on 34 points (explicit-table refusal,
  `test_e2e.py:414–443`), Sym(6) node counts. Rules with **no** test that would fail
  if wrong above the small tier include:
  - §9.4 rule 2 at any size where the greedy sequence has `k ≥ 3` generators and rule 1
    with ≥ 3 non-singleton orbit blocks (C brute force in `test_group_stream.c:114–215`
    is T1-only; `n>4` coverage there is presentation-invariance of the library against
    *itself*, `:8` comment — a self-consistency check, not a §9.4 oracle).
  - §4.2 height numbering where two same-height nodes' order is decided by assigned
    child indices of *different* heights below them (D1: depth ≤ 3, `n ≤ 4`, literals
    over `"ab"` of length ≤ 2, `test_e2e.py:1140`; the depth-60 case is a pure chain
    with no same-height competition, `review_checks.py:355–356`).
  - §7.1 O-stage with > 2 distinct arc labels and counts that need multi-byte
    comparison (multiplicities in tiers are ≤ 2; labels ⊆ `{"", "a", "c"}`).
  - §11.1 admission/output bounds for subgroup/coset leaves whose payload grows under
    conjugation (HANDOFF decision 6 cites the `C_4` on 4 points counterexample — the
    *bound* argument at larger `n` is asserted, and `test_metrics` tiers are `n ≤ 4`
    subsets/digraphs, `test_metrics.c:175`).
- Concrete cheap tests (pick by yield):
  1. `test_group_stream.c`: one `n=9` case, `H = Sym{0,1,2} × Sym{3,4} × ⟨(5 6 7 8)⟩`
     (order `6·2·4=48`, explicit closure trivial in the test), brute-force §9.4 in
     the test exactly as `brute_group_bytes` does (orbits ⇒ rule 2 since the 4-cycle
     block is not full symmetric), compare bytes; plus its rule-1 sibling with the
     4-point block made symmetric. Fails if block ordering by least point or
     least-outside choice is wrong at `k≥2`.
  2. `tests/python/test_e2e.py` D1: add one fixed (non-random) DAG: root
     set{ tuple(lit"b", atom0), tuple(lit"a", atom1), mset{lit"a"×300} } at `n=2`,
     `G=1` — expected stream from the tier's own `normal_form` but *also* asserted
     byte-for-byte against a hand-written hex string in the test, so the test-local
     normaliser itself is pinned once (see H-tests-07). Multiplicity 300 forces a
     two-byte Nat, which no D1 case reaches (counts ≤ 3, `:1158`).
- Remediation plan: ~1 h. Both are small, deterministic, and run in milliseconds.

### H-tests-05 — §11: admission-plan and ledger tests prove `peak ≤ plan` only where the plan was calibrated; boundary, determinism-across-presentations, and multi-handle behaviour are untested

- Severity: **medium-high**. Category: spec-conformance (§11.1/§11.2) coverage.
- Evidence (reasoned):
  - `test_metrics.c:31–40` asserts `used = peak − before ≤ planned` per solve, over
    the T1/digraph tiers (`:169–260`) plus fresh-workspace cases — good, but HANDOFF
    D16 admits the chain estimate is *"calibrated, not proven"*, and the tier is
    exactly the calibration set (comment `:4`: "the test the plan's chain estimate is
    calibrated by"). A plan regression on nested roots with group leaves, on
    certificate-carrying solves near `max_output_bytes` (D3: certificate bounded at
    4× plan), or on the deterministic-witness stabiliser charge would not fail here
    unless it also breaks an `n≤4` subset case. HANDOFF's "1 284 solves with measured
    peak ≤ plan (worst 5 288 / 30 386 bytes)" is a printed extreme, not a pinned
    assertion: `test_metrics.c` tracks `worst_plan/worst_peak` (`:16,41–45`) but (as
    read) never asserts the ratio/count, so the tier can silently shrink.
  - §11.1 requires `CAPACITY_LIMIT` to be *"a function of semantic input, objective
    and descriptor"*. HANDOFF open item 6(i) records that the plan in fact reads the
    generator list and backend, so two generating sets of one group can straddle a
    budget. **No test pins even the current behaviour** at a `max_live_bytes`
    boundary: no `budget = planned−1 / planned / planned+1` triple, no same-group /
    two-generating-sets / two-backends boundary comparison. Whatever the maintainer
    decides on 6(i), today a change in either direction is invisible.
  - §11.2/D17: the ledger is process-wide and per-solve peak is *"exact when one solve
    runs at a time"*. Tests check baseline restoration single-threaded
    (`test_faults.c` main; `test_metrics.c:165–167`). Nothing interleaves two contexts /
    workspaces / a retained result + certificate buffer while a second solve runs, and
    nothing checks `canon_memory_get` against a hand-summed expectation for a known
    live set (context + one group + one object). A double-count or a missed free in a
    rarely-released handle type passes if the *net* returns to baseline in the tested
    flows. (Positive note: a scan found no raw `malloc/calloc/realloc` call sites in
    `src/*.c` outside comments — the one-allocator rule holds as far as grep can see;
    see §4.)
- Concrete tests:
  1. Boundary triple in `test_metrics.c`: build one subset problem, read
     `canon_problem_planned_bytes`, then create contexts with
     `max_live_bytes ∈ {planned−1, planned, planned+1}` and assert the exact
     `CAPACITY_LIMIT`-or-`COMPLETE` pattern the code intends (documenting D16), for
     both backends and for greedy vs full generator lists of the same group. The
     expected pattern is whatever the maintainer confirms for open item 6(i) — the
     test's job is to make any *change* loud.
  2. Interleave test in `test_metrics.c` or `test_batch.c`: retain a result (with
     certificate) from workspace A, run a solve on workspace B, assert
     `live_bytes ≥ bytes(result A retained)` throughout and exact baseline after
     releasing everything in reverse order. Sequential, no threads needed.
- Remediation plan: ~1 h; needs one maintainer sentence on 6(i) to fix test 1's
  expectation (recorded as a spec ambiguity in §3, not resolved here).

### H-tests-06 — Checker test base: single-field mutations only, several named REJECT reasons never elicited, policy ID 0x0002-vs-0x0003 explicitly interchangeable, acceptance counts unpinned

- Severity: **medium**. Category: soundness (checker evidence).
- Evidence (reasoned): `checker/tests/test_checker.py` is genuinely strong —
  acceptance over the full T1/G1 tiers under three policies (`:280–331`), ~25 mutation
  methods (`:338–592`) each asserting the named reason. Gaps:
  - Reasons named in `checker/README.md` rules 1–7 with **no** eliciting test found in
    the harness (searched reason strings): `SWEEP_COUNT` (a non-last sweep that did
    not split, or a missing final no-change sweep — §7.1 "One complete no-change sweep
    is always recorded"), `STAGE_G_SIZES`, `F_MISMATCH`, `TARGET_CELL`,
    `BRANCH_AT_DISCRETE`, `LEAF_NOT_DISCRETE`, `NODE_DEPTH`/`NODE_ATOM`,
    `REBASED_BASE_PREFIX`, `DEGREE_MISMATCH`, `OBJECT_MALFORMED`/`OBJECT_UNSUPPORTED`,
    `HEADER`, `CAPACITY`, `CHILD_MISSING` (deletion is tested via
    `test_delete_explored_child`, which may surface as a different reason — the harness
    does not assert `CHILD_MISSING` by name). A checker regression that stops checking
    e.g. the target-cell rule (§7.1's `(size, position)` choice — load-bearing for the
    trace) would keep every existing mutation red for other reasons or stay green.
  - `test_implicit_automorphism:389–392` asserts that relabelling a `0x0003`
    certificate's header to `0x0002` is **accepted** ("not checkable by the checker").
    Honest and documented — but it means the §11.1 statement "the count is a function
    of … the work-policy ID … recorded in results and certificates" is, for pruned
    trees, verified by nobody: the checker checks count-vs-records, the producer
    chooses the label. No test anywhere cross-checks that a `0x0002`-labelled solve
    actually explored the S7 (explicit-only) node count on a case where the policies
    differ beyond `{0}` under Sym(4) (quota tests do pin counts via `--max-nodes`
    boundaries for two cases, `test_e2e.py:1873–1899` — good — but not via certificates).
  - Acceptance totals are printed, not asserted: `Acceptance` methods end in `print(...)`
    (`:295,314,331`); HANDOFF's "1 822 certificates accepted / 2 733 under three
    policies" cannot regress loudly. If `CANON_E2E_FAST` leaked into a full run, or a
    subgroup enumerator bug dropped a group, the harness stays green with fewer certs.
  - All accepted certificates are producer-emitted. There is no hand-assembled valid
    CERT-0 (even for `n=0`, where the whole certificate is a few dozen bytes) fed to
    `canon-check`, so a producer+checker *joint* misreading of the CERT-0 layout in
    `checker/README.md` (restated from `src/search/certificate.h`) is untestable by
    construction. The harness parser `Parsed` (`:74–186`) shares that layout reading
    a third time.
- Concrete tests:
  1. One mutation per missing reason above, reusing `Parsed` offsets (most are
     one-field edits like the existing ones; `TARGET_CELL` needs swapping two child
     subtrees' `b` values in `branch_case`). Assert exact reason strings.
  2. `assertEqual(stats["certificates"], <exact>)` in the three Acceptance methods
     for non-FAST mode (compute the constants from the tier definitions, as
     `test_e2e.py` does at `:241,306,324`).
  3. A hand-built `n=0` empty-subset certificate as a hex literal in the test
     (derived from `checker/README.md`'s layout alone), asserted `ACCEPT 1`; then the
     same with witness bytes flipped ⇒ `FINAL_WITNESS`.
- Remediation plan: 1–2 h, harness-only.

### H-tests-07 — Circular and self-referential expectations in specific places (distinct from the shared-model issue of H-tests-01)

- Severity: **medium**. Category: soundness.
- Evidence (reasoned), closest to circular first:
  1. `tests/python/test_golden_vectors.py:1–6` says golden.json is recomputed "with
     the independent Python model" — the model is `checks/review_checks.py`, the same
     file that supplies the expected values for the entire e2e suite and whose
     `run_v2` *contains the golden constants inline* (`review_checks.py:316–367`,
     asserted against its own `p1`). So the chain spec §7.4 → golden.json →
     review_checks has exactly one independent link (the original hand derivation,
     attested in HANDOFF), and the test as written would also pass if golden.json and
     review_checks had been edited together. This is a documentation overclaim
     ("independent") more than a live bug — but it is the file a future editor will
     trust when "fixing" a golden mismatch by editing the wrong side.
  2. `tests/python/test_refs.py:144–167` tests the oracle's invariance rules using
     expected bytes produced by the oracle's own `encode`/`group_bytes` (`:149–151`).
     Fine for testing the *invariance logic*, but it means the oracle's encoder has no
     test against a hand-written stream anywhere except through ref-a agreement
     (`:315–354`) — and ref-a is the production path, so oracle-vs-C encoder agreement
     is again two-party, not three.
  3. `fuzz/run.sh:13–19` seeds the CDAG fuzz target from golden.json streams and
     `:20–23` seeds the checker target from two on-the-spot producer certificates;
     `fuzz_cdag_decode.c:25–38` accepts *any* status and, on successful decode, only
     requires that a trivial-group solve not crash. A decoder that accepted every
     byte string (or normalised everything to the empty set) would pass the fuzz
     replay. The fuzz oracle is crash-only by design (D18), but one cheap invariant is
     available (below).
  4. `test_faults.c` compares faulted COMPLETE answers to the fault-free run's digest
     captured from the same binary (`ref_digest`, `:182–186`) — correct for its purpose
     (fault transparency), worth naming so nobody later cites it as correctness evidence.
  5. Several C tiers compare the chain backend against the explicit backend
     (`test_enumerate.c:5–9`, `test_chain.c` T1/T2). The explicit backend is a genuinely
     separate implementation over element tables, so this is *not* circular for group
     operations; it *is* a shared reading for the §8.1 traversal order and hence for
     every "any"-mode first-hit witness (see H-tests-01, and ref-b QUESTION Q2 in the
     M0 run README). Deterministic witnesses, by contrast, are brute-forced from the
     definition in `test_e2e.Brute` (`:541–558`) — that part is sound.
- Concrete tests:
  - For (1): add to `test_golden_vectors.py` a comment fix ("the finite model", not
    "independent") and one assertion that does not pass through `rc` at all: parse
    `expected_stream_hex` of the DAG case with `tools/hexdump_stream.py` and assert
    the record structure `(tags, children)` equals the JSON's record list. (Hexdump is
    a separate parser; this cross-checks transcription, which is the test's stated goal.)
  - For (3): in `fuzz_cdag_decode.c`, when `canon_stream_validate(...) == COMPLETE`,
    additionally decode, re-encode via a trivial-group canonical-image solve *only if*
    the object is already canonical for the trivial group is **not** valid in general —
    instead assert the valid invariant: `validate(stream)==COMPLETE` ⇒
    `object_create(stream)` succeeds, and re-importing the object's *own* stream is
    unnecessary; the cheap sound invariant is: decode succeeds ⇒
    `canon_stream_validate` of the **normal form obtained by solving under the trivial
    group and taking result bytes** is `COMPLETE` and those bytes validate. (Result
    bytes under `G=1` need not equal the input.) This catches "validator accepts what
    the decoder cannot round-trip" without inventing an oracle.
- Remediation plan: < 1 h.

### H-tests-08 — §17 API contract: lifetimes, workspace ownership, sink re-entrancy, batch aliasing, and output-as-input idempotence are thin

- Severity: **medium**. Category: contract coverage.
- Evidence (reasoned): §17 requires client-level golden/error/lifetime vectors for
  *every* public entry point, output-as-input idempotence through wrappers, one active
  owner per workspace, callbacks not recursively entering the same workspace. What exists:
  NULL retain/release no-ops (`test_search_subset.c:230–246`), result-retains-problem
  in one flow (`test_objectives.c:67,567`), thorough sink pause/resume/FAIL including
  offsets (`test_sink.c`, incl. `OUTPUT_ERROR` at `:116–122`), batch-vs-separate identity
  incl. NULL entry and batch-of-0 (`test_batch.c` header). Missing entirely (grep: no
  `pthread`/thread use in tests; no re-entrancy test):
  - Two workspaces solving *concurrently* (or even interleaved via a sink callback
    that solves on a *second* workspace while the first encode is paused) over one
    shared immutable group/context — the exact sharing §17 blesses.
  - A sink callback that calls `canon_solve` on the *same* workspace (must not corrupt
    the paused encode; the spec forbids the callback from doing it, but the library's
    failure mode — clean status vs crash — is untested).
  - `canon_solve_batch` with the same problem pointer twice, with `results == statuses`
    style aliasing cast aside, and with `count` such that `count * sizeof(...)`
    overflows (spec §17 names this call-level `INVALID_INPUT` case; `test_batch.c`
    covers NULL arrays but, as read, not the overflow count — it cannot allocate the
    array, which is precisely why the entry point must reject before touching it).
  - Output-as-input: `C_G(C_G(x)) = C_G(x)` is checked only for signed cross-feed
    (`test_e2e.py:1648`, `test_signed.c:560`) and as encoding-level normalisation
    idempotence (`test_dag.c:446`). No tier feeds a graph's or nested object's
    canonical *stream* back through `canon_object_create` and re-solves, checking
    byte identity and that the deterministic witness is the least element of `A·t`
    (in particular `t = id` is **not** required — a test that asserted identity
    witness would be wrong, which is why the property must be stated as byte
    identity + `verify_witness`).
  - Release order: context released while group/object/result still live (refcount
    correctness is only exercised in LIFO order in the flows read).
- Concrete tests: (a) idempotence loop added to `test_search_dag.c`'s existing random
  tier: take result bytes, `canon_object_create` from them, solve again, assert equal
  bytes/trace and `canon_result_verify_witness` — ~15 lines inside an existing loop,
  catches encoder/decoder asymmetry (the classic CDAG bug class) across all D1 shapes.
  (b) batch overflow: `canon_solve_batch(ws, probs, results, statuses, SIZE_MAX)`
  with non-NULL small arrays ⇒ `INVALID_INPUT` and arrays untouched (sentinel check).
  (c) interleaved-workspace test using the sink-pause hook (no threads needed):
  pause mid-encode on ws1, complete a solve+encode on ws2 inside the callback,
  resume ws1, assert both byte streams intact.
- Remediation plan: 1–2 h in existing files. A real two-thread smoke (two workspaces,
  shared group, 200 solves, ASan+TSan-less so just correctness of bytes) is a further
  cheap addition if the project wants any concurrency evidence at all; §14 machinery
  is future work, but §17's *sharing* sentence is present-tense.

### H-tests-09 — §4.1 relations (tag `0a`) and nested graph records: status-only coverage; validator/decoder agreement untested

- Severity: **medium-low**. Category: spec-conformance coverage.
- Evidence (reasoned): §4.1 fully defines relation records (unique names sorted by
  `B(name)`, one arity per name, duplicate-tuple combining, set-relation import rule).
  The implementation returns `UNSUPPORTED_ACTION` (HANDOFF open item 5, honest).
  Tests: two hand streams through the CLI (`test_e2e.py:1333–1340`), C decoder cases
  (`test_cdag_decode.c:167`), hexdump *accepts* a well-formed relation
  (`test_hexdump_stream.py:95–99`). Untested: whether `canon_stream_validate` on a
  relation stream says `UNSUPPORTED_ACTION` or `INVALID_INPUT` for (i) a canonical
  relation stream, (ii) a relation stream violating §4.1 (duplicate name, unsorted
  names, zero multiplicity, arity mismatch) — i.e. whether well-formedness is checked
  before the unsupported verdict; and the same question for a graph record nested
  inside a tuple (tag `09` as a child). The distinction matters because FORMAT/oracle
  comparisons treat the two statuses differently, and ref-b must match whichever the
  spec intends (an ambiguity candidate, §3 below).
- Concrete test: 6 streams in `test_cdag_decode.c` (canonical relation; relation with
  unsorted names; arity-0 relation with one empty tuple; nested graph inside tuple;
  nested graph with a zero-multiplicity arc) asserting the exact pair
  `(object_create status, stream_validate status)` for each, whatever the maintainer
  confirms the intended pair is.
- Remediation plan: < 1 h once the intended statuses are stated.

### H-tests-10 — §4.1/§11.1 count arithmetic at the u64 boundary via *streams* (multiset/arc combining) is untested

- Severity: **medium-low**. Category: memory-safety / capacity coverage.
- Evidence (reasoned): the Nat *writer* is pinned to `UINT64_MAX`
  (`test_nat.c:27–30`), a 9-byte Nat is rejected on decode (`test_cdag_decode.c:188`),
  and the graph *builder* rejects total multiplicity > u64 (`test_graph.c:143–150`).
  But on the stream-import path, duplicate arcs are *combined by addition*
  (`test_e2e.py:1218` marks duplicate arcs import-accepted) and multiset equal
  children are *summed* (§4.2; `test_e2e.py` MALFORMED covers zero/order, not sums).
  No stream presents e.g. two arcs `(0,1,"",2^63)` and `(0,1,"",2^63)` whose combined
  multiplicity is `2^64`, or three multiset pairs for one child summing past u64.
  Spec §11.1 says counts are exact multi-limb in the abstract API and overflow is a
  capacity error in this implementation (orders are u64 by HANDOFF decision 2); a
  wrapping add would silently produce a wrong (small) multiplicity and a wrong
  canonical form — exactly the failure mode a 20-line stream test excludes.
- Concrete test: in `test_cdag_decode.c` (C, hand hex like its existing cases):
  stream A: graph record, two identical arcs each `Nat(2^63)` ⇒ expect
  `CAPACITY_LIMIT` (or whichever status the decoder documents) and *not* a successful
  import with multiplicity 0/2^63; stream B: same with `2^63−1` twice ⇒ import
  succeeds, re-encoded/normalised multiplicity `2^64−2` (check via validate+solve
  under trivial group or the decoder's test hooks as existing cases do); stream C:
  multiset analogue. Also `q` and child-count fields of `0xFFFFFFFF` against a short
  stream (truncation is covered generically by `test_hexdump_stream.py:43–49` for the
  *tool*; the C decoder's count-vs-remaining-bytes guard for records whose *count* is
  huge but header complete deserves one explicit case).
- Remediation plan: < 1 h.

### H-tests-11 — `CANON_E2E_FAST` sampling is positional (`i % every`), so sanitizer runs systematically skip the same cases forever

- Severity: **low**. Category: maintainability / coverage.
- Evidence (reasoned): `sampled(i, every) = i % every == 0` (`test_e2e.py:113–115`),
  checker `i % 7`, `i % 11` (`test_checker.py:289,306`). Iteration order is by
  subgroup (in `rc.subgroups` sorted order, `review_checks.py:48`) then mask/
  multiplicity lexicographic, so the FAST subsets are not representative draws: e.g.
  in the checker subset tier the kept *cases* are every 7th (group, subset) pair in
  that fixed order for every sanitizer run until the end of time; a memory bug that
  only triggers for, say, non-abelian groups of order 8 at odd positions never meets
  ASan. HANDOFF records the reduction honestly ("reduced seeded subsets" — they are
  not in fact seeded). Two FAST tiers also drop their exact-count assertions
  (`test_e2e.py:654,1541`), so a FAST run cannot detect that it sampled *nothing*.
- Concrete test/fix: offset the stride by an env seed, e.g.
  `sampled(i, every): (i + SEED) % every == 0` with `SEED` from `CANON_TEST_OFFSET`
  defaulting to 0, and set different offsets in the gcc-sanitizer and clang-sanitizer
  CI legs; assert sampled counts > 0 in FAST mode. Five lines; over two CI legs the
  union coverage doubles for free.
- Remediation plan: 15 min.

### H-tests-12 — Counts in HANDOFF's "What works now" are mostly *derivable* from tier definitions but several headline numbers are unpinned snapshots

- Severity: **low / note**. Category: documentation.
- Evidence (reasoned, checked by arithmetic against the tier code):
  - Supported exactly by assertions in the tests: S1 "539 cases" = number of
    (group, subset) pairs; the test asserts `2 × 539` records over two generating
    sets (`test_e2e.py:240–241`). S2 "664 exhaustive" = `2·2·(1+3+2·81)` asserted at
    `:306`; "2880 sampled" = `2·40·36` at `:324`. S5 "280 random nested objects" =
    `10·10 + 30·6` asserted at `:1291`. L1/Z1 tier sizes asserted at `:1541–1549,1719`.
    Finite-check totals are printed by `review_checks.py:125,444` and would change
    loudly only if someone reads the output — the script itself asserts subgroup
    counts (`:82`) but not the PASS totals HANDOFF quotes.
  - **Not pinned by any assertion** (printed or log-only): S7's "1 822 certificates",
    S7b's "1 617", validation record's "2 733 certificates … 32 mutation checks"
    (H-tests-06); S8's "1 253 batch entries", "1 284 solves … worst 5 288/30 386",
    "1 768 single-fault runs", "16 008 fuzz replays" (`test_faults.c` asserts ranges
    and `compared > 0`, not totals; `fuzz/run.sh` asserts only exit status; the
    16 008 figure is `2000×6 + 2000×2 + seeds` arithmetic from `run.sh:24–25` +
    `Makefile:58`, true today, silent if a seed file disappears because the glob
    `stream*.bin` would just feed fewer files — `fuzz_driver.h:101–104` requires
    ≥ 1 file, so losing 5 of 6 CDAG seeds still exits 0).
  - S3's evidence line claims "ten certificate mutations rejected" — that is
    `test_verify.c` (chain verifier), fine, but a reader will conflate it with S7
    certificate mutations; naming collision in the docs, not in the tests.
  - S4's "enumerator visits exactly G, each element once" is asserted vs the explicit
    backend's leaf sequence (`test_enumerate.c` T1/T2) and independently as a *set*
    property in `test_e2e.Brute.assertion` (`:540`) — supported.
  - "Every objective against brute force … both backends" (S4/S6 rows): the Python
    file runs under both backends only when driven so (`make check` does,
    `Makefile:112`; ctest does, `CMakeLists.txt:95–98`), but the *C* enumeration tier
    `test_enumerate.c` compares backends against each other, with brute force over
    element tables coming from the explicit backend — sound for sets of leaves,
    shared-reading for order (H-tests-01/07).
- Recommendation: pin the four S7/S8 totals with `CHECK`/assertEqual (exact in full
  mode, `> 0` in FAST), and have `fuzz/run.sh` assert it found 6 stream seeds and 2
  cert seeds before running. ~20 min.
- Remediation plan: as H-tests-06 item 2, extended to `test_batch.c`/`test_metrics.c`/
  `test_faults.c` printfs.

---

## 3. Spec ambiguities (quoted, not resolved)

1. **§11.1 vs admission plan inputs** (already HANDOFF open item 6(i), recorded here
   because it blocks H-tests-05's boundary test): *"Exceeding that policy yields
   CAPACITY_LIMIT, a function of semantic input, objective and descriptor"*
   (§11.1) — while the implemented plan depends on the generator list and backend.
   The test suite currently encodes neither reading.
2. **Status for a well-formed but unsupported record on the validate path**:
   §4.1 *"unknown schema/action/profile/encoding versions are unsupported, never
   reinterpreted"* vs the relations paragraph, which defines relations as part of
   the grammar with no status named for an implementation that does not solve them.
   H-tests-09 needs the intended `(create, validate)` status pair for relations and
   nested graph records, including whether malformed relations are `INVALID_INPUT`
   even where solving is unsupported. (Cf. HANDOFF open item 6(ii), the same shape of
   question for `SIMPLE-UPPER-1` outside its class, which the code/oracle currently
   read as `UNSUPPORTED_ACTION` and the tests pin at `test_e2e.py:834–842` —
   that pin is good practice; it just has no spec sentence behind it yet.)
3. **§7.3 root shortcuts vs quota**: *"If every generator fixes x, the unsigned orbit
   is a singleton and returning x is equivalent to P1 regardless of trace"* (§7.3)
   and §11.1 *"the §7.3 root shortcuts … visit no tree nodes"*. The tests pin that
   the empty subset under Sym(6) — where every generator fixes `x` — costs **1237**
   unpruned NODE tokens (`test_e2e.py:1892–1899`, `test_metrics.c:71` expects the
   `n=2` analogue at 3 nodes with `singleton_orbit` set). So the implementation does
   *not* take the singleton-orbit shortcut for the trace-carrying solve, which the
   sentences allow ("A caller requesting a trace certificate receives the prescribed
   trace"), but no test pins the boundary question: with `max_search_nodes = 0`
   semantics aside (0 selects the default), is there *any* mode in which the shortcut
   fires and the count is 0? Ref-b's Q3/Q10 (M0 run README) suggest the blind author
   hit the same fork. Tests should pin whichever is intended for (discrete root,
   singleton orbit) × (trace requested, not requested).

No other spec sentence blocked this lane; in particular §3's deterministic-witness
paragraph (v2.2) is unusually testable as written and the tests exploit it well.

---

## 4. Things checked and found sound

- **Golden constants do not leak into `src/` or `tools/`** (spec §20 / §7.4 *"Blind
  implementations must derive them from the rules, not copy expected constants"*):
  scripted scan found none of golden.json's trace/stream/payload hex in `src/`,
  `include/` or `tools/`; the only magic literals are the §4.1 wire prefix
  `43 4e 02` (`src/encoding/{graph_stream,subset_stream,cdag_encode}.c`,
  decoder check `cdag_decode.c:546`), which is grammar, not a golden value. C tests
  consistently carry the "Expected hex lives here, never in src/" discipline.
- **One-allocator rule**: no raw `malloc/calloc/realloc` call sites in `src/*.c`
  (grep; two comment mentions only), consistent with the §11.2 ledger claim's
  precondition.
- **Brute-force quality where it matters most**: deterministic witnesses for every
  objective, `Group(A)` bytes (three implementations: C library, `rc.group_bytes`,
  `test_group_stream.c:brute_group_bytes`, plus oracle's fourth at
  `oracle.py:109–125`), stabilisers, transporter emptiness, signed zero-iff-odd
  (incl. D1 least-odd, `test_refs.py:169–190` and oracle `:639–643`), SIMPLE-UPPER-1
  minima (`test_e2e.py:725–769`) — these are derived from definitions over explicit
  groups, not from the code under test. The product-side pins on non-commuting
  elements (`test_coset_least.c`, `test_enumerate.c`, `test_labeling.c`,
  `test_dag_action.c` conjugation with an order-3 element) are exactly right for the
  `pq` convention.
- **Metamorphic coverage** (§5 representation independence): two generating sets
  everywhere in the Python tiers, shuffled/duplicate arcs (`scramble_arcs`,
  `test_e2e.py:167–177`), streamed-vs-built identity for subsets/graphs
  (`test_search_dag.c`), presentation independence for group leaves. This is a real
  strength; H-tests-01 is about the *shared* half of the comparison, not these.
- **Decoder rule coverage** for tags 01–09 is broad and mirrored across three
  artefacts (C decoder tests, MALFORMED list vs both CLI import and validator with
  the import/validate distinction correctly drawn at `test_e2e.py:1188–1222`,
  hexdump malformed suite incl. every truncation of every golden stream,
  `test_hexdump_stream.py:43–49`). "Every decoder rule tested" (HANDOFF S5) is fair
  for the implemented tags; the exception is relations (H-tests-09).
- **Fault injection** is unusually complete: every allocation index across a
  whole flow, baseline-ledger assertion, and — critically — byte-exact comparison
  of surviving COMPLETE answers (`test_faults.c:182–186`), not just status sanity.
- **Checker harness honesty**: the `0x0003→0x0002` acceptance is asserted and
  documented rather than hidden (`test_checker.py:389–392`); the oracle similarly
  documents its own non-goals (`oracle.py:40–49`). The evidence base's limits are
  mostly *written down in the artefacts* — the findings above are about closing them,
  not about concealment.
- **Quota accounting** (§11.1) is pinned at exact boundaries for P1 nodes, §8.1
  visits, combined quotas, and per-policy counts, against an independently written
  traversal counter (`test_e2e.py:797–831,1873–1899`). Whatever the §3 ambiguity in
  §3.3 above, off-by-one quota regressions in the tested shapes would fail loudly.
- Test wiring hygiene: `CANON_REQUIRE_CLI` turns skips into failures in all gated
  paths; C tests exit non-zero via `check_finish`; Makefile/CMake target lists agree
  (same 36 tests, `Makefile:42–50` vs `CMakeLists.txt:67–79`).

---

*End of report. No repository files were modified other than this report; nothing was
built or run.*
