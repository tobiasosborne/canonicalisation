/*
 * Internal header (not installed): permutation primitives.  DECLARED ONLY; implemented in M2
 * (docs/implementation-plan.md).  Governing text: docs/specification.md section 3.
 *
 * Convention (spec section 3): an array p of length n stores p[v] = v^p, a source-to-target
 * map.  Products act left to right: first p, then q, so
 *     (pq)[v] = q[p[v]].
 * Inversion of a product reverses its factors.
 */
#ifndef CANON_SRC_PERM_PERM_H
#define CANON_SRC_PERM_PERM_H

#include <stdint.h>

/* spec section 3: out[v] = q[p[v]] for v < n, i.e. out = pq.  p and q must be permutations of
 * {0..n-1}.  `out` has room for n entries and must not alias p or q. */
void canon_perm_compose(const uint32_t *p, const uint32_t *q, uint32_t *out, uint32_t n);

/* spec section 3: out[p[v]] = v for v < n, i.e. out = p^-1.  `out` must not alias p. */
void canon_perm_inverse(const uint32_t *p, uint32_t *out, uint32_t n);

#endif /* CANON_SRC_PERM_PERM_H */
