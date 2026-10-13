/*
 * Internal header: the live-memory ledger and the one allocator of the library (spec 11.1
 * "All sizes/products are checked before allocation", 11.2 live-state equation; slice S8,
 * docs/slices/S8.md 1.3, 1.6; detailed plan WP4.8, WP5.1).
 *
 * Every allocation the library makes goes through canon_mem_malloc / calloc / realloc / free,
 * so that M_live of spec 11.2 is measured rather than estimated: each block carries its size in
 * a header, and the process-wide counters (live bytes, peak bytes, allocations, frees) are
 * atomic.  The ledger is process-wide (decision D17): several workspaces on several threads
 * aggregate into the same counters; a per-solve peak (canon_mem_peak_reset at solve entry,
 * canon_mem_stats at exit) is exact when one solve runs at a time.
 *
 * Fault injection (WP4.8): canon_mem_fail_after(k) makes the k-th allocation from now return
 * NULL as if malloc had failed, once; tests use it to show that every allocation failure leaves
 * a releasable state or a clean status (spec 17).  Allocation failures, injected or real, are
 * counted.  Nothing here is thread-local; the fault countdown is atomic too.
 */
#ifndef CANON_SRC_ARENA_MEM_H
#define CANON_SRC_ARENA_MEM_H

#include <stddef.h>
#include <stdint.h>

#include "canon/canon.h"

/* malloc(bytes) with the ledger; NULL on failure or an injected fault (bytes may be 0: a block
 * is still returned, like the checked allocator's one-element convention). */
void *canon_mem_malloc(size_t bytes);
/* calloc(count, size) with the ledger; the product is checked (NULL if it overflows). */
void *canon_mem_calloc(size_t count, size_t size);
/* realloc(p, bytes) with the ledger: on failure p is unchanged and NULL is returned (spec 17:
 * the old state is kept); p NULL is canon_mem_malloc. */
void *canon_mem_realloc(void *p, size_t bytes);
/* free(p); p NULL is a no-op.  p must come from this allocator. */
void canon_mem_free(void *p);

/* The process-wide counters (spec 11.2; canon_memory_stats in the public header). */
void canon_mem_stats(canon_memory_stats *out);
/* Set the peak to the current live count and return that count (the start of a per-solve
 * high-water mark). */
uint64_t canon_mem_peak_reset(void);
/* Fault injection: the k-th allocation request (malloc, calloc or realloc) from now fails,
 * once; k = 0 cancels a pending fault.  Returns the number of faults injected so far. */
uint64_t canon_mem_fail_after(uint64_t k);
/* The number of allocation requests so far (malloc, calloc and realloc, successful or not):
 * the unit canon_mem_fail_after counts in. */
uint64_t canon_mem_requests(void);

#endif /* CANON_SRC_ARENA_MEM_H */
