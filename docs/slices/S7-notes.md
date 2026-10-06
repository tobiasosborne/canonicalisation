# Slice S7 implementation notes

Brief: [`S7.md`](S7.md). Normative text: `docs/specification.md` v2.1 (cited as §x). Prerequisites: S1–S6 ([`S1-notes.md`](S1-notes.md) to [`S6-notes.md`](S6-notes.md)); their readings still apply. S7 is delivered in steps. **This file covers step 1**: the work-policy ID, `A_known` from source (i), orbit pruning of the canonical image, the CLI options and the P1-prune tier. Step 2 (certificate v0 and the independent checker) and slice S7b (source ii) are not started; their sections will be added here when they land. The rule and its proof are in [`docs/pruning-rules.md`](../pruning-rules.md).

## Step 1

### Files added

| Path | Content |
|---|---|
| `src/symmetry/symmetry.h`, `symmetry.c` | `A_known` as a verified chain grown only by checked automorphisms (bijection, membership in `G`, `x^p = x`); source (i) from the group's recorded input generators; the prefix-stabiliser stack (one unverified rebase per explored child below a nontrivial `H_d`); orbit representatives on the target cell (numerically least per orbit) with the cell-preservation guard; counters |
| `tests/c/test_symmetry.c` | `A_known` against the brute-force closure of the fixing generators on every T1 group, subset, generating set and backend; the insertion discipline; prefix-stabiliser orbits on every injective prefix against brute force (incremental stack and a non-incremental rebase), the soundness guard on every generator; Sym(6), C₂³, a directed 3-cycle, a nested tuple; degenerate cases (no fixing generator, `n = 0`, `n = 1`, a conjugate without recorded generators, a signed group) |
| `tests/c/test_prune.c` | Policies `0x0002` and `0x0001` compared on the T1 subsets (both backends, both generating sets), random graphs and nested roots under every T1 group, random groups of degree 5–7 (trace, bytes, flags; deterministic witnesses equal; ANY witnesses verified; internal counters); the Sym(m) count drop with counts derived from the rules; quota boundaries under both policies and with the deterministic witness; the descriptor; the labeling and signed objectives unpruned; count invariance under generator order, duplicates, identities and backend, and its dependence on the generating set; workspace reuse |
| `docs/pruning-rules.md` | The lemma and proof, `A_known` and its sources (ii marked for S7b), the rule, its scope, the quota rule under the work-policy ID, the presentation question, counters, what is not implemented |
| `docs/slices/S7-notes.md` | This file |

### Files changed

- `include/canon/canon.h`: `canon_work_policy` and `CANON_WORK_POLICY_REFERENCE` / `CANON_WORK_POLICY_ORBIT_PRUNE`; the last field `work_policy` of `canon_capacity`; `canon_result_work_policy`; the documentation of the context defaults, both problem constructors, `canon_solve`, the witness modes and `canon_result_witness`.
- `src/api/api.c`: the default `0x0002`, `resolve_capacity`, the `UNSUPPORTED_ACTION` checks at context and problem creation, `solve_canonical` through `canon_p1_search_run_policy`, the effective policy on the result and its accessor.
- `src/bsgs/group.h`, `group.c`: `canon_group.inputs`, `canon_group_set_inputs`, `canon_group_input_generators` (a signed group answers with its `signs->gens`); a shared row-copy helper.
- `src/bsgs/chain_backend.c`, `src/bsgs/explicit.c`: the unsigned constructors record the input generators.
- `src/search/p1_tree.h`, `p1_tree.c`: `canon_p1_search_run_policy`; the rule in `visit`; the explored-children stack; counters `pruned`, `children`, `work_policy` and the embedded `canon_symmetry`.
- `tools/canon-cli.c`: `--work-policy ID`, `--prune on|off`.
- `tests/python/test_e2e.py`: `cli()` passes `--work-policy 1` by default; `run_cli`, `run_graph_cli`, `run_stream` take `policy` and `extra`; class `PrunedTier` (tier P1-prune); the docstring.
- `tests/c/test_header_abi.c`: static asserts on the IDs. The positional `canon_capacity` initialisers of `test_chain.c`, `test_group_explicit.c`, `test_labeling.c`, `test_objectives.c`, `test_search_dag.c`, `test_search_graph.c`, `test_search_subset.c`, `test_signed.c`, `test_signed_group.c` and `tools/canon-cli.c` gain the eighth field (`-Wmissing-field-initializers` under `-Werror`, as in S6). Policy `0x0001` set explicitly where a test compares an ANY witness with an unpruned prediction or tests a node-quota boundary (see the deviations).
- `Makefile`, `CMakeLists.txt`: `src/symmetry/symmetry.c`, `test_symmetry`, `test_prune`.
- READMEs of `include/`, `src/`, `src/api/`, `src/search/`, `src/symmetry/`, `tools/`, `tests/`, `tests/c/`, `tests/python/`.

### Spec readings taken

1. **Unknown work-policy IDs (§3.2, §4.1, §11.1).** §11.1 v2.1 introduces the ID but does not say what an unknown value gives. Following the rule for unknown identifiers ("unsupported … are refused, never reinterpreted"), any value other than `0`, `0x0001`, `0x0002` is `CANON_UNSUPPORTED_ACTION`, at context creation (after the backend check, which stays `INVALID_INPUT` as in S3) and at problem creation (after the objective/profile/encoding/order and signed-group checks, before the degree and capacity checks), for every objective.
2. **"recorded in results … beside the profile" (§11.1).** The spec names no accessor. `canon_result_work_policy(result, &policy)` returns the EFFECTIVE policy of a completed solve: `0x0002` only for a canonical image solved under `0x0002`, `0x0001` otherwise; `INVALID_INPUT` for an incomplete result. The other reading (echo the descriptor's value) is equally possible; canon.h marks this as provisional.
3. **`0x0002` for objectives other than the canonical image.** The brief prunes only `CANONICAL_IMAGE` (§1, §3.1 Scope; §8.2), and D14 defines `0x0002` as "the S7 sequential prune policy's explored-node count". For the other objectives the S7 policy prunes nothing, so its explored count is the reference count: the descriptor value is accepted and the solve runs and counts as under `0x0001` (and reports `0x0001`).
4. **The rule (brief §3.1).** Implemented as stated, with the representative read as the numerically least atom of each orbit (deviation B2). Coverage is proved in `docs/pruning-rules.md` §4.
5. **Quota (§11.1 v2.1, D9).** `NODE` tokens of the explored nodes; a skipped child is never entered. With the deterministic witness the enumeration receives `quota − nodes`, as in S4; `nodes` is now the explored count, so the deterministic least quota drops by exactly the tree difference (`test_prune.c`: 82 against 45 on the empty subset under Sym(4), 41 − 4 = 82 − 45).
6. **No extra work with `A_known = 1`.** The node loop is the unpruned one when `H_0` is trivial (the `prune` flag of `visit` is false from the root): same nodes, same order, same witness, no rebase and no orbit computation (`test_prune.c` checks this on every T1 case with `A_known = 1`, 1052 of 2156). The root still builds `A_known` (one action and one membership test per input generator).
7. **Identity and duplicate generators.** The handle keeps the generators exactly as given (identities and repeats included). `A_known` filters them by membership (`canon_bsgs_insert_verified` inserts only non-members), so they change nothing; `test_prune.c` checks the explored count with every generator doubled and the identity added.
8. **`H_d` validity along the path.** A slot `d ≥ 1` of the prefix-stabiliser stack is valid only if the parent descended; `visit` therefore carries a `prune` flag from parent to child (false below a trivial `H`), instead of reading a possibly stale slot.

### Ambiguities flagged (not guessed)

- **§11.1 root shortcuts and the `0x0001` count.** §11.1 v2.1: "the §7.3 root shortcuts are part of profile P1's policy and visit no tree nodes", while also "`0x0001` is the unpruned reference traversal (the engine then runs unpruned)". The engine (since S1) has no root shortcut and counts the full tree under `0x0001` even when every generator fixes `x`. Step 1 does not change this; whether the `0x0001` count must be zero in the shortcut case is a question for the maintainer.
- **Presentation dependence of the `0x0002` count** (`docs/pruning-rules.md` §7): §11.1 v2.1 "a function of semantic input, objective and descriptor". Under source (i) the explored count depends on the generating set as presented (C₄ on `{0, 2}`: 3 nodes with the greedy generator, 2 with every element). It does not depend on generator order, duplicates, identities or backend. Question for the maintainer: is the generator list part of the "input" here, or must `A_known` come from a presentation-independent generating set?

### Deviations from the brief and why

- **B1. No `prune` flag in the problem options (brief §1).** D14 (§6.2, decided later the same day) replaces it by `canon_capacity.work_policy`; the CLI keeps `--prune on|off` as sugar for `--work-policy 2|1`, with both options together a usage error and neither accepted by `validate`.
- **B2. "the least `b` of each orbit in cell order" (brief §3.1 Rule).** §7.1: "Internal order of members within a cell is not semantic." The cell's internal (`lab`) order is an implementation artefact that a checker cannot reproduce, so the representative is the numerically least atom of the orbit. Correction to the brief; the explored set, the answer and the count do not depend on it (`docs/pruning-rules.md` §4, §6).
- **B3. "Pruned and unpruned runs give identical traces, bytes and witnesses on every tier" (brief Goal).** Contradicted by the brief's own §3.1 Scope (ANY witnesses may differ). Witnesses are compared in deterministic mode only; ANY witnesses are verified by membership and action. canon.h's promise for ANY mode is reworded.
- **B4. "rebase A_known to the node prefix … take the suffix" (brief §3.2).** Implemented incrementally: `H_{d+1}` from `H_d`'s strong generators with base prefix `(a_{d+1})` (one rebase per explored child), which is the same group; the non-incremental rebase to the whole prefix is the oracle in `test_symmetry.c`.
- **B5. "A_known is maintained as a verified chain (`canon_bsgs_insert_verified`)" (brief §3.1).** That function verifies the chain, not the automorphism; the bijection, membership and action checks of §7.3 are done first by `canon_symmetry_insert`.
- **B6. Counters (brief §1, §3.2).** Nodes explored, leaves, pruned children, automorphisms inserted and rebases exist, plus `children`, `considered`, `rejected`, `not_fixing`, `orbit_calls`. They are internal (read by the C tests); `src/metrics` has no public interface yet, and the CLI record is unchanged (seven fields).
- **B7. Not in step 1:** `src/search/certificate.c`, `checker/`, `canon_result_certificate`, the evidence modes, `--certificate`, `test_certificate.c`, the checker tests, and the certificate part of the P1-prune tier (step 2).

### Deviations from the step plan and why

- **P1.** The input generators are recorded by the backend constructors (`chain_backend.c`, `explicit.c`), as the task asked, not in `api.c`; a signed group's `signs->gens` serve instead of a second copy.
- **P2.** Names: `CANON_WORK_POLICY_ORBIT_PRUNE` (not `…_PRUNE_1`) and a `canon_work_policy` typedef beside `canon_profile`; `canon_result_work_policy` returns a status with an out-parameter (as the task specified) rather than the value.
- **P3.** The plan's `visit` read `canon_symmetry_trivial_at(depth)` at every node; below a node that did not prune, that slot is stale. `visit` takes a `prune` flag from its parent instead (reading 8).
- **P4.** The symmetry state is an array of slots (`chain`, `trivial`) instead of parallel `stab`/`trivial` arrays; added the cell-preservation guard (`in_cell`), the `not_fixing` counter and `canon_symmetry_view` for tests.
- **P5.** The plan proposed setting policy `0x0001` once per older test program through the context default. Policy `0x0001` is set at the affected comparison sites instead, so the rest of those programs keeps exercising the default `0x0002`: `test_search_subset.c` (the golden check and the quota block), `test_search_graph.c` (golden case, the C₃-fixed case, both quota blocks), `test_objectives.c` (ANY against DETERMINISTIC on T1), `test_search_dag.c` (the reference run compared with the deterministic witness; its equivariance loop now compares that reference run with pruned runs of `x^h`), `test_labeling.c` and `test_signed.c` (their canonical-image solves compared with the unpruned labeling/signed `t`). Before the change, five programs failed under the default `0x0002` (`test_labeling`, `test_objectives`, `test_search_graph`, `test_search_subset`, `test_signed`); `test_search_dag` passed but compares with an unpruned prediction, so it was changed too. No assertion was weakened.
- **P6.** The "pruned + explored consistency" check uses a new internal counter `children` (`nodes = 1 + children − pruned` on every complete run).
- **P7.** e2e: `cli(args, policy=1)` (keyword, `None` for none) rather than a separate variant; G2 in the new tier uses the greedy generators only (4 graphs per group, 2 in fast mode).

### Measured counters (no timings)

`tests/c/test_symmetry.c`:

- T1 `A_known`: 2152 cases (n = 1..4, both generating sets and backends), 1100 with `A_known > 1`, 1348 insertions, 14792 rebases, 80744 prefix-orbit checks against brute force.

`tests/c/test_prune.c`:

| Tier | Cases | NODE tokens unpruned / pruned | Children pruned | Inserted | Rebases | `A_known = 1` | ANY witness differs |
|---|---|---|---|---|---|---|---|
| T1 subsets (both backends, both generating sets) | 2156 | 6588 / 3866 | 1686 | 1348 | 1358 | 1052 | 12 |
| T1 random graphs (both backends) | 216 | 269 / 240 | 25 | 20 | 20 | 197 | 0 |
| T1 nested roots (n = 4, both backends) | 120 | 888 / 857 | 19 | 13 | 24 | 107 | 0 |
| random groups, n = 5..7 | 36 | 18882 / 706 | 235 | 45 | 118 | 1 | 0 |

- Empty subset under Sym(m) (generators `(0 1)` and the m-cycle): unpruned 3, 10, 41, 206, 1237 NODE tokens for m = 2..6 (`Σ_k m!/(m−k)!`), pruned m; children pruned `m(m−1)/2`; 2 automorphisms inserted; m − 1 rebases.
- Quota, empty subset under Sym(4): least quota with ANY 41 (unpruned) / 4 (pruned); with the deterministic witness 82 / 45.
- C₄ on `{0, 2}`: 3 nodes (greedy generator) against 2 (every element).

The e2e tier prints no counters (the CLI record carries none).

### Left out of step 1

- Certificates and the checker (step 2); source (ii) (S7b); the §7.3 root shortcut; trace-prefix domination and bound pruning; parallel modes; public counters.
- CMake was not configured or built in this step (the gates run are the `make` ones of `CLAUDE.md`); `CMakeLists.txt` was updated in parallel with the `Makefile`.
