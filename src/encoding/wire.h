/*
 * Internal header: CDAG-2 wire primitives (spec section 4.1) and the unsigned byte order of
 * spec section 4.3.  Implemented in slice S1 (docs/slices/S1.md section 4.4).
 *
 * A canon_buf is a growable byte buffer.  Writers append exact big-endian fields and never dump
 * structs (spec section 17).  Every size computation is overflow-checked before allocation
 * (spec section 11.1): an arithmetic overflow of a size is CANON_CAPACITY_LIMIT, an allocation
 * failure is CANON_RESOURCE_LIMIT, and on any failure the buffer keeps its previous contents.
 */
#ifndef CANON_SRC_ENCODING_WIRE_H
#define CANON_SRC_ENCODING_WIRE_H

#include <stddef.h>
#include <stdint.h>

#include "canon/canon.h"

typedef struct canon_buf {
    uint8_t *data; /* NULL until the first growth */
    size_t len;    /* bytes written */
    size_t cap;    /* bytes allocated */
} canon_buf;

/* Initialise an empty buffer (no allocation). */
void canon_buf_init(canon_buf *buf);
/* Free the storage and reset to empty.  Valid on an initialised or zeroed buffer. */
void canon_buf_free(canon_buf *buf);
/* Ensure room for `extra` more bytes. */
canon_status canon_buf_reserve(canon_buf *buf, size_t extra);
/* Drop bytes beyond `len` (len <= buf->len); keeps the allocation. */
void canon_buf_truncate(canon_buf *buf, size_t len);

/* Append raw bytes. */
canon_status canon_buf_put_bytes(canon_buf *buf, const uint8_t *bytes, size_t length);
/* Append one byte (record and token tags). */
canon_status canon_buf_put_u8(canon_buf *buf, uint8_t value);
/* spec section 4.1: U16 is exactly 2 bytes, big endian. */
canon_status canon_buf_put_u16(canon_buf *buf, uint16_t value);
/* spec section 4.1: U32 is exactly 4 bytes, big endian. */
canon_status canon_buf_put_u32(canon_buf *buf, uint32_t value);
/* spec section 4.1: B(s) = U32(len(s)) || s; a length that does not fit U32 is a capacity error. */
canon_status canon_buf_put_b(canon_buf *buf, const uint8_t *bytes, size_t length);

/* spec section 4.3 (CDAG-BYTE-1) and 7.2 (trace order): unsigned byte lexicographic order with a
 * proper prefix smaller.  Returns -1, 0 or +1. */
int canon_bytes_compare(const uint8_t *a, size_t a_len, const uint8_t *b, size_t b_len);

#endif /* CANON_SRC_ENCODING_WIRE_H */
