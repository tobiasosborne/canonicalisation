/* Ordered partitions and the O-stage signatures of canon-check (spec 7.1, 10).  Deliberately
 * simple: whole-array splits and dense exact count vectors. */
#include <stdlib.h>
#include <string.h>

#include "cc.h"

cc_reason cc_partition_init(cc_partition *p, uint32_t n)
{
    cc_reason why = CC_ACCEPT;
    memset(p, 0, sizeof *p);
    p->n = n;
    p->lab = cc_alloc(n, sizeof *p->lab, &why);
    p->start = cc_alloc((size_t)n + 1u, sizeof *p->start, &why);
    p->cell_of = cc_alloc(n, sizeof *p->cell_of, &why);
    if (p->lab == NULL || p->start == NULL || p->cell_of == NULL) {
        cc_partition_free(p);
        return why;
    }
    /* the unit partition: one cell 0..n-1, none for n = 0 (spec 7.1) */
    for (uint32_t v = 0; v < n; ++v) {
        p->lab[v] = v;
        p->cell_of[v] = 0;
    }
    p->start[0] = 0;
    p->cells = n > 0 ? 1 : 0;
    p->start[p->cells] = n;
    return CC_ACCEPT;
}

void cc_partition_free(cc_partition *p)
{
    free(p->lab);
    free(p->start);
    free(p->cell_of);
    memset(p, 0, sizeof *p);
}

void cc_partition_copy(cc_partition *dst, const cc_partition *src)
{
    const uint32_t n = src->n;
    dst->cells = src->cells;
    if (n > 0) {
        memcpy(dst->lab, src->lab, (size_t)n * sizeof *dst->lab);
        memcpy(dst->cell_of, src->cell_of, (size_t)n * sizeof *dst->cell_of);
    }
    memcpy(dst->start, src->start, ((size_t)src->cells + 1u) * sizeof *dst->start);
}

typedef struct sort_ctx {
    int (*cmp)(void *ctx, uint32_t a, uint32_t b);
    void *ctx;
} sort_ctx;

/* Stable insertion sort of a cell's members by key (cells are small in v0's use; stability
 * keeps the member order, which has no meaning, reproducible). */
static void sort_members(uint32_t *m, uint32_t len, const sort_ctx *c)
{
    for (uint32_t i = 1; i < len; ++i) {
        const uint32_t v = m[i];
        uint32_t j = i;
        while (j > 0 && c->cmp(c->ctx, m[j - 1], v) > 0) {
            m[j] = m[j - 1];
            --j;
        }
        m[j] = v;
    }
}

void cc_partition_split(cc_partition *p, int (*cmp)(void *ctx, uint32_t a, uint32_t b), void *ctx,
                        uint32_t *tmp)
{
    /* spec 7.1: "split(P, sig) replaces each old cell, in its old position, by its nonempty
     * signature classes in increasing lexicographic signature order" */
    const sort_ctx c = {cmp, ctx};
    uint32_t *new_start = tmp;
    uint32_t out = 0;
    for (uint32_t i = 0; i < p->cells; ++i) {
        const uint32_t lo = p->start[i], hi = p->start[i + 1];
        sort_members(p->lab + lo, hi - lo, &c);
        new_start[out++] = lo;
        for (uint32_t j = lo + 1; j < hi; ++j) {
            if (cmp(ctx, p->lab[j - 1], p->lab[j]) != 0) {
                new_start[out++] = j;
            }
        }
    }
    new_start[out] = p->n;
    memcpy(p->start, new_start, ((size_t)out + 1u) * sizeof *p->start);
    p->cells = out;
    for (uint32_t i = 0; i < p->cells; ++i) {
        for (uint32_t j = p->start[i]; j < p->start[i + 1]; ++j) {
            p->cell_of[p->lab[j]] = i;
        }
    }
}

void cc_partition_individualise(cc_partition *p, uint32_t cell, uint32_t a)
{
    /* spec 7.1: "replace cell C in place by [{a}, C minus {a}]" */
    const uint32_t lo = p->start[cell];
    uint32_t j = lo;
    while (p->lab[j] != a) {
        ++j;
    }
    for (; j > lo; --j) {
        p->lab[j] = p->lab[j - 1];
    }
    p->lab[lo] = a;
    for (uint32_t i = p->cells + 1; i > cell + 1; --i) {
        p->start[i] = p->start[i - 1];
    }
    p->start[cell + 1] = lo + 1;
    p->cells += 1;
    for (uint32_t i = cell; i < p->cells; ++i) {
        for (uint32_t k = p->start[i]; k < p->start[i + 1]; ++k) {
            p->cell_of[p->lab[k]] = i;
        }
    }
}

void cc_signatures_free(cc_signatures *s)
{
    free(s->counts);
    memset(s, 0, sizeof *s);
}

cc_reason cc_signatures_compute(const cc_object *x, const cc_partition *p, cc_signatures *s)
{
    cc_reason why = CC_ACCEPT;
    const uint32_t n = x->n;
    /* spec 7.1: "for arc labels sorted by B(label), then cell index j: outgoing multiplicity
     * from v into C[j], incoming multiplicity from C[j] into v"; the dense vector has
     * 2 L k entries, index ((l k) + j) 2 + dir.  For a subset every signature is empty. */
    const size_t L = x->graph ? x->label_count : 0, k = p->cells;
    /* the row is 2 L k counts of 8 bytes: every factor is checked before the product (a row
     * size that wrapped would make cc_alloc return a block shorter than the writes below) */
    if (L > 0 && k > 0 &&
        (L > SIZE_MAX / k || L * k > SIZE_MAX / 2u / sizeof *s->counts)) {
        return CC_CAPACITY;
    }
    s->width = 2u * L * k;
    s->counts = cc_alloc(n, s->width * sizeof *s->counts, &why);
    if (s->counts == NULL) {
        return why;
    }
    if (n > 0 && s->width > 0) {
        memset(s->counts, 0, (size_t)n * s->width * sizeof *s->counts);
    }
    for (uint32_t i = 0; x->graph && i < x->e; ++i) {
        const cc_arc *a = &x->arcs[i];
        /* a loop contributes once to the outgoing count of its vertex and once to the
         * incoming count (spec 7.1) */
        uint64_t *out = s->counts + (size_t)a->source * s->width +
                        (((size_t)a->label * k) + p->cell_of[a->target]) * 2u;
        uint64_t *in = s->counts + (size_t)a->target * s->width +
                       (((size_t)a->label * k) + p->cell_of[a->source]) * 2u + 1u;
        if (a->multiplicity > UINT64_MAX - *out || a->multiplicity > UINT64_MAX - *in) {
            return CC_CAPACITY; /* exact counts only (spec 7.1, 11.1) */
        }
        *out += a->multiplicity;
        *in += a->multiplicity;
    }
    return CC_ACCEPT;
}
