/* Tests of the signed canonical image (slice S6, docs/slices/S6.md 3.3, 4; spec 3.2, 4.3, 7.3,
 * 7.4, 8.4, 17, 20):
 *
 *  - spec 7.4: n = 2 under <[1,0]> with sign -1: the empty subset is a certified zero ([1,0]
 *    fixes it), {0} gives c = {1} with s = -1, {1} gives itself with s = +1; the zero encodes
 *    as the single byte 00;
 *  - the spec 7.3 generator fast path (internal counters): any odd generator fixing x is the
 *    zero certificate (the first in input order), otherwise A = G is even and s = +1; the
 *    enumeration route finds the first odd stabiliser hit;
 *  - every T1 group, every character (every consistent sign vector on its greedy generators),
 *    every subset, under both backends, against brute force: zero iff A has an odd element;
 *    a nonzero result has c and t of the unsigned canonical image, s = chi(t), Group(A) of the
 *    stabiliser objective; covariance s(x^h) = chi(h) s(x) with c unchanged for every h in G;
 *    the cross-feed s(C(x)) = +1 (spec 20); verify_witness accepts every result;
 *  - tampered certificates (not a member, not fixing x, even, not a bijection) and a tampered
 *    nonzero witness are rejected;
 *  - graphs, validation, quota, output bound, encode and the sign accessor;
 *  - counters of the consumer on T1 (printed for docs/slices/S6-notes.md). */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "bsgs/group.h"
#include "canon/canon.h"
#include "check.h"
#include "coset/coset.h"
#include "object/object.h"
#include "perm/perm.h"
#include "search/objectives.h"
#include "search/p1_tree.h"
#include "t1_groups.h"

static canon_context *ctx_of[2]; /* chain, explicit */

static canon_group *signed_group(int b, uint32_t n, const uint32_t *gens, uint32_t count,
                                 const int8_t *signs)
{
    canon_group *g = NULL;
    CHECK(canon_group_create_signed(ctx_of[b], n, gens, count, signs, &g) == CANON_COMPLETE);
    return g;
}

static canon_object *subset_of(uint32_t n, const uint32_t *atoms, size_t count)
{
    canon_object *x = NULL;
    CHECK(canon_object_create_subset(ctx_of[0], n, atoms, count, &x) == CANON_COMPLETE);
    return x;
}

static canon_result *solve(canon_workspace *ws, const canon_group *g, const canon_object *x,
                           canon_objective objective, uint64_t quota, uint64_t max_output,
                           canon_status *st)
{
    const canon_profile profile =
        objective == CANON_OBJECTIVE_STABILISER ? CANON_PROFILE_NO_TREE : CANON_PROFILE_P1;
    /* S7: the canonical-image solves here are compared with the signed objective's P1 witness,
     * found on the UNPRUNED tree (spec 8.2: the signed objective stays unpruned), so they run
     * under the reference work policy 0x0001; the other objectives keep the context default */
    const canon_work_policy policy = objective == CANON_OBJECTIVE_CANONICAL_IMAGE
                                         ? (canon_work_policy)CANON_WORK_POLICY_REFERENCE
                                         : (canon_work_policy)0;
    canon_capacity cap = {0, 0, quota, max_output, 0, 0, 0, policy};
    canon_problem *p = NULL;
    canon_workspace *own = NULL;
    canon_result *r = NULL;
    *st = canon_problem_create(ctx_of[0], g, x, objective, profile, CANON_ENCODING_CDAG_2,
                               CANON_ORDER_CDAG_BYTE_1, &cap, &p);
    if (*st == CANON_COMPLETE) {
        if (ws == NULL) {
            CHECK(canon_workspace_create(ctx_of[0], &own) == CANON_COMPLETE);
            ws = own;
        }
        *st = canon_solve(ws, p, &r);
    }
    canon_workspace_release(own);
    canon_problem_release(p);
    return r;
}

static canon_result *solve_signed(canon_workspace *ws, const canon_group *g, const canon_object *x)
{
    canon_status st;
    canon_result *r = solve(ws, g, x, CANON_OBJECTIVE_SIGNED_CANONICAL_IMAGE, 0, 0, &st);
    CHECK(st == CANON_COMPLETE);
    return r;
}

static int sign_of(const canon_result *r)
{
    int s = 9;
    CHECK(canon_result_sign(r, &s) == CANON_COMPLETE);
    return s;
}

typedef const uint8_t *(*bytes_fn)(const canon_result *, size_t *);

/* 1 iff accessor f gives equal, present byte strings for r1 and r2 (each length is read after
 * its own call). */
static int same(bytes_fn f, const canon_result *r1, const canon_result *r2)
{
    size_t l1 = 0, l2 = 0;
    const uint8_t *b1 = f(r1, &l1);
    const uint8_t *b2 = f(r2, &l2);
    return b1 != NULL && b2 != NULL && l1 == l2 && (l1 == 0 || memcmp(b1, b2, l1) == 0);
}

static void verify(const canon_result *r, bool want)
{
    bool valid = !want;
    CHECK(canon_result_verify_witness(r, &valid) == CANON_COMPLETE && valid == want);
}

/* canon_sink_fn collecting at most 64 bytes */
typedef struct sink_buf {
    uint8_t data[64];
    size_t len;
} sink_buf;

static int collect(void *user, const uint8_t *chunk, size_t length, size_t *accepted)
{
    sink_buf *b = user;
    if (b->len + length > sizeof b->data) {
        return 2;
    }
    memcpy(b->data + b->len, chunk, length);
    b->len += length;
    *accepted = length;
    return 0;
}

/* ---- spec 7.4 ---- */

static void golden(void)
{
    const uint32_t swap[2] = {1, 0}, zero = 0, one = 1;
    const int8_t minus = -1;
    canon_object *empty = subset_of(2, NULL, 0), *x0 = subset_of(2, &zero, 1),
                 *x1 = subset_of(2, &one, 1);
    for (int b = 0; b < 2; ++b) {
        canon_group *g = signed_group(b, 2, swap, 1, &minus);
        /* "For n=2 empty subset with transposition character -1, [1,0] fixes x and certifies
         * signed zero." */
        canon_result *r = solve_signed(NULL, g, empty);
        canon_result_flags f = canon_result_get_flags(r);
        CHECK(f.zero_certified && f.witness_valid && !f.nonzero_certified && !f.image_canonical &&
              !f.subgroup_verified && !f.stabiliser_complete && !f.encoding_complete &&
              !f.minimum_proved && !f.transport_exhausted);
        CHECK(sign_of(r) == 0);
        uint32_t deg = 0;
        const uint32_t *a = canon_result_witness(r, &deg);
        CHECK(a != NULL && deg == 2 && a[0] == 1 && a[1] == 0);
        size_t len = 7;
        CHECK(canon_result_bytes(r, &len) == NULL && len == 0); /* "no monomial stream" */
        CHECK(canon_result_trace(r, &len) == NULL && canon_result_group_bytes(r, &len) == NULL);
        CHECK(canon_result_labeling(r, &deg) == NULL && deg == 0);
        sink_buf sb = {{0}, 0};
        CHECK(canon_result_encode(r, collect, &sb) == CANON_COMPLETE);
        CHECK(sb.len == 1 && sb.data[0] == 0x00); /* spec 4.3: "distinguished payload byte 00" */
        verify(r, true);
        canon_result_release(r);
        /* "For x={0} in the same signed group, A=1, P1 returns {1} with sign -1" */
        r = solve_signed(NULL, g, x0);
        f = canon_result_get_flags(r);
        CHECK(f.nonzero_certified && f.image_canonical && f.witness_valid && f.subgroup_verified &&
              f.stabiliser_complete && f.encoding_complete && !f.zero_certified);
        CHECK(sign_of(r) == -1);
        const uint8_t *c = canon_result_bytes(r, &len);
        CHECK(c != NULL && check_hex_is(c, len,
                                        "434e0200010001 00000002 00000002 01 00000001 "
                                        "04 00000001 00000000 00000001"));
        const uint8_t *tr = canon_result_trace(r, &len);
        CHECK(tr != NULL && check_hex_is(tr, len,
                                         "10 00000000 20 00000002 00000001 00000001 21 "
                                         "00000002 00000001 00000001 00"));
        const uint8_t *gb = canon_result_group_bytes(r, &len);
        CHECK(gb != NULL && check_hex_is(gb, len, "01 00000000")); /* Group(A), A = 1 */
        a = canon_result_witness(r, &deg);
        CHECK(a != NULL && a[0] == 1 && a[1] == 0);
        sb.len = 0;
        CHECK(canon_result_encode(r, collect, &sb) == CANON_COMPLETE);
        c = canon_result_bytes(r, &len);
        CHECK(c != NULL && sb.len == len && memcmp(sb.data, c, len) == 0 && sb.data[0] == 0x43);
        verify(r, true);
        canon_result_release(r);
        /* "{1} returns itself with sign +1" */
        r = solve_signed(NULL, g, x1);
        CHECK(sign_of(r) == 1);
        c = canon_result_bytes(r, &len);
        CHECK(c != NULL && check_hex_is(c, len,
                                        "434e0200010001 00000002 00000002 01 00000001 "
                                        "04 00000001 00000000 00000001"));
        a = canon_result_witness(r, &deg);
        CHECK(a != NULL && a[0] == 0 && a[1] == 1);
        verify(r, true);
        canon_result_release(r);
        canon_group_release(g);
    }
    canon_object_release(empty);
    canon_object_release(x0);
    canon_object_release(x1);
}

/* ---- the fast path and the enumeration route, through the internal consumer ---- */

static void run_internal(const canon_group *g, const canon_root *x, canon_obj_outcome *o,
                         canon_obj_stats *stats, uint32_t *w)
{
    canon_obj_search s;
    canon_p1_search p1;
    canon_obj_search_init(&s);
    canon_p1_search_init(&p1);
    CHECK(canon_obj_signed(&s, &p1, g, x, 1u << 20, o) == CANON_COMPLETE);
    *stats = s.stats;
    if (x->n > 0) {
        memcpy(w, s.best, x->n * sizeof *w);
    }
    canon_obj_search_free(&s);
    canon_p1_search_free(&p1);
}

static void paths(void)
{
    /* G = <(0 1) with sign -1, (2 3) with sign +1> on 4 points */
    const uint32_t gens[8] = {1, 0, 2, 3, 0, 1, 3, 2};
    const int8_t signs[2] = {-1, 1}, signs2[2] = {1, -1};
    for (int b = 0; b < 2; ++b) {
        canon_group *g = signed_group(b, 4, gens, 2, signs);
        canon_group *g2 = signed_group(b, 4, gens, 2, signs2);
        canon_root x;
        canon_obj_outcome o;
        canon_obj_stats st;
        uint32_t w[4];
        const uint32_t none[1] = {0}, three[3] = {0, 1, 2}, two[1] = {2}, zero[1] = {0};
        /* the empty set is fixed by both generators: fast path, the first odd generator */
        memset(&x, 0, sizeof x);
        x.kind = CANON_ROOT_SUBSET;
        x.n = 4;
        CHECK(canon_subset_init(&x.u.subset, 4, none, 0) == CANON_COMPLETE);
        run_internal(g, &x, &o, &st, w);
        CHECK(o.flags.zero_certified && o.sign == 0 && st.fast_path == 1 && st.p1_nodes == 0);
        CHECK(memcmp(w, gens, sizeof w) == 0);
        run_internal(g2, &x, &o, &st, w); /* the odd generator is the second */
        CHECK(o.flags.zero_certified && st.fast_path == 1 && memcmp(w, gens + 4, sizeof w) == 0);
        canon_subset_free(&x.u.subset);
        /* {0,1,2}: (0 1) fixes it, (2 3) does not: the enumeration route finds (0 1), odd */
        CHECK(canon_subset_init(&x.u.subset, 4, three, 3) == CANON_COMPLETE);
        run_internal(g, &x, &o, &st, w);
        CHECK(o.flags.zero_certified && st.fast_path == 0 && st.p1_nodes == 0 && st.hits >= 1);
        CHECK(memcmp(w, gens, sizeof w) == 0);
        /* under the other character (0 1) is even and A = <(0 1)> is even: nonzero */
        run_internal(g2, &x, &o, &st, w);
        CHECK(o.flags.nonzero_certified && st.fast_path == 0 && st.p1_nodes > 0);
        canon_subset_free(&x.u.subset);
        /* {2}: (0 1) fixes it: A = <(0 1)> odd under the first character */
        CHECK(canon_subset_init(&x.u.subset, 4, two, 1) == CANON_COMPLETE);
        run_internal(g, &x, &o, &st, w);
        CHECK(o.flags.zero_certified && st.fast_path == 0);
        canon_subset_free(&x.u.subset);
        /* {0}: A = <(2 3)> even under the first character; c = {3}? the P1 image, s = chi(t) */
        CHECK(canon_subset_init(&x.u.subset, 4, zero, 1) == CANON_COMPLETE);
        run_internal(g, &x, &o, &st, w);
        CHECK(o.flags.nonzero_certified && st.fast_path == 0 && st.characters >= 2);
        int chi = 0;
        CHECK(canon_group_character(g, w, &chi) == CANON_COMPLETE && chi == o.sign);
        canon_subset_free(&x.u.subset);
        /* all generators fix x and are even: the fast path's nonzero branch, s = +1, c = x, and
         * the trace is still the prescribed P1 trace (p1_nodes > 0) */
        const uint32_t sym01[4] = {1, 0, 2, 3};
        const int8_t plus = 1;
        canon_group *h = signed_group(b, 4, sym01, 1, &plus);
        CHECK(canon_subset_init(&x.u.subset, 4, two, 1) == CANON_COMPLETE);
        run_internal(h, &x, &o, &st, w);
        CHECK(o.flags.nonzero_certified && o.sign == 1 && st.fast_path == 1 && st.p1_nodes > 0);
        canon_subset_free(&x.u.subset);
        canon_group_release(h);
        canon_group_release(g);
        canon_group_release(g2);
    }
}

/* ---- every T1 group, character and subset ---- */

static int consistent_table(const t1_sym *s, const t1_group *grp, const uint32_t *gen_index,
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

static canon_object *image_of(const t1_sym *s, uint32_t mask, uint32_t e, uint32_t *image_mask)
{
    uint32_t atoms[4], k = 0, m = 0;
    for (uint32_t a = 0; a < s->n; ++a) {
        if (mask >> a & 1u) {
            atoms[k++] = s->elem[e][a];
            m |= 1u << s->elem[e][a];
        }
    }
    *image_mask = m;
    return subset_of(s->n, atoms, k);
}

/* The zero certificate the reference procedure must return, computed independently of the
 * consumer: on the spec 7.3 fast path (every generator fixes x) the first odd generator in
 * input order; otherwise the first leaf of the group's spec 8.1 enumeration (its own
 * `enumerate` op, the order S4 pinned against a model of the spec text) that fixes x and is odd
 * by the brute-force sign table.  Returns 0 if there is none. */
typedef struct first_odd {
    const t1_sym *s;
    const t1_group *grp;
    uint32_t mask;
    const int *chi;
    uint32_t *out;
    int found;
} first_odd;

static canon_status consume_first_odd(void *user, const uint32_t *r, bool *stop)
{
    first_odd *f = user;
    uint32_t m = 0, e = 0;
    for (uint32_t a = 0; a < f->s->n; ++a) {
        m |= (f->mask >> a & 1u) << r[a];
    }
    while (e < f->s->count && f->s->n > 0 && memcmp(f->s->elem[e], r, f->s->n * 4u) != 0) {
        ++e;
    }
    if (m == f->mask && f->chi[e] < 0) {
        memcpy(f->out, r, f->s->n * sizeof *r);
        f->found = 1;
        *stop = true;
    }
    return CANON_COMPLETE;
}

static int expected_certificate(const t1_sym *s, const t1_group *grp, const canon_group *g,
                                uint32_t mask, const int *chi, uint32_t *out)
{
    const uint32_t n = s->n;
    int fixes_all = 1;
    for (uint32_t k = 0; k < grp->gen_count; ++k) {
        uint32_t m = 0;
        for (uint32_t a = 0; a < n; ++a) {
            m |= (mask >> a & 1u) << grp->gens[k * n + a];
        }
        fixes_all &= m == mask;
    }
    if (fixes_all) {
        for (uint32_t k = 0; k < grp->gen_count; ++k) {
            uint32_t e = 0;
            while (e < s->count && n > 0 && memcmp(s->elem[e], grp->gens + k * n, n * 4u) != 0) {
                ++e;
            }
            if (chi[e] < 0) {
                memcpy(out, grp->gens + k * n, n * sizeof *out);
                return 1;
            }
        }
        return 0;
    }
    first_odd f = {s, grp, mask, chi, out, 0};
    canon_coset_visitor v;
    canon_coset_visitor_init(&v, consume_first_odd, NULL, &f, 1u << 20);
    CHECK(g->ops->enumerate(g, &v) == CANON_COMPLETE);
    return f.found;
}

static void t1_tier(void)
{
    uint32_t cases = 0, zeros = 0, covariances = 0, certificates = 0;
    canon_workspace *ws = NULL;
    CHECK(canon_workspace_create(ctx_of[0], &ws) == CANON_COMPLETE);
    for (uint32_t n = 0; n <= 4; ++n) {
        t1_sym s;
        t1_sym_init(&s, n);
        t1_group groups[T1_MAX_GROUPS];
        const uint32_t ng = t1_subgroups(&s, groups);
        for (uint32_t gi = 0; gi < ng; ++gi) {
            const t1_group *grp = &groups[gi];
            uint32_t gen_index[24];
            for (uint32_t k = 0; k < grp->gen_count; ++k) {
                gen_index[k] = 0;
                for (uint32_t e = 0; e < s.count; ++e) {
                    if (n == 0 || memcmp(s.elem[e], grp->gens + k * n, n * sizeof(uint32_t)) == 0) {
                        gen_index[k] = e;
                        break;
                    }
                }
            }
            for (uint32_t bits = 0; bits < (1u << grp->gen_count); ++bits) {
                int chi[24];
                if (!consistent_table(&s, grp, gen_index, bits, chi)) {
                    continue;
                }
                int8_t signs[8];
                for (uint32_t k = 0; k < grp->gen_count; ++k) {
                    signs[k] = (bits >> k & 1u) ? -1 : 1;
                }
                for (int b = 0; b < 2; ++b) {
                    canon_group *g = signed_group(b, n, grp->gens, grp->gen_count, signs);
                    canon_group *u = NULL; /* the same group, unsigned */
                    CHECK(canon_group_create(ctx_of[b], n, grp->gens, grp->gen_count, &u) ==
                          CANON_COMPLETE);
                    for (uint32_t mask = 0; mask < (1u << n); ++mask) {
                        uint32_t m0 = 0;
                        canon_object *x = image_of(&s, mask, 0, &m0); /* x itself */
                        /* brute force: zero iff an odd element of G fixes x */
                        int odd = 0;
                        for (uint32_t e = 0; e < s.count; ++e) {
                            uint32_t m = 0;
                            canon_object *y = NULL;
                            if (!(grp->mask >> e & 1u)) {
                                continue;
                            }
                            y = image_of(&s, mask, e, &m);
                            canon_object_release(y);
                            odd |= m == mask && chi[e] < 0;
                        }
                        canon_result *r = solve_signed(ws, g, x);
                        const int sx = sign_of(r);
                        CHECK((sx == 0) == (odd != 0));
                        verify(r, true);
                        uint32_t deg = 0;
                        const uint32_t *w = canon_result_witness(r, &deg);
                        canon_status st;
                        if (sx == 0) {
                            zeros += 1;
                            /* the certificate is an odd automorphism of x */
                            uint32_t e = 0;
                            while (e < s.count && (n > 0 && memcmp(s.elem[e], w, n * 4u) != 0)) {
                                ++e;
                            }
                            CHECK(e < s.count && (grp->mask >> e & 1u) && chi[e] == -1);
                            /* and it is the expected one (S6 review item 1: chi is evaluated
                             * only on hits that enlarge A_known, which must not change it) */
                            uint32_t want[4];
                            CHECK(expected_certificate(&s, grp, g, mask, chi, want));
                            CHECK(n == 0 || memcmp(want, w, n * 4u) == 0);
                            certificates += 1;
                        } else {
                            /* c and t of the unsigned canonical image; s = chi(t); Group(A) */
                            canon_result *c =
                                solve(ws, u, x, CANON_OBJECTIVE_CANONICAL_IMAGE, 0, 0, &st);
                            canon_result *a =
                                solve(ws, u, x, CANON_OBJECTIVE_STABILISER, 0, 0, &st);
                            CHECK(same(canon_result_bytes, r, c) && same(canon_result_trace, r, c));
                            CHECK(same(canon_result_group_bytes, r, a));
                            const uint32_t *tc = canon_result_witness(c, &deg);
                            CHECK(n == 0 || (tc != NULL && memcmp(tc, w, n * 4u) == 0));
                            uint32_t e = 0;
                            while (e < s.count && n > 0 && memcmp(s.elem[e], w, n * 4u) != 0) {
                                ++e;
                            }
                            CHECK(e < s.count && sx == chi[e]);
                            canon_result_release(a);
                            /* cross-feed (spec 20): s(C(x)) = +1 with C(C(x)) = C(x) */
                            size_t len = 0;
                            const uint8_t *cb = canon_result_bytes(r, &len);
                            canon_object *cx = NULL;
                            CHECK(canon_object_create(ctx_of[0], CANON_SCHEMA_EXT_DAG_1,
                                                      CANON_ACTION_ATOM_TRANSPORT_1, n, cb, len,
                                                      &cx) == CANON_COMPLETE);
                            canon_result *rc = solve_signed(ws, g, cx);
                            CHECK(sign_of(rc) == 1 && same(canon_result_bytes, rc, r));
                            canon_result_release(rc);
                            canon_object_release(cx);
                            canon_result_release(c);
                        }
                        /* covariance (spec 8.4): s(x^h) = chi(h) s(x), c unchanged */
                        for (uint32_t h = 0; h < s.count; ++h) {
                            if (!(grp->mask >> h & 1u)) {
                                continue;
                            }
                            uint32_t m = 0;
                            canon_object *xh = image_of(&s, mask, h, &m);
                            canon_result *rh = solve_signed(ws, g, xh);
                            CHECK(sign_of(rh) == chi[h] * sx);
                            if (sx != 0) {
                                CHECK(same(canon_result_bytes, rh, r));
                            }
                            canon_result_release(rh);
                            canon_object_release(xh);
                            covariances += 1;
                        }
                        canon_result_release(r);
                        canon_object_release(x);
                        cases += 1;
                    }
                    canon_group_release(u);
                    canon_group_release(g);
                }
            }
        }
    }
    canon_workspace_release(ws);
    printf("T1 signed: %u cases (%u zero, %u certificates pinned), %u covariance solves (both "
           "backends)\n",
           cases, zeros, certificates, covariances);
    CHECK(zeros > 0 && zeros < cases && certificates == zeros);
}

/* ---- tampered certificates ---- */

static void tampered(void)
{
    /* G = <(0 1) odd, (2 3) even> on 4 points, x = {0,1}: zero, certificate (0 1) */
    const uint32_t gens[8] = {1, 0, 2, 3, 0, 1, 3, 2}, both[2] = {0, 1}, zero = 0;
    const int8_t signs[2] = {-1, 1};
    canon_group *g = signed_group(0, 4, gens, 2, signs);
    canon_object *x = subset_of(4, both, 2);
    canon_result *r = solve_signed(NULL, g, x);
    CHECK(sign_of(r) == 0);
    verify(r, true);
    uint32_t deg = 0;
    uint32_t *a = (uint32_t *)(uintptr_t)canon_result_witness(r, &deg); /* owned storage */
    uint32_t saved[4];
    memcpy(saved, a, sizeof saved);
    const uint32_t id[4] = {0, 1, 2, 3};      /* a member fixing x, but even */
    const uint32_t even23[4] = {0, 1, 3, 2};  /* (2 3): a member fixing x, but even */
    const uint32_t outside[4] = {2, 1, 0, 3}; /* (0 2): not a member of G */
    const uint32_t odd[4] = {1, 0, 3, 2};     /* (0 1)(2 3): odd and fixes x, also a certificate */
    const uint32_t bad[4] = {0, 0, 1, 2};     /* not a bijection */
    memcpy(a, id, sizeof saved);
    verify(r, false);
    memcpy(a, even23, sizeof saved);
    verify(r, false);
    memcpy(a, outside, sizeof saved);
    verify(r, false);
    memcpy(a, odd, sizeof saved);
    verify(r, true);
    memcpy(a, bad, sizeof saved);
    verify(r, false);
    memcpy(a, saved, sizeof saved);
    verify(r, true);
    canon_result_release(r);
    canon_object_release(x);
    /* x = {0}: A = <(2 3)>, nonzero; (2 3) t (same image, same sign) is accepted, a member
     * sending x elsewhere is rejected */
    x = subset_of(4, &zero, 1);
    r = solve_signed(NULL, g, x);
    CHECK(sign_of(r) != 0);
    verify(r, true);
    uint32_t *t = (uint32_t *)(uintptr_t)canon_result_witness(r, &deg);
    memcpy(saved, t, sizeof saved);
    uint32_t t23[4];
    canon_perm_compose(gens + 4, saved, t23, 4); /* (2 3) t: same image of x, same sign */
    memcpy(t, t23, sizeof t23);
    verify(r, true);
    /* t (0 1): a member of G, but c = {0} or {1} is moved by (0 1), so x^(t (0 1)) != c */
    uint32_t t01[4];
    canon_perm_compose(saved, gens, t01, 4);
    memcpy(t, t01, sizeof t01);
    verify(r, false);
    memcpy(t, saved, sizeof saved);
    verify(r, true);
    canon_result_release(r);
    canon_object_release(x);
    canon_group_release(g);
}

/* ---- graphs, validation, quota, output bound ---- */

static void graphs_and_errors(void)
{
    const uint32_t swap[2] = {1, 0}, zero = 0;
    const int8_t minus = -1;
    const canon_arc arc01 = {0, 1, NULL, 0, 1}, arcs2[2] = {{0, 1, NULL, 0, 1}, {1, 0, NULL, 0, 1}};
    canon_object *one = NULL, *two = NULL;
    CHECK(canon_object_create_graph(ctx_of[0], 2, NULL, NULL, &arc01, 1, &one) == CANON_COMPLETE);
    CHECK(canon_object_create_graph(ctx_of[0], 2, NULL, NULL, arcs2, 2, &two) == CANON_COMPLETE);
    for (int b = 0; b < 2; ++b) {
        canon_group *g = signed_group(b, 2, swap, 1, &minus);
        /* spec 7.4 graph: the one arc 0 -> 1 is moved by (0 1); A = 1; c = the arc 1 -> 0,
         * t = [1,0], s = -1 */
        canon_result *r = solve_signed(NULL, g, one);
        CHECK(sign_of(r) == -1);
        size_t len = 0;
        const uint8_t *c = canon_result_bytes(r, &len);
        CHECK(c != NULL && check_hex_is(c, len,
                                        "434e0200010001 00000002 00000001 09 00000000 "
                                        "00000000 00000001 00000001 00000000 00000000 "
                                        "00000001 01 00000000"));
        verify(r, true);
        canon_result_release(r);
        /* the 2-cycle is fixed by (0 1): fast-path zero */
        r = solve_signed(NULL, g, two);
        CHECK(sign_of(r) == 0);
        verify(r, true);
        canon_result_release(r);
        canon_group_release(g);
    }
    canon_object_release(one);
    canon_object_release(two);

    canon_group *g = signed_group(0, 2, swap, 1, &minus);
    canon_group *u = NULL;
    CHECK(canon_group_create(ctx_of[0], 2, swap, 1, &u) == CANON_COMPLETE);
    canon_object *x = subset_of(2, &zero, 1);
    canon_problem *p = NULL;
    const canon_objective S = CANON_OBJECTIVE_SIGNED_CANONICAL_IMAGE;
    /* an unsigned group, NO_TREE, a deterministic witness, a rho: refused */
    CHECK(canon_problem_create(ctx_of[0], u, x, S, CANON_PROFILE_P1, CANON_ENCODING_CDAG_2,
                               CANON_ORDER_CDAG_BYTE_1, NULL, &p) == CANON_UNSUPPORTED_ACTION);
    CHECK(canon_problem_create(ctx_of[0], g, x, S, CANON_PROFILE_NO_TREE, CANON_ENCODING_CDAG_2,
                               CANON_ORDER_CDAG_BYTE_1, NULL, &p) == CANON_UNSUPPORTED_ACTION);
    const canon_problem_options det = {CANON_WITNESS_DETERMINISTIC, NULL},
                                with_rho = {CANON_WITNESS_ANY, swap};
    CHECK(canon_problem_create_with_options(ctx_of[0], g, x, NULL, S, CANON_PROFILE_P1,
                                            CANON_ENCODING_CDAG_2, CANON_ORDER_CDAG_BYTE_1, NULL,
                                            &det, &p) == CANON_UNSUPPORTED_ACTION);
    CHECK(canon_problem_create_with_options(ctx_of[0], g, x, NULL, S, CANON_PROFILE_P1,
                                            CANON_ENCODING_CDAG_2, CANON_ORDER_CDAG_BYTE_1, NULL,
                                            &with_rho, &p) == CANON_INVALID_INPUT);
    CHECK(p == NULL);
    /* a signed group serves the other objectives, which ignore the signs */
    canon_status st;
    canon_result *r1 = solve(NULL, g, x, CANON_OBJECTIVE_CANONICAL_IMAGE, 0, 0, &st);
    canon_result *r2 = solve(NULL, u, x, CANON_OBJECTIVE_CANONICAL_IMAGE, 0, 0, &st);
    CHECK(same(canon_result_bytes, r1, r2));
    int sign = 4;
    CHECK(canon_result_sign(r1, &sign) == CANON_INVALID_INPUT && sign == 0);
    CHECK(canon_result_sign(NULL, &sign) == CANON_INVALID_INPUT);
    CHECK(canon_result_sign(r1, NULL) == CANON_INVALID_INPUT);
    canon_result_release(r1);
    canon_result_release(r2);
    /* quota (spec 11.1): x = {0} under <[1,0]>: 3 enumeration visits (root and two leaves)
     * then 1 P1 NODE token */
    r1 = solve(NULL, g, x, S, 4, 0, &st);
    CHECK(st == CANON_COMPLETE);
    canon_result_release(r1);
    for (uint64_t q = 1; q <= 3; ++q) {
        r1 = solve(NULL, g, x, S, q, 0, &st);
        CHECK(st == CANON_CAPACITY_LIMIT && canon_result_status(r1) == CANON_CAPACITY_LIMIT);
        CHECK(canon_result_sign(r1, &sign) == CANON_INVALID_INPUT);
        sink_buf sb = {{0}, 0};
        CHECK(canon_result_encode(r1, collect, &sb) == CANON_INVALID_INPUT && sb.len == 0);
        canon_result_release(r1);
    }
    /* the empty set: fast-path zero, no traversal, so even a quota of 1 completes */
    canon_object *empty = subset_of(2, NULL, 0);
    r1 = solve(NULL, g, empty, S, 1, 0, &st);
    CHECK(st == CANON_COMPLETE && sign_of(r1) == 0);
    canon_result_release(r1);
    /* output bound: the exact stream length (33 bytes for one atom) */
    r1 = solve(NULL, g, x, S, 0, 33, &st);
    CHECK(st == CANON_COMPLETE);
    canon_result_release(r1);
    r1 = solve(NULL, g, x, S, 0, 32, &st);
    CHECK(st == CANON_CAPACITY_LIMIT && r1 == NULL);
    canon_object_release(empty);
    canon_object_release(x);
    canon_group_release(g);
    canon_group_release(u);
}

/* Counters of the signed consumer on every T1 group, character and subset (chain backend;
 * printed for docs/slices/S6-notes.md). */
static void counters(void)
{
    uint64_t cases = 0, fast = 0, zeros = 0, visits = 0, hits = 0, chis = 0, p1 = 0, builds = 0;
    for (uint32_t n = 0; n <= 4; ++n) {
        t1_sym s;
        t1_sym_init(&s, n);
        t1_group groups[T1_MAX_GROUPS];
        const uint32_t ng = t1_subgroups(&s, groups);
        for (uint32_t gi = 0; gi < ng; ++gi) {
            const t1_group *grp = &groups[gi];
            uint32_t gen_index[24];
            for (uint32_t k = 0; k < grp->gen_count; ++k) {
                gen_index[k] = 0;
                for (uint32_t e = 0; e < s.count; ++e) {
                    if (n == 0 || memcmp(s.elem[e], grp->gens + k * n, n * sizeof(uint32_t)) == 0) {
                        gen_index[k] = e;
                        break;
                    }
                }
            }
            for (uint32_t bits = 0; bits < (1u << grp->gen_count); ++bits) {
                int chi[24];
                if (!consistent_table(&s, grp, gen_index, bits, chi)) {
                    continue;
                }
                int8_t signs[8];
                for (uint32_t k = 0; k < grp->gen_count; ++k) {
                    signs[k] = (bits >> k & 1u) ? -1 : 1;
                }
                canon_group *g = signed_group(0, n, grp->gens, grp->gen_count, signs);
                for (uint32_t mask = 0; mask < (1u << n); ++mask) {
                    uint32_t atoms[4], k = 0;
                    for (uint32_t a = 0; a < n; ++a) {
                        if (mask >> a & 1u) {
                            atoms[k++] = a;
                        }
                    }
                    canon_root x;
                    memset(&x, 0, sizeof x);
                    x.kind = CANON_ROOT_SUBSET;
                    x.n = n;
                    CHECK(canon_subset_init(&x.u.subset, n, atoms, k) == CANON_COMPLETE);
                    canon_obj_outcome o;
                    canon_obj_stats st;
                    uint32_t w[4];
                    run_internal(g, &x, &o, &st, w);
                    cases += 1;
                    fast += st.fast_path;
                    zeros += o.flags.zero_certified;
                    visits += st.nodes;
                    hits += st.hits;
                    chis += st.characters;
                    p1 += st.p1_nodes;
                    builds += st.stab_builds;
                    canon_subset_free(&x.u.subset);
                }
                canon_group_release(g);
            }
        }
    }
    printf("T1 signed consumer (%llu runs): fast path %llu, zero %llu, enumeration visits %llu, "
           "stabiliser hits %llu, chi evaluations %llu, P1 nodes %llu, verified A rebuilds %llu\n",
           (unsigned long long)cases, (unsigned long long)fast, (unsigned long long)zeros,
           (unsigned long long)visits, (unsigned long long)hits, (unsigned long long)chis,
           (unsigned long long)p1, (unsigned long long)builds);
}

int main(void)
{
    const canon_context_options chain = {CANON_BACKEND_CHAIN}, expl = {CANON_BACKEND_EXPLICIT};
    CHECK(canon_context_create_with_options(NULL, &chain, &ctx_of[0]) == CANON_COMPLETE);
    CHECK(canon_context_create_with_options(NULL, &expl, &ctx_of[1]) == CANON_COMPLETE);
    golden();
    paths();
    t1_tier();
    tampered();
    graphs_and_errors();
    counters();
    canon_context_release(ctx_of[0]);
    canon_context_release(ctx_of[1]);
    return check_finish("test_signed");
}
