/* The stabiliser-chain group backend (slice S3; spec 9.1, 9.2, 7.1, 7.2): canon_group_ops over
 * a verified chain (src/bsgs/chain.h).  The group is immutable after creation and may be
 * shared between threads (spec 17): every operation allocates its own scratch (tuple_min) or
 * uses the caller's (character) or none (order, contains).
 *
 * Slice S6: a signed group (spec 8.4) also keeps the verified chain of its lifted group on
 * n + 2 points, from which `character` reads chi; `conjugate` relabels the chain (spec 3.1). */
#include "bsgs/chain_backend.h"

#include <stdlib.h>

#include "arena/alloc.h"
#include "perm/perm.h"

/* Backend state: the chain of G and, for a signed group, the chain of its lift. */
typedef struct chain_impl {
    canon_bsgs chain;
    canon_bsgs *lift; /* S6: NULL for an unsigned group */
} chain_impl;

static void chain_destroy(void *impl)
{
    chain_impl *c = impl;
    if (c != NULL) {
        canon_bsgs_free(&c->chain);
        if (c->lift != NULL) {
            canon_bsgs_free(c->lift);
            free(c->lift);
        }
        free(c);
    }
}

static const canon_bsgs *chain(const canon_group *group)
{
    const chain_impl *c = group->impl;
    return &c->chain;
}

/* spec 9.2: the order is the product of the orbit lengths (it fits uint64 by construction). */
static uint64_t chain_order(const canon_group *group)
{
    return chain(group)->order;
}

/* spec 9.1/9.2: membership by sifting. */
static canon_status chain_contains(const canon_group *group, const uint32_t *p, bool *out)
{
    return canon_bsgs_contains(chain(group), p, out);
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
    return canon_bsgs_tuple_min(chain(group), L, len, t_out, orbit_id_out, NULL);
}

/* spec 8.1: the coset enumerator over the verified chain (src/coset/enumerate.c). */
static canon_status chain_enumerate(const canon_group *group, canon_coset_visitor *visitor)
{
    return canon_coset_enumerate(chain(group), visitor);
}

/* The lift's membership rule for canon_group_character_by: one sift through the lift's
 * chain (canon_bsgs_contains_scratch, the chain's single membership rule). */
static bool lift_member(const void *lift, const uint32_t *p, uint32_t *residue)
{
    return canon_bsgs_contains_scratch(lift, p, residue);
}

/* spec 8.4: chi(g) from the lift (src/bsgs/group.h). */
static canon_status chain_character(const canon_group *group, const uint32_t *g, uint32_t *scratch,
                                    int *sign)
{
    const chain_impl *c = group->impl;
    return canon_group_character_by(group, c->lift, lift_member, g, scratch, sign);
}

static const canon_group_ops chain_ops;

/* Wrap backend state in a handle (spec 17: the count comes from canon_group_alloc); on failure
 * the state is destroyed. */
static canon_status wrap(chain_impl *c, uint32_t degree, canon_group **out)
{
    canon_status st = canon_group_alloc(&chain_ops, degree, c, out);
    if (st != CANON_COMPLETE) {
        chain_destroy(c);
    }
    return st;
}

/* spec 3.1, 2.1: g^-1 G g by relabelling the verified chain through g (S5 review item 6:
 * canon_bsgs_conjugate keeps the verified flag; see its comment in chain.h). */
static canon_status chain_conjugate(const canon_group *group, const uint32_t *g, canon_group **out)
{
    *out = NULL;
    canon_status st = CANON_COMPLETE;
    chain_impl *c = canon_alloc_array(1, sizeof *c, &st);
    if (c == NULL) {
        return st;
    }
    c->lift = NULL; /* the conjugate is unsigned (signed labeling: deferred, S6 notes) */
    st = canon_bsgs_conjugate(chain(group), g, &c->chain);
    if (st != CANON_COMPLETE) {
        free(c); /* canon_bsgs_conjugate left the chain empty */
        return st;
    }
    return wrap(c, group->degree, out);
}

static const canon_group_ops chain_ops = {chain_destroy,   chain_order,    chain_contains,
                                          chain_tuple_min, chain_admits,   chain_enumerate,
                                          chain_character, chain_conjugate};

const canon_bsgs *canon_group_chain_of(const canon_group *group)
{
    return group != NULL && group->ops == &chain_ops ? chain(group) : NULL;
}

/* The shared constructor: a signed group when is_signed (then signs holds one sign per
 * generator, and may be NULL only without generators). */
static canon_status create(uint32_t degree, const uint32_t *generators, size_t generator_count,
                           bool is_signed, const int8_t *signs, canon_group **out)
{
    *out = NULL;
    if (generator_count > 0 && degree > 0 && generators == NULL) {
        return CANON_INVALID_INPUT;
    }
    if (generator_count > UINT32_MAX - 1u) {
        return CANON_CAPACITY_LIMIT; /* spec 11.1: input indices are uint32 */
    }
    if (is_signed && degree > UINT32_MAX - 2u) {
        /* spec 11.1: "the initial lifted-character validator additionally requires
         * n+2 <= 2^32-1, checked before constructing its two sign points" */
        return CANON_CAPACITY_LIMIT;
    }
    canon_status st = canon_group_validate_generators(degree, generators, generator_count);
    if (st != CANON_COMPLETE) {
        return st;
    }
    chain_impl *c = canon_alloc_array(1, sizeof *c, &st);
    if (c == NULL) {
        return st;
    }
    c->lift = NULL;
    /* Degree 0: every generator is the empty permutation and is not read. */
    const size_t count = degree > 0 ? generator_count : 0;
    /* spec 9.1: "exact verification is mandatory"; the verifier is independent of the
     * constructor (src/bsgs/verify.c). */
    st = canon_bsgs_build_verified(&c->chain, degree, generators, count);
    if (st != CANON_COMPLETE) {
        free(c); /* the build left the chain empty */
        return st;
    }
    if (is_signed) {
        /* spec 8.4: "Validate chi by constructing the lifted generated group on Omega u {+,-}
         * ... chi exists exactly when the subgroup fixing every point of Omega in this lift is
         * trivial".  The projection L -> G (restriction to Omega) is onto with that subgroup
         * as its kernel K, so |L| = |G| |K|: K is trivial iff |L| = |G| (a complete chain
         * calculation, both orders exact).  Every generator is lifted, including identities
         * and repeats (an identity with sign -1 puts the marker swap into K); degree 0 lifts
         * the empty generators to the two markers. */
        uint32_t *lifted =
            canon_group_lift_generators(degree, generators, generator_count, signs, &st);
        c->lift = lifted != NULL ? canon_alloc_array(1, sizeof *c->lift, &st) : NULL;
        if (c->lift != NULL) {
            st = canon_bsgs_build_verified(c->lift, degree + 2u, lifted, generator_count);
            if (st != CANON_COMPLETE) {
                free(c->lift); /* left empty by the build */
                c->lift = NULL;
            } else if (c->lift->order != c->chain.order) {
                st = CANON_INVALID_INPUT; /* spec 8.4: "Reject inconsistent signs" */
            }
        }
        free(lifted);
        if (st != CANON_COMPLETE) {
            chain_destroy(c);
            return st;
        }
    }
    st = wrap(c, degree, out);
    if (st == CANON_COMPLETE) {
        /* S6: a signed handle keeps its generators with their signs; S7 (brief 3.1 source i):
         * an unsigned one keeps its generators as given, for A_known */
        st = is_signed ? canon_group_set_signs(*out, generators, generator_count, signs)
                       : canon_group_set_inputs(*out, generators, generator_count);
        if (st != CANON_COMPLETE) {
            canon_group_unshare(*out);
            *out = NULL;
        }
    }
    return st;
}

canon_status canon_group_chain_create(uint32_t degree, const uint32_t *generators,
                                      size_t generator_count, canon_group **out)
{
    return create(degree, generators, generator_count, false, NULL, out);
}

canon_status canon_group_chain_create_signed(uint32_t degree, const uint32_t *generators,
                                             size_t generator_count, const int8_t *signs,
                                             canon_group **out)
{
    if (generator_count > 0 && signs == NULL) {
        *out = NULL;
        return CANON_INVALID_INPUT;
    }
    return create(degree, generators, generator_count, true, signs, out);
}
