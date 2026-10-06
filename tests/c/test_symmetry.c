/* Slice S7 step 1 (docs/slices/S7.md 3.1, 3.2, 4): the verified automorphism subgroup A_known
 * and its prefix stabilisers (src/symmetry/symmetry.h), against brute force on the T1 tier.
 *
 *  1. A_known from the input generators (source i) is exactly the closure of the generators
 *     that fix x, for every T1 group (n <= 4), both generating sets (greedy, all elements),
 *     every subset and both backends; every recorded generator is in G and fixes x.
 *  2. The insertion discipline (spec 7.3 "verify ... by exact membership and object
 *     equality", spec 14.3 R2): non-bijections, non-members of G and elements moving x are
 *     refused and leave A_known unchanged; identities and members are not inserted; each
 *     insertion at least doubles |A_known| and the chain verifies.
 *  3. Prefix-stabiliser orbits on every injective prefix against brute force (the incremental
 *     stack, and a non-incremental rebase to the whole prefix), the numerically least
 *     representative, and the soundness guard: every generator of H_d fixes the prefix, fixes
 *     x and lies in G.
 *  4. Larger cases: Sym(6) and C_2^3 on the empty subset, a directed 3-cycle, a nested tuple.
 *  5. Degenerate cases: no fixing generator (no rebase at all), n = 0, n = 1, a group without
 *     recorded generators (a conjugate).
 *  6. Counters: rebases = descends below a nontrivial H; insertions <= log2 |A_known|.
 *
 * Convention (spec 3): p[v] = v^p, (pq)[v] = q[p[v]]; a subset acts as x^p = {p[a] : a in x}.
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "bsgs/chain.h"
#include "bsgs/group.h"
#include "bsgs/verify.h"
#include "canon/canon.h"
#include "check.h"
#include "object/object.h"
#include "symmetry/symmetry.h"
#include "t1_groups.h"

static canon_context *CTX[2]; /* chain, explicit */

static canon_group *make_group(int backend, uint32_t n, const uint32_t *gens, uint32_t count)
{
    canon_group *g = NULL;
    CHECK(canon_group_create(CTX[backend], n, gens, count, &g) == CANON_COMPLETE);
    return g;
}

/* x = the subset given by the bit mask over {0..n-1} */
static void subset_root(canon_root *x, uint32_t n, uint32_t mask)
{
    uint32_t atoms[8];
    uint32_t k = 0;
    for (uint32_t v = 0; v < n; ++v) {
        if (mask >> v & 1u) {
            atoms[k++] = v;
        }
    }
    memset(x, 0, sizeof *x);
    x->kind = CANON_ROOT_SUBSET;
    x->n = n;
    CHECK(canon_subset_init(&x->u.subset, n, atoms, k) == CANON_COMPLETE);
}

/* spec 2.1: x^p = {p[a] : a in x} on masks */
static uint32_t act_mask(const uint32_t *p, uint32_t n, uint32_t mask)
{
    uint32_t out = 0;
    for (uint32_t v = 0; v < n; ++v) {
        if (mask >> v & 1u) {
            out |= 1u << p[v];
        }
    }
    return out;
}

static uint32_t sym_index(const t1_sym *s, const uint32_t *p)
{
    for (uint32_t e = 0; e < s->count; ++e) {
        if (s->n == 0 || memcmp(s->elem[e], p, s->n * sizeof *p) == 0) {
            return e;
        }
    }
    return UINT32_MAX;
}

/* ---- 1 and 3: the T1 tier ---- */

typedef struct tier_ctx {
    const t1_sym *sym;
    canon_symmetry *a;
    const canon_group *g;
    uint32_t xmask;
    uint32_t amask; /* brute-force A_known as a mask over Sym(n) */
    uint32_t prefix[4];
    uint64_t descends_nontrivial, orbit_checks;
} tier_ctx;

/* The brute-force pointwise stabiliser of prefix[0..d) in A_known. */
static uint32_t brute_h(const tier_ctx *t, uint32_t d)
{
    uint32_t h = 0;
    for (uint32_t e = 0; e < t->sym->count; ++e) {
        bool fixes = (t->amask >> e & 1u) != 0;
        for (uint32_t i = 0; i < d && fixes; ++i) {
            fixes = t->sym->elem[e][t->prefix[i]] == t->prefix[i];
        }
        if (fixes) {
            h |= 1u << e;
        }
    }
    return h;
}

/* Brute-force orbit of v under the elements in hmask, as a point mask. */
static uint32_t brute_orbit(const t1_sym *s, uint32_t hmask, uint32_t v)
{
    uint32_t o = 0;
    for (uint32_t e = 0; e < s->count; ++e) {
        if (hmask >> e & 1u) {
            o |= 1u << s->elem[e][v];
        }
    }
    return o;
}

static void check_prefix(tier_ctx *t, uint32_t d)
{
    const uint32_t n = t->sym->n;
    canon_symmetry *a = t->a;
    const uint32_t h = brute_h(t, d);
    CHECK(canon_symmetry_trivial_at(a, d) == (h == 1u));
    /* the soundness guard: every generator of H_d fixes the prefix, fixes x, is in G */
    const canon_bsgs *view = NULL;
    uint32_t level = 0;
    canon_symmetry_view(a, d, &view, &level);
    if (!canon_symmetry_trivial_at(a, d)) {
        const canon_bsgs_level *L = &view->levels[level];
        for (uint32_t k = 0; k < L->gen_count; ++k) {
            const uint32_t *s = canon_perm_table_row(&view->gens, L->gen_ids[k]);
            for (uint32_t i = 0; i < d; ++i) {
                CHECK(s[t->prefix[i]] == t->prefix[i]);
            }
            CHECK(act_mask(s, n, t->xmask) == t->xmask);
            bool in = false;
            CHECK(t->g->ops->contains(t->g, s, &in) == CANON_COMPLETE && in);
            const uint32_t e = sym_index(t->sym, s);
            CHECK(e != UINT32_MAX && (h >> e & 1u));
        }
    }
    /* members: the atoms off the prefix, in decreasing order (cell order is not semantic,
     * spec 7.1: the representative must not depend on it) */
    uint32_t used = 0;
    for (uint32_t i = 0; i < d; ++i) {
        used |= 1u << t->prefix[i];
    }
    uint32_t members[4], reps[4], len = 0, count = 99;
    for (uint32_t v = n; v-- > 0;) {
        if (!(used >> v & 1u)) {
            members[len++] = v;
        }
    }
    CHECK(canon_symmetry_orbit_reps(a, d, members, len, reps, &count) == CANON_COMPLETE);
    uint32_t k = 0;
    for (uint32_t i = 0; i < len; ++i) {
        const uint32_t o = brute_orbit(t->sym, h, members[i]);
        CHECK((o & used) == 0); /* H_d preserves the complement of the prefix */
        if ((o & ((1u << members[i]) - 1u)) == 0) {
            /* members[i] is the least point of its orbit: expected, in member order */
            CHECK(k < count && reps[k] == members[i]);
            ++k;
        }
    }
    CHECK(k == count);
    t->orbit_checks += 1;
    /* the non-incremental oracle: rebase A_known to the whole prefix at once (S3, spec 9.2);
     * levels[d] then generates the pointwise stabiliser of the prefix */
    canon_bsgs r;
    CHECK(canon_bsgs_rebase(&a->chain, 0, t->prefix, d, false, &r) == CANON_COMPLETE);
    uint32_t ids[4];
    canon_bsgs_orbit_ids(&r, d, ids);
    for (uint32_t v = 0; v < n; ++v) {
        const uint32_t o = brute_orbit(t->sym, h, v);
        for (uint32_t w = 0; w < n; ++w) {
            CHECK((ids[v] == ids[w]) == ((o >> w & 1u) != 0));
        }
    }
    canon_bsgs_free(&r);
    if (d + 1 >= n) {
        return;
    }
    /* children: descend along each member (siblings overwrite slot d + 1) */
    for (uint32_t i = 0; i < len; ++i) {
        const uint64_t before = a->stats.rebases;
        const bool parent_trivial = canon_symmetry_trivial_at(a, d);
        CHECK(canon_symmetry_descend(a, d, members[i]) == CANON_COMPLETE);
        CHECK(a->stats.rebases == before + (parent_trivial ? 0u : 1u));
        t->descends_nontrivial += parent_trivial ? 0u : 1u;
        t->prefix[d] = members[i];
        check_prefix(t, d + 1);
    }
}

static void test_tier(void)
{
    uint64_t cases = 0, inserted = 0, nontrivial = 0, descends = 0, orbit_checks = 0;
    canon_root_image img;
    canon_root_image_init(&img);
    canon_symmetry a;
    canon_symmetry_init(&a);
    for (uint32_t n = 1; n <= 4; ++n) {
        t1_sym sym;
        t1_sym_init(&sym, n);
        t1_group groups[T1_MAX_GROUPS];
        const uint32_t gcount = t1_subgroups(&sym, groups);
        for (uint32_t gi = 0; gi < gcount; ++gi) {
            for (int full = 0; full < 2; ++full) {
                /* the generating set: greedy, or every element in index order (identity
                 * first, so identities are offered too) */
                uint32_t gens[24 * 4], count = 0;
                if (full) {
                    for (uint32_t e = 0; e < sym.count; ++e) {
                        if (groups[gi].mask >> e & 1u) {
                            memcpy(gens + count++ * n, sym.elem[e], n * sizeof *gens);
                        }
                    }
                } else {
                    count = groups[gi].gen_count;
                    memcpy(gens, groups[gi].gens, count * n * sizeof *gens);
                }
                for (int backend = 0; backend < 2; ++backend) {
                    canon_group *g = make_group(backend, n, gens, count);
                    const canon_perm_table *in = canon_group_input_generators(g);
                    CHECK(in != NULL && in->count == count &&
                          (count == 0 || memcmp(in->data, gens, count * n * sizeof *gens) == 0));
                    for (uint32_t mask = 0; mask < 1u << n; ++mask) {
                        canon_root x;
                        subset_root(&x, n, mask);
                        /* brute force: the closure of the generators fixing x */
                        uint32_t fix = 0;
                        for (uint32_t i = 0; i < count; ++i) {
                            if (act_mask(gens + i * n, n, mask) == mask) {
                                fix |= 1u << sym_index(&sym, gens + i * n);
                            }
                        }
                        const uint32_t amask = t1_closure(&sym, fix);
                        CHECK(canon_symmetry_reset(&a, n) == CANON_COMPLETE);
                        CHECK(canon_symmetry_from_inputs(&a, g, &x, &img) == CANON_COMPLETE);
                        CHECK(a.chain.order == t1_popcount(amask));
                        for (uint32_t e = 0; e < sym.count; ++e) {
                            bool in_a = false;
                            CHECK(canon_bsgs_contains(&a.chain, sym.elem[e], &in_a) ==
                                  CANON_COMPLETE);
                            CHECK(in_a == ((amask >> e & 1u) != 0));
                        }
                        /* A_known <= Aut_G(x): every inserted automorphism is in G, fixes x */
                        for (uint32_t i = 0; i < a.gens.count; ++i) {
                            const uint32_t *p = canon_perm_table_row(&a.gens, i);
                            bool in_g = false;
                            CHECK(g->ops->contains(g, p, &in_g) == CANON_COMPLETE && in_g);
                            CHECK(act_mask(p, n, mask) == mask);
                        }
                        CHECK(a.stats.considered == count && a.stats.rejected == 0);
                        CHECK(a.stats.inserted == a.gens.count);
                        /* each insertion at least doubles |A_known| */
                        CHECK(((uint64_t)1 << a.stats.inserted) <= a.chain.order);
                        CHECK(a.chain.verified);
                        inserted += a.stats.inserted;
                        nontrivial += a.chain.order > 1u;
                        tier_ctx t = {&sym, &a, g, mask, amask, {0, 0, 0, 0}, 0, 0};
                        const uint64_t rebases0 = a.stats.rebases;
                        check_prefix(&t, 0);
                        CHECK(a.stats.rebases - rebases0 == t.descends_nontrivial);
                        descends += t.descends_nontrivial;
                        orbit_checks += t.orbit_checks;
                        canon_root_free(&x);
                        cases += 1;
                    }
                    canon_group_release(g);
                }
            }
        }
    }
    canon_symmetry_free(&a);
    canon_root_image_free(&img);
    printf("T1 A_known: %llu cases (both generating sets and backends), %llu with A_known > 1, "
           "%llu insertions, %llu rebases, %llu prefix orbit checks\n",
           (unsigned long long)cases, (unsigned long long)nontrivial,
           (unsigned long long)inserted, (unsigned long long)descends,
           (unsigned long long)orbit_checks);
}

/* ---- 2: the insertion discipline ---- */

static void test_insertion(void)
{
    canon_root_image img;
    canon_root_image_init(&img);
    canon_symmetry a;
    canon_symmetry_init(&a);
    for (int backend = 0; backend < 2; ++backend) {
        /* G = Sym(4) = <(0 1), (0 1 2 3)>, x = {0}: A = Sym({1,2,3}) */
        const uint32_t sym4[2][4] = {{1, 0, 2, 3}, {1, 2, 3, 0}};
        canon_group *g = make_group(backend, 4, &sym4[0][0], 2);
        canon_root x;
        subset_root(&x, 4, 1u);
        CHECK(canon_symmetry_reset(&a, 4) == CANON_COMPLETE);
        CHECK(canon_symmetry_trivial_at(&a, 0));
        bool ins = true;
        /* not a bijection */
        const uint32_t bad[4] = {0, 0, 2, 3};
        CHECK(canon_symmetry_insert(&a, g, &x, &img, bad, &ins) == CANON_INVALID_INPUT && !ins);
        /* in G but x^p != x: (0 1) sends {0} to {1} */
        CHECK(canon_symmetry_insert(&a, g, &x, &img, sym4[0], &ins) == CANON_INVALID_INPUT &&
              !ins);
        CHECK(a.stats.rejected == 2 && a.chain.order == 1 && a.gens.count == 0);
        /* the identity fixes x and is in G, but is not inserted */
        const uint32_t id[4] = {0, 1, 2, 3};
        CHECK(canon_symmetry_insert(&a, g, &x, &img, id, &ins) == CANON_COMPLETE && !ins);
        CHECK(a.chain.order == 1 && canon_symmetry_trivial_at(&a, 0));
        /* (1 2) is an automorphism: inserted, |A_known| 1 -> 2 */
        const uint32_t t12[4] = {0, 2, 1, 3};
        CHECK(canon_symmetry_insert(&a, g, &x, &img, t12, &ins) == CANON_COMPLETE && ins);
        CHECK(a.chain.order == 2 && !canon_symmetry_trivial_at(&a, 0));
        /* (1 2 3): |A_known| 2 -> 6 */
        const uint32_t c123[4] = {0, 2, 3, 1};
        CHECK(canon_symmetry_insert(&a, g, &x, &img, c123, &ins) == CANON_COMPLETE && ins);
        CHECK(a.chain.order == 6 && a.stats.inserted == 2 && a.gens.count == 2);
        /* (2 3) is now a member: not inserted; so is the product (1 2)(1 2 3), computed with
         * the convention (pq)[v] = q[p[v]] (spec 3): it sends 1 -> 2 -> 3, 2 -> 1 -> 2,
         * 3 -> 3 -> 1, i.e. [0, 3, 2, 1] = (1 3) */
        const uint32_t t23[4] = {0, 1, 3, 2};
        CHECK(canon_symmetry_insert(&a, g, &x, &img, t23, &ins) == CANON_COMPLETE && !ins);
        uint32_t pq[4];
        for (uint32_t v = 0; v < 4; ++v) {
            pq[v] = c123[t12[v]];
        }
        CHECK(pq[0] == 0 && pq[1] == 3 && pq[2] == 2 && pq[3] == 1);
        CHECK(canon_symmetry_insert(&a, g, &x, &img, pq, &ins) == CANON_COMPLETE && !ins);
        CHECK(a.chain.order == 6 && a.gens.count == 2);
        /* the chain verifies against its generators (spec 9.1) */
        canon_bsgs_reason reason = CANON_BSGS_UNCHECKED;
        CHECK(canon_bsgs_verify(&a.chain, a.gens.data, a.gens.count, &reason) ==
                  CANON_COMPLETE &&
              reason == CANON_BSGS_VALID);
        CHECK(a.stats.considered == 7 && a.stats.rejected == 2);
        canon_group_release(g);
        /* not in G although it fixes x: G = C_3 = <(1 2 3)>, p = (2 3) */
        g = make_group(backend, 4, c123, 1);
        CHECK(canon_symmetry_reset(&a, 4) == CANON_COMPLETE);
        CHECK(canon_symmetry_insert(&a, g, &x, &img, t23, &ins) == CANON_INVALID_INPUT && !ins);
        CHECK(a.stats.rejected == 1 && a.chain.order == 1);
        /* the action convention (spec 2.1): [1,2,0,3] sends {0} to {1}, not {2} */
        const uint32_t c012[4] = {1, 2, 0, 3};
        CHECK(canon_root_act_into(&x, c012, &img) == CANON_COMPLETE);
        CHECK(img.root.u.subset.k == 1 && img.root.u.subset.atoms[0] == 1);
        canon_root_image_clear(&img);
        canon_group_release(g);
        canon_root_free(&x);
    }
    canon_symmetry_free(&a);
    canon_root_image_free(&img);
}

/* ---- 4: larger cases ---- */

/* Every atom off the path is one orbit of H_d for the empty subset under Sym(6): one
 * representative at every depth, and A_known = Sym(6). */
static void test_sym6(void)
{
    canon_root_image img;
    canon_root_image_init(&img);
    canon_symmetry a;
    canon_symmetry_init(&a);
    const uint32_t gens[2][6] = {{1, 0, 2, 3, 4, 5}, {1, 2, 3, 4, 5, 0}};
    for (int backend = 0; backend < 2; ++backend) {
        canon_group *g = make_group(backend, 6, &gens[0][0], 2);
        canon_root x;
        subset_root(&x, 6, 0);
        CHECK(canon_symmetry_reset(&a, 6) == CANON_COMPLETE);
        CHECK(canon_symmetry_from_inputs(&a, g, &x, &img) == CANON_COMPLETE);
        CHECK(a.chain.order == 720 && a.stats.inserted <= 9); /* 2^9 <= 720 < 2^10 */
        /* path 5, 3, 1, 0, 2 (any path; the representatives are the least free atoms) */
        const uint32_t path[5] = {5, 3, 1, 0, 2};
        uint32_t used = 0;
        for (uint32_t d = 0; d < 5; ++d) {
            uint32_t members[6], reps[6], len = 0, count = 0;
            for (uint32_t v = 0; v < 6; ++v) {
                if (!(used >> v & 1u)) {
                    members[len++] = v;
                }
            }
            CHECK(!canon_symmetry_trivial_at(&a, d));
            CHECK(canon_symmetry_orbit_reps(&a, d, members, len, reps, &count) ==
                  CANON_COMPLETE);
            CHECK(count == 1 && reps[0] == members[0]);
            CHECK(canon_symmetry_descend(&a, d, path[d]) == CANON_COMPLETE);
            used |= 1u << path[d];
        }
        /* after five points Sym(6) has nothing left */
        CHECK(canon_symmetry_trivial_at(&a, 5) && a.stats.rebases == 5);
        canon_root_free(&x);
        canon_group_release(g);
    }
    /* C_2^3 = <(0 1), (2 3), (4 5)>: orbits {0,1}, {2,3}, {4,5} */
    const uint32_t blocks[3][6] = {{1, 0, 2, 3, 4, 5}, {0, 1, 3, 2, 4, 5}, {0, 1, 2, 3, 5, 4}};
    canon_group *g = make_group(0, 6, &blocks[0][0], 3);
    canon_root x;
    subset_root(&x, 6, 0);
    CHECK(canon_symmetry_reset(&a, 6) == CANON_COMPLETE);
    CHECK(canon_symmetry_from_inputs(&a, g, &x, &img) == CANON_COMPLETE);
    CHECK(a.chain.order == 8 && a.stats.inserted == 3);
    const uint32_t all[6] = {5, 4, 3, 2, 1, 0};
    uint32_t reps[6], count = 0;
    CHECK(canon_symmetry_orbit_reps(&a, 0, all, 6, reps, &count) == CANON_COMPLETE);
    CHECK(count == 3 && reps[0] == 4 && reps[1] == 2 && reps[2] == 0);
    CHECK(canon_symmetry_descend(&a, 0, 1) == CANON_COMPLETE);
    const uint32_t rest[5] = {0, 2, 3, 4, 5};
    CHECK(canon_symmetry_orbit_reps(&a, 1, rest, 5, reps, &count) == CANON_COMPLETE);
    CHECK(count == 3 && reps[0] == 0 && reps[1] == 2 && reps[2] == 4);
    /* the guard: a "cell" that H_1 does not preserve ({2} alone) is an internal error */
    const uint32_t broken[1] = {2};
    CHECK(canon_symmetry_orbit_reps(&a, 1, broken, 1, reps, &count) == CANON_INTERNAL_ERROR);
    canon_root_free(&x);
    canon_group_release(g);
    canon_symmetry_free(&a);
    canon_root_image_free(&img);
}

/* A directed 3-cycle 0 -> 1 -> 2 -> 0 under Sym(3) = <(0 1), (0 1 2)>: (0 1) reverses it, so
 * only (0 1 2) is inserted and A_known = C_3; one representative at the root, and the
 * stabiliser of any point is trivial. */
static void test_graph_and_dag(void)
{
    canon_root_image img;
    canon_root_image_init(&img);
    canon_symmetry a;
    canon_symmetry_init(&a);
    const uint32_t gens[2][3] = {{1, 0, 2}, {1, 2, 0}};
    for (int backend = 0; backend < 2; ++backend) {
        canon_group *g = make_group(backend, 3, &gens[0][0], 2);
        canon_root x;
        memset(&x, 0, sizeof x);
        x.kind = CANON_ROOT_GRAPH;
        x.n = 3;
        const canon_arc arcs[3] = {{0, 1, NULL, 0, 1}, {1, 2, NULL, 0, 1}, {2, 0, NULL, 0, 1}};
        CHECK(canon_graph_init(&x.u.graph, 3, NULL, NULL, arcs, 3) == CANON_COMPLETE);
        CHECK(canon_symmetry_reset(&a, 3) == CANON_COMPLETE);
        CHECK(canon_symmetry_from_inputs(&a, g, &x, &img) == CANON_COMPLETE);
        CHECK(a.chain.order == 3 && a.stats.not_fixing == 1 && a.stats.inserted == 1);
        const uint32_t all[3] = {2, 0, 1};
        uint32_t reps[3], count = 0;
        CHECK(canon_symmetry_orbit_reps(&a, 0, all, 3, reps, &count) == CANON_COMPLETE);
        CHECK(count == 1 && reps[0] == 0);
        CHECK(canon_symmetry_descend(&a, 0, 2) == CANON_COMPLETE);
        CHECK(canon_symmetry_trivial_at(&a, 1));
        canon_root_free(&x);
        canon_group_release(g);
    }
    /* A nested tuple (0, 1) on 4 points (spec 4.1 records A(0), A(1), T(0,1)) under
     * Sym(4) = <(0 1), (1 2), (2 3)>: only (2 3) fixes it, A_known = <(2 3)>. */
    const uint8_t stream[] = {0x43, 0x4e, 0x02, 0x00, 0x01, 0x00, 0x01, 0, 0, 0, 4, /* H(4) */
                              0,    0,    0,    3,                                    /* q = 3 */
                              0x01, 0,    0,    0,    0,                              /* A(0) */
                              0x01, 0,    0,    0,    1,                              /* A(1) */
                              0x03, 0,    0,    0,    2, 0, 0, 0, 0, 0, 0, 0, 1,      /* T */
                              0,    0,    0,    2};                                   /* root */
    const canon_dag_limits lim = {4096, UINT64_MAX, UINT64_MAX, UINT64_MAX};
    const uint32_t adj[3][4] = {{1, 0, 2, 3}, {0, 2, 1, 3}, {0, 1, 3, 2}};
    for (int backend = 0; backend < 2; ++backend) {
        canon_group *g = make_group(backend, 4, &adj[0][0], 3);
        canon_root x;
        CHECK(canon_root_import_stream(&x, 4, stream, sizeof stream, &lim, NULL) ==
              CANON_COMPLETE);
        CHECK(x.kind == CANON_ROOT_DAG);
        CHECK(canon_symmetry_reset(&a, 4) == CANON_COMPLETE);
        CHECK(canon_symmetry_from_inputs(&a, g, &x, &img) == CANON_COMPLETE);
        CHECK(a.chain.order == 2 && a.stats.not_fixing == 2 && a.gens.count == 1);
        CHECK(a.gens.data[0] == 0 && a.gens.data[1] == 1 && a.gens.data[2] == 3 &&
              a.gens.data[3] == 2);
        const uint32_t all[4] = {0, 1, 2, 3};
        uint32_t reps[4], count = 0;
        CHECK(canon_symmetry_orbit_reps(&a, 0, all, 4, reps, &count) == CANON_COMPLETE);
        CHECK(count == 3 && reps[0] == 0 && reps[1] == 1 && reps[2] == 2);
        canon_root_free(&x);
        canon_group_release(g);
    }
    canon_symmetry_free(&a);
    canon_root_image_free(&img);
}

/* ---- 5: degenerate cases ---- */

static void test_degenerate(void)
{
    canon_root_image img;
    canon_root_image_init(&img);
    canon_symmetry a;
    canon_symmetry_init(&a);
    for (int backend = 0; backend < 2; ++backend) {
        /* no generator fixes x = {0} under C_3 = <(0 1 2)>: A_known = 1, no rebase ever */
        const uint32_t c3[3] = {1, 2, 0};
        canon_group *g = make_group(backend, 3, c3, 1);
        canon_root x;
        subset_root(&x, 3, 1u);
        CHECK(canon_symmetry_reset(&a, 3) == CANON_COMPLETE);
        CHECK(canon_symmetry_from_inputs(&a, g, &x, &img) == CANON_COMPLETE);
        CHECK(canon_symmetry_trivial_at(&a, 0) && a.stats.not_fixing == 1);
        CHECK(canon_symmetry_descend(&a, 0, 1) == CANON_COMPLETE);
        CHECK(canon_symmetry_descend(&a, 1, 2) == CANON_COMPLETE);
        CHECK(canon_symmetry_trivial_at(&a, 2) && a.stats.rebases == 0);
        const uint32_t m[2] = {2, 1};
        uint32_t reps[2], count = 0;
        CHECK(canon_symmetry_orbit_reps(&a, 1, m, 2, reps, &count) == CANON_COMPLETE);
        CHECK(count == 2 && reps[0] == 2 && reps[1] == 1);
        /* module contract: an atom out of range, a depth not yet reached */
        CHECK(canon_symmetry_descend(&a, 0, 3) == CANON_INVALID_INPUT);
        CHECK(canon_symmetry_descend(&a, 7, 0) == CANON_INVALID_INPUT);
        canon_root_free(&x);
        /* a group without recorded generators: the conjugate (spec 3.1) */
        canon_group *c = NULL;
        const uint32_t rho[3] = {2, 0, 1};
        CHECK(g->ops->conjugate(g, rho, &c) == CANON_COMPLETE);
        CHECK(canon_group_input_generators(c) == NULL);
        canon_root e;
        subset_root(&e, 3, 0);
        CHECK(canon_symmetry_reset(&a, 3) == CANON_COMPLETE);
        CHECK(canon_symmetry_from_inputs(&a, c, &e, &img) == CANON_COMPLETE);
        CHECK(canon_symmetry_trivial_at(&a, 0) && a.stats.considered == 0);
        canon_root_free(&e);
        canon_group_release(c);
        canon_group_release(g);
        /* n = 0: the empty permutation generates the trivial group */
        g = make_group(backend, 0, NULL, 1);
        subset_root(&e, 0, 0);
        CHECK(canon_symmetry_reset(&a, 0) == CANON_COMPLETE);
        CHECK(canon_symmetry_from_inputs(&a, g, &e, &img) == CANON_COMPLETE);
        CHECK(canon_symmetry_trivial_at(&a, 0) && a.chain.order == 1);
        CHECK(canon_symmetry_orbit_reps(&a, 0, NULL, 0, NULL, &count) == CANON_COMPLETE &&
              count == 0);
        canon_root_free(&e);
        canon_group_release(g);
        /* n = 1: the identity only */
        const uint32_t id1[1] = {0};
        g = make_group(backend, 1, id1, 1);
        subset_root(&e, 1, 1u);
        CHECK(canon_symmetry_reset(&a, 1) == CANON_COMPLETE);
        CHECK(canon_symmetry_from_inputs(&a, g, &e, &img) == CANON_COMPLETE);
        CHECK(canon_symmetry_trivial_at(&a, 0) && a.stats.considered == 1 &&
              a.stats.inserted == 0);
        canon_root_free(&e);
        canon_group_release(g);
        /* degree mismatch is the caller's error */
        g = make_group(backend, 3, c3, 1);
        subset_root(&e, 1, 0);
        CHECK(canon_symmetry_reset(&a, 1) == CANON_COMPLETE);
        CHECK(canon_symmetry_from_inputs(&a, g, &e, &img) == CANON_INVALID_INPUT);
        canon_root_free(&e);
        canon_group_release(g);
    }
    /* signed groups answer with their signed generators (S6 canon_group_signs) */
    {
        const uint32_t t01[3] = {1, 0, 2};
        const int8_t sign = -1;
        canon_group *g = NULL;
        CHECK(canon_group_create_signed(CTX[0], 3, t01, 1, &sign, &g) == CANON_COMPLETE);
        const canon_perm_table *in = canon_group_input_generators(g);
        CHECK(in != NULL && in->count == 1 && in->data[0] == 1 && in->data[1] == 0);
        canon_root e;
        subset_root(&e, 3, 4u);
        CHECK(canon_symmetry_reset(&a, 3) == CANON_COMPLETE);
        CHECK(canon_symmetry_from_inputs(&a, g, &e, &img) == CANON_COMPLETE);
        CHECK(a.chain.order == 2);
        canon_root_free(&e);
        canon_group_release(g);
    }
    canon_symmetry_free(&a);
    canon_root_image_free(&img);
}

int main(void)
{
    const canon_context_options chain = {CANON_BACKEND_CHAIN};
    const canon_context_options explicit_backend = {CANON_BACKEND_EXPLICIT};
    CHECK(canon_context_create_with_options(NULL, &chain, &CTX[0]) == CANON_COMPLETE);
    CHECK(canon_context_create_with_options(NULL, &explicit_backend, &CTX[1]) == CANON_COMPLETE);
    test_insertion();
    test_tier();
    test_sym6();
    test_graph_and_dag();
    test_degenerate();
    canon_context_release(CTX[0]);
    canon_context_release(CTX[1]);
    return check_finish("test_symmetry");
}
