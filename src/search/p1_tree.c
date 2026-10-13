/* P1 canonical-image search for a root object (spec sections 7.1-7.3, 11.1): the unpruned
 * reference traversal (slices S1, S2) and, from slice S7, orbit pruning by prefix-fixing
 * automorphisms (docs/pruning-rules.md) with certificate emission (src/search/certificate.h). */
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
    canon_symmetry_init(&s->sym);
}

static void free_arrays(canon_p1_search *s)
{
    canon_partition_free(&s->part);
    free(s->snaps);
    free(s->leaf_t); /* one block: leaf_t, best_t, prefix, aut */
    s->snaps = NULL;
    s->snap_alloc = 0;
    s->leaf_t = s->best_t = s->prefix = s->aut = NULL;
    s->ready = false;
    s->cap = 0;
    s->n = 0;
}

void canon_p1_search_free(canon_p1_search *s)
{
    free_arrays(s);
    free(s->reps);
    s->reps = NULL;
    s->reps_alloc = 0;
    canon_p1_scratch_free(&s->scratch);
    canon_root_image_free(&s->image);
    canon_buf_free(&s->trace);
    canon_buf_free(&s->leaf_bytes);
    canon_buf_free(&s->best_trace);
    canon_buf_free(&s->best_bytes);
    canon_symmetry_free(&s->sym);
}

/* Size the per-point arrays to a capacity that only grows: a solve of degree n <= cap reuses
 * them; a larger n reallocates them for n.  The snapshot and decision stacks keep their words
 * across degrees (they are indexed by the current degree).  The refinement scratch and the
 * leaf image storage grow on their own (canon_p1_scratch_reserve, canon_root_act_into). */
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
        size_t words = 0;
        if (!canon_size_mul((size_t)n, 4u, &words)) {
            free_arrays(s);
            return CANON_CAPACITY_LIMIT; /* spec 11.1 */
        }
        uint32_t *block = canon_alloc_array(words, sizeof *block, &st);
        if (block == NULL) {
            free_arrays(s);
            return st;
        }
        s->leaf_t = block;
        s->best_t = block + (size_t)n;
        s->prefix = block + 2u * (size_t)n;
        s->aut = block + 3u * (size_t)n;
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

/* Grow a per-depth stack of `words` words per depth to hold depth + 1 slots (geometric growth,
 * spec 11.2: O(n * depth) words).  Pointers into the stack are invalidated by growth. */
static canon_status stack_slot(uint32_t **stack, size_t *alloc, size_t words, uint32_t depth,
                               uint32_t **slot)
{
    size_t need = 0;
    if (!canon_size_mul((size_t)depth + 1u, words, &need)) {
        return CANON_CAPACITY_LIMIT;
    }
    if (need > *alloc) {
        size_t grown_words = *alloc < 64 ? 64 : *alloc;
        while (grown_words < need) {
            if (!canon_size_mul(grown_words, 2u, &grown_words)) {
                return CANON_CAPACITY_LIMIT;
            }
        }
        size_t bytes = 0;
        if (!canon_size_mul(grown_words, sizeof(uint32_t), &bytes)) {
            return CANON_CAPACITY_LIMIT;
        }
        uint32_t *grown = realloc(*stack, bytes);
        if (grown == NULL) {
            return CANON_RESOURCE_LIMIT;
        }
        *stack = grown;
        *alloc = grown_words;
    }
    *slot = *stack + (size_t)depth * words;
    return CANON_COMPLETE;
}

/* Snapshot slot for `depth` (spec 11.2 rollback by snapshot). */
static canon_status snapshot_slot(canon_p1_search *s, uint32_t depth, uint32_t **slot)
{
    return stack_slot(&s->snaps, &s->snap_alloc, s->snap_words, depth, slot);
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
     * and its stream are only computed when the trace is less than or equal to the best.  A
     * certificate records t_L at every leaf (S7 brief 3.3), so then t_L is always computed. */
    int c = -1;
    if (s->have_best) {
        c = canon_bytes_compare(s->trace.data, s->trace.len, s->best_trace.data,
                                s->best_trace.len);
        if (c > 0 && (s->cert == NULL || !s->cert->active)) {
            return CANON_COMPLETE;
        }
    }
    /* spec 7.2: "At a leaf extract L from the partition's singleton order."  Every cell is a
     * singleton, so lab is L. */
    canon_status st = g->ops->tuple_min(g, s->part.lab, n, s->leaf_t, NULL);
    if (st != CANON_COMPLETE) {
        return st;
    }
    if (s->cert != NULL) {
        st = canon_cert_leaf(s->cert, s->leaf_t);
        if (st != CANON_COMPLETE) {
            return st;
        }
    }
    if (c > 0) {
        return CANON_COMPLETE; /* the trace alone decided */
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

/* Decide the children of the node at `depth` whose target cell holds the `size` points
 * cell[0..size-1] (the snapshot's member order).  Without the prune every child is explored.
 * With it (docs/pruning-rules.md, S7 brief 3.1 rule): A_S, the pointwise stabiliser of the
 * node's prefix in A_known, acts on the cell; the first point of each A_S-orbit in cell order
 * is explored and the others are pruned, each with its representative and, for the
 * certificate, an explicit automorphism a of A_S with representative^a = b.  The decisions go
 * to the depth's slot of s->reps (2 words per cell position: representative or
 * CANON_CERT_NONE, automorphism index) BEFORE any child is visited, because the child visits
 * reuse the symmetry scratch. */
static canon_status decide_children(canon_p1_search *s, uint32_t depth, const uint32_t *cell,
                                    uint32_t size, uint32_t **slot_out)
{
    size_t words = 0;
    if (!canon_size_mul((size_t)s->n, 2u, &words)) {
        return CANON_CAPACITY_LIMIT;
    }
    uint32_t *slot = NULL;
    canon_status st = stack_slot(&s->reps, &s->reps_alloc, words, depth, &slot);
    if (st != CANON_COMPLETE) {
        return st;
    }
    *slot_out = slot;
    const bool prune = s->prune && !canon_symmetry_trivial(&s->sym);
    if (prune) {
        /* spec 7.3 "At each node intersect with the stabiliser of its constraints before
         * sibling pruning": A_S from A_known and the prefix (a_1..a_d) */
        st = canon_symmetry_stabiliser(&s->sym, s->prefix, depth);
        if (st != CANON_COMPLETE) {
            return st;
        }
        canon_symmetry_cell_orbits(&s->sym, cell, size);
    }
    for (uint32_t j = 0; j < size; ++j) {
        const uint32_t b = cell[j];
        const uint32_t rep = prune ? canon_symmetry_rep(&s->sym, b) : b;
        slot[2u * j] = rep == b ? CANON_CERT_NONE : rep;
        slot[2u * j + 1u] = CANON_CERT_NONE;
        if (rep != b && s->cert != NULL && s->cert->active) {
            /* the certificate names one automorphism per pruned child (S7 brief 3.3): the
             * Schreier-tree product with rep^a = b, which fixes the prefix pointwise */
            canon_symmetry_transporter(&s->sym, b, s->aut);
            st = canon_cert_automorphism(s->cert, s->aut, &slot[2u * j + 1u]);
            if (st != CANON_COMPLETE) {
                return st;
            }
        }
    }
    return CANON_COMPLETE;
}

/* spec 7.1: one node of the tree; the partition on entry is the node's partition before
 * refinement.  Recursion depth is bounded by the number of individualisations, at most n - 1
 * (spec 7.2 termination). */
static canon_status visit(canon_p1_search *s, const canon_group *g, const canon_root *x,
                          uint32_t depth, uint64_t max_nodes)
{
    /* spec 11.1: the logical work quota counts the NODE tokens of the traversal the work
     * policy names (every node of the reference tree, or the nodes explored under the prune);
     * abandon when the count would exceed it. */
    if (s->nodes >= max_nodes) {
        return CANON_CAPACITY_LIMIT;
    }
    s->nodes += 1;
    size_t mark = s->trace.len;
    canon_status st = CANON_COMPLETE;
    canon_p1_observer obs;
    const canon_p1_observer *obs_ptr = NULL;
    if (s->cert != NULL) {
        st = canon_cert_node_begin(s->cert, depth, depth > 0 ? s->prefix[depth - 1] : CANON_CERT_NONE);
        if (st != CANON_COMPLETE) {
            return st;
        }
        obs = canon_cert_observer(s->cert);
        obs_ptr = &obs;
    }
    st = canon_p1_refine_node(&s->part, g, x, depth, &s->trace, &s->scratch, obs_ptr);
    if (st == CANON_COMPLETE && s->cert != NULL) {
        st = canon_cert_node_refined(s->cert);
    }
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
    if (depth == UINT32_MAX) {
        return CANON_CAPACITY_LIMIT; /* U32(d) must fit; unreachable for n <= 2^32 - 1 */
    }
    uint32_t *decisions = NULL;
    st = decide_children(s, depth, canon_partition_snapshot_lab(snap, s->n) + lo, hi - lo,
                         &decisions);
    if (st != CANON_COMPLETE) {
        return st;
    }
    if (s->cert != NULL) {
        st = canon_cert_branch(s->cert, target, hi - lo);
        if (st != CANON_COMPLETE) {
            return st;
        }
    }
    /* spec 7.1: "for EACH a in that cell: replace cell C in place by [{a}, C minus {a}];
     * recurse at depth+1, with a fresh node refinement loop".  Members are taken in the cell's
     * current (non-semantic) order from the snapshot; a pruned member is recorded and skipped
     * (docs/pruning-rules.md: its subtree's keys are those of its representative's). */
    for (uint32_t j = lo; j < hi; ++j) {
        snap = s->snaps + (size_t)depth * s->snap_words; /* the stacks may have moved */
        decisions = s->reps + (size_t)depth * 2u * (size_t)s->n;
        uint32_t a = canon_partition_snapshot_lab(snap, s->n)[j];
        const uint32_t rep = decisions[2u * (j - lo)], aut = decisions[2u * (j - lo) + 1u];
        if (rep != CANON_CERT_NONE) {
            s->pruned += 1;
            if (s->cert != NULL) {
                st = canon_cert_child_pruned(s->cert, a, rep, aut);
                if (st != CANON_COMPLETE) {
                    return st;
                }
            }
            continue;
        }
        s->explored += 1;
        if (s->cert != NULL) {
            st = canon_cert_child_explored(s->cert, a);
            if (st != CANON_COMPLETE) {
                return st;
            }
        }
        canon_partition_restore(&s->part, snap); /* spec 11.2 rollback */
        canon_partition_individualise(&s->part, target, a);
        s->prefix[depth] = a;
        st = visit(s, g, x, depth + 1, max_nodes);
        if (st != CANON_COMPLETE) {
            return st;
        }
    }
    canon_buf_truncate(&s->trace, mark);
    return CANON_COMPLETE;
}

/* spec 7.3 source (i) of A_known (S7 brief 3.1): every input generator of G that fixes x,
 * checked by exact action, is inserted into the verified subgroup (and listed in the
 * certificate).  Generators that move x are skipped. */
static canon_status root_automorphisms(canon_p1_search *s, const canon_group *g,
                                       const canon_root *x)
{
    for (uint32_t k = 0; k < g->gens.count; ++k) {
        const uint32_t *gen = canon_perm_table_row(&g->gens, k);
        bool inserted = false;
        canon_status st = canon_symmetry_insert(&s->sym, x, &s->image, gen, &inserted);
        if (st == CANON_INVALID_INPUT) {
            continue; /* x^gen != x: not an automorphism */
        }
        if (st != CANON_COMPLETE) {
            return st;
        }
        if (inserted && s->cert != NULL) {
            uint32_t index = 0;
            st = canon_cert_automorphism(s->cert, gen, &index);
            if (st != CANON_COMPLETE) {
                return st;
            }
        }
    }
    return CANON_COMPLETE;
}

canon_status canon_p1_search_run(canon_p1_search *s, const canon_group *g, const canon_root *x,
                                 uint64_t max_nodes, const canon_p1_run_options *opt)
{
    if (g->degree != x->n) {
        return CANON_INVALID_INPUT;
    }
    canon_status st = prepare(s, x);
    if (st != CANON_COMPLETE) {
        return st;
    }
    s->prune = opt != NULL && opt->prune;
    s->cert = opt != NULL ? opt->cert : NULL;
    s->have_best = false;
    s->nodes = 0;
    s->leaves = 0;
    s->images = 0;
    s->pruned = 0;
    s->explored = 0;
    canon_buf_truncate(&s->trace, 0);
    canon_buf_truncate(&s->best_trace, 0);
    canon_buf_truncate(&s->best_bytes, 0);
    st = canon_symmetry_reset(&s->sym, x->n);
    if (st == CANON_COMPLETE && (s->prune || s->cert != NULL)) {
        /* A_known is needed by the prune; the certificate lists its generators too.  A
         * certificate writer that has already failed records nothing. */
        st = root_automorphisms(s, g, x);
    }
    if (st == CANON_COMPLETE) {
        /* spec 7.1 root: initial key partition; spec 7.2: n = 0 gives an empty list and the
         * root is a leaf. */
        canon_p1_initial(&s->part, x, &s->scratch);
        st = visit(s, g, x, 0, max_nodes);
    }
    if (st == CANON_COMPLETE && s->cert != NULL && s->have_best) {
        st = canon_cert_finish(s->cert, s->best_trace.data, s->best_trace.len,
                               s->best_bytes.data, s->best_bytes.len, s->best_t, s->nodes);
    }
    /* Invariant (p1_tree.h): between runs nothing in the workspace points into x; the graph
     * image borrowed x's tables, so drop them now, on every outcome. */
    canon_root_image_clear(&s->image);
    if (st != CANON_COMPLETE) {
        s->have_best = false;
        return st;
    }
    if (!s->have_best) {
        return CANON_INTERNAL_ERROR; /* the tree always has an explored leaf (spec 7.2) */
    }
    return CANON_COMPLETE;
}
