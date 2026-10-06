/* P1 canonical-image search for a root object (spec sections 7.1-7.3, 11.1): the unpruned
 * reference traversal, and (slice S7 step 1, work policy 0x0002) the same traversal with
 * orbit pruning by verified automorphisms fixing the node's prefix (docs/pruning-rules.md). */
#include "search/p1_tree.h"

#include <stdlib.h>
#include <string.h>

#include "arena/alloc.h"
#include "arena/checked.h"
#include "perm/perm.h"
#include "util/sort.h"

void canon_p1_search_init(canon_p1_search *s)
{
    memset(s, 0, sizeof *s);
    canon_p1_scratch_init(&s->scratch);
    canon_root_image_init(&s->image);
    canon_buf_init(&s->trace);
    canon_buf_init(&s->leaf_bytes);
    canon_buf_init(&s->best_trace);
    canon_buf_init(&s->best_bytes);
    canon_symmetry_init(&s->known);
}

static void free_arrays(canon_p1_search *s)
{
    canon_partition_free(&s->part);
    free(s->snaps);
    free(s->leaf_t);
    free(s->best_t);
    free(s->aut);
    s->snaps = NULL;
    s->snap_alloc = 0;
    s->leaf_t = s->best_t = s->aut = NULL;
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
    canon_symmetry_free(&s->known);
    free(s->reps);
    s->reps = NULL;
    s->reps_top = s->reps_cap = 0;
}

/* Room for `need` words in the explored-children stack (grow-only, geometric; the stack may
 * move, so callers index it, never keep pointers across a call). */
static canon_status reps_reserve(canon_p1_search *s, size_t need)
{
    if (need <= s->reps_cap) {
        return CANON_COMPLETE;
    }
    size_t words = s->reps_cap < 64 ? 64 : s->reps_cap;
    while (words < need) {
        if (!canon_size_mul(words, 2u, &words)) {
            return CANON_CAPACITY_LIMIT;
        }
    }
    size_t bytes = 0;
    if (!canon_size_mul(words, sizeof(uint32_t), &bytes)) {
        return CANON_CAPACITY_LIMIT;
    }
    uint32_t *grown = realloc(s->reps, bytes);
    if (grown == NULL) {
        return CANON_RESOURCE_LIMIT;
    }
    s->reps = grown;
    s->reps_cap = words;
    return CANON_COMPLETE;
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
        s->aut = canon_alloc_array(n, sizeof *s->aut, &st);
        if (s->leaf_t == NULL || s->best_t == NULL || s->aut == NULL) {
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
    s->leaf_t_valid = false;
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
    s->leaf_t_valid = true;
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

static int u32_cmp(const void *a, const void *b, void *ctx)
{
    (void)ctx;
    const uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
    return (x > y) - (x < y);
}

/* spec 7.1: one node of the tree; the partition on entry is the node's partition before
 * refinement.  Recursion depth is bounded by the number of individualisations, at most n - 1
 * (spec 7.2 termination).  `atom` is the atom individualised to reach the node (UINT32_MAX at
 * the root; reported to the recorder only).  `prune` (S7, policy 0x0002 only): H_depth, the
 * pointwise stabiliser of this node's prefix in A_known, is nontrivial and valid in s->known;
 * false reproduces the unpruned node exactly. */
static canon_status visit(canon_p1_search *s, const canon_group *g, const canon_root *x,
                          uint32_t depth, uint32_t atom, uint64_t max_nodes, bool prune)
{
    /* spec 11.1: the logical work quota counts NODE tokens of the traversal fixed by the work
     * policy (S7: the nodes explored; a pruned child is never entered, so never counted);
     * abandon when the count would exceed it. */
    if (s->nodes >= max_nodes) {
        return CANON_CAPACITY_LIMIT;
    }
    s->nodes += 1;
    const canon_p1_recorder *rec = s->rec;
    canon_status st = CANON_COMPLETE;
    if (rec != NULL) {
        st = rec->node(rec->ctx, depth, atom);
        if (st != CANON_COMPLETE) {
            return st;
        }
    }
    size_t mark = s->trace.len;
    st = canon_p1_refine_node_rec(&s->part, g, x, depth, &s->trace, &s->scratch, rec);
    if (st != CANON_COMPLETE) {
        return st;
    }
    if (canon_partition_is_discrete(&s->part)) {
        /* spec 7.1: "if all cells singleton: append LEAF; evaluate §7.2" */
        st = canon_p1_trace_leaf(&s->trace);
        if (st == CANON_COMPLETE) {
            st = evaluate_leaf(s, g, x);
        }
        if (st == CANON_COMPLETE && rec != NULL) {
            st = rec->leaf(rec->ctx, s->leaf_t_valid ? s->leaf_t : NULL);
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
    const uint32_t len = hi - lo;
    if (rec != NULL) {
        st = rec->branch(rec->ctx, target, len);
        if (st != CANON_COMPLETE) {
            return st;
        }
    }
    /* The node's slice of the explored-children stack: members[0..len) the target cell's
     * members in increasing atom id, rep_of[0..len) the representative of each.  The stack
     * may move during the recursion below, so it is indexed, never held by pointer. */
    const size_t mark_reps = s->reps_top;
    st = reps_reserve(s, mark_reps + 2u * (size_t)len);
    if (st != CANON_COMPLETE) {
        return st;
    }
    s->reps_top = mark_reps + 2u * (size_t)len;
    s->children += (uint64_t)len;
    snap = s->snaps + (size_t)depth * s->snap_words;
    uint32_t *members = s->reps + mark_reps, *rep_of = members + len;
    memcpy(members, canon_partition_snapshot_lab(snap, s->n) + lo, (size_t)len * sizeof *members);
    /* spec 7.1 "for EACH a in that cell" with "Internal order of members within a cell is not
     * semantic": children are taken in increasing atom id (S7 step 2, one traversal with or
     * without a certificate recorder; the key minimum, the least attaining witness, the node
     * count and the quota verdict do not depend on the order).  rep_of is the sort scratch. */
    canon_stable_sort(members, len, sizeof *members, rep_of, u32_cmp, NULL);
    if (prune) {
        /* docs/pruning-rules.md (S7 brief 3.1 lemma and rule; spec 7.3 "At each node intersect
         * with the stabiliser of its constraints before sibling pruning"): for every a in
         * H_depth and b in the target cell C, the subtrees at S.b and S.b^a carry the same leaf
         * keys, so exploring one child per H_depth-orbit on C (the numerically least member)
         * keeps the minimum key. */
        st = canon_symmetry_orbit_map(&s->known, depth, members, len, rep_of);
        if (st != CANON_COMPLETE) {
            return st;
        }
        /* counted here, before the recursion, as since step 1 */
        for (uint32_t j = 0; j < len; ++j) {
            s->pruned += rep_of[j] != members[j] ? 1u : 0u;
        }
    } else if (len > 0) {
        memcpy(rep_of, members, (size_t)len * sizeof *rep_of);
    }
    /* spec 7.1: "replace cell C in place by [{a}, C minus {a}]; recurse at depth+1, with a
     * fresh node refinement loop" */
    for (uint32_t j = 0; j < len; ++j) {
        const uint32_t a = s->reps[mark_reps + j];
        const uint32_t r = s->reps[mark_reps + len + j];
        if (r != a) {
            /* pruned (counted above): a = r^h for some h in H_depth (r is the least member of
             * a's orbit, an earlier explored child) */
            if (rec != NULL) {
                st = canon_symmetry_transporter(&s->known, depth, r, a, s->aut);
                if (st == CANON_COMPLETE) {
                    st = rec->pruned(rec->ctx, depth, a, r, s->aut);
                }
                if (st != CANON_COMPLETE) {
                    return st;
                }
            }
            continue;
        }
        if (rec != NULL) {
            st = rec->explored(rec->ctx, a);
            if (st != CANON_COMPLETE) {
                return st;
            }
        }
        snap = s->snaps + (size_t)depth * s->snap_words; /* the stack may have moved */
        canon_partition_restore(&s->part, snap); /* spec 11.2 rollback */
        canon_partition_individualise(&s->part, target, a);
        if (depth == UINT32_MAX) {
            return CANON_CAPACITY_LIMIT; /* U32(d) must fit; unreachable for n <= 2^32 - 1 */
        }
        bool child_prune = false;
        if (prune) {
            /* H_{depth+1} = the stabiliser of a in H_depth (one rebase per explored child) */
            st = canon_symmetry_descend(&s->known, depth, a);
            if (st != CANON_COMPLETE) {
                return st;
            }
            child_prune = !canon_symmetry_trivial_at(&s->known, depth + 1);
        }
        st = visit(s, g, x, depth + 1, a, max_nodes, child_prune);
        if (st != CANON_COMPLETE) {
            return st;
        }
    }
    s->reps_top = mark_reps;
    canon_buf_truncate(&s->trace, mark);
    return CANON_COMPLETE;
}

canon_status canon_p1_search_run(canon_p1_search *s, const canon_group *g, const canon_root *x,
                                 uint64_t max_nodes)
{
    return canon_p1_search_run_policy(s, g, x, max_nodes, CANON_WORK_POLICY_REFERENCE);
}

canon_status canon_p1_search_run_policy(canon_p1_search *s, const canon_group *g,
                                        const canon_root *x, uint64_t max_nodes,
                                        canon_work_policy work_policy)
{
    return canon_p1_search_run_recorded(s, g, x, max_nodes, work_policy, NULL);
}

static canon_status run(canon_p1_search *s, const canon_group *g, const canon_root *x,
                        uint64_t max_nodes, canon_work_policy work_policy)
{
    if (work_policy != CANON_WORK_POLICY_REFERENCE &&
        work_policy != CANON_WORK_POLICY_ORBIT_PRUNE) {
        return CANON_UNSUPPORTED_ACTION; /* spec 11.1 v2.1: work-policy IDs */
    }
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
    s->pruned = 0;
    s->children = 0;
    s->reps_top = 0;
    s->work_policy = work_policy;
    memset(&s->known.stats, 0, sizeof s->known.stats);
    canon_buf_truncate(&s->trace, 0);
    canon_buf_truncate(&s->best_trace, 0);
    canon_buf_truncate(&s->best_bytes, 0);
    bool prune = false;
    if (work_policy == CANON_WORK_POLICY_ORBIT_PRUNE && x->n > 0) {
        /* S7 brief 3.2: "At the root: insert into A_known the input generators that fix x
         * (source i), each verified by action" (and by membership, spec 7.3) */
        st = canon_symmetry_reset(&s->known, x->n);
        if (st == CANON_COMPLETE) {
            st = canon_symmetry_from_inputs(&s->known, g, x, &s->image);
        }
        if (st != CANON_COMPLETE) {
            canon_root_image_clear(&s->image);
            return st;
        }
        /* A_known = 1: the traversal below is exactly the unpruned one */
        prune = !canon_symmetry_trivial_at(&s->known, 0);
    }
    /* spec 7.1 root: initial key partition; spec 7.2: n = 0 gives an empty list and the root is
     * a leaf. */
    canon_p1_initial(&s->part, x, &s->scratch);
    st = visit(s, g, x, 0, UINT32_MAX, max_nodes, prune);
    /* Invariant (p1_tree.h): between runs nothing in the workspace points into x; the graph
     * image borrowed x's tables, so drop them now, on every outcome. */
    canon_root_image_clear(&s->image);
    if (st != CANON_COMPLETE) {
        s->have_best = false;
        return st;
    }
    if (!s->have_best) {
        /* the tree always has a leaf (spec 7.2), and pruning explores at least one child of
         * every node (every orbit has a least member) */
        return CANON_INTERNAL_ERROR;
    }
    return CANON_COMPLETE;
}

canon_status canon_p1_search_run_recorded(canon_p1_search *s, const canon_group *g,
                                          const canon_root *x, uint64_t max_nodes,
                                          canon_work_policy work_policy,
                                          const canon_p1_recorder *rec)
{
    s->rec = rec;
    const canon_status st = run(s, g, x, max_nodes, work_policy);
    s->rec = NULL; /* invariant (p1_tree.h): no recorder outlives its run */
    return st;
}
