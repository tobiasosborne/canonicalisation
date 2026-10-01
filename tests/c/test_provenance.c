/* Tests for straight-line provenance (src/bsgs/provenance.c; spec 9.1, 9.2; docs/slices/S3.md
 * 2.2 item 6, 3): the record kinds and their product side; re-derivation of every strong
 * generator and inverse of built chains from their records equals the stored arrays; and the
 * DAG size bound.
 *
 * Bound.  Records are created only for kept inputs (one INPUT each) and for inserted
 * generators.  An inserted Schreier residue t_b s t_(b^s)^-1 sifted through deeper levels
 * costs at most (r_i - 2) + 1 + (r_i - 1) PRODUCT records for its Schreier word (tree paths
 * have at most r_i - 1 edges), at most r_j - 1 for each sift step at level j > i, and one
 * INVERSE record; an inserted input residue costs at most the sift steps plus the INVERSE.
 * Orbits only grow during construction, so with the final orbit lengths r_j:
 *     nodes <= kept_inputs + insertions * (2 n + sum_j r_j + 1),
 * a polynomial bound (sum_j r_j <= n * depth).  The measured maximum of
 * nodes / (insertions * n) is printed for docs/slices/S3-notes.md. */
#include <stdlib.h>
#include <string.h>

#include "bsgs/chain.h"
#include "bsgs/provenance.h"
#include "check.h"
#include "perm/perm.h"
#include "t1_groups.h"

static int eq(const uint32_t *a, const uint32_t *b, uint32_t n)
{
    return n == 0 || memcmp(a, b, n * sizeof *a) == 0;
}

static void records(void)
{
    /* inputs: the 3-cycle c = [1,2,0] and the transposition tau = (1 2) = [0,2,1] */
    canon_perm_table in;
    canon_perm_table_init(&in, 3);
    const uint32_t c[3] = {1, 2, 0}, tau[3] = {0, 2, 1};
    uint32_t row = 0;
    CHECK(canon_perm_table_push(&in, c, &row) == CANON_COMPLETE && row == 0);
    CHECK(canon_perm_table_push(&in, tau, &row) == CANON_COMPLETE && row == 1);
    canon_provenance p;
    canon_prov_init(&p);
    uint32_t a = 0, b = 0, ab = 0, ba = 0, ia = 0, same = 0;
    CHECK(canon_prov_input(&p, 0, &a) == CANON_COMPLETE && a == 0);
    CHECK(canon_prov_input(&p, 1, &b) == CANON_COMPLETE && b == 1);
    CHECK(canon_prov_product(&p, a, b, &ab) == CANON_COMPLETE);
    CHECK(canon_prov_product(&p, b, a, &ba) == CANON_COMPLETE);
    CHECK(canon_prov_inverse(&p, a, &ia) == CANON_COMPLETE);
    /* the identity is a tag, not a record */
    CHECK(canon_prov_product(&p, CANON_PROV_NONE, a, &same) == CANON_COMPLETE && same == a);
    CHECK(canon_prov_product(&p, b, CANON_PROV_NONE, &same) == CANON_COMPLETE && same == b);
    CHECK(p.count == 5);
    CHECK(canon_prov_product(&p, a, 99, &same) == CANON_INTERNAL_ERROR);
    CHECK(canon_prov_inverse(&p, 99, &same) == CANON_INTERNAL_ERROR);
    uint32_t values[5 * 3];
    CHECK(canon_prov_eval_all(&p, &in, values) == CANON_COMPLETE);
    /* PRODUCT a b: a acts first, (c tau)[v] = tau[c[v]] = [2,1,0]; (tau c) = [1,0,2] */
    const uint32_t want_ab[3] = {2, 1, 0}, want_ba[3] = {1, 0, 2}, want_ia[3] = {2, 0, 1};
    CHECK(eq(values + 3 * ab, want_ab, 3) && eq(values + 3 * ba, want_ba, 3));
    CHECK(eq(values + 3 * ia, want_ia, 3));
    /* malformed records are reported, never read out of range */
    p.nodes[ab].b = 4; /* a later node */
    CHECK(canon_prov_eval_all(&p, &in, values) == CANON_INVALID_INPUT);
    p.nodes[ab].b = b;
    p.nodes[a].a = 7; /* no such input */
    CHECK(canon_prov_eval_all(&p, &in, values) == CANON_INVALID_INPUT);
    p.nodes[a].a = 0;
    p.nodes[ia].kind = 9;
    CHECK(canon_prov_eval_all(&p, &in, values) == CANON_INVALID_INPUT);
    canon_prov_free(&p);
    canon_perm_table_free(&in);
}

typedef struct tally {
    uint64_t chains, nodes, insertions, max_nodes;
    double max_ratio; /* nodes / (insertions * n) */
} tally;

/* Build <gens>, re-derive every generator and inverse, check the bound. */
static void rederive(uint32_t n, const uint32_t *gens, uint32_t count, tally *t)
{
    canon_bsgs c;
    CHECK(canon_bsgs_build(&c, n, gens, count, NULL, 0) == CANON_COMPLETE);
    uint32_t *values = malloc(((size_t)c.prov.count * n + 1) * sizeof *values);
    CHECK(values != NULL);
    CHECK(canon_prov_eval_all(&c.prov, &c.inputs, values) == CANON_COMPLETE);
    for (uint32_t k = 0; k < c.gens.count; ++k) {
        CHECK(eq(canon_perm_table_row(&c.gens, k), values + (size_t)c.gen_node[k] * n, n));
        CHECK(eq(canon_perm_table_row(&c.invs, k), values + (size_t)c.inv_node[k] * n, n));
        CHECK(c.prov.nodes[c.inv_node[k]].kind == CANON_PROV_INVERSE &&
              c.prov.nodes[c.inv_node[k]].a == c.gen_node[k]);
    }
    /* INPUT records name kept inputs: the record of input i holds input i */
    uint64_t kept = 0, sum_r = 0;
    for (uint32_t k = 0; k < c.prov.count; ++k) {
        if (c.prov.nodes[k].kind == CANON_PROV_INPUT) {
            ++kept;
            CHECK(c.prov.nodes[k].a < count);
            CHECK(eq(values + (size_t)k * n, gens + (size_t)c.prov.nodes[k].a * n, n));
        }
    }
    for (uint32_t i = 0; i < c.depth; ++i) {
        sum_r += c.levels[i].orbit_len;
    }
    const uint64_t ins = c.stats.insertions;
    CHECK(ins == c.gens.count);
    CHECK(c.prov.count <= kept + ins * (2u * (uint64_t)n + sum_r + 1u));
    /* the S3 brief's example bound holds on every chain of these fixed sets (measured, not
     * proved: the bound above is the proved one) */
    CHECK(c.prov.count <= 4u * ins * (uint64_t)n);
    t->chains += 1;
    t->nodes += c.prov.count;
    t->insertions += ins;
    t->max_nodes = c.prov.count > t->max_nodes ? c.prov.count : t->max_nodes;
    if (ins > 0) {
        double r = (double)c.prov.count / ((double)ins * n);
        t->max_ratio = r > t->max_ratio ? r : t->max_ratio;
    }
    free(values);
    canon_bsgs_free(&c);
}

static void print_tally(const char *what, const tally *t)
{
    printf("%s: %llu chains, %llu nodes, %llu insertions, max nodes %llu, "
           "max nodes/(insertions*n) %.3f\n",
           what, (unsigned long long)t->chains, (unsigned long long)t->nodes,
           (unsigned long long)t->insertions, (unsigned long long)t->max_nodes, t->max_ratio);
}

static void random_perm(uint32_t *p, uint32_t n)
{
    for (uint32_t v = 0; v < n; ++v) {
        p[v] = v;
    }
    for (uint32_t v = n; v > 1; --v) {
        uint32_t j = (uint32_t)(check_rng() % v), t = p[v - 1];
        p[v - 1] = p[j];
        p[j] = t;
    }
}

int main(void)
{
    records();
    tally t1;
    memset(&t1, 0, sizeof t1);
    for (uint32_t n = 0; n <= 4; ++n) {
        t1_sym s;
        t1_sym_init(&s, n);
        t1_group g[T1_MAX_GROUPS];
        uint32_t count = t1_subgroups(&s, g);
        for (uint32_t k = 0; k < count; ++k) {
            rederive(n, g[k].gens, g[k].gen_count, &t1);
        }
    }
    print_tally("T1 provenance", &t1);
    tally t2;
    memset(&t2, 0, sizeof t2);
    check_rng_state = 0x5332ULL * 2654435761ULL;
    for (uint32_t k = 0; k < 200; ++k) {
        uint32_t n = 1 + (uint32_t)(check_rng() % 8), count = 1 + (uint32_t)(check_rng() % 3);
        uint32_t gens[3 * 8];
        for (uint32_t i = 0; i < count; ++i) {
            random_perm(gens + i * n, n);
        }
        rederive(n, gens, count, &t2);
    }
    print_tally("T2 provenance (uniform random generators)", &t2);
    tally big;
    memset(&big, 0, sizeof big);
    uint32_t gens[2 * 64];
    for (uint32_t v = 0; v < 12; ++v) {
        gens[v] = v;
        gens[12 + v] = (v + 1) % 12;
    }
    gens[0] = 1;
    gens[1] = 0;
    rederive(12, gens, 2, &big); /* Sym(12) */
    for (uint32_t v = 0; v < 64; ++v) {
        gens[v] = (v + 1) % 64;
    }
    rederive(64, gens, 1, &big); /* the 64-cycle */
    print_tally("Sym(12) and the 64-cycle", &big);
    return check_finish("test_provenance");
}
