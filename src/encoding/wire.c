/* CDAG-2 wire primitives (spec section 4.1) and the byte order of spec section 4.3. */
#include "encoding/wire.h"

#include <stdlib.h>
#include <string.h>

void canon_buf_init(canon_buf *buf)
{
    buf->data = NULL;
    buf->len = 0;
    buf->cap = 0;
}

void canon_buf_free(canon_buf *buf)
{
    free(buf->data);
    canon_buf_init(buf);
}

/* spec section 11.1: sizes are checked before allocation; no wraparound has meaning. */
canon_status canon_buf_reserve(canon_buf *buf, size_t extra)
{
    if (extra > SIZE_MAX - buf->len) {
        return CANON_CAPACITY_LIMIT;
    }
    size_t need = buf->len + extra;
    if (need <= buf->cap) {
        return CANON_COMPLETE;
    }
    size_t cap = buf->cap < 64 ? 64 : buf->cap;
    while (cap < need) {
        if (cap > SIZE_MAX / 2) {
            cap = need;
            break;
        }
        cap *= 2;
    }
    uint8_t *grown = realloc(buf->data, cap);
    if (grown == NULL) {
        return CANON_RESOURCE_LIMIT; /* spec section 17: old state is kept */
    }
    buf->data = grown;
    buf->cap = cap;
    return CANON_COMPLETE;
}

void canon_buf_truncate(canon_buf *buf, size_t len)
{
    if (len < buf->len) {
        buf->len = len;
    }
}

canon_status canon_buf_put_bytes(canon_buf *buf, const uint8_t *bytes, size_t length)
{
    if (length == 0) {
        return CANON_COMPLETE;
    }
    canon_status st = canon_buf_reserve(buf, length);
    if (st != CANON_COMPLETE) {
        return st;
    }
    memcpy(buf->data + buf->len, bytes, length);
    buf->len += length;
    return CANON_COMPLETE;
}

canon_status canon_buf_put_u8(canon_buf *buf, uint8_t value)
{
    return canon_buf_put_bytes(buf, &value, 1);
}

/* spec section 4.1: U16 exactly 2 bytes, big endian. */
canon_status canon_buf_put_u16(canon_buf *buf, uint16_t value)
{
    uint8_t b[2] = {(uint8_t)(value >> 8), (uint8_t)value};
    return canon_buf_put_bytes(buf, b, sizeof b);
}

/* spec section 4.1: U32 exactly 4 bytes, big endian. */
canon_status canon_buf_put_u32(canon_buf *buf, uint32_t value)
{
    uint8_t b[4] = {(uint8_t)(value >> 24), (uint8_t)(value >> 16), (uint8_t)(value >> 8),
                    (uint8_t)value};
    return canon_buf_put_bytes(buf, b, sizeof b);
}

/* spec section 4.1: B(s) = U32(len(s)) || s; "Lengths and counts must fit U32; overflow is a
 * capacity error." */
canon_status canon_buf_put_b(canon_buf *buf, const uint8_t *bytes, size_t length)
{
    if ((uint64_t)length > UINT32_MAX) {
        return CANON_CAPACITY_LIMIT;
    }
    if (length > SIZE_MAX - 4) {
        return CANON_CAPACITY_LIMIT;
    }
    size_t old = buf->len;
    canon_status st = canon_buf_reserve(buf, 4 + length); /* all or nothing */
    if (st != CANON_COMPLETE) {
        return st;
    }
    st = canon_buf_put_u32(buf, (uint32_t)length);
    if (st == CANON_COMPLETE) {
        st = canon_buf_put_bytes(buf, bytes, length);
    }
    if (st != CANON_COMPLETE) {
        canon_buf_truncate(buf, old);
    }
    return st;
}

/* spec section 4.1: the shortest b with k < 256^b; b = 0 exactly for k = 0. */
static uint32_t nat_width(uint64_t k)
{
    uint32_t b = 0;
    while (k != 0) {
        ++b;
        k >>= 8;
    }
    return b;
}

uint32_t canon_nat_length(uint64_t k)
{
    return 4u + nat_width(k);
}

/* spec section 4.1: "Nat(k)=U32(b) || big_endian_bytes(k,b) uses the shortest b, with b=0 for
 * k=0 and no leading zero otherwise." */
canon_status canon_buf_put_nat(canon_buf *buf, uint64_t k)
{
    uint8_t bytes[12];
    uint32_t b = nat_width(k);
    bytes[0] = (uint8_t)(b >> 24);
    bytes[1] = (uint8_t)(b >> 16);
    bytes[2] = (uint8_t)(b >> 8);
    bytes[3] = (uint8_t)b;
    for (uint32_t i = 0; i < b; ++i) {
        /* big endian: the most significant of the b bytes first, which is nonzero */
        bytes[4 + i] = (uint8_t)(k >> (8u * (b - 1u - i)));
    }
    return canon_buf_put_bytes(buf, bytes, 4u + (size_t)b); /* all or nothing */
}

/* spec section 4.3: unsigned byte lexicographic order, proper prefix smaller. */
int canon_bytes_compare(const uint8_t *a, size_t a_len, const uint8_t *b, size_t b_len)
{
    size_t common = a_len < b_len ? a_len : b_len;
    for (size_t i = 0; i < common; ++i) {
        if (a[i] != b[i]) {
            return a[i] < b[i] ? -1 : 1;
        }
    }
    if (a_len == b_len) {
        return 0;
    }
    return a_len < b_len ? -1 : 1;
}
