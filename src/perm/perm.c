/* Dense permutation primitives (spec section 3).  Implemented in slice S1.
 *
 * Every function states the side it acts on.  Arrays store p[v] = v^p (source to target);
 * (pq)[v] = q[p[v]] (spec section 3). */
#include "perm/perm.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "arena/alloc.h"

/* spec section 3: (pq)[v] = q[p[v]]; p acts first. */
void canon_perm_compose(const uint32_t *p, const uint32_t *q, uint32_t *out, uint32_t n)
{
    for (uint32_t v = 0; v < n; ++v) {
        out[v] = q[p[v]];
    }
}

/* spec section 3: p^-1 maps each target back to its source, out[p[v]] = v. */
void canon_perm_inverse(const uint32_t *p, uint32_t *out, uint32_t n)
{
    for (uint32_t v = 0; v < n; ++v) {
        out[p[v]] = v;
    }
}

/* spec section 3: right action on a list, tuple^p = (p[tuple[0]], p[tuple[1]], ...). */
void canon_perm_apply_tuple(const uint32_t *p, const uint32_t *tuple, uint32_t len, uint32_t *out)
{
    for (uint32_t i = 0; i < len; ++i) {
        out[i] = p[tuple[i]];
    }
}

/* spec section 7.2: image arrays compare numerically lexicographically. */
int canon_perm_lex_compare(const uint32_t *a, const uint32_t *b, uint32_t n)
{
    for (uint32_t v = 0; v < n; ++v) {
        if (a[v] != b[v]) {
            return a[v] < b[v] ? -1 : 1;
        }
    }
    return 0;
}

/* spec section 3: the identity fixes every point. */
bool canon_perm_is_identity(const uint32_t *p, uint32_t n)
{
    for (uint32_t v = 0; v < n; ++v) {
        if (p[v] != v) {
            return false;
        }
    }
    return true;
}

/* spec sections 3, 4.1: range check and injectivity (hence bijectivity on a finite set). */
bool canon_perm_validate_scratch(const uint32_t *p, uint32_t n, uint64_t *bitmap)
{
    size_t words = ((size_t)n + 63u) / 64u; /* no overflow: n <= 2^32 - 1 */
    if (words > 0) {
        memset(bitmap, 0, words * sizeof *bitmap);
    }
    for (uint32_t v = 0; v < n; ++v) {
        uint32_t w = p[v];
        if (w >= n) {
            return false; /* spec 4.1: out-of-range target */
        }
        uint64_t bit = (uint64_t)1 << (w % 64u);
        if ((bitmap[w / 64u] & bit) != 0) {
            return false; /* spec 4.1: nonbijection (repeated target) */
        }
        bitmap[w / 64u] |= bit;
    }
    return true;
}

int canon_perm_validate(const uint32_t *p, uint32_t n)
{
    if (n == 0) {
        return 1;
    }
    size_t words = ((size_t)n + 63u) / 64u;
    uint64_t *bitmap = malloc(words * sizeof *bitmap); /* words <= 2^26: no overflow */
    if (bitmap == NULL) {
        return -1;
    }
    int ok = canon_perm_validate_scratch(p, n, bitmap) ? 1 : 0;
    free(bitmap);
    return ok;
}

canon_status canon_perm_check(const uint32_t *p, uint32_t n, bool *ok)
{
    const int v = canon_perm_validate(p, n);
    *ok = v == 1;
    return v < 0 ? CANON_RESOURCE_LIMIT : CANON_COMPLETE;
}

/* ---- dense permutation table (perm.h) ---- */

void canon_perm_table_init(canon_perm_table *t, uint32_t n)
{
    t->n = n;
    t->count = 0;
    t->cap = 0;
    t->data = NULL;
}

void canon_perm_table_free(canon_perm_table *t)
{
    free(t->data);
    t->data = NULL;
    t->count = 0;
    t->cap = 0;
}

canon_status canon_perm_table_push(canon_perm_table *t, const uint32_t *p, uint32_t *index)
{
    size_t row_bytes = 0;
    if (!canon_size_mul((size_t)t->n, sizeof *t->data, &row_bytes)) {
        return CANON_CAPACITY_LIMIT; /* spec 11.1 */
    }
    void *data = t->data;
    canon_status st = canon_grow_array(&data, &t->cap, t->count, 8u, row_bytes);
    t->data = data;
    if (st != CANON_COMPLETE) {
        return st;
    }
    if (t->n > 0) {
        memcpy(t->data + (size_t)t->count * t->n, p, (size_t)t->n * sizeof *p);
    }
    *index = t->count++;
    return CANON_COMPLETE;
}
