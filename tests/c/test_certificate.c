/* Slice S7 step 2 (docs/slices/S7.md 3.3, 3.5, 4; docs/certificate-format.md): the certificate
 * v0 writer, CERT-0.
 *
 *  A. Chain round trip: the serialised chain (brief 3.5 with input references) of every T1
 *     group's chain, and of rebased chains with random injective prefixes, re-read by the
 *     test-side reader below into a canon_bsgs, field by field equal to the original and
 *     accepted by canon_bsgs_verify against the resolved inputs; single-byte mutations are
 *     rejected by the reader or the verifier.
 *  B. The spec 7.4 cases: header fields, node counts under both work policies (derived from
 *     the rules), the one PRUNED entry of the empty subset under Sym(2); the nested case is
 *     refused at problem creation.  Their traces and bytes are the goldens of
 *     test_search_subset.c / test_search_graph.c; here FINAL is compared with the result.
 *  C. A structural walk of every certificate of the T1 subsets (every group, both generating
 *     sets) and of random G1 graphs, both work policies and both backends: every record is
 *     re-parsed; the refinement is replayed with the engine's partition code to know each
 *     target cell; child entries cover the cell once in increasing atom id; every rebased chain
 *     verifies with base prefix M and M[i] least in its orbit; every automorphism is in G,
 *     fixes x and the prefix and maps rep to b; LEAF t is the last sweep's u; FINAL equals the
 *     result's trace, bytes and witness; the NODE records count the internal search's nodes.
 *  D. Equivalence: with and without a certificate the same trace, bytes and witness;
 *     certificates byte-identical across backends and repeated solves.
 *  E. Errors and ownership.
 *
 * Convention (spec 3): p[v] = v^p, (pq)[v] = q[p[v]].
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "bsgs/chain.h"
#include "bsgs/group.h"
#include "bsgs/verify.h"
#include "canon/canon.h"
#include "check.h"
#include "encoding/wire.h"
#include "object/object.h"
#include "refine/p1.h"
#include "search/certificate.h"
#include "search/p1_tree.h"
#include "t1_groups.h"

#define REF CANON_WORK_POLICY_REFERENCE
#define PRUNE CANON_WORK_POLICY_ORBIT_PRUNE
#define NONE UINT32_MAX

static canon_context *CTX[2]; /* chain, explicit */
static canon_workspace *WS;

/* ---- test-side reader (bounds-checked cursor) ---- */

typedef struct cur {
    const uint8_t *p;
    size_t len, pos;
    bool bad;
} cur;

static uint32_t rd_u32(cur *c)
{
    if (c->bad || c->len - c->pos < 4) {
        c->bad = true;
        return 0;
    }
    const uint8_t *b = c->p + c->pos;
    c->pos += 4;
    return (uint32_t)b[0] << 24 | (uint32_t)b[1] << 16 | (uint32_t)b[2] << 8 | b[3];
}

static uint32_t rd_u16(cur *c)
{
    if (c->bad || c->len - c->pos < 2) {
        c->bad = true;
        return 0;
    }
    const uint8_t *b = c->p + c->pos;
    c->pos += 2;
    return (uint32_t)b[0] << 8 | b[1];
}

static uint32_t rd_u8(cur *c)
{
    if (c->bad || c->pos >= c->len) {
        c->bad = true;
        return 0;
    }
    return c->p[c->pos++];
}

/* B(s): returns a pointer into the stream and its length */
static const uint8_t *rd_b(cur *c, size_t *len)
{
    const uint32_t l = rd_u32(c);
    if (c->bad || c->len - c->pos < l) {
        c->bad = true;
        *len = 0;
        return NULL;
    }
    const uint8_t *s = c->p + c->pos;
    c->pos += l;
    *len = l;
    return s;
}

/* `count` U32 into out (caller-sized) */
static void rd_words(cur *c, uint32_t *out, uint32_t count)
{
    for (uint32_t i = 0; i < count; ++i) {
        out[i] = rd_u32(c);
    }
}

/* count words into a fresh allocation (NULL on a bad stream; at least one word) */
static uint32_t *rd_alloc(cur *c, uint32_t count)
{
    if (c->bad || (c->len - c->pos) / 4 < count) {
        c->bad = true;
        return NULL;
    }
    uint32_t *w = malloc(((size_t)count + 1) * sizeof *w);
    CHECK(w != NULL);
    rd_words(c, w, count);
    return w;
}

static bool is_bijection(const uint32_t *p, uint32_t n)
{
    uint8_t seen[64] = {0};
    if (n > 64) {
        return false;
    }
    for (uint32_t v = 0; v < n; ++v) {
        if (p[v] >= n || seen[p[v]]) {
            return false;
        }
        seen[p[v]] = 1;
    }
    return true;
}

/* brief 3.5 / docs/certificate-format.md CHAIN, read into a canon_bsgs that canon_bsgs_verify
 * can check: levels, generators (inverses recomputed, INVERSE nodes appended to the
 * provenance), gen_node, provenance, inputs resolved through the references into rows of
 * `ref_rows` (ref_count rows of degree n).  *inputs_out (count *input_count) receives the
 * resolved inputs.  false on a malformed stream (out is then freed). */
static bool read_chain(cur *c, uint32_t n, const canon_perm_table *ref_rows, canon_bsgs *out,
                       uint32_t **inputs_out, uint32_t *input_count, uint32_t **refs_out)
{
    canon_bsgs_init(out, n);
    *inputs_out = NULL;
    *refs_out = NULL;
    *input_count = 0;
    const uint32_t depth = rd_u32(c);
    if (c->bad || depth > n) {
        c->bad = true;
        return false;
    }
    out->levels = calloc((size_t)depth + 1, sizeof *out->levels);
    CHECK(out->levels != NULL);
    out->level_cap = depth + 1;
    out->depth = depth;
    out->order = 1;
    for (uint32_t i = 0; i <= depth; ++i) {
        canon_bsgs_level *L = &out->levels[i];
        uint32_t *block = malloc(4u * ((size_t)n + 1) * sizeof *block);
        CHECK(block != NULL);
        L->orbit = block;
        L->orbit_pos = block + n + 1;
        L->parent_point = block + 2 * ((size_t)n + 1);
        L->parent_gen = block + 3 * ((size_t)n + 1);
        for (uint32_t v = 0; v < n; ++v) {
            L->orbit_pos[v] = NONE;
        }
        L->base_point = NONE;
        if (i == depth) {
            continue; /* terminal level: not stored */
        }
        L->base_point = rd_u32(c);
        L->orbit_len = rd_u32(c);
        if (c->bad || L->orbit_len == 0 || L->orbit_len > n) {
            c->bad = true;
            break;
        }
        rd_words(c, L->orbit, L->orbit_len);
        rd_words(c, L->parent_point, L->orbit_len);
        rd_words(c, L->parent_gen, L->orbit_len);
        for (uint32_t j = 0; j < L->orbit_len && !c->bad; ++j) {
            if (L->orbit[j] >= n || L->orbit_pos[L->orbit[j]] != NONE) {
                c->bad = true; /* out of range or repeated: orbit_pos cannot be built */
            } else {
                L->orbit_pos[L->orbit[j]] = j;
            }
        }
        L->gen_count = rd_u32(c);
        L->gen_ids = rd_alloc(c, L->gen_count);
        L->gen_cap = L->gen_count;
        out->order *= L->orbit_len;
        if (c->bad) {
            break;
        }
    }
    const uint32_t total = c->bad ? 0 : rd_u32(c);
    if (!c->bad && (c->len - c->pos) / 4 / (n > 0 ? n : 1) < total) {
        c->bad = true;
    }
    uint32_t *row = malloc(((size_t)n + 1) * sizeof *row), *inv = malloc(((size_t)n + 1) * 4);
    CHECK(row != NULL && inv != NULL);
    for (uint32_t k = 0; k < total && !c->bad; ++k) {
        rd_words(c, row, n);
        if (!is_bijection(row, n)) {
            c->bad = true;
            break;
        }
        canon_perm_inverse(row, inv, n);
        uint32_t id = 0;
        CHECK(canon_perm_table_push(&out->gens, row, &id) == CANON_COMPLETE);
        CHECK(canon_perm_table_push(&out->invs, inv, &id) == CANON_COMPLETE);
    }
    if (!c->bad) {
        out->gen_node = rd_alloc(c, total);
        out->inv_node = calloc((size_t)total + 1, sizeof *out->inv_node);
        CHECK(out->inv_node != NULL);
        out->node_cap = total;
    }
    const uint32_t prov = c->bad ? 0 : rd_u32(c);
    if (!c->bad && (c->len - c->pos) / 12 < prov) {
        c->bad = true;
    }
    for (uint32_t i = 0; i < prov && !c->bad; ++i) {
        const uint32_t kind = rd_u32(c), a = rd_u32(c), b = rd_u32(c);
        if (kind != CANON_PROV_PRODUCT && b != 0) {
            c->bad = true; /* docs/certificate-format.md: b = 0 for INPUT and INVERSE */
            break;
        }
        if (out->prov.count == out->prov.cap) {
            const uint32_t cap = out->prov.cap * 2 + 8;
            canon_prov_node *nodes = realloc(out->prov.nodes, cap * sizeof *nodes);
            CHECK(nodes != NULL);
            out->prov.nodes = nodes;
            out->prov.cap = cap;
        }
        out->prov.nodes[out->prov.count++] = (canon_prov_node){kind, a, b};
    }
    for (uint32_t k = 0; k < total && !c->bad; ++k) {
        /* the reader's own INVERSE node for each stored generator (validated by verify) */
        if (out->gen_node[k] >= out->prov.count) {
            c->bad = true;
            break;
        }
        CHECK(canon_prov_inverse(&out->prov, out->gen_node[k], &out->inv_node[k]) ==
              CANON_COMPLETE);
    }
    const uint32_t count = c->bad ? 0 : rd_u32(c);
    uint32_t *refs = c->bad ? NULL : rd_alloc(c, count);
    uint32_t *inputs = malloc(((size_t)count * n + 1) * sizeof *inputs);
    CHECK(inputs != NULL);
    for (uint32_t j = 0; j < count && !c->bad; ++j) {
        if (refs[j] >= ref_rows->count) {
            c->bad = true;
            break;
        }
        if (n > 0) {
            memcpy(inputs + (size_t)j * n, canon_perm_table_row(ref_rows, refs[j]), n * 4u);
        }
        uint32_t id = 0;
        CHECK(canon_perm_table_push(&out->inputs, n > 0 ? inputs + (size_t)j * n : NULL, &id) ==
              CANON_COMPLETE);
    }
    free(row);
    free(inv);
    if (c->bad) {
        free(inputs);
        free(refs);
        canon_bsgs_free(out);
        return false;
    }
    *inputs_out = inputs;
    *input_count = count;
    *refs_out = refs;
    return true;
}

/* read_chain then canon_bsgs_verify: true iff the stream parses and the chain verifies. */
static bool read_verified(cur *c, uint32_t n, const canon_perm_table *ref_rows, canon_bsgs *out,
                          uint32_t **refs)
{
    uint32_t *inputs = NULL, count = 0;
    if (!read_chain(c, n, ref_rows, out, &inputs, &count, refs)) {
        return false;
    }
    canon_bsgs_reason reason = CANON_BSGS_UNCHECKED;
    CHECK(canon_bsgs_verify(out, inputs, count, &reason) == CANON_COMPLETE);
    free(inputs);
    if (reason != CANON_BSGS_VALID) {
        free(*refs);
        *refs = NULL;
        canon_bsgs_free(out);
        return false;
    }
    return true;
}

/* every serialised field of `a` equals that of `b` */
static bool same_chain(const canon_bsgs *a, const canon_bsgs *b)
{
    const uint32_t n = a->n;
    bool ok = a->depth == b->depth && a->gens.count == b->gens.count &&
              a->inputs.count == b->inputs.count && a->order == b->order;
    for (uint32_t i = 0; i < a->depth && ok; ++i) {
        const canon_bsgs_level *x = &a->levels[i], *y = &b->levels[i];
        ok = x->base_point == y->base_point && x->orbit_len == y->orbit_len &&
             x->gen_count == y->gen_count &&
             memcmp(x->orbit, y->orbit, x->orbit_len * 4u) == 0 &&
             memcmp(x->parent_point, y->parent_point, x->orbit_len * 4u) == 0 &&
             memcmp(x->parent_gen, y->parent_gen, x->orbit_len * 4u) == 0 &&
             (x->gen_count == 0 || memcmp(x->gen_ids, y->gen_ids, x->gen_count * 4u) == 0);
    }
    for (uint32_t k = 0; k < a->gens.count && ok; ++k) {
        ok = (n == 0 || memcmp(canon_perm_table_row(&a->gens, k),
                               canon_perm_table_row(&b->gens, k), n * 4u) == 0);
    }
    for (uint32_t j = 0; j < a->inputs.count && ok && n > 0; ++j) {
        ok = memcmp(canon_perm_table_row(&a->inputs, j), canon_perm_table_row(&b->inputs, j),
                    n * 4u) == 0;
    }
    return ok;
}

/* ---- helpers ---- */

static canon_group *make_group(int backend, uint32_t n, const uint32_t *gens, uint32_t count)
{
    canon_group *g = NULL;
    CHECK(canon_group_create(CTX[backend], n, gens, count, &g) == CANON_COMPLETE);
    return g;
}

static uint32_t gens_of(const t1_sym *s, const t1_group *grp, int full, uint32_t *gens)
{
    const uint32_t n = s->n;
    if (!full) {
        memcpy(gens, grp->gens, grp->gen_count * n * sizeof *gens);
        return grp->gen_count;
    }
    uint32_t count = 0;
    for (uint32_t e = 0; e < s->count; ++e) {
        if (grp->mask >> e & 1u) {
            memcpy(gens + count++ * n, s->elem[e], n * sizeof *gens);
        }
    }
    return count;
}

static canon_result *solve(canon_context *ctx, const canon_group *g, const canon_object *x,
                           canon_work_policy policy, bool certificate, uint64_t quota,
                           canon_status *st)
{
    canon_capacity cap = {0, 0, quota, 0, 0, 0, 0, policy};
    canon_problem_options opts = {CANON_WITNESS_ANY, NULL, certificate};
    canon_problem *p = NULL;
    canon_result *r = NULL;
    *st = canon_problem_create_with_options(ctx, g, x, NULL, CANON_OBJECTIVE_CANONICAL_IMAGE,
                                            CANON_PROFILE_P1, CANON_ENCODING_CDAG_2,
                                            CANON_ORDER_CDAG_BYTE_1, &cap, &opts, &p);
    if (*st == CANON_COMPLETE) {
        *st = canon_solve(WS, p, &r);
    }
    canon_problem_release(p);
    return r;
}

static bool same_get(const uint8_t *(*get)(const canon_result *, size_t *), const canon_result *a,
                     const canon_result *b)
{
    size_t la = 0, lb = 0;
    const uint8_t *pa = get(a, &la), *pb = get(b, &lb);
    return pa != NULL && pb != NULL && la == lb && memcmp(pa, pb, la) == 0;
}

static bool same_witness(const canon_result *a, const canon_result *b)
{
    uint32_t da = 0, db = 0;
    const uint32_t *wa = canon_result_witness(a, &da), *wb = canon_result_witness(b, &db);
    return wa != NULL && wb != NULL && da == db && (da == 0 || memcmp(wa, wb, da * 4u) == 0);
}

/* ---- C: the structural walk ---- */

typedef struct walk {
    const canon_group *g;
    const canon_root *x;
    uint32_t n;
    canon_bsgs root;           /* the root chain as read */
    canon_perm_table autos;    /* AUT rows */
    canon_p1_scratch scr;      /* replay of the refinement */
    canon_buf trace;
    canon_root_image img;
    uint32_t path[64];
    uint64_t node_records, pruned, leaves;
    bool ok;
} walk;

#define WALK_CHECK(w, cond)                                                                        \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            (w)->ok = false;                                                                       \
            CHECK(cond);                                                                           \
            return;                                                                                \
        }                                                                                          \
    } while (0)

static bool fixes_x(walk *w, const uint32_t *a)
{
    CHECK(canon_root_act_into(w->x, a, &w->img) == CANON_COMPLETE);
    const bool eq = canon_root_equal(&w->img.root, w->x);
    canon_root_image_clear(&w->img);
    return eq;
}

static bool in_group(const walk *w, const uint32_t *a)
{
    bool in = false;
    CHECK(w->g->ops->contains(w->g, a, &in) == CANON_COMPLETE);
    return in;
}

/* One NODE record; p is the node's partition before refinement (replayed by the engine's own
 * refinement to know the target cell). */
static void walk_node(walk *w, cur *c, canon_partition *p, uint32_t depth, uint32_t atom)
{
    const uint32_t n = w->n;
    WALK_CHECK(w, rd_u32(c) == depth && rd_u32(c) == atom && !c->bad);
    w->node_records += 1;
    canon_buf_truncate(&w->trace, 0);
    WALK_CHECK(w, canon_p1_refine_node(p, w->g, w->x, depth, &w->trace, &w->scr) ==
                      CANON_COMPLETE);
    const uint32_t sweeps = rd_u32(c);
    WALK_CHECK(w, !c->bad && sweeps >= 1 && sweeps <= n + 1);
    uint32_t sizes[64], ksizes = 0, last_u[64], last_f = 0;
    for (uint32_t s = 0; s < sweeps; ++s) {
        /* sizes after O: each sums to n; F = the singletons after O */
        const uint32_t ko = rd_u32(c);
        WALK_CHECK(w, !c->bad && ko <= n && (n == 0 || ko >= 1));
        uint32_t so[64], sum = 0, singles = 0;
        rd_words(c, so, ko);
        for (uint32_t i = 0; i < ko; ++i) {
            sum += so[i];
            singles += so[i] == 1;
        }
        WALK_CHECK(w, !c->bad && sum == n);
        const uint32_t f = rd_u32(c);
        WALK_CHECK(w, !c->bad && f == singles);
        uint32_t F[64], u[64], M[64];
        rd_words(c, F, f);
        rd_words(c, u, n);
        rd_words(c, M, f);
        WALK_CHECK(w, !c->bad && is_bijection(u, n) && in_group(w, u));
        for (uint32_t i = 0; i < f; ++i) {
            WALK_CHECK(w, F[i] < n && M[i] == u[F[i]]); /* M = F^u */
        }
        /* the rebased chain: verifies against the root chain's level-0 generators (by
         * reference), base prefix M, M[i] least in level i's orbit (spec 7.2 minimality) */
        canon_bsgs reb;
        uint32_t *refs = NULL;
        WALK_CHECK(w, read_verified(c, n, &w->root.gens, &reb, &refs));
        bool ok = reb.inputs.count == w->root.levels[0].gen_count && reb.depth >= f &&
                  reb.order == w->root.order;
        for (uint32_t j = 0; j < reb.inputs.count && ok; ++j) {
            ok = refs[j] == w->root.levels[0].gen_ids[j];
        }
        for (uint32_t i = 0; i < f && ok; ++i) {
            ok = reb.levels[i].base_point == M[i];
            for (uint32_t j = 0; j < reb.levels[i].orbit_len && ok; ++j) {
                ok = reb.levels[i].orbit[j] >= M[i];
            }
        }
        free(refs);
        canon_bsgs_free(&reb);
        WALK_CHECK(w, ok);
        ksizes = rd_u32(c);
        WALK_CHECK(w, !c->bad && ksizes <= n);
        rd_words(c, sizes, ksizes);
        sum = 0;
        for (uint32_t i = 0; i < ksizes; ++i) {
            sum += sizes[i];
        }
        WALK_CHECK(w, !c->bad && sum == n && ksizes >= ko);
        memcpy(last_u, u, n * 4u);
        last_f = f;
    }
    /* the replayed partition after refinement has the last recorded sizes */
    WALK_CHECK(w, p->cells == ksizes);
    for (uint32_t i = 0; i < ksizes; ++i) {
        WALK_CHECK(w, canon_partition_cell_size(p, i) == sizes[i]);
    }
    const uint32_t tag = rd_u8(c);
    if (tag == CANON_CERT_LEAF) {
        /* rule 6: discrete, t = the last sweep's u (F = L) */
        uint32_t t[64];
        rd_words(c, t, n);
        WALK_CHECK(w, !c->bad && ksizes == n && last_f == n && memcmp(t, last_u, n * 4u) == 0);
        w->leaves += 1;
        return;
    }
    WALK_CHECK(w, tag == CANON_CERT_BRANCH);
    const uint32_t cell = rd_u32(c), size = rd_u32(c), children = rd_u32(c);
    WALK_CHECK(w, !c->bad && cell < ksizes && size == sizes[cell] && children == size);
    for (uint32_t i = 0; i < ksizes; ++i) { /* (size, position)-minimal non-singleton */
        WALK_CHECK(w, sizes[i] < 2 || sizes[i] > size || (sizes[i] == size && i >= cell));
    }
    uint32_t members[64];
    memcpy(members, p->lab + p->start[cell], size * 4u);
    for (uint32_t i = 1; i < size; ++i) { /* insertion sort */
        for (uint32_t j = i; j > 0 && members[j - 1] > members[j]; --j) {
            const uint32_t tmp = members[j];
            members[j] = members[j - 1];
            members[j - 1] = tmp;
        }
    }
    size_t words = 0;
    CHECK(canon_partition_snapshot_words(n, &words) == CANON_COMPLETE);
    uint32_t *snap = malloc((words + 1) * sizeof *snap);
    CHECK(snap != NULL);
    canon_partition_save(p, snap);
    bool explored[64] = {false};
    for (uint32_t k = 0; k < children && w->ok; ++k) {
        const uint32_t etag = rd_u8(c), b = rd_u32(c);
        if (c->bad || b != members[k]) { /* the cell, once, in increasing atom id */
            w->ok = false;
            CHECK(!c->bad && b == members[k]);
            break;
        }
        if (etag == CANON_CERT_EXPLORED) {
            explored[b] = true;
            canon_partition_restore(p, snap);
            canon_partition_individualise(p, cell, b);
            w->path[depth] = b;
            walk_node(w, c, p, depth + 1, b);
            continue;
        }
        const uint32_t rep = rd_u32(c), index = rd_u32(c);
        w->pruned += 1;
        bool ok = etag == CANON_CERT_PRUNED && !c->bad && rep < n && explored[rep] &&
                  index < w->autos.count;
        if (ok) {
            const uint32_t *a = canon_perm_table_row(&w->autos, index);
            ok = a[rep] == b;
            for (uint32_t i = 0; i < depth && ok; ++i) {
                ok = a[w->path[i]] == w->path[i]; /* fixes the prefix pointwise */
            }
        }
        if (!ok) {
            w->ok = false;
            CHECK(ok);
        }
    }
    free(snap);
}

typedef struct expect {
    canon_work_policy policy;
    uint64_t nodes, pruned; /* the internal search's counts */
} expect;

/* Walk a whole certificate; returns its size. */
static size_t walk_cert(const canon_group *g, const canon_root *x, const canon_result *r,
                        const expect *e)
{
    const uint8_t *bytes = NULL;
    size_t len = 0;
    CHECK(canon_result_certificate(r, &bytes, &len) == CANON_COMPLETE && bytes != NULL);
    walk w;
    memset(&w, 0, sizeof w);
    w.g = g;
    w.x = x;
    w.n = x->n;
    w.ok = true;
    const uint32_t n = x->n;
    CHECK(n < 64);
    canon_p1_scratch_init(&w.scr);
    CHECK(canon_p1_scratch_reserve(&w.scr, x) == CANON_COMPLETE);
    canon_buf_init(&w.trace);
    canon_root_image_init(&w.img);
    canon_perm_table_init(&w.autos, n);
    cur c = {bytes, len, 0, false};
    /* header (rule 1) */
    CHECK(len >= 4 && memcmp(bytes, "CNC0", 4) == 0);
    c.pos = 4;
    CHECK(rd_u16(&c) == 0x0001 && rd_u16(&c) == 0x0001 && rd_u16(&c) == e->policy &&
          rd_u16(&c) == 0x0002 && rd_u16(&c) == 0x0001 && rd_u32(&c) == n);
    canon_buf xs;
    canon_buf_init(&xs);
    CHECK(canon_root_stream_write(x, &xs) == CANON_COMPLETE);
    size_t xl = 0;
    const uint8_t *xb = rd_b(&c, &xl);
    CHECK(xb != NULL && xl == xs.len && memcmp(xb, xs.data, xl) == 0);
    canon_buf_free(&xs);
    const canon_perm_table *in = canon_group_input_generators(g);
    const uint32_t k = rd_u32(&c);
    CHECK(in != NULL && k == in->count);
    canon_perm_table gen;
    canon_perm_table_init(&gen, n);
    uint32_t row[64];
    for (uint32_t i = 0; i < k && !c.bad; ++i) {
        rd_words(&c, row, n);
        uint32_t id = 0;
        CHECK(canon_perm_table_push(&gen, row, &id) == CANON_COMPLETE);
        CHECK(n == 0 || memcmp(row, canon_perm_table_row(in, i), n * 4u) == 0);
    }
    /* root chain: verifies against the header generators, inputs by reference j -> j */
    uint32_t *refs = NULL;
    CHECK(read_verified(&c, n, &gen, &w.root, &refs));
    for (uint32_t j = 0; j < w.root.inputs.count; ++j) {
        CHECK(refs[j] == j);
    }
    CHECK(w.root.inputs.count == k && w.root.order == g->ops->order(g));
    free(refs);
    /* AUT (rule 2): members of G fixing x */
    const uint32_t a = rd_u32(&c);
    for (uint32_t i = 0; i < a && !c.bad; ++i) {
        rd_words(&c, row, n);
        uint32_t id = 0;
        CHECK(!c.bad && is_bijection(row, n) && in_group(&w, row) && fixes_x(&w, row));
        CHECK(canon_perm_table_push(&w.autos, row, &id) == CANON_COMPLETE);
    }
    /* the nodes, from the root partition of x (spec 7.1) */
    canon_partition p;
    CHECK(canon_partition_init(&p, n) == CANON_COMPLETE);
    canon_p1_initial(&p, x, &w.scr);
    walk_node(&w, &c, &p, 0, NONE);
    canon_partition_free(&p);
    CHECK(w.ok);
    /* FINAL (rule 7) */
    size_t tl = 0, bl = 0, rtl = 0, rbl = 0;
    const uint8_t *tr = rd_b(&c, &tl), *by = rd_b(&c, &bl);
    const uint8_t *rt = canon_result_trace(r, &rtl), *rb = canon_result_bytes(r, &rbl);
    CHECK(tr != NULL && by != NULL && tl == rtl && bl == rbl && memcmp(tr, rt, tl) == 0 &&
          memcmp(by, rb, bl) == 0);
    rd_words(&c, row, n);
    uint32_t deg = 0;
    const uint32_t *wit = canon_result_witness(r, &deg);
    CHECK(!c.bad && wit != NULL && deg == n && (n == 0 || memcmp(row, wit, n * 4u) == 0));
    const uint32_t nb = rd_u32(&c);
    uint64_t count = 0;
    for (uint32_t i = 0; i < nb && nb <= 8; ++i) {
        count = count << 8 | rd_u8(&c);
    }
    CHECK(!c.bad && nb <= 8 && count == w.node_records && c.pos == len);
    /* explored count = the engine's NODE count; pruned entries = its pruned children */
    CHECK(w.node_records == e->nodes && w.pruned == e->pruned);
    CHECK(e->policy == PRUNE || w.pruned == 0);
    canon_perm_table_free(&gen);
    canon_perm_table_free(&w.autos);
    canon_bsgs_free(&w.root);
    canon_root_image_free(&w.img);
    canon_buf_free(&w.trace);
    canon_p1_scratch_free(&w.scr);
    return len;
}

typedef struct tier {
    uint64_t cases, bytes, max_bytes, nodes, pruned;
} tier;

/* Solve x under the chain and explicit groups with a certificate, under `policy`: walk the
 * certificate (C), compare with the run without one (D), across backends and repeats (D). */
static void certify(canon_group *const gs[2], const canon_object *xo, const canon_root *x,
                    canon_work_policy policy, tier *t)
{
    canon_p1_search s;
    canon_p1_search_init(&s);
    CHECK(canon_p1_search_run_policy(&s, gs[0], x, UINT64_MAX, policy) == CANON_COMPLETE);
    const expect e = {policy, s.nodes, s.pruned};
    canon_p1_search_free(&s);
    canon_status st;
    canon_result *r[2], *plain = solve(CTX[0], gs[0], xo, policy, false, 0, &st);
    CHECK(st == CANON_COMPLETE);
    for (int b = 0; b < 2; ++b) {
        r[b] = solve(CTX[b], gs[b], xo, policy, true, 0, &st);
        CHECK(st == CANON_COMPLETE);
        const size_t len = walk_cert(gs[b], x, r[b], &e);
        CHECK(same_get(canon_result_trace, plain, r[b]) &&
              same_get(canon_result_bytes, plain, r[b]) && same_witness(plain, r[b]));
        if (b == 0) {
            t->cases += 1;
            t->bytes += len;
            t->max_bytes = len > t->max_bytes ? len : t->max_bytes;
            t->nodes += e.nodes;
            t->pruned += e.pruned;
        }
    }
    canon_result *again = solve(CTX[0], gs[0], xo, policy, true, 0, &st);
    const uint8_t *c0 = NULL, *c1 = NULL, *c2 = NULL;
    size_t l0 = 0, l1 = 0, l2 = 0;
    CHECK(canon_result_certificate(r[0], &c0, &l0) == CANON_COMPLETE &&
          canon_result_certificate(r[1], &c1, &l1) == CANON_COMPLETE &&
          canon_result_certificate(again, &c2, &l2) == CANON_COMPLETE);
    CHECK(l0 == l1 && l0 == l2 && memcmp(c0, c1, l0) == 0 && memcmp(c0, c2, l0) == 0);
    canon_result_release(plain);
    canon_result_release(r[0]);
    canon_result_release(r[1]);
    canon_result_release(again);
}

static void print_tier(const char *name, canon_work_policy policy, const tier *t)
{
    printf("%s, policy 0x%04x: %llu cases, %llu NODE records, %llu pruned entries, "
           "certificate bytes total %llu, max %llu\n",
           name, (unsigned)policy, (unsigned long long)t->cases, (unsigned long long)t->nodes,
           (unsigned long long)t->pruned, (unsigned long long)t->bytes,
           (unsigned long long)t->max_bytes);
}

static void subset_root(canon_root *x, uint32_t n, const uint32_t *atoms, uint32_t k)
{
    memset(x, 0, sizeof *x);
    x->kind = CANON_ROOT_SUBSET;
    x->n = n;
    CHECK(canon_subset_init(&x->u.subset, n, atoms, k) == CANON_COMPLETE);
}

static void test_t1_subsets(void)
{
    for (int pol = 0; pol < 2; ++pol) {
        const canon_work_policy policy = pol == 0 ? REF : PRUNE;
        tier t;
        memset(&t, 0, sizeof t);
        for (uint32_t n = 0; n <= 4; ++n) {
            t1_sym sym;
            t1_sym_init(&sym, n);
            t1_group groups[T1_MAX_GROUPS];
            const uint32_t gcount = t1_subgroups(&sym, groups);
            for (uint32_t gi = 0; gi < gcount; ++gi) {
                for (int full = 0; full < 2; ++full) {
                    uint32_t gens[24 * 4];
                    const uint32_t count = gens_of(&sym, &groups[gi], full, gens);
                    canon_group *gs[2] = {make_group(0, n, gens, count),
                                          make_group(1, n, gens, count)};
                    for (uint32_t mask = 0; mask < 1u << n; ++mask) {
                        uint32_t atoms[4], k = 0;
                        for (uint32_t v = 0; v < n; ++v) {
                            if (mask >> v & 1u) {
                                atoms[k++] = v;
                            }
                        }
                        canon_object *xo = NULL;
                        CHECK(canon_object_create_subset(CTX[0], n, atoms, k, &xo) ==
                              CANON_COMPLETE);
                        canon_root x;
                        subset_root(&x, n, atoms, k);
                        certify(gs, xo, &x, policy, &t);
                        canon_root_free(&x);
                        canon_object_release(xo);
                    }
                    canon_group_release(gs[0]);
                    canon_group_release(gs[1]);
                }
            }
        }
        print_tier("T1 subsets (every group, both generating sets)", policy, &t);
    }
}

/* G1-like: random coloured, labelled directed multigraphs on n <= 3 vertices under every T1
 * group (greedy generators), with loops and multiplicities. */
static void test_g1_graphs(void)
{
    static const uint8_t lab_a[1] = {'a'};
    const uint8_t *labels[2] = {NULL, lab_a};
    for (int pol = 0; pol < 2; ++pol) {
        const canon_work_policy policy = pol == 0 ? REF : PRUNE;
        tier t;
        memset(&t, 0, sizeof t);
        for (uint32_t n = 1; n <= 3; ++n) {
            t1_sym sym;
            t1_sym_init(&sym, n);
            t1_group groups[T1_MAX_GROUPS];
            const uint32_t gcount = t1_subgroups(&sym, groups);
            for (uint32_t gi = 0; gi < gcount; ++gi) {
                canon_group *gs[2] = {
                    make_group(0, n, groups[gi].gens, groups[gi].gen_count),
                    make_group(1, n, groups[gi].gens, groups[gi].gen_count)};
                for (int round = 0; round < 8; ++round) {
                    canon_arc arcs[6];
                    const size_t count = (size_t)(check_rng() % 6);
                    for (size_t i = 0; i < count; ++i) {
                        const size_t l = (size_t)(check_rng() % 2);
                        arcs[i] =
                            (canon_arc){(uint32_t)(check_rng() % n), (uint32_t)(check_rng() % n),
                                        labels[l], l, 1 + check_rng() % 2};
                    }
                    const uint8_t *colours[3];
                    size_t lengths[3];
                    for (uint32_t v = 0; v < n; ++v) {
                        lengths[v] = (size_t)(check_rng() % 2);
                        colours[v] = labels[lengths[v]];
                    }
                    const bool coloured = round % 3 == 0;
                    canon_object *xo = NULL;
                    CHECK(canon_object_create_graph(CTX[0], n, coloured ? colours : NULL,
                                                    coloured ? lengths : NULL, arcs, count,
                                                    &xo) == CANON_COMPLETE);
                    canon_root x;
                    memset(&x, 0, sizeof x);
                    x.kind = CANON_ROOT_GRAPH;
                    x.n = n;
                    CHECK(canon_graph_init(&x.u.graph, n, coloured ? colours : NULL,
                                           coloured ? lengths : NULL, arcs,
                                           count) == CANON_COMPLETE);
                    certify(gs, xo, &x, policy, &t);
                    canon_root_free(&x);
                    canon_object_release(xo);
                }
                canon_group_release(gs[0]);
                canon_group_release(gs[1]);
            }
        }
        print_tier("G1 graphs (random, every T1 group, n = 1..3)", policy, &t);
    }
}

/* ---- A: chain round trip ---- */

/* Serialise ch (input refs `refs`), read it back resolving refs into `ref_rows`, compare and
 * verify; then flip every byte once and require rejection. */
static void round_trip(const canon_bsgs *ch, const uint32_t *refs, const canon_perm_table *rows,
                       bool mutate, uint64_t *mutations, uint64_t *equivalent)
{
    canon_buf b;
    canon_buf_init(&b);
    CHECK(canon_cert_chain_write(&b, ch, refs) == CANON_COMPLETE);
    cur c = {b.data, b.len, 0, false};
    canon_bsgs back;
    uint32_t *rr = NULL;
    CHECK(read_verified(&c, ch->n, rows, &back, &rr) && c.pos == b.len);
    CHECK(same_chain(ch, &back));
    for (uint32_t j = 0; j < ch->inputs.count; ++j) {
        CHECK(rr[j] == (refs != NULL ? refs[j] : j));
    }
    free(rr);
    canon_bsgs_free(&back);
    for (size_t i = 0; mutate && i < b.len; ++i) {
        b.data[i] ^= 0x01u;
        cur m = {b.data, b.len, 0, false};
        canon_bsgs mb;
        uint32_t *mr = NULL;
        bool accepted = read_verified(&m, ch->n, rows, &mb, &mr) && m.pos == b.len;
        /* the reference rule (docs/certificate-format.md): input j is GEN j (root chain) or
         * the root chain's level-0 generator j (rebased chain) */
        for (uint32_t j = 0; accepted && j < ch->inputs.count; ++j) {
            accepted = mr[j] == (refs != NULL ? refs[j] : j);
        }
        /* accepted only as an equivalent chain: a gen_node or provenance operand moved to
         * another record deriving the same permutation (the verifier re-derives, it does not
         * compare record numbers); every serialised chain field is then unchanged */
        if (accepted) {
            CHECK(same_chain(ch, &mb));
            *equivalent += 1;
        }
        if (mr != NULL || accepted) {
            free(mr);
            canon_bsgs_free(&mb);
        }
        *mutations += 1;
        b.data[i] ^= 0x01u;
    }
    canon_buf_free(&b);
}

static void test_chain_round_trip(void)
{
    uint64_t chains = 0, mutations = 0, equivalent = 0;
    for (uint32_t n = 0; n <= 4; ++n) {
        t1_sym sym;
        t1_sym_init(&sym, n);
        t1_group groups[T1_MAX_GROUPS];
        const uint32_t gcount = t1_subgroups(&sym, groups);
        for (uint32_t gi = 0; gi < gcount; ++gi) {
            for (int full = 0; full < 2; ++full) {
                uint32_t gens[24 * 4];
                const uint32_t count = gens_of(&sym, &groups[gi], full, gens);
                canon_bsgs root;
                CHECK(canon_bsgs_build_verified(&root, n, gens, count) == CANON_COMPLETE);
                canon_perm_table rows;
                canon_perm_table_init(&rows, n);
                for (uint32_t i = 0; i < count; ++i) {
                    uint32_t id = 0;
                    CHECK(canon_perm_table_push(&rows, n > 0 ? gens + i * n : NULL, &id) ==
                          CANON_COMPLETE);
                }
                /* mutations on a sample: Sym(3) and the full Sym(4) generating set */
                const bool mutate = !full && (groups[gi].order == 6 || groups[gi].order == 24);
                round_trip(&root, NULL, &rows, mutate, &mutations, &equivalent);
                chains += 1;
                for (int trial = 0; trial < 4; ++trial) {
                    /* a random injective prefix of random length */
                    uint32_t perm[4] = {0, 1, 2, 3}, len = n > 0 ? (uint32_t)(check_rng() % (n + 1))
                                                                 : 0;
                    for (uint32_t v = n; v > 1; --v) {
                        const uint32_t j = (uint32_t)(check_rng() % v), tmp = perm[v - 1];
                        perm[v - 1] = perm[j];
                        perm[j] = tmp;
                    }
                    canon_bsgs reb;
                    CHECK(canon_bsgs_rebase(&root, 0, perm, len, false, &reb) == CANON_COMPLETE);
                    CHECK(reb.inputs.count == root.levels[0].gen_count);
                    round_trip(&reb, root.levels[0].gen_ids, &root.gens,
                               mutate && trial == 0, &mutations, &equivalent);
                    canon_bsgs_free(&reb);
                    chains += 1;
                }
                canon_perm_table_free(&rows);
                canon_bsgs_free(&root);
            }
        }
    }
    printf("chain round trip: %llu chains, %llu single-byte mutations, %llu rejected, %llu "
           "accepted as an equivalent derivation\n",
           (unsigned long long)chains, (unsigned long long)mutations,
           (unsigned long long)(mutations - equivalent), (unsigned long long)equivalent);
}

/* ---- B: the spec 7.4 cases ---- */

/* The header's policy field and the walk; returns NODE records and PRUNED entries. */
static void case_74(const char *name, uint32_t n, const uint32_t *gens, uint32_t count,
                    bool graph, const uint32_t *atoms, uint32_t k, uint64_t nodes_ref,
                    uint64_t nodes_pruned, uint64_t pruned)
{
    canon_group *gs[2] = {make_group(0, n, gens, count), make_group(1, n, gens, count)};
    canon_object *xo = NULL;
    canon_root x;
    memset(&x, 0, sizeof x);
    const canon_arc arc = {0, 1, NULL, 0, 1};
    if (graph) {
        CHECK(canon_object_create_graph(CTX[0], n, NULL, NULL, &arc, 1, &xo) == CANON_COMPLETE);
        x.kind = CANON_ROOT_GRAPH;
        x.n = n;
        CHECK(canon_graph_init(&x.u.graph, n, NULL, NULL, &arc, 1) == CANON_COMPLETE);
    } else {
        CHECK(canon_object_create_subset(CTX[0], n, atoms, k, &xo) == CANON_COMPLETE);
        subset_root(&x, n, atoms, k);
    }
    for (int pol = 0; pol < 2; ++pol) {
        tier t;
        memset(&t, 0, sizeof t);
        certify(gs, xo, &x, pol == 0 ? REF : PRUNE, &t);
        CHECK(t.nodes == (pol == 0 ? nodes_ref : nodes_pruned));
        CHECK(t.pruned == (pol == 0 ? 0 : pruned));
        printf("spec 7.4 %s, policy %d: %llu bytes\n", name, pol + 1,
               (unsigned long long)t.bytes);
    }
    canon_root_free(&x);
    canon_object_release(xo);
    canon_group_release(gs[0]);
    canon_group_release(gs[1]);
}

static void test_spec_74(void)
{
    const uint32_t swap[2] = {1, 0}, zero = 0;
    /* n = 0: the root is a leaf (spec 7.2) */
    case_74("n=0 empty, G=1", 0, NULL, 0, false, NULL, 0, 1, 1, 0);
    /* n = 2, x = {0}: the initial key separates 0 from 1, so the root is discrete */
    case_74("n=2 {0}, Sym(2)", 2, swap, 1, false, &zero, 1, 1, 1, 0);
    case_74("n=2 {0}, G=1", 2, NULL, 0, false, &zero, 1, 1, 1, 0);
    /* n = 2, x = empty, Sym(2): one cell of size 2, two children; under 0x0002 (0 1) fixes x
     * and the empty prefix, so child 1 is pruned with representative 0 */
    case_74("n=2 empty, Sym(2)", 2, swap, 1, false, NULL, 0, 3, 2, 1);
    /* n = 2, arc 0 -> 1, Sym(2): the O stage separates source and target */
    case_74("n=2 arc 0->1, Sym(2)", 2, swap, 1, true, NULL, 0, 1, 1, 0);
    /* the PRUNED entry of the empty subset under Sym(2): automorphism (0 1), rep 0, b 1 */
    canon_group *g = make_group(0, 2, swap, 1);
    canon_object *xo = NULL;
    CHECK(canon_object_create_subset(CTX[0], 2, NULL, 0, &xo) == CANON_COMPLETE);
    canon_status st;
    canon_result *r = solve(CTX[0], g, xo, PRUNE, true, 0, &st);
    const uint8_t *b = NULL;
    size_t len = 0;
    CHECK(st == CANON_COMPLETE && canon_result_certificate(r, &b, &len) == CANON_COMPLETE);
    /* find U32(a) = 1 followed by the row (1, 0) right after the header and root chain: scan
     * for the PRUNED entry 01 U32(1) U32(0) U32(0) instead (b = 1, rep = 0, index 0) */
    static const uint8_t entry[13] = {1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0};
    bool found = false;
    for (size_t i = 0; i + sizeof entry <= len && !found; ++i) {
        found = memcmp(b + i, entry, sizeof entry) == 0;
    }
    CHECK(found);
    canon_result_release(r);
    canon_object_release(xo);
    canon_group_release(g);
    /* spec 7.4 nested case n = 0, x = (b, b): a certificate is unsupported in v0 */
    static const uint8_t bb[] = {0x43, 0x4e, 0x02, 0x00, 0x01, 0x00, 0x01, 0, 0, 0, 0, 0,
                                 0,    0,    2,    2,    0,    0,    0,    0, 3, 0, 0, 0,
                                 2,    0,    0,    0,    0,    0,    0,    0, 0, 0, 0, 0, 1};
    canon_object *dag = NULL;
    CHECK(canon_object_create(CTX[0], CANON_SCHEMA_EXT_DAG_1, CANON_ACTION_ATOM_TRANSPORT_1, 0,
                              bb, sizeof bb, &dag) == CANON_COMPLETE);
    canon_group *g0 = make_group(0, 0, NULL, 0);
    r = solve(CTX[0], g0, dag, PRUNE, true, 0, &st);
    CHECK(st == CANON_UNSUPPORTED_ACTION && r == NULL);
    r = solve(CTX[0], g0, dag, PRUNE, false, 0, &st);
    CHECK(st == CANON_COMPLETE);
    CHECK(canon_result_certificate(r, &b, &len) == CANON_INVALID_INPUT && b == NULL && len == 0);
    canon_result_release(r);
    canon_object_release(dag);
    canon_group_release(g0);
}

/* ---- E: errors and ownership ---- */

static void test_errors(void)
{
    const uint32_t sym4[8] = {1, 0, 2, 3, 1, 2, 3, 0};
    canon_group *g = make_group(0, 4, sym4, 2);
    canon_object *x = NULL, *y = NULL;
    CHECK(canon_object_create_subset(CTX[0], 4, NULL, 0, &x) == CANON_COMPLETE);
    CHECK(canon_object_create_subset(CTX[0], 4, NULL, 0, &y) == CANON_COMPLETE);
    const canon_problem_options cert = {CANON_WITNESS_ANY, NULL, true},
                                det = {CANON_WITNESS_DETERMINISTIC, NULL, true};
    const uint32_t rho[4] = {0, 1, 2, 3};
    const canon_problem_options lab = {CANON_WITNESS_ANY, rho, true};
    canon_problem *p = NULL;
    /* other objectives (each otherwise valid) */
    CHECK(canon_problem_create_with_options(CTX[0], g, x, NULL, CANON_OBJECTIVE_LEX_MIN_IMAGE,
                                            CANON_PROFILE_NO_TREE, CANON_ENCODING_CDAG_2,
                                            CANON_ORDER_CDAG_BYTE_1, NULL, &cert,
                                            &p) == CANON_UNSUPPORTED_ACTION &&
          p == NULL);
    CHECK(canon_problem_create_with_options(CTX[0], g, x, NULL, CANON_OBJECTIVE_STABILISER,
                                            CANON_PROFILE_NO_TREE, CANON_ENCODING_CDAG_2,
                                            CANON_ORDER_CDAG_BYTE_1, NULL, &cert,
                                            &p) == CANON_UNSUPPORTED_ACTION);
    CHECK(canon_problem_create_with_options(CTX[0], g, x, y, CANON_OBJECTIVE_TRANSPORTER_COSET,
                                            CANON_PROFILE_NO_TREE, CANON_ENCODING_CDAG_2,
                                            CANON_ORDER_CDAG_BYTE_1, NULL, &cert,
                                            &p) == CANON_UNSUPPORTED_ACTION);
    CHECK(canon_problem_create_with_options(
              CTX[0], g, x, NULL, CANON_OBJECTIVE_CANONICAL_LABELING_COSET, CANON_PROFILE_P1,
              CANON_ENCODING_CDAG_2, CANON_ORDER_CDAG_BYTE_1, NULL, &lab,
              &p) == CANON_UNSUPPORTED_ACTION);
    /* the deterministic witness */
    CHECK(canon_problem_create_with_options(CTX[0], g, x, NULL, CANON_OBJECTIVE_CANONICAL_IMAGE,
                                            CANON_PROFILE_P1, CANON_ENCODING_CDAG_2,
                                            CANON_ORDER_CDAG_BYTE_1, NULL, &det,
                                            &p) == CANON_UNSUPPORTED_ACTION);
    /* the order: an unknown work policy is refused first, a degree mismatch after */
    const canon_capacity bad = {0, 0, 0, 0, 0, 0, 0, 7};
    CHECK(canon_problem_create_with_options(CTX[0], g, x, NULL, CANON_OBJECTIVE_STABILISER,
                                            CANON_PROFILE_NO_TREE, CANON_ENCODING_CDAG_2,
                                            CANON_ORDER_CDAG_BYTE_1, &bad, &cert,
                                            &p) == CANON_UNSUPPORTED_ACTION);
    canon_group *g3 = make_group(0, 3, NULL, 0);
    CHECK(canon_problem_create_with_options(CTX[0], g3, x, NULL, CANON_OBJECTIVE_LEX_MIN_IMAGE,
                                            CANON_PROFILE_NO_TREE, CANON_ENCODING_CDAG_2,
                                            CANON_ORDER_CDAG_BYTE_1, NULL, &cert,
                                            &p) == CANON_UNSUPPORTED_ACTION);
    CHECK(canon_problem_create_with_options(CTX[0], g3, x, NULL, CANON_OBJECTIVE_CANONICAL_IMAGE,
                                            CANON_PROFILE_P1, CANON_ENCODING_CDAG_2,
                                            CANON_ORDER_CDAG_BYTE_1, NULL, &cert,
                                            &p) == CANON_INVALID_INPUT);
    canon_group_release(g3);
    /* the accessor */
    const uint8_t *b = (const uint8_t *)"x";
    size_t len = 9;
    CHECK(canon_result_certificate(NULL, &b, &len) == CANON_INVALID_INPUT && b == NULL &&
          len == 0);
    canon_status st;
    canon_result *plain = solve(CTX[0], g, x, PRUNE, false, 0, &st);
    CHECK(st == CANON_COMPLETE);
    CHECK(canon_result_certificate(plain, NULL, &len) == CANON_INVALID_INPUT);
    CHECK(canon_result_certificate(plain, &b, NULL) == CANON_INVALID_INPUT);
    CHECK(canon_result_certificate(plain, &b, &len) == CANON_INVALID_INPUT && b == NULL);
    canon_result_release(plain);
    /* a quota stop discards the partial certificate; the same workspace then gives the
     * certificate of a fresh workspace, and it survives the workspace's release */
    for (uint64_t quota = 1; quota <= 4; ++quota) {
        canon_result *r = solve(CTX[0], g, x, REF, true, quota, &st);
        CHECK(st == CANON_CAPACITY_LIMIT && canon_result_status(r) == CANON_CAPACITY_LIMIT);
        CHECK(canon_result_certificate(r, &b, &len) == CANON_INVALID_INPUT && b == NULL);
        canon_result_release(r);
    }
    canon_result *used = solve(CTX[0], g, x, PRUNE, true, 0, &st);
    CHECK(st == CANON_COMPLETE);
    canon_workspace *saved = WS;
    CHECK(canon_workspace_create(CTX[0], &WS) == CANON_COMPLETE);
    canon_result *fresh = solve(CTX[0], g, x, PRUNE, true, 0, &st);
    CHECK(st == CANON_COMPLETE);
    canon_workspace_release(WS);
    WS = saved;
    const uint8_t *b1 = NULL, *b2 = NULL;
    size_t l1 = 0, l2 = 0;
    CHECK(canon_result_certificate(used, &b1, &l1) == CANON_COMPLETE &&
          canon_result_certificate(fresh, &b2, &l2) == CANON_COMPLETE && l1 == l2 &&
          memcmp(b1, b2, l1) == 0);
    canon_result_release(used);
    canon_result_release(fresh);
    canon_object_release(x);
    canon_object_release(y);
    canon_group_release(g);
}

int main(void)
{
    const canon_context_options chain = {CANON_BACKEND_CHAIN};
    const canon_context_options explicit_backend = {CANON_BACKEND_EXPLICIT};
    CHECK(canon_context_create_with_options(NULL, &chain, &CTX[0]) == CANON_COMPLETE);
    CHECK(canon_context_create_with_options(NULL, &explicit_backend, &CTX[1]) == CANON_COMPLETE);
    CHECK(canon_workspace_create(CTX[0], &WS) == CANON_COMPLETE);
    test_chain_round_trip();
    test_spec_74();
    test_errors();
    test_t1_subsets();
    test_g1_graphs();
    canon_workspace_release(WS);
    canon_context_release(CTX[0]);
    canon_context_release(CTX[1]);
    return check_finish("test_certificate");
}
