/* Unit tests for src/encoding/wire.c and subset_stream.c (spec sections 4.1, 4.2, 4.3, 7.4). */
#include <stdint.h>
#include <string.h>

#include "check.h"
#include "encoding/subset_stream.h"
#include "encoding/wire.h"

static int hex_equal(const canon_buf *buf, const char *hex)
{
    size_t n = strlen(hex);
    if (n != 2 * buf->len) {
        return 0;
    }
    static const char digits[] = "0123456789abcdef";
    for (size_t i = 0; i < buf->len; ++i) {
        if (hex[2 * i] != digits[buf->data[i] >> 4] || hex[2 * i + 1] != digits[buf->data[i] & 15]) {
            return 0;
        }
    }
    return 1;
}

int main(void)
{
    canon_buf b;
    canon_buf_init(&b);
    /* spec 4.1: U16/U32 big endian, exact width. */
    CHECK(canon_buf_put_u16(&b, 0x0102) == CANON_COMPLETE);
    CHECK(canon_buf_put_u32(&b, 0x0a0b0c0d) == CANON_COMPLETE);
    CHECK(canon_buf_put_u8(&b, 0xff) == CANON_COMPLETE);
    CHECK(hex_equal(&b, "01020a0b0c0dff"));
    canon_buf_truncate(&b, 0);
    /* spec 4.1: B(s) = U32(len) || s; B0 = 02 00000000 in the 7.4 abbreviations is tag + B(""). */
    const uint8_t s[3] = {0x61, 0x00, 0x62};
    CHECK(canon_buf_put_b(&b, s, 3) == CANON_COMPLETE);
    CHECK(canon_buf_put_b(&b, NULL, 0) == CANON_COMPLETE);
    CHECK(hex_equal(&b, "0000000361006200000000"));
    /* spec 11.1: size overflow is refused before allocation; contents unchanged. */
    size_t before = b.len;
    CHECK(canon_buf_reserve(&b, SIZE_MAX) == CANON_CAPACITY_LIMIT);
    CHECK(b.len == before);
    canon_buf_free(&b);
    CHECK(b.data == NULL && b.len == 0 && b.cap == 0);
    canon_buf_free(&b); /* idempotent */

    /* spec 4.3 / 7.2: unsigned lexicographic, proper prefix smaller; 00 < 10 < 20 < 21. */
    const uint8_t x0[1] = {0x00}, x10[1] = {0x10}, x20[1] = {0x20}, x21[1] = {0x21};
    const uint8_t pre[2] = {0x10, 0x00}, hi[1] = {0xff};
    CHECK(canon_bytes_compare(x0, 1, x10, 1) < 0);
    CHECK(canon_bytes_compare(x10, 1, x20, 1) < 0);
    CHECK(canon_bytes_compare(x20, 1, x21, 1) < 0);
    CHECK(canon_bytes_compare(x10, 1, pre, 2) < 0);
    CHECK(canon_bytes_compare(pre, 2, x10, 1) > 0);
    CHECK(canon_bytes_compare(hi, 1, x10, 1) > 0); /* unsigned, not signed char */
    CHECK(canon_bytes_compare(NULL, 0, NULL, 0) == 0);
    CHECK(canon_bytes_compare(NULL, 0, x0, 1) < 0);

    /* spec 7.4 subset streams (expected hex lives in tests only). */
    canon_buf_init(&b);
    CHECK(canon_subset_stream_write(&b, 0, NULL, 0) == CANON_COMPLETE);
    CHECK(hex_equal(&b, "434e02000100010000000000000001040000000000000000"));
    canon_buf_truncate(&b, 0);
    const uint32_t one[1] = {1}, zero[1] = {0};
    CHECK(canon_subset_stream_write(&b, 2, one, 1) == CANON_COMPLETE);
    CHECK(hex_equal(&b, "434e02000100010000000200000002010000000104000000010000000000000001"));
    canon_buf_truncate(&b, 0);
    CHECK(canon_subset_stream_write(&b, 2, zero, 1) == CANON_COMPLETE);
    CHECK(hex_equal(&b, "434e02000100010000000200000002010000000004000000010000000000000001"));
    canon_buf_truncate(&b, 0);
    CHECK(canon_subset_stream_write(&b, 2, NULL, 0) == CANON_COMPLETE);
    CHECK(hex_equal(&b, "434e02000100010000000200000001040000000000000000"));
    canon_buf_truncate(&b, 0);
    /* A two-atom set on n=3 derived by hand from spec 4.1/4.2: atoms 0 and 2 as records 0, 1;
     * set record 2 = S(0,1); root 2. */
    const uint32_t two[2] = {0, 2};
    CHECK(canon_subset_stream_write(&b, 3, two, 2) == CANON_COMPLETE);
    CHECK(hex_equal(&b, "434e0200010001000000030000000301000000000100000002"
                        "04000000020000000000000001"
                        "00000002"));
    /* Exact size formula agrees with the writer. */
    for (uint32_t k = 0; k <= 2; ++k) {
        uint64_t size = 0;
        CHECK(canon_subset_stream_size(k, &size) == CANON_COMPLETE);
        CHECK(size == 24u + 9u * (uint64_t)k);
    }
    CHECK(b.len == 24u + 18u);
    uint64_t size = 0;
    CHECK(canon_subset_stream_size(UINT32_MAX, &size) == CANON_CAPACITY_LIMIT);
    canon_buf_free(&b);
    return check_finish("test_wire");
}
