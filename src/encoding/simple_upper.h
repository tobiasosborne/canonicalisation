/*
 * Internal header: the SIMPLE-UPPER-1 key (order 0x0002, spec section 4.4).  Implemented in
 * slice S2 (docs/slices/S2.md section 3.3) as a library function only: the minimum search under
 * this order is slice S4, and canon_problem_create still rejects order 0x0002.
 */
#ifndef CANON_SRC_ENCODING_SIMPLE_UPPER_H
#define CANON_SRC_ENCODING_SIMPLE_UPPER_H

#include "canon/canon.h"
#include "encoding/wire.h"
#include "object/graph.h"

/* spec 4.4: append U32(n) followed by the upper-triangle adjacency bits in the order
 * (0,1), (0,2), (1,2), (0,3), ..., packed most significant bit first, the final byte padded
 * with zero low bits.  The order is defined only for uncoloured simple undirected graphs: every
 * vertex colour and arc label empty, no loops, and exactly one unit arc in each direction for
 * each edge.  Any other graph is CANON_INVALID_INPUT and nothing is appended.  A key whose byte
 * length does not fit size_t is CANON_CAPACITY_LIMIT; allocation failure CANON_RESOURCE_LIMIT
 * (all or nothing). */
canon_status canon_simple_upper_key(const canon_graph *g, canon_buf *out);

#endif /* CANON_SRC_ENCODING_SIMPLE_UPPER_H */
