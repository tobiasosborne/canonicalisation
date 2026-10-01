/*
 * Internal header: the CDAG-2 stream of a top-level coloured directed multigraph (spec sections
 * 4.1, 4.2).  Implemented in slice S2 (docs/slices/S2.md section 3.3).
 *
 * A graph is one record with no child references (spec 4.1 tag 09: its payload holds colours
 * and arcs, not references), so the canonical DAG of spec 4.2 is a single height-0 record:
 *   43 4e 02 | U16(1) | U16(1) | U32(n) | U32(q = 1) |
 *   09 || n x B(colour[v]) || U32(e) || e x (U32 source, U32 target, B(label), Nat(m)) |
 *   U32(root = 0)
 * with the arcs in their normalised order (source, target, B(label)).
 */
#ifndef CANON_SRC_ENCODING_GRAPH_STREAM_H
#define CANON_SRC_ENCODING_GRAPH_STREAM_H

#include <stdint.h>

#include "canon/canon.h"
#include "encoding/wire.h"
#include "object/graph.h"

/* Exact byte length of the stream of g, computed with overflow checks (spec 11.1); the length
 * does not depend on the vertex numbering (it sums over the colour multiset and the arcs'
 * label lengths and Nat lengths), so it is invariant under the action.  CANON_CAPACITY_LIMIT if
 * it does not fit uint64. */
canon_status canon_graph_stream_size(const canon_graph *g, uint64_t *size_out);

/* Append the complete CDAG-2 stream of the normalised graph g.  All or nothing: on failure the
 * buffer keeps its old length. */
canon_status canon_graph_stream_write(canon_buf *out, const canon_graph *g);

#endif /* CANON_SRC_ENCODING_GRAPH_STREAM_H */
