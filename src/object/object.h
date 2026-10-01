/*
 * Internal header: the root object of a problem (spec sections 2.1, 4.1, 7.1), dispatching on
 * the object kind so that refinement and search are not kind-specific.  Slice S2
 * (docs/slices/S2.md section 3.1).  Kinds: the top-level subset (S1, src/object/subset.h) and
 * the top-level coloured directed multigraph (S2, src/object/graph.h); nested DAGs arrive in
 * S5.  The public canon_object wraps one canon_root.
 */
#ifndef CANON_SRC_OBJECT_OBJECT_H
#define CANON_SRC_OBJECT_OBJECT_H

#include <stdbool.h>
#include <stdint.h>

#include "canon/canon.h"
#include "encoding/wire.h"
#include "object/graph.h"
#include "object/subset.h"

typedef enum { CANON_ROOT_SUBSET, CANON_ROOT_GRAPH } canon_root_kind;

typedef struct canon_root {
    canon_root_kind kind;
    uint32_t n; /* degree of the atom domain (spec 2.1) */
    union {
        canon_subset subset;
        canon_graph graph;
    } u;
} canon_root;

/* Release the root's storage; valid after a failed init (the caller zero-initialises it). */
void canon_root_free(canon_root *x);

/* spec 7.1 initial key of atom a: "On the top-level subset ..., the initial key of a is
 * membership 0/1; on a top-level graph it is B(vertex_colour[a])", returned as a rank so that
 * increasing key equals increasing B(colour) (the colour table is sorted by B order). */
uint32_t canon_root_initial_key(const canon_root *x, uint32_t a);

/* Reusable storage for images of roots (spec 17: workspace storage reused across solves).
 * Grows only; switching kinds frees the other kind's storage. */
typedef struct canon_root_image {
    canon_root root;  /* the last image produced */
    bool has_storage; /* root.kind's storage is allocated */
    uint32_t cap;     /* subset images: degree the atom list and bitset are allocated for */
} canon_root_image;

void canon_root_image_init(canon_root_image *img);
void canon_root_image_free(canon_root_image *img);

/* spec 2.1 action ATOM-TRANSPORT-1: img->root = x^g, for g a permutation of {0..n-1}.  A graph
 * image borrows x's colour and label tables (they are never renamed), so it is valid while x
 * is.  CANON_RESOURCE_LIMIT / CANON_CAPACITY_LIMIT if the storage cannot grow. */
canon_status canon_root_act_into(const canon_root *x, const uint32_t *g, canon_root_image *img);

/* spec 4.1, 4.2: append the complete CDAG-2 stream of x (all or nothing). */
canon_status canon_root_stream_write(const canon_root *x, canon_buf *out);

/* spec 11.1: the exact length of x's stream.  It is the same for every image x^g, so it is the
 * exact output size of the canonical image. */
canon_status canon_root_stream_size(const canon_root *x, uint64_t *size_out);

#endif /* CANON_SRC_OBJECT_OBJECT_H */
