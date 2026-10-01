/* Nested EXT-DAG-1 objects: record arena, extensional normalisation, canonical numbering and
 * the ATOM-TRANSPORT-1 action (spec 2.1, 4.1, 4.2, 9.4, 11.1; slice S5, docs/slices/S5.md
 * 3.1-3.3, 3.6). */
#include "object/dag.h"

#include <stdlib.h>
#include <string.h>

#include "arena/alloc.h"
#include "arena/checked.h"
#include "bsgs/chain.h"
#include "encoding/cdag_decode.h"
#include "encoding/cdag_encode.h"
#include "encoding/graph_stream.h"
#include "encoding/group_stream.h"
#include "object/graph.h"
#include "util/sort.h"

/* ---- arena ---- */

void canon_dag_init(canon_dag *d, uint32_t n)
{
    memset(d, 0, sizeof *d);
    d->n = n;
    d->root = CANON_DAG_NONE;
    canon_buf_init(&d->pool);
}

void canon_dag_free(canon_dag *d)
{
    free(d->recs);
    free(d->child);
    free(d->mult);
    canon_buf_free(&d->pool);
    canon_dag_init(d, 0);
}

void canon_dag_reset(canon_dag *d, uint32_t n)
{
    d->n = n;
    d->count = 0;
    d->refs = 0;
    canon_buf_truncate(&d->pool, 0);
    d->root = CANON_DAG_NONE;
    d->stream_size = 0;
    d->literal_bytes = 0;
    d->group_leaves = false;
    d->image_bound = 0;
}

uint32_t canon_dag_atom(const canon_dag *d, uint32_t i)
{
    /* spec 4.1: "01 U32(a)", big endian */
    const uint8_t *p = canon_dag_payload(d, i);
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | (uint32_t)p[3];
}

/* Reserve one more record, k more references and the payload, then append the record header
 * and payload; the caller fills the k child and mult entries at recs[count-1].child_off and
 * sets the height.  On failure nothing visible changes (count, refs and pool length are
 * committed only at the end; grown capacities are harmless). */
static canon_status append_begin(canon_dag *d, uint8_t tag, const uint8_t *payload,
                                 size_t payload_len, uint32_t k, canon_rec **out)
{
    /* spec 11.1: record indices and reference counts are uint32 (CANON_DAG_NONE reserved) */
    if (d->count >= CANON_DAG_NONE - 1u || k > CANON_DAG_NONE - 1u - d->refs) {
        return CANON_CAPACITY_LIMIT;
    }
    void *recs = d->recs, *child = d->child, *mult = d->mult;
    canon_status st = canon_grow_array(&recs, &d->rec_cap, d->count, 16u, sizeof *d->recs);
    d->recs = recs;
    if (st == CANON_COMPLETE) {
        st = canon_grow_array_to(&child, &d->child_cap, d->refs, (uint64_t)d->refs + k, 16u,
                                 sizeof *d->child);
        d->child = child;
    }
    if (st == CANON_COMPLETE) {
        st = canon_grow_array_to(&mult, &d->mult_cap, d->refs, (uint64_t)d->refs + k, 16u,
                                 sizeof *d->mult);
        d->mult = mult;
    }
    const size_t off = d->pool.len;
    if (st == CANON_COMPLETE) {
        st = canon_buf_put_bytes(&d->pool, payload, payload_len);
    }
    if (st != CANON_COMPLETE) {
        return st;
    }
    canon_rec *r = &d->recs[d->count];
    r->tag = tag;
    r->child_count = k;
    r->child_off = d->refs;
    r->payload_off = off;
    r->payload_len = payload_len;
    r->height = 0;
    d->count += 1;
    d->refs += k;
    *out = r;
    return CANON_COMPLETE;
}

/* spec 4.2: "height 0 if it has no child references, otherwise 1+maximum child height". */
static void set_height(canon_dag *d, canon_rec *r)
{
    uint32_t h = 0;
    for (uint32_t j = 0; j < r->child_count; ++j) {
        const uint32_t ch = d->recs[d->child[r->child_off + j]].height + 1u;
        h = ch > h ? ch : h; /* heights < record count <= 2^32 - 2: no overflow */
    }
    r->height = h;
}

canon_status canon_dag_append(canon_dag *d, uint8_t tag, const uint8_t *payload,
                              size_t payload_len, const uint32_t *children,
                              const uint64_t *counts, uint32_t k, uint32_t *index)
{
    for (uint32_t j = 0; j < k; ++j) {
        if (children[j] >= d->count) {
            return CANON_INVALID_INPUT; /* spec 4.1: children precede their parent */
        }
    }
    canon_rec *r = NULL;
    canon_status st = append_begin(d, tag, payload, payload_len, k, &r);
    if (st != CANON_COMPLETE) {
        return st;
    }
    for (uint32_t j = 0; j < k; ++j) {
        d->child[r->child_off + j] = children[j];
        d->mult[r->child_off + j] = counts != NULL ? counts[j] : 1u;
    }
    set_height(d, r);
    if (index != NULL) {
        *index = d->count - 1u;
    }
    return CANON_COMPLETE;
}

/* ---- scratch ---- */

void canon_dag_scratch_init(canon_dag_scratch *s)
{
    memset(s, 0, sizeof *s);
    canon_dag_init(&s->nodes, 0);
    canon_dag_init(&s->raw, 0);
    canon_buf_init(&s->leaf);
    canon_buf_init(&s->leaf2);
    canon_perm_table_init(&s->gens, 0);
    canon_coset_scratch_init(&s->coset);
}

void canon_dag_scratch_free(canon_dag_scratch *s)
{
    free(s->map);
    free(s->reach);
    free(s->final);
    free(s->order);
    free(s->order_tmp);
    free(s->table);
    free(s->pairs);
    free(s->pairs_tmp);
    free(s->fchild);
    free(s->fmult);
    free(s->perm);
    free(s->bits);
    canon_dag_free(&s->nodes);
    canon_dag_free(&s->raw);
    canon_buf_free(&s->leaf);
    canon_buf_free(&s->leaf2);
    canon_perm_table_free(&s->gens);
    canon_coset_scratch_free(&s->coset);
    canon_dag_scratch_init(s);
}

/* Reserve scratch of `need` elements (contents not kept), grow-only. */
static canon_status reserve(void **p, uint32_t *cap, uint64_t need, size_t elem)
{
    void *q = *p;
    canon_status st = canon_grow_array_to(&q, cap, 0, need > 0 ? need : 1u, 16u, elem);
    *p = q;
    return st;
}

/* Per-point arrays for degree n: perm, perm2, inv (one block of 3n words) and the payload
 * readers' bit maps (canon_cdag_bits_words(n) words).  Grow-only. */
static canon_status reserve_points(canon_dag_scratch *s, uint32_t n)
{
    if (s->perm != NULL && n <= s->n_cap) {
        return CANON_COMPLETE;
    }
    canon_status st = CANON_COMPLETE;
    size_t words = 0;
    if (!canon_size_mul((size_t)n, 3u, &words)) {
        return CANON_CAPACITY_LIMIT; /* spec 11.1 */
    }
    uint32_t *block = canon_alloc_array(words, sizeof *block, &st);
    uint64_t *bits = canon_alloc_array(canon_cdag_bits_words(n), sizeof *bits, &st);
    if (block == NULL || bits == NULL) {
        free(block);
        free(bits);
        return st;
    }
    free(s->perm);
    free(s->bits);
    s->perm = block;
    s->perm2 = block + (size_t)n;
    s->inv = block + 2u * (size_t)n;
    s->bits = bits;
    s->n_cap = n;
    return CANON_COMPLETE;
}

/* Empty the generator table for degree n (rows are n words; a table of another degree is
 * released first, since its capacity counts rows of the old degree). */
static void gens_reset(canon_perm_table *t, uint32_t n)
{
    if (t->n != n) {
        canon_perm_table_free(t);
        canon_perm_table_init(t, n);
    }
    t->count = 0;
}

/* ---- canonical leaf payloads (spec 4.1, 9.4) ---- */

/* Read a Group(H) payload at rd into s->gens and build its verified chain in *chain (spec 9.1
 * "exact verification is mandatory": the chain decides canonical bytes). */
static canon_status group_chain(canon_cdag_reader *rd, uint32_t n, canon_dag_scratch *s,
                                canon_bsgs *chain)
{
    canon_cdag_reason reason = CANON_CDAG_OK;
    gens_reset(&s->gens, n);
    canon_status st = canon_cdag_read_group(rd, n, s->bits, &s->gens, s->perm, &reason);
    if (st == CANON_COMPLETE) {
        st = canon_bsgs_build_verified(chain, n, s->gens.data, s->gens.count);
    }
    return st;
}

/* Write the canonical payload of the leaf (tag, payload) of degree n into s->leaf:
 *   07: Group(H) by spec 9.4 rules 1 and 2 (src/encoding/group_stream.h);
 *   08: Group(H) || Perm(r0), r0 the least element of H r (spec 9.4);
 *   09: the normalised graph record payload (spec 4.1: arcs combined and sorted).
 * When `conj` is not NULL the leaf is first conjugated by g = conj (spec 2.1): every generator
 * p of H becomes g^-1 p g, i.e. q[g[v]] = g[p[v]], and r becomes g^-1 r, i.e. r'[g[v]] = r[v]
 * (spec 2.1: "A labeling-coset object H rho ... becomes g^-1 H rho = (g^-1 H g)(g^-1 rho)"). */
static canon_status canonical_leaf(uint32_t n, uint8_t tag, const uint8_t *payload, size_t len,
                                   const uint32_t *conj, canon_dag_scratch *s)
{
    canon_status st = reserve_points(s, n);
    if (st != CANON_COMPLETE) {
        return st;
    }
    canon_cdag_reader rd = {payload, len, 0};
    canon_cdag_reason reason = CANON_CDAG_OK;
    canon_buf_truncate(&s->leaf, 0);
    if (tag == CANON_REC_GRAPH) {
        canon_graph g;
        canon_graph_init_empty(&g);
        st = canon_cdag_read_graph(&rd, n, &g, &reason);
        canon_buf_truncate(&s->leaf2, 0);
        if (st == CANON_COMPLETE) {
            st = canon_graph_stream_write(&s->leaf2, &g); /* spec 4.1 normalised record */
        }
        canon_graph_free(&g);
        /* the stream is header (15 bytes), tag 09, payload, U32(root): keep the payload */
        if (st == CANON_COMPLETE) {
            st = canon_buf_put_bytes(&s->leaf, s->leaf2.data + 16u, s->leaf2.len - 20u);
        }
        return st;
    }
    canon_bsgs chain;
    canon_bsgs_init(&chain, n);
    gens_reset(&s->gens, n);
    st = canon_cdag_read_group(&rd, n, s->bits, &s->gens, s->perm, &reason);
    uint32_t *r = s->perm2; /* coset representative */
    if (st == CANON_COMPLETE && tag == CANON_REC_COSET) {
        st = canon_cdag_read_perm(&rd, n, s->bits, r, &reason);
    }
    if (st == CANON_COMPLETE && rd.pos != len) {
        st = CANON_INVALID_INPUT; /* the payload is exactly Group || [Perm] */
    }
    if (st == CANON_COMPLETE && conj != NULL) {
        for (uint32_t i = 0; i < s->gens.count; ++i) {
            uint32_t *p = s->gens.data + (size_t)i * n;
            for (uint32_t v = 0; v < n; ++v) {
                s->perm[conj[v]] = conj[p[v]]; /* spec 2.1: g^-1 p g */
            }
            if (n > 0) {
                memcpy(p, s->perm, (size_t)n * sizeof *p);
            }
        }
        if (tag == CANON_REC_COSET) {
            for (uint32_t v = 0; v < n; ++v) {
                s->perm[conj[v]] = r[v]; /* spec 2.1: g^-1 r, (g^-1 r)[g[v]] = r[v] */
            }
            r = s->perm;
        }
    }
    if (st == CANON_COMPLETE) {
        st = canon_bsgs_build_verified(&chain, n, s->gens.data, s->gens.count);
    }
    if (st == CANON_COMPLETE) {
        /* spec 9.4: "Use this deterministic priority, never whichever representation the
         * caller supplied"; for a coset "first choose its least image-array element r0 by
         * successive point constraints, then encode the canonical Group(H) and Perm(r0)" */
        st = tag == CANON_REC_GROUP
                 ? canon_group_bytes_write(&s->leaf, &chain, NULL, &s->coset)
                 : canon_coset_bytes_write(&s->leaf, &chain, r, s->inv, NULL, &s->coset);
    }
    canon_bsgs_free(&chain);
    return st;
}

/* ---- interning (spec 4.2) ---- */

/* FNV-1a over the exact content; spec 4.2: "Hashes are lookup aids with exact collision
 * resolution" (every candidate is compared byte for byte). */
static uint64_t hash_bytes(uint64_t h, const uint8_t *b, size_t len)
{
    for (size_t i = 0; i < len; ++i) {
        h = (h ^ b[i]) * 0x100000001b3u;
    }
    return h;
}

static uint64_t hash_u64(uint64_t h, uint64_t v)
{
    for (int i = 0; i < 8; ++i) {
        h = (h ^ (v & 0xffu)) * 0x100000001b3u;
        v >>= 8;
    }
    return h;
}

static int pair_cmp(const void *a, const void *b, void *ctx)
{
    (void)ctx;
    const uint32_t x = ((const canon_dag_pair *)a)->id, y = ((const canon_dag_pair *)b)->id;
    return x < y ? -1 : (x > y ? 1 : 0);
}

/* Is interned node e equal to the candidate (tag, payload, pairs[0..k))? */
static bool node_equal(const canon_dag *nodes, uint32_t e, uint8_t tag, const uint8_t *payload,
                       size_t len, const canon_dag_pair *pairs, uint32_t k)
{
    const canon_rec *r = &nodes->recs[e];
    if (r->tag != tag || r->payload_len != len || r->child_count != k) {
        return false;
    }
    if (len > 0 && memcmp(canon_dag_payload(nodes, e), payload, len) != 0) {
        return false;
    }
    for (uint32_t j = 0; j < k; ++j) {
        if (nodes->child[r->child_off + j] != pairs[j].id ||
            nodes->mult[r->child_off + j] != pairs[j].count) {
            return false;
        }
    }
    return true;
}

/* Intern input record i of `in` into s->nodes; s->map[i] receives its node id. */
static canon_status intern_one(const canon_dag *in, uint32_t i, canon_dag_scratch *s,
                               bool leaves_canonical)
{
    const canon_rec *r = &in->recs[i];
    const uint8_t *payload = canon_dag_payload(in, i);
    size_t len = r->payload_len;
    uint32_t k = 0;
    canon_status st = CANON_COMPLETE;
    if (canon_rec_has_children(r->tag)) {
        k = r->child_count;
        for (uint32_t j = 0; j < k; ++j) {
            s->pairs[j].id = s->map[in->child[r->child_off + j]];
            s->pairs[j].count = r->tag == CANON_REC_MULTISET ? in->mult[r->child_off + j] : 1u;
        }
        if (r->tag != CANON_REC_TUPLE) {
            /* spec 4.2: "For sets/multisets sort by these child indices"; equal children are
             * adjacent after sorting by node id */
            canon_stable_sort(s->pairs, k, sizeof *s->pairs, s->pairs_tmp, pair_cmp, NULL);
            uint32_t w = 0;
            for (uint32_t j = 0; j < k; ++j) {
                if (w > 0 && s->pairs[w - 1].id == s->pairs[j].id) {
                    if (r->tag == CANON_REC_SET) {
                        continue; /* spec 4.2: "Sets deduplicate equal children" */
                    }
                    /* spec 4.2: "multisets combine equal children and add positive counts";
                     * count-bit limit 64 (detailed plan 2.1, spec 11.1) */
                    if (!canon_u64_add(s->pairs[w - 1].count, s->pairs[j].count,
                                       &s->pairs[w - 1].count)) {
                        return CANON_CAPACITY_LIMIT;
                    }
                    continue;
                }
                s->pairs[w++] = s->pairs[j];
            }
            k = w;
        }
    } else {
        if (r->tag == CANON_REC_GRAPH && i != in->root) {
            /* docs/slices/S5.md 1: graph records below the root are a later slice; spec 2.1
             * "A recognised but unavailable type returns UNSUPPORTED_ACTION" */
            return CANON_UNSUPPORTED_ACTION;
        }
        if (!leaves_canonical &&
            (r->tag == CANON_REC_GROUP || r->tag == CANON_REC_COSET || r->tag == CANON_REC_GRAPH)) {
            /* spec 4.2 "exact equality/normal forms for group leaves" (spec 9.4); atoms,
             * literals and the strict Perm grammar are canonical as decoded */
            st = canonical_leaf(in->n, r->tag, payload, len, NULL, s);
            if (st != CANON_COMPLETE) {
                return st;
            }
            payload = s->leaf.data;
            len = s->leaf.len;
        }
    }
    uint64_t h = hash_bytes(0xcbf29ce484222325u, &r->tag, 1u);
    h = hash_bytes(h, payload, len);
    for (uint32_t j = 0; j < k; ++j) {
        h = hash_u64(hash_u64(h, s->pairs[j].id), s->pairs[j].count);
    }
    const uint32_t mask = s->table_slots - 1u;
    uint32_t slot = (uint32_t)(h & mask);
    while (s->table[slot] != 0) {
        const uint32_t e = s->table[slot] - 1u;
        if (node_equal(&s->nodes, e, r->tag, payload, len, s->pairs, k)) {
            s->map[i] = e; /* spec 4.2: equal normalised nodes are one node */
            return CANON_COMPLETE;
        }
        slot = (slot + 1u) & mask;
    }
    canon_rec *nr = NULL;
    st = append_begin(&s->nodes, r->tag, payload, len, k, &nr);
    if (st != CANON_COMPLETE) {
        return st;
    }
    for (uint32_t j = 0; j < k; ++j) {
        s->nodes.child[nr->child_off + j] = s->pairs[j].id;
        s->nodes.mult[nr->child_off + j] = s->pairs[j].count;
    }
    set_height(&s->nodes, nr);
    s->map[i] = s->nodes.count - 1u;
    s->table[slot] = s->nodes.count; /* id + 1 */
    return CANON_COMPLETE;
}

/* ---- canonical numbering (spec 4.2) ---- */

typedef struct number_ctx {
    const canon_dag_scratch *s;
} number_ctx;

static int height_cmp(const void *a, const void *b, void *ctx)
{
    const canon_dag *nodes = &((const number_ctx *)ctx)->s->nodes;
    const uint32_t x = nodes->recs[*(const uint32_t *)a].height;
    const uint32_t y = nodes->recs[*(const uint32_t *)b].height;
    return x < y ? -1 : (x > y ? 1 : 0);
}

static int u32_order(uint32_t a, uint32_t b)
{
    return a < b ? -1 : (a > b ? 1 : 0);
}

/* spec 4.2: "within one height sort their exact record bytes using the already assigned
 * smaller-height child indices"; spec 4.3 unsigned byte order, a proper prefix smaller.  The
 * bytes are compared field by field without being written: the tag byte first; a leaf's
 * record is tag || payload, so equal tags compare their payloads as bytes; a tuple, set or
 * multiset record is U32(k) followed by U32 child indices (and, for a multiset, Nat(count)
 * after each).  Fixed-width big-endian fields compare as numbers, and Nat(m) = U32(b) ||
 * shortest big-endian bytes compares as the number m (a longer shortest form is a larger
 * number), so the first differing field decides exactly as the first differing byte does. */
static int record_cmp(const void *a, const void *b, void *ctx)
{
    const canon_dag_scratch *s = ((const number_ctx *)ctx)->s;
    const canon_dag *nodes = &s->nodes;
    const uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
    const canon_rec *rx = &nodes->recs[x], *ry = &nodes->recs[y];
    if (rx->tag != ry->tag) {
        return rx->tag < ry->tag ? -1 : 1;
    }
    if (!canon_rec_has_children(rx->tag)) {
        return canon_bytes_compare(canon_dag_payload(nodes, x), rx->payload_len,
                                   canon_dag_payload(nodes, y), ry->payload_len);
    }
    int c = u32_order(rx->child_count, ry->child_count);
    for (uint32_t j = 0; c == 0 && j < rx->child_count; ++j) {
        c = u32_order(s->fchild[rx->child_off + j], s->fchild[ry->child_off + j]);
        if (c == 0 && rx->tag == CANON_REC_MULTISET) {
            const uint64_t mx = s->fmult[rx->child_off + j], my = s->fmult[ry->child_off + j];
            c = mx < my ? -1 : (mx > my ? 1 : 0);
        }
    }
    return c;
}

/* Children of node id in final indices (s->fchild/fmult at its child_off); sets and multisets
 * sorted by final index (spec 4.2 "For sets/multisets sort by these child indices"). */
static void final_children(canon_dag_scratch *s, uint32_t id)
{
    const canon_rec *r = &s->nodes.recs[id];
    for (uint32_t j = 0; j < r->child_count; ++j) {
        s->pairs[j].id = s->final[s->nodes.child[r->child_off + j]];
        s->pairs[j].count = s->nodes.mult[r->child_off + j];
    }
    if (r->tag != CANON_REC_TUPLE) {
        canon_stable_sort(s->pairs, r->child_count, sizeof *s->pairs, s->pairs_tmp, pair_cmp,
                          NULL);
    }
    for (uint32_t j = 0; j < r->child_count; ++j) {
        s->fchild[r->child_off + j] = s->pairs[j].id;
        s->fmult[r->child_off + j] = s->pairs[j].count;
    }
}

/* Number the interned nodes and write them to out in that order. */
static canon_status renumber(canon_dag_scratch *s, canon_dag *out)
{
    const canon_dag *nodes = &s->nodes;
    const uint32_t m = nodes->count;
    void *p = s->final;
    canon_status st = reserve(&p, &s->final_cap, m, sizeof *s->final);
    s->final = p;
    p = s->order;
    if (st == CANON_COMPLETE) {
        st = reserve(&p, &s->order_cap, m, sizeof *s->order);
        s->order = p;
    }
    p = s->order_tmp;
    if (st == CANON_COMPLETE) {
        st = reserve(&p, &s->order_tmp_cap, m, sizeof *s->order_tmp);
        s->order_tmp = p;
    }
    p = s->fchild;
    if (st == CANON_COMPLETE) {
        st = reserve(&p, &s->fchild_cap, nodes->refs, sizeof *s->fchild);
        s->fchild = p;
    }
    p = s->fmult;
    if (st == CANON_COMPLETE) {
        st = reserve(&p, &s->fmult_cap, nodes->refs, sizeof *s->fmult);
        s->fmult = p;
    }
    if (st != CANON_COMPLETE) {
        return st;
    }
    number_ctx ctx = {s};
    for (uint32_t id = 0; id < m; ++id) {
        s->order[id] = id;
    }
    /* spec 4.2: "Number nodes by increasing height" */
    canon_stable_sort(s->order, m, sizeof *s->order, s->order_tmp, height_cmp, &ctx);
    uint32_t next = 0;
    for (uint32_t lo = 0; lo < m;) {
        const uint32_t h = nodes->recs[s->order[lo]].height;
        uint32_t hi = lo;
        while (hi < m && nodes->recs[s->order[hi]].height == h) {
            final_children(s, s->order[hi]); /* children have smaller heights: numbered */
            ++hi;
        }
        /* spec 4.2: "within one height sort their exact record bytes" */
        canon_stable_sort(s->order + lo, hi - lo, sizeof *s->order, s->order_tmp, record_cmp,
                          &ctx);
        for (uint32_t j = lo; j < hi; ++j) {
            /* spec 4.2 "Equal records are one node": interning already merged equal values,
             * and distinct interned nodes have distinct records (distinct tag, payload or
             * child identities), so equality here would be a broken invariant. */
            if (j > lo && record_cmp(&s->order[j - 1], &s->order[j], &ctx) == 0) {
                return CANON_INTERNAL_ERROR;
            }
            s->final[s->order[j]] = next++;
        }
        lo = hi;
    }
    for (uint32_t f = 0; f < m; ++f) {
        const uint32_t id = s->order[f];
        const canon_rec *r = &nodes->recs[id];
        st = canon_dag_append(out, r->tag, canon_dag_payload(nodes, id), r->payload_len,
                              s->fchild + r->child_off, s->fmult + r->child_off, r->child_count,
                              NULL);
        if (st != CANON_COMPLETE) {
            return st;
        }
    }
    return CANON_COMPLETE;
}

/* Summary of a normalised arena (spec 11.1 sizes; detailed plan 2.1 capacity counts). */
static canon_status summarise(canon_dag *d)
{
    canon_status st = canon_dag_stream_measure(d, &d->stream_size);
    if (st != CANON_COMPLETE) {
        return st;
    }
    d->literal_bytes = 0;
    d->group_leaves = false;
    for (uint32_t i = 0; i < d->count; ++i) {
        const canon_rec *r = &d->recs[i];
        /* B(s): |s| = payload - U32 length; the sum is checked (spec 11.1) */
        if (r->tag == CANON_REC_LITERAL &&
            !canon_u64_add(d->literal_bytes, (uint64_t)(r->payload_len - 4u), &d->literal_bytes)) {
            return CANON_CAPACITY_LIMIT;
        }
        d->group_leaves = d->group_leaves || r->tag == CANON_REC_GROUP ||
                          r->tag == CANON_REC_COSET;
    }
    d->image_bound = d->stream_size;
    return CANON_COMPLETE;
}

canon_status canon_dag_normalise(const canon_dag *in, canon_dag *out, canon_dag_scratch *s,
                                 bool leaves_canonical)
{
    /* module contract: out is a separate arena, not part of the scratch */
    if (out == in || out == &s->nodes || out == &s->raw) {
        return CANON_INVALID_INPUT;
    }
    canon_dag_reset(out, in->n);
    if (in->count == 0 || in->root >= in->count) {
        return CANON_INVALID_INPUT; /* spec 4.1: q >= 1 and root < q */
    }
    /* Records after the root cannot be reachable (children precede parents). */
    const uint32_t m = in->root + 1u;
    /* spec 4.2: "Validate acyclicity with a bounded explicit traversal before interning": one
     * pass over every reference, checking the spec 4.1 rule that a child precedes its parent,
     * which excludes cycles. */
    uint32_t max_k = 0;
    for (uint32_t i = 0; i < m; ++i) {
        const canon_rec *r = &in->recs[i];
        for (uint32_t j = 0; j < r->child_count; ++j) {
            if (in->child[r->child_off + j] >= i) {
                return CANON_INVALID_INPUT;
            }
        }
        max_k = r->child_count > max_k ? r->child_count : max_k;
    }
    void *p = s->map;
    canon_status st = reserve(&p, &s->map_cap, m, sizeof *s->map);
    s->map = p;
    p = s->reach;
    if (st == CANON_COMPLETE) {
        st = reserve(&p, &s->reach_cap, m, sizeof *s->reach);
        s->reach = p;
    }
    p = s->pairs;
    if (st == CANON_COMPLETE) {
        st = reserve(&p, &s->pairs_cap, max_k, sizeof *s->pairs);
        s->pairs = p;
    }
    p = s->pairs_tmp;
    if (st == CANON_COMPLETE) {
        st = reserve(&p, &s->pairs_tmp_cap, max_k, sizeof *s->pairs_tmp);
        s->pairs_tmp = p;
    }
    if (st != CANON_COMPLETE) {
        return st;
    }
    /* spec 4.2: "Discard unreachable input nodes": mark from the root downwards (every child
     * has a smaller index, so one descending pass reaches the whole closure). */
    memset(s->reach, 0, m);
    s->reach[in->root] = 1;
    uint64_t reachable = 0;
    for (uint32_t i = m; i-- > 0;) {
        if (s->reach[i] == 0) {
            continue;
        }
        ++reachable;
        const canon_rec *r = &in->recs[i];
        for (uint32_t j = 0; j < r->child_count; ++j) {
            s->reach[in->child[r->child_off + j]] = 1;
        }
    }
    /* open-addressing table at most half full */
    uint64_t slots = 16;
    while (slots < 2u * reachable) {
        slots *= 2u;
    }
    p = s->table;
    st = reserve(&p, &s->table_cap, slots, sizeof *s->table);
    s->table = p;
    if (st != CANON_COMPLETE) {
        return st;
    }
    s->table_slots = (uint32_t)slots; /* a power of two <= table_cap */
    memset(s->table, 0, (size_t)slots * sizeof *s->table);
    canon_dag_reset(&s->nodes, in->n);
    /* spec 4.2: "bottom-up normalise reachable ones by exact type/payload/normalised children" */
    for (uint32_t i = 0; i < m; ++i) {
        if (s->reach[i] != 0) {
            st = intern_one(in, i, s, leaves_canonical);
            if (st != CANON_COMPLETE) {
                return st;
            }
        }
    }
    st = renumber(s, out);
    if (st != CANON_COMPLETE) {
        canon_dag_reset(out, in->n);
        return st;
    }
    /* spec 4.2: "The root is the last record: every proper descendant has smaller height." */
    out->root = s->final[s->map[in->root]];
    if (out->root != out->count - 1u) {
        canon_dag_reset(out, in->n);
        return CANON_INTERNAL_ERROR;
    }
    st = summarise(out);
    if (st != CANON_COMPLETE) {
        canon_dag_reset(out, in->n);
    }
    return st;
}

/* ---- action (spec 2.1) ---- */

canon_status canon_dag_act(const canon_dag *x, const uint32_t *g, canon_dag *out,
                           canon_dag_scratch *s)
{
    const uint32_t n = x->n;
    /* module contract: x is a normalised arena (its root is its last record) other than out
     * and the scratch's arenas */
    if (out == x || out == &s->nodes || out == &s->raw || x == &s->raw || x == &s->nodes ||
        x->count == 0 || x->root != x->count - 1u) {
        return CANON_INVALID_INPUT;
    }
    canon_status st = reserve_points(s, n);
    if (st != CANON_COMPLETE) {
        return st;
    }
    canon_dag *raw = &s->raw;
    canon_dag_reset(raw, n);
    for (uint32_t i = 0; i < x->count && st == CANON_COMPLETE; ++i) {
        const canon_rec *r = &x->recs[i];
        const uint8_t *payload = canon_dag_payload(x, i);
        size_t len = r->payload_len;
        switch (r->tag) {
        case CANON_REC_ATOM: {
            /* spec 2.1: "maps atom a to g[a]" */
            const uint32_t b = g[canon_dag_atom(x, i)];
            canon_buf_truncate(&s->leaf, 0);
            st = canon_buf_put_u32(&s->leaf, b);
            payload = s->leaf.data;
            len = s->leaf.len;
            break;
        }
        case CANON_REC_PERM: {
            /* spec 2.1: "A permutation object p becomes g^-1 p g": (g^-1 p g)[g[v]] = g[p[v]],
             * the cycles of p relabelled through g (docs/slices/S5.md 3.3) */
            canon_cdag_reader rd = {payload, len, 0};
            canon_cdag_reason reason = CANON_CDAG_OK;
            st = canon_cdag_read_perm(&rd, n, s->bits, s->perm, &reason);
            for (uint32_t v = 0; st == CANON_COMPLETE && v < n; ++v) {
                s->perm2[g[v]] = g[s->perm[v]];
            }
            canon_buf_truncate(&s->leaf, 0);
            if (st == CANON_COMPLETE) {
                st = canon_perm_bytes_write(&s->leaf, s->perm2, n); /* spec 4.1 Perm */
            }
            payload = s->leaf.data;
            len = s->leaf.len;
            break;
        }
        case CANON_REC_GROUP:
        case CANON_REC_COSET:
            /* spec 2.1: "a subgroup H becomes g^-1 H g"; a labeling coset H rho becomes
             * (g^-1 H g)(g^-1 rho); the canonical payload is recomputed (spec 9.4) */
            st = canonical_leaf(n, r->tag, payload, len, g, s);
            payload = s->leaf.data;
            len = s->leaf.len;
            break;
        case CANON_REC_GRAPH:
            st = CANON_INTERNAL_ERROR; /* graph roots are canon_graph objects, not arenas */
            break;
        default:
            /* literals are fixed (spec 2.1 "fixes literal bytes"); tuples, sets and multisets
             * keep their children ("preserves tuple positions and multiplicities, and acts
             * recursively"): the child records are replaced by their images in place */
            break;
        }
        if (st == CANON_COMPLETE) {
            const bool kids = r->child_count > 0;
            st = canon_dag_append(raw, r->tag, payload, len, kids ? x->child + r->child_off : NULL,
                                  kids ? x->mult + r->child_off : NULL, r->child_count, NULL);
        }
    }
    if (st != CANON_COMPLETE) {
        canon_dag_reset(out, n);
        return st;
    }
    raw->root = x->root;
    /* Re-normalise: sets and multisets re-sort their relabelled children and every height
     * re-sorts its records (spec 4.2).  The leaves are canonical already. */
    return canon_dag_normalise(raw, out, s, true);
}

/* ---- equality, bounds, limits ---- */

bool canon_dag_equal(const canon_dag *a, const canon_dag *b)
{
    if (a->n != b->n || a->count != b->count || a->root != b->root || a->refs != b->refs) {
        return false;
    }
    for (uint32_t i = 0; i < a->count; ++i) {
        const canon_rec *x = &a->recs[i], *y = &b->recs[i];
        if (x->tag != y->tag || x->child_count != y->child_count ||
            x->payload_len != y->payload_len ||
            (x->payload_len > 0 &&
             memcmp(canon_dag_payload(a, i), canon_dag_payload(b, i), x->payload_len) != 0)) {
            return false;
        }
        for (uint32_t j = 0; j < x->child_count; ++j) {
            if (a->child[x->child_off + j] != b->child[y->child_off + j] ||
                a->mult[x->child_off + j] != b->mult[y->child_off + j]) {
                return false;
            }
        }
    }
    return true;
}

canon_status canon_dag_image_bound(canon_dag *d, canon_dag_scratch *s)
{
    d->image_bound = d->stream_size;
    if (!d->group_leaves) {
        return CANON_COMPLETE; /* exact: see dag.h */
    }
    canon_status st = reserve_points(s, d->n);
    uint64_t bound = d->stream_size;
    for (uint32_t i = 0; i < d->count && st == CANON_COMPLETE; ++i) {
        const canon_rec *r = &d->recs[i];
        if (r->tag != CANON_REC_GROUP && r->tag != CANON_REC_COSET) {
            continue;
        }
        canon_cdag_reader rd = {canon_dag_payload(d, i), r->payload_len, 0};
        canon_bsgs chain;
        canon_bsgs_init(&chain, d->n);
        st = group_chain(&rd, d->n, s, &chain);
        uint64_t b = 0;
        /* spec 11.1 "a conservative input-derived bound": spec 9.4 bounds Group(H) through
         * k <= floor(log2 |H|), and |g^-1 H g| = |H|; a coset adds Perm(r0) <= 4 + 8n */
        if (st == CANON_COMPLETE &&
            !canon_group_bytes_bound(d->n, chain.order, r->tag == CANON_REC_COSET, &b)) {
            st = CANON_CAPACITY_LIMIT;
        }
        canon_bsgs_free(&chain);
        if (st == CANON_COMPLETE &&
            (!canon_u64_add(bound - r->payload_len, b, &bound))) { /* payload <= stream size */
            st = CANON_CAPACITY_LIMIT;
        }
    }
    if (st == CANON_COMPLETE) {
        d->image_bound = bound;
    }
    return st;
}

canon_status canon_dag_check_limits(const canon_dag *d, const canon_dag_limits *lim)
{
    /* spec 11.1: "A problem-time capacity descriptor fixes degree/node/reference/literal/
     * output/count-bit limits.  Validation is deterministic over the normalised input." */
    if (d->n > lim->max_n || d->count > lim->max_nodes || d->refs > lim->max_refs ||
        d->literal_bytes > lim->max_literal_bytes) {
        return CANON_CAPACITY_LIMIT;
    }
    return CANON_COMPLETE;
}
