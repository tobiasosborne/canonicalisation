/* End-to-end C test of the slice S2 graph path through the public API (spec sections 3.2, 4.1,
 * 7.1, 7.2, 7.4, 11.1, 17): the spec 7.4 one-arc case (trace, bytes, witness [1,0], output
 * arc 1 -> 0), capacity limits, a graph fixed by its group, equivariance and witness validity
 * on random graphs, invalid input, the simple-graph wrapper, and workspace reuse across object
 * kinds.  Expected hex lives here, never in src/. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "bsgs/group.h"
#include "canon/canon.h"
#include "check.h"
#include "encoding/graph_stream.h"
#include "object/graph.h"
#include "object/object.h"
#include "search/p1_tree.h"

static const char *TRACE_74 =
    "10 00000000 20 00000002 00000001 00000001 21 00000002 00000001 00000001 "
    "20 00000002 00000001 00000001 21 00000002 00000001 00000001 00";
static const char *BYTES_74 = "434e0200010001 00000002 00000001 09 00000000 00000000 00000001 "
                              "00000001 00000000 00000000 00000001 01 00000000";

typedef struct graph_case {
    uint32_t n;
    const uint32_t *gens;
    size_t gen_count;
    const uint8_t *const *colours; /* NULL = all empty */
    const size_t *lengths;
    const canon_arc *arcs;
    size_t arc_count;
} graph_case;

static canon_status make_graph(canon_context *ctx, const graph_case *c, canon_object **x)
{
    const uint8_t *empty[8] = {NULL};
    const size_t zero[8] = {0};
    return canon_object_create_graph(ctx, c->n, c->colours != NULL ? c->colours : empty,
                                     c->colours != NULL ? c->lengths : zero, c->arcs,
                                     c->arc_count, x);
}

static canon_status solve_graph(canon_context *ctx, canon_workspace *ws, const graph_case *c,
                                const canon_capacity *cap, canon_result **out)
{
    canon_group *g = NULL;
    canon_object *x = NULL;
    canon_problem *p = NULL;
    *out = NULL;
    canon_status st = canon_group_create(ctx, c->n, c->gens, c->gen_count, &g);
    if (st == CANON_COMPLETE) {
        st = make_graph(ctx, c, &x);
    }
    if (st == CANON_COMPLETE) {
        st = canon_problem_create(ctx, g, x, CANON_OBJECTIVE_CANONICAL_IMAGE, CANON_PROFILE_P1,
                                  CANON_ENCODING_CDAG_2, CANON_ORDER_CDAG_BYTE_1, cap, &p);
    }
    canon_group_release(g);
    canon_object_release(x);
    if (st == CANON_COMPLETE) {
        st = canon_solve(ws, p, out);
    }
    canon_problem_release(p);
    return st;
}

static int same_result(const canon_result *a, const canon_result *b)
{
    size_t la = 0, lb = 0, ba = 0, bb = 0;
    const uint8_t *ta = canon_result_trace(a, &la), *tb = canon_result_trace(b, &lb);
    const uint8_t *ya = canon_result_bytes(a, &ba), *yb = canon_result_bytes(b, &bb);
    uint32_t da = 0, db = 0;
    const uint32_t *wa = canon_result_witness(a, &da), *wb = canon_result_witness(b, &db);
    return ta != NULL && tb != NULL && la == lb && memcmp(ta, tb, la) == 0 && ya != NULL &&
           yb != NULL && ba == bb && memcmp(ya, yb, ba) == 0 && wa != NULL && wb != NULL &&
           da == db && (da == 0 || memcmp(wa, wb, da * sizeof *wa) == 0);
}

static void test_golden(canon_context *ctx, canon_workspace *ws)
{
    const uint32_t swap[2] = {1, 0};
    const canon_arc arc01[1] = {{0, 1, NULL, 0, 1}};
    graph_case c = {2, swap, 1, NULL, NULL, arc01, 1};
    canon_result *r = NULL;
    CHECK(solve_graph(ctx, ws, &c, NULL, &r) == CANON_COMPLETE);
    canon_result_flags f = canon_result_get_flags(r);
    CHECK(f.witness_valid && f.image_canonical && f.encoding_complete);
    CHECK(!f.minimum_proved && !f.subgroup_verified && !f.stabiliser_complete &&
          !f.transport_exhausted && !f.zero_certified && !f.nonzero_certified);
    size_t tl = 0, bl = 0;
    const uint8_t *t = canon_result_trace(r, &tl), *b = canon_result_bytes(r, &bl);
    CHECK(t != NULL && check_hex_is(t, tl, TRACE_74));
    CHECK(b != NULL && check_hex_is(b, bl, BYTES_74));
    /* spec 7.4: "Witness [1,0], output arc 1->0": the arc's U32 source and U32 target start at
     * offset 28 (header 15 bytes, tag 1, two empty colours 8, U32(e) 4). */
    CHECK(b != NULL && bl == 49 && b[31] == 1 && b[35] == 0);
    uint32_t deg = 0;
    const uint32_t *w = canon_result_witness(r, &deg);
    CHECK(w != NULL && deg == 2 && w[0] == 1 && w[1] == 0);
    canon_result_release(r);
}

static void test_capacity(canon_context *ctx, canon_workspace *ws)
{
    const uint32_t swap[2] = {1, 0};
    /* spec 11.1 logical work quota: the arc-free graph on 2 vertices under Sym(2) has three
     * NODE tokens (root plus two children), as for the empty subset. */
    graph_case c = {2, swap, 1, NULL, NULL, NULL, 0};
    canon_capacity cap = {0, 0, 2, 0};
    canon_result *r = NULL;
    CHECK(solve_graph(ctx, ws, &c, &cap, &r) == CANON_CAPACITY_LIMIT);
    CHECK(r != NULL && canon_result_status(r) == CANON_CAPACITY_LIMIT);
    size_t len = 7;
    uint32_t deg = 7;
    CHECK(canon_result_trace(r, &len) == NULL && len == 0);
    CHECK(canon_result_bytes(r, &len) == NULL && len == 0);
    CHECK(canon_result_witness(r, &deg) == NULL && deg == 0);
    CHECK(!canon_result_get_flags(r).image_canonical);
    canon_result_release(r);
    cap.max_search_nodes = 3;
    CHECK(solve_graph(ctx, ws, &c, &cap, &r) == CANON_COMPLETE);
    canon_result_release(r);
    /* spec 11.1 output bytes: the exact graph stream length, checked at problem time.  The
     * spec 7.4 stream is 49 bytes. */
    const canon_arc arc01[1] = {{0, 1, NULL, 0, 1}};
    c.arcs = arc01;
    c.arc_count = 1;
    cap = (canon_capacity){0, 0, 0, 48};
    CHECK(solve_graph(ctx, ws, &c, &cap, &r) == CANON_CAPACITY_LIMIT && r == NULL);
    cap.max_output_bytes = 49;
    CHECK(solve_graph(ctx, ws, &c, &cap, &r) == CANON_COMPLETE);
    canon_result_release(r);
    /* The size is the same for every image: a heavier Nat (300 needs two bytes) still gives
     * an exact bound (50 bytes). */
    const canon_arc heavy[1] = {{0, 1, NULL, 0, 300}};
    c.arcs = heavy;
    cap.max_output_bytes = 49;
    CHECK(solve_graph(ctx, ws, &c, &cap, &r) == CANON_CAPACITY_LIMIT);
    cap.max_output_bytes = 50;
    CHECK(solve_graph(ctx, ws, &c, &cap, &r) == CANON_COMPLETE);
    size_t bl = 0;
    CHECK(canon_result_bytes(r, &bl) != NULL && bl == 50);
    canon_result_release(r);
}

/* Stream of x^w built independently through the internal graph API. */
static int image_bytes_match(const graph_case *c, const uint32_t *w, const uint8_t *bytes,
                             size_t len)
{
    canon_graph g, img;
    canon_buf b;
    canon_buf_init(&b);
    canon_graph_init_empty(&img);
    int ok = canon_graph_init(&g, c->n, c->colours, c->lengths, c->arcs, c->arc_count) ==
                 CANON_COMPLETE &&
             canon_graph_act_into(&g, w, &img) == CANON_COMPLETE &&
             canon_graph_stream_write(&b, &img) == CANON_COMPLETE && b.len == len &&
             memcmp(b.data, bytes, len) == 0;
    canon_buf_free(&b);
    canon_graph_free(&img);
    canon_graph_free(&g);
    return ok;
}

static void test_fixed_and_equivariant(canon_context *ctx, canon_workspace *ws)
{
    /* A directed 3-cycle is fixed by C3 = <[1,2,0]>: the orbit is a singleton, so the
     * canonical image is x itself, and every leaf attains it; the least witness is the
     * identity (spec 7.4 preamble, S1 notes reading 1). */
    const uint32_t c3[3] = {1, 2, 0};
    const canon_arc cycle[3] = {{0, 1, NULL, 0, 1}, {1, 2, NULL, 0, 1}, {2, 0, NULL, 0, 1}};
    graph_case c = {3, c3, 1, NULL, NULL, cycle, 3};
    canon_result *r = NULL, *r_trivial = NULL;
    CHECK(solve_graph(ctx, ws, &c, NULL, &r) == CANON_COMPLETE);
    graph_case trivial = c;
    trivial.gens = NULL;
    trivial.gen_count = 0;
    CHECK(solve_graph(ctx, ws, &trivial, NULL, &r_trivial) == CANON_COMPLETE);
    size_t bl = 0, tl = 0;
    const uint8_t *b = canon_result_bytes(r, &bl), *bt = canon_result_bytes(r_trivial, &tl);
    CHECK(b != NULL && bt != NULL && bl == tl && memcmp(b, bt, bl) == 0);
    uint32_t deg = 0;
    const uint32_t *w = canon_result_witness(r, &deg);
    CHECK(w != NULL && deg == 3 && w[0] == 0 && w[1] == 1 && w[2] == 2);
    canon_result_release(r);
    canon_result_release(r_trivial);

    /* Random coloured, labelled multigraphs under Sym(3) and C3: x and x^h (h in G) give the
     * same trace and bytes (spec 7.2), and the witness maps x to the returned bytes. */
    const uint32_t sym3[6] = {1, 0, 2, 1, 2, 0};
    const uint32_t c3elems[3][3] = {{0, 1, 2}, {1, 2, 0}, {2, 0, 1}};
    const uint8_t *labels[2] = {(const uint8_t *)"", (const uint8_t *)"a"};
    for (int round = 0; round < 60; ++round) {
        canon_arc arcs[8], moved[8];
        size_t count = (size_t)(check_rng() % 8);
        for (size_t i = 0; i < count; ++i) {
            size_t l = (size_t)(check_rng() % 2);
            arcs[i] = (canon_arc){(uint32_t)(check_rng() % 3), (uint32_t)(check_rng() % 3),
                                  labels[l], l, 1 + check_rng() % 2};
        }
        const uint8_t *colours[3], *mcolours[3];
        size_t lengths[3], mlengths[3];
        for (uint32_t v = 0; v < 3; ++v) {
            size_t l = (size_t)(check_rng() % 2);
            colours[v] = labels[l];
            lengths[v] = l;
        }
        int use_c3 = round % 2;
        const uint32_t *h = c3elems[check_rng() % 3]; /* in both groups */
        for (size_t i = 0; i < count; ++i) {
            moved[i] = arcs[i];
            moved[i].source = h[arcs[i].source];
            moved[i].target = h[arcs[i].target];
        }
        for (uint32_t v = 0; v < 3; ++v) {
            mcolours[h[v]] = colours[v];
            mlengths[h[v]] = lengths[v];
        }
        graph_case a = {3, use_c3 ? c3 : sym3, use_c3 ? 1u : 2u, colours, lengths, arcs, count};
        graph_case m = a;
        m.colours = mcolours;
        m.lengths = mlengths;
        m.arcs = moved;
        canon_result *ra = NULL, *rm = NULL;
        CHECK(solve_graph(ctx, ws, &a, NULL, &ra) == CANON_COMPLETE);
        CHECK(solve_graph(ctx, ws, &m, NULL, &rm) == CANON_COMPLETE);
        size_t la = 0, lm = 0, ya = 0, ym = 0;
        const uint8_t *ta = canon_result_trace(ra, &la), *tm = canon_result_trace(rm, &lm);
        const uint8_t *ba = canon_result_bytes(ra, &ya), *bm = canon_result_bytes(rm, &ym);
        CHECK(ta != NULL && tm != NULL && la == lm && memcmp(ta, tm, la) == 0);
        CHECK(ba != NULL && bm != NULL && ya == ym && memcmp(ba, bm, ya) == 0);
        const uint32_t *wa = canon_result_witness(ra, &deg);
        CHECK(wa != NULL && deg == 3 && image_bytes_match(&a, wa, ba, ya));
        const uint32_t *wm = canon_result_witness(rm, &deg);
        CHECK(wm != NULL && deg == 3 && image_bytes_match(&m, wm, bm, ym));
        canon_result_release(ra);
        canon_result_release(rm);
    }
}

static void test_invalid(canon_context *ctx)
{
    canon_object *x = (canon_object *)ctx;
    const uint8_t *colours[2] = {NULL, NULL};
    const size_t lengths[2] = {0, 0};
    const canon_arc zero[1] = {{0, 1, NULL, 0, 0}};
    const canon_arc range[1] = {{0, 2, NULL, 0, 1}};
    const canon_arc huge[2] = {{0, 1, NULL, 0, UINT64_MAX}, {1, 0, NULL, 0, 1}};
    CHECK(canon_object_create_graph(ctx, 2, colours, lengths, zero, 1, &x) ==
              CANON_INVALID_INPUT &&
          x == NULL);
    CHECK(canon_object_create_graph(ctx, 2, colours, lengths, range, 1, &x) ==
          CANON_INVALID_INPUT);
    /* Review item 5: both colour arrays NULL means every colour is empty, for any degree; one
     * without the other is invalid. */
    CHECK(canon_object_create_graph(ctx, 2, colours, NULL, NULL, 0, &x) == CANON_INVALID_INPUT);
    CHECK(canon_object_create_graph(ctx, 2, NULL, lengths, NULL, 0, &x) == CANON_INVALID_INPUT);
    CHECK(canon_object_create_graph(ctx, 2, NULL, NULL, NULL, 0, &x) == CANON_COMPLETE);
    canon_object_release(x);
    x = NULL;
    CHECK(canon_object_create_graph(ctx, 2, colours, lengths, NULL, 1, &x) ==
          CANON_INVALID_INPUT);
    CHECK(canon_object_create_graph(NULL, 0, NULL, NULL, NULL, 0, &x) == CANON_INVALID_INPUT);
    CHECK(canon_object_create_graph(ctx, 0, NULL, NULL, NULL, 0, NULL) == CANON_INVALID_INPUT);
    CHECK(canon_object_create_graph(ctx, 2, colours, lengths, huge, 2, &x) ==
          CANON_CAPACITY_LIMIT);
    CHECK(canon_object_create_graph(ctx, 0, NULL, NULL, NULL, 0, &x) == CANON_COMPLETE);
    canon_object_release(x);
    /* Context degree limit, checked before the data. */
    canon_context *small = NULL;
    canon_capacity d = {1, 0, 0, 0};
    CHECK(canon_context_create(&d, &small) == CANON_COMPLETE);
    CHECK(canon_object_create_graph(small, 2, colours, lengths, zero, 1, &x) ==
          CANON_CAPACITY_LIMIT);
    const uint32_t loop[1][2] = {{0, 0}};
    CHECK(canon_object_create_simple_graph(small, 2, loop, 1, &x) == CANON_CAPACITY_LIMIT);
    canon_context_release(small);
    /* spec 4.1: the simple wrapper rejects loops. */
    CHECK(canon_object_create_simple_graph(ctx, 2, loop, 1, &x) == CANON_INVALID_INPUT);
    CHECK(x == NULL);
    CHECK(canon_object_create_simple_graph(ctx, 2, NULL, 1, &x) == CANON_INVALID_INPUT);
    /* Degree mismatch and unsupported order with a graph object. */
    canon_group *g = NULL;
    canon_problem *p = (canon_problem *)ctx;
    CHECK(canon_group_create(ctx, 3, NULL, 0, &g) == CANON_COMPLETE);
    CHECK(canon_object_create_graph(ctx, 2, colours, lengths, NULL, 0, &x) == CANON_COMPLETE);
    CHECK(canon_problem_create(ctx, g, x, CANON_OBJECTIVE_CANONICAL_IMAGE, CANON_PROFILE_P1,
                               CANON_ENCODING_CDAG_2, CANON_ORDER_CDAG_BYTE_1, NULL,
                               &p) == CANON_INVALID_INPUT &&
          p == NULL);
    canon_group_release(g);
    CHECK(canon_group_create(ctx, 2, NULL, 0, &g) == CANON_COMPLETE);
    CHECK(canon_problem_create(ctx, g, x, CANON_OBJECTIVE_CANONICAL_IMAGE, CANON_PROFILE_P1,
                               CANON_ENCODING_CDAG_2, CANON_ORDER_SIMPLE_UPPER_1, NULL,
                               &p) == CANON_UNSUPPORTED_ACTION);
    canon_group_release(g);
    canon_object_release(x);
}

static void test_simple_and_reuse(canon_context *ctx, canon_workspace *ws)
{
    /* The wrapper's path 0-1-2 equals the explicit two-arcs-per-edge graph (spec 4.1). */
    const uint32_t sym3[6] = {1, 0, 2, 1, 2, 0};
    const uint32_t path[3][2] = {{1, 0}, {2, 1}, {0, 1}};
    canon_group *g = NULL;
    canon_object *xs = NULL;
    canon_problem *p = NULL;
    canon_result *rs = NULL, *rd = NULL;
    CHECK(canon_group_create(ctx, 3, sym3, 2, &g) == CANON_COMPLETE);
    CHECK(canon_object_create_simple_graph(ctx, 3, path, 3, &xs) == CANON_COMPLETE);
    CHECK(canon_problem_create(ctx, g, xs, CANON_OBJECTIVE_CANONICAL_IMAGE, CANON_PROFILE_P1,
                               CANON_ENCODING_CDAG_2, CANON_ORDER_CDAG_BYTE_1, NULL,
                               &p) == CANON_COMPLETE);
    CHECK(canon_solve(ws, p, &rs) == CANON_COMPLETE);
    canon_problem_release(p);
    canon_object_release(xs);
    canon_group_release(g);
    const canon_arc arcs[4] = {{0, 1, NULL, 0, 1}, {1, 0, NULL, 0, 1}, {1, 2, NULL, 0, 1},
                               {2, 1, NULL, 0, 1}};
    graph_case c = {3, sym3, 2, NULL, NULL, arcs, 4};
    CHECK(solve_graph(ctx, ws, &c, NULL, &rd) == CANON_COMPLETE);
    CHECK(same_result(rs, rd));
    canon_result_release(rs);
    canon_result_release(rd);

    /* One workspace across subset and graph solves of several degrees gives exactly the results
     * of a fresh workspace each time (grow-only storage, kind switches). */
    const uint32_t swap[2] = {1, 0};
    const canon_arc arc01[1] = {{0, 1, NULL, 0, 1}};
    const uint32_t atoms[1] = {0};
    for (int i = 0; i < 6; ++i) {
        canon_workspace *fresh = NULL;
        canon_result *r1 = NULL, *r2 = NULL;
        CHECK(canon_workspace_create(ctx, &fresh) == CANON_COMPLETE);
        if (i % 2 == 0) {
            graph_case gc = i == 2 ? c : (graph_case){2, swap, 1, NULL, NULL, arc01, 1};
            CHECK(solve_graph(ctx, ws, &gc, NULL, &r1) == CANON_COMPLETE);
            CHECK(solve_graph(ctx, fresh, &gc, NULL, &r2) == CANON_COMPLETE);
        } else {
            canon_object *x = NULL;
            canon_problem *q = NULL;
            uint32_t n = i == 3 ? 3u : 2u;
            CHECK(canon_group_create(ctx, n, n == 3 ? sym3 : swap, n == 3 ? 2u : 1u, &g) ==
                  CANON_COMPLETE);
            CHECK(canon_object_create_subset(ctx, n, atoms, 1, &x) == CANON_COMPLETE);
            CHECK(canon_problem_create(ctx, g, x, CANON_OBJECTIVE_CANONICAL_IMAGE,
                                       CANON_PROFILE_P1, CANON_ENCODING_CDAG_2,
                                       CANON_ORDER_CDAG_BYTE_1, NULL, &q) == CANON_COMPLETE);
            CHECK(canon_solve(ws, q, &r1) == CANON_COMPLETE);
            CHECK(canon_solve(fresh, q, &r2) == CANON_COMPLETE);
            canon_problem_release(q);
            canon_object_release(x);
            canon_group_release(g);
        }
        CHECK(same_result(r1, r2));
        canon_result_release(r1);
        canon_result_release(r2);
        canon_workspace_release(fresh);
    }
}

/* Review item 1: spec 7.2 compares the trace first, so a leaf whose trace exceeds the best one
 * is not materialised.  A directed 2-cycle plus a directed 3-cycle under Sym(5): every vertex
 * has in- and out-degree 1, the root does not split, and individualising a 2-cycle vertex or a
 * 3-cycle vertex gives different traces.  The unpruned tree has 12 leaves; only 6 have a trace
 * <= the best so far in visiting order.  Review item 4: after the run the image no longer
 * points into the object. */
static void test_trace_first(canon_context *ctx)
{
    const uint32_t sym5[10] = {1, 0, 2, 3, 4, 1, 2, 3, 4, 0};
    const canon_arc arcs[5] = {{0, 1, NULL, 0, 1}, {1, 0, NULL, 0, 1}, {2, 3, NULL, 0, 1},
                               {3, 4, NULL, 0, 1}, {4, 2, NULL, 0, 1}};
    canon_group *g = NULL;
    CHECK(canon_group_create(ctx, 5, sym5, 2, &g) == CANON_COMPLETE);
    canon_root x;
    memset(&x, 0, sizeof x);
    x.kind = CANON_ROOT_GRAPH;
    x.n = 5;
    CHECK(canon_graph_init(&x.u.graph, 5, NULL, NULL, arcs, 5) == CANON_COMPLETE);
    canon_p1_search s;
    canon_p1_search_init(&s);
    CHECK(canon_p1_search_run(&s, g, &x, 1000) == CANON_COMPLETE);
    CHECK(s.leaves == 12);
    CHECK(s.images == 6 && s.images < s.leaves);
    /* the witness maps x to the returned bytes */
    graph_case c = {5, sym5, 2, NULL, NULL, arcs, 5};
    CHECK(image_bytes_match(&c, s.best_t, s.best_bytes.data, s.best_bytes.len));
    /* spec 17 / review item 4: no borrowed pointer survives the run */
    CHECK(s.image.root.kind == CANON_ROOT_GRAPH);
    CHECK(s.image.root.u.graph.colours.offset == NULL && s.image.root.u.graph.labels.pool == NULL);
    CHECK(s.image.root.u.graph.labels.offset == NULL && s.image.root.u.graph.n == 0);
    canon_root_free(&x);
    /* the same state then serves a subset with a smaller degree */
    canon_root sub;
    memset(&sub, 0, sizeof sub);
    sub.kind = CANON_ROOT_SUBSET;
    sub.n = 5;
    const uint32_t atoms[2] = {0, 3};
    CHECK(canon_subset_init(&sub.u.subset, 5, atoms, 2) == CANON_COMPLETE);
    CHECK(canon_p1_search_run(&s, g, &sub, 1000) == CANON_COMPLETE);
    canon_root_free(&sub);
    canon_p1_search_free(&s);
    canon_group_release(g);
}

/* Review item 4 through the public API: solve a labelled, coloured graph, release the problem,
 * object and result, then reuse the workspace for another graph (smaller degree, other
 * tables); under ASan any use of the released tables would be reported.  The answer must equal
 * a fresh workspace's. */
static void test_release_then_reuse(canon_context *ctx)
{
    const uint32_t sym3[6] = {1, 0, 2, 1, 2, 0}, swap[2] = {1, 0};
    const uint8_t *lab = (const uint8_t *)"lab", *col = (const uint8_t *)"colour";
    const uint8_t *colours[3] = {col, NULL, col};
    const size_t lengths[3] = {6, 0, 6};
    const canon_arc arcs[3] = {{0, 1, lab, 3, 2}, {2, 1, NULL, 0, 1}, {1, 1, lab, 3, 1}};
    graph_case first = {3, sym3, 2, colours, lengths, arcs, 3};
    const canon_arc arc10[1] = {{1, 0, lab, 3, 5}};
    graph_case second = {2, swap, 1, NULL, NULL, arc10, 1};
    canon_workspace *ws = NULL, *fresh = NULL;
    canon_result *r = NULL, *r2 = NULL;
    CHECK(canon_workspace_create(ctx, &ws) == CANON_COMPLETE);
    CHECK(canon_workspace_create(ctx, &fresh) == CANON_COMPLETE);
    CHECK(solve_graph(ctx, ws, &first, NULL, &r) == CANON_COMPLETE); /* releases x and p */
    canon_result_release(r);
    r = NULL;
    CHECK(solve_graph(ctx, ws, &second, NULL, &r) == CANON_COMPLETE);
    CHECK(solve_graph(ctx, fresh, &second, NULL, &r2) == CANON_COMPLETE);
    CHECK(same_result(r, r2));
    canon_result_release(r);
    canon_result_release(r2);
    /* and an abandoned run (node quota) also leaves nothing borrowed: a labelled directed
     * 3-cycle under Sym(3) does not split at the root, so it needs more than one node */
    const canon_arc cycle[3] = {{0, 1, lab, 3, 1}, {1, 2, lab, 3, 1}, {2, 0, lab, 3, 1}};
    graph_case third = {3, sym3, 2, NULL, NULL, cycle, 3};
    canon_capacity cap = {0, 0, 1, 0};
    CHECK(solve_graph(ctx, ws, &third, &cap, &r) == CANON_CAPACITY_LIMIT);
    canon_result_release(r);
    CHECK(solve_graph(ctx, ws, &second, NULL, &r) == CANON_COMPLETE);
    canon_result_release(r);
    canon_workspace_release(fresh);
    canon_workspace_release(ws);
}

int main(void)
{
    canon_context *ctx = NULL;
    canon_workspace *ws = NULL;
    CHECK(canon_context_create(NULL, &ctx) == CANON_COMPLETE);
    CHECK(canon_workspace_create(ctx, &ws) == CANON_COMPLETE);
    test_golden(ctx, ws);
    test_capacity(ctx, ws);
    test_golden(ctx, ws); /* the workspace is usable after an abandoned search */
    test_fixed_and_equivariant(ctx, ws);
    test_invalid(ctx);
    test_simple_and_reuse(ctx, ws);
    test_trace_first(ctx);
    test_release_then_reuse(ctx);
    canon_workspace_release(ws);
    canon_context_release(ctx);
    return check_finish("test_search_graph");
}
