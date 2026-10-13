/* Shared replay driver of the fuzz targets (slice S8, docs/slices/S8.md 1.7, D18): a target
 * defines LLVMFuzzerTestOneInput; built with -fsanitize=fuzzer the libFuzzer runtime drives
 * it, otherwise CANON_FUZZ_MAIN provides a main that replays every file named on the command
 * line as is and under `iterations` deterministic mutations each (xorshift64: byte flips,
 * truncations, insertions, duplications of a span), so that the harness runs with no runtime
 * downloaded here.  The driver depends on the C standard library only. */
#ifndef CANON_FUZZ_DRIVER_H
#define CANON_FUZZ_DRIVER_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

#ifdef CANON_FUZZ_MAIN
static uint64_t fuzz_rng_state = 0x9e3779b97f4a7c15ULL;

static uint64_t fuzz_rng(void)
{
    uint64_t x = fuzz_rng_state;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    fuzz_rng_state = x;
    return x;
}

/* One deterministic mutation of `in` (len bytes) into `out` (capacity at least len + 32);
 * returns the mutated length. */
static size_t fuzz_mutate(const uint8_t *in, size_t len, uint8_t *out)
{
    memcpy(out, in, len);
    const uint64_t kind = fuzz_rng() % 5u;
    if (len == 0) {
        out[0] = (uint8_t)fuzz_rng();
        return 1;
    }
    const size_t at = (size_t)(fuzz_rng() % len);
    if (kind == 0) { /* flip bits */
        out[at] ^= (uint8_t)(1u << (fuzz_rng() % 8u));
        return len;
    }
    if (kind == 1) { /* set a byte */
        out[at] = (uint8_t)fuzz_rng();
        return len;
    }
    if (kind == 2) { /* truncate */
        return at;
    }
    if (kind == 3) { /* insert up to 8 random bytes */
        const size_t k = 1u + (size_t)(fuzz_rng() % 8u);
        memmove(out + at + k, in + at, len - at);
        for (size_t i = 0; i < k; ++i) {
            out[at + i] = (uint8_t)fuzz_rng();
        }
        return len + k;
    }
    /* duplicate a span of up to 32 bytes */
    const size_t k = 1u + (size_t)(fuzz_rng() % 32u);
    const size_t span = k < len - at ? k : len - at;
    memmove(out + at + span, in + at, len - at);
    memcpy(out + at, in + at, span);
    return len + span;
}

static uint8_t *fuzz_read_file(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        return NULL;
    }
    size_t cap = 1u << 16, used = 0;
    uint8_t *buf = malloc(cap);
    while (buf != NULL) {
        if (used == cap) {
            uint8_t *grown = realloc(buf, cap * 2u);
            if (grown == NULL) {
                free(buf);
                buf = NULL;
                break;
            }
            buf = grown;
            cap *= 2u;
        }
        const size_t got = fread(buf + used, 1, cap - used, f);
        used += got;
        if (got == 0) {
            break;
        }
    }
    fclose(f);
    *len = used;
    return buf;
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: %s ITERATIONS FILE...\n", argv[0]);
        return 2;
    }
    const unsigned long iterations = strtoul(argv[1], NULL, 10);
    unsigned long runs = 0;
    for (int a = 2; a < argc; ++a) {
        size_t len = 0;
        uint8_t *seed = fuzz_read_file(argv[a], &len);
        if (seed == NULL) {
            fprintf(stderr, "%s: cannot read %s\n", argv[0], argv[a]);
            return 2;
        }
        (void)LLVMFuzzerTestOneInput(seed, len);
        ++runs;
        /* a mutation grows an input by at most 32 bytes and the input is reset to the seed
         * every 16 rounds, so len + 16 * 32 + 64 bytes hold every intermediate input */
        const size_t cap = len + 16u * 32u + 64u;
        uint8_t *cur = malloc(cap), *next = malloc(cap);
        if (cur == NULL || next == NULL) {
            free(seed);
            free(cur);
            free(next);
            return 2;
        }
        memcpy(cur, seed, len);
        size_t cur_len = len;
        for (unsigned long i = 0; i < iterations; ++i) {
            if (i % 16u == 0) {
                memcpy(cur, seed, len);
                cur_len = len;
            }
            const size_t next_len = fuzz_mutate(cur, cur_len, next);
            (void)LLVMFuzzerTestOneInput(next, next_len);
            ++runs;
            uint8_t *swap = cur;
            cur = next;
            next = swap;
            cur_len = next_len;
        }
        free(seed);
        free(cur);
        free(next);
    }
    printf("%s: %lu inputs, no crash\n", argv[0], runs);
    return 0;
}
#endif /* CANON_FUZZ_MAIN */

#endif /* CANON_FUZZ_DRIVER_H */
