/* Enumeration objectives (spec 8.2) and the deterministic witness (spec 3) over the disjoint
 * coset enumerator of spec 8.1 (slice S4, docs/slices/S4.md 3.3). */
#include "search/objectives.h"

#include <stdlib.h>
#include <string.h>

#include "arena/alloc.h"
#include "arena/checked.h"
#include "bsgs/chain.h"
#include "encoding/simple_upper.h"
#include "perm/perm.h"

void canon_obj_search_init(canon_obj_search *s)
{
    memset(s, 0, sizeof *s);
    canon_root_image_init(&s->image);
    canon_buf_init(&s->key);
    canon_buf_init(&s->best_key);
    canon_buf_init(&s->bytes);
    canon_buf_init(&s->group);
    canon_coset_scratch_init(&s->scratch);
}

void canon_obj_search_free(canon_obj_search *s)
{
    free(s->best); /* one block: best, work, g */
    canon_coset_scratch_free(&s->scratch);
    canon_root_image_free(&s->image);
    canon_buf_free(&s->key);
    canon_buf_free(&s->best_key);
    canon_buf_free(&s->bytes);
    canon_buf_free(&s->group);
    canon_obj_search_init(s);
}

/* Per-point arrays for degree n, grown only (spec 17 reusable workspace storage). */
static canon_status prepare(canon_obj_search *s, uint32_t n)
{
    if (s->best != NULL && n <= s->cap) {
        return CANON_COMPLETE;
    }
    size_t words = 0;
    if (!canon_size_mul((size_t)n, 3u, &words)) {
        return CANON_CAPACITY_LIMIT; /* spec 11.1 */
    }
    canon_status st = CANON_COMPLETE;
    uint32_t *block = canon_alloc_array(words, sizeof *block, &st);
    if (block == NULL) {
        return st; /* the old arrays are kept (spec 17) */
    }
    free(s->best);
    s->best = block;
    s->work = block + (size_t)n;
    s->g = block + 2u * (size_t)n;
    s->cap = n;
    return CANON_COMPLETE;
}

/* ---- consumers (spec 8.2) ---- */

typedef struct obj_ctx {
    canon_obj_search *s;
    const canon_root *x;
    const canon_root *y; /* transporter target; for the stabiliser, x itself */
    canon_order order;
    bool deterministic;
    bool have;               /* minimum: an incumbent exists; transporter: a hit was found */
    canon_bsgs *a;           /* stabiliser: verified chain of A_known */
    canon_perm_table *agens; /* stabiliser: the generators inserted into A_known */
} obj_ctx;

/* spec 8.2 Minimum: "Compare the exact selected order key of x^r; retain the least.  Exhaust
 * all leaves for completion."  Ties: under the deterministic witness mode the lexicographically
 * smaller r (spec 3: "minimise the image array among all solutions sending x to the selected
 * c"); otherwise the first found by the reference traversal. */
static canon_status consume_min(void *user, const uint32_t *r, bool *stop)
{
    obj_ctx *c = user;
    canon_obj_search *s = c->s;
    const uint32_t n = c->x->n;
    (void)stop;
    canon_status st = canon_root_act_into(c->x, r, &s->image); /* spec 2.1: x^r */
    if (st != CANON_COMPLETE) {
        return st;
    }
    canon_buf_truncate(&s->key, 0);
    if (c->order == CANON_ORDER_SIMPLE_UPPER_1) {
        /* spec 4.4: the key of the image graph (the class was checked at problem creation and
         * is invariant under the action) */
        st = canon_simple_upper_key(&s->image.root.u.graph, &s->key);
    } else {
        /* spec 4.3 CDAG-BYTE-1: the complete canonical stream */
        st = canon_root_stream_write(&s->image.root, &s->key);
    }
    if (st != CANON_COMPLETE) {
        return st;
    }
    int cmp = -1;
    if (c->have) {
        /* spec 4.3, 4.4: unsigned bytes, a proper prefix smaller */
        cmp = canon_bytes_compare(s->key.data, s->key.len, s->best_key.data, s->best_key.len);
        if (cmp == 0 && c->deterministic) {
            cmp = canon_perm_lex_compare(r, s->best, n);
        }
    }
    if (cmp < 0) {
        const canon_buf held = s->best_key; /* swap: the new key becomes the incumbent */
        s->best_key = s->key;
        s->key = held;
        if (n > 0) {
            memcpy(s->best, r, (size_t)n * sizeof *s->best);
        }
        c->have = true;
    }
    return CANON_COMPLETE;
}

/* spec 8.2 Transporter one: "Test exact x^r=y; stop successfully on one hit." */
static canon_status consume_transporter(void *user, const uint32_t *r, bool *stop)
{
    obj_ctx *c = user;
    canon_obj_search *s = c->s;
    canon_status st = canon_root_act_into(c->x, r, &s->image);
    if (st != CANON_COMPLETE) {
        return st;
    }
    /* by object equality, not by stream comparison (docs/slices/S4.md 3.3) */
    if (canon_root_equal(&s->image.root, c->y)) {
        s->stats.hits += 1;
        if (c->x->n > 0) {
            memcpy(s->best, r, (size_t)c->x->n * sizeof *s->best);
        }
        c->have = true;
        *stop = true;
    }
    return CANON_COMPLETE;
}

/* spec 8.2 Stabiliser: "Test x^r=x; insert every hit into a verified subgroup."  A hit is
 * inserted into the verified chain of A_known unless it is already a member
 * (canon_bsgs_insert_verified: membership by the chain's one sift rule; a non-member is
 * appended to the generators and the chain rebuilt and verified).  Each insertion at least
 * doubles |A_known|, so there are at most log2 |A| rebuilds.  "Exhaustion proves every member
 * of A was inserted and no other one was": every hit is a member of A (x^r = x) and every
 * member of G is consumed once, so after exhaustion A_known = A. */
static canon_status consume_stabiliser(void *user, const uint32_t *r, bool *stop)
{
    obj_ctx *c = user;
    canon_obj_search *s = c->s;
    (void)stop;
    canon_status st = canon_root_act_into(c->x, r, &s->image);
    if (st != CANON_COMPLETE) {
        return st;
    }
    if (!canon_root_equal(&s->image.root, c->x)) {
        return CANON_COMPLETE;
    }
    s->stats.hits += 1;
    bool inserted = false;
    st = canon_bsgs_insert_verified(c->a, c->agens, r, s->work, &inserted);
    s->stats.stab_builds += inserted;
    return st;
}

/* ---- runs ---- */

/* One enumeration of G with a consumer, sharing the visitor (and so the quota) of the run. */
static canon_status enumerate(const canon_group *g, canon_coset_visitor *v,
                              canon_coset_consume_fn consume, obj_ctx *c)
{
    v->consume = consume;
    v->user = c;
    canon_status st = g->ops->enumerate(g, v);
    canon_obj_search *s = c->s;
    s->stats.nodes = v->nodes;
    s->stats.leaves = v->leaves;
    s->stats.coset = v->stats;
    return st;
}

/* The complete stabiliser A of x as a verified chain in *a (initialised here; the caller frees
 * it on every status). */
static canon_status stabiliser(canon_obj_search *s, const canon_group *g, const canon_root *x,
                               canon_coset_visitor *v, canon_bsgs *a)
{
    canon_status st = canon_bsgs_build_verified(a, x->n, NULL, 0); /* A_known = 1 */
    if (st != CANON_COMPLETE) {
        return st; /* *a was left empty */
    }
    canon_perm_table agens; /* grow-only, freed with the run */
    canon_perm_table_init(&agens, x->n);
    obj_ctx c = {s, x, x, CANON_ORDER_CDAG_BYTE_1, false, false, a, &agens};
    st = enumerate(g, v, consume_stabiliser, &c);
    canon_perm_table_free(&agens);
    return st;
}

static canon_status run(canon_obj_search *s, const canon_group *g, const canon_root *x,
                        const canon_root *y, canon_objective objective, canon_order order,
                        bool deterministic, canon_coset_visitor *v, canon_obj_outcome *out)
{
    const uint32_t n = x->n;
    canon_status st = CANON_COMPLETE;
    obj_ctx c = {s, x, y, order, deterministic, false, NULL, NULL};
    switch (objective) {
    case CANON_OBJECTIVE_LEX_MIN_IMAGE:
        st = enumerate(g, v, consume_min, &c);
        if (st == CANON_COMPLETE && !c.have) {
            st = CANON_INTERNAL_ERROR; /* G has at least the identity */
        }
        if (st == CANON_COMPLETE) {
            /* spec 4.4: "return the selected graph in CDAG-2 plus its order key"; for
             * CDAG-BYTE-1 the key is the stream itself */
            st = canon_root_act_into(x, s->best, &s->image);
            if (st == CANON_COMPLETE) {
                canon_buf_truncate(&s->bytes, 0);
                st = canon_root_stream_write(&s->image.root, &s->bytes);
            }
        }
        if (st == CANON_COMPLETE) {
            /* spec 3.2: the minimum is proved by exhaustion of all leaves */
            out->flags.minimum_proved = true;
            out->flags.witness_valid = true;
            out->flags.encoding_complete = true;
            out->witness = out->bytes = true;
            out->key = order == CANON_ORDER_SIMPLE_UPPER_1;
        }
        return st;
    case CANON_OBJECTIVE_TRANSPORTER_ONE:
        st = enumerate(g, v, consume_transporter, &c);
        if (st == CANON_COMPLETE) {
            /* spec 3.2: "a positive transporter witness needs no negative-search coverage";
             * spec 8.2: "Declare empty only after exhaustion." */
            out->flags.witness_valid = c.have;
            out->flags.transport_exhausted = !c.have;
            out->witness = c.have;
        }
        return st;
    case CANON_OBJECTIVE_STABILISER: {
        canon_bsgs a;
        st = stabiliser(s, g, x, v, &a);
        if (st == CANON_COMPLETE) {
            canon_buf_truncate(&s->group, 0);
            st = canon_group_bytes_write(&s->group, &a, &s->stats.group, &s->scratch); /* 9.4 */
        }
        canon_bsgs_free(&a);
        if (st == CANON_COMPLETE) {
            out->flags.subgroup_verified = true;
            out->flags.stabiliser_complete = true;
            out->group = true;
        }
        return st;
    }
    case CANON_OBJECTIVE_TRANSPORTER_COSET: {
        /* spec 8.2: "Find one g, then run complete stabiliser consumer for x; return A g.
         * All solutions r satisfy r g^-1 in A.  If no g, return exhausted empty." */
        st = enumerate(g, v, consume_transporter, &c);
        if (st != CANON_COMPLETE || !c.have) {
            out->flags.transport_exhausted = st == CANON_COMPLETE;
            return st;
        }
        if (n > 0) {
            memcpy(s->g, s->best, (size_t)n * sizeof *s->g);
        }
        canon_bsgs a;
        st = stabiliser(s, g, x, v, &a);
        if (st == CANON_COMPLETE) {
            /* spec 9.4: Group(A) || Perm(r0), r0 the least element of A g; spec 3: in
             * deterministic mode the witness is the least solution, r0 itself, written
             * straight into the witness buffer (computed once, S4 review item 4) */
            canon_buf_truncate(&s->group, 0);
            st = canon_coset_bytes_write(&s->group, &a, s->g, deterministic ? s->best : s->work,
                                         &s->stats.group, &s->scratch);
        }
        canon_bsgs_free(&a);
        if (st == CANON_COMPLETE) {
            out->flags.witness_valid = true;
            out->flags.subgroup_verified = true;
            out->flags.stabiliser_complete = true;
            out->witness = out->group = true;
        }
        return st;
    }
    default:
        return CANON_UNSUPPORTED_ACTION; /* the API admits only the four above */
    }
}

canon_status canon_obj_run(canon_obj_search *s, const canon_group *g, const canon_root *x,
                           const canon_root *y, canon_objective objective, canon_order order,
                           bool deterministic, uint64_t quota, canon_obj_outcome *out)
{
    memset(out, 0, sizeof *out);
    memset(&s->stats, 0, sizeof s->stats);
    /* module contract (the API checks the same at creation): the transporters need a target of
     * x's kind and degree, the other objectives take none (S4 review item 1) */
    const bool needs_y = objective == CANON_OBJECTIVE_TRANSPORTER_ONE ||
                         objective == CANON_OBJECTIVE_TRANSPORTER_COSET;
    if (g->degree != x->n || needs_y != (y != NULL) ||
        (y != NULL && (y->kind != x->kind || y->n != x->n))) {
        return CANON_INVALID_INPUT;
    }
    canon_status st = prepare(s, x->n);
    if (st != CANON_COMPLETE) {
        return st;
    }
    canon_coset_visitor v;
    canon_coset_visitor_init(&v, NULL, NULL, NULL, quota);
    v.scratch = &s->scratch; /* grow-only, owned by the workspace */
    st = run(s, g, x, y, objective, order, deterministic, &v, out);
    /* Invariant (objectives.h): the image borrowed x's graph tables; drop them on every
     * outcome so that nothing in the workspace points into x after the run. */
    canon_root_image_clear(&s->image);
    if (st != CANON_COMPLETE) {
        memset(out, 0, sizeof *out); /* spec 17: never a fabricated completion flag */
    }
    return st;
}

canon_status canon_obj_deterministic_witness(canon_obj_search *s, const canon_group *g,
                                             const canon_root *x, const uint32_t *t, uint64_t quota)
{
    memset(&s->stats, 0, sizeof s->stats);
    if (g->degree != x->n) {
        return CANON_INVALID_INPUT;
    }
    canon_status st = prepare(s, x->n);
    if (st != CANON_COMPLETE) {
        return st;
    }
    canon_coset_visitor v;
    canon_coset_visitor_init(&v, NULL, NULL, NULL, quota);
    v.scratch = &s->scratch; /* grow-only, owned by the workspace */
    canon_bsgs a;
    st = stabiliser(s, g, x, &v, &a);
    canon_root_image_clear(&s->image); /* objectives.h invariant */
    if (st == CANON_COMPLETE) {
        /* spec 3: "minimising A g after A is proved complete" */
        bool found = false;
        st = canon_coset_least(&a, 0, t, NULL, 0, s->best, &found, &s->stats.coset, &s->scratch);
        st = st == CANON_COMPLETE && !found ? CANON_INTERNAL_ERROR : st;
    }
    canon_bsgs_free(&a);
    return st;
}

canon_status canon_obj_check_witness(const canon_group *g, const canon_root *x, const uint32_t *w,
                                     const uint8_t *c, size_t c_len, bool *valid)
{
    *valid = false;
    const uint32_t n = x->n;
    if (g->degree != n) {
        return CANON_INVALID_INPUT;
    }
    /* a witness must be a permutation of the domain before any group operation reads it */
    const int bijective = canon_perm_validate(w, n);
    if (bijective < 0) {
        return CANON_RESOURCE_LIMIT;
    }
    if (bijective == 0) {
        return CANON_COMPLETE;
    }
    /* spec 17: "result_verify_witness checks membership and exact action, not canonicity" */
    bool member = false;
    canon_status st = g->ops->contains(g, w, &member);
    if (st != CANON_COMPLETE || !member) {
        return st;
    }
    canon_root_image img;
    canon_root_image_init(&img);
    canon_buf stream;
    canon_buf_init(&stream);
    st = canon_root_act_into(x, w, &img); /* x^w */
    if (st == CANON_COMPLETE) {
        st = canon_root_stream_write(&img.root, &stream);
    }
    if (st == CANON_COMPLETE) {
        *valid = canon_bytes_compare(stream.data, stream.len, c, c_len) == 0;
    }
    canon_root_image_clear(&img);
    canon_root_image_free(&img);
    canon_buf_free(&stream);
    return st;
}
