# Fix lane K3-tests: report

Branch: `t3/fix-cross-shape-tests`. No push, no merge. No `src/`,
`checker/` or `HANDOFF.md` edits.

## H-tests-03 — CI/ctest wiring

Confirmed by reading:

- `.github/workflows/ci.yml` python-checks built only `build/make/canon-cli`
  and ran `unittest discover` with `CANON_REQUIRE_CLI=1`.
  `tests/python/test_e2e.py` `Pruning.setUp` calls `self.fail(...)` (not skip)
  when `CANON_REQUIRE_CLI=1` and `canon-check` is absent, and no step built it
  (`Makefile` checker is a separate target, not a dependency of `$(CLI)`).
  So that job as written must fail on `Pruning`.
- `CMakeLists.txt` registered only `test_e2e`, `test_e2e_explicit`,
  `fuzz_replay`, `test_checker`, `canon_check_usage` and the C tests
  (36 C + 5 = HANDOFF's dated "41 ctest entries"). `ctest` never ran
  `checks/review_checks.py`, `test_golden_vectors.py`,
  `test_hexdump_stream.py`, `test_compare.py` or `test_refs.py`, although
  `make check` (Makefile `check:`) runs all of them and HANDOFF's validation
  record presents `make check` + `ctest` together as the verification gate.
- No CI job ran `make check` or `make corpus`.

Changed:

- `.github/workflows/ci.yml`: python-checks now builds
  `build/make/canon-cli build/make/canon-check`; added a `make corpus` step
  (H-tests-03(c), the only manifest-reproduction guard). `make corpus` was
  **not run locally** (FIX-BRIEF forbids it); it is unverified here and must be
  judged by its first CI run.
- `CMakeLists.txt`: registered `review_checks`, `test_golden_vectors`,
  `test_hexdump_stream`, `test_compare`, `test_refs` as CTest tests with the
  existing `CANON_E2E_ENV` (absolute `CANON_CLI`/`CANON_CHECK`,
  `CANON_REQUIRE_CLI=1`, `CANON_E2E_FAST=1` under sanitizers). `ctest` was
  **not run** (FIX-BRIEF forbids it); checked by reading (each file has a
  `unittest.main()`, resolves paths from `__file__`, and reads only
  `CANON_CLI`/`CANON_REQUIRE_CLI`/`CANON_E2E_FAST`).
- HANDOFF claim correction (note only, HANDOFF not edited): the "41 of 41"
  ctest lines are dated 19 October 2026 records of the old registration. With
  the five added tests the ctest set is now 46 entries (36 C + `test_e2e`,
  `test_e2e_explicit`, `review_checks`, 4 Python files, `test_checker`,
  `canon_check_usage`, `fuzz_replay`), assuming Python3 is found. Any future
  validation record should quote 46, and should not imply ctest alone covers
  the explicit-backend checker harness run that only `make check` performs
  (`test_checker` in ctest uses the default backend env).

Test run (the one allowed affected Python class, once, FAST to keep load light):

```
nice -n 19 make build/make/canon-cli build/make/canon-check   # ok
CANON_REQUIRE_CLI=1 CANON_E2E_FAST=1 nice -n 19 python3 -m unittest tests.python.test_e2e.Pruning
# Ran 6 tests in 2.056s  OK
```

## H-tests-02 — cross-shape transporter tests

Confirmed with one correction to the finding: a single cross-shape pair
already existed (`tests/c/test_objectives.c` `validation()`: subset ->
arc-free graph, both transporter objectives). Everything else the finding
lists was absent: no tuple target, no nested root, no graph-as-source,
no stream-subset boundary, no constraint matrix, checkpoints only all-NULL.

Expectations were written from the spec/header text **before** running the
tests (quoted in the test comment):

- spec §3: *"A y of another shape than x (another root kind, e.g. a tuple
  against a set of atoms or a graph against a nested object) with the same
  degree n is an ordinary input, not `INVALID_INPUT`: no g∈G sends x to it,
  so exhaustion gives the complete empty result."*
- spec §8.2: transporter-one *"never hit … complete empty result"*;
  transporter-coset *"If no g, return exhausted empty."*
- spec §7.1 boundary: *"the top-level subset (a set consisting only of atom
  nodes)"* — a stream-imported set of atoms is the same shape and must hit.
- Degree mismatch: spec §3's rule is limited to *"the same degree n"*
  (status named only by exclusion there); `include/canon/canon.h`'s
  problem-creation contract names it `CANON_INVALID_INPUT`.

Changed (`tests/c/test_objectives.c`, already registered in both
`Makefile` `UNIT_TESTS` and `CMakeLists.txt` C-test list — no wiring change
needed; `tests/c/test_version.c` likewise already registered in both):

- `cross_shape()`: at n=2, the 8 ordered pairs subset↔tuple(atom0,atom0),
  subset↔nested set{tuple}, graph↔subset, graph→nested, graph→tuple;
  at n=3, subset→tuple and graph→subset. Each under Sym(n) and G=1, both
  backends, both transporter objectives, both witness modes. Asserts
  `CANON_COMPLETE`, `transport_exhausted` only, no witness/transporter/
  bytes/group payload, `verify_witness` = `INVALID_INPUT`.
- Boundary hits: stream set{atom0} → built {1} and built {0} → stream
  set{atom1} under Sym(2): transporter [1,0]; coset group bytes equal the
  spec §7.4 H=1, r=[1,0] payload.
- Degree mismatch (subset n=2→tuple n=3, graph n=2→subset n=3, subset
  n=3→graph n=2): `CANON_INVALID_INPUT`, out-pointer NULL.
- `constraint_stub_matrix()`: CONSTRAINT_ONE/ENUM × {NO_TREE,P1} ×
  {CDAG-BYTE-1,SIMPLE-UPPER-1} × {NULL target, non-NULL target} (32 rows),
  all `CANON_UNSUPPORTED_ACTION`, out-pointer NULL.
- `tests/c/test_version.c`: checkpoint write/read with a real
  context/workspace and non-NULL bytes are still `UNSUPPORTED_ACTION`
  (read stores NULL through the out-pointer).

Run once after writing (expectations above predated the run):

```
nice -n 19 make build/make/test_objectives build/make/test_version
nice -n 19 ./build/make/test_objectives   # test_objectives: ok
nice -n 19 ./build/make/test_version      # test_version: ok
```

## H-tests-11 / 04 / 05 — if time remains

(see below)
