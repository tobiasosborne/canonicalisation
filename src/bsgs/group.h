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

#include "arena/refcount.h"
#include "canon/canon.h"

typedef struct canon_group_ops {
    /* Free the backend state `impl` (called once, when the last reference is released; the
     * handle itself is freed by src/bsgs/group.c). */
    void (*destroy)(void *impl);
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

/* The opaque public handle (spec section 17).  Create it only with canon_group_alloc, which
 * establishes the reference-count invariant (one reference, owned by the creator); the fields
 * below are read-only after creation.  The count is bookkeeping, reached through `refs` so that
 * a const handle can be shared and retained without a cast (src/arena/refcount.h); `block` is
 * the allocation that holds the handle, used only to free it. */
struct canon_group {
    const canon_group_ops *ops;
    uint32_t degree;
    void *impl;                   /* backend state, freed by ops->destroy */
    canon_refcount *refs;         /* = &refs_storage */
    void *block;                  /* = this handle's allocation */
    canon_refcount refs_storage;  /* the count itself; access only through refs */
};

/* Allocate a group handle for a backend: ops, degree and impl are stored, the count starts at
 * one.  On CANON_RESOURCE_LIMIT *out is NULL and impl is NOT freed (the backend still owns
 * it). */
canon_status canon_group_alloc(const canon_group_ops *ops, uint32_t degree, void *impl,
                               canon_group **out);

/* Add a reference through a const handle (spec 17: immutable groups can be shared). */
void canon_group_share(const canon_group *group);
/* Drop a reference through a const handle; the last one frees the backend and the handle.
 * NULL is a no-op. */
void canon_group_unshare(const canon_group *group);

#endif /* CANON_SRC_BSGS_GROUP_H */
