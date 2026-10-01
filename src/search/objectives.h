/*
 * Internal header: the enumeration objectives of spec 8.2 over the coset enumerator of spec
 * 8.1, and the deterministic witness of spec 3 (slice S4, docs/slices/S4.md 3.3; detailed plan
 * WP4.4).
 *
 *   LEX_MIN_IMAGE      minimum of the order key of x^r over every r in G (CDAG-BYTE-1: the
 *                      CDAG-2 stream; SIMPLE-UPPER-1: the spec 4.4 key);
 *   TRANSPORTER_ONE    one g with x^g = y, or proved empty after exhaustion;
 *   STABILISER         A = Aut_G(x) as a verified chain, returned as Group(A) bytes;
 *   TRANSPORTER_COSET  empty, or A g returned as Group(A) || Perm(r0);
 *   deterministic witness of the canonical image: the least element of A t.
 *
 * Every objective visits the fixed reference traversal through the group's `enumerate` op, so
 * the quota (spec 11.1) is a function of the input and the descriptor.  The group may be of
 * either backend (canon_group_ops); the stabiliser A is always a chain built here.
 */
#ifndef CANON_SRC_SEARCH_OBJECTIVES_H
#define CANON_SRC_SEARCH_OBJECTIVES_H

#include <stdbool.h>
#include <stdint.h>

#include "bsgs/group.h"
#include "canon/canon.h"
#include "coset/coset.h"
#include "encoding/group_stream.h"
#include "encoding/wire.h"
#include "object/object.h"

/* Counters of the last run (docs/slices/S4-notes.md; spec 8.3 "must appear in metrics"). */
typedef struct canon_obj_stats {
    uint64_t nodes;       /* enumeration visit calls (all enumerations of the run) */
    uint64_t leaves;      /* consume calls */
    uint64_t hits;        /* x^r = y (transporter) or x^r = x (stabiliser) */
    uint64_t stab_builds; /* verified rebuilds of the stabiliser chain (<= log2 |A|) */
    canon_coset_stats coset;       /* descents and transient rebuilds, all phases */
    canon_group_bytes_stats group; /* the last Group(A) payload */
} canon_obj_stats;

/* What a complete run produced (spec 3.2 flags plus which buffers hold an answer). */
typedef struct canon_obj_outcome {
    canon_result_flags flags;
    bool witness; /* best holds a witness (x^best = the answer image, or y) */
    bool bytes;   /* bytes holds the CDAG-2 stream of the minimum image */
    bool key;     /* best_key holds the SIMPLE-UPPER-1 key of the minimum */
    bool group;   /* group holds Group(A) or Group(A) || Perm(r0) */
} canon_obj_outcome;

/* Reusable mutable state (one active owner, spec 17).  Buffers persist across runs and grow
 * only.  Invariant: after every run, on every status, no member points into the run's objects
 * or group (the image's borrowed graph tables are cleared, as in src/search/p1_tree.h). */
typedef struct canon_obj_search {
    uint32_t cap;           /* degree the per-point arrays are allocated for */
    uint32_t *best;         /* cap: the witness */
    uint32_t *work;         /* cap: sift residue */
    uint32_t *g;            /* cap: the transporter of a coset */
    uint32_t *agens;        /* 64 * cap: the generators inserted into the stabiliser */
    canon_root_image image; /* x^r */
    canon_buf key;          /* order key of the current leaf */
    canon_buf best_key;     /* least key so far */
    canon_buf bytes;        /* CDAG-2 stream of the minimum image */
    canon_buf group;        /* Group(A) or Group(A) || Perm(r0) */
    canon_obj_stats stats;
} canon_obj_search;

void canon_obj_search_init(canon_obj_search *s);
void canon_obj_search_free(canon_obj_search *s);

/* Run objective LEX_MIN_IMAGE, TRANSPORTER_ONE, STABILISER or TRANSPORTER_COSET for x under g
 * (same degree); y is the target of the transporter objectives (same kind and degree as x),
 * NULL otherwise; `order` is CDAG-BYTE-1 or (minimum only, x in the spec 4.4 class)
 * SIMPLE-UPPER-1; `deterministic` selects the spec 3 deterministic witness (minimum: the least
 * g attaining the minimum; coset: r0); `quota` bounds the visit calls of all enumerations of
 * the run (spec 11.1).  Returns CANON_COMPLETE with *out describing the answer, or
 * CANON_CAPACITY_LIMIT, CANON_CANCELLED, CANON_RESOURCE_LIMIT, CANON_INTERNAL_ERROR (out
 * zeroed); CANON_INVALID_INPUT / CANON_UNSUPPORTED_ACTION for arguments the API excludes. */
canon_status canon_obj_run(canon_obj_search *s, const canon_group *g, const canon_root *x,
                           const canon_root *y, canon_objective objective, canon_order order,
                           bool deterministic, uint64_t quota, canon_obj_outcome *out);

/* spec 3: "minimise A g after A is proved complete": run the stabiliser consumer for x (quota
 * as above) and write the least element of A t into s->best, where t is a witness of the
 * canonical image (x^t = c).  Every g with x^g = c lies in A t (spec 3: if x^p = x^q then
 * p q^-1 fixes x), so this is the least solution. */
canon_status canon_obj_deterministic_witness(canon_obj_search *s, const canon_group *g,
                                             const canon_root *x, const uint32_t *t,
                                             uint64_t quota);

/* spec 17 result_verify_witness: *valid = (w in G, by the backend's contains) and (the
 * CDAG-2 stream of x^w equals c).  Never asserts canonicity.  CANON_RESOURCE_LIMIT /
 * CANON_CAPACITY_LIMIT when scratch cannot be allocated (*valid = false). */
canon_status canon_obj_check_witness(const canon_group *g, const canon_root *x, const uint32_t *w,
                                     const uint8_t *c, size_t c_len, bool *valid);

#endif /* CANON_SRC_SEARCH_OBJECTIVES_H */
