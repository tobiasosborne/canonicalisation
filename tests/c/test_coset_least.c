/* Unit tests for the constrained least element of a coset and the least element outside a
 * subgroup (slice S4, docs/slices/S4.md 3.1, 4; spec 3, 8.1, 9.4).
 *
 *  - the product side, pinned on a transposition and a 3-cycle: J r = {j r} (j first), not
 *    r J;
 *  - every T1 group (every subgroup of Sym(n), n <= 4) as J, every r in Sym(n), random
 *    constraint sets (including inconsistent and unreachable ones), from every level of the
 *    chain and of rebased chains: against brute force over the explicit element tables;
 *  - T2: random groups with n <= 7, elements listed by the explicit backend's enumerator (the
 *    oracle), random cosets and constraints;
 *  - spec 9.4 rule 2 (least element of H \ K) on every T1 group H and random subgroups K;
 *  - invalid arguments. */
#include <stdlib.h>
#include <string.h>

#include "bsgs/chain.h"
#include "bsgs/group.h"
#include "check.h"
#include "coset/coset.h"
#include "perm/perm.h"
#include "t1_groups.h"

#define MAXN 8u
#define MAXG 5040u

static int eq(const uint32_t *a, const uint32_t *b, uint32_t n)
{
    return n == 0 || memcmp(a, b, n * sizeof *a) == 0;
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

/* Brute force over an element list: the least j r (j first, spec 3) with the constraints. */
static int brute_least(const uint32_t *elems, uint32_t count, uint32_t n, const uint32_t *r,
                       const canon_coset_constraint *cons, uint32_t k, uint32_t *out)
{
    int found = 0;
    uint32_t g[MAXN];
    for (uint32_t i = 0; i < count; ++i) {
        canon_perm_compose(elems + (size_t)i * n, r, g, n); /* g = j r */
        int ok = 1;
        for (uint32_t c = 0; c < k && ok; ++c) {
            ok = g[cons[c].point] == cons[c].image;
        }
        if (ok && (!found || canon_perm_lex_compare(g, out, n) < 0)) {
            memcpy(out, g, n * sizeof *g);
            found = 1;
        }
    }
    return found;
}

/* ---- the product side (spec 3) ---- */

static void product_side(void)
{
    /* J = <(1 2)> = {id, [0,2,1]}, r = the 3-cycle [1,2,0].  J r = {[1,2,0], [1,0,2]}, least
     * [1,0,2]; r J = {[1,2,0], [2,1,0]}, least [1,2,0].  A side error cannot pass. */
    const uint32_t tau[3] = {0, 2, 1}, cyc[3] = {1, 2, 0};
    canon_bsgs j;
    CHECK(canon_bsgs_build_verified(&j, 3, tau, 1) == CANON_COMPLETE);
    uint32_t out[3];
    bool found = false;
    CHECK(canon_coset_least(&j, 0, cyc, NULL, 0, out, &found, NULL, NULL) == CANON_COMPLETE &&
          found);
    const uint32_t want[3] = {1, 0, 2};
    CHECK(eq(out, want, 3));
    /* with the constraint 0 -> 1 both elements qualify; with 1 -> 2 only r itself */
    const canon_coset_constraint c12 = {1, 2};
    CHECK(canon_coset_least(&j, 0, cyc, &c12, 1, out, &found, NULL, NULL) == CANON_COMPLETE &&
          found);
    CHECK(eq(out, cyc, 3));
    /* 1 -> 1 is not an image of 1 in J r ({2, 0}): empty */
    const canon_coset_constraint c11 = {1, 1};
    CHECK(canon_coset_least(&j, 0, cyc, &c11, 1, out, &found, NULL, NULL) == CANON_COMPLETE &&
          !found);
    canon_bsgs_free(&j);

    /* spec 7.4 labeling-coset payload case: H = 1 on degree two, r = [1,0]: least is [1,0] */
    canon_bsgs one;
    CHECK(canon_bsgs_build_verified(&one, 2, NULL, 0) == CANON_COMPLETE);
    const uint32_t swap[2] = {1, 0};
    CHECK(canon_coset_least(&one, 0, swap, NULL, 0, out, &found, NULL, NULL) == CANON_COMPLETE &&
          found && eq(out, swap, 2));
    canon_bsgs_free(&one);
}

/* ---- T1 against the element tables ---- */

static void random_constraints(uint32_t n, canon_coset_constraint *cons, uint32_t *k)
{
    *k = (uint32_t)(check_rng() % (n + 2u));
    for (uint32_t i = 0; i < *k; ++i) {
        cons[i].point = (uint32_t)(check_rng() % n);
        cons[i].image = (uint32_t)(check_rng() % n);
    }
}

static canon_coset_scratch shared;

static void compare_one(const canon_bsgs *c, uint32_t level, const uint32_t *elems, uint32_t count,
                        uint32_t n, const uint32_t *r, const canon_coset_constraint *cons,
                        uint32_t k, uint64_t *found_count)
{
    uint32_t want[MAXN], got[MAXN];
    bool found = false;
    canon_coset_stats stats = {0, 0};
    CHECK(canon_coset_least(c, level, r, cons, k, got, &found, &stats, NULL) == CANON_COMPLETE);
    int bf = brute_least(elems, count, n, r, cons, k, want);
    CHECK((int)found == bf);
    if (found && bf) {
        CHECK(eq(got, want, n));
        *found_count += 1;
    }
    CHECK(stats.descents == 1);
    /* the same with one grow-only scratch shared across all cases and degrees (review item 5) */
    uint32_t again[MAXN];
    bool found2 = false;
    CHECK(canon_coset_least(c, level, r, cons, k, again, &found2, NULL, &shared) == CANON_COMPLETE);
    CHECK(found2 == found && (!found || eq(again, got, n)));
}

/* the elements of a T1 group as a flat table */
static uint32_t t1_elements(const t1_sym *s, uint32_t mask, uint32_t *elems)
{
    uint32_t count = 0;
    for (uint32_t e = 0; e < s->count; ++e) {
        if (mask >> e & 1u) {
            memcpy(elems + count * s->n, s->elem[e], s->n * sizeof(uint32_t));
            ++count;
        }
    }
    return count;
}

static void t1(void)
{
    uint64_t cases = 0, nonempty = 0;
    for (uint32_t n = 1; n <= 4; ++n) {
        t1_sym s;
        t1_sym_init(&s, n);
        t1_group groups[T1_MAX_GROUPS];
        const uint32_t ng = t1_subgroups(&s, groups);
        for (uint32_t gi = 0; gi < ng; ++gi) {
            canon_bsgs c;
            CHECK(canon_bsgs_build_verified(&c, n, groups[gi].gens, groups[gi].gen_count) ==
                  CANON_COMPLETE);
            uint32_t elems[24 * 4];
            const uint32_t count = t1_elements(&s, groups[gi].mask, elems);
            for (uint32_t ri = 0; ri < s.count; ++ri) {
                for (uint32_t trial = 0; trial < 6; ++trial) {
                    canon_coset_constraint cons[8];
                    uint32_t k = 0;
                    if (trial > 0) {
                        random_constraints(n, cons, &k);
                    }
                    compare_one(&c, 0, elems, count, n, s.elem[ri], cons, k, &nonempty);
                    ++cases;
                }
            }
            /* deeper levels: K_level is the pointwise stabiliser of the base prefix */
            for (uint32_t lv = 1; lv <= c.depth; ++lv) {
                uint32_t sub[24 * 4], sc = 0;
                for (uint32_t i = 0; i < count; ++i) {
                    int fixes = 1;
                    for (uint32_t j = 0; j < lv; ++j) {
                        const uint32_t b = c.levels[j].base_point;
                        fixes = fixes && elems[i * n + b] == b;
                    }
                    if (fixes) {
                        memcpy(sub + sc * n, elems + i * n, n * sizeof *sub);
                        ++sc;
                    }
                }
                CHECK(sc == canon_bsgs_suffix_order(&c, lv));
                uint32_t r[MAXN];
                random_perm(r, n);
                canon_coset_constraint cons[8];
                uint32_t k = 0;
                random_constraints(n, cons, &k);
                compare_one(&c, lv, sub, sc, n, r, cons, k, &nonempty);
                ++cases;
            }
            canon_bsgs_free(&c);
        }
    }
    printf("T1 coset least: %llu cases, %llu nonempty\n", (unsigned long long)cases,
           (unsigned long long)nonempty);
    CHECK(nonempty > cases / 4);
}

/* ---- T2: random groups, elements from the explicit backend's enumerator ---- */

typedef struct collect {
    uint32_t n, count;
    uint32_t *elems;
} collect;

static canon_status collect_leaf(void *user, const uint32_t *r, bool *stop)
{
    collect *c = user;
    (void)stop;
    memcpy(c->elems + (size_t)c->count * c->n, r, c->n * sizeof *r);
    c->count += 1;
    return CANON_COMPLETE;
}

static void random_generators(uint32_t n, uint32_t *gens, uint32_t *count)
{
    *count = 1u + (uint32_t)(check_rng() % 3u);
    for (uint32_t i = 0; i < *count; ++i) {
        random_perm(gens + (size_t)i * n, n);
    }
}

static void t2(void)
{
    canon_context *ctx = NULL;
    const canon_context_options explicit_opts = {CANON_BACKEND_EXPLICIT};
    CHECK(canon_context_create_with_options(NULL, &explicit_opts, &ctx) == CANON_COMPLETE);
    uint32_t *elems = malloc((size_t)MAXG * MAXN * sizeof *elems);
    uint64_t cases = 0, nonempty = 0;
    for (uint32_t trial = 0; trial < 60; ++trial) {
        const uint32_t n = 3u + (uint32_t)(check_rng() % 5u); /* 3..7 */
        uint32_t gens[3 * MAXN], count = 0;
        random_generators(n, gens, &count);
        canon_group *g = NULL;
        CHECK(canon_group_create(ctx, n, gens, count, &g) == CANON_COMPLETE);
        collect col = {n, 0, elems};
        canon_coset_visitor v;
        canon_coset_visitor_init(&v, collect_leaf, NULL, &col, UINT64_MAX);
        CHECK(g->ops->enumerate(g, &v) == CANON_COMPLETE);
        CHECK(col.count == g->ops->order(g));
        canon_bsgs c;
        CHECK(canon_bsgs_build_verified(&c, n, gens, count) == CANON_COMPLETE);
        for (uint32_t i = 0; i < 20; ++i) {
            uint32_t r[MAXN];
            random_perm(r, n);
            canon_coset_constraint cons[MAXN + 2];
            uint32_t k = 0;
            if (i % 2 == 0) {
                /* constraints from a member of the coset, so the result is nonempty */
                uint32_t member[MAXN];
                canon_perm_compose(elems + (size_t)(check_rng() % col.count) * n, r, member, n);
                k = (uint32_t)(check_rng() % (n + 1u));
                for (uint32_t j = 0; j < k; ++j) {
                    cons[j].point = (uint32_t)(check_rng() % n);
                    cons[j].image = member[cons[j].point];
                }
            } else {
                random_constraints(n, cons, &k);
            }
            compare_one(&c, 0, elems, col.count, n, r, cons, k, &nonempty);
            ++cases;
        }
        canon_bsgs_free(&c);
        canon_group_release(g);
    }
    printf("T2 coset least: %llu cases, %llu nonempty\n", (unsigned long long)cases,
           (unsigned long long)nonempty);
    CHECK(nonempty >= cases / 2);
    free(elems);
    canon_context_release(ctx);
}

/* ---- spec 9.4 rule 2: least element of H \ K ---- */

static void outside(void)
{
    uint64_t cases = 0;
    for (uint32_t n = 1; n <= 4; ++n) {
        t1_sym s;
        t1_sym_init(&s, n);
        t1_group groups[T1_MAX_GROUPS];
        const uint32_t ng = t1_subgroups(&s, groups);
        for (uint32_t hi = 0; hi < ng; ++hi) {
            canon_bsgs h;
            CHECK(canon_bsgs_build_verified(&h, n, groups[hi].gens, groups[hi].gen_count) ==
                  CANON_COMPLETE);
            /* every subgroup K of H (from the T1 list) */
            for (uint32_t ki = 0; ki < ng; ++ki) {
                if ((groups[ki].mask & ~groups[hi].mask) != 0) {
                    continue;
                }
                canon_bsgs k;
                CHECK(canon_bsgs_build_verified(&k, n, groups[ki].gens, groups[ki].gen_count) ==
                      CANON_COMPLETE);
                uint32_t out[MAXN];
                bool found = true;
                CHECK(canon_coset_least_outside(&h, &k, out, &found, NULL, NULL) == CANON_COMPLETE);
                const uint32_t rest = groups[hi].mask & ~groups[ki].mask;
                CHECK(found == (rest != 0));
                if (rest != 0) {
                    uint32_t e = 0;
                    while (!(rest >> e & 1u)) {
                        ++e; /* elements are numbered in lexicographic order */
                    }
                    CHECK(eq(out, s.elem[e], n));
                }
                canon_bsgs_free(&k);
                ++cases;
            }
            canon_bsgs_free(&h);
        }
    }
    printf("rule 2 least outside: %llu (H, K) pairs\n", (unsigned long long)cases);
}

static void invalid(void)
{
    canon_bsgs c, d;
    const uint32_t cyc[3] = {1, 2, 0};
    CHECK(canon_bsgs_build_verified(&c, 3, cyc, 1) == CANON_COMPLETE);
    CHECK(canon_bsgs_build_verified(&d, 2, NULL, 0) == CANON_COMPLETE);
    uint32_t out[3];
    bool found = true;
    const canon_coset_constraint bad = {3, 0}, bad_image = {0, 3};
    CHECK(canon_coset_least(&c, 0, NULL, &bad, 1, out, &found, NULL, NULL) == CANON_INVALID_INPUT &&
          !found);
    CHECK(canon_coset_least(&c, 0, NULL, &bad_image, 1, out, &found, NULL, NULL) ==
          CANON_INVALID_INPUT);
    CHECK(canon_coset_least(&c, c.depth + 1, NULL, NULL, 0, out, &found, NULL, NULL) ==
          CANON_INVALID_INPUT);
    /* two images for one point: empty, not invalid */
    const canon_coset_constraint two[2] = {{0, 1}, {0, 2}};
    CHECK(canon_coset_least(&c, 0, NULL, two, 2, out, &found, NULL, NULL) == CANON_COMPLETE &&
          !found);
    CHECK(canon_coset_least_outside(&c, &d, out, &found, NULL, NULL) == CANON_INVALID_INPUT);
    /* degree 0: the empty permutation */
    canon_bsgs z;
    CHECK(canon_bsgs_build_verified(&z, 0, NULL, 0) == CANON_COMPLETE);
    CHECK(canon_coset_least(&z, 0, NULL, NULL, 0, out, &found, NULL, NULL) == CANON_COMPLETE &&
          found);
    CHECK(canon_coset_least_outside(&z, &z, out, &found, NULL, NULL) == CANON_COMPLETE && !found);
    canon_bsgs_free(&z);
    canon_bsgs_free(&c);
    canon_bsgs_free(&d);
}

int main(void)
{
    canon_coset_scratch_init(&shared);
    product_side();
    t1();
    t2();
    outside();
    invalid();
    canon_coset_scratch_free(&shared);
    return check_finish("test_coset_least");
}
