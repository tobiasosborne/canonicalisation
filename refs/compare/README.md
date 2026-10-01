# refs/compare

**Purpose.** Compare the output of two or more independent implementations (ref-a, ref-b, oracle, later the production library) case by case.

**Files.** `FORMAT.md` defines the interchange record. `compare.py` (Python standard library only) reports agreement per case, exits 1 on any disagreement, and has `--plant` to flip one random hex nibble of one file's bytes and show that the comparison notices (spec §20 planted corruption).

**Governing spec sections.** §20 (acceptance evidence), §3.2 (status), §4.1 (bytes), §7.2 (trace).
