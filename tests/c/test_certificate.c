/* Slice S7: the certificate writer (src/search/certificate.h; docs/slices/S7.md 3.3, 3.5, 4):
 * round trip of the serialised chain, the certificate of each spec 7.4 subset and graph case
 * through the public API (header, embedded stream and generators, final claim), statuses of
 * the certificate option, and the evidence mode.  Acceptance by the independent checker is
 * tested in checker/tests. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "bsgs/chain.h"
#include "canon/canon.h"
#include "check.h"
#include "search/certificate.h"

/* ---- a tiny reader for the layout ---- */

typedef struct rd {
    const uint8_t *d;
    size_t len, at;
} rd;

static uint32_t get32(rd *r)
{
    CHECK(r->len - r->at >= 4);
    if (r->len - r->at < 4) {
        return 0;
    }
    const uint8_t *b = r->d + r->at;
    r->at += 4;
    return (uint32_t)b[0] << 24 | (uint32_t)b[1] << 16 | (uint32_t)b[2] << 8 | b[3];
}

static uint16_t get16(rd *r)
{
    CHECK(r->len - r->at >= 2);
    const uint8_t *b = r->d + r->at;
    r->at += 2;
    return (uint16_t)((uint16_t)b[0] << 8 | b[1]);
}

/* Read a CHAIN and compare every field with c. */
static void check_chain(rd *r, const canon_bsgs *c)
{
    const uint32_t n = c->n;
    CHECK(get32(r) == n);
    CHECK(get32(r) == c->depth);
    for (uint32_t i = 0; i < c->depth; ++i) {
        const canon_bsgs_level *L = &c->levels[i];
        CHECK(get32(r) == L->base_point);
        CHECK(get32(r) == L->orbit_len);
        for (uint32_t q = 0; q < L->orbit_len; ++q) {
            CHECK(get32(r) == L->orbit[q]);
        }
        for (uint32_t q = 0; q < L->orbit_len; ++q) {
            CHECK(get32(r) == L->parent_point[q]);
        }
        for (uint32_t q = 0; q < L->orbit_len; ++q) {
            CHECK(get32(r) == L->parent_gen[q]);
        }
        CHECK(get32(r) == L->gen_count);
        for (uint32_t k = 0; k < L->gen_count; ++k) {
            CHECK(get32(r) == L->gen_ids[k]);
        }
    }
    CHECK(get32(r) == c->gens.count);
    for (uint32_t k = 0; k < c->gens.count; ++k) {
        CHECK(get32(r) == c->gen_node[k]);
        for (uint32_t v = 0; v < n; ++v) {
            CHECK(get32(r) == canon_perm_table_row(&c->gens, k)[v]);
        }
    }
    CHECK(get32(r) == c->prov.count);
    for (uint32_t j = 0; j < c->prov.count; ++j) {
        CHECK(get32(r) == c->prov.nodes[j].kind);
        CHECK(get32(r) == c->prov.nodes[j].a);
        const uint32_t b = get32(r);
        CHECK(c->prov.nodes[j].kind == CANON_PROV_PRODUCT ? b == c->prov.nodes[j].b : b == 0);
    }
    CHECK(get32(r) == c->inputs.count);
    for (uint32_t k = 0; k < c->inputs.count; ++k) {
        for (uint32_t v = 0; v < n; ++v) {
            CHECK(get32(r) == canon_perm_table_row(&c->inputs, k)[v]);
        }
    }
}

static void chain_round_trip(void)
{
    /* Sym(4) from a 4-cycle and a transposition, rebased to the prefix (2, 0) */
    const uint32_t gens[8] = {1, 2, 3, 0, 1, 0, 2, 3};
    canon_bsgs c;
    CHECK(canon_bsgs_build_verified(&c, 4, gens, 2) == CANON_COMPLETE);
    const uint32_t prefix[2] = {2, 0};
    canon_bsgs d;
    CHECK(canon_bsgs_rebase(&c, 0, prefix, 2, false, &d) == CANON_COMPLETE);
    canon_buf out;
    canon_buf_init(&out);
    CHECK(canon_cert_write_chain(&out, &c) == CANON_COMPLETE);
    const size_t first = out.len;
    CHECK(canon_cert_write_chain(&out, &d) == CANON_COMPLETE);
    rd r = {out.data, out.len, 0};
    check_chain(&r, &c);
    CHECK(r.at == first);
    check_chain(&r, &d);
    CHECK(r.at == out.len);
    /* the trivial chain of degree 0 and of degree 3 */
    canon_bsgs t;
    CHECK(canon_bsgs_build_verified(&t, 0, NULL, 0) == CANON_COMPLETE);
    canon_buf_truncate(&out, 0);
    CHECK(canon_cert_write_chain(&out, &t) == CANON_COMPLETE);
    CHECK(out.len == 5 * 4); /* n, depth, g, p, i */
    canon_bsgs_free(&t);
    canon_buf_free(&out);
    canon_bsgs_free(&c);
    canon_bsgs_free(&d);
}

/* ---- certificates through the API ---- */

static canon_context *ctx;

typedef struct built {
    canon_group *g;
    canon_object *x;
} built;

static canon_result *solve(const built *b, bool certificate, canon_objective objective,
                           uint16_t policy, canon_status *st)
{
    canon_capacity cap = {0, 0, 0, 0, 0, 0, 0, policy};
    canon_problem_options opts = {CANON_WITNESS_ANY, NULL, certificate};
    canon_problem *p = NULL;
    canon_workspace *ws = NULL;
    canon_result *r = NULL;
    const canon_profile profile =
        objective == CANON_OBJECTIVE_CANONICAL_IMAGE ? CANON_PROFILE_P1 : CANON_PROFILE_NO_TREE;
    *st = canon_problem_create_with_options(ctx, b->g, b->x, NULL, objective, profile,
                                            CANON_ENCODING_CDAG_2, CANON_ORDER_CDAG_BYTE_1, &cap,
                                            &opts, &p);
    if (*st == CANON_COMPLETE) {
        CHECK(canon_workspace_create(ctx, &ws) == CANON_COMPLETE);
        *st = canon_solve(ws, p, &r);
    }
    canon_workspace_release(ws);
    canon_problem_release(p);
    return r;
}

/* The certificate's header, embedded input and final claim agree with the result (the node
 * records in between are the checker's business). */
static void check_certificate(const built *b, uint16_t policy, uint32_t n,
                              const uint32_t *gens, uint32_t gen_count)
{
    canon_status st = CANON_COMPLETE;
    canon_result *r = solve(b, true, CANON_OBJECTIVE_CANONICAL_IMAGE, policy, &st);
    CHECK(st == CANON_COMPLETE && r != NULL);
    if (r == NULL) {
        return;
    }
    size_t len = 0;
    const uint8_t *cert = canon_result_certificate(r, &len);
    CHECK(cert != NULL && len > 0);
    CHECK(canon_result_evidence(r) == CANON_EVIDENCE_CHECKED_CERTIFICATE);
    rd rr = {cert, len, 0};
    CHECK(len >= 4 && memcmp(cert, "CNC0", 4) == 0);
    rr.at = 4;
    CHECK(get16(&rr) == CANON_OBJECTIVE_CANONICAL_IMAGE);
    CHECK(get16(&rr) == CANON_PROFILE_P1);
    CHECK(get16(&rr) == (policy == 0 ? CANON_WORK_POLICY_PRUNE_1 : policy));
    CHECK(get16(&rr) == CANON_ENCODING_CDAG_2);
    CHECK(get16(&rr) == CANON_ORDER_CDAG_BYTE_1);
    CHECK(get32(&rr) == n);
    /* B(stream of x): the input, not the image; for a fixed point of G they coincide */
    const uint32_t slen = get32(&rr);
    CHECK(rr.len - rr.at >= slen);
    CHECK(slen >= 7 && memcmp(cert + rr.at, "\x43\x4e\x02", 3) == 0);
    rr.at += slen;
    CHECK(get32(&rr) == gen_count);
    for (uint32_t j = 0; j < gen_count; ++j) {
        for (uint32_t v = 0; v < n; ++v) {
            CHECK(get32(&rr) == gens[j * n + v]);
        }
    }
    /* the final claim, parsed from the end: U64(nodes), dense(t), B(bytes), B(trace) */
    size_t tl = 0, bl = 0;
    const uint8_t *trace = canon_result_trace(r, &tl);
    const uint8_t *bytes = canon_result_bytes(r, &bl);
    uint32_t deg = 0;
    const uint32_t *w = canon_result_witness(r, &deg);
    const size_t tail = 8u + 4u * (size_t)n + 4u + bl + 4u + tl;
    CHECK(len >= tail);
    rd fr = {cert, len, len - tail};
    CHECK(get32(&fr) == tl && memcmp(cert + fr.at, trace, tl) == 0);
    fr.at += tl;
    CHECK(get32(&fr) == bl && memcmp(cert + fr.at, bytes, bl) == 0);
    fr.at += bl;
    for (uint32_t v = 0; v < n; ++v) {
        CHECK(get32(&fr) == w[v]);
    }
    CHECK(get32(&fr) == 0); /* nodes < 2^32 here */
    const uint32_t nodes = get32(&fr);
    CHECK(nodes >= 1 && fr.at == len);
    /* a second solve without the option carries none and is TRUSTED_ENGINE */
    canon_result *plain = solve(b, false, CANON_OBJECTIVE_CANONICAL_IMAGE, policy, &st);
    CHECK(st == CANON_COMPLETE && canon_result_certificate(plain, &len) == NULL && len == 0);
    CHECK(canon_result_evidence(plain) == CANON_EVIDENCE_TRUSTED_ENGINE);
    CHECK(canon_result_evidence(NULL) == CANON_EVIDENCE_TRUSTED_ENGINE);
    CHECK(canon_result_certificate(NULL, &len) == NULL && len == 0);
    canon_result_release(plain);
    canon_result_release(r);
}

static void golden_certificates(void)
{
    /* spec 7.4 subset cases */
    const uint32_t swap[2] = {1, 0}, one[1] = {0};
    const struct {
        uint32_t n;
        const uint32_t *gens;
        uint32_t gen_count;
        const uint32_t *atoms;
        uint32_t k;
    } subsets[4] = {{0, NULL, 0, NULL, 0}, {2, swap, 1, one, 1}, {2, NULL, 0, one, 1},
                    {2, swap, 1, NULL, 0}};
    for (size_t i = 0; i < 4; ++i) {
        built b = {NULL, NULL};
        CHECK(canon_group_create(ctx, subsets[i].n, subsets[i].gens, subsets[i].gen_count, &b.g) ==
              CANON_COMPLETE);
        CHECK(canon_object_create_subset(ctx, subsets[i].n, subsets[i].atoms, subsets[i].k, &b.x) ==
              CANON_COMPLETE);
        check_certificate(&b, CANON_WORK_POLICY_UNPRUNED, subsets[i].n, subsets[i].gens,
                          subsets[i].gen_count);
        check_certificate(&b, CANON_WORK_POLICY_PRUNE_1, subsets[i].n, subsets[i].gens,
                          subsets[i].gen_count);
        check_certificate(&b, 0, subsets[i].n, subsets[i].gens, subsets[i].gen_count);
        canon_group_release(b.g);
        canon_object_release(b.x);
    }
    /* spec 7.4 graph case: one unit arc 0 -> 1 under Sym(2) */
    const canon_arc arc = {0, 1, NULL, 0, 1};
    built g = {NULL, NULL};
    CHECK(canon_group_create(ctx, 2, swap, 1, &g.g) == CANON_COMPLETE);
    CHECK(canon_object_create_graph(ctx, 2, NULL, NULL, &arc, 1, &g.x) == CANON_COMPLETE);
    check_certificate(&g, CANON_WORK_POLICY_PRUNE_1, 2, swap, 1);
    canon_group_release(g.g);
    canon_object_release(g.x);
}

static void statuses(void)
{
    const uint32_t swap[2] = {1, 0};
    built b = {NULL, NULL};
    CHECK(canon_group_create(ctx, 2, swap, 1, &b.g) == CANON_COMPLETE);
    CHECK(canon_object_create_subset(ctx, 2, NULL, 0, &b.x) == CANON_COMPLETE);
    canon_status st = CANON_COMPLETE;
    /* a certificate for another objective is unsupported (v1) */
    canon_result *r = solve(&b, true, CANON_OBJECTIVE_STABILISER, 0, &st);
    CHECK(st == CANON_UNSUPPORTED_ACTION && r == NULL);
    canon_object_release(b.x);
    /* a nested root is unsupported (v0): the tuple (b, b) of spec 7.4 on degree 2 */
    static const uint8_t tuple[] = {0x43, 0x4e, 0x02, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00,
                                    0x00, 0x02, 0x00, 0x00, 0x00, 0x02, 0x02, 0x00, 0x00,
                                    0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00,
                                    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01};
    CHECK(canon_object_create(ctx, CANON_SCHEMA_EXT_DAG_1, CANON_ACTION_ATOM_TRANSPORT_1, 2,
                              tuple, sizeof tuple, &b.x) == CANON_COMPLETE);
    r = solve(&b, true, CANON_OBJECTIVE_CANONICAL_IMAGE, 0, &st);
    CHECK(st == CANON_UNSUPPORTED_ACTION && r == NULL);
    r = solve(&b, false, CANON_OBJECTIVE_CANONICAL_IMAGE, 0, &st); /* without one: fine */
    CHECK(st == CANON_COMPLETE);
    canon_result_release(r);
    canon_object_release(b.x);
    /* a capacity-limited run has no certificate */
    CHECK(canon_object_create_subset(ctx, 2, NULL, 0, &b.x) == CANON_COMPLETE);
    {
        canon_capacity cap = {0, 0, 1, 0, 0, 0, 0, CANON_WORK_POLICY_UNPRUNED};
        canon_problem_options opts = {CANON_WITNESS_ANY, NULL, true};
        canon_problem *p = NULL;
        canon_workspace *ws = NULL;
        CHECK(canon_problem_create_with_options(ctx, b.g, b.x, NULL, CANON_OBJECTIVE_CANONICAL_IMAGE,
                                                CANON_PROFILE_P1, CANON_ENCODING_CDAG_2,
                                                CANON_ORDER_CDAG_BYTE_1, &cap, &opts,
                                                &p) == CANON_COMPLETE);
        CHECK(canon_workspace_create(ctx, &ws) == CANON_COMPLETE);
        CHECK(canon_solve(ws, p, &r) == CANON_CAPACITY_LIMIT);
        size_t len = 7;
        CHECK(canon_result_certificate(r, &len) == NULL && len == 0);
        CHECK(canon_result_evidence(r) == CANON_EVIDENCE_TRUSTED_ENGINE);
        canon_result_release(r);
        /* the same workspace then produces a complete certificate */
        canon_problem_release(p);
        cap.max_search_nodes = 0;
        CHECK(canon_problem_create_with_options(ctx, b.g, b.x, NULL, CANON_OBJECTIVE_CANONICAL_IMAGE,
                                                CANON_PROFILE_P1, CANON_ENCODING_CDAG_2,
                                                CANON_ORDER_CDAG_BYTE_1, &cap, &opts,
                                                &p) == CANON_COMPLETE);
        CHECK(canon_solve(ws, p, &r) == CANON_COMPLETE);
        CHECK(canon_result_certificate(r, &len) != NULL && len > 0);
        canon_result_release(r);
        canon_problem_release(p);
        canon_workspace_release(ws);
    }
    canon_object_release(b.x);
    canon_group_release(b.g);
}

int main(void)
{
    CHECK(canon_context_create(NULL, &ctx) == CANON_COMPLETE);
    chain_round_trip();
    golden_certificates();
    statuses();
    canon_context_release(ctx);
    return check_finish("test_certificate");
}
