/* Unpruned P1 canonical-image search for a root object (spec sections 7.1-7.3, 11.1). */
#include "search/p1_tree.h"

#include <stdlib.h>
#include <string.h>

#include "arena/alloc.h"
#include "arena/checked.h"
#include "perm/perm.h"

void canon_p1_search_init(canon_p1_search *s)
{
    memset(s, 0, sizeof *s);
    canon_p1_scratch_init(&s->scratch);
    canon_root_image_init(&s->image);
    canon_buf_init(&s->trace);
    canon_buf_init(&s->leaf_bytes);
    canon_buf_init(&s->best_trace);
    canon_buf_init(&s->best_bytes);
}

static void free_arrays(canon_p1_search *s)
{
    canon_partition_free(&s->part);
    free(s->snaps);
    free(s->leaf_t);
    free(s->best_t);
    s->snaps = NULL;
    s->snap_alloc = 0;
    s->leaf_t = s->best_t = NULL;
    s->ready = false;
    s->cap = 0;
    s->n = 0;
}

void canon_p1_search_free(canon_p1_search *s)
{
    free_arrays(s);
    canon_p1_scratch_free(&s->scratch);
    canon_root_image_free(&s->image);
    canon_buf_free(&s->trace);
    canon_buf_free(&s->leaf_bytes);
    canon_buf_free(&s->best_trace);
    canon_buf_free(&s->best_bytes);
}

/* Size the per-point arrays to a capacity that only grows: a solve of degree n <= cap reuses
 * them; a larger n reallocates them for n.  The snapshot stack keeps its words across degrees
 * (it is indexed by the current snap_words).  The refinement scratch and the leaf image storage
 * grow on their own (canon_p1_scratch_reserve, canon_root_act_into). */
static canon_status prepare(canon_p1_search *s, const canon_root *x)
{
    const uint32_t n = x->n;
    canon_status st = CANON_COMPLETE;
    if (!s->ready || n > s->cap) {
        uint32_t *snaps = s->snaps; /* keep the stack: snapshot_slot grows it as needed */
        size_t snap_alloc = s->snap_alloc;
        s->snaps = NULL;
        free_arrays(s);
        s->snaps = snaps;
        s->snap_alloc = snap_alloc;
        st = canon_partition_init(&s->part, n);
        if (st != CANON_COMPLETE) {
            free_arrays(s);
            return st;
        }
        s->leaf_t = canon_alloc_array(n, sizeof *s->leaf_t, &st);
        s->best_t = canon_alloc_array(n, sizeof *s->best_t, &st);
        if (s->leaf_t == NULL || s->best_t == NULL) {
            free_arrays(s);
            return st;
        }
        s->cap = n;
        s->ready = true;
    }
    st = canon_p1_scratch_reserve(&s->scratch, x);
    if (st != CANON_COMPLETE) {
        return st;
    }
    st = canon_partition_snapshot_words(n, &s->snap_words);
    if (st != CANON_COMPLETE) {
        return st;
    }
    canon_partition_set_degree(&s->part, n);
    s->n = n;
    return CANON_COMPLETE;
}

/* Snapshot slot for `depth`, growing the stack geometrically (spec 11.2 rollback by snapshot,
 * O(n * depth) words).  The returned pointer is invalidated by the next growth. */
static canon_status snapshot_slot(canon_p1_search *s, uint32_t depth, uint32_t **slot)
{
    size_t need = 0;
    if (!canon_size_mul((size_t)depth + 1u, s->snap_words, &need)) {
        return CANON_CAPACITY_LIMIT;
    }
    if (need > s->snap_alloc) {
        size_t words = s->snap_alloc < 64 ? 64 : s->snap_alloc;
        while (words < need) {
            if (!canon_size_mul(words, 2u, &words)) {
                return CANON_CAPACITY_LIMIT;
            }
        }
        size_t bytes = 0;
        if (!canon_size_mul(words, sizeof(uint32_t), &bytes)) {
            return CANON_CAPACITY_LIMIT;
        }
        uint32_t *grown = realloc(s->snaps, bytes);
        if (grown == NULL) {
            return CANON_RESOURCE_LIMIT;
        }
        s->snaps = grown;
        s->snap_alloc = words;
    }
    *slot = s->snaps + (size_t)depth * s->snap_words;
    return CANON_COMPLETE;
}

/* spec 7.2 leaf map: L = singleton order; t_L the unique element minimising L^G; image x^t_L;
 * key (complete trace, CDAG-2 bytes), trace compared first.  Keep the least key; on an equal
 * key keep the lexicographically smaller witness (spec 7.4 preamble). */
static canon_status evaluate_leaf(canon_p1_search *s, const canon_group *g, const canon_root *x)
{
    const uint32_t n = s->n;
    s->leaves += 1;
    /* spec 7.2: "The objective is the lexicographic pair (complete trace, CDAG-2 bytes of
     * x^t_L), with trace compared first"; spec 4.3 byte order, proper prefix smaller.  A leaf
     * whose trace is greater than the best one cannot win whatever its bytes, so t_L, the image
     * and its stream are only computed when the trace is less than or equal to the best. */
    int c = -1;
    if (s->have_best) {
        c = canon_bytes_compare(s->trace.data, s->trace.len, s->best_trace.data,
                                s->best_trace.len);
        if (c > 0) {
            return CANON_COMPLETE;
        }
    }
    /* spec 7.2: "At a leaf extract L from the partition's singleton order."  Every cell is a
     * singleton, so lab is L. */
    canon_status st = g->ops->tuple_min(g, s->part.lab, n, s->leaf_t, NULL);
    if (st != CANON_COMPLETE) {
        return st;
    }
    /* spec 2.1: the image x^t (subset: {t[a] : a in x}; graph: vertices and arcs relabelled
     * by t, re-sorted); spec 4.1/4.2: its CDAG-2 stream. */
    st = canon_root_act_into(x, s->leaf_t, &s->image);
    if (st != CANON_COMPLETE) {
        return st;
    }
    s->images += 1;
    canon_buf_truncate(&s->leaf_bytes, 0);
    st = canon_root_stream_write(&s->image.root, &s->leaf_bytes);
    if (st != CANON_COMPLETE) {
        return st;
    }
    if (c == 0) {
        /* equal traces: compare the CDAG-2 bytes, then (spec 7.4 preamble: "minimise among the
         * witnesses that actually attain that key") the witness; S1 reports the least leaf
         * witness, see canon_result_witness. */
        c = canon_bytes_compare(s->leaf_bytes.data, s->leaf_bytes.len, s->best_bytes.data,
                                s->best_bytes.len);
        if (c == 0) {
            c = canon_perm_lex_compare(s->leaf_t, s->best_t, n);
        }
    }
    if (c >= 0) {
        return CANON_COMPLETE;
    }
    canon_buf_truncate(&s->best_trace, 0);
    canon_buf_truncate(&s->best_bytes, 0);
    st = canon_buf_put_bytes(&s->best_trace, s->trace.data, s->trace.len);
    if (st == CANON_COMPLETE) {
        st = canon_buf_put_bytes(&s->best_bytes, s->leaf_bytes.data, s->leaf_bytes.len);
    }
    if (st != CANON_COMPLETE) {
        s->have_best = false;
        return st;
    }
    if (n > 0) {
        memcpy(s->best_t, s->leaf_t, (size_t)n * sizeof *s->best_t);
    }
    s->have_best = true;
    return CANON_COMPLETE;
}

/* spec 7.1: one node of the unpruned tree; the partition on entry is the node's partition
 * before refinement.  Recursion depth is bounded by the number of individualisations, at most
 * n - 1 (spec 7.2 termination). */
static canon_status visit(canon_p1_search *s, const canon_group *g, const canon_root *x,
                          uint32_t depth, uint64_t max_nodes)
{
    /* spec 11.1: the logical work quota counts NODE tokens of the fixed reference traversal;
     * abandon when the count would exceed it. */
    if (s->nodes >= max_nodes) {
        return CANON_CAPACITY_LIMIT;
    }
    s->nodes += 1;
    size_t mark = s->trace.len;
    canon_status st = canon_p1_refine_node(&s->part, g, x, depth, &s->trace, &s->scratch);
    if (st != CANON_COMPLETE) {
        return st;
    }
    if (canon_partition_is_discrete(&s->part)) {
        /* spec 7.1: "if all cells singleton: append LEAF; evaluate §7.2" */
        st = canon_p1_trace_leaf(&s->trace);
        if (st == CANON_COMPLETE) {
            st = evaluate_leaf(s, g, x);
        }
        canon_buf_truncate(&s->trace, mark);
        return st;
    }
    /* spec 7.1: "choose cell minimising (cell size, cell position), among size > 1" */
    uint32_t target = UINT32_MAX, best_size = UINT32_MAX;
    for (uint32_t i = 0; i < s->part.cells; ++i) {
        uint32_t size = canon_partition_cell_size(&s->part, i);
        if (size > 1 && size < best_size) {
            best_size = size;
            target = i;
        }
    }
    uint32_t *snap = NULL;
    st = snapshot_slot(s, depth, &snap);
    if (st != CANON_COMPLETE) {
        return st;
    }
    canon_partition_save(&s->part, snap);
    const uint32_t lo = s->part.start[target], hi = s->part.start[target + 1];
    /* spec 7.1: "for EACH a in that cell: replace cell C in place by [{a}, C minus {a}];
     * recurse at depth+1, with a fresh node refinement loop".  Members are taken in the cell's
     * current (non-semantic) order from the snapshot. */
    for (uint32_t j = lo; j < hi; ++j) {
        snap = s->snaps + (size_t)depth * s->snap_words; /* the stack may have moved */
        uint32_t a = canon_partition_snapshot_lab(snap, s->n)[j];
        canon_partition_restore(&s->part, snap); /* spec 11.2 rollback */
        canon_partition_individualise(&s->part, target, a);
        if (depth == UINT32_MAX) {
            return CANON_CAPACITY_LIMIT; /* U32(d) must fit; unreachable for n <= 2^32 - 1 */
        }
        st = visit(s, g, x, depth + 1, max_nodes);
        if (st != CANON_COMPLETE) {
            return st;
        }
    }
    canon_buf_truncate(&s->trace, mark);
    return CANON_COMPLETE;
}

canon_status canon_p1_search_run(canon_p1_search *s, const canon_group *g, const canon_root *x,
                                 uint64_t max_nodes)
{
    if (g->degree != x->n) {
        return CANON_INVALID_INPUT;
    }
    canon_status st = prepare(s, x);
    if (st != CANON_COMPLETE) {
        return st;
    }
    s->have_best = false;
    s->nodes = 0;
    s->leaves = 0;
    s->images = 0;
    canon_buf_truncate(&s->trace, 0);
    canon_buf_truncate(&s->best_trace, 0);
    canon_buf_truncate(&s->best_bytes, 0);
    /* spec 7.1 root: initial key partition; spec 7.2: n = 0 gives an empty list and the root is
     * a leaf. */
    canon_p1_initial(&s->part, x, &s->scratch);
    st = visit(s, g, x, 0, max_nodes);
    /* Invariant (p1_tree.h): between runs nothing in the workspace points into x; the graph
     * image borrowed x's tables, so drop them now, on every outcome. */
    canon_root_image_clear(&s->image);
    if (st != CANON_COMPLETE) {
        s->have_best = false;
        return st;
    }
    if (!s->have_best) {
        return CANON_INTERNAL_ERROR; /* the unpruned tree always has a leaf (spec 7.2) */
    }
    return CANON_COMPLETE;
}
