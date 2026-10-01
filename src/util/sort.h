/*
 * Internal header: one stable merge sort shared by the modules (slice S1 review item 5).
 * Used for the group element table (src/bsgs/explicit.c, rows by image array, spec 7.2) and
 * for partition cells (src/partition/partition.c, members by signature, spec 7.1).
 * Ordering decisions belong to the callers' comparators; this file only sorts.
 */
#ifndef CANON_SRC_UTIL_SORT_H
#define CANON_SRC_UTIL_SORT_H

#include <stddef.h>

/* Comparator: negative, zero or positive as *a orders before, with, or after *b. */
typedef int (*canon_sort_cmp)(const void *a, const void *b, void *ctx);

/* Stable bottom-up merge sort of `count` elements of `size` bytes at `base`.  `tmp` must hold
 * count * size bytes (the caller computed that product with overflow checks before allocating
 * it, spec 11.1) and must not overlap `base`.  Equal elements keep their input order.  No
 * allocation; O(count log count) comparisons. */
void canon_stable_sort(void *base, size_t count, size_t size, void *tmp, canon_sort_cmp cmp,
                       void *ctx);

#endif /* CANON_SRC_UTIL_SORT_H */
