/*
 * Internal header: the CDAG-2 stream of a top-level subset (spec sections 4.1, 4.2).
 * Implemented in slice S1 (docs/slices/S1.md section 4.4) as a specialisation of the canonical
 * DAG numbering of spec section 4.2 for a set whose children are all atoms; the general
 * normaliser and the decoder arrive in slice S5.
 */
#ifndef CANON_SRC_ENCODING_SUBSET_STREAM_H
#define CANON_SRC_ENCODING_SUBSET_STREAM_H

#include <stdint.h>

#include "canon/canon.h"
#include "encoding/wire.h"

/* Exact byte length of the stream of a k-element subset (independent of n and of which atoms),
 * computed without overflow.  Returns CANON_CAPACITY_LIMIT if the record count q = k + 1 does
 * not fit U32 (spec section 4.1) or the length does not fit uint64. */
canon_status canon_subset_stream_size(uint32_t k, uint64_t *size_out);

/* Append the complete CDAG-2 stream of the subset {atoms[0..k-1]} of {0..n-1}.  `atoms` must be
 * strictly increasing and every atom < n.  On failure the buffer is restored to its old
 * length. */
canon_status canon_subset_stream_write(canon_buf *out, uint32_t n, const uint32_t *atoms,
                                       uint32_t k);

#endif /* CANON_SRC_ENCODING_SUBSET_STREAM_H */
