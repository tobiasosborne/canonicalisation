/* The subset object (spec sections 2.1, 4.2, 7.1). */
#include "object/subset.h"

#include <stdlib.h>
#include <string.h>

#include "arena/checked.h"

size_t canon_bitset_words(uint32_t n)
{
    return ((size_t)n + 63u) / 64u; /* no overflow: n <= 2^32 - 1 */
}

/* Write the members of an n-bit bitset in increasing order; returns the count. */
static uint32_t bitset_members(const uint64_t *bits, uint32_t n, uint32_t *out)
{
    uint32_t k = 0;
    size_t words = canon_bitset_words(n);
    for (size_t w = 0; w < words; ++w) {
        uint64_t word = bits[w];
        while (word != 0) {
            uint32_t bit = 0;
            while (((word >> bit) & 1u) == 0) {
                ++bit;
            }
            if (out != NULL) {
                out[k] = (uint32_t)(w * 64u) + bit;
            }
            ++k;
            word &= word - 1u; /* clear the lowest set bit */
        }
    }
    return k;
}

canon_status canon_subset_init(canon_subset *s, uint32_t n, const uint32_t *atoms, size_t count)
{
    s->n = n;
    s->k = 0;
    s->atoms = NULL;
    s->bits = NULL;
    if (count > 0 && atoms == NULL) {
        return CANON_INVALID_INPUT;
    }
    for (size_t i = 0; i < count; ++i) {
        if (atoms[i] >= n) {
            return CANON_INVALID_INPUT; /* spec 4.1: out-of-domain atom IDs are invalid */
        }
    }
    if (n == 0) {
        return CANON_COMPLETE; /* spec 7.1: empty sets qualify as subsets; n = 0 allowed */
    }
    size_t words = canon_bitset_words(n);
    s->bits = calloc(words, sizeof *s->bits);
    if (s->bits == NULL) {
        return CANON_RESOURCE_LIMIT;
    }
    for (size_t i = 0; i < count; ++i) {
        s->bits[atoms[i] / 64u] |= (uint64_t)1 << (atoms[i] % 64u); /* spec 4.2: dedupe */
    }
    uint32_t k = bitset_members(s->bits, n, NULL);
    if (k > 0) {
        /* spec 11.1: size product checked before allocation */
        size_t bytes = 0;
        if (canon_size_mul((size_t)k, sizeof *s->atoms, &bytes)) {
            s->atoms = malloc(bytes);
        }
        if (s->atoms == NULL) {
            free(s->bits);
            s->atoms = NULL;
            s->bits = NULL;
            return CANON_RESOURCE_LIMIT;
        }
        (void)bitset_members(s->bits, n, s->atoms);
    }
    s->k = k;
    return CANON_COMPLETE;
}

void canon_subset_free(canon_subset *s)
{
    free(s->atoms);
    free(s->bits);
    s->atoms = NULL;
    s->bits = NULL;
    s->k = 0;
}

bool canon_subset_contains(const canon_subset *s, uint32_t a)
{
    return a < s->n && ((s->bits[a / 64u] >> (a % 64u)) & 1u) != 0;
}

/* spec 7.1: "On the top-level subset ..., the initial key of a is membership 0/1". */
uint32_t canon_subset_initial_key(const canon_subset *s, uint32_t a)
{
    return canon_subset_contains(s, a) ? 1u : 0u;
}

/* spec 2.1: ATOM-TRANSPORT-1 maps atom a to g[a]; a set maps elementwise. */
void canon_subset_act_sorted(const canon_subset *s, const uint32_t *g, uint64_t *scratch,
                             uint32_t *out_atoms)
{
    size_t words = canon_bitset_words(s->n);
    if (words > 0) {
        memset(scratch, 0, words * sizeof *scratch);
    }
    for (uint32_t i = 0; i < s->k; ++i) {
        uint32_t b = g[s->atoms[i]];
        scratch[b / 64u] |= (uint64_t)1 << (b % 64u);
    }
    (void)bitset_members(scratch, s->n, out_atoms);
}

bool canon_subset_equal(const canon_subset *a, const canon_subset *b)
{
    if (a->n != b->n || a->k != b->k) {
        return false;
    }
    return a->k == 0 || memcmp(a->atoms, b->atoms, (size_t)a->k * sizeof *a->atoms) == 0;
}
