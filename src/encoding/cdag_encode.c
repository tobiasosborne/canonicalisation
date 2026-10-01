/* The general CDAG-2 encoder of a record arena (spec 4.1, 4.2; slice S5). */
#include "encoding/cdag_encode.h"

#include "arena/checked.h"

/* spec 4.1 stream framing: magic 3, U16 schema, U16 action, U32 n, U32 q; then U32 root. */
#define HEADER_BYTES 15u
#define ROOT_BYTES 4u

canon_status canon_dag_record_size(const canon_dag *d, uint32_t i, uint64_t *size_out)
{
    const canon_rec *r = &d->recs[i];
    uint64_t size = 1u; /* tag */
    switch (r->tag) {
    case CANON_REC_TUPLE:
    case CANON_REC_SET:
        /* spec 4.1: U32(k), k child references */
        size += 4u + 4u * (uint64_t)r->child_count; /* < 2^35: no overflow */
        break;
    case CANON_REC_MULTISET:
        /* spec 4.1: U32(k), k pairs (child reference, Nat(m)) */
        size += 4u;
        for (uint32_t j = 0; j < r->child_count; ++j) {
            const uint64_t m = d->mult[r->child_off + j];
            if (!canon_u64_add(size, 4u + (uint64_t)canon_nat_length(m), &size)) {
                return CANON_CAPACITY_LIMIT;
            }
        }
        break;
    default:
        /* leaves: the stored wire payload */
        if (!canon_u64_add(size, (uint64_t)r->payload_len, &size)) {
            return CANON_CAPACITY_LIMIT;
        }
        break;
    }
    *size_out = size;
    return CANON_COMPLETE;
}

canon_status canon_dag_stream_measure(const canon_dag *d, uint64_t *size_out)
{
    uint64_t size = HEADER_BYTES + ROOT_BYTES;
    for (uint32_t i = 0; i < d->count; ++i) {
        uint64_t rs = 0;
        canon_status st = canon_dag_record_size(d, i, &rs);
        if (st != CANON_COMPLETE) {
            return st;
        }
        if (!canon_u64_add(size, rs, &size)) {
            return CANON_CAPACITY_LIMIT; /* spec 11.1 */
        }
    }
    *size_out = size;
    return CANON_COMPLETE;
}

canon_status canon_dag_stream_write(canon_buf *out, const canon_dag *d)
{
    if (d->root >= d->count) {
        return CANON_INTERNAL_ERROR; /* spec 4.1: q >= 1 and root < q */
    }
    uint64_t size = 0;
    canon_status st = canon_dag_stream_measure(d, &size);
    if (st != CANON_COMPLETE) {
        return st;
    }
    if (size > SIZE_MAX) {
        return CANON_CAPACITY_LIMIT; /* spec 11.1: uint64 offsets checked against SIZE_MAX */
    }
    const size_t old = out->len;
    st = canon_buf_reserve(out, (size_t)size);
    if (st != CANON_COMPLETE) {
        return st;
    }
    /* With room reserved the writers below cannot fail. */
    static const uint8_t magic[3] = {0x43, 0x4e, 0x02}; /* spec 4.1 stream prefix */
    (void)canon_buf_put_bytes(out, magic, sizeof magic);
    (void)canon_buf_put_u16(out, CANON_SCHEMA_EXT_DAG_1);        /* spec 4.1: U16(schema=1) */
    (void)canon_buf_put_u16(out, CANON_ACTION_ATOM_TRANSPORT_1); /* spec 4.1: U16(action=1) */
    (void)canon_buf_put_u32(out, d->n);                          /* spec 4.1: U32(n) */
    (void)canon_buf_put_u32(out, d->count);                      /* spec 4.1: U32(q) */
    for (uint32_t i = 0; i < d->count; ++i) {
        const canon_rec *r = &d->recs[i];
        (void)canon_buf_put_u8(out, r->tag);
        if (canon_rec_has_children(r->tag)) {
            /* spec 4.1: "03 U32(k), k child references"; "04 U32(k), k increasing distinct
             * child references"; "05 U32(k), k pairs (child reference, Nat(m))" */
            (void)canon_buf_put_u32(out, r->child_count);
            for (uint32_t j = 0; j < r->child_count; ++j) {
                (void)canon_buf_put_u32(out, d->child[r->child_off + j]);
                if (r->tag == CANON_REC_MULTISET) {
                    (void)canon_buf_put_nat(out, d->mult[r->child_off + j]);
                }
            }
        } else {
            (void)canon_buf_put_bytes(out, canon_dag_payload(d, i), r->payload_len);
        }
    }
    (void)canon_buf_put_u32(out, d->root); /* spec 4.1: U32(root) */
    if ((uint64_t)(out->len - old) != size) {
        canon_buf_truncate(out, old);
        return CANON_INTERNAL_ERROR;
    }
    return CANON_COMPLETE;
}
