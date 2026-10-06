# tests/

**Purpose.** Unit tests for the library and for the repository's data and tools.

- `c/`: C17 tests run by `ctest` and by `make test` (no test framework; plain executables returning 0 on success); see `c/README.md` for the list.
- `python/`: `unittest` tests (standard library only, no pytest): `test_golden_vectors.py` verifies `refs/vectors/golden.json` against `checks/review_checks.py`; `test_hexdump_stream.py` tests `tools/hexdump_stream.py`; `test_e2e.py` drives `canon-cli` against the finite model (see `python/README.md`; since slice S7 step 1 it compares the model tiers under the unpruned reference work policy `0x0001` and adds tier P1-prune for the default policy `0x0002`).

**Run.** `make test` for C; `nice -n 19 python3 -m unittest discover -s tests/python -q` for Python; `make check` for the review checks plus the Python tests.

**Governing spec sections.** §20 (acceptance evidence), §17 (client-level golden, error and lifetime vectors before each entry point is released), §7.4 (golden cases). Tests must derive expected values from rules where possible; golden constants come only from `refs/vectors/golden.json`, never from the implementation under test.
