# refs/compare

**Purpose.** Compare the output of two or more independent implementations (ref-a, ref-b, oracle, later the production library) case by case.

**Files.** `FORMAT.md` defines the interchange record (seven fields since slice S4, which added `group_hex` for canonical `Group(H)`, coset and `SIMPLE-UPPER-1` key bytes; slice S6 documents the labeling and signed records within the same fields: `λ` as the labeling witness, the `;sign=` suffix including `;sign=0` for a certified zero, `00` as its bytes). `compare.py` (Python standard library only) reports agreement per case, exits 1 on any disagreement, and has `--plant` to flip one random hex nibble of one file's `bytes_hex` or `group_hex` and show that the comparison notices (spec §20 planted corruption). `tests/python/test_compare.py` exercises it on records produced by `tools/canon-cli`.

**Governing spec sections.** §20 (acceptance evidence), §3.2 (status), §4.1 (bytes), §7.2 (trace), §9.4 (group and coset bytes), §4.4 (order key).
