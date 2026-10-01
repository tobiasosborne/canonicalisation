/* The CDAG-2 stream of a top-level coloured directed multigraph (spec sections 4.1, 4.2). */
#include "encoding/graph_stream.h"

#include "arena/checked.h"

/* Record tag of the spec 4.1 table: coloured directed multigraph. */
enum { TAG_GRAPH = 0x09 };

canon_status canon_graph_stream_measure(const canon_graph *g, uint64_t *size_out)
{
    /* spec 4.1: magic 3, U16 schema, U16 action, U32 n, U32 q; tag; U32(e); U32 root. */
    uint64_t size = 3u + 2u + 2u + 4u + 4u + 1u + 4u + 4u;
    for (uint32_t v = 0; v < g->n; ++v) {
        size_t len = 0;
        (void)canon_byte_table_get(&g->colours, g->colour_id[v], &len);
        /* B(colour[v]) */
        if (!canon_u64_add(size, 4u, &size) || !canon_u64_add(size, (uint64_t)len, &size)) {
            return CANON_CAPACITY_LIMIT;
        }
    }
    for (uint32_t i = 0; i < g->e; ++i) {
        size_t len = 0;
        (void)canon_byte_table_get(&g->labels, g->arcs[i].label, &len);
        /* U32 source, U32 target, B(label), Nat(multiplicity) */
        if (!canon_u64_add(size, 12u, &size) || !canon_u64_add(size, (uint64_t)len, &size) ||
            !canon_u64_add(size, canon_nat_length(g->arcs[i].multiplicity), &size)) {
            return CANON_CAPACITY_LIMIT;
        }
    }
    *size_out = size;
    return CANON_COMPLETE;
}

canon_status canon_graph_stream_size(const canon_graph *g, uint64_t *size_out)
{
    *size_out = g->stream_size; /* measured at import; images borrow it */
    return CANON_COMPLETE;
}

canon_status canon_graph_stream_write(canon_buf *out, const canon_graph *g)
{
    const uint64_t size = g->stream_size; /* spec 11.1: exact, cached at import */
    canon_status st = CANON_COMPLETE;
    if (size > SIZE_MAX) {
        return CANON_CAPACITY_LIMIT; /* spec 11.1: uint64 offsets checked against SIZE_MAX */
    }
    size_t old = out->len;
    st = canon_buf_reserve(out, (size_t)size);
    if (st != CANON_COMPLETE) {
        return st;
    }
    /* With room reserved the writers below cannot fail (every length fits U32, checked at
     * import). */
    static const uint8_t magic[3] = {0x43, 0x4e, 0x02}; /* spec 4.1 stream prefix */
    (void)canon_buf_put_bytes(out, magic, sizeof magic);
    (void)canon_buf_put_u16(out, CANON_SCHEMA_EXT_DAG_1);        /* spec 4.1: U16(schema=1) */
    (void)canon_buf_put_u16(out, CANON_ACTION_ATOM_TRANSPORT_1); /* spec 4.1: U16(action=1) */
    (void)canon_buf_put_u32(out, g->n);                          /* spec 4.1: U32(n) */
    /* spec 4.2: one node of height 0 (the graph record has no child references), so q = 1 */
    (void)canon_buf_put_u32(out, 1u);
    /* spec 4.1 tag 09: "n values B(vertex_colour), U32(e), e arc records" */
    (void)canon_buf_put_u8(out, TAG_GRAPH);
    for (uint32_t v = 0; v < g->n; ++v) {
        size_t len = 0;
        const uint8_t *c = canon_byte_table_get(&g->colours, g->colour_id[v], &len);
        (void)canon_buf_put_b(out, c, len);
    }
    (void)canon_buf_put_u32(out, g->e);
    for (uint32_t i = 0; i < g->e; ++i) {
        /* spec 4.1: "An arc record is U32(source),U32(target),B(label),Nat(multiplicity)", in
         * the normalised order (source, target, B(label)) of the graph. */
        const canon_graph_arc *a = &g->arcs[i];
        size_t len = 0;
        const uint8_t *label = canon_byte_table_get(&g->labels, a->label, &len);
        (void)canon_buf_put_u32(out, a->source);
        (void)canon_buf_put_u32(out, a->target);
        (void)canon_buf_put_b(out, label, len);
        (void)canon_buf_put_nat(out, a->multiplicity);
    }
    (void)canon_buf_put_u32(out, 0u); /* spec 4.2: the root is the last record, index q - 1 = 0 */
    if ((uint64_t)(out->len - old) != size) {
        canon_buf_truncate(out, old);
        return CANON_INTERNAL_ERROR;
    }
    return CANON_COMPLETE;
}
