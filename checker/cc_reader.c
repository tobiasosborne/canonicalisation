/* Byte reader, checked allocation and permutation primitives of canon-check (spec 3, 4.1). */
#include <stdlib.h>
#include <string.h>

#include "cc.h"

bool cc_get_u8(cc_reader *r, uint8_t *out)
{
    if (r->len - r->at < 1) {
        return false;
    }
    *out = r->data[r->at++];
    return true;
}

bool cc_get_u16(cc_reader *r, uint16_t *out)
{
    if (r->len - r->at < 2) {
        return false;
    }
    *out = (uint16_t)((uint16_t)r->data[r->at] << 8 | r->data[r->at + 1]);
    r->at += 2;
    return true;
}

bool cc_get_u32(cc_reader *r, uint32_t *out)
{
    if (r->len - r->at < 4) {
        return false;
    }
    const uint8_t *d = r->data + r->at;
    *out = (uint32_t)d[0] << 24 | (uint32_t)d[1] << 16 | (uint32_t)d[2] << 8 | (uint32_t)d[3];
    r->at += 4;
    return true;
}

bool cc_get_u64(cc_reader *r, uint64_t *out)
{
    uint32_t hi = 0, lo = 0;
    if (!cc_get_u32(r, &hi) || !cc_get_u32(r, &lo)) {
        return false;
    }
    *out = (uint64_t)hi << 32 | lo;
    return true;
}

bool cc_get_b(cc_reader *r, const uint8_t **out, uint32_t *len)
{
    if (!cc_get_u32(r, len) || r->len - r->at < *len) {
        return false;
    }
    *out = r->data + r->at;
    r->at += *len;
    return true;
}

void *cc_alloc(size_t count, size_t size, cc_reason *why)
{
    /* never malloc(0): an empty array is one element, so a success is never NULL */
    if (count == 0) {
        count = 1;
    }
    if (size == 0) {
        size = 1;
    }
    if (count > CC_MAX_ALLOC / size) {
        *why = CC_CAPACITY; /* beyond CC_MAX_ALLOC (or SIZE_MAX) */
        return NULL;
    }
    void *block = malloc(count * size);
    if (block == NULL) {
        *why = CC_OUT_OF_MEMORY;
    }
    return block;
}

uint32_t *cc_get_u32s(cc_reader *r, uint32_t k, cc_reason *why)
{
    const size_t count = k;
    if (count > SIZE_MAX / 4u) {
        *why = CC_CAPACITY; /* a 32-bit size_t could not hold the byte count */
        return NULL;
    }
    if (r->len - r->at < 4u * count) {
        *why = CC_TRUNCATED;
        return NULL;
    }
    uint32_t *out = cc_alloc(k, sizeof *out, why);
    if (out == NULL) {
        return NULL;
    }
    for (uint32_t i = 0; i < k; ++i) {
        (void)cc_get_u32(r, &out[i]);
    }
    return out;
}

/* ---- permutations (spec 3: p[v] = v^p, (pq)[v] = q[p[v]]) ---- */

void cc_compose(const uint32_t *p, const uint32_t *q, uint32_t *out, uint32_t n)
{
    for (uint32_t v = 0; v < n; ++v) {
        out[v] = q[p[v]]; /* p acts first */
    }
}

void cc_inverse(const uint32_t *p, uint32_t *out, uint32_t n)
{
    for (uint32_t v = 0; v < n; ++v) {
        out[p[v]] = v;
    }
}

bool cc_is_identity(const uint32_t *p, uint32_t n)
{
    for (uint32_t v = 0; v < n; ++v) {
        if (p[v] != v) {
            return false;
        }
    }
    return true;
}

bool cc_is_bijection(const uint32_t *p, uint32_t n, uint64_t *bitmap)
{
    const size_t words = ((size_t)n + 63u) / 64u;
    for (size_t w = 0; w < words; ++w) {
        bitmap[w] = 0;
    }
    for (uint32_t v = 0; v < n; ++v) {
        const uint32_t t = p[v];
        if (t >= n || (bitmap[t / 64u] >> (t % 64u) & 1u)) {
            return false; /* spec 4.1: out-of-range target or a repeated target */
        }
        bitmap[t / 64u] |= (uint64_t)1 << (t % 64u);
    }
    return true;
}

int cc_perm_compare(const uint32_t *a, const uint32_t *b, uint32_t n)
{
    for (uint32_t v = 0; v < n; ++v) {
        if (a[v] != b[v]) {
            return a[v] < b[v] ? -1 : 1;
        }
    }
    return 0;
}

int cc_bytes_compare(const uint8_t *a, size_t a_len, const uint8_t *b, size_t b_len)
{
    const size_t m = a_len < b_len ? a_len : b_len;
    for (size_t i = 0; i < m; ++i) {
        if (a[i] != b[i]) {
            return a[i] < b[i] ? -1 : 1;
        }
    }
    /* spec 4.3: a proper prefix is smaller */
    return a_len < b_len ? -1 : (a_len > b_len ? 1 : 0);
}

const char *cc_reason_name(cc_reason r)
{
    switch (r) {
    case CC_ACCEPT: return "ACCEPT";
    case CC_TRUNCATED: return "TRUNCATED";
    case CC_TRAILING_BYTES: return "TRAILING_BYTES";
    case CC_CAPACITY: return "CAPACITY";
    case CC_HEADER: return "HEADER";
    case CC_DEGREE_MISMATCH: return "DEGREE_MISMATCH";
    case CC_OBJECT_MALFORMED: return "OBJECT_MALFORMED";
    case CC_OBJECT_UNSUPPORTED: return "OBJECT_UNSUPPORTED";
    case CC_GENERATOR_NOT_BIJECTION: return "GENERATOR_NOT_BIJECTION";
    case CC_ROOT_CHAIN_INPUTS: return "ROOT_CHAIN_INPUTS";
    case CC_CHAIN_STRUCTURE: return "CHAIN_STRUCTURE";
    case CC_CHAIN_NOT_BIJECTION: return "CHAIN_NOT_BIJECTION";
    case CC_CHAIN_PROVENANCE: return "CHAIN_PROVENANCE";
    case CC_CHAIN_PREFIX_NOT_FIXED: return "CHAIN_PREFIX_NOT_FIXED";
    case CC_CHAIN_NOT_NESTED: return "CHAIN_NOT_NESTED";
    case CC_CHAIN_ORBIT_TREE: return "CHAIN_ORBIT_TREE";
    case CC_CHAIN_ORBIT_NOT_CLOSED: return "CHAIN_ORBIT_NOT_CLOSED";
    case CC_CHAIN_TRANSVERSAL: return "CHAIN_TRANSVERSAL";
    case CC_CHAIN_SCHREIER_RESIDUE: return "CHAIN_SCHREIER_RESIDUE";
    case CC_CHAIN_INPUT_NOT_MEMBER: return "CHAIN_INPUT_NOT_MEMBER";
    case CC_AUTOMORPHISM_NOT_MEMBER: return "AUTOMORPHISM_NOT_MEMBER";
    case CC_AUTOMORPHISM_MOVES_X: return "AUTOMORPHISM_MOVES_X";
    case CC_NODE_DEPTH: return "NODE_DEPTH";
    case CC_NODE_ATOM: return "NODE_ATOM";
    case CC_SWEEP_COUNT: return "SWEEP_COUNT";
    case CC_STAGE_O_SIZES: return "STAGE_O_SIZES";
    case CC_F_MISMATCH: return "F_MISMATCH";
    case CC_U_NOT_BIJECTION: return "U_NOT_BIJECTION";
    case CC_U_NOT_MEMBER: return "U_NOT_MEMBER";
    case CC_M_MISMATCH: return "M_MISMATCH";
    case CC_REBASED_NOT_SAME_GROUP: return "REBASED_NOT_SAME_GROUP";
    case CC_REBASED_BASE_PREFIX: return "REBASED_BASE_PREFIX";
    case CC_M_NOT_MINIMAL: return "M_NOT_MINIMAL";
    case CC_STAGE_G_SIZES: return "STAGE_G_SIZES";
    case CC_LEAF_NOT_DISCRETE: return "LEAF_NOT_DISCRETE";
    case CC_LEAF_T_MISMATCH: return "LEAF_T_MISMATCH";
    case CC_BRANCH_AT_DISCRETE: return "BRANCH_AT_DISCRETE";
    case CC_TARGET_CELL: return "TARGET_CELL";
    case CC_CHILD_NOT_IN_CELL: return "CHILD_NOT_IN_CELL";
    case CC_CHILD_REPEATED: return "CHILD_REPEATED";
    case CC_CHILD_MISSING: return "CHILD_MISSING";
    case CC_PRUNED_REP_NOT_EXPLORED: return "PRUNED_REP_NOT_EXPLORED";
    case CC_PRUNED_AUTOMORPHISM_INDEX: return "PRUNED_AUTOMORPHISM_INDEX";
    case CC_PRUNED_AUTOMORPHISM_MOVES_PREFIX: return "PRUNED_AUTOMORPHISM_MOVES_PREFIX";
    case CC_PRUNED_AUTOMORPHISM_IMAGE: return "PRUNED_AUTOMORPHISM_IMAGE";
    case CC_PRUNED_UNDER_UNPRUNED_POLICY: return "PRUNED_UNDER_UNPRUNED_POLICY";
    case CC_NODE_COUNT: return "NODE_COUNT";
    case CC_FINAL_TRACE: return "FINAL_TRACE";
    case CC_FINAL_BYTES: return "FINAL_BYTES";
    case CC_FINAL_WITNESS: return "FINAL_WITNESS";
    case CC_OUT_OF_MEMORY: return "OUT_OF_MEMORY";
    }
    return "UNKNOWN";
}
