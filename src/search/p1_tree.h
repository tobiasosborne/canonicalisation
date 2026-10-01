/*
 * Internal header: the unpruned P1 canonical-image search (spec sections 7.1, 7.2, 7.3, 11.1).
 * Implemented in slice S1 (docs/slices/S1.md 4.7) for a top-level subset and generalised in
 * slice S2 (docs/slices/S2.md 3.5) to any root object (src/object/object.h): the leaf acts on
 * the root and encodes the image by kind.
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
#include "object/object.h"
#include "partition/partition.h"
#include "refine/p1.h"

/* Reusable mutable search state (one active owner, spec 17).  Buffers persist across solves and
 * grow on demand; nothing here is ever aliased by a result.  Invariant: when
 * canon_p1_search_run returns (on every status), no member points into the root object or
 * the group of that run (the leaf image's borrowed graph tables are cleared), so the object
 * may be released before the workspace is reused or freed. */
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
    canon_root_image image; /* reusable storage for the leaf image x^t_L (grow-only) */
    canon_buf trace;       /* trace of the current root-to-node path */
    canon_buf leaf_bytes;  /* CDAG-2 bytes of the current leaf image */
    canon_buf best_trace;  /* least key found so far: complete trace ... */
    canon_buf best_bytes;  /* ... CDAG-2 bytes ... */
    uint32_t *best_t;      /* ... and the least witness attaining it */
    bool have_best;
    uint64_t nodes;        /* NODE tokens of the reference traversal so far */
    uint64_t leaves;       /* leaves reached in the last run */
    uint64_t images;       /* leaves whose image and stream were materialised (trace <= best) */
} canon_p1_search;

/* Zero state, no allocation. */
void canon_p1_search_init(canon_p1_search *s);
/* Release all storage; valid on an initialised or failed state. */
void canon_p1_search_free(canon_p1_search *s);

/* spec 7.1-7.2: run the unpruned P1 tree for the root x under g (same degree n), keeping the
 * least key (complete trace, CDAG-2 bytes of x^t_L), compared trace first, each by unsigned
 * byte order with a proper prefix smaller; among leaves attaining that key, the
 * lexicographically least t_L (spec 7.4 preamble).  On CANON_COMPLETE the answer is in
 * best_trace, best_bytes and best_t.
 *
 * spec 11.1 logical work quota: if the reference traversal has more than max_nodes NODE
 * tokens the search is abandoned with CANON_CAPACITY_LIMIT; the outcome depends only on the
 * input and max_nodes.  Allocation failure is CANON_RESOURCE_LIMIT.  A degree mismatch between
 * g and x is CANON_INVALID_INPUT (module contract; the API checks it at problem creation). */
canon_status canon_p1_search_run(canon_p1_search *s, const canon_group *g, const canon_root *x,
                                 uint64_t max_nodes);

#endif /* CANON_SRC_SEARCH_P1_TREE_H */
