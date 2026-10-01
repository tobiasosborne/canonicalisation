/*
 * Internal header (not installed): dense permutation primitives.
 * Implemented in slice S1 (docs/slices/S1.md section 4.1).  Governing text:
 * docs/specification.md section 3.
 *
 * Convention (spec section 3): an array p of length n stores p[v] = v^p, a source-to-target
 * map.  Products act left to right: first p, then q, so
 *     (pq)[v] = q[p[v]].
 * Inversion of a product reverses its factors.
 */
#ifndef CANON_SRC_PERM_PERM_H
#define CANON_SRC_PERM_PERM_H

#include <stdbool.h>
#include <stdint.h>

/* spec section 3: out[v] = q[p[v]] for v < n, i.e. out = pq (p acts first, then q).  p and q
 * must be permutations of {0..n-1}.  `out` has room for n entries and must not alias p or q. */
void canon_perm_compose(const uint32_t *p, const uint32_t *q, uint32_t *out, uint32_t n);

/* spec section 3: out[p[v]] = v for v < n, i.e. out = p^-1.  `out` must not alias p. */
void canon_perm_inverse(const uint32_t *p, uint32_t *out, uint32_t n);

/* spec section 3: right action on a list of points, out[i] = tuple[i]^p = p[tuple[i]] for
 * i < len.  Every tuple entry must be a point of p's domain.  `out` must not alias `tuple`. */
void canon_perm_apply_tuple(const uint32_t *p, const uint32_t *tuple, uint32_t len, uint32_t *out);

/* spec section 7.2: numerical lexicographic comparison of two image arrays of length n.
 * Returns -1, 0 or +1. */
int canon_perm_lex_compare(const uint32_t *a, const uint32_t *b, uint32_t n);

/* spec section 3: true iff p[v] = v for every v < n (true for n = 0). */
bool canon_perm_is_identity(const uint32_t *p, uint32_t n);

/* spec sections 3, 4.1 ("reject ... out-of-range targets or a nonbijection"): true iff p is a
 * bijection of {0..n-1} (every entry < n, no entry repeated).  `bitmap` is caller scratch of
 * (n + 63) / 64 words, overwritten (cleared on entry); no allocation, so a caller validating
 * many permutations allocates it once. */
bool canon_perm_validate_scratch(const uint32_t *p, uint32_t n, uint64_t *bitmap);

/* As canon_perm_validate_scratch with its own bitmap: returns 1 if p is a bijection, 0 if not,
 * and -1 if the bitmap could not be allocated (the caller maps this to CANON_RESOURCE_LIMIT,
 * spec section 17). */
int canon_perm_validate(const uint32_t *p, uint32_t n);

#endif /* CANON_SRC_PERM_PERM_H */
