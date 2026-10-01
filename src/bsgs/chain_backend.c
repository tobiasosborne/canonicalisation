/* The stabiliser-chain group backend (slice S3; spec 9.1, 9.2, 7.1, 7.2): canon_group_ops over
 * a verified chain (src/bsgs/chain.h).  The group is immutable after creation and may be
 * shared between threads (spec 17): every operation allocates its own scratch (tuple_min) or
 * none (order, contains). */
#include "bsgs/chain_backend.h"

#include <stdlib.h>

#include "arena/alloc.h"
#include "perm/perm.h"

static void chain_destroy(void *impl)
{
    canon_bsgs *c = impl;
    if (c != NULL) {
        canon_bsgs_free(c);
        free(c);
    }
}

/* spec 9.2: the order is the product of the orbit lengths (it fits uint64 by construction). */
static uint64_t chain_order(const canon_group *group)
{
    const canon_bsgs *c = group->impl;
    return c->order;
}

/* spec 9.1/9.2: membership by sifting. */
static canon_status chain_contains(const canon_group *group, const uint32_t *p, bool *out)
{
    return canon_bsgs_contains(group->impl, p, out);
}

/* spec 11.1: S3 brief 2.5 - the chain is limited only by its uint64 order, which every built
 * chain satisfies, so every problem descriptor admits it in S3. */
static canon_status chain_admits(const canon_group *group, const canon_capacity *cap)
{
    (void)group;
    (void)cap;
    return CANON_COMPLETE;
}

/* spec 7.1 G stage, 7.2 leaf map: see src/bsgs/group.h and canon_bsgs_tuple_min. */
static canon_status chain_tuple_min(const canon_group *group, const uint32_t *L, uint32_t len,
                                    uint32_t *t_out, uint32_t *orbit_id_out)
{
    return canon_bsgs_tuple_min(group->impl, L, len, t_out, orbit_id_out, NULL);
}

/* spec 8.1: the coset enumerator over the verified chain (src/coset/enumerate.c). */
static canon_status chain_enumerate(const canon_group *group, canon_coset_visitor *visitor)
{
    return canon_coset_enumerate(group->impl, visitor);
}

static const canon_group_ops chain_ops = {chain_destroy,   chain_order,  chain_contains,
                                          chain_tuple_min, chain_admits, chain_enumerate};

const canon_bsgs *canon_group_chain_of(const canon_group *group)
{
    return group != NULL && group->ops == &chain_ops ? group->impl : NULL;
}

canon_status canon_group_chain_create(uint32_t degree, const uint32_t *generators,
                                      size_t generator_count, canon_group **out)
{
    *out = NULL;
    if (generator_count > 0 && degree > 0 && generators == NULL) {
        return CANON_INVALID_INPUT;
    }
    if (generator_count > UINT32_MAX - 1u) {
        return CANON_CAPACITY_LIMIT; /* spec 11.1: input indices are uint32 */
    }
    canon_status st = CANON_COMPLETE;
    /* spec 4.1, 9.1: generators must be bijections of the domain (one scratch bitmap). */
    if (degree > 0 && generator_count > 0) {
        uint64_t *bitmap = canon_alloc_array(((size_t)degree + 63u) / 64u, sizeof *bitmap, &st);
        if (bitmap == NULL) {
            return st;
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
    canon_bsgs *c = canon_alloc_array(1, sizeof *c, &st);
    if (c == NULL) {
        return st;
    }
    /* Degree 0: every generator is the empty permutation and is not read. */
    const size_t count = degree > 0 ? generator_count : 0;
    /* spec 9.1: "exact verification is mandatory"; the verifier is independent of the
     * constructor (src/bsgs/verify.c). */
    st = canon_bsgs_build_verified(c, degree, generators, count);
    if (st == CANON_COMPLETE) {
        /* spec 17: the handle and its reference count come from canon_group_alloc. */
        st = canon_group_alloc(&chain_ops, degree, c, out);
    }
    if (st != CANON_COMPLETE) {
        chain_destroy(c);
    }
    return st;
}
