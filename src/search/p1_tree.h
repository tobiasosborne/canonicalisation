/*
 * Internal header: the P1 canonical-image search (spec sections 7.1, 7.2, 7.3, 11.1).
 * Implemented in slice S1 (docs/slices/S1.md 4.7) for a top-level subset and generalised in
 * slice S2 (docs/slices/S2.md 3.5) to any root object (src/object/object.h): the leaf acts on
 * the root and encodes the image by kind.
 *
 * spec 7.3 "The unpruned evaluator above is normative": canon_p1_search_run is that evaluator.
 * Slice S7 step 1 adds ONE optimisation with a coverage lemma, orbit pruning by verified
 * automorphisms fixing the node's prefix (docs/pruning-rules.md; docs/slices/S7.md 3.1),
 * selected by work policy 0x0002 through canon_p1_search_run_policy.  Only the canonical-image
 * solve passes that policy (src/api/api.c); the labeling and signed objectives call
 * canon_p1_search_run and stay unpruned (spec 8.2).
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
#include "symmetry/symmetry.h"

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
    uint64_t nodes;        /* NODE tokens of the traversal the work policy fixes, so far: the
                              nodes explored (spec 11.1; S7: no count of pruned subtrees) */
    uint64_t leaves;       /* leaves reached in the last run */
    uint64_t images;       /* leaves whose image and stream were materialised (trace <= best) */
    /* S7 (docs/pruning-rules.md): the work policy of the last run (1 or 2), the children it
     * skipped by orbit pruning, A_known with its prefix-stabiliser stack (its stats count the
     * automorphisms inserted and the rebases; zero under policy 1), and a stack of the explored
     * children of the nodes on the current path (grow-only). */
    canon_work_policy work_policy;
    uint64_t pruned;
    uint64_t children; /* children of the explored internal nodes (explored or pruned): on a
                          complete run nodes = 1 + children - pruned (every child is entered or
                          skipped by the rule, none is lost) */
    canon_symmetry known;
    uint32_t *reps;
    size_t reps_top, reps_cap;
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
 * g and x is CANON_INVALID_INPUT (module contract; the API checks it at problem creation).
 * This is the unpruned reference traversal (work policy 0x0001). */
canon_status canon_p1_search_run(canon_p1_search *s, const canon_group *g, const canon_root *x,
                                 uint64_t max_nodes);

/* spec 11.1 v2.1 (slice S7): as canon_p1_search_run under the work policy `work_policy`.
 * CANON_WORK_POLICY_REFERENCE: exactly canon_p1_search_run.  CANON_WORK_POLICY_ORBIT_PRUNE
 * (docs/pruning-rules.md): A_known is built at the root from the input generators of g that
 * fix x (canon_symmetry_from_inputs); at a node of depth d whose prefix stabiliser H_d in
 * A_known is nontrivial, only the numerically least member of each H_d-orbit on the target
 * cell is explored and the other children are skipped (s->pruned).  The answer (best_trace,
 * best_bytes) is the same as under 0x0001; best_t is the least attaining witness among the
 * EXPLORED leaves, a valid witness that may differ from the unpruned one.  max_nodes counts
 * the NODE tokens of the explored nodes only.  With A_known trivial the traversal is exactly
 * the unpruned one (no rebase, no orbit computation).  Any other policy value is
 * CANON_UNSUPPORTED_ACTION. */
canon_status canon_p1_search_run_policy(canon_p1_search *s, const canon_group *g,
                                        const canon_root *x, uint64_t max_nodes,
                                        canon_work_policy work_policy);

#endif /* CANON_SRC_SEARCH_P1_TREE_H */
