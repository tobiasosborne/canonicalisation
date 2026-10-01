/* Unpruned P1 canonical-image search for a top-level subset (spec sections 7.1-7.3, 11.1). */
#include "search/p1_tree.h"

#include <stdlib.h>
#include <string.h>

#include "arena/checked.h"
#include "encoding/subset_stream.h"
#include "perm/perm.h"

void canon_p1_search_init(canon_p1_search *s)
{
    memset(s, 0, sizeof *s);
    s->n = UINT32_MAX;
    canon_buf_init(&s->trace);
    canon_buf_init(&s->leaf_bytes);
    canon_buf_init(&s->best_trace);
    canon_buf_init(&s->best_bytes);
}

static void free_arrays(canon_p1_search *s)
{
    canon_partition_free(&s->part);
    free(s->scratch.fixed);
    free(s->scratch.u);
    free(s->scratch.orbit);
    free(s->scratch.sig);
    free(s->snaps);
    free(s->leaf_t);
    free(s->leaf_atoms);
    free(s->leaf_bits);
    free(s->best_t);
    s->scratch.fixed = s->scratch.u = s->scratch.orbit = s->scratch.sig = NULL;
    s->snaps = NULL;
    s->snap_cap = 0;
    s->leaf_t = s->leaf_atoms = s->best_t = NULL;
    s->leaf_bits = NULL;
    s->ready = false;
    s->n = UINT32_MAX;
}

void canon_p1_search_free(canon_p1_search *s)
{
    free_arrays(s);
    canon_buf_free(&s->trace);
    canon_buf_free(&s->leaf_bytes);
    canon_buf_free(&s->best_trace);
    canon_buf_free(&s->best_bytes);
}

/* Size the per-degree arrays (lazily, on first use of a degree). */
static canon_status prepare(canon_p1_search *s, uint32_t n)
{
    if (s->ready && s->n == n) {
        return CANON_COMPLETE;
    }
    free_arrays(s);
    size_t entries = n > 0 ? (size_t)n : 1, bytes = 0, bit_bytes = 0;
    size_t words = canon_bitset_words(n) > 0 ? canon_bitset_words(n) : 1;
    if (!canon_size_mul(entries, sizeof(uint32_t), &bytes) ||
        !canon_size_mul(words, sizeof(uint64_t), &bit_bytes)) {
        return CANON_CAPACITY_LIMIT; /* spec 11.1 */
    }
    canon_status st = canon_partition_init(&s->part, n);
    if (st != CANON_COMPLETE) {
        free_arrays(s);
        return st;
    }
    st = canon_partition_snapshot_words(n, &s->snap_words);
    if (st != CANON_COMPLETE) {
        free_arrays(s);
        return st;
    }
    s->scratch.fixed = malloc(bytes);
    s->scratch.u = malloc(bytes);
    s->scratch.orbit = malloc(bytes);
    s->scratch.sig = malloc(bytes);
    s->leaf_t = malloc(bytes);
    s->leaf_atoms = malloc(bytes);
    s->best_t = malloc(bytes);
    s->leaf_bits = malloc(bit_bytes);
    if (s->scratch.fixed == NULL || s->scratch.u == NULL || s->scratch.orbit == NULL ||
        s->scratch.sig == NULL || s->leaf_t == NULL || s->leaf_atoms == NULL ||
        s->best_t == NULL || s->leaf_bits == NULL) {
        free_arrays(s);
        return CANON_RESOURCE_LIMIT;
    }
    s->n = n;
    s->ready = true;
    return CANON_COMPLETE;
}

/* Snapshot slot for `depth`, growing the stack geometrically (spec 11.2 rollback by snapshot,
 * O(n * depth) words).  The returned pointer is invalidated by the next growth. */
static canon_status snapshot_slot(canon_p1_search *s, uint32_t depth, uint32_t **slot)
{
    if ((size_t)depth >= s->snap_cap) {
        size_t cap = s->snap_cap == 0 ? 4 : s->snap_cap;
        while (cap <= (size_t)depth) {
            if (!canon_size_mul(cap, 2u, &cap)) {
                return CANON_CAPACITY_LIMIT;
            }
        }
        size_t bytes = 0;
        if (!canon_size_mul3(cap, s->snap_words, sizeof(uint32_t), &bytes)) {
            return CANON_CAPACITY_LIMIT;
        }
        uint32_t *grown = realloc(s->snaps, bytes);
        if (grown == NULL) {
            return CANON_RESOURCE_LIMIT;
        }
        s->snaps = grown;
        s->snap_cap = cap;
    }
    *slot = s->snaps + (size_t)depth * s->snap_words;
    return CANON_COMPLETE;
}

/* spec 7.2 leaf map: L = singleton order; t_L the unique element minimising L^G; image x^t_L;
 * key (complete trace, CDAG-2 bytes).  Keep the least key; on an equal key keep the
 * lexicographically smaller witness (spec 7.4 preamble). */
static canon_status evaluate_leaf(canon_p1_search *s, const canon_group *g, const canon_subset *x)
{
    const uint32_t n = s->n;
    /* spec 7.2: "At a leaf extract L from the partition's singleton order."  Every cell is a
     * singleton, so lab is L. */
    canon_status st = g->ops->tuple_min(g, s->part.lab, n, s->leaf_t, NULL);
    if (st != CANON_COMPLETE) {
        return st;
    }
    /* spec 2.1: x^t = {t[a] : a in x}; spec 4.1/4.2: its CDAG-2 stream. */
    canon_subset_act_sorted(x, s->leaf_t, s->leaf_bits, s->leaf_atoms);
    canon_buf_truncate(&s->leaf_bytes, 0);
    st = canon_subset_stream_write(&s->leaf_bytes, n, s->leaf_atoms, x->k);
    if (st != CANON_COMPLETE) {
        return st;
    }
    int better = !s->have_best;
    if (!better) {
        /* spec 7.2: "The objective is the lexicographic pair (complete trace, CDAG-2 bytes of
         * x^t_L), with trace compared first"; spec 4.3 byte order, proper prefix smaller. */
        int c = canon_bytes_compare(s->trace.data, s->trace.len, s->best_trace.data,
                                    s->best_trace.len);
        if (c == 0) {
            c = canon_bytes_compare(s->leaf_bytes.data, s->leaf_bytes.len, s->best_bytes.data,
                                    s->best_bytes.len);
        }
        if (c == 0) {
            /* spec 7.4 preamble: "minimise among the witnesses that actually attain that key"
             * (S1 reports the least leaf witness; see canon_result_witness). */
            c = canon_perm_lex_compare(s->leaf_t, s->best_t, n);
        }
        better = c < 0;
    }
    if (!better) {
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
static canon_status visit(canon_p1_search *s, const canon_group *g, const canon_subset *x,
                          uint32_t depth, uint64_t max_nodes)
{
    /* spec 11.1: the logical work quota counts NODE tokens of the fixed reference traversal;
     * abandon when the count would exceed it. */
    if (s->nodes >= max_nodes) {
        return CANON_CAPACITY_LIMIT;
    }
    s->nodes += 1;
    size_t mark = s->trace.len;
    canon_status st = canon_p1_refine_node(&s->part, g, depth, &s->trace, &s->scratch);
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
    const size_t lab_offset = 1u + (size_t)s->n + 1u; /* lab inside a snapshot */
    /* spec 7.1: "for EACH a in that cell: replace cell C in place by [{a}, C minus {a}];
     * recurse at depth+1, with a fresh node refinement loop".  Members are taken in the cell's
     * current (non-semantic) order from the snapshot. */
    for (uint32_t j = lo; j < hi; ++j) {
        snap = s->snaps + (size_t)depth * s->snap_words; /* the stack may have moved */
        uint32_t a = snap[lab_offset + j];
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

canon_status canon_p1_search_subset(canon_p1_search *s, const canon_group *g,
                                    const canon_subset *x, uint64_t max_nodes)
{
    if (g->degree != x->n) {
        return CANON_INVALID_INPUT;
    }
    canon_status st = prepare(s, x->n);
    if (st != CANON_COMPLETE) {
        return st;
    }
    s->have_best = false;
    s->nodes = 0;
    canon_buf_truncate(&s->trace, 0);
    canon_buf_truncate(&s->best_trace, 0);
    canon_buf_truncate(&s->best_bytes, 0);
    /* spec 7.1 root: initial key partition; spec 7.2: n = 0 gives an empty list and the root is
     * a leaf. */
    canon_p1_initial_subset(&s->part, x, &s->scratch);
    st = visit(s, g, x, 0, max_nodes);
    if (st != CANON_COMPLETE) {
        s->have_best = false;
        return st;
    }
    if (!s->have_best) {
        return CANON_INTERNAL_ERROR; /* the unpruned tree always has a leaf (spec 7.2) */
    }
    return CANON_COMPLETE;
}
