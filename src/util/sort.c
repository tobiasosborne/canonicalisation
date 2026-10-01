/* Stable bottom-up merge sort (see sort.h). */
#include "util/sort.h"

#include <string.h>

void canon_stable_sort(void *base, size_t count, size_t size, void *tmp, canon_sort_cmp cmp,
                       void *ctx)
{
    if (count < 2 || size == 0) {
        return;
    }
    unsigned char *src = base, *dst = tmp;
    /* Offsets are i * size with i <= count, and count * size fits size_t (caller's
     * allocation), so no product below can wrap.  Widths double while below count; the
     * comparison `width > count / 2` stops the doubling before it could wrap. */
    for (size_t width = 1; width < count; width = width > count / 2 ? count : width * 2) {
        for (size_t lo = 0; lo < count;) {
            size_t mid = lo + (width < count - lo ? width : count - lo);
            size_t hi = mid + (width < count - mid ? width : count - mid);
            size_t i = lo, j = mid, o = lo;
            while (i < mid && j < hi) {
                /* Stability: take from the right run only when strictly smaller. */
                if (cmp(src + j * size, src + i * size, ctx) < 0) {
                    memcpy(dst + o * size, src + j * size, size);
                    ++j;
                } else {
                    memcpy(dst + o * size, src + i * size, size);
                    ++i;
                }
                ++o;
            }
            if (i < mid) {
                memcpy(dst + o * size, src + i * size, (mid - i) * size);
                o += mid - i;
            }
            if (j < hi) {
                memcpy(dst + o * size, src + j * size, (hi - j) * size);
            }
            lo = hi;
        }
        unsigned char *swap = src;
        src = dst;
        dst = swap;
    }
    if (src != (unsigned char *)base) {
        memcpy(base, src, count * size);
    }
}
