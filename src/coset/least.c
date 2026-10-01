/* Constrained least elements of cosets (spec 9.4 "first choose its least image-array element
 * r0 by successive point constraints", "descend in point-image lexicographic order through
 * exact constrained cosets"; spec 8.1 t_b; slice S4, docs/slices/S4.md 3.1; detailed plan 2.4:
 * one audited primitive shared by the enumerator and the group encoding).
 *
 * Descent.  The current coset is J' r' where J' is the pointwise stabiliser in J of the points
 * already fixed, represented as level `lv` of a chain `cur`.  Points v = 0, 1, ..., n-1 are
 * fixed in increasing order, so the image array is minimised entry by entry: the images of v
 * under J' r' are (v^J')^r' = {r'[o] : o in v^J'}; the least admissible one c = r'[o] is
 * chosen, and the elements of J' r' sending v to c are exactly J'_v t_o r', where t_o in J'
 * sends v to o (h r' sends v to c iff h[v] = o iff h t_o^-1 fixes v).  Hence r' <- t_o r'
 * (t_o first, spec 3) and J' <- J'_v.  When v is fixed by J' its image r'[v] is forced and
 * nothing changes.  Computing J'_v needs a chain of J' whose first base point is v: one
 * rebuild (spec 9.2 base change, transient and unverified, S3 review item 2) whenever v is
 * moved by J' and is not already the current base point.  The number of rebuilds is therefore
 * at most the number of strict descents, log2 |J|; it is the S3 cost model, and reusing levels
 * is M5 work. */
#include <stdlib.h>
#include <string.h>

#include "arena/alloc.h"
#include "coset/coset.h"
#include "perm/perm.h"
#include "util/sort.h"

typedef struct least_scratch {
    uint32_t n;
    uint32_t *want;  /* n: constrained image of each point, or CANON_BSGS_NONE */
    uint32_t *t;     /* n: transporter */
    uint32_t *tmp;   /* n */
    uint32_t *queue; /* n: orbit of a point that is not a base point */
    uint32_t *mark;  /* n: visit stamps for the orbit traversal */
    uint32_t *order; /* n: orbit points sorted by image (least_outside) */
    uint32_t *sort_tmp;
    uint32_t *g;     /* n: sift residue (least_outside) */
    uint32_t stamp;
} least_scratch;

static void scratch_free(least_scratch *x)
{
    free(x->want); /* one block holds every array */
    memset(x, 0, sizeof *x);
}

static canon_status scratch_alloc(least_scratch *x, uint32_t n)
{
    memset(x, 0, sizeof *x);
    canon_status st = CANON_COMPLETE;
    size_t words = 0;
    if (!canon_size_mul((size_t)n, 9u, &words)) {
        return CANON_CAPACITY_LIMIT; /* spec 11.1 */
    }
    uint32_t *block = canon_alloc_array(words, sizeof *block, &st);
    if (block == NULL) {
        return st;
    }
    x->n = n;
    x->want = block;
    x->t = block + (size_t)n;
    x->tmp = block + 2u * (size_t)n;
    x->queue = block + 3u * (size_t)n;
    x->mark = block + 4u * (size_t)n;
    x->order = block + 5u * (size_t)n;
    x->sort_tmp = block + 6u * (size_t)n;
    x->g = block + 7u * (size_t)n;
    for (uint32_t v = 0; v < n; ++v) {
        x->want[v] = CANON_BSGS_NONE;
        x->mark[v] = 0;
    }
    return CANON_COMPLETE;
}

uint32_t canon_coset_least_moved(const canon_bsgs *c, uint32_t level)
{
    const canon_bsgs_level *L = &c->levels[level];
    uint32_t least = CANON_BSGS_NONE;
    /* spec 8.1 "smallest atom moved by H": the least point moved by some generator */
    for (uint32_t k = 0; k < L->gen_count; ++k) {
        const uint32_t *s = canon_perm_table_row(&c->gens, L->gen_ids[k]);
        for (uint32_t v = 0; v < c->n && v < least; ++v) {
            if (s[v] != v) {
                least = v;
                break;
            }
        }
    }
    return least;
}

/* The orbit of v under K_lv of c: the stored orbit when v is the level's base point, else a
 * queue traversal over the level generators into x->queue.  Returns its length; *orbit points
 * to its points (in discovery order). */
static uint32_t orbit_of(const canon_bsgs *c, uint32_t lv, uint32_t v, least_scratch *x,
                         const uint32_t **orbit)
{
    const canon_bsgs_level *L = &c->levels[lv];
    if (lv < c->depth && L->base_point == v) {
        *orbit = L->orbit;
        return L->orbit_len;
    }
    if (x->stamp == UINT32_MAX) {
        for (uint32_t w = 0; w < c->n; ++w) {
            x->mark[w] = 0;
        }
        x->stamp = 0;
    }
    const uint32_t st = ++x->stamp;
    uint32_t len = 1;
    x->queue[0] = v;
    x->mark[v] = st;
    for (uint32_t head = 0; head < len; ++head) {
        for (uint32_t k = 0; k < L->gen_count; ++k) {
            const uint32_t y = canon_perm_table_row(&c->gens, L->gen_ids[k])[x->queue[head]];
            if (x->mark[y] != st) {
                x->mark[y] = st;
                x->queue[len++] = y;
            }
        }
    }
    *orbit = x->queue;
    return len;
}

/* Make v the base point of the current level (spec 9.2 base change): if it is not already,
 * rebuild K_lv with base prefix (v) into *own (transient, not verified) and continue at level 0
 * of it. */
static canon_status ensure_base(const canon_bsgs **cur, uint32_t *lv, uint32_t v, canon_bsgs *own,
                                canon_coset_stats *stats)
{
    const canon_bsgs *c = *cur;
    if (*lv < c->depth && c->levels[*lv].base_point == v) {
        return CANON_COMPLETE;
    }
    canon_bsgs next;
    canon_status st = canon_bsgs_rebase(c, *lv, &v, 1, false, &next);
    if (st != CANON_COMPLETE) {
        return st;
    }
    if (stats != NULL) {
        stats->rebuilds += 1;
    }
    canon_bsgs_free(own); /* c may be own: it is no longer read */
    *own = next;
    *cur = own;
    *lv = 0;
    return CANON_COMPLETE;
}

/* One step of the descent at point v (file comment): among the images of v under J' r', take
 * `want` if it is not CANON_BSGS_NONE (and report *empty if it is not an image of v), else the
 * least one; then r' <- t_o r' and J' <- J'_v.  When v is fixed by J' nothing changes (its
 * image r'[v] is forced). */
static canon_status step(const canon_bsgs **cur, uint32_t *lv, uint32_t v, uint32_t want,
                         uint32_t *r, least_scratch *x, canon_bsgs *own, bool *empty,
                         canon_coset_stats *stats)
{
    const uint32_t n = x->n;
    *empty = false;
    if ((*cur)->levels[*lv].gen_count == 0) {
        /* J' is trivial: the coset is {r'} */
        *empty = want != CANON_BSGS_NONE && r[v] != want;
        return CANON_COMPLETE;
    }
    const uint32_t *orbit = NULL;
    const uint32_t len = orbit_of(*cur, *lv, v, x, &orbit);
    /* spec 9.4: the least image r'[o], o in v^J' (or the constrained one, if it occurs) */
    uint32_t o = CANON_BSGS_NONE;
    for (uint32_t p = 0; p < len; ++p) {
        const uint32_t w = orbit[p];
        if (want != CANON_BSGS_NONE) {
            if (r[w] == want) {
                o = w;
                break;
            }
        } else if (o == CANON_BSGS_NONE || r[w] < r[o]) {
            o = w;
        }
    }
    if (o == CANON_BSGS_NONE) {
        *empty = true; /* the constrained image is not an image of v */
        return CANON_COMPLETE;
    }
    if (len == 1) {
        return CANON_COMPLETE; /* v fixed by J': J'_v = J', image forced */
    }
    canon_status st = ensure_base(cur, lv, v, own, stats);
    if (st != CANON_COMPLETE) {
        return st;
    }
    /* r' <- t_o r' with t_o in J', v^t_o = o (spec 3: t_o acts first) */
    canon_bsgs_transporter_scratch(*cur, *lv, o, x->t, x->tmp);
    canon_perm_compose(x->t, r, x->tmp, n);
    memcpy(r, x->tmp, (size_t)n * sizeof *r);
    *lv += 1; /* J' <- J'_v, the next level of a chain based at v */
    return CANON_COMPLETE;
}

/* The descent of the file comment from (cur, lv, r) with the constraints in x->want; r is
 * updated in place to the least admissible element.  The constrained points are fixed first
 * (spec 9.4 "successive point constraints"), which restricts the coset to exactly the
 * admissible elements, a coset J'' r'' again; the unconstrained descent over v = 0..n-1 then
 * minimises the image array within it.  (Choosing least images greedily before applying a
 * later constraint would be wrong: the least branch at v may contain no admissible element.) */
static canon_status descend(const canon_bsgs *cur, uint32_t lv, uint32_t *r, least_scratch *x,
                            canon_bsgs *own, bool *found, canon_coset_stats *stats)
{
    const uint32_t n = x->n;
    *found = false;
    bool empty = false;
    for (uint32_t v = 0; v < n; ++v) {
        if (x->want[v] != CANON_BSGS_NONE) {
            canon_status st = step(&cur, &lv, v, x->want[v], r, x, own, &empty, stats);
            if (st != CANON_COMPLETE || empty) {
                return st;
            }
        }
    }
    for (uint32_t v = 0; v < n && cur->levels[lv].gen_count > 0; ++v) {
        canon_status st = step(&cur, &lv, v, CANON_BSGS_NONE, r, x, own, &empty, stats);
        if (st != CANON_COMPLETE) {
            return st;
        }
    }
    *found = true;
    return CANON_COMPLETE;
}

canon_status canon_coset_least(const canon_bsgs *c, uint32_t level, const uint32_t *r,
                               const canon_coset_constraint *cons, uint32_t k, uint32_t *out,
                               bool *found, canon_coset_stats *stats)
{
    const uint32_t n = c->n;
    *found = false;
    if (level > c->depth) {
        return CANON_INVALID_INPUT;
    }
    for (uint32_t i = 0; i < k; ++i) {
        if (cons[i].point >= n || cons[i].image >= n) {
            return CANON_INVALID_INPUT;
        }
    }
    least_scratch x;
    canon_status st = scratch_alloc(&x, n);
    if (st != CANON_COMPLETE) {
        return st;
    }
    if (stats != NULL) {
        stats->descents += 1;
    }
    bool consistent = true;
    for (uint32_t i = 0; i < k; ++i) {
        uint32_t *w = &x.want[cons[i].point];
        /* two different images for one point: no element satisfies both */
        consistent = consistent && (*w == CANON_BSGS_NONE || *w == cons[i].image);
        *w = cons[i].image;
    }
    if (consistent) {
        for (uint32_t v = 0; v < n; ++v) {
            out[v] = r != NULL ? r[v] : v;
        }
        canon_bsgs own;
        canon_bsgs_init(&own, n);
        st = descend(c, level, out, &x, &own, found, stats);
        canon_bsgs_free(&own);
    }
    scratch_free(&x);
    return st;
}

/* ---- spec 9.4 rule 2: the least element of H \ K ---- */

/* p in K, by sifting a copy (spec 9.2 membership). */
static bool member(const canon_bsgs *k, const uint32_t *p, least_scratch *x)
{
    memcpy(x->g, p, (size_t)x->n * sizeof *x->g);
    uint32_t stop = 0;
    canon_bsgs_sift(k, 0, x->g, &stop, NULL);
    return stop == k->depth && canon_perm_is_identity(x->g, x->n);
}

/* spec 9.4 "J <= K": every generator of J = K_lv of c sifts to the identity in K. */
static bool level_in(const canon_bsgs *c, uint32_t lv, const canon_bsgs *k, least_scratch *x)
{
    const canon_bsgs_level *L = &c->levels[lv];
    for (uint32_t i = 0; i < L->gen_count; ++i) {
        if (!member(k, canon_perm_table_row(&c->gens, L->gen_ids[i]), x)) {
            return false;
        }
    }
    return true;
}

typedef struct by_image {
    const uint32_t *r;
} by_image;

/* orbit points by their image under r (distinct, since r is a bijection) */
static int image_cmp(const void *a, const void *b, void *ctx)
{
    const uint32_t *r = ((const by_image *)ctx)->r;
    const uint32_t ia = r[*(const uint32_t *)a], ib = r[*(const uint32_t *)b];
    return ia < ib ? -1 : ia > ib;
}

/* The descent of rule 2.  Invariant: the current coset C = J' r' is not contained in K.
 * spec 9.4: "For C=Jr, C ⊆ K iff r ∈ K and J ≤ K; discard exactly these branches and choose the
 * least surviving point-image branch."  The children of C at point v are J'_v t_o r' for o in
 * v^J'; they share J'_v, so "J'_v <= K" is tested once per level, and a child is discarded iff
 * that holds and t_o r' is in K.  Since C is the disjoint union of its children and C is not
 * in K, some child survives.  Once J' <= K, r' is not in K (else C would be), so no element of
 * C is in K (j r' in K would give r' in K) and the answer is the least element of C. */
static canon_status outside_descend(const canon_bsgs *cur, const canon_bsgs *k, uint32_t *r,
                                    least_scratch *x, canon_bsgs *own, uint32_t *out,
                                    canon_coset_stats *stats)
{
    const uint32_t n = x->n;
    uint32_t lv = 0;
    for (uint32_t v = 0; v < n; ++v) {
        if (level_in(cur, lv, k, x)) {
            /* J' <= K and C not in K: C is disjoint from K; its least element (no constraint;
             * x->want is all NONE here) is the answer */
            bool found = false;
            memcpy(out, r, (size_t)n * sizeof *out);
            canon_bsgs inner;
            canon_bsgs_init(&inner, n);
            canon_status st = descend(cur, lv, out, x, &inner, &found, stats);
            canon_bsgs_free(&inner);
            return st == CANON_COMPLETE && !found ? CANON_INTERNAL_ERROR : st;
        }
        const uint32_t *orbit = NULL;
        const uint32_t len = orbit_of(cur, lv, v, x, &orbit);
        if (len == 1) {
            continue; /* v fixed by J': one child, C itself */
        }
        memcpy(x->order, orbit, (size_t)len * sizeof *x->order);
        canon_status st = ensure_base(&cur, &lv, v, own, stats);
        if (st != CANON_COMPLETE) {
            return st;
        }
        /* candidate images in increasing order (spec 9.4 point-image lexicographic order) */
        by_image ctx = {r};
        canon_stable_sort(x->order, len, sizeof *x->order, x->sort_tmp, image_cmp, &ctx);
        const bool child_group_in = level_in(cur, lv + 1u, k, x); /* J'_v <= K */
        bool chosen = false;
        for (uint32_t p = 0; p < len && !chosen; ++p) {
            canon_bsgs_transporter_scratch(cur, lv, x->order[p], x->t, x->tmp);
            canon_perm_compose(x->t, r, x->tmp, n); /* t_o r': t_o acts first (spec 3) */
            chosen = !child_group_in || !member(k, x->tmp, x);
        }
        if (!chosen) {
            return CANON_INTERNAL_ERROR; /* contradicts the invariant: C would lie in K */
        }
        memcpy(r, x->tmp, (size_t)n * sizeof *r);
        lv += 1;
    }
    /* every point fixed: C = {r'} and r' is not in K */
    memcpy(out, r, (size_t)n * sizeof *out);
    return CANON_COMPLETE;
}

canon_status canon_coset_least_outside(const canon_bsgs *h, const canon_bsgs *k, uint32_t *out,
                                       bool *found, canon_coset_stats *stats)
{
    *found = false;
    if (h->n != k->n) {
        return CANON_INVALID_INPUT;
    }
    /* K <= H with |K| = |H| means K = H: nothing is outside (spec 9.4 "until K=H") */
    if (canon_bsgs_suffix_order(k, 0) == canon_bsgs_suffix_order(h, 0)) {
        return CANON_COMPLETE;
    }
    const uint32_t n = h->n;
    least_scratch x;
    canon_status st = scratch_alloc(&x, n);
    if (st != CANON_COMPLETE) {
        return st;
    }
    if (stats != NULL) {
        stats->descents += 1;
    }
    canon_status alloc = CANON_COMPLETE;
    uint32_t *r = canon_alloc_array(n, sizeof *r, &alloc);
    if (r == NULL) {
        scratch_free(&x);
        return alloc;
    }
    for (uint32_t v = 0; v < n; ++v) {
        r[v] = v; /* C = H id, not contained in K */
    }
    canon_bsgs own;
    canon_bsgs_init(&own, n);
    st = outside_descend(h, k, r, &x, &own, out, stats);
    canon_bsgs_free(&own);
    free(r);
    scratch_free(&x);
    *found = st == CANON_COMPLETE;
    return st;
}
