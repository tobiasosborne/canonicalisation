/*
 * Internal header: atomic reference counts for the opaque handles of spec section 17
 * ("Opaque retain/release handles ... immutable contexts/registries/groups can be shared").
 * Slice S1 review item 2.
 *
 * Design.  A handle's reference count is bookkeeping, not logical state: retaining or
 * releasing an immutable group or object does not change the value it denotes, so the public
 * API may retain through a const pointer (canon_problem_create takes const group and object).
 * C has no `mutable`, so each handle stores its count out of line and keeps a pointer to it:
 * through a const handle the pointer member is const, but the count it points to is not, and
 * no cast is needed.  The count lives in the same allocation as the handle (canon_refcount
 * storage member), so there is no extra allocation or failure path.  The count is _Atomic, so
 * retain/release are safe from several threads sharing a handle; everything else about an
 * immutable handle is read-only after creation.
 */
#ifndef CANON_SRC_ARENA_REFCOUNT_H
#define CANON_SRC_ARENA_REFCOUNT_H

#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>

typedef struct canon_refcount {
    _Atomic size_t count;
} canon_refcount;

/* Start at one reference (the creator's). */
static inline void canon_ref_init(canon_refcount *ref)
{
    atomic_init(&ref->count, (size_t)1);
}

/* Add a reference.  Relaxed suffices: the caller already holds a reference. */
static inline void canon_ref_retain(canon_refcount *ref)
{
    atomic_fetch_add_explicit(&ref->count, (size_t)1, memory_order_relaxed);
}

/* Drop a reference; true when it was the last one and the handle must be freed.  acq_rel
 * orders every use of the handle before its destruction. */
static inline bool canon_ref_release(canon_refcount *ref)
{
    return atomic_fetch_sub_explicit(&ref->count, (size_t)1, memory_order_acq_rel) == 1;
}

#endif /* CANON_SRC_ARENA_REFCOUNT_H */
