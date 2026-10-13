# Work package M0a: preparation of the M0 gate (ref-a runner, generated corpus, oracle, corruption runs, seal)

**Status: brief, written 13 October 2026 by the implementing agent (the maintainer directed "grind through the next steps"; the §0a schedule says "After S8 the M0 gate ... follow[s]").** Normative text: `docs/specification.md` v2.1 (§3, §4, §7, §8, §9.4, §11.1, §17, §20). Plan: `docs/implementation-plan.md` M0; `docs/detailed-implementation-plan.md` §3 (WP0.2–WP0.8) and §0a item 6; `refs/README.md`, `refs/BRIEF.md` (maintainer answers), `refs/CASES.md`, `refs/compare/FORMAT.md`.

## 1. Goal

Everything of the M0 gate that one agent on the coordinator's side can do without breaking the blind protocol: the production path becomes **ref-a** through a case-file runner, the seeded generated corpus exists, the exhaustive oracle exists, the planted-corruption runs are made and logged, and ref-a is sealed. **Not in scope:** ref-b (a blind author, commissioned by the maintainer), the comparison ref-a against ref-b, the divergence log, the "semantics frozen" tag.

## 2. Preparatory library change: the deterministic witness for every objective

`refs/CASES.md` Part B (maintainer, 2 October 2026): "Every M0 corpus case requests [the deterministic witness], and `compare.py` compares the `witness` field on every case"; "For objectives that report no witness, FORMAT writes `-` whatever the mode." The library refuses `CANON_WITNESS_DETERMINISTIC` for `TRANSPORTER_ONE`, `STABILISER` (S4 brief 2), `CANONICAL_LABELING_COSET` and `SIGNED_CANONICAL_IMAGE` (S6 reading 16), so the corpus could not be run as decided. Spec §3 defines the witness for every solution set: "minimise the image array among all solutions sending x to the selected c, either by complete enumeration or by minimising A g after A is proved complete." Readings, each implemented and tested:

1. **`TRANSPORTER_ONE`.** The solutions are exactly `A g` (§8.2: "All solutions r satisfy r g⁻¹ ∈ A"); the deterministic witness is their least element `r₀`, computed as for `TRANSPORTER_COSET` (one hit, the complete stabiliser, the coset descent of §9.4) but reported without the group bytes. The quota covers the whole solve (§11.1), so a deterministic transporter may hit `CAPACITY_LIMIT` where the "any" mode completes; the status stays a function of input and descriptor.
2. **`STABILISER`.** No witness exists; the mode is accepted and nothing changes (`-` in FORMAT).
3. **`CANONICAL_LABELING_COSET`.** The labelings taking x to c are `Aλ` (§3.1); the deterministic labeling is its least element `λ₀`, which the payload already carries as `Perm(λ₀)` (§9.4); the reported `t` becomes `t₀ = ρ⁻¹λ₀ ∈ G′`, so `λ₀ = ρt₀` still holds. Trace, bytes and payload are unchanged.
4. **`SIGNED_CANONICAL_IMAGE`.** Nonzero: `A` is complete on that route (§8.4), and the deterministic witness is the least element of `A t`; χ is constant on `A t` because `A` is even, so the sign is unchanged. Certified zero: the witness is the first odd automorphism of the frozen §8.1 traversal (or the first odd generator on the §7.3 fast path), as before; the mode changes nothing, since §3's sentence speaks of solutions sending x to c and a zero has no c.

Byte identity: the "any" and deterministic modes give the same status (except the quota case of reading 1), trace, bytes and group bytes on every tier.

## 3. Deliverables

| Item | Path | Content |
|---|---|---|
| ref-a runner | `refs/ref-a/run_cases.py`, `refs/ref-a/README.md` | Reads a case file (`golden.json`'s `p1_cases` or a generated file's `cases`, `refs/CASES.md`), maps each case to one `canon-cli` invocation (work policy `0x0001` unless the case names another, `--witness deterministic` unless `witness_mode` is `"any"`, `--max-nodes` from `capacity`), writes `tuple` and `dag` objects as raw CDAG-2 streams (spec §4.1 record layouts; input encoding only, never an answer), and prints one FORMAT record per case. A signed labeling case, objectives 8–9 and an unknown work policy are `UNSUPPORTED_ACTION`; a malformed case is `INVALID_INPUT`; both without calling the CLI. Standard library only. |
| generated corpus | `tools/gen_cases.py`, `refs/vectors/generated/` | WP0.6 tiers T1 (exhaustive on the listed groups and objects), T2 (random, seeded), T3 (structured families, seeded), every objective, every case requesting the deterministic witness; `{"cases": [...], "seed": ..., "tier": ...}`; deterministic for a given seed. Counts are arguments; the committed files record the seeds used. |
| oracle | `refs/oracle/oracle.py`, `refs/oracle/README.md` | WP0.3: for a case file and a FORMAT file, for every case with `|G| ≤ 2·10⁵`, checks the record against the definitions of §3, §4, §8.2, §8.4 and §9.4 by explicit enumeration, with its own CDAG-2 encoder, normaliser and `Group(H)` encoder written from the spec (it reuses `closure`, `mul`, `inverse` of `checks/review_checks.py` only, as WP0.3 permits). Traces are not checked (they are defined by P1, not by the orbit). Exit 1 on any failure; each failure names the case and the rule. |
| corruption runs | `refs/compare/runs/` | WP0.7: `CANON_PLANT=sign|action|encoding` switches in the production path, one T1 run of planted against clean ref-a per switch, each showing at least one `DISAGREE`; logs retained; the switches removed before the seal. |
| seal | `refs/SEALS.md` | The ref-a row (C17, gcc 13.3.0, the production path S1–S8, not blind w.r.t. `checks/` and `tools/`, the landing commit). |
| tests | `tests/python/test_refs.py` | The runner on `golden.json` reproduces every `expected_*` field; the generator is deterministic and schema-valid; the oracle accepts ref-a's records on a T1 sample and rejects a corrupted record and a wrong-order minimum. |
| docs | `docs/slices/M0a-notes.md`, READMEs, `HANDOFF.md`, `README.md`, `CLAUDE.md`, detailed plan M0 checklist | As for the slices. |

## 4. Rules

- The runner and the generator are coordinator tooling; they may import `checks/review_checks.py` for group closure. The oracle must not call `p1`, `dag_bytes`, `graph_bytes` or `group_bytes` (WP0.3).
- Nothing here tells a blind author what the C path does; the generated files carry inputs only.
- No download; `nice -n 19` for every run; the T3 filter (`COMPLETE` under `max_search_nodes = 10⁶`) is applied by the runner's records, not by the generator.
- Keep machine load light: generated counts default below WP0.6's targets; the targets are recorded for the full M0 run.
