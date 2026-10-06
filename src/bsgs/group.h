/*
 * Internal header: the group interface (spec sections 7.1, 7.2, 9) behind the public opaque
 * canon_group handle.  Implemented in slice S1 by an explicit-enumeration backend
 * (src/bsgs/explicit.h); slice S3 replaces the backend by a verified stabiliser chain behind the
 * same operations.  This header deliberately contains no backend details.
 *
 * Slice S3 changed `contains` to report allocation failure and added `admits` (review items 5
 * and 8).  Slice S4 adds `enumerate` (spec 8.1).  Slice S6 adds `character` (spec 8.4: chi(g)
 * of a signed group, validated by the lifted group) and `conjugate` (spec 3.1: rho^-1 G rho for
 * the labeling objective), and the signed generators of a signed group (canon_group_signs).
 * Slice S7 adds the input generators of every group made by a constructor
 * (canon_group_input_generators), the source (i) of the pruning subgroup A_known (spec 7.3;
 * docs/slices/S7.md 3.1).
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
#include "coset/coset.h"
#include "perm/perm.h"

typedef struct canon_group_ops {
    /* Free the backend state `impl` (called once, when the last reference is released; the
     * handle itself is freed by src/bsgs/group.c). */
    void (*destroy)(void *impl);
    /* spec 9.2: exact group order.  S1 admits orders that fit uint64 only (capacity bound). */
    uint64_t (*order)(const canon_group *group);
    /* spec 9.1: exact membership of the permutation p (a bijection of length `degree`) in
     * *out.  Returns CANON_COMPLETE, or CANON_RESOURCE_LIMIT / CANON_CAPACITY_LIMIT when the
     * backend's per-call scratch cannot be allocated (*out = false); slice S3 review item 5. */
    canon_status (*contains)(const canon_group *group, const uint32_t *p, bool *out);
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
    /* spec 11.1: whether a problem with the resolved capacity descriptor `cap` may use this
     * group (CANON_COMPLETE) or not (CANON_CAPACITY_LIMIT).  Each backend states its own
     * limit (slice S3 review item 8): the explicit backend admits order <= max_group_order;
     * the chain backend admits every group it could build. */
    canon_status (*admits)(const canon_group *group, const canon_capacity *cap);
    /* spec 8.1: the disjoint coset enumerator over the whole group, visit(G, id), with zero
     * pruning, calling visitor->consume once for every element of G in the fixed reference
     * order and counting visits against visitor->quota through canon_coset_visit_enter
     * (src/coset/coset.h).  Slice S4: the chain backend runs src/coset/enumerate.c; the
     * explicit backend runs the same traversal over its element table, as the test oracle.
     * Both visit the same nodes in the same order, so they consume the same sequence and
     * reach CAPACITY_LIMIT at the same point. */
    canon_status (*enumerate)(const canon_group *group, canon_coset_visitor *visitor);
    /* spec 8.4 (slice S6): *sign = chi(g) in {-1, +1} for a member g of a signed group, read
     * from the backend's lifted group L on degree + 2 points (canon_group_lift_element): chi(g)
     * = +1 iff the lift of g with sign +1 (both markers fixed) is in L, -1 iff the lift with
     * sign -1 (markers swapped) is.  L projects onto G, and the elements of L over g are
     * exactly these two extensions, so g is in G iff one of them is in L; by the validation at
     * creation (|L| = |G|) at most one is.  g must be a bijection of {0..degree-1} (the caller
     * validates).  `scratch` is NULL (per-call allocation) or canon_group_character_words(
     * degree) words owned by the caller (no allocation; the hot path of the signed consumer).
     * CANON_UNSUPPORTED_ACTION for an unsigned group, CANON_INVALID_INPUT if g is not in G
     * (*sign = 0 on every failure), CANON_RESOURCE_LIMIT / CANON_CAPACITY_LIMIT when scratch
     * cannot be allocated. */
    canon_status (*character)(const canon_group *group, const uint32_t *g, uint32_t *scratch,
                              int *sign);
    /* spec 3.1 (slice S6): *out = a new unsigned group handle of the same backend for
     * g^-1 G g, the conjugate by the permutation g of {0..degree-1}: as a set,
     * {g^-1 h g : h in G}, where (g^-1 h g)[g[v]] = g[h[v]] (spec 3: g^-1 acts first).  The
     * chain backend relabels its verified chain (canon_bsgs_conjugate, no rebuild); the
     * explicit backend conjugates and re-sorts its element table.  On failure *out is NULL;
     * CANON_RESOURCE_LIMIT / CANON_CAPACITY_LIMIT on allocation failure. */
    canon_status (*conjugate)(const canon_group *group, const uint32_t *g, canon_group **out);
} canon_group_ops;

/* spec 8.4 (slice S6): the generators of a signed group as given and their signs chi(g_i), kept
 * by the handle for the generator fast path of spec 7.3 ("Signed mode in this case tests chi on
 * generators").  Shared by both backends; each backend keeps its own lift for `character`. */
typedef struct canon_group_signs {
    canon_perm_table gens; /* the input generators in input order (degree n; rows not read for
                              n = 0) */
    int8_t *signs;         /* gens.count entries, each -1 or +1 */
} canon_group_signs;

/* The opaque public handle (spec section 17).  Create it only with canon_group_alloc, which
 * establishes the reference-count invariant (one reference, owned by the creator); the fields
 * below are read-only after creation.  The count is bookkeeping, reached through `refs` so that
 * a const handle can be shared and retained without a cast (src/arena/refcount.h); `block` is
 * the allocation that holds the handle, used only to free it. */
struct canon_group {
    const canon_group_ops *ops;
    uint32_t degree;
    void *impl;                   /* backend state, freed by ops->destroy */
    canon_group_signs *signs;     /* S6: NULL for an unsigned group; owned, freed with the
                                     handle (canon_group_set_signs) */
    canon_perm_table *inputs;     /* S7: the generators as given to an unsigned constructor
                                     (canon_group_set_inputs), identities and repeats kept;
                                     NULL for a signed group (its signs->gens are the same
                                     list) and for a group made by `conjugate`; owned */
    canon_refcount *refs;         /* = &refs_storage */
    void *block;                  /* = this handle's allocation */
    canon_refcount refs_storage;  /* the count itself; access only through refs */
};

/* Allocate a group handle for a backend: ops, degree and impl are stored, the count starts at
 * one.  On CANON_RESOURCE_LIMIT *out is NULL and impl is NOT freed (the backend still owns
 * it). */
canon_status canon_group_alloc(const canon_group_ops *ops, uint32_t degree, void *impl,
                               canon_group **out);

/* spec 8.4 (slice S6): give a handle that was just allocated (and not yet shared) its signed
 * generators: copies the `count` generators of degree `degree` (flat image arrays, not read for
 * degree 0) and their signs.  On failure the handle is unchanged (signs NULL).
 * CANON_RESOURCE_LIMIT / CANON_CAPACITY_LIMIT on allocation failure. */
canon_status canon_group_set_signs(canon_group *group, const uint32_t *gens, size_t count,
                                   const int8_t *signs);

/* Slice S7 (docs/slices/S7.md 3.1 source i): give a handle that was just allocated (and not
 * yet shared) a copy of the `count` generators it was built from, in input order (flat image
 * arrays of length degree, not read for degree 0), identities and repeats KEPT (the pruning
 * code filters them by membership).  On failure the handle is unchanged (inputs NULL).
 * CANON_RESOURCE_LIMIT / CANON_CAPACITY_LIMIT on allocation failure. */
canon_status canon_group_set_inputs(canon_group *group, const uint32_t *gens, size_t count);

/* Slice S7: the generators the group was built from, as given (rows in input order, degree
 * group->degree): the unsigned constructors' copy (canon_group_set_inputs) or a signed group's
 * signs->gens; NULL when none were recorded (a group made by the `conjugate` op).  Both
 * backends record them, so the pruning subgroup is the same whatever the backend. */
const canon_perm_table *canon_group_input_generators(const canon_group *group);

/* spec 8.4 (slice S6): the lift of g with sign s: "each generator acts on Omega as given and
 * swaps the last two points iff its sign is -1".  out (n + 2 entries) receives g on {0..n-1}
 * and, on the markers n (+) and n + 1 (-), the identity for s = +1 or the swap for s = -1.
 * Products: lift(g, s) lift(h, t) = lift(gh, st) ("swaps compose by sign multiplication"), so
 * the map is a homomorphism wherever chi is.  n + 2 must fit uint32 (the caller checks, spec
 * 11.1). */
void canon_group_lift_element(const uint32_t *g, uint32_t n, int sign, uint32_t *out);

/* spec 8.4, 11.1 (slice S6): the lifted generators of a signed group as one flat array of
 * count * (n + 2) images (canon_group_lift_element of generator i with sign signs[i]; for
 * n = 0 the generators are not read).  Returns the array (free with free()), or NULL with
 * *status set: CANON_CAPACITY_LIMIT when n + 2 does not fit uint32 ("the initial
 * lifted-character validator additionally requires n+2 <= 2^32-1, checked before constructing
 * its two sign points") or a size does not fit, CANON_RESOURCE_LIMIT on allocation failure. */
uint32_t *canon_group_lift_generators(uint32_t n, const uint32_t *gens, size_t count,
                                      const int8_t *signs, canon_status *status);

/* spec 4.1, 9.1 (slice S6 review item 4): every generator is a bijection of {0..degree-1}
 * (one scratch bitmap, canon_perm_validate_scratch).  CANON_COMPLETE, CANON_INVALID_INPUT for a
 * non-bijection, or the allocator's status.  Nothing is read for degree 0 or no generators.
 * Shared by both backends. */
canon_status canon_group_validate_generators(uint32_t degree, const uint32_t *gens, size_t count);

/* Membership of an element of degree + 2 points in a backend's lift: `residue` is n + 2 words
 * of scratch the test may overwrite. */
typedef bool (*canon_lift_member_fn)(const void *lift, const uint32_t *p, uint32_t *residue);

/* spec 8.4 (slice S6 review item 5): the one implementation of the `character` op's rule
 * (see canon_group_ops.character): chi(g) = +1 iff lift(g, +1) is a member of the lift, -1 iff
 * lift(g, -1) is, and CANON_INVALID_INPUT (g not in G) if neither.  `lift` is the backend's
 * lift, tested through `member`; NULL means an unsigned group (CANON_UNSUPPORTED_ACTION).
 * `scratch` as for the op (NULL: allocated here).  *sign = 0 on every failure. */
canon_status canon_group_character_by(const canon_group *group, const void *lift,
                                      canon_lift_member_fn member, const uint32_t *g,
                                      uint32_t *scratch, int *sign);

/* Words of caller scratch that `character` accepts for a group of degree n: 2 (n + 2) (the
 * lifted element and the sift residue).  False if the count does not fit size_t. */
bool canon_group_character_words(uint32_t n, size_t *words);

/* Add a reference through a const handle (spec 17: immutable groups can be shared). */
void canon_group_share(const canon_group *group);
/* Drop a reference through a const handle; the last one frees the backend and the handle.
 * NULL is a no-op. */
void canon_group_unshare(const canon_group *group);

#endif /* CANON_SRC_BSGS_GROUP_H */
