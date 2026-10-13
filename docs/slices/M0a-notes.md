# Work package M0a implementation notes

Brief: [`M0a.md`](M0a.md) (written in this session; its decisions are the implementer's and await the maintainer's review). Normative text: `docs/specification.md` v2.1 (cited as §x). Prerequisites: S1–S8. Implemented 13 October 2026 by one agent on the maintainer's instruction ("grind through the next steps"); the §0a schedule names the M0 gate as what follows S8. Self-review below.

## Files added

| Path | Content |
|---|---|
| `refs/ref-a/run_cases.py` | The ref-a case-file adapter over `canon-cli` (brief §3): schema checks, raw CDAG-2 writer for `tuple`/`dag` objects and mixed transporter pairs, one FORMAT record per case |
| `tools/gen_cases.py` | The seeded WP0.6 generator (T1 exhaustive, T2 random, T3 structured; `--check-manifest`) |
| `refs/vectors/generated/README.md`, `MANIFEST.json` | Retention of the corpus as seeds, counts and SHA-256 hashes; the files come from `make corpus` |
| `refs/oracle/oracle.py` | The WP0.3 exhaustive oracle: own CDAG-2 encoder and §4.2 normaliser, §9.4 `Group(H)` and coset writers, §4.4 key, §8.4 lift; per-objective rules and cross-case invariances |
| `refs/compare/runs/2026-10-13-wp07/` | The WP0.7 corruption-run logs and their README |
| `tests/python/test_refs.py` | Runner on the golden cases and the schema's out-of-scope statuses; generator determinism and schema; oracle acceptance and rejection |

## Files changed

- `src/api/api.c`, `src/search/objectives.c`, `objectives.h`, `include/canon/canon.h`: the deterministic witness for every objective (brief §2); `canon_obj_labeling` and `canon_obj_signed` take a `deterministic` flag.
- `tests/c/test_objectives.c`, `test_labeling.c`, `test_signed.c`: the mode is accepted; `TRANSPORTER_ONE` reports `r₀` on the Sym(4) coset case and no group bytes; on every T1 labeling case the deterministic labeling equals the brute-force least of `Aλ` with `ρt = λ`, same bytes, trace and payload; on every T1 signed case the deterministic witness equals the brute-force least of `A t` (nonzero) or the same odd automorphism (zero), same sign, bytes and group.
- `tests/python/test_e2e.py`: `EnumerationObjectives` checks the deterministic transporter (least solution) and stabiliser; `labeling_case` and `signed_case` run the deterministic mode against the model (`min(coset)`, `min(a t)`); the former refusal checks now expect `COMPLETE`.
- `Makefile` (`make corpus`), `.gitignore` (generated case files), READMEs (`refs/`, `refs/ref-a/`, `refs/oracle/`, `refs/compare/`, `refs/vectors/`, `tools/`, `tests/python/`, `src/search/`, `src/api/`), `CLAUDE.md`, `README.md`, `HANDOFF.md`, detailed plan (M0a readings, exit-checklist row), `refs/SEALS.md` (ref-a row, in the sealing commit).

## Readings taken

1. **Deterministic witness everywhere (§3).** As the brief's §2 states, with these consequences checked: for `TRANSPORTER_ONE` the quota may now be exceeded where the "any" mode completes (the stabiliser enumeration is part of the solve); for the labeling, `t` changes to `ρ⁻¹λ₀`, which `canon_obj_check_labeling` accepts since `ρt = λ₀` and `λ₀ρ⁻¹ = a λ ρ⁻¹ ∈ G`; for the signed nonzero route the sign is read off the replaced `t`, so no invariant is assumed.
2. **Mismatched transporter targets.** `canon-cli` requires the target to have the object's root kind (a set of atoms is a subset object, an empty multiset a nested one) and reports `INVALID_INPUT` otherwise (S4: §17 "mismatched target"). Found by the oracle on a T2 case (`t2-s11-000199-transporter_one`); the oracle now accepts `INVALID_INPUT` or the exhausted empty result when the two shapes differ, and nothing else.
3. **Quota 0.** The CLI reads `--max-nodes 0` as its default, so the runner cannot request a quota of 0; the generator never writes one. Noted in the runner's documentation.
4. **Raw DAG input (`refs/CASES.md` Part B).** Children may be listed after their parents; the wire needs them below. The runner emits the reachable records in child-first order with the root last, deduplicates set children and merges multiset pairs (the wire forbids duplicates); a `Group` leaf is written as a rule-2 presentation of its generators (rule 1 for the trivial group), which import normalises. None of this chooses an answer.
5. **Corruption switch `encoding` on T1.** T1 has no nested object, so omitting the §4.2 height ordering changes no T1 stream; the switch is shown on T2 (10 disagreements of 846). Recorded in the run README and the detailed plan.

## Measured counters

- Corpus: T1 31 121 cases (every subgroup of Sym(n), n ≤ 4: 539 subsets, 2 099 3-tuples, 332 digraphs; every objective; 1.2 s); T2 default 8 395 cases (200 groups); T3 default 1 724 cases (50 groups). Generation is deterministic (tested).
- ref-a on the T2 sample of 20 groups (846 cases): 770 `COMPLETE`, 76 `INVALID_INPUT` (the inconsistent sign vectors), 9.9 s. Oracle on those records: 846 checked, 0 failed, 8.4 s (it also confirmed that every `INVALID_INPUT` was an inconsistent character).
- Oracle on the golden cases through ref-a: 6 checked, 0 failed.
- WP0.7: `sign` 588 and `action` 110 disagreements on 1 557 T1 cases (every 20th); `encoding` 0 on T1, 10 on the 846 T2 cases; a control run of the planted binary without a switch agreed on every record.
- T3: a run of the default file was stopped after 73 records (72 `COMPLETE`, 2 `INVALID_INPUT`) when the 74th case, `STABILISER` of a nested object on 21 points under a two-generator group, had run for more than ten minutes: the unpruned §8.1 enumeration under a 10⁶ quota visits up to 10⁶ cosets, each acting on a nested object. A full T3 run is hours of CPU on this machine and is scheduled with the ref-a/ref-b comparison; the T3 canonical-image cases on 48 points took under 20 ms each.

## Self-review

1. The deterministic `TRANSPORTER_ONE` reuses the coset route's pieces (`stabiliser`, `canon_coset_least`) and frees the chain on every path; `s->g` holds the hit while `s->best` receives `r₀`.
2. The labeling's deterministic branch runs only after a complete payload (`s->work` holds `λ₀`), uses `s->g` as scratch for `ρ⁻¹` (free at that point) and keeps `n == 0` out (nothing to copy).
3. The signed branch replaces `t` before the character is evaluated, so `s = χ(t₀)` is computed, not inferred.
4. The runner never computes an answer: every record comes from the CLI except the schema statuses, which carry empty hex fields and `-`.
5. The oracle's encoder was checked against the production streams on 6 golden and 770 T2 records (every `bytes_hex` compared as an orbit member or minimum) and its `Group(H)` writer against 200 stabiliser and coset payloads of the T2 sample; a deliberate corruption of a witness, a minimum and a group each fails (tested).
6. The three corruption switches were deleted by pattern after the runs; the two original lines they wrapped were restored and `git diff` of `dag.c` and `p1_tree.c` is empty; gcc, clang and the sanitizers rebuilt afterwards.
7. gcc and clang `-Werror` clean; `make test`, `make SANITIZE=1 test`, `make check` (with `test_refs.py`) and the CMake sanitizer `ctest` pass (see HANDOFF's validation record).

## Left out

- ref-b, the comparison ref-a/ref-b/oracle, the divergence log and the "semantics frozen" tag (need a blind author and the maintainer).
- WP0.6's full counts (2 000 and 500 cases per objective) and the full T3 run.
- A quota of 0 through the CLI; a `--witness any` corpus (no M0 case uses it).
