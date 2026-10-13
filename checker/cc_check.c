/* The certificate rules of canon-check (spec 7.1, 7.2, 7.3, 9.1, 20; slice S7,
 * docs/slices/S7.md 3.4, rules 1-7).  cc_check reads a CERT-0 stream and replays the P1
 * search it describes with the checker's own partition, refinement, group and object code. */
#include <stdlib.h>
#include <string.h>

#include "cc.h"

cc_reason cc_check(const uint8_t *cert, size_t len, uint64_t *nodes_out);

typedef struct checker {
    cc_reader r;
    uint32_t n;
    uint16_t work_policy;
    cc_object x, img;
    uint32_t k;
    uint32_t *gens;  /* k x n */
    cc_chain root;
    uint32_t m;
    uint32_t *auts;  /* m x n */
    /* replay state */
    cc_buf trace, best_trace, best_bytes, bytes;
    bool have_best;
    uint32_t *best_t, *prefix, *u, *last_u, *fixed, *orbit_id, *key, *scratch, *tmp;
    uint64_t *bitmap;
    uint64_t node_records, pruned_entries;
    cc_chain rebased;
} checker;

static void checker_free(checker *c)
{
    cc_object_free(&c->x);
    cc_object_free(&c->img);
    free(c->gens);
    cc_chain_free(&c->root);
    free(c->auts);
    cc_buf_free(&c->trace);
    cc_buf_free(&c->best_trace);
    cc_buf_free(&c->best_bytes);
    cc_buf_free(&c->bytes);
    free(c->best_t);
    free(c->prefix);
    free(c->u);
    free(c->last_u);
    free(c->fixed);
    free(c->orbit_id);
    free(c->key);
    free(c->scratch);
    free(c->tmp);
    free(c->bitmap);
    cc_chain_free(&c->rebased);
}

/* ---- trace tokens (spec 7.2) ---- */

static bool trace_sizes(cc_buf *t, uint8_t tag, const cc_partition *p)
{
    bool ok = cc_buf_put_u8(t, tag) && cc_buf_put_u32(t, p->cells);
    for (uint32_t i = 0; i < p->cells && ok; ++i) {
        ok = cc_buf_put_u32(t, cc_cell_size(p, i));
    }
    return ok;
}

/* Read U32(k) and k sizes and compare them with the partition's cells (rule 4). */
static cc_reason sizes_match(checker *c, const cc_partition *p, cc_reason mismatch)
{
    uint32_t k = 0;
    if (!cc_get_u32(&c->r, &k)) {
        return CC_TRUNCATED;
    }
    if (k != p->cells) {
        return mismatch;
    }
    for (uint32_t i = 0; i < k; ++i) {
        uint32_t size = 0;
        if (!cc_get_u32(&c->r, &size)) {
            return CC_TRUNCATED;
        }
        if (size != cc_cell_size(p, i)) {
            return mismatch;
        }
    }
    return CC_ACCEPT;
}

static bool read_dense(checker *c, uint32_t *out)
{
    if (c->r.len - c->r.at < 4u * (size_t)c->n) {
        return false;
    }
    for (uint32_t v = 0; v < c->n; ++v) {
        (void)cc_get_u32(&c->r, &out[v]);
    }
    return true;
}

/* ---- the stages ---- */

typedef struct sig_ctx {
    const cc_signatures *s;
} sig_ctx;

/* spec 7.1: "Count signatures compare lexicographically using the ordinary numerical order on
 * mathematical naturals" */
static int sig_cmp(void *ctx, uint32_t a, uint32_t b)
{
    const cc_signatures *s = ((sig_ctx *)ctx)->s;
    const uint64_t *sa = s->counts + (size_t)a * s->width, *sb = s->counts + (size_t)b * s->width;
    for (size_t i = 0; i < s->width; ++i) {
        if (sa[i] != sb[i]) {
            return sa[i] < sb[i] ? -1 : 1;
        }
    }
    return 0;
}

static int key_cmp(void *ctx, uint32_t a, uint32_t b)
{
    const uint32_t *key = ctx;
    return key[a] < key[b] ? -1 : (key[a] > key[b] ? 1 : 0);
}

/* spec 7.1 O stage, then the recorded sizes (rule 4). */
static cc_reason stage_o(checker *c, cc_partition *p)
{
    cc_signatures s;
    memset(&s, 0, sizeof s);
    /* spec 7.1: "Within O all signatures refer to its entry snapshot": computed before the
     * split from p, which is the entry snapshot */
    cc_reason why = cc_signatures_compute(&c->x, p, &s);
    if (why == CC_ACCEPT) {
        sig_ctx ctx = {&s};
        cc_partition_split(p, sig_cmp, &ctx, c->tmp);
    }
    cc_signatures_free(&s);
    if (why != CC_ACCEPT) {
        return why;
    }
    if (!trace_sizes(&c->trace, 0x20, p)) {
        return CC_OUT_OF_MEMORY;
    }
    return sizes_match(c, p, CC_STAGE_O_SIZES);
}

/* spec 7.1 G stage from the certificate's data (rule 4): F must be the singletons in
 * partition order; u in G with F^u = M; the rebased chain verifies, generates G, has base
 * prefix M, and M[i] is the least point of level i's orbit; the orbits of G_M (level f) pulled
 * back through u split the partition; then the recorded sizes. */
static cc_reason stage_g(checker *c, cc_partition *p)
{
    const uint32_t n = c->n;
    uint32_t f = 0, rf = 0;
    for (uint32_t i = 0; i < p->cells; ++i) {
        if (cc_cell_size(p, i) == 1) {
            c->fixed[f++] = p->lab[p->start[i]]; /* "singleton atoms in current partition order" */
        }
    }
    if (!cc_get_u32(&c->r, &rf)) {
        return CC_TRUNCATED;
    }
    if (rf != f) {
        return CC_F_MISMATCH;
    }
    for (uint32_t i = 0; i < f; ++i) {
        uint32_t a = 0;
        if (!cc_get_u32(&c->r, &a)) {
            return CC_TRUNCATED;
        }
        if (a != c->fixed[i]) {
            return CC_F_MISMATCH;
        }
    }
    if (!read_dense(c, c->u)) {
        return CC_TRUNCATED;
    }
    if (!cc_is_bijection(c->u, n, c->bitmap)) {
        return CC_U_NOT_BIJECTION;
    }
    if (!cc_chain_contains(&c->root, c->u, c->scratch)) {
        return CC_U_NOT_MEMBER; /* spec 7.1: "choose any u in G with F^u = M" */
    }
    for (uint32_t i = 0; i < f; ++i) {
        uint32_t mi = 0;
        if (!cc_get_u32(&c->r, &mi)) {
            return CC_TRUNCATED;
        }
        if (mi != c->u[c->fixed[i]]) {
            return CC_M_MISMATCH; /* M = F^u */
        }
    }
    /* the chain of G with base prefix M */
    cc_chain_free(&c->rebased);
    cc_reason why = cc_chain_read(&c->r, n, &c->rebased);
    if (why == CC_ACCEPT) {
        why = cc_chain_verify(&c->rebased); /* spec 9.1: verified before anything is read off it */
    }
    if (why != CC_ACCEPT) {
        return why;
    }
    /* the same group as the root chain: mutual membership of the generating sets (spec 9.2
     * "Group equality checks mutual generator membership in complete chains") */
    for (uint32_t j = 0; j < c->rebased.i; ++j) {
        if (!cc_chain_contains(&c->root, c->rebased.inputs + (size_t)j * n, c->scratch)) {
            return CC_REBASED_NOT_SAME_GROUP;
        }
    }
    for (uint32_t j = 0; j < c->k; ++j) {
        if (!cc_chain_contains(&c->rebased, c->gens + (size_t)j * n, c->scratch)) {
            return CC_REBASED_NOT_SAME_GROUP;
        }
    }
    if (c->rebased.depth < f) {
        return CC_REBASED_BASE_PREFIX;
    }
    for (uint32_t i = 0; i < f; ++i) {
        const cc_level *L = &c->rebased.levels[i];
        if (L->base != c->u[c->fixed[i]]) {
            return CC_REBASED_BASE_PREFIX;
        }
        /* spec 7.1 "M = lexicographically least F^G" and spec 7.2's constructive procedure:
         * M[i] is the least point of the orbit of M[i] under the stabiliser of M[0..i-1],
         * which is level i's orbit; the greedy choice is forced by the uniqueness of the
         * minimum, so these f conditions characterise M. */
        if (cc_level_min(L) != L->base) {
            return CC_M_NOT_MINIMAL;
        }
    }
    /* spec 7.1: "compute G_M orbits; sort each orbit's target labels increasingly, then sort
     * the orbit lists lexicographically; sig(v) = position of the orbit containing u[v]".  G_M
     * is the suffix after f levels (spec 9.2 point stabilisers), generated by level f. */
    cc_chain_orbit_ids(&c->rebased, f, c->orbit_id, c->scratch);
    for (uint32_t v = 0; v < n; ++v) {
        c->key[v] = c->orbit_id[c->u[v]];
    }
    cc_partition_split(p, key_cmp, c->key, c->tmp);
    if (!trace_sizes(&c->trace, 0x21, p)) {
        return CC_OUT_OF_MEMORY;
    }
    if (n > 0) {
        memcpy(c->last_u, c->u, (size_t)n * sizeof *c->last_u);
    }
    return sizes_match(c, p, CC_STAGE_G_SIZES);
}

/* spec 7.2 leaf (rule 6): the partition is discrete; t equals the last G stage's u (there
 * F = L, and the verified minimality of M = L^u makes u the unique minimiser of L^G, which is
 * t_L); the key (trace, bytes of x^t) is compared with the running minimum, the least t kept
 * on an equal key (spec 7.4 preamble). */
static cc_reason leaf(checker *c, const cc_partition *p)
{
    const uint32_t n = c->n;
    if (p->cells != n) {
        return CC_LEAF_NOT_DISCRETE;
    }
    if (!read_dense(c, c->u)) {
        return CC_TRUNCATED;
    }
    if (n > 0 && memcmp(c->u, c->last_u, (size_t)n * sizeof *c->u) != 0) {
        return CC_LEAF_T_MISMATCH;
    }
    if (!cc_buf_put_u8(&c->trace, 0x00)) {
        return CC_OUT_OF_MEMORY;
    }
    cc_reason why = cc_object_act(&c->x, c->u, &c->img); /* spec 2.1: x^t */
    if (why == CC_ACCEPT) {
        why = cc_object_encode(&c->img, &c->bytes); /* spec 4.1, 4.2: its canonical stream */
    }
    if (why != CC_ACCEPT) {
        return why;
    }
    /* spec 7.2: "(complete trace, CDAG-2 bytes of x^t_L), with trace compared first" */
    int cmp = -1;
    if (c->have_best) {
        cmp = cc_bytes_compare(c->trace.data, c->trace.len, c->best_trace.data, c->best_trace.len);
        if (cmp == 0) {
            cmp = cc_bytes_compare(c->bytes.data, c->bytes.len, c->best_bytes.data,
                                   c->best_bytes.len);
        }
        if (cmp == 0) {
            cmp = cc_perm_compare(c->u, c->best_t, n);
        }
    }
    if (cmp < 0) {
        c->best_trace.len = 0;
        c->best_bytes.len = 0;
        if (!cc_buf_put(&c->best_trace, c->trace.data, c->trace.len) ||
            !cc_buf_put(&c->best_bytes, c->bytes.data, c->bytes.len)) {
            return CC_OUT_OF_MEMORY;
        }
        if (n > 0) {
            memcpy(c->best_t, c->u, (size_t)n * sizeof *c->best_t);
        }
        c->have_best = true;
    }
    return CC_ACCEPT;
}

static cc_reason node(checker *c, cc_partition *p, uint32_t depth, uint32_t atom);

/* spec 7.1 branch (rule 5): the recorded cell is the (size, position)-minimal non-singleton
 * cell; the child entries name every member exactly once; a pruned child b has an explored
 * representative b' and a listed automorphism a with b'^a = b that fixes the node's prefix
 * pointwise (docs/pruning-rules.md: the lemma then covers b's subtree by b'`s). */
static cc_reason branch(checker *c, cc_partition *p, uint32_t depth)
{
    const uint32_t n = c->n;
    if (p->cells == n) {
        return CC_BRANCH_AT_DISCRETE;
    }
    uint32_t target = CC_NONE, best = CC_NONE;
    for (uint32_t i = 0; i < p->cells; ++i) {
        const uint32_t size = cc_cell_size(p, i);
        if (size > 1 && size < best) {
            best = size;
            target = i;
        }
    }
    uint32_t rpos = 0, rsize = 0;
    if (!cc_get_u32(&c->r, &rpos) || !cc_get_u32(&c->r, &rsize)) {
        return CC_TRUNCATED;
    }
    if (rpos != target || rsize != best) {
        return CC_TARGET_CELL;
    }
    const uint32_t lo = p->start[target];
    cc_reason why = CC_ACCEPT;
    uint8_t *state = cc_alloc(best, sizeof *state, &why); /* 0 unseen, 1 explored, 2 pruned */
    uint32_t *reps = cc_alloc(best, sizeof *reps, &why);   /* representative index of a pruned */
    cc_partition child;
    memset(&child, 0, sizeof child);
    if (state == NULL || reps == NULL || (why = cc_partition_init(&child, n)) != CC_ACCEPT) {
        free(state);
        free(reps);
        cc_partition_free(&child);
        return why;
    }
    memset(state, 0, best);
    for (uint32_t i = 0; i < best && why == CC_ACCEPT; ++i) {
        uint8_t kind = 0;
        uint32_t b = 0;
        if (!cc_get_u8(&c->r, &kind) || !cc_get_u32(&c->r, &b)) {
            why = CC_TRUNCATED;
            break;
        }
        if (b >= n || p->cell_of[b] != target) {
            why = CC_CHILD_NOT_IN_CELL;
            break;
        }
        uint32_t idx = 0;
        while (p->lab[lo + idx] != b) {
            ++idx;
        }
        if (state[idx] != 0) {
            why = CC_CHILD_REPEATED;
            break;
        }
        if (kind == 0x01) {
            state[idx] = 1;
            /* spec 7.1: "replace cell C in place by [{a}, C minus {a}]; recurse at depth+1" */
            cc_partition_copy(&child, p);
            cc_partition_individualise(&child, target, b);
            c->prefix[depth] = b;
            why = node(c, &child, depth + 1u, b);
        } else if (kind == 0x00) {
            uint32_t rep = 0, aut = 0;
            if (!cc_get_u32(&c->r, &rep) || !cc_get_u32(&c->r, &aut)) {
                why = CC_TRUNCATED;
                break;
            }
            if (rep >= n || p->cell_of[rep] != target) {
                why = CC_CHILD_NOT_IN_CELL;
                break;
            }
            if (aut >= c->m) {
                why = CC_PRUNED_AUTOMORPHISM_INDEX;
                break;
            }
            const uint32_t *a = c->auts + (size_t)aut * n;
            for (uint32_t d = 0; d < depth; ++d) {
                if (a[c->prefix[d]] != c->prefix[d]) {
                    why = CC_PRUNED_AUTOMORPHISM_MOVES_PREFIX; /* spec 7.3: node-stabilising */
                    break;
                }
            }
            if (why != CC_ACCEPT) {
                break;
            }
            if (a[rep] != b) {
                why = CC_PRUNED_AUTOMORPHISM_IMAGE;
                break;
            }
            state[idx] = 2;
            uint32_t ridx = 0;
            while (p->lab[lo + ridx] != rep) {
                ++ridx;
            }
            reps[idx] = ridx;
            c->pruned_entries += 1;
        } else {
            why = CC_CHILD_MISSING; /* an unknown child kind: the cover is not established */
        }
    }
    for (uint32_t i = 0; i < best && why == CC_ACCEPT; ++i) {
        if (state[i] == 0) {
            why = CC_CHILD_MISSING;
        } else if (state[i] == 2 && state[reps[i]] != 1) {
            why = CC_PRUNED_REP_NOT_EXPLORED; /* "retained equivalent subtree" (spec 7.3) */
        }
    }
    free(state);
    free(reps);
    cc_partition_free(&child);
    return why;
}

/* One node record (rule 4): depth and atom as expected, the sweeps replayed exactly as spec 7.1
 * (continue only after a strict split, one no-change sweep always), then a leaf or a branch. */
static cc_reason node(checker *c, cc_partition *p, uint32_t depth, uint32_t atom)
{
    uint32_t rdepth = 0, ratom = 0, sweeps = 0;
    if (!cc_get_u32(&c->r, &rdepth) || !cc_get_u32(&c->r, &ratom) || !cc_get_u32(&c->r, &sweeps)) {
        return CC_TRUNCATED;
    }
    if (rdepth != depth) {
        return CC_NODE_DEPTH;
    }
    if (ratom != atom) {
        return CC_NODE_ATOM;
    }
    c->node_records += 1;
    const size_t mark = c->trace.len;
    /* spec 7.2: NODE(d) = 10 || U32(d) */
    if (!cc_buf_put_u8(&c->trace, 0x10) || !cc_buf_put_u32(&c->trace, depth)) {
        return CC_OUT_OF_MEMORY;
    }
    if (sweeps == 0) {
        return CC_SWEEP_COUNT; /* spec 7.1: "One complete no-change sweep is always recorded" */
    }
    cc_reason why = CC_ACCEPT;
    for (uint32_t sw = 0; sw < sweeps && why == CC_ACCEPT; ++sw) {
        const uint32_t c0 = p->cells;
        why = stage_o(c, p);
        if (why == CC_ACCEPT) {
            why = stage_g(c, p);
        }
        if (why != CC_ACCEPT) {
            break;
        }
        /* spec 7.1: "if number of cells == c0: break"; "Continue only after a strict split" */
        const bool last = sw + 1u == sweeps;
        if ((p->cells == c0) != last) {
            why = CC_SWEEP_COUNT;
        }
    }
    uint8_t kind = 0;
    if (why == CC_ACCEPT && !cc_get_u8(&c->r, &kind)) {
        why = CC_TRUNCATED;
    }
    if (why == CC_ACCEPT) {
        if (kind == 0x00) {
            why = leaf(c, p);
        } else if (kind == 0x01) {
            why = branch(c, p, depth);
        } else {
            why = CC_TRUNCATED;
        }
    }
    c->trace.len = mark;
    return why;
}

/* ---- the whole certificate ---- */

static cc_reason run(checker *c)
{
    cc_reader *r = &c->r;
    /* rule 1: header identifiers */
    uint8_t magic[4] = {0, 0, 0, 0};
    for (int i = 0; i < 4; ++i) {
        if (!cc_get_u8(r, &magic[i])) {
            return CC_TRUNCATED;
        }
    }
    uint16_t ids[5] = {0, 0, 0, 0, 0};
    for (int i = 0; i < 5; ++i) {
        if (!cc_get_u16(r, &ids[i])) {
            return CC_TRUNCATED;
        }
    }
    if (memcmp(magic, "CNC0", 4) != 0 || ids[0] != 0x0001 /* spec 3: CANONICAL_IMAGE */ ||
        ids[1] != 0x0001 /* spec 7.1: P1 */ ||
        (ids[2] != 0x0001 && ids[2] != 0x0002 && ids[2] != 0x0003) /* spec 11.1 policies */ ||
        ids[3] != 0x0002 /* spec 4.1: CDAG-2 */ || ids[4] != 0x0001 /* spec 4.3: CDAG-BYTE-1 */) {
        return CC_HEADER;
    }
    c->work_policy = ids[2];
    if (!cc_get_u32(r, &c->n)) {
        return CC_TRUNCATED;
    }
    const uint32_t n = c->n;
    const uint8_t *stream = NULL;
    uint32_t stream_len = 0;
    if (!cc_get_b(r, &stream, &stream_len)) {
        return CC_TRUNCATED;
    }
    cc_reason why = cc_object_decode(stream, stream_len, n, &c->x); /* spec 4.1 */
    if (why != CC_ACCEPT) {
        return why;
    }
    c->best_t = cc_alloc(n, sizeof *c->best_t, &why);
    c->prefix = cc_alloc(n, sizeof *c->prefix, &why);
    c->u = cc_alloc(n, sizeof *c->u, &why);
    c->last_u = cc_alloc(n, sizeof *c->last_u, &why);
    c->fixed = cc_alloc(n, sizeof *c->fixed, &why);
    c->orbit_id = cc_alloc(n, sizeof *c->orbit_id, &why);
    c->key = cc_alloc(n, sizeof *c->key, &why);
    c->scratch = cc_alloc(n, sizeof *c->scratch, &why);
    c->tmp = cc_alloc((size_t)n + 1u, sizeof *c->tmp, &why);
    c->bitmap = cc_alloc(((size_t)n + 63u) / 64u, sizeof *c->bitmap, &why);
    if (c->best_t == NULL || c->prefix == NULL || c->u == NULL || c->last_u == NULL ||
        c->fixed == NULL || c->orbit_id == NULL || c->key == NULL || c->scratch == NULL ||
        c->tmp == NULL || c->bitmap == NULL) {
        return why;
    }
    for (uint32_t v = 0; v < n; ++v) {
        c->last_u[v] = v; /* a leaf at a root with no G stage cannot happen: sweeps >= 1 */
    }
    /* rule 1: generators are bijections */
    if (!cc_get_u32(r, &c->k)) {
        return CC_TRUNCATED;
    }
    if ((size_t)c->k > SIZE_MAX / 4u / (n > 0 ? n : 1u)) {
        return CC_CAPACITY;
    }
    c->gens = cc_alloc((size_t)c->k * n, sizeof *c->gens, &why);
    if (c->gens == NULL) {
        return why;
    }
    for (uint32_t j = 0; j < c->k; ++j) {
        if (!read_dense(c, c->gens + (size_t)j * n)) {
            return CC_TRUNCATED;
        }
        if (!cc_is_bijection(c->gens + (size_t)j * n, n, c->bitmap)) {
            return CC_GENERATOR_NOT_BIJECTION;
        }
    }
    /* rule 1: the root chain verifies and its inputs are exactly the generator list, so the
     * group it certifies is <generators> (spec 9.1 "input-generator membership at the root",
     * every strong generator derived from the inputs) */
    why = cc_chain_read(r, n, &c->root);
    if (why == CC_ACCEPT) {
        why = cc_chain_verify(&c->root);
    }
    if (why != CC_ACCEPT) {
        return why;
    }
    if (c->root.i != c->k ||
        (n > 0 && c->k > 0 &&
         memcmp(c->root.inputs, c->gens, (size_t)c->k * n * sizeof *c->gens) != 0)) {
        return CC_ROOT_CHAIN_INPUTS;
    }
    /* rule 2: every listed automorphism is a member of G and fixes x */
    if (!cc_get_u32(r, &c->m)) {
        return CC_TRUNCATED;
    }
    if ((size_t)c->m > SIZE_MAX / 4u / (n > 0 ? n : 1u)) {
        return CC_CAPACITY;
    }
    c->auts = cc_alloc((size_t)c->m * n, sizeof *c->auts, &why);
    if (c->auts == NULL) {
        return why;
    }
    for (uint32_t j = 0; j < c->m; ++j) {
        uint32_t *a = c->auts + (size_t)j * n;
        if (!read_dense(c, a)) {
            return CC_TRUNCATED;
        }
        if (!cc_is_bijection(a, n, c->bitmap) || !cc_chain_contains(&c->root, a, c->scratch)) {
            return CC_AUTOMORPHISM_NOT_MEMBER; /* spec 7.3: exact membership */
        }
        why = cc_object_act(&c->x, a, &c->img);
        if (why != CC_ACCEPT) {
            return why;
        }
        if (!cc_object_equal(&c->img, &c->x)) {
            return CC_AUTOMORPHISM_MOVES_X; /* spec 7.3: exact object equality */
        }
    }
    /* rule 3: the root partition from the checker's own initial keys (spec 7.1) */
    cc_partition p;
    why = cc_partition_init(&p, n);
    if (why != CC_ACCEPT) {
        return why;
    }
    for (uint32_t v = 0; v < n; ++v) {
        c->key[v] = cc_object_initial_key(&c->x, v);
    }
    cc_partition_split(&p, key_cmp, c->key, c->tmp); /* "order cells by increasing key" */
    why = node(c, &p, 0, CC_NONE); /* rules 4-6 */
    cc_partition_free(&p);
    if (why != CC_ACCEPT) {
        return why;
    }
    /* rule 7: the final claim equals the minimum over the explored leaves, the witness is the
     * least t attaining it, and the count is the number of node records under the policy */
    const uint8_t *claim = NULL;
    uint32_t claim_len = 0;
    if (!cc_get_b(r, &claim, &claim_len)) {
        return CC_TRUNCATED;
    }
    if (!c->have_best || cc_bytes_compare(claim, claim_len, c->best_trace.data, c->best_trace.len) != 0) {
        return CC_FINAL_TRACE;
    }
    if (!cc_get_b(r, &claim, &claim_len)) {
        return CC_TRUNCATED;
    }
    if (cc_bytes_compare(claim, claim_len, c->best_bytes.data, c->best_bytes.len) != 0) {
        return CC_FINAL_BYTES;
    }
    if (!read_dense(c, c->u)) {
        return CC_TRUNCATED;
    }
    if (n > 0 && memcmp(c->u, c->best_t, (size_t)n * sizeof *c->u) != 0) {
        return CC_FINAL_WITNESS;
    }
    uint64_t nodes = 0;
    if (!cc_get_u64(r, &nodes)) {
        return CC_TRUNCATED;
    }
    if (nodes != c->node_records) {
        return CC_NODE_COUNT; /* spec 11.1: the count of the traversal the policy names */
    }
    if (c->work_policy == 0x0001 && c->pruned_entries != 0) {
        return CC_PRUNED_UNDER_UNPRUNED_POLICY; /* 0x0001 is the unpruned reference traversal */
    }
    if (r->at != r->len) {
        return CC_TRAILING_BYTES;
    }
    return CC_ACCEPT;
}

cc_reason cc_check(const uint8_t *cert, size_t len, uint64_t *nodes_out)
{
    checker c;
    memset(&c, 0, sizeof c);
    c.r.data = cert;
    c.r.len = len;
    cc_object_init(&c.x);
    cc_object_init(&c.img);
    cc_chain_init(&c.root);
    cc_chain_init(&c.rebased);
    cc_buf_init(&c.trace);
    cc_buf_init(&c.best_trace);
    cc_buf_init(&c.best_bytes);
    cc_buf_init(&c.bytes);
    const cc_reason why = run(&c);
    if (nodes_out != NULL) {
        *nodes_out = c.node_records;
    }
    checker_free(&c);
    return why;
}
