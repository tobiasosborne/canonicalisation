/* The strict CDAG-2 decoder, import and the canonical-form validator (spec 4.1, 4.2, 11.1;
 * slice S5, docs/slices/S5.md 3.5). */
#include "encoding/cdag_decode.h"

#include <stdlib.h>
#include <string.h>

#include "arena/alloc.h"
#include "arena/checked.h"
#include "encoding/cdag_encode.h"

/* ---- reader ---- */

/* Take k bytes at the current position; false (nothing consumed) if fewer remain. */
static bool take(canon_cdag_reader *r, size_t k, const uint8_t **out)
{
    if (k > r->len - r->pos) {
        return false;
    }
    *out = r->data + r->pos;
    r->pos += k;
    return true;
}

/* spec 4.1: "U16/U32 are exactly 2/4 bytes, big endian". */
static bool take_u16(canon_cdag_reader *r, uint16_t *v)
{
    const uint8_t *b = NULL;
    if (!take(r, 2u, &b)) {
        return false;
    }
    *v = (uint16_t)((unsigned)b[0] << 8 | (unsigned)b[1]);
    return true;
}

static bool take_u32(canon_cdag_reader *r, uint32_t *v)
{
    const uint8_t *b = NULL;
    if (!take(r, 4u, &b)) {
        return false;
    }
    *v = (uint32_t)b[0] << 24 | (uint32_t)b[1] << 16 | (uint32_t)b[2] << 8 | (uint32_t)b[3];
    return true;
}

static size_t remaining(const canon_cdag_reader *r)
{
    return r->len - r->pos;
}

static canon_status fail(canon_cdag_reason *reason, canon_cdag_reason why, canon_status st)
{
    *reason = why;
    return st;
}

#define TRUNCATED(reason) fail(reason, CANON_CDAG_TRUNCATED, CANON_INVALID_INPUT)

/* spec 4.1: "Nat(k)=U32(b) || big_endian_bytes(k,b) uses the shortest b, with b=0 for k=0 and
 * no leading zero otherwise."  A shortest form longer than 8 bytes is a number of at least
 * 2^64: well formed, but above the count-bit limit of 64 (detailed plan 2.1), so
 * CANON_CAPACITY_LIMIT. */
static canon_status take_nat(canon_cdag_reader *r, uint64_t *v, canon_cdag_reason *reason)
{
    uint32_t b = 0;
    const uint8_t *bytes = NULL;
    if (!take_u32(r, &b) || !take(r, b, &bytes)) {
        return TRUNCATED(reason);
    }
    if (b > 0 && bytes[0] == 0) {
        return fail(reason, CANON_CDAG_NAT_LEADING_ZERO, CANON_INVALID_INPUT);
    }
    if (b > 8u) {
        return fail(reason, CANON_CDAG_NAT_RANGE, CANON_CAPACITY_LIMIT);
    }
    uint64_t x = 0;
    for (uint32_t i = 0; i < b; ++i) {
        x = x << 8 | bytes[i];
    }
    *v = x;
    return CANON_COMPLETE;
}

/* ---- leaf payloads ---- */

size_t canon_cdag_bits_words(uint32_t n)
{
    return 2u * (((size_t)n + 63u) / 64u); /* <= 2^27 words for n < 2^32: no overflow */
}

static bool bit_get(const uint64_t *m, uint32_t v)
{
    return ((m[v / 64u] >> (v % 64u)) & 1u) != 0;
}

static void bit_set(uint64_t *m, uint32_t v)
{
    m[v / 64u] |= (uint64_t)1 << (v % 64u);
}

canon_status canon_cdag_read_perm(canon_cdag_reader *r, uint32_t n, uint64_t *bits,
                                  uint32_t *dense, canon_cdag_reason *reason)
{
    uint32_t s = 0;
    if (!take_u32(r, &s)) {
        return TRUNCATED(reason);
    }
    if (s > remaining(r) / 8u) {
        return TRUNCATED(reason); /* s pairs of 8 bytes */
    }
    const size_t words = canon_cdag_bits_words(n) / 2u;
    uint64_t *src = bits, *dst = bits + words;
    if (words > 0) {
        memset(bits, 0, 2u * words * sizeof *bits);
    }
    const size_t start = r->pos;
    /* spec 4.1: "s pairs U32(i),U32(p[i]) in increasing i, exactly the moved support ...
     * Reject duplicate sources, fixed pairs, out-of-range targets" */
    uint32_t last = 0;
    for (uint32_t t = 0; t < s; ++t) {
        uint32_t i = 0, j = 0;
        (void)take_u32(r, &i); /* available: checked above */
        (void)take_u32(r, &j);
        if (i >= n || j >= n) {
            return fail(reason, CANON_CDAG_PERM_RANGE, CANON_INVALID_INPUT);
        }
        if (t > 0 && i <= last) {
            return fail(reason, CANON_CDAG_PERM_ORDER, CANON_INVALID_INPUT);
        }
        if (i == j) {
            return fail(reason, CANON_CDAG_PERM_FIXED, CANON_INVALID_INPUT);
        }
        last = i;
        bit_set(src, i);
    }
    /* "or a nonbijection": the targets are distinct and are exactly the sources (unlisted
     * points are fixed, so the moved support maps onto itself) */
    r->pos = start;
    if (dense != NULL) {
        for (uint32_t v = 0; v < n; ++v) {
            dense[v] = v;
        }
    }
    for (uint32_t t = 0; t < s; ++t) {
        uint32_t i = 0, j = 0;
        (void)take_u32(r, &i);
        (void)take_u32(r, &j);
        if (!bit_get(src, j) || bit_get(dst, j)) {
            return fail(reason, CANON_CDAG_PERM_BIJECTION, CANON_INVALID_INPUT);
        }
        bit_set(dst, j);
        if (dense != NULL) {
            dense[i] = j;
        }
    }
    return CANON_COMPLETE;
}

canon_status canon_cdag_read_group(canon_cdag_reader *r, uint32_t n, uint64_t *bits,
                                   canon_perm_table *gens, uint32_t *tmp,
                                   canon_cdag_reason *reason)
{
    const uint8_t *mode = NULL;
    uint32_t k = 0;
    if (!take(r, 1u, &mode) || !take_u32(r, &k)) {
        return TRUNCATED(reason);
    }
    canon_status st = CANON_COMPLETE;
    if (*mode == 0x01) {
        /* spec 9.4 rule 1: "Encode 01 || U32(k) followed by each non-singleton orbit as
         * U32(size), U32(points...); points increase, blocks order by least point.  Omit
         * singleton orbits."  The blocks are orbits, hence disjoint. */
        const size_t words = canon_cdag_bits_words(n) / 2u;
        if (words > 0) {
            memset(bits, 0, words * sizeof *bits);
        }
        uint32_t prev_least = 0;
        for (uint32_t b = 0; b < k; ++b) {
            uint32_t size = 0;
            if (!take_u32(r, &size)) {
                return TRUNCATED(reason);
            }
            if (size < 2u) {
                return fail(reason, CANON_CDAG_GROUP_BLOCK, CANON_INVALID_INPUT);
            }
            if (size > remaining(r) / 4u) {
                return TRUNCATED(reason);
            }
            const size_t start = r->pos;
            uint32_t last = 0;
            for (uint32_t j = 0; j < size; ++j) {
                uint32_t v = 0;
                (void)take_u32(r, &v); /* available: checked above */
                if (v >= n) {
                    return fail(reason, CANON_CDAG_ATOM_RANGE, CANON_INVALID_INPUT);
                }
                if ((j > 0 && v <= last) || (j == 0 && b > 0 && v <= prev_least) ||
                    bit_get(bits, v)) {
                    return fail(reason, CANON_CDAG_GROUP_BLOCK, CANON_INVALID_INPUT);
                }
                bit_set(bits, v);
                if (j == 0) {
                    prev_least = v;
                }
                last = v;
            }
            if (gens == NULL) {
                continue;
            }
            /* the symmetric group on the block is generated by the transposition of its first
             * two points and the cycle through all of them */
            for (int which = 0; which < 2 && st == CANON_COMPLETE; ++which) {
                if (which == 1 && size == 2u) {
                    break; /* the cycle is the transposition */
                }
                for (uint32_t v = 0; v < n; ++v) {
                    tmp[v] = v;
                }
                canon_cdag_reader pts = {r->data, r->len, start};
                uint32_t first = 0, prev = 0, cur = 0;
                for (uint32_t j = 0; j < size; ++j) {
                    (void)take_u32(&pts, &cur);
                    if (j == 0) {
                        first = cur;
                    } else if (which == 1 || j == 1) {
                        tmp[prev] = cur; /* prev -> cur */
                    }
                    if (which == 0 && j == 1) {
                        tmp[cur] = first; /* the transposition closes */
                        break;
                    }
                    prev = cur;
                }
                if (which == 1) {
                    tmp[prev] = first; /* the cycle closes */
                }
                uint32_t index = 0;
                st = canon_perm_table_push(gens, tmp, &index);
            }
            if (st != CANON_COMPLETE) {
                return st;
            }
        }
        return CANON_COMPLETE;
    }
    if (*mode == 0x00) {
        /* spec 9.4 rule 2: "encode 00 || U32(k) || Perm(g_1)...Perm(g_k)" */
        for (uint32_t i = 0; i < k; ++i) {
            st = canon_cdag_read_perm(r, n, bits, gens != NULL ? tmp : NULL, reason);
            if (st == CANON_COMPLETE && gens != NULL) {
                uint32_t index = 0;
                st = canon_perm_table_push(gens, tmp, &index);
            }
            if (st != CANON_COMPLETE) {
                return st;
            }
        }
        return CANON_COMPLETE;
    }
    return fail(reason, CANON_CDAG_GROUP_MODE, CANON_INVALID_INPUT);
}

canon_status canon_cdag_read_graph(canon_cdag_reader *r, uint32_t n, canon_graph *g,
                                   canon_cdag_reason *reason)
{
    const size_t start = r->pos;
    const uint8_t *bytes = NULL;
    /* spec 4.1 tag 09: "n values B(vertex_colour), U32(e), e arc records" */
    for (uint32_t v = 0; v < n; ++v) {
        uint32_t len = 0;
        if (!take_u32(r, &len) || !take(r, len, &bytes)) {
            return TRUNCATED(reason);
        }
    }
    uint32_t e = 0;
    if (!take_u32(r, &e)) {
        return TRUNCATED(reason);
    }
    const size_t arcs_at = r->pos;
    for (uint32_t i = 0; i < e; ++i) {
        /* "An arc record is U32(source),U32(target),B(label),Nat(multiplicity)" */
        uint32_t s = 0, t = 0, len = 0;
        if (!take_u32(r, &s) || !take_u32(r, &t) || !take_u32(r, &len) ||
            !take(r, len, &bytes)) {
            return TRUNCATED(reason);
        }
        if (s >= n || t >= n) {
            return fail(reason, CANON_CDAG_ATOM_RANGE, CANON_INVALID_INPUT);
        }
        uint64_t m = 0;
        canon_status st = take_nat(r, &m, reason);
        if (st != CANON_COMPLETE) {
            return st;
        }
        if (m == 0) {
            /* spec 4.1: "Positive multiplicities require k>0"; "input zero multiplicities
             * are invalid" */
            return fail(reason, CANON_CDAG_ZERO_COUNT, CANON_INVALID_INPUT);
        }
    }
    if (g == NULL) {
        return CANON_COMPLETE;
    }
    /* Import (spec 4.1, as the S2 builder): arcs combined and sorted by canon_graph_init.  The
     * arrays are bounded by the bytes just validated (4 bytes per colour, 16 per arc). */
    canon_status st = CANON_COMPLETE;
    const uint8_t **colours = canon_alloc_array(n, sizeof *colours, &st);
    size_t *lengths = canon_alloc_array(n, sizeof *lengths, &st);
    canon_arc *arcs = canon_alloc_array(e, sizeof *arcs, &st);
    if (colours != NULL && lengths != NULL && arcs != NULL) {
        canon_cdag_reader again = {r->data, r->len, start};
        for (uint32_t v = 0; v < n; ++v) {
            uint32_t len = 0;
            (void)take_u32(&again, &len);
            (void)take(&again, len, &colours[v]);
            lengths[v] = len;
        }
        again.pos = arcs_at;
        for (uint32_t i = 0; i < e; ++i) {
            uint32_t len = 0;
            (void)take_u32(&again, &arcs[i].source);
            (void)take_u32(&again, &arcs[i].target);
            (void)take_u32(&again, &len);
            (void)take(&again, len, &arcs[i].label);
            arcs[i].label_length = len;
            (void)take_nat(&again, &arcs[i].multiplicity, reason);
        }
        st = canon_graph_init(g, n, n > 0 ? colours : NULL, n > 0 ? lengths : NULL, arcs, e);
    }
    free(colours);
    free(lengths);
    free(arcs);
    return st;
}

/* ---- stream ---- */

/* A growable list of child references and counts for one record (decoder scratch). */
typedef struct kids {
    uint32_t cap_c, cap_m;
    uint32_t *child;
    uint64_t *mult;
} kids;

static canon_status kids_reserve(kids *k, uint32_t need)
{
    void *c = k->child, *m = k->mult;
    canon_status st = canon_grow_array_to(&c, &k->cap_c, 0, need > 0 ? need : 1u, 16u,
                                          sizeof *k->child);
    k->child = c;
    if (st == CANON_COMPLETE) {
        st = canon_grow_array_to(&m, &k->cap_m, 0, need > 0 ? need : 1u, 16u, sizeof *k->mult);
        k->mult = m;
    }
    return st;
}

/* Record i (tag already read) with child references: spec 4.1 "03 U32(k), k child
 * references"; "04 U32(k), k increasing distinct child references"; "05 U32(k), k pairs
 * (child reference, Nat(m)), references increasing", m > 0; "Every child reference is a U32
 * index smaller than the parent index". */
static canon_status read_children(canon_cdag_reader *r, uint8_t tag, uint32_t i, kids *kd,
                                  canon_dag *out, canon_cdag_reason *reason)
{
    uint32_t k = 0;
    if (!take_u32(r, &k)) {
        return TRUNCATED(reason);
    }
    /* each reference takes at least 4 bytes (a multiset pair at least 9) */
    if (k > remaining(r) / (tag == CANON_REC_MULTISET ? 9u : 4u)) {
        return TRUNCATED(reason);
    }
    canon_status st = kids_reserve(kd, k);
    if (st != CANON_COMPLETE) {
        return st;
    }
    for (uint32_t j = 0; j < k; ++j) {
        uint32_t c = 0;
        if (!take_u32(r, &c)) {
            return TRUNCATED(reason);
        }
        if (c >= i) {
            return fail(reason, CANON_CDAG_FORWARD_REF, CANON_INVALID_INPUT);
        }
        if (j > 0 && tag != CANON_REC_TUPLE && c <= kd->child[j - 1]) {
            return fail(reason,
                        tag == CANON_REC_SET ? CANON_CDAG_SET_ORDER : CANON_CDAG_MULTISET_ORDER,
                        CANON_INVALID_INPUT);
        }
        kd->child[j] = c;
        kd->mult[j] = 1u;
        if (tag == CANON_REC_MULTISET) {
            st = take_nat(r, &kd->mult[j], reason);
            if (st != CANON_COMPLETE) {
                return st;
            }
            if (kd->mult[j] == 0) {
                return fail(reason, CANON_CDAG_ZERO_COUNT, CANON_INVALID_INPUT); /* "m>0" */
            }
        }
    }
    return canon_dag_append(out, tag, NULL, 0, kd->child, kd->mult, k, NULL);
}

static canon_status decode_records(canon_cdag_reader *r, uint32_t n, uint32_t q, uint64_t *bits,
                                   canon_dag *out, canon_cdag_reason *reason)
{
    kids kd = {0, 0, NULL, NULL};
    canon_status st = CANON_COMPLETE;
    for (uint32_t i = 0; i < q && st == CANON_COMPLETE; ++i) {
        const uint8_t *tagp = NULL;
        if (!take(r, 1u, &tagp)) {
            st = TRUNCATED(reason);
            break;
        }
        const uint8_t tag = *tagp;
        const size_t start = r->pos; /* the payload starts after the tag */
        const uint8_t *bytes = NULL;
        uint32_t v = 0;
        switch (tag) {
        case CANON_REC_ATOM:
            /* spec 4.1: "01 U32(a), a<n"; "out-of-domain atom IDs ... are invalid" */
            if (!take_u32(r, &v)) {
                st = TRUNCATED(reason);
            } else if (v >= n) {
                st = fail(reason, CANON_CDAG_ATOM_RANGE, CANON_INVALID_INPUT);
            }
            break;
        case CANON_REC_LITERAL:
            /* spec 4.1: "02 B(s)"; "a literal is its bytes" */
            if (!take_u32(r, &v) || !take(r, v, &bytes)) {
                st = TRUNCATED(reason);
            }
            break;
        case CANON_REC_TUPLE:
        case CANON_REC_SET:
        case CANON_REC_MULTISET:
            st = read_children(r, tag, i, &kd, out, reason);
            continue; /* appended there */
        case CANON_REC_PERM:
            st = canon_cdag_read_perm(r, n, bits, NULL, reason); /* spec 4.1 Perm(p) */
            break;
        case CANON_REC_GROUP:
            st = canon_cdag_read_group(r, n, bits, NULL, NULL, reason); /* spec 9.4 */
            break;
        case CANON_REC_COSET:
            /* spec 4.1: "08 Group(H), Perm(r)" */
            st = canon_cdag_read_group(r, n, bits, NULL, NULL, reason);
            if (st == CANON_COMPLETE) {
                st = canon_cdag_read_perm(r, n, bits, NULL, reason);
            }
            break;
        case CANON_REC_GRAPH:
            st = canon_cdag_read_graph(r, n, NULL, reason); /* spec 4.1 tag 09 */
            break;
        case CANON_REC_RELATIONS:
            /* docs/slices/S5.md 1: the relations record is a later slice; spec 2.1 "A
             * recognised but unavailable type returns UNSUPPORTED_ACTION" */
            st = fail(reason, CANON_CDAG_RELATIONS, CANON_UNSUPPORTED_ACTION);
            break;
        default:
            st = fail(reason, CANON_CDAG_UNKNOWN_TAG, CANON_INVALID_INPUT); /* spec 4.1 */
            break;
        }
        if (st == CANON_COMPLETE) {
            st = canon_dag_append(out, tag, r->data + start, r->pos - start, NULL, NULL, 0, NULL);
        }
    }
    free(kd.child);
    free(kd.mult);
    return st;
}

canon_status canon_cdag_decode(const uint8_t *stream, size_t length, const uint32_t *expect_n,
                               uint32_t max_n, canon_dag *out, canon_cdag_reason *reason)
{
    *reason = CANON_CDAG_OK;
    canon_dag_reset(out, 0);
    if (stream == NULL && length > 0) {
        return CANON_INVALID_INPUT;
    }
    canon_cdag_reader r = {stream, length, 0};
    const uint8_t *magic = NULL;
    /* spec 4.1: "43 4e 02 | U16(schema=1) | U16(action=1) | U32(n) | U32(q) | record[0] ...
     * record[q-1] | U32(root)" */
    if (!take(&r, 3u, &magic)) {
        return TRUNCATED(reason);
    }
    if (magic[0] != 0x43 || magic[1] != 0x4e) {
        return fail(reason, CANON_CDAG_BAD_MAGIC, CANON_INVALID_INPUT);
    }
    if (magic[2] != 0x02) {
        /* spec 4.1: "unknown schema/action/profile/encoding versions are unsupported, never
         * reinterpreted" (the third byte is the encoding version, CDAG-2) */
        return fail(reason, CANON_CDAG_VERSION, CANON_UNSUPPORTED_ACTION);
    }
    uint16_t schema = 0, action = 0;
    uint32_t n = 0, q = 0, root = 0;
    if (!take_u16(&r, &schema) || !take_u16(&r, &action)) {
        return TRUNCATED(reason);
    }
    if (schema != CANON_SCHEMA_EXT_DAG_1 || action != CANON_ACTION_ATOM_TRANSPORT_1) {
        return fail(reason, CANON_CDAG_SCHEMA_ACTION, CANON_UNSUPPORTED_ACTION);
    }
    if (!take_u32(&r, &n)) {
        return TRUNCATED(reason);
    }
    if (expect_n != NULL && n != *expect_n) {
        return fail(reason, CANON_CDAG_DEGREE, CANON_INVALID_INPUT); /* spec 17: explicit n */
    }
    if (n > max_n) {
        return fail(reason, CANON_CDAG_DEGREE_LIMIT, CANON_CAPACITY_LIMIT); /* spec 11.1 */
    }
    if (!take_u32(&r, &q)) {
        return TRUNCATED(reason);
    }
    if (q == 0) {
        return fail(reason, CANON_CDAG_EMPTY, CANON_INVALID_INPUT); /* spec 4.1: "q>=1" */
    }
    /* every record takes at least 5 bytes, and U32(root) follows */
    if (remaining(&r) < 4u || q > (remaining(&r) - 4u) / 5u) {
        return TRUNCATED(reason);
    }
    canon_dag_reset(out, n);
    canon_status st = CANON_COMPLETE;
    uint64_t *bits = canon_alloc_array(canon_cdag_bits_words(n), sizeof *bits, &st);
    if (bits == NULL) {
        return fail(reason, CANON_CDAG_SIZE, st);
    }
    st = decode_records(&r, n, q, bits, out, reason);
    free(bits);
    if (st != CANON_COMPLETE) {
        canon_dag_reset(out, n);
        return st;
    }
    if (!take_u32(&r, &root)) {
        return TRUNCATED(reason);
    }
    if (root >= q) {
        return fail(reason, CANON_CDAG_ROOT_RANGE, CANON_INVALID_INPUT);
    }
    /* spec 4.1: "Counts determine all variable boundaries; no padding or trailing bytes are
     * permitted." */
    if (r.pos != r.len) {
        return fail(reason, CANON_CDAG_TRAILING, CANON_INVALID_INPUT);
    }
    out->root = root;
    return CANON_COMPLETE;
}

/* Status of the normaliser as a reason (it reports no reason of its own). */
static canon_cdag_reason normalise_reason(canon_status st)
{
    switch (st) {
    case CANON_UNSUPPORTED_ACTION:
        return CANON_CDAG_NESTED_GRAPH; /* its only unsupported case */
    case CANON_CAPACITY_LIMIT:
    case CANON_RESOURCE_LIMIT:
        return CANON_CDAG_SIZE;
    default:
        return CANON_CDAG_NOT_CANONICAL;
    }
}

canon_status canon_cdag_import(const uint8_t *stream, size_t length, uint32_t degree,
                               const canon_dag_limits *limits, canon_dag *out,
                               canon_cdag_reason *reason)
{
    canon_dag raw;
    canon_dag_init(&raw, degree);
    canon_dag_scratch s;
    canon_dag_scratch_init(&s);
    canon_status st = canon_cdag_decode(stream, length, &degree, limits->max_n, &raw, reason);
    if (st == CANON_COMPLETE) {
        /* spec 4.2: sharing, order and unreachable records have no meaning */
        st = canon_dag_normalise(&raw, out, &s, false);
        if (st != CANON_COMPLETE) {
            *reason = normalise_reason(st);
        }
    }
    if (st == CANON_COMPLETE) {
        /* spec 11.1: "Validation is deterministic over the normalised input." */
        st = canon_dag_check_limits(out, limits);
        if (st != CANON_COMPLETE) {
            *reason = CANON_CDAG_SIZE;
        }
    }
    if (st == CANON_COMPLETE) {
        st = canon_dag_image_bound(out, &s); /* spec 11.1 output bound of the images */
        if (st != CANON_COMPLETE) {
            *reason = CANON_CDAG_SIZE;
        }
    }
    canon_dag_scratch_free(&s);
    canon_dag_free(&raw);
    return st;
}

canon_status canon_cdag_validate(const uint8_t *stream, size_t length,
                                 const canon_dag_limits *limits, canon_cdag_reason *reason)
{
    canon_dag raw, norm;
    canon_dag_init(&raw, 0);
    canon_dag_init(&norm, 0);
    canon_dag_scratch s;
    canon_dag_scratch_init(&s);
    canon_buf again;
    canon_buf_init(&again);
    canon_status st = canon_cdag_decode(stream, length, NULL, limits->max_n, &raw, reason);
    if (st == CANON_COMPLETE) {
        st = canon_dag_normalise(&raw, &norm, &s, false);
        if (st != CANON_COMPLETE) {
            *reason = normalise_reason(st);
        }
    }
    if (st == CANON_COMPLETE) {
        st = canon_dag_check_limits(&norm, limits);
        if (st != CANON_COMPLETE) {
            *reason = CANON_CDAG_SIZE;
        }
    }
    if (st == CANON_COMPLETE) {
        /* spec 4.1: "all records are reachable from root" in a canonical stream */
        bool all = raw.root == raw.count - 1u;
        for (uint32_t i = 0; all && i < raw.count; ++i) {
            all = s.reach[i] != 0;
        }
        if (!all) {
            st = fail(reason, CANON_CDAG_UNREACHABLE, CANON_INVALID_INPUT);
        }
    }
    if (st == CANON_COMPLETE) {
        /* spec 4.2: "reconstructing this normal form and requiring byte identity" */
        st = canon_dag_stream_write(&again, &norm);
        if (st == CANON_COMPLETE &&
            canon_bytes_compare(again.data, again.len, stream, length) != 0) {
            st = fail(reason, CANON_CDAG_NOT_CANONICAL, CANON_INVALID_INPUT);
        }
    }
    canon_buf_free(&again);
    canon_dag_scratch_free(&s);
    canon_dag_free(&norm);
    canon_dag_free(&raw);
    return st;
}
