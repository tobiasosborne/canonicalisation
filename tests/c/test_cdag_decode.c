/* Slice S5 tests of the strict CDAG-2 decoder and the canonical-form validator (spec 4.1, 4.2,
 * 9.4, 11.1; docs/slices/S5.md 3.5, 4).  Every rejection rule of tools/hexdump_stream.py is
 * mirrored (truncation, magic, schema/action, q >= 1, unknown tag, atom range, child
 * references below the parent, set and multiset order, zero counts, Nat leading zeros, Perm
 * order, fixed pairs and bijection, Group mode and blocks, arc multiplicities, root range,
 * trailing bytes, reachability, root last, overlapping rule-1 blocks), plus the rules the tool
 * does not check (version byte, degree, Nat range, relations and nested graphs unsupported,
 * canonical order).  Round trips: the six spec 7.4 streams and every tag validate and
 * re-encode to themselves.  Expected hex lives here, never in src/. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "encoding/cdag_decode.h"
#include "encoding/cdag_encode.h"
#include "object/dag.h"

#define HDR "434e0200010001"

/* Decode hex (spaces ignored) into buf; returns the length. */
static size_t unhex(const char *hex, uint8_t *buf, size_t cap)
{
    size_t len = 0;
    int hi = -1;
    for (const char *c = hex; *c != '\0'; ++c) {
        int v = *c >= '0' && *c <= '9' ? *c - '0' : (*c >= 'a' && *c <= 'f' ? *c - 'a' + 10 : -1);
        if (v < 0) {
            continue;
        }
        if (hi < 0) {
            hi = v;
        } else {
            if (len < cap) {
                buf[len] = (uint8_t)(hi * 16 + v);
            }
            ++len;
            hi = -1;
        }
    }
    return len;
}

static const canon_dag_limits WIDE = {4096, UINT64_MAX, UINT64_MAX, UINT64_MAX};

/* Decode `hex`; check the status and reason (reason ignored when want_reason < 0). */
static void decode_is(const char *hex, canon_status want, int want_reason, int line)
{
    uint8_t buf[512];
    size_t len = unhex(hex, buf, sizeof buf);
    canon_dag d;
    canon_dag_init(&d, 0);
    canon_cdag_reason reason = CANON_CDAG_OK;
    canon_status st = canon_cdag_decode(buf, len, NULL, WIDE.max_n, &d, &reason);
    if (st != want || (want_reason >= 0 && (int)reason != want_reason)) {
        fprintf(stderr, "line %d: decode status %d reason %d, want %d reason %d\n", line, (int)st,
                (int)reason, (int)want, want_reason);
        CHECK(0);
    }
    /* a stream refused by the decoder is refused by the validator with the same status */
    if (want != CANON_COMPLETE) {
        CHECK(canon_cdag_validate(buf, len, &WIDE, &reason) == want);
    }
    canon_dag_free(&d);
}

/* Validate `hex`; check the status and reason. */
static void validate_is(const char *hex, canon_status want, int want_reason, int line)
{
    uint8_t buf[512];
    size_t len = unhex(hex, buf, sizeof buf);
    canon_cdag_reason reason = CANON_CDAG_OK;
    canon_status st = canon_cdag_validate(buf, len, &WIDE, &reason);
    if (st != want || (want_reason >= 0 && (int)reason != want_reason)) {
        fprintf(stderr, "line %d: validate status %d reason %d, want %d reason %d\n", line, (int)st,
                (int)reason, (int)want, want_reason);
        CHECK(0);
    }
}

#define BAD(hex, reason) decode_is(hex, CANON_INVALID_INPUT, reason, __LINE__)
#define UNSUP(hex, reason) decode_is(hex, CANON_UNSUPPORTED_ACTION, reason, __LINE__)
#define CAP(hex, reason) decode_is(hex, CANON_CAPACITY_LIMIT, reason, __LINE__)
#define OK(hex) decode_is(hex, CANON_COMPLETE, CANON_CDAG_OK, __LINE__)
#define VALID(hex) validate_is(hex, CANON_COMPLETE, CANON_CDAG_OK, __LINE__)
#define NONCANON(hex, reason) validate_is(hex, CANON_INVALID_INPUT, reason, __LINE__)

/* The six spec 7.4 streams, transcribed (golden.json p1_cases), plus one stream per tag. */
static const char *const CANONICAL[] = {
    HDR "00000000 00000001 04 00000000 00000000",
    HDR "00000002 00000002 01 00000001 04 00000001 00000000 00000001",
    HDR "00000002 00000002 01 00000000 04 00000001 00000000 00000001",
    HDR "00000002 00000001 04 00000000 00000000",
    HDR "00000002 00000001 09 00000000 00000000 00000001 00000001 00000000 00000000 00000001 01 "
        "00000000",
    HDR "00000000 00000002 02 00000000 03 00000002 00000000 00000000 00000001",
    /* atom 0, Perm([1,0]) (tags 01, 06), multiset {atom0 x 2, perm x 1} */
    HDR "00000002 00000003 01 00000000 06 00000002 00000000 00000001 00000001 00000000 "
        "05 00000002 00000000 00000001 02 00000001 00000001 01 00000002",
    /* spec 7.4 payloads as leaves: Group(Sym(2)), Group(C3), the coset H = 1, r = [1,0] */
    HDR "00000002 00000001 07 01 00000001 00000002 00000000 00000001 00000000",
    HDR "00000003 00000001 07 00 00000001 00000003 00000000 00000001 00000001 00000002 "
        "00000002 00000000 00000000",
    HDR "00000002 00000001 08 01 00000000 00000002 00000000 00000001 00000001 00000000 "
        "00000000",
    /* the trivial group and a tuple holding a set and a literal */
    HDR "00000001 00000001 07 01 00000000 00000000",
    HDR "00000001 00000004 01 00000000 02 00000001 61 04 00000001 00000000 "
        "03 00000002 00000002 00000001 00000003",
};

static void test_round_trips(void)
{
    for (size_t i = 0; i < sizeof CANONICAL / sizeof *CANONICAL; ++i) {
        uint8_t buf[512];
        size_t len = unhex(CANONICAL[i], buf, sizeof buf);
        canon_dag raw, norm;
        canon_dag_init(&raw, 0);
        canon_dag_init(&norm, 0);
        canon_dag_scratch s;
        canon_dag_scratch_init(&s);
        canon_cdag_reason reason = CANON_CDAG_OK;
        CHECK(canon_cdag_decode(buf, len, NULL, 4096, &raw, &reason) == CANON_COMPLETE);
        CHECK(canon_dag_normalise(&raw, &norm, &s, false) == CANON_COMPLETE);
        canon_buf out;
        canon_buf_init(&out);
        CHECK(canon_dag_stream_write(&out, &norm) == CANON_COMPLETE);
        CHECK(out.len == len && memcmp(out.data, buf, len) == 0);
        CHECK(norm.stream_size == len);
        CHECK(canon_cdag_validate(buf, len, &WIDE, &reason) == CANON_COMPLETE);
        /* spec 4.1 "Counts determine all variable boundaries": every proper prefix and every
         * extension by one byte is refused */
        for (size_t cut = 0; cut < len; ++cut) {
            CHECK(canon_cdag_decode(buf, cut, NULL, 4096, &raw, &reason) == CANON_INVALID_INPUT);
            CHECK(reason == CANON_CDAG_TRUNCATED);
        }
        buf[len] = 0;
        CHECK(canon_cdag_decode(buf, len + 1, NULL, 4096, &raw, &reason) == CANON_INVALID_INPUT);
        CHECK(reason == CANON_CDAG_TRAILING);
        canon_buf_free(&out);
        canon_dag_scratch_free(&s);
        canon_dag_free(&raw);
        canon_dag_free(&norm);
    }
}

/* tools/hexdump_stream.py's rules (Malformed, ExtendedTags), then the stricter ones. */
static void test_rejections(void)
{
    /* header */
    BAD("", CANON_CDAG_TRUNCATED);
    BAD("434e", CANON_CDAG_TRUNCATED);
    BAD("444e0200010001 00000000 00000001 04 00000000 00000000", CANON_CDAG_BAD_MAGIC);
    BAD("434f0200010001 00000000 00000001 04 00000000 00000000", CANON_CDAG_BAD_MAGIC);
    /* hexdump: bad magic/version "434e03..." - the third byte is the encoding version, spec
     * 4.1: "unknown ... encoding versions are unsupported, never reinterpreted" */
    UNSUP("434e03000100010000000000000001", CANON_CDAG_VERSION);
    UNSUP("434e0200020001 00000000 00000001 04 00000000 00000000", CANON_CDAG_SCHEMA_ACTION);
    UNSUP("434e0200010002 00000000 00000001 04 00000000 00000000", CANON_CDAG_SCHEMA_ACTION);
    BAD(HDR "00000000 00000000 00000000", CANON_CDAG_EMPTY);                    /* q >= 1 */
    CAP(HDR "00001001 00000001 04 00000000 00000000", CANON_CDAG_DEGREE_LIMIT); /* n > 4096 */
    BAD(HDR "00000000 00000005 04 00000000 00000000", CANON_CDAG_TRUNCATED);    /* q too big */
    /* tags */
    BAD(HDR "00000002 00000001 ff 00000000 00000000", CANON_CDAG_UNKNOWN_TAG);
    BAD(HDR "00000002 00000001 00 00000000 00000000", CANON_CDAG_UNKNOWN_TAG);
    BAD(HDR "00000002 00000001 0b 00000000 00000000", CANON_CDAG_UNKNOWN_TAG);
    /* relations (hexdump's well-formed example): recognised, unsupported in S5 */
    UNSUP(HDR "00000002 00000001 0a 00000001 00000001 72 00000001 00000001 00000001 00000001 01 "
              "00000000",
          CANON_CDAG_RELATIONS);
    /* atoms and references */
    BAD(HDR "00000002 00000001 01 00000002 00000000", CANON_CDAG_ATOM_RANGE);
    BAD(HDR "00000002 00000002 01 00000000 03 00000001 00000001 00000001", CANON_CDAG_FORWARD_REF);
    BAD(HDR "00000002 00000002 01 00000000 03 00000001 00000002 00000001", CANON_CDAG_FORWARD_REF);
    BAD(HDR "00000002 00000003 01 00000000 01 00000001 04 00000002 00000001 00000000 00000002",
        CANON_CDAG_SET_ORDER);
    BAD(HDR "00000002 00000002 01 00000000 04 00000002 00000000 00000000 00000001",
        CANON_CDAG_SET_ORDER);
    BAD(HDR "00000002 00000003 01 00000000 01 00000001 05 00000002 00000001 00000001 01 "
            "00000000 00000001 01 00000002",
        CANON_CDAG_MULTISET_ORDER);
    BAD(HDR "00000002 00000002 01 00000000 05 00000001 00000000 00000000 00000001",
        CANON_CDAG_ZERO_COUNT);
    BAD(HDR "00000002 00000002 01 00000000 05 00000001 00000000 00000001 00 00000001",
        CANON_CDAG_NAT_LEADING_ZERO);
    BAD(HDR "00000002 00000002 01 00000000 05 00000001 00000000 00000002 0001 00000001",
        CANON_CDAG_NAT_LEADING_ZERO);
    /* a 9-byte shortest Nat is at least 2^64: count-bit limit 64 */
    CAP(HDR "00000002 00000002 01 00000000 05 00000001 00000000 00000009 010000000000000000 "
            "00000001",
        CANON_CDAG_NAT_RANGE);
    BAD(HDR "00000000 00000001 02 00000005 6162 00000000", CANON_CDAG_TRUNCATED);
    /* Perm (spec 4.1) */
    BAD(HDR "00000002 00000001 06 00000001 00000000 00000000 00000000", CANON_CDAG_PERM_FIXED);
    /* hexdump's "not a bijection" example lists the fixed pair (1, 1) */
    BAD(HDR "00000002 00000001 06 00000002 00000000 00000001 00000001 00000001 00000000",
        CANON_CDAG_PERM_FIXED);
    BAD(HDR "00000003 00000001 06 00000003 00000000 00000001 00000001 00000000 00000002 "
            "00000000 00000000",
        CANON_CDAG_PERM_BIJECTION);
    BAD(HDR "00000003 00000001 06 00000002 00000000 00000001 00000001 00000002 00000000",
        CANON_CDAG_PERM_BIJECTION);
    BAD(HDR "00000002 00000001 06 00000002 00000001 00000000 00000000 00000001 00000000",
        CANON_CDAG_PERM_ORDER);
    BAD(HDR "00000002 00000001 06 00000002 00000000 00000001 00000000 00000001 00000000",
        CANON_CDAG_PERM_ORDER);
    BAD(HDR "00000002 00000001 06 00000002 00000000 00000002 00000002 00000000 00000000",
        CANON_CDAG_PERM_RANGE);
    BAD(HDR "00000002 00000001 06 00000003 00000000 00000001 00000001 00000000 00000000",
        CANON_CDAG_TRUNCATED);
    /* Group (spec 9.4) */
    BAD(HDR "00000002 00000001 07 02 00000000 00000000", CANON_CDAG_GROUP_MODE);
    BAD(HDR "00000002 00000001 07 01 00000001 00000001 00000000 00000000", CANON_CDAG_GROUP_BLOCK);
    BAD(HDR "00000002 00000001 07 01 00000001 00000002 00000001 00000000 00000000",
        CANON_CDAG_GROUP_BLOCK);
    BAD(HDR "00000004 00000001 07 01 00000002 00000002 00000002 00000003 00000002 00000000 "
            "00000001 00000000",
        CANON_CDAG_GROUP_BLOCK); /* blocks not ordered by least point */
    /* overlapping blocks {0,1}, {1,2}: ordered by least point, but not orbits (the tool checks
     * this too since the S5 review) */
    BAD(HDR "00000003 00000001 07 01 00000002 00000002 00000000 00000001 00000002 00000001 "
            "00000002 00000000",
        CANON_CDAG_GROUP_BLOCK);
    BAD(HDR "00000002 00000001 07 01 00000001 00000002 00000000 00000002 00000000",
        CANON_CDAG_ATOM_RANGE);
    BAD(HDR "00000002 00000001 07 00 00000001 00000001 00000000 00000000 00000000",
        CANON_CDAG_PERM_FIXED);
    BAD(HDR "00000002 00000001 08 01 00000000 00000001 00000001 00000001 00000000",
        CANON_CDAG_PERM_FIXED);
    /* graph (spec 4.1 arc records) */
    BAD(HDR "00000002 00000001 09 00000000 00000000 00000001 00000000 00000002 00000000 00000001 "
            "01 00000000",
        CANON_CDAG_ATOM_RANGE);
    BAD(HDR "00000002 00000001 09 00000000 00000000 00000001 00000000 00000001 00000000 00000000 "
            "00000000",
        CANON_CDAG_ZERO_COUNT);
    BAD(HDR "00000002 00000001 09 00000000 00000000 00000001 00000000 00000001 00000000 00000001 "
            "00 00000000",
        CANON_CDAG_NAT_LEADING_ZERO);
    BAD(HDR "00000002 00000001 09 00000000 00000000", CANON_CDAG_TRUNCATED);
    /* root and framing */
    BAD(HDR "00000002 00000001 04 00000000 00000001", CANON_CDAG_ROOT_RANGE);
    BAD(HDR "00000002 00000001 04 00000000 00000000 00", CANON_CDAG_TRAILING);
}

/* What import accepts and the validator refuses (spec 4.2). */
static void test_noncanonical(void)
{
    /* hexdump: record 0 unreachable (root = 1, the second atom) */
    OK(HDR "00000002 00000002 01 00000000 01 00000001 00000001");
    NONCANON(HDR "00000002 00000002 01 00000000 01 00000001 00000001", CANON_CDAG_UNREACHABLE);
    /* hexdump: root not last */
    OK(HDR "00000002 00000002 01 00000000 01 00000001 00000000");
    NONCANON(HDR "00000002 00000002 01 00000000 01 00000001 00000000", CANON_CDAG_UNREACHABLE);
    /* a re-ordered but equivalent stream: the atoms in decreasing order */
    OK(HDR "00000002 00000003 01 00000001 01 00000000 04 00000002 00000000 00000001 00000002");
    NONCANON(HDR "00000002 00000003 01 00000001 01 00000000 04 00000002 00000000 00000001 "
                 "00000002",
             CANON_CDAG_NOT_CANONICAL);
    /* repeated equal records, both reachable: the tuple (a0, a0') */
    NONCANON(HDR "00000002 00000003 01 00000000 01 00000000 03 00000002 00000000 00000001 "
                 "00000002",
             CANON_CDAG_NOT_CANONICAL);
    /* a non-canonical Group presentation: Sym(2) by rule 2 */
    NONCANON(HDR "00000002 00000001 07 00 00000001 00000002 00000000 00000001 00000001 00000000 "
                 "00000000",
             CANON_CDAG_NOT_CANONICAL);
    /* a coset with a representative that is not the least element: H = Sym(2), r = [1,0] */
    NONCANON(HDR "00000002 00000001 08 01 00000001 00000002 00000000 00000001 00000002 00000000 "
                 "00000001 00000001 00000000 00000000",
             CANON_CDAG_NOT_CANONICAL);
    /* docs/slices/S5.md 3.5 duplicate arcs: combined on import, invalid in a canonical stream
     * (hexdump: "arcs not sorted/unique by (source, target, B(label))") */
    const char *dup = HDR "00000002 00000001 09 00000000 00000000 00000002 00000000 00000001 "
                          "00000000 00000001 01 00000000 00000001 00000000 00000001 01 00000000";
    OK(dup);
    NONCANON(dup, CANON_CDAG_NOT_CANONICAL);
    const char *unsorted = HDR "00000002 00000001 09 00000000 00000000 00000002 00000001 "
                               "00000000 00000000 00000001 01 00000000 00000001 00000000 "
                               "00000001 01 00000000";
    OK(unsorted);
    NONCANON(unsorted, CANON_CDAG_NOT_CANONICAL);
    /* a graph record below the root: unsupported in S5 (later slice); an unreachable one is
     * only non-canonical */
    validate_is(HDR "00000001 00000002 09 00000000 00000000 03 00000001 00000000 00000001",
                CANON_UNSUPPORTED_ACTION, CANON_CDAG_NESTED_GRAPH, __LINE__);
    NONCANON(HDR "00000001 00000002 09 00000000 00000000 02 00000000 00000001",
             CANON_CDAG_UNREACHABLE);
    /* the limits apply to the normal form (spec 11.1) */
    uint8_t buf[128];
    size_t len =
        unhex(HDR "00000002 00000002 01 00000001 04 00000001 00000000 00000001", buf, sizeof buf);
    canon_cdag_reason reason = CANON_CDAG_OK;
    canon_dag_limits lim = WIDE;
    lim.max_nodes = 1;
    CHECK(canon_cdag_validate(buf, len, &lim, &reason) == CANON_CAPACITY_LIMIT);
    lim = WIDE;
    lim.max_refs = 0;
    CHECK(canon_cdag_validate(buf, len, &lim, &reason) == CANON_CAPACITY_LIMIT);
    lim = WIDE;
    lim.max_n = 1;
    CHECK(canon_cdag_validate(buf, len, &lim, &reason) == CANON_CAPACITY_LIMIT);
    CHECK(reason == CANON_CDAG_DEGREE_LIMIT);
    len = unhex(HDR "00000000 00000001 02 00000003 616263 00000000", buf, sizeof buf);
    lim = WIDE;
    lim.max_literal_bytes = 2;
    CHECK(canon_cdag_validate(buf, len, &lim, &reason) == CANON_CAPACITY_LIMIT);
    lim.max_literal_bytes = 3;
    CHECK(canon_cdag_validate(buf, len, &lim, &reason) == CANON_COMPLETE);
    /* the degree expected by import */
    canon_dag d;
    canon_dag_init(&d, 0);
    uint32_t three = 3, zero = 0;
    CHECK(canon_cdag_decode(buf, len, &three, 4096, &d, &reason) == CANON_INVALID_INPUT &&
          reason == CANON_CDAG_DEGREE);
    CHECK(canon_cdag_decode(buf, len, &zero, 4096, &d, &reason) == CANON_COMPLETE);
    canon_dag_free(&d);
}

/* Random mutations of canonical streams never crash and never decode to something the
 * validator accepts unless the mutation is itself canonical (a light fuzz under the sanitizer
 * build). */
static void test_mutations(void)
{
    canon_dag d;
    canon_dag_init(&d, 0);
    for (int trial = 0; trial < 3000; ++trial) {
        uint8_t buf[512];
        const char *src = CANONICAL[check_rng() % (sizeof CANONICAL / sizeof *CANONICAL)];
        size_t len = unhex(src, buf, sizeof buf);
        size_t at = (size_t)(check_rng() % len);
        buf[at] = (uint8_t)(buf[at] ^ (1u << (check_rng() % 8)));
        canon_cdag_reason reason = CANON_CDAG_OK;
        canon_status st = canon_cdag_decode(buf, len, NULL, 4096, &d, &reason);
        CHECK(st == CANON_COMPLETE || st == CANON_INVALID_INPUT || st == CANON_UNSUPPORTED_ACTION ||
              st == CANON_CAPACITY_LIMIT);
        CHECK((st == CANON_COMPLETE) == (reason == CANON_CDAG_OK));
        canon_status v = canon_cdag_validate(buf, len, &WIDE, &reason);
        CHECK(v != CANON_INTERNAL_ERROR && (st == CANON_COMPLETE || v == st));
    }
    canon_dag_free(&d);
}

/* S5 review item 2: the payload readers leave `bits` all zero on every path (the contract of
 * cdag_decode.h), on valid, refused and randomly corrupted Perm and Group payloads. */
static void test_bits_contract(void)
{
    static const char *const PAYLOADS[] = {
        "00000002 00000000 00000001 00000001 00000000",                      /* Perm([1,0,2]) */
        "00000002 00000000 00000001 00000001 00000002",                      /* not a bijection */
        "00000003 00000000 00000001 00000001 00000000 00000002 00000000",    /* repeated target */
        "00000002 00000001 00000000 00000000 00000001",                      /* sources unordered */
        "00000002 00000000 00000001 00000001 00000001",                      /* fixed pair */
        "00000002 00000000 00000001 00000002 00000007",                      /* out of range */
        "01 00000001 00000003 00000000 00000001 00000002",                   /* rule 1, valid */
        "01 00000002 00000002 00000000 00000001 00000002 00000001 00000002", /* overlap */
        "01 00000002 00000002 00000001 00000002 00000002 00000000 00000002", /* unordered */
        "01 00000001 00000003 00000000 00000002 00000001",                   /* points unordered */
        "00 00000002 00000002 00000000 00000001 00000001 00000000 00000001 00000002 00000000",
    };
    uint64_t bits[2];
    uint32_t dense[3], tmp[3];
    for (int trial = 0; trial < 4000; ++trial) {
        const size_t which = (size_t)trial % (sizeof PAYLOADS / sizeof *PAYLOADS);
        uint8_t buf[64];
        size_t len = unhex(PAYLOADS[which], buf, sizeof buf);
        if (trial >= (int)(sizeof PAYLOADS / sizeof *PAYLOADS)) {
            buf[check_rng() % len] ^= (uint8_t)(1u << (check_rng() % 8)); /* corrupt one bit */
        }
        bits[0] = bits[1] = 0;
        canon_cdag_reader r = {buf, len, 0};
        canon_cdag_reason reason = CANON_CDAG_OK;
        canon_perm_table gens;
        canon_perm_table_init(&gens, 3);
        if (buf[0] == 0x00 && len % 2 == 1) {
            (void)canon_cdag_read_group(&r, 3, bits, &gens, tmp, &reason);
        } else if (buf[0] <= 0x01 && len % 2 == 1) {
            (void)canon_cdag_read_group(&r, 3, bits, NULL, NULL, &reason);
        } else {
            (void)canon_cdag_read_perm(&r, 3, bits, dense, &reason);
        }
        CHECK(bits[0] == 0 && bits[1] == 0);
        canon_perm_table_free(&gens);
    }
}

/* S5 review item 3: an arc's endpoints are checked before its label is consumed, so an
 * out-of-range source followed by a truncated label is ATOM_RANGE (the first violation in
 * stream order), not TRUNCATED. */
static void test_reason_order(void)
{
    BAD(HDR "00000002 00000001 09 00000000 00000000 00000001 00000002 00000000 0000ffff 00000000",
        CANON_CDAG_ATOM_RANGE);
    BAD(HDR "00000002 00000001 09 00000000 00000000 00000001 00000000 00000005 0000ffff 00000000",
        CANON_CDAG_ATOM_RANGE);
    /* in range, then the truncated label decides */
    BAD(HDR "00000002 00000001 09 00000000 00000000 00000001 00000000 00000001 0000ffff 00000000",
        CANON_CDAG_TRUNCATED);
}

int main(void)
{
    test_bits_contract();
    test_reason_order();
    test_round_trips();
    test_rejections();
    test_noncanonical();
    test_mutations();
    return check_finish("test_cdag_decode");
}
