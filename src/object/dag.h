/*
 * Internal header: nested objects of schema EXT-DAG-1 as a record arena (spec 2.1, 4.1, 4.2,
 * 7.1; slice S5, docs/slices/S5.md 3.1-3.3; detailed plan WP3.2, WP3.6).
 *
 * An arena is a list of records numbered 0..count-1, each a tag of the spec 4.1 table, a
 * payload and (tuples, sets, multisets) a list of child references.  Every child reference is
 * smaller than its parent's index (spec 4.1: "Every child reference is a U32 index smaller than
 * the parent index"), which makes an arena acyclic by construction; canon_dag_normalise checks
 * it with one bounded pass over the references (spec 4.2: "Validate acyclicity with a bounded
 * explicit traversal before interning").
 *
 * Payloads are wire bytes: the bytes of the record after its tag.  An atom's payload is U32(a)
 * (4 bytes, big endian); a literal's is B(s); a permutation's Perm(p); a subgroup's Group(H);
 * a labeling coset's Group(H) || Perm(r); a graph's the 09 payload.  In a NORMALISED arena the
 * payloads of permutation, subgroup, coset and graph records are their canonical payloads
 * (Perm(p) per spec 4.1, Group(H) and Group(H) || Perm(r0) per spec 9.4, the normalised graph
 * per spec 4.1), so that extensional equality of leaves is byte equality of payloads (spec 4.2:
 * "This proof assumes exact equality/normal forms for group leaves"), and a record's bytes are
 * tag || payload for a leaf.
 *
 * A NORMALISED arena is the canonical DAG of spec 4.2: only records reachable from the root,
 * one record per extensional value, numbered by increasing height and, within a height, by
 * their exact record bytes; sets and multisets list their children by increasing index; the
 * root is the last record.  Its CDAG-2 stream is written by src/encoding/cdag_encode.h.
 */
#ifndef CANON_SRC_OBJECT_DAG_H
#define CANON_SRC_OBJECT_DAG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "canon/canon.h"
#include "coset/coset.h"
#include "encoding/wire.h"
#include "perm/perm.h"

/* Record tags of the spec 4.1 table. */
enum {
    CANON_REC_ATOM = 0x01,
    CANON_REC_LITERAL = 0x02,
    CANON_REC_TUPLE = 0x03,
    CANON_REC_SET = 0x04,
    CANON_REC_MULTISET = 0x05,
    CANON_REC_PERM = 0x06,
    CANON_REC_GROUP = 0x07,
    CANON_REC_COSET = 0x08,
    CANON_REC_GRAPH = 0x09,
    CANON_REC_RELATIONS = 0x0a
};

#define CANON_DAG_NONE UINT32_MAX

/* True for the tags whose records carry child references (tuple, set, multiset). */
static inline bool canon_rec_has_children(uint8_t tag)
{
    return tag == CANON_REC_TUPLE || tag == CANON_REC_SET || tag == CANON_REC_MULTISET;
}

/* One record (docs/slices/S5.md 3.1).  payload_off/payload_len are size_t rather than uint32:
 * a literal's payload B(s) alone may exceed 2^32 - 1 bytes (spec 4.1 allows |s| up to U32). */
typedef struct canon_rec {
    uint8_t tag;          /* CANON_REC_ATOM .. CANON_REC_GRAPH */
    uint32_t child_count; /* tuple/set/multiset: children; others 0 */
    uint32_t child_off;   /* first entry in the arena's child and mult arrays */
    size_t payload_off;   /* first payload byte in the arena's pool */
    size_t payload_len;
    uint32_t height;      /* spec 4.2: 0 without child references, else 1 + max child height */
} canon_rec;

typedef struct canon_dag {
    uint32_t n;          /* degree of the atom domain (spec 2.1, the stream's U32(n)) */
    uint32_t count;      /* records */
    uint32_t rec_cap;
    canon_rec *recs;
    uint32_t refs;       /* child references in use (entries of child and mult) */
    uint32_t child_cap, mult_cap;
    uint32_t *child;     /* child record indices */
    uint64_t *mult;      /* parallel to child: multiset counts (> 0); 1 for tuples and sets */
    canon_buf pool;      /* payload bytes */
    uint32_t root;       /* CANON_DAG_NONE until set */
    /* Summary of a normalised arena (canon_dag_normalise): */
    uint64_t stream_size;   /* spec 11.1: the exact length of its CDAG-2 stream */
    uint64_t literal_bytes; /* total |s| over its literal records (capacity, detailed plan 2.1) */
    bool group_leaves;      /* it has a subgroup or labeling-coset record */
    uint64_t image_bound;   /* spec 11.1 output bound of every image (canon_dag_image_bound) */
} canon_dag;

/* An empty arena of degree n (no allocation); free with canon_dag_free. */
void canon_dag_init(canon_dag *d, uint32_t n);
void canon_dag_free(canon_dag *d);
/* Forget every record and set the degree to n, keeping the storage (grow-only reuse). */
void canon_dag_reset(canon_dag *d, uint32_t n);

/* Append a record with `k` children (`children` may be NULL when k == 0; `counts` NULL means
 * every count is 1) and a payload of payload_len bytes (copied).  The height is computed from
 * the children (spec 4.2), which must already be records of d.  *index (may be NULL) receives
 * the new record's index.  CANON_CAPACITY_LIMIT when the record or reference count would not
 * fit uint32, CANON_RESOURCE_LIMIT on allocation failure; d is unchanged on failure. */
canon_status canon_dag_append(canon_dag *d, uint8_t tag, const uint8_t *payload,
                              size_t payload_len, const uint32_t *children,
                              const uint64_t *counts, uint32_t k, uint32_t *index);

/* Payload of record i. */
static inline const uint8_t *canon_dag_payload(const canon_dag *d, uint32_t i)
{
    return d->pool.data != NULL ? d->pool.data + d->recs[i].payload_off : NULL;
}

/* The atom id of an atom record (its payload U32(a)). */
uint32_t canon_dag_atom(const canon_dag *d, uint32_t i);

/* Grow-only scratch of the normaliser and the action (spec 17: workspace storage reused across
 * leaves and solves, no heap round-trip per leaf once grown).  Not shareable between threads. */
typedef struct canon_dag_pair {
    uint32_t id;
    uint64_t count;
} canon_dag_pair;

typedef struct canon_dag_scratch {
    uint32_t map_cap, reach_cap;
    uint32_t *map;    /* input record -> interned node id */
    uint8_t *reach;   /* input record reachable from the root */
    uint32_t final_cap, order_cap, order_tmp_cap;
    uint32_t *final;  /* interned node id -> final index */
    uint32_t *order, *order_tmp;
    uint32_t table_cap;   /* entries allocated in table */
    uint32_t table_slots; /* slots in use for the current normalisation (a power of two) */
    uint32_t *table;      /* hash slots: node id + 1, 0 = empty */
    uint32_t pairs_cap, pairs_tmp_cap;
    canon_dag_pair *pairs, *pairs_tmp;
    uint32_t fchild_cap, fmult_cap;
    uint32_t *fchild; /* children of interned nodes in final indices (sorted for sets) */
    uint64_t *fmult;
    canon_dag nodes;  /* the interned nodes */
    canon_dag raw;    /* the action's image before normalisation */
    canon_buf leaf;   /* a leaf payload under construction */
    canon_buf leaf2;
    uint32_t n_cap;   /* degree the per-point arrays below hold */
    uint32_t *perm, *perm2, *inv; /* n entries each */
    uint64_t *bits;               /* 2 * ceil(n / 64) words (src/encoding/cdag_decode.h) */
    canon_perm_table gens;        /* generators of a subgroup leaf */
    canon_coset_scratch coset;    /* the spec 9.4 descents (src/coset/coset.h) */
} canon_dag_scratch;

void canon_dag_scratch_init(canon_dag_scratch *s);
void canon_dag_scratch_free(canon_dag_scratch *s);

/* spec 4.2 extensional normalisation of `in` into `out` (reset first; out != in):
 *   1. acyclicity: every child reference is smaller than its parent (one bounded pass);
 *   2. records not reachable from in->root are discarded ("Discard unreachable input nodes");
 *   3. bottom-up interning by exact (tag, payload, normalised children, counts): sets sort
 *      and deduplicate their children, multisets sort them and add the counts of equal
 *      children ("Sets deduplicate equal children; multisets combine equal children and add
 *      positive counts"); leaf payloads are replaced by their canonical form (Perm, Group,
 *      coset per spec 9.4, graph per spec 4.1) unless `leaves_canonical` says they already
 *      are (the action recomputes them itself);
 *   4. numbering by increasing height and, within a height, by exact record bytes with the
 *      already assigned child indices; the root is the last record.
 * Statuses: CANON_INVALID_INPUT for out == in or out one of the scratch's arenas, a root out of
 * range, a child reference not smaller than its parent or a malformed leaf payload; CANON_UNSUPPORTED_ACTION for a reachable graph
 * record other than the root (slice S5 scope); CANON_CAPACITY_LIMIT for a merged multiset count
 * above uint64 (count-bit limit 64, detailed plan 2.1), an order above uint64 or a size that
 * does not fit; CANON_RESOURCE_LIMIT on allocation failure; CANON_INTERNAL_ERROR if an
 * invariant fails.  On success out->stream_size, literal_bytes and group_leaves are set and
 * out->image_bound = out->stream_size (see canon_dag_image_bound). */
canon_status canon_dag_normalise(const canon_dag *in, canon_dag *out, canon_dag_scratch *s,
                                 bool leaves_canonical);

/* spec 2.1 ATOM-TRANSPORT-1 on a normalised arena: out = the normalised arena of x^g (out is
 * reset first; out != x).  Atom a -> g[a]; literals fixed; tuple positions and multiplicities
 * preserved; a permutation leaf p -> g^-1 p g; a subgroup leaf H -> g^-1 H g; a labeling coset
 * H r -> (g^-1 H g)(g^-1 r); leaf payloads are recomputed canonically and the image is
 * re-normalised, since set and multiset child orders change.  g is a permutation of
 * {0..x->n-1}.  CANON_INVALID_INPUT when x is not normalised (its root is not its last record)
 * or x or out is one of the scratch's arenas or out == x; otherwise statuses as
 * canon_dag_normalise (a graph record is CANON_INTERNAL_ERROR: graph roots are top-level
 * graphs, src/object/graph.h). */
canon_status canon_dag_act(const canon_dag *x, const uint32_t *g, canon_dag *out,
                           canon_dag_scratch *s);

/* Exact equality of two normalised arenas (spec 4.2: equal values have equal normal forms, so
 * this is extensional equality, and it holds iff their streams are equal). */
bool canon_dag_equal(const canon_dag *a, const canon_dag *b);

/* spec 11.1 output size of the images of a normalised arena d: sets d->image_bound to
 *   - d->stream_size when d has no subgroup or labeling-coset leaf: the action maps records
 *     one to one and keeps every record's length (atoms 5 bytes, literals fixed, tuples and sets
 *     keep k, multisets keep their counts, a permutation's conjugate moves as many points), so
 *     every image has exactly this length;
 *   - otherwise a conservative input-derived bound: each Group(H) payload counted at the
 *     spec 9.4 bound for |H| (canon_group_bytes_bound), each coset's Perm(r0) at 4 + 8n bytes.
 *     The canonical Group bytes of a conjugate can be shorter or longer than those of H
 *     (rule 2's greedy sequence depends on the numbering; docs/slices/S5-notes.md), so no exact
 *     invariant length exists there.
 * The bound depends only on the orbit of d (|H| is invariant under conjugation).  Builds one
 * verified chain per subgroup or coset leaf (import time only). */
canon_status canon_dag_image_bound(canon_dag *d, canon_dag_scratch *s);

/* Capacity of a normalised arena (spec 11.1, detailed plan 2.1: max_nodes, max_refs,
 * max_literal_bytes; zero fields are not allowed here, the API resolves them). */
typedef struct canon_dag_limits {
    uint32_t max_n;
    uint64_t max_nodes, max_refs, max_literal_bytes;
} canon_dag_limits;

/* CANON_CAPACITY_LIMIT when d has more records, references or literal bytes than `lim`
 * admits, else CANON_COMPLETE. */
canon_status canon_dag_check_limits(const canon_dag *d, const canon_dag_limits *lim);

#endif /* CANON_SRC_OBJECT_DAG_H */
