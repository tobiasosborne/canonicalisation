/* Dense permutation primitives (spec section 3).  Implemented in slice S1.
 *
 * Every function states the side it acts on.  Arrays store p[v] = v^p (source to target);
 * (pq)[v] = q[p[v]] (spec section 3). */
#include "perm/perm.h"

#include <stddef.h>
#include <stdlib.h>

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
int canon_perm_validate(const uint32_t *p, uint32_t n)
{
    if (n == 0) {
        return 1;
    }
    /* spec section 11.1: size computed without overflow: ceil(n/64) words, n <= 2^32 - 1. */
    size_t words = ((size_t)n + 63u) / 64u;
    uint64_t *seen = calloc(words, sizeof *seen);
    if (seen == NULL) {
        return -1;
    }
    int ok = 1;
    for (uint32_t v = 0; v < n; ++v) {
        uint32_t w = p[v];
        if (w >= n) {
            ok = 0; /* spec 4.1: out-of-range target */
            break;
        }
        uint64_t bit = (uint64_t)1 << (w % 64u);
        if ((seen[w / 64u] & bit) != 0) {
            ok = 0; /* spec 4.1: nonbijection (repeated target) */
            break;
        }
        seen[w / 64u] |= bit;
    }
    free(seen);
    return ok;
}
