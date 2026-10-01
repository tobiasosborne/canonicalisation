# tests/python

**Purpose.** `unittest` tests for data and tools (no pytest, no pip installs).

**Run.** `nice -n 19 python3 -m unittest discover -s tests/python -q` from the repository root, after `make` (`test_e2e.py` needs the CLI: it reads `CANON_CLI`, else `build/make/canon-cli`; without a built CLI its tests skip with a message, unless `CANON_REQUIRE_CLI=1`, which `make check`, `ctest` and CI set, turns the skip into a failure).

**Tests.** `test_golden_vectors.py` recomputes each entry of `refs/vectors/golden.json` with `checks/review_checks.py` (P1 trace, stream and witness; group and coset payloads; convention vectors; signed and labeling examples). `test_hexdump_stream.py` parses the six spec §7.4 streams and malformed variants with `tools/hexdump_stream.py`. `test_e2e.py` (slice S1) runs `canon-cli` over every subgroup of `Sym(n)`, `n ≤ 4`, and every subset with two generating sets (full element list and greedy sequence), comparing trace, bytes and witness with `checks/review_checks.p1` and with each other, checks that the witness is the spec §3 deterministic witness, runs the subset entries of `golden.json`, and checks the §11.1 node quota, invalid input and usage errors.

**Governing spec sections.** §7.4 (golden cases), §4.1 (wire grammar), §9.4 (Group payload), §20.
