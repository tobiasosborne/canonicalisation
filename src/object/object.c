/* Root-object dispatch (spec sections 2.1, 4.1, 4.2, 7.1, 11.1). */
#include "object/object.h"

#include <stdlib.h>
#include <string.h>

#include "arena/alloc.h"
#include "encoding/cdag_encode.h"
#include "encoding/graph_stream.h"
#include "encoding/subset_stream.h"

void canon_root_free(canon_root *x)
{
    switch (x->kind) {
    case CANON_ROOT_SUBSET:
        canon_subset_free(&x->u.subset);
        break;
    case CANON_ROOT_GRAPH:
        canon_graph_free(&x->u.graph);
        break;
    case CANON_ROOT_DAG:
        canon_dag_free(&x->u.dag);
        break;
    }
}

uint32_t canon_root_initial_key(const canon_root *x, uint32_t a)
{
    switch (x->kind) {
    case CANON_ROOT_SUBSET:
        return canon_subset_initial_key(&x->u.subset, a); /* spec 7.1: membership 0/1 */
    case CANON_ROOT_GRAPH:
        return canon_graph_initial_key(&x->u.graph, a); /* spec 7.1: B(vertex_colour[a]) rank */
    case CANON_ROOT_DAG:
        (void)a;
        return 0; /* spec 7.1: "on every other root it is the empty key" */
    }
    return 0;
}

void canon_root_image_init(canon_root_image *img)
{
    img->root.kind = CANON_ROOT_SUBSET;
    img->root.n = 0;
    img->root.u.subset = (canon_subset){0, 0, NULL, NULL};
    img->has_storage = false;
    img->cap = 0;
    canon_dag_scratch_init(&img->dag_scratch);
}

void canon_root_image_free(canon_root_image *img)
{
    if (img->has_storage) {
        canon_root_free(&img->root);
    }
    canon_dag_scratch_free(&img->dag_scratch);
    canon_root_image_init(img);
}

/* Subset image storage for degree n: k <= n atoms and ceil(n/64) bitset words (grow-only). */
static canon_status reserve_subset(canon_root_image *img, uint32_t n)
{
    if (img->has_storage && n <= img->cap) {
        return CANON_COMPLETE;
    }
    canon_status st = CANON_COMPLETE;
    uint32_t *atoms = canon_alloc_array(n, sizeof *atoms, &st);
    uint64_t *bits = canon_alloc_array(canon_bitset_words(n), sizeof *bits, &st);
    if (atoms == NULL || bits == NULL) {
        free(atoms);
        free(bits);
        return st;
    }
    canon_root_image_free(img);
    img->root.kind = CANON_ROOT_SUBSET;
    img->root.u.subset = (canon_subset){0, 0, atoms, bits};
    img->has_storage = true;
    img->cap = n;
    return CANON_COMPLETE;
}

canon_status canon_root_act_into(const canon_root *x, const uint32_t *g, canon_root_image *img)
{
    if (img->has_storage && img->root.kind != x->kind) {
        canon_root_image_free(img); /* switch kinds */
    }
    canon_status st = CANON_COMPLETE;
    switch (x->kind) {
    case CANON_ROOT_SUBSET:
        st = reserve_subset(img, x->n);
        if (st != CANON_COMPLETE) {
            return st;
        }
        /* spec 2.1: x^g = {g[a] : a in x}; the bitset holds the image's members afterwards. */
        canon_subset_act_sorted(&x->u.subset, g, img->root.u.subset.bits,
                                img->root.u.subset.atoms);
        img->root.u.subset.n = x->n;
        img->root.u.subset.k = x->u.subset.k;
        break;
    case CANON_ROOT_GRAPH:
        if (!img->has_storage) {
            img->root.kind = CANON_ROOT_GRAPH;
            canon_graph_init_empty(&img->root.u.graph);
            img->has_storage = true;
        }
        /* spec 2.1: colours move with their vertices, arcs map endpoint-wise. */
        st = canon_graph_act_into(&x->u.graph, g, &img->root.u.graph);
        if (st != CANON_COMPLETE) {
            return st;
        }
        break;
    case CANON_ROOT_DAG:
        if (!img->has_storage) {
            img->root.kind = CANON_ROOT_DAG;
            canon_dag_init(&img->root.u.dag, x->n);
            img->has_storage = true;
        }
        /* spec 2.1 recursively: atoms relabelled, leaves conjugated, re-normalised (spec 4.2) */
        st = canon_dag_act(&x->u.dag, g, &img->root.u.dag, &img->dag_scratch);
        if (st != CANON_COMPLETE) {
            return st;
        }
        break;
    }
    img->root.kind = x->kind;
    img->root.n = x->n;
    return CANON_COMPLETE;
}

void canon_root_image_clear(canon_root_image *img)
{
    if (img->has_storage && img->root.kind == CANON_ROOT_GRAPH) {
        canon_graph_image_clear(&img->root.u.graph); /* drop the borrowed tables */
    }
    img->root.n = 0;
    if (img->has_storage && img->root.kind == CANON_ROOT_SUBSET) {
        img->root.u.subset.n = 0; /* owns its arrays; nothing borrowed */
        img->root.u.subset.k = 0;
    }
    if (img->has_storage && img->root.kind == CANON_ROOT_DAG) {
        canon_dag_reset(&img->root.u.dag, 0); /* owns its arena; nothing borrowed */
    }
}

bool canon_root_equal(const canon_root *a, const canon_root *b)
{
    if (a->kind != b->kind || a->n != b->n) {
        return false;
    }
    switch (a->kind) {
    case CANON_ROOT_SUBSET:
        return canon_subset_equal(&a->u.subset, &b->u.subset);
    case CANON_ROOT_GRAPH:
        return canon_graph_equal(&a->u.graph, &b->u.graph);
    case CANON_ROOT_DAG:
        return canon_dag_equal(&a->u.dag, &b->u.dag); /* spec 4.2: normal forms */
    }
    return false;
}

canon_status canon_root_stream_write(const canon_root *x, canon_buf *out)
{
    switch (x->kind) {
    case CANON_ROOT_SUBSET:
        return canon_subset_stream_write(out, x->n, x->u.subset.atoms, x->u.subset.k);
    case CANON_ROOT_GRAPH:
        return canon_graph_stream_write(out, &x->u.graph);
    case CANON_ROOT_DAG:
        return canon_dag_stream_write(out, &x->u.dag); /* spec 4.1, 4.2 general encoder */
    }
    return CANON_INTERNAL_ERROR;
}

canon_status canon_root_stream_size(const canon_root *x, uint64_t *size_out)
{
    switch (x->kind) {
    case CANON_ROOT_SUBSET:
        /* the image has as many members as x */
        return canon_subset_stream_size(x->u.subset.k, size_out);
    case CANON_ROOT_GRAPH:
        /* spec 11.1: the colour multiset, the arc count, the label bytes and the Nat lengths are
         * unchanged by the action, so this is the exact image length */
        return canon_graph_stream_size(&x->u.graph, size_out);
    case CANON_ROOT_DAG:
        /* exact without subgroup/coset leaves, else conservative (canon_dag_image_bound) */
        *size_out = x->u.dag.image_bound;
        return CANON_COMPLETE;
    }
    return CANON_INTERNAL_ERROR;
}

void canon_root_counts(const canon_root *x, uint64_t *nodes, uint64_t *refs,
                       uint64_t *literal_bytes)
{
    *nodes = *refs = *literal_bytes = 0;
    switch (x->kind) {
    case CANON_ROOT_SUBSET:
        /* spec 4.2: k atom records and one set record with k references */
        *nodes = (uint64_t)x->u.subset.k + 1u;
        *refs = x->u.subset.k;
        break;
    case CANON_ROOT_GRAPH:
        *nodes = 1u; /* one 09 record with no child references */
        break;
    case CANON_ROOT_DAG:
        *nodes = x->u.dag.count;
        *refs = x->u.dag.refs;
        *literal_bytes = x->u.dag.literal_bytes;
        break;
    }
}

/* spec 7.1, docs/slices/S5.md 3.4: give the normalised arena d (consumed) its root kind. */
static canon_status root_from_dag(canon_root *x, canon_dag *d)
{
    const uint32_t n = d->n;
    const canon_rec *top = &d->recs[d->root];
    canon_status st = CANON_COMPLETE;
    bool atoms_only = top->tag == CANON_REC_SET;
    for (uint32_t j = 0; atoms_only && j < top->child_count; ++j) {
        atoms_only = d->recs[d->child[top->child_off + j]].tag == CANON_REC_ATOM;
    }
    if (atoms_only) {
        /* spec 7.1: "the top-level subset (a set consisting only of atom nodes) ... Empty sets
         * qualify as subsets": the S1 subset object, built exactly as the builder builds it */
        uint32_t *atoms = canon_alloc_array(top->child_count, sizeof *atoms, &st);
        if (atoms != NULL) {
            for (uint32_t j = 0; j < top->child_count; ++j) {
                atoms[j] = canon_dag_atom(d, d->child[top->child_off + j]);
            }
            st = canon_subset_init(&x->u.subset, n, atoms, top->child_count);
            free(atoms);
        }
        if (st == CANON_COMPLETE) {
            x->kind = CANON_ROOT_SUBSET;
        }
    } else if (top->tag == CANON_REC_GRAPH) {
        /* spec 7.1 "a top-level graph": the S2 graph object that the normaliser imported
         * (canon_graph_init: tables, combined sorted arcs, CSR/CSC index, cached stream size,
         * exactly as the builder) is handed over; the stream is not imported twice (S5 review
         * item 7) */
        if (d->graph != NULL) {
            x->u.graph = *d->graph; /* moved: the arrays now belong to the root */
            free(d->graph);
            d->graph = NULL;
            x->kind = CANON_ROOT_GRAPH;
        } else {
            st = CANON_INTERNAL_ERROR; /* canon_cdag_import always keeps a graph root's object */
        }
    } else {
        /* spec 7.1: "on every other root it is the empty key": a DAG root keeps its arena */
        x->kind = CANON_ROOT_DAG;
        x->u.dag = *d;
        canon_dag_init(d, n); /* moved: d no longer owns the storage */
    }
    canon_dag_free(d);
    return st;
}

canon_status canon_root_import_stream(canon_root *x, uint32_t degree, const uint8_t *stream,
                                      size_t length, const canon_dag_limits *limits,
                                      canon_cdag_reason *reason)
{
    canon_cdag_reason local = CANON_CDAG_OK;
    reason = reason != NULL ? reason : &local;
    memset(x, 0, sizeof *x);
    x->kind = CANON_ROOT_SUBSET; /* an empty subset until the import succeeds */
    x->n = degree;
    x->u.subset = (canon_subset){degree, 0, NULL, NULL};
    canon_dag d;
    canon_dag_init(&d, degree);
    canon_status st = canon_cdag_import(stream, length, degree, limits, &d, reason);
    if (st != CANON_COMPLETE) {
        canon_dag_free(&d);
        return st;
    }
    return root_from_dag(x, &d);
}
