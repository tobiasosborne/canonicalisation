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
 *   deterministic witness of the canonical image: the least element of A t;
 *   CANONICAL_LABELING_COSET (slice S6, spec 3.1, 8.2): P1 on the target coordinates for
 *                      t in G' = rho^-1 G rho, lambda = rho t, and the complete coset A lambda
 *                      returned as Group(A) || Perm(lambda0);
 *   SIGNED_CANONICAL_IMAGE (slice S6, spec 8.4, 7.3): a certified zero with its odd
 *                      automorphism, or the P1 image c with s = chi(t) and the complete A.
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
#include "search/p1_tree.h"

/* Counters of the last run (docs/slices/S4-notes.md; spec 8.3 "must appear in metrics"). */
typedef struct canon_obj_stats {
    uint64_t nodes;                /* enumeration visit calls (all enumerations of the run) */
    uint64_t leaves;               /* consume calls */
    uint64_t hits;                 /* x^r = y (transporter) or x^r = x (stabiliser) */
    uint64_t stab_builds;          /* verified rebuilds of the stabiliser chain (<= log2 |A|) */
    canon_coset_stats coset;       /* descents and transient rebuilds, all phases */
    canon_group_bytes_stats group; /* the last Group(A) payload */
    uint64_t p1_nodes;             /* S6: NODE tokens of the P1 run of a labeling or signed solve */
    uint64_t fast_path;            /* S6: 1 iff the signed solve took the spec 7.3 generator fast
                                      path (every generator fixes x, so A = G) */
    uint64_t characters;           /* S6: evaluations of chi (stabiliser hits and checks) */
} canon_obj_stats;

/* What a complete run produced (spec 3.2 flags plus which buffers hold an answer). */
typedef struct canon_obj_outcome {
    canon_result_flags flags;
    bool witness; /* best holds a witness (x^best = the answer image, or y) */
    bool bytes;   /* bytes holds the CDAG-2 stream of the minimum image */
    bool key;     /* best_key holds the SIMPLE-UPPER-1 key of the minimum */
    bool group;   /* group holds Group(A) or Group(A) || Perm(r0) */
    bool p1;      /* S6: the P1 search passed to the run holds the trace (best_trace) and the
                     stream of c (best_bytes); best holds its witness t */
    bool labeling; /* S6: lambda holds lambda = rho t */
    int sign;      /* S6 signed: +1 or -1 for a nonzero result, 0 for a certified zero (then best
                      holds the odd automorphism) */
} canon_obj_outcome;

/* Reusable mutable state (one active owner, spec 17).  Buffers persist across runs and grow
 * only.  Invariant: after every run, on every status, no member points into the run's objects
 * or group (the image's borrowed graph tables are cleared, as in src/search/p1_tree.h). */
typedef struct canon_obj_search {
    uint32_t cap;                /* degree the per-point arrays are allocated for */
    uint32_t *best;              /* cap: the witness */
    uint32_t *work;              /* cap: sift residue */
    uint32_t *g;                 /* cap: the transporter of a coset */
    uint32_t *lambda;            /* S6, cap: the labeling lambda = rho t */
    uint32_t *chi;               /* S6, 2 (cap + 2): caller scratch of the character op */
    canon_coset_scratch scratch; /* descents and enumerator (src/coset/coset.h) */
    canon_root_image image;      /* x^r */
    canon_root_image prime;      /* S6: x' = x^rho, the root of the labeling's P1 run */
    canon_buf key;               /* order key of the current leaf */
    canon_buf best_key;          /* least key so far */
    canon_buf bytes;             /* CDAG-2 stream of the minimum image */
    canon_buf group;             /* Group(A) or Group(A) || Perm(r0) */
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

/* spec 3.1, 8.2 (slice S6) "Canonical labeling coset: Run §7 on target coordinates, then
 * complete stabiliser; reconstruct §3.1 and return A lambda": with x' = x^rho and
 * G' = rho^-1 G rho (built by g->ops->conjugate), P1 on D_n gives t in G' and c = x'^t (trace,
 * stream and t in `p1`, t also in s->best); lambda = rho t (rho acts first) goes to s->lambda;
 * the complete stabiliser A of x under G on Omega gives the payload Group(A) || Perm(lambda0)
 * in s->group, lambda0 the least element of A lambda.  rho must be a bijection of
 * {0..n-1} (CANON_INVALID_INPUT otherwise, module contract).  `quota` bounds the P1 NODE tokens
 * plus the enumeration visits (spec 11.1).  Statuses as canon_obj_run. */
canon_status canon_obj_labeling(canon_obj_search *s, canon_p1_search *p1, const canon_group *g,
                                const canon_root *x, const uint32_t *rho, uint64_t quota,
                                canon_obj_outcome *out);

/* spec 8.4, 7.3 (slice S6): the signed canonical image of x under the signed group g
 * (CANON_UNSUPPORTED_ACTION for an unsigned group, module contract).
 *   Fast path (spec 7.3): if every generator fixes x then A = G; the first odd generator (input
 *   order) is the zero certificate; if there is none, A = G is even, P1 gives c = x and t, and
 *   s = +1.
 *   Otherwise (spec 8.4 reference algorithm): the stabiliser enumeration of spec 8.2 evaluates
 *   chi on every hit and stops at the first odd one (the zero certificate, in s->best); on
 *   exhaustion A is complete and chi = +1 is checked on its generators; P1 gives c = x^t and
 *   s = chi(t), so [x] = s [c].
 * A nonzero outcome has the trace, stream and t as for canon_obj_labeling, the sign in
 * out->sign and Group(A) in s->group.  `quota` bounds the enumeration visits plus the P1 NODE
 * tokens (spec 11.1).  Statuses as canon_obj_run. */
canon_status canon_obj_signed(canon_obj_search *s, canon_p1_search *p1, const canon_group *g,
                              const canon_root *x, uint64_t quota, canon_obj_outcome *out);

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

/* spec 17, 3.1 (slice S6) result_verify_witness for a labeling result: *valid = (t, rho and
 * lambda are bijections) and (lambda = rho t) and (rho t rho^-1 in G, i.e. t in
 * G' = rho^-1 G rho and lambda in G rho) and (the stream of x^lambda equals c).  Allocation
 * failure as canon_obj_check_witness. */
canon_status canon_obj_check_labeling(const canon_group *g, const canon_root *x,
                                      const uint32_t *rho, const uint32_t *t,
                                      const uint32_t *lambda, const uint8_t *c, size_t c_len,
                                      bool *valid);

/* spec 17, 8.4 (slice S6) result_verify_witness for a signed result: for sign = 0 (a certified
 * zero) *valid = (w in G) and (x^w = x, by streams) and (chi(w) = -1); for sign = +1 or -1
 * *valid = (w in G) and (x^w = c) and (chi(w) = sign).  g must be signed (else
 * CANON_UNSUPPORTED_ACTION).  Allocation failure as canon_obj_check_witness. */
canon_status canon_obj_check_signed(const canon_group *g, const canon_root *x, const uint32_t *w,
                                    int sign, const uint8_t *c, size_t c_len, bool *valid);

#endif /* CANON_SRC_SEARCH_OBJECTIVES_H */
