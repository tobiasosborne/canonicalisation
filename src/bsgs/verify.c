/* Independent verifier of a stabiliser chain (spec 9.1 third paragraph; docs/slices/S3.md 2.3).
 *
 * spec 9.1: "The independent verifier checks input bijections, each generator's derivation
 * from the original input, base-prefix fixation, orbit reachability via stored tree edges,
 * orbit closure under level generators, transversal images, all Schreier residues' membership
 * in the certified next subgroup, input-generator membership at the root, and terminal
 * triviality.  Induction gives both inclusions at every level and completeness."
 *
 * This file uses only the chain data layout (chain.h), perm.h and provenance.h.  Its
 * transporter reconstruction and its sift are its own (written before chain.c, without
 * reading it), so a side error in the constructor's products cannot be mirrored here.
 *
 * Nesting.  The listed conditions alone do not give the induction: they accept, for Sym(3)
 * from (0 1) and (1 2), a chain with S_0 = {(0 1)} (base 0, orbit {0,1}) and S_1 = {(1 2)}
 * (base 1, orbit {1,2}), whose order 4 is wrong (docs/slices/S3-notes.md).  The induction step
 * K_(i+1) <= Stab_(K_i)(b_i) needs K_(i+1) <= K_i, which the "certified next subgroup" of the
 * spec presupposes; it is checked here as S_(i+1) contained in S_i (CANON_BSGS_NOT_NESTED).
 *
 * Proof sketch (both inclusions).  Let K_i = <S_i>, K_depth = 1 (terminal triviality).  By
 * reachability through level generators and closure, orbit_i = b_i^(K_i).  The reconstructed
 * t_b lie in K_i and send b_i to b (transversal images), with t_(b_i) = 1.  Schreier's lemma
 * gives Stab_(K_i)(b_i) = <t_b s t_(b^s)^-1>, and every such residue sifts to the identity
 * through levels > i, so it lies in K_(i+1) by induction from the bottom (the sift writes it
 * as a product of transversal elements of those levels).  Conversely K_(i+1) <= K_i
 * (nesting) and K_(i+1) fixes b_i (prefix fixation), so K_(i+1) = Stab_(K_i)(b_i) and
 * |K_i| = |orbit_i| |K_(i+1)|.  Every stored generator is derived from the inputs, so
 * K_0 <= <inputs> = G; every input sifts to the identity, so G <= K_0.  Hence K_0 = G and
 * |G| is the product of the orbit lengths. */
#include "bsgs/verify.h"

#include <stdlib.h>
#include <string.h>

#include "arena/alloc.h"
#include "arena/checked.h"
#include "bsgs/provenance.h"
#include "perm/perm.h"

typedef struct vscratch {
    uint32_t *mark;   /* n stamps */
    uint32_t stamp;   /* current stamp; mark[v] == stamp means "marked now" */
    uint32_t *t;      /* n: dense transporter */
    uint32_t *tmp;    /* n */
    uint32_t *g;      /* n: residue under sift */
    uint32_t *path;   /* n: generator ids or orbit positions along a tree path */
    uint32_t *gmark;  /* one stamp per strong generator (nesting) */
    uint64_t *bitmap; /* (n + 63) / 64 words (bijection checks) */
    uint32_t *values; /* prov.count * n: every provenance node evaluated */
} vscratch;

static void scratch_free(vscratch *s)
{
    free(s->mark);
    free(s->t);
    free(s->tmp);
    free(s->g);
    free(s->path);
    free(s->gmark);
    free(s->bitmap);
    free(s->values);
}

/* A fresh stamp: everything marked before is now unmarked. */
static uint32_t next_stamp(vscratch *s, uint32_t n)
{
    if (s->stamp == UINT32_MAX) {
        for (uint32_t v = 0; v < n; ++v) {
            s->mark[v] = 0;
        }
        s->stamp = 0;
    }
    return ++s->stamp;
}

static const uint32_t *gen_row(const canon_bsgs *c, uint32_t id)
{
    return canon_perm_table_row(&c->gens, id);
}

static const uint32_t *inv_row(const canon_bsgs *c, uint32_t id)
{
    return canon_perm_table_row(&c->invs, id);
}

/* ---- structure: everything later reads only in-range indices ---- */

static bool structure_ok(const canon_bsgs *c, vscratch *s)
{
    const uint32_t n = c->n;
    if (c->gens.n != n || c->invs.n != n || c->inputs.n != n ||
        c->gens.count != c->invs.count || c->levels == NULL || c->depth >= c->level_cap) {
        return false;
    }
    if (n > 0 && ((c->gens.count > 0 && (c->gens.data == NULL || c->invs.data == NULL)) ||
                  (c->inputs.count > 0 && c->inputs.data == NULL))) {
        return false;
    }
    if (c->gens.count > 0 && (c->gen_node == NULL || c->inv_node == NULL)) {
        return false;
    }
    if (c->prov.count > 0 && c->prov.nodes == NULL) {
        return false;
    }
    for (uint32_t k = 0; k < c->gens.count; ++k) {
        if (c->gen_node[k] >= c->prov.count || c->inv_node[k] >= c->prov.count) {
            return false;
        }
    }
    uint32_t st = next_stamp(s, n);
    for (uint32_t i = 0; i < c->depth; ++i) {
        const canon_bsgs_level *L = &c->levels[i];
        if (L->base_point >= n || s->mark[L->base_point] == st) {
            return false; /* base points are distinct points of the domain */
        }
        s->mark[L->base_point] = st;
        if (L->orbit_len < 1 || L->orbit_len > n || L->orbit == NULL || L->orbit_pos == NULL ||
            L->parent_point == NULL || L->parent_gen == NULL ||
            (L->gen_count > 0 && L->gen_ids == NULL)) {
            return false;
        }
        for (uint32_t k = 0; k < L->gen_count; ++k) {
            if (L->gen_ids[k] >= c->gens.count) {
                return false;
            }
        }
        for (uint32_t p = 0; p < L->orbit_len; ++p) {
            if (L->orbit[p] >= n) {
                return false;
            }
        }
    }
    return true;
}

/* ---- transporters and sifting, independent of chain.c ---- */

/* Collect, walking from orbit position `pos` up to the root, the generator ids of the edges
 * in upward order: path[0] is the edge into the point, path[k-1] the edge out of the base
 * point.  Requires verified reachability.  Returns k. */
static uint32_t upward_path(const canon_bsgs_level *L, uint32_t pos, uint32_t *path)
{
    uint32_t k = 0;
    while (pos != 0) {
        path[k++] = L->gen_ids[L->parent_gen[pos]];
        pos = L->orbit_pos[L->parent_point[pos]];
    }
    return k;
}

/* Dense t_b for the point at orbit position pos: if the tree path from the base point is
 * b_0 -> b_1 -> ... -> b with b_(j+1) = b_j^(s_j), then t_b = s_0 s_1 ... s_(k-1), multiplied
 * left to right (spec 3: (pq)[v] = q[p[v]]).  The upward path lists s_(k-1), ..., s_0, so it is
 * read backwards, each factor applied on the right: t <- t s. */
static void transporter(const canon_bsgs *c, const canon_bsgs_level *L, uint32_t pos,
                        vscratch *s)
{
    const uint32_t n = c->n;
    uint32_t k = upward_path(L, pos, s->path);
    for (uint32_t v = 0; v < n; ++v) {
        s->t[v] = v;
    }
    for (uint32_t j = k; j-- > 0;) {
        canon_perm_compose(s->t, gen_row(c, s->path[j]), s->tmp, n); /* tmp = t s */
        uint32_t *swap = s->t;
        s->t = s->tmp;
        s->tmp = swap;
    }
}

/* g <- g t_x^-1 where x is the point at orbit position pos.  t_x^-1 = s_(k-1)^-1 ... s_0^-1,
 * and the upward walk meets s_(k-1) first, so each inverse is applied on the right in walk
 * order: g'[v] = s^-1[g[v]]. */
static void times_transporter_inverse(const canon_bsgs *c, const canon_bsgs_level *L,
                                      uint32_t pos, uint32_t *g)
{
    const uint32_t n = c->n;
    while (pos != 0) {
        const uint32_t *inv = inv_row(c, L->gen_ids[L->parent_gen[pos]]);
        for (uint32_t v = 0; v < n; ++v) {
            g[v] = inv[g[v]];
        }
        pos = L->orbit_pos[L->parent_point[pos]];
    }
}

/* spec 9.2 membership sift through levels from..depth-1; true iff the residue is the
 * identity.  g is overwritten. */
static bool sifts_to_identity(const canon_bsgs *c, uint32_t from, uint32_t *g)
{
    for (uint32_t j = from; j < c->depth; ++j) {
        const canon_bsgs_level *L = &c->levels[j];
        uint32_t pos = L->orbit_pos[g[L->base_point]];
        if (pos == CANON_BSGS_NONE) {
            return false;
        }
        times_transporter_inverse(c, L, pos, g); /* now g fixes b_j */
    }
    return canon_perm_is_identity(g, c->n);
}

/* ---- the conditions ---- */

/* 1: input bijections (spec 9.1), and every stored generator and inverse. */
static bool bijections(const canon_bsgs *c, const uint32_t *inputs, uint32_t input_count,
                       vscratch *s)
{
    const uint32_t n = c->n;
    if (n == 0) {
        return true;
    }
    for (uint32_t i = 0; i < input_count; ++i) {
        if (!canon_perm_validate_scratch(inputs + (size_t)i * n, n, s->bitmap)) {
            return false;
        }
    }
    for (uint32_t i = 0; i < c->inputs.count; ++i) {
        if (!canon_perm_validate_scratch(canon_perm_table_row(&c->inputs, i), n, s->bitmap)) {
            return false;
        }
    }
    for (uint32_t k = 0; k < c->gens.count; ++k) {
        if (!canon_perm_validate_scratch(gen_row(c, k), n, s->bitmap) ||
            !canon_perm_validate_scratch(inv_row(c, k), n, s->bitmap)) {
            return false;
        }
    }
    return true;
}

/* 2a: the recorded inputs are exactly the original validated inputs. */
static bool inputs_match(const canon_bsgs *c, const uint32_t *inputs, uint32_t input_count)
{
    const uint32_t n = c->n;
    if (c->inputs.count != input_count) {
        return false;
    }
    return n == 0 || input_count == 0 ||
           memcmp(c->inputs.data, inputs, (size_t)input_count * n * sizeof *inputs) == 0;
}

/* 2b: every stored generator and inverse equals the array re-derived from its record, down to
 * INPUT entries (evaluated from the recorded inputs, which 2a proved equal to the originals). */
static bool provenance_matches(const canon_bsgs *c, vscratch *s)
{
    const uint32_t n = c->n;
    if (canon_prov_eval_all(&c->prov, &c->inputs, s->values) != CANON_COMPLETE) {
        return false; /* malformed record */
    }
    for (uint32_t k = 0; k < c->gens.count; ++k) {
        if (n > 0 && (memcmp(gen_row(c, k), s->values + (size_t)c->gen_node[k] * n,
                             (size_t)n * sizeof(uint32_t)) != 0 ||
                      memcmp(inv_row(c, k), s->values + (size_t)c->inv_node[k] * n,
                             (size_t)n * sizeof(uint32_t)) != 0)) {
            return false;
        }
    }
    return true;
}

/* 2c: invs[k] is the inverse of gens[k] (sifting relies on it). */
static bool inverses_match(const canon_bsgs *c, vscratch *s)
{
    for (uint32_t k = 0; k < c->gens.count; ++k) {
        canon_perm_compose(gen_row(c, k), inv_row(c, k), s->tmp, c->n);
        if (!canon_perm_is_identity(s->tmp, c->n)) {
            return false;
        }
    }
    return true;
}

/* 3: level-i generators fix b_0 ... b_(i-1) pointwise. */
static bool prefix_fixed(const canon_bsgs *c)
{
    for (uint32_t i = 0; i < c->depth; ++i) {
        const canon_bsgs_level *L = &c->levels[i];
        for (uint32_t k = 0; k < L->gen_count; ++k) {
            const uint32_t *g = gen_row(c, L->gen_ids[k]);
            for (uint32_t j = 0; j < i; ++j) {
                uint32_t b = c->levels[j].base_point;
                if (g[b] != b) {
                    return false;
                }
            }
        }
    }
    return true;
}

/* S_(i+1) is contained in S_i (see the file comment). */
static bool nested(const canon_bsgs *c, vscratch *s)
{
    for (uint32_t i = 0; i + 1 < c->depth; ++i) {
        const canon_bsgs_level *L = &c->levels[i], *M = &c->levels[i + 1];
        for (uint32_t k = 0; k < L->gen_count; ++k) {
            s->gmark[L->gen_ids[k]] = i + 1;
        }
        for (uint32_t k = 0; k < M->gen_count; ++k) {
            if (s->gmark[M->gen_ids[k]] != i + 1) {
                return false;
            }
        }
    }
    return true;
}

/* 4: the orbit of every level is a tree of stored edges rooted at the base point. */
static canon_bsgs_reason orbits_reachable(const canon_bsgs *c, vscratch *s)
{
    const uint32_t n = c->n;
    for (uint32_t i = 0; i < c->depth; ++i) {
        const canon_bsgs_level *L = &c->levels[i];
        const uint32_t len = L->orbit_len;
        if (L->orbit[0] != L->base_point || L->parent_point[0] != CANON_BSGS_NONE ||
            L->parent_gen[0] != CANON_BSGS_NONE) {
            return CANON_BSGS_ORBIT_ROOT;
        }
        uint32_t st = next_stamp(s, n);
        for (uint32_t p = 0; p < len; ++p) {
            if (s->mark[L->orbit[p]] == st) {
                return CANON_BSGS_ORBIT_DUPLICATE;
            }
            s->mark[L->orbit[p]] = st;
        }
        /* orbit_pos inverts orbit and marks nothing else */
        for (uint32_t p = 0; p < len; ++p) {
            if (L->orbit_pos[L->orbit[p]] != p) {
                return CANON_BSGS_ORBIT_POS_MISMATCH;
            }
        }
        for (uint32_t v = 0; v < n; ++v) {
            uint32_t q = L->orbit_pos[v];
            if (q != CANON_BSGS_NONE && (q >= len || L->orbit[q] != v)) {
                return CANON_BSGS_ORBIT_POS_MISMATCH;
            }
        }
        /* each stored edge: parent in the orbit, a level generator s, parent^s = point */
        for (uint32_t p = 1; p < len; ++p) {
            uint32_t pp = L->parent_point[p], pg = L->parent_gen[p];
            if (pg >= L->gen_count || pp >= n || L->orbit_pos[pp] == CANON_BSGS_NONE ||
                gen_row(c, L->gen_ids[pg])[pp] != L->orbit[p]) {
                return CANON_BSGS_ORBIT_EDGE;
            }
        }
        /* reachability: following parents from every position reaches position 0.  Positions
         * already shown to reach it carry the stamp `good`; a walk longer than len has met a
         * cycle. */
        uint32_t good = next_stamp(s, n);
        s->mark[0] = good; /* mark is indexed by orbit position here (len <= n) */
        for (uint32_t p = 1; p < len; ++p) {
            uint32_t q = p, steps = 0;
            while (s->mark[q] != good) {
                if (steps == len) {
                    return CANON_BSGS_ORBIT_UNREACHABLE;
                }
                s->path[steps++] = q;
                q = L->orbit_pos[L->parent_point[q]];
            }
            for (uint32_t j = 0; j < steps; ++j) {
                s->mark[s->path[j]] = good;
            }
        }
    }
    return CANON_BSGS_VALID;
}

/* 5: every orbit is closed under its level's generators. */
static bool orbits_closed(const canon_bsgs *c)
{
    for (uint32_t i = 0; i < c->depth; ++i) {
        const canon_bsgs_level *L = &c->levels[i];
        for (uint32_t k = 0; k < L->gen_count; ++k) {
            const uint32_t *g = gen_row(c, L->gen_ids[k]);
            for (uint32_t p = 0; p < L->orbit_len; ++p) {
                if (L->orbit_pos[g[L->orbit[p]]] == CANON_BSGS_NONE) {
                    return false;
                }
            }
        }
    }
    return true;
}

/* 6: the reconstructed transporter t_b sends the base point to b, for every b. */
static bool transversal_images(const canon_bsgs *c, vscratch *s)
{
    for (uint32_t i = 0; i < c->depth; ++i) {
        const canon_bsgs_level *L = &c->levels[i];
        for (uint32_t p = 0; p < L->orbit_len; ++p) {
            transporter(c, L, p, s);
            if (s->t[L->base_point] != L->orbit[p]) {
                return false;
            }
        }
    }
    return true;
}

/* 7: every Schreier residue t_b s t_(b^s)^-1 of level i sifts to the identity through the
 * levels below i (the certified next subgroup). */
static bool schreier_residues(const canon_bsgs *c, vscratch *s)
{
    const uint32_t n = c->n;
    for (uint32_t i = 0; i < c->depth; ++i) {
        const canon_bsgs_level *L = &c->levels[i];
        for (uint32_t p = 0; p < L->orbit_len; ++p) {
            transporter(c, L, p, s); /* s->t = t_b, b = orbit[p] */
            for (uint32_t k = 0; k < L->gen_count; ++k) {
                const uint32_t *gen = gen_row(c, L->gen_ids[k]);
                uint32_t bs = gen[L->orbit[p]]; /* b^s */
                canon_perm_compose(s->t, gen, s->g, n); /* g = t_b s */
                times_transporter_inverse(c, L, L->orbit_pos[bs], s->g); /* g t_(b^s)^-1 */
                if (s->g[L->base_point] != L->base_point ||
                    !sifts_to_identity(c, i + 1, s->g)) {
                    return false;
                }
            }
        }
    }
    return true;
}

/* 8: every original input generator sifts to the identity from the root. */
static bool inputs_member(const canon_bsgs *c, const uint32_t *inputs, uint32_t input_count,
                          vscratch *s)
{
    const uint32_t n = c->n;
    for (uint32_t i = 0; i < input_count; ++i) {
        if (n > 0) {
            memcpy(s->g, inputs + (size_t)i * n, (size_t)n * sizeof *s->g);
        }
        if (!sifts_to_identity(c, 0, s->g)) {
            return false;
        }
    }
    return true;
}

/* 10: order is the exact product of the orbit lengths (a product beyond uint64 cannot be
 * stored, so it is a mismatch; such chains are refused at construction). */
static bool order_matches(const canon_bsgs *c)
{
    uint64_t prod = 1;
    for (uint32_t i = 0; i < c->depth; ++i) {
        if (!canon_u64_mul(prod, c->levels[i].orbit_len, &prod)) {
            return false;
        }
    }
    return prod == c->order;
}

static canon_bsgs_reason run(canon_bsgs *c, const uint32_t *inputs, uint32_t input_count,
                             vscratch *s)
{
    if (!structure_ok(c, s)) {
        return CANON_BSGS_BAD_STRUCTURE;
    }
    if (c->levels[c->depth].gen_count != 0) {
        return CANON_BSGS_LAST_NOT_TRIVIAL; /* 9 */
    }
    if (!bijections(c, inputs, input_count, s)) {
        return CANON_BSGS_NOT_BIJECTION; /* 1 */
    }
    if (!inputs_match(c, inputs, input_count)) {
        return CANON_BSGS_INPUT_MISMATCH; /* 2 */
    }
    if (!provenance_matches(c, s)) {
        return CANON_BSGS_PROVENANCE_MISMATCH; /* 2 */
    }
    if (!inverses_match(c, s)) {
        return CANON_BSGS_INVERSE_MISMATCH; /* 2 */
    }
    if (!prefix_fixed(c)) {
        return CANON_BSGS_PREFIX_NOT_FIXED; /* 3 */
    }
    if (!nested(c, s)) {
        return CANON_BSGS_NOT_NESTED;
    }
    canon_bsgs_reason r = orbits_reachable(c, s); /* 4 */
    if (r != CANON_BSGS_VALID) {
        return r;
    }
    if (!orbits_closed(c)) {
        return CANON_BSGS_ORBIT_NOT_CLOSED; /* 5 */
    }
    if (!transversal_images(c, s)) {
        return CANON_BSGS_TRANSVERSAL_IMAGE; /* 6 */
    }
    if (!schreier_residues(c, s)) {
        return CANON_BSGS_SCHREIER_RESIDUE; /* 7 */
    }
    if (!inputs_member(c, inputs, input_count, s)) {
        return CANON_BSGS_INPUT_NOT_MEMBER; /* 8 */
    }
    if (!order_matches(c)) {
        return CANON_BSGS_ORDER_MISMATCH; /* 10 */
    }
    return CANON_BSGS_VALID;
}

canon_status canon_bsgs_verify(canon_bsgs *c, const uint32_t *inputs, uint32_t input_count,
                               canon_bsgs_reason *reason)
{
    c->verified = false;
    *reason = CANON_BSGS_UNCHECKED;
    const uint32_t n = c->n;
    canon_status st = CANON_COMPLETE;
    vscratch s;
    memset(&s, 0, sizeof s);
    size_t values = 0;
    if (!canon_size_mul((size_t)c->prov.count, (size_t)n, &values)) {
        return CANON_CAPACITY_LIMIT; /* spec 11.1 */
    }
    s.mark = canon_alloc_array(n, sizeof *s.mark, &st);
    s.t = canon_alloc_array(n, sizeof *s.t, &st);
    s.tmp = canon_alloc_array(n, sizeof *s.tmp, &st);
    s.g = canon_alloc_array(n, sizeof *s.g, &st);
    s.path = canon_alloc_array(n, sizeof *s.path, &st);
    s.gmark = canon_alloc_array(c->gens.count, sizeof *s.gmark, &st);
    s.bitmap = canon_alloc_array(((size_t)n + 63u) / 64u, sizeof *s.bitmap, &st);
    s.values = canon_alloc_array(values, sizeof *s.values, &st);
    if (s.mark == NULL || s.t == NULL || s.tmp == NULL || s.g == NULL || s.path == NULL ||
        s.gmark == NULL || s.bitmap == NULL || s.values == NULL) {
        scratch_free(&s);
        return st;
    }
    for (uint32_t v = 0; v < n; ++v) {
        s.mark[v] = 0;
    }
    for (uint32_t k = 0; k < c->gens.count; ++k) {
        s.gmark[k] = 0;
    }
    *reason = run(c, inputs, input_count, &s);
    c->verified = *reason == CANON_BSGS_VALID;
    scratch_free(&s);
    return CANON_COMPLETE;
}

const char *canon_bsgs_reason_name(canon_bsgs_reason reason)
{
    switch (reason) {
    case CANON_BSGS_VALID:
        return "VALID";
    case CANON_BSGS_UNCHECKED:
        return "UNCHECKED";
    case CANON_BSGS_BAD_STRUCTURE:
        return "BAD_STRUCTURE";
    case CANON_BSGS_LAST_NOT_TRIVIAL:
        return "LAST_NOT_TRIVIAL";
    case CANON_BSGS_NOT_BIJECTION:
        return "NOT_BIJECTION";
    case CANON_BSGS_INPUT_MISMATCH:
        return "INPUT_MISMATCH";
    case CANON_BSGS_PROVENANCE_MISMATCH:
        return "PROVENANCE_MISMATCH";
    case CANON_BSGS_INVERSE_MISMATCH:
        return "INVERSE_MISMATCH";
    case CANON_BSGS_PREFIX_NOT_FIXED:
        return "PREFIX_NOT_FIXED";
    case CANON_BSGS_NOT_NESTED:
        return "NOT_NESTED";
    case CANON_BSGS_ORBIT_ROOT:
        return "ORBIT_ROOT";
    case CANON_BSGS_ORBIT_DUPLICATE:
        return "ORBIT_DUPLICATE";
    case CANON_BSGS_ORBIT_POS_MISMATCH:
        return "ORBIT_POS_MISMATCH";
    case CANON_BSGS_ORBIT_EDGE:
        return "ORBIT_EDGE";
    case CANON_BSGS_ORBIT_UNREACHABLE:
        return "ORBIT_UNREACHABLE";
    case CANON_BSGS_ORBIT_NOT_CLOSED:
        return "ORBIT_NOT_CLOSED";
    case CANON_BSGS_TRANSVERSAL_IMAGE:
        return "TRANSVERSAL_IMAGE";
    case CANON_BSGS_SCHREIER_RESIDUE:
        return "SCHREIER_RESIDUE";
    case CANON_BSGS_INPUT_NOT_MEMBER:
        return "INPUT_NOT_MEMBER";
    case CANON_BSGS_ORDER_MISMATCH:
        return "ORDER_MISMATCH";
    }
    return "UNKNOWN";
}
