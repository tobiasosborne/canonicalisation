# Slice S7 implementation notes

Brief: [`S7.md`](S7.md) (decisions D9–D11, D14 of 2 October 2026). Normative text: `docs/specification.md` v2.1 (cited as §x). Prerequisites: S1–S6 and their notes. Rules: [`docs/pruning-rules.md`](../pruning-rules.md). Checker: [`checker/README.md`](../../checker/README.md). This file records what was built, the readings taken, the deviations from the brief, the measured counters, certificate sizes and check times (no engine timings), and what was left out. Implemented 12 October 2026 by one agent (no separate implementer/reviewer this time, on the maintainer's instruction; the self-review is recorded below).

## Files added

| Path | Content |
|---|---|
| `src/symmetry/symmetry.h`, `symmetry.c` | `A_known` as a verified chain (`canon_symmetry_insert`: exact action check, membership by sift, verified rebuild); the prefix stabiliser by transient rebase (`canon_symmetry_stabiliser`); orbits of the target cell by BFS with a Schreier tree (`canon_symmetry_cell_orbits`, `canon_symmetry_rep`); the explicit transporter of a pruned child (`canon_symmetry_transporter`); counters |
| `src/search/certificate.h`, `certificate.c` | Certificate v0 writer (`CERT-0`): header, embedded stream and generators, the root chain built and verified from the generators, the automorphism table, node records through a refinement observer, the final claim; the chain serialiser |
| `checker/cc.h`, `cc_reader.c`, `cc_perm` (in `cc_reader.c`), `cc_chain.c`, `cc_object.c`, `cc_refine.c`, `cc_check.c`, `main.c` (rewritten) | The independent checker `canon-check` (components and rules in `checker/README.md`) |
| `checker/tests/test_checker.py` | Acceptance tiers (T1 subsets, G1 digraphs, a random sample on 5 and 6 points, both policies) and the mutation tests |
| `docs/pruning-rules.md` | Rule P1-ORBIT-1: lemma, proof, rule, sources, objective scope, quota, checker rule; the source-ii argument marked for S7b |
| `tests/c/test_symmetry.c`, `test_prune.c`, `test_certificate.c` | See `tests/c/README.md` |
| `docs/slices/S7-notes.md` | This file |

Files changed:

- `include/canon/canon.h`: `canon_capacity.work_policy` and `CANON_WORK_POLICY_UNPRUNED`/`PRUNE_1`; `canon_problem_options.certificate`; `canon_evidence`, `canon_result_certificate`, `canon_result_evidence`; the witness documentation under the prune.
- `src/api/api.c`: the work-policy default (`0x0002`) and validation, the certificate option (canonical image of a subset or graph root only), the certificate writer in the workspace, the result's certificate copy, `solve_canonical` with run options.
- `src/search/p1_tree.h`, `p1_tree.c`: run options (`canon_p1_run_options`: prune, certificate writer); root automorphisms from the group's generators; the prefix stack; per-depth child decisions (representative, automorphism index) taken before any child is visited; pruned children skipped and counted; certificate hooks at every node, sweep, leaf and child; the leaf computes `t_L` for every leaf when a certificate is written.
- `src/refine/p1.h`, `p1.c`: the refinement observer (`canon_p1_observer`) called after the O stage (sizes) and the G stage (F, u, sizes); `canon_p1_refine_node` takes it.
- `src/bsgs/group.h`, `group.c`, `chain_backend.c`, `explicit.c`: every group handle keeps a copy of its input generators (`canon_group_set_generators`), on both backends, for the automorphism source and the certificate.
- `src/search/objectives.c`: the labeling and signed P1 runs pass no options (unpruned under every policy).
- `tools/canon-cli.c`: `--prune on|off`, `--certificate FILE`.
- `tests/python/test_e2e.py`: `cli()` passes `--prune off` unless the caller gives `--prune` (D14: the model tiers compare under policy `0x0001`); the `Pruning` class (tier P1-prune and the certificate pipe).
- `tests/c/test_search_subset.c`, `test_search_graph.c`, `test_objectives.c`, `test_labeling.c`, `test_signed.c`: the solves whose statements are about the unpruned tree (reference node counts; the "any" witness equals the deterministic one; the labeling's and signed objective's `t` against a canonical-image solve) set `CANON_WORK_POLICY_UNPRUNED` explicitly. The positional `canon_capacity` and `canon_problem_options` initialisers gained the new fields (`-Wmissing-field-initializers`).
- `Makefile`, `CMakeLists.txt`: the new sources, the checker's six sources, the three C tests, the checker harness in `make check` and `ctest` (`test_checker`; `canon_check_usage` replaces the stub test); CMake passes `CANON_CHECK` to the Python tests.
- READMEs of `checker/`, `src/symmetry/`, `src/search/`, `src/bsgs/`, `src/api/`, `include/`, `src/`, `tools/`, `tests/c/`, `tests/python/`; `README.md`, `HANDOFF.md`, `CLAUDE.md`, `docs/detailed-implementation-plan.md` (§0a).

## Spec and brief readings taken

1. **Cell order and the representative (brief §3.1 "the least `b` of each orbit in cell order").** Read as: the first member of the orbit in the cell's current member order is explored, every later member is pruned with it as representative. The member order inside a cell is not semantic (§7.1), so under `witness_mode = ANY` the pruned run's witness (the least `t` over the explored leaves) may depend on it; the brief accepts this and the deterministic mode is unaffected. The node count does not depend on it (equivariant subtrees have equal counts, `docs/pruning-rules.md` §2).
2. **Policy `0x0002` for the other objectives (§11.1 v2.1).** The work-policy ID names "the traversal fixed by the profile's policy for the objective". The S7 prune covers `CANONICAL_IMAGE` only (brief §1, §8.2), so under `0x0002` every other objective runs, and counts, its reference traversal; the P1 runs inside the labeling and signed objectives stay unpruned too (their `t` is part of the answer and compared with the model). Stated in `docs/pruning-rules.md` and `canon.h`.
3. **Source (i) only (D11).** `A_known` is generated by the input generators of `G` that fix `x`, checked by exact action. The lemma needs no completeness; a trivial `A_known` prunes nothing. The generators are taken as given (identities and repeats included; `canon_bsgs_insert_verified` drops members).
4. **The leaf's `t` and the last G stage (brief §3.4 rule 6).** At a discrete node after the final sweep, `F = L` and the G stage's `u` minimises `L^G`; the minimiser is unique (`L` is a full list), so `t_L = u`. The certificate records `t_L` as the engine computed it (by `tuple_min`) and the checker requires `t = u` after verifying `u`'s minimality through the rebased chain. No separate chain for the leaf.
5. **Rebased-chain group equality (brief §3.5 "checks the inputs against the root chain's generators").** Read as mutual membership: every input of the rebased chain sifts in the root chain and every generator of `G` sifts in the rebased chain (§9.2 "Group equality checks mutual generator membership in complete chains"). With both chains verified this gives `⟨rebased inputs⟩ = G`.
6. **`M` minimality (brief §3.4 rule 4).** `M = min F^G` lexicographically iff, for each `i`, `M[i]` is the least point of the orbit of `M[i]` under `G_{M[0..i-1]}`: §7.2's constructive procedure chooses `b = min(a^H)` greedily, and the uniqueness of the lexicographic minimum makes the greedy choice forced. Level `i` of a chain with base prefix `M` is exactly that orbit, so the checker reads the condition off the verified rebased chain.
7. **Evidence mode (§3.2).** A result carrying a certificate reports `CANON_EVIDENCE_CHECKED_CERTIFICATE`, meaning that the evidence offered is a certificate for the independent checker; the claim is checked only when `canon-check` accepts it. The engine never asserts that acceptance ("A valid witness alone never upgrades one to the other").
8. **Quota under a certificate.** Writing a certificate does not change the traversal or the count; the leaf's `t_L` is computed for every leaf (also those the trace alone rejects), which is extra work, not extra nodes.
9. **Certificate failures.** A writer failure (allocation, a size that does not fit) fails the run with that status; a requested certificate is never silently dropped (§17: no fabricated completion). A capacity-limited run has no certificate.

## Deviations from the brief and why

- **Format of the node records (brief §3.3).** A sweep count per node (the record is self-delimiting; the checker verifies that the recorded count is exactly the number of sweeps §7.1 prescribes), explicit child points `U32(b)` on both explored and pruned entries (so the checker checks the cover of the cell as a set and never depends on the engine's internal member order), one generator id per generator and the provenance node of every generator in the serialised chain (the inverses are recomputed by the checker, not stored), and `U64(nodes explored)` in the final claim (the checker verifies it against the node records and, under policy `0x0001`, that no child was pruned: D14(b) "the checker verifies the count it is given against the policy named"). The header carries `U16(work_policy)` after the profile (D14(b)).
- **One transporter per pruned child in the automorphism table.** The brief lists "automorphisms: dense arrays, in insertion order". The table holds the input generators inserted into `A_known` followed by, for every pruned child, the explicit element `a ∈ A_S` with `b'^a = b` read off the Schreier tree (a product of `A_known`'s generators). The checker needs exactly one automorphism per pruned child (brief §3.3); verifying each by its own sift and action keeps the checker free of any orbit computation.
- **`canon_group_set_generators`.** The explicit backend keeps no generators (S6 notes), and the automorphism source and the certificate need them on both backends, so every handle now keeps a copy; the chain backend's own `inputs` table is unchanged.
- **The refinement observer** (`canon_p1_observer` in `src/refine/p1.h`) instead of a certificate pointer in the refiner: `refine` stays independent of `search/certificate.h`.
- **Child decisions before recursion.** The orbit computation's Schreier tree is reused by every child visit, so the representatives and the transporters of a node's pruned children are computed and written (into the automorphism table) before the first child is visited; the per-depth decision stack grows like the snapshot stack.
- **Workspace scratch.** The certificate writer lives in the workspace (its buffers are reused across solves; the result owns a copy, §17).
- **No separate `prune` flag in the API (D14).** The work policy selects it; the CLI's `--prune on|off` maps to the two IDs.

## Brief errors found

- **§3.2 "a case with a large automorphism group ... shows the node count drop to the orbit count"** is right; the unpruned count of the empty subset under `Sym(6)` is 1 + 6 + 30 + 120 + 360 + 720 = 1237 (after five individualisations the partition is discrete), not 1957 as first assumed when writing `test_prune.c`.
- **§3.3 drafted node record** had no sweep count and no child point; see the deviations (the checker would otherwise have to reproduce the engine's internal cell member order, coupling it to `src/partition/`).
- **§3.4 rule 7 "the witness equals the recorded leaf's `t` (any-witness mode)"**: the certificate's witness is the least `t` over the explored leaves attaining the key, whatever the result's witness mode; in deterministic mode the result's witness (from the complete stabiliser) may differ from the certificate's. Stated in `canon_cert_finish`.
- No mathematical defect in the lemma or the rules was found; the brief's lemma is reproduced in `docs/pruning-rules.md` with the induction over depth made explicit.

## Measured counters (no timings of the engine)

`test_symmetry` (T1, every subset): 772 candidates, 306 inserted, 19 648 orbit representatives and 19 648 transporters checked against brute force.

`test_prune` (both backends): 1 546 pruned-versus-unpruned pairs agree on trace and bytes, 1 234 in deterministic mode on the witness too. Node counts:

| Case | Unpruned nodes | Pruned nodes | Pruned children | Rebases |
|---|---|---|---|---|
| empty subset, `Sym(2)` (§7.4) | 3 | 2 | 1 | 0 |
| empty subset, `Sym(6)` | 1237 | 6 | 15 | 4 |

`checker/tests/test_checker.py` (chain backend, full tiers; certificate sizes in bytes; check time is the wall-clock sum of the `canon-check` processes on this container, reported as required by §20 "Count certificate generation/checking and size"):

| Tier | Certificates | Total bytes | Largest | Nodes | Pruned children | Check time |
|---|---|---|---|---|---|---|
| T1 subsets, both policies | 1078 | 1 829 309 | 23 772 | 2655 | 389 | 2.0 s |
| G1 digraphs, both policies | 664 | 372 189 | 994 | 691 | 9 | 1.3 s |
| random sample `n = 5, 6` | 80 | 1 362 620 | 189 443 | 1214 | 26 | 0.16 s |

The size is dominated by the rebased chain per G stage per node (D10); the 189 kB certificate is a random subset under a group on 6 points with 73 nodes.

## Self-review (no separate reviewer this slice)

Items checked after the implementation, in the manner of the earlier reviews:

1. Every writer failure returns a status that fails the run; `fail()` deactivates the writer so a later hook cannot write into a half-finished record.
2. No pointer into a released object survives a run: `canon_symmetry_insert` clears the image it used; `canon_root_image_clear` at the end of the run as before.
3. The decision stack and the prefix array are indexed by depth ≤ n − 1 and grow geometrically; the snapshot and decision slots are re-read after each child visit because the stacks may have moved.
4. The S1–S6 tests that assume the reference traversal were made explicit about it rather than weakened: four files now set `CANON_WORK_POLICY_UNPRUNED`; the two reference-count quota checks and the S4 "any equals deterministic witness" check still hold under policy `0x0001`.
5. The checker reads every index before using it (chain structure, child points, automorphism indices, cell positions) and bounds every allocation; a certificate ending inside a field is `TRUNCATED`, never a read past the buffer (the harness truncates certificates).
6. `-Werror` clean under gcc and clang; ASan/UBSan clean on the C tests, the e2e tiers and the checker harness (`ctest` with `CANON_SANITIZE=ON`: 36 of 36 in about 325 s, of which `test_e2e` 145 s and `test_e2e_explicit` 140 s under `CANON_E2E_FAST`, `test_checker` 9 s).

## What was left out

- Implicit automorphisms from equal leaf keys (source ii): slice S7b.
- Certificates for the enumeration, labeling and signed objectives and for nested roots: v1.
- Trace-prefix domination and bound pruning (§7.3).
- The Lean checker (M1/M4). The C checker's acceptance is evidence under the trusted compiler and runtime (§20).
- Compaction of the certificate (one rebased chain per G stage per node is kept, D10).
