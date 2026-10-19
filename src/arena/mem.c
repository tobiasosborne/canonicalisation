/* The live-memory ledger and allocator (spec 11.1, 11.2; slice S8). */
#include "arena/mem.h"

#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

/* A block's header: its byte count, padded to the strictest alignment so that the payload is
 * suitably aligned for every object type. */
typedef union canon_mem_header {
    size_t size;
    max_align_t align;
} canon_mem_header;

static _Atomic uint64_t mem_live, mem_peak, mem_allocations, mem_frees, mem_failures;
static _Atomic uint64_t mem_fault_countdown; /* 0: no fault pending; k: fail the k-th */
static _Atomic uint64_t mem_faults_injected;
static _Atomic uint64_t mem_requests;

/* Raise the peak to at least `live` (spec 11.2: "Reclamation lag counts"). */
static void raise_peak(uint64_t live)
{
    uint64_t peak = atomic_load_explicit(&mem_peak, memory_order_relaxed);
    while (live > peak &&
           !atomic_compare_exchange_weak_explicit(&mem_peak, &peak, live, memory_order_relaxed,
                                                  memory_order_relaxed)) {
    }
}

/* true iff an injected fault fires on this allocation. */
static bool fault_fires(void)
{
    uint64_t left = atomic_load_explicit(&mem_fault_countdown, memory_order_relaxed);
    while (left != 0) {
        if (atomic_compare_exchange_weak_explicit(&mem_fault_countdown, &left, left - 1u,
                                                  memory_order_relaxed, memory_order_relaxed)) {
            if (left == 1u) {
                atomic_fetch_add_explicit(&mem_faults_injected, 1u, memory_order_relaxed);
                return true;
            }
            return false;
        }
    }
    return false;
}

static void *allocate(size_t bytes, bool zero)
{
    atomic_fetch_add_explicit(&mem_requests, 1u, memory_order_relaxed);
    /* every request counts down a pending fault, whether or not it could succeed */
    if (fault_fires() || bytes > SIZE_MAX - sizeof(canon_mem_header)) {
        atomic_fetch_add_explicit(&mem_failures, 1u, memory_order_relaxed);
        return NULL;
    }
    canon_mem_header *h = zero ? calloc(1u, sizeof *h + bytes) : malloc(sizeof *h + bytes);
    if (h == NULL) {
        atomic_fetch_add_explicit(&mem_failures, 1u, memory_order_relaxed);
        return NULL;
    }
    h->size = bytes;
    atomic_fetch_add_explicit(&mem_allocations, 1u, memory_order_relaxed);
    raise_peak(atomic_fetch_add_explicit(&mem_live, bytes, memory_order_relaxed) + bytes);
    return h + 1;
}

void *canon_mem_malloc(size_t bytes)
{
    return allocate(bytes, false);
}

void *canon_mem_calloc(size_t count, size_t size)
{
    if (size != 0 && count > SIZE_MAX / size) {
        /* spec 11.1: no wraparound has meaning.  Still a request, so it advances the fault
         * countdown like every request canon_mem_requests counts (S8 independent review). */
        atomic_fetch_add_explicit(&mem_requests, 1u, memory_order_relaxed);
        (void)fault_fires();
        atomic_fetch_add_explicit(&mem_failures, 1u, memory_order_relaxed);
        return NULL;
    }
    return allocate(count * size, true);
}

void *canon_mem_realloc(void *p, size_t bytes)
{
    if (p == NULL) {
        return canon_mem_malloc(bytes);
    }
    canon_mem_header *h = (canon_mem_header *)p - 1;
    const size_t old = h->size;
    atomic_fetch_add_explicit(&mem_requests, 1u, memory_order_relaxed);
    /* every request counts down a pending fault, whether or not it could succeed */
    if (fault_fires() || bytes > SIZE_MAX - sizeof(canon_mem_header)) {
        atomic_fetch_add_explicit(&mem_failures, 1u, memory_order_relaxed);
        return NULL;
    }
    canon_mem_header *grown = realloc(h, sizeof *grown + bytes);
    if (grown == NULL) {
        atomic_fetch_add_explicit(&mem_failures, 1u, memory_order_relaxed);
        return NULL; /* spec 17: the old block is kept */
    }
    grown->size = bytes;
    /* a resize of an existing block: not a new allocation (allocations == frees once
     * everything is released) */
    if (bytes >= old) {
        raise_peak(atomic_fetch_add_explicit(&mem_live, bytes - old, memory_order_relaxed) +
                   (bytes - old));
    } else {
        atomic_fetch_sub_explicit(&mem_live, old - bytes, memory_order_relaxed);
    }
    return grown + 1;
}

void canon_mem_free(void *p)
{
    if (p == NULL) {
        return;
    }
    canon_mem_header *h = (canon_mem_header *)p - 1;
    atomic_fetch_sub_explicit(&mem_live, h->size, memory_order_relaxed);
    atomic_fetch_add_explicit(&mem_frees, 1u, memory_order_relaxed);
    free(h);
}

void canon_mem_stats(canon_memory_stats *out)
{
    out->live_bytes = atomic_load_explicit(&mem_live, memory_order_relaxed);
    out->peak_bytes = atomic_load_explicit(&mem_peak, memory_order_relaxed);
    out->allocations = atomic_load_explicit(&mem_allocations, memory_order_relaxed);
    out->frees = atomic_load_explicit(&mem_frees, memory_order_relaxed);
    out->failures = atomic_load_explicit(&mem_failures, memory_order_relaxed);
}

uint64_t canon_mem_peak_reset(void)
{
    const uint64_t live = atomic_load_explicit(&mem_live, memory_order_relaxed);
    atomic_store_explicit(&mem_peak, live, memory_order_relaxed);
    return live;
}

uint64_t canon_mem_requests(void)
{
    return atomic_load_explicit(&mem_requests, memory_order_relaxed);
}

uint64_t canon_mem_fail_after(uint64_t k)
{
    atomic_store_explicit(&mem_fault_countdown, k, memory_order_relaxed);
    return atomic_load_explicit(&mem_faults_injected, memory_order_relaxed);
}
