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
    /* a certified zero has no stream; canon_result_encode writes its 00 payload (S8 review) */
    CHECK(m.signed_fast_path && m.output_bytes == 1);
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
            /* the same group with the parity character (always a homomorphism) */
            int8_t parity[8];
            for (uint32_t k = 0; k < groups[gi].gen_count; ++k) {
                const uint32_t *p = groups[gi].gens + k * n;
                uint32_t seen = 0, cycles = 0;
                for (uint32_t v = 0; v < n; ++v) {
                    for (uint32_t w = v, first = 1; !(seen >> w & 1u); w = p[w], first = 0) {
                        seen |= 1u << w;
                        cycles += first;
                    }
                }
                parity[k] = (n - cycles) % 2u == 0 ? 1 : -1;
            }
            canon_group *sg = NULL;
            CHECK(canon_group_create_signed(ctx, n, groups[gi].gens, groups[gi].gen_count, parity, &sg) == CANON_COMPLETE);
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
                /* spec v2.2 3: the signed image under the parity character in the deterministic
                 * mode (a zero reports the least odd element of A: the exhausted enumeration, or
                 * on the fast path the Schreier generators of A+) */
                canon_problem_options det = {CANON_WITNESS_DETERMINISTIC, NULL, false};
                CHECK(run(ws, sg, x, NULL, CANON_OBJECTIVE_SIGNED_CANONICAL_IMAGE, CANON_PROFILE_P1, &det, NULL, &m) == CANON_COMPLETE);
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
            canon_group_release(sg);
            canon_group_release(g);
        }
    }
    canon_workspace_release(ws);
}

/* ---- S8 independent review (19 October 2026, docs/slices/S8-notes.md) ---- */

/* run() on a workspace of its own, so that the peak includes the workspace's growth. */
static canon_status fresh(const canon_group *g, const canon_object *x, canon_objective obj,
                          canon_profile prof, const canon_problem_options *opts,
                          const canon_capacity *cap, canon_metrics *m)
{
    canon_workspace *ws = NULL;
    CHECK(canon_workspace_create(ctx, &ws) == CANON_COMPLETE);
    const canon_status st = run(ws, g, x, NULL, obj, prof, opts,
                                cap, m);
    canon_workspace_release(ws);
    return st;
}

static uint8_t dag_buf[1u << 16];
static size_t dag_len;

static void put8(unsigned v)
{
    dag_buf[dag_len++] = (uint8_t)v;
}

static void put32(uint32_t v)
{
    put8(v >> 24);
    put8((v >> 16) & 255u);
    put8((v >> 8) & 255u);
    put8(v & 255u);
}

/* A CDAG-2 stream on degree n (spec 4.1): with `groups` false, the n atoms, the ordered pairs
 * (a, b), a != b, as tuples, and the set of those tuples; with `groups`
 * true, the subgroups <(a b c)> for a < b < c (rule 2 of spec 9.4: the 3-cycle a -> b -> c is
 * the least non-identity element) and the set of them. */
static canon_object *nested(uint32_t n, bool groups)
{
    dag_len = 0;
    const uint8_t head[7] = {0x43, 0x4e, 0x02, 0x00, 0x01, 0x00, 0x01};
    for (size_t i = 0; i < sizeof head; ++i) {
        put8(head[i]);
    }
    put32(n);
    const size_t q_at = dag_len;
    put32(0);
    uint32_t q = 0, first = 0;
    if (!groups) {
        for (uint32_t a = 0; a < n; ++a) {
            put8(0x01);
            put32(a);
            ++q;
        }
        first = q;
        for (uint32_t a = 0; a < n; ++a) {
            for (uint32_t b = 0; b < n; ++b) {
                if (a != b) {
                    put8(0x03);
                    put32(2);
                    put32(a);
                    put32(b);
                    ++q;
                }
            }
        }
    } else {
        for (uint32_t a = 0; a < n; ++a) {
            for (uint32_t b = a + 1u; b < n; ++b) {
                for (uint32_t c = b + 1u; c < n; ++c) {
                    const uint32_t perm[7] = {3, a, b, b, c, c, a};
                    put8(0x07);
                    put8(0x00);
                    put32(1);
                    for (size_t i = 0; i < 7; ++i) {
                        put32(perm[i]);
                    }
                    ++q;
                }
            }
        }
    }
    put8(0x04);
    put32(q - first);
    for (uint32_t i = first; i < q; ++i) {
        put32(i);
    }
    ++q;
    put32(q - 1u);
    for (int i = 0; i < 4; ++i) {
        dag_buf[q_at + (size_t)i] = (uint8_t)(q >> (24 - 8 * i));
    }
    canon_object *x = NULL;
    CHECK(canon_object_create(ctx, CANON_SCHEMA_EXT_DAG_1, CANON_ACTION_ATOM_TRANSPORT_1, n, dag_buf,
                              dag_len, &x) == CANON_COMPLETE);
    return x;
}

static void review(void)
{
    /* Finding 1: a certificate's length grows with the node count, which the plan did not
     * bound (the unpruned certificate of the empty subset under Sym(6) used 4.2 MB against a
     * plan of 1.1 MB).  It is now bounded by max_output_bytes and charged by the plan. */
    uint32_t sym[2 * 6];
    for (uint32_t v = 0; v < 6; ++v) {
        sym[v] = v;
        sym[6 + v] = (v + 1u) % 6u;
    }
    sym[0] = 1;
    sym[1] = 0;
    canon_group *s6 = NULL, *c13 = NULL, *c9 = NULL;
    canon_object *empty = NULL;
    CHECK(canon_group_create(ctx, 6, sym, 2, &s6) == CANON_COMPLETE);
    CHECK(canon_object_create_subset(ctx, 6, NULL, 0, &empty) == CANON_COMPLETE);
    canon_metrics m;
    canon_problem_options cert = {CANON_WITNESS_ANY, NULL, true};
    canon_capacity unpruned = {0, 0, 0, 0, 0, 0, 0, CANON_WORK_POLICY_UNPRUNED, 0};
    CHECK(fresh(s6, empty, CANON_OBJECTIVE_CANONICAL_IMAGE, CANON_PROFILE_P1, &cert, &unpruned,
                &m) == CANON_COMPLETE);
    CHECK(m.certificate_bytes > 1000000u && m.p1_nodes > 1000u);
    const uint64_t big = m.certificate_bytes;
    /* a limit below the certificate's length: CAPACITY_LIMIT, still within the plan */
    canon_capacity limited = unpruned;
    limited.max_output_bytes = big - 1u;
    CHECK(fresh(s6, empty, CANON_OBJECTIVE_CANONICAL_IMAGE, CANON_PROFILE_P1, &cert, &limited,
                &m) == CANON_CAPACITY_LIMIT);
    limited.max_output_bytes = big; /* exactly the length: admitted */
    CHECK(fresh(s6, empty, CANON_OBJECTIVE_CANONICAL_IMAGE, CANON_PROFILE_P1, &cert, &limited,
                &m) == CANON_COMPLETE && m.certificate_bytes == big);
    /* Finding 3: a status-only result reports no counters of an earlier solve.  A limit
     * below the certificate's header stops the solve before its P1 run. */
    canon_workspace *ws = NULL;
    CHECK(canon_workspace_create(ctx, &ws) == CANON_COMPLETE);
    CHECK(run(ws, s6, empty, NULL, CANON_OBJECTIVE_CANONICAL_IMAGE, CANON_PROFILE_P1, NULL,
              &unpruned, &m) == CANON_COMPLETE && m.p1_nodes > 1000u);
    limited.max_output_bytes = 64;
    CHECK(run(ws, s6, empty, NULL, CANON_OBJECTIVE_CANONICAL_IMAGE, CANON_PROFILE_P1, &cert,
              &limited, &m) == CANON_CAPACITY_LIMIT);
    CHECK(m.p1_nodes == 0 && m.p1_leaves == 0 && m.children_explored == 0 &&
          !m.singleton_orbit && !m.root_discrete);
    canon_workspace_release(ws);
    /* Finding 2: nested roots.  Their image arenas were charged as stream bytes only (the
     * set of all 156 pairs on 13 points under C13 used 84 KB against a plan of 68 KB, 158 KB
     * with the deterministic witness; the 84 subgroup leaves on 9 points under C9 with the
     * deterministic witness 102 KB against 83 KB). */
    uint32_t cyc[13];
    for (uint32_t v = 0; v < 13; ++v) {
        cyc[v] = (v + 1u) % 13u;
    }
    CHECK(canon_group_create(ctx, 13, cyc, 1, &c13) == CANON_COMPLETE);
    for (uint32_t v = 0; v < 9; ++v) {
        cyc[v] = (v + 1u) % 9u;
    }
    CHECK(canon_group_create(ctx, 9, cyc, 1, &c9) == CANON_COMPLETE);
    canon_object *pairs = nested(13, false), *leaves = nested(9, true);
    canon_problem_options det = {CANON_WITNESS_DETERMINISTIC, NULL, false};
    CHECK(fresh(c13, pairs, CANON_OBJECTIVE_CANONICAL_IMAGE, CANON_PROFILE_P1, NULL, NULL, &m) ==
          CANON_COMPLETE);
    CHECK(fresh(c13, pairs, CANON_OBJECTIVE_CANONICAL_IMAGE, CANON_PROFILE_P1, &det, NULL, &m) ==
          CANON_COMPLETE);
    CHECK(fresh(c9, leaves, CANON_OBJECTIVE_CANONICAL_IMAGE, CANON_PROFILE_P1, &det, NULL, &m) ==
          CANON_COMPLETE);
    CHECK(fresh(c9, leaves, CANON_OBJECTIVE_STABILISER, CANON_PROFILE_NO_TREE, NULL, NULL, &m) ==
          CANON_COMPLETE);
    /* Finding 4: the deterministic witness of a canonical image runs the enumeration, which
     * the plan now charges like the other enumerating solves (Sym(6), fresh workspace) */
    CHECK(fresh(s6, empty, CANON_OBJECTIVE_CANONICAL_IMAGE, CANON_PROFILE_P1, &det, NULL, &m) ==
          CANON_COMPLETE);
    CHECK(m.enumeration_visits > 0);
    /* spec v2.2 3: the deterministic zero witness on the spec 7.3 route builds A+ from 2 k
     * Schreier generators; Sym(6) given by 40 generators (transpositions and powers of a
     * 6-cycle) under the parity character, the empty set, a fresh workspace: peak <= plan */
    {
        uint32_t many[40 * 6];
        int8_t odd[40];
        for (uint32_t i = 0; i < 40; ++i) {
            uint32_t *q = many + i * 6u;
            for (uint32_t v = 0; v < 6; ++v) {
                /* even i: the transposition (a b); odd i: a power of the 6-cycle */
                q[v] = i % 2u == 0 ? v : (v + 1u + i / 2u % 5u) % 6u;
            }
            if (i % 2u == 0) {
                const uint32_t a = i / 2u % 6u, b = (a + 1u + i / 12u) % 6u;
                q[a] = b;
                q[b] = a;
            }
            uint32_t seen = 0, cycles = 0; /* parity: (6 - cycles) mod 2 */
            for (uint32_t v = 0; v < 6; ++v) {
                for (uint32_t w = v, first = 1; !(seen >> w & 1u); w = q[w], first = 0) {
                    seen |= 1u << w;
                    cycles += first;
                }
            }
            odd[i] = (6u - cycles) % 2u == 0 ? 1 : -1;
        }
        canon_group *many_g = NULL;
        CHECK(canon_group_create_signed(ctx, 6, many, 40, odd, &many_g) == CANON_COMPLETE);
        canon_object *none = NULL;
        CHECK(canon_object_create_subset(ctx, 6, NULL, 0, &none) == CANON_COMPLETE);
        CHECK(fresh(many_g, none, CANON_OBJECTIVE_SIGNED_CANONICAL_IMAGE, CANON_PROFILE_P1, &det,
                    NULL, &m) == CANON_COMPLETE);
        CHECK(m.signed_fast_path && m.enumeration_visits == 0);
        canon_object_release(none);
        canon_group_release(many_g);
    }
    canon_object_release(pairs);
    canon_object_release(leaves);
    canon_object_release(empty);
    canon_group_release(s6);
    canon_group_release(c13);
    canon_group_release(c9);
}

int main(void)
{
    CHECK(canon_context_create(NULL, &ctx) == CANON_COMPLETE);
    golden_counters();
    ledger();
    tiers();
    review();
    printf("metrics: %llu solves, peak <= plan on every one; the largest peak/plan ratio is "
           "%llu / %llu bytes\n",
           (unsigned long long)solves, (unsigned long long)worst_peak, (unsigned long long)worst_plan);
    canon_context_release(ctx);
    return check_finish("test_metrics");
}
