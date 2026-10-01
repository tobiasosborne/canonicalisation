/* Unit tests for src/perm (spec section 3 conventions, section 7.4 convention vectors). */
#include <stdint.h>
#include <string.h>

#include "check.h"
#include "perm/perm.h"

#define MAXN 12

static void random_perm(uint32_t *p, uint32_t n)
{
    for (uint32_t v = 0; v < n; ++v) {
        p[v] = v;
    }
    for (uint32_t i = n; i > 1; --i) { /* Fisher-Yates */
        uint32_t j = (uint32_t)(check_rng() % i);
        uint32_t tmp = p[i - 1];
        p[i - 1] = p[j];
        p[j] = tmp;
    }
}

int main(void)
{
    /* spec 7.4 public convention vectors: p=[1,0,2], q=[0,2,1]: pq=[2,0,1], qp=[1,2,0],
     * p^-1 = p, (0,2)^(pq) = (2,1). */
    const uint32_t p[3] = {1, 0, 2};
    const uint32_t q[3] = {0, 2, 1};
    uint32_t out[MAXN];
    canon_perm_compose(p, q, out, 3);
    CHECK(out[0] == 2 && out[1] == 0 && out[2] == 1);
    uint32_t pq[3];
    memcpy(pq, out, sizeof pq);
    canon_perm_compose(q, p, out, 3);
    CHECK(out[0] == 1 && out[1] == 2 && out[2] == 0);
    canon_perm_inverse(p, out, 3);
    CHECK(out[0] == 1 && out[1] == 0 && out[2] == 2);
    const uint32_t tup[2] = {0, 2};
    canon_perm_apply_tuple(pq, tup, 2, out);
    CHECK(out[0] == 2 && out[1] == 1);
    /* (x^p)^q = x^(pq) on the tuple. */
    uint32_t mid[2];
    canon_perm_apply_tuple(p, tup, 2, mid);
    canon_perm_apply_tuple(q, mid, 2, out);
    CHECK(out[0] == 2 && out[1] == 1);

    /* lex_compare, is_identity, validate. */
    const uint32_t id3[3] = {0, 1, 2};
    CHECK(canon_perm_lex_compare(id3, p, 3) < 0);
    CHECK(canon_perm_lex_compare(p, id3, 3) > 0);
    CHECK(canon_perm_lex_compare(p, p, 3) == 0);
    CHECK(canon_perm_lex_compare(NULL, NULL, 0) == 0);
    CHECK(canon_perm_is_identity(id3, 3));
    CHECK(!canon_perm_is_identity(p, 3));
    CHECK(canon_perm_is_identity(NULL, 0));
    CHECK(canon_perm_validate(p, 3) == 1);
    CHECK(canon_perm_validate(NULL, 0) == 1);
    const uint32_t dup[3] = {0, 0, 2};
    const uint32_t range[3] = {0, 1, 3};
    CHECK(canon_perm_validate(dup, 3) == 0);
    CHECK(canon_perm_validate(range, 3) == 0);
    const uint32_t big[1] = {UINT32_MAX};
    CHECK(canon_perm_validate(big, 1) == 0);

    /* Random: compose(p, p^-1) = id, compose(p^-1, p) = id,
     * (pq)^-1 = q^-1 p^-1 (inversion reverses factors, spec section 3). */
    for (int trial = 0; trial < 500; ++trial) {
        uint32_t n = (uint32_t)(check_rng() % (MAXN + 1));
        uint32_t a[MAXN], b[MAXN], ai[MAXN], bi[MAXN], ab[MAXN], abi[MAXN], rhs[MAXN];
        random_perm(a, n);
        random_perm(b, n);
        CHECK(canon_perm_validate(a, n) == 1);
        canon_perm_inverse(a, ai, n);
        canon_perm_compose(a, ai, out, n);
        CHECK(canon_perm_is_identity(out, n));
        canon_perm_compose(ai, a, out, n);
        CHECK(canon_perm_is_identity(out, n));
        canon_perm_inverse(b, bi, n);
        canon_perm_compose(a, b, ab, n);
        canon_perm_inverse(ab, abi, n);
        canon_perm_compose(bi, ai, rhs, n);
        CHECK(canon_perm_lex_compare(abi, rhs, n) == 0);
    }
    return check_finish("test_perm");
}
