/*
 * Internal header: the one checked array allocator (slice S2 review item 7).
 *
 * Convention, stated once: an array of `count` elements of `size` bytes is allocated as
 * max(count, 1) elements, so that a successful allocation is never NULL even for an empty
 * array.  The byte size is checked before allocation (spec 11.1: "All sizes/products are
 * checked before allocation"): a product that does not fit size_t is CANON_CAPACITY_LIMIT; a
 * failed malloc is CANON_RESOURCE_LIMIT (spec 17: allocator failure leaves the old state, so
 * callers allocate into temporaries and swap on success).
 */
#ifndef CANON_SRC_ARENA_ALLOC_H
#define CANON_SRC_ARENA_ALLOC_H

#include <stddef.h>
#include <stdlib.h>

#include "arena/checked.h"
#include "canon/canon.h"

/* Allocate max(count, 1) * size bytes.  Returns the block, or NULL with *status set to
 * CANON_CAPACITY_LIMIT or CANON_RESOURCE_LIMIT.  *status is left untouched on success, so a
 * caller may chain several allocations and test the status once. */
static inline void *canon_alloc_array(size_t count, size_t size, canon_status *status)
{
    size_t bytes = 0;
    if (!canon_size_mul(count > 0 ? count : 1u, size, &bytes)) {
        *status = CANON_CAPACITY_LIMIT;
        return NULL;
    }
    void *block = malloc(bytes);
    if (block == NULL) {
        *status = CANON_RESOURCE_LIMIT;
    }
    return block;
}

#endif /* CANON_SRC_ARENA_ALLOC_H */
