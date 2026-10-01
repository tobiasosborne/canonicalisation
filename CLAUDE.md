# CLAUDE.md: guidance for AI agents working in this repository

## Project summary
- An exact, dependency-free C17 library (`canon`) for canonical forms of objects under finite permutation groups, with a frozen wire format (`CDAG-2`) and an independent certificate checker.
- Design-stage: `docs/specification.md` (v2.0) is **normative**; `docs/implementation-plan.md` owns milestones M0-M8. Only `canon_version()` and UNSUPPORTED stubs exist in C.
- Read `README.md` and `HANDOFF.md` first. Do not start a new research cycle without the user's direction.

## Standing constraints (from HANDOFF.md)
- **Keep machine load light.** The user's machine has CPU contention: no dependency downloads, no mathlib fetch/unpacking, no calibration benchmarks, no heavy builds, no broad filesystem scans. Run anything non-trivial under `nice -n 19`.
- **Git.** Stage explicit paths; never `git add -A` at the root. Commit when a task is complete. Push to `origin/main` only when the user asks (or the task is itself publication).
- **No third-party source redistribution.** `review_sources/` is provenance only (URLs, hashes); never commit papers, TeX, vendor documents, isocert code or mathlib files. They are git-ignored.
- **Evidence policy.** Literature claims rest on local primary sources, preferably TeX, with provenance. Web summaries (including TensorGR's web-sourced claims) are leads only; source code and manufacturer documents support implementation and hardware claims, not measurements.
- Lean/lake are not installed here; the `lean/` skeleton is files only and must never be built or fetched.
- Review delegated output skeptically before committing it.

## Build and test
```sh
make                       # gcc, strict flags, library + checker + tests (no cmake needed)
make test                  # run the C tests
make check                 # nice -n 19 review_checks.py + python unit tests
make format                # clang-format if available
make SANITIZE=1 BUILD=build/san test        # address,undefined
cmake -S . -B build && cmake --build build && ctest --test-dir build   # parallel definition
nice -n 19 python3 -m unittest discover -s tests/python -q
nice -n 19 python3 checks/review_checks.py                              # finite sanity checks (~0.6 s)
python3 review_sources/fetch_sources.py                                 # offline provenance check
```

## Layout
`include/canon/` public headers; `src/<module>/` one directory per spec §5 module (api, object, encoding, perm, bsgs, coset, partition, refine, search, symmetry, scheduler, arena, checkpoint, cpu_dispatch, metrics); `checker/` independent certificate checker (must not link the library or include `src/`); `refs/` M0 blind references, oracle, comparison and golden vectors; `tests/c`, `tests/python`; `tools/`; `bench/`; `lean/` (skeleton, not built); `docs/`, `reviews/`, `checks/`, `review_sources/` (coordinator-owned, do not edit without being asked).

## Rules for code
- **The spec is normative; code must cite the spec section in a comment at each rule it implements** (e.g. `/* spec 7.2: leaf map */`). If the spec is ambiguous, stop and report the quoted sentence; do not guess.
- **Never copy golden constants into implementations: derive them from the rules (spec §20).** Golden vectors (`refs/vectors/golden.json`) are test expectations only. The blind references must not see each other or the expected answers (`refs/README.md`).
- Permutation convention: `p[v] = v^p`, `(pq)[v] = q[p[v]]`. Test array direction, product order and inverses explicitly.
- Do not modify `docs/specification.md`, `docs/implementation-plan.md`, `docs/performance-lower-bounds.md`, `reviews/*`, `review_sources/*`, `LICENSE` or `checks/review_checks.py` unless the user asks.
- C17 only; no external dependencies; check sizes, overflow, aliasing and ownership; wire streams never dump structs.
- Every faster path must reproduce P1 exactly or carry a new profile ID (PROFILE-EQUIV); never claim stronger completeness than the evidence supports.
