/* Certificate v0 writer (spec 3.2, 7.3, 20; slice S7, docs/slices/S7.md 3.3, 3.5).  The layout
 * is stated in certificate.h; every field is written through the wire primitives of
 * src/encoding/wire.h (spec 17: "wire streams never dump structs"). */
#include "search/certificate.h"

#include <stdlib.h>
#include <string.h>

#include "arena/alloc.h"
#include "arena/checked.h"

static const uint8_t CERT_MAGIC[4] = {'C', 'N', 'C', '0'};

void canon_cert_init(canon_cert *c)
{
    memset(c, 0, sizeof *c);
    canon_buf_init(&c->head);
    canon_buf_init(&c->auts);
    canon_buf_init(&c->nodes);
    canon_buf_init(&c->out);
    canon_bsgs_init(&c->root, 0);
    canon_bsgs_init(&c->rebased, 0);
}

void canon_cert_free(canon_cert *c)
{
    free(c->m);
    canon_buf_free(&c->head);
    canon_buf_free(&c->auts);
    canon_buf_free(&c->nodes);
    canon_buf_free(&c->out);
    canon_bsgs_free(&c->root);
    canon_bsgs_free(&c->rebased);
    canon_cert_init(c);
}

void canon_cert_reset(canon_cert *c)
{
    canon_buf_truncate(&c->head, 0);
    canon_buf_truncate(&c->auts, 0);
    canon_buf_truncate(&c->nodes, 0);
    canon_buf_truncate(&c->out, 0);
    canon_bsgs_free(&c->root);
    canon_bsgs_free(&c->rebased);
    c->aut_count = 0;
    c->active = false;
    c->complete = false;
    c->sweeps = 0;
    c->sweeps_at = 0;
    c->chains = 0;
}

/* n x U32: a dense image array (n = 0 writes nothing). */
static canon_status put_dense(canon_buf *out, const uint32_t *p, uint32_t n)
{
    size_t bytes = 0;
    if (!canon_size_mul((size_t)n, 4u, &bytes)) {
        return CANON_CAPACITY_LIMIT; /* spec 11.1 */
    }
    canon_status st = canon_buf_reserve(out, bytes);
    for (uint32_t v = 0; v < n && st == CANON_COMPLETE; ++v) {
        st = canon_buf_put_u32(out, p[v]);
    }
    return st;
}

/* U32(k) followed by k x U32. */
static canon_status put_list(canon_buf *out, const uint32_t *p, uint32_t k)
{
    canon_status st = canon_buf_put_u32(out, k);
    return st == CANON_COMPLETE ? put_dense(out, p, k) : st;
}

canon_status canon_cert_write_chain(canon_buf *out, const canon_bsgs *chain)
{
    const uint32_t n = chain->n;
    canon_status st = canon_buf_put_u32(out, n);
    if (st == CANON_COMPLETE) {
        st = canon_buf_put_u32(out, chain->depth);
    }
    for (uint32_t i = 0; i < chain->depth && st == CANON_COMPLETE; ++i) {
        const canon_bsgs_level *L = &chain->levels[i];
        st = canon_buf_put_u32(out, L->base_point);
        if (st == CANON_COMPLETE) {
            st = put_list(out, L->orbit, L->orbit_len); /* U32(r) and the orbit */
        }
        if (st == CANON_COMPLETE) {
            st = put_dense(out, L->parent_point, L->orbit_len);
        }
        if (st == CANON_COMPLETE) {
            st = put_dense(out, L->parent_gen, L->orbit_len);
        }
        if (st == CANON_COMPLETE) {
            st = put_list(out, L->gen_ids, L->gen_count);
        }
    }
    if (st == CANON_COMPLETE) {
        st = canon_buf_put_u32(out, chain->gens.count);
    }
    for (uint32_t k = 0; k < chain->gens.count && st == CANON_COMPLETE; ++k) {
        st = canon_buf_put_u32(out, chain->gen_node[k]);
        if (st == CANON_COMPLETE) {
            st = put_dense(out, canon_perm_table_row(&chain->gens, k), n);
        }
    }
    if (st == CANON_COMPLETE) {
        st = canon_buf_put_u32(out, chain->prov.count);
    }
    for (uint32_t j = 0; j < chain->prov.count && st == CANON_COMPLETE; ++j) {
        const canon_prov_node *node = &chain->prov.nodes[j];
        st = canon_buf_put_u32(out, node->kind);
        if (st == CANON_COMPLETE) {
            st = canon_buf_put_u32(out, node->a);
        }
        if (st == CANON_COMPLETE) {
            st = canon_buf_put_u32(out, node->kind == CANON_PROV_PRODUCT ? node->b : 0u);
        }
    }
    if (st == CANON_COMPLETE) {
        st = canon_buf_put_u32(out, chain->inputs.count);
    }
    for (uint32_t k = 0; k < chain->inputs.count && st == CANON_COMPLETE; ++k) {
        st = put_dense(out, canon_perm_table_row(&chain->inputs, k), n);
    }
    return st;
}

/* Any failure ends the certificate: the search goes on, the result carries no certificate. */
static canon_status fail(canon_cert *c, canon_status st)
{
    if (st != CANON_COMPLETE) {
        c->active = false;
    }
    return st;
}

canon_status canon_cert_begin(canon_cert *c, const canon_group *g, const canon_root *x,
                              uint16_t work_policy)
{
    canon_cert_reset(c);
    const uint32_t n = x->n;
    if (c->m == NULL || n > c->m_cap) {
        canon_status as = CANON_COMPLETE;
        uint32_t *m = canon_alloc_array(n, sizeof *m, &as);
        if (m == NULL) {
            return as;
        }
        free(c->m);
        c->m = m;
        c->m_cap = n;
    }
    c->n = n;
    canon_buf *h = &c->head;
    canon_status st = canon_buf_put_bytes(h, CERT_MAGIC, sizeof CERT_MAGIC);
    /* spec 3 objective tag, spec 4.3 profile, spec 11.1 work policy, spec 4.1 encoding, spec
     * 4.3 order: the frozen identifiers of the claim */
    const uint16_t ids[5] = {CANON_OBJECTIVE_CANONICAL_IMAGE, CANON_PROFILE_P1, work_policy,
                             CANON_ENCODING_CDAG_2, CANON_ORDER_CDAG_BYTE_1};
    for (size_t i = 0; i < 5 && st == CANON_COMPLETE; ++i) {
        st = canon_buf_put_u16(h, ids[i]);
    }
    if (st == CANON_COMPLETE) {
        st = canon_buf_put_u32(h, n);
    }
    /* B(stream of x): the input as the checker's own decoder reads it (spec 4.1) */
    canon_buf stream;
    canon_buf_init(&stream);
    if (st == CANON_COMPLETE) {
        st = canon_root_stream_write(x, &stream);
    }
    if (st == CANON_COMPLETE) {
        st = canon_buf_put_b(h, stream.data, stream.len);
    }
    canon_buf_free(&stream);
    /* the generators of G as given (canon_group_set_generators) */
    if (st == CANON_COMPLETE) {
        st = canon_buf_put_u32(h, g->gens.count);
    }
    for (uint32_t k = 0; k < g->gens.count && st == CANON_COMPLETE; ++k) {
        st = put_dense(h, canon_perm_table_row(&g->gens, k), n);
    }
    /* spec 9.1: the root chain is built and verified here, from those generators, whatever
     * the group's backend; its inputs are exactly the generator list above */
    if (st == CANON_COMPLETE) {
        st = canon_bsgs_build_verified(&c->root, n, g->gens.data, g->gens.count);
    }
    if (st == CANON_COMPLETE) {
        st = canon_cert_write_chain(h, &c->root);
    }
    if (st != CANON_COMPLETE) {
        return st;
    }
    c->active = true;
    return CANON_COMPLETE;
}

canon_status canon_cert_automorphism(canon_cert *c, const uint32_t *a, uint32_t *index)
{
    if (!c->active) {
        return CANON_COMPLETE;
    }
    if (c->aut_count == UINT32_MAX) {
        return fail(c, CANON_CAPACITY_LIMIT); /* spec 11.1: the index is U32 */
    }
    canon_status st = put_dense(&c->auts, a, c->n);
    if (st != CANON_COMPLETE) {
        return fail(c, st);
    }
    *index = c->aut_count++;
    return CANON_COMPLETE;
}

/* Overwrite a U32 written earlier at `at` (the sweep count placeholder). */
static void patch_u32(canon_buf *b, size_t at, uint32_t value)
{
    b->data[at] = (uint8_t)(value >> 24);
    b->data[at + 1] = (uint8_t)(value >> 16);
    b->data[at + 2] = (uint8_t)(value >> 8);
    b->data[at + 3] = (uint8_t)value;
}

static canon_status put_sizes(canon_buf *out, const canon_partition *p)
{
    canon_status st = canon_buf_put_u32(out, p->cells);
    for (uint32_t i = 0; i < p->cells && st == CANON_COMPLETE; ++i) {
        st = canon_buf_put_u32(out, canon_partition_cell_size(p, i));
    }
    return st;
}

static canon_status observe_o(void *user, const canon_partition *p)
{
    canon_cert *c = user;
    if (!c->active) {
        return CANON_COMPLETE;
    }
    if (c->sweeps == UINT32_MAX) {
        return fail(c, CANON_CAPACITY_LIMIT);
    }
    c->sweeps += 1;
    return fail(c, put_sizes(&c->nodes, p)); /* STAGE_O sizes (S7 brief 3.4 rule 4) */
}

static canon_status observe_g(void *user, const canon_partition *p, const uint32_t *fixed,
                              uint32_t f, const uint32_t *u)
{
    canon_cert *c = user;
    if (!c->active) {
        return CANON_COMPLETE;
    }
    canon_buf *out = &c->nodes;
    canon_status st = put_list(out, fixed, f); /* U32(f) F */
    if (st == CANON_COMPLETE) {
        st = put_dense(out, u, c->n); /* u */
    }
    /* M = F^u (spec 7.1), written explicitly; the checker recomputes and compares */
    for (uint32_t i = 0; i < f && st == CANON_COMPLETE; ++i) {
        st = canon_buf_put_u32(out, u[fixed[i]]);
    }
    /* spec 9.2 base change: a chain of G whose base starts with M, so that level i's orbit is
     * the orbit of M[i] under the stabiliser of M[0..i-1] (the checker's minimality rule) and
     * level f's generators generate G_M (the orbits of the G stage).  Transient and unverified
     * here; the checker verifies it (S7 brief 3.4 rule 4).  The points u[fixed[i]] are
     * distinct (u is a bijection and F has distinct entries), as the rebase requires. */
    if (st == CANON_COMPLETE) {
        for (uint32_t i = 0; i < f; ++i) {
            c->m[i] = u[fixed[i]];
        }
        canon_bsgs_free(&c->rebased);
        st = canon_bsgs_rebase(&c->root, 0, c->m, f, false, &c->rebased);
    }
    if (st == CANON_COMPLETE) {
        c->chains += 1;
        st = canon_cert_write_chain(out, &c->rebased);
    }
    if (st == CANON_COMPLETE) {
        st = put_sizes(out, p); /* STAGE_G sizes */
    }
    return fail(c, st);
}

canon_p1_observer canon_cert_observer(canon_cert *c)
{
    canon_p1_observer o = {c, observe_o, observe_g};
    return o;
}

canon_status canon_cert_node_begin(canon_cert *c, uint32_t depth, uint32_t atom)
{
    if (!c->active) {
        return CANON_COMPLETE;
    }
    canon_buf *out = &c->nodes;
    canon_status st = canon_buf_put_u32(out, depth);
    if (st == CANON_COMPLETE) {
        st = canon_buf_put_u32(out, atom);
    }
    c->sweeps_at = out->len;
    c->sweeps = 0;
    if (st == CANON_COMPLETE) {
        st = canon_buf_put_u32(out, 0); /* patched by canon_cert_node_refined */
    }
    return fail(c, st);
}

canon_status canon_cert_node_refined(canon_cert *c)
{
    if (c->active) {
        patch_u32(&c->nodes, c->sweeps_at, c->sweeps);
    }
    return CANON_COMPLETE;
}

canon_status canon_cert_leaf(canon_cert *c, const uint32_t *t)
{
    if (!c->active) {
        return CANON_COMPLETE;
    }
    canon_status st = canon_buf_put_u8(&c->nodes, 0x00);
    if (st == CANON_COMPLETE) {
        st = put_dense(&c->nodes, t, c->n);
    }
    return fail(c, st);
}

canon_status canon_cert_branch(canon_cert *c, uint32_t cell, uint32_t size)
{
    if (!c->active) {
        return CANON_COMPLETE;
    }
    canon_status st = canon_buf_put_u8(&c->nodes, 0x01);
    if (st == CANON_COMPLETE) {
        st = canon_buf_put_u32(&c->nodes, cell);
    }
    if (st == CANON_COMPLETE) {
        st = canon_buf_put_u32(&c->nodes, size);
    }
    return fail(c, st);
}

canon_status canon_cert_child_explored(canon_cert *c, uint32_t b)
{
    if (!c->active) {
        return CANON_COMPLETE;
    }
    canon_status st = canon_buf_put_u8(&c->nodes, 0x01);
    if (st == CANON_COMPLETE) {
        st = canon_buf_put_u32(&c->nodes, b);
    }
    return fail(c, st);
}

canon_status canon_cert_child_pruned(canon_cert *c, uint32_t b, uint32_t rep, uint32_t aut)
{
    if (!c->active) {
        return CANON_COMPLETE;
    }
    const uint32_t fields[3] = {b, rep, aut};
    canon_status st = canon_buf_put_u8(&c->nodes, 0x00);
    for (size_t i = 0; i < 3 && st == CANON_COMPLETE; ++i) {
        st = canon_buf_put_u32(&c->nodes, fields[i]);
    }
    return fail(c, st);
}

canon_status canon_cert_finish(canon_cert *c, const uint8_t *trace, size_t trace_len,
                               const uint8_t *bytes, size_t bytes_len, const uint32_t *t,
                               uint64_t nodes)
{
    if (!c->active) {
        return CANON_COMPLETE;
    }
    canon_buf *o = &c->out;
    canon_buf_truncate(o, 0);
    size_t total = 0;
    if (!canon_size_add(c->head.len, c->auts.len, &total) ||
        !canon_size_add(total, c->nodes.len, &total) ||
        !canon_size_add(total, trace_len, &total) || !canon_size_add(total, bytes_len, &total) ||
        !canon_size_add(total, 20u + 4u * (size_t)c->n, &total)) {
        return fail(c, CANON_CAPACITY_LIMIT);
    }
    canon_status st = canon_buf_reserve(o, total);
    if (st == CANON_COMPLETE) {
        st = canon_buf_put_bytes(o, c->head.data, c->head.len);
    }
    if (st == CANON_COMPLETE) {
        st = canon_buf_put_u32(o, c->aut_count);
    }
    if (st == CANON_COMPLETE) {
        st = canon_buf_put_bytes(o, c->auts.data, c->auts.len);
    }
    if (st == CANON_COMPLETE) {
        st = canon_buf_put_bytes(o, c->nodes.data, c->nodes.len);
    }
    /* FINAL: the claim (S7 brief 3.3, 3.4 rule 7) */
    if (st == CANON_COMPLETE) {
        st = canon_buf_put_b(o, trace, trace_len);
    }
    if (st == CANON_COMPLETE) {
        st = canon_buf_put_b(o, bytes, bytes_len);
    }
    if (st == CANON_COMPLETE) {
        st = put_dense(o, t, c->n);
    }
    if (st == CANON_COMPLETE) {
        /* spec 11.1: the count of the traversal the header's work policy names, as U64 */
        st = canon_buf_put_u32(o, (uint32_t)(nodes >> 32));
    }
    if (st == CANON_COMPLETE) {
        st = canon_buf_put_u32(o, (uint32_t)nodes);
    }
    if (st != CANON_COMPLETE) {
        return fail(c, st);
    }
    c->active = false;
    c->complete = true;
    return CANON_COMPLETE;
}

const uint8_t *canon_cert_bytes(const canon_cert *c, size_t *length)
{
    if (!c->complete) {
        *length = 0;
        return NULL;
    }
    *length = c->out.len;
    return c->out.data;
}
