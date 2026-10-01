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
    if (p->count == p->cap) {
        uint32_t new_cap = p->cap < 16u ? 16u : (p->cap > UINT32_MAX / 2u ? UINT32_MAX : p->cap * 2u);
        canon_status st = CANON_COMPLETE;
        canon_prov_node *grown = canon_alloc_array(new_cap, sizeof *grown, &st);
        if (grown == NULL) {
            return st;
        }
        if (p->count > 0) {
            memcpy(grown, p->nodes, (size_t)p->count * sizeof *grown);
        }
        free(p->nodes);
        p->nodes = grown;
        p->cap = new_cap;
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

canon_status canon_prov_eval_all(const canon_provenance *p, const canon_perm_table *inputs,
                                 uint32_t *values)
{
    const uint32_t n = inputs->n;
    for (uint32_t i = 0; i < p->count; ++i) {
        const canon_prov_node *nd = &p->nodes[i];
        uint32_t *out = values + (size_t)i * n;
        switch (nd->kind) {
        case CANON_PROV_INPUT:
            if (nd->a >= inputs->count) {
                return CANON_INVALID_INPUT;
            }
            /* Inputs are copied with a range check, so every node value below has entries
             * < n and is a bijection whenever its inputs are (checked in INVERSE). */
            {
                const uint32_t *src = canon_perm_table_row(inputs, nd->a);
                for (uint32_t v = 0; v < n; ++v) {
                    if (src[v] >= n) {
                        return CANON_INVALID_INPUT;
                    }
                    out[v] = src[v];
                }
            }
            break;
        case CANON_PROV_INVERSE:
            if (nd->a >= i) {
                return CANON_INVALID_INPUT; /* operands are earlier nodes only */
            }
            /* spec 3: out = p^-1, out[p[v]] = v.  A non-bijective operand (possible only from
             * a non-bijective input) is reported rather than leaving entries unset. */
            {
                const uint32_t *src = values + (size_t)nd->a * n;
                for (uint32_t v = 0; v < n; ++v) {
                    out[v] = n; /* sentinel: unset */
                }
                for (uint32_t v = 0; v < n; ++v) {
                    if (out[src[v]] != n) {
                        return CANON_INVALID_INPUT;
                    }
                    out[src[v]] = v;
                }
            }
            break;
        case CANON_PROV_PRODUCT:
            if (nd->a >= i || nd->b >= i) {
                return CANON_INVALID_INPUT;
            }
            /* spec 3: (pq)[v] = q[p[v]], node a acts first. */
            canon_perm_compose(values + (size_t)nd->a * n, values + (size_t)nd->b * n, out, n);
            break;
        default:
            return CANON_INVALID_INPUT;
        }
    }
    return CANON_COMPLETE;
}
