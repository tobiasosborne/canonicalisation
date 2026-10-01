/* The CDAG-2 stream of a top-level subset (spec sections 4.1, 4.2).
 *
 * spec 4.2: a subset is a set node whose children are atom nodes.  Atom records have no child
 * references, so height 0; the set has height 1 and is the root, the last record.  Within
 * height 0 the records `01 U32(a)` are sorted by their exact bytes, which for atoms is
 * increasing numeric order of a, so atom a_i receives index i.  The set record lists its
 * children's indices in increasing order: 0..k-1.  Equal atoms are one node (sets deduplicate
 * equal children), which the caller guarantees by passing strictly increasing atoms.
 *
 * This is a specialisation, not a general DAG normaliser: nested objects and the general
 * section 4.2 numbering are slice S5 work. */
#include "encoding/subset_stream.h"

/* Record tags of the spec 4.1 table. */
enum { TAG_ATOM = 0x01, TAG_SET = 0x04 };

/* spec 4.1 stream: 3 magic bytes, U16 schema, U16 action, U32 n, U32 q, records, U32 root.
 * Atom record: 1 + 4 bytes; set record: 1 + 4 + 4k bytes. */
canon_status canon_subset_stream_size(uint32_t k, uint64_t *size_out)
{
    if (k == UINT32_MAX) {
        return CANON_CAPACITY_LIMIT; /* spec 4.1: q = k + 1 must fit U32 */
    }
    /* 24 + 9k <= 24 + 9 * (2^32 - 2): fits uint64 with room to spare. */
    *size_out = 3u + 2u + 2u + 4u + 4u + 5u * (uint64_t)k + 5u + 4u * (uint64_t)k + 4u;
    return CANON_COMPLETE;
}

canon_status canon_subset_stream_write(canon_buf *out, uint32_t n, const uint32_t *atoms,
                                       uint32_t k)
{
    uint64_t size = 0;
    canon_status st = canon_subset_stream_size(k, &size);
    if (st != CANON_COMPLETE) {
        return st;
    }
    if (size > SIZE_MAX) {
        return CANON_CAPACITY_LIMIT; /* spec 11.1: uint64 offsets checked against SIZE_MAX */
    }
    size_t old = out->len;
    st = canon_buf_reserve(out, (size_t)size);
    if (st != CANON_COMPLETE) {
        return st;
    }
    /* With room reserved the writers below cannot fail. */
    static const uint8_t magic[3] = {0x43, 0x4e, 0x02}; /* spec 4.1 stream prefix "CN" 2 */
    (void)canon_buf_put_bytes(out, magic, sizeof magic);
    (void)canon_buf_put_u16(out, CANON_SCHEMA_EXT_DAG_1);        /* spec 4.1: U16(schema=1) */
    (void)canon_buf_put_u16(out, CANON_ACTION_ATOM_TRANSPORT_1); /* spec 4.1: U16(action=1) */
    (void)canon_buf_put_u32(out, n);                             /* spec 4.1: U32(n) */
    (void)canon_buf_put_u32(out, k + 1u);                        /* spec 4.1: U32(q), q >= 1 */
    for (uint32_t i = 0; i < k; ++i) {
        /* spec 4.2: height-0 atom records in increasing record-byte (= atom) order */
        (void)canon_buf_put_u8(out, TAG_ATOM);
        (void)canon_buf_put_u32(out, atoms[i]); /* spec 4.1: U32(a), a < n */
    }
    /* spec 4.1: set record, k increasing distinct child references, each < parent index */
    (void)canon_buf_put_u8(out, TAG_SET);
    (void)canon_buf_put_u32(out, k);
    for (uint32_t i = 0; i < k; ++i) {
        (void)canon_buf_put_u32(out, i);
    }
    (void)canon_buf_put_u32(out, k); /* spec 4.2: the root is the last record, index q - 1 */
    if ((uint64_t)(out->len - old) != size) {
        canon_buf_truncate(out, old);
        return CANON_INTERNAL_ERROR;
    }
    return CANON_COMPLETE;
}
