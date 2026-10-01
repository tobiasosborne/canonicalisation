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
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

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

/* Grow-only array of uint32-indexed elements, general form (slice S5: an arena appends k
 * elements at once): make *cap >= need by repeated saturating doubling from at least min_cap
 * (canon_u32_grow), allocate the new block through canon_alloc_array, copy the first `used`
 * elements (used <= *cap) and free the old block.  Nothing happens when need <= *cap.
 * CANON_CAPACITY_LIMIT when need exceeds UINT32_MAX (no uint32 index is left) or the byte size
 * does not fit, CANON_RESOURCE_LIMIT when allocation fails; on failure *data and *cap are
 * unchanged (spec 17).  elem_bytes may be 0 (rows of degree 0).  With used = 0 this reserves
 * scratch whose contents need not be kept. */
static inline canon_status canon_grow_array_to(void **data, uint32_t *cap, uint32_t used,
                                               uint64_t need, uint32_t min_cap,
                                               size_t elem_bytes)
{
    if (*data != NULL && need <= *cap) {
        return CANON_COMPLETE;
    }
    if (need > UINT32_MAX) {
        return CANON_CAPACITY_LIMIT; /* spec 11.1: ids are uint32 */
    }
    uint32_t new_cap = *cap;
    while (new_cap < need || new_cap == 0) {
        new_cap = canon_u32_grow(new_cap, min_cap > 0 ? min_cap : 1u); /* saturates */
    }
    canon_status st = CANON_COMPLETE;
    void *grown = canon_alloc_array(new_cap, elem_bytes > 0 ? elem_bytes : 1u, &st);
    if (grown == NULL) {
        return st;
    }
    if (used > 0 && elem_bytes > 0) {
        memcpy(grown, *data, (size_t)used * elem_bytes); /* fits: the old block held it */
    }
    free(*data);
    *data = grown;
    *cap = new_cap;
    return CANON_COMPLETE;
}

/* Grow-only array of uint32-indexed elements (slice S3 review item 6): make *cap > count,
 * keeping the first `count` elements; canon_grow_array_to with used = count and need =
 * count + 1.  Nothing happens when count < *cap.  CANON_CAPACITY_LIMIT for count == UINT32_MAX
 * (no uint32 index is left) or a byte size that does not fit, CANON_RESOURCE_LIMIT when
 * allocation fails; on failure *data and *cap are unchanged (spec 17).  elem_bytes may be 0. */
static inline canon_status canon_grow_array(void **data, uint32_t *cap, uint32_t count,
                                            uint32_t min_cap, size_t elem_bytes)
{
    if (count < *cap) {
        return CANON_COMPLETE;
    }
    if (count == UINT32_MAX) {
        return CANON_CAPACITY_LIMIT; /* spec 11.1: ids are uint32 */
    }
    return canon_grow_array_to(data, cap, count, (uint64_t)count + 1u, min_cap, elem_bytes);
}

#endif /* CANON_SRC_ARENA_ALLOC_H */
