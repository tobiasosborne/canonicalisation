/* Tests for the SIMPLE-UPPER-1 key (src/encoding/simple_upper.c, slice S2; spec 4.4).  Keys are
 * computed by hand: U32(n), then the upper-triangle bits in the order (0,1),(0,2),(1,2),(0,3),
 * (1,3),(2,3),(0,4),..., most significant bit first, final byte padded with zero low bits. */
#include <stdint.h>
#include <string.h>

#include "check.h"
#include "encoding/simple_upper.h"
#include "object/graph.h"

static void key_is(uint32_t n, const uint32_t (*edges)[2], size_t count, const char *want)
{
    canon_graph g;
    canon_buf b;
    canon_buf_init(&b);
    CHECK(canon_graph_init_simple(&g, n, edges, count) == CANON_COMPLETE);
    CHECK(canon_simple_upper_key(&g, &b) == CANON_COMPLETE);
    CHECK(check_hex_is(b.data, b.len, want));
    canon_buf_free(&b);
    canon_graph_free(&g);
}

static void rejected(uint32_t n, const uint8_t *const *colours, const size_t *lengths,
                     const canon_arc *arcs, size_t count)
{
    canon_graph g;
    canon_buf b;
    canon_buf_init(&b);
    CHECK(canon_graph_init(&g, n, colours, lengths, arcs, count) == CANON_COMPLETE);
    CHECK(canon_simple_upper_key(&g, &b) == CANON_INVALID_INPUT);
    CHECK(b.len == 0); /* nothing appended */
    canon_buf_free(&b);
    canon_graph_free(&g);
}

int main(void)
{
    /* n = 0 and 1: no pairs, no bit bytes. */
    key_is(0, NULL, 0, "00000000");
    key_is(1, NULL, 0, "00000001");
    /* n = 2: one pair (0,1) in one byte, seven zero padding bits. */
    key_is(2, NULL, 0, "00000002 00");
    const uint32_t e01[1][2] = {{1, 0}};
    key_is(2, e01, 1, "00000002 80");
    /* n = 3: bits (0,1),(0,2),(1,2). */
    const uint32_t star2[2][2] = {{0, 2}, {2, 1}};
    key_is(3, star2, 2, "00000003 60"); /* 0 1 1 | 00000 */
    const uint32_t k3[3][2] = {{0, 1}, {0, 2}, {1, 2}};
    key_is(3, k3, 3, "00000003 e0");
    const uint32_t e01b[1][2] = {{0, 1}};
    key_is(3, e01b, 1, "00000003 80");
    /* n = 4: bits (0,1),(0,2),(1,2),(0,3),(1,3),(2,3). */
    const uint32_t k4[6][2] = {{0, 1}, {0, 2}, {1, 2}, {0, 3}, {1, 3}, {2, 3}};
    key_is(4, k4, 6, "00000004 fc"); /* 111111 | 00 */
    const uint32_t e23[1][2] = {{3, 2}};
    key_is(4, e23, 1, "00000004 04"); /* bit 5 */
    const uint32_t e03[1][2] = {{0, 3}};
    key_is(4, e03, 1, "00000004 10"); /* bit 3 */
    /* n = 5: ten pairs, two bytes, six padding bits; (3,4) is bit 9. */
    const uint32_t e34[2][2] = {{4, 3}, {1, 0}};
    key_is(5, e34, 2, "00000005 8040");

    /* spec 4.4: available only for uncoloured simple undirected graphs. */
    const canon_arc one_way[1] = {{0, 1, NULL, 0, 1}};
    rejected(2, NULL, NULL, one_way, 1); /* a directed graph with a one-way arc */
    const canon_arc loop[1] = {{1, 1, NULL, 0, 1}};
    rejected(2, NULL, NULL, loop, 1);
    const canon_arc doubled[2] = {{0, 1, NULL, 0, 2}, {1, 0, NULL, 0, 2}};
    rejected(2, NULL, NULL, doubled, 2); /* not exactly one arc in each direction */
    const uint8_t *x = (const uint8_t *)"x";
    const canon_arc labelled[2] = {{0, 1, x, 1, 1}, {1, 0, x, 1, 1}};
    rejected(2, NULL, NULL, labelled, 2);
    const canon_arc mixed[3] = {{0, 1, NULL, 0, 1}, {1, 0, NULL, 0, 1}, {1, 0, x, 1, 1}};
    rejected(2, NULL, NULL, mixed, 3);
    const uint8_t *colours[2] = {x, NULL};
    const size_t lengths[2] = {1, 0};
    rejected(2, colours, lengths, NULL, 0); /* coloured */
    return check_finish("test_simple_upper");
}
