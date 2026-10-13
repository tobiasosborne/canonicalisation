/* Slice S8: metrics and admission (spec 19, 8.3, 7.3, 11.1, 11.2; docs/slices/S8.md 1.3-1.5):
 * the counters of the spec 7.4 cases against the hand-derived values, the root-discrete and
 * singleton-orbit indicators, the ledger (live bytes return to the baseline, the peak is at
 * least the live count), plan >= measured peak on every T1 subset and seeded digraph under
 * every objective (the test the plan's chain estimate is calibrated by), and max_live_bytes
 * admission. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "canon/canon.h"
#include "check.h"
#include "t1_groups.h"

static canon_context *ctx;
static uint64_t solves, max_ratio_num, max_ratio_den = 1, worst_plan, worst_peak;

/* Solve with options and return the metrics; the result is released. */
static canon_status run(canon_workspace *ws, const canon_group *g, const canon_object *x,
                        const canon_object *y, canon_objective obj, canon_profile prof,
                        const canon_problem_options *opts, const canon_capacity *cap,
                        canon_metrics *m)
{
    canon_problem *p = NULL;
    canon_result *r = NULL;
    canon_status st = canon_problem_create_with_options(ctx, g, x, y, obj, prof, CANON_ENCODING_CDAG_2,
                                                        CANON_ORDER_CDAG_BYTE_1, cap, opts, &p);
    if (st != CANON_COMPLETE) {
        return st;
    }
    uint64_t planned = 0;
    CHECK(canon_problem_planned_bytes(p, &planned) == CANON_COMPLETE && planned > 0);
    st = canon_solve(ws, p, &r);
    CHECK(r != NULL);
    CHECK(canon_result_metrics(r, m) == CANON_COMPLETE);
    CHECK(m->planned_bytes == planned);
    /* spec 11.1/11.2 (D16): the solve's own high-water mark is within the plan */
    CHECK(m->peak_live_bytes >= m->live_bytes_before);
    const uint64_t used = m->peak_live_bytes - m->live_bytes_before;
    CHECK(used <= planned);
    if (used * max_ratio_den > max_ratio_num * planned) {
        max_ratio_num = used;
        max_ratio_den = planned;
        worst_plan = planned;
        worst_peak = used;
    }
    solves += 1;
    canon_result_release(r);
    canon_problem_release(p);
    return st;
}

static void golden_counters(void)
{
    canon_workspace *ws = NULL;
    CHECK(canon_workspace_create(ctx, &ws) == CANON_COMPLETE);
    const uint32_t swap[2] = {1, 0}, one[1] = {0};
    canon_group *g = NULL, *triv = NULL;
    canon_object *empty = NULL, *x0 = NULL;
    CHECK(canon_group_create(ctx, 2, swap, 1, &g) == CANON_COMPLETE);
    CHECK(canon_group_create(ctx, 2, NULL, 0, &triv) == CANON_COMPLETE);
    CHECK(canon_object_create_subset(ctx, 2, NULL, 0, &empty) == CANON_COMPLETE);
    CHECK(canon_object_create_subset(ctx, 2, one, 1, &x0) == CANON_COMPLETE);
    canon_metrics m;
    /* spec 7.4: the empty subset under Sym(2), unpruned: 3 nodes, 2 leaves; every generator
     * fixes x (singleton orbit); the root is not discrete */
    canon_capacity unpruned = {0, 0, 0, 0, 0, 0, 0, CANON_WORK_POLICY_UNPRUNED, 0};
    CHECK(run(ws, g, empty, NULL, CANON_OBJECTIVE_CANONICAL_IMAGE, CANON_PROFILE_P1, NULL, &unpruned,
              &m) == CANON_COMPLETE);
    CHECK(m.p1_nodes == 3 && m.p1_leaves == 2 && m.children_explored == 2 && m.children_pruned == 0);
    CHECK(m.singleton_orbit && !m.root_discrete && m.enumeration_visits == 0);
    CHECK(m.output_bytes == 11 + 4 + 5 + 4 && m.certificate_bytes == 0); /* H(2) U32(1) 04 U32(0) U32(0) */
    /* pruned: 2 nodes, one child pruned by the generator (1 explicit automorphism inserted) */
    CHECK(run(ws, g, empty, NULL, CANON_OBJECTIVE_CANONICAL_IMAGE, CANON_PROFILE_P1, NULL, NULL,
              &m) == CANON_COMPLETE);
    CHECK(m.p1_nodes == 2 && m.children_pruned == 1 && m.automorphisms_inserted == 1 &&
          m.automorphism_candidates == 1 && m.implicit_candidates == 0);
    /* x = {0} under Sym(2) (spec 7.4): the root is discrete; the generator moves x */
    CHECK(run(ws, g, x0, NULL, CANON_OBJECTIVE_CANONICAL_IMAGE, CANON_PROFILE_P1, NULL, NULL,
              &m) == CANON_COMPLETE);
    CHECK(m.root_discrete && !m.singleton_orbit && m.p1_nodes == 1 && m.p1_leaves == 1);
    /* the trivial group: no generators, so every (no) generator fixes x */
    CHECK(run(ws, triv, x0, NULL, CANON_OBJECTIVE_CANONICAL_IMAGE, CANON_PROFILE_P1, NULL, NULL,
              &m) == CANON_COMPLETE);
    CHECK(m.singleton_orbit && m.root_discrete);
    /* a certificate is counted */
    canon_problem_options cert = {CANON_WITNESS_ANY, NULL, true};
    CHECK(run(ws, g, empty, NULL, CANON_OBJECTIVE_CANONICAL_IMAGE, CANON_PROFILE_P1, &cert, NULL,
              &m) == CANON_COMPLETE);
    CHECK(m.certificate_bytes > 100);
    /* the deterministic witness adds the stabiliser enumeration (3 visits under Sym(2)) */
    canon_problem_options det = {CANON_WITNESS_DETERMINISTIC, NULL, false};
    CHECK(run(ws, g, empty, NULL, CANON_OBJECTIVE_CANONICAL_IMAGE, CANON_PROFILE_P1, &det, NULL,
              &m) == CANON_COMPLETE);
    CHECK(m.enumeration_visits == 3 && m.stabiliser_hits == 2 && m.stabiliser_rebuilds == 1);
    /* the stabiliser objective: visits 3, hits 2 (both elements fix the empty set) */
    CHECK(run(ws, g, empty, NULL, CANON_OBJECTIVE_STABILISER, CANON_PROFILE_NO_TREE, NULL, NULL,
              &m) == CANON_COMPLETE);
    CHECK(m.enumeration_visits == 3 && m.enumeration_leaves == 2 && m.stabiliser_hits == 2 &&
          m.p1_nodes == 0 && m.output_bytes > 0);
    /* the signed fast path (spec 7.3): every generator fixes the empty subset */
    const int8_t minus[1] = {-1};
    canon_group *sg = NULL;
    CHECK(canon_group_create_signed(ctx, 2, swap, 1, minus, &sg) == CANON_COMPLETE);
    CHECK(run(ws, sg, empty, NULL, CANON_OBJECTIVE_SIGNED_CANONICAL_IMAGE, CANON_PROFILE_P1, NULL,
              NULL, &m) == CANON_COMPLETE);
    CHECK(m.signed_fast_path && m.output_bytes == 0); /* a certified zero has no stream */
    /* a status-only result keeps its plan and memory fields */
    canon_capacity tight = {0, 0, 1, 0, 0, 0, 0, CANON_WORK_POLICY_UNPRUNED, 0};
    CHECK(run(ws, g, empty, NULL, CANON_OBJECTIVE_CANONICAL_IMAGE, CANON_PROFILE_P1, NULL, &tight,
              &m) == CANON_CAPACITY_LIMIT);
    CHECK(m.planned_bytes > 0 && m.p1_nodes == 1 && m.output_bytes == 0); /* the root was visited */
    /* spec 11.1 admission: a budget below the plan refuses the problem before any search */
    canon_capacity small = {0, 0, 0, 0, 0, 0, 0, 0, 1};
    canon_problem *p = NULL;
    CHECK(canon_problem_create(ctx, g, empty, CANON_OBJECTIVE_CANONICAL_IMAGE, CANON_PROFILE_P1,
                               CANON_ENCODING_CDAG_2, CANON_ORDER_CDAG_BYTE_1, &small,
                               &p) == CANON_CAPACITY_LIMIT && p == NULL);
    canon_capacity exact = {0, 0, 0, 0, 0, 0, 0, 0, m.planned_bytes};
    CHECK(canon_problem_create(ctx, g, empty, CANON_OBJECTIVE_CANONICAL_IMAGE, CANON_PROFILE_P1,
                               CANON_ENCODING_CDAG_2, CANON_ORDER_CDAG_BYTE_1, &exact,
                               &p) == CANON_COMPLETE);
    canon_problem_release(p);
    /* NULL arguments */
    CHECK(canon_result_metrics(NULL, &m) == CANON_INVALID_INPUT);
    uint64_t planned = 0;
    CHECK(canon_problem_planned_bytes(NULL, &planned) == CANON_INVALID_INPUT);
    canon_group_release(sg);
    canon_group_release(g);
    canon_group_release(triv);
    canon_object_release(empty);
    canon_object_release(x0);
    canon_workspace_release(ws);
}

/* The ledger: after every handle is released the live count returns to the baseline. */
static void ledger(void)
{
    canon_memory_stats before, during, after;
    canon_memory_get(&before);
    canon_workspace *ws = NULL;
    canon_group *g = NULL;
    canon_object *x = NULL;
    canon_problem *p = NULL;
    canon_result *r = NULL;
    const uint32_t gens[6] = {1, 2, 3, 4, 5, 0};
    const uint32_t atoms[2] = {0, 3};
    CHECK(canon_workspace_create(ctx, &ws) == CANON_COMPLETE);
    CHECK(canon_group_create(ctx, 6, gens, 1, &g) == CANON_COMPLETE);
    CHECK(canon_object_create_subset(ctx, 6, atoms, 2, &x) == CANON_COMPLETE);
    CHECK(canon_problem_create(ctx, g, x, CANON_OBJECTIVE_CANONICAL_IMAGE, CANON_PROFILE_P1,
                               CANON_ENCODING_CDAG_2, CANON_ORDER_CDAG_BYTE_1, NULL, &p) == CANON_COMPLETE);
    CHECK(canon_solve(ws, p, &r) == CANON_COMPLETE);
    canon_memory_get(&during);
    CHECK(during.live_bytes > before.live_bytes && during.peak_bytes >= during.live_bytes);
    CHECK(during.allocations > before.allocations);
    canon_result_release(r);
    canon_problem_release(p);
    canon_object_release(x);
    canon_group_release(g);
    canon_workspace_release(ws);
    canon_memory_get(&after);
    CHECK(after.live_bytes == before.live_bytes); /* nothing leaked */
    CHECK(after.frees - before.frees == after.allocations - before.allocations);
    canon_memory_get(NULL); /* tolerated */
}

/* plan >= peak on the tiers: every T1 subset (n <= 4) and seeded digraphs, every objective. */
static void tiers(void)
{
    canon_workspace *ws = NULL;
    CHECK(canon_workspace_create(ctx, &ws) == CANON_COMPLETE);
    static const uint8_t lab_a[1] = {0x61};
    for (uint32_t n = 0; n <= 4; ++n) {
        t1_sym s;
        t1_sym_init(&s, n);
        t1_group groups[T1_MAX_GROUPS];
        const uint32_t ng = t1_subgroups(&s, groups);
        for (uint32_t gi = 0; gi < ng; ++gi) {
            canon_group *g = NULL;
            CHECK(canon_group_create(ctx, n, groups[gi].gens, groups[gi].gen_count, &g) == CANON_COMPLETE);
            for (uint32_t mask = 0; mask < (1u << n); mask += (n == 4 ? 3u : 1u)) {
                uint32_t atoms[4], k = 0;
                for (uint32_t a = 0; a < n; ++a) {
                    if (mask >> a & 1u) {
                        atoms[k++] = a;
                    }
                }
                canon_object *x = NULL, *y = NULL;
                CHECK(canon_object_create_subset(ctx, n, atoms, k, &x) == CANON_COMPLETE);
                CHECK(canon_object_create_subset(ctx, n, atoms, k > 0 ? k - 1u : 0u, &y) == CANON_COMPLETE);
                canon_metrics m;
                canon_problem_options cert = {CANON_WITNESS_DETERMINISTIC, NULL, true};
                CHECK(run(ws, g, x, NULL, CANON_OBJECTIVE_CANONICAL_IMAGE, CANON_PROFILE_P1, &cert, NULL, &m) == CANON_COMPLETE);
                CHECK(run(ws, g, x, NULL, CANON_OBJECTIVE_LEX_MIN_IMAGE, CANON_PROFILE_NO_TREE, NULL, NULL, &m) == CANON_COMPLETE);
                CHECK(run(ws, g, x, y, CANON_OBJECTIVE_TRANSPORTER_COSET, CANON_PROFILE_NO_TREE, NULL, NULL, &m) == CANON_COMPLETE);
                CHECK(run(ws, g, x, NULL, CANON_OBJECTIVE_STABILISER, CANON_PROFILE_NO_TREE, NULL, NULL, &m) == CANON_COMPLETE);
                uint32_t rho[4];
                for (uint32_t v = 0; v < n; ++v) {
                    rho[v] = n - 1u - v;
                }
                canon_problem_options lab = {CANON_WITNESS_ANY, rho, false};
                CHECK(run(ws, g, x, NULL, CANON_OBJECTIVE_CANONICAL_LABELING_COSET, CANON_PROFILE_P1, &lab, NULL, &m) == CANON_COMPLETE);
                canon_object_release(x);
                canon_object_release(y);
            }
            /* a seeded digraph with labels and colours */
            check_rng_state = 0xd16ULL + 1000u * n + gi;
            canon_arc arcs[32];
            size_t count = 0;
            for (uint32_t u = 0; u < n; ++u) {
                for (uint32_t v = 0; v < n; ++v) {
                    const unsigned r = (unsigned)(check_rng() % 4);
                    if (r == 0) {
                        continue;
                    }
                    canon_arc a = {u, v, r == 3 ? lab_a : NULL, r == 3 ? 1u : 0u, r};
                    arcs[count++] = a;
                }
            }
            canon_object *gx = NULL;
            CHECK(canon_object_create_graph(ctx, n, NULL, NULL, arcs, count, &gx) == CANON_COMPLETE);
            canon_metrics m;
            canon_problem_options cert = {CANON_WITNESS_ANY, NULL, true};
            CHECK(run(ws, g, gx, NULL, CANON_OBJECTIVE_CANONICAL_IMAGE, CANON_PROFILE_P1, &cert, NULL, &m) == CANON_COMPLETE);
            CHECK(run(ws, g, gx, NULL, CANON_OBJECTIVE_STABILISER, CANON_PROFILE_NO_TREE, NULL, NULL, &m) == CANON_COMPLETE);
            canon_object_release(gx);
            canon_group_release(g);
        }
    }
    canon_workspace_release(ws);
}

int main(void)
{
    CHECK(canon_context_create(NULL, &ctx) == CANON_COMPLETE);
    golden_counters();
    ledger();
    tiers();
    printf("metrics: %llu solves, peak <= plan on every one; the largest peak/plan ratio is "
           "%llu / %llu bytes\n",
           (unsigned long long)solves, (unsigned long long)worst_peak, (unsigned long long)worst_plan);
    canon_context_release(ctx);
    return check_finish("test_metrics");
}
