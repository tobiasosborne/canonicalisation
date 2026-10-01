/* The direct Schreier recursion of spec 9.1 (first paragraph), for tests only.
 *
 * spec 9.1: "For ordered base (0,...,n-1), use a deterministic Schreier construction.  At a
 * level with generators S for K fixing preceding base points: include inverses, remove
 * identities/duplicates, sort image arrays; build the orbit of a by queue traversal in
 * increasing discovered-label order and sorted generator order, retaining t_b with a^t_b=b.
 * For each b and s form t_b s t_(b^s)^-1, which fixes a.  Recurse with the deduplicated
 * nonidentity Schreier generators, finishing with a trivial subgroup."
 *
 * Convention (spec 3): (pq)[v] = q[p[v]].  A point y = x^s discovered from x through s gets
 * t_y = t_x s, so that i^(t_y) = (i^(t_x))^s = x^s = y. */
#include "bsgs/reference.h"

#include <stdlib.h>
#include <string.h>

#include "arena/alloc.h"
#include "arena/checked.h"
#include "perm/perm.h"
#include "util/sort.h"

#define REF_NONE UINT32_MAX

typedef struct cmp_ctx {
    const uint32_t *rows;
    uint32_t n;
} cmp_ctx;

static int row_cmp(const void *a, const void *b, void *ctx)
{
    const cmp_ctx *o = ctx;
    size_t ia = *(const size_t *)a, ib = *(const size_t *)b;
    return canon_perm_lex_compare(o->rows + ia * o->n, o->rows + ib * o->n, o->n);
}

/* Sort the `count` rows of `rows` (n entries each) lexicographically, drop identities and
 * duplicates, and return the new count (rows rewritten in place). */
static canon_status normalise(uint32_t *rows, size_t count, uint32_t n, size_t *out_count)
{
    canon_status st = CANON_COMPLETE;
    size_t *idx = canon_alloc_array(count, sizeof *idx, &st);
    size_t *tmp = canon_alloc_array(count, sizeof *tmp, &st);
    size_t words = 0;
    uint32_t *copy = NULL;
    if (canon_size_mul(count, n, &words)) {
        copy = canon_alloc_array(words, sizeof *copy, &st);
    } else {
        st = CANON_CAPACITY_LIMIT;
    }
    if (idx == NULL || tmp == NULL || copy == NULL) {
        free(idx);
        free(tmp);
        free(copy);
        return st;
    }
    for (size_t i = 0; i < count; ++i) {
        idx[i] = i;
    }
    cmp_ctx o = {rows, n};
    canon_stable_sort(idx, count, sizeof *idx, tmp, row_cmp, &o);
    size_t kept = 0;
    for (size_t i = 0; i < count; ++i) {
        const uint32_t *r = rows + idx[i] * n;
        if (canon_perm_is_identity(r, n)) {
            continue;
        }
        if (kept > 0 && canon_perm_lex_compare(copy + (kept - 1) * n, r, n) == 0) {
            continue;
        }
        memcpy(copy + kept * n, r, (size_t)n * sizeof *copy);
        ++kept;
    }
    if (kept > 0) {
        memcpy(rows, copy, kept * n * sizeof *rows);
    }
    free(idx);
    free(tmp);
    free(copy);
    *out_count = kept;
    return CANON_COMPLETE;
}

void canon_ref_free(canon_ref_chain *r)
{
    if (r->levels != NULL) {
        for (uint32_t i = 0; i < r->n; ++i) {
            free(r->levels[i].orbit);
            free(r->levels[i].pos);
            free(r->levels[i].trans);
            free(r->levels[i].trans_inv);
        }
    }
    free(r->levels);
    r->levels = NULL;
    r->order = 0;
}

canon_status canon_ref_build(uint32_t n, const uint32_t *gens, size_t count, canon_ref_chain *out)
{
    out->n = n;
    out->order = 1;
    out->levels = NULL;
    canon_status st = CANON_COMPLETE;
    out->levels = canon_alloc_array(n, sizeof *out->levels, &st);
    if (out->levels == NULL) {
        return st;
    }
    memset(out->levels, 0, (size_t)(n > 0 ? n : 1) * sizeof *out->levels);
    /* S: the current level's generators, with room for their inverses */
    size_t s_count = n > 0 ? count : 0, words = 0;
    if (!canon_size_mul3(s_count, 2u, n, &words)) {
        canon_ref_free(out);
        return CANON_CAPACITY_LIMIT;
    }
    uint32_t *S = canon_alloc_array(words, sizeof *S, &st);
    if (S == NULL) {
        canon_ref_free(out);
        return st;
    }
    if (s_count > 0) {
        memcpy(S, gens, s_count * n * sizeof *S);
    }
    for (uint32_t i = 0; i < n && st == CANON_COMPLETE; ++i) {
        canon_ref_level *L = &out->levels[i];
        /* "include inverses, remove identities/duplicates, sort image arrays" */
        for (size_t k = 0; k < s_count; ++k) {
            canon_perm_inverse(S + k * n, S + (s_count + k) * n, n);
        }
        size_t sc = 0;
        st = normalise(S, 2 * s_count, n, &sc);
        if (st != CANON_COMPLETE) {
            break;
        }
        L->gen_count = (uint32_t)sc;
        /* "build the orbit of a by queue traversal ... retaining t_b with a^t_b=b" */
        size_t tw = 0;
        if (!canon_size_mul((size_t)n, (size_t)n, &tw)) {
            st = CANON_CAPACITY_LIMIT;
            break;
        }
        L->orbit = canon_alloc_array(n, sizeof *L->orbit, &st);
        L->pos = canon_alloc_array(n, sizeof *L->pos, &st);
        L->trans = canon_alloc_array(tw, sizeof *L->trans, &st);
        L->trans_inv = canon_alloc_array(tw, sizeof *L->trans_inv, &st);
        if (L->orbit == NULL || L->pos == NULL || L->trans == NULL || L->trans_inv == NULL) {
            break;
        }
        for (uint32_t v = 0; v < n; ++v) {
            L->pos[v] = REF_NONE;
            L->trans[v] = v; /* t_i = identity */
        }
        L->orbit[0] = i;
        L->pos[i] = 0;
        uint32_t len = 1;
        for (uint32_t head = 0; head < len; ++head) {
            for (size_t k = 0; k < sc; ++k) {
                const uint32_t *s = S + k * n;
                uint32_t y = s[L->orbit[head]];
                if (L->pos[y] == REF_NONE) {
                    L->orbit[len] = y;
                    L->pos[y] = len;
                    /* t_y = t_x s (spec 3: t_x acts first) */
                    canon_perm_compose(L->trans + (size_t)head * n, s, L->trans + (size_t)len * n,
                                       n);
                    ++len;
                }
            }
        }
        L->orbit_len = len;
        for (uint32_t p = 0; p < len; ++p) {
            canon_perm_inverse(L->trans + (size_t)p * n, L->trans_inv + (size_t)p * n, n);
        }
        if (!canon_u64_mul(out->order, len, &out->order)) {
            st = CANON_CAPACITY_LIMIT;
            break;
        }
        /* "For each b and s form t_b s t_(b^s)^-1": the next level's generators */
        size_t next = 0, nw = 0;
        if (!canon_size_mul3((size_t)len * (sc > 0 ? sc : 1), 2u, n, &nw)) {
            st = CANON_CAPACITY_LIMIT;
            break;
        }
        uint32_t *T = canon_alloc_array(nw, sizeof *T, &st);
        uint32_t *tmp = canon_alloc_array(n, sizeof *tmp, &st);
        if (T == NULL || tmp == NULL) {
            free(T);
            free(tmp);
            break;
        }
        for (uint32_t p = 0; p < len; ++p) {
            for (size_t k = 0; k < sc; ++k) {
                const uint32_t *s = S + k * n;
                uint32_t bs = s[L->orbit[p]];
                canon_perm_compose(L->trans + (size_t)p * n, s, tmp, n); /* t_b s */
                canon_perm_compose(tmp, L->trans_inv + (size_t)L->pos[bs] * n, T + next * n,
                                   n); /* (t_b s) t_(b^s)^-1 */
                ++next;
            }
        }
        free(tmp);
        free(S);
        S = T;
        s_count = next; /* "Recurse with the deduplicated nonidentity Schreier generators" */
        st = normalise(S, s_count, n, &s_count);
    }
    if (st == CANON_COMPLETE) {
        /* "finishing with a trivial subgroup": after the last base point nothing moves */
        for (size_t k = 0; k < s_count; ++k) {
            if (!canon_perm_is_identity(S + k * n, n)) {
                st = CANON_INTERNAL_ERROR;
            }
        }
    }
    free(S);
    if (st != CANON_COMPLETE) {
        canon_ref_free(out);
    }
    return st;
}

bool canon_ref_contains(const canon_ref_chain *r, const uint32_t *p, uint32_t *scratch)
{
    const uint32_t n = r->n;
    if (n > 0) {
        memcpy(scratch, p, (size_t)n * sizeof *scratch);
    }
    for (uint32_t i = 0; i < n; ++i) {
        const canon_ref_level *L = &r->levels[i];
        uint32_t q = L->pos[scratch[i]];
        if (q == REF_NONE) {
            return false;
        }
        const uint32_t *inv = L->trans_inv + (size_t)q * n;
        for (uint32_t v = 0; v < n; ++v) {
            scratch[v] = inv[scratch[v]]; /* g <- g t_b^-1: now g fixes i */
        }
    }
    return canon_perm_is_identity(scratch, n);
}
