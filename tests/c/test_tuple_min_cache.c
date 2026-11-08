/* K2 (docs/review-2026-11/fix-K2-tuplemin.md): canon_bsgs_tuple_min_ws with workspace
 * scratch and the spec 16 stabiliser cache / G-stage memo returns exactly the old
 * canon_bsgs_tuple_min result (least minimiser t and G_M orbit ids), and, where the explicit
 * backend admits the group, exactly the explicit backend's.
 *
 *  - random generator sets of degree <= 8 against both oracles, and of degree 9..12 against
 *    the old entry point (the explicit backend too when its order limit admits the group);
 *  - lists of every shape the callers produce: empty, singleton prefixes of a random
 *    partition order (the G stage's F, spec 7.1), full lists in random order (the leaf map,
 *    spec 7.2), and lists with repeated entries;
 *  - one shared cache across every list of a group, then across two alternating groups
 *    (the spec 16 key includes G: the fingerprint must reset, never alias);
 *  - a repeated (G, F) call is a memo hit, and the trivial group / degree 0 fast paths. */
#include <string.h>

#include "bsgs/chain.h"
#include "bsgs/chain_backend.h"
#include "bsgs/explicit.h"
#include "check.h"

#define MAXN 12u
#define MAXGENS 4u

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

/* A random list: distinct entries (a shuffled prefix) or, with repeats, entries drawn with
 * replacement - the two shapes of src/bsgs/group.h's contract tests in test_chain.c. */
static void random_list(uint32_t *L, uint32_t len, uint32_t n, int repeats)
{
    if (repeats) {
        for (uint32_t i = 0; i < len; ++i) {
            L[i] = n > 0 ? (uint32_t)(check_rng() % n) : 0;
        }
    } else {
        uint32_t p[MAXN];
        random_perm(p, n);
        for (uint32_t i = 0; i < len && i < n; ++i) {
            L[i] = p[i];
        }
    }
}

typedef struct fixtures {
    canon_bsgs_tuple_cache cache;
    canon_bsgs_tuple_scratch scratch;
    uint32_t work[MAXN], queue[MAXN], mark[MAXN];
} fixtures;

static void fixtures_init(fixtures *f)
{
    canon_bsgs_tuple_cache_init(&f->cache);
    f->scratch.work = f->work;
    f->scratch.queue = f->queue;
    f->scratch.mark = f->mark;
}

/* ws (scratch + cache) == old entry point == explicit (when ex is not NULL), t and orbits. */
static void compare_one(const canon_bsgs *c, const canon_group *ex, const uint32_t *L, uint32_t len,
                        uint32_t n, fixtures *f)
{
    uint32_t told[MAXN], oold[MAXN], tws[MAXN], ows[MAXN], tex[MAXN], oex[MAXN];
    CHECK(canon_bsgs_tuple_min(c, L, len, told, oold, NULL) == CANON_COMPLETE);
    CHECK(canon_bsgs_tuple_min_ws(c, L, len, tws, ows, NULL, &f->scratch, &f->cache) ==
          CANON_COMPLETE);
    CHECK(eq(told, tws, n) && eq(oold, ows, n));
    CHECK(canon_bsgs_tuple_min_ws(c, L, len, tws, NULL, NULL, &f->scratch, &f->cache) ==
          CANON_COMPLETE);
    CHECK(eq(told, tws, n));
    if (ex != NULL) {
        CHECK(ex->ops->tuple_min(ex, L, len, tex, oex) == CANON_COMPLETE);
        CHECK(eq(told, tex, n) && eq(oold, oex, n));
    }
}

/* Every list shape for one group, all through the one shared cache (interleaving is what
 * exercises prefix hits, eviction and the memo). */
static void compare_group(const canon_bsgs *c, const canon_group *ex, uint32_t n, fixtures *f)
{
    uint32_t L[MAXN] = {0};
    compare_one(c, ex, L, 0, n, f); /* empty F: the least element and the orbits of G */
    for (uint32_t len = 0; len <= n; ++len) {
        for (int r = 0; r < 3; ++r) {
            random_list(L, len, n, r == 2);
            compare_one(c, ex, L, len, n, f);
        }
    }
    /* G-stage shape: prefixes of one partition order, in sweep order, then repeated */
    uint32_t order[MAXN];
    random_perm(order, n);
    for (uint32_t len = 0; len <= n; ++len) {
        compare_one(c, ex, order, len, n, f);
    }
    for (uint32_t len = n;; --len) {
        compare_one(c, ex, order, len, n, f);
        if (len == 0) {
            break;
        }
    }
}

static void random_tier(uint32_t max_n, int groups, int explicit_always)
{
    fixtures f;
    fixtures_init(&f);
    for (int gi = 0; gi < groups; ++gi) {
        const uint32_t n = max_n == 0 ? 0 : (uint32_t)(check_rng() % (max_n + 1u));
        const size_t count = 1u + (size_t)(check_rng() % MAXGENS);
        uint32_t gens[MAXGENS * MAXN];
        for (size_t k = 0; k < count; ++k) {
            random_perm(gens + k * n, n);
        }
        canon_group *ch = NULL, *ex = NULL;
        CHECK(canon_group_chain_create(n, gens, count, &ch) == CANON_COMPLETE);
        canon_status st = canon_group_explicit_create(n, gens, count, 300000u, &ex);
        if (explicit_always) {
            CHECK(st == CANON_COMPLETE); /* n <= 8: |G| <= 40320 always admitted */
        } else if (st != CANON_COMPLETE) {
            CHECK(st == CANON_CAPACITY_LIMIT);
            ex = NULL;
        }
        compare_group(canon_group_chain_of(ch), ex, n, &f);
        canon_group_release(ch);
        if (ex != NULL) {
            canon_group_release(ex);
        }
    }
    canon_bsgs_tuple_cache_free(&f.cache);
}

/* The spec 16 key includes G: alternating two groups through one cache must never serve a
 * stale entry of the other group (the fingerprint reset in tuple_cache_prepare). */
static void alternating_groups(void)
{
    const uint32_t n = 7;
    uint32_t ga[MAXGENS * MAXN], gb[MAXGENS * MAXN];
    for (size_t k = 0; k < 3; ++k) {
        random_perm(ga + k * n, n);
        random_perm(gb + k * n, n);
    }
    canon_group *a = NULL, *b = NULL, *ea = NULL, *eb = NULL;
    CHECK(canon_group_chain_create(n, ga, 3, &a) == CANON_COMPLETE);
    CHECK(canon_group_chain_create(n, gb, 3, &b) == CANON_COMPLETE);
    CHECK(canon_group_explicit_create(n, ga, 3, 50000u, &ea) == CANON_COMPLETE);
    CHECK(canon_group_explicit_create(n, gb, 3, 50000u, &eb) == CANON_COMPLETE);
    fixtures f;
    fixtures_init(&f);
    uint32_t L[MAXN];
    random_perm(L, n);
    for (int round = 0; round < 4; ++round) {
        for (uint32_t len = 0; len <= n; ++len) {
            compare_one(canon_group_chain_of(a), ea, L, len, n, &f);
            compare_one(canon_group_chain_of(b), eb, L, len, n, &f);
        }
    }
    canon_bsgs_tuple_cache_free(&f.cache);
    canon_group_release(a);
    canon_group_release(b);
    canon_group_release(ea);
    canon_group_release(eb);
}

/* Structured degree-12 groups, the fast paths, the memo-hit counter and reset. */
static void structured_and_fast_paths(void)
{
    fixtures f;
    fixtures_init(&f);
    const uint32_t n = 12;
    uint32_t gens[4 * MAXN], L[MAXN], t[MAXN], o[MAXN];

    /* Trivial group: t = id, every orbit id its own point (fast path), also through ws. */
    for (uint32_t v = 0; v < n; ++v) {
        gens[v] = v;
    }
    canon_group *g = NULL;
    CHECK(canon_group_chain_create(n, gens, 1, &g) == CANON_COMPLETE);
    random_perm(L, n);
    CHECK(canon_bsgs_tuple_min_ws(canon_group_chain_of(g), L, n, t, o, NULL, &f.scratch,
                                  &f.cache) == CANON_COMPLETE);
    for (uint32_t v = 0; v < n; ++v) {
        CHECK(t[v] == v && o[v] == v);
    }
    compare_group(canon_group_chain_of(g), NULL, n, &f);
    canon_group_release(g);

    /* A group fixing most points (Sym(4) on {0..3} at degree 12): fixed-point steps. */
    for (uint32_t i = 0; i < n; ++i) {
        gens[i] = i == 0 ? 1 : (i == 1 ? 0 : i);
        gens[n + i] = i < 4 ? (i + 1) % 4 : i;
    }
    CHECK(canon_group_chain_create(n, gens, 2, &g) == CANON_COMPLETE);
    canon_group *ex = NULL;
    CHECK(canon_group_explicit_create(n, gens, 2, 50000u, &ex) == CANON_COMPLETE);
    compare_group(canon_group_chain_of(g), ex, n, &f);

    /* A repeated (G, F) with orbits is served by the memo on the second call. */
    const uint64_t hits0 = f.cache.memo_hits;
    random_perm(L, n);
    CHECK(canon_bsgs_tuple_min_ws(canon_group_chain_of(g), L, 3, t, o, NULL, &f.scratch,
                                  &f.cache) == CANON_COMPLETE);
    const uint64_t hits1 = f.cache.memo_hits;
    CHECK(canon_bsgs_tuple_min_ws(canon_group_chain_of(g), L, 3, t, o, NULL, &f.scratch,
                                  &f.cache) == CANON_COMPLETE);
    CHECK(f.cache.memo_hits == hits1 + 1 && hits1 >= hits0);
    canon_bsgs_tuple_cache_reset(&f.cache);
    CHECK(f.cache.count == 0 && f.cache.memo_count == 0);
    compare_one(canon_group_chain_of(g), ex, L, 5, n, &f); /* still exact after a reset */
    canon_group_release(g);
    canon_group_release(ex);

    /* Sym(12) from a transposition and a 12-cycle (old entry point as the oracle), and
     * the cyclic group C12 (depth 1: its suffixes are trivial fast). */
    for (uint32_t i = 0; i < n; ++i) {
        gens[i] = i == 0 ? 1 : (i == 1 ? 0 : i);
        gens[n + i] = (i + 1) % n;
    }
    CHECK(canon_group_chain_create(n, gens, 2, &g) == CANON_COMPLETE);
    compare_group(canon_group_chain_of(g), NULL, n, &f);
    canon_group_release(g);
    CHECK(canon_group_chain_create(n, gens + n, 1, &g) == CANON_COMPLETE);
    CHECK(canon_group_explicit_create(n, gens + n, 1, 50000u, &ex) == CANON_COMPLETE);
    compare_group(canon_group_chain_of(g), ex, n, &f);

    /* Out-of-range entry through ws, as through the old entry point. */
    L[0] = n;
    CHECK(canon_bsgs_tuple_min_ws(canon_group_chain_of(g), L, 1, t, o, NULL, &f.scratch,
                                  &f.cache) == CANON_INVALID_INPUT);
    canon_group_release(g);
    canon_group_release(ex);

    /* Degree 0 through ws (spec 7.2 empty list): complete, outputs untouched. */
    uint32_t dummy = 77;
    CHECK(canon_group_chain_create(0, gens, 1, &g) == CANON_COMPLETE);
    CHECK(canon_bsgs_tuple_min_ws(canon_group_chain_of(g), NULL, 0, &dummy, &dummy, NULL,
                                  &f.scratch, &f.cache) == CANON_COMPLETE);
    CHECK(dummy == 77);
    canon_group_release(g);
    canon_bsgs_tuple_cache_free(&f.cache);
}

int main(void)
{
    random_tier(8, 120, 1);
    random_tier(12, 40, 0);
    alternating_groups();
    structured_and_fast_paths();
    return check_finish("test_tuple_min_cache");
}
