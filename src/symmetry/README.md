# src/symmetry

**Purpose.** Verified automorphism discovery and the known-automorphism subgroup used for pruning.

**Governing spec sections** (`docs/specification.md`, normative): §7.3 (pruning and A_known), §8 (objective-specific completeness), §8.4 (signed canonical images), §9.1/§9.2 (verified chains, rebase), §14.3 R2 (only verified automorphisms).

**Milestone.** **M4** (`docs/implementation-plan.md`); delivered by slices.

**Status.** Implemented in slice S7 step 1 (`docs/slices/S7.md` §3.1, §3.2; the rule and its proof in `docs/pruning-rules.md`): `symmetry.h`/`symmetry.c`. `A_known` is a verified stabiliser chain (`canon_bsgs_build_verified` from zero generators, enlarged by `canon_bsgs_insert_verified`) of automorphisms that passed `canon_symmetry_insert`'s exact checks: a bijection, a member of `G` (the group's own test), `x^p = x` (action and extensional equality) (§7.3, §14.3 R2). Source (i) only: `canon_symmetry_from_inputs` offers every recorded input generator of the group (`canon_group_input_generators`) and skips those that move `x`. Prefix stabilisers: `canon_symmetry_descend` builds `H_{d+1} = Stab_{H_d}(a_{d+1})` by one unverified `canon_bsgs_rebase` per explored child, none below a trivial `H`. `canon_symmetry_orbit_reps` returns the numerically least member of each `H_d`-orbit on the target cell (§7.1: order inside a cell is not semantic), after checking that every generator of `H_d` preserves the cell. Counters: candidates considered, rejected, generators not fixing `x`, insertions, rebases, orbit calls. Source (ii), implicit automorphisms from equal leaf keys, is slice S7b. Tests: `tests/c/test_symmetry.c` (brute force on the T1 tier), `tests/c/test_prune.c`.

**What may live here.** C17 sources and internal headers for this module only, no third-party code and no dependencies beyond the C standard library. Each rule implemented must cite its spec section in a comment (see `CLAUDE.md`). Golden constants must be derived from the rules, never copied (spec §20).
