/* Slice S8: output sink pause and resume (spec 17 "PAUSE retains the offset for resume, FAIL
 * returns OUTPUT_ERROR. No bytes are skipped or duplicated on resume."; docs/slices/S8.md 1.2,
 * D15) through the public API: a sink that pauses at every offset of a canonical stream, the
 * resumed stream equals the result's bytes exactly, partial acceptance, failure modes, the
 * signed zero payload, and the paused status. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "canon/canon.h"
#include "check.h"

typedef struct sink {
    uint8_t data[4096];
    size_t len;
    size_t pause_at;  /* pause (return 1) when len reaches this (SIZE_MAX: never) */
    size_t take;      /* accept at most this many bytes per call (0: all) */
    int fail_rc;      /* return this instead of 0 once len reaches fail_at */
    size_t fail_at;
    int calls;
} sink;

static int collect(void *user, const uint8_t *chunk, size_t length, size_t *accepted)
{
    sink *s = user;
    s->calls += 1;
    if (s->fail_rc != 0 && s->len >= s->fail_at) {
        *accepted = 0;
        return s->fail_rc;
    }
    if (s->len >= s->pause_at) {
        *accepted = 0;
        return 1; /* PAUSE before taking anything */
    }
    size_t take = s->take != 0 && s->take < length ? s->take : length;
    if (s->len + take > s->pause_at) {
        take = s->pause_at - s->len; /* take up to the pause point, then pause next call */
    }
    if (s->len + take > sizeof s->data) {
        return -1;
    }
    memcpy(s->data + s->len, chunk, take);
    s->len += take;
    *accepted = take;
    return 0;
}

static canon_result *solve_graph(canon_context *ctx, canon_workspace *ws)
{
    /* a graph on 6 vertices with labelled arcs: a stream of a few hundred bytes */
    const uint32_t gens[6] = {1, 2, 3, 4, 5, 0};
    static const uint8_t lab[2] = {0x61, 0x62};
    canon_arc arcs[12];
    for (uint32_t i = 0; i < 6; ++i) {
        canon_arc a = {i, (i + 1u) % 6u, lab, 2, i + 1u};
        canon_arc b = {i, (i + 2u) % 6u, NULL, 0, 1};
        arcs[2 * i] = a;
        arcs[2 * i + 1] = b;
    }
    canon_group *g = NULL;
    canon_object *x = NULL;
    canon_problem *p = NULL;
    canon_result *r = NULL;
    CHECK(canon_group_create(ctx, 6, gens, 1, &g) == CANON_COMPLETE);
    CHECK(canon_object_create_graph(ctx, 6, NULL, NULL, arcs, 12, &x) == CANON_COMPLETE);
    CHECK(canon_problem_create(ctx, g, x, CANON_OBJECTIVE_CANONICAL_IMAGE, CANON_PROFILE_P1,
                               CANON_ENCODING_CDAG_2, CANON_ORDER_CDAG_BYTE_1, NULL,
                               &p) == CANON_COMPLETE);
    CHECK(canon_solve(ws, p, &r) == CANON_COMPLETE);
    canon_problem_release(p);
    canon_object_release(x);
    canon_group_release(g);
    return r;
}

int main(void)
{
    canon_context *ctx = NULL;
    canon_workspace *ws = NULL;
    CHECK(canon_context_create(NULL, &ctx) == CANON_COMPLETE);
    CHECK(canon_workspace_create(ctx, &ws) == CANON_COMPLETE);
    canon_result *r = solve_graph(ctx, ws);
    size_t total = 0;
    const uint8_t *bytes = canon_result_bytes(r, &total);
    CHECK(bytes != NULL && total > 100 && total < 4096);

    /* pause at every offset, resume to completion: the collected stream is the result's */
    for (size_t at = 0; at <= total; ++at) {
        sink s = {{0}, 0, at, 7, 0, 0, 0};
        size_t offset = 0;
        canon_status st = canon_result_encode_resume(r, &offset, collect, &s);
        if (at < total) {
            CHECK(st == CANON_CANCELLED); /* D15: a pause is the interrupted status */
            CHECK(offset == at && s.len == at);
            s.pause_at = SIZE_MAX;
            st = canon_result_encode_resume(r, &offset, collect, &s);
        }
        CHECK(st == CANON_COMPLETE && offset == total && s.len == total);
        CHECK(memcmp(s.data, bytes, total) == 0); /* no byte skipped or duplicated */
    }
    /* canon_result_encode: offset 0, a pause is CANCELLED too; a failure is OUTPUT_ERROR */
    sink s = {{0}, 0, 10, 0, 0, 0, 0};
    CHECK(canon_result_encode(r, collect, &s) == CANON_CANCELLED && s.len == 10);
    sink f = {{0}, 0, SIZE_MAX, 5, -5, 20, 0}; /* five bytes a call, FAIL once 20 are taken */
    size_t offset = 0;
    CHECK(canon_result_encode_resume(r, &offset, collect, &f) == CANON_OUTPUT_ERROR);
    CHECK(offset == 20 && f.len == 20); /* the offset tells how far the sink got */
    /* an offset beyond the stream is INVALID_INPUT; at the end nothing is sent */
    offset = total + 1;
    CHECK(canon_result_encode_resume(r, &offset, collect, &f) == CANON_INVALID_INPUT);
    offset = total;
    sink none = {{0}, 0, SIZE_MAX, 0, 0, 0, 0};
    CHECK(canon_result_encode_resume(r, &offset, collect, &none) == CANON_COMPLETE && none.calls == 0);
    CHECK(canon_result_encode_resume(r, NULL, collect, &none) == CANON_INVALID_INPUT);
    CHECK(canon_result_encode_resume(NULL, &offset, collect, &none) == CANON_INVALID_INPUT);
    canon_result_release(r);

    /* a certified signed zero streams its single byte 00 and can be paused before it */
    {
        const uint32_t swap[2] = {1, 0};
        const int8_t minus[1] = {-1};
        canon_group *g = NULL;
        canon_object *x = NULL;
        canon_problem *p = NULL;
        canon_result *z = NULL;
        CHECK(canon_group_create_signed(ctx, 2, swap, 1, minus, &g) == CANON_COMPLETE);
        CHECK(canon_object_create_subset(ctx, 2, NULL, 0, &x) == CANON_COMPLETE);
        CHECK(canon_problem_create(ctx, g, x, CANON_OBJECTIVE_SIGNED_CANONICAL_IMAGE, CANON_PROFILE_P1,
                                   CANON_ENCODING_CDAG_2, CANON_ORDER_CDAG_BYTE_1, NULL,
                                   &p) == CANON_COMPLETE);
        CHECK(canon_solve(ws, p, &z) == CANON_COMPLETE);
        sink zs = {{0}, 0, 0, 0, 0, 0, 0};
        size_t off = 0;
        CHECK(canon_result_encode_resume(z, &off, collect, &zs) == CANON_CANCELLED && off == 0);
        zs.pause_at = SIZE_MAX;
        CHECK(canon_result_encode_resume(z, &off, collect, &zs) == CANON_COMPLETE);
        CHECK(off == 1 && zs.len == 1 && zs.data[0] == 0x00);
        canon_result_release(z);
        canon_problem_release(p);
        canon_object_release(x);
        canon_group_release(g);
    }
    canon_workspace_release(ws);
    canon_context_release(ctx);
    return check_finish("test_sink");
}
