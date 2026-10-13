/* Fuzz target: the CDAG-2 decoder, normaliser and validator through the public API (spec 4.1,
 * 4.2; slice S8, docs/slices/S8.md 1.7; detailed plan WP4.8).  The first four bytes of an
 * input select the degree (modulo 9, so that atoms and permutations stay in a small domain
 * often enough to reach the deeper rules); the rest is the stream.  Any status is acceptable;
 * a crash, a sanitizer report or a leak is a finding. */
#include "canon/canon.h"
#include "fuzz_driver.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size < 4) {
        return 0;
    }
    const uint32_t n = ((uint32_t)data[0] << 24 | (uint32_t)data[1] << 16 | (uint32_t)data[2] << 8 |
                        data[3]) % 9u;
    const uint8_t *stream = data + 4;
    const size_t len = size - 4;
    canon_context *ctx = NULL;
    if (canon_context_create(NULL, &ctx) != CANON_COMPLETE) {
        return 0;
    }
    canon_object *x = NULL;
    const canon_status st = canon_object_create(ctx, CANON_SCHEMA_EXT_DAG_1,
                                                CANON_ACTION_ATOM_TRANSPORT_1, n, stream, len, &x);
    (void)canon_stream_validate(ctx, stream, len);
    if (st == CANON_COMPLETE) {
        /* a decoded object solves under the trivial group: the P1 search on an arbitrary root */
        canon_group *g = NULL;
        canon_problem *p = NULL;
        canon_workspace *ws = NULL;
        canon_result *r = NULL;
        canon_capacity cap = {0, 0, 64, 0, 0, 0, 0, 0, 0};
        if (canon_group_create(ctx, n, NULL, 0, &g) == CANON_COMPLETE &&
            canon_problem_create(ctx, g, x, CANON_OBJECTIVE_CANONICAL_IMAGE, CANON_PROFILE_P1,
                                 CANON_ENCODING_CDAG_2, CANON_ORDER_CDAG_BYTE_1, &cap,
                                 &p) == CANON_COMPLETE &&
            canon_workspace_create(ctx, &ws) == CANON_COMPLETE) {
            (void)canon_solve(ws, p, &r);
        }
        canon_result_release(r);
        canon_workspace_release(ws);
        canon_problem_release(p);
        canon_group_release(g);
    }
    canon_object_release(x);
    canon_context_release(ctx);
    return 0;
}
