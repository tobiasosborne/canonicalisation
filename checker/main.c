/* canon-check: independent certificate checker (docs/specification.md section 20; slice S7,
 * docs/slices/S7.md 3.4).  Depends on nothing but the C standard library: it must not link the
 * canon library and must not include anything from src/ or include/ (checker/README.md).
 *
 *   canon-check FILE      read a CERT-0 certificate (FILE, or '-' for standard input)
 *   exit 0 and "ACCEPT <nodes>" on acceptance; exit 1 and "REJECT <reason>" on rejection;
 *   exit 2 and "CAPACITY" when the certificate exceeds this checker's resource limits
 *   (checker/README.md rule 7: no verdict on the certificate); exit 2 on a usage or input
 *   error. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cc.h"

cc_reason cc_check(const uint8_t *cert, size_t len, uint64_t *nodes_out);

/* Read the whole certificate, at most CC_MAX_INPUT_BYTES of it (rule 7).  Returns 0 on
 * success, 1 on a read or allocation failure, 2 when the input is larger than the limit
 * (one byte past it is enough to know). */
static int read_all(FILE *f, uint8_t **out, size_t *len)
{
    size_t cap = 1u << 16, used = 0;
    uint8_t *buf = malloc(cap);
    if (buf == NULL) {
        return 1;
    }
    for (;;) {
        if (used == cap) {
            if (cap > CC_MAX_INPUT_BYTES) {
                free(buf);
                return 2; /* already past the limit and the stream is not exhausted */
            }
            size_t next = cap > SIZE_MAX / 2u ? cap : cap * 2u;
            if (next > CC_MAX_INPUT_BYTES + 1u) {
                next = CC_MAX_INPUT_BYTES + 1u;
            }
            uint8_t *grown = realloc(buf, next);
            if (grown == NULL) {
                free(buf);
                return 1;
            }
            buf = grown;
            cap = next;
        }
        const size_t got = fread(buf + used, 1, cap - used, f);
        used += got;
        if (got == 0) {
            break;
        }
    }
    if (ferror(f)) {
        free(buf);
        return 1;
    }
    if (used > CC_MAX_INPUT_BYTES) {
        free(buf);
        return 2;
    }
    *out = buf;
    *len = used;
    return 0;
}

static int capacity_exit(void)
{
    puts("CAPACITY");
    fputs("canon-check: certificate exceeds checker capacity (no verdict)\n", stderr);
    return 2;
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fputs("usage: canon-check FILE   (FILE '-' reads standard input)\n", stderr);
        return 2;
    }
    FILE *f = strcmp(argv[1], "-") == 0 ? stdin : fopen(argv[1], "rb");
    if (f == NULL) {
        fprintf(stderr, "canon-check: cannot open %s\n", argv[1]);
        return 2;
    }
    size_t len = 0;
    uint8_t *cert = NULL;
    const int read_status = read_all(f, &cert, &len);
    if (f != stdin) {
        fclose(f);
    }
    if (read_status == 2) {
        return capacity_exit(); /* rule 7: beyond CC_MAX_INPUT_BYTES, no verdict */
    }
    if (read_status != 0) {
        fputs("canon-check: cannot read the certificate\n", stderr);
        return 2;
    }
    uint64_t nodes = 0;
    const cc_reason why = cc_check(cert, len, &nodes);
    free(cert);
    if (why == CC_OUT_OF_MEMORY) {
        fputs("canon-check: out of memory\n", stderr);
        return 2;
    }
    if (why == CC_CAPACITY) {
        return capacity_exit(); /* rule 7: a checker limit, never a rejection */
    }
    if (why == CC_ACCEPT) {
        printf("ACCEPT %llu\n", (unsigned long long)nodes);
        return 0;
    }
    printf("REJECT %s\n", cc_reason_name(why));
    return 1;
}
