/*
 * Internal header: the SIMPLE-UPPER-1 key (order 0x0002, spec section 4.4).  Implemented in
 * slice S2 (docs/slices/S2.md section 3.3); slice S4 adds the minimum search under this order
 * (LEX_MIN_IMAGE, src/search/objectives.c).
 */
#ifndef CANON_SRC_ENCODING_SIMPLE_UPPER_H
#define CANON_SRC_ENCODING_SIMPLE_UPPER_H

#include <stdbool.h>

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

/* spec 4.4: the exact length of the key of any graph on n vertices, 4 + ceil(n(n-1)/16)
 * bytes (U32(n) and the packed upper-triangle bits); a function of n alone. */
uint64_t canon_simple_upper_key_size(uint32_t n);

/* spec 4.4: true iff g is in the class the order is defined for (uncoloured, empty labels, no
 * loops, exactly one unit arc in each direction for each edge).  The class is invariant under
 * the action, so slice S4 checks it once, at problem creation. */
bool canon_simple_upper_in_class(const canon_graph *g);

#endif /* CANON_SRC_ENCODING_SIMPLE_UPPER_H */
