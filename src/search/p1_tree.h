/*
 * Internal header: the P1 canonical-image search (spec sections 7.1, 7.2, 7.3, 11.1).
 * Implemented in slice S1 (docs/slices/S1.md 4.7) for a top-level subset and generalised in
 * slice S2 (docs/slices/S2.md 3.5) to any root object (src/object/object.h): the leaf acts on
 * the root and encodes the image by kind.
 *
 * Slice S7 (docs/slices/S7.md 3.1-3.3, docs/pruning-rules.md) adds the first prune, selected by
 * the run options: orbit pruning of the target cell by the pointwise stabiliser of the node's
 * prefix in the verified automorphism subgroup A_known (spec 7.3 "a checked node-stabilising
 * automorphism and retained equivalent subtree"), whose sources are the input generators of G
 * that fix x and, from slice S7b (docs/slices/S7b.md), the implicit automorphisms
 * t_L t_best^-1 of leaves whose key equals the incumbent's, verified by membership and action.  The pruned and the unpruned run give the same least key; with
 * the prune the witness of the run is the least t among the EXPLORED leaves attaining it.  The
 * run can also emit a certificate (src/search/certificate.h) for the independent checker.
 * Spec 7.3 "The unpruned evaluator above is normative": work policy 0x0001 runs it unchanged.
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
#include "search/certificate.h"
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
    uint64_t nodes;        /* NODE tokens of the traversal so far (the explored nodes) */
    uint64_t leaves;       /* leaves reached in the last run */
    uint64_t images;       /* leaves whose image and stream were materialised (trace <= best) */
    /* S7 */
    canon_symmetry sym;    /* A_known and the prefix stabilisers */
    uint32_t *prefix;      /* cap: prefix[d] = the atom individualised to enter depth d + 1 */
    uint32_t *reps;        /* per-depth child decisions: at depth d, 2 n words from
                              reps + d * 2 n: (representative or CANON_CERT_NONE for an
                              explored child, automorphism index) per cell position */
    size_t reps_alloc;     /* words allocated in reps (only grows) */
    uint32_t *aut;         /* cap: the automorphism of a pruned child (certificate), or the
                              implicit automorphism of a leaf (S7b) */
    uint32_t *inv;         /* cap: t_best^-1 (S7b) */
    uint8_t *roots;        /* cap: representative flags of a cell (S7b) */
    bool prune;            /* the run's work policy prunes */
    bool implicit;         /* S7b: implicit automorphisms from equal leaf keys are inserted */
    canon_cert *cert;      /* the run's certificate writer, or NULL */
    uint64_t pruned;       /* children pruned in the last run */
    uint64_t explored;     /* children explored in the last run */
    uint64_t implicit_candidates; /* S7b: leaves with the incumbent's key */
    uint64_t implicit_inserted;   /* S7b: implicit automorphisms that enlarged A_known */
    uint64_t fixing;              /* S8: input generators that fix x (spec 7.3 singleton orbit
                                     iff fixing == generator count) */
} canon_p1_search;

/* Options of one run (slices S7, S7b).  prune selects a pruning policy (docs/pruning-rules.md):
 * with implicit false the explicit automorphisms only (work policy 0x0002), with implicit true
 * also the implicit ones from equal leaf keys (0x0003); neither without prune (0x0001).  cert
 * (may be NULL, else begun by canon_cert_begin) receives the certificate; a writer failure
 * fails the run with the writer's status. */
typedef struct canon_p1_run_options {
    bool prune;
    bool implicit;
    canon_cert *cert;
} canon_p1_run_options;

/* Zero state, no allocation. */
void canon_p1_search_init(canon_p1_search *s);
/* Release all storage; valid on an initialised or failed state. */
void canon_p1_search_free(canon_p1_search *s);

/* spec 7.1-7.2: run the P1 tree for the root x under g (same degree n), keeping the least key
 * (complete trace, CDAG-2 bytes of x^t_L), compared trace first, each by unsigned byte order
 * with a proper prefix smaller; among the explored leaves attaining that key, the
 * lexicographically least t_L (spec 7.4 preamble; every leaf is explored without the prune).
 * On CANON_COMPLETE the answer is in best_trace, best_bytes and best_t.  `opt` NULL means no
 * prune and no certificate (the unpruned reference traversal, work policy 0x0001).
 *
 * spec 11.1 logical work quota: the NODE tokens of the traversal the work policy names (every
 * node of the reference tree, or the nodes explored under the prune) are counted; past
 * max_nodes the search is abandoned with CANON_CAPACITY_LIMIT; the outcome depends only on the
 * input, the policy and max_nodes.  Allocation failure is CANON_RESOURCE_LIMIT.  A degree
 * mismatch between g and x is CANON_INVALID_INPUT (module contract; the API checks it at
 * problem creation). */
canon_status canon_p1_search_run(canon_p1_search *s, const canon_group *g, const canon_root *x,
                                 uint64_t max_nodes, const canon_p1_run_options *opt);

#endif /* CANON_SRC_SEARCH_P1_TREE_H */
