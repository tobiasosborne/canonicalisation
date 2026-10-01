/* Tests for the CDAG-2 graph stream (src/encoding/graph_stream.c, slice S2; spec 4.1, 4.2,
 * 7.2, 7.4).  Expected hex lives here, in tests only: the spec 7.4 one-arc case (transcribed
 * from the spec with H(n) expanded), the n = 1 loop case and two further streams derived by
 * hand from the spec 4.1 grammar.  The golden cases run through the public API. */
#include <stdint.h>
#include <string.h>

#include "canon/canon.h"
#include "check.h"
#include "encoding/graph_stream.h"
#include "object/graph.h"

static const uint8_t *str(const char *s)
{
    return (const uint8_t *)s;
}

/* Solve CANONICAL_IMAGE for a graph through the public API and compare every field. */
static void api_case(uint32_t n, const uint32_t *gens, size_t gen_count, const canon_arc *arcs,
                     size_t arc_count, const char *trace, const char *bytes,
                     const uint32_t *witness)
{
    canon_context *ctx = NULL;
    canon_group *g = NULL;
    canon_object *x = NULL;
    canon_problem *p = NULL;
    canon_workspace *ws = NULL;
    canon_result *r = NULL;
    const uint8_t *colours[4] = {NULL, NULL, NULL, NULL};
    const size_t lengths[4] = {0, 0, 0, 0};
    CHECK(canon_context_create(NULL, &ctx) == CANON_COMPLETE);
    CHECK(canon_group_create(ctx, n, gens, gen_count, &g) == CANON_COMPLETE);
    CHECK(canon_object_create_graph(ctx, n, colours, lengths, arcs, arc_count, &x) ==
          CANON_COMPLETE);
    CHECK(canon_problem_create(ctx, g, x, CANON_OBJECTIVE_CANONICAL_IMAGE, CANON_PROFILE_P1,
                               CANON_ENCODING_CDAG_2, CANON_ORDER_CDAG_BYTE_1, NULL,
                               &p) == CANON_COMPLETE);
    CHECK(canon_workspace_create(ctx, &ws) == CANON_COMPLETE);
    CHECK(canon_solve(ws, p, &r) == CANON_COMPLETE);
    size_t tl = 0, bl = 0;
    const uint8_t *t = canon_result_trace(r, &tl), *b = canon_result_bytes(r, &bl);
    CHECK(t != NULL && check_hex_is(t, tl, trace));
    CHECK(b != NULL && check_hex_is(b, bl, bytes));
    uint32_t deg = 99;
    const uint32_t *w = canon_result_witness(r, &deg);
    CHECK(w != NULL && deg == n);
    if (w != NULL && deg == n && n > 0) {
        CHECK(memcmp(w, witness, n * sizeof *w) == 0);
    }
    canon_result_release(r);
    canon_workspace_release(ws);
    canon_problem_release(p);
    canon_object_release(x);
    canon_group_release(g);
    canon_context_release(ctx);
}

/* Write the stream of a graph built from the inputs and compare; also the exact size. */
static void stream_case(uint32_t n, const uint8_t *const *colours, const size_t *lengths,
                        const canon_arc *arcs, size_t arc_count, const char *want)
{
    canon_graph g;
    canon_buf b;
    canon_buf_init(&b);
    CHECK(canon_graph_init(&g, n, colours, lengths, arcs, arc_count) == CANON_COMPLETE);
    CHECK(canon_buf_put_u8(&b, 0xee) == CANON_COMPLETE); /* appends after existing bytes */
    CHECK(canon_graph_stream_write(&b, &g) == CANON_COMPLETE);
    CHECK(b.len > 0 && b.data[0] == 0xee && check_hex_is(b.data + 1, b.len - 1, want));
    uint64_t size = 0;
    CHECK(canon_graph_stream_size(&g, &size) == CANON_COMPLETE && size == b.len - 1);
    canon_buf_free(&b);
    canon_graph_free(&g);
}

int main(void)
{
    /* spec 7.4: "n=2, one unit arc 0->1, all labels empty, G=Sym(2)": trace and stream as
     * printed there; witness [1,0]; output arc 1->0. */
    const uint32_t swap[2] = {1, 0}, w10[2] = {1, 0}, w0[1] = {0};
    const canon_arc one_arc[1] = {{0, 1, NULL, 0, 1}};
    api_case(2, swap, 1, one_arc, 1,
             "10 00000000 20 00000002 00000001 00000001 21 00000002 00000001 00000001 "
             "20 00000002 00000001 00000001 21 00000002 00000001 00000001 00",
             "434e0200010001 00000002 00000001 09 00000000 00000000 00000001 "
             "00000001 00000000 00000000 00000001 01 00000000",
             w10);
    /* n = 1 loop case (S2 brief 3.2; review_checks.run_v2): (0,0,"",1) twice equals
     * (0,0,"",2).  Derived from spec 4.1: H(1), q = 1, 09, B(""), U32(1),
     * arc 0 -> 0, B(""), Nat(2) = 00000001 02, root 0.  Trace: the discrete root's single
     * no-change sweep (spec 7.1) and LEAF. */
    const char *loop_trace = "10 00000000 20 00000001 00000001 21 00000001 00000001 00";
    const char *loop_bytes = "434e0200010001 00000001 00000001 09 00000000 00000001 "
                             "00000000 00000000 00000000 00000001 02 00000000";
    const canon_arc two_loops[2] = {{0, 0, NULL, 0, 1}, {0, 0, NULL, 0, 1}};
    const canon_arc double_loop[1] = {{0, 0, NULL, 0, 2}};
    api_case(1, NULL, 0, two_loops, 2, loop_trace, loop_bytes, w0);
    api_case(1, NULL, 0, double_loop, 1, loop_trace, loop_bytes, w0);
    stream_case(1, NULL, NULL, two_loops, 2, loop_bytes);
    /* n = 0: spec 4.1 "Graphs with no arcs remain valid"; the record holds no colours. */
    api_case(0, NULL, 0, NULL, 0, "10 00000000 20 00000000 21 00000000 00",
             "434e0200010001 00000000 00000001 09 00000000 00000000", NULL);
    /* Colours and labels, derived by hand from spec 4.1: colours ["a", ""]; arcs given as
     * (1,0,"x",300), (0,1,"",1), (0,1,"x",1) are written sorted by (source, target, B(label)):
     * (0,1,""), (0,1,"x"), (1,0,"x"); Nat(300) = 00000002 012c. */
    const uint8_t *colours[2] = {str("a"), NULL};
    const size_t lengths[2] = {1, 0};
    const canon_arc arcs[3] = {{1, 0, str("x"), 1, 300}, {0, 1, NULL, 0, 1}, {0, 1, str("x"), 1, 1}};
    stream_case(2, colours, lengths, arcs, 3,
                "434e0200010001 00000002 00000001 09 00000001 61 00000000 00000003 "
                "00000000 00000001 00000000 00000001 01 "
                "00000000 00000001 00000001 78 00000001 01 "
                "00000001 00000000 00000001 78 00000002 012c "
                "00000000");
    return check_finish("test_graph_stream");
}
