# Fix lane K3-tests: report

Branch: `t3/c39a4af0` (worktree branch at start). No push, no merge. No `src/`,
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

(see below)

## H-tests-11 / 04 / 05 — if time remains

(see below)
