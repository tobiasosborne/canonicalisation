# Fix lane K4-checker (8 November 2026)

Branch: `t3/checker-resource-bounds`. No output changes intended (PROFILE-EQUIV):
step 1 is comment-only; step 2 changes only the checker's capacity verdict
(exit/message) and adds bounds that honest certificates do not reach.

## 1. Stale contract text — A-search-02, A-search-03, C-checker-api-08

Confirmed in code before editing:

- **A-search-02** (`src/search/objectives.h:114`): the `canon_obj_signed` comment said a
  deterministic zero certificate is "unchanged" (pre-D1). The code implements spec v2.2 §3
  (D1): fast path `least_odd_of_group` (`src/search/objectives.c:698-704`, A⁺ = ker χ by
  Schreier generators over the transversal {1, a}) and enumeration route exhausted with
  `least_odd` over A⁺ (`:741-744`). Comment rewritten: "first odd" only for ANY mode on both
  routes; deterministic zero = least odd element of A = least element of A⁺a.
- **A-search-03** (`src/search/certificate.c:129`): the orphaned comment claimed a failed
  certificate is dropped and the search goes on. Opposite is implemented and documented in
  `src/search/certificate.h:48-50` (`fail` deactivates *and* returns the status; call sites
  propagate it). Comment corrected; no hardening of the `!active` early returns (comment-only
  per this task; noted as reachable only after the first failing status was seen).
- **C-checker-api-08** (`include/canon/canon.h`): (a) normative version v2.1 → v2.2 (line 4);
  (b) the supported-combination table's "witness mode CANON_WITNESS_ANY" restriction on
  labeling/signed and the validation-order sentence making a deterministic witness for
  TRANSPORTER_ONE / STABILISER / CANONICAL_LABELING_COSET / SIGNED_CANONICAL_IMAGE
  `UNSUPPORTED_ACTION` are removed — `combination_supported` (`src/api/api.c:529-557`) ignores
  witness mode, mode is validated only as an enum value (`api.c:758-761`, unknown mode =
  INVALID_INPUT, checked before the combination), and spec v2.2 §3 defines the deterministic
  witness of every objective. (c) `canon_solve` evidence line: TRUSTED_ENGINE unless a
  certificate was requested and is attached to a complete result, then
  CHECKED_CERTIFICATE (made checked only by `canon-check` acceptance; `api.c:1433-1439`,
  canon.h evidence enum). (d) `canon_problem_create`: object may also be a nested S5 stream
  root, not only subset/graph. (e) The two §11.1 readings of `SUMMARY.md` §1
  "Recommendations taken" items 1–2 added as comments: item 1 (presentation is part of the
  pruned input; 0x0001 presentation-free; admission may read backend + given generators) at
  `canon_capacity.work_policy` and `max_live_bytes`; item 2 (profile claims trace, bytes and
  the *deterministic* witness; ANY may differ under a prune) at `work_policy` and
  `canon_result_witness`.
- Not edited: `docs/pruning-rules.md:3` still cites spec v2.1 (finding C-08(d)); that file is
  coordinator-owned and outside this lane's file list.

Tests: comment-only; `nice -n 19 make build/make/canon-check` builds clean. No behaviour
changed, so no test can prove the text (per A-search-02).

## 2. C-checker-api-06 — checker resource bounds

Confirmed: `CC_MAX_ALLOC` capped single allocations only (`checker/cc.h`, `cc_reader.c`);
one chain held gens+invs+inputs+`p×n` provenance rows plus `depth×n` `pos` words at once;
`main.c` read the input with unbounded doubling; nesting is `O(depth·s_i·s_{i+1}·n)`
compares (`cc_chain.c` `nested`), refinement splits are insertion sorts with full
signature compares (`cc_refine.c`, `sig_cmp`), every sweep re-verifies a chain; and
`main.c:74-83` printed `REJECT CAPACITY` with exit 1 although `cc.h` says CAPACITY is
"never a verdict".

Changed (checker only; no `src/` or `include/` includes, no producer code):

- **Live-memory ledger** (`cc_reader.c`, declared in `cc.h`): `cc_alloc`/`cc_realloc` charge
  every block (size header, `max_align_t`-sized), `cc_free` uncharges; total live beyond
  `CC_MAX_TOTAL_BYTES` (1 GiB) or a single block beyond `CC_MAX_ALLOC` (1 GiB) is
  `CC_CAPACITY`. All `free()` of checker allocations became `cc_free` (audited: every such
  pointer originates in `cc_alloc`/`cc_get_u32s`); `cc_buf` growth goes through `cc_realloc`.
  `main.c`'s input buffer stays raw but is bounded by `CC_MAX_INPUT_BYTES` (256 MiB) in the
  rewritten `read_all`.
- **Work ledger**: `cc_work_charge`/`cc_work_exceeded` with budget `CC_MAX_WORK` (2³¹ word
  operations), charged at transporter/sift steps (`n` per step), provenance rows, bijection
  scans, nesting compares (`n` per pair), orbit-tree walks, orbit closure, prefix checks,
  level orbit unions, signature zeroing (`n·width`) and scans (`width` per `sig_cmp`), split
  insertion-sort comparisons, string (`bytes_order`) and arc (`arc_order`) compares; checked
  in the verifier helpers, `stage_o`/`stage_g`, `node` entry and the generator/automorphism
  loops, returning `CC_CAPACITY`.
- **Declared-size bounds with checked arithmetic**: `chain_work_bound` in `cc_chain_verify`
  precomputes an upper bound on the verifier's work from `n, depth, g, p, i, s_i, r_i` alone
  (saturating add/mul; one term per condition, transporter ≤ `r` steps, sift ≤ `depth·Rmax`
  steps) and refuses `CC_CAPACITY` before the work; `cc_chain_read` checks `depth×n` (`pos`
  arrays) and `p×n` (provenance rows) against the live budget before reading/allocating them;
  `cc_signatures_compute` checks `n × 2·L·k` likewise.
- **Distinct capacity outcome**: `main` maps `CC_CAPACITY` and an oversized input file to
  exit 2 with `CAPACITY` on stdout and `certificate exceeds checker capacity (no verdict)`
  on stderr; `REJECT` is only for rule violations. Documented in `checker/README.md`
  (usage paragraph and rule 7 rewritten) and `cc.h`.
- `cc_check` resets both ledgers on entry (the fuzz target calls it repeatedly in-process).

Tests added (`checker/tests/test_checker.py`, class `Capacity`): a Nat length of 9 in the
embedded graph stream ⇒ `(2, "CAPACITY")`; a hand-built certificate with `n = depth = 20000`
(small file, `depth×n` alone beyond budget) ⇒ `(2, "CAPACITY")`; a root chain with one
generator id repeated 30000× in each of the first two levels (nesting term
`s₀·s₁·n > 2³¹`) ⇒ `(2, "CAPACITY")`. Probed against the built checker before the full run
(`/tmp/k4_probe.py`, importing the harness): all three pass; a normal certificate still
`ACCEPT`s and a trailing byte still `REJECT TRAILING_BYTES`. The cumulative work ledger and
the 256 MiB input cap have no dedicated test (tripping them needs ~2³¹ charged operations
or a >256 MiB file in the harness); they share the tested `CAPACITY` exit path. Overshoot
note: work is charged as it is done and checked at loop boundaries, so the cumulative total
can exceed the budget by at most one check interval (itself bounded by the per-chain bound).

## 3. H-tests-06 / C-checker-api-05

**C-checker-api-05 (fixed, small).** Confirmed: `cc_object_decode` accepted well-formed but
non-canonical embedded streams (unreachable records, root not last, unsorted/duplicated
arcs normalised away). Fix in `cc_check.c` `run()` after decode: re-encode `x` with the
checker's own `cc_object_encode` and require byte identity with `B(stream of x)`, else
`OBJECT_MALFORMED` (spec §4.1 reachability, §4.2 validate-by-reconstruction; comment cites
both). The producer embeds `canon_root_stream_write` output (the canonical stream), so
honest certificates are unaffected. Documented in `checker/README.md` rule 1.

**H-tests-06 (tests added, `checker/tests/test_checker.py` only).** One mutation per
previously unelicited reason, each asserting the exact reason string: `NODE_DEPTH` (root
depth 0→1; also the deleted-child case now pins `NODE_DEPTH`, probed),
`NODE_ATOM` (root atom NONE→0), `SWEEP_COUNT` (sweeps 1→0), `F_MISMATCH` (recorded `f` +1),
`STAGE_G_SIZES` (first post-chain size +1), `TARGET_CELL` (branch position +1),
`CHILD_MISSING` (unknown child kind `0x02`), `BRANCH_AT_DISCRETE` (leaf kind byte → branch),
`LEAF_NOT_DISCRETE` (branch kind byte → leaf), `REBASED_BASE_PREFIX` (the forged
base-(0,2,1) chain of `test_m_not_minimal` under the honest `M = (0,1,2)`),
`DEGREE_MISMATCH` (stream `U32(n)` +1), `OBJECT_UNSUPPORTED` (set tag `04`→`05`),
`OBJECT_MALFORMED` twice for C-05 (swapped atom records; an unreachable atom record — both
decode to the same subset as before the fix). `HEADER` was already elicited by
`test_policy_header` (the H-tests list appears to have missed it); `CAPACITY` is the new
`Capacity` class (exit 2, not a `REJECT` reason anymore). Acceptance totals are now pinned
in non-FAST mode: T1 = 1617 (539 pairs × 3 policies), G1 = 996 (332 × 3), random sample
= 120 (36 in FAST) — subgroup counts recomputed from `rc.subgroups` in the probe
(1, 1, 2, 6, 30 for Sym(0..4)). A hand-built `n = 0` empty-subset certificate (assembled
from `checker/README.md`'s layout, no producer bytes) is accepted as `ACCEPT 1`, with
`NODE_COUNT` and `FINAL_BYTES` mutations rejected; at `n = 0` the witness is empty, so the
H-suggested witness flip is replaced by those two.

Probed before the full run (`/tmp/k4_probe2.py`, `/tmp/k4_probe3.py` driving the new methods
directly): counts 1617/996, deletion elicits `NODE_DEPTH`, hand-built cert accepted, and all
ten new/updated mutation methods pass.

## Tests run (final)

TODO after the final run.
