/* Explicit-enumeration group backend (slice S1; spec sections 7.1, 7.2, 9).
 *
 * The group is closed by breadth-first generation: starting from the identity, rows are
 * processed in discovery order and each is multiplied on the right by every generator in input
 * order (spec 3: (pq)[v] = q[p[v]]).  For a finite group the monoid generated equals the group.
 * The table is then sorted lexicographically, so "least element" queries are table order.
 *
 * Every query below is O(|G| * n) per call.  This backend is replaced in slice S3 by a verified
 * stabiliser chain (spec 9.1) behind the same canon_group_ops. */
#include "bsgs/explicit.h"

#include <stdlib.h>
#include <string.h>

#include "perm/perm.h"
#include "util/sort.h"

typedef struct explicit_group {
    uint64_t order;  /* number of rows */
    uint32_t *table; /* order * degree entries, rows sorted lexicographically */
} explicit_group;

static const uint32_t *row_of(const explicit_group *e, uint32_t degree, size_t i)
{
    return e->table + i * (size_t)degree;
}

/* ---- closure-time hash set over row indices (lookup aid only; exact comparison resolves every
 * collision, spec 4.2 "Hashes are lookup aids with exact collision resolution") ---- */

typedef struct row_set {
    size_t *slots; /* row index + 1, 0 = empty */
    size_t mask;   /* capacity - 1, capacity a power of two */
    size_t count;
} row_set;

static uint64_t hash_row(const uint32_t *row, uint32_t degree)
{
    uint64_t h = 1469598103934665603ULL; /* FNV-1a 64 */
    for (uint32_t v = 0; v < degree; ++v) {
        h ^= row[v];
        h *= 1099511628211ULL;
    }
    return h;
}

/* Returns the slot holding `row` or the empty slot where it belongs. */
static size_t set_probe(const row_set *s, const uint32_t *table, uint32_t degree,
                        const uint32_t *row, uint64_t h)
{
    size_t i = (size_t)h & s->mask;
    for (;;) {
        size_t idx = s->slots[i];
        if (idx == 0) {
            return i;
        }
        if (degree == 0 ||
            memcmp(table + (idx - 1) * (size_t)degree, row, (size_t)degree * sizeof *row) == 0) {
            return i;
        }
        i = (i + 1) & s->mask;
    }
}

static canon_status set_grow(row_set *s, const uint32_t *table, uint32_t degree)
{
    size_t cap = s->mask + 1;
    if (cap > SIZE_MAX / 2 / sizeof *s->slots) {
        return CANON_CAPACITY_LIMIT; /* spec 11.1 */
    }
    size_t new_cap = cap * 2;
    size_t *slots = calloc(new_cap, sizeof *slots);
    if (slots == NULL) {
        return CANON_RESOURCE_LIMIT;
    }
    row_set grown = {slots, new_cap - 1, s->count};
    for (size_t i = 0; i < cap; ++i) {
        size_t idx = s->slots[i];
        if (idx != 0) {
            const uint32_t *row = table + (idx - 1) * (size_t)degree;
            size_t j = set_probe(&grown, table, degree, row, hash_row(row, degree));
            grown.slots[j] = idx;
        }
    }
    free(s->slots);
    *s = grown;
    return CANON_COMPLETE;
}

/* ---- sorting the closed table (stable merge sort of row indices, src/util/sort.h) ---- */

typedef struct row_order {
    const uint32_t *table;
    uint32_t degree;
} row_order;

/* spec 7.2: image arrays compare numerically lexicographically. */
static int row_cmp(const void *a, const void *b, void *ctx)
{
    const row_order *o = ctx;
    size_t ia = *(const size_t *)a, ib = *(const size_t *)b;
    return canon_perm_lex_compare(o->table + ia * (size_t)o->degree,
                                  o->table + ib * (size_t)o->degree, o->degree);
}

/* ---- operations ---- */

static void explicit_destroy(void *impl)
{
    explicit_group *e = impl;
    if (e != NULL) {
        free(e->table);
        free(e);
    }
}

static uint64_t explicit_order(const canon_group *group)
{
    const explicit_group *e = group->impl;
    return e->order;
}

/* spec 9.1: exact membership, here by binary search in the sorted table. */
static bool explicit_contains(const canon_group *group, const uint32_t *p)
{
    const explicit_group *e = group->impl;
    size_t lo = 0, hi = (size_t)e->order;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        int c = canon_perm_lex_compare(row_of(e, group->degree, mid), p, group->degree);
        if (c == 0) {
            return true;
        }
        if (c < 0) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return false;
}

/* Union-find over the domain stored in `parent`, invariant parent[x] <= x, so every root is
 * the least point of its class. */
static uint32_t uf_find(uint32_t *parent, uint32_t x)
{
    while (parent[x] != x) {
        parent[x] = parent[parent[x]]; /* path halving keeps parent[x] <= x */
        x = parent[x];
    }
    return x;
}

static void uf_union(uint32_t *parent, uint32_t a, uint32_t b)
{
    uint32_t ra = uf_find(parent, a), rb = uf_find(parent, b);
    if (ra < rb) {
        parent[rb] = ra;
    } else if (rb < ra) {
        parent[ra] = rb;
    }
}

/* spec 7.1 G stage and 7.2 leaf map, by scanning the sorted table. */
static canon_status explicit_tuple_min(const canon_group *group, const uint32_t *L, uint32_t len,
                                       uint32_t *t_out, uint32_t *orbit_id_out)
{
    const explicit_group *e = group->impl;
    const uint32_t n = group->degree;
    for (uint32_t i = 0; i < len; ++i) {
        if (L[i] >= n) {
            return CANON_INVALID_INPUT;
        }
    }
    /* spec 7.1: "M = lexicographically least F^G"; spec 7.2: t_L minimises L^G.  Ties (only
     * when L is not a full list) keep the first row, i.e. the least g, because the table is
     * sorted and only a strictly smaller image replaces the incumbent. */
    const uint32_t *b = row_of(e, n, 0);
    for (size_t r = 1; r < (size_t)e->order; ++r) {
        const uint32_t *g = row_of(e, n, r);
        for (uint32_t i = 0; i < len; ++i) {
            if (g[L[i]] != b[L[i]]) {
                if (g[L[i]] < b[L[i]]) {
                    b = g;
                }
                break;
            }
        }
    }
    if (n > 0) {
        memcpy(t_out, b, (size_t)n * sizeof *t_out);
    }
    if (orbit_id_out == NULL) {
        return CANON_COMPLETE;
    }
    /* spec 7.1: "compute G_M orbits" where M = L^t; G_M = pointwise stabiliser of M, found by
     * filtering the table; orbits by union-find over its elements. */
    uint32_t *parent = orbit_id_out;
    for (uint32_t v = 0; v < n; ++v) {
        parent[v] = v;
    }
    for (size_t r = 0; r < (size_t)e->order; ++r) {
        const uint32_t *g = row_of(e, n, r);
        bool fixes = true;
        for (uint32_t i = 0; i < len && fixes; ++i) {
            uint32_t m = b[L[i]]; /* M[i] */
            fixes = g[m] == m;
        }
        if (!fixes) {
            continue;
        }
        for (uint32_t v = 0; v < n; ++v) {
            uf_union(parent, v, g[v]);
        }
    }
    /* spec 7.1: "sort each orbit's target labels increasingly, then sort the orbit lists
     * lexicographically".  Orbits are disjoint, so the lexicographic order of the sorted lists
     * is the order of their least points, i.e. of the union-find roots.  One increasing pass
     * replaces each root by its rank and each other point by its root's rank (parent[v] < v
     * has already been rewritten when v is reached). */
    for (uint32_t v = 0; v < n; ++v) {
        (void)uf_find(parent, v);
    }
    uint32_t next = 0;
    for (uint32_t v = 0; v < n; ++v) {
        uint32_t r = parent[v];
        parent[v] = r == v ? next++ : parent[r];
    }
    return CANON_COMPLETE;
}

static const canon_group_ops explicit_ops = {explicit_destroy, explicit_order, explicit_contains,
                                             explicit_tuple_min};

canon_status canon_group_explicit_create(uint32_t degree, const uint32_t *generators,
                                         size_t generator_count, uint64_t max_order,
                                         canon_group **out)
{
    *out = NULL;
    if (generator_count > 0 && degree > 0 && generators == NULL) {
        return CANON_INVALID_INPUT;
    }
    if (degree > 0 && generator_count > SIZE_MAX / degree) {
        return CANON_CAPACITY_LIMIT; /* spec 11.1: products checked */
    }
    /* spec 4.1, 9.1: generators must be bijections of the domain.  On degree 0 every
     * generator is the empty permutation and is not read (generators may be NULL).  One
     * scratch bitmap serves every generator. */
    if (degree > 0 && generator_count > 0) {
        size_t words = ((size_t)degree + 63u) / 64u; /* <= 2^26: the product cannot wrap */
        uint64_t *bitmap = malloc(words * sizeof *bitmap);
        if (bitmap == NULL) {
            return CANON_RESOURCE_LIMIT;
        }
        bool valid = true;
        for (size_t i = 0; i < generator_count && valid; ++i) {
            valid = canon_perm_validate_scratch(generators + i * (size_t)degree, degree, bitmap);
        }
        free(bitmap);
        if (!valid) {
            return CANON_INVALID_INPUT;
        }
    }
    if (max_order < 1) {
        return CANON_CAPACITY_LIMIT; /* even the trivial group has one element */
    }
    /* Row storage: at least one uint32 so the table pointer is never NULL (n = 0). */
    const size_t row_words = degree > 0 ? (size_t)degree : 1;
    if (row_words > SIZE_MAX / sizeof(uint32_t) / 16) {
        return CANON_CAPACITY_LIMIT;
    }
    size_t rows_cap = 16;
    uint32_t *table = malloc(rows_cap * row_words * sizeof *table);
    uint32_t *tmp = malloc(row_words * sizeof *tmp);
    row_set set = {calloc(64, sizeof(size_t)), 63, 0};
    canon_status st = CANON_COMPLETE;
    size_t count = 0;
    if (table == NULL || tmp == NULL || set.slots == NULL) {
        st = CANON_RESOURCE_LIMIT;
        goto fail;
    }
    for (uint32_t v = 0; v < degree; ++v) {
        table[v] = v; /* row 0: the identity */
    }
    count = 1;
    set.slots[set_probe(&set, table, degree, table, hash_row(table, degree))] = 1;
    set.count = 1;
    /* spec 9.1 (reference construction, here by explicit closure): breadth first in discovery
     * order, generators in input order. */
    for (size_t i = 0; i < count; ++i) {
        for (size_t j = 0; j < generator_count && degree > 0; ++j) {
            canon_perm_compose(table + i * row_words, generators + j * (size_t)degree, tmp,
                               degree); /* tmp = row_i * gen_j, row_i acts first */
            uint64_t h = hash_row(tmp, degree);
            size_t slot = set_probe(&set, table, degree, tmp, h);
            if (set.slots[slot] != 0) {
                continue;
            }
            /* spec 11.1: capacity is checked before each growth of the table, never after. */
            if ((uint64_t)count >= max_order) {
                st = CANON_CAPACITY_LIMIT;
                goto fail;
            }
            if (count == rows_cap) {
                if (rows_cap > SIZE_MAX / 2 / row_words / sizeof *table) {
                    st = CANON_CAPACITY_LIMIT;
                    goto fail;
                }
                uint32_t *grown = realloc(table, rows_cap * 2 * row_words * sizeof *table);
                if (grown == NULL) {
                    st = CANON_RESOURCE_LIMIT;
                    goto fail;
                }
                table = grown;
                rows_cap *= 2;
            }
            memcpy(table + count * row_words, tmp, (size_t)degree * sizeof *tmp);
            ++count;
            set.slots[slot] = count; /* index + 1 */
            ++set.count;
            if (set.count * 2 > set.mask + 1) {
                st = set_grow(&set, table, degree);
                if (st != CANON_COMPLETE) {
                    goto fail;
                }
            }
        }
    }
    free(set.slots);
    set.slots = NULL;
    free(tmp);
    tmp = NULL;

    /* Sort the rows lexicographically (S1 brief 4.2), so table order is image-array order. */
    {
        if (count > SIZE_MAX / 16) {
            st = CANON_CAPACITY_LIMIT;
            goto fail;
        }
        size_t *idx = malloc(count * sizeof *idx);
        size_t *scratch = malloc(count * sizeof *scratch);
        uint32_t *sorted = malloc(count * row_words * sizeof *sorted);
        if (idx == NULL || scratch == NULL || sorted == NULL) {
            free(idx);
            free(scratch);
            free(sorted);
            st = CANON_RESOURCE_LIMIT;
            goto fail;
        }
        for (size_t i = 0; i < count; ++i) {
            idx[i] = i;
        }
        row_order order = {table, degree};
        canon_stable_sort(idx, count, sizeof *idx, scratch, row_cmp, &order);
        for (size_t i = 0; i < count && degree > 0; ++i) {
            memcpy(sorted + i * row_words, table + idx[i] * row_words, row_words * sizeof *sorted);
        }
        free(idx);
        free(scratch);
        free(table);
        table = sorted;
    }

    explicit_group *e = malloc(sizeof *e);
    if (e == NULL) {
        st = CANON_RESOURCE_LIMIT;
        goto fail;
    }
    e->order = (uint64_t)count;
    e->table = table;
    /* spec 17: the handle and its reference count come from canon_group_alloc. */
    st = canon_group_alloc(&explicit_ops, degree, e, out);
    if (st != CANON_COMPLETE) {
        explicit_destroy(e); /* frees table too */
        return st;
    }
    return CANON_COMPLETE;

fail:
    free(set.slots);
    free(tmp);
    free(table);
    return st;
}
