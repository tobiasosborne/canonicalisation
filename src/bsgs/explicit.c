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

#include "arena/alloc.h"
#include "perm/perm.h"
#include "util/sort.h"

typedef struct explicit_group {
    uint64_t order;  /* number of rows */
    uint32_t *table; /* order * degree entries, rows sorted lexicographically */
    struct explicit_group *lift; /* S6, spec 8.4: the lifted group's table on degree + 2
                                    points for a signed group (its own lift is NULL); NULL
                                    for an unsigned group */
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
        explicit_destroy(e->lift);
        free(e->table);
        free(e);
    }
}

static uint64_t explicit_order(const canon_group *group)
{
    const explicit_group *e = group->impl;
    return e->order;
}

/* spec 9.1: exact membership, here by binary search in the sorted table (no allocation). */
static bool table_contains(const explicit_group *e, uint32_t degree, const uint32_t *p)
{
    size_t lo = 0, hi = (size_t)e->order;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        int c = canon_perm_lex_compare(row_of(e, degree, mid), p, degree);
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

static canon_status explicit_contains(const canon_group *group, const uint32_t *p, bool *out)
{
    *out = table_contains(group->impl, group->degree, p);
    return CANON_COMPLETE;
}

/* spec 11.1: the explicit table is bounded by the descriptor's max_group_order. */
static canon_status explicit_admits(const canon_group *group, const canon_capacity *cap)
{
    return explicit_order(group) <= cap->max_group_order ? CANON_COMPLETE : CANON_CAPACITY_LIMIT;
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

/* ---- spec 8.1 coset enumeration over the table (slice S4: the oracle for the chain's
 * enumerator, src/coset/enumerate.c) ----
 *
 * The same reference traversal, written independently of the chain code: a subgroup H is the
 * list of its rows in table order (so the first row with a property is the least element with
 * it), "a = smallest atom moved by H" is found by scanning the rows, the orbit a^H is the set
 * of images row[a], t_b is the first row with row[a] = b, and H_a is the sublist of rows fixing
 * a.  Visits are counted by canon_coset_visit_enter, the rule the chain enumerator uses. */

typedef struct ex_enum {
    const explicit_group *e;
    uint32_t n;
    canon_coset_visitor *v;
} ex_enum;

static uint32_t ex_least_moved(const ex_enum *x, const size_t *rows, size_t count)
{
    for (uint32_t a = 0; a < x->n; ++a) {
        for (size_t i = 0; i < count; ++i) {
            if (row_of(x->e, x->n, rows[i])[a] != a) {
                return a;
            }
        }
    }
    return x->n;
}

static canon_status ex_visit(const ex_enum *x, const size_t *rows, size_t count,
                             const uint32_t *r)
{
    canon_status st = canon_coset_visit_enter(x->v);
    if (st != CANON_COMPLETE) {
        return st;
    }
    const uint32_t n = x->n;
    if (count == 1) {
        x->v->leaves += 1; /* spec 8.1: H = {id}: consume(r) */
        return x->v->consume(x->v->user, r, &x->v->stopped);
    }
    /* spec 8.1: "a = smallest atom moved by H" (H has a nonidentity row, so a < n) */
    const uint32_t a = ex_least_moved(x, rows, count);
    size_t child_count = 0;
    for (size_t i = 0; i < count; ++i) {
        child_count += row_of(x->e, n, rows[i])[a] == a;
    }
    size_t *child = canon_alloc_array(child_count, sizeof *child, &st);
    uint32_t *child_r = canon_alloc_array(n, sizeof *child_r, &st);
    uint8_t *in_orbit = canon_alloc_array(n, sizeof *in_orbit, &st);
    if (child != NULL && child_r != NULL && in_orbit != NULL) {
        memset(in_orbit, 0, n);
        for (size_t i = 0, k = 0; i < count; ++i) {
            const uint32_t *h = row_of(x->e, n, rows[i]);
            in_orbit[h[a]] = 1; /* a^H */
            if (h[a] == a) {
                child[k++] = rows[i]; /* H_a, still in table order */
            }
        }
        /* spec 8.1: "for b in sorted(a^H)" */
        for (uint32_t b = 0; b < n && st == CANON_COMPLETE && !x->v->stopped; ++b) {
            if (!in_orbit[b]) {
                continue;
            }
            /* "t_b = least image-array element of H with a^t_b=b": the first such row */
            const uint32_t *t_b = NULL;
            for (size_t i = 0; i < count && t_b == NULL; ++i) {
                const uint32_t *h = row_of(x->e, n, rows[i]);
                t_b = h[a] == b ? h : NULL;
            }
            canon_perm_compose(t_b, r, child_r, n); /* t_b r: t_b acts first (spec 3) */
            st = ex_visit(x, child, child_count, child_r); /* visit(H_a, t_b r) */
        }
    }
    free(child);
    free(child_r);
    free(in_orbit);
    return st;
}

static canon_status explicit_enumerate(const canon_group *group, canon_coset_visitor *visitor)
{
    const explicit_group *e = group->impl;
    const uint32_t n = group->degree;
    canon_status st = CANON_COMPLETE;
    size_t *rows = canon_alloc_array((size_t)e->order, sizeof *rows, &st);
    uint32_t *id = canon_alloc_array(n, sizeof *id, &st);
    if (rows != NULL && id != NULL) {
        for (size_t i = 0; i < (size_t)e->order; ++i) {
            rows[i] = i;
        }
        for (uint32_t v = 0; v < n; ++v) {
            id[v] = v;
        }
        ex_enum x = {e, n, visitor};
        visitor->stopped = false;
        st = ex_visit(&x, rows, (size_t)e->order, id); /* spec 8.1: start visit(G, id) */
    }
    free(rows);
    free(id);
    return st;
}

/* Sort the `count` rows of `table` (row_words words each, `degree` of them used)
 * lexicographically (S1 brief 4.2), so table order is image-array order.  On success *table is
 * replaced by the sorted copy. */
static canon_status sort_rows(uint32_t **table, size_t count, uint32_t degree, size_t row_words)
{
    if (count > SIZE_MAX / 16) {
        return CANON_CAPACITY_LIMIT;
    }
    canon_status st = CANON_COMPLETE;
    size_t *idx = canon_alloc_array(count, sizeof *idx, &st);
    size_t *scratch = canon_alloc_array(count, sizeof *scratch, &st);
    uint32_t *sorted = idx != NULL && scratch != NULL
                           ? canon_alloc_array(count * row_words, sizeof *sorted, &st)
                           : NULL;
    if (sorted != NULL) {
        for (size_t i = 0; i < count; ++i) {
            idx[i] = i;
        }
        row_order order = {*table, degree};
        canon_stable_sort(idx, count, sizeof *idx, scratch, row_cmp, &order);
        for (size_t i = 0; i < count && degree > 0; ++i) {
            memcpy(sorted + i * row_words, *table + idx[i] * row_words, row_words * sizeof *sorted);
        }
        free(*table);
        *table = sorted;
    }
    free(idx);
    free(scratch);
    return st;
}

/* spec 8.4 (slice S6): chi(g) by membership of the two extensions of g in the lift's sorted
 * table (src/bsgs/group.h contract), by binary search, as for contains. */
static canon_status explicit_character(const canon_group *group, const uint32_t *g,
                                       uint32_t *scratch, int *sign)
{
    *sign = 0;
    const explicit_group *e = group->impl;
    if (e->lift == NULL) {
        return CANON_UNSUPPORTED_ACTION; /* an unsigned group has no character */
    }
    const uint32_t n = group->degree, m = n + 2u; /* fits: checked at creation (spec 11.1) */
    canon_status st = CANON_COMPLETE;
    uint32_t *own = NULL;
    if (scratch == NULL) {
        scratch = own = canon_alloc_array(m, sizeof *own, &st);
        if (own == NULL) {
            return st;
        }
    }
    st = CANON_INVALID_INPUT; /* neither extension is in the lift: g is not in G */
    for (int s = 1; s >= -1; s -= 2) {
        canon_group_lift_element(g, n, s, scratch);
        if (table_contains(e->lift, m, scratch)) {
            *sign = s;
            st = CANON_COMPLETE;
            break;
        }
    }
    free(own);
    return st;
}

static canon_status explicit_conjugate(const canon_group *group, const uint32_t *g,
                                       canon_group **out);

static const canon_group_ops explicit_ops = {
    explicit_destroy, explicit_order,     explicit_contains,  explicit_tuple_min,
    explicit_admits,  explicit_enumerate, explicit_character, explicit_conjugate};

/* spec 3.1 (slice S6): g^-1 G g as a new table: every row h becomes q with q[g[v]] = g[h[v]]
 * (spec 2.1 "g^-1 p g": the cycles of h relabelled through g), and the rows are sorted again.
 * Conjugation is a bijection of Sym(n), so the rows stay distinct and the order is |G|.  The
 * result is unsigned. */
static canon_status explicit_conjugate(const canon_group *group, const uint32_t *g,
                                       canon_group **out)
{
    *out = NULL;
    const explicit_group *e = group->impl;
    const uint32_t n = group->degree;
    const size_t row_words = n > 0 ? (size_t)n : 1;
    canon_status st = CANON_COMPLETE;
    size_t words = 0;
    if (!canon_size_mul((size_t)e->order, row_words, &words)) {
        return CANON_CAPACITY_LIMIT; /* spec 11.1 */
    }
    explicit_group *c = canon_alloc_array(1, sizeof *c, &st);
    uint32_t *table = canon_alloc_array(words, sizeof *table, &st);
    if (c == NULL || table == NULL) {
        free(c);
        free(table);
        return st;
    }
    for (size_t i = 0; i < (size_t)e->order; ++i) {
        const uint32_t *h = row_of(e, n, i);
        uint32_t *q = table + i * row_words;
        for (uint32_t v = 0; v < n; ++v) {
            q[g[v]] = g[h[v]];
        }
    }
    st = sort_rows(&table, (size_t)e->order, n, row_words);
    if (st != CANON_COMPLETE) {
        free(c);
        free(table);
        return st;
    }
    c->order = e->order;
    c->table = table;
    c->lift = NULL;
    st = canon_group_alloc(&explicit_ops, n, c, out); /* spec 17: count from canon_group_alloc */
    if (st != CANON_COMPLETE) {
        explicit_destroy(c);
    }
    return st;
}

/* Close <generators> into a sorted table (the S1 construction), bounded by max_order. */
static canon_status build_table(uint32_t degree, const uint32_t *generators, size_t generator_count,
                                uint64_t max_order, explicit_group **out)
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
    st = sort_rows(&table, count, degree, row_words);
    if (st != CANON_COMPLETE) {
        goto fail;
    }

    explicit_group *e = malloc(sizeof *e);
    if (e == NULL) {
        st = CANON_RESOURCE_LIMIT;
        goto fail;
    }
    e->order = (uint64_t)count;
    e->table = table;
    e->lift = NULL;
    *out = e;
    return CANON_COMPLETE;

fail:
    free(set.slots);
    free(tmp);
    free(table);
    return st;
}

canon_status canon_group_explicit_create(uint32_t degree, const uint32_t *generators,
                                         size_t generator_count, uint64_t max_order,
                                         canon_group **out)
{
    *out = NULL;
    explicit_group *e = NULL;
    canon_status st = build_table(degree, generators, generator_count, max_order, &e);
    if (st == CANON_COMPLETE) {
        /* spec 17: the handle and its reference count come from canon_group_alloc. */
        st = canon_group_alloc(&explicit_ops, degree, e, out);
        if (st != CANON_COMPLETE) {
            explicit_destroy(e); /* frees table too */
        }
    }
    return st;
}

canon_status canon_group_explicit_create_signed(uint32_t degree, const uint32_t *generators,
                                                size_t generator_count, const int8_t *signs,
                                                uint64_t max_order, canon_group **out)
{
    *out = NULL;
    if (generator_count > 0 && signs == NULL) {
        return CANON_INVALID_INPUT;
    }
    if (degree > UINT32_MAX - 2u) {
        /* spec 11.1: "n+2 <= 2^32-1, checked before constructing its two sign points" */
        return CANON_CAPACITY_LIMIT;
    }
    explicit_group *e = NULL;
    canon_status st = build_table(degree, generators, generator_count, max_order, &e);
    if (st != CANON_COMPLETE) {
        return st;
    }
    /* spec 8.4: the lifted group on Omega u {+,-}.  Its projection onto G (restriction to
     * Omega) is onto with kernel K, the lift's elements fixing every point of Omega, so
     * |lift| = |G| |K| >= |G|, and chi exists iff K = 1 iff |lift| = |G|.  The closure is
     * therefore bounded by |G|: if it would exceed |G| rows (CAPACITY_LIMIT of the bounded
     * closure) the orders differ and the signs are inconsistent; otherwise |lift| = |G|.  So
     * the lift never needs more rows than G.  So that CAPACITY_LIMIT of that closure can only
     * mean "more than |G| rows", every size it can reach (at most 2|G| rows of degree + 2 words
     * and a hash set of at most 4|G| slots) is checked to fit first. */
    size_t bytes = 0;
    if (!canon_size_mul3((size_t)e->order, (size_t)degree + 2u, 4u * sizeof(uint32_t), &bytes) ||
        !canon_size_mul3((size_t)e->order, 4u, sizeof(size_t), &bytes)) {
        explicit_destroy(e);
        return CANON_CAPACITY_LIMIT; /* spec 11.1 */
    }
    uint32_t *lifted = canon_group_lift_generators(degree, generators, generator_count, signs, &st);
    if (lifted != NULL) {
        st = build_table(degree + 2u, lifted, generator_count, e->order, &e->lift);
        if (st == CANON_CAPACITY_LIMIT) {
            st = CANON_INVALID_INPUT; /* spec 8.4: "Reject inconsistent signs" */
        } else if (st == CANON_COMPLETE && e->lift->order != e->order) {
            st = CANON_INTERNAL_ERROR; /* |lift| >= |G| and the closure stopped at |G| */
        }
        free(lifted);
    }
    if (st == CANON_COMPLETE) {
        st = canon_group_alloc(&explicit_ops, degree, e, out);
        if (st == CANON_COMPLETE) {
            e = NULL; /* owned by the handle */
            st = canon_group_set_signs(*out, generators, generator_count, signs);
            if (st != CANON_COMPLETE) {
                canon_group_unshare(*out);
                *out = NULL;
            }
        }
    }
    explicit_destroy(e);
    return st;
}
