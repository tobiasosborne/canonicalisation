/*
 * Internal header: ordered partitions of {0..n-1} with split, individualisation and
 * snapshot/rollback (spec sections 7.1, 10, 11.2).  Implemented in slice S1
 * (docs/slices/S1.md section 4.5).
 *
 * Layout (spec 10): flat arrays lab, pos and cell_of with lab[pos[v]] = v.  Cells are
 * contiguous ranges of lab laid out in semantic order: cell i is lab[start[i] .. start[i+1]).
 * The order of members inside a cell is not semantic (spec 7.1).
 *
 * Rollback (spec 11.2) is by whole snapshots of (cells, start, lab): O(n) words per saved
 * depth.  The trail/replay variants of spec 11.2 arrive in slice S8.
 */
#ifndef CANON_SRC_PARTITION_PARTITION_H
#define CANON_SRC_PARTITION_PARTITION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "canon/canon.h"

/* (signature, member) pair sorted by split. */
typedef struct canon_partition_pair {
    uint32_t key;
    uint32_t member;
} canon_partition_pair;

typedef struct canon_partition {
    uint32_t n;        /* current domain size, n <= cap */
    uint32_t cap;      /* degree the arrays are allocated for */
    uint32_t cells;    /* number of cells */
    uint32_t *lab;     /* n entries: members, cell by cell in semantic order */
    uint32_t *pos;     /* n entries: lab[pos[v]] = v */
    uint32_t *cell_of; /* n entries: semantic position of the cell containing v */
    uint32_t *start;   /* n + 1 entries: start[i] = first lab index of cell i; start[cells] = n */
    /* split scratch */
    uint32_t *new_start;                          /* n + 1 entries */
    canon_partition_pair *pairs, *pairs_tmp;      /* n entries each */
} canon_partition;

/* Allocate for degrees up to `cap`, set degree cap and the unit partition: one cell holding
 * 0..n-1 in increasing order, or no cell when n = 0 (spec 7.1: "n=0 has an empty ordered
 * partition").  Returns CANON_RESOURCE_LIMIT / CANON_CAPACITY_LIMIT on allocation or size
 * failure; the partition is then still valid to free. */
canon_status canon_partition_init(canon_partition *p, uint32_t cap);
/* Release storage; valid after a failed init or on a zeroed struct. */
void canon_partition_free(canon_partition *p);

/* Switch to degree n <= cap (no allocation) and reset to the unit partition. */
void canon_partition_set_degree(canon_partition *p, uint32_t n);

/* Reset to the unit partition of the current degree (no allocation). */
void canon_partition_reset(canon_partition *p);

/* spec 7.1: split(P, sig) replaces each old cell, in its old position, by its nonempty
 * signature classes in increasing signature order; old cells are never reordered or merged.
 * sig[v] is the uint32 signature of point v.  Returns true iff the number of cells grew. */
bool canon_partition_split(canon_partition *p, const uint32_t *sig);

/* spec 7.1: replace cell C (semantic position `cell`) in place by [{a}, C minus {a}].
 * Requires cell_of[a] == cell and |C| > 1. */
void canon_partition_individualise(canon_partition *p, uint32_t cell, uint32_t a);

/* Size of the cell at semantic position i. */
static inline uint32_t canon_partition_cell_size(const canon_partition *p, uint32_t i)
{
    return p->start[i + 1] - p->start[i];
}

/* true iff every cell is a singleton (true for n = 0). */
static inline bool canon_partition_is_discrete(const canon_partition *p)
{
    return p->cells == p->n;
}

/* Number of uint32 words of a snapshot of a degree-n partition: 1 + (n + 1) + n. */
canon_status canon_partition_snapshot_words(uint32_t n, size_t *words);
/* Save the exact state (cells, start, lab) into snap. */
void canon_partition_save(const canon_partition *p, uint32_t *snap);
/* Restore exactly the state saved by canon_partition_save (spec 11.2 rollback). */
void canon_partition_restore(canon_partition *p, const uint32_t *snap);
/* The member order `lab` (n entries) stored in a snapshot of a degree-n partition.  The
 * snapshot layout is private to partition.c; read it only through this accessor. */
const uint32_t *canon_partition_snapshot_lab(const uint32_t *snap, uint32_t n);

#endif /* CANON_SRC_PARTITION_PARTITION_H */
