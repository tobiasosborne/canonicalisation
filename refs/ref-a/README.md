# refs/ref-a

**Purpose.** Reference implementation A of the frozen semantics (P1, CDAG-2, objectives, status), per `refs/README.md` and spec §20 / plan M0.

**What ref-a is (decided 2 October 2026, detailed plan §0a item 6; `refs/BRIEF.md` answer 13).** ref-a is the sealed snapshot of the unpruned production C path: `libcanon` and `tools/canon-cli` at the commit named in `refs/SEALS.md`, run under work policy `0x0001` (the unpruned reference traversal) with the deterministic witness. It is **not blind** with respect to `checks/` and `tools/` (its seal row says so); it shares no code with ref-b and its authors have not read `refs/ref-b/`. ref-b (Julia) is the blind implementation.

**`run_cases.py`** (work package M0a, `docs/slices/M0a.md`). The case-file adapter: `python3 refs/ref-a/run_cases.py [--cli PATH] [--backend chain|explicit] [--out FILE] CASEFILE...` reads the `p1_cases` of `refs/vectors/golden.json` or the `cases` of a generated file (`refs/CASES.md`), turns every case into one `canon-cli` call (`--prune off` unless the case names work policy 2 or 3, `--witness deterministic` unless `witness_mode` is `"any"`, `--max-nodes` from `capacity`; the CLI reads a quota of 0 as its default) and prints one `refs/compare/FORMAT.md` record per case in file order. Objects of kind `tuple` and `dag`, and any transporter pair of mixed kinds, are written as raw CDAG-2 streams of the spec §4.1 grammar (children reordered below their parents, set children deduplicated, multiset pairs merged, `Group` leaves as rule-2 presentations): an input encoding the library normalises on import (spec §4.2), never an answer. Without calling the CLI it answers a signed labeling problem, objectives 8–9 and an unknown work policy with `UNSUPPORTED_ACTION`, and a schema violation (missing or forbidden key, objective name and tag that disagree) with `INVALID_INPUT`, in a record with empty hex fields. Standard library only.

**Build.** `make` (gcc 13.3.0 here; `make CC=clang` also builds). The runner finds `build/make/canon-cli`, or `$CANON_CLI`, or `--cli`.

**Tests.** `tests/python/test_refs.py`: the runner reproduces every expected field of `golden.json`; the statuses answered without the CLI; a DAG listed parent-first. The oracle (`refs/oracle/oracle.py`) accepts its records on the golden cases and a seeded T2 sample.

**Governing spec sections.** §3, §3.1, §3.2, §4, §7, §8, §11, §17, §20.
