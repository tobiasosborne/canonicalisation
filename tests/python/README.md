# tests/python

**Purpose.** `unittest` tests for data and tools (no pytest, no pip installs).

**Run.** `nice -n 19 python3 -m unittest discover -s tests/python -q` from the repository root.

**Tests.** `test_golden_vectors.py` recomputes each entry of `refs/vectors/golden.json` with `checks/review_checks.py` (P1 trace, stream and witness; group and coset payloads; convention vectors; signed and labeling examples). `test_hexdump_stream.py` parses the six spec §7.4 streams and malformed variants with `tools/hexdump_stream.py`.

**Governing spec sections.** §7.4 (golden cases), §4.1 (wire grammar), §9.4 (Group payload), §20.
