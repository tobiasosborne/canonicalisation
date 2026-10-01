/* Minimal shared assertion helper for the C tests (no framework, exit status 0 on success). */
#ifndef CANON_TESTS_CHECK_H
#define CANON_TESTS_CHECK_H

#include <stdio.h>

static int check_failures;

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond);               \
            ++check_failures;                                                                      \
        }                                                                                          \
    } while (0)

/* Deterministic xorshift64 generator for randomised but reproducible tests. */
static unsigned long long check_rng_state = 0x9e3779b97f4a7c15ULL;
static inline unsigned long long check_rng(void)
{
    unsigned long long x = check_rng_state;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    check_rng_state = x;
    return x;
}

static inline int check_finish(const char *name)
{
    if (check_failures != 0) {
        fprintf(stderr, "%s: %d failure(s)\n", name, check_failures);
        return 1;
    }
    printf("%s: ok\n", name);
    return 0;
}

#endif /* CANON_TESTS_CHECK_H */
