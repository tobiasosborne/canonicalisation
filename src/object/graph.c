/* The coloured directed multigraph object (spec sections 2.1, 4.1, 7.1, 10, 11.1). */
#include "object/graph.h"

#include <stdlib.h>
#include <string.h>

#include "arena/checked.h"
#include "util/sort.h"

/* ---- allocation helpers (spec 11.1: sizes checked before allocation) ---- */

/* *out = malloc(max(count, 1) * size), checked.  CAPACITY_LIMIT if the product overflows,
 * RESOURCE_LIMIT if malloc fails. */
static canon_status alloc_array(size_t count, size_t size, void **out)
{
    size_t bytes = 0;
    *out = NULL;
    if (!canon_size_mul(count > 0 ? count : 1u, size, &bytes)) {
        return CANON_CAPACITY_LIMIT;
    }
    *out = malloc(bytes);
    return *out != NULL ? CANON_COMPLETE : CANON_RESOURCE_LIMIT;
}

void canon_graph_init_empty(canon_graph *g)
{
    memset(g, 0, sizeof *g);
}

static void table_free(canon_byte_table *t)
{
    free(t->offset);
    free(t->pool);
    memset(t, 0, sizeof *t);
}

void canon_graph_free(canon_graph *g)
{
    if (!g->borrowed_tables) {
        table_free(&g->colours);
        table_free(&g->labels);
    }
    free(g->colour_id);
    free(g->arcs);
    free(g->out_start);
    free(g->in_start);
    free(g->in_arc);
    free(g->arcs_tmp);
    canon_graph_init_empty(g);
}

/* ---- byte-string interning ---- */

typedef struct str_ref {
    const uint8_t *bytes;
    size_t length;
    size_t src; /* input position */
} str_ref;

/* spec 4.1: "Labels compare by (byte length, unsigned bytes)"; the same order is B(s) order
 * (spec 4.3: "a B field compares length first"), used for colours by the spec 7.1 key. */
static int bytes_order(const uint8_t *a, size_t al, const uint8_t *b, size_t bl)
{
    if (al != bl) {
        return al < bl ? -1 : 1;
    }
    if (al == 0) {
        return 0;
    }
    int c = memcmp(a, b, al); /* memcmp compares as unsigned char */
    return c < 0 ? -1 : (c > 0 ? 1 : 0);
}

static int str_ref_cmp(const void *a, const void *b, void *ctx)
{
    (void)ctx;
    const str_ref *x = a, *y = b;
    return bytes_order(x->bytes, x->length, y->bytes, y->length);
}

/* Build the table of the distinct strings among refs[0..count) sorted by (length, bytes), and
 * ids[refs[i].src] = id of refs[i]'s string.  refs is reordered.  Every length already fits U32
 * (checked by the caller). */
static canon_status intern(str_ref *refs, size_t count, canon_byte_table *table, uint32_t *ids)
{
    memset(table, 0, sizeof *table);
    if (count == 0) {
        return CANON_COMPLETE;
    }
    str_ref *tmp = NULL;
    canon_status st = alloc_array(count, sizeof *tmp, (void **)&tmp);
    if (st != CANON_COMPLETE) {
        return st;
    }
    canon_stable_sort(refs, count, sizeof *refs, tmp, str_ref_cmp, NULL);
    free(tmp);
    /* Count distinct strings and their total length (spec 11.1: checked). */
    size_t distinct = 0, pool_bytes = 0;
    for (size_t i = 0; i < count; ++i) {
        if (i == 0 || str_ref_cmp(&refs[i - 1], &refs[i], NULL) != 0) {
            ++distinct;
            if (!canon_size_add(pool_bytes, refs[i].length, &pool_bytes)) {
                return CANON_CAPACITY_LIMIT;
            }
        }
    }
    if ((uint64_t)distinct > UINT32_MAX) {
        return CANON_CAPACITY_LIMIT; /* ids are uint32 */
    }
    st = alloc_array(distinct + 1u, sizeof *table->offset, (void **)&table->offset);
    if (st == CANON_COMPLETE && pool_bytes > 0) {
        st = alloc_array(pool_bytes, 1u, (void **)&table->pool);
    }
    if (st != CANON_COMPLETE) {
        table_free(table);
        return st;
    }
    uint32_t id = 0;
    size_t at = 0;
    table->offset[0] = 0;
    for (size_t i = 0; i < count; ++i) {
        bool first = i == 0 || str_ref_cmp(&refs[i - 1], &refs[i], NULL) != 0;
        if (first && i > 0) {
            ++id; /* a new distinct string: ids increase with (length, bytes) */
        }
        ids[refs[i].src] = id;
        if (first) {
            if (refs[i].length > 0) {
                memcpy(table->pool + at, refs[i].bytes, refs[i].length);
            }
            at += refs[i].length;
            table->offset[id + 1u] = at;
        }
    }
    table->count = (uint32_t)distinct;
    return CANON_COMPLETE;
}

/* ---- arcs ---- */

/* spec 4.1: arcs sorted by (source, target, B(label)); label ids are B(label) ranks. */
static int arc_cmp(const void *a, const void *b, void *ctx)
{
    (void)ctx;
    const canon_graph_arc *x = a, *y = b;
    if (x->source != y->source) {
        return x->source < y->source ? -1 : 1;
    }
    if (x->target != y->target) {
        return x->target < y->target ? -1 : 1;
    }
    if (x->label != y->label) {
        return x->label < y->label ? -1 : 1;
    }
    return 0;
}

/* CSR by source and CSC by target over g->arcs (spec 10: CSR/CSC traversal visits only
 * relevant incidences).  Arrays must hold n + 1 and e entries. */
static void build_index(canon_graph *g)
{
    const uint32_t n = g->n;
    memset(g->out_start, 0, ((size_t)n + 1u) * sizeof *g->out_start);
    memset(g->in_start, 0, ((size_t)n + 1u) * sizeof *g->in_start);
    for (uint32_t i = 0; i < g->e; ++i) {
        g->out_start[g->arcs[i].source + 1u] += 1u;
        g->in_start[g->arcs[i].target + 1u] += 1u;
    }
    for (uint32_t v = 0; v < n; ++v) {
        g->out_start[v + 1u] += g->out_start[v];
        g->in_start[v + 1u] += g->in_start[v];
    }
    /* Counting placement: in_start[v] serves as the cursor of v, ending at the start of v + 1;
     * then shift back.  Arcs of one target stay in increasing arc order. */
    for (uint32_t i = 0; i < g->e; ++i) {
        g->in_arc[g->in_start[g->arcs[i].target]++] = i;
    }
    for (uint32_t v = n; v > 0; --v) {
        g->in_start[v] = g->in_start[v - 1u];
    }
    g->in_start[0] = 0;
}

canon_status canon_graph_init(canon_graph *g, uint32_t n, const uint8_t *const *colours,
                              const size_t *colour_lengths, const canon_arc *arcs,
                              size_t arc_count)
{
    canon_graph_init_empty(g);
    /* Validation first (spec 4.1: "out-of-domain atom IDs and malformed fields are invalid";
     * "input zero multiplicities are invalid"), then the capacity checks (spec 4.1: "Lengths
     * and counts must fit U32; overflow is a capacity error"). */
    if ((colours == NULL) != (colour_lengths == NULL) || (arc_count > 0 && arcs == NULL)) {
        return CANON_INVALID_INPUT;
    }
    for (uint32_t v = 0; colours != NULL && v < n; ++v) {
        if (colours[v] == NULL && colour_lengths[v] > 0) {
            return CANON_INVALID_INPUT;
        }
    }
    for (size_t i = 0; i < arc_count; ++i) {
        if (arcs[i].source >= n || arcs[i].target >= n || arcs[i].multiplicity == 0 ||
            (arcs[i].label == NULL && arcs[i].label_length > 0)) {
            return CANON_INVALID_INPUT;
        }
    }
    for (uint32_t v = 0; colours != NULL && v < n; ++v) {
        if ((uint64_t)colour_lengths[v] > UINT32_MAX) {
            return CANON_CAPACITY_LIMIT; /* spec 4.1: B(vertex_colour) length fits U32 */
        }
    }
    for (size_t i = 0; i < arc_count; ++i) {
        if ((uint64_t)arcs[i].label_length > UINT32_MAX) {
            return CANON_CAPACITY_LIMIT; /* spec 4.1: B(label) length fits U32 */
        }
    }

    g->n = n;
    str_ref *refs = NULL;
    uint32_t *label_ids = NULL;
    canon_graph_arc *work = NULL, *tmp = NULL;
    size_t ref_count = arc_count > (size_t)n ? arc_count : (size_t)n;
    canon_status st = alloc_array(ref_count, sizeof *refs, (void **)&refs);
    if (st == CANON_COMPLETE) {
        st = alloc_array(arc_count, sizeof *label_ids, (void **)&label_ids);
    }
    if (st == CANON_COMPLETE) {
        st = alloc_array(arc_count, sizeof *work, (void **)&work);
    }
    if (st == CANON_COMPLETE) {
        st = alloc_array(arc_count, sizeof *tmp, (void **)&tmp);
    }
    if (st == CANON_COMPLETE) {
        st = alloc_array(n, sizeof *g->colour_id, (void **)&g->colour_id);
    }
    if (st != CANON_COMPLETE) {
        goto fail;
    }

    /* spec 7.1 key B(vertex_colour[a]): intern colours in (length, bytes) order. */
    for (uint32_t v = 0; v < n; ++v) {
        refs[v].bytes = colours != NULL ? colours[v] : NULL;
        refs[v].length = colours != NULL ? colour_lengths[v] : 0;
        refs[v].src = v;
    }
    st = intern(refs, n, &g->colours, g->colour_id);
    if (st != CANON_COMPLETE) {
        goto fail;
    }
    /* spec 4.1: labels compare by (byte length, unsigned bytes), including the empty label. */
    for (size_t i = 0; i < arc_count; ++i) {
        refs[i].bytes = arcs[i].label;
        refs[i].length = arcs[i].label_length;
        refs[i].src = i;
    }
    st = intern(refs, arc_count, &g->labels, label_ids);
    if (st != CANON_COMPLETE) {
        goto fail;
    }

    /* spec 4.1: "Combine duplicate (source,target,label) arcs by exact addition, delete none with
     * positive multiplicity, and sort by (source,target,B(label))". */
    for (size_t i = 0; i < arc_count; ++i) {
        work[i].source = arcs[i].source;
        work[i].target = arcs[i].target;
        work[i].label = label_ids[i];
        work[i].multiplicity = arcs[i].multiplicity;
    }
    canon_stable_sort(work, arc_count, sizeof *work, tmp, arc_cmp, NULL);
    size_t e = 0;
    uint64_t total = 0;
    for (size_t i = 0; i < arc_count; ++i) {
        /* spec 11.1: "Built-in graph counts cannot exceed the total positive input
         * multiplicity; compute that bound exactly during import." */
        if (work[i].multiplicity > UINT64_MAX - total) {
            st = CANON_CAPACITY_LIMIT; /* count-bit limit 64 (detailed plan 2.1) */
            goto fail;
        }
        total += work[i].multiplicity;
        if (e > 0 && arc_cmp(&work[e - 1], &work[i], NULL) == 0) {
            /* exact addition; cannot overflow because the total did not */
            work[e - 1].multiplicity += work[i].multiplicity;
        } else {
            work[e++] = work[i];
        }
    }
    if ((uint64_t)e > UINT32_MAX) {
        st = CANON_CAPACITY_LIMIT; /* spec 4.1: U32(e) */
        goto fail;
    }
    g->e = (uint32_t)e;
    g->total_multiplicity = total;
    g->arcs = work;
    work = NULL;
    if (e > 0 && e < arc_count) {
        canon_graph_arc *shrunk = realloc(g->arcs, e * sizeof *g->arcs);
        if (shrunk != NULL) {
            g->arcs = shrunk; /* a failed shrink keeps the larger block */
        }
    }
    st = alloc_array((size_t)n + 1u, sizeof *g->out_start, (void **)&g->out_start);
    if (st == CANON_COMPLETE) {
        st = alloc_array((size_t)n + 1u, sizeof *g->in_start, (void **)&g->in_start);
    }
    if (st == CANON_COMPLETE) {
        st = alloc_array(e, sizeof *g->in_arc, (void **)&g->in_arc);
    }
    if (st != CANON_COMPLETE) {
        goto fail;
    }
    build_index(g);
    free(refs);
    free(label_ids);
    free(tmp);
    return CANON_COMPLETE;

fail:
    free(refs);
    free(label_ids);
    free(work);
    free(tmp);
    canon_graph_free(g);
    return st;
}

/* ---- simple undirected wrapper (spec 4.1) ---- */

typedef struct edge_pair {
    uint32_t a, b; /* a < b */
} edge_pair;

static int edge_cmp(const void *x, const void *y, void *ctx)
{
    (void)ctx;
    const edge_pair *p = x, *q = y;
    if (p->a != q->a) {
        return p->a < q->a ? -1 : 1;
    }
    if (p->b != q->b) {
        return p->b < q->b ? -1 : 1;
    }
    return 0;
}

canon_status canon_graph_init_simple(canon_graph *g, uint32_t n, const uint32_t (*edges)[2],
                                     size_t edge_count)
{
    canon_graph_init_empty(g);
    if (edge_count > 0 && edges == NULL) {
        return CANON_INVALID_INPUT;
    }
    for (size_t i = 0; i < edge_count; ++i) {
        if (edges[i][0] >= n || edges[i][1] >= n) {
            return CANON_INVALID_INPUT; /* spec 4.1: out-of-domain atom IDs are invalid */
        }
        if (edges[i][0] == edges[i][1]) {
            return CANON_INVALID_INPUT; /* spec 4.1: "wrappers for simple graphs reject loops" */
        }
    }
    edge_pair *pairs = NULL, *tmp = NULL;
    canon_arc *arcs = NULL;
    canon_status st = alloc_array(edge_count, sizeof *pairs, (void **)&pairs);
    if (st == CANON_COMPLETE) {
        st = alloc_array(edge_count, sizeof *tmp, (void **)&tmp);
    }
    if (st != CANON_COMPLETE) {
        free(pairs);
        free(tmp);
        return st;
    }
    /* An undirected edge is the unordered pair {a, b}: normalise to a < b. */
    for (size_t i = 0; i < edge_count; ++i) {
        uint32_t a = edges[i][0], b = edges[i][1];
        pairs[i].a = a < b ? a : b;
        pairs[i].b = a < b ? b : a;
    }
    canon_stable_sort(pairs, edge_count, sizeof *pairs, tmp, edge_cmp, NULL);
    free(tmp);
    /* spec 4.1: "coalesce duplicate undirected edges" */
    size_t m = 0;
    for (size_t i = 0; i < edge_count; ++i) {
        if (m == 0 || edge_cmp(&pairs[m - 1], &pairs[i], NULL) != 0) {
            pairs[m++] = pairs[i];
        }
    }
    size_t arc_count = 0;
    if (!canon_size_mul(m, 2u, &arc_count)) {
        free(pairs);
        return CANON_CAPACITY_LIMIT;
    }
    st = alloc_array(arc_count, sizeof *arcs, (void **)&arcs);
    if (st != CANON_COMPLETE) {
        free(pairs);
        return st;
    }
    /* spec 4.1: "translating to two opposite unit arcs"; empty labels, empty colours. */
    for (size_t i = 0; i < m; ++i) {
        arcs[2 * i] = (canon_arc){pairs[i].a, pairs[i].b, NULL, 0, 1};
        arcs[2 * i + 1] = (canon_arc){pairs[i].b, pairs[i].a, NULL, 0, 1};
    }
    free(pairs);
    st = canon_graph_init(g, n, NULL, NULL, arcs, arc_count);
    free(arcs);
    return st;
}

/* ---- action (spec 2.1) ---- */

/* Grow image storage to hold n vertices and e arcs (grow-only). */
static canon_status reserve_image(canon_graph *d, uint32_t n, uint32_t e)
{
    if (d->colour_id == NULL || n > d->cap_n) {
        uint32_t *cid = NULL, *os = NULL, *is = NULL;
        canon_status st = alloc_array(n, sizeof *cid, (void **)&cid);
        if (st == CANON_COMPLETE) {
            st = alloc_array((size_t)n + 1u, sizeof *os, (void **)&os);
        }
        if (st == CANON_COMPLETE) {
            st = alloc_array((size_t)n + 1u, sizeof *is, (void **)&is);
        }
        if (st != CANON_COMPLETE) {
            free(cid);
            free(os);
            free(is);
            return st;
        }
        free(d->colour_id);
        free(d->out_start);
        free(d->in_start);
        d->colour_id = cid;
        d->out_start = os;
        d->in_start = is;
        d->cap_n = n;
    }
    if (d->arcs == NULL || e > d->cap_e) {
        canon_graph_arc *arcs = NULL, *tmp = NULL;
        uint32_t *in_arc = NULL;
        canon_status st = alloc_array(e, sizeof *arcs, (void **)&arcs);
        if (st == CANON_COMPLETE) {
            st = alloc_array(e, sizeof *tmp, (void **)&tmp);
        }
        if (st == CANON_COMPLETE) {
            st = alloc_array(e, sizeof *in_arc, (void **)&in_arc);
        }
        if (st != CANON_COMPLETE) {
            free(arcs);
            free(tmp);
            free(in_arc);
            return st;
        }
        free(d->arcs);
        free(d->arcs_tmp);
        free(d->in_arc);
        d->arcs = arcs;
        d->arcs_tmp = tmp;
        d->in_arc = in_arc;
        d->cap_e = e;
    }
    return CANON_COMPLETE;
}

canon_status canon_graph_act_into(const canon_graph *g, const uint32_t *p, canon_graph *dest)
{
    if (!dest->borrowed_tables) {
        /* dest may have owned tables only if it was imported; image storage never does. */
        table_free(&dest->colours);
        table_free(&dest->labels);
    }
    canon_status st = reserve_image(dest, g->n, g->e);
    if (st != CANON_COMPLETE) {
        return st;
    }
    dest->n = g->n;
    dest->e = g->e;
    /* spec 2.1: "Graph colours and arc labels are fixed byte strings; they are never freely
     * renamed": the image uses the same tables. */
    dest->colours = g->colours;
    dest->labels = g->labels;
    dest->borrowed_tables = true;
    dest->total_multiplicity = g->total_multiplicity;
    /* spec 2.1: atom a maps to p[a], so vertex p[v] of the image carries v's colour. */
    for (uint32_t v = 0; v < g->n; ++v) {
        dest->colour_id[p[v]] = g->colour_id[v];
    }
    /* spec 2.1: "preserves ... multiplicities": (s, t, l, m) -> (p[s], p[t], l, m).  p is a
     * bijection, so distinct (s, t, l) stay distinct and nothing combines. */
    for (uint32_t i = 0; i < g->e; ++i) {
        dest->arcs[i].source = p[g->arcs[i].source];
        dest->arcs[i].target = p[g->arcs[i].target];
        dest->arcs[i].label = g->arcs[i].label;
        dest->arcs[i].multiplicity = g->arcs[i].multiplicity;
    }
    /* spec 4.1: re-sort by (source, target, B(label)). */
    canon_stable_sort(dest->arcs, g->e, sizeof *dest->arcs, dest->arcs_tmp, arc_cmp, NULL);
    build_index(dest);
    return CANON_COMPLETE;
}

/* ---- equality (spec 4.2: extensional) ---- */

static bool table_entry_equal(const canon_byte_table *ta, uint32_t ia, const canon_byte_table *tb,
                              uint32_t ib)
{
    size_t la = 0, lb = 0;
    const uint8_t *a = canon_byte_table_get(ta, ia, &la);
    const uint8_t *b = canon_byte_table_get(tb, ib, &lb);
    return bytes_order(a, la, b, lb) == 0;
}

bool canon_graph_equal(const canon_graph *a, const canon_graph *b)
{
    if (a->n != b->n || a->e != b->e) {
        return false;
    }
    for (uint32_t v = 0; v < a->n; ++v) {
        if (!table_entry_equal(&a->colours, a->colour_id[v], &b->colours, b->colour_id[v])) {
            return false;
        }
    }
    for (uint32_t i = 0; i < a->e; ++i) {
        const canon_graph_arc *x = &a->arcs[i], *y = &b->arcs[i];
        if (x->source != y->source || x->target != y->target ||
            x->multiplicity != y->multiplicity ||
            !table_entry_equal(&a->labels, x->label, &b->labels, y->label)) {
            return false;
        }
    }
    return true;
}
