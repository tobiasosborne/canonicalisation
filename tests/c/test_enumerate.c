/* Unit tests for the spec 8.1 disjoint coset enumerator (slice S4, docs/slices/S4.md 3.2, 4).
 *
 *  - the product side t_b r, pinned on Sym(3) (3-cycles and transpositions do not commute):
 *    the exact leaf sequence is derived by hand below; the other side repeats elements;
 *  - "leaves are exactly G, each once": every T1 group and 100 random groups with n <= 7,
 *    with both backends; the explicit backend (an independent implementation of the same
 *    traversal over the element table) is the oracle: same leaf sequence, same node count;
 *  - the logical work quota (spec 11.1): CAPACITY_LIMIT iff the traversal has more nodes than
 *    the quota, at the same point for both backends; stop on request; cancellation;
 *  - groups beyond the explicit table (C_2^17, Sym(9)) through the chain only.
 * Node and rebuild counters are printed (docs/slices/S4-notes.md records them). */
#include <stdlib.h>
#include <string.h>

#include "bsgs/chain.h"
#include "bsgs/chain_backend.h"
#include "bsgs/group.h"
#include "check.h"
#include "coset/coset.h"
#include "perm/perm.h"
#include "t1_groups.h"

#define MAXN 34u
#define MAXG 5040u

typedef struct leaves {
    uint32_t n, count, cap;
    uint32_t *elems;    /* consumed elements in order */
    uint32_t stop_at;   /* stop after this many leaves (0 = never) */
    uint32_t polls;     /* poll calls so far */
    uint32_t cancel_at; /* cancel at this poll (0 = never) */
} leaves;

static canon_status on_leaf(void *user, const uint32_t *r, bool *stop)
{
    leaves *l = user;
    if (l->count < l->cap) {
        memcpy(l->elems + (size_t)l->count * l->n, r, l->n * sizeof *r);
    }
    l->count += 1;
    if (l->stop_at != 0 && l->count == l->stop_at) {
        *stop = true;
    }
    return CANON_COMPLETE;
}

static bool on_poll(void *user)
{
    leaves *l = user;
    l->polls += 1;
    return l->cancel_at != 0 && l->polls >= l->cancel_at;
}

static void random_perm(uint32_t *p, uint32_t n)
{
    for (uint32_t v = 0; v < n; ++v) {
        p[v] = v;
    }
    for (uint32_t v = n; v > 1; --v) {
        uint32_t j = (uint32_t)(check_rng() % v), t = p[v - 1];
        p[v - 1] = p[j];
        p[j] = t;
    }
}

static canon_context *ctx_chain, *ctx_explicit;

static canon_group *make(canon_context *ctx, uint32_t n, const uint32_t *gens, uint32_t count)
{
    canon_group *g = NULL;
    CHECK(canon_group_create(ctx, n, gens, count, &g) == CANON_COMPLETE);
    return g;
}

/* Enumerate g into l with a quota; returns the status and the visitor's node count. */
static canon_status run(const canon_group *g, leaves *l, uint64_t quota, canon_coset_visitor *v)
{
    canon_coset_visitor_init(v, on_leaf, on_poll, l, quota);
    l->count = 0;
    l->polls = 0;
    return g->ops->enumerate(g, v);
}

/* The rank of p among the image arrays of Sym(n) in lexicographic order (Lehmer code). */
static uint32_t perm_rank(const uint32_t *p, uint32_t n)
{
    uint32_t rank = 0;
    for (uint32_t i = 0; i < n; ++i) {
        uint32_t smaller = 0;
        for (uint32_t j = i + 1; j < n; ++j) {
            smaller += p[j] < p[i];
        }
        rank = rank * (n - i) + smaller;
    }
    return rank;
}

/* every consumed element is in G, and they are pairwise distinct */
static void check_exactly_once(const canon_group *g, const leaves *l)
{
    const uint32_t n = l->n;
    CHECK(l->count == g->ops->order(g));
    for (uint32_t i = 0; i < l->count && i < l->cap; ++i) {
        bool in = false;
        CHECK(g->ops->contains(g, l->elems + (size_t)i * n, &in) == CANON_COMPLETE && in);
    }
    /* distinct: mark each element's lexicographic rank in Sym(n) (n <= 7 here) */
    static unsigned char seen[MAXG];
    memset(seen, 0, sizeof seen);
    for (uint32_t i = 0; i < l->count && i < l->cap; ++i) {
        const uint32_t rank = perm_rank(l->elems + (size_t)i * n, n);
        CHECK(rank < MAXG && !seen[rank]);
        if (rank < MAXG) {
            seen[rank] = 1;
        }
    }
}

/* ---- the product side ---- */

static void product_side(void)
{
    /* G = Sym(3) from (0 1) = [1,0,2] and the 3-cycle [1,2,0].  Root: a = 0, t_0 = id,
     * t_1 = [1,0,2], t_2 = [2,0,1] (least elements sending 0 to b); H_0 = {id, [0,2,1]} with
     * a = 1, t'_1 = id, t'_2 = [0,2,1].  Leaves t'_c t_b (t'_c first):
     *   b = 0: id, [0,2,1];  b = 1: [1,0,2], [0,2,1][1,0,2] = [1,2,0];
     *   b = 2: [2,0,1], [0,2,1][2,0,1] = [2,1,0].
     * With the other side t_b t'_c, b = 1 would give [1,0,2][0,2,1] = [2,0,1], a repeat. */
    const uint32_t gens[6] = {1, 0, 2, 1, 2, 0};
    const uint32_t want[18] = {0, 1, 2, 0, 2, 1, 1, 0, 2, 1, 2, 0, 2, 0, 1, 2, 1, 0};
    canon_context *ctxs[2] = {ctx_chain, ctx_explicit};
    for (int b = 0; b < 2; ++b) {
        canon_group *g = make(ctxs[b], 3, gens, 2);
        uint32_t buf[18];
        leaves l = {3, 0, 6, buf, 0, 0, 0};
        canon_coset_visitor v;
        CHECK(run(g, &l, UINT64_MAX, &v) == CANON_COMPLETE);
        CHECK(l.count == 6 && memcmp(buf, want, sizeof want) == 0);
        /* nodes: root, three children, six leaves */
        CHECK(v.nodes == 10 && v.leaves == 6 && !v.stopped);
        canon_group_release(g);
    }
}

/* ---- leaves are exactly G, each once; both backends agree exactly ---- */

static uint64_t total_nodes, total_rebuilds, total_descents, total_leaves;

static void compare_backends(uint32_t n, const uint32_t *gens, uint32_t count, uint32_t *buf_a,
                             uint32_t *buf_b)
{
    canon_group *gc = make(ctx_chain, n, gens, count);
    canon_group *ge = make(ctx_explicit, n, gens, count);
    if (gc == NULL || ge == NULL) {
        canon_group_release(gc);
        canon_group_release(ge);
        return;
    }
    leaves la = {n, 0, MAXG, buf_a, 0, 0, 0}, lb = {n, 0, MAXG, buf_b, 0, 0, 0};
    canon_coset_visitor va, vb;
    CHECK(run(gc, &la, UINT64_MAX, &va) == CANON_COMPLETE);
    CHECK(run(ge, &lb, UINT64_MAX, &vb) == CANON_COMPLETE);
    check_exactly_once(gc, &la);
    CHECK(la.count == lb.count && va.nodes == vb.nodes && va.leaves == la.count);
    CHECK(n == 0 || memcmp(buf_a, buf_b, (size_t)la.count * n * sizeof *buf_a) == 0);
    /* one poll per node, before it is counted */
    CHECK(la.polls == va.nodes);
    total_nodes += va.nodes;
    total_leaves += va.leaves;
    total_rebuilds += va.stats.rebuilds;
    total_descents += va.stats.descents;
    /* spec 11.1 quota: exactly `nodes` visits are admitted; one fewer is CAPACITY_LIMIT, at
     * the same point for both backends */
    if (va.nodes > 1) {
        canon_coset_visitor qa, qb;
        CHECK(run(gc, &la, va.nodes, &qa) == CANON_COMPLETE);
        CHECK(run(gc, &la, va.nodes - 1, &qa) == CANON_CAPACITY_LIMIT);
        CHECK(run(ge, &lb, va.nodes - 1, &qb) == CANON_CAPACITY_LIMIT);
        CHECK(qa.nodes == va.nodes - 1 && qb.nodes == qa.nodes && la.count == lb.count);
        /* stop on request after k leaves */
        const uint32_t k = 1u + (uint32_t)(check_rng() % la.count);
        la.stop_at = lb.stop_at = k;
        CHECK(run(gc, &la, UINT64_MAX, &qa) == CANON_COMPLETE && qa.stopped && la.count == k);
        CHECK(run(ge, &lb, UINT64_MAX, &qb) == CANON_COMPLETE && qb.stopped && lb.count == k);
        CHECK(qa.nodes == qb.nodes);
        la.stop_at = lb.stop_at = 0;
        /* cancellation at the j-th poll */
        const uint32_t j = 1u + (uint32_t)(check_rng() % va.nodes);
        la.cancel_at = lb.cancel_at = j;
        CHECK(run(gc, &la, UINT64_MAX, &qa) == CANON_CANCELLED && la.polls == j);
        CHECK(run(ge, &lb, UINT64_MAX, &qb) == CANON_CANCELLED && lb.polls == j);
        CHECK(qa.nodes == j - 1u && qb.nodes == j - 1u);
        la.cancel_at = lb.cancel_at = 0;
    }
    canon_group_release(gc);
    canon_group_release(ge);
}

static void t1(uint32_t *buf_a, uint32_t *buf_b)
{
    uint32_t groups_done = 0;
    for (uint32_t n = 0; n <= 4; ++n) {
        t1_sym s;
        t1_sym_init(&s, n);
        t1_group groups[T1_MAX_GROUPS];
        const uint32_t ng = t1_subgroups(&s, groups);
        for (uint32_t gi = 0; gi < ng; ++gi) {
            compare_backends(n, groups[gi].gens, groups[gi].gen_count, buf_a, buf_b);
            ++groups_done;
        }
    }
    CHECK(groups_done == 40);
    printf("T1 enumeration (40 groups): nodes %llu, leaves %llu, rebuilds %llu, descents %llu\n",
           (unsigned long long)total_nodes, (unsigned long long)total_leaves,
           (unsigned long long)total_rebuilds, (unsigned long long)total_descents);
}

static void t2(uint32_t *buf_a, uint32_t *buf_b)
{
    total_nodes = total_leaves = total_rebuilds = total_descents = 0;
    for (uint32_t trial = 0; trial < 100; ++trial) {
        const uint32_t n = 1u + (uint32_t)(check_rng() % 7u);
        uint32_t gens[3 * 8], count = 1u + (uint32_t)(check_rng() % 3u);
        for (uint32_t i = 0; i < count; ++i) {
            random_perm(gens + i * n, n);
        }
        compare_backends(n, gens, count, buf_a, buf_b);
    }
    printf("T2 enumeration (100 groups, n <= 7): nodes %llu, leaves %llu, rebuilds %llu, "
           "descents %llu\n",
           (unsigned long long)total_nodes, (unsigned long long)total_leaves,
           (unsigned long long)total_rebuilds, (unsigned long long)total_descents);
}

/* ---- beyond the explicit table: the chain only ---- */

static void chain_only(void)
{
    /* C_2^17 on 34 points (order 131072 > the default explicit limit 65536): count only */
    const uint32_t n = 34;
    uint32_t gens[17 * 34];
    for (uint32_t i = 0; i < 17; ++i) {
        for (uint32_t v = 0; v < n; ++v) {
            gens[i * n + v] = v;
        }
        gens[i * n + 2 * i] = 2 * i + 1;
        gens[i * n + 2 * i + 1] = 2 * i;
    }
    canon_group *g = make(ctx_chain, n, gens, 17);
    canon_group *e = NULL;
    CHECK(canon_group_create(ctx_explicit, n, gens, 17, &e) == CANON_CAPACITY_LIMIT);
    leaves l = {n, 0, 0, NULL, 0, 0, 0};
    canon_coset_visitor v;
    CHECK(run(g, &l, UINT64_MAX, &v) == CANON_COMPLETE);
    /* a = 2i at depth i, orbit {2i, 2i+1}: 2^17 leaves, 2^18 - 1 nodes */
    CHECK(l.count == 131072u && v.nodes == 262143u);
    canon_group_release(g);
}

int main(void)
{
    const canon_context_options co = {CANON_BACKEND_CHAIN}, eo = {CANON_BACKEND_EXPLICIT};
    CHECK(canon_context_create_with_options(NULL, &co, &ctx_chain) == CANON_COMPLETE);
    CHECK(canon_context_create_with_options(NULL, &eo, &ctx_explicit) == CANON_COMPLETE);
    uint32_t *buf_a = malloc((size_t)MAXG * 8 * sizeof *buf_a);
    uint32_t *buf_b = malloc((size_t)MAXG * 8 * sizeof *buf_b);
    product_side();
    t1(buf_a, buf_b);
    t2(buf_a, buf_b);
    chain_only();
    free(buf_a);
    free(buf_b);
    canon_context_release(ctx_chain);
    canon_context_release(ctx_explicit);
    return check_finish("test_enumerate");
}
