/* Certificate v0 writer, CERT-0 (slice S7 step 2; docs/slices/S7.md 3.3, 3.5; grammar in
 * docs/certificate-format.md).  Every field is written by the wire primitives of spec 4.1
 * (U8 tags, U16 identifiers, U32 big-endian words, B(s), Nat(k)); no struct is dumped
 * (spec 17). */
#include "search/certificate.h"

#include <stdlib.h>
#include <string.h>

#include "arena/alloc.h"
#include "partition/partition.h"
#include "perm/perm.h"

#define NONE CANON_BSGS_NONE /* U32 0xFFFFFFFF: "no atom" at the root, no Schreier parent */

void canon_cert_init(canon_cert *c)
{
    memset(c, 0, sizeof *c);
    canon_buf_init(&c->head);
    canon_buf_init(&c->nodes);
    canon_buf_init(&c->out);
    canon_perm_table_init(&c->autos, 0);
    canon_bsgs_init(&c->root, 0);
    canon_root_image_init(&c->img);
}

static void free_scratch(canon_cert *c)
{
    free(c->path);
    free(c->last_u);
    free(c->M);
    free(c->ids);
    free(c->scratch);
    c->path = c->last_u = c->M = c->ids = c->scratch = NULL;
    c->cap = 0;
}

void canon_cert_end(canon_cert *c)
{
    if (c->have_root) {
        canon_bsgs_free(&c->root);
        c->have_root = false;
    }
    canon_root_image_clear(&c->img);
    c->g = NULL;
    c->x = NULL;
}

void canon_cert_free(canon_cert *c)
{
    canon_cert_end(c);
    canon_buf_free(&c->head);
    canon_buf_free(&c->nodes);
    canon_buf_free(&c->out);
    canon_perm_table_free(&c->autos);
    canon_root_image_free(&c->img);
    free_scratch(c);
}

/* spec 4.1: `count` U32 words, big endian */
static canon_status put_words(canon_buf *b, const uint32_t *w, uint32_t count)
{
    size_t bytes = 0;
    if (!canon_size_mul((size_t)count, 4u, &bytes)) {
        return CANON_CAPACITY_LIMIT;
    }
    canon_status st = canon_buf_reserve(b, bytes);
    for (uint32_t i = 0; i < count && st == CANON_COMPLETE; ++i) {
        st = canon_buf_put_u32(b, w[i]);
    }
    return st;
}

/* Overwrite the U32 at byte offset `at` (written earlier as a placeholder). */
static void patch_u32(canon_buf *b, size_t at, uint32_t v)
{
    b->data[at] = (uint8_t)(v >> 24);
    b->data[at + 1] = (uint8_t)(v >> 16);
    b->data[at + 2] = (uint8_t)(v >> 8);
    b->data[at + 3] = (uint8_t)v;
}

/* brief 3.5, docs/certificate-format.md CHAIN: U32(depth); per level (terminal level not
 * stored) U32(base) U32(orbit_len) orbit[] parent_point[] parent_gen[] U32(gen_count)
 * gen_ids[]; U32(gen_total) generator rows (inverses not stored); gen_node[gen_total];
 * U32(prov_count) (kind, a, b) per provenance node (PRODUCT a b: a acts first, spec 3);
 * U32(input_count) input references. */
canon_status canon_cert_chain_write(canon_buf *out, const canon_bsgs *ch, const uint32_t *refs)
{
    const uint32_t n = ch->n;
    canon_status st = canon_buf_put_u32(out, ch->depth);
    for (uint32_t i = 0; i < ch->depth && st == CANON_COMPLETE; ++i) {
        const canon_bsgs_level *L = &ch->levels[i];
        st = canon_buf_put_u32(out, L->base_point);
        if (st == CANON_COMPLETE) {
            st = canon_buf_put_u32(out, L->orbit_len);
        }
        if (st == CANON_COMPLETE) {
            st = put_words(out, L->orbit, L->orbit_len);
        }
        if (st == CANON_COMPLETE) {
            st = put_words(out, L->parent_point, L->orbit_len);
        }
        if (st == CANON_COMPLETE) {
            st = put_words(out, L->parent_gen, L->orbit_len);
        }
        if (st == CANON_COMPLETE) {
            st = canon_buf_put_u32(out, L->gen_count);
        }
        if (st == CANON_COMPLETE) {
            st = put_words(out, L->gen_ids, L->gen_count);
        }
    }
    const uint32_t total = ch->gens.count;
    if (st == CANON_COMPLETE) {
        st = canon_buf_put_u32(out, total);
    }
    for (uint32_t k = 0; k < total && st == CANON_COMPLETE && n > 0; ++k) {
        st = put_words(out, canon_perm_table_row(&ch->gens, k), n);
    }
    /* Only the provenance nodes reachable from gen_node are written, renumbered in their
     * order (operands still precede their users), so every written node derives part of some
     * generator: a record no generator depends on would be inert data no check could reach.
     * The chain's inverse nodes and the records of rejected Schreier products are dropped; the
     * reader re-creates inverses itself. */
    const uint32_t count = ch->prov.count;
    uint32_t *id = NULL;
    if (st == CANON_COMPLETE) {
        id = canon_alloc_array(count, sizeof *id, &st);
    }
    if (st != CANON_COMPLETE) {
        return st;
    }
    for (uint32_t i = 0; i < count; ++i) {
        id[i] = 0; /* 0 unreached, 1 reached (then the new id) */
    }
    for (uint32_t k = 0; k < total; ++k) {
        if (ch->gen_node[k] >= count) {
            free(id);
            return CANON_INTERNAL_ERROR;
        }
        id[ch->gen_node[k]] = 1;
    }
    for (uint32_t i = count; i-- > 0;) {
        const canon_prov_node *p = &ch->prov.nodes[i];
        if (id[i] != 0 && p->kind != CANON_PROV_INPUT) {
            id[p->a] = 1; /* operands are earlier nodes (provenance.h) */
            if (p->kind == CANON_PROV_PRODUCT) {
                id[p->b] = 1;
            }
        }
    }
    uint32_t kept = 0;
    for (uint32_t i = 0; i < count; ++i) {
        id[i] = id[i] != 0 ? kept++ : NONE;
    }
    for (uint32_t k = 0; k < total && st == CANON_COMPLETE; ++k) {
        st = canon_buf_put_u32(out, id[ch->gen_node[k]]);
    }
    if (st == CANON_COMPLETE) {
        st = canon_buf_put_u32(out, kept);
    }
    for (uint32_t i = 0; i < count && st == CANON_COMPLETE; ++i) {
        const canon_prov_node *p = &ch->prov.nodes[i];
        if (id[i] == NONE) {
            continue;
        }
        /* (kind, a, b): INPUT a = input index; INVERSE a = node; PRODUCT a b, a acts first */
        const uint32_t w[3] = {p->kind, p->kind == CANON_PROV_INPUT ? p->a : id[p->a],
                               p->kind == CANON_PROV_PRODUCT ? id[p->b] : 0};
        st = put_words(out, w, 3u);
    }
    free(id);
    if (st == CANON_COMPLETE) {
        st = canon_buf_put_u32(out, ch->inputs.count);
    }
    for (uint32_t j = 0; j < ch->inputs.count && st == CANON_COMPLETE; ++j) {
        st = canon_buf_put_u32(out, refs != NULL ? refs[j] : j);
    }
    return st;
}

/* U32(cells) then each cell size in partition order (spec 7.2 STAGE_O / STAGE_G sizes). */
static canon_status put_sizes(canon_buf *b, const canon_partition *p)
{
    canon_status st = canon_buf_put_u32(b, p->cells);
    for (uint32_t i = 0; i < p->cells && st == CANON_COMPLETE; ++i) {
        st = canon_buf_put_u32(b, canon_partition_cell_size(p, i));
    }
    return st;
}

/* ---- recorder hooks (src/refine/p1.h) ---- */

static canon_status hook_node(void *ctx, uint32_t depth, uint32_t atom)
{
    canon_cert *c = ctx;
    if (depth > c->n || (depth > 0) != (atom != NONE) || (depth > 0 && atom >= c->n)) {
        return CANON_INTERNAL_ERROR;
    }
    if (depth > 0) {
        c->path[depth - 1] = atom; /* the prefix a_1..a_depth of this node */
    }
    c->node_records += 1;
    c->sweeps = 0;
    /* NODE := U32(depth) U32(atom | NONE) U32(sweeps), the count patched at LEAF/BRANCH */
    canon_status st = canon_buf_put_u32(&c->nodes, depth);
    if (st == CANON_COMPLETE) {
        st = canon_buf_put_u32(&c->nodes, atom);
    }
    c->sweep_patch = c->nodes.len;
    if (st == CANON_COMPLETE) {
        st = canon_buf_put_u32(&c->nodes, 0);
    }
    return st;
}

/* spec 7.1 O stage: "append STAGE_O(cell sizes of P)" */
static canon_status hook_stage_o(void *ctx, const canon_partition *p)
{
    canon_cert *c = ctx;
    c->sweeps += 1;
    return put_sizes(&c->nodes, p);
}

/* spec 7.1 G stage: "M = lexicographically least F^G; choose any u in G with F^u=M; compute
 * G_M orbits".  The engine exposes no chain (its backends rebase internally), so the writer
 * rebuilds one from its root chain with base prefix M (spec 9.2 base change) and checks that
 * its orbits of G_M, ranked by least point (spec 7.1), are the engine's. */
static canon_status hook_stage_g(void *ctx, const canon_partition *p, const uint32_t *fixed,
                                 uint32_t f, const uint32_t *u, const uint32_t *orbit)
{
    canon_cert *c = ctx;
    const uint32_t n = c->n;
    if (f > n) {
        return CANON_INTERNAL_ERROR;
    }
    for (uint32_t i = 0; i < f; ++i) {
        c->M[i] = u[fixed[i]]; /* M = F^u (spec 3: L^g = (g[L[0]], ...)) */
    }
    canon_bsgs reb;
    canon_status st = canon_bsgs_rebase(&c->root, 0, c->M, f, false, &reb);
    if (st != CANON_COMPLETE) {
        return st;
    }
    /* Cross-checks (never emit a certificate that disagrees with the run): the base starts
     * with M; spec 7.2 minimality, M[i] the least point of level i's orbit; spec 7.1 the
     * orbits of G_M (the suffix after |M| levels) equal the engine's ids. */
    bool ok = reb.depth >= f;
    for (uint32_t i = 0; i < f && ok; ++i) {
        const canon_bsgs_level *L = &reb.levels[i];
        ok = L->base_point == c->M[i];
        for (uint32_t j = 0; j < L->orbit_len && ok; ++j) {
            ok = L->orbit[j] >= c->M[i];
        }
    }
    if (ok) {
        canon_bsgs_orbit_ids(&reb, f, c->ids);
        ok = n == 0 || memcmp(c->ids, orbit, (size_t)n * sizeof *orbit) == 0;
    }
    if (!ok) {
        st = CANON_INTERNAL_ERROR;
    }
    /* SWEEP (continued) := U32(f) F[f] u M[f] CHAIN U32(kG) sizeG[kG] */
    if (st == CANON_COMPLETE) {
        st = canon_buf_put_u32(&c->nodes, f);
    }
    if (st == CANON_COMPLETE) {
        st = put_words(&c->nodes, fixed, f);
    }
    if (st == CANON_COMPLETE) {
        st = put_words(&c->nodes, u, n);
    }
    if (st == CANON_COMPLETE) {
        st = put_words(&c->nodes, c->M, f);
    }
    if (st == CANON_COMPLETE) {
        /* inputs of the rebased chain = the root chain's level-0 generators, in order
         * (canon_bsgs_rebase builds from them) */
        st = reb.inputs.count == c->root.levels[0].gen_count
                 ? canon_cert_chain_write(&c->nodes, &reb, c->root.levels[0].gen_ids)
                 : CANON_INTERNAL_ERROR;
    }
    if (st == CANON_COMPLETE) {
        st = put_sizes(&c->nodes, p);
    }
    canon_bsgs_free(&reb);
    if (st == CANON_COMPLETE) {
        if (n > 0) {
            memcpy(c->last_u, u, (size_t)n * sizeof *u);
        }
        c->last_f = f;
    }
    return st;
}

/* spec 7.2 leaf: the last sweep had F = L (every cell a singleton), so its u is t_L (a full
 * list determines t uniquely) and LEAF carries only t (docs/certificate-format.md). */
static canon_status hook_leaf(void *ctx, const uint32_t *t)
{
    canon_cert *c = ctx;
    const uint32_t n = c->n;
    if (c->sweeps == 0 || c->last_f != n ||
        (t != NULL && n > 0 && memcmp(t, c->last_u, (size_t)n * sizeof *t) != 0)) {
        return CANON_INTERNAL_ERROR;
    }
    patch_u32(&c->nodes, c->sweep_patch, c->sweeps);
    canon_status st = canon_buf_put_u8(&c->nodes, CANON_CERT_LEAF);
    if (st == CANON_COMPLETE) {
        st = put_words(&c->nodes, c->last_u, n);
    }
    return st;
}

/* spec 7.1 branch: BRANCH := U32(cell index) U32(cell size) U32(child count = cell size) */
static canon_status hook_branch(void *ctx, uint32_t cell, uint32_t size)
{
    canon_cert *c = ctx;
    if (c->sweeps == 0) {
        return CANON_INTERNAL_ERROR;
    }
    patch_u32(&c->nodes, c->sweep_patch, c->sweeps);
    canon_status st = canon_buf_put_u8(&c->nodes, CANON_CERT_BRANCH);
    const uint32_t w[3] = {cell, size, size};
    if (st == CANON_COMPLETE) {
        st = put_words(&c->nodes, w, 3u);
    }
    return st;
}

static canon_status hook_explored(void *ctx, uint32_t b)
{
    canon_cert *c = ctx;
    canon_status st = canon_buf_put_u8(&c->nodes, CANON_CERT_EXPLORED);
    if (st == CANON_COMPLETE) {
        st = canon_buf_put_u32(&c->nodes, b);
    }
    return st;
}

/* docs/pruning-rules.md lemma; spec 7.3 "Verify ... by exact membership and object equality
 * before publication", spec 14.3 R2: the automorphism is checked here (a bijection, a member
 * of G, x^a = x, fixing the node's prefix pointwise, rep^a = b) before it is written. */
static canon_status hook_pruned(void *ctx, uint32_t depth, uint32_t b, uint32_t rep,
                                const uint32_t *aut)
{
    canon_cert *c = ctx;
    const uint32_t n = c->n;
    bool ok = false;
    canon_status st = canon_perm_check(aut, n, &ok);
    if (st != CANON_COMPLETE) {
        return st;
    }
    if (ok) {
        ok = depth <= n && b < n && rep < n && aut[rep] == b;
    }
    for (uint32_t i = 0; i < depth && ok; ++i) {
        ok = aut[c->path[i]] == c->path[i];
    }
    if (ok) {
        st = c->g->ops->contains(c->g, aut, &ok);
        if (st != CANON_COMPLETE) {
            return st;
        }
    }
    if (ok) {
        st = canon_root_act_into(c->x, aut, &c->img);
        if (st != CANON_COMPLETE) {
            canon_root_image_clear(&c->img);
            return st;
        }
        ok = canon_root_equal(&c->img.root, c->x);
        canon_root_image_clear(&c->img);
    }
    if (!ok) {
        return CANON_INTERNAL_ERROR;
    }
    /* AUT list in first-use order, no dedupe (docs/certificate-format.md) */
    const uint32_t index = c->autos.count;
    uint32_t row = 0;
    st = canon_perm_table_push(&c->autos, aut, &row);
    if (st != CANON_COMPLETE) {
        return st;
    }
    if (row != index) {
        return CANON_INTERNAL_ERROR;
    }
    st = canon_buf_put_u8(&c->nodes, CANON_CERT_PRUNED);
    const uint32_t w[3] = {b, rep, index};
    if (st == CANON_COMPLETE) {
        st = put_words(&c->nodes, w, 3u);
    }
    return st;
}

const canon_p1_recorder *canon_cert_hooks(canon_cert *c)
{
    c->hooks.ctx = c;
    c->hooks.stage_o = hook_stage_o;
    c->hooks.stage_g = hook_stage_g;
    c->hooks.node = hook_node;
    c->hooks.leaf = hook_leaf;
    c->hooks.branch = hook_branch;
    c->hooks.explored = hook_explored;
    c->hooks.pruned = hook_pruned;
    return &c->hooks;
}

static canon_status reserve_scratch(canon_cert *c, uint32_t n)
{
    if (c->path != NULL && n <= c->cap) {
        return CANON_COMPLETE;
    }
    free_scratch(c);
    canon_status st = CANON_COMPLETE;
    c->path = canon_alloc_array(n, sizeof *c->path, &st);
    c->last_u = canon_alloc_array(n, sizeof *c->last_u, &st);
    c->M = canon_alloc_array(n, sizeof *c->M, &st);
    c->ids = canon_alloc_array(n, sizeof *c->ids, &st);
    c->scratch = canon_alloc_array(n, sizeof *c->scratch, &st);
    if (c->path == NULL || c->last_u == NULL || c->M == NULL || c->ids == NULL ||
        c->scratch == NULL) {
        free_scratch(c);
        return st != CANON_COMPLETE ? st : CANON_RESOURCE_LIMIT;
    }
    c->cap = n;
    return CANON_COMPLETE;
}

canon_status canon_cert_begin(canon_cert *c, const canon_group *g, const canon_root *x,
                              canon_work_policy policy)
{
    canon_cert_end(c);
    canon_buf_truncate(&c->head, 0);
    canon_buf_truncate(&c->nodes, 0);
    canon_buf_truncate(&c->out, 0);
    c->node_records = 0;
    c->sweeps = 0;
    c->last_f = 0;
    if (x->kind == CANON_ROOT_DAG) {
        return CANON_UNSUPPORTED_ACTION; /* brief 3.4: nested roots unsupported in v0 */
    }
    const canon_perm_table *in = canon_group_input_generators(g);
    if (in == NULL) {
        return CANON_UNSUPPORTED_ACTION; /* no recorded generators to certify against */
    }
    const uint32_t n = x->n;
    if (g->degree != n || (n > 0 && in->n != n) ||
        (policy != CANON_WORK_POLICY_REFERENCE && policy != CANON_WORK_POLICY_ORBIT_PRUNE)) {
        return CANON_INTERNAL_ERROR;
    }
    canon_status st = reserve_scratch(c, n);
    if (st != CANON_COMPLETE) {
        return st;
    }
    /* the AUT table at degree n (grow-only storage replaced when the degree changes) */
    canon_perm_table_free(&c->autos);
    canon_perm_table_init(&c->autos, n);
    c->g = g;
    c->x = x;
    c->n = n;
    /* the root chain, one path for both backends: spec 9.1 "exact verification is
     * mandatory" */
    st = canon_bsgs_build_verified(&c->root, n, in->data, in->count);
    if (st != CANON_COMPLETE) {
        return st;
    }
    c->have_root = true;
    if (c->root.inputs.count != in->count ||
        (n > 0 && in->count > 0 &&
         memcmp(c->root.inputs.data, in->data, (size_t)in->count * n * sizeof *in->data) != 0)) {
        return CANON_INTERNAL_ERROR; /* the root chain's INPUT j must be header generator j */
    }
    /* Header (brief 3.3 with D14): "CNC0" U16(objective) U16(profile) U16(work_policy)
     * U16(encoding) U16(order) U32(n) B(x in CDAG-2) U32(k) GEN[k] CHAIN */
    canon_buf *h = &c->head;
    static const uint8_t magic[4] = {'C', 'N', 'C', '0'};
    st = canon_buf_put_bytes(h, magic, sizeof magic);
    const uint16_t ids[5] = {CANON_OBJECTIVE_CANONICAL_IMAGE, CANON_PROFILE_P1, policy,
                             CANON_ENCODING_CDAG_2, CANON_ORDER_CDAG_BYTE_1};
    for (int i = 0; i < 5 && st == CANON_COMPLETE; ++i) {
        st = canon_buf_put_u16(h, ids[i]);
    }
    if (st == CANON_COMPLETE) {
        st = canon_buf_put_u32(h, n);
    }
    if (st == CANON_COMPLETE) {
        st = canon_root_stream_write(x, &c->out); /* spec 4.1 stream of x; out is scratch */
    }
    if (st == CANON_COMPLETE) {
        st = canon_buf_put_b(h, c->out.data, c->out.len);
    }
    canon_buf_truncate(&c->out, 0);
    if (st == CANON_COMPLETE) {
        st = canon_buf_put_u32(h, in->count);
    }
    for (uint32_t k = 0; k < in->count && st == CANON_COMPLETE && n > 0; ++k) {
        st = put_words(h, canon_perm_table_row(in, k), n);
    }
    if (st == CANON_COMPLETE) {
        st = canon_cert_chain_write(h, &c->root, NULL);
    }
    return st;
}

canon_status canon_cert_finish(canon_cert *c, const canon_p1_search *s)
{
    if (c->node_records != s->nodes || !s->have_best || s->n != c->n) {
        return CANON_INTERNAL_ERROR;
    }
    canon_buf *o = &c->out;
    canon_buf_truncate(o, 0);
    canon_status st = canon_buf_reserve(o, c->head.len);
    if (st == CANON_COMPLETE) {
        st = canon_buf_put_bytes(o, c->head.data, c->head.len);
    }
    /* U32(a) AUT[a] */
    if (st == CANON_COMPLETE) {
        st = canon_buf_put_u32(o, c->autos.count);
    }
    for (uint32_t k = 0; k < c->autos.count && st == CANON_COMPLETE && c->n > 0; ++k) {
        st = put_words(o, canon_perm_table_row(&c->autos, k), c->n);
    }
    if (st == CANON_COMPLETE) {
        st = canon_buf_put_bytes(o, c->nodes.data, c->nodes.len);
    }
    /* FINAL := B(best trace) B(best bytes) witness Nat(node count) */
    if (st == CANON_COMPLETE) {
        st = canon_buf_put_b(o, s->best_trace.data, s->best_trace.len);
    }
    if (st == CANON_COMPLETE) {
        st = canon_buf_put_b(o, s->best_bytes.data, s->best_bytes.len);
    }
    if (st == CANON_COMPLETE) {
        st = put_words(o, s->best_t, c->n);
    }
    if (st == CANON_COMPLETE) {
        st = canon_buf_put_nat(o, s->nodes);
    }
    if (st != CANON_COMPLETE) {
        canon_buf_truncate(o, 0);
    }
    return st;
}
