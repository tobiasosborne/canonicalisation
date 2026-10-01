/*
 * Internal header: the Perm, Group and labeling-coset payloads (spec 4.1, 9.4; slice S4,
 * docs/slices/S4.md 3.4; detailed plan WP2.7).
 *
 * These are the payloads of records 06, 07 and 08 and the canonical answers of the stabiliser
 * and transporter-coset objectives.  The group is given as a stabiliser chain
 * (src/bsgs/chain.h); the bytes depend only on the group it represents, never on its
 * generators or base (spec 9.4: "Use this deterministic priority, never whichever
 * representation the caller supplied").  Orders fit uint64 in this release (S3 admission);
 * the multi-limb canon_nat is deferred (docs/slices/S4-notes.md).
 */
#ifndef CANON_SRC_ENCODING_GROUP_STREAM_H
#define CANON_SRC_ENCODING_GROUP_STREAM_H

#include <stdint.h>

#include "bsgs/chain.h"
#include "canon/canon.h"
#include "coset/coset.h"
#include "encoding/wire.h"

/* How a Group(H) payload was produced (tests and the notes' counters). */
typedef struct canon_group_bytes_stats {
    uint32_t rule;     /* 1 (orbit blocks) or 2 (greedy generator sequence) */
    uint32_t k;        /* blocks (rule 1) or generators (rule 2) */
    uint64_t k_builds; /* rule 2: verified rebuilds of K */
    canon_coset_stats coset; /* descents and transient rebuilds */
} canon_group_bytes_stats;

/* spec 4.1: Perm(p) = U32(s) followed by s pairs U32(i), U32(p[i]) in increasing i, exactly
 * the moved support.  All or nothing. */
canon_status canon_perm_bytes_write(canon_buf *out, const uint32_t *p, uint32_t n);

/* spec 9.4: append the canonical Group(H) payload of the group of chain `h`:
 *   rule 1 when |H| equals the product of the factorials of its orbit sizes:
 *          01 || U32(k) || k blocks U32(size) U32(points...), non-singleton orbits only,
 *          points increasing, blocks by least point;
 *   rule 2 otherwise: 00 || U32(k) || Perm(g_1) ... Perm(g_k), g_i the least image array in
 *          H \ <g_1, ..., g_(i-1)>.
 * All or nothing (on failure the buffer keeps its old length).  `stats` may be NULL.
 * CANON_RESOURCE_LIMIT / CANON_CAPACITY_LIMIT on allocation or size failure,
 * CANON_INTERNAL_ERROR if a verified rebuild of K is rejected. */
canon_status canon_group_bytes_write(canon_buf *out, const canon_bsgs *h,
                                     canon_group_bytes_stats *stats);

/* spec 9.4: the labeling-coset payload of H r: Group(H) || Perm(r0), r0 the least image-array
 * element of H r (r a permutation of the domain).  All or nothing. */
canon_status canon_coset_bytes_write(canon_buf *out, const canon_bsgs *h, const uint32_t *r,
                                     canon_group_bytes_stats *stats);

/* spec 11.1: a conservative input-derived bound on the length of Group(H) for every subgroup
 * H of a group of degree n and order at most `order` (rule 1: at most 5 + 4 floor(n/2) + 4n
 * bytes; rule 2: k <= floor(log2 |H|) generators of at most 4 + 8n bytes each, spec 9.4
 * "k <= floor(log2 |H|)"), plus the Perm of a coset representative when `coset`.  False if the
 * bound does not fit uint64. */
bool canon_group_bytes_bound(uint32_t n, uint64_t order, bool coset, uint64_t *bound);

#endif /* CANON_SRC_ENCODING_GROUP_STREAM_H */
