/*
 * Internal header: the direct Schreier recursion of spec 9.1 (first paragraph), a correctness
 * reference for small groups used only by tests (slice S3, docs/slices/S3.md 1; detailed plan
 * WP2.2).  It is exponential in general and is not used by the library.
 */
#ifndef CANON_SRC_BSGS_REFERENCE_H
#define CANON_SRC_BSGS_REFERENCE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "canon/canon.h"

typedef struct canon_ref_level {
    uint32_t gen_count;   /* |S_i| after closing under inverses, dedup, identity removal */
    uint32_t orbit_len;
    uint32_t *orbit;      /* n entries: queue order, orbit[0] = i */
    uint32_t *pos;        /* n entries: position in orbit or UINT32_MAX */
    uint32_t *trans;      /* orbit_len * n: t_b for b = orbit[p], with i^(t_b) = b */
    uint32_t *trans_inv;  /* orbit_len * n: t_b^-1 */
} canon_ref_level;

typedef struct canon_ref_chain {
    uint32_t n;
    canon_ref_level *levels; /* n levels, base point of level i is i (ordered base 0..n-1) */
    uint64_t order;          /* product of the orbit lengths */
} canon_ref_chain;

/* spec 9.1: build the reference chain of <gens> (count image arrays of length n, bijections).
 * CANON_CAPACITY_LIMIT if the order or a size overflows, CANON_RESOURCE_LIMIT on allocation
 * failure, CANON_INTERNAL_ERROR if the last level is not trivial (cannot happen by the
 * Schreier generation lemma).  On failure *out is empty. */
canon_status canon_ref_build(uint32_t n, const uint32_t *gens, size_t count, canon_ref_chain *out);

/* Membership by sifting through the dense transversals; `scratch` holds n entries. */
bool canon_ref_contains(const canon_ref_chain *r, const uint32_t *p, uint32_t *scratch);

void canon_ref_free(canon_ref_chain *r);

#endif /* CANON_SRC_BSGS_REFERENCE_H */
