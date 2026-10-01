/* Ordered partitions (spec sections 7.1, 10, 11.2). */
#include "partition/partition.h"

#include <stdlib.h>
#include <string.h>

#include "arena/checked.h"
#include "util/sort.h"

/* Snapshot layout (private): [cells, start[0..n] (n + 1 words), lab[0..n-1]]. */
#define SNAP_START 1u
static size_t snap_lab_offset(uint32_t n)
{
    return 1u + (size_t)n + 1u;
}

static void rebuild_index(canon_partition *p)
{
    /* spec 10: maintain lab[pos[v]] = v and the cell index of every point. */
    for (uint32_t i = 0; i < p->cells; ++i) {
        for (uint32_t j = p->start[i]; j < p->start[i + 1]; ++j) {
            p->pos[p->lab[j]] = j;
            p->cell_of[p->lab[j]] = i;
        }
    }
}

void canon_partition_reset(canon_partition *p)
{
    for (uint32_t v = 0; v < p->n; ++v) {
        p->lab[v] = v;
    }
    p->start[0] = 0;
    if (p->n == 0) {
        p->cells = 0; /* spec 7.1: n = 0 has an empty ordered partition */
    } else {
        p->cells = 1;
        p->start[1] = p->n;
    }
    rebuild_index(p);
}

void canon_partition_set_degree(canon_partition *p, uint32_t n)
{
    p->n = n <= p->cap ? n : p->cap; /* precondition n <= cap; clamp defensively */
    canon_partition_reset(p);
}

canon_status canon_partition_init(canon_partition *p, uint32_t cap)
{
    memset(p, 0, sizeof *p);
    /* Allocate at least one entry per array so that no pointer is NULL when cap = 0. */
    size_t entries = cap > 0 ? (size_t)cap : 1, bytes = 0, start_bytes = 0, pair_bytes = 0;
    if (!canon_size_mul(entries, sizeof(uint32_t), &bytes) ||
        !canon_size_mul((size_t)cap + 1u, sizeof(uint32_t), &start_bytes) ||
        !canon_size_mul(entries, sizeof(canon_partition_pair), &pair_bytes)) {
        return CANON_CAPACITY_LIMIT; /* spec 11.1 */
    }
    p->lab = malloc(bytes);
    p->pos = malloc(bytes);
    p->cell_of = malloc(bytes);
    p->start = malloc(start_bytes);
    p->new_start = malloc(start_bytes);
    p->pairs = malloc(pair_bytes);
    p->pairs_tmp = malloc(pair_bytes);
    if (p->lab == NULL || p->pos == NULL || p->cell_of == NULL || p->start == NULL ||
        p->new_start == NULL || p->pairs == NULL || p->pairs_tmp == NULL) {
        canon_partition_free(p);
        return CANON_RESOURCE_LIMIT;
    }
    p->cap = cap;
    p->n = cap;
    canon_partition_reset(p);
    return CANON_COMPLETE;
}

void canon_partition_free(canon_partition *p)
{
    free(p->lab);
    free(p->pos);
    free(p->cell_of);
    free(p->start);
    free(p->new_start);
    free(p->pairs);
    free(p->pairs_tmp);
    memset(p, 0, sizeof *p);
}

/* spec 7.1: classes are ordered by increasing signature.  The stable sort keeps the old member
 * order inside a class; that order is not semantic (spec 7.1) but keeps runs reproducible. */
static int pair_cmp(const void *a, const void *b, void *ctx)
{
    (void)ctx;
    uint32_t ka = ((const canon_partition_pair *)a)->key;
    uint32_t kb = ((const canon_partition_pair *)b)->key;
    return ka < kb ? -1 : (ka > kb ? 1 : 0);
}

/* spec 7.1: split(P, sig). */
bool canon_partition_split(canon_partition *p, const uint32_t *sig)
{
    uint32_t old_cells = p->cells, out = 0;
    for (uint32_t i = 0; i < old_cells; ++i) {
        uint32_t lo = p->start[i], hi = p->start[i + 1];
        for (uint32_t j = lo; j < hi; ++j) {
            p->pairs[j].key = sig[p->lab[j]];
            p->pairs[j].member = p->lab[j];
        }
        /* spec 7.1: classes in increasing signature order, in the old cell's position */
        canon_stable_sort(p->pairs + lo, hi - lo, sizeof *p->pairs, p->pairs_tmp, pair_cmp, NULL);
        p->new_start[out++] = lo;
        for (uint32_t j = lo; j < hi; ++j) {
            p->lab[j] = p->pairs[j].member;
            if (j > lo && p->pairs[j].key != p->pairs[j - 1].key) {
                p->new_start[out++] = j; /* nonempty classes only */
            }
        }
    }
    p->new_start[out] = p->n;
    memcpy(p->start, p->new_start, ((size_t)out + 1u) * sizeof *p->start);
    p->cells = out;
    rebuild_index(p);
    return out > old_cells;
}

/* spec 7.1: "replace cell C in place by [{a}, C minus {a}]". */
void canon_partition_individualise(canon_partition *p, uint32_t cell, uint32_t a)
{
    uint32_t lo = p->start[cell];
    /* Move a to the front of its cell, keeping the remaining members' relative order. */
    for (uint32_t j = p->pos[a]; j > lo; --j) {
        p->lab[j] = p->lab[j - 1];
    }
    p->lab[lo] = a;
    /* Insert the boundary lo + 1 after cell `cell`; later cells shift one position right. */
    for (uint32_t i = p->cells + 1; i > cell + 1; --i) {
        p->start[i] = p->start[i - 1];
    }
    p->start[cell + 1] = lo + 1;
    p->cells += 1;
    rebuild_index(p);
}

canon_status canon_partition_snapshot_words(uint32_t n, size_t *words)
{
    size_t w = 0;
    if (!canon_size_mul((size_t)n, 2u, &w) || !canon_size_add(w, 2u, words)) {
        return CANON_CAPACITY_LIMIT;
    }
    return CANON_COMPLETE;
}

void canon_partition_save(const canon_partition *p, uint32_t *snap)
{
    snap[0] = p->cells;
    memcpy(snap + SNAP_START, p->start, ((size_t)p->cells + 1u) * sizeof *snap);
    if (p->n > 0) {
        memcpy(snap + snap_lab_offset(p->n), p->lab, (size_t)p->n * sizeof *snap);
    }
}

/* spec 11.2: rollback restores the previous partition exactly. */
void canon_partition_restore(canon_partition *p, const uint32_t *snap)
{
    p->cells = snap[0];
    memcpy(p->start, snap + SNAP_START, ((size_t)p->cells + 1u) * sizeof *snap);
    if (p->n > 0) {
        memcpy(p->lab, snap + snap_lab_offset(p->n), (size_t)p->n * sizeof *snap);
    }
    rebuild_index(p);
}

const uint32_t *canon_partition_snapshot_lab(const uint32_t *snap, uint32_t n)
{
    return snap + snap_lab_offset(n);
}
