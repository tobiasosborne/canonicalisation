/* End-to-end C test of the slice S1 path through the public API only: the four subset cases of
 * spec section 7.4 (expected hex lives here, in tests, never in src/), capacity limits, invalid
 * input, unsupported requests, result_encode and its sink, workspace reuse, and handle
 * lifetimes (spec sections 3.2, 7.4, 11.1, 17). */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "canon/canon.h"
#include "check.h"

static void to_hex(const uint8_t *b, size_t n, char *out)
{
    static const char d[] = "0123456789abcdef";
    for (size_t i = 0; i < n; ++i) {
        out[2 * i] = d[b[i] >> 4];
        out[2 * i + 1] = d[b[i] & 15];
    }
    out[2 * n] = '\0';
}

static int hex_is(const uint8_t *b, size_t n, const char *want)
{
    char buf[1024];
    if (2 * n + 1 > sizeof buf) {
        return 0;
    }
    to_hex(b, n, buf);
    return strcmp(buf, want) == 0;
}

typedef struct {
    uint32_t n;
    const uint32_t *gens;
    size_t gen_count;
    const uint32_t *atoms;
    size_t atom_count;
    const char *trace;
    const char *bytes;
    const uint32_t *witness;
} golden_case;

/* Collecting sink: optional chunk limit (partial acceptance) and failure mode. */
typedef struct {
    uint8_t data[1024];
    size_t len;
    size_t max_take; /* 0 = take everything offered */
    int fail_rc;     /* returned on the call numbered fail_at (1-based); 0 = never fail */
    int fail_at;
    int calls;
} sink_state;

static int collect(void *user, const uint8_t *chunk, size_t length, size_t *accepted)
{
    sink_state *s = user;
    s->calls += 1;
    if (s->fail_rc != 0 && s->calls == s->fail_at) {
        *accepted = 0;
        return s->fail_rc;
    }
    size_t take = s->max_take != 0 && s->max_take < length ? s->max_take : length;
    if (s->len + take > sizeof s->data) {
        return -1;
    }
    memcpy(s->data + s->len, chunk, take);
    s->len += take;
    *accepted = take;
    return 0;
}

static canon_status solve_case(canon_context *ctx, canon_workspace *ws, const golden_case *c,
                               const canon_capacity *cap, canon_result **out)
{
    canon_group *g = NULL;
    canon_object *x = NULL;
    canon_problem *p = NULL;
    *out = NULL;
    canon_status st = canon_group_create(ctx, c->n, c->gens, c->gen_count, &g);
    if (st == CANON_COMPLETE) {
        st = canon_object_create_subset(ctx, c->n, c->atoms, c->atom_count, &x);
    }
    if (st == CANON_COMPLETE) {
        st = canon_problem_create(ctx, g, x, CANON_OBJECTIVE_CANONICAL_IMAGE, CANON_PROFILE_P1,
                                  CANON_ENCODING_CDAG_2, CANON_ORDER_CDAG_BYTE_1, cap, &p);
    }
    /* The problem retains group and object: release ours before solving. */
    canon_group_release(g);
    canon_object_release(x);
    if (st == CANON_COMPLETE) {
        st = canon_solve(ws, p, out);
    }
    canon_problem_release(p);
    return st;
}

static void check_golden(canon_context *ctx, canon_workspace *ws, const golden_case *c)
{
    canon_result *r = NULL;
    CHECK(solve_case(ctx, ws, c, NULL, &r) == CANON_COMPLETE);
    CHECK(r != NULL);
    if (r == NULL) {
        return;
    }
    CHECK(canon_result_status(r) == CANON_COMPLETE);
    canon_result_flags f = canon_result_get_flags(r);
    /* spec 3.2 / brief: exactly these three flags are true. */
    CHECK(f.witness_valid && f.image_canonical && f.encoding_complete);
    CHECK(!f.minimum_proved && !f.subgroup_verified && !f.stabiliser_complete &&
          !f.transport_exhausted && !f.zero_certified && !f.nonzero_certified);
    size_t tl = 0, bl = 0;
    const uint8_t *t = canon_result_trace(r, &tl);
    const uint8_t *b = canon_result_bytes(r, &bl);
    CHECK(t != NULL && hex_is(t, tl, c->trace));
    CHECK(b != NULL && hex_is(b, bl, c->bytes));
    uint32_t deg = 99;
    const uint32_t *w = canon_result_witness(r, &deg);
    CHECK(w != NULL); /* produced, even for degree 0 */
    CHECK(deg == c->n);
    if (w != NULL && deg == c->n && c->n > 0) {
        CHECK(memcmp(w, c->witness, c->n * sizeof *w) == 0);
    }
    /* result_encode reproduces the bytes, whole and in one-byte chunks. */
    sink_state s;
    memset(&s, 0, sizeof s);
    CHECK(canon_result_encode(r, collect, &s) == CANON_COMPLETE);
    CHECK(s.len == bl && memcmp(s.data, b, bl) == 0);
    memset(&s, 0, sizeof s);
    s.max_take = 1;
    CHECK(canon_result_encode(r, collect, &s) == CANON_COMPLETE);
    CHECK(s.len == bl && memcmp(s.data, b, bl) == 0 && s.calls == (int)bl);
    canon_result_release(r);
}

/* ---- handle lifetime: release in every order ---- */

enum { H_CTX, H_GROUP, H_OBJECT, H_PROBLEM, H_WS, H_RESULT, H_COUNT };

static void release_one(int which, void **h)
{
    switch (which) {
    case H_CTX:
        canon_context_release(h[H_CTX]);
        break;
    case H_GROUP:
        canon_group_release(h[H_GROUP]);
        break;
    case H_OBJECT:
        canon_object_release(h[H_OBJECT]);
        break;
    case H_PROBLEM:
        canon_problem_release(h[H_PROBLEM]);
        break;
    case H_WS:
        canon_workspace_release(h[H_WS]);
        break;
    default:
        canon_result_release(h[H_RESULT]);
        break;
    }
}

static int next_permutation(int *a, int n)
{
    int i = n - 2;
    while (i >= 0 && a[i] >= a[i + 1]) {
        --i;
    }
    if (i < 0) {
        return 0;
    }
    int j = n - 1;
    while (a[j] <= a[i]) {
        --j;
    }
    int t = a[i];
    a[i] = a[j];
    a[j] = t;
    for (int lo = i + 1, hi = n - 1; lo < hi; ++lo, --hi) {
        t = a[lo];
        a[lo] = a[hi];
        a[hi] = t;
    }
    return 1;
}

static void lifetimes(void)
{
    int order[H_COUNT] = {0, 1, 2, 3, 4, 5};
    const uint32_t gen[2] = {1, 0};
    int rounds = 0;
    do {
        void *h[H_COUNT] = {NULL};
        canon_context *ctx = NULL;
        canon_group *g = NULL;
        canon_object *x = NULL;
        canon_problem *p = NULL;
        canon_workspace *ws = NULL;
        canon_result *r = NULL;
        CHECK(canon_context_create(NULL, &ctx) == CANON_COMPLETE);
        CHECK(canon_group_create(ctx, 2, gen, 1, &g) == CANON_COMPLETE);
        CHECK(canon_object_create_subset(ctx, 2, NULL, 0, &x) == CANON_COMPLETE);
        CHECK(canon_problem_create(ctx, g, x, CANON_OBJECTIVE_CANONICAL_IMAGE, CANON_PROFILE_P1,
                                   CANON_ENCODING_CDAG_2, CANON_ORDER_CDAG_BYTE_1, NULL,
                                   &p) == CANON_COMPLETE);
        CHECK(canon_workspace_create(ctx, &ws) == CANON_COMPLETE);
        CHECK(canon_solve(ws, p, &r) == CANON_COMPLETE);
        h[H_CTX] = ctx;
        h[H_GROUP] = g;
        h[H_OBJECT] = x;
        h[H_PROBLEM] = p;
        h[H_WS] = ws;
        h[H_RESULT] = r;
        for (int i = 0; i < H_COUNT; ++i) {
            release_one(order[i], h);
            /* The result stays readable until it is released itself. */
            if (order[i] != H_RESULT && i < H_COUNT - 1) {
                int released_result = 0;
                for (int j = 0; j <= i; ++j) {
                    released_result |= order[j] == H_RESULT;
                }
                if (!released_result) {
                    size_t len = 0;
                    CHECK(canon_result_bytes(r, &len) != NULL && len == 24);
                }
            }
        }
        ++rounds;
    } while (next_permutation(order, H_COUNT));
    CHECK(rounds == 720);
    /* Retain/release pairs and NULL no-ops. */
    canon_context_release(NULL);
    canon_group_retain(NULL);
    canon_group_release(NULL);
    canon_object_retain(NULL);
    canon_object_release(NULL);
    canon_problem_retain(NULL);
    canon_problem_release(NULL);
    canon_workspace_retain(NULL);
    canon_workspace_release(NULL);
    canon_result_retain(NULL);
    canon_result_release(NULL);
    canon_context *ctx = NULL;
    canon_group *g = NULL;
    CHECK(canon_context_create(NULL, &ctx) == CANON_COMPLETE);
    CHECK(canon_group_create(ctx, 2, gen, 1, &g) == CANON_COMPLETE);
    canon_group_retain(g);
    canon_group_release(g);
    canon_group_release(g);
    canon_context_release(ctx);
}

int main(void)
{
    /* spec 7.4: the four subset golden cases. */
    const uint32_t swap[2] = {1, 0}, zero[1] = {0}, w10[2] = {1, 0}, w01[2] = {0, 1};
    const char *split_trace = "1000000000200000000200000001000000012100000002000000010000000100";
    const golden_case cases[4] = {
        {0, NULL, 0, NULL, 0, "10000000002000000000210000000000",
         "434e02000100010000000000000001040000000000000000", NULL},
        {2, swap, 1, zero, 1, split_trace,
         "434e02000100010000000200000002010000000104000000010000000000000001", w10},
        {2, NULL, 0, zero, 1, split_trace,
         "434e02000100010000000200000002010000000004000000010000000000000001", w01},
        {2, swap, 1, NULL, 0,
         "10000000002000000001000000022100000001000000021000000001200000000200000001000000012100"
         "000002000000010000000100",
         "434e02000100010000000200000001040000000000000000", w01},
    };
    canon_context *ctx = NULL;
    canon_workspace *ws = NULL;
    CHECK(canon_context_create(NULL, &ctx) == CANON_COMPLETE);
    CHECK(canon_workspace_create(ctx, &ws) == CANON_COMPLETE);
    /* Workspace reuse across degrees, in both directions. */
    for (int round = 0; round < 2; ++round) {
        for (int i = 0; i < 4; ++i) {
            check_golden(ctx, ws, &cases[round == 0 ? i : 3 - i]);
        }
    }
    /* Duplicate atoms are deduplicated (spec 4.2). */
    {
        const uint32_t dup[3] = {0, 0, 0};
        golden_case c = cases[1];
        c.atoms = dup;
        c.atom_count = 3;
        check_golden(ctx, ws, &c);
    }
    /* Results never alias the workspace: a later solve leaves an earlier result unchanged. */
    {
        canon_result *r1 = NULL, *r2 = NULL;
        CHECK(solve_case(ctx, ws, &cases[1], NULL, &r1) == CANON_COMPLETE);
        CHECK(solve_case(ctx, ws, &cases[0], NULL, &r2) == CANON_COMPLETE);
        size_t len = 0;
        const uint8_t *b = canon_result_bytes(r1, &len);
        CHECK(b != NULL && hex_is(b, len, cases[1].bytes));
        canon_result_release(r1);
        canon_result_release(r2);
    }

    /* spec 11.1: logical work quota.  The n=2 empty subset under Sym(2) has 3 NODE tokens. */
    {
        canon_capacity cap = {0, 0, 2, 0};
        canon_result *r = NULL;
        CHECK(solve_case(ctx, ws, &cases[3], &cap, &r) == CANON_CAPACITY_LIMIT);
        CHECK(r != NULL);
        CHECK(canon_result_status(r) == CANON_CAPACITY_LIMIT);
        canon_result_flags f = canon_result_get_flags(r);
        CHECK(!f.witness_valid && !f.image_canonical && !f.encoding_complete &&
              !f.minimum_proved && !f.subgroup_verified && !f.stabiliser_complete &&
              !f.transport_exhausted && !f.zero_certified && !f.nonzero_certified);
        size_t len = 7;
        uint32_t deg = 7;
        CHECK(canon_result_trace(r, &len) == NULL && len == 0);
        len = 7;
        CHECK(canon_result_bytes(r, &len) == NULL && len == 0);
        CHECK(canon_result_witness(r, &deg) == NULL && deg == 0);
        sink_state s;
        memset(&s, 0, sizeof s);
        CHECK(canon_result_encode(r, collect, &s) == CANON_INVALID_INPUT);
        CHECK(s.calls == 0);
        canon_result_release(r);
        cap.max_search_nodes = 3; /* exactly the traversal: admitted */
        CHECK(solve_case(ctx, ws, &cases[3], &cap, &r) == CANON_COMPLETE);
        canon_result_release(r);
        /* The workspace is still usable after an abandoned search. */
        check_golden(ctx, ws, &cases[3]);
    }
    /* spec 11.1: degree, group order and output byte limits at problem time. */
    {
        canon_result *r = NULL;
        canon_capacity cap = {1, 0, 0, 0};
        CHECK(solve_case(ctx, ws, &cases[1], &cap, &r) == CANON_CAPACITY_LIMIT);
        CHECK(r == NULL);
        cap = (canon_capacity){0, 1, 0, 0};
        CHECK(solve_case(ctx, ws, &cases[1], &cap, &r) == CANON_CAPACITY_LIMIT);
        cap = (canon_capacity){0, 0, 0, 32}; /* one-atom stream is 33 bytes */
        CHECK(solve_case(ctx, ws, &cases[1], &cap, &r) == CANON_CAPACITY_LIMIT);
        cap = (canon_capacity){0, 0, 0, 33};
        CHECK(solve_case(ctx, ws, &cases[1], &cap, &r) == CANON_COMPLETE);
        canon_result_release(r);
        /* Context-level limits apply to the builders. */
        canon_context *small = NULL;
        canon_capacity d = {1, 1, 0, 0};
        CHECK(canon_context_create(&d, &small) == CANON_COMPLETE);
        canon_group *g = (canon_group *)&d;
        canon_object *x = (canon_object *)&d;
        CHECK(canon_group_create(small, 2, swap, 1, &g) == CANON_CAPACITY_LIMIT && g == NULL);
        CHECK(canon_group_create(small, 1, NULL, 0, &g) == CANON_COMPLETE);
        canon_group_release(g);
        const uint32_t id2[2] = {0, 1};
        d.max_n = 2;
        canon_context_release(small);
        CHECK(canon_context_create(&d, &small) == CANON_COMPLETE);
        CHECK(canon_group_create(small, 2, id2, 1, &g) == CANON_COMPLETE);
        canon_group_release(g);
        CHECK(canon_group_create(small, 2, swap, 1, &g) == CANON_CAPACITY_LIMIT && g == NULL);
        CHECK(canon_object_create_subset(small, 3, NULL, 0, &x) == CANON_CAPACITY_LIMIT);
        CHECK(x == NULL);
        canon_context_release(small);
    }
    /* Invalid input: degree mismatch, atom out of range, invalid generator, NULL args. */
    {
        canon_group *g = NULL;
        canon_object *x = NULL;
        canon_problem *p = NULL;
        CHECK(canon_group_create(ctx, 2, swap, 1, &g) == CANON_COMPLETE);
        CHECK(canon_object_create_subset(ctx, 3, zero, 1, &x) == CANON_COMPLETE);
        p = (canon_problem *)&ctx;
        CHECK(canon_problem_create(ctx, g, x, CANON_OBJECTIVE_CANONICAL_IMAGE, CANON_PROFILE_P1,
                                   CANON_ENCODING_CDAG_2, CANON_ORDER_CDAG_BYTE_1, NULL,
                                   &p) == CANON_INVALID_INPUT);
        CHECK(p == NULL);
        canon_object_release(x);
        x = (canon_object *)&ctx;
        const uint32_t bad_atom[1] = {2};
        CHECK(canon_object_create_subset(ctx, 2, bad_atom, 1, &x) == CANON_INVALID_INPUT);
        CHECK(x == NULL);
        CHECK(canon_object_create_subset(ctx, 2, NULL, 1, &x) == CANON_INVALID_INPUT);
        CHECK(canon_object_create_subset(NULL, 2, NULL, 0, &x) == CANON_INVALID_INPUT);
        CHECK(canon_object_create_subset(ctx, 2, NULL, 0, NULL) == CANON_INVALID_INPUT);
        canon_group *bad = (canon_group *)&ctx;
        const uint32_t notperm[2] = {1, 1};
        CHECK(canon_group_create(ctx, 2, notperm, 1, &bad) == CANON_INVALID_INPUT);
        CHECK(bad == NULL);
        CHECK(canon_context_create(NULL, NULL) == CANON_INVALID_INPUT);

        /* Unsupported objective, profile, encoding, order (spec 3.2, 4.1). */
        CHECK(canon_object_create_subset(ctx, 2, zero, 1, &x) == CANON_COMPLETE);
        CHECK(canon_problem_create(ctx, g, x, CANON_OBJECTIVE_LEX_MIN_IMAGE, CANON_PROFILE_P1,
                                   CANON_ENCODING_CDAG_2, CANON_ORDER_CDAG_BYTE_1, NULL,
                                   &p) == CANON_UNSUPPORTED_ACTION);
        CHECK(p == NULL);
        CHECK(canon_problem_create(ctx, g, x, CANON_OBJECTIVE_SIGNED_CANONICAL_IMAGE,
                                   CANON_PROFILE_P1, CANON_ENCODING_CDAG_2,
                                   CANON_ORDER_CDAG_BYTE_1, NULL,
                                   &p) == CANON_UNSUPPORTED_ACTION);
        CHECK(canon_problem_create(ctx, g, x, CANON_OBJECTIVE_CANONICAL_IMAGE,
                                   CANON_PROFILE_NO_TREE, CANON_ENCODING_CDAG_2,
                                   CANON_ORDER_CDAG_BYTE_1, NULL,
                                   &p) == CANON_UNSUPPORTED_ACTION);
        CHECK(canon_problem_create(ctx, g, x, CANON_OBJECTIVE_CANONICAL_IMAGE, CANON_PROFILE_P1, 3,
                                   CANON_ORDER_CDAG_BYTE_1, NULL, &p) == CANON_UNSUPPORTED_ACTION);
        CHECK(canon_problem_create(ctx, g, x, CANON_OBJECTIVE_CANONICAL_IMAGE, CANON_PROFILE_P1,
                                   CANON_ENCODING_CDAG_2, CANON_ORDER_SIMPLE_UPPER_1, NULL,
                                   &p) == CANON_UNSUPPORTED_ACTION);
        CHECK(p == NULL);
        CHECK(canon_problem_create(ctx, NULL, x, CANON_OBJECTIVE_CANONICAL_IMAGE,
                                   CANON_PROFILE_P1, CANON_ENCODING_CDAG_2,
                                   CANON_ORDER_CDAG_BYTE_1, NULL, &p) == CANON_INVALID_INPUT);
        CHECK(canon_problem_create(ctx, g, x, CANON_OBJECTIVE_CANONICAL_IMAGE, CANON_PROFILE_P1,
                                   CANON_ENCODING_CDAG_2, CANON_ORDER_CDAG_BYTE_1, NULL,
                                   &p) == CANON_COMPLETE);
        canon_result *r = (canon_result *)&ctx;
        CHECK(canon_solve(NULL, p, &r) == CANON_INVALID_INPUT && r == NULL);
        CHECK(canon_solve(ws, NULL, &r) == CANON_INVALID_INPUT && r == NULL);
        CHECK(canon_solve(ws, p, NULL) == CANON_INVALID_INPUT);
        /* Sink failure and pause (deferred to S8) are OUTPUT_ERROR; result unchanged. */
        CHECK(canon_solve(ws, p, &r) == CANON_COMPLETE);
        sink_state s;
        memset(&s, 0, sizeof s);
        s.fail_rc = 7;
        s.fail_at = 1;
        CHECK(canon_result_encode(r, collect, &s) == CANON_OUTPUT_ERROR);
        memset(&s, 0, sizeof s);
        s.fail_rc = 1;
        s.fail_at = 2;
        s.max_take = 5;
        CHECK(canon_result_encode(r, collect, &s) == CANON_OUTPUT_ERROR);
        CHECK(s.len == 5);
        CHECK(canon_result_encode(r, NULL, &s) == CANON_INVALID_INPUT);
        CHECK(canon_result_status(r) == CANON_COMPLETE);
        CHECK(canon_result_get_flags(r).encoding_complete);
        memset(&s, 0, sizeof s);
        CHECK(canon_result_encode(r, collect, &s) == CANON_COMPLETE);
        CHECK(hex_is(s.data, s.len, cases[1].bytes));
        canon_result_release(r);
        CHECK(canon_result_status(NULL) == CANON_INVALID_INPUT);
        CHECK(!canon_result_get_flags(NULL).witness_valid);
        canon_problem_release(p);
        canon_object_release(x);
        canon_group_release(g);
    }
    canon_workspace_release(ws);
    canon_context_release(ctx);
    lifetimes();
    return check_finish("test_search_subset");
}
