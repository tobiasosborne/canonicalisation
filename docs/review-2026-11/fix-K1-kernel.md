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

## B-kernel-04 — `least_outside` equality shortcut trusts its precondition

**Confirmed.** `canon_coset_least_outside` (`src/coset/least.c`) returned "nothing
outside" whenever the recomputed suffix orders were equal, i.e. it trusted the
unchecked `K <= H` precondition, and `canon_bsgs_suffix_order` returns 0 on `uint64`
overflow so two overflowing products would also compare equal. New test cases with
two distinct order-2 subgroups of Sym(3) returned "nothing outside" before the fix.

**Change.** The shortcut now uses the chains' stored exact `order` fields (never the
0-on-overflow recomputation; a built chain's order is at least 1, also guarded) **and**
checks containment `K <= H` via the existing `level_in` (spec §9.2 "containment
checks all generators") before deciding `K = H` (spec §9.4 "until K=H"). Equal orders
of distinct groups therefore fall through to the descent, which returns the genuine
least outside element. The containment test runs only when the orders are already
equal — for the sole production caller (rule 2 in `src/encoding/group_stream.c`) that
is only the terminating iteration. The general `K <= H` precondition stays in
`src/coset/coset.h`, now with the shortcut's checks stated. One edge changed
deliberately: the shortcut now binds the descent scratch first, so an allocation
failure there surfaces as `CANON_RESOURCE_LIMIT` instead of a scratch-free
`CANON_COMPLETE` on the equal-order path; results, bytes and descent counts on
successful runs are unchanged.

**Tests.** `tests/c/test_coset_least.c`, new `outside_shortcut()`: `H = ⟨(0 1)⟩`,
`K = ⟨(1 2)⟩` (and symmetrically) must find `(0 1)` resp. `(1 2)` outside; Sym(3)
from two presentations must give "nothing outside". `test_coset_least: ok` and
`test_group_stream: ok` (Group bytes of the production caller unchanged).

## B-kernel-05 — `canon_group_set_signs` leaks on a second call

**Confirmed.** `src/bsgs/group.c` assigned `group->signs = s` without releasing any
existing block; the §11.2 live-memory ledger showed the leak directly (new test:
live bytes grew on the second call and never returned to baseline after release).

**Change.** A new `group_signs_free()` helper in `src/bsgs/group.c` (also used by
`canon_group_unshare`, replacing its open-coded triple) is called on the previous
block after the new one is fully built, mirroring `canon_group_set_generators`;
failure paths are unchanged, so a failed call still leaves the handle unchanged
(spec §17). The `src/bsgs/group.h` contract now says a previous block is freed
(the old "(signs NULL)" on failure was wrong for a second call). No production
caller calls it twice today (both backends call it once on a fresh handle), so
behaviour there is identical.

**Tests.** `tests/c/test_signed_group.c`, new `set_signs_twice()`: three calls with
different signs/counts; the replacement content is read back each time, live bytes
after a same-size second call equal those after the first, and after
`canon_group_release` live bytes return exactly to the pre-create baseline.
`test_signed_group: ok`, `test_signed: ok`.

## B-kernel-06 — chain `insert()` not failure-atomic

**Confirmed.** `insert()` in `src/bsgs/chain.c` pushed the generator row, then the
inverse row, then the provenance inverse, then the id level by level; a failure
partway left `gens.count != invs.count`, the id in some levels only, and possibly an
appended base point. No inconsistent chain escapes today (`canon_bsgs_build` frees
the whole chain on any failure), so this was a contract hazard for any future reuse.

**Change.** `insert()` snapshots the entry depth and generator count; a new
`insert_rollback()` helper undoes the counted pushes on every allocation-failure
path (node-array growth, the gens/invs/provenance pushes, and each level push —
levels `0..i-1` when level `i` fails) and removes a base point appended by the same
insert, restoring the terminal level. After an allocation failure the chain denotes
exactly the group it denoted on entry (`gens.count == invs.count`, no level names a
missing generator). Capacities, construction counters and already-appended
provenance nodes keep their grown values (work done, not chain meaning; unreferenced
provenance nodes are inert). The one deliberately non-atomic failure stays the
order overflow (`CANON_CAPACITY_LIMIT`), detected only after the orbits are
extended; the comment at `insert()` states the chain must then be freed, not reused,
and that `canon_bsgs_build` frees on every failure regardless. Success-path behaviour
is untouched: T1/T2/Sym(12) construction counters are identical to before.

**Tests.** `insert()` is static and `canon_bsgs_build` frees the chain on failure, so
the rollback itself is observable only as releasable, consistent state under fault
injection: `tests/c/test_chain.c`, new `build_fault_atomicity()`, sweeps every
allocation index of a Sym(8) verified build — each faulted build ends in
RESOURCE/CAPACITY with an empty chain and live bytes back at baseline (no leak, no
double free; ASan covers the frees in the sanitizer run below). The pre-existing
whole-flow sweep also passes: `test_faults: ok` (1927 allocations per flow, 1927
faulted runs, every complete solve equal to the fault-free answer).
