# refs/ref-b

**Purpose.** Blind reference implementation B of the frozen semantics (P1, CDAG-2, objectives, status), per `refs/README.md` and spec §20 / plan M0.

**Rules.**
- Written from `docs/specification.md` alone, by an author who does not see `ref-a/`, shares no code with it and exchanges no implementation choices before comparison.
- Any language; may use the standard library of that language but no existing canonicalisation software. Do not copy `checks/review_checks.py` or the expected constants in `refs/vectors/golden.json`: derive results from the rules.
- Reads cases from the JSON inputs in `refs/vectors/` (and generated case files named by seed) and **emits one record per case in the format of `refs/compare/FORMAT.md`**.
- Keep sources, build instructions and seeds in this directory. Seal it by recording the commit hash in `refs/SEALS.md` when complete.

**Governing spec sections.** §3, §3.1, §3.2, §4, §7, §8, §11, §17, §20.

## Running (ref-b, Julia)

**Toolchain:** Julia **1.11.7** (official x86-64 Linux binary), standard library only (`Test`
and `Random` in the tests); no package is downloaded. The JSON reader is ref-b's own
(`src/json.jl`; integers exact).

```sh
# one FORMAT record per case of a case file ("cases" or "p1_cases")
JULIA_DEPOT_PATH=<any writable dir> julia run.jl CASEFILE > out.tsv
# e.g. the golden P1 inputs (deliverable golden-p1.tsv):
julia run.jl ../vectors/golden-inputs.json > golden-p1.tsv
# per-case timing on stderr:
REFB_TIMING=1 julia run.jl FILE > out.tsv
# ref-b's own tests (conventions, kernel vs brute force, invariance, quota):
julia tests/runtests.jl
```

`golden-inputs.json` is the input-field projection of `golden.json`'s `p1_cases`; `run.jl`
reads no answer field of any file.

**Layout.** `run.jl` (runner); `src/json.jl` (JSON reader), `src/perm.jl` (permutations,
spec §3), `src/group.jl` (Schreier-Sims, least coset elements, §8.1 enumerator, quota,
§9.4 `Group(H)`/`Perm`), `src/cdag.jl` (objects, action §2.1, CDAG-2 §4.1, normalisation
§4.2), `src/p1.jl` (P1 §7.1-7.2), `src/cases.jl` (case parsing, CASES.md),
`src/objectives.jl` (objectives 0x0001-0x0007, §3, §3.1, §8, §11.1, FORMAT records);
`tests/runtests.jl`, `tests/handchecked-inputs.json` (ref-b's own inputs for the other §7.4
examples); `QUESTIONS.md` (spec questions raised); `NOTES.md` (state, design, runtimes).
Seeds: the tests use fixed `MersenneTwister` seeds 1-4; no other randomness.

## Running from the repository (coordinator note, added at sealing, 19 October 2026)

The author worked in a sandbox where `refs/vectors/golden-inputs.json` was the input-field
projection of `golden.json`. That file is not in the repository: `run.jl` reads the repository's
`refs/vectors/golden.json` directly (it takes the `p1_cases` array and ignores every other key,
including the answer fields, which it never reads), and the output is byte-identical to
`golden-p1.tsv`. From the repository root, with Julia 1.11.7 on the path as `julia`:

```sh
make corpus                                   # writes refs/vectors/generated/T{1,2,3}-*.json
export JULIA_DEPOT_PATH=$(mktemp -d)          # any writable directory; nothing is downloaded
nice -n 19 julia refs/ref-b/run.jl refs/vectors/golden.json > golden-b.tsv   # = golden-p1.tsv
nice -n 19 julia refs/ref-b/run.jl refs/vectors/generated/T1-seed20261013.json > T1-b.tsv
REFB_TIMING=T1-b.times nice -n 19 julia refs/ref-b/run.jl FILE > out.tsv      # per-case seconds
nice -n 19 julia refs/ref-b/tests/runtests.jl
```

`REFB_TIMING` names a file to which `run.jl` appends `case_id<TAB>status<TAB>seconds` per case
(the comment in `run.jl` is authoritative; the "on stderr" line above is the author's earlier
wording).
