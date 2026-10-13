# refs/vectors/generated

**Purpose.** The generated M0 corpus of detailed plan WP0.6 (tiers T1, T2, T3), produced by `tools/gen_cases.py` from fixed seeds in the case schema of `refs/CASES.md`. Inputs only: no expected answer is stored here. The blind author (`refs/BRIEF.md`) may read these files.

**Retention (decision of work package M0a, `docs/slices/M0a.md`).** The case files are not committed (the exhaustive T1 file alone is 12 MB); `MANIFEST.json` records, for each default file, the generator arguments, the seed, the case count, the byte size and the SHA-256 of the file and of the generator. `make corpus` regenerates the three files and checks them against the manifest (`tools/gen_cases.py --check-manifest`). The generator is deterministic for its arguments (tested by `tests/python/test_refs.py`), so the manifest and the generator together retain the case lists exactly, as spec §20 requires ("seeds must be retained").

| File | Arguments | Cases |
|---|---|---|
| `T1-seed20261013.json` | `--tier T1 --seed 20261013` | 31 121 (exhaustive: every subgroup of `Sym(n)`, `n ≤ 4`; every subset, 3-tuple and, for `n ≤ 2`, digraph with multiplicities ≤ 2 under two colourings; every objective, both orders where defined, two transporter targets, one labeling, every character and one inconsistent sign vector) |
| `T2-seed20261013-count200.json` | `--tier T2 --seed 20261013 --count 200` | 8 395 (200 random groups on `n ≤ 8` with `|G| ≤ 2·10⁵`, one subset, tuple, labelled digraph and nested DAG each) |
| `T3-seed20261013-count50.json` | `--tier T3 --seed 20261013 --count 50` | 1 724 (50 structured groups on `n ≤ 48`: cyclic, dihedral, wreath, grid, products, intransitive sums; tensor skeletons as labelled digraphs; capacity 10⁶ nodes) |

WP0.6's targets are 2 000 (T2) and 500 (T3) cases per objective; the defaults above are smaller so that a run stays light. A larger file is one `--count` away and is recorded by adding a manifest row.

**Running.** `python3 refs/ref-a/run_cases.py FILE > records.tsv` (ref-a), then `python3 refs/oracle/oracle.py FILE records.tsv` on the oracle-sized tiers, and `python3 refs/compare/compare.py` between references. The T3 `COMPLETE` filter of WP0.6 is applied by the records: cases where both references report `CAPACITY_LIMIT` are retained as status-agreement cases.

**Governing spec sections.** §20 (generated cases beyond oracle range, retention), §11.1 (the quota every case names).
