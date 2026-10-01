/* Slice S5 tests of the ATOM-TRANSPORT-1 action on nested objects (spec 2.1, 4.2, 7.4, 9.4;
 * docs/slices/S5.md 3.3, 4): the conjugation side of permutation leaves (g^-1 p g, pinned on
 * non-commuting elements with a g of order 3, since for the involution g = [0,2,1] of the
 * brief both sides coincide), the spec 7.4 cycle vector (0,2)^(pq) = (2,1), relabelled nested
 * sets re-sorting their children, (x^p)^q = x^(pq) on random DAGs with every leaf type,
 * x^g against an independent construction of the image (atoms relabelled and generators
 * conjugated before normalisation), presentation-independent group-leaf payloads, and the
 * labeling-coset action (g^-1 H g)(g^-1 r) against brute force.  Expected hex lives here. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "encoding/cdag_encode.h"
#include "encoding/group_stream.h"
#include "object/dag.h"
#include "perm/perm.h"

static canon_dag_scratch S;

static void put_atom(canon_dag *d, uint32_t a, uint32_t *index)
{
    uint8_t p[4] = {(uint8_t)(a >> 24), (uint8_t)(a >> 16), (uint8_t)(a >> 8), (uint8_t)a};
    CHECK(canon_dag_append(d, CANON_REC_ATOM, p, 4, NULL, NULL, 0, index) == CANON_COMPLETE);
}

static void put_perm(canon_dag *d, const uint32_t *p, uint32_t n, uint32_t *index)
{
    canon_buf b;
    canon_buf_init(&b);
    CHECK(canon_perm_bytes_write(&b, p, n) == CANON_COMPLETE);
    CHECK(canon_dag_append(d, CANON_REC_PERM, b.data, b.len, NULL, NULL, 0, index) ==
          CANON_COMPLETE);
    canon_buf_free(&b);
}

/* A rule-2 Group payload listing the k generators (any presentation), optionally followed by
 * Perm(r) for a coset. */
static void put_group(canon_dag *d, const uint32_t *gens, uint32_t k, uint32_t n, const uint32_t *r,
                      uint32_t *index)
{
    canon_buf b;
    canon_buf_init(&b);
    CHECK(canon_buf_put_u8(&b, 0x00) == CANON_COMPLETE && canon_buf_put_u32(&b, k) == 0);
    for (uint32_t i = 0; i < k; ++i) {
        CHECK(canon_perm_bytes_write(&b, gens + (size_t)i * n, n) == CANON_COMPLETE);
    }
    if (r != NULL) {
        CHECK(canon_perm_bytes_write(&b, r, n) == CANON_COMPLETE);
    }
    CHECK(canon_dag_append(d, r != NULL ? CANON_REC_COSET : CANON_REC_GROUP, b.data, b.len, NULL,
                           NULL, 0, index) == CANON_COMPLETE);
    canon_buf_free(&b);
}

static void stream_of(const canon_dag *d, canon_buf *out)
{
    canon_buf_truncate(out, 0);
    CHECK(canon_dag_stream_write(out, d) == CANON_COMPLETE);
}

static int buf_eq(const canon_buf *a, const canon_buf *b)
{
    return a->len == b->len && (a->len == 0 || memcmp(a->data, b->data, a->len) == 0);
}

/* The image of the permutation leaf p under g, as an array. */
static void act_perm(const uint32_t *p, const uint32_t *g, uint32_t n, uint32_t *out)
{
    canon_dag d, norm, img;
    canon_dag_init(&d, n);
    canon_dag_init(&norm, 0);
    canon_dag_init(&img, 0);
    uint32_t r = 0;
    put_perm(&d, p, n, &r);
    d.root = r;
    CHECK(canon_dag_normalise(&d, &norm, &S, false) == CANON_COMPLETE);
    CHECK(canon_dag_act(&norm, g, &img, &S) == CANON_COMPLETE);
    /* decode the image's Perm payload: U32(s), then pairs */
    const uint8_t *q = canon_dag_payload(&img, img.root);
    for (uint32_t v = 0; v < n; ++v) {
        out[v] = v;
    }
    uint32_t s = (uint32_t)q[3];
    for (uint32_t i = 0; i < s; ++i) {
        out[q[4 + 8 * i + 3]] = q[4 + 8 * i + 7];
    }
    canon_dag_free(&d);
    canon_dag_free(&norm);
    canon_dag_free(&img);
}

/* spec 2.1: "A permutation object p becomes g^-1 p g". */
static void test_conjugation_side(void)
{
    const uint32_t p[3] = {1, 0, 2};
    uint32_t out[3], ginv[3], t[3], left[3], right[3];
    /* the brief's pair: g = [0,2,1]; the cycle (0 1) relabelled through g is (0 2) = [2,1,0] */
    const uint32_t g[3] = {0, 2, 1};
    act_perm(p, g, 3, out);
    CHECK(out[0] == 2 && out[1] == 1 && out[2] == 0);
    /* p and g do not commute (spec 7.4: pq = [2,0,1], qp = [1,2,0]), but g is an involution,
     * so g^-1 p g = g p g^-1 and this pair cannot tell the sides apart.  A g of order 3 can:
     * g = [1,2,0], g^-1 = [2,0,1]: g^-1 p g = [0,2,1] (the cycle (0 1) relabelled 0 -> 1,
     * 1 -> 2 is (1 2)), while g p g^-1 = [2,1,0]. */
    const uint32_t h[3] = {1, 2, 0};
    act_perm(p, h, 3, out);
    CHECK(out[0] == 0 && out[1] == 2 && out[2] == 1);
    canon_perm_inverse(h, ginv, 3);
    canon_perm_compose(ginv, p, t, 3); /* g^-1 p: g^-1 first (spec 3: (pq)[v] = q[p[v]]) */
    canon_perm_compose(t, h, left, 3); /* (g^-1 p) g */
    canon_perm_compose(h, p, t, 3);
    canon_perm_compose(t, ginv, right, 3); /* g p g^-1 */
    CHECK(memcmp(out, left, sizeof out) == 0 && memcmp(out, right, sizeof out) != 0);
    /* spec 7.4: "(0,2)^(pq)=(2,1)", read as the cycle (0 2) = [2,1,0] under pq = [2,0,1]: the
     * cycle (2 1) = [0,2,1] */
    const uint32_t c02[3] = {2, 1, 0}, pq[3] = {2, 0, 1};
    act_perm(c02, pq, 3, out);
    CHECK(out[0] == 0 && out[1] == 2 && out[2] == 1);
    /* equivariance with the atoms: the pair (a, p) with p[a] = b maps to (g[a], q) with
     * q[g[a]] = g[b], for every a, on random permutations */
    for (int trial = 0; trial < 200; ++trial) {
        uint32_t n = 1 + (uint32_t)(check_rng() % 7), pp[7], gg[7], qq[7];
        for (uint32_t v = 0; v < n; ++v) {
            pp[v] = gg[v] = v;
        }
        for (uint32_t v = n; v > 1; --v) {
            uint32_t j = (uint32_t)(check_rng() % v), x = pp[v - 1];
            pp[v - 1] = pp[j];
            pp[j] = x;
            j = (uint32_t)(check_rng() % v);
            x = gg[v - 1];
            gg[v - 1] = gg[j];
            gg[j] = x;
        }
        act_perm(pp, gg, n, qq);
        for (uint32_t a = 0; a < n; ++a) {
            CHECK(qq[gg[a]] == gg[pp[a]]);
        }
    }
}

/* Relabelling changes the order of records within a height and of children within a set; the
 * image is re-normalised (spec 4.2). */
static void test_nested_resort(void)
{
    canon_dag d, norm, img, direct, dnorm;
    canon_dag_init(&d, 2);
    canon_dag_init(&norm, 0);
    canon_dag_init(&img, 0);
    canon_dag_init(&direct, 2);
    canon_dag_init(&dnorm, 0);
    canon_buf a, b;
    canon_buf_init(&a);
    canon_buf_init(&b);
    /* x = {(0,1), (1,1)}; g = [1,0] gives {(1,0), (0,0)}, whose records sort the other way */
    uint32_t a0 = 0, a1 = 0, t01 = 0, t11 = 0, top = 0;
    put_atom(&d, 0, &a0);
    put_atom(&d, 1, &a1);
    uint32_t k01[2] = {a0, a1}, k11[2] = {a1, a1};
    CHECK(canon_dag_append(&d, CANON_REC_TUPLE, NULL, 0, k01, NULL, 2, &t01) == 0);
    CHECK(canon_dag_append(&d, CANON_REC_TUPLE, NULL, 0, k11, NULL, 2, &t11) == 0);
    uint32_t ks[2] = {t01, t11};
    CHECK(canon_dag_append(&d, CANON_REC_SET, NULL, 0, ks, NULL, 2, &top) == 0);
    d.root = top;
    CHECK(canon_dag_normalise(&d, &norm, &S, false) == CANON_COMPLETE);
    stream_of(&norm, &a);
    CHECK(check_hex_is(a.data, a.len,
                       "434e0200010001 00000002 00000005 01 00000000 01 00000001 "
                       "03 00000002 00000000 00000001 03 00000002 00000001 "
                       "00000001 04 00000002 00000002 00000003 00000004"));
    const uint32_t swap[2] = {1, 0};
    CHECK(canon_dag_act(&norm, swap, &img, &S) == CANON_COMPLETE);
    stream_of(&img, &a);
    CHECK(check_hex_is(a.data, a.len,
                       "434e0200010001 00000002 00000005 01 00000000 01 00000001 "
                       "03 00000002 00000000 00000000 03 00000002 00000001 "
                       "00000000 04 00000002 00000002 00000003 00000004"));
    /* the same value built directly */
    put_atom(&direct, 1, &a1);
    put_atom(&direct, 0, &a0);
    uint32_t k10[2] = {a1, a0}, k00[2] = {a0, a0};
    CHECK(canon_dag_append(&direct, CANON_REC_TUPLE, NULL, 0, k10, NULL, 2, &t01) == 0);
    CHECK(canon_dag_append(&direct, CANON_REC_TUPLE, NULL, 0, k00, NULL, 2, &t11) == 0);
    uint32_t kd[2] = {t01, t11};
    CHECK(canon_dag_append(&direct, CANON_REC_SET, NULL, 0, kd, NULL, 2, &top) == 0);
    direct.root = top;
    CHECK(canon_dag_normalise(&direct, &dnorm, &S, false) == CANON_COMPLETE);
    stream_of(&dnorm, &b);
    CHECK(buf_eq(&a, &b) && canon_dag_equal(&img, &dnorm));
    canon_buf_free(&a);
    canon_buf_free(&b);
    canon_dag_free(&d);
    canon_dag_free(&norm);
    canon_dag_free(&img);
    canon_dag_free(&direct);
    canon_dag_free(&dnorm);
}

/* ---- random DAGs with every leaf type, and an independent image construction ---- */

typedef struct rnode {
    uint8_t tag;
    uint32_t value; /* atom id or literal choice */
    uint32_t p[6];  /* permutation leaf, coset representative */
    uint32_t k;     /* children, or generators of a subgroup/coset leaf */
    uint32_t kid[3];
    uint64_t cnt[3];
    uint32_t gens[3][6];
} rnode;

static void random_perm(uint32_t n, uint32_t *p)
{
    for (uint32_t v = 0; v < n; ++v) {
        p[v] = v;
    }
    for (uint32_t v = n; v > 1; --v) {
        uint32_t j = (uint32_t)(check_rng() % v), t = p[v - 1];
        p[v - 1] = p[j];
        p[j] = t;
    }
}

static void random_value(uint32_t n, rnode *nodes, uint32_t m)
{
    for (uint32_t i = 0; i < m; ++i) {
        rnode *x = &nodes[i];
        memset(x, 0, sizeof *x);
        const uint32_t pick = (uint32_t)(check_rng() % 10);
        if (i == 0 || (i < m - 1 && pick < 5)) {
            static const uint8_t leaf_tags[5] = {CANON_REC_ATOM, CANON_REC_LITERAL, CANON_REC_PERM,
                                                 CANON_REC_GROUP, CANON_REC_COSET};
            x->tag = leaf_tags[check_rng() % 5];
            x->value = (uint32_t)(check_rng() % (x->tag == CANON_REC_ATOM ? n : 3));
            random_perm(n, x->p);
            x->k = (uint32_t)(check_rng() % 3);
            for (uint32_t j = 0; j < x->k; ++j) {
                if (check_rng() % 2 == 0) {
                    random_perm(n, x->gens[j]);
                } else { /* a transposition, so that small subgroups occur */
                    for (uint32_t v = 0; v < n; ++v) {
                        x->gens[j][v] = v;
                    }
                    uint32_t a = (uint32_t)(check_rng() % n), b = (uint32_t)(check_rng() % n);
                    x->gens[j][a] = b;
                    x->gens[j][b] = a;
                }
            }
            continue;
        }
        x->tag = (uint8_t)(CANON_REC_TUPLE + check_rng() % 3);
        x->k = (uint32_t)(check_rng() % 4);
        x->k = x->k > 3 ? 3 : x->k;
        for (uint32_t j = 0; j < x->k; ++j) {
            x->kid[j] = (uint32_t)(check_rng() % i);
            x->cnt[j] = 1 + check_rng() % 3;
        }
    }
}

/* Build the raw arena of the value, transformed by g (NULL = identity) before encoding:
 * atoms a -> g[a], permutation leaves q[g[v]] = g[p[v]], subgroup generators likewise, coset
 * representatives r'[g[v]] = r[v] (spec 2.1), so that normalisation alone gives x^g. */
static uint32_t build(canon_dag *d, uint32_t n, const rnode *nodes, uint32_t m, const uint32_t *g)
{
    static const char *const LITS[3] = {"", "a", "ab"};
    uint32_t rec[32];
    canon_dag_reset(d, n);
    for (uint32_t i = 0; i < m; ++i) {
        const rnode *x = &nodes[i];
        uint32_t tmp[6], gens[18];
        switch (x->tag) {
        case CANON_REC_ATOM:
            put_atom(d, g != NULL ? g[x->value] : x->value, &rec[i]);
            break;
        case CANON_REC_LITERAL: {
            uint8_t p[8] = {0, 0, 0, (uint8_t)strlen(LITS[x->value])};
            memcpy(p + 4, LITS[x->value], strlen(LITS[x->value]));
            CHECK(canon_dag_append(d, CANON_REC_LITERAL, p, 4 + strlen(LITS[x->value]), NULL, NULL,
                                   0, &rec[i]) == CANON_COMPLETE);
            break;
        }
        case CANON_REC_PERM:
            for (uint32_t v = 0; v < n; ++v) {
                tmp[g != NULL ? g[v] : v] = g != NULL ? g[x->p[v]] : x->p[v];
            }
            put_perm(d, tmp, n, &rec[i]);
            break;
        case CANON_REC_GROUP:
        case CANON_REC_COSET:
            for (uint32_t j = 0; j < x->k; ++j) {
                for (uint32_t v = 0; v < n; ++v) {
                    gens[j * n + (g != NULL ? g[v] : v)] =
                        g != NULL ? g[x->gens[j][v]] : x->gens[j][v];
                }
            }
            for (uint32_t v = 0; v < n; ++v) {
                tmp[g != NULL ? g[v] : v] = x->p[v]; /* g^-1 r */
            }
            put_group(d, gens, x->k, n, x->tag == CANON_REC_COSET ? tmp : NULL, &rec[i]);
            break;
        default: {
            uint32_t kids[3];
            uint64_t cnt[3];
            uint32_t w = 0;
            for (uint32_t j = 0; j < x->k; ++j) {
                kids[w] = rec[x->kid[j]];
                cnt[w] = x->cnt[j];
                ++w;
            }
            if (x->tag != CANON_REC_TUPLE) { /* strictly increasing references, merged */
                for (uint32_t a = 0; a < w; ++a) {
                    for (uint32_t b = a + 1; b < w; ++b) {
                        if (kids[b] < kids[a]) {
                            uint32_t t = kids[a];
                            kids[a] = kids[b];
                            kids[b] = t;
                            uint64_t c = cnt[a];
                            cnt[a] = cnt[b];
                            cnt[b] = c;
                        }
                    }
                }
                uint32_t u = 0;
                for (uint32_t a = 0; a < w; ++a) {
                    if (u > 0 && kids[u - 1] == kids[a]) {
                        cnt[u - 1] += cnt[a];
                    } else {
                        kids[u] = kids[a];
                        cnt[u++] = cnt[a];
                    }
                }
                w = u;
            }
            CHECK(canon_dag_append(d, x->tag, NULL, 0, kids,
                                   x->tag == CANON_REC_MULTISET ? cnt : NULL, w,
                                   &rec[i]) == CANON_COMPLETE);
            break;
        }
        }
    }
    return rec[m - 1];
}

static void test_random_actions(void)
{
    canon_dag raw, x, xp, xpq, xr, direct, dnorm;
    canon_dag_init(&raw, 0);
    canon_dag_init(&x, 0);
    canon_dag_init(&xp, 0);
    canon_dag_init(&xpq, 0);
    canon_dag_init(&xr, 0);
    canon_dag_init(&direct, 0);
    canon_dag_init(&dnorm, 0);
    canon_buf a, b;
    canon_buf_init(&a);
    canon_buf_init(&b);
    rnode nodes[16];
    for (int trial = 0; trial < 250; ++trial) {
        uint32_t n = 1 + (uint32_t)(check_rng() % 5);
        uint32_t m = 2 + (uint32_t)(check_rng() % 14);
        random_value(n, nodes, m);
        raw.root = build(&raw, n, nodes, m, NULL);
        CHECK(canon_dag_normalise(&raw, &x, &S, false) == CANON_COMPLETE);
        uint32_t p[6], q[6], pq[6], id[6];
        random_perm(n, p);
        random_perm(n, q);
        canon_perm_compose(p, q, pq, n); /* spec 3: (pq)[v] = q[p[v]] */
        for (uint32_t v = 0; v < n; ++v) {
            id[v] = v;
        }
        /* x^id = x */
        CHECK(canon_dag_act(&x, id, &xr, &S) == CANON_COMPLETE && canon_dag_equal(&x, &xr));
        /* spec 3: (x^p)^q = x^(pq) */
        CHECK(canon_dag_act(&x, p, &xp, &S) == CANON_COMPLETE);
        CHECK(canon_dag_act(&xp, q, &xpq, &S) == CANON_COMPLETE);
        CHECK(canon_dag_act(&x, pq, &xr, &S) == CANON_COMPLETE);
        CHECK(canon_dag_equal(&xpq, &xr));
        /* x^p against the independent construction */
        direct.root = build(&direct, n, nodes, m, p);
        CHECK(canon_dag_normalise(&direct, &dnorm, &S, false) == CANON_COMPLETE);
        stream_of(&xp, &a);
        stream_of(&dnorm, &b);
        CHECK(buf_eq(&a, &b));
        CHECK(xp.count == x.count && xp.refs == x.refs);
    }
    canon_buf_free(&a);
    canon_buf_free(&b);
    canon_dag_free(&raw);
    canon_dag_free(&x);
    canon_dag_free(&xp);
    canon_dag_free(&xpq);
    canon_dag_free(&xr);
    canon_dag_free(&direct);
    canon_dag_free(&dnorm);
}

/* ---- subgroup and coset leaves against brute force ---- */

/* The elements of <gens> (k generators of degree n <= 5) by closure; returns the order. */
static uint32_t closure(const uint32_t *gens, uint32_t k, uint32_t n, uint32_t *elts)
{
    uint32_t count = 1;
    for (uint32_t v = 0; v < n; ++v) {
        elts[v] = v;
    }
    for (uint32_t i = 0; i < count; ++i) {
        for (uint32_t j = 0; j < k; ++j) {
            uint32_t c[5];
            canon_perm_compose(elts + (size_t)i * n, gens + (size_t)j * n, c, n);
            uint32_t e = 0;
            while (e < count && memcmp(elts + (size_t)e * n, c, n * sizeof *c) != 0) {
                ++e;
            }
            if (e == count) {
                memcpy(elts + (size_t)count * n, c, n * sizeof *c);
                ++count;
            }
        }
    }
    return count;
}

/* Canonical Group(H) payload of <gens> through a verified chain and the S4 writer. */
static void group_payload(const uint32_t *gens, uint32_t k, uint32_t n, canon_buf *out)
{
    canon_bsgs c;
    canon_bsgs_init(&c, n);
    CHECK(canon_bsgs_build_verified(&c, n, gens, k) == CANON_COMPLETE);
    canon_buf_truncate(out, 0);
    CHECK(canon_group_bytes_write(out, &c, NULL, NULL) == CANON_COMPLETE);
    canon_bsgs_free(&c);
}

static void test_group_leaves(void)
{
    canon_dag d, x, img;
    canon_dag_init(&d, 0);
    canon_dag_init(&x, 0);
    canon_dag_init(&img, 0);
    canon_buf want;
    canon_buf_init(&want);
    static uint32_t elts[120 * 5];
    for (int trial = 0; trial < 300; ++trial) {
        const uint32_t n = 1 + (uint32_t)(check_rng() % 5);
        uint32_t gens[3 * 5], conj[3 * 5], g[5], r[5], k = 1 + (uint32_t)(check_rng() % 3);
        for (uint32_t j = 0; j < k; ++j) {
            random_perm(n, gens + j * n);
            if (check_rng() % 2 == 0) { /* a transposition instead */
                uint32_t a = (uint32_t)(check_rng() % n), b = (uint32_t)(check_rng() % n);
                for (uint32_t v = 0; v < n; ++v) {
                    gens[j * n + v] = v;
                }
                gens[j * n + a] = b;
                gens[j * n + b] = a;
            }
        }
        random_perm(n, g);
        random_perm(n, r);
        const uint32_t order = closure(gens, k, n, elts);
        /* presentation independence: H generated by k random elements of H plus the given
         * generators in reverse order has the same payload */
        uint32_t alt[6 * 5], ak = 0;
        for (uint32_t j = k; j-- > 0;) {
            memcpy(alt + ak++ * n, gens + j * n, n * sizeof *alt);
        }
        for (uint32_t j = 0; j < 3; ++j) {
            memcpy(alt + ak++ * n, elts + (check_rng() % order) * n, n * sizeof *alt);
        }
        for (int coset = 0; coset < 2; ++coset) {
            canon_dag_reset(&d, n);
            uint32_t r1 = 0, r2 = 0;
            put_group(&d, gens, k, n, coset ? r : NULL, &r1);
            d.root = r1;
            CHECK(canon_dag_normalise(&d, &x, &S, false) == CANON_COMPLETE);
            canon_dag_reset(&d, n);
            put_group(&d, alt, ak, n, coset ? r : NULL, &r2);
            d.root = r2;
            CHECK(canon_dag_normalise(&d, &img, &S, false) == CANON_COMPLETE);
            CHECK(canon_dag_equal(&x, &img));
            /* the image under g: g^-1 H g, and for a coset (g^-1 H g)(g^-1 r) */
            CHECK(canon_dag_act(&x, g, &img, &S) == CANON_COMPLETE);
            for (uint32_t j = 0; j < k; ++j) {
                for (uint32_t v = 0; v < n; ++v) {
                    conj[j * n + g[v]] = g[gens[j * n + v]]; /* g^-1 p g */
                }
            }
            group_payload(conj, k, n, &want);
            if (coset) {
                /* brute force: the least element of {g^-1 h r : h in H} */
                uint32_t ginv[5], best[5], t[5], u[5];
                canon_perm_inverse(g, ginv, n);
                for (uint32_t e = 0; e < order; ++e) {
                    canon_perm_compose(ginv, elts + e * n, t, n);
                    canon_perm_compose(t, r, u, n); /* g^-1 h r */
                    if (e == 0 || canon_perm_lex_compare(u, best, n) < 0) {
                        memcpy(best, u, sizeof best);
                    }
                }
                CHECK(canon_perm_bytes_write(&want, best, n) == CANON_COMPLETE);
            }
            CHECK(img.recs[0].payload_len == want.len &&
                  memcmp(canon_dag_payload(&img, 0), want.data, want.len) == 0);
        }
    }
    canon_buf_free(&want);
    canon_dag_free(&d);
    canon_dag_free(&x);
    canon_dag_free(&img);
}

int main(void)
{
    canon_dag_scratch_init(&S);
    test_conjugation_side();
    test_nested_resort();
    test_random_actions();
    test_group_leaves();
    canon_dag_scratch_free(&S);
    return check_finish("test_dag_action");
}
