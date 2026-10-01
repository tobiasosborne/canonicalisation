/* Profile P1 refinement for a non-graph root (spec sections 7.1, 7.2). */
#include "refine/p1.h"

#include "arena/checked.h"

/* spec 7.2 token bytes. */
enum { TOKEN_LEAF = 0x00, TOKEN_NODE = 0x10, TOKEN_STAGE_O = 0x20, TOKEN_STAGE_G = 0x21 };

/* spec 7.2: STAGE_x(sizes) = tag || U32(k) || U32(s_0) ... U32(s_(k-1)), cell sizes of P in
 * semantic order. */
static canon_status append_stage(canon_buf *trace, uint8_t tag, const canon_partition *p)
{
    size_t bytes = 0;
    /* 1 + 4 + 4k bytes, k <= n <= 2^32 - 1: checked against SIZE_MAX (spec 11.1). */
    if (!canon_size_mul((size_t)p->cells, 4u, &bytes) || !canon_size_add(bytes, 5u, &bytes)) {
        return CANON_CAPACITY_LIMIT;
    }
    canon_status st = canon_buf_reserve(trace, bytes);
    if (st != CANON_COMPLETE) {
        return st;
    }
    (void)canon_buf_put_u8(trace, tag);
    (void)canon_buf_put_u32(trace, p->cells);
    for (uint32_t i = 0; i < p->cells; ++i) {
        (void)canon_buf_put_u32(trace, canon_partition_cell_size(p, i));
    }
    return CANON_COMPLETE;
}

void canon_p1_initial_subset(canon_partition *p, const canon_subset *x, canon_p1_scratch *s)
{
    /* spec 7.1: "All atoms are retained, including unused/fixed ones." */
    canon_partition_reset(p);
    for (uint32_t a = 0; a < p->n; ++a) {
        /* spec 7.1: "On the top-level subset ..., the initial key of a is membership 0/1" */
        s->sig[a] = canon_subset_initial_key(x, a);
    }
    /* spec 7.1: "Partition by equal keys and order cells by increasing key." */
    (void)canon_partition_split(p, s->sig);
}

canon_status canon_p1_refine_node(canon_partition *p, const canon_group *g, uint32_t depth,
                                  canon_buf *trace, canon_p1_scratch *s)
{
    /* spec 7.1/7.2: append NODE(depth) = 10 || U32(d) */
    canon_status st = canon_buf_put_u8(trace, TOKEN_NODE);
    if (st == CANON_COMPLETE) {
        st = canon_buf_put_u32(trace, depth);
    }
    for (;;) {
        if (st != CANON_COMPLETE) {
            return st;
        }
        uint32_t c0 = p->cells; /* spec 7.1: c0 = number of cells */

        /* spec 7.1 O stage: "for every other root, sig(v) is the empty vector", so the split
         * is the identity; the STAGE_O token is still appended. */
        st = append_stage(trace, TOKEN_STAGE_O, p);
        if (st != CANON_COMPLETE) {
            return st;
        }

        /* spec 7.1 G stage: "F = singleton atoms in current partition order (not branch
         * order)"; G reads the post-O partition. */
        uint32_t f = 0;
        for (uint32_t i = 0; i < p->cells; ++i) {
            if (canon_partition_cell_size(p, i) == 1) {
                s->fixed[f++] = p->lab[p->start[i]];
            }
        }
        /* spec 7.1: "M = lexicographically least F^G; choose any u in G with F^u=M; compute
         * G_M orbits; sort ...".  The backend returns orbit ids on target labels. */
        st = g->ops->tuple_min(g, s->fixed, f, s->u, s->orbit);
        if (st != CANON_COMPLETE) {
            return st;
        }
        /* spec 7.1: "sig(v) = position of the orbit containing u[v]" (pull back through u^-1) */
        for (uint32_t v = 0; v < p->n; ++v) {
            s->sig[v] = s->orbit[s->u[v]];
        }
        (void)canon_partition_split(p, s->sig);
        st = append_stage(trace, TOKEN_STAGE_G, p);
        if (st != CANON_COMPLETE) {
            return st;
        }
        /* spec 7.1: "if number of cells == c0: break"; one complete no-change sweep is always
         * recorded, even at a discrete root and at n = 0. */
        if (p->cells == c0) {
            return CANON_COMPLETE;
        }
    }
}

/* spec 7.2: LEAF = 00 */
canon_status canon_p1_trace_leaf(canon_buf *trace)
{
    return canon_buf_put_u8(trace, TOKEN_LEAF);
}
