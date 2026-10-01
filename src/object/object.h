/*
 * Internal header: the root object of a problem (spec sections 2.1, 4.1, 7.1), dispatching on
 * the object kind so that refinement and search are not kind-specific.  Slice S2
 * (docs/slices/S2.md section 3.1).  Kinds: the top-level subset (S1, src/object/subset.h), the
 * top-level coloured directed multigraph (S2, src/object/graph.h) and (S5,
 * docs/slices/S5.md 3.4) every other EXT-DAG-1 root as a normalised record arena
 * (src/object/dag.h).  An object imported from a CDAG-2 stream is normalised first, and a root
 * that is a set of atoms or a graph record becomes a subset or graph root, so that the S1 and
 * S2 paths are reused unchanged (spec 7.1 names exactly these two roots).  The public
 * canon_object wraps one canon_root.
 */
#ifndef CANON_SRC_OBJECT_OBJECT_H
#define CANON_SRC_OBJECT_OBJECT_H

#include <stdbool.h>
#include <stdint.h>

#include "canon/canon.h"
#include "encoding/cdag_decode.h"
#include "encoding/wire.h"
#include "object/dag.h"
#include "object/graph.h"
#include "object/subset.h"

typedef enum { CANON_ROOT_SUBSET, CANON_ROOT_GRAPH, CANON_ROOT_DAG } canon_root_kind;

typedef struct canon_root {
    canon_root_kind kind;
    uint32_t n; /* degree of the atom domain (spec 2.1) */
    union {
        canon_subset subset;
        canon_graph graph;
        canon_dag dag; /* S5: a normalised arena whose root is neither a subset nor a graph */
    } u;
} canon_root;

/* Release the root's storage; valid after a failed init (the caller zero-initialises it). */
void canon_root_free(canon_root *x);

/* spec 7.1 initial key of atom a: "On the top-level subset ..., the initial key of a is
 * membership 0/1; on a top-level graph it is B(vertex_colour[a])", returned as a rank so that
 * increasing key equals increasing B(colour) (the colour table is sorted by B order); "on every
 * other root it is the empty key" (0 for every atom: one cell). */
uint32_t canon_root_initial_key(const canon_root *x, uint32_t a);

/* Reusable storage for images of roots (spec 17: workspace storage reused across solves).
 * Grows only; switching kinds frees the other kind's storage. */
typedef struct canon_root_image {
    canon_root root;  /* the last image produced */
    bool has_storage; /* root.kind's storage is allocated */
    uint32_t cap;     /* subset images: degree the atom list and bitset are allocated for */
    canon_dag_scratch dag_scratch; /* S5: the normaliser's grow-only scratch for DAG images */
} canon_root_image;

void canon_root_image_init(canon_root_image *img);
void canon_root_image_free(canon_root_image *img);

/* spec 2.1 action ATOM-TRANSPORT-1: img->root = x^g, for g a permutation of {0..n-1}.  A graph
 * image borrows x's colour and label tables (they are never renamed), so it is valid while x
 * is; a DAG image (S5) owns its arena (canon_dag_act: leaves recomputed, re-normalised).
 * CANON_RESOURCE_LIMIT / CANON_CAPACITY_LIMIT if the storage cannot grow. */
canon_status canon_root_act_into(const canon_root *x, const uint32_t *g, canon_root_image *img);

/* Forget the last image: a graph image drops the table pointers it borrowed from its source
 * (canon_graph_image_clear), so the storage refers to no object; capacities are kept. */
void canon_root_image_clear(canon_root_image *img);

/* spec 4.2 extensional equality of two roots (slice S4: the transporter and stabiliser
 * consumers of spec 8.2 test x^r = y and x^r = x this way rather than by comparing streams):
 * same kind, same degree, and equal normalised content (src/object/subset.h, graph.h). */
bool canon_root_equal(const canon_root *a, const canon_root *b);

/* spec 4.1, 4.2: append the complete CDAG-2 stream of x (all or nothing). */
canon_status canon_root_stream_write(const canon_root *x, canon_buf *out);

/* spec 11.1: the output size of every image x^g.  For a subset, a graph and a DAG without
 * subgroup or labeling-coset leaves this is the exact length of x's stream, which is the same
 * for every image; for a DAG with such leaves it is the conservative input-derived bound of
 * canon_dag_image_bound (the canonical Group bytes of a conjugate subgroup can differ in
 * length, docs/slices/S5-notes.md).  Either way it depends only on the orbit of x. */
canon_status canon_root_stream_size(const canon_root *x, uint64_t *size_out);

/* Record, reference and literal-byte counts of x's normal form (spec 11.1, detailed plan 2.1:
 * the descriptor's max_nodes, max_refs, max_literal_bytes): a subset of k atoms has k + 1
 * records and k references, a graph one record, a DAG its arena's counts.  Graph colours and
 * labels are not literal records and are not counted (max_output_bytes covers them). */
void canon_root_counts(const canon_root *x, uint64_t *nodes, uint64_t *refs,
                       uint64_t *literal_bytes);

/* spec 17, 4.1, 4.2 (slice S5): import x from a CDAG-2 stream of degree `degree` (the header's
 * U32(n) must equal it): strict decode (src/encoding/cdag_decode.h), extensional
 * normalisation, the limits of the normal form, then the root kind (docs/slices/S5.md 3.4):
 * a set whose children are all atoms (including the empty set) is a subset root, a graph
 * record a graph root (both built exactly as the S1/S2 builders build them), anything else a
 * DAG root.  *reason (may be NULL) receives the decoder's reason.  On failure x is an empty
 * subset of degree `degree` and canon_root_free(x) is valid. */
canon_status canon_root_import_stream(canon_root *x, uint32_t degree, const uint8_t *stream,
                                      size_t length, const canon_dag_limits *limits,
                                      canon_cdag_reason *reason);

#endif /* CANON_SRC_OBJECT_OBJECT_H */
