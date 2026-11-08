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

TODO.

## 3. H-tests-06 / C-checker-api-05 (if time)

TODO.
