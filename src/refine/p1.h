/*
 * Internal header: profile P1 refinement (spec section 7.1) and its trace tokens (spec 7.2).
 * Implemented in slice S1 (docs/slices/S1.md section 4.6) for a top-level subset root: the
 * initial key is membership, the O stage has empty signatures (graph signatures arrive in
 * slice S2), and the G stage uses the group interface of src/bsgs/group.h.
 */
#ifndef CANON_SRC_REFINE_P1_H
#define CANON_SRC_REFINE_P1_H

#include <stdint.h>

#include "bsgs/group.h"
#include "canon/canon.h"
#include "encoding/wire.h"
#include "object/subset.h"
#include "partition/partition.h"

/* Caller-owned scratch for one refinement, each array of `n` entries (at least one entry). */
typedef struct canon_p1_scratch {
    uint32_t *fixed; /* F: singleton atoms in partition order */
    uint32_t *u;     /* transporter with F^u = M */
    uint32_t *orbit; /* orbit ids of G_M on target labels */
    uint32_t *sig;   /* per-point signature */
} canon_p1_scratch;

/* spec 7.1: reset P to the root partition of a top-level subset: cells of equal membership key
 * ordered by increasing key ([non-members, members], omitting an empty class); n = 0 gives the
 * empty ordered partition. */
void canon_p1_initial_subset(canon_partition *p, const canon_subset *x, canon_p1_scratch *s);

/* spec 7.1: the node refinement loop for a non-graph root, appending NODE(depth) and one
 * STAGE_O and one STAGE_G token per sweep to `trace` (spec 7.2), up to and including the first
 * sweep that does not increase the number of cells.  On failure (allocation) the trace may hold
 * a partial node; the caller truncates it. */
canon_status canon_p1_refine_node(canon_partition *p, const canon_group *g, uint32_t depth,
                                  canon_buf *trace, canon_p1_scratch *s);

/* spec 7.2: append the LEAF token. */
canon_status canon_p1_trace_leaf(canon_buf *trace);

#endif /* CANON_SRC_REFINE_P1_H */
