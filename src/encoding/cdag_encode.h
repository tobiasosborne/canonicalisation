/*
 * Internal header: the general CDAG-2 encoder of a normalised record arena (spec 4.1, 4.2;
 * slice S5, docs/slices/S5.md 1, 3.2; detailed plan WP3.3).
 *
 * The stream is 43 4e 02 | U16(schema=1) | U16(action=1) | U32(n) | U32(q) | records | U32(root)
 * with each record written as its tag followed by
 *   leaves (atom, literal, Perm, Group, coset, graph): the stored wire payload;
 *   tuple: U32(k) and the k child indices in order;
 *   set: U32(k) and the k child indices (increasing in a normalised arena);
 *   multiset: U32(k) and k pairs (child index, Nat(count)).
 * The subset (src/encoding/subset_stream.h) and graph (graph_stream.h) streams of slices S1 and
 * S2 are specialisations kept as fast paths; tests/c/test_dag.c checks that this encoder gives
 * identical bytes for them.
 */
#ifndef CANON_SRC_ENCODING_CDAG_ENCODE_H
#define CANON_SRC_ENCODING_CDAG_ENCODE_H

#include <stdint.h>

#include "canon/canon.h"
#include "encoding/wire.h"
#include "object/dag.h"

/* The byte length of record i of d (spec 4.1), with overflow checks. */
canon_status canon_dag_record_size(const canon_dag *d, uint32_t i, uint64_t *size_out);

/* spec 11.1: the exact length of the stream of d, with overflow checks (CANON_CAPACITY_LIMIT if
 * it does not fit uint64). */
canon_status canon_dag_stream_measure(const canon_dag *d, uint64_t *size_out);

/* Append the complete stream of the arena d (normally a normalised one; the encoder writes
 * records exactly as stored).  d->root must be set.  All or nothing. */
canon_status canon_dag_stream_write(canon_buf *out, const canon_dag *d);

#endif /* CANON_SRC_ENCODING_CDAG_ENCODE_H */
