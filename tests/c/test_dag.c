/* Slice S5 tests of the record arena and its normalisation (spec 4.1, 4.2, 9.4, 11.1;
 * docs/slices/S5.md 3.1, 3.2, 3.6, 4): interning, set deduplication, multiset merging,
 * unreachable discard, height numbering and within-height order, the spec 7.4 (b,b) case, the
 * depth-60 shared chain with 61 records, sets of sets, insertion-order and sharing
 * independence, idempotence of normalisation, encode-decode-normalise identity, identity of
 * the general encoder with the S1 subset and S2 graph streams, leaf canonicalisation, and the
 * output-size argument (exact for tags 01-06, a bound for subgroup leaves).  Expected hex lives
 * here, never in src/. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "encoding/cdag_decode.h"
#include "encoding/cdag_encode.h"
#include "encoding/graph_stream.h"
#include "encoding/group_stream.h"
#include "encoding/subset_stream.h"
#include "object/dag.h"
#include "object/graph.h"

static canon_dag_scratch S; /* one scratch for every normalisation: grow-only reuse */

static uint32_t atom(canon_dag *d, uint32_t a)
{
    uint8_t p[4] = {(uint8_t)(a >> 24), (uint8_t)(a >> 16), (uint8_t)(a >> 8), (uint8_t)a};
    uint32_t i = 0;
    CHECK(canon_dag_append(d, CANON_REC_ATOM, p, 4, NULL, NULL, 0, &i) == CANON_COMPLETE);
    return i;
}

static uint32_t literal(canon_dag *d, const char *s)
{
    size_t len = strlen(s);
    uint8_t p[64];
    p[0] = p[1] = p[2] = 0;
    p[3] = (uint8_t)len;
    memcpy(p + 4, s, len);
    uint32_t i = 0;
    CHECK(canon_dag_append(d, CANON_REC_LITERAL, p, 4 + len, NULL, NULL, 0, &i) ==
          CANON_COMPLETE);
    return i;
}

static uint32_t node(canon_dag *d, uint8_t tag, const uint32_t *kids, const uint64_t *counts,
                     uint32_t k)
{
    uint32_t i = 0;
    CHECK(canon_dag_append(d, tag, NULL, 0, kids, counts, k, &i) == CANON_COMPLETE);
    return i;
}

static uint32_t leaf_hex(canon_dag *d, uint8_t tag, const char *hex)
{
    uint8_t p[256];
    size_t len = 0;
    for (const char *c = hex; *c != '\0'; ++c) {
        if (*c == ' ') {
            continue;
        }
        unsigned v = 0;
        for (int k = 0; k < 2; ++k, ++c) {
            v = v * 16u + (unsigned)(*c <= '9' ? *c - '0' : *c - 'a' + 10);
        }
        --c;
        p[len++] = (uint8_t)v;
    }
    uint32_t i = 0;
    CHECK(canon_dag_append(d, tag, p, len, NULL, NULL, 0, &i) == CANON_COMPLETE);
    return i;
}

/* Normalise d (root r) and return its stream in *out (caller frees). */
static canon_status normal_stream(canon_dag *d, uint32_t r, canon_buf *out, canon_dag *norm)
{
    d->root = r;
    canon_status st = canon_dag_normalise(d, norm, &S, false);
    canon_buf_truncate(out, 0);
    if (st == CANON_COMPLETE) {
        st = canon_dag_stream_write(out, norm);
    }
    return st;
}

static int buf_is(const canon_buf *b, const char *hex)
{
    return check_hex_is(b->data, b->len, hex);
}

static int buf_eq(const canon_buf *a, const canon_buf *b)
{
    return a->len == b->len && (a->len == 0 || memcmp(a->data, b->data, a->len) == 0);
}

/* spec 7.4: "n=0, x=(b,b), b=empty literal ... Shared and duplicate b storage give these same
 * bytes." */
static void test_bb(void)
{
    const char *want = "434e0200010001 00000000 00000002 02 00000000 03 00000002 00000000 "
                       "00000000 00000001";
    canon_dag d, norm;
    canon_dag_init(&d, 0);
    canon_dag_init(&norm, 0);
    canon_buf out;
    canon_buf_init(&out);
    uint32_t b = literal(&d, "");
    uint32_t kids[2] = {b, b};
    uint32_t t = node(&d, CANON_REC_TUPLE, kids, NULL, 2);
    CHECK(normal_stream(&d, t, &out, &norm) == CANON_COMPLETE && buf_is(&out, want));
    CHECK(norm.count == 2 && norm.refs == 2 && norm.root == 1);
    CHECK(norm.recs[0].height == 0 && norm.recs[1].height == 1);
    /* duplicate storage, the tuple referring to two copies in reverse order */
    canon_dag_reset(&d, 0);
    uint32_t b1 = literal(&d, ""), b2 = literal(&d, "");
    uint32_t kids2[2] = {b2, b1};
    t = node(&d, CANON_REC_TUPLE, kids2, NULL, 2);
    CHECK(normal_stream(&d, t, &out, &norm) == CANON_COMPLETE && buf_is(&out, want));
    CHECK(norm.stream_size == out.len && norm.image_bound == out.len);
    canon_buf_free(&out);
    canon_dag_free(&d);
    canon_dag_free(&norm);
}

/* spec 4.2: "For x0=literal, x(i+1)=(xi,xi), the output has i+1 records and 2i references,
 * rather than 2^i occurrences."  Depth 60 shared; depth 10 also fully duplicated (2^11 - 1
 * records), which must give the shared stream. */
static void test_deep_chain(void)
{
    canon_dag d, norm;
    canon_dag_init(&d, 0);
    canon_dag_init(&norm, 0);
    canon_buf out, dup;
    canon_buf_init(&out);
    canon_buf_init(&dup);
    uint32_t x = literal(&d, "x");
    for (int i = 0; i < 60; ++i) {
        uint32_t kids[2] = {x, x};
        x = node(&d, CANON_REC_TUPLE, kids, NULL, 2);
    }
    CHECK(normal_stream(&d, x, &out, &norm) == CANON_COMPLETE);
    CHECK(norm.count == 61 && norm.refs == 120);
    CHECK(out.len == 15u + 6u + 60u * 13u + 4u); /* review_checks: 19 + 6 + 60 * 13 */
    CHECK(norm.recs[60].height == 60);
    /* record i is the depth-i tuple referring twice to record i - 1 */
    for (uint32_t i = 1; i <= 60; ++i) {
        const canon_rec *r = &norm.recs[i];
        CHECK(r->tag == CANON_REC_TUPLE && norm.child[r->child_off] == i - 1 &&
              norm.child[r->child_off + 1] == i - 1);
    }
    /* depth 10, shared versus a full binary tree of copies */
    canon_dag_reset(&d, 0);
    x = literal(&d, "x");
    for (int i = 0; i < 10; ++i) {
        uint32_t kids[2] = {x, x};
        x = node(&d, CANON_REC_TUPLE, kids, NULL, 2);
    }
    CHECK(normal_stream(&d, x, &out, &norm) == CANON_COMPLETE && norm.count == 11);
    canon_dag tree;
    canon_dag_init(&tree, 0);
    /* level l has 2^(10-l) copies; level 0 literals first */
    uint32_t first = 0, width = 1024;
    for (uint32_t j = 0; j < width; ++j) {
        uint32_t li = literal(&tree, "x");
        if (j == 0) {
            first = li;
        }
    }
    for (int level = 1; level <= 10; ++level) {
        uint32_t next = tree.count;
        for (uint32_t j = 0; j < width / 2; ++j) {
            uint32_t kids[2] = {first + 2 * j, first + 2 * j + 1};
            (void)node(&tree, CANON_REC_TUPLE, kids, NULL, 2);
        }
        first = next;
        width /= 2;
    }
    CHECK(tree.count == 2047);
    CHECK(normal_stream(&tree, tree.count - 1, &dup, &norm) == CANON_COMPLETE);
    CHECK(norm.count == 11 && buf_eq(&out, &dup));
    canon_dag_free(&tree);
    canon_buf_free(&out);
    canon_buf_free(&dup);
    canon_dag_free(&d);
    canon_dag_free(&norm);
}

/* Interning, set dedupe, unreachable discard, multiset merge (spec 4.2). */
static void test_interning(void)
{
    canon_dag d, norm;
    canon_dag_init(&d, 2);
    canon_dag_init(&norm, 0);
    canon_buf out, want;
    canon_buf_init(&out);
    canon_buf_init(&want);
    /* review_checks: dag_bytes(2, [(1,1),(1,0),(2,b"unused"),(4,(0,1,0))]) equals
     * subset_bytes(2, {0,1}); here with a second copy of atom 1 instead of a repeated
     * reference (the wire grammar requires strictly increasing set children) */
    uint32_t a1 = atom(&d, 1), a0 = atom(&d, 0);
    (void)literal(&d, "unused");
    uint32_t a1b = atom(&d, 1);
    uint32_t kids[3] = {a1, a0, a1b};
    uint32_t set = node(&d, CANON_REC_SET, kids, NULL, 3);
    CHECK(normal_stream(&d, set, &out, &norm) == CANON_COMPLETE);
    const uint32_t atoms01[2] = {0, 1};
    CHECK(canon_subset_stream_write(&want, 2, atoms01, 2) == CANON_COMPLETE);
    CHECK(buf_eq(&out, &want));
    CHECK(norm.count == 3 && norm.literal_bytes == 0);
    /* multiset {a x 2, a' x 3, b x 1} with a = a' merges to {a x 5, b x 1}; atom 0 < atom 1 */
    canon_dag_reset(&d, 2);
    uint32_t x = atom(&d, 1), y = atom(&d, 0), z = atom(&d, 1);
    uint32_t mk[3] = {x, y, z};
    uint64_t mc[3] = {2, 1, 3};
    uint32_t ms = node(&d, CANON_REC_MULTISET, mk, mc, 3);
    CHECK(normal_stream(&d, ms, &out, &norm) == CANON_COMPLETE);
    CHECK(buf_is(&out, "434e0200010001 00000002 00000003 01 00000000 01 00000001 "
                       "05 00000002 00000000 00000001 01 00000001 00000001 05 00000002"));
    /* merged count overflow: count-bit limit 64 (detailed plan 2.1) */
    canon_dag_reset(&d, 2);
    x = atom(&d, 1);
    z = atom(&d, 1);
    uint32_t ok2[2] = {x, z};
    uint64_t big[2] = {UINT64_MAX, 1};
    ms = node(&d, CANON_REC_MULTISET, ok2, big, 2);
    CHECK(normal_stream(&d, ms, &out, &norm) == CANON_CAPACITY_LIMIT);
    CHECK(norm.count == 0);
    canon_dag_reset(&d, 2);
    x = atom(&d, 1);
    z = atom(&d, 1);
    big[0] = UINT64_MAX - 1;
    ms = node(&d, CANON_REC_MULTISET, ok2, big, 2);
    CHECK(normal_stream(&d, ms, &out, &norm) == CANON_COMPLETE);
    CHECK(norm.count == 2 && norm.mult[norm.recs[1].child_off] == UINT64_MAX);
    canon_buf_free(&out);
    canon_buf_free(&want);
    canon_dag_free(&d);
    canon_dag_free(&norm);
}

/* spec 4.2 heights and within-height order by record bytes: tag first (01 < 02 < 03 < 04),
 * then the payload or the U32 fields; sets sort children by the assigned indices. */
static void test_numbering(void)
{
    canon_dag d, norm;
    canon_dag_init(&d, 3);
    canon_dag_init(&norm, 0);
    canon_buf out;
    canon_buf_init(&out);
    /* x = ( {2, 0}, "ab", (), "b", 1 ) built children-last-first */
    uint32_t a2 = atom(&d, 2), a0 = atom(&d, 0);
    uint32_t sk[2] = {a2, a0};
    uint32_t set = node(&d, CANON_REC_SET, sk, NULL, 2);
    uint32_t ab = literal(&d, "ab"), b = literal(&d, "b");
    uint32_t empty = node(&d, CANON_REC_TUPLE, NULL, NULL, 0);
    uint32_t a1 = atom(&d, 1);
    uint32_t tk[5] = {set, ab, empty, b, a1};
    uint32_t t = node(&d, CANON_REC_TUPLE, tk, NULL, 5);
    CHECK(normal_stream(&d, t, &out, &norm) == CANON_COMPLETE);
    /* height 0: 01 a0, 01 a1, 01 a2, 02 "b" (length 1 < 2), 02 "ab", 03 (); height 1: the set
     * {0, 2} -> indices {0, 2}; height 2: the tuple (6, 4, 5, 3, 1) */
    CHECK(buf_is(&out, "434e0200010001 00000003 00000008 "
                       "01 00000000 01 00000001 01 00000002 02 00000001 62 "
                       "02 00000002 6162 03 00000000 04 00000002 00000000 00000002 "
                       "03 00000005 00000006 00000004 00000005 00000003 00000001 00000007"));
    CHECK(norm.recs[5].height == 0 && norm.recs[6].height == 1 && norm.recs[7].height == 2);
    CHECK(norm.literal_bytes == 3);
    /* sets of sets: {{0}, {1}, {0}'} = {{0}, {1}}, and {{1},{0}} gives the same */
    canon_dag_reset(&d, 2);
    uint32_t p0 = atom(&d, 0), p1 = atom(&d, 1), p0b = atom(&d, 0);
    uint32_t s1 = node(&d, CANON_REC_SET, &p1, NULL, 1);
    uint32_t s0 = node(&d, CANON_REC_SET, &p0, NULL, 1);
    uint32_t s0b = node(&d, CANON_REC_SET, &p0b, NULL, 1);
    uint32_t ss[3] = {s1, s0, s0b};
    uint32_t top = node(&d, CANON_REC_SET, ss, NULL, 3);
    CHECK(normal_stream(&d, top, &out, &norm) == CANON_COMPLETE);
    CHECK(buf_is(&out, "434e0200010001 00000002 00000005 01 00000000 01 00000001 "
                       "04 00000001 00000000 04 00000001 00000001 "
                       "04 00000002 00000002 00000003 00000004"));
    canon_buf_free(&out);
    canon_dag_free(&d);
    canon_dag_free(&norm);
}

/* ---- random DAGs (tags 01..06) ---- */

typedef struct rnode {
    uint8_t tag;
    uint32_t value;   /* atom id, literal choice, permutation choice */
    uint32_t k;
    uint32_t kid[4];  /* child node indices (smaller) */
    uint64_t cnt[4];
} rnode;

static const char *const LITS[4] = {"", "a", "b", "ab"};

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

/* A random value as nodes 0..m-1 (children of node i are < i); the last node is the root. */
static uint32_t random_nodes(uint32_t n, rnode *nodes, uint32_t m)
{
    for (uint32_t i = 0; i < m; ++i) {
        rnode *x = &nodes[i];
        memset(x, 0, sizeof *x);
        uint32_t pick = (uint32_t)(check_rng() % 6);
        if (i == 0 || (i < m - 1 && pick < 3)) {
            x->tag = (uint8_t)(n > 0 ? 1 + check_rng() % 2 : 2);
            if (x->tag == 2 && check_rng() % 4 == 0 && n > 0) {
                x->tag = CANON_REC_PERM;
            }
            x->value = (uint32_t)(check_rng() % (x->tag == 1 ? n : 4));
            continue;
        }
        x->tag = (uint8_t)(3 + check_rng() % 3);
        x->k = (uint32_t)(check_rng() % 4);
        for (uint32_t j = 0; j < x->k; ++j) {
            x->kid[j] = (uint32_t)(check_rng() % i);
            x->cnt[j] = 1 + check_rng() % 3;
        }
    }
    return m - 1;
}

/* Emit node i (and, recursively, a copy of each child unless `share` and one exists) into d;
 * returns the record index.  copy[i] holds an existing record for node i or UINT32_MAX. */
static uint32_t emit(canon_dag *d, uint32_t n, const rnode *nodes, uint32_t i, uint32_t *copy,
                     int share)
{
    /* share: mostly reuse a record; otherwise duplicate freely, up to a size cap */
    if (copy[i] != UINT32_MAX &&
        (share ? check_rng() % 4 != 0 : (d->count > 400 || check_rng() % 3 == 0))) {
        return copy[i];
    }
    const rnode *x = &nodes[i];
    uint32_t r = 0;
    if (x->tag == CANON_REC_ATOM) {
        r = atom(d, x->value);
    } else if (x->tag == CANON_REC_LITERAL) {
        r = literal(d, LITS[x->value]);
    } else if (x->tag == CANON_REC_PERM) {
        /* a permutation chosen from the value: the (value + 1)-th power of a fixed cycle */
        uint32_t p[8];
        for (uint32_t v = 0; v < n; ++v) {
            p[v] = (v + x->value + 1) % n;
        }
        canon_buf b;
        canon_buf_init(&b);
        CHECK(canon_perm_bytes_write(&b, p, n) == CANON_COMPLETE);
        CHECK(canon_dag_append(d, CANON_REC_PERM, b.data, b.len, NULL, NULL, 0, &r) ==
              CANON_COMPLETE);
        canon_buf_free(&b);
    } else {
        uint32_t kids[4];
        uint64_t cnt[4];
        for (uint32_t j = 0; j < x->k; ++j) {
            kids[j] = emit(d, n, nodes, x->kid[j], copy, share);
            cnt[j] = x->cnt[j];
        }
        if (x->tag != CANON_REC_TUPLE) {
            /* the wire grammar needs strictly increasing references: sort, and merge repeats
             * (a set ignores the repeat, a multiset adds the counts) */
            for (uint32_t a = 0; a < x->k; ++a) {
                for (uint32_t b = a + 1; b < x->k; ++b) {
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
            uint32_t w = 0;
            for (uint32_t a = 0; a < x->k; ++a) {
                if (w > 0 && kids[w - 1] == kids[a]) {
                    cnt[w - 1] += cnt[a];
                } else {
                    kids[w] = kids[a];
                    cnt[w++] = cnt[a];
                }
            }
            r = node(d, x->tag, kids, x->tag == CANON_REC_MULTISET ? cnt : NULL, w);
        } else {
            r = node(d, x->tag, kids, NULL, x->k);
        }
    }
    if (copy[i] == UINT32_MAX) {
        copy[i] = r;
    }
    return r;
}

/* Build a raw arena for the random value with a random amount of sharing and junk records. */
static uint32_t build(canon_dag *d, uint32_t n, const rnode *nodes, uint32_t m, int share)
{
    uint32_t copy[64];
    for (uint32_t i = 0; i < m; ++i) {
        copy[i] = UINT32_MAX;
    }
    canon_dag_reset(d, n);
    if (check_rng() % 2 == 0) {
        (void)literal(d, "junk"); /* unreachable */
    }
    uint32_t root = emit(d, n, nodes, m - 1, copy, share);
    if (check_rng() % 2 == 0) {
        (void)literal(d, "after the root"); /* unreachable, after the root */
    }
    return root;
}

/* Insertion order, sharing and junk have no effect (spec 4.2, 5); normalisation is idempotent;
 * encode-decode-normalise reproduces the bytes (docs/slices/S5.md 3.2 item 4). */
static void test_random_independence(void)
{
    canon_dag a, b, na, nb, again, dec;
    canon_dag_init(&a, 0);
    canon_dag_init(&b, 0);
    canon_dag_init(&na, 0);
    canon_dag_init(&nb, 0);
    canon_dag_init(&again, 0);
    canon_dag_init(&dec, 0);
    canon_buf sa, sb, sc;
    canon_buf_init(&sa);
    canon_buf_init(&sb);
    canon_buf_init(&sc);
    rnode nodes[24];
    for (int trial = 0; trial < 400; ++trial) {
        uint32_t n = (uint32_t)(check_rng() % 5);
        uint32_t m = 2 + (uint32_t)(check_rng() % 22);
        (void)random_nodes(n, nodes, m);
        uint32_t ra = build(&a, n, nodes, m, 1);
        uint32_t rb = build(&b, n, nodes, m, 0);
        CHECK(normal_stream(&a, ra, &sa, &na) == CANON_COMPLETE);
        CHECK(normal_stream(&b, rb, &sb, &nb) == CANON_COMPLETE);
        CHECK(buf_eq(&sa, &sb) && canon_dag_equal(&na, &nb));
        CHECK(na.count <= a.count && na.root == na.count - 1);
        /* idempotent: the normal form is its own normal form */
        CHECK(canon_dag_normalise(&na, &again, &S, false) == CANON_COMPLETE);
        CHECK(canon_dag_equal(&na, &again));
        /* encode, decode, normalise: identical bytes; and the stream validates */
        canon_cdag_reason reason = CANON_CDAG_OK;
        CHECK(canon_cdag_decode(sa.data, sa.len, &n, UINT32_MAX, &dec, &reason) ==
              CANON_COMPLETE);
        CHECK(canon_dag_normalise(&dec, &again, &S, false) == CANON_COMPLETE);
        canon_buf_truncate(&sc, 0);
        CHECK(canon_dag_stream_write(&sc, &again) == CANON_COMPLETE && buf_eq(&sa, &sc));
        canon_dag_limits lim = {UINT32_MAX, UINT64_MAX, UINT64_MAX, UINT64_MAX};
        CHECK(canon_cdag_validate(sa.data, sa.len, &lim, &reason) == CANON_COMPLETE);
        /* the raw encoding validates iff it already is the normal form */
        canon_buf_truncate(&sc, 0);
        a.root = ra;
        CHECK(canon_dag_stream_write(&sc, &a) == CANON_COMPLETE);
        canon_status v = canon_cdag_validate(sc.data, sc.len, &lim, &reason);
        CHECK((v == CANON_COMPLETE) == buf_eq(&sa, &sc));
        CHECK(v == CANON_COMPLETE || v == CANON_INVALID_INPUT);
    }
    canon_buf_free(&sa);
    canon_buf_free(&sb);
    canon_buf_free(&sc);
    canon_dag_free(&a);
    canon_dag_free(&b);
    canon_dag_free(&na);
    canon_dag_free(&nb);
    canon_dag_free(&again);
    canon_dag_free(&dec);
}

/* The general encoder reproduces the S1 subset stream and the S2 graph stream byte for byte
 * (docs/slices/S5.md 1: the specialisations are fast paths). */
static void test_specialisations(void)
{
    canon_dag d, norm;
    canon_dag_init(&d, 0);
    canon_dag_init(&norm, 0);
    canon_buf gen, fast;
    canon_buf_init(&gen);
    canon_buf_init(&fast);
    for (int trial = 0; trial < 200; ++trial) {
        uint32_t n = (uint32_t)(check_rng() % 9);
        uint32_t members[8], k = 0;
        canon_dag_reset(&d, n);
        uint32_t kids[16], nk = 0;
        for (uint32_t a = n; a-- > 0;) { /* decreasing insertion, some repeated */
            if (check_rng() % 2 == 0) {
                kids[nk++] = atom(&d, a);
                if (check_rng() % 3 == 0) {
                    kids[nk++] = atom(&d, a);
                }
            }
        }
        for (uint32_t a = 0; a < n; ++a) {
            for (uint32_t j = 0; j < nk; ++j) {
                if (canon_dag_atom(&d, kids[j]) == a) {
                    members[k++] = a;
                    break;
                }
            }
        }
        /* kids are record indices in increasing order already (appended in order) */
        uint32_t set = node(&d, CANON_REC_SET, kids, NULL, nk);
        CHECK(normal_stream(&d, set, &gen, &norm) == CANON_COMPLETE);
        canon_buf_truncate(&fast, 0);
        CHECK(canon_subset_stream_write(&fast, n, members, k) == CANON_COMPLETE);
        CHECK(buf_eq(&gen, &fast));
    }
    /* graphs: a random graph through canon_graph_init and its stream; the arena holds the same
     * graph record with the arcs shuffled and split (normalised to the same payload) */
    for (int trial = 0; trial < 200; ++trial) {
        uint32_t n = (uint32_t)(check_rng() % 5);
        canon_arc arcs[12];
        uint8_t labels[2] = {'a', 'b'};
        size_t e = n > 0 ? (size_t)(check_rng() % 12) : 0;
        for (size_t i = 0; i < e; ++i) {
            arcs[i].source = (uint32_t)(check_rng() % n);
            arcs[i].target = (uint32_t)(check_rng() % n);
            arcs[i].label = labels + check_rng() % 2;
            arcs[i].label_length = (size_t)(check_rng() % 2);
            arcs[i].multiplicity = 1 + check_rng() % 300;
        }
        const uint8_t *colours[4];
        size_t lengths[4];
        for (uint32_t v = 0; v < n; ++v) {
            colours[v] = labels;
            lengths[v] = (size_t)(check_rng() % 3 == 0);
        }
        canon_graph g;
        CHECK(canon_graph_init(&g, n, n > 0 ? colours : NULL, n > 0 ? lengths : NULL, arcs, e) ==
              CANON_COMPLETE);
        canon_buf_truncate(&fast, 0);
        CHECK(canon_graph_stream_write(&fast, &g) == CANON_COMPLETE);
        /* the raw payload: colours, then the arcs in reverse input order */
        canon_buf raw;
        canon_buf_init(&raw);
        for (uint32_t v = 0; v < n; ++v) {
            CHECK(canon_buf_put_b(&raw, colours[v], lengths[v]) == CANON_COMPLETE);
        }
        CHECK(canon_buf_put_u32(&raw, (uint32_t)e) == CANON_COMPLETE);
        for (size_t i = e; i-- > 0;) {
            (void)canon_buf_put_u32(&raw, arcs[i].source);
            (void)canon_buf_put_u32(&raw, arcs[i].target);
            (void)canon_buf_put_b(&raw, arcs[i].label, arcs[i].label_length);
            (void)canon_buf_put_nat(&raw, arcs[i].multiplicity);
        }
        canon_dag_reset(&d, n);
        uint32_t r = 0;
        CHECK(canon_dag_append(&d, CANON_REC_GRAPH, raw.data, raw.len, NULL, NULL, 0, &r) ==
              CANON_COMPLETE);
        CHECK(normal_stream(&d, r, &gen, &norm) == CANON_COMPLETE);
        CHECK(buf_eq(&gen, &fast));
        canon_buf_free(&raw);
        canon_graph_free(&g);
    }
    canon_buf_free(&gen);
    canon_buf_free(&fast);
    canon_dag_free(&d);
    canon_dag_free(&norm);
}

/* Leaf canonicalisation (spec 9.4): a non-canonical Group presentation and coset
 * representative become the canonical payloads; a graph below the root is unsupported. */
static void test_leaves(void)
{
    canon_dag d, norm;
    canon_dag_init(&d, 3);
    canon_dag_init(&norm, 0);
    canon_buf out;
    canon_buf_init(&out);
    /* C3 presented by [2,0,1] (rule 2): spec 7.4 "Group(C3)=00 00000001 00000003 00000000
     * 00000001 00000001 00000002 00000002 00000000" (the generator [1,2,0]) */
    uint32_t g = leaf_hex(&d, CANON_REC_GROUP, "00 00000001 00000003 00000000 00000002 00000001 "
                                               "00000000 00000002 00000001");
    CHECK(normal_stream(&d, g, &out, &norm) == CANON_COMPLETE);
    CHECK(buf_is(&out, "434e0200010001 00000003 00000001 07 00 00000001 00000003 00000000 "
                       "00000001 00000001 00000002 00000002 00000000 00000000"));
    CHECK(norm.group_leaves);
    /* Sym(2) on {0, 1} presented by rule 2 becomes rule 1 */
    canon_dag_reset(&d, 3);
    g = leaf_hex(&d, CANON_REC_GROUP, "00 00000001 00000002 00000000 00000001 00000001 00000000");
    CHECK(normal_stream(&d, g, &out, &norm) == CANON_COMPLETE);
    CHECK(buf_is(&out, "434e0200010001 00000003 00000001 07 01 00000001 00000002 00000000 "
                       "00000001 00000000"));
    /* coset H r with H = <(0 1)>, r = [2,0,1]: H r = {h r} = {[2,0,1], [0,2,1]} ((h r)[v] =
     * r[h[v]]), least [0,2,1]; with r = [0,2,1] the same coset; spec 9.4 "first choose its
     * least image-array element r0" */
    const char *want = "434e0200010001 00000003 00000001 08 01 00000001 00000002 00000000 "
                       "00000001 00000002 00000001 00000002 00000002 00000001 00000000";
    canon_dag_reset(&d, 3);
    g = leaf_hex(&d, CANON_REC_COSET, "01 00000001 00000002 00000000 00000001 "
                                      "00000003 00000000 00000002 00000001 00000000 00000002 "
                                      "00000001");
    CHECK(normal_stream(&d, g, &out, &norm) == CANON_COMPLETE && buf_is(&out, want));
    canon_dag_reset(&d, 3);
    g = leaf_hex(&d, CANON_REC_COSET, "01 00000001 00000002 00000000 00000001 "
                                      "00000002 00000001 00000002 00000002 00000001");
    CHECK(normal_stream(&d, g, &out, &norm) == CANON_COMPLETE && buf_is(&out, want));
    /* two presentations of one group are one node */
    canon_dag_reset(&d, 3);
    uint32_t g1 = leaf_hex(&d, CANON_REC_GROUP, "01 00000001 00000002 00000000 00000001");
    uint32_t g2 = leaf_hex(&d, CANON_REC_GROUP,
                           "00 00000002 00000002 00000000 00000001 00000001 00000000 "
                           "00000002 00000000 00000001 00000001 00000000");
    uint32_t gk[2] = {g1, g2};
    uint32_t set = node(&d, CANON_REC_SET, gk, NULL, 2);
    CHECK(normal_stream(&d, set, &out, &norm) == CANON_COMPLETE && norm.count == 2);
    /* a graph record below the root: UNSUPPORTED_ACTION (later slice) */
    canon_dag_reset(&d, 1);
    uint32_t gr = leaf_hex(&d, CANON_REC_GRAPH, "00000000 00000000");
    uint32_t tup = node(&d, CANON_REC_TUPLE, &gr, NULL, 1);
    CHECK(normal_stream(&d, tup, &out, &norm) == CANON_UNSUPPORTED_ACTION);
    /* ... but a graph root is fine, and an unreachable graph record is discarded */
    CHECK(normal_stream(&d, gr, &out, &norm) == CANON_COMPLETE && norm.count == 1);
    canon_dag_reset(&d, 1);
    (void)leaf_hex(&d, CANON_REC_GRAPH, "00000000 00000000");
    uint32_t lit = literal(&d, "z");
    CHECK(normal_stream(&d, lit, &out, &norm) == CANON_COMPLETE && norm.count == 1);
    canon_buf_free(&out);
    canon_dag_free(&d);
    canon_dag_free(&norm);
}

/* Bounded acyclicity check (spec 4.2) and the arena contract. */
static void test_acyclicity(void)
{
    canon_dag d, norm;
    canon_dag_init(&d, 1);
    canon_dag_init(&norm, 0);
    canon_buf out;
    canon_buf_init(&out);
    uint32_t a = atom(&d, 0);
    uint32_t t = node(&d, CANON_REC_TUPLE, &a, NULL, 1);
    uint32_t bad = 5;
    CHECK(canon_dag_append(&d, CANON_REC_TUPLE, NULL, 0, &bad, NULL, 1, NULL) ==
          CANON_INVALID_INPUT);
    CHECK(d.count == 2);
    d.child[d.recs[t].child_off] = t; /* a self loop, written behind the API */
    CHECK(normal_stream(&d, t, &out, &norm) == CANON_INVALID_INPUT);
    CHECK(normal_stream(&d, 7, &out, &norm) == CANON_INVALID_INPUT); /* root out of range */
    canon_dag_reset(&d, 1);
    d.root = 0;
    CHECK(canon_dag_normalise(&d, &norm, &S, false) == CANON_INVALID_INPUT); /* q = 0 */
    canon_buf_free(&out);
    canon_dag_free(&d);
    canon_dag_free(&norm);
}

/* spec 11.1 output size (docs/slices/S5.md 3.6): for tags 01..06 every image has the stream
 * length of x; for a subgroup leaf the canonical bytes of a conjugate can differ, so the
 * bound applies. */
static void test_output_size(void)
{
    canon_dag d, norm, img;
    canon_dag_init(&d, 0);
    canon_dag_init(&norm, 0);
    canon_dag_init(&img, 0);
    canon_buf out;
    canon_buf_init(&out);
    rnode nodes[24];
    uint32_t g[8];
    for (int trial = 0; trial < 300; ++trial) {
        uint32_t n = 1 + (uint32_t)(check_rng() % 6);
        uint32_t m = 2 + (uint32_t)(check_rng() % 22);
        (void)random_nodes(n, nodes, m);
        uint32_t r = build(&d, n, nodes, m, 1);
        CHECK(normal_stream(&d, r, &out, &norm) == CANON_COMPLETE);
        CHECK(canon_dag_image_bound(&norm, &S) == CANON_COMPLETE);
        CHECK(norm.image_bound == norm.stream_size && !norm.group_leaves);
        for (int k = 0; k < 4; ++k) {
            random_perm(n, g);
            CHECK(canon_dag_act(&norm, g, &img, &S) == CANON_COMPLETE);
            CHECK(img.stream_size == norm.stream_size);
            canon_buf_truncate(&out, 0);
            CHECK(canon_dag_stream_write(&out, &img) == CANON_COMPLETE &&
                  out.len == norm.stream_size);
        }
    }
    /* C4 = <(0 2 1 3)> = {id, [1,0,3,2], [2,3,1,0], [3,2,0,1]}: its rule-2 sequence starts
     * with the least element (0 1)(2 3), which generates only half of it, so k = 2 (77
     * bytes); conjugated by g = (1 2) = [0,2,1,3] it becomes <(0 1 2 3)>, whose least element
     * [1,2,3,0] generates it, k = 1 (41 bytes). */
    canon_dag_reset(&d, 4);
    uint32_t h = leaf_hex(&d, CANON_REC_GROUP, "00 00000001 00000004 00000000 00000002 00000001 "
                                               "00000003 00000002 00000001 00000003 00000000");
    CHECK(normal_stream(&d, h, &out, &norm) == CANON_COMPLETE);
    CHECK(norm.recs[0].payload_len == 77);
    CHECK(canon_dag_image_bound(&norm, &S) == CANON_COMPLETE);
    const uint32_t swap12[4] = {0, 2, 1, 3};
    CHECK(canon_dag_act(&norm, swap12, &img, &S) == CANON_COMPLETE);
    CHECK(img.recs[0].payload_len == 41 && img.stream_size + 36 == norm.stream_size);
    CHECK(norm.image_bound >= norm.stream_size && norm.image_bound >= img.stream_size);
    /* the bound is the same for the conjugate (|H| is invariant) */
    CHECK(canon_dag_image_bound(&img, &S) == CANON_COMPLETE);
    CHECK(img.image_bound == norm.image_bound);
    canon_buf_free(&out);
    canon_dag_free(&d);
    canon_dag_free(&norm);
    canon_dag_free(&img);
}

int main(void)
{
    canon_dag_scratch_init(&S);
    test_bb();
    test_deep_chain();
    test_interning();
    test_numbering();
    test_random_independence();
    test_specialisations();
    test_leaves();
    test_acyclicity();
    test_output_size();
    canon_dag_scratch_free(&S);
    return check_finish("test_dag");
}
