/* Ordered partitions (spec sections 7.1, 10, 11.2). */
#include "partition/partition.h"

#include <stdlib.h>
#include <string.h>

#include "arena/checked.h"

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

canon_status canon_partition_init(canon_partition *p, uint32_t n)
{
    memset(p, 0, sizeof *p);
    p->n = n;
    /* Allocate at least one entry per array so that no pointer is NULL when n = 0. */
    size_t entries = n > 0 ? (size_t)n : 1, bytes = 0, start_bytes = 0;
    if (!canon_size_mul(entries, sizeof(uint32_t), &bytes) ||
        !canon_size_mul((size_t)n + 1u, sizeof(uint32_t), &start_bytes)) {
        return CANON_CAPACITY_LIMIT; /* spec 11.1 */
    }
    p->lab = malloc(bytes);
    p->pos = malloc(bytes);
    p->cell_of = malloc(bytes);
    p->key = malloc(bytes);
    p->key_tmp = malloc(bytes);
    p->mem_tmp = malloc(bytes);
    p->start = malloc(start_bytes);
    p->new_start = malloc(start_bytes);
    if (p->lab == NULL || p->pos == NULL || p->cell_of == NULL || p->key == NULL ||
        p->key_tmp == NULL || p->mem_tmp == NULL || p->start == NULL || p->new_start == NULL) {
        canon_partition_free(p);
        p->n = n;
        return CANON_RESOURCE_LIMIT;
    }
    canon_partition_reset(p);
    return CANON_COMPLETE;
}

void canon_partition_free(canon_partition *p)
{
    free(p->lab);
    free(p->pos);
    free(p->cell_of);
    free(p->start);
    free(p->key);
    free(p->key_tmp);
    free(p->mem_tmp);
    free(p->new_start);
    memset(p, 0, sizeof *p);
}

/* Stable bottom-up merge sort of (key[lo..hi), lab[lo..hi)) by key.  Stability is not
 * semantic (member order inside a cell is not, spec 7.1) but keeps runs reproducible. */
static void sort_cell(canon_partition *p, uint32_t lo, uint32_t hi)
{
    uint32_t len = hi - lo;
    uint32_t *k = p->key + lo, *m = p->lab + lo;
    uint32_t *kt = p->key_tmp + lo, *mt = p->mem_tmp + lo;
    for (uint64_t width = 1; width < len; width *= 2) {
        for (uint64_t a = 0; a < len; a += 2 * width) {
            uint32_t mid = (uint32_t)(a + width < len ? a + width : len);
            uint32_t end = (uint32_t)(a + 2 * width < len ? a + 2 * width : len);
            uint32_t i = (uint32_t)a, j = mid, o = (uint32_t)a;
            while (i < mid && j < end) {
                if (k[j] < k[i]) {
                    kt[o] = k[j];
                    mt[o++] = m[j++];
                } else {
                    kt[o] = k[i];
                    mt[o++] = m[i++];
                }
            }
            while (i < mid) {
                kt[o] = k[i];
                mt[o++] = m[i++];
            }
            while (j < end) {
                kt[o] = k[j];
                mt[o++] = m[j++];
            }
        }
        memcpy(k, kt, (size_t)len * sizeof *k);
        memcpy(m, mt, (size_t)len * sizeof *m);
    }
}

/* spec 7.1: split(P, sig). */
bool canon_partition_split(canon_partition *p, const uint32_t *sig)
{
    uint32_t old_cells = p->cells, out = 0;
    for (uint32_t i = 0; i < old_cells; ++i) {
        uint32_t lo = p->start[i], hi = p->start[i + 1];
        for (uint32_t j = lo; j < hi; ++j) {
            p->key[j] = sig[p->lab[j]];
        }
        /* spec 7.1: classes in increasing signature order, in the old cell's position */
        sort_cell(p, lo, hi);
        p->new_start[out++] = lo;
        for (uint32_t j = lo + 1; j < hi; ++j) {
            if (p->key[j] != p->key[j - 1]) {
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
    memcpy(snap + 1, p->start, ((size_t)p->cells + 1u) * sizeof *snap);
    if (p->n > 0) {
        memcpy(snap + 1 + (size_t)p->n + 1u, p->lab, (size_t)p->n * sizeof *snap);
    }
}

/* spec 11.2: rollback restores the previous partition exactly. */
void canon_partition_restore(canon_partition *p, const uint32_t *snap)
{
    p->cells = snap[0];
    memcpy(p->start, snap + 1, ((size_t)p->cells + 1u) * sizeof *snap);
    if (p->n > 0) {
        memcpy(p->lab, snap + 1 + (size_t)p->n + 1u, (size_t)p->n * sizeof *snap);
    }
    rebuild_index(p);
}
