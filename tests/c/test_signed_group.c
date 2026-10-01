/* Tests of signed groups (slice S6, docs/slices/S6.md 3.1, 4; spec 8.4, 11.1, 17):
 *
 *  - validation by the lifted group, through the public API under both backends: the spec 7.4
 *    case <[1,0]> with sign -1 (valid), [1,0] listed twice with signs +1 and -1 (invalid), an
 *    identity generator with sign -1 (invalid, also at degree 0), zero generators, sign values
 *    other than +-1, NULL arguments;
 *  - canon_group_character on every element of every T1 group, for every sign vector on its
 *    greedy generators, against a brute-force sign table built here over the Cayley graph
 *    (a sign vector is a character iff the table is consistent; then chi is that table, and
 *    non-members are INVALID_INPUT), plus the homomorphism law and a redundant presentation
 *    (every element as a generator, signed by the table);
 *  - the lift's product rule, lift(g, s) lift(h, t) = lift(gh, st), on non-commuting elements
 *    of order 3 and 2 (spec 8.4 "swaps compose by sign multiplication"), and the backend op
 *    with caller scratch;
 *  - the n + 2 capacity check of spec 11.1, before anything is built. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "bsgs/group.h"
#include "canon/canon.h"
#include "check.h"
#include "perm/perm.h"
#include "t1_groups.h"

static canon_context *ctx_of[2]; /* chain, explicit */

static canon_status create_signed(int backend, uint32_t n, const uint32_t *gens, size_t count,
                                  const int8_t *signs, canon_group **g)
{
    return canon_group_create_signed(ctx_of[backend], n, gens, count, signs, g);
}

/* ---- spec 7.4 and the validation cases ---- */

static void validation(void)
{
    const uint32_t swap[2] = {1, 0}, twice[4] = {1, 0, 1, 0}, id2[2] = {0, 1};
    const int8_t minus = -1, plus_minus[2] = {1, -1}, plus_plus[2] = {1, 1};
    for (int b = 0; b < 2; ++b) {
        canon_group *g = (canon_group *)&ctx_of[0];
        /* spec 7.4: "n=2 empty subset with transposition character -1": valid */
        CHECK(create_signed(b, 2, swap, 1, &minus, &g) == CANON_COMPLETE && g != NULL);
        int sign = 7;
        CHECK(canon_group_character(g, swap, &sign) == CANON_COMPLETE && sign == -1);
        CHECK(canon_group_character(g, id2, &sign) == CANON_COMPLETE && sign == 1);
        uint64_t order = 0;
        CHECK(canon_group_order(g, &order) == CANON_COMPLETE && order == 2);
        canon_group_release(g);
        /* the same generator twice with both signs: the lift contains the marker swap on its
         * own (spec 8.4: "a kernel sign swap would give two signs for the same g") */
        g = (canon_group *)&ctx_of[0];
        CHECK(create_signed(b, 2, twice, 2, plus_minus, &g) == CANON_INVALID_INPUT && g == NULL);
        CHECK(create_signed(b, 2, twice, 2, plus_plus, &g) == CANON_COMPLETE);
        canon_group_release(g);
        /* an identity generator marked odd gives a forbidden projection kernel */
        CHECK(create_signed(b, 2, id2, 1, &minus, &g) == CANON_INVALID_INPUT && g == NULL);
        CHECK(create_signed(b, 0, NULL, 1, &minus, &g) == CANON_INVALID_INPUT && g == NULL);
        const int8_t plus = 1;
        CHECK(create_signed(b, 0, NULL, 1, &plus, &g) == CANON_COMPLETE);
        CHECK(canon_group_character(g, NULL, &sign) == CANON_INVALID_INPUT && sign == 0);
        canon_group_release(g);
        /* zero generators: the trivial group with the trivial character */
        CHECK(create_signed(b, 3, NULL, 0, NULL, &g) == CANON_COMPLETE);
        const uint32_t id3[3] = {0, 1, 2}, cyc[3] = {1, 2, 0};
        CHECK(canon_group_character(g, id3, &sign) == CANON_COMPLETE && sign == 1);
        CHECK(canon_group_character(g, cyc, &sign) == CANON_INVALID_INPUT && sign == 0);
        canon_group_release(g);
        /* signs must be -1 or +1; required with generators */
        const int8_t zero = 0, two = 2;
        CHECK(create_signed(b, 2, swap, 1, &zero, &g) == CANON_INVALID_INPUT && g == NULL);
        CHECK(create_signed(b, 2, swap, 1, &two, &g) == CANON_INVALID_INPUT && g == NULL);
        CHECK(create_signed(b, 2, swap, 1, NULL, &g) == CANON_INVALID_INPUT && g == NULL);
        CHECK(create_signed(b, 2, NULL, 1, &minus, &g) == CANON_INVALID_INPUT && g == NULL);
        const uint32_t bad[2] = {1, 1};
        CHECK(create_signed(b, 2, bad, 1, &minus, &g) == CANON_INVALID_INPUT && g == NULL);
        CHECK(create_signed(b, 2, swap, 1, &minus, NULL) == CANON_INVALID_INPUT);
        CHECK(canon_group_create_signed(NULL, 2, swap, 1, &minus, &g) == CANON_INVALID_INPUT);
        /* character: unsigned group, non-bijection, NULL arguments */
        CHECK(canon_group_create(ctx_of[b], 2, swap, 1, &g) == CANON_COMPLETE);
        CHECK(canon_group_character(g, swap, &sign) == CANON_UNSUPPORTED_ACTION && sign == 0);
        canon_group_release(g);
        CHECK(create_signed(b, 2, swap, 1, &minus, &g) == CANON_COMPLETE);
        CHECK(canon_group_character(g, bad, &sign) == CANON_INVALID_INPUT && sign == 0);
        CHECK(canon_group_character(g, swap, NULL) == CANON_INVALID_INPUT);
        CHECK(canon_group_character(NULL, swap, &sign) == CANON_INVALID_INPUT);
        canon_group_release(g);
    }
    /* the context's max_n is checked first (spec 11.1) */
    canon_capacity small = {1, 0, 0, 0, 0, 0, 0};
    canon_context *tiny = NULL;
    CHECK(canon_context_create(&small, &tiny) == CANON_COMPLETE);
    canon_group *g = NULL;
    CHECK(canon_group_create_signed(tiny, 2, swap, 1, &minus, &g) == CANON_CAPACITY_LIMIT);
    canon_context_release(tiny);
}

/* spec 11.1: "The initial lifted-character validator additionally requires n+2 <= 2^32-1,
 * checked before constructing its two sign points": degrees 2^32 - 1 and 2^32 - 2 are refused
 * before any generator or table is touched (no generators: nothing could be read anyway). */
static void capacity(void)
{
    canon_capacity huge = {UINT32_MAX, 0, 0, 0, 0, 0, 0};
    for (int b = 0; b < 2; ++b) {
        const canon_context_options opts = {b == 0 ? CANON_BACKEND_CHAIN : CANON_BACKEND_EXPLICIT};
        canon_context *ctx = NULL;
        CHECK(canon_context_create_with_options(&huge, &opts, &ctx) == CANON_COMPLETE);
        canon_group *g = NULL;
        CHECK(canon_group_create_signed(ctx, UINT32_MAX, NULL, 0, NULL, &g) ==
              CANON_CAPACITY_LIMIT);
        CHECK(canon_group_create_signed(ctx, UINT32_MAX - 1u, NULL, 0, NULL, &g) ==
              CANON_CAPACITY_LIMIT);
        CHECK(g == NULL);
        canon_context_release(ctx);
    }
    canon_status st = CANON_COMPLETE;
    const int8_t plus = 1;
    CHECK(canon_group_lift_generators(UINT32_MAX - 1u, NULL, 1, &plus, &st) == NULL &&
          st == CANON_CAPACITY_LIMIT);
    size_t words = 0;
    CHECK(canon_group_character_words(5, &words) && words == 14);
}

/* ---- the lift's product rule (spec 8.4) ---- */

static void product_rule(void)
{
    /* g of order 3 and h of order 2 do not commute: gh = [0,2,1] != hg = [2,1,0]; an
     * involution alone could not tell the two sides apart (S5 notes, conflict 2) */
    const uint32_t g[3] = {1, 2, 0}, h[3] = {1, 0, 2};
    uint32_t gh[3], hg[3];
    canon_perm_compose(g, h, gh, 3);
    canon_perm_compose(h, g, hg, 3);
    CHECK(gh[0] == 0 && gh[1] == 2 && gh[2] == 1 && hg[0] == 2 && hg[1] == 1 && hg[2] == 0);
    for (int s = -1; s <= 1; s += 2) {
        for (int t = -1; t <= 1; t += 2) {
            uint32_t lg[5], lh[5], prod[5], want[5];
            canon_group_lift_element(g, 3, s, lg);
            canon_group_lift_element(h, 3, t, lh);
            canon_perm_compose(lg, lh, prod, 5); /* lift(g, s) first, then lift(h, t) */
            canon_group_lift_element(gh, 3, s * t, want);
            CHECK(memcmp(prod, want, sizeof want) == 0);
            /* the markers: 3 is +, 4 is -; swapped iff s t = -1 */
            CHECK(prod[3] == (s * t < 0 ? 4u : 3u) && prod[4] == (s * t < 0 ? 3u : 4u));
        }
    }
    /* chi through the backend op with caller scratch equals the public accessor, and the
     * group of <g with sign +1, h with sign -1> is Sym(3) with the sign character */
    const uint32_t gens[6] = {1, 2, 0, 1, 0, 2};
    const int8_t signs[2] = {1, -1};
    for (int b = 0; b < 2; ++b) {
        canon_group *grp = NULL;
        CHECK(create_signed(b, 3, gens, 2, signs, &grp) == CANON_COMPLETE);
        uint32_t scratch[10];
        int a = 0, c = 0;
        CHECK(grp->ops->character(grp, gh, scratch, &a) == CANON_COMPLETE && a == -1);
        CHECK(canon_group_character(grp, hg, &c) == CANON_COMPLETE && c == -1);
        CHECK(grp->ops->character(grp, g, scratch, &a) == CANON_COMPLETE && a == 1);
        canon_group_release(grp);
    }
}

/* ---- every T1 group, every sign vector on the greedy generators ---- */

/* Brute-force sign table of the sign vector `bits` (bit k set: generator k is odd) by a
 * breadth-first walk of the Cayley graph from the identity: sign(e g_k) = sign(e) s_k.  The
 * vector defines a homomorphism iff no element receives two signs (then the table is chi).
 * Returns 1 if consistent. */
static int sign_table(const t1_sym *s, const t1_group *grp, const uint32_t *gen_index,
                      uint32_t bits, int *table)
{
    int seen[24];
    for (uint32_t e = 0; e < s->count; ++e) {
        seen[e] = 0;
        table[e] = 0;
    }
    uint32_t queue[24], head = 0, tail = 0;
    queue[tail++] = 0;
    seen[0] = 1;
    table[0] = 1;
    while (head < tail) {
        const uint32_t e = queue[head++];
        for (uint32_t k = 0; k < grp->gen_count; ++k) {
            const uint32_t f = s->mul[e][gen_index[k]];
            const int sign = table[e] * ((bits >> k & 1u) ? -1 : 1);
            if (!seen[f]) {
                seen[f] = 1;
                table[f] = sign;
                queue[tail++] = f;
            } else if (table[f] != sign) {
                return 0;
            }
        }
    }
    return 1;
}

static void t1_characters(void)
{
    uint32_t vectors = 0, characters = 0, evaluations = 0;
    for (uint32_t n = 0; n <= 4; ++n) {
        t1_sym s;
        t1_sym_init(&s, n);
        t1_group groups[T1_MAX_GROUPS];
        const uint32_t ng = t1_subgroups(&s, groups);
        for (uint32_t gi = 0; gi < ng; ++gi) {
            const t1_group *grp = &groups[gi];
            uint32_t gen_index[24];
            for (uint32_t k = 0; k < grp->gen_count; ++k) {
                for (uint32_t e = 0; e < s.count; ++e) {
                    if (n == 0 || memcmp(s.elem[e], grp->gens + k * n, n * sizeof(uint32_t)) == 0) {
                        gen_index[k] = e;
                        break;
                    }
                }
            }
            for (uint32_t bits = 0; bits < (1u << grp->gen_count); ++bits) {
                int table[24];
                const int consistent = sign_table(&s, grp, gen_index, bits, table);
                int8_t signs[8];
                for (uint32_t k = 0; k < grp->gen_count; ++k) {
                    signs[k] = (bits >> k & 1u) ? -1 : 1;
                }
                vectors += 1;
                characters += (uint32_t)consistent;
                for (int b = 0; b < 2; ++b) {
                    canon_group *g = NULL;
                    canon_status st = create_signed(b, n, grp->gens, grp->gen_count, signs, &g);
                    CHECK(st == (consistent ? CANON_COMPLETE : CANON_INVALID_INPUT));
                    if (st != CANON_COMPLETE) {
                        continue;
                    }
                    for (uint32_t e = 0; e < s.count; ++e) {
                        int sign = 7;
                        st = canon_group_character(g, s.elem[e], &sign);
                        evaluations += 1;
                        if (grp->mask >> e & 1u) {
                            CHECK(st == CANON_COMPLETE && sign == table[e]);
                        } else {
                            CHECK(st == CANON_INVALID_INPUT && sign == 0); /* not in G */
                        }
                    }
                    /* the homomorphism law on members */
                    for (uint32_t e = 0; e < s.count; ++e) {
                        for (uint32_t f = 0; f < s.count; ++f) {
                            if ((grp->mask >> e & 1u) && (grp->mask >> f & 1u)) {
                                CHECK(table[s.mul[e][f]] == table[e] * table[f]);
                            }
                        }
                    }
                    canon_group_release(g);
                    /* a redundant presentation: every element as a generator, signed by chi */
                    uint32_t all[24 * 4];
                    int8_t all_signs[24];
                    uint32_t count = 0;
                    for (uint32_t e = 0; e < s.count; ++e) {
                        if (grp->mask >> e & 1u) {
                            memcpy(all + count * n, s.elem[e], n * sizeof(uint32_t));
                            all_signs[count++] = (int8_t)table[e];
                        }
                    }
                    CHECK(create_signed(b, n, all, count, all_signs, &g) == CANON_COMPLETE);
                    canon_group_release(g);
                }
            }
        }
    }
    printf("T1 sign vectors: %u, characters %u, chi evaluations %u\n", vectors, characters,
           evaluations);
    CHECK(characters > 0 && characters < vectors);
}

int main(void)
{
    const canon_context_options chain = {CANON_BACKEND_CHAIN}, expl = {CANON_BACKEND_EXPLICIT};
    CHECK(canon_context_create_with_options(NULL, &chain, &ctx_of[0]) == CANON_COMPLETE);
    CHECK(canon_context_create_with_options(NULL, &expl, &ctx_of[1]) == CANON_COMPLETE);
    validation();
    capacity();
    product_rule();
    t1_characters();
    canon_context_release(ctx_of[0]);
    canon_context_release(ctx_of[1]);
    return check_finish("test_signed_group");
}
