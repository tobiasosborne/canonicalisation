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

/* spec 11.2 M_allocator_overhead: a per-block allowance for the system allocator's own chunk
 * bookkeeping and size rounding, charged in addition to what we request from it
 * (sizeof(canon_mem_header) + payload).  32 bytes is a conservative allowance, not a
 * measurement of a specific allocator: common allocators (e.g. glibc malloc) keep one size
 * word per chunk and round the chunk up to the strictest alignment, which for a request of
 * at least our header size costs at most a word plus one alignment quantum, and a minimum
 * chunk size can add the same again for small requests.  Charging a stated upper allowance
 * keeps the ledger an upper accounting of held memory on such allocators; an allocator with
 * larger per-block overhead would need this constant raised. */
#define CANON_MEM_SYSTEM_OVERHEAD ((size_t)32)

static size_t block_overhead(void)
{
    return sizeof(canon_mem_header) + CANON_MEM_SYSTEM_OVERHEAD;
}

uint64_t canon_mem_block_overhead(void)
{
    return (uint64_t)block_overhead();
}

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
    /* every request counts down a pending fault, whether or not it could succeed; the bound
     * also keeps the charged size (payload + overhead) from wrapping */
    if (fault_fires() || bytes > SIZE_MAX - block_overhead()) {
        atomic_fetch_add_explicit(&mem_failures, 1u, memory_order_relaxed);
        return NULL;
    }
    canon_mem_header *h = zero ? calloc(1u, sizeof *h + bytes) : malloc(sizeof *h + bytes);
    if (h == NULL) {
        atomic_fetch_add_explicit(&mem_failures, 1u, memory_order_relaxed);
        return NULL;
    }
    h->size = bytes; /* payload only: realloc/copy semantics and the free path derive the rest */
    /* spec 11.2: charge what is requested (header + payload) plus M_allocator_overhead, so a
     * zero-byte block still charges its header and allowance */
    const uint64_t charged = (uint64_t)bytes + (uint64_t)block_overhead();
    atomic_fetch_add_explicit(&mem_allocations, 1u, memory_order_relaxed);
    raise_peak(atomic_fetch_add_explicit(&mem_live, charged, memory_order_relaxed) + charged);
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
    /* every request counts down a pending fault, whether or not it could succeed; the bound
     * also keeps the charged size (payload + overhead) from wrapping */
    if (fault_fires() || bytes > SIZE_MAX - block_overhead()) {
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
     * everything is released); the per-block overhead is unchanged by a resize, so the
     * charged delta is exactly the payload delta (spec 11.2) */
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
    /* spec 11.2: uncharge payload + header + allowance, exactly what allocate charged */
    atomic_fetch_sub_explicit(&mem_live, (uint64_t)h->size + (uint64_t)block_overhead(),
                              memory_order_relaxed);
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
