/* canon-check: independent certificate checker (docs/specification.md section 20; slice S7,
 * docs/slices/S7.md 3.4).  Depends on nothing but the C standard library: it must not link the
 * canon library and must not include anything from src/ or include/ (checker/README.md).
 *
 *   canon-check FILE      read a CERT-0 certificate (FILE, or '-' for standard input)
 *   exit 0 and "ACCEPT <nodes>" on acceptance; exit 1 and "REJECT <reason>" on rejection;
 *   exit 2 on a usage or input error. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cc.h"

cc_reason cc_check(const uint8_t *cert, size_t len, uint64_t *nodes_out);

static uint8_t *read_all(FILE *f, size_t *len)
{
    size_t cap = 1u << 16, used = 0;
    uint8_t *buf = malloc(cap);
    if (buf == NULL) {
        return NULL;
    }
    for (;;) {
        if (used == cap) {
            if (cap > SIZE_MAX / 2u) {
                free(buf);
                return NULL;
            }
            uint8_t *grown = realloc(buf, cap * 2u);
            if (grown == NULL) {
                free(buf);
                return NULL;
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
    if (ferror(f)) {
        free(buf);
        return NULL;
    }
    *len = used;
    return buf;
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
    uint8_t *cert = read_all(f, &len);
    if (f != stdin) {
        fclose(f);
    }
    if (cert == NULL) {
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
    if (why == CC_ACCEPT) {
        printf("ACCEPT %llu\n", (unsigned long long)nodes);
        return 0;
    }
    printf("REJECT %s\n", cc_reason_name(why));
    return 1;
}
