/*
 * Internal header: overflow-checked size arithmetic (spec section 11.1: "All sizes/products are
 * checked before allocation; no wraparound or saturated arithmetic has semantic meaning").
 * Added in slice S1; the admission planner and memory ledger of spec 11 arrive in slice S8.
 */
#ifndef CANON_SRC_ARENA_CHECKED_H
#define CANON_SRC_ARENA_CHECKED_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* *out = a * b; false (and *out untouched) if the product does not fit size_t. */
static inline bool canon_size_mul(size_t a, size_t b, size_t *out)
{
    if (b != 0 && a > SIZE_MAX / b) {
        return false;
    }
    *out = a * b;
    return true;
}

/* *out = a + b; false (and *out untouched) if the sum does not fit size_t. */
static inline bool canon_size_add(size_t a, size_t b, size_t *out)
{
    if (a > SIZE_MAX - b) {
        return false;
    }
    *out = a + b;
    return true;
}

/* *out = a * b * c, checked. */
static inline bool canon_size_mul3(size_t a, size_t b, size_t c, size_t *out)
{
    size_t ab;
    return canon_size_mul(a, b, &ab) && canon_size_mul(ab, c, out);
}

#endif /* CANON_SRC_ARENA_CHECKED_H */
