/* Tests of the labeling-coset objective (slice S6, docs/slices/S6.md 3.2, 4; spec 3.1, 3.2,
 * 7.4, 8.2, 9.4, 11.1, 17):
 *
 *  - spec 7.4: Omega = (a,b), rho = [1,0], G = 1, x = {a}: t = id, lambda = rho, c = {1},
 *    A lambda = {rho} with the payload of spec 7.4 ("returning id as the source labeling is
 *    wrong");
 *  - the product sides on non-commuting elements with rho of order 3: G' = rho^-1 G rho (rho^-1
 *    first) and lambda = rho t (rho first), by a hand derivation and against P1 run directly on
 *    (x^rho, G') with G' built from hand-conjugated generators;
 *  - the conjugate op of both backends on every T1 group and every rho (order, membership);
 *  - every T1 group, subset and rho under both backends (one workspace throughout):
 *    lambda in G rho, x^lambda = c, c and the trace equal P1 on the transformed problem, the
 *    payload equals Group(A) (from the STABILISER objective) || Perm(least element of A lambda,
 *    by brute force); representative change rho' = k rho (k in G) keeps c, the trace and the
 *    payload (every k for n <= 3, a fifth of G for n = 4); coordinate rename by one mu per case
 *    keeps c and gives mu^-1 lambda (spec 3.1);
 *  - a graph and a nested object; verify_witness (and tampered t, lambda); validation, quota,
 *    workspace reuse; the consumer's module contract and counters on T1 (printed for
 *    docs/slices/S6-notes.md). */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "bsgs/group.h"
#include "canon/canon.h"
#include "check.h"
#include "object/object.h"
#include "perm/perm.h"
#include "search/objectives.h"
#include "search/p1_tree.h"
#include "t1_groups.h"

static canon_context *ctx_of[2]; /* chain, explicit */

static canon_group *group_of(int b, uint32_t n, const uint32_t *gens, uint32_t count)
{
    canon_group *g = NULL;
    CHECK(canon_group_create(ctx_of[b], n, gens, count, &g) == CANON_COMPLETE);
    return g;
}

static canon_object *subset_of(uint32_t n, const uint32_t *atoms, size_t count)
{
    canon_object *x = NULL;
    CHECK(canon_object_create_subset(ctx_of[0], n, atoms, count, &x) == CANON_COMPLETE);
    return x;
}

/* Solve objective `objective` (labeling, canonical image or stabiliser); rho only for the
 * labeling.  `ws` may be NULL (a fresh workspace). */
static canon_result *solve(canon_workspace *ws, const canon_group *g, const canon_object *x,
                           canon_objective objective, const uint32_t *rho, uint64_t quota,
                           canon_status *st)
{
    const canon_profile profile =
        objective == CANON_OBJECTIVE_STABILISER ? CANON_PROFILE_NO_TREE : CANON_PROFILE_P1;
    /* S7: the canonical-image solves here are compared with the labeling's t, which P1 finds
     * on the UNPRUNED tree (spec 8.2: the labeling stays unpruned), so they run under the
     * reference work policy 0x0001; the other objectives keep the context default */
    canon_capacity cap = {0, 0, quota, 0, 0, 0, 0,
                          objective == CANON_OBJECTIVE_CANONICAL_IMAGE
                              ? (canon_work_policy)CANON_WORK_POLICY_REFERENCE
                              : (canon_work_policy)0};
    canon_problem_options opts = {CANON_WITNESS_ANY, rho};
    canon_problem *p = NULL;
    canon_workspace *own = NULL;
    canon_result *r = NULL;
    *st = canon_problem_create_with_options(ctx_of[0], g, x, NULL, objective, profile,
                                            CANON_ENCODING_CDAG_2, CANON_ORDER_CDAG_BYTE_1, &cap,
                                            &opts, &p);
    if (*st == CANON_COMPLETE) {
        if (ws == NULL) {
            CHECK(canon_workspace_create(ctx_of[0], &own) == CANON_COMPLETE);
            ws = own;
        }
        *st = canon_solve(ws, p, &r);
    }
    canon_workspace_release(own);
    canon_problem_release(p);
    return r;
}

static int same_bytes(const uint8_t *a, size_t al, const uint8_t *b, size_t bl)
{
    return a != NULL && b != NULL && al == bl && (al == 0 || memcmp(a, b, al) == 0);
}

typedef const uint8_t *(*bytes_fn)(const canon_result *, size_t *);

/* 1 iff accessor f returns equal, present byte strings for r1 and r2 (each length is read
 * after its own call: argument evaluation order is unspecified in C). */
static int same(bytes_fn f, const canon_result *r1, const canon_result *r2)
{
    size_t l1 = 0, l2 = 0;
    const uint8_t *b1 = f(r1, &l1);
    const uint8_t *b2 = f(r2, &l2);
    return same_bytes(b1, l1, b2, l2);
}

static void verify_ok(const canon_result *r)
{
    bool valid = false;
    CHECK(canon_result_verify_witness(r, &valid) == CANON_COMPLETE && valid);
}

/* ---- spec 7.4 ---- */

static void golden(void)
{
    const uint32_t rho[2] = {1, 0}, zero = 0;
    canon_object *x = subset_of(2, &zero, 1);
    for (int b = 0; b < 2; ++b) {
        canon_group *g = group_of(b, 2, NULL, 0); /* G = 1 */
        canon_status st;
        canon_result *r = solve(NULL, g, x, CANON_OBJECTIVE_CANONICAL_LABELING_COSET, rho, 0, &st);
        CHECK(st == CANON_COMPLETE && canon_result_status(r) == CANON_COMPLETE);
        canon_result_flags f = canon_result_get_flags(r);
        CHECK(f.image_canonical && f.witness_valid && f.subgroup_verified &&
              f.stabiliser_complete && f.encoding_complete && !f.zero_certified &&
              !f.nonzero_certified && !f.minimum_proved && !f.transport_exhausted);
        uint32_t deg = 0;
        const uint32_t *t = canon_result_witness(r, &deg); /* t on D_2 */
        CHECK(t != NULL && deg == 2 && t[0] == 0 && t[1] == 1);
        const uint32_t *lambda = canon_result_labeling(r, &deg);
        /* "t=id on D_2 and lambda=rho, c={1}, A lambda={rho}; returning id as the source
         * labeling is wrong" */
        CHECK(lambda != NULL && deg == 2 && lambda[0] == 1 && lambda[1] == 0);
        size_t len = 0;
        const uint8_t *c = canon_result_bytes(r, &len);
        CHECK(c != NULL && check_hex_is(c, len,
                                        "434e0200010001 00000002 00000002 01 00000001 "
                                        "04 00000001 00000000 00000001"));
        const uint8_t *gb = canon_result_group_bytes(r, &len);
        CHECK(gb != NULL &&
              check_hex_is(gb, len, "01 00000000 00000002 00000000 00000001 00000001 00000000"));
        CHECK(canon_result_trace(r, &len) != NULL &&
              check_hex_is(canon_result_trace(r, &len), len,
                           "10 00000000 20 00000002 00000001 00000001 21 00000002 00000001 "
                           "00000001 00"));
        int sign = 5;
        CHECK(canon_result_sign(r, &sign) == CANON_INVALID_INPUT && sign == 0);
        verify_ok(r);
        canon_result_release(r);
        canon_group_release(g);
    }
    canon_object_release(x);
}

/* ---- product sides (spec 3, 3.1) ---- */

static void product_sides(void)
{
    /* G = <(0 1)> = {id, [1,0,2]} on 3 points, rho = [1,2,0] of order 3, x = {0}.
     *   G' = rho^-1 G rho: (0 1) relabelled through rho is (rho0 rho1) = (1 2) = [0,2,1]
     *   (the other side, rho G rho^-1, would give (0 2) = [2,1,0]);
     *   x' = x^rho = {1}; under G' = {id, [0,2,1]} P1 has initial cells [{0,2},{1}], the G
     *   stage splits {0,2} (G'_{1} = 1), the leaf L = (0,2,1) has t = [0,2,1] (L^t = (0,1,2)),
     *   c = x'^t = {2};
     *   lambda = rho t: lambda[v] = t[rho[v]] = [2,1,0] (t rho would be [1,0,2], with
     *   x^(t rho) = {1} != c); lambda = [1,0,2] rho is in G rho;
     *   A = Stab_G({0}) = 1, so the payload is Group(1) || Perm([2,1,0]). */
    const uint32_t swap01[3] = {1, 0, 2}, rho[3] = {1, 2, 0}, zero = 0, one = 1;
    const uint32_t gp_gen[3] = {0, 2, 1};
    canon_object *x = subset_of(3, &zero, 1), *xp = subset_of(3, &one, 1);
    for (int b = 0; b < 2; ++b) {
        canon_group *g = group_of(b, 3, swap01, 1), *gp = group_of(b, 3, gp_gen, 1);
        canon_status st;
        canon_result *r = solve(NULL, g, x, CANON_OBJECTIVE_CANONICAL_LABELING_COSET, rho, 0, &st);
        canon_result *d = solve(NULL, gp, xp, CANON_OBJECTIVE_CANONICAL_IMAGE, NULL, 0, &st);
        uint32_t deg = 0;
        const uint32_t *t = canon_result_witness(r, &deg);
        const uint32_t *lambda = canon_result_labeling(r, &deg);
        CHECK(t != NULL && t[0] == 0 && t[1] == 2 && t[2] == 1);
        CHECK(lambda != NULL && lambda[0] == 2 && lambda[1] == 1 && lambda[2] == 0);
        size_t la = 0;
        CHECK(same(canon_result_bytes, r, d));
        CHECK(same(canon_result_trace, r, d));
        const uint32_t *td = canon_result_witness(d, &deg);
        CHECK(td != NULL && memcmp(td, t, sizeof(uint32_t) * 3) == 0);
        const uint8_t *c = canon_result_bytes(r, &la);
        CHECK(c != NULL && check_hex_is(c, la,
                                        "434e0200010001 00000003 00000002 01 00000002 "
                                        "04 00000001 00000000 00000001"));
        const uint8_t *gb = canon_result_group_bytes(r, &la);
        CHECK(gb != NULL && check_hex_is(gb, la,
                                         "01 00000000 00000002 00000000 00000002 "
                                         "00000002 00000000"));
        verify_ok(r);
        canon_result_release(r);
        canon_result_release(d);
        /* the conjugate op directly: rho^-1 (0 1) rho = (1 2) */
        canon_group *c2 = NULL;
        CHECK(g->ops->conjugate(g, rho, &c2) == CANON_COMPLETE);
        bool in = false;
        CHECK(c2->ops->contains(c2, gp_gen, &in) == CANON_COMPLETE && in);
        const uint32_t wrong[3] = {2, 1, 0};
        CHECK(c2->ops->contains(c2, wrong, &in) == CANON_COMPLETE && !in);
        CHECK(c2->signs == NULL);
        canon_group_release(c2);
        canon_group_release(g);
        canon_group_release(gp);
    }
    canon_object_release(x);
    canon_object_release(xp);
}

/* ---- the conjugate op on every T1 group and every rho ---- */

static void conjugates(void)
{
    uint32_t checked = 0;
    for (uint32_t n = 0; n <= 4; ++n) {
        t1_sym s;
        t1_sym_init(&s, n);
        t1_group groups[T1_MAX_GROUPS];
        const uint32_t ng = t1_subgroups(&s, groups);
        for (uint32_t gi = 0; gi < ng; ++gi) {
            for (int b = 0; b < 2; ++b) {
                canon_group *g = group_of(b, n, groups[gi].gens, groups[gi].gen_count);
                for (uint32_t ri = 0; ri < s.count; ++ri) {
                    const uint32_t *rho = s.elem[ri];
                    canon_group *c = NULL;
                    CHECK(g->ops->conjugate(g, rho, &c) == CANON_COMPLETE);
                    CHECK(c->ops == g->ops && c->ops->order(c) == groups[gi].order);
                    for (uint32_t e = 0; e < s.count; ++e) {
                        uint32_t q[4];
                        for (uint32_t v = 0; v < n; ++v) {
                            q[rho[v]] = rho[s.elem[e][v]]; /* rho^-1 e rho */
                        }
                        bool in = false;
                        CHECK(c->ops->contains(c, q, &in) == CANON_COMPLETE);
                        CHECK(in == ((groups[gi].mask >> e & 1u) != 0));
                    }
                    canon_group_release(c);
                    checked += 1;
                }
                canon_group_release(g);
            }
        }
    }
    printf("conjugates: %u (group, rho, backend) triples\n", checked);
}

/* ---- every T1 group, subset and rho ---- */

/* index of the image array p in Sym(n) */
static uint32_t index_of(const t1_sym *s, const uint32_t *p)
{
    for (uint32_t e = 0; e < s->count; ++e) {
        if (s->n == 0 || memcmp(s->elem[e], p, s->n * sizeof *p) == 0) {
            return e;
        }
    }
    return UINT32_MAX;
}

static void t1_tier(void)
{
    uint32_t cases = 0, changes = 0, renames = 0;
    canon_workspace *ws = NULL; /* one workspace for the whole tier (reuse across degrees) */
    CHECK(canon_workspace_create(ctx_of[0], &ws) == CANON_COMPLETE);
    for (uint32_t n = 0; n <= 4; ++n) {
        t1_sym s;
        t1_sym_init(&s, n);
        t1_group groups[T1_MAX_GROUPS];
        const uint32_t ng = t1_subgroups(&s, groups);
        for (uint32_t gi = 0; gi < ng; ++gi) {
            const t1_group *grp = &groups[gi];
            for (int b = 0; b < 2; ++b) {
                canon_group *g = group_of(b, n, grp->gens, grp->gen_count);
                for (uint32_t mask = 0; mask < (1u << n); ++mask) {
                    uint32_t atoms[4], k = 0;
                    for (uint32_t a = 0; a < n; ++a) {
                        if (mask >> a & 1u) {
                            atoms[k++] = a;
                        }
                    }
                    canon_object *x = subset_of(n, atoms, k);
                    canon_status st;
                    canon_result *stab = solve(ws, g, x, CANON_OBJECTIVE_STABILISER, NULL, 0, &st);
                    size_t stab_len = 0;
                    const uint8_t *stab_bytes = canon_result_group_bytes(stab, &stab_len);
                    for (uint32_t ri = 0; ri < s.count; ++ri) {
                        const uint32_t *rho = s.elem[ri];
                        canon_result *r =
                            solve(ws, g, x, CANON_OBJECTIVE_CANONICAL_LABELING_COSET, rho, 0, &st);
                        CHECK(st == CANON_COMPLETE);
                        uint32_t deg = 0;
                        const uint32_t *t = canon_result_witness(r, &deg);
                        const uint32_t *lambda = canon_result_labeling(r, &deg);
                        CHECK(t != NULL && lambda != NULL && deg == n);
                        /* lambda = rho t, and lambda rho^-1 = rho t rho^-1 in G (spec 3.1:
                         * "lambda = g rho in Lambda") */
                        uint32_t rt[4], rho_inv[4], k_[4];
                        canon_perm_compose(rho, t, rt, n);
                        CHECK(n == 0 || memcmp(rt, lambda, n * sizeof *rt) == 0);
                        canon_perm_inverse(rho, rho_inv, n);
                        canon_perm_compose(lambda, rho_inv, k_, n); /* lambda first */
                        CHECK(grp->mask >> index_of(&s, k_) & 1u);
                        /* c equals P1 on the transformed problem (x^rho, rho^-1 G rho), the
                         * latter built from hand-conjugated generators */
                        uint32_t xp_atoms[4], cg[24 * 4];
                        for (uint32_t i = 0; i < k; ++i) {
                            xp_atoms[i] = rho[atoms[i]];
                        }
                        for (uint32_t j = 0; j < grp->gen_count; ++j) {
                            for (uint32_t v = 0; v < n; ++v) {
                                cg[j * n + rho[v]] = rho[grp->gens[j * n + v]];
                            }
                        }
                        canon_object *xp = subset_of(n, xp_atoms, k);
                        canon_group *gp = group_of(b, n, cg, grp->gen_count);
                        canon_result *d =
                            solve(ws, gp, xp, CANON_OBJECTIVE_CANONICAL_IMAGE, NULL, 0, &st);
                        size_t la = 0;
                        CHECK(same(canon_result_bytes, r, d));
                        CHECK(same(canon_result_trace, r, d));
                        const uint32_t *td = canon_result_witness(d, &deg);
                        CHECK(n == 0 || (td != NULL && memcmp(td, t, n * sizeof *t) == 0));
                        canon_result_release(d);
                        canon_group_release(gp);
                        canon_object_release(xp);
                        /* the payload: Group(A) || Perm(least of A lambda), A by brute force */
                        uint32_t least[4] = {0, 0, 0, 0};
                        int have = 0;
                        for (uint32_t e = 0; e < s.count; ++e) {
                            if (!(grp->mask >> e & 1u)) {
                                continue;
                            }
                            uint32_t fixes = 1, img = 0;
                            for (uint32_t i = 0; i < k; ++i) {
                                img |= 1u << s.elem[e][atoms[i]];
                            }
                            fixes = img == mask;
                            if (!fixes) {
                                continue;
                            }
                            uint32_t al[4];
                            canon_perm_compose(s.elem[e], lambda, al, n); /* a lambda */
                            if (!have || canon_perm_lex_compare(al, least, n) < 0) {
                                memcpy(least, al, sizeof least);
                                have = 1;
                            }
                        }
                        const uint8_t *gb = canon_result_group_bytes(r, &la);
                        CHECK(gb != NULL && stab_bytes != NULL && la > stab_len &&
                              memcmp(gb, stab_bytes, stab_len) == 0);
                        uint32_t moved = 0;
                        for (uint32_t v = 0; v < n; ++v) {
                            moved += least[v] != v;
                        }
                        CHECK(la == stab_len + 4u + 8u * moved);
                        if (gb != NULL && la == stab_len + 4u + 8u * moved) {
                            const uint8_t *pp = gb + stab_len + 4;
                            for (uint32_t v = 0; v < n; ++v) {
                                if (least[v] != v) {
                                    CHECK(pp[3] == v && pp[7] == least[v]); /* source, target */
                                    pp += 8;
                                }
                            }
                        }
                        verify_ok(r);
                        /* representative change rho' = k rho, k in G (spec 3.1: "G' is
                         * unchanged and x^rho_new is in the same G' orbit, hence the canonical
                         * target bytes are unchanged"); A lambda is the same set */
                        for (uint32_t e = 0; e < s.count; ++e) {
                            if (!(grp->mask >> e & 1u) || (n == 4 && e % 5u != gi % 5u)) {
                                continue;
                            }
                            uint32_t rho2[4];
                            canon_perm_compose(s.elem[e], rho, rho2, n); /* k first */
                            canon_result *r2 = solve(
                                ws, g, x, CANON_OBJECTIVE_CANONICAL_LABELING_COSET, rho2, 0, &st);
                            CHECK(same(canon_result_bytes, r2, r));
                            CHECK(same(canon_result_trace, r2, r));
                            CHECK(same(canon_result_group_bytes, r2, r));
                            canon_result_release(r2);
                            changes += 1;
                        }
                        /* coordinate rename by mu (spec 3.1: "use x^mu, Lambda_new =
                         * mu^-1 Lambda and output mu^-1 lambda"): x^mu, mu^-1 G mu, mu^-1 rho */
                        const uint32_t mi = (ri + 1u + gi) % s.count;
                        const uint32_t *mu = s.elem[mi];
                        uint32_t mu_inv[4], rho3[4], xm[4], gm[24 * 4];
                        canon_perm_inverse(mu, mu_inv, n);
                        canon_perm_compose(mu_inv, rho, rho3, n);
                        for (uint32_t i = 0; i < k; ++i) {
                            xm[i] = mu[atoms[i]];
                        }
                        for (uint32_t j = 0; j < grp->gen_count; ++j) {
                            for (uint32_t v = 0; v < n; ++v) {
                                gm[j * n + mu[v]] = mu[grp->gens[j * n + v]];
                            }
                        }
                        canon_object *xr = subset_of(n, xm, k);
                        canon_group *gr = group_of(b, n, gm, grp->gen_count);
                        canon_result *r3 = solve(
                            ws, gr, xr, CANON_OBJECTIVE_CANONICAL_LABELING_COSET, rho3, 0, &st);
                        CHECK(same(canon_result_bytes, r3, r));
                        const uint32_t *l3 = canon_result_labeling(r3, &deg);
                        uint32_t want[4];
                        canon_perm_compose(mu_inv, lambda, want, n); /* mu^-1 lambda */
                        CHECK(n == 0 || (l3 != NULL && memcmp(l3, want, n * sizeof *want) == 0));
                        canon_result_release(r3);
                        canon_group_release(gr);
                        canon_object_release(xr);
                        renames += 1;
                        canon_result_release(r);
                        cases += 1;
                    }
                    canon_result_release(stab);
                    canon_object_release(x);
                }
                canon_group_release(g);
            }
        }
    }
    canon_workspace_release(ws);
    printf("T1 labeling: %u cases, %u representative changes, %u renames (both backends)\n", cases,
           changes, renames);
}

/* ---- graphs and nested objects ---- */

static void other_kinds(void)
{
    /* spec 7.4 one-arc graph 0 -> 1 under Sym(2), rho = [1,0]: x' = the arc 1 -> 0 */
    const uint32_t swap[2] = {1, 0};
    const canon_arc arc01 = {0, 1, NULL, 0, 1}, arc10 = {1, 0, NULL, 0, 1};
    canon_object *x = NULL, *xp = NULL;
    CHECK(canon_object_create_graph(ctx_of[0], 2, NULL, NULL, &arc01, 1, &x) == CANON_COMPLETE);
    CHECK(canon_object_create_graph(ctx_of[0], 2, NULL, NULL, &arc10, 1, &xp) == CANON_COMPLETE);
    /* a nested object: the tuple (atom 0, atom 1, literal "") and its image under rho */
    const uint8_t tuple01[] = {0x43, 0x4e, 0x02, 0, 1,    0, 1, 0,    0, 0, 2, 0, 0,
                               0,    4,    0x01, 0, 0,    0, 0, 0x01, 0, 0, 0, 1, 0x02,
                               0,    0,    0,    0, 0x03, 0, 0, 0,    3, 0, 0, 0, 0,
                               0,    0,    0,    1, 0,    0, 0, 2,    0, 0, 0, 3};
    uint8_t tuple10[sizeof tuple01];
    memcpy(tuple10, tuple01, sizeof tuple01);
    tuple10[38] = 1; /* children (1, 0, 2): atoms swapped */
    tuple10[42] = 0;
    canon_object *dx = NULL, *dxp = NULL;
    CHECK(canon_object_create(ctx_of[0], CANON_SCHEMA_EXT_DAG_1, CANON_ACTION_ATOM_TRANSPORT_1, 2,
                              tuple01, sizeof tuple01, &dx) == CANON_COMPLETE);
    CHECK(canon_object_create(ctx_of[0], CANON_SCHEMA_EXT_DAG_1, CANON_ACTION_ATOM_TRANSPORT_1, 2,
                              tuple10, sizeof tuple10, &dxp) == CANON_COMPLETE);
    canon_workspace *ws = NULL;
    CHECK(canon_workspace_create(ctx_of[0], &ws) == CANON_COMPLETE);
    for (int b = 0; b < 2; ++b) {
        canon_group *g = group_of(b, 2, swap, 1); /* G' = G for G = Sym(2) */
        const canon_object *objs[2][2] = {{x, xp}, {dx, dxp}};
        for (int kind = 0; kind < 2; ++kind) {
            for (int reuse = 0; reuse < 2; ++reuse) {
                canon_status st;
                canon_result *r = solve(reuse ? ws : NULL, g, objs[kind][0],
                                        CANON_OBJECTIVE_CANONICAL_LABELING_COSET, swap, 0, &st);
                canon_result *d = solve(reuse ? ws : NULL, g, objs[kind][1],
                                        CANON_OBJECTIVE_CANONICAL_IMAGE, NULL, 0, &st);
                CHECK(st == CANON_COMPLETE);
                CHECK(same(canon_result_bytes, r, d));
                CHECK(same(canon_result_trace, r, d));
                verify_ok(r);
                canon_result_release(r);
                canon_result_release(d);
            }
        }
        canon_group_release(g);
    }
    canon_workspace_release(ws);
    canon_object_release(x);
    canon_object_release(xp);
    canon_object_release(dx);
    canon_object_release(dxp);
}

/* ---- verify_witness, validation, quota ---- */

static void checks_and_errors(void)
{
    const uint32_t swap01[3] = {1, 0, 2}, rho[3] = {1, 2, 0}, zero = 0;
    canon_object *x = subset_of(3, &zero, 1);
    canon_group *g = group_of(0, 3, swap01, 1);
    canon_status st;
    canon_result *r = solve(NULL, g, x, CANON_OBJECTIVE_CANONICAL_LABELING_COSET, rho, 0, &st);
    verify_ok(r);
    /* the result owns its arrays; tampering in place is defined and must be noticed */
    uint32_t deg = 0;
    uint32_t *t = (uint32_t *)(uintptr_t)canon_result_witness(r, &deg);
    uint32_t *lambda = (uint32_t *)(uintptr_t)canon_result_labeling(r, &deg);
    const uint32_t t0[3] = {t[0], t[1], t[2]}, l0[3] = {lambda[0], lambda[1], lambda[2]};
    bool valid = true;
    const uint32_t id[3] = {0, 1, 2}, bad[3] = {0, 0, 1};
    memcpy(t, id, sizeof id); /* lambda != rho t */
    CHECK(canon_result_verify_witness(r, &valid) == CANON_COMPLETE && !valid);
    memcpy(t, bad, sizeof bad); /* not a bijection */
    CHECK(canon_result_verify_witness(r, &valid) == CANON_COMPLETE && !valid);
    memcpy(t, t0, sizeof t0);
    /* t rho instead of rho t: lambda = [1,0,2] is in G rho... (G rho = {[1,2,0], [2,1,0]})? no:
     * [1,0,2] rho^-1 = [0,2,1] is not in G */
    const uint32_t wrong_side[3] = {1, 0, 2};
    memcpy(lambda, wrong_side, sizeof wrong_side);
    CHECK(canon_result_verify_witness(r, &valid) == CANON_COMPLETE && !valid);
    memcpy(lambda, l0, sizeof l0);
    verify_ok(r);
    canon_result_release(r); /* the problem was already released: the result kept rho */
    CHECK(canon_result_labeling(NULL, &deg) == NULL && deg == 0);

    /* validation (canon.h order): missing rho, rho for another objective, non-bijective rho,
     * profile NO_TREE, a deterministic witness */
    canon_problem *p = NULL;
    canon_problem_options none = {CANON_WITNESS_ANY, NULL}, with = {CANON_WITNESS_ANY, rho};
    canon_problem_options badrho = {CANON_WITNESS_ANY, bad},
                          det = {CANON_WITNESS_DETERMINISTIC, rho};
    const canon_objective L = CANON_OBJECTIVE_CANONICAL_LABELING_COSET;
#define CREATE(obj, prof, opt)                                                                     \
    canon_problem_create_with_options(ctx_of[0], g, x, NULL, obj, prof, CANON_ENCODING_CDAG_2,     \
                                      CANON_ORDER_CDAG_BYTE_1, NULL, opt, &p)
    CHECK(CREATE(L, CANON_PROFILE_P1, &none) == CANON_INVALID_INPUT && p == NULL);
    CHECK(CREATE(L, CANON_PROFILE_P1, NULL) == CANON_INVALID_INPUT && p == NULL);
    CHECK(CREATE(L, CANON_PROFILE_P1, &badrho) == CANON_INVALID_INPUT && p == NULL);
    CHECK(CREATE(CANON_OBJECTIVE_CANONICAL_IMAGE, CANON_PROFILE_P1, &with) == CANON_INVALID_INPUT);
    CHECK(CREATE(CANON_OBJECTIVE_STABILISER, CANON_PROFILE_NO_TREE, &with) == CANON_INVALID_INPUT);
    CHECK(CREATE(L, CANON_PROFILE_NO_TREE, &with) == CANON_UNSUPPORTED_ACTION);
    CHECK(CREATE(L, CANON_PROFILE_P1, &det) == CANON_UNSUPPORTED_ACTION);
    CHECK(canon_problem_create_with_options(ctx_of[0], g, x, NULL, L, CANON_PROFILE_P1,
                                            CANON_ENCODING_CDAG_2, CANON_ORDER_SIMPLE_UPPER_1, NULL,
                                            &with, &p) == CANON_UNSUPPORTED_ACTION);
    /* the problem copies rho */
    uint32_t mutable_rho[3] = {1, 2, 0};
    canon_problem_options copy = {CANON_WITNESS_ANY, mutable_rho};
    CHECK(CREATE(L, CANON_PROFILE_P1, &copy) == CANON_COMPLETE);
    mutable_rho[0] = 0;
    mutable_rho[1] = 0;
    canon_workspace *ws = NULL;
    CHECK(canon_workspace_create(ctx_of[0], &ws) == CANON_COMPLETE);
    CHECK(canon_solve(ws, p, &r) == CANON_COMPLETE);
    const uint32_t *l2 = canon_result_labeling(r, &deg);
    CHECK(l2 != NULL && l2[0] == 2 && l2[1] == 1 && l2[2] == 0); /* product_sides */
    canon_result_release(r);
    canon_problem_release(p);
#undef CREATE
    /* quota (spec 11.1): P1 NODE tokens plus the stabiliser's visits.  Here P1 on D_3 has 2
     * nodes (root, its single child after the G stage? computed below) and the enumeration of
     * G = <(0 1)> has 3 visits (root and two leaves). */
    uint64_t nodes = 0;
    for (uint64_t q = 1; q < 64 && nodes == 0; ++q) {
        r = solve(ws, g, x, L, rho, q, &st);
        if (st == CANON_COMPLETE) {
            nodes = q;
        } else {
            CHECK(st == CANON_CAPACITY_LIMIT && canon_result_status(r) == CANON_CAPACITY_LIMIT);
            CHECK(canon_result_labeling(r, &deg) == NULL && deg == 0);
            CHECK(canon_result_get_flags(r).witness_valid == false);
        }
        canon_result_release(r);
    }
    /* the D_3 tree of x' = {1} under G' = {id, [0,2,1]} is discrete after the root's G stage:
     * one NODE; then 3 visits */
    CHECK(nodes == 1u + 3u);
    canon_workspace_release(ws);
    canon_group_release(g);
    canon_object_release(x);
}

/* Counters of the labeling consumer on every T1 group, subset and rho (chain backend; printed
 * for docs/slices/S6-notes.md). */
static void counters(void)
{
    uint64_t runs = 0, p1 = 0, visits = 0, hits = 0, builds = 0, descents = 0;
    canon_obj_search s;
    canon_p1_search p1s;
    canon_obj_search_init(&s);
    canon_p1_search_init(&p1s);
    for (uint32_t n = 0; n <= 4; ++n) {
        t1_sym sym;
        t1_sym_init(&sym, n);
        t1_group groups[T1_MAX_GROUPS];
        const uint32_t ng = t1_subgroups(&sym, groups);
        for (uint32_t gi = 0; gi < ng; ++gi) {
            canon_group *g = group_of(0, n, groups[gi].gens, groups[gi].gen_count);
            for (uint32_t mask = 0; mask < (1u << n); ++mask) {
                uint32_t atoms[4], k = 0;
                for (uint32_t a = 0; a < n; ++a) {
                    if (mask >> a & 1u) {
                        atoms[k++] = a;
                    }
                }
                canon_root x;
                memset(&x, 0, sizeof x);
                x.kind = CANON_ROOT_SUBSET;
                x.n = n;
                CHECK(canon_subset_init(&x.u.subset, n, atoms, k) == CANON_COMPLETE);
                for (uint32_t ri = 0; ri < sym.count; ++ri) {
                    canon_obj_outcome o;
                    CHECK(canon_obj_labeling(&s, &p1s, g, &x, sym.elem[ri], 1u << 20, &o) ==
                          CANON_COMPLETE);
                    CHECK(o.labeling && o.group && o.p1 && o.sign == 0);
                    runs += 1;
                    p1 += s.stats.p1_nodes;
                    visits += s.stats.nodes;
                    hits += s.stats.hits;
                    builds += s.stats.stab_builds;
                    descents += s.stats.coset.descents;
                }
                canon_subset_free(&x.u.subset);
            }
            canon_group_release(g);
        }
    }
    /* module contract: rho must be a bijection */
    canon_obj_outcome o;
    canon_group *g = group_of(0, 2, NULL, 0);
    canon_root x;
    memset(&x, 0, sizeof x);
    x.kind = CANON_ROOT_SUBSET;
    x.n = 2;
    CHECK(canon_subset_init(&x.u.subset, 2, NULL, 0) == CANON_COMPLETE);
    const uint32_t bad[2] = {1, 1};
    CHECK(canon_obj_labeling(&s, &p1s, g, &x, bad, 10, &o) == CANON_INVALID_INPUT);
    CHECK(canon_obj_labeling(&s, &p1s, g, &x, NULL, 10, &o) == CANON_INVALID_INPUT);
    CHECK(!o.flags.witness_valid && !o.labeling);
    CHECK(canon_obj_signed(&s, &p1s, g, &x, 10, &o) == CANON_UNSUPPORTED_ACTION); /* unsigned */
    canon_subset_free(&x.u.subset);
    canon_group_release(g);
    canon_obj_search_free(&s);
    canon_p1_search_free(&p1s);
    printf("T1 labeling consumer (%llu runs, one workspace): P1 nodes %llu, stabiliser visits "
           "%llu, hits %llu, verified A rebuilds %llu, descents %llu (one conjugated group per "
           "run)\n",
           (unsigned long long)runs, (unsigned long long)p1, (unsigned long long)visits,
           (unsigned long long)hits, (unsigned long long)builds, (unsigned long long)descents);
}

int main(void)
{
    const canon_context_options chain = {CANON_BACKEND_CHAIN}, expl = {CANON_BACKEND_EXPLICIT};
    CHECK(canon_context_create_with_options(NULL, &chain, &ctx_of[0]) == CANON_COMPLETE);
    CHECK(canon_context_create_with_options(NULL, &expl, &ctx_of[1]) == CANON_COMPLETE);
    golden();
    product_sides();
    conjugates();
    t1_tier();
    other_kinds();
    checks_and_errors();
    counters();
    canon_context_release(ctx_of[0]);
    canon_context_release(ctx_of[1]);
    return check_finish("test_labeling");
}
