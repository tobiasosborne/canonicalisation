/* Stabiliser chains in canon-check (spec 9.1, 9.2): deserialisation of the CHAIN layout, the
 * independent verifier of spec 9.1's third paragraph plus the nesting condition, membership
 * by sifting, and the orbits of a level's group.  Written from the specification, not from
 * src/bsgs/verify.c (checker/README.md). */
#include <stdlib.h>
#include <string.h>

#include "cc.h"

void cc_chain_init(cc_chain *c)
{
    memset(c, 0, sizeof *c);
}

void cc_chain_free(cc_chain *c)
{
    for (uint32_t i = 0; c->levels != NULL && i < c->depth; ++i) {
        free(c->levels[i].orbit);
        free(c->levels[i].parent_point);
        free(c->levels[i].parent_gen);
        free(c->levels[i].gen_ids);
        free(c->levels[i].pos);
    }
    free(c->levels);
    free(c->gen_node);
    free(c->gens);
    free(c->invs);
    free(c->prov);
    free(c->inputs);
    cc_chain_init(c);
}

static const uint32_t *gen_row(const cc_chain *c, uint32_t id)
{
    return c->gens + (size_t)id * c->n;
}

static const uint32_t *inv_row(const cc_chain *c, uint32_t id)
{
    return c->invs + (size_t)id * c->n;
}

cc_reason cc_chain_read(cc_reader *r, uint32_t n, cc_chain *c)
{
    cc_chain_init(c);
    cc_reason why = CC_ACCEPT;
    uint32_t cn = 0;
    if (!cc_get_u32(r, &cn) || !cc_get_u32(r, &c->depth)) {
        return CC_TRUNCATED;
    }
    if (cn != n) {
        return CC_CHAIN_STRUCTURE;
    }
    c->n = n;
    if (c->depth > n) {
        return CC_CHAIN_STRUCTURE; /* base points are distinct points of the domain */
    }
    c->levels = cc_alloc(c->depth, sizeof *c->levels, &why);
    if (c->levels == NULL) {
        return why;
    }
    memset(c->levels, 0, (c->depth > 0 ? c->depth : 1u) * sizeof *c->levels);
    for (uint32_t i = 0; i < c->depth; ++i) {
        cc_level *L = &c->levels[i];
        if (!cc_get_u32(r, &L->base) || !cc_get_u32(r, &L->r)) {
            return CC_TRUNCATED;
        }
        if (L->r > n) {
            return CC_CHAIN_STRUCTURE;
        }
        L->orbit = cc_get_u32s(r, L->r, &why);
        L->parent_point = L->orbit != NULL ? cc_get_u32s(r, L->r, &why) : NULL;
        L->parent_gen = L->parent_point != NULL ? cc_get_u32s(r, L->r, &why) : NULL;
        if (L->parent_gen == NULL) {
            return why;
        }
        if (!cc_get_u32(r, &L->s)) {
            return CC_TRUNCATED;
        }
        L->gen_ids = cc_get_u32s(r, L->s, &why);
        L->pos = L->gen_ids != NULL ? cc_alloc(n, sizeof *L->pos, &why) : NULL;
        if (L->pos == NULL) {
            return why;
        }
    }
    if (!cc_get_u32(r, &c->g)) {
        return CC_TRUNCATED;
    }
    c->gen_node = cc_alloc(c->g, sizeof *c->gen_node, &why);
    if (c->gen_node == NULL) {
        return why;
    }
    if ((size_t)c->g > SIZE_MAX / 4u / (n > 0 ? n : 1u)) {
        return CC_CAPACITY;
    }
    c->gens = cc_alloc((size_t)c->g * n, sizeof *c->gens, &why);
    c->invs = c->gens != NULL ? cc_alloc((size_t)c->g * n, sizeof *c->invs, &why) : NULL;
    if (c->invs == NULL) {
        return why;
    }
    for (uint32_t k = 0; k < c->g; ++k) {
        if (!cc_get_u32(r, &c->gen_node[k])) {
            return CC_TRUNCATED;
        }
        if (r->len - r->at < 4u * (size_t)n) {
            return CC_TRUNCATED;
        }
        for (uint32_t v = 0; v < n; ++v) {
            (void)cc_get_u32(r, &c->gens[(size_t)k * n + v]);
        }
    }
    if (!cc_get_u32(r, &c->p)) {
        return CC_TRUNCATED;
    }
    if (c->p > UINT32_MAX / 3u) {
        return CC_CAPACITY;
    }
    c->prov = cc_get_u32s(r, c->p * 3u, &why);
    if (c->prov == NULL) {
        return why;
    }
    if (!cc_get_u32(r, &c->i)) {
        return CC_TRUNCATED;
    }
    if ((size_t)c->i > SIZE_MAX / 4u / (n > 0 ? n : 1u)) {
        return CC_CAPACITY;
    }
    c->inputs = cc_alloc((size_t)c->i * n, sizeof *c->inputs, &why);
    if (c->inputs == NULL) {
        return why;
    }
    if (r->len - r->at < 4u * (size_t)n * c->i) {
        return CC_TRUNCATED;
    }
    for (size_t j = 0; j < (size_t)c->i * n; ++j) {
        (void)cc_get_u32(r, &c->inputs[j]);
    }
    return CC_ACCEPT;
}

/* ---- transporters and sifting ---- */

/* t_b for orbit position pos of level L: the product s_1 ... s_k of the generators along the
 * tree path base -> ... -> b, s_1 first (spec 3), so t[v] = s_k[... s_1[v]].  path: n words of
 * scratch. */
static void transporter(const cc_chain *c, const cc_level *L, uint32_t pos, uint32_t *t,
                        uint32_t *path)
{
    uint32_t k = 0;
    while (pos != 0) {
        path[k++] = L->gen_ids[L->parent_gen[pos]];
        pos = L->pos[L->parent_point[pos]];
    }
    for (uint32_t v = 0; v < c->n; ++v) {
        t[v] = v;
    }
    while (k-- > 0) {
        const uint32_t *s = gen_row(c, path[k]);
        for (uint32_t v = 0; v < c->n; ++v) {
            t[v] = s[t[v]]; /* t <- t s */
        }
    }
}

/* g <- g t_b^-1 for the point b at orbit position pos: t_b^-1 = s_k^-1 ... s_1^-1, applied on
 * the right in that order, which is the upward walk's order. */
static void times_transporter_inverse(const cc_chain *c, const cc_level *L, uint32_t pos,
                                      uint32_t *g)
{
    while (pos != 0) {
        const uint32_t *inv = inv_row(c, L->gen_ids[L->parent_gen[pos]]);
        for (uint32_t v = 0; v < c->n; ++v) {
            g[v] = inv[g[v]];
        }
        pos = L->pos[L->parent_point[pos]];
    }
}

/* spec 9.2 "Membership sifts g by repeatedly mapping the base image back with the
 * corresponding transversal inverse and checking terminal identity", from level `from`. */
static bool sifts_to_identity(const cc_chain *c, uint32_t from, uint32_t *g)
{
    for (uint32_t j = from; j < c->depth; ++j) {
        const cc_level *L = &c->levels[j];
        const uint32_t pos = L->pos[g[L->base]];
        if (pos == CC_NONE) {
            return false;
        }
        times_transporter_inverse(c, L, pos, g);
    }
    return cc_is_identity(g, c->n);
}

bool cc_chain_contains(const cc_chain *c, const uint32_t *p, uint32_t *scratch)
{
    if (c->n > 0) {
        memcpy(scratch, p, (size_t)c->n * sizeof *scratch);
    }
    return sifts_to_identity(c, 0, scratch);
}

/* ---- the verifier (spec 9.1 third paragraph; checker/README.md lists the conditions) ---- */

typedef struct vs {
    uint32_t *t, *tmp, *g, *path, *mark, *gmark;
    uint64_t *bitmap;
    uint32_t *rows; /* p x n: every provenance node evaluated */
} vs;

static void vs_free(vs *s)
{
    free(s->t);
    free(s->tmp);
    free(s->g);
    free(s->path);
    free(s->mark);
    free(s->gmark);
    free(s->bitmap);
    free(s->rows);
}

/* Structure: every index read later is in range. */
static cc_reason structure(const cc_chain *c, vs *s)
{
    const uint32_t n = c->n;
    for (uint32_t v = 0; v < n; ++v) {
        s->mark[v] = 0;
    }
    for (uint32_t i = 0; i < c->depth; ++i) {
        const cc_level *L = &c->levels[i];
        if (L->base >= n || s->mark[L->base] != 0 || L->r < 1) {
            return CC_CHAIN_STRUCTURE;
        }
        s->mark[L->base] = 1;
        for (uint32_t k = 0; k < L->s; ++k) {
            if (L->gen_ids[k] >= c->g) {
                return CC_CHAIN_STRUCTURE;
            }
        }
        for (uint32_t q = 0; q < L->r; ++q) {
            if (L->orbit[q] >= n) {
                return CC_CHAIN_STRUCTURE;
            }
        }
    }
    for (uint32_t k = 0; k < c->g; ++k) {
        if (c->gen_node[k] >= c->p) {
            return CC_CHAIN_STRUCTURE;
        }
    }
    return CC_ACCEPT;
}

/* 1: inputs and generators are bijections of the domain (spec 9.1 "input bijections"). */
static cc_reason bijections(const cc_chain *c, vs *s)
{
    for (uint32_t k = 0; k < c->i; ++k) {
        if (!cc_is_bijection(c->inputs + (size_t)k * c->n, c->n, s->bitmap)) {
            return CC_CHAIN_NOT_BIJECTION;
        }
    }
    for (uint32_t k = 0; k < c->g; ++k) {
        if (!cc_is_bijection(gen_row(c, k), c->n, s->bitmap)) {
            return CC_CHAIN_NOT_BIJECTION;
        }
    }
    return CC_ACCEPT;
}

/* 2: "each generator's derivation from the original input": every provenance node is
 * evaluated in order (operands refer to earlier nodes), and generator k must equal the array
 * of node gen_node[k].  Kinds: 1 INPUT a, 2 INVERSE a, 3 PRODUCT a b (a acts first). */
static cc_reason provenance(const cc_chain *c, vs *s)
{
    const uint32_t n = c->n;
    for (uint32_t j = 0; j < c->p; ++j) {
        const uint32_t kind = c->prov[3u * j], a = c->prov[3u * j + 1u], b = c->prov[3u * j + 2u];
        uint32_t *row = s->rows + (size_t)j * n;
        if (kind == 1) {
            if (a >= c->i) {
                return CC_CHAIN_PROVENANCE;
            }
            if (n > 0) {
                memcpy(row, c->inputs + (size_t)a * n, (size_t)n * sizeof *row);
            }
        } else if (kind == 2) {
            if (a >= j) {
                return CC_CHAIN_PROVENANCE;
            }
            cc_inverse(s->rows + (size_t)a * n, row, n);
        } else if (kind == 3) {
            if (a >= j || b >= j) {
                return CC_CHAIN_PROVENANCE;
            }
            cc_compose(s->rows + (size_t)a * n, s->rows + (size_t)b * n, row, n);
        } else {
            return CC_CHAIN_PROVENANCE;
        }
    }
    for (uint32_t k = 0; k < c->g; ++k) {
        if (n > 0 && memcmp(gen_row(c, k), s->rows + (size_t)c->gen_node[k] * n,
                            (size_t)n * sizeof(uint32_t)) != 0) {
            return CC_CHAIN_PROVENANCE;
        }
    }
    return CC_ACCEPT;
}

/* 3: "base-prefix fixation": level-i generators fix b_0 .. b_(i-1). */
static cc_reason prefix_fixed(const cc_chain *c)
{
    for (uint32_t i = 0; i < c->depth; ++i) {
        const cc_level *L = &c->levels[i];
        for (uint32_t k = 0; k < L->s; ++k) {
            const uint32_t *g = gen_row(c, L->gen_ids[k]);
            for (uint32_t j = 0; j < i; ++j) {
                if (g[c->levels[j].base] != c->levels[j].base) {
                    return CC_CHAIN_PREFIX_NOT_FIXED;
                }
            }
        }
    }
    return CC_ACCEPT;
}

/* Nesting (spec 9.1): the generators of level i + 1 are among those of level i, so that
 * K_(i+1) <= K_i; compared as image arrays. */
static cc_reason nested(const cc_chain *c, vs *s)
{
    for (uint32_t i = 0; i + 1 < c->depth; ++i) {
        const cc_level *L = &c->levels[i], *M = &c->levels[i + 1];
        for (uint32_t k = 0; k < M->s; ++k) {
            const uint32_t *gm = gen_row(c, M->gen_ids[k]);
            bool found = false;
            for (uint32_t l = 0; l < L->s && !found; ++l) {
                found = c->n == 0 || memcmp(gm, gen_row(c, L->gen_ids[l]),
                                            (size_t)c->n * sizeof *gm) == 0;
            }
            if (!found) {
                return CC_CHAIN_NOT_NESTED;
            }
        }
    }
    (void)s;
    return CC_ACCEPT;
}

/* 4: "orbit reachability via stored tree edges": orbit[0] is the base point, points are
 * distinct, pos inverts orbit, every edge is parent^s = point for a level generator s, and
 * every position reaches position 0 through parents. */
static cc_reason orbit_trees(cc_chain *c, vs *s)
{
    const uint32_t n = c->n;
    for (uint32_t i = 0; i < c->depth; ++i) {
        cc_level *L = &c->levels[i];
        if (L->orbit[0] != L->base || L->parent_point[0] != CC_NONE || L->parent_gen[0] != CC_NONE) {
            return CC_CHAIN_ORBIT_TREE;
        }
        for (uint32_t v = 0; v < n; ++v) {
            L->pos[v] = CC_NONE;
        }
        for (uint32_t q = 0; q < L->r; ++q) {
            if (L->pos[L->orbit[q]] != CC_NONE) {
                return CC_CHAIN_ORBIT_TREE; /* duplicate */
            }
            L->pos[L->orbit[q]] = q;
        }
        for (uint32_t q = 1; q < L->r; ++q) {
            const uint32_t pp = L->parent_point[q], pg = L->parent_gen[q];
            if (pg >= L->s || pp >= n || L->pos[pp] == CC_NONE ||
                gen_row(c, L->gen_ids[pg])[pp] != L->orbit[q]) {
                return CC_CHAIN_ORBIT_TREE;
            }
        }
        /* reachability: walk parents; a walk longer than r has met a cycle */
        for (uint32_t q = 1; q < L->r; ++q) {
            uint32_t at = q, steps = 0;
            while (at != 0) {
                if (steps++ > L->r) {
                    return CC_CHAIN_ORBIT_TREE;
                }
                at = L->pos[L->parent_point[at]];
            }
        }
    }
    (void)s;
    return CC_ACCEPT;
}

/* 5: "orbit closure under level generators". */
static cc_reason orbits_closed(const cc_chain *c)
{
    for (uint32_t i = 0; i < c->depth; ++i) {
        const cc_level *L = &c->levels[i];
        for (uint32_t k = 0; k < L->s; ++k) {
            const uint32_t *g = gen_row(c, L->gen_ids[k]);
            for (uint32_t q = 0; q < L->r; ++q) {
                if (L->pos[g[L->orbit[q]]] == CC_NONE) {
                    return CC_CHAIN_ORBIT_NOT_CLOSED;
                }
            }
        }
    }
    return CC_ACCEPT;
}

/* 6: "transversal images": the reconstructed t_b sends the base point to b. */
static cc_reason transversals(const cc_chain *c, vs *s)
{
    for (uint32_t i = 0; i < c->depth; ++i) {
        const cc_level *L = &c->levels[i];
        for (uint32_t q = 0; q < L->r; ++q) {
            transporter(c, L, q, s->t, s->path);
            if (s->t[L->base] != L->orbit[q]) {
                return CC_CHAIN_TRANSVERSAL;
            }
        }
    }
    return CC_ACCEPT;
}

/* 7: "all Schreier residues' membership in the certified next subgroup": t_b s t_(b^s)^-1
 * sifts to the identity through the levels below i. */
static cc_reason schreier(const cc_chain *c, vs *s)
{
    for (uint32_t i = 0; i < c->depth; ++i) {
        const cc_level *L = &c->levels[i];
        for (uint32_t q = 0; q < L->r; ++q) {
            transporter(c, L, q, s->t, s->path); /* t_b */
            for (uint32_t k = 0; k < L->s; ++k) {
                const uint32_t *gen = gen_row(c, L->gen_ids[k]);
                cc_compose(s->t, gen, s->g, c->n);                       /* t_b s */
                times_transporter_inverse(c, L, L->pos[gen[L->orbit[q]]], s->g); /* t_(b^s)^-1 */
                if (s->g[L->base] != L->base || !sifts_to_identity(c, i + 1, s->g)) {
                    return CC_CHAIN_SCHREIER_RESIDUE;
                }
            }
        }
    }
    return CC_ACCEPT;
}

/* 8: "input-generator membership at the root". */
static cc_reason inputs_member(const cc_chain *c, vs *s)
{
    for (uint32_t k = 0; k < c->i; ++k) {
        if (c->n > 0) {
            memcpy(s->g, c->inputs + (size_t)k * c->n, (size_t)c->n * sizeof *s->g);
        }
        if (!sifts_to_identity(c, 0, s->g)) {
            return CC_CHAIN_INPUT_NOT_MEMBER;
        }
    }
    return CC_ACCEPT;
}

cc_reason cc_chain_verify(cc_chain *c)
{
    const uint32_t n = c->n;
    c->verified = false;
    vs s;
    memset(&s, 0, sizeof s);
    cc_reason why = CC_ACCEPT;
    s.t = cc_alloc(n, sizeof *s.t, &why);
    s.tmp = cc_alloc(n, sizeof *s.tmp, &why);
    s.g = cc_alloc(n, sizeof *s.g, &why);
    s.path = cc_alloc(n, sizeof *s.path, &why);
    s.mark = cc_alloc(n, sizeof *s.mark, &why);
    s.gmark = cc_alloc(c->g, sizeof *s.gmark, &why);
    s.bitmap = cc_alloc(((size_t)n + 63u) / 64u, sizeof *s.bitmap, &why);
    if ((size_t)c->p > SIZE_MAX / 4u / (n > 0 ? n : 1u)) {
        why = CC_CAPACITY;
    } else {
        s.rows = cc_alloc((size_t)c->p * n, sizeof *s.rows, &why);
    }
    if (s.t == NULL || s.tmp == NULL || s.g == NULL || s.path == NULL || s.mark == NULL ||
        s.gmark == NULL || s.bitmap == NULL || s.rows == NULL) {
        vs_free(&s);
        return why;
    }
    why = structure(c, &s);
    if (why == CC_ACCEPT) {
        why = bijections(c, &s); /* 1 */
    }
    if (why == CC_ACCEPT) {
        for (uint32_t k = 0; k < c->g; ++k) {
            cc_inverse(gen_row(c, k), c->invs + (size_t)k * n, n); /* own inverses */
        }
        why = provenance(c, &s); /* 2 */
    }
    if (why == CC_ACCEPT) {
        why = prefix_fixed(c); /* 3 */
    }
    if (why == CC_ACCEPT) {
        why = nested(c, &s);
    }
    if (why == CC_ACCEPT) {
        why = orbit_trees(c, &s); /* 4, fills pos */
    }
    if (why == CC_ACCEPT) {
        why = orbits_closed(c); /* 5 */
    }
    if (why == CC_ACCEPT) {
        why = transversals(c, &s); /* 6 */
    }
    if (why == CC_ACCEPT) {
        why = schreier(c, &s); /* 7 */
    }
    if (why == CC_ACCEPT) {
        why = inputs_member(c, &s); /* 8 */
    }
    /* 9, "terminal triviality": the layout has no terminal level; the levels end where the
     * list ends, and the residue of the last sift must be the identity (sifts_to_identity).
     * 10, the order: not stated in the layout and not needed by the checker. */
    vs_free(&s);
    c->verified = why == CC_ACCEPT;
    return why;
}

static uint32_t uf_find(uint32_t *parent, uint32_t v)
{
    while (parent[v] != v) {
        parent[v] = parent[parent[v]];
        v = parent[v];
    }
    return v;
}

void cc_chain_orbit_ids(const cc_chain *c, uint32_t level, uint32_t *orbit_id, uint32_t *scratch)
{
    const uint32_t n = c->n;
    uint32_t *parent = scratch;
    for (uint32_t v = 0; v < n; ++v) {
        parent[v] = v;
    }
    if (level < c->depth) {
        const cc_level *L = &c->levels[level];
        for (uint32_t k = 0; k < L->s; ++k) {
            const uint32_t *s = gen_row(c, L->gen_ids[k]);
            for (uint32_t v = 0; v < n; ++v) {
                uint32_t a = uf_find(parent, v), b = uf_find(parent, s[v]);
                if (a < b) {
                    parent[b] = a;
                } else if (b < a) {
                    parent[a] = b;
                }
            }
        }
    }
    /* spec 7.1: "sort each orbit's target labels increasingly, then sort the orbit lists
     * lexicographically": the orbits are ranked by their least points, which are the roots
     * (unions keep the smaller root), in one increasing pass. */
    uint32_t next = 0;
    for (uint32_t v = 0; v < n; ++v) {
        const uint32_t r = uf_find(parent, v);
        orbit_id[v] = r == v ? next++ : orbit_id[r];
    }
}

uint32_t cc_level_min(const cc_level *L)
{
    uint32_t m = L->orbit[0];
    for (uint32_t q = 1; q < L->r; ++q) {
        if (L->orbit[q] < m) {
            m = L->orbit[q];
        }
    }
    return m;
}
