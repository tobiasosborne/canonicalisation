/* Slice S8: canon_solve_batch (spec 17 v2.1; docs/slices/S8-batch-reading.md 5, S8.md 1.1):
 * byte identity with separate solves on every T1 objective, a batch of 0, an invalid input
 * among valid ones, a NULL entry, NULL arrays, a per-input quota, certificates in a batch. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "canon/canon.h"
#include "check.h"
#include "t1_groups.h"

static canon_context *ctx;

static int same(const uint8_t *(*get)(const canon_result *, size_t *), const canon_result *a,
                const canon_result *b)
{
    size_t la = 0, lb = 0;
    const uint8_t *pa = get(a, &la), *pb = get(b, &lb);
    return (pa == NULL) == (pb == NULL) && la == lb && (la == 0 || memcmp(pa, pb, la) == 0);
}

static int same_witness(const canon_result *a, const canon_result *b)
{
    uint32_t da = 0, db = 0;
    const uint32_t *wa = canon_result_witness(a, &da), *wb = canon_result_witness(b, &db);
    return (wa == NULL) == (wb == NULL) && da == db && (da == 0 || memcmp(wa, wb, da * sizeof *wa) == 0);
}

/* The result of a batch entry equals the result of a separate solve, field by field. */
static void same_results(const canon_result *a, const canon_result *b)
{
    CHECK(canon_result_status(a) == canon_result_status(b));
    canon_result_flags fa = canon_result_get_flags(a), fb = canon_result_get_flags(b);
    CHECK(memcmp(&fa, &fb, sizeof fa) == 0);
    CHECK(same(canon_result_trace, a, b) && same(canon_result_bytes, a, b));
    CHECK(same(canon_result_group_bytes, a, b) && same(canon_result_order_key, a, b));
    CHECK(same(canon_result_certificate, a, b));
    CHECK(same_witness(a, b));
    int sa = 0, sb = 0;
    CHECK(canon_result_sign(a, &sa) == canon_result_sign(b, &sb) && sa == sb);
    canon_metrics ma, mb;
    CHECK(canon_result_metrics(a, &ma) == CANON_COMPLETE && canon_result_metrics(b, &mb) == CANON_COMPLETE);
    CHECK(ma.p1_nodes == mb.p1_nodes && ma.enumeration_visits == mb.enumeration_visits &&
          ma.children_pruned == mb.children_pruned && ma.output_bytes == mb.output_bytes);
}

/* Every objective on x = {0, 2} (or every T1 subset for n <= 3) under group g: one problem per
 * objective, in a batch and one by one. */
static uint64_t batches, entries;

static void batch_of_objectives(canon_workspace *ws, const canon_group *g, uint32_t n,
                                const uint32_t *atoms, uint32_t k)
{
    canon_object *x = NULL, *y = NULL;
    CHECK(canon_object_create_subset(ctx, n, atoms, k, &x) == CANON_COMPLETE);
    uint32_t yat[4];
    for (uint32_t i = 0; i < k; ++i) {
        yat[i] = n - 1u - atoms[i]; /* a target: the reversed subset */
    }
    CHECK(canon_object_create_subset(ctx, n, yat, k, &y) == CANON_COMPLETE);
    uint32_t rho[4];
    for (uint32_t v = 0; v < n; ++v) {
        rho[v] = (v + 1u) % (n > 0 ? n : 1u);
    }
    const struct {
        canon_objective obj;
        canon_profile prof;
        canon_order order;
        bool target, labeling, deterministic, certificate;
    } specs[] = {
        {CANON_OBJECTIVE_CANONICAL_IMAGE, CANON_PROFILE_P1, CANON_ORDER_CDAG_BYTE_1, false, false, false, false},
        {CANON_OBJECTIVE_CANONICAL_IMAGE, CANON_PROFILE_P1, CANON_ORDER_CDAG_BYTE_1, false, false, true, true},
        {CANON_OBJECTIVE_LEX_MIN_IMAGE, CANON_PROFILE_NO_TREE, CANON_ORDER_CDAG_BYTE_1, false, false, true, false},
        {CANON_OBJECTIVE_TRANSPORTER_ONE, CANON_PROFILE_NO_TREE, CANON_ORDER_CDAG_BYTE_1, true, false, false, false},
        {CANON_OBJECTIVE_STABILISER, CANON_PROFILE_NO_TREE, CANON_ORDER_CDAG_BYTE_1, false, false, false, false},
        {CANON_OBJECTIVE_TRANSPORTER_COSET, CANON_PROFILE_NO_TREE, CANON_ORDER_CDAG_BYTE_1, true, false, true, false},
        {CANON_OBJECTIVE_CANONICAL_LABELING_COSET, CANON_PROFILE_P1, CANON_ORDER_CDAG_BYTE_1, false, true, false, false},
    };
    const size_t count = sizeof specs / sizeof *specs;
    canon_problem *problems[8] = {NULL};
    canon_result *results[8] = {NULL}, *alone[8] = {NULL};
    canon_status statuses[8], st_alone[8];
    for (size_t i = 0; i < count; ++i) {
        canon_problem_options opts = {specs[i].deterministic ? CANON_WITNESS_DETERMINISTIC
                                                             : CANON_WITNESS_ANY,
                                      specs[i].labeling ? rho : NULL, specs[i].certificate};
        CHECK(canon_problem_create_with_options(ctx, g, x, specs[i].target ? y : NULL, specs[i].obj,
                                                specs[i].prof, CANON_ENCODING_CDAG_2,
                                                specs[i].order, NULL, &opts,
                                                &problems[i]) == CANON_COMPLETE);
    }
    /* spec 17: "every input is attempted in caller order" and the call is COMPLETE */
    CHECK(canon_solve_batch(ws, (const canon_problem *const *)problems, count, results,
                            statuses) == CANON_COMPLETE);
    for (size_t i = 0; i < count; ++i) {
        st_alone[i] = canon_solve(ws, problems[i], &alone[i]);
        CHECK(statuses[i] == st_alone[i] && statuses[i] == CANON_COMPLETE);
        CHECK(results[i] != NULL && alone[i] != NULL);
        same_results(results[i], alone[i]);
        canon_result_release(results[i]);
        canon_result_release(alone[i]);
        canon_problem_release(problems[i]);
        entries += 1;
    }
    batches += 1;
    canon_object_release(x);
    canon_object_release(y);
}

static void t1_tier(void)
{
    canon_workspace *ws = NULL;
    CHECK(canon_workspace_create(ctx, &ws) == CANON_COMPLETE);
    for (uint32_t n = 0; n <= 4; ++n) {
        t1_sym s;
        t1_sym_init(&s, n);
        t1_group groups[T1_MAX_GROUPS];
        const uint32_t ng = t1_subgroups(&s, groups);
        for (uint32_t gi = 0; gi < ng; ++gi) {
            canon_group *g = NULL;
            CHECK(canon_group_create(ctx, n, groups[gi].gens, groups[gi].gen_count, &g) == CANON_COMPLETE);
            for (uint32_t mask = 0; mask < (1u << n); mask += (n == 4 ? 5u : 1u)) {
                uint32_t atoms[4], k = 0;
                for (uint32_t a = 0; a < n; ++a) {
                    if (mask >> a & 1u) {
                        atoms[k++] = a;
                    }
                }
                batch_of_objectives(ws, g, n, atoms, k);
            }
            canon_group_release(g);
        }
    }
    canon_workspace_release(ws);
}

static void edge_cases(void)
{
    canon_workspace *ws = NULL;
    CHECK(canon_workspace_create(ctx, &ws) == CANON_COMPLETE);
    const uint32_t swap[2] = {1, 0};
    canon_group *g = NULL;
    canon_object *x = NULL;
    CHECK(canon_group_create(ctx, 2, swap, 1, &g) == CANON_COMPLETE);
    CHECK(canon_object_create_subset(ctx, 2, NULL, 0, &x) == CANON_COMPLETE);
    canon_problem *ok = NULL, *tight = NULL;
    CHECK(canon_problem_create(ctx, g, x, CANON_OBJECTIVE_CANONICAL_IMAGE, CANON_PROFILE_P1,
                               CANON_ENCODING_CDAG_2, CANON_ORDER_CDAG_BYTE_1, NULL, &ok) == CANON_COMPLETE);
    /* spec 11.1: quota per input; 1 node is too few for the empty subset under Sym(2) */
    canon_capacity cap = {0, 0, 1, 0, 0, 0, 0, 0, 0};
    CHECK(canon_problem_create(ctx, g, x, CANON_OBJECTIVE_CANONICAL_IMAGE, CANON_PROFILE_P1,
                               CANON_ENCODING_CDAG_2, CANON_ORDER_CDAG_BYTE_1, &cap, &tight) == CANON_COMPLETE);
    /* a batch of 0 */
    CHECK(canon_solve_batch(ws, NULL, 0, NULL, NULL) == CANON_COMPLETE);
    /* NULL arrays with count > 0: rejected at call level, nothing written */
    canon_result *r[3] = {(canon_result *)(void *)&batches, NULL, NULL};
    canon_status st[3] = {CANON_COMPLETE, CANON_COMPLETE, CANON_COMPLETE};
    const canon_problem *problems[3] = {ok, NULL, tight};
    CHECK(canon_solve_batch(NULL, problems, 3, r, st) == CANON_INVALID_INPUT);
    CHECK(canon_solve_batch(ws, NULL, 3, r, st) == CANON_INVALID_INPUT);
    CHECK(canon_solve_batch(ws, problems, 3, NULL, st) == CANON_INVALID_INPUT);
    CHECK(canon_solve_batch(ws, problems, 3, r, NULL) == CANON_INVALID_INPUT);
    CHECK(r[0] == (canon_result *)(void *)&batches && st[1] == CANON_COMPLETE); /* untouched */
    CHECK(canon_solve_batch(ws, problems, SIZE_MAX / 2u, r, st) == CANON_INVALID_INPUT); /* size overflow */
    /* a NULL entry and a per-input CAPACITY_LIMIT among a valid input: the call is COMPLETE,
     * each entry has its own status and result */
    CHECK(canon_solve_batch(ws, problems, 3, r, st) == CANON_COMPLETE);
    CHECK(st[0] == CANON_COMPLETE && r[0] != NULL && canon_result_status(r[0]) == CANON_COMPLETE);
    CHECK(st[1] == CANON_INVALID_INPUT && r[1] == NULL);
    CHECK(st[2] == CANON_CAPACITY_LIMIT && r[2] != NULL && canon_result_status(r[2]) == CANON_CAPACITY_LIMIT);
    size_t len = 7;
    CHECK(canon_result_bytes(r[0], &len) != NULL && len > 0);
    CHECK(canon_result_bytes(r[2], &len) == NULL && len == 0);
    canon_result_release(r[0]);
    canon_result_release(r[2]);
    /* the same workspace solves again afterwards */
    canon_result *again = NULL;
    CHECK(canon_solve(ws, ok, &again) == CANON_COMPLETE);
    canon_result_release(again);
    canon_problem_release(ok);
    canon_problem_release(tight);
    canon_object_release(x);
    canon_group_release(g);
    canon_workspace_release(ws);
}

int main(void)
{
    CHECK(canon_context_create(NULL, &ctx) == CANON_COMPLETE);
    t1_tier();
    edge_cases();
    printf("batches: %llu of %llu entries agree with separate solves\n", (unsigned long long)batches,
           (unsigned long long)entries);
    canon_context_release(ctx);
    return check_finish("test_batch");
}
