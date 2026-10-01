/*
 * Internal header: the group interface (spec sections 7.1, 7.2, 9) behind the public opaque
 * canon_group handle.  Implemented in slice S1 by an explicit-enumeration backend
 * (src/bsgs/explicit.h); slice S3 replaces the backend by a verified stabiliser chain behind the
 * same operations.  This header deliberately contains no backend details.
 *
 * Convention (spec section 3): permutations are dense image arrays p[v] = v^p of length
 * `degree`; lists act on the right, L^g = (g[L[0]], g[L[1]], ...).
 */
#ifndef CANON_SRC_BSGS_GROUP_H
#define CANON_SRC_BSGS_GROUP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "canon/canon.h"

typedef struct canon_group_ops {
    /* Free the backend state (called once, when the last reference is released). */
    void (*destroy)(canon_group *group);
    /* spec 9.2: exact group order.  S1 admits orders that fit uint64 only (capacity bound). */
    uint64_t (*order)(const canon_group *group);
    /* spec 9.1: exact membership of the permutation p (length `degree`). */
    bool (*contains)(const canon_group *group, const uint32_t *p);
    /* spec 7.1 (G stage) and 7.2 (leaf map): t_out receives an element t of G minimising L^t
     * numerically lexicographically among all elements of G; when several do (L not a full
     * list), the least such t as an image array.  If orbit_id_out is not NULL it receives, for
     * every point v of the domain, the index of the orbit of the pointwise stabiliser G_M of
     * M = L^t containing v, after the spec 7.1 ordering: each orbit's points sorted
     * increasingly, then the orbit lists sorted lexicographically (orbit_id ranks the orbits
     * by their least point).  The ids are on TARGET labels; a caller pulls them back through
     * t^-1 by reading orbit_id[t[v]].  Entries of L must be < degree.  Returns
     * CANON_COMPLETE, or CANON_INVALID_INPUT for an out-of-range list entry. */
    canon_status (*tuple_min)(const canon_group *group, const uint32_t *L, uint32_t len,
                              uint32_t *t_out, uint32_t *orbit_id_out);
} canon_group_ops;

/* The opaque public handle (spec section 17).  `refs` is managed by src/api. */
struct canon_group {
    const canon_group_ops *ops;
    uint32_t degree;
    size_t refs;
    void *impl; /* backend state */
};

#endif /* CANON_SRC_BSGS_GROUP_H */
