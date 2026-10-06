/* Profile P1 refinement (spec sections 7.1, 7.2, 10) for any root object: subset (S1) and
 * coloured directed multigraph (S2, O-stage arc-count signatures). */
#include "refine/p1.h"

#include <stdlib.h>
#include <string.h>

#include "arena/alloc.h"
#include "arena/checked.h"
#include "util/sort.h"

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

/* ---- scratch (grow-only) ---- */

void canon_p1_scratch_init(canon_p1_scratch *s)
{
    memset(s, 0, sizeof *s);
}

void canon_p1_scratch_free(canon_p1_scratch *s)
{
    free(s->pt.block);
    free(s->entries); /* entries_tmp is the second half of this block */
    canon_p1_scratch_init(s);
}

/* Allocate the per-point arrays for n points as one block.  The uint32 arrays are listed once,
 * in `slots`; sig_len (size_t) comes first so that every array is suitably aligned (malloc
 * alignment, then a multiple of sizeof(size_t), then uint32 arrays). */
static canon_status points_alloc(canon_p1_points *pt, uint32_t n)
{
    uint32_t **const slots[] = {&pt->fixed, &pt->u, &pt->orbit, &pt->sig, &pt->order,
                                &pt->order_tmp};
    const size_t count = sizeof slots / sizeof *slots;
    const size_t m = n > 0 ? (size_t)n : 1u;
    size_t len_bytes = 0, u32_bytes = 0, total = 0;
    if (!canon_size_mul(m, sizeof(size_t), &len_bytes) ||
        !canon_size_mul3(m, count, sizeof(uint32_t), &u32_bytes) ||
        !canon_size_add(len_bytes, u32_bytes, &total)) {
        return CANON_CAPACITY_LIMIT; /* spec 11.1 */
    }
    canon_status st = CANON_COMPLETE;
    unsigned char *block = canon_alloc_array(total, 1u, &st);
    if (block == NULL) {
        return st;
    }
    pt->block = block;
    pt->sig_len = (size_t *)(void *)block;
    uint32_t *next = (uint32_t *)(void *)(block + len_bytes);
    for (size_t i = 0; i < count; ++i) {
        *slots[i] = next;
        next += m;
    }
    return CANON_COMPLETE;
}

canon_status canon_p1_scratch_reserve(canon_p1_scratch *s, const canon_root *x)
{
    if (s->pt.block == NULL || x->n > s->cap_n) {
        canon_p1_points fresh;
        memset(&fresh, 0, sizeof fresh);
        canon_status st = points_alloc(&fresh, x->n);
        if (st != CANON_COMPLETE) {
            return st; /* spec 17: the previous arrays stay */
        }
        free(s->pt.block);
        s->pt = fresh; /* swapped wholesale */
        s->cap_n = x->n;
    }
    if (x->kind != CANON_ROOT_GRAPH) {
        return CANON_COMPLETE;
    }
    /* Each arc is one out-entry of its source and one in-entry of its target: 2e entries, and
     * as many again for the sort scratch. */
    size_t need = 0, both = 0;
    if (!canon_size_mul((size_t)x->u.graph.e, 2u, &need) ||
        !canon_size_mul(need > 0 ? need : 1u, 2u, &both)) {
        return CANON_CAPACITY_LIMIT; /* spec 11.1 */
    }
    if (s->entries != NULL && need <= s->cap_entries) {
        return CANON_COMPLETE;
    }
    canon_status st = CANON_COMPLETE;
    canon_p1_sig_entry *entries = canon_alloc_array(both, sizeof *entries, &st);
    if (entries == NULL) {
        return st;
    }
    free(s->entries);
    s->entries = entries;
    s->entries_tmp = entries + both / 2u;
    s->cap_entries = both / 2u;
    return CANON_COMPLETE;
}

/* ---- root partition ---- */

void canon_p1_initial(canon_partition *p, const canon_root *x, canon_p1_scratch *s)
{
    /* spec 7.1: "All atoms are retained, including unused/fixed ones." */
    canon_partition_reset(p);
    for (uint32_t a = 0; a < p->n; ++a) {
        /* spec 7.1: membership 0/1 on a top-level subset, B(vertex_colour[a]) on a top-level
         * graph (as its rank in B order) */
        s->pt.sig[a] = canon_root_initial_key(x, a);
    }
    /* spec 7.1: "Partition by equal keys and order cells by increasing key." */
    (void)canon_partition_split(p, s->pt.sig);
}

/* ---- O stage for a graph root (spec 7.1, 10) ---- */

/* Sparse signatures (docs/slices/S2.md section 3.4).  The dense spec 7.1 signature of v is the
 * vector, in this index order,
 *     for label rank l in 0..L-1 (labels sorted by B(label)):
 *         for cell index j in 0..k-1 (the entry snapshot):
 *             out(v, l, j) = total multiplicity of arcs v -> w with label l and w in C[j]
 *             in (v, l, j) = total multiplicity of arcs w -> v with label l and w in C[j]
 * so out(v, l, j) sits at index ((l * k) + j) * 2 and in(v, l, j) at ((l * k) + j) * 2 + 1.
 * It has 2 * L * k entries, quadratic in n for a discrete partition, so it is never built: a
 * signature is the list of its nonzero entries (index, count) by increasing index.
 *
 * Comparison rule.  Within one stage every dense vector has the same length 2 * L * k (L and k
 * are fixed during the stage).  Walk both sparse lists in step.  While the next indices are
 * equal, the dense vectors agree at every position before that index (both zero, or equal
 * counts already compared), so compare the counts there.  If A's next index i is smaller than
 * B's, then the dense vectors agree before i, A[i] > 0 and B[i] = 0 (B has no entry at i), so
 * A > B; symmetrically B > A.  If one list is exhausted, every remaining dense position of that
 * vector is zero: the other vector is greater iff it still has an entry (necessarily positive),
 * else the vectors are equal.  This is exactly the dense lexicographic comparison with the
 * zeros elided (spec 7.1: "Count signatures compare lexicographically using the ordinary
 * numerical order on mathematical naturals"); tests/c/test_signature.c checks it against a
 * dense implementation. */
int canon_p1_sig_compare(const canon_p1_sig_entry *a, size_t a_len, const canon_p1_sig_entry *b,
                         size_t b_len)
{
    size_t i = 0;
    for (; i < a_len && i < b_len; ++i) {
        if (a[i].index != b[i].index) {
            /* the smaller index is a positive entry where the other vector has zero */
            return a[i].index < b[i].index ? 1 : -1;
        }
        if (a[i].count != b[i].count) {
            return a[i].count < b[i].count ? -1 : 1;
        }
    }
    if (i < a_len) {
        return 1;
    }
    if (i < b_len) {
        return -1;
    }
    return 0;
}

static int entry_cmp(const void *x, const void *y, void *ctx)
{
    (void)ctx;
    uint64_t a = ((const canon_p1_sig_entry *)x)->index;
    uint64_t b = ((const canon_p1_sig_entry *)y)->index;
    return a < b ? -1 : (a > b ? 1 : 0);
}

/* First entry of v's signature: v's incident arcs are out_start[v] + in_start[v] entries in. */
static size_t sig_offset(const canon_graph *g, uint32_t v)
{
    return (size_t)g->out_start[v] + (size_t)g->in_start[v];
}

const canon_p1_sig_entry *canon_p1_signature(const canon_p1_scratch *s, const canon_graph *g,
                                             uint32_t v, size_t *length)
{
    *length = s->pt.sig_len[v];
    return s->entries + sig_offset(g, v);
}

canon_status canon_p1_graph_signatures(const canon_partition *p, const canon_graph *g,
                                       canon_p1_scratch *s)
{
    /* spec 7.1: "Within O all signatures refer to its entry snapshot": every signature is
     * computed from p (cell_of and k) before the split modifies it, so p itself is the entry
     * snapshot and no copy is needed. */
    if (!g->indexed) {
        return CANON_INTERNAL_ERROR; /* precondition: an imported graph, which has CSR/CSC */
    }
    const uint64_t k = p->cells;
    uint64_t lk = 0;
    if (!canon_u64_mul(g->labels.count, k, &lk) || lk > UINT64_MAX / 2u) {
        return CANON_CAPACITY_LIMIT; /* the index range 2 * L * k must fit uint64 */
    }
    for (uint32_t v = 0; v < g->n; ++v) {
        canon_p1_sig_entry *sig = s->entries + sig_offset(g, v);
        size_t len = 0;
        /* spec 7.1 "outgoing multiplicity from v into C[j]" (CSR: spec 10 visits only
         * incident arcs).  A loop v -> v is counted here once ... */
        for (uint32_t i = g->out_start[v]; i < g->out_start[v + 1u]; ++i) {
            const canon_graph_arc *a = &g->arcs[i];
            sig[len].index = (((uint64_t)a->label * k) + p->cell_of[a->target]) * 2u;
            sig[len].count = a->multiplicity;
            ++len;
        }
        /* ... and here once more: spec 7.1 "A graph loop contributes once to each
         * incoming/outgoing count".  "incoming multiplicity from C[j] into v" (CSC). */
        for (uint32_t i = g->in_start[v]; i < g->in_start[v + 1u]; ++i) {
            const canon_graph_arc *a = &g->arcs[g->in_arc[i]];
            sig[len].index = (((uint64_t)a->label * k) + p->cell_of[a->source]) * 2u + 1u;
            sig[len].count = a->multiplicity;
            ++len;
        }
        /* Exact counts: arcs into the same (label, cell, direction) accumulate (spec 10:
         * "duplicate updates accumulate exactly"). */
        canon_stable_sort(sig, len, sizeof *sig, s->entries_tmp, entry_cmp, NULL);
        size_t out = 0;
        for (size_t i = 0; i < len; ++i) {
            if (out > 0 && sig[out - 1].index == sig[i].index) {
                if (!canon_u64_add(sig[out - 1].count, sig[i].count, &sig[out - 1].count)) {
                    return CANON_CAPACITY_LIMIT; /* count-bit limit 64 (detailed plan 2.1) */
                }
            } else {
                sig[out++] = sig[i];
            }
        }
        s->pt.sig_len[v] = out;
    }
    return CANON_COMPLETE;
}

typedef struct vertex_cmp_ctx {
    const canon_p1_scratch *s;
    const canon_graph *g;
} vertex_cmp_ctx;

static int vertex_cmp(const void *x, const void *y, void *ctx)
{
    const vertex_cmp_ctx *c = ctx;
    size_t la = 0, lb = 0;
    const canon_p1_sig_entry *a = canon_p1_signature(c->s, c->g, *(const uint32_t *)x, &la);
    const canon_p1_sig_entry *b = canon_p1_signature(c->s, c->g, *(const uint32_t *)y, &lb);
    return canon_p1_sig_compare(a, la, b, lb);
}

canon_status canon_p1_stage_o(canon_partition *p, const canon_root *x, canon_p1_scratch *s)
{
    /* spec 7.1: "for every other root, sig(v) is the empty vector", so split is the identity. */
    if (x->kind != CANON_ROOT_GRAPH) {
        return CANON_COMPLETE;
    }
    const canon_graph *g = &x->u.graph;
    if (g->e == 0) {
        return CANON_COMPLETE; /* no labels: every signature is the empty vector */
    }
    canon_status st = canon_p1_graph_signatures(p, g, s);
    if (st != CANON_COMPLETE) {
        return st;
    }
    /* Rank the distinct signatures in increasing lexicographic order over all vertices, then
     * split with the ranks as keys: split orders the classes of each cell by increasing key and
     * the ranks respect the global signature order, so this is spec 7.1's "nonempty signature
     * classes in increasing lexicographic signature order".  Signatures are sorted by exact
     * comparison, never by hash (spec 10). */
    const uint32_t n = p->n;
    for (uint32_t v = 0; v < n; ++v) {
        s->pt.order[v] = v;
    }
    vertex_cmp_ctx ctx = {s, g};
    canon_stable_sort(s->pt.order, n, sizeof *s->pt.order, s->pt.order_tmp, vertex_cmp, &ctx);
    uint32_t rank = 0;
    for (uint32_t i = 0; i < n; ++i) {
        if (i > 0 && vertex_cmp(&s->pt.order[i - 1], &s->pt.order[i], &ctx) != 0) {
            ++rank; /* at most n - 1 distinct ranks: fits uint32 */
        }
        s->pt.sig[s->pt.order[i]] = rank;
    }
    (void)canon_partition_split(p, s->pt.sig);
    return CANON_COMPLETE;
}

canon_status canon_p1_refine_node(canon_partition *p, const canon_group *g, const canon_root *x,
                                  uint32_t depth, canon_buf *trace, canon_p1_scratch *s)
{
    return canon_p1_refine_node_rec(p, g, x, depth, trace, s, NULL);
}

canon_status canon_p1_refine_node_rec(canon_partition *p, const canon_group *g,
                                      const canon_root *x, uint32_t depth, canon_buf *trace,
                                      canon_p1_scratch *s, const canon_p1_recorder *rec)
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

        /* spec 7.1 O stage: "P = split(P, sig); append STAGE_O(cell sizes of P)".  For a
         * non-graph root every signature is empty and the split is the identity; the STAGE_O
         * token is still appended. */
        st = canon_p1_stage_o(p, x, s);
        if (st != CANON_COMPLETE) {
            return st;
        }
        st = append_stage(trace, TOKEN_STAGE_O, p);
        if (st == CANON_COMPLETE && rec != NULL) {
            st = rec->stage_o(rec->ctx, p); /* S7 step 2: certificate sweep record */
        }
        if (st != CANON_COMPLETE) {
            return st;
        }

        /* spec 7.1 G stage: "F = singleton atoms in current partition order (not branch
         * order)"; G reads the post-O partition. */
        uint32_t f = 0;
        for (uint32_t i = 0; i < p->cells; ++i) {
            if (canon_partition_cell_size(p, i) == 1) {
                s->pt.fixed[f++] = p->lab[p->start[i]];
            }
        }
        /* spec 7.1: "M = lexicographically least F^G; choose any u in G with F^u=M; compute
         * G_M orbits; sort ...".  The backend returns orbit ids on target labels. */
        st = g->ops->tuple_min(g, s->pt.fixed, f, s->pt.u, s->pt.orbit);
        if (st != CANON_COMPLETE) {
            return st;
        }
        /* spec 7.1: "sig(v) = position of the orbit containing u[v]" (pull back through u^-1) */
        for (uint32_t v = 0; v < p->n; ++v) {
            s->pt.sig[v] = s->pt.orbit[s->pt.u[v]];
        }
        (void)canon_partition_split(p, s->pt.sig);
        st = append_stage(trace, TOKEN_STAGE_G, p);
        if (st == CANON_COMPLETE && rec != NULL) {
            st = rec->stage_g(rec->ctx, p, s->pt.fixed, f, s->pt.u, s->pt.orbit);
        }
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
