/* Slice S7: the verified automorphism subgroup A_known and the prefix-stabiliser orbits
 * (src/symmetry/symmetry.h; spec 7.3; docs/slices/S7.md 3.1-3.2, 4) against brute force on the
 * T1 tier: every subgroup of Sym(n), n <= 4, every subset. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "bsgs/chain.h"
#include "bsgs/chain_backend.h"
#include "bsgs/group.h"
#include "check.h"
#include "object/object.h"
#include "symmetry/symmetry.h"
#include "t1_groups.h"

static uint32_t elem_index(const t1_sym *s, const uint32_t *p)
{
    for (uint32_t i = 0; i < s->count; ++i) {
        if (s->n == 0 || memcmp(s->elem[i], p, s->n * sizeof *p) == 0) {
            return i;
        }
    }
    return UINT32_MAX;
}

/* x^p = x for a subset x given as a membership mask. */
static int fixes_mask(uint32_t n, uint32_t mask, const uint32_t *p)
{
    uint32_t image = 0;
    for (uint32_t a = 0; a < n; ++a) {
        if (mask >> a & 1u) {
            image |= 1u << p[a];
        }
    }
    return image == mask;
}

static uint64_t candidates, inserted_total, orbit_checks, transporter_checks;

/* Every ordered sequence of `d` distinct points of {0..n-1} (prefix), as the cell every
 * remaining point in increasing order: check orbits and transporters of A_S by brute force. */
static void check_prefixes(canon_symmetry *sym, const t1_sym *s, uint32_t a_mask, uint32_t n,
                           uint32_t *prefix, uint32_t d, uint32_t used)
{
    /* cell: the points not in the prefix, increasing */
    uint32_t cell[4], len = 0;
    for (uint32_t v = 0; v < n; ++v) {
        if (!(used >> v & 1u)) {
            cell[len++] = v;
        }
    }
    if (len >= 2) {
        CHECK(canon_symmetry_stabiliser(sym, prefix, d) == CANON_COMPLETE);
        canon_symmetry_cell_orbits(sym, cell, len, NULL);
        /* A_S by brute force: elements of A_known fixing the prefix pointwise */
        uint32_t stab = 0;
        for (uint32_t e = 0; e < s->count; ++e) {
            if (!(a_mask >> e & 1u)) {
                continue;
            }
            int fixes = 1;
            for (uint32_t i = 0; i < d; ++i) {
                fixes = fixes && s->elem[e][prefix[i]] == prefix[i];
            }
            if (fixes) {
                stab |= 1u << e;
            }
        }
        for (uint32_t j = 0; j < len; ++j) {
            const uint32_t b = cell[j];
            /* the representative is the first cell point in b's A_S-orbit */
            uint32_t rep = UINT32_MAX;
            for (uint32_t i = 0; i <= j && rep == UINT32_MAX; ++i) {
                for (uint32_t e = 0; e < s->count; ++e) {
                    if ((stab >> e & 1u) && s->elem[e][cell[i]] == b) {
                        rep = cell[i];
                        break;
                    }
                }
            }
            CHECK(canon_symmetry_rep(sym, b) == rep);
            orbit_checks += 1;
            /* S7b: with the first-of-orbit representatives given as roots, the same
             * assignment; with only cell[0] as root, points outside its orbit stay their own */
            uint8_t roots[4] = {0, 0, 0, 0};
            for (uint32_t i = 0; i < len; ++i) {
                roots[i] = canon_symmetry_rep(sym, cell[i]) == cell[i];
            }
            canon_symmetry_cell_orbits(sym, cell, len, roots);
            CHECK(canon_symmetry_rep(sym, b) == rep);
            uint8_t first[4] = {1, 0, 0, 0};
            canon_symmetry_cell_orbits(sym, cell, len, first);
            CHECK(canon_symmetry_rep(sym, b) == (rep == cell[0] ? cell[0] : b));
            canon_symmetry_cell_orbits(sym, cell, len, NULL); /* restore for the transporter */
            uint32_t a[4] = {0, 1, 2, 3};
            canon_symmetry_transporter(sym, b, a);
            const uint32_t ai = elem_index(s, a);
            CHECK(ai != UINT32_MAX && (stab >> ai & 1u)); /* a in A_S */
            CHECK(a[rep] == b);
            transporter_checks += 1;
        }
    }
    if (d + 1 >= n) {
        return;
    }
    for (uint32_t v = 0; v < n; ++v) {
        if (!(used >> v & 1u)) {
            prefix[d] = v;
            check_prefixes(sym, s, a_mask, n, prefix, d + 1, used | 1u << v);
        }
    }
}

static void t1_tier(void)
{
    canon_symmetry sym;
    canon_symmetry_init(&sym);
    canon_root_image img;
    canon_root_image_init(&img);
    for (uint32_t n = 0; n <= 4; ++n) {
        t1_sym s;
        t1_sym_init(&s, n);
        t1_group groups[T1_MAX_GROUPS];
        const uint32_t ng = t1_subgroups(&s, groups);
        for (uint32_t gi = 0; gi < ng; ++gi) {
            canon_group *g = NULL;
            CHECK(canon_group_chain_create(n, groups[gi].gens, groups[gi].gen_count, &g) ==
                  CANON_COMPLETE);
            CHECK(g->gens.count == groups[gi].gen_count); /* S7: the handle keeps them */
            for (uint32_t mask = 0; mask < (1u << n); ++mask) {
                canon_root x;
                memset(&x, 0, sizeof x);
                x.kind = CANON_ROOT_SUBSET;
                x.n = n;
                uint32_t atoms[4], k = 0;
                for (uint32_t a = 0; a < n; ++a) {
                    if (mask >> a & 1u) {
                        atoms[k++] = a;
                    }
                }
                CHECK(canon_subset_init(&x.u.subset, n, atoms, k) == CANON_COMPLETE);
                CHECK(canon_symmetry_reset(&sym, n) == CANON_COMPLETE);
                CHECK(canon_symmetry_trivial(&sym));
                /* source i: the generators that fix x */
                uint32_t fixing = 0;
                for (uint32_t j = 0; j < g->gens.count; ++j) {
                    const uint32_t *gen = canon_perm_table_row(&g->gens, j);
                    bool ins = false;
                    const canon_status st = canon_symmetry_insert(&sym, &x, &img, gen, &ins);
                    candidates += 1;
                    if (fixes_mask(n, mask, gen)) {
                        CHECK(st == CANON_COMPLETE);
                        fixing |= 1u << elem_index(&s, gen);
                        inserted_total += ins;
                    } else {
                        CHECK(st == CANON_INVALID_INPUT && !ins);
                    }
                }
                /* A_known = <fixing generators>, every member fixes x, as a verified chain */
                const uint32_t a_mask = t1_closure(&s, fixing);
                CHECK(sym.a.order == t1_popcount(a_mask));
                CHECK(canon_symmetry_trivial(&sym) == (a_mask == 1u));
                CHECK(sym.gens.count == 0 || sym.a.verified);
                uint32_t scratch[4];
                for (uint32_t e = 0; e < s.count; ++e) {
                    const bool member = canon_bsgs_contains_scratch(&sym.a, s.elem[e], scratch);
                    CHECK(member == ((a_mask >> e & 1u) != 0));
                    if (member) {
                        CHECK(fixes_mask(n, mask, s.elem[e]));
                    }
                }
                /* a member offered again is not inserted; a non-member of G that fixes x would
                 * be, but the module trusts its caller for membership in G (brief 3.1) */
                for (uint32_t j = 0; j < g->gens.count; ++j) {
                    bool ins = true;
                    const uint32_t *gen = canon_perm_table_row(&g->gens, j);
                    if (fixes_mask(n, mask, gen)) {
                        CHECK(canon_symmetry_insert(&sym, &x, &img, gen, &ins) == CANON_COMPLETE);
                        CHECK(!ins);
                    }
                }
                uint32_t prefix[4] = {0, 0, 0, 0};
                check_prefixes(&sym, &s, a_mask, n, prefix, 0, 0);
                canon_root_free(&x);
            }
            canon_group_release(g);
        }
    }
    canon_root_image_free(&img);
    canon_symmetry_free(&sym);
}

/* The image storage left by insert refers to no object (p1_tree.h invariant). */
static void borrowed_tables(void)
{
    const uint32_t swap[2] = {1, 0};
    const canon_arc arcs[2] = {{0, 1, NULL, 0, 1}, {1, 0, NULL, 0, 1}};
    canon_root x;
    memset(&x, 0, sizeof x);
    x.kind = CANON_ROOT_GRAPH;
    x.n = 2;
    CHECK(canon_graph_init(&x.u.graph, 2, NULL, NULL, arcs, 2) == CANON_COMPLETE);
    canon_symmetry sym;
    canon_symmetry_init(&sym);
    canon_root_image img;
    canon_root_image_init(&img);
    CHECK(canon_symmetry_reset(&sym, 2) == CANON_COMPLETE);
    bool ins = false;
    CHECK(canon_symmetry_insert(&sym, &x, &img, swap, &ins) == CANON_COMPLETE && ins);
    CHECK(img.root.u.graph.colours.offset == NULL && img.root.u.graph.n == 0);
    canon_root_free(&x);
    /* reset to a larger degree reallocates; to a smaller one reuses */
    CHECK(canon_symmetry_reset(&sym, 7) == CANON_COMPLETE && sym.cap == 7);
    CHECK(canon_symmetry_reset(&sym, 3) == CANON_COMPLETE && sym.cap == 7 && sym.n == 3);
    canon_root_image_free(&img);
    canon_symmetry_free(&sym);
}

int main(void)
{
    t1_tier();
    borrowed_tables();
    printf("T1 symmetry: %llu candidates, %llu inserted, %llu orbit checks, %llu transporters\n",
           (unsigned long long)candidates, (unsigned long long)inserted_total,
           (unsigned long long)orbit_checks, (unsigned long long)transporter_checks);
    return check_finish("test_symmetry");
}
