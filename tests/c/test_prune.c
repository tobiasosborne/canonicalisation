/* Slice S7: pruned versus unpruned P1 runs (docs/pruning-rules.md; docs/slices/S7.md 3.1,
 * 3.2, 4) through the public API: on every T1 subset and on seeded digraphs, under both group
 * backends, the two work policies give the same trace and bytes (any witness mode) and the
 * same witness (deterministic mode); node counts are logged and checked on the cases the
 * brief names.  The internal search is used for the counters. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "bsgs/group.h"
#include "canon/canon.h"
#include "check.h"
#include "object/object.h"
#include "search/p1_tree.h"
#include "t1_groups.h"

static canon_context *ctx_of[2];

static canon_result *solve(canon_context *ctx, canon_workspace *ws, const canon_group *g,
                           const canon_object *x, uint16_t policy, canon_witness_mode mode,
                           uint64_t quota, canon_status *st)
{
    canon_capacity cap = {0, 0, quota, 0, 0, 0, 0, policy};
    canon_problem_options opts = {mode, NULL, false};
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

static uint64_t cases, det_cases;

/* The pair of runs for one (group, object): identical trace and bytes; identical witness in
 * deterministic mode; a valid witness in any mode under both policies. */
static void compare(canon_context *ctx, canon_workspace *ws, const canon_group *g,
                    const canon_object *x, uint32_t n, bool deterministic_too)
{
    canon_status st1, st2;
    canon_result *off = solve(ctx, ws, g, x, CANON_WORK_POLICY_UNPRUNED, CANON_WITNESS_ANY, 0, &st1);
    canon_result *on = solve(ctx, ws, g, x, CANON_WORK_POLICY_PRUNE_1, CANON_WITNESS_ANY, 0, &st2);
    CHECK(st1 == CANON_COMPLETE && st2 == CANON_COMPLETE);
    CHECK(same_bytes(canon_result_trace, off, on));
    CHECK(same_bytes(canon_result_bytes, off, on));
    bool valid = false;
    CHECK(canon_result_verify_witness(on, &valid) == CANON_COMPLETE && valid);
    CHECK(canon_result_evidence(on) == CANON_EVIDENCE_TRUSTED_ENGINE);
    cases += 1;
    canon_result_release(off);
    canon_result_release(on);
    if (!deterministic_too) {
        return;
    }
    off = solve(ctx, ws, g, x, CANON_WORK_POLICY_UNPRUNED, CANON_WITNESS_DETERMINISTIC, 0, &st1);
    on = solve(ctx, ws, g, x, CANON_WORK_POLICY_PRUNE_1, CANON_WITNESS_DETERMINISTIC, 0, &st2);
    CHECK(st1 == CANON_COMPLETE && st2 == CANON_COMPLETE);
    uint32_t d1 = 0, d2 = 0;
    const uint32_t *w1 = canon_result_witness(off, &d1), *w2 = canon_result_witness(on, &d2);
    CHECK(w1 != NULL && w2 != NULL && d1 == n && d2 == n &&
          (n == 0 || memcmp(w1, w2, n * sizeof *w1) == 0));
    det_cases += 1;
    canon_result_release(off);
    canon_result_release(on);
}

static void t1_subsets(void)
{
    for (int b = 0; b < 2; ++b) {
        canon_workspace *ws = NULL;
        CHECK(canon_workspace_create(ctx_of[b], &ws) == CANON_COMPLETE);
        for (uint32_t n = 0; n <= 4; ++n) {
            t1_sym s;
            t1_sym_init(&s, n);
            t1_group groups[T1_MAX_GROUPS];
            const uint32_t ng = t1_subgroups(&s, groups);
            for (uint32_t gi = 0; gi < ng; ++gi) {
                canon_group *g = NULL;
                CHECK(canon_group_create(ctx_of[b], n, groups[gi].gens, groups[gi].gen_count, &g) ==
                      CANON_COMPLETE);
                for (uint32_t mask = 0; mask < (1u << n); ++mask) {
                    uint32_t atoms[4], k = 0;
                    for (uint32_t a = 0; a < n; ++a) {
                        if (mask >> a & 1u) {
                            atoms[k++] = a;
                        }
                    }
                    canon_object *x = NULL;
                    CHECK(canon_object_create_subset(ctx_of[b], n, atoms, k, &x) == CANON_COMPLETE);
                    compare(ctx_of[b], ws, g, x, n, true);
                    canon_object_release(x);
                }
                canon_group_release(g);
            }
        }
        canon_workspace_release(ws);
    }
}

/* Seeded digraphs on n <= 4 under every T1 subgroup: multiplicities in {0, 1, 2}, labels from
 * two strings, colours from two strings (the G1/G2 shapes of tests/python/test_e2e.py). */
static void digraphs(void)
{
    static const uint8_t lab_a[1] = {0x61}, col_c[1] = {0x63};
    for (int b = 0; b < 2; ++b) {
        canon_workspace *ws = NULL;
        CHECK(canon_workspace_create(ctx_of[b], &ws) == CANON_COMPLETE);
        for (uint32_t n = 1; n <= 4; ++n) {
            t1_sym s;
            t1_sym_init(&s, n);
            t1_group groups[T1_MAX_GROUPS];
            const uint32_t ng = t1_subgroups(&s, groups);
            for (uint32_t gi = 0; gi < ng; ++gi) {
                canon_group *g = NULL;
                CHECK(canon_group_create(ctx_of[b], n, groups[gi].gens, groups[gi].gen_count, &g) ==
                      CANON_COMPLETE);
                for (int rep = 0; rep < 6; ++rep) {
                    check_rng_state = 0x5eed0000ULL + 1000u * n + 37u * gi + (unsigned)rep + 1u;
                    canon_arc arcs[32];
                    size_t count = 0;
                    const uint8_t *colours[4];
                    size_t lengths[4];
                    for (uint32_t v = 0; v < n; ++v) {
                        const int c = check_rng() % 2 == 0;
                        colours[v] = c ? col_c : NULL;
                        lengths[v] = c ? 1 : 0;
                    }
                    for (uint32_t u = 0; u < n; ++u) {
                        for (uint32_t v = 0; v < n; ++v) {
                            for (int l = 0; l < 2; ++l) {
                                const unsigned m = (unsigned)(check_rng() % 3);
                                if (m == 0) {
                                    continue;
                                }
                                canon_arc a = {u, v, l ? lab_a : NULL, l ? 1u : 0u, m};
                                arcs[count++] = a;
                            }
                        }
                    }
                    canon_object *x = NULL;
                    CHECK(canon_object_create_graph(ctx_of[b], n, colours, lengths, arcs, count,
                                                    &x) == CANON_COMPLETE);
                    compare(ctx_of[b], ws, g, x, n, rep < 2);
                    canon_object_release(x);
                }
                canon_group_release(g);
            }
        }
        canon_workspace_release(ws);
    }
}

/* Node counts through the internal search: the empty subset under Sym(2) (spec 7.4) has 3
 * nodes unpruned and 2 pruned; under Sym(6), 1237 unpruned (1 + 6 + 30 + 120 + 360 + 720:
 * after five individualisations the partition is discrete) and 6 pruned (one child per
 * level: the orbit count); the pruned count is the CAPACITY_LIMIT boundary of the pruned
 * run. */
static void node_counts(void)
{
    const struct {
        uint32_t n;
        uint64_t unpruned, pruned;
    } want[] = {{2, 3, 2}, {6, 1237, 6}};
    for (size_t i = 0; i < sizeof want / sizeof *want; ++i) {
        const uint32_t n = want[i].n;
        uint32_t gens[12];
        for (uint32_t v = 0; v < n; ++v) {
            gens[v] = (v + 1u) % n; /* an n-cycle */
            gens[n + v] = v < 2 ? 1u - v : v; /* the transposition (0 1) */
        }
        canon_group *g = NULL;
        CHECK(canon_group_create(ctx_of[0], n, gens, 2, &g) == CANON_COMPLETE);
        canon_root x;
        memset(&x, 0, sizeof x);
        x.kind = CANON_ROOT_SUBSET;
        x.n = n;
        CHECK(canon_subset_init(&x.u.subset, n, NULL, 0) == CANON_COMPLETE);
        canon_p1_search s;
        canon_p1_search_init(&s);
        const canon_p1_run_options prune = {true, NULL};
        CHECK(canon_p1_search_run(&s, g, &x, 1u << 20, NULL) == CANON_COMPLETE);
        CHECK(s.nodes == want[i].unpruned && s.pruned == 0);
        CHECK(canon_p1_search_run(&s, g, &x, 1u << 20, &prune) == CANON_COMPLETE);
        CHECK(s.nodes == want[i].pruned);
        CHECK(s.explored == s.nodes - 1u); /* every explored child is a node */
        printf("empty subset under Sym(%u): nodes unpruned %llu, pruned %llu (pruned children "
               "%llu, rebases %llu)\n",
               (unsigned)n, (unsigned long long)want[i].unpruned, (unsigned long long)s.nodes,
               (unsigned long long)s.pruned, (unsigned long long)s.sym.stats.rebases);
        /* spec 11.1: the quota under policy 0x0002 is the explored-node count */
        CHECK(canon_p1_search_run(&s, g, &x, want[i].pruned - 1u, &prune) == CANON_CAPACITY_LIMIT);
        CHECK(canon_p1_search_run(&s, g, &x, want[i].pruned, &prune) == CANON_COMPLETE);
        canon_p1_search_free(&s);
        canon_root_free(&x);
        canon_group_release(g);
    }
}

/* API statuses: an unknown work policy is unsupported; the default policy is the prune. */
static void statuses(void)
{
    const uint32_t swap[2] = {1, 0};
    canon_group *g = NULL;
    canon_object *x = NULL;
    canon_workspace *ws = NULL;
    CHECK(canon_group_create(ctx_of[0], 2, swap, 1, &g) == CANON_COMPLETE);
    CHECK(canon_object_create_subset(ctx_of[0], 2, NULL, 0, &x) == CANON_COMPLETE);
    CHECK(canon_workspace_create(ctx_of[0], &ws) == CANON_COMPLETE);
    canon_status st = CANON_COMPLETE;
    canon_result *r = solve(ctx_of[0], ws, g, x, 3, CANON_WITNESS_ANY, 0, &st);
    CHECK(st == CANON_UNSUPPORTED_ACTION && r == NULL);
    /* default policy: the tree of the spec 7.4 empty-subset case has 2 explored nodes */
    r = solve(ctx_of[0], ws, g, x, 0, CANON_WITNESS_ANY, 2, &st);
    CHECK(st == CANON_COMPLETE);
    canon_result_release(r);
    r = solve(ctx_of[0], ws, g, x, 0, CANON_WITNESS_ANY, 1, &st);
    CHECK(st == CANON_CAPACITY_LIMIT);
    canon_result_release(r);
    r = solve(ctx_of[0], ws, g, x, CANON_WORK_POLICY_UNPRUNED, CANON_WITNESS_ANY, 2, &st);
    CHECK(st == CANON_CAPACITY_LIMIT);
    canon_result_release(r);
    /* a context default for the policy is honoured */
    canon_capacity defaults = {0, 0, 0, 0, 0, 0, 0, CANON_WORK_POLICY_UNPRUNED};
    canon_context *uctx = NULL;
    CHECK(canon_context_create(&defaults, &uctx) == CANON_COMPLETE);
    canon_group *ug = NULL;
    canon_object *ux = NULL;
    CHECK(canon_group_create(uctx, 2, swap, 1, &ug) == CANON_COMPLETE);
    CHECK(canon_object_create_subset(uctx, 2, NULL, 0, &ux) == CANON_COMPLETE);
    r = solve(uctx, ws, ug, ux, 0, CANON_WITNESS_ANY, 2, &st);
    CHECK(st == CANON_CAPACITY_LIMIT);
    canon_result_release(r);
    canon_group_release(ug);
    canon_object_release(ux);
    canon_context_release(uctx);
    canon_workspace_release(ws);
    canon_object_release(x);
    canon_group_release(g);
}

int main(void)
{
    const canon_context_options explicit_backend = {CANON_BACKEND_EXPLICIT};
    CHECK(canon_context_create(NULL, &ctx_of[0]) == CANON_COMPLETE);
    CHECK(canon_context_create_with_options(NULL, &explicit_backend, &ctx_of[1]) == CANON_COMPLETE);
    t1_subsets();
    digraphs();
    node_counts();
    statuses();
    printf("pruned versus unpruned: %llu cases (any witness), %llu (deterministic), both "
           "backends\n",
           (unsigned long long)cases, (unsigned long long)det_cases);
    canon_context_release(ctx_of[0]);
    canon_context_release(ctx_of[1]);
    return check_finish("test_prune");
}
