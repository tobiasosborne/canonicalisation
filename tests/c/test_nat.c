/* Unit tests for the spec 4.1 Nat writer (src/encoding/wire.c, slice S2):
 * Nat(k) = U32(b) || big_endian_bytes(k, b), shortest b, b = 0 for k = 0, no leading zero.
 * Expectations are written by hand from that sentence. */
#include <stdint.h>

#include "check.h"
#include "encoding/wire.h"

static void nat_is(uint64_t k, const char *want, uint32_t length)
{
    canon_buf b;
    canon_buf_init(&b);
    CHECK(canon_buf_put_nat(&b, k) == CANON_COMPLETE);
    CHECK(check_hex_is(b.data, b.len, want));
    CHECK(b.len == length && canon_nat_length(k) == length);
    canon_buf_free(&b);
}

int main(void)
{
    nat_is(0, "00000000", 4);                    /* b = 0 for k = 0 */
    nat_is(1, "00000001 01", 5);
    nat_is(255, "00000001 ff", 5);
    nat_is(256, "00000002 0100", 6);             /* no leading zero: 0100, not 000100 */
    nat_is(65535, "00000002 ffff", 6);
    nat_is(65536, "00000003 010000", 7);
    nat_is(UINT32_MAX, "00000004 ffffffff", 8);
    nat_is((uint64_t)1 << 32, "00000005 0100000000", 9);
    nat_is(UINT64_MAX, "00000008 ffffffffffffffff", 12);
    nat_is((uint64_t)1 << 63, "00000008 8000000000000000", 12);
    nat_is(300, "00000002 012c", 6);
    /* Appends after existing bytes, all or nothing. */
    canon_buf b;
    canon_buf_init(&b);
    CHECK(canon_buf_put_u8(&b, 0x09) == CANON_COMPLETE);
    CHECK(canon_buf_put_nat(&b, 2) == CANON_COMPLETE);
    CHECK(check_hex_is(b.data, b.len, "09 00000001 02"));
    canon_buf_free(&b);
    return check_finish("test_nat");
}
