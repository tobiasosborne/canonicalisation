/* Tests for the P1 O stage (src/refine/p1.c, slice S2; spec 7.1, 10): the sparse arc-count
 * signatures and their comparison against a DENSE implementation that lives only here, on
 * random small graphs and partitions; the O-stage split against a dense model of spec 7.1's
 * split; and hand-built cases including spec 7.4's one-arc graph, where O yields [{1},{0}]. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "object/object.h"
#include "partition/partition.h"
#include "refine/p1.h"

#define MAXN 7
#define MAXA 24
#define MAXL 4
#define DENSE (2 * MAXL * MAXN)

static const char *const LABELS[MAXL] = {"", "a", "b", "ab"};

/* ---- dense reference (spec 7.1, written directly from the text) ---- */

typedef struct raw_graph {
    uint32_t n;
    size_t arc_count;
    canon_arc arcs[MAXA];
} raw_graph;

/* Rank of a label among the DISTINCT labels of the input sorted by (length, bytes) (spec 4.1),
 * computed independently of src/. */
static uint32_t label_rank(const raw_graph *r, const canon_arc *a, uint32_t *L)
{
    const uint8_t *seen[MAXA];
    size_t seen_len[MAXA], count = 0;
    for (size_t i = 0; i < r->arc_count; ++i) {
        int dup = 0;
        for (size_t j = 0; j < count; ++j) {
            dup |= seen_len[j] == r->arcs[i].label_length &&
                   memcmp(seen[j], r->arcs[i].label, seen_len[j]) == 0;
        }
        if (!dup) {
            seen[count] = r->arcs[i].label;
            seen_len[count++] = r->arcs[i].label_length;
        }
    }
    uint32_t rank = 0;
    for (size_t j = 0; j < count; ++j) {
        int less = seen_len[j] < a->label_length ||
                   (seen_len[j] == a->label_length &&
                    memcmp(seen[j], a->label, seen_len[j]) < 0);
        rank += less ? 1u : 0u;
    }
    *L = (uint32_t)count;
    return rank;
}

/* Dense signature: for each label (sorted), for each cell j of the entry snapshot:
 * outgoing multiplicity from v into C[j], incoming multiplicity from C[j] into v.  A loop
 * contributes once to each.  Returns the length 2 * L * k. */
static size_t dense_sig(const raw_graph *r, const canon_partition *p, uint32_t v, uint64_t *out)
{
    uint32_t L = 0;
    if (r->arc_count > 0) {
        (void)label_rank(r, &r->arcs[0], &L);
    }
    size_t len = 2u * L * p->cells;
    memset(out, 0, sizeof(uint64_t) * DENSE);
    for (size_t i = 0; i < r->arc_count; ++i) {
        const canon_arc *a = &r->arcs[i];
        uint32_t l = label_rank(r, a, &L);
        if (a->source == v) {
            out[(l * p->cells + p->cell_of[a->target]) * 2u] += a->multiplicity;
        }
        if (a->target == v) {
            out[(l * p->cells + p->cell_of[a->source]) * 2u + 1u] += a->multiplicity;
        }
    }
    return len;
}

static int dense_cmp(const uint64_t *a, const uint64_t *b, size_t len)
{
    for (size_t i = 0; i < len; ++i) {
        if (a[i] != b[i]) {
            return a[i] < b[i] ? -1 : 1;
        }
    }
    return 0;
}

static int sign(int x)
{
    return x < 0 ? -1 : (x > 0 ? 1 : 0);
}

/* ---- helpers ---- */

static void random_graph(raw_graph *r, uint32_t n)
{
    r->n = n;
    r->arc_count = n == 0 ? 0 : (size_t)(check_rng() % (MAXA + 1));
    for (size_t i = 0; i < r->arc_count; ++i) {
        const char *l = LABELS[check_rng() % MAXL];
        r->arcs[i] = (canon_arc){(uint32_t)(check_rng() % n), (uint32_t)(check_rng() % n),
                                 (const uint8_t *)l, strlen(l), 1 + check_rng() % 3};
    }
}

static void build_root(canon_root *x, const raw_graph *r)
{
    memset(x, 0, sizeof *x);
    x->kind = CANON_ROOT_GRAPH;
    x->n = r->n;
    CHECK(canon_graph_init(&x->u.graph, r->n, NULL, NULL, r->arcs, r->arc_count) ==
          CANON_COMPLETE);
}

/* Sorted member set of cell i. */
static size_t cell_members(const canon_partition *p, uint32_t i, uint32_t *out)
{
    size_t k = 0;
    for (uint32_t j = p->start[i]; j < p->start[i + 1]; ++j) {
        out[k++] = p->lab[j];
    }
    for (size_t a = 1; a < k; ++a) {
        for (size_t b = a; b > 0 && out[b - 1] > out[b]; --b) {
            uint32_t t = out[b];
            out[b] = out[b - 1];
            out[b - 1] = t;
        }
    }
    return k;
}

/* Dense model of spec 7.1's split by signature: each old cell, in its old position, replaced
 * by its nonempty classes in increasing dense signature order.  Writes the expected cells as
 * member lists into cells[] / sizes[]; returns the count. */
static uint32_t dense_split(const raw_graph *r, const canon_partition *p,
                            uint32_t cells[MAXN][MAXN], size_t sizes[MAXN])
{
    uint64_t sig[MAXN][DENSE];
    size_t len = 0;
    for (uint32_t v = 0; v < p->n; ++v) {
        len = dense_sig(r, p, v, sig[v]);
    }
    uint32_t out = 0;
    for (uint32_t i = 0; i < p->cells; ++i) {
        uint32_t members[MAXN];
        size_t k = cell_members(p, i, members);
        int done[MAXN] = {0};
        for (size_t taken = 0; taken < k;) {
            /* least remaining signature */
            size_t best = k;
            for (size_t a = 0; a < k; ++a) {
                if (!done[a] && (best == k || dense_cmp(sig[members[a]], sig[members[best]],
                                                        len) < 0)) {
                    best = a;
                }
            }
            sizes[out] = 0;
            for (size_t a = 0; a < k; ++a) {
                if (!done[a] && dense_cmp(sig[members[a]], sig[members[best]], len) == 0) {
                    cells[out][sizes[out]++] = members[a];
                    done[a] = 1;
                    ++taken;
                }
            }
            ++out;
        }
    }
    return out;
}

/* Run the O stage on p and compare with the dense model. */
static void check_stage_o(const raw_graph *r, canon_root *x, canon_partition *p,
                          canon_p1_scratch *s)
{
    uint32_t want[MAXN][MAXN];
    size_t want_sizes[MAXN];
    uint32_t count = dense_split(r, p, want, want_sizes);
    CHECK(canon_p1_stage_o(p, x, s) == CANON_COMPLETE);
    CHECK(p->cells == count);
    for (uint32_t i = 0; i < count && i < p->cells; ++i) {
        uint32_t got[MAXN];
        size_t k = cell_members(p, i, got);
        CHECK(k == want_sizes[i] && memcmp(got, want[i], k * sizeof *got) == 0);
    }
}

/* ---- tests ---- */

static void test_compare_hand(void)
{
    /* dense (0,1) < (1,0): A = {1:1}, B = {0:1}; B's smaller index is a positive entry where A
     * has zero, so B > A. */
    const canon_p1_sig_entry a[1] = {{1, 1}}, b[1] = {{0, 1}}, c[2] = {{0, 1}, {3, 2}},
                             d[2] = {{0, 2}, {1, 1}};
    CHECK(canon_p1_sig_compare(a, 1, b, 1) == -1);
    CHECK(canon_p1_sig_compare(b, 1, a, 1) == 1);
    CHECK(canon_p1_sig_compare(NULL, 0, a, 1) == -1); /* zero vector is least */
    CHECK(canon_p1_sig_compare(a, 1, NULL, 0) == 1);
    CHECK(canon_p1_sig_compare(NULL, 0, NULL, 0) == 0);
    CHECK(canon_p1_sig_compare(b, 1, c, 2) == -1); /* (1,0,0,0) < (1,0,0,2) */
    CHECK(canon_p1_sig_compare(c, 2, d, 2) == -1); /* (1,0,..) < (2,1,..) by count */
    CHECK(canon_p1_sig_compare(d, 2, d, 2) == 0);
    /* Counts compare as naturals, not as signed values. */
    const canon_p1_sig_entry big[1] = {{0, UINT64_MAX}}, one[1] = {{0, 1}};
    CHECK(canon_p1_sig_compare(big, 1, one, 1) == 1);
}

static void test_random_against_dense(void)
{
    canon_p1_scratch s;
    canon_p1_scratch_init(&s);
    for (int round = 0; round < 400; ++round) {
        raw_graph r;
        random_graph(&r, (uint32_t)(check_rng() % (MAXN + 1)));
        canon_root x;
        build_root(&x, &r);
        CHECK(canon_p1_scratch_reserve(&s, &x) == CANON_COMPLETE);
        canon_partition p;
        CHECK(canon_partition_init(&p, r.n) == CANON_COMPLETE);
        /* a random ordered partition: up to three random splits */
        uint32_t keys[MAXN];
        int splits = (int)(check_rng() % 4);
        for (int t = 0; t < splits; ++t) {
            for (uint32_t v = 0; v < r.n; ++v) {
                keys[v] = (uint32_t)(check_rng() % 3);
            }
            (void)canon_partition_split(&p, keys);
        }
        CHECK(canon_p1_graph_signatures(&p, &x.u.graph, &s) == CANON_COMPLETE);
        uint64_t dense[MAXN][DENSE];
        size_t len = 0;
        for (uint32_t v = 0; v < r.n; ++v) {
            len = dense_sig(&r, &p, v, dense[v]);
            /* the sparse list is exactly the nonzero entries, by increasing index */
            size_t sl = 0;
            const canon_p1_sig_entry *e = canon_p1_signature(&s, &x.u.graph, v, &sl);
            uint64_t expanded[DENSE];
            memset(expanded, 0, sizeof expanded);
            for (size_t i = 0; i < sl; ++i) {
                CHECK(e[i].count > 0 && e[i].index < len);
                CHECK(i == 0 || e[i - 1].index < e[i].index);
                if (e[i].index < len) {
                    expanded[e[i].index] = e[i].count;
                }
            }
            CHECK(dense_cmp(expanded, dense[v], len) == 0);
        }
        for (uint32_t v = 0; v < r.n; ++v) {
            for (uint32_t w = 0; w < r.n; ++w) {
                size_t lv = 0, lw = 0;
                const canon_p1_sig_entry *sv = canon_p1_signature(&s, &x.u.graph, v, &lv);
                const canon_p1_sig_entry *sw = canon_p1_signature(&s, &x.u.graph, w, &lw);
                CHECK(canon_p1_sig_compare(sv, lv, sw, lw) == sign(dense_cmp(dense[v], dense[w],
                                                                             len)));
            }
        }
        check_stage_o(&r, &x, &p, &s);
        canon_partition_free(&p);
        canon_root_free(&x);
    }
    canon_p1_scratch_free(&s);
}

/* Hand-built graphs on the unit partition: expected cells listed by hand from spec 7.1. */
static void hand_case(uint32_t n, const canon_arc *arcs, size_t count, const uint32_t *want_lab)
{
    raw_graph r;
    r.n = n;
    r.arc_count = count;
    memcpy(r.arcs, arcs, count * sizeof *arcs);
    canon_root x;
    build_root(&x, &r);
    canon_p1_scratch s;
    canon_p1_scratch_init(&s);
    CHECK(canon_p1_scratch_reserve(&s, &x) == CANON_COMPLETE);
    canon_partition p;
    CHECK(canon_partition_init(&p, n) == CANON_COMPLETE);
    CHECK(canon_p1_stage_o(&p, &x, &s) == CANON_COMPLETE);
    CHECK(p.cells == n); /* each case below is discretised */
    for (uint32_t i = 0; i < n; ++i) {
        CHECK(p.lab[i] == want_lab[i]);
    }
    canon_partition_free(&p);
    canon_p1_scratch_free(&s);
    canon_root_free(&x);
}

static void test_hand(void)
{
    const uint8_t *a = (const uint8_t *)"a";
    /* spec 7.4: "O signatures are (1,0) at 0 and (0,1) at 1, so cells become [{1},{0}]". */
    const canon_arc one_arc[1] = {{0, 1, NULL, 0, 1}};
    const uint32_t lab74[2] = {1, 0};
    hand_case(2, one_arc, 1, lab74);
    /* Loop: sig(0) = (1, 1) (once out, once in), sig(1) = (0, 0): [{1},{0}]. */
    const canon_arc loop[1] = {{0, 0, NULL, 0, 1}};
    hand_case(2, loop, 1, lab74);
    /* Labels "" < "a": sig(0) = (1,0, 0,0), sig(1) = (0,1, 0,1), sig(2) = (0,0, 1,0):
     * order 2 < 1 < 0. */
    const canon_arc labelled[2] = {{0, 1, NULL, 0, 1}, {2, 1, a, 1, 1}};
    const uint32_t lab_l[3] = {2, 1, 0};
    hand_case(3, labelled, 2, lab_l);
    /* Multiplicities: sig(0) = (2,0), sig(1) = (1,0), sig(2) = (0,3): order 2 < 1 < 0. */
    const canon_arc mult[2] = {{0, 2, NULL, 0, 2}, {1, 2, NULL, 0, 1}};
    hand_case(3, mult, 2, lab_l);
    /* Non-graph roots: the O stage is the identity (spec 7.1 "empty vector"). */
    canon_root sub;
    memset(&sub, 0, sizeof sub);
    sub.kind = CANON_ROOT_SUBSET;
    sub.n = 3;
    const uint32_t atoms[1] = {1};
    CHECK(canon_subset_init(&sub.u.subset, 3, atoms, 1) == CANON_COMPLETE);
    canon_p1_scratch s;
    canon_p1_scratch_init(&s);
    CHECK(canon_p1_scratch_reserve(&s, &sub) == CANON_COMPLETE);
    canon_partition p;
    CHECK(canon_partition_init(&p, 3) == CANON_COMPLETE);
    CHECK(canon_p1_stage_o(&p, &sub, &s) == CANON_COMPLETE);
    CHECK(p.cells == 1);
    canon_partition_free(&p);
    canon_p1_scratch_free(&s);
    canon_root_free(&sub);
}

int main(void)
{
    test_compare_hand();
    test_hand();
    test_random_against_dense();
    return check_finish("test_signature");
}
