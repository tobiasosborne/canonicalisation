# M0 comparison run, 19 October 2026 (WP0.7/WP0.8): ref-a against ref-b

**State: partial.** Stopped on the maintainer's instruction before T3 finished under ref-a and before the
divergence review of ref-b's questions. Golden, T1 and T2 are compared in full; T3 only on ref-a's completed
prefix (76 of 1 900 cases).

**Inputs.** `make corpus` at `38f3f95` (the manifest check passed: T1 34 091, T2 11 331, T3 1 900 cases;
seeds in `refs/vectors/generated/MANIFEST.json`) and `refs/vectors/golden.json` (6 `p1_cases`).

**Implementations.**
- ref-a: sealed at `f9c0fb6` (`refs/SEALS.md`, third ref-a row); `git diff --stat f9c0fb6 HEAD -- src include tools refs/ref-a` is
  empty, so HEAD runs the sealed code. Built with `make` (gcc 13.3.0), run through `refs/ref-a/run_cases.py` (imported
  unchanged by a scratch wrapper that timed each `run_case` call; on golden its output is byte-identical to the
  runner's own).
- ref-b: sealed at `2f82a37` (`refs/SEALS.md`, ref-b row), Julia 1.11.7, `julia refs/ref-b/run.jl FILE` with
  `REFB_TIMING` (one process per tier; the first case of each tier carries the JIT warm-up).
- Both under `nice -n 19`, one process each; the ref-a T3 run overlapped the other runs.

## Results

| Tier | Cases | compare.py | Statuses (both refs identical) | Oracle on ref-a | Oracle on ref-b | ref-a wall | ref-b wall |
|---|---|---|---|---|---|---|---|
| golden | 6 | 6 AGREE, 0 DISAGREE | 6 COMPLETE | 6 checked, 0 failed | 6 checked, 0 failed | 0.06 s | 9 s |
| T1 | 34 091 | 34 091 AGREE, 0 DISAGREE | 33 061 COMPLETE, 1 030 INVALID_INPUT | 34 091 checked, 0 failed | 34 091 checked, 0 failed | 50 s | 13 s |
| T2 | 11 331 | 11 331 AGREE, 0 DISAGREE | 10 683 COMPLETE, 648 INVALID_INPUT | 11 331 checked, 0 failed | 11 331 checked, 0 failed | 237 s | 155 s |
| T3 | 1 900 | **prefix only:** 76 AGREE, 0 DISAGREE (cases 0–75) | ref-b on all 1 900: 1 598 COMPLETE, 302 CAPACITY_LIMIT | not run (T3 is beyond oracle range by design) | not run | **unfinished**: 76 cases in 0.2 s, then stopped after ≈ 17 min on case 77 | 42 s (all 1 900) |

The oracle skipped nothing (0 beyond range or capacity) on golden, T1 and T2.

**Planted corruption (`compare.py --plant`, default seed).** T1: nibble 2 of `group_hex` of
`t1-s20261013-027205-signed_canonical_image` flipped in ref-b's file, 1 DISAGREE, `PLANT DETECTED`. T2: nibble 55
of `bytes_hex` of `t2-s20261013-010143-canonical_labeling_coset`, 1 DISAGREE, `PLANT DETECTED`.

**Divergences.** None on any compared case (golden, T1, T2, T3 prefix), so there is no per-cause divergence log
yet; `DIVERGENCES.md` is not written.

**T3 under ref-a.** The run reached case index 76 and was stopped while ref-a was on
`t3-s20261013-000077-canonical_image` (n = 32, 3 generators, a subset), which had not returned after about
17 minutes; ref-b answers that case `CAPACITY_LIMIT` in 0.27 s. The 76 completed ref-a cases took 0.2 s in total (slowest 8 ms,
`t3-s20261013-000038-canonical_labeling_coset`), so no meaningful ref-a T3 top-10 exists yet. ref-b's ten slowest
T3 cases (seconds): 000001-canonical_image 7.40 (includes JIT), 000412-transporter_coset 1.02,
000415-signed 0.83, 001766-canonical_image 0.76, 000413-stabiliser 0.72, 000409-transporter_one 0.72,
000324-labeling 0.68, 000317-canonical_image 0.68, 000407-canonical_image 0.66, 000416-signed 0.64; sum of
per-case times 38.3 s.

## Files

`golden-compare.txt`, `T1-compare.txt`, `T2-compare.txt`, `T3-prefix-compare.txt`: `compare.py` output with the
AGREE lines removed (the summary line; there were no other lines). `T1-plant.txt`, `T2-plant.txt`: `--plant`
output without AGREE lines. `*-oracle-a.txt`, `*-oracle-b.txt`: the oracle's summary line. The TSVs are not
kept (regenerable); their SHA-256:

```
9e3309c33e7fca3c565b76eddc2739901302e2d56d7bd95d6a0abed4e44b4895  golden-a.tsv
45a2dbb5cbf47b8173578128174bf134c187fb8c12aca335ae69ebb4d2a77bab  golden-b.tsv (= refs/ref-b/golden-p1.tsv)
e8c3687ec68dce412aab7436ca4b7e87a7ef818f590921f2f6dd71691286b37d  T1-a.tsv
53a77cf67d269a613797dd27606fbaf6559640573e5476df4d8aaacd99e2716a  T1-b.tsv
5c97bf9a950ab21b3c96942c011393177ac37f6bf62cf65395e74fa4faeddafc  T2-a.tsv
8f4ccf0ac60cb4d8311c6cf2e05284611b835d307a2c32b8e1fc4263695467e6  T2-b.tsv
b4d235ec9e60863ed39bc2d1922d3eebac43fabdd8768abbb566f354ad721eb7  T3-a.tsv (prefix: header + 76 records)
5aae28567574e480174d755c5ffbc13404bf4dacdaf0ed0d28a844e70b85b080  T3-b.tsv (all 1 900 records)
```

(ref-a files start with the runner's `#` header line; ref-b files have none.)

## Unfinished (next steps)

1. T3 under ref-a in full (hours; it stalls on canonical-image cases of n = 32 such as 000077 that ref-b reports
   `CAPACITY_LIMIT` quickly), then `compare.py` on all 1 900 cases. The WP0.6 rule keeps a case where both
   return `CAPACITY_LIMIT`; whether ref-a returns `CAPACITY_LIMIT` on 000077 at all is not yet known.
2. The review of ref-b's `QUESTIONS.md` (Q1–Q11) against spec v2.2 and against ref-a's readings, and
   `DIVERGENCES.md` for any T3 disagreement. On golden, T1 and T2 no question produced a disagreement; several
   (Q1 quota boundary, Q2 visit unit, Q3 discrete-root count, Q10 singleton-orbit count) can only show at a
   quota boundary, which T3's `CAPACITY_LIMIT` cases are the first to exercise.
3. `--plant` against ref-b's records on T3 once complete, and the WP0.7 environment switches against ref-b (the
   switches were removed from ref-a before its seal; the nibble plant is the only one run here).
