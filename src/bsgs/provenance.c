/* Straight-line provenance records (spec 9.1, 9.2); see provenance.h. */
#include "bsgs/provenance.h"

#include <stdlib.h>
#include <string.h>

#include "arena/alloc.h"

void canon_prov_init(canon_provenance *p)
{
    p->count = 0;
    p->cap = 0;
    p->nodes = NULL;
}

void canon_prov_free(canon_provenance *p)
{
    free(p->nodes);
    canon_prov_init(p);
}

static canon_status append(canon_provenance *p, uint32_t kind, uint32_t a, uint32_t b,
                           uint32_t *id)
{
    if (p->count >= UINT32_MAX - 1u) {
        return CANON_CAPACITY_LIMIT; /* spec 11.1: node ids are uint32; NONE is reserved */
    }
    void *nodes = p->nodes;
    canon_status st = canon_grow_array(&nodes, &p->cap, p->count, 16u, sizeof *p->nodes);
    p->nodes = nodes;
    if (st != CANON_COMPLETE) {
        return st;
    }
    p->nodes[p->count].kind = kind;
    p->nodes[p->count].a = a;
    p->nodes[p->count].b = b;
    *id = p->count++;
    return CANON_COMPLETE;
}

canon_status canon_prov_input(canon_provenance *p, uint32_t input, uint32_t *id)
{
    return append(p, CANON_PROV_INPUT, input, 0, id);
}

canon_status canon_prov_inverse(canon_provenance *p, uint32_t node, uint32_t *id)
{
    if (node >= p->count) {
        return CANON_INTERNAL_ERROR;
    }
    return append(p, CANON_PROV_INVERSE, node, 0, id);
}

canon_status canon_prov_product(canon_provenance *p, uint32_t j, uint32_t k, uint32_t *id)
{
    /* spec 9.3: the identity is a tag, not a node; multiplying by it adds nothing. */
    if (j == CANON_PROV_NONE) {
        *id = k;
        return CANON_COMPLETE;
    }
    if (k == CANON_PROV_NONE) {
        *id = j;
        return CANON_COMPLETE;
    }
    if (j >= p->count || k >= p->count) {
        return CANON_INTERNAL_ERROR;
    }
    return append(p, CANON_PROV_PRODUCT, j, k, id);
}

/* Structural validity of every record (kinds, earlier operands, input indices). */
static bool records_valid(const canon_provenance *p, uint32_t input_count)
{
    for (uint32_t i = 0; i < p->count; ++i) {
        const canon_prov_node *nd = &p->nodes[i];
        switch (nd->kind) {
        case CANON_PROV_INPUT:
            if (nd->a >= input_count) {
                return false;
            }
            break;
        case CANON_PROV_INVERSE:
            if (nd->a >= i) {
                return false; /* operands are earlier nodes only */
            }
            break;
        case CANON_PROV_PRODUCT:
            if (nd->a >= i || nd->b >= i) {
                return false;
            }
            break;
        default:
            return false;
        }
    }
    return true;
}

typedef struct row_pool {
    uint32_t n;
    uint32_t rows, cap;     /* rows ever allocated (the peak), capacity */
    uint32_t *data;         /* cap * n entries */
    uint32_t free_count;
    uint32_t free_cap;
    uint32_t *free_list;    /* released slots */
} row_pool;

static canon_status pool_take(row_pool *pool, uint32_t *slot)
{
    if (pool->free_count > 0) {
        *slot = pool->free_list[--pool->free_count];
        return CANON_COMPLETE;
    }
    void *data = pool->data, *fl = pool->free_list;
    canon_status st = canon_grow_array(&data, &pool->cap, pool->rows, 8u,
                                       (size_t)pool->n * sizeof *pool->data);
    pool->data = data;
    if (st == CANON_COMPLETE) {
        /* every slot can be free at once: the free list needs as many entries as rows */
        st = canon_grow_array(&fl, &pool->free_cap, pool->rows, 8u, sizeof *pool->free_list);
        pool->free_list = fl;
    }
    if (st != CANON_COMPLETE) {
        return st;
    }
    *slot = pool->rows++;
    return CANON_COMPLETE;
}

static void pool_release(row_pool *pool, uint32_t slot)
{
    pool->free_list[pool->free_count++] = slot; /* free_cap > rows >= free_count */
}

static uint32_t *pool_row(const row_pool *pool, uint32_t slot)
{
    return pool->data + (size_t)slot * pool->n;
}

/* Evaluate node i into its row; false if an input or operand is not a bijection. */
static bool eval_node(const canon_provenance *p, const canon_perm_table *inputs,
                      const row_pool *pool, const uint32_t *slot, uint32_t i, uint32_t *out)
{
    const uint32_t n = inputs->n;
    const canon_prov_node *nd = &p->nodes[i];
    if (nd->kind == CANON_PROV_INPUT) {
        /* copied with a range check, so every node value has entries < n */
        const uint32_t *src = canon_perm_table_row(inputs, nd->a);
        for (uint32_t v = 0; v < n; ++v) {
            if (src[v] >= n) {
                return false;
            }
            out[v] = src[v];
        }
        return true;
    }
    const uint32_t *a = pool_row(pool, slot[nd->a]);
    if (nd->kind == CANON_PROV_INVERSE) {
        /* spec 3: out = a^-1, out[a[v]] = v; a repeated entry is reported */
        for (uint32_t v = 0; v < n; ++v) {
            out[v] = n; /* sentinel: unset */
        }
        for (uint32_t v = 0; v < n; ++v) {
            if (out[a[v]] != n) {
                return false;
            }
            out[a[v]] = v;
        }
        return true;
    }
    /* spec 3: (ab)[v] = b[a[v]], node a acts first */
    canon_perm_compose(a, pool_row(pool, slot[nd->b]), out, n);
    return true;
}

canon_status canon_prov_check(const canon_provenance *p, const canon_perm_table *inputs,
                              const uint32_t *targets, const uint32_t *const *expected,
                              uint32_t count, bool *match, uint32_t *peak_rows)
{
    const uint32_t n = inputs->n, P = p->count;
    *match = false;
    *peak_rows = 0;
    if (!records_valid(p, inputs->count)) {
        return CANON_INVALID_INPUT;
    }
    for (uint32_t k = 0; k < count; ++k) {
        if (targets[k] >= P) {
            return CANON_INVALID_INPUT;
        }
    }
    canon_status st = CANON_COMPLETE;
    uint32_t *last = canon_alloc_array(P, sizeof *last, &st);   /* last user, or NONE */
    uint32_t *slot = canon_alloc_array(P, sizeof *slot, &st);   /* row of an evaluated node */
    uint32_t *first = canon_alloc_array(P, sizeof *first, &st); /* first target naming it */
    uint32_t *next = canon_alloc_array(count, sizeof *next, &st);
    uint8_t *need = canon_alloc_array(P, sizeof *need, &st);
    row_pool pool = {n, 0, 0, NULL, 0, 0, NULL};
    if (last == NULL || slot == NULL || first == NULL || next == NULL || need == NULL) {
        goto done;
    }
    for (uint32_t i = 0; i < P; ++i) {
        last[i] = CANON_PROV_NONE;
        first[i] = CANON_PROV_NONE;
        need[i] = 0;
    }
    for (uint32_t k = count; k-- > 0;) {
        need[targets[k]] = 1;
        next[k] = first[targets[k]];
        first[targets[k]] = k;
    }
    /* reachability, walking down from the last node: an operand's first visit from above is
     * its last use */
    for (uint32_t i = P; i-- > 0;) {
        const canon_prov_node *nd = &p->nodes[i];
        if (!need[i] || nd->kind == CANON_PROV_INPUT) {
            continue;
        }
        const uint32_t ops[2] = {nd->a, nd->kind == CANON_PROV_PRODUCT ? nd->b : nd->a};
        for (int j = 0; j < 2; ++j) {
            need[ops[j]] = 1;
            if (last[ops[j]] == CANON_PROV_NONE) {
                last[ops[j]] = i;
            }
        }
    }
    bool all = true;
    for (uint32_t i = 0; i < P && all; ++i) {
        if (!need[i]) {
            continue;
        }
        st = pool_take(&pool, &slot[i]);
        if (st != CANON_COMPLETE) {
            goto done;
        }
        uint32_t *row = pool_row(&pool, slot[i]);
        if (!eval_node(p, inputs, &pool, slot, i, row)) {
            st = CANON_INVALID_INPUT;
            goto done;
        }
        for (uint32_t k = first[i]; k != CANON_PROV_NONE && all; k = next[k]) {
            all = n == 0 || memcmp(row, expected[k], (size_t)n * sizeof *row) == 0;
        }
        /* release the operands at their last use (once if both operands are the same node),
         * and the node itself if nothing later uses it */
        const canon_prov_node *nd = &p->nodes[i];
        if (nd->kind != CANON_PROV_INPUT && last[nd->a] == i) {
            pool_release(&pool, slot[nd->a]);
        }
        if (nd->kind == CANON_PROV_PRODUCT && nd->b != nd->a && last[nd->b] == i) {
            pool_release(&pool, slot[nd->b]);
        }
        if (last[i] == CANON_PROV_NONE) {
            pool_release(&pool, slot[i]);
        }
    }
    *match = all;
    *peak_rows = pool.rows;
done:
    free(last);
    free(slot);
    free(first);
    free(next);
    free(need);
    free(pool.data);
    free(pool.free_list);
    return st;
}
