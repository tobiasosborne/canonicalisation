/* Slice S7 step 1 (docs/slices/S7.md 3.1, 3.2, 4, 6.2; docs/pruning-rules.md): orbit pruning of
 * the canonical image under work policy 0x0002 against the unpruned reference policy 0x0001.
 *
 *  1. Every T1 group (n <= 4) and subset, both backends, both generating sets (greedy and every
 *     element): the two policies give the same trace, bytes and flags; with the deterministic
 *     witness the same witness (equal to the unpruned ANY witness, S1 reading 1); with
 *     CANON_WITNESS_ANY the pruned witness verifies (membership and action).  The internal
 *     counters: explored nodes <= unpruned nodes; nodes = 1 + children - pruned on both
 *     policies; with A_known trivial the pruned run IS the unpruned run.  The same on random
 *     coloured, labelled graphs (G1/G2-like) and nested roots under every T1 group.
 *  2. Random groups of degree 5..7 with x a union of cycles of a generator.
 *  3. The node count drops to the orbit count: the empty subset under Sym(m), m = 2..6, explores
 *     m nodes against sum_k m!/(m-k)! (both derived here from the rules), and a hand case.
 *  4. The quota (spec 11.1 v2.1): exact boundaries under both policies; the deterministic
 *     witness's enumeration gets the same share under both.
 *  5. The descriptor: unknown IDs refused at context and problem creation, 0 = context default,
 *     canon_result_work_policy.
 *  6. The labeling and signed objectives stay unpruned under the default descriptor.
 *  7. The explored count does not depend on generator order, duplicates, identities or the
 *     backend; it does depend on the generating set (documented in docs/pruning-rules.md).
 *  8. Workspace reuse across policies and objectives.
 *
 * Convention (spec 3): p[v] = v^p, (pq)[v] = q[p[v]].
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "bsgs/group.h"
#include "canon/canon.h"
#include "check.h"
#include "object/object.h"
#include "search/objectives.h"
#include "search/p1_tree.h"
#include "t1_groups.h"

#define REF CANON_WORK_POLICY_REFERENCE
#define PRUNE CANON_WORK_POLICY_ORBIT_PRUNE
#define BIG ((uint64_t)1 << 40)

static canon_context *CTX[2]; /* chain, explicit; default work policy (0x0002) */
static canon_workspace *WS;

static canon_group *make_group(int backend, uint32_t n, const uint32_t *gens, uint32_t count)
{
    canon_group *g = NULL;
    CHECK(canon_group_create(CTX[backend], n, gens, count, &g) == CANON_COMPLETE);
    return g;
}

/* CANONICAL_IMAGE of x under g with an explicit work policy (0 = context default). */
static canon_result *solve(canon_context *ctx, canon_workspace *ws, const canon_group *g,
                           const canon_object *x, canon_work_policy policy,
                           canon_witness_mode mode, uint64_t quota, canon_status *st)
{
    canon_capacity cap = {0, 0, quota, 0, 0, 0, 0, policy};
    canon_problem_options opts = {mode, NULL};
    canon_problem *p = NULL;
    canon_result *r = NULL;
    *st = canon_problem_create_with_options(ctx, g, x, NULL, CANON_OBJECTIVE_CANONICAL_IMAGE,
                                            CANON_PROFILE_P1, CANON_ENCODING_CDAG_2,
                                            CANON_ORDER_CDAG_BYTE_1, &cap, &opts, &p);
    if (*st == CANON_COMPLETE) {
        *st = canon_solve(ws, p, &r);
    }
    canon_problem_release(p);
    return r;
}

static int same_bytes(const uint8_t *(*get)(const canon_result *, size_t *), const canon_result *a,
                      const canon_result *b)
{
    size_t la = 0, lb = 0;
    const uint8_t *pa = get(a, &la), *pb = get(b, &lb);
    return pa != NULL && pb != NULL && la == lb && memcmp(pa, pb, la) == 0;
}

static int same_witness(const canon_result *a, const canon_result *b)
{
    uint32_t da = 0, db = 0;
    const uint32_t *wa = canon_result_witness(a, &da), *wb = canon_result_witness(b, &db);
    return wa != NULL && wb != NULL && da == db && (da == 0 || memcmp(wa, wb, da * 4u) == 0);
}

static int same_flags(const canon_result *a, const canon_result *b)
{
    canon_result_flags fa = canon_result_get_flags(a), fb = canon_result_get_flags(b);
    return fa.witness_valid == fb.witness_valid && fa.image_canonical == fb.image_canonical &&
           fa.minimum_proved == fb.minimum_proved &&
           fa.subgroup_verified == fb.subgroup_verified &&
           fa.stabiliser_complete == fb.stabiliser_complete &&
           fa.transport_exhausted == fb.transport_exhausted &&
           fa.zero_certified == fb.zero_certified &&
           fa.nonzero_certified == fb.nonzero_certified &&
           fa.encoding_complete == fb.encoding_complete;
}

static canon_work_policy policy_of(const canon_result *r)
{
    canon_work_policy p = 99;
    CHECK(canon_result_work_policy(r, &p) == CANON_COMPLETE);
    return p;
}

/* Tier totals (no timings). */
typedef struct totals {
    uint64_t cases, nodes_ref, nodes_pruned, pruned, inserted, rebases, any_differs,
        trivial_known;
} totals;

/* The comparison of one input under both policies: public API (x as an object) and the
 * internal search (x as a root, for the counters). */
static void compare(const canon_group *g, const canon_object *xo, const canon_root *x,
                    totals *t)
{
    canon_status s1, s2, s3, s4;
    canon_result *r1 = solve(CTX[0], WS, g, xo, REF, CANON_WITNESS_ANY, 0, &s1);
    canon_result *r2 = solve(CTX[0], WS, g, xo, PRUNE, CANON_WITNESS_ANY, 0, &s2);
    canon_result *d1 = solve(CTX[0], WS, g, xo, REF, CANON_WITNESS_DETERMINISTIC, 0, &s3);
    canon_result *d2 = solve(CTX[0], WS, g, xo, PRUNE, CANON_WITNESS_DETERMINISTIC, 0, &s4);
    CHECK(s1 == CANON_COMPLETE && s2 == CANON_COMPLETE && s3 == CANON_COMPLETE &&
          s4 == CANON_COMPLETE);
    /* trace, bytes and flags are P1's under both policies (PROFILE-EQUIV) */
    CHECK(same_bytes(canon_result_trace, r1, r2) && same_bytes(canon_result_trace, r1, d2));
    CHECK(same_bytes(canon_result_bytes, r1, r2) && same_bytes(canon_result_bytes, r1, d2));
    CHECK(same_flags(r1, r2) && same_flags(d1, d2) && same_flags(r1, d1));
    /* deterministic mode: the witness is unchanged (brief 3.1 Scope), and under the reference
     * policy it is the least attaining leaf witness (S1 reading 1) */
    CHECK(same_witness(d1, d2) && same_witness(r1, d1));
    /* ANY mode under pruning: a valid witness (in G, x^w = c), possibly another one */
    bool valid = false;
    CHECK(canon_result_verify_witness(r2, &valid) == CANON_COMPLETE && valid);
    t->any_differs += !same_witness(r1, r2);
    /* the effective policy is recorded on the result (spec 11.1 v2.1) */
    CHECK(policy_of(r1) == REF && policy_of(r2) == PRUNE && policy_of(d2) == PRUNE);
    canon_result_release(r1);
    canon_result_release(r2);
    canon_result_release(d1);
    canon_result_release(d2);
    /* the internal counters */
    canon_p1_search a, b;
    canon_p1_search_init(&a);
    canon_p1_search_init(&b);
    CHECK(canon_p1_search_run_policy(&a, g, x, BIG, REF) == CANON_COMPLETE);
    CHECK(canon_p1_search_run_policy(&b, g, x, BIG, PRUNE) == CANON_COMPLETE);
    CHECK(a.best_trace.len == b.best_trace.len &&
          memcmp(a.best_trace.data, b.best_trace.data, a.best_trace.len) == 0);
    CHECK(a.best_bytes.len == b.best_bytes.len &&
          memcmp(a.best_bytes.data, b.best_bytes.data, a.best_bytes.len) == 0);
    CHECK(a.work_policy == REF && b.work_policy == PRUNE);
    CHECK(a.pruned == 0 && a.known.stats.rebases == 0 && a.known.stats.considered == 0);
    /* every child of an explored node is entered or skipped by the rule */
    CHECK(a.nodes == 1 + a.children && b.nodes == 1 + b.children - b.pruned);
    CHECK(b.nodes <= a.nodes && b.leaves <= a.leaves);
    if (x->n > 0 && b.known.chain.order == 1) {
        /* A_known = 1: exactly the unpruned traversal, no extra tree work */
        CHECK(b.nodes == a.nodes && b.leaves == a.leaves && b.pruned == 0);
        CHECK(b.known.stats.rebases == 0 && b.known.stats.orbit_calls == 0);
        CHECK(memcmp(a.best_t, b.best_t, x->n * sizeof *a.best_t) == 0);
        t->trivial_known += 1;
    }
    if (b.pruned > 0) {
        CHECK(b.nodes < a.nodes);
    }
    t->cases += 1;
    t->nodes_ref += a.nodes;
    t->nodes_pruned += b.nodes;
    t->pruned += b.pruned;
    t->inserted += b.known.stats.inserted;
    t->rebases += b.known.stats.rebases;
    canon_p1_search_free(&a);
    canon_p1_search_free(&b);
}

static void print_totals(const char *name, const totals *t)
{
    printf("%s: %llu cases, NODE tokens %llu unpruned / %llu pruned, %llu children pruned, "
           "%llu automorphisms inserted, %llu rebases, %llu with A_known = 1, %llu ANY "
           "witnesses differ\n",
           name, (unsigned long long)t->cases, (unsigned long long)t->nodes_ref,
           (unsigned long long)t->nodes_pruned, (unsigned long long)t->pruned,
           (unsigned long long)t->inserted, (unsigned long long)t->rebases,
           (unsigned long long)t->trivial_known, (unsigned long long)t->any_differs);
}

static void subset_root(canon_root *x, uint32_t n, const uint32_t *atoms, uint32_t k)
{
    memset(x, 0, sizeof *x);
    x->kind = CANON_ROOT_SUBSET;
    x->n = n;
    CHECK(canon_subset_init(&x->u.subset, n, atoms, k) == CANON_COMPLETE);
}

/* The generating set of a T1 group: greedy, or every element in index order. */
static uint32_t gens_of(const t1_sym *s, const t1_group *grp, int full, uint32_t *gens)
{
    const uint32_t n = s->n;
    if (!full) {
        memcpy(gens, grp->gens, grp->gen_count * n * sizeof *gens);
        return grp->gen_count;
    }
    uint32_t count = 0;
    for (uint32_t e = 0; e < s->count; ++e) {
        if (grp->mask >> e & 1u) {
            memcpy(gens + count++ * n, s->elem[e], n * sizeof *gens);
        }
    }
    return count;
}

/* ---- 1: the T1 tier ---- */

static void test_t1_subsets(void)
{
    totals t;
    memset(&t, 0, sizeof t);
    for (uint32_t n = 0; n <= 4; ++n) {
        t1_sym sym;
        t1_sym_init(&sym, n);
        t1_group groups[T1_MAX_GROUPS];
        const uint32_t gcount = t1_subgroups(&sym, groups);
        for (uint32_t gi = 0; gi < gcount; ++gi) {
            for (int full = 0; full < 2; ++full) {
                uint32_t gens[24 * 4];
                const uint32_t count = gens_of(&sym, &groups[gi], full, gens);
                for (int backend = 0; backend < 2; ++backend) {
                    canon_group *g = make_group(backend, n, gens, count);
                    for (uint32_t mask = 0; mask < 1u << n; ++mask) {
                        uint32_t atoms[4], k = 0;
                        for (uint32_t v = 0; v < n; ++v) {
                            if (mask >> v & 1u) {
                                atoms[k++] = v;
                            }
                        }
                        canon_object *xo = NULL;
                        CHECK(canon_object_create_subset(CTX[0], n, atoms, k, &xo) ==
                              CANON_COMPLETE);
                        canon_root x;
                        subset_root(&x, n, atoms, k);
                        compare(g, xo, &x, &t);
                        canon_root_free(&x);
                        canon_object_release(xo);
                    }
                    canon_group_release(g);
                }
            }
        }
    }
    print_totals("T1 subsets (both backends, both generating sets)", &t);
}

/* Random coloured, labelled directed multigraphs on n <= 3 vertices under every T1 group
 * (greedy generators, both backends), with loops and multiplicities. */
static void test_t1_graphs(void)
{
    totals t;
    memset(&t, 0, sizeof t);
    static const uint8_t lab_a[1] = {'a'};
    const uint8_t *labels[2] = {NULL, lab_a};
    for (uint32_t n = 1; n <= 3; ++n) {
        t1_sym sym;
        t1_sym_init(&sym, n);
        t1_group groups[T1_MAX_GROUPS];
        const uint32_t gcount = t1_subgroups(&sym, groups);
        for (uint32_t gi = 0; gi < gcount; ++gi) {
            for (int backend = 0; backend < 2; ++backend) {
                canon_group *g = make_group(backend, n, groups[gi].gens, groups[gi].gen_count);
                for (int round = 0; round < 12; ++round) {
                    canon_arc arcs[6];
                    const size_t count = (size_t)(check_rng() % 6);
                    for (size_t i = 0; i < count; ++i) {
                        const size_t l = (size_t)(check_rng() % 2);
                        arcs[i] = (canon_arc){(uint32_t)(check_rng() % n),
                                              (uint32_t)(check_rng() % n), labels[l], l,
                                              1 + check_rng() % 2};
                    }
                    const uint8_t *colours[3];
                    size_t lengths[3];
                    for (uint32_t v = 0; v < n; ++v) {
                        lengths[v] = (size_t)(check_rng() % 2);
                        colours[v] = labels[lengths[v]];
                    }
                    const bool coloured = round % 3 == 0;
                    canon_object *xo = NULL;
                    CHECK(canon_object_create_graph(CTX[0], n, coloured ? colours : NULL,
                                                    coloured ? lengths : NULL, arcs, count,
                                                    &xo) == CANON_COMPLETE);
                    canon_root x;
                    memset(&x, 0, sizeof x);
                    x.kind = CANON_ROOT_GRAPH;
                    x.n = n;
                    CHECK(canon_graph_init(&x.u.graph, n, coloured ? colours : NULL,
                                           coloured ? lengths : NULL, arcs,
                                           count) == CANON_COMPLETE);
                    compare(g, xo, &x, &t);
                    canon_root_free(&x);
                    canon_object_release(xo);
                }
                canon_group_release(g);
            }
        }
    }
    print_totals("T1 graphs (random, both backends)", &t);
}

/* Big-endian U32 writer for hand-made CDAG-2 streams (spec 4.1). */
typedef struct sbuf {
    uint8_t b[256];
    size_t len;
} sbuf;

static void put8(sbuf *s, uint8_t v)
{
    s->b[s->len++] = v;
}

static void put32(sbuf *s, uint32_t v)
{
    for (int i = 3; i >= 0; --i) {
        put8(s, (uint8_t)(v >> (8 * i)));
    }
}

/* Nested roots on n = 4 (spec 4.1 records; header H(4) = 43 4e 02 00 01 00 01 || U32(4)):
 * the tuple (a, b), and the set {(a, b), (c, d)} of two tuples. */
static void dag_stream(sbuf *s, int which, const uint32_t *p)
{
    s->len = 0;
    const uint8_t magic[7] = {0x43, 0x4e, 0x02, 0x00, 0x01, 0x00, 0x01};
    for (int i = 0; i < 7; ++i) {
        put8(s, magic[i]);
    }
    put32(s, 4);
    put32(s, which == 0 ? 3u : 7u); /* record count q */
    const uint32_t atoms = which == 0 ? 2u : 4u;
    for (uint32_t i = 0; i < atoms; ++i) {
        put8(s, 0x01);
        put32(s, p[i]);
    }
    for (uint32_t i = 0; i < atoms / 2; ++i) {
        put8(s, 0x03);
        put32(s, 2);
        put32(s, 2 * i);
        put32(s, 2 * i + 1);
    }
    if (which == 1) {
        put8(s, 0x04);
        put32(s, 2);
        put32(s, 4);
        put32(s, 5);
    }
    put32(s, which == 0 ? 2u : 6u); /* root */
}

static void test_t1_dags(void)
{
    totals t;
    memset(&t, 0, sizeof t);
    const canon_dag_limits lim = {4096, UINT64_MAX, UINT64_MAX, UINT64_MAX};
    t1_sym sym;
    t1_sym_init(&sym, 4);
    t1_group groups[T1_MAX_GROUPS];
    const uint32_t gcount = t1_subgroups(&sym, groups);
    for (uint32_t gi = 0; gi < gcount; ++gi) {
        for (int backend = 0; backend < 2; ++backend) {
            canon_group *g = make_group(backend, 4, groups[gi].gens, groups[gi].gen_count);
            for (int which = 0; which < 2; ++which) {
                const uint32_t *p = sym.elem[check_rng() % sym.count];
                sbuf s;
                dag_stream(&s, which, p);
                canon_object *xo = NULL;
                CHECK(canon_object_create(CTX[0], CANON_SCHEMA_EXT_DAG_1,
                                          CANON_ACTION_ATOM_TRANSPORT_1, 4, s.b, s.len,
                                          &xo) == CANON_COMPLETE);
                canon_root x;
                CHECK(canon_root_import_stream(&x, 4, s.b, s.len, &lim, NULL) ==
                      CANON_COMPLETE);
                CHECK(x.kind == CANON_ROOT_DAG);
                compare(g, xo, &x, &t);
                canon_root_free(&x);
                canon_object_release(xo);
            }
            canon_group_release(g);
        }
    }
    print_totals("T1 nested roots (n = 4, both backends)", &t);
}

/* ---- 2: random groups beyond T1 ---- */

static void random_perm(uint32_t n, uint32_t *p)
{
    for (uint32_t v = 0; v < n; ++v) {
        p[v] = v;
    }
    for (uint32_t v = n; v > 1; --v) {
        const uint32_t j = (uint32_t)(check_rng() % v), tmp = p[v - 1];
        p[v - 1] = p[j];
        p[j] = tmp;
    }
}

static void test_random_groups(void)
{
    totals t;
    memset(&t, 0, sizeof t);
    for (int round = 0; round < 36; ++round) {
        const uint32_t n = 5 + (uint32_t)(round % 3);
        const uint32_t count = 1 + (uint32_t)(check_rng() % 3);
        uint32_t gens[3 * 7];
        for (uint32_t i = 0; i < count; ++i) {
            random_perm(n, gens + i * n);
        }
        /* x = a union of cycles of the first generator, so that it is fixed by it (A_known is
         * nontrivial unless that generator is the identity) */
        uint32_t atoms[7], k = 0, seen = 0;
        for (uint32_t v = 0; v < n; ++v) {
            if (seen >> v & 1u) {
                continue;
            }
            const bool take = check_rng() % 2 == 0;
            for (uint32_t w = v; !(seen >> w & 1u); w = gens[w]) {
                seen |= 1u << w;
                if (take) {
                    atoms[k++] = w;
                }
            }
        }
        canon_group *g = make_group(round % 2, n, gens, count);
        canon_object *xo = NULL;
        CHECK(canon_object_create_subset(CTX[0], n, atoms, k, &xo) == CANON_COMPLETE);
        canon_root x;
        subset_root(&x, n, atoms, k);
        compare(g, xo, &x, &t);
        canon_root_free(&x);
        canon_object_release(xo);
        canon_group_release(g);
    }
    print_totals("random groups (n = 5..7)", &t);
}

/* ---- 3: the node count drops to the orbit count ---- */

static void test_count_drop(void)
{
    for (uint32_t m = 2; m <= 6; ++m) {
        /* Sym(m) = <(0 1), (0 1 ... m-1)> */
        uint32_t gens[2 * 6];
        for (uint32_t v = 0; v < m; ++v) {
            gens[v] = v;
            gens[m + v] = (v + 1) % m;
        }
        gens[0] = 1;
        gens[1] = 0;
        /* spec 7.1 on the empty subset under Sym(m): every node's partition is [{prefix
         * singletons}, rest] and the G stage never splits the rest (one G_M orbit), so the
         * unpruned tree has one node per injective prefix of length k = 0..m-1:
         * sum_k m!/(m-k)!; under pruning H_d = Sym(rest) has one orbit on the rest, so one
         * child per node: m nodes, and sum_{j=2..m} (j - 1) pruned children. */
        uint64_t unpruned = 0, falling = 1, pruned_children = 0;
        for (uint32_t k = 0; k < m; ++k) {
            unpruned += falling;
            falling *= m - k;
        }
        for (uint32_t j = 2; j <= m; ++j) {
            pruned_children += j - 1;
        }
        canon_root x;
        subset_root(&x, m, NULL, 0);
        for (int backend = 0; backend < 2; ++backend) {
            canon_group *g = make_group(backend, m, gens, 2);
            canon_p1_search a, b;
            canon_p1_search_init(&a);
            canon_p1_search_init(&b);
            CHECK(canon_p1_search_run_policy(&a, g, &x, BIG, REF) == CANON_COMPLETE);
            CHECK(canon_p1_search_run_policy(&b, g, &x, BIG, PRUNE) == CANON_COMPLETE);
            CHECK(a.nodes == unpruned && b.nodes == m && b.pruned == pruned_children);
            CHECK(b.leaves == 1 && b.known.chain.order == falling);
            /* one rebase per explored child (m - 1 of them) */
            CHECK(b.known.stats.rebases == m - 1);
            if (backend == 0) {
                printf("empty subset under Sym(%u): %llu NODE tokens unpruned, %llu pruned "
                       "(%llu children pruned, %llu automorphisms inserted)\n",
                       (unsigned)m, (unsigned long long)a.nodes, (unsigned long long)b.nodes,
                       (unsigned long long)b.pruned,
                       (unsigned long long)b.known.stats.inserted);
            }
            canon_p1_search_free(&a);
            canon_p1_search_free(&b);
            canon_group_release(g);
        }
        canon_root_free(&x);
    }
    /* By hand: x = {0} under Sym(3) = <(1 2), (0 1 2)>.  Only (1 2) fixes x, A_known =
     * <(1 2)>.  Root cells [{1,2},{0}] (key 0 before 1), no stage splits; target {1,2}: two
     * leaves unpruned (3 NODE tokens), one orbit pruned (2 NODE tokens, 1 child pruned). */
    const uint32_t gens[6] = {0, 2, 1, 1, 2, 0}, zero = 0;
    canon_root x;
    subset_root(&x, 3, &zero, 1);
    canon_group *g = make_group(0, 3, gens, 2);
    canon_p1_search a, b;
    canon_p1_search_init(&a);
    canon_p1_search_init(&b);
    CHECK(canon_p1_search_run_policy(&a, g, &x, BIG, REF) == CANON_COMPLETE);
    CHECK(canon_p1_search_run_policy(&b, g, &x, BIG, PRUNE) == CANON_COMPLETE);
    CHECK(a.nodes == 3 && b.nodes == 2 && b.pruned == 1 && b.known.chain.order == 2);
    CHECK(b.known.stats.not_fixing == 1 && b.known.stats.inserted == 1);
    /* a policy the module does not know */
    CHECK(canon_p1_search_run_policy(&b, g, &x, BIG, 3) == CANON_UNSUPPORTED_ACTION);
    canon_p1_search_free(&a);
    canon_p1_search_free(&b);
    canon_group_release(g);
    canon_root_free(&x);
}

/* ---- 4: the quota ---- */

/* The least quota with which the solve completes, by linear search from 1 (small cases). */
static uint64_t least_quota(canon_context *ctx, const canon_group *g, const canon_object *x,
                            canon_work_policy policy, canon_witness_mode mode)
{
    for (uint64_t q = 1; q < 4096; ++q) {
        canon_status st;
        canon_result *r = solve(ctx, WS, g, x, policy, mode, q, &st);
        CHECK(st == CANON_COMPLETE || st == CANON_CAPACITY_LIMIT);
        canon_result_release(r);
        if (st == CANON_COMPLETE) {
            return q;
        }
    }
    CHECK(0);
    return 0;
}

static void test_quota(void)
{
    /* the empty subset under Sym(4): 41 = 1 + 4 + 12 + 24 NODE tokens unpruned (falling
     * factorials, as in test_count_drop), 4 pruned */
    const uint32_t gens[8] = {1, 0, 2, 3, 1, 2, 3, 0};
    uint64_t unpruned = 0, falling = 1;
    for (uint32_t k = 0; k < 4; ++k) {
        unpruned += falling;
        falling *= 4 - k;
    }
    for (int backend = 0; backend < 2; ++backend) {
        canon_group *g = make_group(backend, 4, gens, 2);
        canon_object *x = NULL;
        CHECK(canon_object_create_subset(CTX[0], 4, NULL, 0, &x) == CANON_COMPLETE);
        canon_status st;
        canon_result *r = NULL;
        /* policy 0x0002 counts the explored nodes: 4 complete, 3 is the limit */
        r = solve(CTX[backend], WS, g, x, PRUNE, CANON_WITNESS_ANY, 4, &st);
        CHECK(st == CANON_COMPLETE && policy_of(r) == PRUNE);
        canon_result_release(r);
        r = solve(CTX[backend], WS, g, x, PRUNE, CANON_WITNESS_ANY, 3, &st);
        CHECK(st == CANON_CAPACITY_LIMIT && canon_result_status(r) == CANON_CAPACITY_LIMIT);
        canon_work_policy p = 7;
        CHECK(canon_result_work_policy(r, &p) == CANON_INVALID_INPUT && p == 0);
        canon_result_release(r);
        /* the default descriptor is 0x0002 */
        r = solve(CTX[backend], WS, g, x, 0, CANON_WITNESS_ANY, 4, &st);
        CHECK(st == CANON_COMPLETE && policy_of(r) == PRUNE);
        canon_result_release(r);
        /* policy 0x0001 counts the unpruned traversal */
        r = solve(CTX[backend], WS, g, x, REF, CANON_WITNESS_ANY, 4, &st);
        CHECK(st == CANON_CAPACITY_LIMIT);
        canon_result_release(r);
        r = solve(CTX[backend], WS, g, x, REF, CANON_WITNESS_ANY, unpruned - 1, &st);
        CHECK(st == CANON_CAPACITY_LIMIT);
        canon_result_release(r);
        r = solve(CTX[backend], WS, g, x, REF, CANON_WITNESS_ANY, unpruned, &st);
        CHECK(st == CANON_COMPLETE && policy_of(r) == REF);
        canon_result_release(r);
        /* the deterministic witness: one quota for the solve (spec 11.1), the enumeration
         * gets what the tree left, so the two least quotas differ by exactly the tree counts */
        const uint64_t q1 = least_quota(CTX[backend], g, x, REF, CANON_WITNESS_DETERMINISTIC);
        const uint64_t q2 = least_quota(CTX[backend], g, x, PRUNE, CANON_WITNESS_DETERMINISTIC);
        CHECK(q1 > unpruned && q2 > 4 && q1 - q2 == unpruned - 4);
        CHECK(least_quota(CTX[backend], g, x, PRUNE, CANON_WITNESS_ANY) == 4);
        CHECK(least_quota(CTX[backend], g, x, REF, CANON_WITNESS_ANY) == unpruned);
        if (backend == 0) {
            printf("quota, empty subset under Sym(4): ANY %llu / %llu, DETERMINISTIC %llu / "
                   "%llu (unpruned / pruned)\n",
                   (unsigned long long)unpruned, 4ull, (unsigned long long)q1,
                   (unsigned long long)q2);
        }
        canon_object_release(x);
        canon_group_release(g);
    }
}

/* ---- 5: the descriptor ---- */

static void test_descriptor(void)
{
    canon_context *ctx = (canon_context *)&WS; /* overwritten with NULL on failure */
    const canon_capacity bad = {0, 0, 0, 0, 0, 0, 0, 3};
    CHECK(canon_context_create(&bad, &ctx) == CANON_UNSUPPORTED_ACTION && ctx == NULL);
    const canon_capacity huge = {0, 0, 0, 0, 0, 0, 0, 0xffff};
    const canon_context_options opts = {CANON_BACKEND_EXPLICIT};
    CHECK(canon_context_create_with_options(&huge, &opts, &ctx) == CANON_UNSUPPORTED_ACTION);
    /* an unknown backend is decided first (INVALID_INPUT, S3), then the policy */
    const canon_context_options wrong = {(canon_backend)7};
    CHECK(canon_context_create_with_options(&bad, &wrong, &ctx) == CANON_INVALID_INPUT);
    /* a context whose default is the reference policy */
    const canon_capacity ref = {0, 0, 0, 0, 0, 0, 0, REF};
    CHECK(canon_context_create(&ref, &ctx) == CANON_COMPLETE);
    const uint32_t swap[2] = {1, 0};
    canon_group *g = NULL;
    canon_object *x = NULL;
    CHECK(canon_group_create(ctx, 2, swap, 1, &g) == CANON_COMPLETE);
    CHECK(canon_object_create_subset(ctx, 2, NULL, 0, &x) == CANON_COMPLETE);
    canon_status st;
    /* the spec 7.4 empty subset under Sym(2): 3 NODE tokens unpruned, 2 pruned */
    canon_result *r = solve(ctx, WS, g, x, 0, CANON_WITNESS_ANY, 2, &st);
    CHECK(st == CANON_CAPACITY_LIMIT); /* 0 selects the context default 0x0001 */
    canon_result_release(r);
    r = solve(ctx, WS, g, x, PRUNE, CANON_WITNESS_ANY, 2, &st);
    CHECK(st == CANON_COMPLETE && policy_of(r) == PRUNE); /* the problem's field wins */
    canon_result_release(r);
    r = solve(ctx, WS, g, x, 0, CANON_WITNESS_ANY, 3, &st);
    CHECK(st == CANON_COMPLETE && policy_of(r) == REF);
    canon_result_release(r);
    /* problem creation refuses unknown IDs for every objective */
    canon_problem *p = (canon_problem *)&WS;
    CHECK(canon_problem_create(ctx, g, x, CANON_OBJECTIVE_CANONICAL_IMAGE, CANON_PROFILE_P1,
                               CANON_ENCODING_CDAG_2, CANON_ORDER_CDAG_BYTE_1, &bad,
                               &p) == CANON_UNSUPPORTED_ACTION &&
          p == NULL);
    CHECK(canon_problem_create(ctx, g, x, CANON_OBJECTIVE_STABILISER, CANON_PROFILE_NO_TREE,
                               CANON_ENCODING_CDAG_2, CANON_ORDER_CDAG_BYTE_1, &huge,
                               &p) == CANON_UNSUPPORTED_ACTION);
    /* accessor arguments */
    canon_work_policy pol = 5;
    CHECK(canon_result_work_policy(NULL, &pol) == CANON_INVALID_INPUT && pol == 0);
    r = solve(ctx, WS, g, x, 0, CANON_WITNESS_ANY, 0, &st);
    CHECK(canon_result_work_policy(r, NULL) == CANON_INVALID_INPUT);
    canon_result_release(r);
    /* other objectives run the reference traversal whatever the descriptor says: the
     * effective policy recorded is 0x0001 */
    const canon_capacity prune = {0, 0, 0, 0, 0, 0, 0, PRUNE};
    CHECK(canon_problem_create(ctx, g, x, CANON_OBJECTIVE_STABILISER, CANON_PROFILE_NO_TREE,
                               CANON_ENCODING_CDAG_2, CANON_ORDER_CDAG_BYTE_1, &prune,
                               &p) == CANON_COMPLETE);
    CHECK(canon_solve(WS, p, &r) == CANON_COMPLETE && policy_of(r) == REF);
    canon_result_release(r);
    canon_problem_release(p);
    canon_object_release(x);
    canon_group_release(g);
    canon_context_release(ctx);
}

/* ---- 6: the labeling and signed objectives stay unpruned ---- */

static uint64_t least_quota_objective(const canon_group *g, const canon_object *x,
                                      canon_objective objective, const uint32_t *rho,
                                      canon_work_policy policy, canon_work_policy *recorded)
{
    for (uint64_t q = 1; q < 4096; ++q) {
        canon_capacity cap = {0, 0, q, 0, 0, 0, 0, policy};
        canon_problem_options opts = {CANON_WITNESS_ANY, rho};
        canon_problem *p = NULL;
        canon_result *r = NULL;
        CHECK(canon_problem_create_with_options(CTX[0], g, x, NULL, objective, CANON_PROFILE_P1,
                                                CANON_ENCODING_CDAG_2, CANON_ORDER_CDAG_BYTE_1,
                                                &cap, &opts, &p) == CANON_COMPLETE);
        const canon_status st = canon_solve(WS, p, &r);
        if (st == CANON_COMPLETE) {
            *recorded = policy_of(r);
        }
        canon_result_release(r);
        canon_problem_release(p);
        if (st == CANON_COMPLETE) {
            return q;
        }
    }
    CHECK(0);
    return 0;
}

static void test_objectives_unpruned(void)
{
    /* the empty subset under Sym(4): the P1 run of these objectives is the whole tree */
    const uint32_t gens[8] = {1, 0, 2, 3, 1, 2, 3, 0}, rho[4] = {2, 0, 3, 1};
    const int8_t signs[2] = {1, 1};
    canon_group *g = make_group(0, 4, gens, 2);
    canon_group *sg = NULL;
    CHECK(canon_group_create_signed(CTX[0], 4, gens, 2, signs, &sg) == CANON_COMPLETE);
    canon_object *x = NULL;
    CHECK(canon_object_create_subset(CTX[0], 4, NULL, 0, &x) == CANON_COMPLETE);
    /* through the public API: the quota boundary is the same under both descriptors */
    canon_work_policy r1 = 0, r2 = 0;
    const uint64_t l1 =
        least_quota_objective(g, x, CANON_OBJECTIVE_CANONICAL_LABELING_COSET, rho, REF, &r1);
    const uint64_t l2 =
        least_quota_objective(g, x, CANON_OBJECTIVE_CANONICAL_LABELING_COSET, rho, 0, &r2);
    CHECK(l1 == l2 && r1 == REF && r2 == REF);
    const uint64_t s1 =
        least_quota_objective(sg, x, CANON_OBJECTIVE_SIGNED_CANONICAL_IMAGE, NULL, REF, &r1);
    const uint64_t s2 =
        least_quota_objective(sg, x, CANON_OBJECTIVE_SIGNED_CANONICAL_IMAGE, NULL, PRUNE, &r2);
    CHECK(s1 == s2 && r1 == REF && r2 == REF);
    /* internally: the consumers' P1 runs are the reference traversal */
    canon_root xr;
    subset_root(&xr, 4, NULL, 0);
    canon_p1_search p1;
    canon_p1_search_init(&p1);
    canon_obj_search obj;
    canon_obj_search_init(&obj);
    canon_obj_outcome o;
    CHECK(canon_p1_search_run_policy(&p1, g, &xr, BIG, REF) == CANON_COMPLETE);
    const uint64_t unpruned = p1.nodes;
    CHECK(canon_p1_search_run_policy(&p1, g, &xr, BIG, PRUNE) == CANON_COMPLETE);
    CHECK(p1.nodes < unpruned);
    CHECK(canon_obj_labeling(&obj, &p1, g, &xr, rho, BIG, &o) == CANON_COMPLETE);
    CHECK(obj.stats.p1_nodes == unpruned && p1.work_policy == REF && p1.pruned == 0);
    CHECK(canon_p1_search_run_policy(&p1, g, &xr, BIG, PRUNE) == CANON_COMPLETE);
    CHECK(canon_obj_signed(&obj, &p1, sg, &xr, BIG, &o) == CANON_COMPLETE);
    CHECK(p1.work_policy == REF && p1.pruned == 0);
    CHECK(o.p1 && obj.stats.p1_nodes == unpruned);
    canon_obj_search_free(&obj);
    canon_p1_search_free(&p1);
    canon_root_free(&xr);
    canon_object_release(x);
    canon_group_release(sg);
    canon_group_release(g);
}

/* ---- 7: what the explored count depends on ---- */

static uint64_t pruned_nodes(const canon_group *g, const canon_root *x)
{
    canon_p1_search s;
    canon_p1_search_init(&s);
    CHECK(canon_p1_search_run_policy(&s, g, x, BIG, PRUNE) == CANON_COMPLETE);
    const uint64_t nodes = s.nodes;
    canon_p1_search_free(&s);
    return nodes;
}

static void test_count_invariance(void)
{
    uint64_t checked = 0;
    for (int round = 0; round < 24; ++round) {
        const uint32_t n = 4 + (uint32_t)(round % 3);
        const uint32_t count = 2 + (uint32_t)(check_rng() % 2);
        uint32_t gens[3 * 6], more[8 * 6];
        for (uint32_t i = 0; i < count; ++i) {
            random_perm(n, gens + i * n);
        }
        /* x: a union of cycles of the first generator */
        uint32_t atoms[6], k = 0, seen = 0;
        for (uint32_t v = 0; v < n; ++v) {
            const bool take = check_rng() % 2 == 0;
            for (uint32_t w = v; !(seen >> w & 1u); w = gens[w]) {
                seen |= 1u << w;
                if (take) {
                    atoms[k++] = w;
                }
            }
        }
        canon_root x;
        subset_root(&x, n, atoms, k);
        canon_group *g = make_group(0, n, gens, count);
        const uint64_t base = pruned_nodes(g, &x);
        canon_group_release(g);
        /* reversed order, every generator twice, and the identity: the same A_known */
        uint32_t m = 0;
        for (uint32_t v = 0; v < n; ++v) {
            more[m * n + v] = v;
        }
        ++m;
        for (uint32_t i = count; i-- > 0;) {
            memcpy(more + m++ * n, gens + i * n, n * sizeof *gens);
            memcpy(more + m++ * n, gens + i * n, n * sizeof *gens);
        }
        for (int backend = 0; backend < 2; ++backend) {
            g = make_group(backend, n, more, m);
            CHECK(pruned_nodes(g, &x) == base);
            canon_group_release(g);
            g = make_group(backend, n, gens, count);
            CHECK(pruned_nodes(g, &x) == base);
            canon_group_release(g);
        }
        checked += 1;
        canon_root_free(&x);
    }
    /* The generating set matters (docs/pruning-rules.md, the presentation question): C_4 on 4
     * points with x = {0, 2}.  The greedy generator c = (0 1 2 3) moves x, so A_known = 1;
     * listing every element includes c^2 = (0 2)(1 3), which fixes x, so A_known = <c^2>. */
    const uint32_t c[4] = {1, 2, 3, 0};
    uint32_t all[4 * 4];
    for (uint32_t v = 0; v < 4; ++v) {
        all[v] = v;           /* identity */
        all[4 + v] = c[v];    /* c */
        all[8 + v] = c[c[v]]; /* c^2: (c c)[v] = c[c[v]] (spec 3: left factor acts first) */
    }
    for (uint32_t v = 0; v < 4; ++v) {
        all[12 + v] = all[8 + c[v]]; /* c^3 = c c^2: (c c^2)[v] = c^2[c[v]] */
    }
    CHECK(all[8] == 2 && all[9] == 3 && all[10] == 0 && all[11] == 1);
    CHECK(all[12] == 3 && all[13] == 0 && all[14] == 1 && all[15] == 2);
    const uint32_t two[2] = {0, 2};
    canon_root x;
    subset_root(&x, 4, two, 2);
    canon_object *xo = NULL;
    CHECK(canon_object_create_subset(CTX[0], 4, two, 2, &xo) == CANON_COMPLETE);
    canon_group *greedy = make_group(0, 4, c, 1), *full = make_group(0, 4, all, 4);
    const uint64_t ng = pruned_nodes(greedy, &x), nf = pruned_nodes(full, &x);
    CHECK(nf < ng);
    canon_status st;
    canon_result *rg = solve(CTX[0], WS, greedy, xo, PRUNE, CANON_WITNESS_ANY, 0, &st);
    canon_result *rf = solve(CTX[0], WS, full, xo, PRUNE, CANON_WITNESS_ANY, 0, &st);
    CHECK(same_bytes(canon_result_trace, rg, rf) && same_bytes(canon_result_bytes, rg, rf));
    canon_result_release(rg);
    canon_result_release(rf);
    printf("explored count: %llu random presentations invariant under order, duplicates, "
           "identity and backend; C4 on {0,2}: %llu (greedy) vs %llu (all elements)\n",
           (unsigned long long)checked, (unsigned long long)ng, (unsigned long long)nf);
    canon_object_release(xo);
    canon_group_release(greedy);
    canon_group_release(full);
    canon_root_free(&x);
}

/* ---- 8: workspace reuse across policies and objectives ---- */

static void test_reuse(void)
{
    const uint32_t gens[8] = {1, 0, 2, 3, 1, 2, 3, 0}, rho[4] = {0, 1, 2, 3}, one = 1;
    canon_group *g = make_group(0, 4, gens, 2);
    canon_object *x = NULL;
    CHECK(canon_object_create_subset(CTX[0], 4, &one, 1, &x) == CANON_COMPLETE);
    canon_workspace *ws = NULL;
    CHECK(canon_workspace_create(CTX[0], &ws) == CANON_COMPLETE);
    canon_status st;
    canon_result *fresh_pruned = solve(CTX[0], WS, g, x, PRUNE, CANON_WITNESS_ANY, 0, &st);
    canon_result *fresh_ref = solve(CTX[0], WS, g, x, REF, CANON_WITNESS_ANY, 0, &st);
    for (int round = 0; round < 2; ++round) {
        canon_result *a = solve(CTX[0], ws, g, x, PRUNE, CANON_WITNESS_ANY, 0, &st);
        canon_result *b = solve(CTX[0], ws, g, x, REF, CANON_WITNESS_ANY, 0, &st);
        CHECK(same_bytes(canon_result_bytes, a, fresh_pruned) && same_witness(a, fresh_pruned));
        CHECK(same_bytes(canon_result_bytes, b, fresh_ref) && same_witness(b, fresh_ref));
        /* a labeling in between (unpruned P1 in the same workspace) */
        canon_capacity cap = {0, 0, 0, 0, 0, 0, 0, 0};
        canon_problem_options opts = {CANON_WITNESS_ANY, rho};
        canon_problem *p = NULL;
        canon_result *l = NULL;
        CHECK(canon_problem_create_with_options(CTX[0], g, x, NULL,
                                                CANON_OBJECTIVE_CANONICAL_LABELING_COSET,
                                                CANON_PROFILE_P1, CANON_ENCODING_CDAG_2,
                                                CANON_ORDER_CDAG_BYTE_1, &cap, &opts,
                                                &p) == CANON_COMPLETE);
        CHECK(canon_solve(ws, p, &l) == CANON_COMPLETE);
        CHECK(same_bytes(canon_result_bytes, l, a) && same_bytes(canon_result_trace, l, a));
        canon_problem_release(p);
        canon_result_release(l);
        canon_result_release(a);
        canon_result_release(b);
    }
    canon_result_release(fresh_pruned);
    canon_result_release(fresh_ref);
    canon_workspace_release(ws);
    canon_object_release(x);
    canon_group_release(g);
}

int main(void)
{
    const canon_context_options chain = {CANON_BACKEND_CHAIN};
    const canon_context_options explicit_backend = {CANON_BACKEND_EXPLICIT};
    CHECK(canon_context_create_with_options(NULL, &chain, &CTX[0]) == CANON_COMPLETE);
    CHECK(canon_context_create_with_options(NULL, &explicit_backend, &CTX[1]) == CANON_COMPLETE);
    CHECK(canon_workspace_create(CTX[0], &WS) == CANON_COMPLETE);
    test_count_drop();
    test_quota();
    test_descriptor();
    test_objectives_unpruned();
    test_count_invariance();
    test_reuse();
    test_t1_subsets();
    test_t1_graphs();
    test_t1_dags();
    test_random_groups();
    canon_workspace_release(WS);
    canon_context_release(CTX[0]);
    canon_context_release(CTX[1]);
    return check_finish("test_prune");
}
