/* Enumeration objectives (spec 8.2) and the deterministic witness (spec 3) over the disjoint
 * coset enumerator of spec 8.1 (slice S4, docs/slices/S4.md 3.3); the labeling coset (spec 3.1,
 * 8.2) and the signed canonical image (spec 8.4, 7.3) of slice S6 (docs/slices/S6.md 3.2,
 * 3.3). */
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
    canon_root_image_init(&s->prime);
    canon_buf_init(&s->key);
    canon_buf_init(&s->best_key);
    canon_buf_init(&s->bytes);
    canon_buf_init(&s->group);
    canon_coset_scratch_init(&s->scratch);
}

void canon_obj_search_free(canon_obj_search *s)
{
    free(s->best); /* one block: best, work, g, lambda, chi */
    canon_coset_scratch_free(&s->scratch);
    canon_root_image_free(&s->image);
    canon_root_image_free(&s->prime);
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
    /* best, work, g, lambda: n words each; chi: canon_group_character_words(n) */
    size_t words = 0, chi_words = 0;
    if (!canon_size_mul((size_t)n, 4u, &words) || !canon_group_character_words(n, &chi_words) ||
        !canon_size_add(words, chi_words, &words)) {
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
    s->lambda = block + 3u * (size_t)n;
    s->chi = block + 4u * (size_t)n;
    s->cap = n;
    return CANON_COMPLETE;
}

/* Status of canon_perm_validate as (status, *ok): -1 is an allocation failure. */
static canon_status bijection(const uint32_t *p, uint32_t n, bool *ok)
{
    const int v = canon_perm_validate(p, n);
    *ok = v == 1;
    return v < 0 ? CANON_RESOURCE_LIMIT : CANON_COMPLETE;
}

/* ---- consumers (spec 8.2) ---- */

typedef struct obj_ctx {
    canon_obj_search *s;
    const canon_root *x;
    const canon_root *y; /* transporter target; for the stabiliser, x itself */
    canon_order order;
    bool deterministic;
    bool have;               /* minimum: an incumbent exists; transporter: a hit was found;
                                signed stabiliser (S6): an odd automorphism was found */
    canon_bsgs *a;           /* stabiliser: verified chain of A_known */
    canon_perm_table *agens; /* stabiliser: the generators inserted into A_known */
    const canon_group *group; /* S6 signed stabiliser: the signed group (chi); else NULL */
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
    canon_status st = canon_root_act_into(c->x, r, &s->image);
    if (st != CANON_COMPLETE) {
        return st;
    }
    if (!canon_root_equal(&s->image.root, c->x)) {
        return CANON_COMPLETE;
    }
    s->stats.hits += 1;
    if (c->group != NULL) {
        /* spec 8.4 (S6): "enumerate the stabiliser as in §8.2, stopping immediately if an odd
         * witness is verified": r is in G (it is a leaf of G's enumeration) and fixes x (the
         * exact test above), so chi(r) = -1 makes it a complete one-sided zero certificate
         * ("If a in A and chi(a) = -1, then [x] = -[x], hence [x] = 0 over Q"). */
        int sign = 0;
        st = c->group->ops->character(c->group, r, s->chi, &sign);
        s->stats.characters += 1;
        if (st == CANON_INVALID_INPUT) {
            return CANON_INTERNAL_ERROR; /* a leaf of G's enumeration is in G */
        }
        if (st != CANON_COMPLETE) {
            return st;
        }
        if (sign < 0) {
            if (c->x->n > 0) {
                memcpy(s->best, r, (size_t)c->x->n * sizeof *s->best);
            }
            c->have = true;
            *stop = true;
            return CANON_COMPLETE;
        }
    }
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
 * it on every status).  With `odd` (S6, signed: g is a signed group) chi is evaluated on every
 * hit and the enumeration stops at the first odd one, with *odd = true and the hit in s->best
 * (A is then incomplete); otherwise *odd = false and, A being complete, spec 8.4 "check chi=+1
 * on its generators" is done here (an odd generator would contradict the per-hit test:
 * CANON_INTERNAL_ERROR). */
static canon_status stabiliser_run(canon_obj_search *s, const canon_group *g, const canon_root *x,
                                   canon_coset_visitor *v, canon_bsgs *a, bool *odd)
{
    canon_status st = canon_bsgs_build_verified(a, x->n, NULL, 0); /* A_known = 1 */
    if (st != CANON_COMPLETE) {
        return st; /* *a was left empty */
    }
    canon_perm_table agens; /* grow-only, freed with the run */
    canon_perm_table_init(&agens, x->n);
    obj_ctx c = {s, x, x, CANON_ORDER_CDAG_BYTE_1, false, false, a, &agens, NULL};
    c.group = odd != NULL ? g : NULL;
    st = enumerate(g, v, consume_stabiliser, &c);
    if (odd != NULL) {
        *odd = st == CANON_COMPLETE && c.have;
    }
    for (uint32_t i = 0; st == CANON_COMPLETE && odd != NULL && !*odd && i < agens.count; ++i) {
        int sign = 0;
        st = g->ops->character(g, canon_perm_table_row(&agens, i), s->chi, &sign);
        s->stats.characters += 1;
        st = st == CANON_COMPLETE && sign != 1 ? CANON_INTERNAL_ERROR : st;
    }
    canon_perm_table_free(&agens);
    return st;
}

static canon_status stabiliser(canon_obj_search *s, const canon_group *g, const canon_root *x,
                               canon_coset_visitor *v, canon_bsgs *a)
{
    return stabiliser_run(s, g, x, v, a, NULL);
}

static canon_status run(canon_obj_search *s, const canon_group *g, const canon_root *x,
                        const canon_root *y, canon_objective objective, canon_order order,
                        bool deterministic, canon_coset_visitor *v, canon_obj_outcome *out)
{
    const uint32_t n = x->n;
    canon_status st = CANON_COMPLETE;
    obj_ctx c = {s, x, y, order, deterministic, false, NULL, NULL, NULL};
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

/* ---- slice S6: labeling coset (spec 3.1, 8.2) ---- */

/* Start a run of a P1-based objective: zero the outcome and counters, check the module
 * contract and size the per-point arrays. */
static canon_status start_p1_run(canon_obj_search *s, const canon_group *g, const canon_root *x,
                                 canon_obj_outcome *out)
{
    memset(out, 0, sizeof *out);
    memset(&s->stats, 0, sizeof s->stats);
    if (g->degree != x->n) {
        return CANON_INVALID_INPUT;
    }
    return prepare(s, x->n);
}

/* The quota left after `used` units (spec 11.1: one quota for the whole solve).  A used count
 * equal to the quota leaves 0, which the next traversal refuses at its first unit. */
static uint64_t remaining(uint64_t quota, uint64_t used)
{
    return used < quota ? quota - used : 0;
}

/* Run P1 (spec 7) for root x under g with the given quota; t is copied to s->best. */
static canon_status run_p1(canon_obj_search *s, canon_p1_search *p1, const canon_group *g,
                           const canon_root *x, uint64_t quota)
{
    canon_status st = canon_p1_search_run(p1, g, x, quota); /* quota 0: refused at the root */
    s->stats.p1_nodes = p1->nodes;
    if (st == CANON_COMPLETE && x->n > 0) {
        memcpy(s->best, p1->best_t, (size_t)x->n * sizeof *s->best);
    }
    return st;
}

static canon_status labeling(canon_obj_search *s, canon_p1_search *p1, const canon_group *g,
                             const canon_root *x, const uint32_t *rho, uint64_t quota,
                             canon_obj_outcome *out)
{
    const uint32_t n = x->n;
    /* spec 3.1: "Compute x' = x^rho and G' = rho^-1 G rho <= Sym(D_n)".  G' is built by the
     * group's backend: rho^-1 g rho for every element, i.e. h'[rho[v]] = rho[g[v]] (rho^-1
     * acts first, spec 3). */
    canon_group *gp = NULL;
    canon_status st = g->ops->conjugate(g, rho, &gp);
    if (st != CANON_COMPLETE) {
        return st;
    }
    st = canon_root_act_into(x, rho, &s->prime); /* spec 2.1: x' = x^rho on D_n */
    if (st == CANON_COMPLETE && s->prime.root.kind == CANON_ROOT_GRAPH) {
        /* the O stage of P1 reads the CSR/CSC index (spec 10), which images lack */
        st = canon_graph_build_index(&s->prime.root.u.graph);
    }
    if (st == CANON_COMPLETE) {
        /* spec 3.1: "Solve on D_n for t in G'"; spec 8.2: "Run §7 on target coordinates" */
        st = run_p1(s, p1, gp, &s->prime.root, quota);
    }
    canon_root_image_clear(&s->prime); /* x' borrowed x's tables (objectives.h invariant) */
    canon_group_unshare(gp);
    if (st != CANON_COMPLETE) {
        return st;
    }
    /* spec 3.1: "return lambda = rho t : Omega -> D_n, c = (x^rho)^t = x^lambda": rho acts
     * first, (rho t)[v] = t[rho[v]] (spec 3) */
    canon_perm_compose(rho, s->best, s->lambda, n);
    /* spec 8.2: "then complete stabiliser" of x under G on the source domain Omega; spec 3.1:
     * "The complete set of labelings taking x to c is A lambda" */
    canon_coset_visitor v;
    canon_coset_visitor_init(&v, NULL, NULL, NULL, remaining(quota, s->stats.p1_nodes));
    v.scratch = &s->scratch;
    canon_bsgs a;
    st = stabiliser(s, g, x, &v, &a);
    if (st == CANON_COMPLETE) {
        /* spec 9.4: "For a labeling coset H r, first choose its least image-array element r0
         * by successive point constraints, then encode the canonical Group(H) and Perm(r0)":
         * the coset descent applies unchanged, since lambda is a bijection of {0..n-1} in
         * machine terms and A acts first (A lambda = {a lambda}) */
        canon_buf_truncate(&s->group, 0);
        st = canon_coset_bytes_write(&s->group, &a, s->lambda, s->work, &s->stats.group,
                                     &s->scratch);
    }
    canon_bsgs_free(&a);
    if (st == CANON_COMPLETE) {
        /* spec 3.2: P1's coverage (unpruned tree) for c, the complete stabiliser for A lambda */
        out->flags.image_canonical = true;
        out->flags.witness_valid = true;
        out->flags.subgroup_verified = true;
        out->flags.stabiliser_complete = true;
        out->flags.encoding_complete = true;
        out->witness = out->p1 = out->labeling = out->group = true;
    }
    return st;
}

canon_status canon_obj_labeling(canon_obj_search *s, canon_p1_search *p1, const canon_group *g,
                                const canon_root *x, const uint32_t *rho, uint64_t quota,
                                canon_obj_outcome *out)
{
    canon_status st = start_p1_run(s, g, x, out);
    bool bijective = false;
    if (st == CANON_COMPLETE) {
        /* module contract (the API checks the same at creation): rho is a bijection
         * Omega -> D_n (spec 3.1) */
        st = rho != NULL ? bijection(rho, x->n, &bijective) : CANON_INVALID_INPUT;
        st = st == CANON_COMPLETE && !bijective ? CANON_INVALID_INPUT : st;
    }
    if (st == CANON_COMPLETE) {
        st = labeling(s, p1, g, x, rho, quota, out);
    }
    canon_root_image_clear(&s->image); /* objectives.h invariant */
    if (st != CANON_COMPLETE) {
        memset(out, 0, sizeof *out); /* spec 17: never a fabricated completion flag */
    }
    return st;
}

/* ---- slice S6: signed canonical image (spec 8.4, 7.3) ---- */

/* The nonzero answer from a complete stabiliser *a (spec 8.4): run P1 for c = x^t, s = chi(t),
 * Group(A) as the complete-stabiliser evidence.  `even` says that A = G was found even on the
 * generators (fast path), so chi(t) = +1 must hold. */
static canon_status signed_nonzero(canon_obj_search *s, canon_p1_search *p1, const canon_group *g,
                                   const canon_root *x, const canon_bsgs *a, uint64_t quota,
                                   bool even, canon_obj_outcome *out)
{
    /* spec 8.4: "run/finish P1 for c=x^t, and return s=chi(t), so [x]=s[c].  If p and q reach
     * c, p q^-1 in A makes their signs equal." */
    canon_status st = run_p1(s, p1, g, x, quota);
    int sign = 0;
    if (st == CANON_COMPLETE) {
        st = g->ops->character(g, s->best, s->chi, &sign);
        s->stats.characters += 1;
        st = st == CANON_INVALID_INPUT ? CANON_INTERNAL_ERROR : st; /* t is in G */
    }
    if (st == CANON_COMPLETE && even && sign != 1) {
        st = CANON_INTERNAL_ERROR; /* spec 7.3: "otherwise all of G=A is even" */
    }
    if (st == CANON_COMPLETE) {
        canon_buf_truncate(&s->group, 0);
        st = canon_group_bytes_write(&s->group, a, &s->stats.group, &s->scratch); /* spec 9.4 */
    }
    if (st == CANON_COMPLETE) {
        /* spec 3.2: "an empty transporter, minimum, canonical image, nonzero sign or complete
         * stabiliser needs its applicable coverage evidence": the unpruned P1 tree and the
         * complete A ("proved absence of odd stabilisers", spec 3 table) */
        out->flags.nonzero_certified = true;
        out->flags.image_canonical = true;
        out->flags.witness_valid = true;
        out->flags.subgroup_verified = true;
        out->flags.stabiliser_complete = true;
        out->flags.encoding_complete = true;
        out->witness = out->p1 = out->group = true;
        out->sign = sign;
    }
    return st;
}

/* A certified zero (spec 8.4 "one-sided zero certificate"; spec 3.2 "A zero certificate can
 * complete signed search early without a canonical monomial"): s->best holds the verified odd
 * automorphism. */
static void signed_zero(canon_obj_outcome *out)
{
    out->flags.zero_certified = true;
    out->flags.witness_valid = true;
    out->witness = true;
    out->sign = 0;
}

static canon_status signed_image(canon_obj_search *s, canon_p1_search *p1, const canon_group *g,
                                 const canon_root *x, uint64_t quota, canon_obj_outcome *out)
{
    const uint32_t n = x->n;
    const canon_group_signs *sg = g->signs;
    /* spec 7.3: "If every generator fixes x, the unsigned orbit is a singleton and returning x
     * is equivalent to P1 regardless of trace."  Degree 0: the only permutation is the
     * identity, which fixes x. */
    bool fixes_all = true;
    for (uint32_t i = 0; fixes_all && n > 0 && i < sg->gens.count; ++i) {
        canon_status st = canon_root_act_into(x, canon_perm_table_row(&sg->gens, i), &s->image);
        if (st != CANON_COMPLETE) {
            return st;
        }
        fixes_all = canon_root_equal(&s->image.root, x);
    }
    canon_bsgs a;
    canon_status st = CANON_COMPLETE;
    if (fixes_all) {
        /* "Signed mode in this case tests chi on generators: any odd one proves zero, otherwise
         * all of G=A is even."  Exact: A = G, and chi is a homomorphism. */
        s->stats.fast_path = 1;
        for (uint32_t i = 0; i < sg->gens.count; ++i) {
            if (sg->signs[i] < 0) {
                if (n > 0) {
                    memcpy(s->best, canon_perm_table_row(&sg->gens, i),
                           (size_t)n * sizeof *s->best);
                }
                signed_zero(out);
                return CANON_COMPLETE;
            }
        }
        /* A = G: its verified chain from the generators (spec 9.1), for the Group(A) evidence
         * ("A caller requesting a trace certificate receives the prescribed trace": P1 runs) */
        st = canon_bsgs_build_verified(&a, n, n > 0 ? sg->gens.data : NULL,
                                       n > 0 ? sg->gens.count : 0);
        if (st == CANON_COMPLETE) {
            st = signed_nonzero(s, p1, g, x, &a, quota, true, out);
            canon_bsgs_free(&a);
        }
        return st;
    }
    /* spec 8.4 reference algorithm: "enumerate the stabiliser as in §8.2, stopping immediately
     * if an odd witness is verified.  Otherwise exhaustion gives complete A; check chi=+1 on
     * its generators, run/finish P1".  SIGN-COVER is open: the nonzero route always completes
     * the stabiliser. */
    canon_coset_visitor v;
    canon_coset_visitor_init(&v, NULL, NULL, NULL, quota);
    v.scratch = &s->scratch;
    bool odd = false;
    st = stabiliser_run(s, g, x, &v, &a, &odd);
    if (st == CANON_COMPLETE && odd) {
        signed_zero(out);
    } else if (st == CANON_COMPLETE) {
        st = signed_nonzero(s, p1, g, x, &a, remaining(quota, v.nodes), false, out);
    }
    canon_bsgs_free(&a);
    return st;
}

canon_status canon_obj_signed(canon_obj_search *s, canon_p1_search *p1, const canon_group *g,
                              const canon_root *x, uint64_t quota, canon_obj_outcome *out)
{
    canon_status st = start_p1_run(s, g, x, out);
    if (st == CANON_COMPLETE && g->signs == NULL) {
        st = CANON_UNSUPPORTED_ACTION; /* module contract: chi needs a signed group */
    }
    if (st == CANON_COMPLETE) {
        st = signed_image(s, p1, g, x, quota, out);
    }
    canon_root_image_clear(&s->image); /* objectives.h invariant */
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

/* *match = (the CDAG-2 stream of x^w equals c), w a permutation of {0..n-1} (spec 17 "exact
 * action": the image is re-acted and re-encoded, never trusted). */
static canon_status action_matches(const canon_root *x, const uint32_t *w, const uint8_t *c,
                                   size_t c_len, bool *match)
{
    *match = false;
    canon_root_image img;
    canon_root_image_init(&img);
    canon_buf stream;
    canon_buf_init(&stream);
    canon_status st = canon_root_act_into(x, w, &img); /* x^w */
    if (st == CANON_COMPLETE) {
        st = canon_root_stream_write(&img.root, &stream);
    }
    if (st == CANON_COMPLETE) {
        *match = canon_bytes_compare(stream.data, stream.len, c, c_len) == 0;
    }
    canon_root_image_clear(&img);
    canon_root_image_free(&img);
    canon_buf_free(&stream);
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
    bool ok = false;
    canon_status st = bijection(w, n, &ok);
    if (st != CANON_COMPLETE || !ok) {
        return st;
    }
    /* spec 17: "result_verify_witness checks membership and exact action, not canonicity" */
    bool member = false;
    st = g->ops->contains(g, w, &member);
    if (st != CANON_COMPLETE || !member) {
        return st;
    }
    return action_matches(x, w, c, c_len, valid);
}

canon_status canon_obj_check_labeling(const canon_group *g, const canon_root *x,
                                      const uint32_t *rho, const uint32_t *t,
                                      const uint32_t *lambda, const uint8_t *c, size_t c_len,
                                      bool *valid)
{
    *valid = false;
    const uint32_t n = x->n;
    if (g->degree != n) {
        return CANON_INVALID_INPUT;
    }
    bool ok_rho = false, ok_t = false, ok_lambda = false;
    canon_status st = bijection(rho, n, &ok_rho);
    if (st == CANON_COMPLETE) {
        st = bijection(t, n, &ok_t);
    }
    if (st == CANON_COMPLETE) {
        st = bijection(lambda, n, &ok_lambda);
    }
    if (st != CANON_COMPLETE || !ok_rho || !ok_t || !ok_lambda) {
        return st;
    }
    uint32_t *tmp = canon_alloc_array((size_t)n, 2u * sizeof *tmp, &st);
    if (tmp == NULL) {
        return st;
    }
    uint32_t *rho_t = tmp, *k = tmp + n; /* n entries each */
    /* spec 3.1: "return lambda = rho t" (rho acts first, spec 3) */
    canon_perm_compose(rho, t, rho_t, n);
    bool member = false;
    if (canon_perm_lex_compare(rho_t, lambda, n) == 0) {
        /* spec 3.1 "Since t = rho^-1 g rho, lambda = g rho in Lambda": g = lambda rho^-1
         * (lambda first, then rho^-1: g[w] = rho^-1[lambda[w]]), which is rho t rho^-1, must
         * be in G.  k first holds rho^-1, then g. */
        canon_perm_inverse(rho, k, n);
        for (uint32_t w = 0; w < n; ++w) {
            rho_t[w] = k[lambda[w]]; /* rho_t is free again: it now holds g */
        }
        st = g->ops->contains(g, rho_t, &member);
    }
    free(tmp);
    if (st != CANON_COMPLETE || !member) {
        return st;
    }
    /* spec 3.1: c = (x^rho)^t = x^lambda */
    return action_matches(x, lambda, c, c_len, valid);
}

canon_status canon_obj_check_signed(const canon_group *g, const canon_root *x, const uint32_t *w,
                                    int sign, const uint8_t *c, size_t c_len, bool *valid)
{
    *valid = false;
    if (g->degree != x->n || sign < -1 || sign > 1) {
        return CANON_INVALID_INPUT;
    }
    if (g->signs == NULL) {
        return CANON_UNSUPPORTED_ACTION; /* no character */
    }
    canon_buf own;
    canon_buf_init(&own);
    canon_status st = CANON_COMPLETE;
    if (sign == 0) {
        /* spec 8.4 zero certificate: "One checked membership/action/sign witness": x^a = x */
        st = canon_root_stream_write(x, &own);
        c = own.data;
        c_len = own.len;
    }
    bool acts = false;
    if (st == CANON_COMPLETE) {
        st = canon_obj_check_witness(g, x, w, c, c_len, &acts); /* bijection, member, x^w = c */
    }
    canon_buf_free(&own);
    if (st != CANON_COMPLETE || !acts) {
        return st;
    }
    int chi = 0;
    st = g->ops->character(g, w, NULL, &chi);
    /* spec 8.4: a zero needs chi(a) = -1; a nonzero [x] = s [c] needs s = chi(t) */
    *valid = st == CANON_COMPLETE && chi == (sign == 0 ? -1 : sign);
    return st;
}
