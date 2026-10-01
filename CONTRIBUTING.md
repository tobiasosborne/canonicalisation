# Contributing

The specification (`docs/specification.md`) is normative. Read `HANDOFF.md` and `CLAUDE.md` for standing constraints (light machine load, explicit `git add` paths, no third-party source redistribution).

## Conventions
- **C17 only**, no dependencies beyond the C standard library. Build with `-std=c17 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Wvla -Werror`; zero warnings. Format with `make format` (`.clang-format`: LLVM base, 4-space indent, 100 columns).
- **Permutation convention.** Arrays store `p[v] = v^p` (source to target). Products act left to right: `(pq)[v] = q[p[v]]`, and `(x^p)^q = x^(pq)`. Inversion of a product reverses its factors. Never mix in the usual `Equiv.Perm` composition.
- **Every public function is documented with its spec section** (a one-line comment in the header). Internal rules cite the spec section where they are implemented.
- **Tests are required** for every function: C tests in `tests/c` (ctest), Python tests in `tests/python` (`unittest`). New behaviour without a test is not complete.
- **No golden constants in implementations.** Derive results from the rules (spec §20); constants appear only in tests and `refs/vectors`.
- No dependency downloads; run non-trivial commands under `nice -n 19`.

## Commit messages
Imperative mood, a short subject (50 characters or fewer, no trailing period), a blank line, then a body explaining why and citing spec sections or milestone IDs (M0-M8). Stage explicit paths; never `git add -A`. Commit when a task is complete; push to `origin/main` only when asked.

## How to add a golden vector
1. Derive the case **by hand from the spec rules** (not from the code). If the case comes from spec §7.4, transcribe the printed hex exactly and expand the abbreviations `H(n)`, `A(a)`, `S(ids)`, `B0`, `T(ids)`.
2. Add an entry to `refs/vectors/golden.json` (hex lowercase, no spaces, `source` field naming the spec section, `spec_text` quoting the case).
3. Make `tests/python/test_golden_vectors.py` recompute it with `checks/review_checks.py` (or an independent model); it must pass before the entry is accepted. Quote any ambiguity in the spec instead of guessing.
4. Re-run `nice -n 19 python3 -m unittest discover -s tests/python -q` and `make check`.
5. Do not tell blind implementers (`refs/README.md`) the expected values.
