/* Perm, Group(H) and labeling-coset payloads (spec 4.1, 9.4; slice S4, docs/slices/S4.md 3.4,
 * 3.5). */
#include "encoding/group_stream.h"

#include <stdlib.h>
#include <string.h>

#include "arena/alloc.h"
#include "arena/checked.h"
#include "perm/perm.h"

canon_status canon_perm_bytes_write(canon_buf *out, const uint32_t *p, uint32_t n)
{
    /* spec 4.1: "Perm(p)=U32(s) followed by s pairs U32(i),U32(p[i]) in increasing i, exactly
     * the moved support; unlisted points are fixed." */
    uint32_t s = 0;
    for (uint32_t i = 0; i < n; ++i) {
        s += p[i] != i;
    }
    size_t bytes = 0;
    if (!canon_size_mul((size_t)s, 8u, &bytes) || !canon_size_add(bytes, 4u, &bytes)) {
        return CANON_CAPACITY_LIMIT; /* spec 11.1 */
    }
    canon_status st = canon_buf_reserve(out, bytes);
    if (st != CANON_COMPLETE) {
        return st;
    }
    (void)canon_buf_put_u32(out, s); /* reserved: cannot fail */
    for (uint32_t i = 0; i < n; ++i) {
        if (p[i] != i) {
            (void)canon_buf_put_u32(out, i);    /* source ... */
            (void)canon_buf_put_u32(out, p[i]); /* ... then target (spec 7.4) */
        }
    }
    return CANON_COMPLETE;
}

/* spec 9.4 rule 1.  The orbits of H are read from the chain (union-find over its strong
 * generators, ranked by least point, src/bsgs/chain.h canon_bsgs_orbit_ids).  "H embeds in the
 * product of symmetric groups on those orbits.  If its exact order equals the product of
 * orbit factorials, equality follows by finite containment."  The product is computed in
 * uint64 with overflow detection: an overflowing product exceeds 2^64 - 1 >= |H| (orders fit
 * uint64 in this release), so rule 1 does not apply, which decides the rule exactly
 * (docs/slices/S4.md 3.5).  *applies is false then; otherwise rule 1 is written. */
static canon_status rule1(canon_buf *out, const canon_bsgs *h, bool *applies, uint32_t *blocks)
{
    const uint32_t n = h->n;
    *applies = false;
    canon_status st = CANON_COMPLETE;
    size_t words = 0;
    if (!canon_size_mul((size_t)n, 3u, &words)) {
        return CANON_CAPACITY_LIMIT;
    }
    uint32_t *block = canon_alloc_array(words, sizeof *block, &st);
    if (block == NULL) {
        return st;
    }
    uint32_t *id = block, *size = block + (size_t)n, *points = block + 2u * (size_t)n;
    canon_bsgs_orbit_ids(h, 0, id);
    uint32_t orbits = 0;
    for (uint32_t v = 0; v < n; ++v) {
        size[v] = 0;
    }
    for (uint32_t v = 0; v < n; ++v) {
        size[id[v]] += 1;
        orbits = id[v] + 1u > orbits ? id[v] + 1u : orbits;
    }
    uint64_t product = 1;
    bool fits = true;
    uint32_t k = 0;
    for (uint32_t o = 0; o < orbits && fits; ++o) {
        k += size[o] > 1;
        for (uint64_t f = 2; f <= size[o] && fits; ++f) {
            fits = canon_u64_mul(product, f, &product); /* |O|! */
        }
    }
    if (fits && product == canon_bsgs_suffix_order(h, 0)) {
        *applies = true;
        *blocks = k;
        /* points of each orbit in increasing order: a counting sort by orbit id over
         * v = 0..n-1 into points[] */
        uint32_t offset = 0;
        for (uint32_t o = 0; o < orbits; ++o) {
            const uint32_t sz = size[o];
            size[o] = offset; /* size[] now holds each orbit's next free slot */
            offset += sz;
        }
        /* place v in its orbit's next slot */
        for (uint32_t v = 0; v < n; ++v) {
            points[size[id[v]]++] = v;
        }
        /* size[o] is now the end of orbit o; its start is the end of orbit o - 1 */
        size_t bytes = 5u; /* 01 || U32(k) */
        for (uint32_t o = 0; o < orbits; ++o) {
            const uint32_t lo = o == 0 ? 0 : size[o - 1u], len = size[o] - lo;
            if (len > 1) {
                bytes += 4u + 4u * (size_t)len; /* <= 5 + 8n: fits, n words were allocated */
            }
        }
        st = canon_buf_reserve(out, bytes);
        if (st == CANON_COMPLETE) {
            /* spec 9.4: "Encode 01 || U32(k) followed by each non-singleton orbit as
             * U32(size), U32(points...); points increase, blocks order by least point.  Omit
             * singleton orbits." (orbit ids are ranked by least point) */
            (void)canon_buf_put_u8(out, 0x01);
            (void)canon_buf_put_u32(out, k);
            for (uint32_t o = 0; o < orbits; ++o) {
                const uint32_t lo = o == 0 ? 0 : size[o - 1u], len = size[o] - lo;
                if (len > 1) {
                    (void)canon_buf_put_u32(out, len);
                    for (uint32_t i = lo; i < lo + len; ++i) {
                        (void)canon_buf_put_u32(out, points[i]);
                    }
                }
            }
        }
    }
    free(block);
    return st;
}

/* spec 9.4 rule 2: "Start K=1; repeatedly choose the lexicographically least image-array
 * g in H\K and set K <- <K,g> until K=H."  g comes from the constrained descent of
 * src/coset/least.c ("descend in point-image lexicographic order through exact constrained
 * cosets"); K is rebuilt from g_1..g_i and verified after each insertion (detailed plan WP2.7,
 * canon_bsgs_insert_verified), since its membership test decides the next descent.  The
 * generators live in a grow-only canon_perm_table; each step at least doubles |K|, so there
 * are at most log2 |H| of them. */
static canon_status rule2(canon_buf *out, const canon_bsgs *h, canon_group_bytes_stats *stats,
                          canon_coset_scratch *scratch)
{
    const uint32_t n = h->n;
    canon_status st = CANON_COMPLETE;
    uint32_t *g = canon_alloc_array(n, sizeof *g, &st);
    uint32_t *sift = canon_alloc_array(n, sizeof *sift, &st);
    canon_perm_table gens;
    canon_perm_table_init(&gens, n);
    canon_bsgs k;
    canon_bsgs_init(&k, n);
    if (g != NULL && sift != NULL) {
        st = canon_bsgs_build_verified(&k, n, NULL, 0); /* K = 1 */
    }
    while (st == CANON_COMPLETE && g != NULL && sift != NULL) {
        bool found = false;
        st = canon_coset_least_outside(h, &k, g, &found, &stats->coset, scratch);
        if (st != CANON_COMPLETE || !found) {
            break; /* K = H: the sequence is complete */
        }
        bool inserted = false;
        st = canon_bsgs_insert_verified(&k, &gens, g, sift, &inserted); /* K <- <K, g> */
        stats->k_builds += inserted;
        if (st == CANON_COMPLETE && !inserted) {
            st = CANON_INTERNAL_ERROR; /* g was chosen outside K */
        }
    }
    canon_bsgs_free(&k);
    if (st == CANON_COMPLETE) {
        stats->k = gens.count;
        /* spec 9.4: "encode 00 || U32(k) || Perm(g_1)...Perm(g_k)" */
        st = canon_buf_put_u8(out, 0x00);
        if (st == CANON_COMPLETE) {
            st = canon_buf_put_u32(out, gens.count);
        }
        for (uint32_t i = 0; i < gens.count && st == CANON_COMPLETE; ++i) {
            st = canon_perm_bytes_write(out, canon_perm_table_row(&gens, i), n);
        }
    }
    canon_perm_table_free(&gens);
    free(g);
    free(sift);
    return st;
}

canon_status canon_group_bytes_write(canon_buf *out, const canon_bsgs *h,
                                     canon_group_bytes_stats *stats, canon_coset_scratch *scratch)
{
    canon_group_bytes_stats local;
    memset(&local, 0, sizeof local);
    const size_t mark = out->len;
    /* spec 9.4: "Use this deterministic priority": rule 1 first, then rule 2 */
    bool applies = false;
    uint32_t blocks = 0;
    canon_status st = rule1(out, h, &applies, &blocks);
    if (st == CANON_COMPLETE && applies) {
        local.rule = 1;
        local.k = blocks;
    } else if (st == CANON_COMPLETE) {
        local.rule = 2;
        st = rule2(out, h, &local, scratch);
    }
    if (st != CANON_COMPLETE) {
        canon_buf_truncate(out, mark); /* all or nothing */
    }
    if (stats != NULL) {
        *stats = local;
    }
    return st;
}

canon_status canon_coset_bytes_write(canon_buf *out, const canon_bsgs *h, const uint32_t *r,
                                     uint32_t *r0, canon_group_bytes_stats *stats,
                                     canon_coset_scratch *scratch)
{
    const uint32_t n = h->n;
    const size_t mark = out->len;
    canon_group_bytes_stats local; /* every counter starts at zero on every path */
    memset(&local, 0, sizeof local);
    /* spec 9.4: "For a labeling coset H r, first choose its least image-array element r0 by
     * successive point constraints, then encode the canonical Group(H) and Perm(r0)." */
    bool found = false;
    canon_coset_stats cs = {0, 0};
    canon_status st = canon_coset_least(h, 0, r, NULL, 0, r0, &found, &cs, scratch);
    if (st == CANON_COMPLETE && !found) {
        st = CANON_INTERNAL_ERROR; /* an unconstrained coset is never empty */
    }
    if (st == CANON_COMPLETE) {
        st = canon_group_bytes_write(out, h, &local, scratch);
    }
    if (st == CANON_COMPLETE) {
        st = canon_perm_bytes_write(out, r0, n);
    }
    local.coset.descents += cs.descents;
    local.coset.rebuilds += cs.rebuilds;
    if (st != CANON_COMPLETE) {
        canon_buf_truncate(out, mark);
    }
    if (stats != NULL) {
        *stats = local;
    }
    return st;
}

bool canon_group_bytes_bound(uint32_t n, uint64_t order, bool coset, uint64_t *bound)
{
    /* rule 1: 5 + sum over non-singleton orbits of 4 + 4|O| <= 5 + 4 floor(n/2) + 4n */
    const uint64_t nn = n;
    const uint64_t r1 = 5u + 4u * (nn / 2u) + 4u * nn;
    /* rule 2: k <= floor(log2 |H|) <= floor(log2 order) Perm records of <= 4 + 8n bytes */
    uint64_t log2 = 0;
    for (uint64_t o = order; o > 1; o >>= 1) {
        ++log2;
    }
    uint64_t perm = 4u + 8u * nn, r2 = 0;
    if (!canon_u64_mul(log2, perm, &r2) || !canon_u64_add(r2, 5u, &r2)) {
        return false;
    }
    uint64_t b = r1 > r2 ? r1 : r2;
    if (coset && !canon_u64_add(b, perm, &b)) {
        return false;
    }
    *bound = b;
    return true;
}
