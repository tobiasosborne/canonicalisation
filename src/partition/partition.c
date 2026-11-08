/* Ordered partitions (spec sections 7.1, 10, 11.2). */
#include "partition/partition.h"
#include "arena/mem.h"

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
    p->lab = canon_mem_malloc(bytes);
    p->pos = canon_mem_malloc(bytes);
    p->cell_of = canon_mem_malloc(bytes);
    p->start = canon_mem_malloc(start_bytes);
    p->new_start = canon_mem_malloc(start_bytes);
    p->pairs = canon_mem_malloc(pair_bytes);
    p->pairs_tmp = canon_mem_malloc(pair_bytes);
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
    canon_mem_free(p->lab);
    canon_mem_free(p->pos);
    canon_mem_free(p->cell_of);
    canon_mem_free(p->start);
    canon_mem_free(p->new_start);
    canon_mem_free(p->pairs);
    canon_mem_free(p->pairs_tmp);
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

/* Counting path for one old cell [lo, hi) whose (key, member) pairs already sit in
 * p->pairs[lo..hi) in lab order, with keys in [min_key, min_key + range).  Returns the
 * next free output cell index.  spec 10 licenses count arrays and fixed-width counting
 * ("Signature sorting may use fixed-width radix or comparison sort"); hashes never order
 * the classes: buckets are visited in increasing key order.  Stability: members are
 * scattered in lab (input) order and each key's cursor only advances, so the output
 * ordering by (key, input position) is the same unique ordering the stable merge sort
 * produces; lab, the cell order and the boundaries are bit-identical.  pos/cell_of are
 * written at placement time (spec 10 invariant lab[pos[v]] = v), so split needs no
 * separate rebuild_index pass. */
static uint32_t split_cell_counting(canon_partition *p, uint32_t lo, uint32_t hi,
                                    uint32_t min_key, uint32_t range, uint32_t out)
{
    /* Buckets reuse the fallback's sort scratch (partition.h): .key counts, then places;
     * .member records the output cell index of that key's class. */
    canon_partition_pair *buckets = p->pairs_tmp;
    for (uint32_t t = 0; t < range; ++t) {
        buckets[t].key = 0;
    }
    for (uint32_t j = lo; j < hi; ++j) {
        buckets[p->pairs[j].key - min_key].key += 1u;
    }
    /* Prefix over keys in increasing order (spec 7.1: classes in increasing signature
     * order, in the old cell's position): each nonempty class gets its destination
     * segment and its output cell index. */
    uint32_t cursor = lo;
    for (uint32_t t = 0; t < range; ++t) {
        uint32_t count = buckets[t].key;
        if (count > 0) {
            p->new_start[out] = cursor; /* nonempty classes only */
            buckets[t].member = out;
            buckets[t].key = cursor;
            cursor += count;
            out += 1u;
        }
    }
    /* Stable scatter in input order, maintaining pos/cell_of at placement (spec 10). */
    for (uint32_t j = lo; j < hi; ++j) {
        uint32_t t = p->pairs[j].key - min_key;
        uint32_t dest = buckets[t].key;
        buckets[t].key = dest + 1u;
        uint32_t v = p->pairs[j].member;
        p->lab[dest] = v;
        p->pos[v] = dest;
        p->cell_of[v] = buckets[t].member;
    }
    return out;
}

/* Fallback for one old cell with a sparse key range: the previous algorithm unchanged
 * (stable merge sort of the pairs through canon_stable_sort, boundaries where the key
 * changes), except that pos/cell_of are written during the write-back instead of by a
 * later rebuild_index.  Returns the next free output cell index. */
static uint32_t split_cell_sorted(canon_partition *p, uint32_t lo, uint32_t hi, uint32_t out)
{
    /* spec 7.1: classes in increasing signature order, in the old cell's position */
    canon_stable_sort(p->pairs + lo, hi - lo, sizeof *p->pairs, p->pairs_tmp, pair_cmp, NULL);
    p->new_start[out] = lo;
    uint32_t cell = out;
    out += 1u;
    for (uint32_t j = lo; j < hi; ++j) {
        if (j > lo && p->pairs[j].key != p->pairs[j - 1].key) {
            p->new_start[out] = j; /* nonempty classes only */
            cell = out;
            out += 1u;
        }
        uint32_t v = p->pairs[j].member;
        p->lab[j] = v;
        p->pos[v] = j; /* spec 10: lab[pos[v]] = v */
        p->cell_of[v] = cell;
    }
    return out;
}

/* spec 7.1: split(P, sig).
 *
 * Counting threshold: a cell of size s takes the counting path iff its key range
 * R = max - min + 1 satisfies R <= 4 * s and R fits the bucket scratch (cap pairs,
 * allocated at init; every production caller hands split keys in [0, n), so
 * R <= n <= cap there: initial 0/1 or colour-rank keys, O-stage ranks and G-stage orbit
 * ids in src/refine/p1.c).  Why 4 * s: counting costs two passes over R (zero, prefix)
 * plus the count and scatter passes over s, all plain integer ops, i.e. O(s + R).  With
 * R <= 4 * s that is O(s) with a small constant independent of log s, and summed over
 * the cells the split is O(n).  Beyond 4 * s the zero/prefix passes over a mostly empty
 * range cost more than the merge sort's O(s log s), whose cost does not depend on how
 * sparse the keys are, so the sparse cell falls back to the sort.  The factor 4 is an
 * implementation choice under spec 10, not a spec rule; both paths produce the unique
 * stable ordering, so the choice is unobservable in the output. */
bool canon_partition_split(canon_partition *p, const uint32_t *sig)
{
    uint32_t old_cells = p->cells, out = 0;
    uint32_t buckets_cap = p->cap > 0 ? p->cap : 1u; /* pairs_tmp entries allocated */
    for (uint32_t i = 0; i < old_cells; ++i) {
        uint32_t lo = p->start[i], hi = p->start[i + 1];
        uint32_t min_key = UINT32_MAX, max_key = 0;
        for (uint32_t j = lo; j < hi; ++j) {
            uint32_t v = p->lab[j], key = sig[v];
            p->pairs[j].key = key;
            p->pairs[j].member = v;
            if (key < min_key) {
                min_key = key;
            }
            if (key > max_key) {
                max_key = key;
            }
        }
        if (hi - lo <= 1 || min_key == max_key) {
            /* One class equal to the old cell: the stable order is the input order, so lab
             * and pos are unchanged; only cell_of may need renumbering when an earlier
             * cell split (then out > i).  This is the common final-sweep case. */
            p->new_start[out] = lo;
            if (out != i) {
                for (uint32_t j = lo; j < hi; ++j) {
                    p->cell_of[p->lab[j]] = out;
                }
            }
            out += 1u;
            continue;
        }
        uint64_t range = (uint64_t)max_key - (uint64_t)min_key + 1u; /* no uint32 wrap */
        if (range <= 4u * (uint64_t)(hi - lo) && range <= (uint64_t)buckets_cap) {
            out = split_cell_counting(p, lo, hi, min_key, (uint32_t)range, out);
        } else {
            out = split_cell_sorted(p, lo, hi, out);
        }
    }
    p->new_start[out] = p->n;
    memcpy(p->start, p->new_start, ((size_t)out + 1u) * sizeof *p->start);
    p->cells = out;
    /* No rebuild_index: every path above wrote pos/cell_of at placement (or left both
     * provably unchanged), maintaining the spec 10 invariant directly. */
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
