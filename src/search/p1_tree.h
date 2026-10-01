/*
 * Internal header: the unpruned P1 canonical-image search for a top-level subset
 * (spec sections 7.1, 7.2, 7.3, 11.1).  Implemented in slice S1 (docs/slices/S1.md 4.7).
 *
 * No pruning: spec 7.3 "The unpruned evaluator above is normative".  Pruning with its coverage
 * lemmas arrives in slice S7.
 */
#ifndef CANON_SRC_SEARCH_P1_TREE_H
#define CANON_SRC_SEARCH_P1_TREE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "bsgs/group.h"
#include "canon/canon.h"
#include "encoding/wire.h"
#include "object/subset.h"
#include "partition/partition.h"
#include "refine/p1.h"

/* Reusable mutable search state (one active owner, spec 17).  Buffers persist across solves and
 * grow on demand; nothing here is ever aliased by a result. */
typedef struct canon_p1_search {
    uint32_t n;               /* degree of the current solve, n <= cap */
    uint32_t cap;             /* degree the per-point arrays are allocated for (only grows) */
    bool ready;               /* arrays allocated for cap */
    canon_partition part;     /* current partition (allocated for cap) */
    canon_p1_scratch scratch; /* refinement scratch, cap entries each */
    uint32_t *snaps;          /* snapshot stack: depth d at snaps + d * snap_words */
    size_t snap_words;        /* words per snapshot at the current degree n */
    size_t snap_alloc;        /* words allocated in snaps (only grows) */
    uint32_t *leaf_t;      /* cap: t_L at the current leaf */
    uint32_t *leaf_atoms;  /* cap: sorted image x^t_L */
    uint64_t *leaf_bits;   /* bitset scratch for the image */
    canon_buf trace;       /* trace of the current root-to-node path */
    canon_buf leaf_bytes;  /* CDAG-2 bytes of the current leaf image */
    canon_buf best_trace;  /* least key found so far: complete trace ... */
    canon_buf best_bytes;  /* ... CDAG-2 bytes ... */
    uint32_t *best_t;      /* ... and the least witness attaining it */
    bool have_best;
    uint64_t nodes;        /* NODE tokens of the reference traversal so far */
} canon_p1_search;

/* Zero state, no allocation. */
void canon_p1_search_init(canon_p1_search *s);
/* Release all storage; valid on an initialised or failed state. */
void canon_p1_search_free(canon_p1_search *s);

/* spec 7.1-7.2: run the unpruned P1 tree for the subset x under g (same degree n), keeping the
 * least key (complete trace, CDAG-2 bytes of x^t_L), compared trace first, each by unsigned
 * byte order with a proper prefix smaller; among leaves attaining that key, the
 * lexicographically least t_L (spec 7.4 preamble).  On CANON_COMPLETE the answer is in
 * best_trace, best_bytes and best_t.
 *
 * spec 11.1 logical work quota: if the reference traversal has more than max_nodes NODE
 * tokens the search is abandoned with CANON_CAPACITY_LIMIT; the outcome depends only on the
 * input and max_nodes.  Allocation failure is CANON_RESOURCE_LIMIT. */
canon_status canon_p1_search_subset(canon_p1_search *s, const canon_group *g,
                                    const canon_subset *x, uint64_t max_nodes);

#endif /* CANON_SRC_SEARCH_P1_TREE_H */
