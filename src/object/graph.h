/*
 * Internal header: the coloured directed multigraph object (spec sections 2.1, 4.1, 7.1, 10,
 * 11.1).  Implemented in slice S2 (docs/slices/S2.md section 3.2).
 *
 * Normalised representation (spec 4.1, 4.2: insertion order and duplicate storage have no
 * meaning):
 *   - a colour table and a label table, each the DISTINCT byte strings sorted by
 *     (length, unsigned bytes), which is the order of B(s) (spec 4.1: "Labels compare by (byte
 *     length, unsigned bytes)"; spec 4.3: "a B field compares length first");
 *   - colour_id[v] indexes the colour table, so colour_id is also the colour's rank;
 *   - e arcs (source, target, label id, multiplicity > 0) with duplicate (source, target, label)
 *     combined by exact addition and sorted by (source, target, B(label)) (spec 4.1); label ids
 *     are ranks in the label table, so sorting by id is sorting by B(label);
 *   - CSR by source (arcs are sorted by source, so out_start alone indexes them) and CSC by
 *     target (in_start, in_arc), so that the O stage visits only incident arcs (spec 10).
 * Colours and labels are fixed byte strings: the action never renames them (spec 2.1).
 *
 * Capacities (spec 11.1, detailed plan 2.1): e and every byte-string length fit U32; every
 * multiplicity and the total positive multiplicity fit uint64 (count-bit limit 64 in this
 * release); a value that does not fit is CANON_CAPACITY_LIMIT.
 */
#ifndef CANON_SRC_OBJECT_GRAPH_H
#define CANON_SRC_OBJECT_GRAPH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "canon/canon.h"

/* A table of distinct byte strings sorted by (length, bytes): string i is
 * pool[offset[i] .. offset[i + 1]). */
typedef struct canon_byte_table {
    uint32_t count;
    size_t *offset; /* count + 1 entries (NULL when count == 0) */
    uint8_t *pool;  /* offset[count] bytes (NULL when that is 0) */
} canon_byte_table;

/* One normalised arc. */
typedef struct canon_graph_arc {
    uint32_t source, target;
    uint32_t label;          /* index into the label table = rank of B(label) */
    uint64_t multiplicity;   /* > 0 (spec 4.1) */
} canon_graph_arc;

/* Two kinds of canon_graph share this type:
 *   - an IMPORTED graph (canon_graph_init, canon_graph_init_simple): owns its colour and label
 *     tables, has its CSR/CSC index built (indexed = true) and is immutable afterwards;
 *   - IMAGE storage (canon_graph_init_empty, then canon_graph_act_into): borrows the tables of
 *     the graph it was last produced from, has NO index (indexed = false; out_start, in_start
 *     and in_arc are absent unless canon_graph_build_index is called), and is valid only while
 *     that source graph is alive (canon_graph_image_clear drops the borrowed pointers). */
typedef struct canon_graph {
    uint32_t n;                 /* vertices = atoms of the domain (spec 2.1) */
    uint32_t e;                 /* distinct (source, target, label) arcs */
    canon_byte_table colours;   /* distinct vertex colours, sorted */
    canon_byte_table labels;    /* distinct arc labels, sorted; labels.count = L */
    bool imported;              /* owns the tables (see above); false for image storage */
    bool indexed;               /* out_start, in_start, in_arc are valid */
    uint32_t *colour_id;        /* n entries */
    canon_graph_arc *arcs;      /* e entries sorted by (source, target, label) */
    uint64_t total_multiplicity; /* spec 11.1: exact sum of all multiplicities */
    uint64_t stream_size;       /* spec 11.1: exact CDAG-2 stream length, measured at import;
                                   the same for every image (the action only renumbers) */
    uint32_t *out_start;        /* n + 1: arcs with source v are arcs[out_start[v] .. +1) */
    uint32_t *in_start;         /* n + 1: arcs with target v are in_arc[in_start[v] .. +1) */
    uint32_t *in_arc;           /* e: arc indices grouped by target, increasing within a group */
    /* Image storage only (grow-only capacities, spec 17 reusable workspace storage). */
    uint32_t cap_n, cap_e;
    canon_graph_arc *arcs_tmp;  /* cap_e: sort scratch of canon_graph_act_into */
} canon_graph;

/* Zero state (no allocation); valid to free. */
void canon_graph_init_empty(canon_graph *g);

/* spec 4.1 import, copying all data (spec 17 builders copy).  `colours[v]` has
 * `colour_lengths[v]` bytes; colours and colour_lengths may both be NULL to mean that every
 * colour is empty (the public builder only permits that for n = 0).  A NULL colours[v] or arc
 * label with a nonzero length is CANON_INVALID_INPUT, as is a vertex >= n or a zero
 * multiplicity (spec 4.1: "input zero multiplicities are invalid").  A byte string longer than
 * U32, more than 2^32 - 1 distinct arcs, or a combined or total multiplicity above uint64 is
 * CANON_CAPACITY_LIMIT (spec 4.1, 11.1), as is a stream length above uint64.  All invalid
 * conditions take precedence over the capacity ones.  Allocation failure is
 * CANON_RESOURCE_LIMIT.  On success the graph is imported and indexed, with its exact stream
 * length cached; on failure *g is empty and canon_graph_free(g) is still valid. */
canon_status canon_graph_init(canon_graph *g, uint32_t n, const uint8_t *const *colours,
                              const size_t *colour_lengths, const canon_arc *arcs,
                              size_t arc_count);

/* spec 4.1 schema-specific wrapper for simple undirected graphs: `edges` are unordered pairs
 * {a, b}; a loop (a == b) or a vertex >= n is CANON_INVALID_INPUT ("Schema-specific wrappers for
 * simple graphs reject loops"); duplicate edges, in either orientation, are coalesced; each
 * edge becomes the two opposite unit arcs a -> b and b -> a with the empty label; every vertex
 * colour is empty.  The core never makes a directed graph undirected: this is the only place. */
canon_status canon_graph_init_simple(canon_graph *g, uint32_t n, const uint32_t (*edges)[2],
                                     size_t edge_count);

/* Release storage (tables only when imported).  Valid on an empty, failed or image graph. */
void canon_graph_free(canon_graph *g);

/* spec 10: build the CSR (by source) and CSC (by target) index over g's arcs, replacing any
 * previous index.  Called by import; image storage has no index unless this is called (tests).
 * CANON_CAPACITY_LIMIT / CANON_RESOURCE_LIMIT on allocation failure (the old index is then
 * kept, and indexed keeps its value). */
canon_status canon_graph_build_index(canon_graph *g);

/* Drop an image's borrowed table pointers and mark it empty (n = e = 0), keeping its storage
 * for reuse.  After this the image refers to no other graph.  No-op on an imported graph. */
void canon_graph_image_clear(canon_graph *g);

/* spec 7.1: "on a top-level graph [the initial key] is B(vertex_colour[a])".  The colour table
 * is sorted by B order, so the colour id is the key's rank: increasing id = increasing key. */
static inline uint32_t canon_graph_initial_key(const canon_graph *g, uint32_t v)
{
    return g->colour_id[v];
}

/* Byte string i of a table, with its length. */
static inline const uint8_t *canon_byte_table_get(const canon_byte_table *t, uint32_t i,
                                                  size_t *length)
{
    *length = t->offset[i + 1] - t->offset[i];
    return t->pool != NULL ? t->pool + t->offset[i] : NULL;
}

/* spec 2.1 action ATOM-TRANSPORT-1 on a graph: colour'[p[v]] = colour[v] and each arc
 * (s, t, l, m) becomes (p[s], p[t], l, m), re-sorted by (source, target, B(label)); colours and
 * labels are fixed byte strings and are never renamed, so the image BORROWS g's tables, total
 * multiplicity and stream length.  The image gets no CSR/CSC index (nothing on the leaf path
 * reads it).  Precondition: `dest` is image storage (canon_graph_init_empty once, then any
 * number of calls; arrays grow only), never an imported graph and never g; an imported dest is
 * refused with CANON_INVALID_INPUT and left untouched.  p must be a permutation of {0..n-1}.
 * dest is valid only while g is alive.  CANON_RESOURCE_LIMIT / CANON_CAPACITY_LIMIT on growth
 * failure. */
canon_status canon_graph_act_into(const canon_graph *g, const uint32_t *p, canon_graph *dest);

/* Extensional equality on the normalised representation (spec 4.2): same n, same colour bytes
 * at every vertex, same arc list with the same label bytes and multiplicities.  Graphs with
 * different tables compare by bytes. */
bool canon_graph_equal(const canon_graph *a, const canon_graph *b);

#endif /* CANON_SRC_OBJECT_GRAPH_H */
