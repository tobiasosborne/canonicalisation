/* The verified known-automorphism subgroup and prefix-stabiliser orbits (spec 7.3, 9.2; slice
 * S7, docs/slices/S7.md 3.1-3.2). */
#include "symmetry/symmetry.h"

#include <stdlib.h>
#include <string.h>

#include "arena/alloc.h"
#include "arena/checked.h"

void canon_symmetry_init(canon_symmetry *s)
{
    memset(s, 0, sizeof *s);
    canon_bsgs_init(&s->a, 0);
    canon_perm_table_init(&s->gens, 0);
    canon_bsgs_init(&s->stab, 0);
}

static void free_arrays(canon_symmetry *s)
{
    free(s->scratch); /* one block: scratch, mark, parent_point, parent_gen, rep, queue */
    s->scratch = s->mark = s->parent_point = s->parent_gen = s->rep = s->queue = NULL;
    s->cap = 0;
    s->ready = false;
}

void canon_symmetry_free(canon_symmetry *s)
{
    canon_bsgs_free(&s->a);
    canon_perm_table_free(&s->gens);
    canon_bsgs_free(&s->stab);
    free_arrays(s);
    canon_symmetry_init(s);
}

canon_status canon_symmetry_reset(canon_symmetry *s, uint32_t n)
{
    s->ready = false;
    memset(&s->stats, 0, sizeof s->stats);
    if (s->scratch == NULL || n > s->cap) {
        /* six per-point arrays in one block (spec 17: workspace storage grows only) */
        size_t words = 0;
        if (!canon_size_mul((size_t)n, 6u, &words)) {
            return CANON_CAPACITY_LIMIT; /* spec 11.1 */
        }
        canon_status st = CANON_COMPLETE;
        uint32_t *block = canon_alloc_array(words, sizeof *block, &st);
        if (block == NULL) {
            return st;
        }
        free_arrays(s);
        s->scratch = block;
        s->mark = block + (size_t)n;
        s->parent_point = block + 2u * (size_t)n;
        s->parent_gen = block + 3u * (size_t)n;
        s->rep = block + 4u * (size_t)n;
        s->queue = block + 5u * (size_t)n;
        s->cap = n;
    }
    /* spec 7.3: A_known starts trivial; the chain and the generator table are of degree n */
    canon_bsgs_free(&s->a);
    canon_perm_table_free(&s->gens);
    canon_bsgs_free(&s->stab);
    canon_bsgs_init(&s->a, n);
    canon_perm_table_init(&s->gens, n);
    canon_bsgs_init(&s->stab, n);
    s->stab_depth = 0;
    s->n = n;
    s->stamp = 0;
    s->epoch = 0;
    for (uint32_t v = 0; v < n; ++v) {
        s->mark[v] = 0;
    }
    s->ready = true;
    return CANON_COMPLETE;
}

canon_status canon_symmetry_insert(canon_symmetry *s, const canon_root *x, canon_root_image *img,
                                   const uint32_t *g, bool *inserted)
{
    *inserted = false;
    s->stats.candidates += 1;
    /* spec 7.3: an automorphism is published only after the exact action check x^g = x
     * (spec 2.1 action, spec 4.2 extensional equality) */
    canon_status st = canon_root_act_into(x, g, img);
    if (st != CANON_COMPLETE) {
        return st;
    }
    const bool fixes = canon_root_equal(&img->root, x);
    canon_root_image_clear(img); /* a graph image borrows x's tables: drop them now */
    if (!fixes) {
        return CANON_INVALID_INPUT;
    }
    /* spec 7.3 "verified subgroup": membership in A_known by one sift, else a verified rebuild
     * of the chain of <gens, g> (canon_bsgs_insert_verified) */
    st = canon_bsgs_insert_verified(&s->a, &s->gens, g, s->scratch, inserted);
    if (st == CANON_COMPLETE && *inserted) {
        s->stats.inserted += 1;
        s->epoch += 1;
        s->stab_depth = 0; /* any stabiliser computed so far is stale: A_known grew */
    }
    return st;
}

bool canon_symmetry_trivial(const canon_symmetry *s)
{
    return s->gens.count == 0;
}

canon_status canon_symmetry_stabiliser(canon_symmetry *s, const uint32_t *prefix, uint32_t d)
{
    if (d == 0 || canon_symmetry_trivial(s)) {
        /* the pointwise stabiliser of the empty prefix is A_known itself; a trivial A_known
         * has no levels to rebase and every orbit is a singleton */
        s->stab_depth = 0;
        return CANON_COMPLETE;
    }
    /* spec 9.2 "base change rebuilds/certifies for the requested ordered base (reuse is
     * optional)": rebuild A_known with the prefix as the first d base points (each prefix point
     * becomes a base point even when its orbit is a singleton, src/bsgs/chain.c build_run), so
     * that levels[d] generates the pointwise stabiliser of the prefix (spec 9.2 "Point
     * stabilisers").  Not verified: the chain is transient (S3 review item 2); the certificate
     * checker re-verifies every automorphism derived from it (S7 brief 3.4 rule 5). */
    canon_bsgs_free(&s->stab);
    s->stab_depth = 0;
    canon_status st = canon_bsgs_rebase(&s->a, 0, prefix, d, false, &s->stab);
    if (st != CANON_COMPLETE) {
        return st;
    }
    s->stats.rebases += 1;
    s->stab_depth = d;
    return CANON_COMPLETE;
}

/* The chain and level whose strong generators generate A_S. */
static const canon_bsgs *stab_chain(const canon_symmetry *s, uint32_t *level)
{
    if (s->stab_depth == 0) {
        *level = 0;
        return &s->a;
    }
    *level = s->stab_depth;
    return &s->stab;
}

/* A fresh stamp: everything marked before is now unmarked. */
static uint32_t next_stamp(canon_symmetry *s)
{
    if (s->stamp == UINT32_MAX) {
        for (uint32_t v = 0; v < s->n; ++v) {
            s->mark[v] = 0;
        }
        s->stamp = 0;
    }
    return ++s->stamp;
}

/* BFS of the orbit of the representative r under the level generators, marking every point
 * reached that is not yet marked with the current stamp. */
static void grow_orbit(canon_symmetry *s, const canon_bsgs *c, const canon_bsgs_level *L,
                       uint32_t r, uint32_t st)
{
    const uint32_t gen_count = L != NULL ? L->gen_count : 0;
    uint32_t head = 0, tail = 0;
    s->queue[tail++] = r;
    while (head < tail) {
        const uint32_t p = s->queue[head++];
        for (uint32_t k = 0; k < gen_count; ++k) {
            const uint32_t id = L->gen_ids[k];
            const uint32_t q = canon_perm_table_row(&c->gens, id)[p]; /* p^s */
            if (s->mark[q] != st) {
                s->mark[q] = st;
                s->rep[q] = r;
                s->parent_point[q] = p;
                s->parent_gen[q] = id;
                s->queue[tail++] = q; /* orbits are disjoint: at most n points in all */
            }
        }
    }
}

void canon_symmetry_cell_orbits(canon_symmetry *s, const uint32_t *cell, uint32_t len,
                                const uint8_t *root)
{
    uint32_t level = 0;
    const canon_bsgs *c = stab_chain(s, &level);
    const canon_bsgs_level *L = c->depth > level ? &c->levels[level] : NULL;
    const uint32_t st = next_stamp(s);
    if (root != NULL) {
        /* S7b: the representatives are given; mark them first so that a representative
         * reached from another one (two explored children whose orbits merged when A_known
         * grew) keeps itself as representative */
        for (uint32_t j = 0; j < len; ++j) {
            if (root[j]) {
                s->mark[cell[j]] = st;
                s->rep[cell[j]] = cell[j];
                s->parent_point[cell[j]] = CANON_BSGS_NONE;
                s->parent_gen[cell[j]] = CANON_BSGS_NONE;
            }
        }
        for (uint32_t j = 0; j < len; ++j) {
            if (root[j]) {
                s->stats.orbits += 1;
                grow_orbit(s, c, L, cell[j], st);
            }
        }
        for (uint32_t j = 0; j < len; ++j) {
            if (s->mark[cell[j]] != st) {
                s->rep[cell[j]] = cell[j]; /* reached by no representative: undecided */
            }
        }
        return;
    }
    for (uint32_t j = 0; j < len; ++j) {
        const uint32_t r = cell[j];
        if (s->mark[r] == st) {
            continue; /* reached from an earlier cell point: not a representative */
        }
        /* S7 brief 3.1 rule: the first cell point of each orbit, in cell order, is explored;
         * its orbit under A_S is the BFS closure under the level generators. */
        s->stats.orbits += 1;
        s->mark[r] = st;
        s->rep[r] = r;
        s->parent_point[r] = CANON_BSGS_NONE;
        s->parent_gen[r] = CANON_BSGS_NONE;
        grow_orbit(s, c, L, r, st);
    }
}

uint32_t canon_symmetry_rep(const canon_symmetry *s, uint32_t v)
{
    return s->rep[v];
}

void canon_symmetry_transporter(canon_symmetry *s, uint32_t b, uint32_t *out)
{
    uint32_t level = 0;
    const canon_bsgs *c = stab_chain(s, &level);
    const uint32_t n = s->n;
    /* Collect the edges from b up to the representative: tmp[k] = generator of the k-th edge
     * walking upward, so the path downward applies them in reverse order. */
    uint32_t k = 0, v = b;
    while (s->parent_point[v] != CANON_BSGS_NONE) {
        s->queue[k++] = s->parent_gen[v]; /* queue is free scratch here (<= n edges) */
        v = s->parent_point[v];
    }
    for (uint32_t w = 0; w < n; ++w) {
        out[w] = w;
    }
    /* a = s_1 s_2 ... s_k, s_1 the edge out of the representative: out <- out s, i.e.
     * out[w] = s[out[w]] (spec 3: (pq)[v] = q[p[v]]), for s = s_1, ..., s_k in that order. */
    while (k-- > 0) {
        const uint32_t *gen = canon_perm_table_row(&c->gens, s->queue[k]);
        for (uint32_t w = 0; w < n; ++w) {
            out[w] = gen[out[w]];
        }
    }
}
