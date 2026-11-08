# Fix lane K1-kernel: report

Branch: `t3/certify-rebase-order`. Base: `main` at the fix-brief commit. No push, no merge.
Output must not change (PROFILE-EQUIV); every fix below is checked for identical
traces/bytes/statuses/node counts where observable (construction counters in `test_chain`
are byte-identical before and after B-kernel-01).

## B-kernel-01 — certify every base change by order equality (decision 1.3)

**Confirmed.** All five production `canon_bsgs_rebase` calls pass `verify=false`
(`src/bsgs/chain.c` tuple minimum, `src/coset/enumerate.c:127`, `src/coset/least.c:152`,
`src/symmetry/symmetry.c:128`, `src/search/certificate.c:282`), and the `verify=false`
path in `canon_bsgs_rebase` returned the rebuilt chain unchecked, against spec §9.2
"base change rebuilds/certifies".

**Change.** Inside `canon_bsgs_rebase` itself, so all five call sites are covered
without touching any caller (the tuple-minimum rewrite in another lane is untouched;
the diff in `src/bsgs/chain.c` is confined to the rebase function and one new helper):

- New helper `rebase_order_certified(src, from, out)` in `src/bsgs/chain.c`: computes
  the product of the rebuilt chain's orbit (transversal) sizes and the source suffix
  order from `from`, both via `orbit_product`, which multiplies with checked `uint64`
  arithmetic (`canon_u64_mul`, `src/arena/checked.h`). Overflow cannot certify. It
  also cross-checks the product against `out->order`. The comment cites spec §9.2 and
  decision 1.3 (`docs/review-2026-11/SUMMARY.md` §1, "Recommendations taken" item 3)
  and states the induction: the source suffix order is itself verified or
  order-certified by the same rule from a verified chain, and equality makes the
  rebuilt chain a complete BSGS of the suffix subgroup. For suffix rebases (`from > 0`,
  e.g. the coset descent and tuple minimum) the comparison is against the suffix
  order, not the whole-group order — comparing against `src->order` would be wrong
  there.
- `canon_bsgs_rebase`: on the `verify=false` path, a failed certificate frees `*out`
  and returns `CANON_INTERNAL_ERROR`. The `verify=true` path is unchanged (full
  verification is stronger than the order check). Order-certified chains stay
  `verified == false`: the check certifies completeness, not the verifier conditions.
- `src/bsgs/chain.h`: the `canon_bsgs_rebase` contract comment now states the order
  certificate, its status, and the freed-and-empty `*out` on failure.

**Tests.** `tests/c/test_chain.c`, new `rebase_order_certificate()` (run first in
`main`):

- Normal rebases pass and keep `order == canon_bsgs_suffix_order(src, from)`:
  Sym(3); Sym(8) (prefixes of length 1, 3 and the full reverse base, plus a suffix
  rebase from level 2 and the terminal suffix); the product group
  Sym(4)×Sym(4) (interleaved prefix); the 64-cycle (long Schreier paths: one orbit
  of 64, a 63-edge path).
- The check fires on a deliberately truncated chain: truncating the source level's
  generator list (`gen_count` cut to 1 or 0, at level 0 and at a suffix level) makes
  the rebase return `CANON_INTERNAL_ERROR` with `*out` freed and empty; restoring
  the count makes the same rebase pass again.
- Before the fix this test failed with exactly 12 failures (the six truncated cases
  × status + emptiness); after the fix `test_chain: ok`, with T1/T2/Sym(12)
  construction counters identical to the pre-fix run.

## B-kernel-03 — verifier trusts unchecked capacities

**Confirmed.** `structure_ok` (`src/bsgs/verify.c`) checked value ranges and non-NULL
pointers but never any capacity field (`node_cap` was not read anywhere in verify.c);
mutating any recorded capacity below its count still verified VALID (6 new test cases
failed before the fix).

**Change.** `structure_ok` now rejects, as `CANON_BSGS_BAD_STRUCTURE`, any count beyond
its recorded capacity: `gens.cap`/`invs.cap`/`inputs.cap` vs their counts, `node_cap` vs
`gens.count`, `prov.cap` vs `prov.count`, and per-level `gen_cap` vs `gen_count`.
What no field records cannot be checked — the allocations behind the pointers and the
per-level orbit blocks (one 5n block in the constructor, no stored capacity) — so
`src/bsgs/verify.h` now states that precondition explicitly at `canon_bsgs_verify`,
including the obligation of any future chain importer. This is the finding's
"cap checks where possible + contract statement" option; a capacity field that
*overstates* its allocation remains undetectable and is named in the contract.

**Tests.** `tests/c/test_verify.c` `other_reasons()`: six mutations (node_cap, the three
table caps, prov.cap, a level's gen_cap, each set one below its count) must each give
BAD_STRUCTURE. `test_verify: ok`; `test_chain: ok` (hand-built chains there and in
test_verify's nesting counterexample already record honest capacities).
