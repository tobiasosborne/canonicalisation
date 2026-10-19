/* Slice S8: allocation fault injection (detailed plan WP4.8; spec 17 "Allocator failure leaves
 * the old state or a releasable partial state, never a fabricated completion flag").  For every
 * k, the k-th allocation of a whole flow (context, group, objects, problems for every
 * objective, solves with certificates and deterministic witnesses, encoding, witness
 * verification, release) fails; the flow must end with every status in {COMPLETE,
 * RESOURCE_LIMIT, CAPACITY_LIMIT}, every handle released, the ledger back at its baseline (no
 * leak), and never a COMPLETE result with a missing answer.  Afterwards the same flow runs
 * without a fault.  ASan (the sanitizer build) checks the frees. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "arena/mem.h"
#include "canon/canon.h"
#include "check.h"

static int bad_statuses, completes, resource_limits;

/* S8 independent review: every COMPLETE solve of a faulted flow must give exactly the
 * fault-free answer (spec 17 "never a fabricated completion flag": a swallowed allocation
 * failure that changed the answer or the traversal would show here), so the fault-free flow
 * records a digest of each solve's answer and counters. */
static uint64_t ref_digest[16];
static bool have_ref;
static uint64_t compared;

static uint64_t mix(uint64_t h, const void *data, size_t len)
{
    const uint8_t *b = data;
    h = (h ^ (uint64_t)len) * 0x100000001b3ULL;
    for (size_t i = 0; b != NULL && i < len; ++i) {
        h = (h ^ b[i]) * 0x100000001b3ULL;
    }
    return h;
}

static uint64_t answer_digest(const canon_result *r)
{
    uint64_t h = 0xcbf29ce484222325ULL;
    size_t len = 0;
    uint32_t deg = 0;
    const canon_result_flags f = canon_result_get_flags(r);
    h = mix(h, &f, sizeof f);
    const uint8_t *b = canon_result_trace(r, &len);
    h = mix(h, b, len);
    b = canon_result_bytes(r, &len);
    h = mix(h, b, len);
    b = canon_result_group_bytes(r, &len);
    h = mix(h, b, len);
    b = canon_result_order_key(r, &len);
    h = mix(h, b, len);
    b = canon_result_certificate(r, &len);
    h = mix(h, b, len);
    const uint32_t *w = canon_result_witness(r, &deg);
    h = mix(h, w, w != NULL ? (size_t)deg * sizeof *w : 0);
    w = canon_result_labeling(r, &deg);
    h = mix(h, w, w != NULL ? (size_t)deg * sizeof *w : 0);
    int sign = 0;
    (void)canon_result_sign(r, &sign);
    h = mix(h, &sign, sizeof sign);
    canon_metrics m;
    CHECK(canon_result_metrics(r, &m) == CANON_COMPLETE);
    const uint64_t counts[8] = {m.p1_nodes, m.p1_leaves, m.children_explored, m.children_pruned,
                                m.automorphisms_inserted, m.enumeration_visits,
                                m.enumeration_leaves, m.output_bytes};
    return mix(h, counts, sizeof counts);
}

static void note(canon_status st)
{
    if (st == CANON_COMPLETE) {
        ++completes;
    } else if (st == CANON_RESOURCE_LIMIT) {
        ++resource_limits;
    } else if (st != CANON_CAPACITY_LIMIT) {
        ++bad_statuses;
    }
}

static int sink(void *user, const uint8_t *chunk, size_t length, size_t *accepted)
{
    (void)user;
    (void)chunk;
    *accepted = length;
    return 0;
}

/* One complete flow; returns true if every step completed (no fault met). */
static bool flow(void)
{
    bool all = true;
    const uint32_t gens[8] = {1, 2, 3, 0, 1, 0, 2, 3};
    const int8_t signs[2] = {-1, -1};
    const uint32_t atoms[2] = {0, 2}, other[1] = {1}, rho[4] = {3, 2, 1, 0};
    const canon_arc arcs[3] = {{0, 1, NULL, 0, 1}, {1, 2, NULL, 0, 2}, {2, 0, NULL, 0, 1}};
    canon_context *ctx = NULL;
    canon_group *g = NULL, *sg = NULL;
    canon_object *x = NULL, *y = NULL, *gr = NULL, *d = NULL;
    canon_workspace *ws = NULL;
    canon_status st = canon_context_create(NULL, &ctx);
    note(st);
    all = all && st == CANON_COMPLETE;
    if (ctx != NULL) {
        st = canon_group_create(ctx, 4, gens, 2, &g);
        note(st);
        all = all && st == CANON_COMPLETE;
        st = canon_group_create_signed(ctx, 4, gens, 2, signs, &sg);
        note(st);
        all = all && st == CANON_COMPLETE;
        st = canon_object_create_subset(ctx, 4, atoms, 2, &x);
        note(st);
        all = all && st == CANON_COMPLETE;
        st = canon_object_create_subset(ctx, 4, other, 1, &y);
        note(st);
        all = all && st == CANON_COMPLETE;
        st = canon_object_create_graph(ctx, 4, NULL, NULL, arcs, 3, &gr);
        note(st);
        all = all && st == CANON_COMPLETE;
        /* the tuple (b, b) of spec 7.4 on degree 4 */
        static const uint8_t tuple[] = {0x43, 0x4e, 0x02, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00,
                                        0x00, 0x04, 0x00, 0x00, 0x00, 0x02, 0x02, 0x00, 0x00,
                                        0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00,
                                        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01};
        st = canon_object_create(ctx, CANON_SCHEMA_EXT_DAG_1, CANON_ACTION_ATOM_TRANSPORT_1, 4, tuple,
                                 sizeof tuple, &d);
        note(st);
        all = all && st == CANON_COMPLETE;
        st = canon_workspace_create(ctx, &ws);
        note(st);
        all = all && st == CANON_COMPLETE;
    }
    const struct {
        canon_objective obj;
        canon_profile prof;
        int object; /* 0 x, 1 graph, 2 dag */
        bool target, labeling, deterministic, certificate, signed_group;
    } specs[] = {
        {CANON_OBJECTIVE_CANONICAL_IMAGE, CANON_PROFILE_P1, 0, false, false, true, true, false},
        {CANON_OBJECTIVE_CANONICAL_IMAGE, CANON_PROFILE_P1, 1, false, false, false, true, false},
        {CANON_OBJECTIVE_CANONICAL_IMAGE, CANON_PROFILE_P1, 2, false, false, false, false, false},
        {CANON_OBJECTIVE_LEX_MIN_IMAGE, CANON_PROFILE_NO_TREE, 0, false, false, true, false, false},
        {CANON_OBJECTIVE_TRANSPORTER_ONE, CANON_PROFILE_NO_TREE, 0, true, false, false, false, false},
        {CANON_OBJECTIVE_STABILISER, CANON_PROFILE_NO_TREE, 1, false, false, false, false, false},
        {CANON_OBJECTIVE_TRANSPORTER_COSET, CANON_PROFILE_NO_TREE, 0, true, false, true, false, false},
        {CANON_OBJECTIVE_CANONICAL_LABELING_COSET, CANON_PROFILE_P1, 0, false, true, false, false, false},
        {CANON_OBJECTIVE_SIGNED_CANONICAL_IMAGE, CANON_PROFILE_P1, 0, false, false, false, false, true},
        {CANON_OBJECTIVE_SIGNED_CANONICAL_IMAGE, CANON_PROFILE_P1, 1, false, false, false, false, true},
        /* spec v2.2 3: the deterministic zero witness, by the exhausted enumeration (x = {0,2}
         * is fixed by the odd (0 2)) and on the spec 7.3 fast path (every generator fixes the
         * tuple, A = G, the Schreier generators of A+) */
        {CANON_OBJECTIVE_SIGNED_CANONICAL_IMAGE, CANON_PROFILE_P1, 0, false, false, true, false, true},
        {CANON_OBJECTIVE_SIGNED_CANONICAL_IMAGE, CANON_PROFILE_P1, 2, false, false, true, false, true},
    };
    for (size_t i = 0; i < sizeof specs / sizeof *specs && ws != NULL && g != NULL && sg != NULL &&
                       x != NULL && gr != NULL && d != NULL && y != NULL;
         ++i) {
        canon_problem_options opts = {specs[i].deterministic ? CANON_WITNESS_DETERMINISTIC
                                                             : CANON_WITNESS_ANY,
                                      specs[i].labeling ? rho : NULL, specs[i].certificate};
        canon_object *obj = specs[i].object == 0 ? x : (specs[i].object == 1 ? gr : d);
        canon_problem *p = NULL;
        canon_result *r = NULL;
        st = canon_problem_create_with_options(ctx, specs[i].signed_group ? sg : g, obj,
                                               specs[i].target ? y : NULL, specs[i].obj,
                                               specs[i].prof, CANON_ENCODING_CDAG_2,
                                               CANON_ORDER_CDAG_BYTE_1, NULL, &opts, &p);
        note(st);
        all = all && st == CANON_COMPLETE;
        if (p != NULL) {
            st = canon_solve(ws, p, &r);
            note(st);
            all = all && st == CANON_COMPLETE;
            if (r != NULL && canon_result_status(r) == CANON_COMPLETE) {
                const uint64_t digest = answer_digest(r);
                if (!have_ref) {
                    ref_digest[i] = digest;
                } else {
                    CHECK(digest == ref_digest[i]); /* the fault-free answer, exactly */
                    ++compared;
                }
                /* a COMPLETE result must carry its answer */
                canon_result_flags f = canon_result_get_flags(r);
                size_t len = 0;
                if (f.encoding_complete) {
                    CHECK(canon_result_bytes(r, &len) != NULL);
                }
                if (f.witness_valid) {
                    uint32_t deg = 0;
                    CHECK(canon_result_witness(r, &deg) != NULL);
                }
                if (specs[i].certificate) {
                    CHECK(canon_result_certificate(r, &len) != NULL);
                }
                if (f.witness_valid) {
                    bool valid = false;
                    st = canon_result_verify_witness(r, &valid);
                    note(st);
                    all = all && st == CANON_COMPLETE;
                    CHECK(st != CANON_COMPLETE || valid);
                }
                if (canon_result_bytes(r, &len) != NULL || f.zero_certified) {
                    st = canon_result_encode(r, sink, NULL);
                    note(st);
                    all = all && st == CANON_COMPLETE;
                }
            } else if (r != NULL) {
                /* spec 3.2: an incomplete result carries no answer and no flags */
                canon_result_flags f = canon_result_get_flags(r);
                CHECK(!f.witness_valid && !f.image_canonical && !f.encoding_complete);
            }
            canon_result_release(r);
            canon_problem_release(p);
        }
    }
    canon_workspace_release(ws);
    canon_object_release(d);
    canon_object_release(gr);
    canon_object_release(y);
    canon_object_release(x);
    canon_group_release(sg);
    canon_group_release(g);
    canon_context_release(ctx);
    return all;
}

int main(void)
{
    canon_memory_stats base;
    canon_memory_get(&base);
    const uint64_t requests_before = canon_mem_requests();
    /* the fault-free flow completes everything */
    CHECK(flow());
    have_ref = true;
    canon_memory_stats after;
    canon_memory_get(&after);
    CHECK(after.live_bytes == base.live_bytes);
    const uint64_t per_flow = canon_mem_requests() - requests_before; /* fault-injectable requests */
    CHECK(per_flow > 100);
    /* every k: the k-th allocation fails */
    uint64_t faulted_runs = 0, faults_before = canon_mem_fail_after(0);
    for (uint64_t k = 1; k <= per_flow + 1u; ++k) {
        canon_mem_fail_after(k);
        const bool completed = flow();
        const uint64_t faults = canon_mem_fail_after(0); /* cancel a pending fault, read count */
        canon_memory_get(&after);
        CHECK(after.live_bytes == base.live_bytes); /* every partial state was releasable */
        if (faults == faults_before) {
            CHECK(completed); /* no allocation was left to fail: the flow ran fault-free */
            break;
        }
        faults_before = faults;
        faulted_runs += 1;
    }
    CHECK(faulted_runs >= per_flow - 2u && faulted_runs <= per_flow + 1u);
    CHECK(bad_statuses == 0);
    CHECK(compared > 0);
    printf("faults: %llu allocations per flow, %llu faulted runs, %d RESOURCE_LIMIT statuses, %d "
           "COMPLETE statuses, %d other; %llu complete solves equal to the fault-free answer\n",
           (unsigned long long)per_flow, (unsigned long long)faulted_runs, resource_limits, completes,
           bad_statuses, (unsigned long long)compared);
    return check_finish("test_faults");
}
