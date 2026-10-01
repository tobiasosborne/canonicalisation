/* Unit tests for the stabiliser chain and its group backend (slice S3, docs/slices/S3.md 3;
 * spec 3, 7.1, 7.2, 9.1, 9.2, 11.1).
 *
 *  - the side of every product, pinned on a 3-cycle and a transposition: transporter
 *    reconstruction along a Schreier path, sifting, and the spec 7.2 step t <- t u;
 *  - T1: every subgroup of Sym(n), n <= 4, from greedy generators, against the explicit
 *    backend (order, membership of every element of Sym(n), tuple_min t and orbit ids on random
 *    lists of every length, rebase to random prefixes, verification);
 *  - T2: 200 seeded random generator sets with n <= 8, the same comparisons;
 *  - Sym(12) from two generators, an n = 64 cycle (long Schreier path), Sym(20)/Sym(21)
 *    capacity, the public backend option and canon_group_order.
 * Construction counters for T1 and T2 are printed (docs/slices/S3-notes.md records them). */
#include <stdlib.h>
#include <string.h>

#include "bsgs/chain.h"
#include "bsgs/chain_backend.h"
#include "bsgs/explicit.h"
#include "bsgs/verify.h"
#include "check.h"
#include "perm/perm.h"
#include "t1_groups.h"

#define MAXN 64u

/* spec 9.1 membership through the ops (S3 review item 5: contains reports allocation
 * failure) */
static bool in(const canon_group *g, const uint32_t *p)
{
    bool r = false;
    CHECK(g->ops->contains(g, p, &r) == CANON_COMPLETE);
    return r;
}

/* membership in a bare chain */
static bool chain_in(const canon_bsgs *c, const uint32_t *p)
{
    bool r = false;
    CHECK(canon_bsgs_contains(c, p, &r) == CANON_COMPLETE);
    return r;
}

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

static void add_stats(canon_bsgs_stats *acc, const canon_bsgs_stats *s)
{
    acc->candidates += s->candidates;
    acc->sifts += s->sifts;
    acc->compositions += s->compositions;
    acc->insertions += s->insertions;
}

static void print_stats(const char *what, uint32_t groups, const canon_bsgs_stats *s)
{
    printf("%s (%u groups): candidates %llu, sifts %llu, insertions %llu, "
           "dense compositions %llu\n",
           what, groups, (unsigned long long)s->candidates, (unsigned long long)s->sifts,
           (unsigned long long)s->insertions, (unsigned long long)s->compositions);
}

/* ---- the side of every product (spec 3: p[v] = v^p, (pq)[v] = q[p[v]]) ---- */

/* A hand-built level: n = 3, base point 1, generators (in gen_ids order) the transposition
 * tau = (1 2) = [0,2,1] and the 3-cycle c = (0 1 2) = [1,2,0]. */
static void conventions(void)
{
    const uint32_t tau[3] = {0, 2, 1}, cyc[3] = {1, 2, 0};
    canon_bsgs ch;
    canon_bsgs_init(&ch, 3);
    uint32_t id = 0, inv[3];
    CHECK(canon_perm_table_push(&ch.gens, tau, &id) == CANON_COMPLETE && id == 0);
    canon_perm_inverse(tau, inv, 3);
    CHECK(canon_perm_table_push(&ch.invs, inv, &id) == CANON_COMPLETE);
    CHECK(canon_perm_table_push(&ch.gens, cyc, &id) == CANON_COMPLETE && id == 1);
    canon_perm_inverse(cyc, inv, 3);
    CHECK(canon_perm_table_push(&ch.invs, inv, &id) == CANON_COMPLETE);
    ch.levels = calloc(2, sizeof *ch.levels);
    ch.level_cap = 2;
    ch.depth = 1;
    canon_bsgs_level *L = &ch.levels[0];
    uint32_t *block = malloc(4 * 3 * sizeof *block); /* the chain.c layout: one block */
    L->orbit = block;
    L->orbit_pos = block + 3;
    L->parent_point = block + 6;
    L->parent_gen = block + 9;
    for (uint32_t v = 0; v < 3; ++v) {
        L->orbit_pos[v] = CANON_BSGS_NONE;
    }
    L->base_point = 1;
    L->orbit_len = 0;
    L->gen_ids = malloc(2 * sizeof *L->gen_ids);
    L->gen_ids[0] = 0;
    L->gen_ids[1] = 1;
    L->gen_count = L->gen_cap = 2;
    ch.levels[1].base_point = CANON_BSGS_NONE;
    canon_bsgs_level_recompute(&ch, 0);
    /* Queue traversal: 1 -tau-> 2, then from 2: 2 -c-> 0.  The path to 0 is tau then c. */
    const uint32_t orbit[3] = {1, 2, 0};
    CHECK(L->orbit_len == 3 && eq(L->orbit, orbit, 3));
    CHECK(L->parent_point[1] == 1 && L->parent_gen[1] == 0);
    CHECK(L->parent_point[2] == 2 && L->parent_gen[2] == 1);

    /* Transporter: t_0 = tau c (tau first): [1,0,2], which sends the base point 1 to 0.  The
     * other order c tau = [2,1,0] fixes 1, so a side error cannot pass this check. */
    uint32_t t[3], ct[3], tc[3];
    CHECK(canon_bsgs_transporter(&ch, 0, 0, t) == CANON_COMPLETE);
    canon_perm_compose(tau, cyc, tc, 3);
    canon_perm_compose(cyc, tau, ct, 3);
    const uint32_t want_t[3] = {1, 0, 2};
    CHECK(eq(t, want_t, 3) && eq(t, tc, 3) && t[1] == 0);
    CHECK(!eq(ct, want_t, 3) && ct[1] != 0);
    CHECK(canon_bsgs_transporter(&ch, 0, 2, t) == CANON_COMPLETE && eq(t, tau, 3));

    /* Sift: g = c^2 = [2,0,1] sends the base point 1 to 0; g t_0^-1 = [2,1,0] fixes 1.  The
     * wrong side t_0^-1 g = [0,2,1] sends 1 to 2. */
    uint32_t g[3] = {2, 0, 1}, t0inv[3], wrong[3], stop = 7;
    canon_perm_inverse(want_t, t0inv, 3);
    canon_perm_compose(t0inv, g, wrong, 3);
    canon_bsgs_sift(&ch, 0, g, &stop, NULL);
    const uint32_t want_r[3] = {2, 1, 0};
    CHECK(stop == 1 && eq(g, want_r, 3) && g[1] == 1);
    CHECK(wrong[1] != 1);
    canon_bsgs_free(&ch);

    /* spec 7.2 step t <- t u on Sym(3) (generated by tau and c): for a full list L the
     * minimiser is unique with L^t = (0, 1, 2), i.e. t[L[i]] = i; for L = (2, 0) it is
     * t = [1,2,0].  When the procedure's first u is (0 2) the second is (1 2), and
     * (0 2)(1 2) = [1,2,0] while (1 2)(0 2) = [2,0,1]: the order of the step is pinned. */
    const uint32_t gens[6] = {0, 2, 1, 1, 2, 0};
    canon_group *grp = NULL;
    CHECK(canon_group_chain_create(3, gens, 2, &grp) == CANON_COMPLETE);
    const uint32_t L2[2] = {2, 0}, want_c[3] = {1, 2, 0};
    CHECK(grp->ops->tuple_min(grp, L2, 2, t, NULL) == CANON_COMPLETE && eq(t, want_c, 3));
    const uint32_t s02[3] = {2, 1, 0}, s12[3] = {0, 2, 1};
    canon_perm_compose(s02, s12, tc, 3);
    canon_perm_compose(s12, s02, ct, 3);
    CHECK(eq(tc, want_c, 3) && !eq(ct, want_c, 3));
    for (uint32_t r = 0; r < 6; ++r) {
        uint32_t Lf[3];
        random_perm(Lf, 3);
        CHECK(grp->ops->tuple_min(grp, Lf, 3, t, NULL) == CANON_COMPLETE);
        for (uint32_t i = 0; i < 3; ++i) {
            CHECK(t[Lf[i]] == i);
        }
    }
    canon_group_release(grp);
}

/* ---- comparison against the explicit backend ---- */

/* A random list of length len over {0..n-1}: distinct entries, or (every third list) with
 * repeats allowed. */
static void random_list(uint32_t *L, uint32_t len, uint32_t n, int repeats)
{
    uint32_t p[MAXN];
    random_perm(p, n);
    for (uint32_t i = 0; i < len; ++i) {
        L[i] = repeats ? (uint32_t)(check_rng() % n) : p[i];
    }
}

/* Compare chain and explicit groups on <gens>; `all` enumerates every element of Sym(n) for
 * membership (n <= 8).  Returns the chain's construction counters in *stats. */
static void compare(uint32_t n, const uint32_t *gens, uint32_t count, int lists_per_len,
                    canon_bsgs_stats *stats)
{
    canon_group *ch = NULL, *ex = NULL;
    CHECK(canon_group_chain_create(n, gens, count, &ch) == CANON_COMPLETE);
    CHECK(canon_group_explicit_create(n, gens, count, 50000, &ex) == CANON_COMPLETE);
    if (ch == NULL || ex == NULL) {
        canon_group_release(ch);
        canon_group_release(ex);
        return;
    }
    const canon_bsgs *c = canon_group_chain_of(ch);
    CHECK(c != NULL && c->verified);
    CHECK(canon_group_chain_of(ex) == NULL);
    /* S3 review item 8: the chain admits every descriptor; the explicit table only up to its
     * order */
    canon_capacity tight = {0, 1, 0, 0, 0, 0, 0};
    CHECK(ch->ops->admits(ch, &tight) == CANON_COMPLETE);
    CHECK(ex->ops->admits(ex, &tight) ==
          (ex->ops->order(ex) <= 1 ? CANON_COMPLETE : CANON_CAPACITY_LIMIT));
    add_stats(stats, &c->stats);
    /* the verifier accepts the chain again from the original inputs */
    {
        canon_bsgs copy;
        CHECK(canon_bsgs_build(&copy, n, gens, count, NULL, 0) == CANON_COMPLETE);
        canon_bsgs_reason why = CANON_BSGS_UNCHECKED;
        CHECK(canon_bsgs_verify(&copy, gens, count, &why) == CANON_COMPLETE);
        CHECK(why == CANON_BSGS_VALID && copy.verified);
        canon_bsgs_free(&copy);
    }
    const uint64_t order = ex->ops->order(ex);
    CHECK(ch->ops->order(ch) == order);

    /* membership of every element of Sym(n) (members and non-members), plus random perms */
    uint32_t p[MAXN], members = 0;
    if (n <= 8) {
        for (uint32_t v = 0; v < n; ++v) {
            p[v] = v;
        }
        for (;;) {
            bool member = in(ex, p);
            CHECK(in(ch, p) == member);
            members += member;
            int i = (int)n - 2;
            while (i >= 0 && p[i] > p[i + 1]) {
                --i;
            }
            if (i < 0) {
                break;
            }
            int j = (int)n - 1;
            while (p[j] < p[i]) {
                --j;
            }
            uint32_t tt = p[i];
            p[i] = p[j];
            p[j] = tt;
            for (int l = i + 1, r = (int)n - 1; l < r; ++l, --r) {
                tt = p[l];
                p[l] = p[r];
                p[r] = tt;
            }
        }
        CHECK(members == order);
    }
    uint32_t nonmembers = 0;
    for (uint32_t k = 0; k < 2000 && nonmembers < 200; ++k) {
        random_perm(p, n);
        bool member = in(ex, p);
        CHECK(in(ch, p) == member);
        nonmembers += !member;
    }

    /* tuple_min: t and the orbit ids of G_M, on random lists of every length 0..n */
    uint32_t L[MAXN], t1[MAXN], t2[MAXN], o1[MAXN], o2[MAXN];
    for (uint32_t len = 0; len <= n; ++len) {
        for (int r = 0; r < lists_per_len; ++r) {
            random_list(L, len, n > 0 ? n : 1, n > 0 && r % 3 == 2);
            CHECK(ex->ops->tuple_min(ex, L, len, t1, o1) == CANON_COMPLETE);
            CHECK(ch->ops->tuple_min(ch, L, len, t2, o2) == CANON_COMPLETE);
            CHECK(eq(t1, t2, n) && eq(o1, o2, n));
            CHECK(ch->ops->tuple_min(ch, L, len, t2, NULL) == CANON_COMPLETE && eq(t1, t2, n));
        }
    }
    if (n > 0) {
        L[0] = n; /* out of range */
        CHECK(ch->ops->tuple_min(ch, L, 1, t2, o2) == CANON_INVALID_INPUT);
    }

    /* rebase to a random prefix: verified, same order, same membership */
    for (int r = 0; r < 2 && n > 0; ++r) {
        uint32_t prefix[MAXN];
        uint32_t plen = 1 + (uint32_t)(check_rng() % n);
        random_list(prefix, plen, n, 0);
        canon_bsgs rb;
        /* with and without verification: the same chain, verified only when asked */
        canon_bsgs quiet;
        CHECK(canon_bsgs_rebase(c, 0, prefix, plen, false, &quiet) == CANON_COMPLETE);
        CHECK(!quiet.verified);
        CHECK(canon_bsgs_rebase(c, 0, prefix, plen, true, &rb) == CANON_COMPLETE);
        CHECK(quiet.order == rb.order && quiet.depth == rb.depth);
        canon_bsgs_free(&quiet);
        CHECK(rb.verified && rb.order == order && rb.depth >= plen);
        for (uint32_t i = 0; i < plen && i < rb.depth; ++i) {
            CHECK(rb.levels[i].base_point == prefix[i]);
        }
        for (uint32_t k = 0; k < 300; ++k) {
            random_perm(p, n);
            CHECK(chain_in(&rb, p) == in(ex, p));
        }
        for (uint32_t k = 0; k < count; ++k) {
            CHECK(chain_in(&rb, gens + (size_t)k * n));
        }
        canon_bsgs_free(&rb);
    }
    canon_group_release(ch);
    canon_group_release(ex);
}

static void t1_tier(void)
{
    canon_bsgs_stats st;
    memset(&st, 0, sizeof st);
    uint32_t groups = 0;
    const uint32_t expect[5] = {1, 1, 2, 6, 30};
    for (uint32_t n = 0; n <= 4; ++n) {
        t1_sym s;
        t1_sym_init(&s, n);
        t1_group g[T1_MAX_GROUPS];
        uint32_t count = t1_subgroups(&s, g);
        CHECK(count == expect[n]);
        for (uint32_t k = 0; k < count; ++k) {
            compare(n, g[k].gens, g[k].gen_count, 6, &st);
            ++groups;
        }
    }
    CHECK(groups == 40);
    print_stats("T1 counters", groups, &st);
}

/* A random generator: a uniform permutation, or one cycle on a random subset (so that
 * intransitive and small groups occur). */
static void random_gen(uint32_t *g, uint32_t n)
{
    if (check_rng() % 2 == 0) {
        random_perm(g, n);
        return;
    }
    uint32_t p[MAXN];
    random_perm(p, n);
    uint32_t len = 2 + (uint32_t)(check_rng() % (n > 2 ? n - 1 : 1));
    len = len > n ? n : len;
    for (uint32_t v = 0; v < n; ++v) {
        g[v] = v;
    }
    for (uint32_t i = 0; i < len; ++i) {
        g[p[i]] = p[(i + 1) % len];
    }
}

static void t2_tier(void)
{
    canon_bsgs_stats st;
    memset(&st, 0, sizeof st);
    check_rng_state = 0x5332ULL * 2654435761ULL;
    for (uint32_t k = 0; k < 200; ++k) {
        uint32_t n = 1 + (uint32_t)(check_rng() % 8);
        uint32_t count = 1 + (uint32_t)(check_rng() % 3);
        uint32_t gens[3 * 8];
        for (uint32_t i = 0; i < count; ++i) {
            random_gen(gens + i * n, n);
        }
        compare(n, gens, count, 1, &st);
    }
    print_stats("T2 counters", 200, &st);
}

/* ---- larger groups ---- */

static void sym12(void)
{
    const uint32_t n = 12;
    uint32_t gens[2 * 12];
    for (uint32_t v = 0; v < n; ++v) {
        gens[v] = v;
        gens[n + v] = (v + 1) % n;
    }
    gens[0] = 1;
    gens[1] = 0;
    canon_group *g = NULL;
    CHECK(canon_group_chain_create(n, gens, 2, &g) == CANON_COMPLETE);
    if (g == NULL) {
        return;
    }
    CHECK(g->ops->order(g) == 479001600u); /* 12! */
    const canon_bsgs *c = canon_group_chain_of(g);
    CHECK(c->verified);
    print_stats("Sym(12) counters", 1, &c->stats);
    /* spec 9.2 point stabilisers: the suffix from level k is the pointwise stabiliser of k
     * base points, Sym(12 - k), whichever points they are */
    uint64_t f = 1;
    for (uint32_t k = c->depth; k-- > 0;) {
        f *= (uint64_t)(n - k);
        CHECK(canon_bsgs_suffix_order(c, k) == f);
    }
    CHECK(c->depth == 11 && canon_bsgs_suffix_order(c, c->depth) == 1);
    /* membership of random products of the generators */
    uint32_t w[12], tmp[12];
    for (uint32_t r = 0; r < 50; ++r) {
        for (uint32_t v = 0; v < n; ++v) {
            w[v] = v;
        }
        uint32_t len = (uint32_t)(check_rng() % 60);
        for (uint32_t i = 0; i < len; ++i) {
            canon_perm_compose(w, gens + (check_rng() % 2) * n, tmp, n);
            memcpy(w, tmp, sizeof w);
        }
        CHECK(in(g, w));
    }
    /* tuple_min of an increasing full list is the identity; of any full list, L^t = (0..11) */
    uint32_t L[12], t[12], orb[12];
    for (uint32_t v = 0; v < n; ++v) {
        L[v] = v;
    }
    CHECK(g->ops->tuple_min(g, L, n, t, orb) == CANON_COMPLETE);
    CHECK(canon_perm_is_identity(t, n));
    for (uint32_t v = 0; v < n; ++v) {
        CHECK(orb[v] == v); /* G_M is trivial */
    }
    for (uint32_t r = 0; r < 3; ++r) {
        random_perm(L, n);
        CHECK(g->ops->tuple_min(g, L, n, t, NULL) == CANON_COMPLETE);
        for (uint32_t i = 0; i < n; ++i) {
            CHECK(t[L[i]] == i);
        }
        /* a partial list: M = (0..k-1), and t is the least such element, so it is
         * increasing on the points outside L */
        uint32_t k = 1 + (uint32_t)(check_rng() % (n - 1));
        CHECK(g->ops->tuple_min(g, L, k, t, orb) == CANON_COMPLETE);
        for (uint32_t i = 0; i < k; ++i) {
            CHECK(t[L[i]] == i && orb[i] == i);
        }
        for (uint32_t v = k; v < n; ++v) {
            CHECK(orb[v] == k); /* one orbit: the rest */
        }
        uint32_t last = 0;
        int first = 1;
        for (uint32_t v = 0; v < n; ++v) {
            int in_l = 0;
            for (uint32_t i = 0; i < k; ++i) {
                in_l |= L[i] == v;
            }
            if (!in_l) {
                CHECK(first || t[v] > last);
                last = t[v];
                first = 0;
            }
        }
    }
    canon_group_release(g);
}

static void cycle64(void)
{
    const uint32_t n = 64;
    uint32_t cyc[64];
    for (uint32_t v = 0; v < n; ++v) {
        cyc[v] = (v + 1) % n;
    }
    canon_group *g = NULL;
    CHECK(canon_group_chain_create(n, cyc, 1, &g) == CANON_COMPLETE);
    if (g == NULL) {
        return;
    }
    CHECK(g->ops->order(g) == 64);
    const canon_bsgs *c = canon_group_chain_of(g);
    CHECK(c->depth == 1 && c->levels[0].orbit_len == 64);
    /* the Schreier path to 63 has 63 edges; t_63 = cyc^63 */
    uint32_t t[64], w[64], tmp[64];
    CHECK(canon_bsgs_transporter(c, 0, 63, t) == CANON_COMPLETE);
    for (uint32_t v = 0; v < n; ++v) {
        CHECK(t[v] == (v + 63) % n);
        w[v] = v;
    }
    for (uint32_t k = 0; k < n; ++k) {
        CHECK(in(g, w));
        canon_perm_compose(w, cyc, tmp, n);
        memcpy(w, tmp, sizeof w);
    }
    w[0] = 1;
    w[1] = 0; /* a transposition (w was the identity again) */
    CHECK(!in(g, w));
    const uint32_t L[1] = {5};
    uint32_t orb[64];
    CHECK(g->ops->tuple_min(g, L, 1, t, orb) == CANON_COMPLETE);
    CHECK(t[5] == 0 && orb[0] == 0 && orb[63] == 63); /* G_(0) is trivial */
    canon_group_release(g);
}

static void capacity_and_api(void)
{
    canon_context *ctx = NULL;
    CHECK(canon_context_create(NULL, &ctx) == CANON_COMPLETE);
    uint32_t gens[2 * 21];
    for (uint32_t n = 20; n <= 21; ++n) {
        for (uint32_t v = 0; v < n; ++v) {
            gens[v] = v;
            gens[n + v] = (v + 1) % n;
        }
        gens[0] = 1;
        gens[1] = 0;
        canon_group *g = (canon_group *)ctx;
        canon_status st = canon_group_create(ctx, n, gens, 2, &g);
        if (n == 20) {
            /* 20! = 2432902008176640000 < 2^64 */
            uint64_t order = 0;
            CHECK(st == CANON_COMPLETE && canon_group_order(g, &order) == CANON_COMPLETE);
            CHECK(order == 2432902008176640000ULL);
        } else {
            /* 21! = 51090942171709440000 > 2^64 - 1: the order does not fit uint64, the
             * count-bit limit of this release (detailed plan 2.1; spec 11.1), so the chain
             * backend refuses the group (multi-limb orders are deferred). */
            CHECK(st == CANON_CAPACITY_LIMIT && g == NULL);
        }
        canon_group_release(g);
    }
    /* canon_group_order and the backend option */
    uint64_t order = 0;
    CHECK(canon_group_order(NULL, &order) == CANON_INVALID_INPUT);
    /* the backend is a creation-time option of an immutable context (S3 review item 1) */
    canon_context *ectx = (canon_context *)&order;
    canon_context_options opt = {(canon_backend)7};
    CHECK(canon_context_create_with_options(NULL, &opt, &ectx) == CANON_INVALID_INPUT);
    CHECK(ectx == NULL);
    CHECK(canon_context_create_with_options(NULL, &opt, NULL) == CANON_INVALID_INPUT);
    opt.backend = CANON_BACKEND_EXPLICIT;
    CHECK(canon_context_create_with_options(NULL, &opt, &ectx) == CANON_COMPLETE);
    const uint32_t s3[6] = {1, 0, 2, 1, 2, 0};
    canon_group *a = NULL, *b = NULL;
    CHECK(canon_group_create(ctx, 3, s3, 2, &a) == CANON_COMPLETE);
    CHECK(canon_group_chain_of(a) != NULL);
    CHECK(canon_group_order(a, NULL) == CANON_INVALID_INPUT);
    CHECK(canon_group_create(ectx, 3, s3, 2, &b) == CANON_COMPLETE);
    CHECK(canon_group_chain_of(b) == NULL); /* the explicit backend */
    canon_context_release(ectx);
    /* NULL options are the defaults: the chain */
    CHECK(canon_context_create_with_options(NULL, NULL, &ectx) == CANON_COMPLETE);
    canon_group *d = NULL;
    CHECK(canon_group_create(ectx, 3, s3, 2, &d) == CANON_COMPLETE);
    CHECK(canon_group_chain_of(d) != NULL);
    canon_group_release(d);
    canon_context_release(ectx);
    uint64_t oa = 0, ob = 0;
    CHECK(canon_group_order(a, &oa) == CANON_COMPLETE && canon_group_order(b, &ob) ==
                                                            CANON_COMPLETE);
    CHECK(oa == 6 && ob == 6);
    canon_group_release(a);
    canon_group_release(b);
    /* invalid generators and degree 0 */
    const uint32_t bad[2] = {1, 1};
    CHECK(canon_group_create(ctx, 2, bad, 1, &a) == CANON_INVALID_INPUT && a == NULL);
    CHECK(canon_group_create(ctx, 0, NULL, 3, &a) == CANON_COMPLETE);
    CHECK(canon_group_order(a, &oa) == CANON_COMPLETE && oa == 1);
    uint32_t dummy = 0;
    CHECK(a->ops->tuple_min(a, NULL, 0, &dummy, &dummy) == CANON_COMPLETE);
    CHECK(in(a, NULL));
    canon_group_release(a);
    canon_context_release(ctx);
}

/* Slice S5 review item 6: the conjugate of a verified chain by g, obtained by relabelling
 * points, passes the independent verifier against the conjugated inputs, has the same order,
 * and its members are exactly the conjugates g^-1 h g (spec 2.1). */
static void conjugation(void)
{
    uint32_t checked = 0;
    for (uint32_t n = 0; n <= 4; ++n) {
        t1_sym s;
        t1_sym_init(&s, n);
        t1_group g[T1_MAX_GROUPS];
        const uint32_t count = t1_subgroups(&s, g);
        for (uint32_t gi = 0; gi < count; ++gi) {
            canon_bsgs c, k;
            CHECK(canon_bsgs_build_verified(&c, n, g[gi].gens, g[gi].gen_count) == CANON_COMPLETE);
            for (uint32_t e = 0; e < s.count; ++e) {
                const uint32_t *x = s.elem[e]; /* the conjugator, any element of Sym(n) */
                CHECK(canon_bsgs_conjugate(&c, x, &k) == CANON_COMPLETE);
                CHECK(k.verified && k.order == c.order && k.depth == c.depth);
                canon_bsgs_reason why = CANON_BSGS_UNCHECKED;
                CHECK(canon_bsgs_verify(&k, k.inputs.data, k.inputs.count, &why) ==
                          CANON_COMPLETE &&
                      why == CANON_BSGS_VALID);
                for (uint32_t h = 0; h < s.count; ++h) {
                    uint32_t q[4];
                    for (uint32_t v = 0; v < n; ++v) {
                        q[x[v]] = x[s.elem[h][v]]; /* x^-1 h x */
                    }
                    bool member = false;
                    CHECK(canon_bsgs_contains(&k, q, &member) == CANON_COMPLETE);
                    CHECK(member == ((g[gi].mask >> h & 1u) != 0));
                }
                canon_bsgs_free(&k);
                ++checked;
            }
            canon_bsgs_free(&c);
        }
    }
    CHECK(checked == 1 + 1 + 2 * 2 + 6 * 6 + 30 * 24);
}

int main(void)
{
    conjugation();
    conventions();
    t1_tier();
    t2_tier();
    sym12();
    cycle64();
    capacity_and_api();
    return check_finish("test_chain");
}
