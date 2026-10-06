/* The verified automorphism subgroup A_known and its prefix stabilisers (spec 7.3, 9.1, 9.2,
 * 14.3 R2; slice S7 step 1, docs/slices/S7.md 3.1, 3.2; docs/pruning-rules.md).
 *
 * Soundness (docs/pruning-rules.md section 3): every element ever placed in A_known passed
 * canon_symmetry_insert's three checks (bijection, membership in G, x^p = x), so A_known <=
 * Aut_G(x) = A.  Every strong generator of a chain built from a list of generators is a word in
 * that list (chain.c provenance), so every generator of a rebased prefix-stabiliser chain lies
 * in A_known, and one stored at level 1 of a chain with base prefix (a) fixes a (the S3 chain
 * invariant of src/bsgs/chain.h: a strong generator inserted at level j appears in S_0..S_j and
 * fixes b_0..b_{j-1}).  By induction the
 * generators of H_d fix a_1..a_d pointwise.  The rebases are not verified (S3: transient
 * chains of a descent); an incomplete rebased chain could only make the orbit partition finer,
 * which loses pruning and never soundness. */
#include "symmetry/symmetry.h"

#include <stdlib.h>
#include <string.h>

#include "arena/alloc.h"
#include "arena/checked.h"

void canon_symmetry_init(canon_symmetry *a)
{
    memset(a, 0, sizeof *a);
    canon_bsgs_init(&a->chain, 0);
    canon_perm_table_init(&a->gens, 0);
}

void canon_symmetry_free(canon_symmetry *a)
{
    canon_bsgs_free(&a->chain);
    canon_perm_table_free(&a->gens);
    for (uint32_t d = 0; d < a->slot_count; ++d) {
        canon_bsgs_free(&a->slots[d].chain);
    }
    free(a->slots);
    free(a->work);
    free(a->orbit_id);
    free(a->rep);
    free(a->in_cell);
    canon_symmetry_init(a);
}

/* Make slots[0..d] exist (grow-only; new slots hold an empty chain and are trivial). */
static canon_status ensure_slot(canon_symmetry *a, uint32_t d)
{
    if (d == UINT32_MAX) {
        return CANON_CAPACITY_LIMIT; /* unreachable: depth <= n - 1 (spec 7.2 termination) */
    }
    const uint32_t need = d + 1u;
    void *data = a->slots;
    canon_status st =
        canon_grow_array_to(&data, &a->slot_cap, a->slot_count, need, 8u, sizeof *a->slots);
    a->slots = data;
    if (st != CANON_COMPLETE) {
        return st;
    }
    for (; a->slot_count < need; ++a->slot_count) {
        canon_bsgs_init(&a->slots[a->slot_count].chain, a->n);
        a->slots[a->slot_count].trivial = true;
    }
    return CANON_COMPLETE;
}

canon_status canon_symmetry_reset(canon_symmetry *a, uint32_t n)
{
    canon_bsgs_free(&a->chain);
    canon_perm_table_free(&a->gens);
    canon_perm_table_init(&a->gens, n);
    for (uint32_t d = 0; d < a->slot_count; ++d) {
        canon_bsgs_free(&a->slots[d].chain);
        a->slots[d].trivial = true;
    }
    memset(&a->stats, 0, sizeof a->stats);
    a->n = n;
    canon_status st = CANON_COMPLETE;
    if (a->work == NULL || n > a->cap) {
        free(a->work);
        free(a->orbit_id);
        free(a->rep);
        free(a->in_cell);
        a->work = canon_alloc_array(n, sizeof *a->work, &st);
        a->orbit_id = canon_alloc_array(n, sizeof *a->orbit_id, &st);
        a->rep = canon_alloc_array(n, sizeof *a->rep, &st);
        a->in_cell = canon_alloc_array(n, sizeof *a->in_cell, &st);
        if (a->work == NULL || a->orbit_id == NULL || a->rep == NULL || a->in_cell == NULL) {
            free(a->work);
            free(a->orbit_id);
            free(a->rep);
            free(a->in_cell);
            a->work = a->orbit_id = a->rep = NULL;
            a->in_cell = NULL;
            a->cap = 0;
            return st;
        }
        a->cap = n;
    }
    memset(a->in_cell, 0, n > 0 ? (size_t)n : 1u);
    /* A_known = 1: a verified chain with no generators (as the spec 8.2 stabiliser consumer) */
    st = canon_bsgs_build_verified(&a->chain, n, NULL, 0);
    if (st != CANON_COMPLETE) {
        return st;
    }
    st = ensure_slot(a, 0);
    if (st != CANON_COMPLETE) {
        return st;
    }
    a->slots[0].trivial = true;
    return CANON_COMPLETE;
}

/* Outcome of the spec 7.3 checks on a candidate automorphism. */
typedef enum { AUT_OK, AUT_NOT_BIJECTION, AUT_NOT_MEMBER, AUT_NOT_FIXING } aut_verdict;

/* spec 7.3 "Verify ... by exact membership and object equality"; spec 14.3 R2. */
static canon_status check_automorphism(const canon_symmetry *a, const canon_group *g,
                                       const canon_root *x, canon_root_image *img,
                                       const uint32_t *p, aut_verdict *verdict)
{
    *verdict = AUT_NOT_BIJECTION;
    bool ok = false;
    canon_status st = canon_perm_check(p, a->n, &ok); /* spec 4.1: a bijection of the domain */
    if (st != CANON_COMPLETE || !ok) {
        return st;
    }
    *verdict = AUT_NOT_MEMBER;
    st = g->ops->contains(g, p, &ok); /* spec 9.1: exact membership in G */
    if (st != CANON_COMPLETE || !ok) {
        return st;
    }
    *verdict = AUT_NOT_FIXING;
    /* spec 2.1 action and spec 4.2 extensional equality: x^p = x exactly */
    st = canon_root_act_into(x, p, img);
    if (st != CANON_COMPLETE) {
        return st;
    }
    ok = canon_root_equal(&img->root, x);
    canon_root_image_clear(img); /* borrow nothing from x after the check */
    if (ok) {
        *verdict = AUT_OK;
    }
    return CANON_COMPLETE;
}

/* Insert a checked automorphism (canon_bsgs_insert_verified: a member is not inserted). */
static canon_status insert_checked(canon_symmetry *a, const uint32_t *p, bool *inserted)
{
    canon_status st = canon_bsgs_insert_verified(&a->chain, &a->gens, p, a->work, inserted);
    if (st == CANON_COMPLETE && *inserted) {
        a->stats.inserted += 1;
        a->slots[0].trivial = a->chain.order == 1u; /* H_0 = A_known */
    }
    return st;
}

canon_status canon_symmetry_insert(canon_symmetry *a, const canon_group *g, const canon_root *x,
                                   canon_root_image *img, const uint32_t *p, bool *inserted)
{
    *inserted = false;
    if (g->degree != a->n || x->n != a->n || a->work == NULL || a->slot_count == 0) {
        return CANON_INVALID_INPUT; /* module contract: reset(a, n) first, same degree */
    }
    a->stats.considered += 1;
    aut_verdict v = AUT_NOT_BIJECTION;
    canon_status st = check_automorphism(a, g, x, img, p, &v);
    if (st != CANON_COMPLETE) {
        return st;
    }
    if (v != AUT_OK) {
        a->stats.rejected += 1; /* spec 14.3 R2: never insert an unverified element */
        return CANON_INVALID_INPUT;
    }
    return insert_checked(a, p, inserted);
}

canon_status canon_symmetry_from_inputs(canon_symmetry *a, const canon_group *g,
                                        const canon_root *x, canon_root_image *img)
{
    if (g->degree != a->n || x->n != a->n || a->work == NULL || a->slot_count == 0) {
        return CANON_INVALID_INPUT;
    }
    const canon_perm_table *in = canon_group_input_generators(g);
    if (in == NULL || a->n == 0) {
        return CANON_COMPLETE; /* nothing recorded, or degree 0: A_known = 1 */
    }
    /* S7 brief 3.1 source (i): "Input generators that fix x (checked by action)" */
    for (uint32_t i = 0; i < in->count; ++i) {
        const uint32_t *p = canon_perm_table_row(in, i);
        a->stats.considered += 1;
        aut_verdict v = AUT_NOT_BIJECTION;
        canon_status st = check_automorphism(a, g, x, img, p, &v);
        if (st != CANON_COMPLETE) {
            return st;
        }
        if (v == AUT_NOT_FIXING) {
            a->stats.not_fixing += 1; /* a generator of G that is not in A: skipped */
            continue;
        }
        if (v != AUT_OK) {
            a->stats.rejected += 1;
            return CANON_INTERNAL_ERROR; /* the handle's generators are bijections in G */
        }
        bool inserted = false;
        st = insert_checked(a, p, &inserted);
        if (st != CANON_COMPLETE) {
            return st;
        }
    }
    return CANON_COMPLETE;
}

bool canon_symmetry_trivial_at(const canon_symmetry *a, uint32_t depth)
{
    return depth >= a->slot_count || a->slots[depth].trivial;
}

void canon_symmetry_view(const canon_symmetry *a, uint32_t depth, const canon_bsgs **chain,
                         uint32_t *level)
{
    if (depth == 0) {
        *chain = &a->chain; /* H_0 = A_known: every strong generator is in S_0 */
        *level = 0;
    } else {
        *chain = &a->slots[depth].chain; /* base prefix (a_depth): S_1 generates H_depth */
        *level = 1;
    }
}

canon_status canon_symmetry_descend(canon_symmetry *a, uint32_t depth, uint32_t atom)
{
    if (atom >= a->n || depth >= a->slot_count) {
        return CANON_INVALID_INPUT; /* module contract */
    }
    canon_status st = ensure_slot(a, depth + 1u); /* may move a->slots: views are taken after */
    if (st != CANON_COMPLETE) {
        return st;
    }
    canon_symmetry_slot *dst = &a->slots[depth + 1u];
    canon_bsgs_free(&dst->chain);
    dst->trivial = true;
    if (a->slots[depth].trivial) {
        return CANON_COMPLETE; /* the stabiliser of a point in 1 is 1: no rebase */
    }
    /* spec 7.3 "intersect with the stabiliser of its constraints"; spec 9.2 base change:
     * the chain of H_depth rebased to start with `atom` (no verification, S3 review item 2),
     * whose level 1 generates H_{depth+1} = (H_depth)_atom */
    const canon_bsgs *src = NULL;
    uint32_t from = 0;
    canon_symmetry_view(a, depth, &src, &from);
    a->stats.rebases += 1;
    st = canon_bsgs_rebase(src, from, &atom, 1u, false, &dst->chain);
    if (st != CANON_COMPLETE) {
        return st; /* dst->chain left empty, slot trivial */
    }
    dst->trivial = dst->chain.levels[1].gen_count == 0;
    return CANON_COMPLETE;
}

canon_status canon_symmetry_orbit_reps(canon_symmetry *a, uint32_t depth, const uint32_t *members,
                                       uint32_t len, uint32_t *reps, uint32_t *count)
{
    *count = 0;
    if (depth >= a->slot_count) {
        return CANON_INVALID_INPUT;
    }
    a->stats.orbit_calls += 1;
    for (uint32_t i = 0; i < len; ++i) {
        if (members[i] >= a->n) {
            return CANON_INVALID_INPUT;
        }
    }
    const canon_bsgs *c = NULL;
    uint32_t level = 0;
    canon_symmetry_view(a, depth, &c, &level);
    const canon_bsgs_level *L = a->slots[depth].trivial ? NULL : &c->levels[level];
    if (L == NULL || L->gen_count == 0) {
        /* H_depth = 1: every member is its own orbit */
        if (len > 0) {
            memcpy(reps, members, (size_t)len * sizeof *reps);
        }
        *count = len;
        return CANON_COMPLETE;
    }
    /* Soundness guard (docs/pruning-rules.md, lemma: C_S^a = C_S for a fixing the prefix):
     * every generator of H_depth maps the target cell into itself. */
    for (uint32_t i = 0; i < len; ++i) {
        a->in_cell[members[i]] = 1;
    }
    bool closed = true;
    for (uint32_t k = 0; k < L->gen_count && closed; ++k) {
        const uint32_t *s = canon_perm_table_row(&c->gens, L->gen_ids[k]);
        for (uint32_t i = 0; i < len && closed; ++i) {
            closed = a->in_cell[s[members[i]]] != 0;
        }
    }
    for (uint32_t i = 0; i < len; ++i) {
        a->in_cell[members[i]] = 0;
    }
    if (!closed) {
        return CANON_INTERNAL_ERROR;
    }
    /* union-find over H_depth's generators (canon_bsgs_orbit_ids, spec 7.1 orbit ranking) */
    canon_bsgs_orbit_ids(c, level, a->orbit_id);
    for (uint32_t i = 0; i < len; ++i) {
        a->rep[a->orbit_id[members[i]]] = UINT32_MAX;
    }
    /* spec 7.1: "Internal order of members within a cell is not semantic", so the
     * representative of an orbit is its numerically least atom (docs/pruning-rules.md) */
    for (uint32_t i = 0; i < len; ++i) {
        uint32_t *r = &a->rep[a->orbit_id[members[i]]];
        if (members[i] < *r) {
            *r = members[i];
        }
    }
    uint32_t k = 0;
    for (uint32_t i = 0; i < len; ++i) {
        if (a->rep[a->orbit_id[members[i]]] == members[i]) {
            reps[k++] = members[i];
        }
    }
    *count = k;
    return CANON_COMPLETE;
}
