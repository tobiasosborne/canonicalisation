/* The direct Schreier recursion of spec 9.1 (src/bsgs/reference.c) as the correctness anchor
 * for the practical constructor (docs/slices/S3.md 3; detailed plan WP2.2): on every T1 group
 * (every subgroup of Sym(n), n <= 4) from its greedy generators and from its full element
 * list, the reference chain and the stabiliser chain agree on the order and on membership of
 * every element of Sym(n), and the order is the subgroup's size. */
#include <stdlib.h>
#include <string.h>

#include "bsgs/chain.h"
#include "bsgs/reference.h"
#include "check.h"
#include "t1_groups.h"

static void agree(const t1_sym *s, uint32_t order, const uint32_t *gens, uint32_t count)
{
    const uint32_t n = s->n;
    canon_ref_chain r;
    canon_bsgs c;
    CHECK(canon_ref_build(n, gens, count, &r) == CANON_COMPLETE);
    CHECK(canon_bsgs_build(&c, n, gens, count, NULL, 0) == CANON_COMPLETE);
    CHECK(r.order == order && c.order == order);
    uint32_t scratch[4];
    for (uint32_t e = 0; e < s->count; ++e) {
        CHECK(canon_ref_contains(&r, s->elem[e], scratch) == canon_bsgs_contains(&c, s->elem[e]));
    }
    canon_ref_free(&r);
    canon_bsgs_free(&c);
}

int main(void)
{
    uint32_t groups = 0;
    for (uint32_t n = 0; n <= 4; ++n) {
        t1_sym s;
        t1_sym_init(&s, n);
        t1_group g[T1_MAX_GROUPS];
        uint32_t count = t1_subgroups(&s, g);
        for (uint32_t k = 0; k < count; ++k) {
            agree(&s, g[k].order, g[k].gens, g[k].gen_count);
            /* the full element list as generators (identity and all) */
            uint32_t full[24 * 4], m = 0;
            for (uint32_t e = 0; e < s.count; ++e) {
                if (g[k].mask >> e & 1u) {
                    memcpy(full + m * n, s.elem[e], n * sizeof *full);
                    ++m;
                }
            }
            CHECK(m == g[k].order);
            agree(&s, g[k].order, full, m);
            /* membership counts equal the order */
            canon_ref_chain r;
            CHECK(canon_ref_build(n, g[k].gens, g[k].gen_count, &r) == CANON_COMPLETE);
            uint32_t members = 0, scratch[4];
            for (uint32_t e = 0; e < s.count; ++e) {
                bool in = canon_ref_contains(&r, s.elem[e], scratch);
                CHECK(in == ((g[k].mask >> e & 1u) != 0));
                members += in;
            }
            CHECK(members == g[k].order);
            canon_ref_free(&r);
            ++groups;
        }
    }
    CHECK(groups == 40);
    return check_finish("test_reference_schreier");
}
