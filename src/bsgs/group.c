/* The canon_group handle shared by every group backend (spec section 17: opaque retain/release
 * handles; immutable groups can be shared).  Backends construct handles only through
 * canon_group_alloc, so the reference-count invariant lives here, not in each backend. */
#include "bsgs/group.h"

#include <stdlib.h>
#include <string.h>

#include "arena/alloc.h"
#include "arena/checked.h"

canon_status canon_group_alloc(const canon_group_ops *ops, uint32_t degree, void *impl,
                               canon_group **out)
{
    *out = NULL;
    canon_group *g = malloc(sizeof *g);
    if (g == NULL) {
        return CANON_RESOURCE_LIMIT;
    }
    g->ops = ops;
    g->degree = degree;
    g->impl = impl;
    g->signs = NULL; /* unsigned until canon_group_set_signs (S6) */
    g->block = g;
    g->refs = &g->refs_storage;
    canon_ref_init(g->refs); /* one reference, owned by the creator */
    *out = g;
    return CANON_COMPLETE;
}

void canon_group_share(const canon_group *group)
{
    if (group != NULL) {
        canon_ref_retain(group->refs);
    }
}

void canon_group_unshare(const canon_group *group)
{
    if (group != NULL && canon_ref_release(group->refs)) {
        canon_group *owned = group->block; /* the allocation, now unreferenced */
        owned->ops->destroy(owned->impl);
        if (owned->signs != NULL) {
            canon_perm_table_free(&owned->signs->gens);
            free(owned->signs->signs);
            free(owned->signs);
        }
        free(owned);
    }
}

canon_status canon_group_set_signs(canon_group *group, const uint32_t *gens, size_t count,
                                   const int8_t *signs)
{
    if (count > UINT32_MAX - 1u) {
        return CANON_CAPACITY_LIMIT; /* spec 11.1: generator indices are uint32 */
    }
    canon_status st = CANON_COMPLETE;
    canon_group_signs *s = canon_alloc_array(1, sizeof *s, &st);
    int8_t *copy = canon_alloc_array(count, sizeof *copy, &st);
    if (s == NULL || copy == NULL) {
        free(s);
        free(copy);
        return st;
    }
    canon_perm_table_init(&s->gens, group->degree);
    for (size_t i = 0; i < count && st == CANON_COMPLETE; ++i) {
        uint32_t row = 0;
        /* degree 0: the row is the empty permutation and is not read */
        st = canon_perm_table_push(
            &s->gens, group->degree > 0 ? gens + i * (size_t)group->degree : NULL, &row);
    }
    if (st != CANON_COMPLETE) {
        canon_perm_table_free(&s->gens);
        free(s);
        free(copy);
        return st; /* spec 17: the handle is unchanged */
    }
    if (count > 0) {
        memcpy(copy, signs, count * sizeof *copy);
    }
    s->signs = copy;
    group->signs = s;
    return CANON_COMPLETE;
}

/* spec 8.4: "each generator acts on Omega as given and swaps the last two points iff its sign
 * is -1".  With p = lift(g, s) and q = lift(h, t), (pq)[v] = q[p[v]] is (gh)[v] on Omega, and
 * on the markers the product of two swaps or two identities is the identity and of one of each
 * the swap, i.e. the marker map of the sign s t (spec 8.4 "swaps compose by sign
 * multiplication"). */
void canon_group_lift_element(const uint32_t *g, uint32_t n, int sign, uint32_t *out)
{
    if (n > 0) {
        memcpy(out, g, (size_t)n * sizeof *out);
    }
    out[n] = sign < 0 ? n + 1u : n;      /* the marker + */
    out[n + 1u] = sign < 0 ? n : n + 1u; /* the marker - */
}

uint32_t *canon_group_lift_generators(uint32_t n, const uint32_t *gens, size_t count,
                                      const int8_t *signs, canon_status *status)
{
    /* spec 11.1: "n+2 <= 2^32-1, checked before constructing its two sign points" */
    if (n > UINT32_MAX - 2u) {
        *status = CANON_CAPACITY_LIMIT;
        return NULL;
    }
    const uint32_t m = n + 2u;
    size_t words = 0;
    if (!canon_size_mul(count, (size_t)m, &words)) {
        *status = CANON_CAPACITY_LIMIT;
        return NULL;
    }
    uint32_t *out = canon_alloc_array(words, sizeof *out, status);
    if (out == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < count; ++i) {
        canon_group_lift_element(n > 0 ? gens + i * (size_t)n : NULL, n, signs[i],
                                 out + i * (size_t)m);
    }
    return out;
}

bool canon_group_character_words(uint32_t n, size_t *words)
{
    /* the lifted element and the sift residue, n + 2 words each */
    return canon_size_mul((size_t)n + 2u, 2u, words);
}

/* Public retain/release (include/canon/canon.h). */
void canon_group_retain(canon_group *group)
{
    canon_group_share(group);
}

void canon_group_release(canon_group *group)
{
    canon_group_unshare(group);
}
