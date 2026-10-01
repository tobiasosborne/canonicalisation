/* Unit test for the shared stable merge sort (src/util/sort.c). */
#include <stdint.h>
#include <string.h>

#include "check.h"
#include "util/sort.h"

typedef struct {
    uint32_t key, seq;
} item;

static int by_key(const void *a, const void *b, void *ctx)
{
    (void)ctx;
    uint32_t x = ((const item *)a)->key, y = ((const item *)b)->key;
    return x < y ? -1 : (x > y ? 1 : 0);
}

int main(void)
{
    item a[300], tmp[300];
    canon_stable_sort(NULL, 0, sizeof *a, NULL, by_key, NULL); /* empty: no access */
    for (size_t n = 1; n <= 300; n += (n < 20 ? 1 : 37)) {
        for (int round = 0; round < 5; ++round) {
            uint32_t range = round == 0 ? 1u : (uint32_t)(1 + check_rng() % 10);
            uint32_t hist[11] = {0};
            for (size_t i = 0; i < n; ++i) {
                a[i].key = (uint32_t)(check_rng() % range);
                a[i].seq = (uint32_t)i;
                hist[a[i].key] += 1;
            }
            canon_stable_sort(a, n, sizeof *a, tmp, by_key, NULL);
            uint32_t after[11] = {0};
            for (size_t i = 0; i < n; ++i) {
                after[a[i].key] += 1;
                if (i > 0) {
                    CHECK(a[i - 1].key <= a[i].key);
                    if (a[i - 1].key == a[i].key) {
                        CHECK(a[i - 1].seq < a[i].seq); /* stable */
                    }
                }
            }
            CHECK(memcmp(hist, after, sizeof hist) == 0); /* a permutation of the input */
        }
    }
    return check_finish("test_sort");
}
