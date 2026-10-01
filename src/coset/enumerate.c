/* The disjoint coset enumerator of spec 8.1 over a stabiliser chain (slice S4,
 * docs/slices/S4.md 3.2; detailed plan 2.4, WP2.6).
 *
 * spec 8.1, the exact reference, with zero pruning:
 *     visit(H, r):                         # region H r
 *         if H == {id}: consume(r); return
 *         a = smallest atom moved by H
 *         for b in sorted(a^H):
 *             t_b = least image-array element of H with a^t_b=b
 *             visit(H_a, t_b r)
 *     start visit(G, id)
 * "An element h in H sends a to b iff h t_b^-1 in H_a, proving H r = disjoint union over b of
 * H_a t_b r."  So every element of G is consumed exactly once.  H is the suffix of a chain from
 * some level; it is rebased (spec 9.2 base change, transient and unverified, S3 review item 2)
 * so that its first base point is a, unless a already is; H_a is then the next level.  t_b is
 * the constrained least element of src/coset/least.c (the shared primitive of detailed plan
 * 2.4), and t_b r means t_b first, then r (spec 3: (pq)[v] = q[p[v]]).  Recursion depth is at
 * most log2 |G| (each child stabiliser has at most half the order, spec 8.1). */
#include <stdlib.h>
#include <string.h>

#include "arena/alloc.h"
#include "coset/coset.h"
#include "perm/perm.h"
#include "util/sort.h"

void canon_coset_visitor_init(canon_coset_visitor *v, canon_coset_consume_fn consume,
                              canon_coset_poll_fn cancelled, void *user, uint64_t quota)
{
    memset(v, 0, sizeof *v);
    v->consume = consume;
    v->cancelled = cancelled;
    v->user = user;
    v->quota = quota;
}

canon_status canon_coset_visit_enter(canon_coset_visitor *v)
{
    if (v->cancelled != NULL && v->cancelled(v->user)) {
        return CANON_CANCELLED; /* spec 11.1: cancellation policy, not semantic capacity */
    }
    /* spec 11.1: the logical work quota counts the fixed reference traversal; abandon when the
     * count would exceed it, so the outcome depends only on the input and the descriptor. */
    if (v->nodes >= v->quota) {
        return CANON_CAPACITY_LIMIT;
    }
    v->nodes += 1;
    return CANON_COMPLETE;
}

static int u32_cmp(const void *a, const void *b, void *ctx)
{
    (void)ctx;
    const uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
    return x < y ? -1 : x > y;
}

/* The node block of recursion depth d (4 * n words: orbit, sort buffer, t_b, child r),
 * allocated the first time that depth is reached and then reused; blocks never move, so a
 * parent's child r stays valid while its children run (S4 review item 5). */
static canon_status node_block(canon_coset_scratch *s, uint32_t depth, uint32_t **out)
{
    while (s->node_count <= depth) {
        void *nodes = s->nodes;
        canon_status st =
            canon_grow_array(&nodes, &s->node_cap, s->node_count, 8u, sizeof *s->nodes);
        s->nodes = nodes;
        if (st != CANON_COMPLETE) {
            return st;
        }
        size_t words = 0;
        if (!canon_size_mul((size_t)s->node_n, 4u, &words)) {
            return CANON_CAPACITY_LIMIT;
        }
        uint32_t *block = canon_alloc_array(words, sizeof *block, &st);
        if (block == NULL) {
            return st;
        }
        s->nodes[s->node_count++] = block;
    }
    *out = s->nodes[depth];
    return CANON_COMPLETE;
}

/* Node blocks for degree n: blocks of a larger degree are reused, smaller ones are dropped. */
static void node_degree(canon_coset_scratch *s, uint32_t n)
{
    if (n > s->node_n) {
        for (uint32_t d = 0; d < s->node_count; ++d) {
            free(s->nodes[d]);
        }
        s->node_count = 0;
        s->node_n = n;
    }
}

static canon_status visit(const canon_bsgs *c, uint32_t lv, const uint32_t *r,
                          canon_coset_visitor *v, canon_coset_scratch *s, uint32_t depth)
{
    canon_status st = canon_coset_visit_enter(v);
    if (st != CANON_COMPLETE) {
        return st;
    }
    const uint32_t n = c->n;
    if (c->levels[lv].gen_count == 0) {
        /* spec 8.1: "if H == {id}: consume(r)" (a level without generators is trivial) */
        v->leaves += 1;
        return v->consume(v->user, r, &v->stopped);
    }
    /* spec 8.1: "a = smallest atom moved by H" */
    uint32_t a = canon_coset_least_moved(c, lv);
    /* this depth's scratch: the sorted orbit, its sort buffer, t_b and the child's r */
    uint32_t *block = NULL;
    st = node_block(s, depth, &block);
    if (st != CANON_COMPLETE) {
        return st;
    }
    uint32_t *orbit = block, *sort_tmp = block + (size_t)n;
    uint32_t *t_b = block + 2u * (size_t)n, *child_r = block + 3u * (size_t)n;
    canon_bsgs own;
    canon_bsgs_init(&own, n);
    const canon_bsgs *h = c;
    uint32_t hl = lv;
    if (lv >= c->depth || c->levels[lv].base_point != a) {
        /* spec 9.2 base change: H with base starting at a; H_a is its next level */
        st = canon_bsgs_rebase(c, lv, &a, 1, false, &own);
        h = &own;
        hl = 0;
        v->stats.rebuilds += 1;
    }
    if (st == CANON_COMPLETE) {
        /* spec 8.1: "for b in sorted(a^H)" */
        const uint32_t len = h->levels[hl].orbit_len;
        memcpy(orbit, h->levels[hl].orbit, (size_t)len * sizeof *orbit);
        canon_stable_sort(orbit, len, sizeof *orbit, sort_tmp, u32_cmp, NULL);
        for (uint32_t i = 0; i < len && st == CANON_COMPLETE && !v->stopped; ++i) {
            /* spec 8.1: "t_b = least image-array element of H with a^t_b=b" */
            const canon_coset_constraint ab = {a, orbit[i]};
            bool found = false;
            st = canon_coset_least(h, hl, NULL, &ab, 1, t_b, &found, &v->stats, s);
            if (st == CANON_COMPLETE && !found) {
                st = CANON_INTERNAL_ERROR; /* b is in a^H, so some element sends a to b */
            }
            if (st == CANON_COMPLETE) {
                canon_perm_compose(t_b, r, child_r, n); /* t_b r: t_b acts first (spec 3) */
                /* spec 8.1: visit(H_a, t_b r); depth < log2 |G| + 1 <= 64 */
                st = visit(h, hl + 1u, child_r, v, s, depth + 1u);
            }
        }
    }
    canon_bsgs_free(&own);
    return st;
}

canon_status canon_coset_enumerate(const canon_bsgs *c, canon_coset_visitor *v)
{
    const uint32_t n = c->n;
    canon_coset_scratch local;
    canon_coset_scratch *s = v->scratch;
    if (s == NULL) {
        canon_coset_scratch_init(&local);
        s = &local;
    }
    node_degree(s, n);
    canon_status st = CANON_COMPLETE;
    uint32_t *id = canon_alloc_array(n, sizeof *id, &st); /* once per enumeration */
    if (id != NULL) {
        for (uint32_t p = 0; p < n; ++p) {
            id[p] = p;
        }
        v->stopped = false;
        st = visit(c, 0, id, v, s, 0); /* spec 8.1: "start visit(G, id)" */
        free(id);
    }
    if (v->scratch == NULL) {
        canon_coset_scratch_free(&local);
    }
    return st;
}
