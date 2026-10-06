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
    g->signs = NULL;  /* unsigned until canon_group_set_signs (S6) */
    g->inputs = NULL; /* none until canon_group_set_inputs (S7) */
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
        if (owned->inputs != NULL) {
            canon_perm_table_free(owned->inputs);
            free(owned->inputs);
        }
        free(owned);
    }
}

/* Copy `count` flat generators of degree n into a new table (rows not read for n = 0). */
static canon_status copy_rows(uint32_t n, const uint32_t *gens, size_t count, canon_perm_table *t)
{
    canon_perm_table_init(t, n);
    canon_status st = CANON_COMPLETE;
    for (size_t i = 0; i < count && st == CANON_COMPLETE; ++i) {
        uint32_t row = 0;
        /* degree 0: the row is the empty permutation and is not read */
        st = canon_perm_table_push(t, n > 0 ? gens + i * (size_t)n : NULL, &row);
    }
    if (st != CANON_COMPLETE) {
        canon_perm_table_free(t);
    }
    return st;
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
    st = copy_rows(group->degree, gens, count, &s->gens);
    if (st != CANON_COMPLETE) {
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

canon_status canon_group_set_inputs(canon_group *group, const uint32_t *gens, size_t count)
{
    if (count > UINT32_MAX - 1u) {
        return CANON_CAPACITY_LIMIT; /* spec 11.1: generator indices are uint32 */
    }
    canon_status st = CANON_COMPLETE;
    canon_perm_table *t = canon_alloc_array(1, sizeof *t, &st);
    if (t == NULL) {
        return st;
    }
    st = copy_rows(group->degree, gens, count, t);
    if (st != CANON_COMPLETE) {
        free(t);
        return st; /* spec 17: the handle is unchanged */
    }
    group->inputs = t;
    return CANON_COMPLETE;
}

const canon_perm_table *canon_group_input_generators(const canon_group *group)
{
    if (group->inputs != NULL) {
        return group->inputs;
    }
    return group->signs != NULL ? &group->signs->gens : NULL;
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

canon_status canon_group_validate_generators(uint32_t degree, const uint32_t *gens, size_t count)
{
    if (degree == 0 || count == 0) {
        return CANON_COMPLETE; /* degree 0: every generator is the empty permutation */
    }
    canon_status st = CANON_COMPLETE;
    uint64_t *bitmap = canon_alloc_array(((size_t)degree + 63u) / 64u, sizeof *bitmap, &st);
    if (bitmap == NULL) {
        return st;
    }
    bool valid = true;
    for (size_t i = 0; i < count && valid; ++i) {
        valid = canon_perm_validate_scratch(gens + i * (size_t)degree, degree, bitmap);
    }
    free(bitmap);
    return valid ? CANON_COMPLETE : CANON_INVALID_INPUT;
}

/* spec 8.4: the elements of the lift L over g are exactly lift(g, +1) and lift(g, -1), since L
 * projects onto G with the kernel fixing Omega; so g is in G iff one of them is in L, and after
 * validation (|L| = |G|, trivial kernel) at most one is.  One membership test for an even
 * member, two otherwise. */
canon_status canon_group_character_by(const canon_group *group, const void *lift,
                                      canon_lift_member_fn member, const uint32_t *g,
                                      uint32_t *scratch, int *sign)
{
    *sign = 0;
    if (lift == NULL) {
        return CANON_UNSUPPORTED_ACTION; /* an unsigned group has no character */
    }
    const uint32_t n = group->degree, m = n + 2u; /* fits: checked at creation (spec 11.1) */
    canon_status st = CANON_COMPLETE;
    uint32_t *own = NULL;
    if (scratch == NULL) {
        size_t words = 0;
        if (!canon_group_character_words(n, &words)) {
            return CANON_CAPACITY_LIMIT;
        }
        scratch = own = canon_alloc_array(words, sizeof *own, &st);
        if (own == NULL) {
            return st;
        }
    }
    uint32_t *ext = scratch, *residue = scratch + m;
    st = CANON_INVALID_INPUT; /* neither extension is in the lift: g is not in G */
    for (int s = 1; s >= -1; s -= 2) {
        canon_group_lift_element(g, n, s, ext);
        if (member(lift, ext, residue)) {
            *sign = s;
            st = CANON_COMPLETE;
            break;
        }
    }
    free(own);
    return st;
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
