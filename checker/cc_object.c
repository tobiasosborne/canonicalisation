/* Objects in canon-check (spec 2.1, 4.1, 4.2): the CDAG-2 decoder for a set of atoms and a
 * coloured directed multigraph, the ATOM-TRANSPORT-1 action, extensional equality and the
 * canonical encoder.  Written from the specification (checker/README.md). */
#include <stdlib.h>
#include <string.h>

#include "cc.h"

void cc_object_init(cc_object *x)
{
    memset(x, 0, sizeof *x);
}

void cc_object_free(cc_object *x)
{
    free(x->atoms);
    free(x->member);
    if (x->owns_tables) {
        free(x->colours);
        free(x->labels);
    }
    free(x->colour_of);
    free(x->arcs);
    cc_object_init(x);
}

/* ---- growable buffer ---- */

void cc_buf_init(cc_buf *b)
{
    memset(b, 0, sizeof *b);
}

void cc_buf_free(cc_buf *b)
{
    free(b->data);
    cc_buf_init(b);
}

bool cc_buf_put(cc_buf *b, const uint8_t *bytes, size_t len)
{
    if (len > SIZE_MAX - b->len) {
        return false;
    }
    if (b->len + len > b->cap) {
        size_t cap = b->cap < 256 ? 256 : b->cap;
        while (cap < b->len + len) {
            if (cap > SIZE_MAX / 2u) {
                return false;
            }
            cap *= 2u;
        }
        uint8_t *grown = realloc(b->data, cap);
        if (grown == NULL) {
            return false;
        }
        b->data = grown;
        b->cap = cap;
    }
    if (len > 0) {
        memcpy(b->data + b->len, bytes, len);
    }
    b->len += len;
    return true;
}

bool cc_buf_put_u8(cc_buf *b, uint8_t v)
{
    return cc_buf_put(b, &v, 1);
}

bool cc_buf_put_u32(cc_buf *b, uint32_t v)
{
    const uint8_t be[4] = {(uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v};
    return cc_buf_put(b, be, 4);
}

/* spec 4.1 B order: (byte length, unsigned bytes). */
static int bytes_order(const cc_bytes *a, const cc_bytes *b)
{
    if (a->len != b->len) {
        return a->len < b->len ? -1 : 1;
    }
    const int c = a->len == 0 ? 0 : memcmp(a->data, b->data, a->len);
    return c < 0 ? -1 : (c > 0 ? 1 : 0);
}

static int bytes_qsort(const void *a, const void *b)
{
    return bytes_order(a, b);
}

/* Sort `count` strings and keep the distinct ones (B order); rank[i] receives the rank of
 * string i.  table gets the distinct strings (count entries allocated). */
static cc_reason rank_strings(const cc_bytes *strings, uint32_t count, cc_bytes **table,
                              uint32_t *table_count, uint32_t *rank)
{
    cc_reason why = CC_ACCEPT;
    cc_bytes *sorted = cc_alloc(count, sizeof *sorted, &why);
    if (sorted == NULL) {
        return why;
    }
    if (count > 0) {
        memcpy(sorted, strings, (size_t)count * sizeof *sorted);
        qsort(sorted, count, sizeof *sorted, bytes_qsort);
    }
    uint32_t distinct = 0;
    for (uint32_t i = 0; i < count; ++i) {
        if (i == 0 || bytes_order(&sorted[i - 1], &sorted[i]) != 0) {
            sorted[distinct++] = sorted[i];
        }
    }
    for (uint32_t i = 0; i < count; ++i) {
        /* binary search in the distinct table */
        uint32_t lo = 0, hi = distinct;
        while (lo + 1 < hi) {
            const uint32_t mid = lo + (hi - lo) / 2u;
            if (bytes_order(&sorted[mid], &strings[i]) <= 0) {
                lo = mid;
            } else {
                hi = mid;
            }
        }
        rank[i] = lo;
    }
    *table = sorted;
    *table_count = distinct;
    return CC_ACCEPT;
}

/* spec 4.1 Nat(k) = U32(b) || big-endian bytes; this checker admits k < 2^64 (the engine's
 * count-bit limit) and rejects a longer field as CC_CAPACITY. */
static cc_reason get_nat(cc_reader *r, uint64_t *out)
{
    uint32_t b = 0;
    if (!cc_get_u32(r, &b)) {
        return CC_TRUNCATED;
    }
    if (b > 8) {
        return CC_CAPACITY;
    }
    if (r->len - r->at < b) {
        return CC_TRUNCATED;
    }
    uint64_t v = 0;
    for (uint32_t i = 0; i < b; ++i) {
        if (i == 0 && r->data[r->at] == 0) {
            return CC_OBJECT_MALFORMED; /* spec 4.1: no leading zero */
        }
        v = v << 8 | r->data[r->at++];
    }
    *out = v;
    return CC_ACCEPT;
}

static int arc_order(const void *pa, const void *pb)
{
    const cc_arc *a = pa, *b = pb;
    if (a->source != b->source) {
        return a->source < b->source ? -1 : 1;
    }
    if (a->target != b->target) {
        return a->target < b->target ? -1 : 1;
    }
    if (a->label != b->label) {
        return a->label < b->label ? -1 : 1; /* label ranks follow B order */
    }
    return 0;
}

/* Sort arcs by (source, target, label) and combine duplicates by exact addition (spec 4.1). */
static cc_reason normalise_arcs(cc_arc *arcs, uint32_t *count)
{
    if (*count > 0) {
        qsort(arcs, *count, sizeof *arcs, arc_order);
    }
    uint32_t out = 0;
    for (uint32_t i = 0; i < *count; ++i) {
        if (out > 0 && arc_order(&arcs[out - 1], &arcs[i]) == 0) {
            if (arcs[i].multiplicity > UINT64_MAX - arcs[out - 1].multiplicity) {
                return CC_CAPACITY;
            }
            arcs[out - 1].multiplicity += arcs[i].multiplicity;
        } else {
            arcs[out++] = arcs[i];
        }
    }
    *count = out;
    return CC_ACCEPT;
}

typedef struct record {
    uint8_t tag;
    uint32_t atom;      /* tag 01 */
    size_t at;          /* offset of the payload (tags 04, 09) */
} record;

cc_reason cc_object_decode(const uint8_t *stream, size_t len, uint32_t n, cc_object *x)
{
    cc_object_init(x);
    cc_reader r = {stream, len, 0};
    /* spec 4.1: 43 4e 02 | U16(schema=1) | U16(action=1) | U32(n) | U32(q) */
    uint8_t magic[3] = {0, 0, 0};
    uint16_t schema = 0, action = 0;
    uint32_t sn = 0, q = 0;
    for (int i = 0; i < 3; ++i) {
        if (!cc_get_u8(&r, &magic[i])) {
            return CC_OBJECT_MALFORMED;
        }
    }
    if (magic[0] != 0x43 || magic[1] != 0x4e || magic[2] != 0x02 || !cc_get_u16(&r, &schema) ||
        !cc_get_u16(&r, &action) || !cc_get_u32(&r, &sn) || !cc_get_u32(&r, &q)) {
        return CC_OBJECT_MALFORMED;
    }
    if (schema != 1 || action != 1) {
        return CC_OBJECT_UNSUPPORTED;
    }
    if (sn != n) {
        return CC_DEGREE_MISMATCH;
    }
    if (q < 1 || q > r.len - r.at) {
        return CC_OBJECT_MALFORMED; /* every record is at least its tag byte (spec 4.1) */
    }
    cc_reason why = CC_ACCEPT;
    record *recs = cc_alloc(q, sizeof *recs, &why);
    if (recs == NULL) {
        return why;
    }
    x->n = n;
    /* first pass: locate every record (its payload is parsed by kind below) */
    for (uint32_t j = 0; j < q; ++j) {
        if (!cc_get_u8(&r, &recs[j].tag)) {
            why = CC_OBJECT_MALFORMED;
            goto done;
        }
        recs[j].at = r.at;
        if (recs[j].tag == 0x01) {
            if (!cc_get_u32(&r, &recs[j].atom) || recs[j].atom >= n) {
                why = CC_OBJECT_MALFORMED; /* spec 4.1: out-of-domain atom IDs are invalid */
                goto done;
            }
        } else if (recs[j].tag == 0x04) {
            uint32_t k = 0;
            if (!cc_get_u32(&r, &k) || r.len - r.at < 4u * (size_t)k) {
                why = CC_OBJECT_MALFORMED;
                goto done;
            }
            uint32_t prev = 0;
            for (uint32_t i = 0; i < k; ++i) {
                uint32_t ref = 0;
                (void)cc_get_u32(&r, &ref);
                if (ref >= j || (i > 0 && ref <= prev)) {
                    why = CC_OBJECT_MALFORMED; /* spec 4.1: increasing distinct references to
                                                  earlier records */
                    goto done;
                }
                prev = ref;
            }
        } else if (recs[j].tag == 0x09) {
            for (uint32_t v = 0; v < n; ++v) {
                const uint8_t *s = NULL;
                uint32_t l = 0;
                if (!cc_get_b(&r, &s, &l)) {
                    why = CC_OBJECT_MALFORMED;
                    goto done;
                }
            }
            uint32_t e = 0;
            if (!cc_get_u32(&r, &e)) {
                why = CC_OBJECT_MALFORMED;
                goto done;
            }
            for (uint32_t i = 0; i < e; ++i) {
                uint32_t s = 0, t = 0, l = 0;
                const uint8_t *lab = NULL;
                uint64_t m = 0;
                if (!cc_get_u32(&r, &s) || !cc_get_u32(&r, &t) || !cc_get_b(&r, &lab, &l)) {
                    why = CC_OBJECT_MALFORMED;
                    goto done;
                }
                why = get_nat(&r, &m);
                if (why == CC_TRUNCATED) {
                    why = CC_OBJECT_MALFORMED;
                }
                if (why != CC_ACCEPT) {
                    goto done;
                }
                if (s >= n || t >= n || m == 0) {
                    why = CC_OBJECT_MALFORMED; /* spec 4.1: zero multiplicities are invalid */
                    goto done;
                }
            }
        } else if (recs[j].tag >= 0x02 && recs[j].tag <= 0x0a) {
            why = CC_OBJECT_UNSUPPORTED; /* tags 02, 03, 05-08, 0a: certificate v1 */
            goto done;
        } else {
            why = CC_OBJECT_MALFORMED; /* spec 4.1: unknown type tags are invalid */
            goto done;
        }
    }
    uint32_t root = 0;
    if (!cc_get_u32(&r, &root) || root >= q) {
        why = CC_OBJECT_MALFORMED;
        goto done;
    }
    if (r.at != r.len) {
        why = CC_OBJECT_MALFORMED; /* spec 4.1: no trailing bytes */
        goto done;
    }
    /* second pass: build the root object */
    if (recs[root].tag == 0x04) {
        cc_reader rr = {stream, len, recs[root].at};
        uint32_t k = 0;
        (void)cc_get_u32(&rr, &k);
        x->graph = false;
        x->member = cc_alloc(n, sizeof *x->member, &why);
        x->atoms = cc_alloc(n, sizeof *x->atoms, &why);
        if (x->member == NULL || x->atoms == NULL) {
            goto done;
        }
        memset(x->member, 0, n > 0 ? n : 1u);
        for (uint32_t i = 0; i < k; ++i) {
            uint32_t ref = 0;
            (void)cc_get_u32(&rr, &ref);
            if (recs[ref].tag != 0x01) {
                why = CC_OBJECT_UNSUPPORTED; /* a set with a non-atom child is a nested object */
                goto done;
            }
            x->member[recs[ref].atom] = 1; /* spec 4.2: sets deduplicate equal children */
        }
        x->k = 0;
        for (uint32_t a = 0; a < n; ++a) {
            if (x->member[a]) {
                x->atoms[x->k++] = a;
            }
        }
    } else if (recs[root].tag == 0x09) {
        cc_reader rr = {stream, len, recs[root].at};
        x->graph = true;
        x->owns_tables = true;
        cc_bytes *colours = cc_alloc(n, sizeof *colours, &why);
        x->colour_of = cc_alloc(n, sizeof *x->colour_of, &why);
        if (colours == NULL || x->colour_of == NULL) {
            free(colours);
            goto done;
        }
        for (uint32_t v = 0; v < n; ++v) {
            (void)cc_get_b(&rr, &colours[v].data, &colours[v].len);
        }
        why = rank_strings(colours, n, &x->colours, &x->colour_count, x->colour_of);
        free(colours);
        if (why != CC_ACCEPT) {
            goto done;
        }
        uint32_t e = 0;
        (void)cc_get_u32(&rr, &e);
        if (e > (rr.len - rr.at) / 16u) {
            why = CC_OBJECT_MALFORMED; /* an arc is two U32, a B() and a natural: >= 16 bytes */
            goto done;
        }
        x->arcs = cc_alloc(e, sizeof *x->arcs, &why);
        cc_bytes *labels = cc_alloc(e, sizeof *labels, &why);
        uint32_t *label_rank = cc_alloc(e, sizeof *label_rank, &why);
        if (x->arcs == NULL || labels == NULL || label_rank == NULL) {
            free(labels);
            free(label_rank);
            goto done;
        }
        for (uint32_t i = 0; i < e; ++i) {
            (void)cc_get_u32(&rr, &x->arcs[i].source);
            (void)cc_get_u32(&rr, &x->arcs[i].target);
            (void)cc_get_b(&rr, &labels[i].data, &labels[i].len);
            (void)get_nat(&rr, &x->arcs[i].multiplicity);
        }
        why = rank_strings(labels, e, &x->labels, &x->label_count, label_rank);
        free(labels);
        if (why == CC_ACCEPT) {
            for (uint32_t i = 0; i < e; ++i) {
                x->arcs[i].label = label_rank[i];
            }
            x->e = e;
            why = normalise_arcs(x->arcs, &x->e);
        }
        free(label_rank);
        if (why != CC_ACCEPT) {
            goto done;
        }
    } else {
        why = CC_OBJECT_UNSUPPORTED; /* an atom root or a literal: nested-object territory */
    }
done:
    free(recs);
    if (why != CC_ACCEPT) {
        cc_object_free(x);
    }
    return why;
}

cc_reason cc_object_act(const cc_object *x, const uint32_t *p, cc_object *img)
{
    const uint32_t n = x->n;
    cc_reason why = CC_ACCEPT;
    /* storage for the image, reused when it fits */
    if (img->n != n || img->graph != x->graph || img->atoms == NULL || img->member == NULL ||
        (x->graph && (img->colour_of == NULL || img->e < x->e))) {
        cc_object_free(img);
        img->n = n;
        img->graph = x->graph;
        img->atoms = cc_alloc(n, sizeof *img->atoms, &why);
        img->member = cc_alloc(n, sizeof *img->member, &why);
        if (x->graph) {
            img->colour_of = cc_alloc(n, sizeof *img->colour_of, &why);
            img->arcs = cc_alloc(x->e, sizeof *img->arcs, &why);
        }
        if (img->atoms == NULL || img->member == NULL ||
            (x->graph && (img->colour_of == NULL || img->arcs == NULL))) {
            cc_object_free(img);
            return why;
        }
    }
    if (!x->graph) {
        /* spec 2.1: x^p = {p[a] : a in x} */
        memset(img->member, 0, n > 0 ? n : 1u);
        for (uint32_t i = 0; i < x->k; ++i) {
            img->member[p[x->atoms[i]]] = 1;
        }
        img->k = 0;
        for (uint32_t a = 0; a < n; ++a) {
            if (img->member[a]) {
                img->atoms[img->k++] = a;
            }
        }
        return CC_ACCEPT;
    }
    /* spec 2.1: colour'[p[v]] = colour[v]; each arc (s, t, l, m) becomes (p[s], p[t], l, m);
     * the tables (fixed byte strings) are shared with x */
    img->owns_tables = false;
    img->colours = x->colours; /* borrowed: never freed through img (see cc_object_free) */
    img->colour_count = x->colour_count;
    img->labels = x->labels;
    img->label_count = x->label_count;
    for (uint32_t v = 0; v < n; ++v) {
        img->colour_of[p[v]] = x->colour_of[v];
    }
    for (uint32_t i = 0; i < x->e; ++i) {
        img->arcs[i] = x->arcs[i];
        img->arcs[i].source = p[x->arcs[i].source];
        img->arcs[i].target = p[x->arcs[i].target];
    }
    img->e = x->e;
    if (img->e > 0) {
        qsort(img->arcs, img->e, sizeof *img->arcs, arc_order); /* distinct stays distinct */
    }
    return CC_ACCEPT;
}

bool cc_object_equal(const cc_object *a, const cc_object *b)
{
    if (a->graph != b->graph || a->n != b->n) {
        return false;
    }
    if (!a->graph) {
        return a->k == b->k && (a->k == 0 || memcmp(a->atoms, b->atoms, (size_t)a->k * sizeof *a->atoms) == 0);
    }
    /* colours by bytes (the tables may differ between two decoded objects) */
    for (uint32_t v = 0; v < a->n; ++v) {
        if (bytes_order(&a->colours[a->colour_of[v]], &b->colours[b->colour_of[v]]) != 0) {
            return false;
        }
    }
    if (a->e != b->e) {
        return false;
    }
    for (uint32_t i = 0; i < a->e; ++i) {
        const cc_arc *p = &a->arcs[i], *q = &b->arcs[i];
        if (p->source != q->source || p->target != q->target ||
            p->multiplicity != q->multiplicity ||
            bytes_order(&a->labels[p->label], &b->labels[q->label]) != 0) {
            return false;
        }
    }
    return true;
}

static bool put_b(cc_buf *out, const cc_bytes *s)
{
    return cc_buf_put_u32(out, s->len) && cc_buf_put(out, s->data, s->len);
}

static bool put_nat(cc_buf *out, uint64_t k)
{
    uint8_t bytes[8];
    uint32_t b = 0;
    for (uint64_t v = k; v != 0; v >>= 8) {
        ++b; /* shortest big-endian length, 0 for k = 0 (spec 4.1) */
    }
    for (uint32_t i = 0; i < b; ++i) {
        bytes[b - 1 - i] = (uint8_t)(k >> (8 * i));
    }
    return cc_buf_put_u32(out, b) && cc_buf_put(out, bytes, b);
}

cc_reason cc_object_encode(const cc_object *x, cc_buf *out)
{
    const uint32_t n = x->n;
    out->len = 0;
    static const uint8_t header[7] = {0x43, 0x4e, 0x02, 0x00, 0x01, 0x00, 0x01};
    bool ok = cc_buf_put(out, header, sizeof header) && cc_buf_put_u32(out, n);
    if (!x->graph) {
        /* spec 4.2 canonical DAG of a set of atoms: the k atom records (height 0, sorted by
         * their bytes, i.e. by atom), then the set (height 1) referring to 0..k-1; root k */
        if (x->k == UINT32_MAX) {
            return CC_CAPACITY;
        }
        ok = ok && cc_buf_put_u32(out, x->k + 1u);
        for (uint32_t i = 0; i < x->k && ok; ++i) {
            ok = cc_buf_put_u8(out, 0x01) && cc_buf_put_u32(out, x->atoms[i]);
        }
        ok = ok && cc_buf_put_u8(out, 0x04) && cc_buf_put_u32(out, x->k);
        for (uint32_t i = 0; i < x->k && ok; ++i) {
            ok = cc_buf_put_u32(out, i);
        }
        ok = ok && cc_buf_put_u32(out, x->k);
        return ok ? CC_ACCEPT : CC_OUT_OF_MEMORY;
    }
    /* spec 4.1 tag 09: one record, root 0 */
    ok = ok && cc_buf_put_u32(out, 1) && cc_buf_put_u8(out, 0x09);
    for (uint32_t v = 0; v < n && ok; ++v) {
        ok = put_b(out, &x->colours[x->colour_of[v]]);
    }
    ok = ok && cc_buf_put_u32(out, x->e);
    for (uint32_t i = 0; i < x->e && ok; ++i) {
        const cc_arc *a = &x->arcs[i];
        ok = cc_buf_put_u32(out, a->source) && cc_buf_put_u32(out, a->target) &&
             put_b(out, &x->labels[a->label]) && put_nat(out, a->multiplicity);
    }
    ok = ok && cc_buf_put_u32(out, 0);
    return ok ? CC_ACCEPT : CC_OUT_OF_MEMORY;
}

uint32_t cc_object_initial_key(const cc_object *x, uint32_t a)
{
    /* spec 7.1: membership 0/1 on a subset; B(vertex_colour[a]) on a graph, as its rank */
    return x->graph ? x->colour_of[a] : x->member[a];
}
