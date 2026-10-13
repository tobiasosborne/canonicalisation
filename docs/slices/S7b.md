# Slice S7b: implicit automorphisms from equal leaf keys

**Status: implemented 13 October 2026 (`S7b-notes.md`); written and implemented by one agent on the maintainer's instruction ("work by yourself"), as S7 was.** Normative text: `docs/specification.md` v2.1 §3, §7.2, §7.3, §11.1, §14.3 (R2), §20. Prerequisite: S7 (`S7.md`, `S7-notes.md`, `docs/pruning-rules.md`). This slice adds the second source of `A_known` that S7 deferred (D11) and the work-policy ID its count needs.

## 1. Scope

In scope:

- **Source (ii), spec §7.3 "Verify implicit automorphisms by exact membership and object equality before publication".** When a leaf `L` has the same key as the incumbent (equal trace and equal bytes), `x^{t_L} = x^{t_best}`, so `a = t_L t_best^{-1}` fixes `x` (§3: "If x^p=x^q then p q⁻¹ fixes x"). The engine verifies `a ∈ G` (the group's membership test) and `x^a = x` (exact action and extensional equality), then inserts `a` into the verified chain `A_known` unless it is already a member. The lemma of `docs/pruning-rules.md` applies to the enlarged subgroup unchanged.
- **Incremental orbit decisions at a node.** The children of a node are decided in cell order as they are reached, not all at once: before a child is explored, if `A_known` grew since the node's last orbit computation, the prefix stabiliser `A_S` is recomputed and the orbits of every explored child and of the child about to be explored are marked under the new `A_S`; every undecided member reached is pruned with that explored child as representative and an explicit transporter. An automorphism found below a child therefore prunes the later children of every ancestor node (the usual use of a discovered automorphism), not only nodes entered later.
- **Work-policy ID `0x0003` (`CANON_WORK_POLICY_PRUNE_2`), the new default.** §11.1 v2.1: "a pruning policy that changes the count has its own work-policy ID". `0x0002` (`PRUNE_1`, explicit automorphisms only, decisions taken once per node) is kept as a selectable policy with S7's count; `0x0001` is the unpruned reference. The profile ID stays `0x0001`: every policy reproduces P1's trace and bytes.
- Certificates: no format change. Implicit automorphisms are listed in the automorphism table like the explicit ones; the checker verifies every listed automorphism by its own sift and action (rule 2) and every pruned child by one named automorphism (rule 5), so the checker is unchanged except for accepting `0x0003` in the header.
- CLI: `--prune on|explicit|off` (`on` = `0x0003`, the default; `explicit` = `0x0002`; `off` = `0x0001`).
- Tests: `test_prune.c` and tier P1-prune compare all three policies (trace and bytes; deterministic witness); node counts under the three policies on the cases of S7 and on a case where only implicit automorphisms exist; the checker harness accepts the `0x0003` certificates.

Out of scope: pruning for other objectives (§8.2), trace-prefix domination and bound pruning (§7.3), certificate v1, any change to the checker's rules.

## 2. Soundness

`A_known ≤ A = Aut_G(x)` is preserved: each inserted element is verified to lie in `G` and to fix `x`. The lemma of `docs/pruning-rules.md` §2 needs only `a ∈ A` fixing the prefix pointwise, which the stabiliser `A_S ≤ A_known` provides for every element it yields, including the transporters read off the Schreier tree. The incremental decision keeps the invariant "every pruned child has an explored representative in its `A_S`-orbit for the `A_S` current at the time of the decision", and `A_S` only grows at a node, so a representative explored earlier remains an explored child of the same node. Hence the explored leaves still cover every leaf key, and the least key over the explored leaves is the canonical image.

The count under `0x0003` is a function of the input (object, generators as given), the objective and the descriptor: the sequential policy fixes the order in which leaves are met and automorphisms are discovered, and every decision depends only on `A_known` at that moment and the cell order. Parallel modes must reproduce it (S8).

## 3. Definition of done

- [x] All S1–S7 tests pass unchanged under `0x0001`; `0x0002` keeps S7's counts; `0x0003` agrees with both on trace and bytes on every tier, and on the witness in deterministic mode.
- [x] `docs/pruning-rules.md` states source (ii) as implemented, the incremental rule and the three policies.
- [x] Checker accepts every emitted certificate under all three policies; the mutation tests pass unchanged.
- [x] `make`, `make test`, `make SANITIZE=1 BUILD=build/san test`, `make check`, `make CC=clang`, CMake with `CANON_SANITIZE=ON` and `ctest`.
- [x] `docs/slices/S7b-notes.md`; READMEs; HANDOFF.
