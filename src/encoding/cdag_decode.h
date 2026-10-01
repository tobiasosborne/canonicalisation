/*
 * Internal header: the strict CDAG-2 decoder and the canonical-form validator (spec 4.1, 4.2;
 * slice S5, docs/slices/S5.md 3.5; detailed plan WP3.3).
 *
 * The decoder parses the spec 4.1 grammar for tags 01..09 into a record arena
 * (src/object/dag.h) without normalising it: payloads are kept verbatim and child references
 * as written.  Every violation of the grammar is CANON_INVALID_INPUT with a reason (for tests);
 * what is recognised but not supported in this release is CANON_UNSUPPORTED_ACTION ("unknown
 * schema/action/profile/encoding versions are unsupported, never reinterpreted"): the
 * encoding version byte other than 02, schema/action other than 1/1, the relations record 0a.
 * A count that is well formed but does not fit the count-bit limit of 64 (a Nat longer than
 * 8 bytes) is CANON_CAPACITY_LIMIT (detailed plan 2.1).  The first violation in stream order
 * decides the status.
 *
 * What the decoder deliberately accepts (spec 4.2: "Source sharing, unreachable allocation
 * records and insertion order have no meaning"): repeated equal records, unreachable records,
 * a root other than the last record, non-canonical Group presentations and coset
 * representatives, and graph arcs in any order or repeated (spec 4.1 "Combine duplicate
 * (source,target,label) arcs by exact addition", as the S2 builder imports them).  Those are
 * removed by normalisation; canon_cdag_validate rejects them by byte identity.
 */
#ifndef CANON_SRC_ENCODING_CDAG_DECODE_H
#define CANON_SRC_ENCODING_CDAG_DECODE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "canon/canon.h"
#include "object/dag.h"
#include "object/graph.h"
#include "perm/perm.h"

/* Why a stream was refused (tests; the public API reports the status only). */
typedef enum canon_cdag_reason {
    CANON_CDAG_OK = 0,
    CANON_CDAG_TRUNCATED,        /* INVALID: a field runs past the end */
    CANON_CDAG_BAD_MAGIC,        /* INVALID: not 43 4e */
    CANON_CDAG_VERSION,          /* UNSUPPORTED: encoding version byte other than 02 */
    CANON_CDAG_SCHEMA_ACTION,    /* UNSUPPORTED: schema/action other than 1/1 */
    CANON_CDAG_DEGREE,           /* INVALID: U32(n) differs from the expected degree */
    CANON_CDAG_DEGREE_LIMIT,     /* CAPACITY: n above the admitted degree */
    CANON_CDAG_EMPTY,            /* INVALID: q = 0 */
    CANON_CDAG_UNKNOWN_TAG,      /* INVALID: a tag outside 01..0a */
    CANON_CDAG_RELATIONS,        /* UNSUPPORTED: record 0a */
    CANON_CDAG_ATOM_RANGE,       /* INVALID: an atom id >= n */
    CANON_CDAG_FORWARD_REF,      /* INVALID: a child reference >= the parent index */
    CANON_CDAG_SET_ORDER,        /* INVALID: set children not strictly increasing */
    CANON_CDAG_MULTISET_ORDER,   /* INVALID: multiset children not strictly increasing */
    CANON_CDAG_ZERO_COUNT,       /* INVALID: a multiset count or arc multiplicity of 0 */
    CANON_CDAG_NAT_LEADING_ZERO, /* INVALID: Nat not in shortest form */
    CANON_CDAG_NAT_RANGE,        /* CAPACITY: Nat above uint64 */
    CANON_CDAG_PERM_ORDER,       /* INVALID: Perm sources not strictly increasing */
    CANON_CDAG_PERM_FIXED,       /* INVALID: Perm lists a fixed pair */
    CANON_CDAG_PERM_RANGE,       /* INVALID: Perm source or target >= n */
    CANON_CDAG_PERM_BIJECTION,   /* INVALID: Perm is not a bijection of its support */
    CANON_CDAG_GROUP_MODE,       /* INVALID: Group mode byte other than 00/01 */
    CANON_CDAG_GROUP_BLOCK,      /* INVALID: rule-1 block of size < 2, points not increasing,
                                    blocks not ordered by least point, or blocks overlapping */
    CANON_CDAG_ROOT_RANGE,       /* INVALID: root >= q */
    CANON_CDAG_TRAILING,         /* INVALID: bytes after U32(root) */
    CANON_CDAG_SIZE,             /* CAPACITY: a count or length that does not fit memory */
    CANON_CDAG_NESTED_GRAPH,     /* UNSUPPORTED: a reachable graph record below the root */
    CANON_CDAG_UNREACHABLE,      /* validate: a record not reachable from the root */
    CANON_CDAG_NOT_CANONICAL     /* validate: re-normalising changes the bytes */
} canon_cdag_reason;

/* A bounded reader over a byte span. */
typedef struct canon_cdag_reader {
    const uint8_t *data;
    size_t len, pos;
} canon_cdag_reader;

/* Number of uint64 words the payload readers need as `bits` for degree n: two n-bit maps. */
size_t canon_cdag_bits_words(uint32_t n);

/* spec 4.1 Perm(p): "U32(s) followed by s pairs U32(i),U32(p[i]) in increasing i, exactly the
 * moved support ... Reject duplicate sources, fixed pairs, out-of-range targets or a
 * nonbijection."  Reads one Perm at r's position.  `bits` has canon_cdag_bits_words(n) zero
 * words and is zero again on return.  `dense` (n entries, may be NULL) receives p as an image
 * array.  CANON_INVALID_INPUT with *reason on a violation. */
canon_status canon_cdag_read_perm(canon_cdag_reader *r, uint32_t n, uint64_t *bits, uint32_t *dense,
                                  canon_cdag_reason *reason);

/* spec 9.4 Group(H): rule 1 "01 || U32(k)" then k blocks "U32(size), U32(points...)" (size
 * >= 2, points increasing, blocks ordered by least point, disjoint), or rule 2 "00 || U32(k)
 * || Perm(g_1)...Perm(g_k)".  Any well-formed presentation is accepted; whether it is the
 * canonical one is decided by re-encoding.  When `gens` is not NULL (degree n) a generating
 * set of H is appended to it: per rule-1 block the transposition of its first two points and
 * the cycle through all its points, per rule-2 entry the permutation; `tmp` has n entries
 * (needed only with gens).  `bits` as for canon_cdag_read_perm. */
canon_status canon_cdag_read_group(canon_cdag_reader *r, uint32_t n, uint64_t *bits,
                                   canon_perm_table *gens, uint32_t *tmp,
                                   canon_cdag_reason *reason);

/* spec 4.1 graph payload: "n values B(vertex_colour), U32(e), e arc records", an arc record
 * "U32(source),U32(target),B(label),Nat(multiplicity)" with source, target < n and a positive
 * shortest Nat.  Arcs may come in any order and repeat (combined on import, spec 4.1).  When
 * g is not NULL it receives the imported graph (canon_graph_init: combined, sorted, indexed). */
canon_status canon_cdag_read_graph(canon_cdag_reader *r, uint32_t n, canon_graph *g,
                                   canon_cdag_reason *reason);

/* spec 4.1 strict parse of a complete stream into the raw arena `out` (reset to the stream's
 * n): header 43 4e 02, U16(schema) = 1, U16(action) = 1, U32(n), U32(q >= 1), q records
 * (tags 01..09, child references smaller than the parent index, set and multiset children
 * strictly increasing, positive shortest Nat counts, atoms < n, strict Perm and Group
 * payloads, graph payloads as above), U32(root < q), no trailing bytes.  If expect_n is not
 * NULL, U32(n) must equal *expect_n (CANON_INVALID_INPUT otherwise); n above max_n is
 * CANON_CAPACITY_LIMIT.  out->root is set; the arena is not normalised.  `bits` scratch is
 * allocated internally. */
canon_status canon_cdag_decode(const uint8_t *stream, size_t length, const uint32_t *expect_n,
                               uint32_t max_n, canon_dag *out, canon_cdag_reason *reason);

/* spec 4.2: "Validate imported canonical streams by reconstructing this normal form and
 * requiring byte identity; repeated equal nodes, redundant references, leading zeroes,
 * noncanonical orders or unreachable records are invalid canonical encodings."  Decode,
 * check the limits of the normal form, normalise, encode and compare.  CANON_COMPLETE iff
 * `stream` is canonical; otherwise the decoder's or normaliser's status, or
 * CANON_INVALID_INPUT with CANON_CDAG_UNREACHABLE / CANON_CDAG_NOT_CANONICAL (this includes a
 * graph with arcs out of order or repeated, which import accepts and combines). */
canon_status canon_cdag_validate(const uint8_t *stream, size_t length,
                                 const canon_dag_limits *limits, canon_cdag_reason *reason);

/* Decode and normalise a stream for import (spec 4.1, 4.2): `out` receives the normalised
 * arena, checked against `limits` (spec 11.1, deterministic over the normalised input), with
 * its image bound computed (canon_dag_image_bound).  Statuses as canon_cdag_decode and
 * canon_dag_normalise, plus CANON_CAPACITY_LIMIT for the limits. */
canon_status canon_cdag_import(const uint8_t *stream, size_t length, uint32_t degree,
                               const canon_dag_limits *limits, canon_dag *out,
                               canon_cdag_reason *reason);

#endif /* CANON_SRC_ENCODING_CDAG_DECODE_H */
