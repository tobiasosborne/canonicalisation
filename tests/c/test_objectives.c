/* Tests of the enumeration objectives through the public API (slice S4, docs/slices/S4.md 2,
 * 3.3, 4; spec 3, 3.2, 4.4, 7.4, 8.2, 9.4, 11.1, 17).
 *
 *  - spec 7.4: the CDAG-BYTE-1 minimum of {0} under <[1,0]> is {0} with the identity witness,
 *    while P1 returns {1};
 *  - transporter one: a hit and an exhausted empty result; the transporter accessor;
 *  - stabiliser of a subset and of a graph: Group(A) bytes and flags;
 *  - transporter coset: the spec 7.4 payload, the empty case, the deterministic witness r0;
 *  - SIMPLE-UPPER-1 minimum and its order key;
 *  - the deterministic witness of the canonical image equals the S1 least leaf witness on
 *    every T1 group and subset, under both backends;
 *  - canon_result_verify_witness accepts every engine witness and rejects tampered ones
 *    (non-member, wrong image, non-bijection);
 *  - problem validation (targets, combinations, the spec 4.4 class, options), capacity
 *    (quota and the output bound), results outliving their problem, workspace reuse;
 *  - counters of the stabiliser objective on T1 (printed for docs/slices/S4-notes.md). */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "canon/canon.h"
#include "check.h"
#include "object/object.h"
#include "object/subset.h"
#include "search/objectives.h"
#include "t1_groups.h"

static canon_context *ctx_chain, *ctx_explicit;

static canon_group *group(canon_context *ctx, uint32_t n, const uint32_t *gens, uint32_t count)
{
    canon_group *g = NULL;
    CHECK(canon_group_create(ctx, n, gens, count, &g) == CANON_COMPLETE);
    return g;
}

static canon_object *subset(uint32_t n, const uint32_t *atoms, size_t count)
{
    canon_object *x = NULL;
    CHECK(canon_object_create_subset(ctx_chain, n, atoms, count, &x) == CANON_COMPLETE);
    return x;
}

/* Create a problem and solve it; returns the result (caller releases), *st the solve status. */
static canon_result *solve(const canon_group *g, const canon_object *x, const canon_object *y,
                           canon_objective objective, canon_order order, canon_witness_mode mode,
                           uint64_t max_nodes, canon_status *st)
{
    const canon_profile profile = objective == CANON_OBJECTIVE_CANONICAL_IMAGE
                                      ? CANON_PROFILE_P1
                                      : CANON_PROFILE_NO_TREE;
    canon_capacity cap = {0, 0, max_nodes, 0};
    canon_problem_options opts = {mode};
    canon_problem *p = NULL;
    canon_workspace *ws = NULL;
    canon_result *r = NULL;
    *st = canon_problem_create_with_options(ctx_chain, g, x, y, objective, profile,
                                            CANON_ENCODING_CDAG_2, order, &cap, &opts, &p);
    if (*st == CANON_COMPLETE) {
        CHECK(canon_workspace_create(ctx_chain, &ws) == CANON_COMPLETE);
        *st = canon_solve(ws, p, &r);
    }
    canon_workspace_release(ws);
    canon_problem_release(p); /* the result retains what it needs */
    return r;
}

static int flags_are(canon_result_flags f, bool witness, bool canonical, bool minimum,
                     bool subgroup, bool complete, bool exhausted, bool encoding)
{
    return f.witness_valid == witness && f.image_canonical == canonical &&
           f.minimum_proved == minimum && f.subgroup_verified == subgroup &&
           f.stabiliser_complete == complete && f.transport_exhausted == exhausted &&
           !f.zero_certified && !f.nonzero_certified && f.encoding_complete == encoding;
}

static void verify_ok(const canon_result *r)
{
    bool valid = false;
    CHECK(canon_result_verify_witness(r, &valid) == CANON_COMPLETE && valid);
}

/* ---- spec 7.4 and the objectives on hand-built cases ---- */

static void hand_built(void)
{
    const uint32_t swap[2] = {1, 0}, zero = 0, one = 1;
    canon_group *s2 = group(ctx_chain, 2, swap, 1);
    canon_object *x0 = subset(2, &zero, 1), *x1 = subset(2, &one, 1);
    canon_status st;
    size_t len = 0;
    uint32_t deg = 0;

    /* spec 7.4: "For the second case CDAG-BYTE-1 minimum is {0}, with identity witness,
     * whereas P1 returns {1}" */
    canon_result *r = solve(s2, x0, NULL, CANON_OBJECTIVE_LEX_MIN_IMAGE, CANON_ORDER_CDAG_BYTE_1,
                            CANON_WITNESS_ANY, 0, &st);
    CHECK(st == CANON_COMPLETE && canon_result_status(r) == CANON_COMPLETE);
    CHECK(flags_are(canon_result_get_flags(r), true, false, true, false, false, false, true));
    const uint8_t *b = canon_result_bytes(r, &len);
    CHECK(b != NULL &&
          check_hex_is(b, len, "434e0200010001 00000002 00000002 01 00000000 04 00000001 "
                               "00000000 00000001"));
    const uint32_t *w = canon_result_witness(r, &deg);
    CHECK(w != NULL && deg == 2 && w[0] == 0 && w[1] == 1);
    CHECK(canon_result_trace(r, &len) == NULL && len == 0); /* NO_TREE: no trace */
    CHECK(canon_result_group_bytes(r, &len) == NULL && canon_result_order_key(r, &len) == NULL);
    CHECK(canon_result_transporter(r, &deg) == NULL && deg == 0);
    verify_ok(r);
    canon_result_release(r);
    r = solve(s2, x0, NULL, CANON_OBJECTIVE_CANONICAL_IMAGE, CANON_ORDER_CDAG_BYTE_1,
              CANON_WITNESS_ANY, 0, &st);
    b = canon_result_bytes(r, &len);
    CHECK(b != NULL && check_hex_is(b, len, "434e0200010001 00000002 00000002 01 00000001 04 "
                                            "00000001 00000000 00000001"));
    verify_ok(r);
    canon_result_release(r);

    /* transporter one: {0} -> {1} under Sym(2) is [1,0]; under the trivial group, empty */
    r = solve(s2, x0, x1, CANON_OBJECTIVE_TRANSPORTER_ONE, CANON_ORDER_CDAG_BYTE_1,
              CANON_WITNESS_ANY, 0, &st);
    CHECK(st == CANON_COMPLETE);
    CHECK(flags_are(canon_result_get_flags(r), true, false, false, false, false, false, false));
    w = canon_result_transporter(r, &deg);
    CHECK(w != NULL && deg == 2 && w[0] == 1 && w[1] == 0);
    CHECK(canon_result_bytes(r, &len) == NULL && canon_result_group_bytes(r, &len) == NULL);
    verify_ok(r);
    canon_result_release(r);
    canon_group *triv = group(ctx_chain, 2, NULL, 0);
    r = solve(triv, x0, x1, CANON_OBJECTIVE_TRANSPORTER_ONE, CANON_ORDER_CDAG_BYTE_1,
              CANON_WITNESS_ANY, 0, &st);
    CHECK(st == CANON_COMPLETE); /* spec 3.2: a proved empty transporter is COMPLETE */
    CHECK(flags_are(canon_result_get_flags(r), false, false, false, false, false, true, false));
    CHECK(canon_result_transporter(r, &deg) == NULL && canon_result_witness(r, &deg) == NULL);
    bool valid = true;
    CHECK(canon_result_verify_witness(r, &valid) == CANON_INVALID_INPUT && !valid);
    canon_result_release(r);

    /* stabiliser: of {0} under Sym(2) is 1 (Group(1)); of {} it is Sym(2) */
    r = solve(s2, x0, NULL, CANON_OBJECTIVE_STABILISER, CANON_ORDER_CDAG_BYTE_1,
              CANON_WITNESS_ANY, 0, &st);
    CHECK(flags_are(canon_result_get_flags(r), false, false, false, true, true, false, false));
    b = canon_result_group_bytes(r, &len);
    CHECK(b != NULL && check_hex_is(b, len, "01 00000000"));
    CHECK(canon_result_witness(r, &deg) == NULL);
    canon_result_release(r);
    canon_object *empty = subset(2, NULL, 0);
    r = solve(s2, empty, NULL, CANON_OBJECTIVE_STABILISER, CANON_ORDER_CDAG_BYTE_1,
              CANON_WITNESS_ANY, 0, &st);
    b = canon_result_group_bytes(r, &len);
    CHECK(b != NULL && check_hex_is(b, len, "01 00000001 00000002 00000000 00000001"));
    canon_result_release(r);

    /* transporter coset: spec 7.4 "H=1 and r=[1,0] give 01 00000000 00000002 00000000
     * 00000001 00000001 00000000" */
    for (int mode = 0; mode < 2; ++mode) {
        r = solve(s2, x0, x1, CANON_OBJECTIVE_TRANSPORTER_COSET, CANON_ORDER_CDAG_BYTE_1,
                  (canon_witness_mode)mode, 0, &st);
        CHECK(st == CANON_COMPLETE);
        CHECK(flags_are(canon_result_get_flags(r), true, false, false, true, true, false, false));
        b = canon_result_group_bytes(r, &len);
        CHECK(b != NULL && check_hex_is(b, len, "01 00000000 00000002 00000000 00000001 "
                                                "00000001 00000000"));
        w = canon_result_transporter(r, &deg);
        CHECK(w != NULL && w[0] == 1 && w[1] == 0);
        verify_ok(r);
        canon_result_release(r);
    }
    r = solve(triv, x0, x1, CANON_OBJECTIVE_TRANSPORTER_COSET, CANON_ORDER_CDAG_BYTE_1,
              CANON_WITNESS_ANY, 0, &st);
    CHECK(st == CANON_COMPLETE);
    CHECK(flags_are(canon_result_get_flags(r), false, false, false, false, false, true, false));
    CHECK(canon_result_group_bytes(r, &len) == NULL && len == 0);
    canon_result_release(r);

    canon_object_release(empty);
    canon_object_release(x0);
    canon_object_release(x1);
    canon_group_release(s2);
    canon_group_release(triv);
}

/* a coset with a nontrivial A: x = {0,1} on 4 points under Sym(4), y = {2,3}.  A = Sym{0,1} x
 * Sym{2,3}; the solutions are the g with {g[0], g[1]} = {2,3}; the least is [2,3,0,1]. */
static void coset_deterministic(void)
{
    const uint32_t gens[8] = {1, 0, 2, 3, 1, 2, 3, 0};
    const uint32_t a01[2] = {0, 1}, a23[2] = {2, 3};
    canon_group *s4 = group(ctx_chain, 4, gens, 2);
    canon_object *x = subset(4, a01, 2), *y = subset(4, a23, 2);
    canon_status st;
    canon_result *r = solve(s4, x, y, CANON_OBJECTIVE_TRANSPORTER_COSET, CANON_ORDER_CDAG_BYTE_1,
                            CANON_WITNESS_DETERMINISTIC, 0, &st);
    uint32_t deg = 0;
    const uint32_t *w = canon_result_witness(r, &deg);
    const uint32_t least[4] = {2, 3, 0, 1};
    CHECK(st == CANON_COMPLETE && w != NULL && memcmp(w, least, sizeof least) == 0);
    size_t len = 0;
    const uint8_t *b = canon_result_group_bytes(r, &len);
    /* Group(A): rule 1 (|A| = 4 = 2! 2!), blocks {0,1}, {2,3}; then Perm([2,3,0,1]) */
    CHECK(b != NULL && check_hex_is(b, len, "01 00000002 00000002 00000000 00000001 00000002 "
                                            "00000002 00000003 "
                                            "00000004 00000000 00000002 00000001 00000003 "
                                            "00000002 00000000 00000003 00000001"));
    verify_ok(r);
    canon_result_release(r);
    canon_object_release(x);
    canon_object_release(y);
    canon_group_release(s4);
}

/* ---- SIMPLE-UPPER-1 (spec 4.4) ---- */

static void simple_upper(void)
{
    /* the path 0-1-2 under Sym(3): the least key puts the missing edge first: bits (0,1),
     * (0,2), (1,2) = 0,1,1, key U32(3) || 0x60 */
    const uint32_t gens[6] = {1, 0, 2, 1, 2, 0};
    const uint32_t edges[2][2] = {{0, 1}, {1, 2}};
    canon_group *s3 = group(ctx_chain, 3, gens, 2);
    canon_object *path = NULL;
    CHECK(canon_object_create_simple_graph(ctx_chain, 3, edges, 2, &path) == CANON_COMPLETE);
    canon_status st;
    canon_result *r = solve(s3, path, NULL, CANON_OBJECTIVE_LEX_MIN_IMAGE,
                            CANON_ORDER_SIMPLE_UPPER_1, CANON_WITNESS_DETERMINISTIC, 0, &st);
    CHECK(st == CANON_COMPLETE);
    size_t len = 0;
    const uint8_t *key = canon_result_order_key(r, &len);
    CHECK(key != NULL && check_hex_is(key, len, "00000003 60"));
    /* the selected graph has edges {0,2} and {1,2}; the least g sending the path there maps
     * the middle vertex 1 to 2 and 0 to 0: [0,2,1] */
    uint32_t deg = 0;
    const uint32_t *w = canon_result_witness(r, &deg);
    CHECK(w != NULL && w[0] == 0 && w[1] == 2 && w[2] == 1);
    CHECK(canon_result_bytes(r, &len) != NULL);
    verify_ok(r);
    canon_result_release(r);
    /* outside the class: a subset, or a one-way arc */
    const uint32_t zero = 0;
    canon_object *x = subset(3, &zero, 1);
    canon_problem *p = NULL;
    CHECK(canon_problem_create(ctx_chain, s3, x, CANON_OBJECTIVE_LEX_MIN_IMAGE,
                               CANON_PROFILE_NO_TREE, CANON_ENCODING_CDAG_2,
                               CANON_ORDER_SIMPLE_UPPER_1, NULL, &p) == CANON_UNSUPPORTED_ACTION);
    canon_arc arc = {0, 1, NULL, 0, 1};
    canon_object *directed = NULL;
    CHECK(canon_object_create_graph(ctx_chain, 3, NULL, NULL, &arc, 1, &directed) ==
          CANON_COMPLETE);
    CHECK(canon_problem_create(ctx_chain, s3, directed, CANON_OBJECTIVE_LEX_MIN_IMAGE,
                               CANON_PROFILE_NO_TREE, CANON_ENCODING_CDAG_2,
                               CANON_ORDER_SIMPLE_UPPER_1, NULL, &p) == CANON_UNSUPPORTED_ACTION);
    CHECK(p == NULL);
    canon_object_release(directed);
    canon_object_release(x);
    canon_object_release(path);
    canon_group_release(s3);
}

/* ---- deterministic witness of the canonical image on T1, both backends ---- */

static void deterministic_t1(void)
{
    uint32_t cases = 0;
    canon_context *ctxs[2] = {ctx_chain, ctx_explicit};
    for (uint32_t n = 0; n <= 4; ++n) {
        t1_sym s;
        t1_sym_init(&s, n);
        t1_group groups[T1_MAX_GROUPS];
        const uint32_t ng = t1_subgroups(&s, groups);
        for (uint32_t gi = 0; gi < ng; ++gi) {
            for (int b = 0; b < 2; ++b) {
                canon_group *g = group(ctxs[b], n, groups[gi].gens, groups[gi].gen_count);
                for (uint32_t mask = 0; mask < (1u << n); ++mask) {
                    uint32_t atoms[4], k = 0;
                    for (uint32_t a = 0; a < n; ++a) {
                        if (mask >> a & 1u) {
                            atoms[k++] = a;
                        }
                    }
                    canon_object *x = subset(n, atoms, k);
                    canon_status st1, st2;
                    canon_result *any = solve(g, x, NULL, CANON_OBJECTIVE_CANONICAL_IMAGE,
                                              CANON_ORDER_CDAG_BYTE_1, CANON_WITNESS_ANY, 0, &st1);
                    canon_result *det = solve(g, x, NULL, CANON_OBJECTIVE_CANONICAL_IMAGE,
                                              CANON_ORDER_CDAG_BYTE_1,
                                              CANON_WITNESS_DETERMINISTIC, 0, &st2);
                    CHECK(st1 == CANON_COMPLETE && st2 == CANON_COMPLETE);
                    uint32_t d1 = 0, d2 = 0;
                    const uint32_t *w1 = canon_result_witness(any, &d1);
                    const uint32_t *w2 = canon_result_witness(det, &d2);
                    /* S1 reading 1: the least attaining leaf witness of the unpruned tree is
                     * the least element of A t; the two computations must agree */
                    CHECK(w1 != NULL && w2 != NULL && d1 == d2 &&
                          (n == 0 || memcmp(w1, w2, n * sizeof *w1) == 0));
                    size_t l1 = 0, l2 = 0;
                    const uint8_t *b1 = canon_result_bytes(any, &l1);
                    const uint8_t *b2 = canon_result_bytes(det, &l2);
                    CHECK(l1 == l2 && memcmp(b1, b2, l1) == 0);
                    verify_ok(det);
                    canon_result_release(any);
                    canon_result_release(det);
                    canon_object_release(x);
                    ++cases;
                }
                canon_group_release(g);
            }
        }
    }
    CHECK(cases == 2u * 539u);
}

/* ---- verify_witness rejects tampered witnesses ---- */

static void tampered(void)
{
    /* G = <(0 1)> on 3 points, x = {0}: G = {id, [1,0,2]} with images {0} and {1} */
    const uint32_t swap01[3] = {1, 0, 2}, zero = 0;
    canon_group *g = group(ctx_chain, 3, swap01, 1);
    canon_object *x = subset(3, &zero, 1);
    canon_status st;
    canon_result *r = solve(g, x, NULL, CANON_OBJECTIVE_CANONICAL_IMAGE, CANON_ORDER_CDAG_BYTE_1,
                            CANON_WITNESS_ANY, 0, &st);
    verify_ok(r);
    size_t len = 0;
    const uint8_t *c = canon_result_bytes(r, &len);
    uint32_t deg = 0;
    const uint32_t *w = canon_result_witness(r, &deg);
    /* the internal check on an independently built root for x */
    canon_root xr;
    memset(&xr, 0, sizeof xr);
    xr.kind = CANON_ROOT_SUBSET;
    xr.n = 3;
    CHECK(canon_subset_init(&xr.u.subset, 3, &zero, 1) == CANON_COMPLETE);
    bool valid = false;
    CHECK(canon_obj_check_witness(g, &xr, w, c, len, &valid) == CANON_COMPLETE && valid);
    /* the other element of G is a member with the other image */
    const uint32_t id3[3] = {0, 1, 2};
    const uint32_t *other = w[0] == 0 ? swap01 : id3;
    CHECK(canon_obj_check_witness(g, &xr, other, c, len, &valid) == CANON_COMPLETE && !valid);
    /* (0 2) is not in G (spec 17: membership is checked, not only the action) */
    const uint32_t swap02[3] = {2, 1, 0};
    CHECK(canon_obj_check_witness(g, &xr, swap02, c, len, &valid) == CANON_COMPLETE && !valid);
    /* not a permutation */
    const uint32_t bad[3] = {0, 0, 2};
    CHECK(canon_obj_check_witness(g, &xr, bad, c, len, &valid) == CANON_COMPLETE && !valid);
    /* through the public API: the witness array is the result's own (heap, non-const)
     * storage, so tampering with it in place is defined; verify_witness must notice */
    uint32_t *mut = (uint32_t *)(uintptr_t)w;
    const uint32_t saved[3] = {w[0], w[1], w[2]};
    memcpy(mut, other, sizeof saved);
    CHECK(canon_result_verify_witness(r, &valid) == CANON_COMPLETE && !valid);
    memcpy(mut, swap02, sizeof saved);
    CHECK(canon_result_verify_witness(r, &valid) == CANON_COMPLETE && !valid);
    memcpy(mut, saved, sizeof saved);
    verify_ok(r);
    CHECK(canon_result_verify_witness(NULL, &valid) == CANON_INVALID_INPUT && !valid);
    CHECK(canon_result_verify_witness(r, NULL) == CANON_INVALID_INPUT);
    canon_subset_free(&xr.u.subset);
    canon_result_release(r);
    canon_object_release(x);
    canon_group_release(g);
}

/* ---- problem validation (canon_problem_create_with_options) ---- */

static void validation(void)
{
    const uint32_t swap[2] = {1, 0}, zero = 0;
    canon_group *g = group(ctx_chain, 2, swap, 1);
    canon_group *g3 = group(ctx_chain, 3, NULL, 0);
    canon_object *x = subset(2, &zero, 1), *x3 = subset(3, &zero, 1);
    canon_object *graph = NULL;
    CHECK(canon_object_create_graph(ctx_chain, 2, NULL, NULL, NULL, 0, &graph) == CANON_COMPLETE);
    static int sentinel;
    canon_problem *p = (canon_problem *)(void *)&sentinel; /* must become NULL */
    const canon_problem_options det = {CANON_WITNESS_DETERMINISTIC}, bad = {(canon_witness_mode)7};
#define CREATE(group_, x_, y_, obj_, prof_, ord_, opts_)                                           \
    canon_problem_create_with_options(ctx_chain, group_, x_, y_, obj_, prof_,                      \
                                      CANON_ENCODING_CDAG_2, ord_, NULL, opts_, &p)
    const canon_profile NT = CANON_PROFILE_NO_TREE, P1 = CANON_PROFILE_P1;
    const canon_order CB = CANON_ORDER_CDAG_BYTE_1, SU = CANON_ORDER_SIMPLE_UPPER_1;
    /* NULL arguments and an unknown witness mode */
    CHECK(CREATE(NULL, x, NULL, CANON_OBJECTIVE_STABILISER, NT, CB, NULL) == CANON_INVALID_INPUT);
    CHECK(p == NULL);
    CHECK(CREATE(g, x, NULL, CANON_OBJECTIVE_STABILISER, NT, CB, &bad) == CANON_INVALID_INPUT);
    /* unsupported combinations (spec 4.3: NO_TREE for coset-enumeration objectives) */
    CHECK(CREATE(g, x, NULL, CANON_OBJECTIVE_LEX_MIN_IMAGE, P1, CB, NULL) ==
          CANON_UNSUPPORTED_ACTION);
    CHECK(CREATE(g, x, NULL, CANON_OBJECTIVE_CANONICAL_IMAGE, NT, CB, NULL) ==
          CANON_UNSUPPORTED_ACTION);
    CHECK(CREATE(g, x, NULL, CANON_OBJECTIVE_STABILISER, NT, SU, NULL) ==
          CANON_UNSUPPORTED_ACTION);
    CHECK(CREATE(g, x, NULL, CANON_OBJECTIVE_STABILISER, NT, CB, &det) ==
          CANON_UNSUPPORTED_ACTION);
    CHECK(CREATE(g, x, x, CANON_OBJECTIVE_TRANSPORTER_ONE, NT, CB, &det) ==
          CANON_UNSUPPORTED_ACTION);
    CHECK(CREATE(g, x, NULL, CANON_OBJECTIVE_CANONICAL_LABELING_COSET, NT, CB, NULL) ==
          CANON_UNSUPPORTED_ACTION);
    CHECK(CREATE(g, x, NULL, CANON_OBJECTIVE_CONSTRAINT_ONE, NT, CB, NULL) ==
          CANON_UNSUPPORTED_ACTION);
    CHECK(canon_problem_create_with_options(ctx_chain, g, x, NULL, CANON_OBJECTIVE_STABILISER,
                                            NT, 1, CB, NULL, NULL, &p) ==
          CANON_UNSUPPORTED_ACTION); /* encoding 1 is not CDAG-2 */
    /* targets: required for the transporters, refused otherwise, same kind and degree */
    CHECK(CREATE(g, x, NULL, CANON_OBJECTIVE_TRANSPORTER_ONE, NT, CB, NULL) ==
          CANON_INVALID_INPUT);
    CHECK(CREATE(g, x, NULL, CANON_OBJECTIVE_TRANSPORTER_COSET, NT, CB, NULL) ==
          CANON_INVALID_INPUT);
    CHECK(CREATE(g, x, x, CANON_OBJECTIVE_STABILISER, NT, CB, NULL) == CANON_INVALID_INPUT);
    CHECK(CREATE(g, x, x, CANON_OBJECTIVE_CANONICAL_IMAGE, P1, CB, NULL) == CANON_INVALID_INPUT);
    CHECK(CREATE(g, x, graph, CANON_OBJECTIVE_TRANSPORTER_ONE, NT, CB, NULL) ==
          CANON_INVALID_INPUT);
    CHECK(CREATE(g, x, x3, CANON_OBJECTIVE_TRANSPORTER_ONE, NT, CB, NULL) == CANON_INVALID_INPUT);
    CHECK(CREATE(g3, x, NULL, CANON_OBJECTIVE_STABILISER, NT, CB, NULL) == CANON_INVALID_INPUT);
    CHECK(p == NULL);
    /* spec 4.4 class: an arc-free uncoloured graph is in it */
    CHECK(CREATE(g, graph, NULL, CANON_OBJECTIVE_LEX_MIN_IMAGE, NT, SU, &det) == CANON_COMPLETE);
    canon_problem_release(p);
    /* capacity: the Group(A) bound for |G| = 2, n = 2 is 5 + 1 * (4 + 16) = 25 bytes */
    canon_capacity cap = {0, 0, 0, 24};
    CHECK(canon_problem_create_with_options(ctx_chain, g, x, NULL, CANON_OBJECTIVE_STABILISER,
                                            NT, CANON_ENCODING_CDAG_2, CB, &cap, NULL, &p) ==
          CANON_CAPACITY_LIMIT);
    cap.max_output_bytes = 25;
    CHECK(canon_problem_create_with_options(ctx_chain, g, x, NULL, CANON_OBJECTIVE_STABILISER,
                                            NT, CANON_ENCODING_CDAG_2, CB, &cap, NULL, &p) ==
          CANON_COMPLETE);
    canon_problem_release(p);
    /* the coset adds Perm(r0) <= 4 + 16 bytes; the transporter has no byte output */
    cap.max_output_bytes = 44;
    CHECK(canon_problem_create_with_options(ctx_chain, g, x, x, CANON_OBJECTIVE_TRANSPORTER_COSET,
                                            NT, CANON_ENCODING_CDAG_2, CB, &cap, NULL, &p) ==
          CANON_CAPACITY_LIMIT);
    cap.max_output_bytes = 1;
    CHECK(canon_problem_create_with_options(ctx_chain, g, x, x, CANON_OBJECTIVE_TRANSPORTER_ONE,
                                            NT, CANON_ENCODING_CDAG_2, CB, &cap, NULL, &p) ==
          CANON_COMPLETE);
    canon_problem_release(p);
    /* the minimum's stream is 33 bytes for a one-atom subset on degree 2 */
    cap.max_output_bytes = 32;
    CHECK(canon_problem_create_with_options(ctx_chain, g, x, NULL, CANON_OBJECTIVE_LEX_MIN_IMAGE,
                                            NT, CANON_ENCODING_CDAG_2, CB, &cap, NULL, &p) ==
          CANON_CAPACITY_LIMIT);
#undef CREATE
    canon_object_release(graph);
    canon_object_release(x);
    canon_object_release(x3);
    canon_group_release(g);
    canon_group_release(g3);
}

/* ---- quota, lifetimes and workspace reuse ---- */

static void lifetimes(void)
{
    /* spec 11.1: the empty subset under Sym(2): 3 visits (root and two leaves) */
    const uint32_t swap[2] = {1, 0};
    canon_group *g = group(ctx_chain, 2, swap, 1);
    canon_object *x = subset(2, NULL, 0);
    canon_status st;
    canon_result *r = solve(g, x, NULL, CANON_OBJECTIVE_STABILISER, CANON_ORDER_CDAG_BYTE_1,
                            CANON_WITNESS_ANY, 2, &st);
    CHECK(st == CANON_CAPACITY_LIMIT && canon_result_status(r) == CANON_CAPACITY_LIMIT);
    CHECK(flags_are(canon_result_get_flags(r), false, false, false, false, false, false, false));
    size_t len = 0;
    CHECK(canon_result_group_bytes(r, &len) == NULL && len == 0);
    canon_result_release(r);
    r = solve(g, x, NULL, CANON_OBJECTIVE_STABILISER, CANON_ORDER_CDAG_BYTE_1, CANON_WITNESS_ANY,
              3, &st);
    CHECK(st == CANON_COMPLETE);
    /* the result outlives its problem (released in solve), group and object */
    canon_group_release(g);
    canon_object_release(x);
    CHECK(canon_result_group_bytes(r, &len) != NULL);
    canon_result_release(r);

    /* one workspace across objectives, kinds and degrees; each object is released before the
     * workspace is reused (no borrowed pointer survives a run); results compared with a fresh
     * workspace */
    canon_workspace *ws = NULL;
    CHECK(canon_workspace_create(ctx_chain, &ws) == CANON_COMPLETE);
    const uint32_t s4[8] = {1, 0, 2, 3, 1, 2, 3, 0};
    for (int round = 0; round < 6; ++round) {
        const uint32_t n = round % 2 == 0 ? 4u : 2u;
        canon_group *gg = group(ctx_chain, n, n == 4 ? s4 : swap, n == 4 ? 2u : 1u);
        static const uint8_t lab[1] = {'a'};
        canon_arc arcs[2] = {{0, 1, lab, 1, 2}, {1, 0, NULL, 0, 1}};
        static const uint8_t c0[1] = {'c'};
        const uint8_t *colours[4] = {c0, NULL, NULL, NULL};
        const size_t lengths[4] = {1, 0, 0, 0};
        canon_object *gx = NULL;
        CHECK(canon_object_create_graph(ctx_chain, n, colours, lengths, arcs, 2, &gx) ==
              CANON_COMPLETE);
        const canon_objective objs[3] = {CANON_OBJECTIVE_LEX_MIN_IMAGE,
                                         CANON_OBJECTIVE_STABILISER,
                                         CANON_OBJECTIVE_TRANSPORTER_COSET};
        const canon_objective obj = objs[round % 3];
        const bool target = obj == CANON_OBJECTIVE_TRANSPORTER_COSET;
        canon_problem *p = NULL;
        CHECK(canon_problem_create_with_options(ctx_chain, gg, gx, target ? gx : NULL, obj,
                                                CANON_PROFILE_NO_TREE, CANON_ENCODING_CDAG_2,
                                                CANON_ORDER_CDAG_BYTE_1, NULL, NULL, &p) ==
              CANON_COMPLETE);
        canon_object_release(gx); /* the problem holds it */
        canon_result *a = NULL, *b = NULL;
        canon_workspace *fresh = NULL;
        CHECK(canon_workspace_create(ctx_chain, &fresh) == CANON_COMPLETE);
        CHECK(canon_solve(ws, p, &a) == CANON_COMPLETE);
        CHECK(canon_solve(fresh, p, &b) == CANON_COMPLETE);
        canon_problem_release(p); /* releases the object: the workspace must not refer to it */
        size_t la = 0, lb = 0;
        const uint8_t *ba = canon_result_bytes(a, &la), *bb = canon_result_bytes(b, &lb);
        CHECK(la == lb && (la == 0 || memcmp(ba, bb, la) == 0));
        ba = canon_result_group_bytes(a, &la);
        bb = canon_result_group_bytes(b, &lb);
        CHECK(la == lb && (la == 0 || memcmp(ba, bb, la) == 0));
        CHECK(canon_result_get_flags(a).encoding_complete ==
              canon_result_get_flags(b).encoding_complete);
        if (canon_result_witness(a, NULL) != NULL) {
            verify_ok(a); /* the result retains the object */
        }
        canon_result_release(a);
        canon_result_release(b);
        canon_workspace_release(fresh);
        canon_group_release(gg);
    }
    canon_workspace_release(ws);
}

/* ---- counters for the notes: the stabiliser objective over T1 (internal run) ---- */

static void counters(void)
{
    canon_obj_stats total;
    memset(&total, 0, sizeof total);
    uint64_t runs = 0;
    canon_obj_search s;
    canon_obj_search_init(&s);
    for (uint32_t n = 0; n <= 4; ++n) {
        t1_sym sym;
        t1_sym_init(&sym, n);
        t1_group groups[T1_MAX_GROUPS];
        const uint32_t ng = t1_subgroups(&sym, groups);
        for (uint32_t gi = 0; gi < ng; ++gi) {
            canon_group *g = group(ctx_chain, n, groups[gi].gens, groups[gi].gen_count);
            for (uint32_t mask = 0; mask < (1u << n); ++mask) {
                canon_root xr;
                memset(&xr, 0, sizeof xr);
                xr.kind = CANON_ROOT_SUBSET;
                xr.n = n;
                uint32_t atoms[4], k = 0;
                for (uint32_t a = 0; a < n; ++a) {
                    if (mask >> a & 1u) {
                        atoms[k++] = a;
                    }
                }
                CHECK(canon_subset_init(&xr.u.subset, n, atoms, k) == CANON_COMPLETE);
                canon_obj_outcome o;
                CHECK(canon_obj_run(&s, g, &xr, NULL, CANON_OBJECTIVE_STABILISER,
                                    CANON_ORDER_CDAG_BYTE_1, false, UINT64_MAX,
                                    &o) == CANON_COMPLETE);
                CHECK(o.group && o.flags.stabiliser_complete && s.stats.leaves == groups[gi].order);
                /* at most log2 |A| verified rebuilds (each insertion doubles |A_known|) */
                CHECK(((uint64_t)1 << s.stats.stab_builds) <= s.stats.hits);
                total.nodes += s.stats.nodes;
                total.leaves += s.stats.leaves;
                total.hits += s.stats.hits;
                total.stab_builds += s.stats.stab_builds;
                total.coset.rebuilds += s.stats.coset.rebuilds;
                total.coset.descents += s.stats.coset.descents;
                total.group.k_builds += s.stats.group.k_builds;
                total.group.coset.rebuilds += s.stats.group.coset.rebuilds;
                ++runs;
                canon_subset_free(&xr.u.subset);
            }
            canon_group_release(g);
        }
    }
    canon_obj_search_free(&s);
    CHECK(runs == 539);
    printf("T1 stabiliser objective (539 runs): visits %llu, leaves %llu, hits %llu, "
           "stabiliser rebuilds %llu, enumeration rebuilds %llu, descents %llu, "
           "Group(A) rule-2 K rebuilds %llu, Group(A) descent rebuilds %llu\n",
           (unsigned long long)total.nodes, (unsigned long long)total.leaves,
           (unsigned long long)total.hits, (unsigned long long)total.stab_builds,
           (unsigned long long)total.coset.rebuilds, (unsigned long long)total.coset.descents,
           (unsigned long long)total.group.k_builds,
           (unsigned long long)total.group.coset.rebuilds);
}

int main(void)
{
    const canon_context_options co = {CANON_BACKEND_CHAIN}, eo = {CANON_BACKEND_EXPLICIT};
    CHECK(canon_context_create_with_options(NULL, &co, &ctx_chain) == CANON_COMPLETE);
    CHECK(canon_context_create_with_options(NULL, &eo, &ctx_explicit) == CANON_COMPLETE);
    hand_built();
    coset_deterministic();
    simple_upper();
    deterministic_t1();
    tampered();
    validation();
    lifetimes();
    counters();
    canon_context_release(ctx_chain);
    canon_context_release(ctx_explicit);
    return check_finish("test_objectives");
}
