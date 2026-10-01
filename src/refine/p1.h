/*
 * Internal header: profile P1 refinement (spec section 7.1) and its trace tokens (spec 7.2).
 * Implemented in slice S1 (docs/slices/S1.md section 4.6) for a top-level subset root, and
 * generalised in slice S2 (docs/slices/S2.md section 3.4) to any root object
 * (src/object/object.h): the initial key comes from the root, and for a graph root the O stage
 * splits by sparse arc-count signatures.  The G stage uses the group interface of
 * src/bsgs/group.h.
 */
#ifndef CANON_SRC_REFINE_P1_H
#define CANON_SRC_REFINE_P1_H

#include <stddef.h>
#include <stdint.h>

#include "bsgs/group.h"
#include "canon/canon.h"
#include "encoding/wire.h"
#include "object/graph.h"
#include "object/object.h"
#include "partition/partition.h"

/* One nonzero entry of a sparse O-stage signature (see canon_p1_sig_compare). */
typedef struct canon_p1_sig_entry {
    uint64_t index; /* ((l * k) + j) * 2 + dir: label rank l, entry-snapshot cell j, dir 0 out / 1 in */
    uint64_t count; /* total multiplicity, > 0 */
} canon_p1_sig_entry;

/* Refinement scratch, owned by a workspace and reused across solves; every array only grows
 * (canon_p1_scratch_reserve). */
typedef struct canon_p1_scratch {
    uint32_t cap_n;  /* degree the per-point arrays hold (at least one entry each) */
    uint32_t *fixed; /* F: singleton atoms in partition order */
    uint32_t *u;     /* transporter with F^u = M */
    uint32_t *orbit; /* orbit ids of G_M on target labels */
    uint32_t *sig;   /* per-point uint32 key handed to split */
    /* O stage (graph roots): per-vertex sparse signatures.  The entries of vertex v live at
     * entries[out_start[v] + in_start[v] ..] (v's incident arc count bounds them), sig_len[v]
     * of them after merging. */
    size_t *sig_len;             /* cap_n */
    uint32_t *order, *order_tmp; /* cap_n: vertices sorted by signature, and sort scratch */
    size_t cap_entries;          /* entries allocated in entries and entries_tmp */
    canon_p1_sig_entry *entries, *entries_tmp;
} canon_p1_scratch;

/* Zero state (no allocation). */
void canon_p1_scratch_init(canon_p1_scratch *s);
/* Release all storage; valid on an initialised or failed scratch. */
void canon_p1_scratch_free(canon_p1_scratch *s);
/* Grow the scratch for refining x (degree x->n; for a graph root, 2e signature entries: each
 * arc is one out-entry of its source and one in-entry of its target).  Grow-only.
 * CANON_CAPACITY_LIMIT on size overflow, CANON_RESOURCE_LIMIT on allocation failure (the
 * scratch then keeps its previous arrays and capacities, and may be reserved again or freed). */
canon_status canon_p1_scratch_reserve(canon_p1_scratch *s, const canon_root *x);

/* spec 7.1: reset P (degree x->n) to the root partition of x: cells of equal initial key ordered
 * by increasing key (canon_root_initial_key); n = 0 gives the empty ordered partition. */
void canon_p1_initial(canon_partition *p, const canon_root *x, canon_p1_scratch *s);

/* spec 7.1 O stage for a graph root, signatures only: for every vertex v, the sparse signature
 * of v against the current partition (the stage's entry snapshot), readable through
 * canon_p1_signature.  Requires canon_p1_scratch_reserve for g's root.
 * CANON_CAPACITY_LIMIT if 2 * L * k does not fit uint64 or a count overflows uint64 (neither
 * happens for an imported graph, whose total multiplicity fits uint64). */
canon_status canon_p1_graph_signatures(const canon_partition *p, const canon_graph *g,
                                       canon_p1_scratch *s);

/* The sparse signature of v computed by canon_p1_graph_signatures: *length entries with
 * strictly increasing index and positive count. */
const canon_p1_sig_entry *canon_p1_signature(const canon_p1_scratch *s, const canon_graph *g,
                                             uint32_t v, size_t *length);

/* spec 7.1: "Count signatures compare lexicographically using the ordinary numerical order on
 * mathematical naturals."  Compares two sparse signatures as the dense vectors they elide (same
 * length 2 * L * k within one stage); returns -1, 0 or +1.  See p1.c for the justification. */
int canon_p1_sig_compare(const canon_p1_sig_entry *a, size_t a_len, const canon_p1_sig_entry *b,
                         size_t b_len);

/* spec 7.1 O stage: split P by the root's signatures (graph: sparse arc counts against the
 * entry snapshot; every other root: empty signatures, so the identity).  Does not append the
 * STAGE_O token. */
canon_status canon_p1_stage_o(canon_partition *p, const canon_root *x, canon_p1_scratch *s);

/* spec 7.1: the node refinement loop, appending NODE(depth) and one STAGE_O and one STAGE_G
 * token per sweep to `trace` (spec 7.2), up to and including the first sweep that does not
 * increase the number of cells.  On failure the trace may hold a partial node; the caller
 * truncates it. */
canon_status canon_p1_refine_node(canon_partition *p, const canon_group *g, const canon_root *x,
                                  uint32_t depth, canon_buf *trace, canon_p1_scratch *s);

/* spec 7.2: append the LEAF token. */
canon_status canon_p1_trace_leaf(canon_buf *trace);

#endif /* CANON_SRC_REFINE_P1_H */
