/* Deterministic stabiliser chain (spec 9.1 second paragraph, 9.2, 7.2; docs/slices/S3.md 2.2,
 * 2.4; detailed plan WP2.3, WP2.5).  Layout and conventions: chain.h.
 *
 * Construction policies (S3 brief 2.2, detailed plan WP2.3), as implemented:
 *  1. Input normalisation: identities and exact duplicates are dropped, first occurrences keep
 *     their order; each kept input i becomes the provenance record INPUT i.
 *  2. Base extension: a residue that fixes every current base point and is not the identity
 *     appends its least moved point as the next base point.
 *  3. Insertion level: the deepest level whose base prefix the residue fixes pointwise, which
 *     is the level at which its sift stopped (or the new level of rule 2).  The generator joins
 *     S_0, ..., S_j (inclusive membership, chain.h).
 *  4. Rebuild: after an insertion at level j the generator lists of levels 0..j have changed,
 *     so their orbits and Schreier vectors are extended by the new generator (queue traversal
 *     continued in discovery order, generators in gen_ids order, existing tree entries kept);
 *     levels > j are unchanged.  The closure then sifts the Schreier generators
 *     t_b s t_(b^s)^-1 not yet checked, level by level from level j up to level 0 (deepest
 *     first), each level in orbit order then generator order, through the levels below.  A
 *     residue that is not the identity is inserted (rules 2, 3) and the closure restarts at
 *     its insertion level, the deepest level affected.  It ends when level 0 is reached and
 *     finished without inserting.  Checks are incremental (S3 review item 3, closure()).
 *     (Reading of "levels >= i" and "lowest affected level": docs/slices/S3-notes.md.)
 *  5. Strict growth: only the nonidentity remainder of a sift is inserted.  The sift stopped at
 *     level j because the residue's image of b_j is outside b_j^(K_j) (or j is new), so the
 *     residue is not in K_j and K_j grows strictly; this bounds the number of insertions by
 *     the lengths of the subgroup chains (spec 9.1 "finite subgroup growth bounds
 *     termination").
 *  6. Provenance (spec 9.2): an inserted residue is recorded as a straight-line product of
 *     existing records along the Schreier-tree paths it used, built only when it is inserted;
 *     long words are never expanded.
 *
 * Order overflow.  Inclusive membership gives K_(i+1) <= Stab_(K_i)(b_i) at every stage, so
 * the product of the orbit lengths is a lower bound on |<inserted generators>| <= |G| during
 * construction and equals |G| at the end.  The construction stops with CAPACITY_LIMIT as soon
 * as that product overflows uint64, which happens iff |G| > 2^64 - 1 (S3 brief 2.5); this also
 * bounds the work for huge groups. */
#include "bsgs/chain.h"

#include <stdlib.h>
#include <string.h>

#include "arena/alloc.h"
#include "arena/checked.h"
#include "bsgs/verify.h"
#include "util/sort.h"

/* ---- lifetime ---- */

void canon_bsgs_init(canon_bsgs *c, uint32_t n)
{
    memset(c, 0, sizeof *c);
    c->n = n;
    canon_perm_table_init(&c->gens, n);
    canon_perm_table_init(&c->invs, n);
    canon_perm_table_init(&c->inputs, n);
    canon_prov_init(&c->prov);
    c->order = 1;
}

void canon_bsgs_free(canon_bsgs *c)
{
    for (uint32_t i = 0; i < c->level_cap; ++i) {
        free(c->levels[i].orbit); /* the block holding orbit, orbit_pos and the vector */
        free(c->levels[i].gen_ids);
    }
    free(c->levels);
    canon_perm_table_free(&c->gens);
    canon_perm_table_free(&c->invs);
    canon_perm_table_free(&c->inputs);
    free(c->gen_node);
    free(c->inv_node);
    canon_prov_free(&c->prov);
    canon_bsgs_init(c, c->n);
}

/* ---- levels ---- */

/* Room for `need` (>= 1) level entries; new entries are terminal levels. */
static canon_status ensure_levels(canon_bsgs *c, uint32_t need)
{
    const uint32_t old_cap = c->level_cap;
    void *levels = c->levels;
    canon_status st = canon_grow_array(&levels, &c->level_cap, need - 1u, 4u, sizeof *c->levels);
    c->levels = levels;
    if (st != CANON_COMPLETE) {
        return st;
    }
    for (uint32_t i = old_cap; i < c->level_cap; ++i) {
        memset(&c->levels[i], 0, sizeof c->levels[i]);
        c->levels[i].base_point = CANON_BSGS_NONE; /* a terminal level */
    }
    return CANON_COMPLETE;
}

/* Turn the terminal level levels[depth] into a level with base point b (orbit {b}, no
 * generators yet) and open a new terminal level. */
static canon_status append_base_point(canon_bsgs *c, uint32_t b)
{
    const uint32_t n = c->n;
    canon_status st = ensure_levels(c, c->depth + 2u);
    if (st != CANON_COMPLETE) {
        return st;
    }
    size_t words = 0;
    if (!canon_size_mul((size_t)n, 5u, &words)) {
        return CANON_CAPACITY_LIMIT;
    }
    uint32_t *block = canon_alloc_array(words, sizeof *block, &st);
    if (block == NULL) {
        return st;
    }
    canon_bsgs_level *L = &c->levels[c->depth];
    L->orbit = block;
    L->orbit_pos = block + n;
    L->parent_point = block + 2u * (size_t)n;
    L->parent_gen = block + 3u * (size_t)n;
    L->schreier_done = block + 4u * (size_t)n;
    for (uint32_t v = 0; v < n; ++v) {
        L->orbit_pos[v] = CANON_BSGS_NONE;
    }
    L->schreier_done[0] = 0;
    L->base_point = b;
    L->orbit_len = 1;
    L->orbit[0] = b;
    L->orbit_pos[b] = 0;
    L->parent_point[0] = CANON_BSGS_NONE;
    L->parent_gen[0] = CANON_BSGS_NONE;
    c->depth += 1;
    return CANON_COMPLETE;
}

static canon_status level_push_gen(canon_bsgs_level *L, uint32_t id)
{
    void *ids = L->gen_ids;
    canon_status st = canon_grow_array(&ids, &L->gen_cap, L->gen_count, 4u, sizeof *L->gen_ids);
    L->gen_ids = ids;
    if (st == CANON_COMPLETE) {
        L->gen_ids[L->gen_count++] = id;
    }
    return st;
}

/* S3 review item 3: extend the orbit of `level` after generators gen_ids[first_new..] were
 * appended.  The queue traversal continues from the existing tree, which is kept: the old
 * points (positions < old length) are expanded by the new generators only, since the old
 * generators already map the old orbit into itself, and every newly discovered point by all
 * generators, in discovery order (FIFO).  Because no stored edge changes, the transporter of
 * every old point, and so every Schreier generator already checked, keeps its value. */
static void level_extend(canon_bsgs *c, uint32_t level, uint32_t first_new)
{
    canon_bsgs_level *L = &c->levels[level];
    const uint32_t old_len = L->orbit_len;
    uint32_t len = old_len;
    for (uint32_t head = 0; head < len; ++head) {
        const uint32_t x = L->orbit[head];
        for (uint32_t gi = head < old_len ? first_new : 0; gi < L->gen_count; ++gi) {
            const uint32_t y = canon_perm_table_row(&c->gens, L->gen_ids[gi])[x]; /* x^s */
            if (L->orbit_pos[y] == CANON_BSGS_NONE) {
                L->orbit[len] = y;
                L->orbit_pos[y] = len;
                L->parent_point[len] = x;
                L->parent_gen[len] = gi;
                L->schreier_done[len] = 0; /* no Schreier generator of y checked yet */
                ++len;
            }
        }
    }
    L->orbit_len = len;
}

/* S3 brief 2.2 item 4: queue traversal from the base point, points expanded in discovery order
 * (FIFO), generators in gen_ids order; a point discovered through generator gen_ids[gi] from
 * point x records parent_point = x, parent_gen = gi.  orbit_pos must describe the current
 * orbit on entry (NONE elsewhere).  The constructor extends orbits instead (level_extend); this
 * full recomputation serves levels built or edited by hand (tests). */
void canon_bsgs_level_recompute(canon_bsgs *c, uint32_t level)
{
    canon_bsgs_level *L = &c->levels[level];
    for (uint32_t p = 0; p < L->orbit_len; ++p) {
        L->orbit_pos[L->orbit[p]] = CANON_BSGS_NONE;
    }
    const uint32_t b = L->base_point;
    L->orbit[0] = b;
    L->orbit_pos[b] = 0;
    L->parent_point[0] = CANON_BSGS_NONE;
    L->parent_gen[0] = CANON_BSGS_NONE;
    uint32_t len = 1;
    for (uint32_t head = 0; head < len; ++head) {
        const uint32_t x = L->orbit[head];
        for (uint32_t gi = 0; gi < L->gen_count; ++gi) {
            const uint32_t y = canon_perm_table_row(&c->gens, L->gen_ids[gi])[x]; /* x^s */
            if (L->orbit_pos[y] == CANON_BSGS_NONE) {
                L->orbit[len] = y;
                L->orbit_pos[y] = len;
                L->parent_point[len] = x;
                L->parent_gen[len] = gi;
                ++len;
            }
        }
    }
    L->orbit_len = len;
}

/* spec 9.2: order = product of the orbit lengths of levels from..depth-1; false on overflow. */
static bool orbit_product(const canon_bsgs *c, uint32_t from, uint64_t *out)
{
    uint64_t prod = 1;
    for (uint32_t i = from; i < c->depth; ++i) {
        if (!canon_u64_mul(prod, c->levels[i].orbit_len, &prod)) {
            return false;
        }
    }
    *out = prod;
    return true;
}

uint64_t canon_bsgs_suffix_order(const canon_bsgs *c, uint32_t level)
{
    uint64_t order = 0;
    return orbit_product(c, level, &order) ? order : 0;
}

/* ---- transporters and sifting ---- */

/* t_x for the point at orbit position pos (S3 brief 2.2): the upward walk meets the edges in
 * the order s_(k-1), ..., s_0, and t_x = s_0 s_1 ... s_(k-1), so each generator met is
 * multiplied on the LEFT of the product so far: out <- s out, (s out)[v] = out[s[v]] (spec 3).
 * Returns the number of dense products. */
static uint64_t transporter_into(const canon_bsgs *c, const canon_bsgs_level *L, uint32_t pos,
                                 uint32_t *out, uint32_t *tmp)
{
    const uint32_t n = c->n;
    uint64_t steps = 0;
    for (uint32_t v = 0; v < n; ++v) {
        out[v] = v;
    }
    while (pos != 0) {
        const uint32_t *s = canon_perm_table_row(&c->gens, L->gen_ids[L->parent_gen[pos]]);
        canon_perm_compose(s, out, tmp, n); /* tmp = s out */
        memcpy(out, tmp, (size_t)n * sizeof *out);
        pos = L->orbit_pos[L->parent_point[pos]];
        ++steps;
    }
    return steps;
}

canon_status canon_bsgs_transporter(const canon_bsgs *c, uint32_t level, uint32_t b,
                                    uint32_t *out)
{
    if (level >= c->depth || b >= c->n || c->levels[level].orbit_pos[b] == CANON_BSGS_NONE) {
        return CANON_INVALID_INPUT;
    }
    canon_status st = CANON_COMPLETE;
    uint32_t *tmp = canon_alloc_array(c->n, sizeof *tmp, &st);
    if (tmp == NULL) {
        return st;
    }
    (void)transporter_into(c, &c->levels[level], c->levels[level].orbit_pos[b], out, tmp);
    free(tmp);
    return CANON_COMPLETE;
}

void canon_bsgs_transporter_scratch(const canon_bsgs *c, uint32_t level, uint32_t b,
                                    uint32_t *out, uint32_t *tmp)
{
    const canon_bsgs_level *L = &c->levels[level];
    (void)transporter_into(c, L, L->orbit_pos[b], out, tmp);
}

/* g <- g t_x^-1 for the point x at orbit position pos.  t_x^-1 = s_(k-1)^-1 ... s_0^-1 and
 * the upward walk meets s_(k-1) first, so each stored inverse is applied on the RIGHT in walk
 * order: g'[v] = s^-1[g[v]].  Returns the number of dense products. */
static uint64_t times_inverse_path(const canon_bsgs *c, const canon_bsgs_level *L, uint32_t pos,
                                   uint32_t *g)
{
    const uint32_t n = c->n;
    uint64_t steps = 0;
    while (pos != 0) {
        const uint32_t *inv = canon_perm_table_row(&c->invs, L->gen_ids[L->parent_gen[pos]]);
        for (uint32_t v = 0; v < n; ++v) {
            g[v] = inv[g[v]];
        }
        pos = L->orbit_pos[L->parent_point[pos]];
        ++steps;
    }
    return steps;
}

/* spec 9.2 sift from level `from`, recording the (level, orbit position) of every step whose
 * transporter is not the identity (for provenance) when rec_level is not NULL. */
static void sift_record(const canon_bsgs *c, uint32_t from, uint32_t *g, uint32_t *stop,
                        uint32_t *rec_level, uint32_t *rec_pos, uint32_t *rec_count,
                        canon_bsgs_stats *stats)
{
    uint32_t k = 0;
    for (uint32_t j = from; j < c->depth; ++j) {
        const canon_bsgs_level *L = &c->levels[j];
        uint32_t pos = L->orbit_pos[g[L->base_point]];
        if (pos == CANON_BSGS_NONE) {
            *stop = j; /* the image of b_j is outside b_j^(K_j) */
            if (rec_count != NULL) {
                *rec_count = k;
            }
            return;
        }
        if (pos != 0) {
            if (rec_level != NULL) {
                rec_level[k] = j;
                rec_pos[k] = pos;
                ++k;
            }
            uint64_t steps = times_inverse_path(c, L, pos, g); /* g now fixes b_j */
            if (stats != NULL) {
                stats->compositions += steps;
            }
        }
    }
    *stop = c->depth;
    if (rec_count != NULL) {
        *rec_count = k;
    }
}

void canon_bsgs_sift(const canon_bsgs *c, uint32_t from, uint32_t *g, uint32_t *stop,
                     canon_bsgs_stats *stats)
{
    if (stats != NULL) {
        stats->sifts += 1;
    }
    sift_record(c, from, g, stop, NULL, NULL, NULL, stats);
}

canon_status canon_bsgs_contains(const canon_bsgs *c, const uint32_t *p, bool *out)
{
    /* spec 9.1/9.2: membership by sifting a copy of p once, in per-call scratch (S3 review
     * item 5): p is in the group iff the residue is the identity. */
    *out = false;
    canon_status st = CANON_COMPLETE;
    uint32_t *g = canon_alloc_array(c->n, sizeof *g, &st);
    if (g == NULL) {
        return st;
    }
    if (c->n > 0) {
        memcpy(g, p, (size_t)c->n * sizeof *g);
    }
    uint32_t stop = 0;
    sift_record(c, 0, g, &stop, NULL, NULL, NULL, NULL);
    *out = stop == c->depth && canon_perm_is_identity(g, c->n);
    free(g);
    return CANON_COMPLETE;
}

/* ---- construction ---- */

typedef struct build_ctx {
    uint32_t *g;         /* residue */
    uint32_t *t;         /* transporter t_b */
    uint32_t *tmp;
    uint32_t *rec_level; /* sift record, n + 1 entries */
    uint32_t *rec_pos;
    uint32_t rec_count;
} build_ctx;

/* Provenance of the recorded sift steps applied after `node`: node t_(c_0)^-1 t_(c_1)^-1 ...,
 * each inverse transporter as the product of stored inverse records in upward walk order. */
static canon_status prov_times_recorded(canon_bsgs *c, const build_ctx *x, uint32_t *node)
{
    for (uint32_t i = 0; i < x->rec_count; ++i) {
        const canon_bsgs_level *L = &c->levels[x->rec_level[i]];
        for (uint32_t q = x->rec_pos[i]; q != 0; q = L->orbit_pos[L->parent_point[q]]) {
            const uint32_t inv = c->inv_node[L->gen_ids[L->parent_gen[q]]];
            canon_status st = canon_prov_product(&c->prov, *node, inv, node);
            if (st != CANON_COMPLETE) {
                return st;
            }
        }
    }
    return CANON_COMPLETE;
}

/* Insert the nonidentity residue x->g with provenance `node` at level `level` (policies 2, 3:
 * level == depth means the base is first extended by its least moved point), extend the orbits
 * of the changed levels 0..level, and refresh the order.  *inserted_at receives the level. */
static canon_status insert(canon_bsgs *c, build_ctx *x, uint32_t level, uint32_t node,
                           uint32_t *inserted_at)
{
    const uint32_t n = c->n;
    canon_status st = CANON_COMPLETE;
    if (level == c->depth) {
        /* policy 2: base extension by the least moved point */
        uint32_t b = 0;
        while (b < n && x->g[b] == b) {
            ++b;
        }
        if (b == n) {
            return CANON_INTERNAL_ERROR; /* the identity is never inserted */
        }
        st = append_base_point(c, b);
        if (st != CANON_COMPLETE) {
            return st;
        }
    }
    /* gen_node and inv_node are parallel arrays sharing node_cap: grow both, then commit */
    uint32_t cap_g = c->node_cap, cap_i = c->node_cap;
    void *gn = c->gen_node, *in = c->inv_node;
    st = canon_grow_array(&gn, &cap_g, c->gens.count, 8u, sizeof *c->gen_node);
    c->gen_node = gn;
    if (st == CANON_COMPLETE) {
        st = canon_grow_array(&in, &cap_i, c->gens.count, 8u, sizeof *c->inv_node);
        c->inv_node = in;
    }
    if (st != CANON_COMPLETE) {
        return st;
    }
    c->node_cap = cap_g < cap_i ? cap_g : cap_i;
    uint32_t id = 0, inv_id = 0, inv_node = 0;
    canon_perm_inverse(x->g, x->tmp, n); /* spec 3: the inverse, stored once (S3 brief 2.1) */
    c->stats.compositions += 1;
    st = canon_perm_table_push(&c->gens, x->g, &id);
    if (st == CANON_COMPLETE) {
        st = canon_perm_table_push(&c->invs, x->tmp, &inv_id);
    }
    if (st == CANON_COMPLETE) {
        st = canon_prov_inverse(&c->prov, node, &inv_node);
    }
    if (st != CANON_COMPLETE) {
        return st;
    }
    c->gen_node[id] = node;
    c->inv_node[id] = inv_node;
    /* inclusive membership: the residue fixes b_0..b_(level-1), so it joins S_0..S_level */
    for (uint32_t i = 0; i <= level; ++i) {
        st = level_push_gen(&c->levels[i], id);
        if (st != CANON_COMPLETE) {
            return st;
        }
    }
    for (uint32_t i = 0; i <= level; ++i) {
        /* policy 4: the changed levels, extended by the new generator (the last in gen_ids) */
        level_extend(c, i, c->levels[i].gen_count - 1u);
    }
    c->stats.insertions += 1;
    if (!orbit_product(c, 0, &c->order)) {
        return CANON_CAPACITY_LIMIT; /* |G| > 2^64 - 1 (file comment) */
    }
    *inserted_at = level;
    return CANON_COMPLETE;
}

/* Policy 4 with incremental Schreier checks (S3 review item 3): the closure visits levels
 * deepest first, restarting at each insertion level, and at each level sifts only the Schreier
 * generators t_b s t_(b^s)^-1 not yet checked.  schreier_done[p] counts the generators of the
 * point at orbit position p already checked, a prefix of gen_ids.  A check stays valid after
 * later insertions: the old transporters are unchanged (level_extend keeps the tree), and an
 * element that sifted to the identity through the levels below lies in their group, which only
 * grows.  After an insertion of s at level j the new checks are exactly the pairs
 * (new point, any generator) and (any point, s) of levels 0..j. */
static canon_status closure(canon_bsgs *c, build_ctx *x, uint32_t start)
{
    const uint32_t n = c->n;
    uint32_t k = start;
restart:
    for (uint32_t lv = k + 1; lv-- > 0;) {
        for (uint32_t p = 0; p < c->levels[lv].orbit_len; ++p) {
            canon_bsgs_level *L = &c->levels[lv];
            if (L->schreier_done[p] >= L->gen_count) {
                continue;
            }
            const uint32_t b = L->orbit[p];
            c->stats.compositions += transporter_into(c, L, p, x->t, x->tmp); /* t_b */
            for (uint32_t gi = L->schreier_done[p]; gi < L->gen_count; ++gi) {
                /* The pair counts as checked from here on: either its residue is the identity
                 * or it is inserted below, after which the pair's element lies in the group of
                 * the levels below. */
                L->schreier_done[p] = gi + 1u;
                const uint32_t sid = L->gen_ids[gi];
                const uint32_t *s = canon_perm_table_row(&c->gens, sid);
                const uint32_t cpos = L->orbit_pos[s[b]]; /* b^s is in the orbit (closed) */
                c->stats.candidates += 1;
                if (L->parent_point[cpos] == b && L->parent_gen[cpos] == gi) {
                    continue; /* tree edge: t_(b^s) = t_b s, the residue is the identity */
                }
                /* spec 9.1: t_b s t_(b^s)^-1, which fixes the base point */
                canon_perm_compose(x->t, s, x->g, n);
                c->stats.compositions += 1 + times_inverse_path(c, L, cpos, x->g);
                uint32_t stop = 0;
                c->stats.sifts += 1;
                sift_record(c, lv + 1, x->g, &stop, x->rec_level, x->rec_pos, &x->rec_count,
                            &c->stats);
                if (canon_perm_is_identity(x->g, n)) {
                    continue;
                }
                /* policy 6: PRODUCT(PRODUCT(t_b, s), t_(b^s)^-1) then the sift steps */
                uint32_t node = CANON_PROV_NONE;
                canon_status st = CANON_COMPLETE;
                for (uint32_t q = p; q != 0 && st == CANON_COMPLETE;
                     q = L->orbit_pos[L->parent_point[q]]) {
                    st = canon_prov_product(&c->prov, c->gen_node[L->gen_ids[L->parent_gen[q]]],
                                            node, &node); /* left factor: t_b = s_0 ... */
                }
                if (st == CANON_COMPLETE) {
                    st = canon_prov_product(&c->prov, node, c->gen_node[sid], &node);
                }
                for (uint32_t q = cpos; q != 0 && st == CANON_COMPLETE;
                     q = L->orbit_pos[L->parent_point[q]]) {
                    st = canon_prov_product(&c->prov, node,
                                            c->inv_node[L->gen_ids[L->parent_gen[q]]], &node);
                }
                if (st == CANON_COMPLETE) {
                    st = prov_times_recorded(c, x, &node);
                }
                uint32_t at = 0;
                if (st == CANON_COMPLETE) {
                    st = insert(c, x, stop, node, &at); /* may move c->levels */
                }
                if (st != CANON_COMPLETE) {
                    return st;
                }
                k = at; /* policy 4: restart at the deepest affected level */
                goto restart;
            }
        }
    }
    return CANON_COMPLETE;
}

typedef struct rows_ctx {
    const uint32_t *rows;
    uint32_t n;
} rows_ctx;

/* spec 7.2 numerical lexicographic order of the image arrays of two row indices */
static int row_index_cmp(const void *a, const void *b, void *ctx)
{
    const rows_ctx *r = ctx;
    size_t ia = *(const size_t *)a, ib = *(const size_t *)b;
    return canon_perm_lex_compare(r->rows + ia * r->n, r->rows + ib * r->n, r->n);
}

/* Policy 1 (S3 review item 9): keep[i] = 1 iff input i is not the identity and no earlier
 * input equals it.  The row indices are sorted by image array with the shared stable sort, so
 * equal rows are adjacent and in input order: the first of each run is the first occurrence.
 * O(count log count) comparisons of n entries. */
static canon_status kept_inputs(const uint32_t *gens, uint32_t count, uint32_t n, uint8_t *keep)
{
    canon_status st = CANON_COMPLETE;
    size_t *idx = canon_alloc_array(count, sizeof *idx, &st);
    size_t *tmp = canon_alloc_array(count, sizeof *tmp, &st);
    if (idx == NULL || tmp == NULL) {
        free(idx);
        free(tmp);
        return st;
    }
    for (uint32_t i = 0; i < count; ++i) {
        idx[i] = i;
    }
    rows_ctx r = {gens, n};
    canon_stable_sort(idx, count, sizeof *idx, tmp, row_index_cmp, &r);
    for (uint32_t k = 0; k < count; ++k) {
        const uint32_t *row = gens + idx[k] * n;
        keep[idx[k]] = !canon_perm_is_identity(row, n) &&
                       (k == 0 || row_index_cmp(&idx[k - 1], &idx[k], &r) != 0);
    }
    free(idx);
    free(tmp);
    return CANON_COMPLETE;
}

static canon_status build_run(canon_bsgs *c, const uint32_t *gens, uint32_t count,
                              const uint32_t *prefix, uint32_t prefix_len, build_ctx *x)
{
    const uint32_t n = c->n;
    canon_status st = CANON_COMPLETE;
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t row = 0;
        /* degree 0: rows are empty and never read (gens may be NULL) */
        st = canon_perm_table_push(&c->inputs, n > 0 ? gens + (size_t)i * n : NULL, &row);
        if (st != CANON_COMPLETE) {
            return st;
        }
    }
    st = ensure_levels(c, 1);
    if (st != CANON_COMPLETE) {
        return st;
    }
    /* spec 9.2 base change: the requested ordered prefix comes first */
    for (uint32_t i = 0; i < prefix_len; ++i) {
        if (prefix[i] >= n) {
            return CANON_INVALID_INPUT;
        }
        for (uint32_t j = 0; j < i; ++j) {
            if (prefix[j] == prefix[i]) {
                return CANON_INVALID_INPUT;
            }
        }
        st = append_base_point(c, prefix[i]);
        if (st != CANON_COMPLETE) {
            return st;
        }
    }
    if (n == 0 || count == 0) {
        return CANON_COMPLETE; /* degree 0: every generator is the identity; order 1 */
    }
    uint8_t *keep = canon_alloc_array(count, sizeof *keep, &st);
    if (keep == NULL) {
        return st;
    }
    st = kept_inputs(gens, count, n, keep);
    for (uint32_t i = 0; i < count && st == CANON_COMPLETE; ++i) {
        const uint32_t *gi = gens + (size_t)i * n;
        /* policy 1: drop identities and exact duplicates of an earlier kept input */
        if (!keep[i]) {
            continue;
        }
        uint32_t node = 0;
        st = canon_prov_input(&c->prov, i, &node);
        if (st != CANON_COMPLETE) {
            break;
        }
        /* policy 5: sift first; only the nonidentity remainder is inserted */
        memcpy(x->g, gi, (size_t)n * sizeof *x->g);
        uint32_t stop = 0;
        c->stats.candidates += 1;
        c->stats.sifts += 1;
        sift_record(c, 0, x->g, &stop, x->rec_level, x->rec_pos, &x->rec_count, &c->stats);
        if (canon_perm_is_identity(x->g, n)) {
            continue;
        }
        st = prov_times_recorded(c, x, &node);
        uint32_t at = 0;
        if (st == CANON_COMPLETE) {
            st = insert(c, x, stop, node, &at);
        }
        if (st == CANON_COMPLETE) {
            st = closure(c, x, at);
        }
    }
    free(keep);
    return st; /* c->order was refreshed at every insertion */
}

canon_status canon_bsgs_build(canon_bsgs *out, uint32_t n, const uint32_t *gens, size_t count,
                              const uint32_t *prefix, uint32_t prefix_len)
{
    canon_bsgs_init(out, n);
    if (count > UINT32_MAX - 1u) {
        return CANON_CAPACITY_LIMIT; /* spec 11.1: input indices are uint32 */
    }
    canon_status st = CANON_COMPLETE;
    build_ctx x;
    x.rec_count = 0;
    x.g = canon_alloc_array(n, sizeof *x.g, &st);
    x.t = canon_alloc_array(n, sizeof *x.t, &st);
    x.tmp = canon_alloc_array(n, sizeof *x.tmp, &st);
    x.rec_level = canon_alloc_array((size_t)n + 1u, sizeof *x.rec_level, &st);
    x.rec_pos = canon_alloc_array((size_t)n + 1u, sizeof *x.rec_pos, &st);
    if (x.g != NULL && x.t != NULL && x.tmp != NULL && x.rec_level != NULL &&
        x.rec_pos != NULL) {
        st = build_run(out, gens, (uint32_t)count, prefix, prefix_len, &x);
    }
    free(x.g);
    free(x.t);
    free(x.tmp);
    free(x.rec_level);
    free(x.rec_pos);
    if (st != CANON_COMPLETE) {
        canon_bsgs_free(out);
    }
    return st;
}

/* ---- rebase (spec 9.2) ---- */

canon_status canon_bsgs_rebase(const canon_bsgs *src, uint32_t from, const uint32_t *prefix,
                               uint32_t prefix_len, bool verify, canon_bsgs *out)
{
    const uint32_t n = src->n;
    canon_bsgs_init(out, n);
    if (from > src->depth) {
        return CANON_INVALID_INPUT;
    }
    const canon_bsgs_level *L = &src->levels[from];
    size_t words = 0;
    if (!canon_size_mul((size_t)L->gen_count, (size_t)n, &words)) {
        return CANON_CAPACITY_LIMIT;
    }
    canon_status st = CANON_COMPLETE;
    uint32_t *flat = canon_alloc_array(words, sizeof *flat, &st);
    if (flat == NULL) {
        return st;
    }
    for (uint32_t k = 0; k < L->gen_count && n > 0; ++k) {
        memcpy(flat + (size_t)k * n, canon_perm_table_row(&src->gens, L->gen_ids[k]),
               (size_t)n * sizeof *flat);
    }
    st = canon_bsgs_build(out, n, flat, L->gen_count, prefix, prefix_len);
    if (st == CANON_COMPLETE && verify) {
        /* spec 9.2: "base change rebuilds/certifies" */
        canon_bsgs_reason reason = CANON_BSGS_UNCHECKED;
        st = canon_bsgs_verify(out, flat, L->gen_count, &reason);
        if (st == CANON_COMPLETE && reason != CANON_BSGS_VALID) {
            st = CANON_INTERNAL_ERROR;
        }
        if (st != CANON_COMPLETE) {
            canon_bsgs_free(out);
        }
    }
    free(flat);
    return st;
}

canon_status canon_bsgs_build_verified(canon_bsgs *out, uint32_t n, const uint32_t *gens,
                                       size_t count)
{
    canon_status st = canon_bsgs_build(out, n, gens, count, NULL, 0);
    if (st != CANON_COMPLETE) {
        return st; /* canon_bsgs_build left *out empty */
    }
    /* spec 9.1: the verifier is independent of the constructor (src/bsgs/verify.c). */
    canon_bsgs_reason reason = CANON_BSGS_UNCHECKED;
    st = canon_bsgs_verify(out, gens, (uint32_t)count, &reason);
    if (st == CANON_COMPLETE && reason != CANON_BSGS_VALID) {
        st = CANON_INTERNAL_ERROR;
    }
    if (st != CANON_COMPLETE) {
        canon_bsgs_free(out);
    }
    return st;
}

/* ---- orbits of a pointwise stabiliser (spec 7.1) ---- */

/* Union-find with parent[x] <= x, so every root is the least point of its class. */
static uint32_t uf_find(uint32_t *parent, uint32_t x)
{
    while (parent[x] != x) {
        parent[x] = parent[parent[x]];
        x = parent[x];
    }
    return x;
}

void canon_bsgs_orbit_ids(const canon_bsgs *c, uint32_t level, uint32_t *orbit_id)
{
    const uint32_t n = c->n;
    const canon_bsgs_level *L = &c->levels[level];
    uint32_t *parent = orbit_id;
    for (uint32_t v = 0; v < n; ++v) {
        parent[v] = v;
    }
    /* the orbits of K_level are the classes of "v ~ v^s" for s in S_level */
    for (uint32_t k = 0; k < L->gen_count; ++k) {
        const uint32_t *s = canon_perm_table_row(&c->gens, L->gen_ids[k]);
        for (uint32_t v = 0; v < n; ++v) {
            uint32_t ra = uf_find(parent, v), rb = uf_find(parent, s[v]);
            if (ra < rb) {
                parent[rb] = ra;
            } else if (rb < ra) {
                parent[ra] = rb;
            }
        }
    }
    /* spec 7.1: "sort each orbit's target labels increasingly, then sort the orbit lists
     * lexicographically": disjoint orbits order by their least points, which are the roots.
     * Invariant: parent[x] <= x (unions attach the larger root to the smaller, path halving
     * only shortens), so in one pass over v in increasing order parent[v] = r < v is a point
     * already rewritten to the rank of its class, which is v's class; a root (r == v) takes
     * the next rank. */
    uint32_t next = 0;
    for (uint32_t v = 0; v < n; ++v) {
        uint32_t r = parent[v];
        parent[v] = r == v ? next++ : parent[r];
    }
}

/* ---- tuple minimum (spec 7.2) ---- */

typedef struct tm_ctx {
    uint32_t *t, *u, *tmp, *queue, *mark;
    uint32_t stamp;
} tm_ctx;

/* The least point of a^K where K = <S_level of c>, by a queue traversal. */
static uint32_t orbit_min(const canon_bsgs *c, uint32_t level, uint32_t a, tm_ctx *x,
                          uint32_t *size)
{
    const canon_bsgs_level *L = &c->levels[level];
    if (level < c->depth && a == L->base_point) {
        uint32_t m = a;
        for (uint32_t p = 0; p < L->orbit_len; ++p) {
            m = L->orbit[p] < m ? L->orbit[p] : m;
        }
        *size = L->orbit_len;
        return m;
    }
    if (x->stamp == UINT32_MAX) {
        for (uint32_t v = 0; v < c->n; ++v) {
            x->mark[v] = 0;
        }
        x->stamp = 0;
    }
    const uint32_t st = ++x->stamp;
    uint32_t len = 1, m = a;
    x->queue[0] = a;
    x->mark[a] = st;
    for (uint32_t head = 0; head < len; ++head) {
        for (uint32_t k = 0; k < L->gen_count; ++k) {
            uint32_t y = canon_perm_table_row(&c->gens, L->gen_ids[k])[x->queue[head]];
            if (x->mark[y] != st) {
                x->mark[y] = st;
                x->queue[len++] = y;
                m = y < m ? y : m;
            }
        }
    }
    *size = len;
    return m;
}

static void add_stats(canon_bsgs_stats *acc, const canon_bsgs_stats *s)
{
    if (acc != NULL) {
        acc->candidates += s->candidates;
        acc->sifts += s->sifts;
        acc->compositions += s->compositions;
        acc->insertions += s->insertions;
    }
}

/* spec 7.2: "A constructive procedure starts t=id, H=G; at list position i set a=t[L[i]],
 * choose b=min(a^H) and u in H with a^u=b, then t <- t u and H <- H_b."  H is a suffix of a
 * chain (cur, level); when b is not the base point of that level, H is rebased so that its
 * first base point is b.  That is one rebuild per such step, not verified: these chains are
 * transient and correct by construction.  At most O(n) rebuilds per call, the known S3 cost
 * (M5 work).  u = t_a^-1, where t_a sends b to a in the rebased level.
 *
 * The list processed is L followed by 0, 1, ..., n-1.  After the |L| entries of L, t
 * minimises L^t and the minimisers are exactly the coset t G_M, M = L^t (if L^g = M then
 * t^-1 g fixes M); the orbit ids of G_M are read there.  The remaining entries choose, among
 * them, the one with the least image array (t g)[0], (t g)[1], ...: the same procedure on the
 * full list (0, ..., n-1), so the result is the least minimiser, as the group interface
 * requires (src/bsgs/group.h). */
static canon_status tuple_run(const canon_bsgs *c, const uint32_t *Lst, uint32_t len,
                              uint32_t *orbit_id_out, tm_ctx *x, canon_bsgs *own,
                              canon_bsgs_stats *stats)
{
    const uint32_t n = c->n;
    const canon_bsgs *cur = c;
    uint32_t level = 0;
    bool orbits_done = orbit_id_out == NULL;
    const uint64_t total = (uint64_t)len + n;
    for (uint64_t i = 0; i < total; ++i) {
        if (i == len && !orbits_done) {
            canon_bsgs_orbit_ids(cur, level, orbit_id_out); /* spec 7.1: orbits of G_M */
            orbits_done = true;
        }
        if (cur->levels[level].gen_count == 0) {
            break; /* H is trivial: t is final */
        }
        const uint32_t pt = i < len ? Lst[i] : (uint32_t)(i - len);
        const uint32_t a = x->t[pt];
        uint32_t size = 0;
        const uint32_t b = orbit_min(cur, level, a, x, &size); /* b = min(a^H) */
        if (size == 1) {
            continue; /* H fixes a: u = id, H_b = H */
        }
        if (level >= cur->depth || cur->levels[level].base_point != b) {
            /* spec 9.2 base change: H with base starting at b */
            canon_bsgs next;
            /* internal and transient: correct by the deterministic closure, not verified
             * (S3 review item 2; the group's own chain was verified at creation) */
            canon_status st = canon_bsgs_rebase(cur, level, &b, 1, false, &next);
            if (st != CANON_COMPLETE) {
                return st;
            }
            add_stats(stats, &next.stats);
            canon_bsgs_free(own);
            *own = next;
            cur = own;
            level = 0;
        }
        /* u = t_a^-1 in H: t_a (level `level`, base b) sends b to a */
        const canon_bsgs_level *Lv = &cur->levels[level];
        uint64_t steps = transporter_into(cur, Lv, Lv->orbit_pos[a], x->u, x->tmp);
        canon_perm_inverse(x->u, x->tmp, n); /* tmp = u, a^u = b */
        canon_perm_compose(x->t, x->tmp, x->u, n); /* spec 7.2: t <- t u (u acts second) */
        memcpy(x->t, x->u, (size_t)n * sizeof *x->t);
        if (stats != NULL) {
            stats->compositions += steps + 2u;
        }
        level += 1; /* H <- H_b, the suffix after fixing b */
    }
    if (!orbits_done) {
        canon_bsgs_orbit_ids(cur, level, orbit_id_out); /* H trivial before |L| steps */
    }
    return CANON_COMPLETE;
}

canon_status canon_bsgs_tuple_min(const canon_bsgs *c, const uint32_t *L, uint32_t len,
                                  uint32_t *t_out, uint32_t *orbit_id_out,
                                  canon_bsgs_stats *stats)
{
    const uint32_t n = c->n;
    for (uint32_t i = 0; i < len; ++i) {
        if (L[i] >= n) {
            return CANON_INVALID_INPUT;
        }
    }
    canon_status st = CANON_COMPLETE;
    tm_ctx x;
    x.stamp = 0;
    x.t = canon_alloc_array(n, sizeof *x.t, &st);
    x.u = canon_alloc_array(n, sizeof *x.u, &st);
    x.tmp = canon_alloc_array(n, sizeof *x.tmp, &st);
    x.queue = canon_alloc_array(n, sizeof *x.queue, &st);
    x.mark = canon_alloc_array(n, sizeof *x.mark, &st);
    canon_bsgs own;
    canon_bsgs_init(&own, n);
    if (x.t != NULL && x.u != NULL && x.tmp != NULL && x.queue != NULL && x.mark != NULL) {
        for (uint32_t v = 0; v < n; ++v) {
            x.t[v] = v;
            x.mark[v] = 0;
        }
        st = tuple_run(c, L, len, orbit_id_out, &x, &own, stats);
        if (st == CANON_COMPLETE && n > 0) {
            memcpy(t_out, x.t, (size_t)n * sizeof *t_out);
        }
    }
    canon_bsgs_free(&own);
    free(x.t);
    free(x.u);
    free(x.tmp);
    free(x.queue);
    free(x.mark);
    return st;
}
