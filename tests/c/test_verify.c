/* Tests for the independent verifier (src/bsgs/verify.c; spec 9.1 third paragraph;
 * docs/slices/S3.md 2.3, 3).  A verified chain is mutated in ten distinct ways and each
 * mutation must be rejected with the matching reason; further cases cover the remaining
 * reasons and the nesting condition (verify.c file comment). */
#include <stdlib.h>
#include <string.h>

#include "bsgs/chain.h"
#include "bsgs/verify.h"
#include "check.h"
#include "perm/perm.h"

/* Sym(4) from the transpositions (0 1), (1 2), (2 3): every generator is an involution, so
 * every stored tree edge can be traversed backwards by a level generator. */
static const uint32_t N = 4;
static const uint32_t GENS[3 * 4] = {1, 0, 2, 3, 0, 2, 1, 3, 0, 1, 3, 2};
#define GEN_COUNT 3u

static void build(canon_bsgs *c)
{
    CHECK(canon_bsgs_build(c, N, GENS, GEN_COUNT, NULL, 0) == CANON_COMPLETE);
}

static canon_bsgs_reason verdict(canon_bsgs *c)
{
    canon_bsgs_reason why = CANON_BSGS_UNCHECKED;
    CHECK(canon_bsgs_verify(c, GENS, GEN_COUNT, &why) == CANON_COMPLETE);
    CHECK(c->verified == (why == CANON_BSGS_VALID));
    return why;
}

static void expect(canon_bsgs *c, canon_bsgs_reason want, const char *what)
{
    canon_bsgs_reason got = verdict(c);
    if (got != want) {
        fprintf(stderr, "mutation '%s': got %s, want %s\n", what, canon_bsgs_reason_name(got),
                canon_bsgs_reason_name(want));
    }
    CHECK(got == want);
    canon_bsgs_free(c);
}

/* The deepest level of generator `id`, i.e. its insertion level. */
static uint32_t deepest_level(const canon_bsgs *c, uint32_t id)
{
    uint32_t deep = CANON_BSGS_NONE;
    for (uint32_t i = 0; i < c->depth; ++i) {
        for (uint32_t k = 0; k < c->levels[i].gen_count; ++k) {
            if (c->levels[i].gen_ids[k] == id) {
                deep = i;
            }
        }
    }
    return deep;
}

static void ten_mutations(void)
{
    canon_bsgs c;
    build(&c);
    CHECK(verdict(&c) == CANON_BSGS_VALID); /* the unmutated chain */
    CHECK(c.depth == 3 && c.order == 24);
    canon_bsgs_free(&c);

    /* 1. flip an orbit entry: level 1's orbit does not contain b_0; put b_0 in place of its
     *    last entry (orbit_pos untouched). */
    build(&c);
    {
        canon_bsgs_level *L = &c.levels[1];
        CHECK(L->orbit_len >= 2 && L->orbit_pos[c.levels[0].base_point] == CANON_BSGS_NONE);
        L->orbit[L->orbit_len - 1] = c.levels[0].base_point;
    }
    expect(&c, CANON_BSGS_ORBIT_POS_MISMATCH, "flip an orbit entry");

    /* 2. swap a Schreier parent: exchange the parents of two level-0 points whose stored edges
     *    then no longer hold. */
    build(&c);
    {
        canon_bsgs_level *L = &c.levels[0];
        bool done = false;
        for (uint32_t p = 1; p < L->orbit_len && !done; ++p) {
            for (uint32_t q = p + 1; q < L->orbit_len && !done; ++q) {
                const uint32_t *sp = canon_perm_table_row(&c.gens, L->gen_ids[L->parent_gen[p]]);
                if (L->parent_point[p] != L->parent_point[q] &&
                    sp[L->parent_point[q]] != L->orbit[p]) {
                    uint32_t t = L->parent_point[p];
                    L->parent_point[p] = L->parent_point[q];
                    L->parent_point[q] = t;
                    done = true;
                }
            }
        }
        CHECK(done);
    }
    expect(&c, CANON_BSGS_ORBIT_EDGE, "swap a Schreier parent");

    /* 3. reverse a transporter edge: for an edge b -> c with b not the base point and a level
     *    generator s' with c^s' = b, record b as discovered from c through s'.  Both edges are
     *    locally valid, but b and c now form a cycle that never reaches the base point. */
    build(&c);
    {
        bool done = false;
        for (uint32_t i = 0; i < c.depth && !done; ++i) {
            canon_bsgs_level *L = &c.levels[i];
            for (uint32_t p = 1; p < L->orbit_len && !done; ++p) {
                uint32_t b = L->parent_point[p], cpt = L->orbit[p], q = L->orbit_pos[b];
                if (q == 0) {
                    continue;
                }
                for (uint32_t gi = 0; gi < L->gen_count && !done; ++gi) {
                    if (canon_perm_table_row(&c.gens, L->gen_ids[gi])[cpt] == b) {
                        L->parent_point[q] = cpt;
                        L->parent_gen[q] = gi;
                        done = true;
                    }
                }
            }
        }
        CHECK(done);
    }
    expect(&c, CANON_BSGS_ORBIT_UNREACHABLE, "reverse a transporter edge");

    /* 4. drop a generator: remove the last inserted generator from its insertion level
     *    (recomputing that level's orbit), so that level represents a smaller group; a
     *    Schreier residue of the level above no longer sifts. */
    build(&c);
    {
        uint32_t id = c.gens.count - 1, j = deepest_level(&c, id);
        CHECK(j != CANON_BSGS_NONE && j >= 1);
        if (j != CANON_BSGS_NONE && j >= 1) {
            canon_bsgs_level *L = &c.levels[j];
            uint32_t w = 0;
            for (uint32_t k = 0; k < L->gen_count; ++k) {
                if (L->gen_ids[k] != id) {
                    L->gen_ids[w++] = L->gen_ids[k];
                }
            }
            L->gen_count = w;
            canon_bsgs_level_recompute(&c, j);
        }
    }
    expect(&c, CANON_BSGS_SCHREIER_RESIDUE, "drop a generator");

    /* 5. corrupt a provenance record: the INPUT record of input 0 now names input 1. */
    build(&c);
    {
        bool done = false;
        for (uint32_t k = 0; k < c.prov.count && !done; ++k) {
            if (c.prov.nodes[k].kind == CANON_PROV_INPUT && c.prov.nodes[k].a == 0) {
                c.prov.nodes[k].a = 1;
                done = true;
            }
        }
        CHECK(done);
    }
    expect(&c, CANON_BSGS_PROVENANCE_MISMATCH, "corrupt a provenance record");

    /* 6. change a base point: b_0 becomes a point that is not a base point and that a level-1
     *    generator moves. */
    build(&c);
    {
        uint32_t x = CANON_BSGS_NONE;
        for (uint32_t v = 0; v < N && x == CANON_BSGS_NONE; ++v) {
            bool base = false, moved = false;
            for (uint32_t i = 0; i < c.depth; ++i) {
                base |= c.levels[i].base_point == v;
            }
            for (uint32_t k = 0; k < c.levels[1].gen_count; ++k) {
                moved |= canon_perm_table_row(&c.gens, c.levels[1].gen_ids[k])[v] != v;
            }
            x = !base && moved ? v : x;
        }
        CHECK(x != CANON_BSGS_NONE);
        c.levels[0].base_point = x;
    }
    expect(&c, CANON_BSGS_PREFIX_NOT_FIXED, "change a base point");

    /* 7. truncate the last level: the chain ends one level early. */
    build(&c);
    c.depth -= 1;
    expect(&c, CANON_BSGS_LAST_NOT_TRIVIAL, "truncate the last level");

    /* 8. alter order */
    build(&c);
    c.order += 1;
    expect(&c, CANON_BSGS_ORDER_MISMATCH, "alter order");

    /* 9. duplicate an orbit point */
    build(&c);
    CHECK(c.levels[0].orbit_len >= 3);
    c.levels[0].orbit[2] = c.levels[0].orbit[1];
    expect(&c, CANON_BSGS_ORBIT_DUPLICATE, "duplicate an orbit point");

    /* 10. change an input generator: the chain's record of input 0 no longer equals the
     *     original validated input. */
    build(&c);
    {
        uint32_t *row = c.inputs.data; /* row 0 */
        uint32_t t = row[2];
        row[2] = row[3];
        row[3] = t;
    }
    expect(&c, CANON_BSGS_INPUT_MISMATCH, "change an input generator");
}

static void other_reasons(void)
{
    canon_bsgs c;
    /* a stored generator that is not a bijection */
    build(&c);
    c.gens.data[1] = c.gens.data[0];
    expect(&c, CANON_BSGS_NOT_BIJECTION, "non-bijective generator");

    /* a stored inverse that is not the inverse (but re-derived consistently is impossible
     * without also changing provenance, so the provenance check reports it first) */
    build(&c);
    {
        uint32_t *row = c.invs.data;
        uint32_t t = row[0];
        row[0] = row[1];
        row[1] = t;
    }
    expect(&c, CANON_BSGS_PROVENANCE_MISMATCH, "corrupt a stored inverse");

    /* orbit not closed: drop the last discovered point of level 0 (a leaf of the tree) */
    build(&c);
    {
        canon_bsgs_level *L = &c.levels[0];
        L->orbit_len -= 1;
        L->orbit_pos[L->orbit[L->orbit_len]] = CANON_BSGS_NONE;
    }
    expect(&c, CANON_BSGS_ORBIT_NOT_CLOSED, "orbit not closed");

    /* root: orbit[0] is not the base point */
    build(&c);
    c.levels[0].parent_point[0] = 0;
    expect(&c, CANON_BSGS_ORBIT_ROOT, "root with a parent");

    /* structure: a generator id out of range */
    build(&c);
    c.levels[0].gen_ids[0] = 99;
    expect(&c, CANON_BSGS_BAD_STRUCTURE, "generator id out of range");

    /* structure: a repeated base point */
    build(&c);
    c.levels[1].base_point = c.levels[0].base_point;
    expect(&c, CANON_BSGS_BAD_STRUCTURE, "repeated base point");

    /* a provenance record that refers to a later node */
    build(&c);
    {
        bool done = false;
        for (uint32_t k = 0; k + 1 < c.prov.count && !done; ++k) {
            if (c.prov.nodes[k].kind != CANON_PROV_INPUT) {
                c.prov.nodes[k].a = c.prov.count - 1; /* operands must be earlier nodes */
                done = true;
            }
        }
        CHECK(done);
    }
    expect(&c, CANON_BSGS_PROVENANCE_MISMATCH, "forward provenance reference");

    /* an input that is not a member: <(0 1)> on 4 points from (0 1) and the identity; the
     * identity input is dropped by normalisation, so replacing it (in the chain's record and
     * in the originals alike) by (2 3) leaves every record consistent, but (2 3) does not
     * sift. */
    {
        uint32_t in[8] = {1, 0, 2, 3, 0, 1, 2, 3};
        CHECK(canon_bsgs_build(&c, 4, in, 2, NULL, 0) == CANON_COMPLETE);
        in[6] = 3;
        in[7] = 2;
        c.inputs.data[6] = 3;
        c.inputs.data[7] = 2;
        canon_bsgs_reason why = CANON_BSGS_UNCHECKED;
        CHECK(canon_bsgs_verify(&c, in, 2, &why) == CANON_COMPLETE);
        CHECK(why == CANON_BSGS_INPUT_NOT_MEMBER && !c.verified);
        canon_bsgs_free(&c);
    }
}

/* The nesting condition.  For Sym(3) from (0 1) and (1 2): level 0 with base 0 and
 * S_0 = {(0 1)} (orbit {0, 1}); level 1 with base 1 and S_1 = {(1 2)} (orbit {1, 2}); order
 * 2 * 2 = 4.  Bijections, provenance, prefix fixation, orbits, transversals, Schreier
 * residues (each level's are trivial), input membership (both inputs sift), terminal
 * triviality and the order product all hold, yet |Sym(3)| = 6: (1 2) is not in <S_0>.  The
 * verifier must reject it (verify.c file comment). */
static void nesting_counterexample(void)
{
    const uint32_t in[6] = {1, 0, 2, 0, 2, 1};
    canon_bsgs c;
    canon_bsgs_init(&c, 3);
    uint32_t id = 0, node = 0, inv[3];
    c.gen_node = malloc(2 * sizeof *c.gen_node);
    c.inv_node = malloc(2 * sizeof *c.inv_node);
    c.node_cap = 2;
    for (uint32_t k = 0; k < 2; ++k) {
        CHECK(canon_perm_table_push(&c.inputs, in + 3 * k, &id) == CANON_COMPLETE);
        CHECK(canon_perm_table_push(&c.gens, in + 3 * k, &id) == CANON_COMPLETE);
        canon_perm_inverse(in + 3 * k, inv, 3);
        CHECK(canon_perm_table_push(&c.invs, inv, &id) == CANON_COMPLETE);
        CHECK(canon_prov_input(&c.prov, k, &node) == CANON_COMPLETE);
        c.gen_node[k] = node;
        CHECK(canon_prov_inverse(&c.prov, node, &node) == CANON_COMPLETE);
        c.inv_node[k] = node;
    }
    c.levels = calloc(3, sizeof *c.levels);
    c.level_cap = 3;
    c.depth = 2;
    for (uint32_t i = 0; i < 2; ++i) {
        canon_bsgs_level *L = &c.levels[i];
        uint32_t *block = malloc(4 * 3 * sizeof *block);
        L->orbit = block;
        L->orbit_pos = block + 3;
        L->parent_point = block + 6;
        L->parent_gen = block + 9;
        for (uint32_t v = 0; v < 3; ++v) {
            L->orbit_pos[v] = CANON_BSGS_NONE;
        }
        L->base_point = i;
        L->gen_ids = malloc(sizeof *L->gen_ids);
        L->gen_ids[0] = i; /* S_0 = {(0 1)}, S_1 = {(1 2)}: not nested */
        L->gen_count = L->gen_cap = 1;
        canon_bsgs_level_recompute(&c, i);
    }
    c.levels[2].base_point = CANON_BSGS_NONE;
    c.order = 4;
    canon_bsgs_reason why = CANON_BSGS_UNCHECKED;
    CHECK(canon_bsgs_verify(&c, in, 2, &why) == CANON_COMPLETE);
    CHECK(why == CANON_BSGS_NOT_NESTED && !c.verified);
    canon_bsgs_free(&c);
    /* the constructor's chain for the same inputs is accepted and has order 6 */
    CHECK(canon_bsgs_build(&c, 3, in, 2, NULL, 0) == CANON_COMPLETE);
    CHECK(canon_bsgs_verify(&c, in, 2, &why) == CANON_COMPLETE && why == CANON_BSGS_VALID);
    CHECK(c.order == 6);
    canon_bsgs_free(&c);
}

int main(void)
{
    ten_mutations();
    other_reasons();
    nesting_counterexample();
    return check_finish("test_verify");
}
