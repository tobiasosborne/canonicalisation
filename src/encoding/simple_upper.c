/* The SIMPLE-UPPER-1 key (spec section 4.4). */
#include "encoding/simple_upper.h"

#include <string.h>

/* true iff g has an arc s -> t (any label).  The arcs are sorted by (source, target, label)
 * (spec 4.1), so a binary search over the arc array finds it without the CSR index; this works
 * for imported graphs and for images alike. */
static int has_arc(const canon_graph *g, uint32_t s, uint32_t t)
{
    uint32_t lo = 0, hi = g->e;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2u;
        const canon_graph_arc *a = &g->arcs[mid];
        if (a->source < s || (a->source == s && a->target < t)) {
            lo = mid + 1u;
        } else {
            hi = mid;
        }
    }
    return lo < g->e && g->arcs[lo].source == s && g->arcs[lo].target == t;
}

/* true iff the table is empty or holds only the empty string. */
static int only_empty(const canon_byte_table *t)
{
    size_t len = 0;
    if (t->count == 0) {
        return 1;
    }
    (void)canon_byte_table_get(t, 0, &len);
    return t->count == 1 && len == 0;
}

/* spec 4.4: "available only for uncoloured simple undirected graphs (empty vertex/arc labels,
 * no loops, and exactly one arc in each direction for each edge)". */
static int in_class(const canon_graph *g)
{
    /* Uncoloured, and empty arc labels only. */
    if (!only_empty(&g->colours) || !only_empty(&g->labels)) {
        return 0;
    }
    for (uint32_t i = 0; i < g->e; ++i) {
        const canon_graph_arc *a = &g->arcs[i];
        /* No loops; one unit arc (arcs are already combined, so multiplicity 1 means exactly
         * one); and the opposite arc exists, which with the same rule for it makes exactly one
         * arc in each direction. */
        if (a->source == a->target || a->multiplicity != 1 || !has_arc(g, a->target, a->source)) {
            return 0;
        }
    }
    return 1;
}

uint64_t canon_simple_upper_key_size(uint32_t n)
{
    /* spec 4.4: U32(n), then n(n - 1)/2 bits packed into whole bytes.  n <= 2^32 - 1, so
     * n(n - 1) < 2^64 and every quantity fits uint64. */
    const uint64_t m = n;
    const uint64_t pairs = m == 0 ? 0 : m * (m - 1u) / 2u;
    return 4u + pairs / 8u + (pairs % 8u != 0 ? 1u : 0u);
}

bool canon_simple_upper_in_class(const canon_graph *g)
{
    return in_class(g) != 0;
}

canon_status canon_simple_upper_key(const canon_graph *g, canon_buf *out)
{
    if (!in_class(g)) {
        return CANON_INVALID_INPUT; /* spec 4.4 restricts this order to the class above */
    }
    const uint64_t total = canon_simple_upper_key_size(g->n);
    if (total > (uint64_t)SIZE_MAX) {
        return CANON_CAPACITY_LIMIT; /* spec 11.1 */
    }
    const size_t key_bytes = (size_t)total - 4u;
    canon_status st = canon_buf_reserve(out, (size_t)total);
    if (st != CANON_COMPLETE) {
        return st;
    }
    (void)canon_buf_put_u32(out, g->n); /* spec 4.4: U32(n) */
    uint8_t *bits = out->data + out->len;
    if (key_bytes > 0) {
        memset(bits, 0, key_bytes); /* spec 4.4: padding bits are zero */
    }
    out->len += key_bytes;
    for (uint32_t i = 0; i < g->e; ++i) {
        uint64_t a = g->arcs[i].source, b = g->arcs[i].target;
        if (a > b) {
            continue; /* each edge is set once, from its arc a -> b with a < b */
        }
        /* spec 4.4: order (0,1),(0,2),(1,2),(0,3),...: pair (a, b) with a < b is bit number
         * b(b - 1)/2 + a; most significant bit first within each byte. */
        uint64_t k = b * (b - 1u) / 2u + a;
        bits[k / 8u] |= (uint8_t)(0x80u >> (unsigned)(k % 8u));
    }
    return CANON_COMPLETE;
}
