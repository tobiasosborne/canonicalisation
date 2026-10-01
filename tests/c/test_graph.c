/* Unit tests for the graph object (src/object/graph.c, slice S2): spec 4.1 import and
 * normalisation (duplicate arcs combined, zero multiplicities invalid, sort by
 * (source, target, B(label)), labels by (length, bytes)), spec 2.1 action, extensional
 * equality, loops, insertion-order independence (spec 5), CSR/CSC consistency, capacity
 * limits (spec 11.1) and the simple undirected wrapper (spec 4.1). */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "encoding/graph_stream.h"
#include "object/graph.h"
#include "perm/perm.h"

static const uint8_t *str(const char *s)
{
    return (const uint8_t *)s;
}

/* Label bytes of arc i. */
static int label_is(const canon_graph *g, uint32_t i, const char *want, size_t want_len)
{
    size_t len = 0;
    const uint8_t *b = canon_byte_table_get(&g->labels, g->arcs[i].label, &len);
    return len == want_len && (len == 0 || memcmp(b, want, len) == 0);
}

/* CSR/CSC cover every arc exactly once at the right vertex. */
static void check_index(const canon_graph *g)
{
    CHECK(g->out_start[0] == 0 && g->out_start[g->n] == g->e);
    CHECK(g->in_start[0] == 0 && g->in_start[g->n] == g->e);
    for (uint32_t v = 0; v < g->n; ++v) {
        for (uint32_t i = g->out_start[v]; i < g->out_start[v + 1]; ++i) {
            CHECK(g->arcs[i].source == v);
        }
        for (uint32_t i = g->in_start[v]; i < g->in_start[v + 1]; ++i) {
            CHECK(g->arcs[g->in_arc[i]].target == v);
            if (i > g->in_start[v]) {
                CHECK(g->in_arc[i - 1] < g->in_arc[i]);
            }
        }
    }
    for (uint32_t i = 1; i < g->e; ++i) {
        const canon_graph_arc *a = &g->arcs[i - 1], *b = &g->arcs[i];
        /* spec 4.1: strictly increasing (source, target, label) after combining */
        CHECK(a->source < b->source || (a->source == b->source && a->target < b->target) ||
              (a->source == b->source && a->target == b->target && a->label < b->label));
    }
    uint64_t total = 0;
    for (uint32_t i = 0; i < g->e; ++i) {
        CHECK(g->arcs[i].multiplicity > 0);
        total += g->arcs[i].multiplicity;
    }
    CHECK(total == g->total_multiplicity);
}

static void stream_of(const canon_graph *g, canon_buf *b)
{
    canon_buf_truncate(b, 0);
    CHECK(canon_graph_stream_write(b, g) == CANON_COMPLETE);
}

static void test_combine_and_loops(void)
{
    canon_graph a, b;
    /* spec 4.1 and S2 brief: (0,0,"",1) twice equals (0,0,"",2), the n = 1 loop case. */
    const canon_arc two_units[2] = {{0, 0, NULL, 0, 1}, {0, 0, NULL, 0, 1}};
    const canon_arc one_double[1] = {{0, 0, NULL, 0, 2}};
    CHECK(canon_graph_init(&a, 1, NULL, NULL, two_units, 2) == CANON_COMPLETE);
    CHECK(canon_graph_init(&b, 1, NULL, NULL, one_double, 1) == CANON_COMPLETE);
    CHECK(a.e == 1 && a.arcs[0].multiplicity == 2 && a.total_multiplicity == 2);
    CHECK(a.arcs[0].source == 0 && a.arcs[0].target == 0); /* loops survive */
    CHECK(canon_graph_equal(&a, &b));
    check_index(&a);
    canon_buf sa, sb;
    canon_buf_init(&sa);
    canon_buf_init(&sb);
    stream_of(&a, &sa);
    stream_of(&b, &sb);
    CHECK(sa.len == sb.len && memcmp(sa.data, sb.data, sa.len) == 0);
    canon_graph_free(&a);
    canon_graph_free(&b);
    /* Different labels do not combine. */
    const canon_arc mixed[3] = {{0, 1, str("a"), 1, 1}, {0, 1, NULL, 0, 1}, {0, 1, str("a"), 1, 3}};
    CHECK(canon_graph_init(&a, 2, NULL, NULL, mixed, 3) == CANON_COMPLETE);
    CHECK(a.e == 2 && a.labels.count == 2);
    CHECK(label_is(&a, 0, "", 0) && a.arcs[0].multiplicity == 1);
    CHECK(label_is(&a, 1, "a", 1) && a.arcs[1].multiplicity == 4);
    check_index(&a);
    canon_graph_free(&a);
    canon_buf_free(&sa);
    canon_buf_free(&sb);
}

static void test_label_order(void)
{
    /* spec 4.1: "Labels compare by (byte length, unsigned bytes), including the empty label":
     * "" < "a" < "b" < "\xff" < "\x00\x00" (longer after every shorter one regardless of bytes). */
    const canon_arc arcs[5] = {{0, 1, str("\x00\x00"), 2, 1},
                               {0, 1, str("\xff"), 1, 1},
                               {0, 1, str("b"), 1, 1},
                               {0, 1, NULL, 0, 1},
                               {0, 1, str("a"), 1, 1}};
    canon_graph g;
    CHECK(canon_graph_init(&g, 2, NULL, NULL, arcs, 5) == CANON_COMPLETE);
    CHECK(g.e == 5 && g.labels.count == 5);
    CHECK(label_is(&g, 0, "", 0));
    CHECK(label_is(&g, 1, "a", 1));
    CHECK(label_is(&g, 2, "b", 1));
    CHECK(label_is(&g, 3, "\xff", 1));
    CHECK(label_is(&g, 4, "\x00\x00", 2));
    canon_graph_free(&g);
    /* Colours: sorted by (length, bytes), duplicates merged; colour_id is the rank. */
    const uint8_t *colours[4] = {str("bb"), str("c"), NULL, str("c")};
    const size_t lengths[4] = {2, 1, 0, 1};
    CHECK(canon_graph_init(&g, 4, colours, lengths, NULL, 0) == CANON_COMPLETE);
    CHECK(g.colours.count == 3);
    CHECK(g.colour_id[0] == 2 && g.colour_id[1] == 1 && g.colour_id[2] == 0 &&
          g.colour_id[3] == 1);
    CHECK(canon_graph_initial_key(&g, 2) < canon_graph_initial_key(&g, 1));
    CHECK(g.e == 0); /* spec 4.1: graphs with no arcs remain valid and retain all colours */
    check_index(&g);
    canon_graph_free(&g);
}

static void test_invalid_and_capacity(void)
{
    canon_graph g;
    const canon_arc zero[1] = {{0, 1, NULL, 0, 0}};
    CHECK(canon_graph_init(&g, 2, NULL, NULL, zero, 1) == CANON_INVALID_INPUT);
    const canon_arc range[1] = {{0, 2, NULL, 0, 1}};
    CHECK(canon_graph_init(&g, 2, NULL, NULL, range, 1) == CANON_INVALID_INPUT);
    const canon_arc src_range[1] = {{5, 0, NULL, 0, 1}};
    CHECK(canon_graph_init(&g, 2, NULL, NULL, src_range, 1) == CANON_INVALID_INPUT);
    const canon_arc null_label[1] = {{0, 1, NULL, 3, 1}};
    CHECK(canon_graph_init(&g, 2, NULL, NULL, null_label, 1) == CANON_INVALID_INPUT);
    CHECK(canon_graph_init(&g, 2, NULL, NULL, NULL, 1) == CANON_INVALID_INPUT);
    const uint8_t *colours[1] = {NULL};
    const size_t bad_len[1] = {2};
    CHECK(canon_graph_init(&g, 1, colours, bad_len, NULL, 0) == CANON_INVALID_INPUT);
    canon_graph_free(&g); /* valid after failure */
    /* spec 11.1 / detailed plan 2.1: a total multiplicity above uint64 is a capacity limit. */
    const canon_arc big[2] = {{0, 1, NULL, 0, (uint64_t)1 << 63}, {1, 0, NULL, 0, (uint64_t)1 << 63}};
    CHECK(canon_graph_init(&g, 2, NULL, NULL, big, 2) == CANON_CAPACITY_LIMIT);
    const canon_arc combine_big[2] = {{0, 1, NULL, 0, UINT64_MAX}, {0, 1, NULL, 0, 1}};
    CHECK(canon_graph_init(&g, 2, NULL, NULL, combine_big, 2) == CANON_CAPACITY_LIMIT);
    const canon_arc fits[2] = {{0, 1, NULL, 0, UINT64_MAX - 1}, {0, 1, NULL, 0, 1}};
    CHECK(canon_graph_init(&g, 2, NULL, NULL, fits, 2) == CANON_COMPLETE);
    CHECK(g.e == 1 && g.arcs[0].multiplicity == UINT64_MAX && g.total_multiplicity == UINT64_MAX);
    canon_graph_free(&g);
}

/* Random graph helpers. */
static const char *const LABELS[3] = {"", "a", "bc"};

static size_t random_arcs(uint32_t n, canon_arc *arcs, size_t cap)
{
    size_t count = 0;
    if (n == 0) {
        return 0;
    }
    size_t want = (size_t)(check_rng() % (cap + 1));
    for (; count < want; ++count) {
        const char *l = LABELS[check_rng() % 3];
        arcs[count].source = (uint32_t)(check_rng() % n);
        arcs[count].target = (uint32_t)(check_rng() % n);
        arcs[count].label = str(l);
        arcs[count].label_length = strlen(l);
        arcs[count].multiplicity = 1 + check_rng() % 3;
    }
    return count;
}

static void test_insertion_order_and_action(void)
{
    canon_buf s1, s2;
    canon_buf_init(&s1);
    canon_buf_init(&s2);
    canon_graph img, img2;
    canon_graph_init_empty(&img);
    canon_graph_init_empty(&img2);
    for (int round = 0; round < 300; ++round) {
        uint32_t n = (uint32_t)(check_rng() % 6);
        canon_arc arcs[24], shuffled[48];
        size_t count = random_arcs(n, arcs, 24);
        /* spec 5: arc insertion order and splitting a multiplicity into duplicates have no
         * meaning. */
        size_t sc = 0;
        for (size_t i = 0; i < count; ++i) {
            if (arcs[i].multiplicity >= 2 && check_rng() % 2 == 0) {
                shuffled[sc] = arcs[i];
                shuffled[sc++].multiplicity = 1;
                shuffled[sc] = arcs[i];
                shuffled[sc++].multiplicity = arcs[i].multiplicity - 1;
            } else {
                shuffled[sc++] = arcs[i];
            }
        }
        for (size_t i = sc; i > 1; --i) {
            size_t j = (size_t)(check_rng() % i);
            canon_arc t = shuffled[i - 1];
            shuffled[i - 1] = shuffled[j];
            shuffled[j] = t;
        }
        const uint8_t *colours[6];
        size_t lengths[6];
        for (uint32_t v = 0; v < n; ++v) {
            const char *c = LABELS[check_rng() % 3];
            colours[v] = str(c);
            lengths[v] = strlen(c);
        }
        canon_graph a, b;
        CHECK(canon_graph_init(&a, n, colours, lengths, arcs, count) == CANON_COMPLETE);
        CHECK(canon_graph_init(&b, n, colours, lengths, shuffled, sc) == CANON_COMPLETE);
        CHECK(canon_graph_equal(&a, &b));
        /* identical normalised representation, field by field (the struct has padding) */
        CHECK(a.e == b.e && a.labels.count == b.labels.count);
        for (uint32_t i = 0; i < a.e && a.e == b.e; ++i) {
            CHECK(a.arcs[i].source == b.arcs[i].source && a.arcs[i].target == b.arcs[i].target &&
                  a.arcs[i].label == b.arcs[i].label &&
                  a.arcs[i].multiplicity == b.arcs[i].multiplicity);
        }
        for (uint32_t v = 0; v < n; ++v) {
            CHECK(a.colour_id[v] == b.colour_id[v]);
        }
        check_index(&a);
        stream_of(&a, &s1);
        stream_of(&b, &s2);
        CHECK(s1.len == s2.len && memcmp(s1.data, s2.data, s1.len) == 0);

        /* spec 2.1 action: build the expected image directly from the mapped input arcs and
         * colours, and compare with canon_graph_act_into. */
        uint32_t p[6], q[6], pq[6];
        for (uint32_t v = 0; v < n; ++v) {
            p[v] = q[v] = v;
        }
        for (uint32_t v = n; v > 1; --v) {
            uint32_t j = (uint32_t)(check_rng() % v), t = p[v - 1];
            p[v - 1] = p[j];
            p[j] = t;
            j = (uint32_t)(check_rng() % v);
            t = q[v - 1];
            q[v - 1] = q[j];
            q[j] = t;
        }
        canon_arc mapped[24];
        const uint8_t *mcol[6];
        size_t mlen[6];
        for (size_t i = 0; i < count; ++i) {
            mapped[i] = arcs[i];
            mapped[i].source = p[arcs[i].source];
            mapped[i].target = p[arcs[i].target];
        }
        for (uint32_t v = 0; v < n; ++v) {
            mcol[p[v]] = colours[v]; /* colour'[p[v]] = colour[v] */
            mlen[p[v]] = lengths[v];
        }
        canon_graph want;
        CHECK(canon_graph_init(&want, n, mcol, mlen, mapped, count) == CANON_COMPLETE);
        CHECK(canon_graph_act_into(&a, p, &img) == CANON_COMPLETE);
        CHECK(canon_graph_equal(&img, &want));
        /* An image carries no CSR/CSC (review item 2); building it on demand is consistent. */
        CHECK(!img.indexed && !img.imported && a.indexed && a.imported);
        CHECK(canon_graph_build_index(&img) == CANON_COMPLETE && img.indexed);
        check_index(&img);
        CHECK(img.total_multiplicity == a.total_multiplicity);
        /* spec 3: (x^p)^q = x^(pq), with (pq)[v] = q[p[v]]. */
        canon_perm_compose(p, q, pq, n);
        CHECK(canon_graph_act_into(&img, q, &img2) == CANON_COMPLETE);
        CHECK(canon_graph_act_into(&a, pq, &img) == CANON_COMPLETE);
        CHECK(canon_graph_equal(&img, &img2));
        /* The identity fixes x; stream length is invariant under the action (spec 11.1). */
        CHECK(canon_graph_act_into(&a, q, &img2) == CANON_COMPLETE);
        uint64_t size_a = 0, size_img = 0;
        CHECK(canon_graph_stream_size(&a, &size_a) == CANON_COMPLETE);
        CHECK(canon_graph_stream_size(&img2, &size_img) == CANON_COMPLETE);
        CHECK(size_a == size_img && size_a == s1.len);
        /* The cached length (review item 3) equals a fresh measurement of the image. */
        uint64_t measured = 0;
        CHECK(canon_graph_stream_measure(&img2, &measured) == CANON_COMPLETE &&
              measured == size_a);
        for (uint32_t v = 0; v < n; ++v) {
            q[v] = v;
        }
        CHECK(canon_graph_act_into(&a, q, &img2) == CANON_COMPLETE);
        CHECK(canon_graph_equal(&img2, &a));
        canon_graph_free(&want);
        canon_graph_free(&a);
        canon_graph_free(&b);
    }
    canon_graph_free(&img);
    canon_graph_free(&img2);
    canon_buf_free(&s1);
    canon_buf_free(&s2);
}

static void test_equality(void)
{
    canon_graph a, b;
    const canon_arc x[1] = {{0, 1, str("a"), 1, 1}};
    const canon_arc y[1] = {{0, 1, str("b"), 1, 1}};
    const canon_arc z[1] = {{0, 1, str("a"), 1, 2}};
    CHECK(canon_graph_init(&a, 2, NULL, NULL, x, 1) == CANON_COMPLETE);
    CHECK(canon_graph_init(&b, 2, NULL, NULL, y, 1) == CANON_COMPLETE);
    CHECK(!canon_graph_equal(&a, &b)); /* same label id 0, different bytes */
    canon_graph_free(&b);
    CHECK(canon_graph_init(&b, 2, NULL, NULL, z, 1) == CANON_COMPLETE);
    CHECK(!canon_graph_equal(&a, &b));
    canon_graph_free(&b);
    const uint8_t *col[2] = {str("c"), NULL};
    const size_t len[2] = {1, 0};
    CHECK(canon_graph_init(&b, 2, col, len, x, 1) == CANON_COMPLETE);
    CHECK(!canon_graph_equal(&a, &b));
    canon_graph_free(&b);
    CHECK(canon_graph_init(&b, 3, NULL, NULL, x, 1) == CANON_COMPLETE);
    CHECK(!canon_graph_equal(&a, &b));
    canon_graph_free(&b);
    canon_graph_free(&a);
}

static void test_simple_wrapper(void)
{
    canon_graph g, d;
    /* spec 4.1: duplicates coalesced (either orientation); two opposite unit arcs per edge. */
    const uint32_t edges[4][2] = {{1, 0}, {0, 1}, {2, 1}, {0, 1}};
    CHECK(canon_graph_init_simple(&g, 3, edges, 4) == CANON_COMPLETE);
    const canon_arc arcs[4] = {{0, 1, NULL, 0, 1}, {1, 0, NULL, 0, 1}, {1, 2, NULL, 0, 1},
                               {2, 1, NULL, 0, 1}};
    CHECK(canon_graph_init(&d, 3, NULL, NULL, arcs, 4) == CANON_COMPLETE);
    CHECK(canon_graph_equal(&g, &d));
    CHECK(g.e == 4 && g.total_multiplicity == 4);
    check_index(&g);
    canon_graph_free(&g);
    canon_graph_free(&d);
    const uint32_t loop[1][2] = {{2, 2}};
    CHECK(canon_graph_init_simple(&g, 3, loop, 1) == CANON_INVALID_INPUT); /* spec 4.1 */
    const uint32_t out[1][2] = {{0, 3}};
    CHECK(canon_graph_init_simple(&g, 3, out, 1) == CANON_INVALID_INPUT);
    CHECK(canon_graph_init_simple(&g, 3, NULL, 1) == CANON_INVALID_INPUT);
    CHECK(canon_graph_init_simple(&g, 0, NULL, 0) == CANON_COMPLETE);
    CHECK(g.n == 0 && g.e == 0);
    canon_graph_free(&g);
}

/* Review items 4 and 9: an imported graph is never used as image storage, and clearing an
 * image drops its borrowed tables. */
static void test_image_storage(void)
{
    const canon_arc arcs[2] = {{0, 1, str("a"), 1, 1}, {1, 1, NULL, 0, 2}};
    const uint32_t swap[2] = {1, 0};
    canon_graph a, b, img;
    CHECK(canon_graph_init(&a, 2, NULL, NULL, arcs, 2) == CANON_COMPLETE);
    CHECK(canon_graph_init(&b, 2, NULL, NULL, arcs, 1) == CANON_COMPLETE);
    CHECK(canon_graph_act_into(&a, swap, &b) == CANON_INVALID_INPUT);
    CHECK(b.imported && b.e == 1 && b.labels.count == 1); /* untouched */
    canon_graph_init_empty(&img);
    CHECK(canon_graph_act_into(&a, swap, &img) == CANON_COMPLETE);
    CHECK(img.labels.pool == a.labels.pool && img.e == 2); /* borrowed, not copied */
    CHECK(canon_graph_act_into(&img, swap, &img) == CANON_INVALID_INPUT); /* dest == g */
    canon_graph_image_clear(&img);
    CHECK(img.colours.offset == NULL && img.labels.offset == NULL && img.labels.pool == NULL);
    CHECK(img.n == 0 && img.e == 0 && img.cap_e >= 2);
    canon_graph_free(&a); /* the image no longer refers to a */
    CHECK(canon_graph_act_into(&b, swap, &img) == CANON_COMPLETE && img.e == 1);
    canon_graph_image_clear(&b); /* no-op on an imported graph */
    CHECK(b.labels.count == 1);
    canon_graph_free(&img);
    canon_graph_free(&b);
}

int main(void)
{
    test_combine_and_loops();
    test_label_order();
    test_invalid_and_capacity();
    test_insertion_order_and_action();
    test_equality();
    test_simple_wrapper();
    test_image_storage();
    return check_finish("test_graph");
}
