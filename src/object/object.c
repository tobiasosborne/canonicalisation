/* Root-object dispatch (spec sections 2.1, 4.1, 7.1, 11.1). */
#include "object/object.h"

#include <stdlib.h>

#include "arena/checked.h"
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
    }
}

uint32_t canon_root_initial_key(const canon_root *x, uint32_t a)
{
    switch (x->kind) {
    case CANON_ROOT_SUBSET:
        return canon_subset_initial_key(&x->u.subset, a); /* spec 7.1: membership 0/1 */
    case CANON_ROOT_GRAPH:
        return canon_graph_initial_key(&x->u.graph, a); /* spec 7.1: B(vertex_colour[a]) rank */
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
}

void canon_root_image_free(canon_root_image *img)
{
    if (img->has_storage) {
        canon_root_free(&img->root);
    }
    canon_root_image_init(img);
}

/* Subset image storage for degree n: k <= n atoms and ceil(n/64) bitset words (grow-only). */
static canon_status reserve_subset(canon_root_image *img, uint32_t n)
{
    if (img->has_storage && n <= img->cap) {
        return CANON_COMPLETE;
    }
    size_t atom_bytes = 0, bit_bytes = 0;
    size_t words = canon_bitset_words(n) > 0 ? canon_bitset_words(n) : 1u;
    if (!canon_size_mul(n > 0 ? (size_t)n : 1u, sizeof(uint32_t), &atom_bytes) ||
        !canon_size_mul(words, sizeof(uint64_t), &bit_bytes)) {
        return CANON_CAPACITY_LIMIT; /* spec 11.1 */
    }
    uint32_t *atoms = malloc(atom_bytes);
    uint64_t *bits = malloc(bit_bytes);
    if (atoms == NULL || bits == NULL) {
        free(atoms);
        free(bits);
        return CANON_RESOURCE_LIMIT;
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
    }
    img->root.kind = x->kind;
    img->root.n = x->n;
    return CANON_COMPLETE;
}

canon_status canon_root_stream_write(const canon_root *x, canon_buf *out)
{
    switch (x->kind) {
    case CANON_ROOT_SUBSET:
        return canon_subset_stream_write(out, x->n, x->u.subset.atoms, x->u.subset.k);
    case CANON_ROOT_GRAPH:
        return canon_graph_stream_write(out, &x->u.graph);
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
    }
    return CANON_INTERNAL_ERROR;
}
