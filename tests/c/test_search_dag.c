/* Slice S5 tests through the public API (spec 3.2, 4.1, 4.2, 7.1, 7.2, 7.4, 11.1, 17;
 * docs/slices/S5.md 2, 3.4, 3.6, 4): canon_object_create statuses and limits, the spec 7.4
 * (b,b) case, a subset imported from a stream and a graph imported from a stream give exactly
 * the trace, bytes and witness of the builders' objects (every T1 group), P1 on nested roots
 * (equivariance, valid witnesses, canonical output that validates, the deterministic witness),
 * the enumeration objectives on nested objects, canon_stream_validate, capacity at problem
 * creation (records, references, literal bytes, the output bound of subgroup leaves), and
 * workspace reuse across object kinds.  Expected hex lives here, never in src/. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "canon/canon.h"
#include "check.h"
#include "encoding/cdag_decode.h"
#include "encoding/cdag_encode.h"
#include "encoding/group_stream.h"
#include "object/dag.h"
#include "object/object.h"
#include "search/p1_tree.h"
#include "t1_groups.h"

static canon_context *CTX;

static size_t unhex(const char *hex, uint8_t *buf, size_t cap)
{
    size_t len = 0;
    int hi = -1;
    for (const char *c = hex; *c != '\0'; ++c) {
        int v = *c >= '0' && *c <= '9' ? *c - '0' : (*c >= 'a' && *c <= 'f' ? *c - 'a' + 10 : -1);
        if (v < 0) {
            continue;
        }
        if (hi < 0) {
            hi = v;
        } else {
            if (len < cap) {
                buf[len] = (uint8_t)(hi * 16 + v);
            }
            ++len;
            hi = -1;
        }
    }
    return len;
}

static canon_status from_hex(uint32_t n, const char *hex, canon_object **out)
{
    uint8_t buf[1024];
    size_t len = unhex(hex, buf, sizeof buf);
    return canon_object_create(CTX, CANON_SCHEMA_EXT_DAG_1, CANON_ACTION_ATOM_TRANSPORT_1, n, buf,
                               len, out);
}

/* Solve objective (CANONICAL_IMAGE under P1, or an S4 objective under NO_TREE). */
static canon_status solve(canon_workspace *ws, const canon_group *g, const canon_object *x,
                          const canon_object *y, canon_objective objective,
                          const canon_capacity *cap, canon_witness_mode mode, canon_result **out)
{
    canon_problem *p = NULL;
    const canon_problem_options opts = {mode, NULL};
    *out = NULL;
    canon_status st = canon_problem_create_with_options(
        CTX, g, x, y, objective,
        objective == CANON_OBJECTIVE_CANONICAL_IMAGE ? CANON_PROFILE_P1 : CANON_PROFILE_NO_TREE,
        CANON_ENCODING_CDAG_2, CANON_ORDER_CDAG_BYTE_1, cap, &opts, &p);
    if (st == CANON_COMPLETE) {
        st = canon_solve(ws, p, out);
    }
    canon_problem_release(p);
    return st;
}

static canon_status p1(canon_workspace *ws, const canon_group *g, const canon_object *x,
                       canon_result **out)
{
    return solve(ws, g, x, NULL, CANON_OBJECTIVE_CANONICAL_IMAGE, NULL, CANON_WITNESS_ANY, out);
}

static int same_result(const canon_result *a, const canon_result *b)
{
    size_t la = 0, lb = 0, ba = 0, bb = 0;
    const uint8_t *ta = canon_result_trace(a, &la), *tb = canon_result_trace(b, &lb);
    const uint8_t *ya = canon_result_bytes(a, &ba), *yb = canon_result_bytes(b, &bb);
    uint32_t da = 0, db = 0;
    const uint32_t *wa = canon_result_witness(a, &da), *wb = canon_result_witness(b, &db);
    return ta != NULL && tb != NULL && la == lb && memcmp(ta, tb, la) == 0 && ya != NULL &&
           yb != NULL && ba == bb && memcmp(ya, yb, ba) == 0 && wa != NULL && wb != NULL &&
           da == db && (da == 0 || memcmp(wa, wb, da * sizeof *wa) == 0);
}

/* A growable hex-free byte writer for streams built in tests. */
typedef struct sbuf {
    uint8_t b[4096];
    size_t len;
} sbuf;

static void u8(sbuf *s, uint8_t v)
{
    s->b[s->len++] = v;
}

static void u32(sbuf *s, uint32_t v)
{
    u8(s, (uint8_t)(v >> 24));
    u8(s, (uint8_t)(v >> 16));
    u8(s, (uint8_t)(v >> 8));
    u8(s, (uint8_t)v);
}

static void header(sbuf *s, uint32_t n, uint32_t q)
{
    static const uint8_t h[7] = {0x43, 0x4e, 0x02, 0x00, 0x01, 0x00, 0x01};
    s->len = 0;
    for (int i = 0; i < 7; ++i) {
        u8(s, h[i]);
    }
    u32(s, n);
    u32(s, q);
}

static canon_status create(uint32_t n, const sbuf *s, canon_object **out)
{
    return canon_object_create(CTX, CANON_SCHEMA_EXT_DAG_1, CANON_ACTION_ATOM_TRANSPORT_1, n, s->b,
                               s->len, out);
}

/* spec 7.4: "n=0, x=(b,b), b=empty literal ... Trace equals n=0 case above." */
static void test_bb(canon_workspace *ws)
{
    static const char *const STREAMS[2] = {
        "434e0200010001 00000000 00000002 02 00000000 03 00000002 00000000 00000000 00000001",
        /* duplicate storage, an unreachable literal, the root not last */
        "434e0200010001 00000000 00000004 02 00000000 02 00000000 03 00000002 00000001 00000000 "
        "02 00000001 7a 00000002"};
    canon_group *g = NULL;
    CHECK(canon_group_create(CTX, 0, NULL, 0, &g) == CANON_COMPLETE);
    for (int i = 0; i < 2; ++i) {
        canon_object *x = NULL;
        CHECK(from_hex(0, STREAMS[i], &x) == CANON_COMPLETE);
        canon_result *r = NULL;
        CHECK(p1(ws, g, x, &r) == CANON_COMPLETE);
        size_t tl = 0, bl = 0;
        const uint8_t *t = canon_result_trace(r, &tl), *b = canon_result_bytes(r, &bl);
        CHECK(t != NULL && check_hex_is(t, tl, "10 00000000 20 00000000 21 00000000 00"));
        CHECK(b != NULL && check_hex_is(b, bl, STREAMS[0]));
        uint32_t deg = 9;
        CHECK(canon_result_witness(r, &deg) != NULL && deg == 0);
        CHECK(canon_stream_validate(CTX, b, bl) == CANON_COMPLETE);
        uint8_t raw[256];
        size_t len = unhex(STREAMS[i], raw, sizeof raw);
        CHECK(canon_stream_validate(CTX, raw, len) ==
              (i == 0 ? CANON_COMPLETE : CANON_INVALID_INPUT));
        canon_result_release(r);
        canon_object_release(x);
    }
    canon_group_release(g);
}

/* canon_object_create statuses (canon.h order). */
static void test_create_statuses(void)
{
    canon_object *x = (canon_object *)&CTX;
    const char *ok = "434e0200010001 00000002 00000002 01 00000001 04 00000001 00000000 00000001";
    uint8_t buf[256];
    size_t len = unhex(ok, buf, sizeof buf);
    CHECK(canon_object_create(CTX, 2, CANON_ACTION_ATOM_TRANSPORT_1, 2, buf, len, &x) ==
          CANON_UNSUPPORTED_ACTION);
    CHECK(x == NULL);
    CHECK(canon_object_create(CTX, CANON_SCHEMA_EXT_DAG_1, 2, 2, buf, len, &x) ==
          CANON_UNSUPPORTED_ACTION);
    CHECK(canon_object_create(CTX, CANON_SCHEMA_EXT_DAG_1, CANON_ACTION_ATOM_TRANSPORT_1, 3, buf,
                              len, &x) == CANON_INVALID_INPUT); /* header n = 2 */
    CHECK(canon_object_create(CTX, CANON_SCHEMA_EXT_DAG_1, CANON_ACTION_ATOM_TRANSPORT_1, 5000, buf,
                              len, &x) == CANON_CAPACITY_LIMIT); /* above max_n */
    CHECK(canon_object_create(CTX, CANON_SCHEMA_EXT_DAG_1, CANON_ACTION_ATOM_TRANSPORT_1, 2, NULL,
                              len, &x) == CANON_INVALID_INPUT);
    CHECK(canon_object_create(CTX, CANON_SCHEMA_EXT_DAG_1, CANON_ACTION_ATOM_TRANSPORT_1, 2, buf,
                              len - 1, &x) == CANON_INVALID_INPUT);
    CHECK(canon_object_create(CTX, CANON_SCHEMA_EXT_DAG_1, CANON_ACTION_ATOM_TRANSPORT_1, 2, buf,
                              len, NULL) == CANON_INVALID_INPUT);
    /* relations and nested graphs: recognised, unsupported (later slices) */
    CHECK(from_hex(2, "434e0200010001 00000002 00000001 0a 00000000 00000000", &x) ==
          CANON_UNSUPPORTED_ACTION);
    CHECK(from_hex(1,
                   "434e0200010001 00000001 00000002 09 00000000 00000000 03 00000001 "
                   "00000000 00000001",
                   &x) == CANON_UNSUPPORTED_ACTION);
    CHECK(x == NULL);
    CHECK(from_hex(2, ok, &x) == CANON_COMPLETE && x != NULL);
    canon_object_release(x);
    /* limits of the normal form against the context defaults */
    canon_capacity d = {0, 0, 0, 0, 2, 1, 1, 0};
    canon_context *small = NULL;
    CHECK(canon_context_create(&d, &small) == CANON_COMPLETE);
    CHECK(canon_object_create(small, CANON_SCHEMA_EXT_DAG_1, CANON_ACTION_ATOM_TRANSPORT_1, 2, buf,
                              len, &x) == CANON_COMPLETE); /* 2 records, 1 reference */
    canon_object_release(x);
    len = unhex("434e0200010001 00000000 00000001 02 00000002 6162 00000000", buf, sizeof buf);
    CHECK(canon_object_create(small, CANON_SCHEMA_EXT_DAG_1, CANON_ACTION_ATOM_TRANSPORT_1, 0, buf,
                              len, &x) == CANON_CAPACITY_LIMIT); /* 2 literal bytes */
    len = unhex("434e0200010001 00000002 00000003 01 00000000 01 00000001 04 00000002 00000000 "
                "00000001 00000002",
                buf, sizeof buf);
    CHECK(canon_object_create(small, CANON_SCHEMA_EXT_DAG_1, CANON_ACTION_ATOM_TRANSPORT_1, 2, buf,
                              len, &x) == CANON_CAPACITY_LIMIT); /* 3 records */
    /* the limits apply to the normal form, not the raw stream: 4 raw records, 2 normalised */
    len = unhex("434e0200010001 00000002 00000004 01 00000001 01 00000001 02 00000000 "
                "04 00000002 00000000 00000001 00000003",
                buf, sizeof buf);
    CHECK(canon_object_create(small, CANON_SCHEMA_EXT_DAG_1, CANON_ACTION_ATOM_TRANSPORT_1, 2, buf,
                              len, &x) == CANON_COMPLETE);
    canon_object_release(x);
    canon_context_release(small);
    /* canon_stream_validate */
    CHECK(canon_stream_validate(CTX, NULL, 3) == CANON_INVALID_INPUT);
    CHECK(canon_stream_validate(CTX, buf, 0) == CANON_INVALID_INPUT);
    CHECK(canon_stream_validate(CTX, buf, len) == CANON_INVALID_INPUT); /* not canonical */
}

/* A subset (S1) and a graph (S2) imported from streams give exactly the builders' results on
 * every T1 group (docs/slices/S5.md 3.4); the streams are deliberately not canonical. */
static void test_kinds_from_streams(canon_workspace *ws)
{
    for (uint32_t n = 0; n <= 4; ++n) {
        t1_sym sym;
        t1_sym_init(&sym, n);
        t1_group groups[T1_MAX_GROUPS];
        const uint32_t count = t1_subgroups(&sym, groups);
        for (uint32_t gi = 0; gi < count; ++gi) {
            canon_group *g = NULL;
            CHECK(canon_group_create(CTX, n, groups[gi].gens, groups[gi].gen_count, &g) ==
                  CANON_COMPLETE);
            for (uint32_t mask = 0; mask < (1u << n); ++mask) {
                uint32_t atoms[4], k = 0;
                for (uint32_t a = 0; a < n; ++a) {
                    if (mask >> a & 1u) {
                        atoms[k++] = a;
                    }
                }
                canon_object *built = NULL, *imported = NULL;
                CHECK(canon_object_create_subset(CTX, n, atoms, k, &built) == CANON_COMPLETE);
                /* raw stream: a junk literal, each member twice in decreasing order, the set */
                sbuf s;
                header(&s, n, 2 * k + 2);
                u8(&s, 0x02);
                u32(&s, 1);
                u8(&s, 'j');
                for (uint32_t j = k; j-- > 0;) {
                    for (int c = 0; c < 2; ++c) {
                        u8(&s, 0x01);
                        u32(&s, atoms[j]);
                    }
                }
                u8(&s, 0x04);
                u32(&s, 2 * k);
                for (uint32_t j = 0; j < 2 * k; ++j) {
                    u32(&s, 1 + j);
                }
                u32(&s, 2 * k + 1);
                CHECK(create(n, &s, &imported) == CANON_COMPLETE);
                canon_result *ra = NULL, *rb = NULL;
                CHECK(p1(ws, g, built, &ra) == CANON_COMPLETE);
                CHECK(p1(ws, g, imported, &rb) == CANON_COMPLETE);
                CHECK(same_result(ra, rb));
                canon_result_release(ra);
                canon_result_release(rb);
                canon_object_release(built);
                canon_object_release(imported);
            }
            /* graphs: random arcs, colours from {"", "c"}, labels from {"", "a"} */
            for (int trial = 0; trial < 6 && n > 0; ++trial) {
                canon_arc arcs[8];
                const uint8_t c = 'c', lab = 'a';
                const uint8_t *colours[4];
                size_t lengths[4];
                size_t e = (size_t)(check_rng() % 8);
                for (size_t i = 0; i < e; ++i) {
                    arcs[i].source = (uint32_t)(check_rng() % n);
                    arcs[i].target = (uint32_t)(check_rng() % n);
                    arcs[i].label = &lab;
                    arcs[i].label_length = (size_t)(check_rng() % 2);
                    arcs[i].multiplicity = 1 + check_rng() % 2;
                }
                for (uint32_t v = 0; v < n; ++v) {
                    colours[v] = &c;
                    lengths[v] = (size_t)(check_rng() % 2);
                }
                canon_object *built = NULL, *imported = NULL;
                CHECK(canon_object_create_graph(CTX, n, colours, lengths, arcs, e, &built) ==
                      CANON_COMPLETE);
                /* raw: arcs in reverse input order (unsorted, repeats kept separate) */
                sbuf s;
                header(&s, n, 1);
                u8(&s, 0x09);
                for (uint32_t v = 0; v < n; ++v) {
                    u32(&s, (uint32_t)lengths[v]);
                    if (lengths[v] > 0) {
                        u8(&s, 'c');
                    }
                }
                u32(&s, (uint32_t)e);
                for (size_t i = e; i-- > 0;) {
                    u32(&s, arcs[i].source);
                    u32(&s, arcs[i].target);
                    u32(&s, (uint32_t)arcs[i].label_length);
                    if (arcs[i].label_length > 0) {
                        u8(&s, 'a');
                    }
                    u32(&s, 1);
                    u8(&s, (uint8_t)arcs[i].multiplicity);
                }
                u32(&s, 0);
                CHECK(create(n, &s, &imported) == CANON_COMPLETE);
                canon_result *ra = NULL, *rb = NULL;
                CHECK(p1(ws, g, built, &ra) == CANON_COMPLETE);
                CHECK(p1(ws, g, imported, &rb) == CANON_COMPLETE);
                CHECK(same_result(ra, rb));
                size_t bl = 0;
                const uint8_t *b = canon_result_bytes(rb, &bl);
                CHECK(canon_stream_validate(CTX, b, bl) == CANON_COMPLETE);
                canon_result_release(ra);
                canon_result_release(rb);
                canon_object_release(built);
                canon_object_release(imported);
            }
            canon_group_release(g);
        }
    }
}

/* Stream of a random nested value of degree n (tags 01..06, the root a tuple or a multiset so
 * that it is never a subset) into s. */
static void random_dag_stream(uint32_t n, sbuf *s)
{
    sbuf body;
    body.len = 0;
    uint32_t q = 0;
    const uint32_t leaves = 1 + (uint32_t)(check_rng() % 4);
    for (uint32_t i = 0; i < leaves; ++i, ++q) {
        const uint32_t pick = (uint32_t)(check_rng() % 3);
        if (pick == 0 && n > 0) {
            u8(&body, 0x01);
            u32(&body, (uint32_t)(check_rng() % n));
        } else if (pick == 1 && n > 1) {
            /* Perm of the transposition (a b), a < b */
            uint32_t a = (uint32_t)(check_rng() % (n - 1)),
                     b = a + 1 + (uint32_t)(check_rng() % (n - 1 - a));
            u8(&body, 0x06);
            u32(&body, 2);
            u32(&body, a);
            u32(&body, b);
            u32(&body, b);
            u32(&body, a);
        } else {
            u8(&body, 0x02);
            u32(&body, 1);
            u8(&body, (uint8_t)('a' + check_rng() % 2));
        }
    }
    const uint32_t inner = 1 + (uint32_t)(check_rng() % 3);
    for (uint32_t i = 0; i < inner; ++i, ++q) {
        const uint32_t k = 1 + (uint32_t)(check_rng() % 3);
        const uint8_t tag =
            (uint8_t)(i + 1 == inner ? (check_rng() % 2 ? 0x03 : 0x05) : 0x03 + check_rng() % 3);
        u8(&body, tag);
        if (tag == 0x03) {
            u32(&body, k);
            for (uint32_t j = 0; j < k; ++j) {
                u32(&body, (uint32_t)(check_rng() % q));
            }
        } else {
            /* strictly increasing distinct references below q */
            uint32_t refs[3], m = 0;
            for (uint32_t c = 0; c < q && m < k; ++c) {
                if (check_rng() % 2 == 0 || q - c <= k - m) {
                    refs[m++] = c;
                }
            }
            u32(&body, m);
            for (uint32_t j = 0; j < m; ++j) {
                u32(&body, refs[j]);
                if (tag == 0x05) {
                    u32(&body, 1);
                    u8(&body, (uint8_t)(1 + check_rng() % 3));
                }
            }
        }
    }
    header(s, n, q);
    memcpy(s->b + s->len, body.b, body.len);
    s->len += body.len;
    u32(s, q - 1);
}

/* P1 on nested roots: equivariance (spec 7.2: C(x^h) = C(x)), valid witnesses, canonical output
 * that validates, the deterministic witness equal to the least leaf witness (unpruned tree),
 * and the enumeration objectives (spec 8.2) on nested objects. */
static void test_dag_roots(canon_workspace *ws)
{
    canon_dag_scratch sc;
    canon_dag_scratch_init(&sc);
    canon_dag raw, x, img;
    canon_dag_init(&raw, 0);
    canon_dag_init(&x, 0);
    canon_dag_init(&img, 0);
    canon_buf moved;
    canon_buf_init(&moved);
    for (uint32_t n = 0; n <= 4; ++n) {
        t1_sym sym;
        t1_sym_init(&sym, n);
        t1_group groups[T1_MAX_GROUPS];
        const uint32_t count = t1_subgroups(&sym, groups);
        for (uint32_t gi = 0; gi < count; ++gi) {
            canon_group *g = NULL;
            CHECK(canon_group_create(CTX, n, groups[gi].gens, groups[gi].gen_count, &g) ==
                  CANON_COMPLETE);
            for (int trial = 0; trial < 3; ++trial) {
                sbuf s;
                random_dag_stream(n, &s);
                canon_object *xo = NULL;
                CHECK(create(n, &s, &xo) == CANON_COMPLETE);
                canon_result *r = NULL;
                CHECK(p1(ws, g, xo, &r) == CANON_COMPLETE);
                bool valid = false;
                CHECK(canon_result_verify_witness(r, &valid) == CANON_COMPLETE && valid);
                size_t bl = 0, tl = 0;
                const uint8_t *b = canon_result_bytes(r, &bl);
                const uint8_t *t = canon_result_trace(r, &tl);
                CHECK(canon_stream_validate(CTX, b, bl) == CANON_COMPLETE);
                /* the trace of a nested root is that of the empty-key root: the empty subset */
                canon_object *empty = NULL;
                CHECK(canon_object_create_subset(CTX, n, NULL, 0, &empty) == CANON_COMPLETE);
                canon_result *re = NULL;
                CHECK(p1(ws, g, empty, &re) == CANON_COMPLETE);
                size_t tel = 0;
                const uint8_t *te = canon_result_trace(re, &tel);
                CHECK(tel == tl && memcmp(te, t, tl) == 0);
                canon_result_release(re);
                canon_object_release(empty);
                /* the deterministic witness (spec 3) equals the least leaf witness */
                canon_result *rd = NULL;
                CHECK(solve(ws, g, xo, NULL, CANON_OBJECTIVE_CANONICAL_IMAGE, NULL,
                            CANON_WITNESS_DETERMINISTIC, &rd) == CANON_COMPLETE);
                CHECK(same_result(r, rd));
                canon_result_release(rd);
                /* equivariance: x^h for every h in G has the same canonical form */
                canon_cdag_reason reason = CANON_CDAG_OK;
                CHECK(canon_cdag_decode(s.b, s.len, &n, 4096, &raw, &reason) == CANON_COMPLETE);
                CHECK(canon_dag_normalise(&raw, &x, &sc, false) == CANON_COMPLETE);
                for (uint32_t e = 0; e < sym.count; ++e) {
                    if ((groups[gi].mask >> e & 1u) == 0) {
                        continue;
                    }
                    CHECK(canon_dag_act(&x, sym.elem[e], &img, &sc) == CANON_COMPLETE);
                    canon_buf_truncate(&moved, 0);
                    CHECK(canon_dag_stream_write(&moved, &img) == CANON_COMPLETE);
                    canon_object *xh = NULL;
                    CHECK(canon_object_create(CTX, 1, 1, n, moved.data, moved.len, &xh) ==
                          CANON_COMPLETE);
                    canon_result *rh = NULL;
                    CHECK(p1(ws, g, xh, &rh) == CANON_COMPLETE);
                    size_t bhl = 0, thl = 0;
                    const uint8_t *bh = canon_result_bytes(rh, &bhl);
                    const uint8_t *th = canon_result_trace(rh, &thl);
                    CHECK(bhl == bl && memcmp(bh, b, bl) == 0 && thl == tl &&
                          memcmp(th, t, tl) == 0);
                    /* transporter x -> x^h exists, and the stabiliser and minimum run */
                    canon_result *rt = NULL;
                    CHECK(solve(ws, g, xo, xh, CANON_OBJECTIVE_TRANSPORTER_ONE, NULL,
                                CANON_WITNESS_ANY, &rt) == CANON_COMPLETE);
                    CHECK(canon_result_get_flags(rt).witness_valid);
                    valid = false;
                    CHECK(canon_result_verify_witness(rt, &valid) == CANON_COMPLETE && valid);
                    canon_result_release(rt);
                    canon_result_release(rh);
                    canon_object_release(xh);
                }
                canon_result *rs = NULL, *rm = NULL;
                CHECK(solve(ws, g, xo, NULL, CANON_OBJECTIVE_STABILISER, NULL, CANON_WITNESS_ANY,
                            &rs) == CANON_COMPLETE);
                CHECK(canon_result_get_flags(rs).stabiliser_complete);
                CHECK(solve(ws, g, xo, NULL, CANON_OBJECTIVE_LEX_MIN_IMAGE, NULL,
                            CANON_WITNESS_DETERMINISTIC, &rm) == CANON_COMPLETE);
                size_t ml = 0;
                const uint8_t *mb = canon_result_bytes(rm, &ml);
                CHECK(mb != NULL && canon_stream_validate(CTX, mb, ml) == CANON_COMPLETE);
                canon_result_release(rs);
                canon_result_release(rm);
                /* a target of another kind (an empty subset) is INVALID_INPUT (S4 rule) */
                canon_object *other = NULL;
                canon_result *ro = NULL;
                CHECK(canon_object_create_subset(CTX, n, NULL, 0, &other) == CANON_COMPLETE);
                CHECK(solve(ws, g, xo, other, CANON_OBJECTIVE_TRANSPORTER_ONE, NULL,
                            CANON_WITNESS_ANY, &ro) == CANON_INVALID_INPUT);
                canon_object_release(other);
                canon_result_release(r);
                canon_object_release(xo);
            }
            canon_group_release(g);
        }
    }
    canon_buf_free(&moved);
    canon_dag_free(&raw);
    canon_dag_free(&x);
    canon_dag_free(&img);
    canon_dag_scratch_free(&sc);
}

/* spec 11.1 at problem creation: records, references, literal bytes and the output size
 * (exact for tags 01..06, the conservative bound for subgroup leaves). */
static void test_problem_capacity(canon_workspace *ws)
{
    canon_group *g = NULL;
    const uint32_t swap[2] = {1, 0};
    CHECK(canon_group_create(CTX, 2, swap, 1, &g) == CANON_COMPLETE);
    canon_object *x = NULL;
    /* ("ab", 0, 1): 4 records, 3 references, 2 literal bytes; stream of 15 + 5 + 5 + 7 + 17 + 4
     * = 53 bytes */
    CHECK(from_hex(2,
                   "434e0200010001 00000002 00000004 01 00000000 01 00000001 02 00000002 6162 "
                   "03 00000003 00000002 00000000 00000001 00000003",
                   &x) == CANON_COMPLETE);
    canon_result *r = NULL;
    const struct {
        canon_capacity cap;
        canon_status want;
    } cases[] = {
        {{0, 0, 0, 0, 3, 0, 0, 0}, CANON_CAPACITY_LIMIT},  {{0, 0, 0, 0, 4, 0, 0, 0}, CANON_COMPLETE},
        {{0, 0, 0, 0, 0, 2, 0, 0}, CANON_CAPACITY_LIMIT},  {{0, 0, 0, 0, 0, 3, 0, 0}, CANON_COMPLETE},
        {{0, 0, 0, 0, 0, 0, 1, 0}, CANON_CAPACITY_LIMIT},  {{0, 0, 0, 0, 0, 0, 2, 0}, CANON_COMPLETE},
        {{0, 0, 0, 52, 0, 0, 0, 0}, CANON_CAPACITY_LIMIT}, {{0, 0, 0, 53, 0, 0, 0, 0}, CANON_COMPLETE},
    };
    for (size_t i = 0; i < sizeof cases / sizeof *cases; ++i) {
        CHECK(solve(ws, g, x, NULL, CANON_OBJECTIVE_CANONICAL_IMAGE, &cases[i].cap,
                    CANON_WITNESS_ANY, &r) == cases[i].want);
        if (r != NULL) {
            size_t bl = 0;
            CHECK(canon_result_bytes(r, &bl) != NULL && bl == 53);
        }
        canon_result_release(r);
    }
    canon_object_release(x);
    canon_group_release(g);
    /* a subgroup leaf: C4 = <(0 2 1 3)> encodes in 77 bytes, its conjugate <(0 1 2 3)> in 41;
     * the problem is admitted only within the bound 15 + 1 + 77 + 4 = 97 bytes, which covers
     * every image (spec 11.1 "may conservatively reject an instance with a smaller actual
     * output") */
    const char *c4 = "434e0200010001 00000004 00000001 07 00 00000001 00000004 00000000 00000002 "
                     "00000001 00000003 00000002 00000001 00000003 00000000 00000000";
    CHECK(from_hex(4, c4, &x) == CANON_COMPLETE);
    const uint32_t s4[8] = {1, 0, 2, 3, 1, 2, 3, 0};
    CHECK(canon_group_create(CTX, 4, s4, 2, &g) == CANON_COMPLETE);
    canon_capacity cap = {0, 0, 0, 96, 0, 0, 0, 0};
    CHECK(solve(ws, g, x, NULL, CANON_OBJECTIVE_CANONICAL_IMAGE, &cap, CANON_WITNESS_ANY, &r) ==
          CANON_CAPACITY_LIMIT);
    cap.max_output_bytes = 97;
    CHECK(solve(ws, g, x, NULL, CANON_OBJECTIVE_CANONICAL_IMAGE, &cap, CANON_WITNESS_ANY, &r) ==
          CANON_COMPLETE);
    size_t bl = 0;
    const uint8_t *b = canon_result_bytes(r, &bl);
    CHECK(b != NULL && bl <= 97 && canon_stream_validate(CTX, b, bl) == CANON_COMPLETE);
    /* under Sym(4) the canonical image is the conjugate with the shorter payload or the same */
    CHECK(bl == 61 || bl == 97);
    canon_result_release(r);
    canon_object_release(x);
    canon_group_release(g);
}

/* Workspace reuse across kinds, releasing each object before the next solve (spec 17: no
 * borrowed pointer after a run); every result equals a fresh workspace's. */
static void test_workspace_reuse(void)
{
    canon_workspace *ws = NULL, *fresh = NULL;
    CHECK(canon_workspace_create(CTX, &ws) == CANON_COMPLETE);
    const uint32_t s3[6] = {1, 0, 2, 1, 2, 0};
    canon_group *g = NULL;
    CHECK(canon_group_create(CTX, 3, s3, 2, &g) == CANON_COMPLETE);
    static const char *const STREAMS[] = {
        "434e0200010001 00000003 00000003 01 00000000 06 00000002 00000001 00000002 00000002 "
        "00000001 03 00000002 00000000 00000001 00000002",
        "434e0200010001 00000003 00000002 01 00000002 04 00000001 00000000 00000001",
        "434e0200010001 00000003 00000001 09 00000000 00000001 63 00000000 00000001 00000000 "
        "00000001 00000000 00000001 01 00000000",
        "434e0200010001 00000003 00000004 01 00000000 01 00000001 07 01 00000001 00000002 "
        "00000000 00000002 05 00000003 00000000 00000001 01 00000001 00000001 02 00000002 "
        "00000001 03 00000003",
    };
    for (int round = 0; round < 2; ++round) {
        for (size_t i = 0; i < sizeof STREAMS / sizeof *STREAMS; ++i) {
            canon_object *x = NULL;
            CHECK(from_hex(3, STREAMS[i], &x) == CANON_COMPLETE);
            canon_result *a = NULL, *b = NULL;
            CHECK(p1(ws, g, x, &a) == CANON_COMPLETE);
            CHECK(canon_workspace_create(CTX, &fresh) == CANON_COMPLETE);
            CHECK(p1(fresh, g, x, &b) == CANON_COMPLETE);
            canon_workspace_release(fresh);
            canon_object_release(x); /* the results and ws keep nothing borrowed from x */
            CHECK(same_result(a, b));
            size_t bl = 0;
            const uint8_t *bytes = canon_result_bytes(a, &bl);
            CHECK(canon_stream_validate(CTX, bytes, bl) == CANON_COMPLETE);
            canon_result_release(a);
            canon_result_release(b);
        }
    }
    canon_group_release(g);
    canon_workspace_release(ws);
}

static void put_perm(sbuf *s, const uint32_t *p, uint32_t n)
{
    uint32_t moved = 0;
    for (uint32_t v = 0; v < n; ++v) {
        moved += p[v] != v;
    }
    u32(s, moved);
    for (uint32_t v = 0; v < n; ++v) {
        if (p[v] != v) {
            u32(s, v);
            u32(s, p[v]);
        }
    }
}

static void random_perm4(uint32_t n, uint32_t *p)
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

/* S5 review item 6: the chain work per leaf image.  Objects as in tier D1 with subgroup and
 * coset leaves (an atom, a subgroup given by 1-2 random generators, a coset with a random
 * representative, under a tuple) under every T1 group, solved by the internal P1 search: with
 * the verified chains kept at import every leaf image conjugates a stored chain and builds
 * none; with the chains dropped (the behaviour before the review) every leaf image builds and
 * verifies one.  Both give the same trace, bytes and witness. */
static void test_chain_reuse(void)
{
    canon_dag_stats kept = {0, 0}, dropped = {0, 0};
    uint64_t solves = 0;
    const canon_dag_limits lim = {4096, UINT64_MAX, UINT64_MAX, UINT64_MAX};
    for (uint32_t n = 1; n <= 4; ++n) {
        t1_sym sym;
        t1_sym_init(&sym, n);
        t1_group groups[T1_MAX_GROUPS];
        const uint32_t count = t1_subgroups(&sym, groups);
        for (uint32_t gi = 0; gi < count; ++gi) {
            canon_group *g = NULL;
            CHECK(canon_group_create(CTX, n, groups[gi].gens, groups[gi].gen_count, &g) ==
                  CANON_COMPLETE);
            for (int trial = 0; trial < 3; ++trial) {
                sbuf st;
                header(&st, n, 4);
                u8(&st, 0x01);
                u32(&st, (uint32_t)(check_rng() % n));
                uint32_t p[4], r[4];
                for (int coset = 0; coset < 2; ++coset) {
                    const uint32_t k = 1 + (uint32_t)(check_rng() % 2);
                    u8(&st, coset ? 0x08 : 0x07);
                    u8(&st, 0x00);
                    u32(&st, k);
                    for (uint32_t j = 0; j < k; ++j) {
                        random_perm4(n, p);
                        put_perm(&st, p, n);
                    }
                    if (coset) {
                        random_perm4(n, r);
                        put_perm(&st, r, n);
                    }
                }
                u8(&st, 0x03);
                u32(&st, 3);
                u32(&st, 2);
                u32(&st, 0);
                u32(&st, 1);
                u32(&st, 3);
                canon_root x;
                CHECK(canon_root_import_stream(&x, n, st.b, st.len, &lim, NULL) == CANON_COMPLETE);
                CHECK(x.kind == CANON_ROOT_DAG && x.u.dag.chains_cap > 0);
                canon_p1_search a, b;
                canon_p1_search_init(&a);
                canon_p1_search_init(&b);
                CHECK(canon_p1_search_run(&a, g, &x, 1u << 20) == CANON_COMPLETE);
                /* drop the stored chains: every leaf image then rebuilds (the old path) */
                for (uint32_t i = 0; i < x.u.dag.chains_cap; ++i) {
                    if (x.u.dag.chains[i] != NULL) {
                        canon_bsgs_free(x.u.dag.chains[i]);
                        free(x.u.dag.chains[i]);
                        x.u.dag.chains[i] = NULL;
                    }
                }
                CHECK(canon_p1_search_run(&b, g, &x, 1u << 20) == CANON_COMPLETE);
                CHECK(a.best_bytes.len == b.best_bytes.len &&
                      memcmp(a.best_bytes.data, b.best_bytes.data, a.best_bytes.len) == 0 &&
                      a.best_trace.len == b.best_trace.len &&
                      memcmp(a.best_trace.data, b.best_trace.data, a.best_trace.len) == 0 &&
                      memcmp(a.best_t, b.best_t, n * sizeof *a.best_t) == 0);
                const canon_dag_stats *sa = &a.image.dag_scratch.stats;
                const canon_dag_stats *sb = &b.image.dag_scratch.stats;
                kept.chain_builds += sa->chain_builds;
                kept.chain_conjugations += sa->chain_conjugations;
                dropped.chain_builds += sb->chain_builds;
                dropped.chain_conjugations += sb->chain_conjugations;
                ++solves;
                canon_p1_search_free(&a);
                canon_p1_search_free(&b);
                canon_root_free(&x);
            }
            canon_group_release(g);
        }
    }
    printf("chain reuse (%llu solves, T1 groups, n = 1..4): stored chains: %llu builds, %llu "
           "conjugations; chains dropped: %llu builds, %llu conjugations\n",
           (unsigned long long)solves, (unsigned long long)kept.chain_builds,
           (unsigned long long)kept.chain_conjugations, (unsigned long long)dropped.chain_builds,
           (unsigned long long)dropped.chain_conjugations);
    CHECK(kept.chain_builds == 0 && dropped.chain_conjugations == 0);
    CHECK(kept.chain_conjugations > 0 && kept.chain_conjugations == dropped.chain_builds);
}

int main(void)
{
    CHECK(canon_context_create(NULL, &CTX) == CANON_COMPLETE);
    canon_workspace *ws = NULL;
    CHECK(canon_workspace_create(CTX, &ws) == CANON_COMPLETE);
    test_bb(ws);
    test_create_statuses();
    test_kinds_from_streams(ws);
    test_dag_roots(ws);
    test_problem_capacity(ws);
    test_workspace_reuse();
    test_chain_reuse();
    canon_workspace_release(ws);
    canon_context_release(CTX);
    return check_finish("test_search_dag");
}
