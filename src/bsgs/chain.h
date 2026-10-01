/*
 * Internal header: verified stabiliser chain (base and strong generating set) for a group of
 * permutations of {0..n-1} (spec 9.1-9.3; slice S3, docs/slices/S3.md 2.1-2.4; detailed plan
 * 2.3).
 *
 * Convention (spec 3): permutations are dense image arrays p[v] = v^p; products act left to
 * right, (pq)[v] = q[p[v]].  A transporter t_b at a level sends that level's base point to b.
 *
 * Layout.  levels[i], 0 <= i < depth, has base point b_i and generator list S_i (ids into the
 * chain's generator table) for K_i = <S_i>, the pointwise stabiliser G_(b_0..b_{i-1}) once the
 * chain is complete; levels[depth] is the trivial terminal level (no base point, no
 * generators).  Membership is inclusive: a strong generator inserted at level j appears in
 * S_0, ..., S_j (it fixes b_0..b_{j-1}), in insertion order, so S_{i+1} is a sublist of S_i.
 * The orbit of b_i under S_i is stored in discovery order with a Schreier vector over orbit
 * positions: the point orbit[p], p > 0, was discovered from the point parent_point[p] through
 * the generator gen_ids[parent_gen[p]], i.e. orbit[p] = parent_point[p]^s; the root (p = 0)
 * has both entries CANON_BSGS_NONE.
 *
 * The identity is never stored (spec 9.3 tagged identity): input identities are dropped, a
 * residue equal to the identity is never inserted, and the transporter of a base point is the
 * implicit identity.  Strong generators are dense, each with its dense inverse, so transporter
 * walks never recompute inverses (S3 brief 2.1).
 */
#ifndef CANON_SRC_BSGS_CHAIN_H
#define CANON_SRC_BSGS_CHAIN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "bsgs/provenance.h"
#include "canon/canon.h"
#include "perm/perm.h"

#define CANON_BSGS_NONE UINT32_MAX

typedef struct canon_bsgs_level {
    uint32_t base_point;    /* b_i; CANON_BSGS_NONE on the terminal level */
    uint32_t orbit_len;     /* 0 on the terminal level */
    uint32_t *orbit;        /* n entries capacity; orbit[0] == base_point; discovery order */
    uint32_t *orbit_pos;    /* n entries: position in orbit, or CANON_BSGS_NONE */
    uint32_t *parent_point; /* by orbit position: the point it was discovered from */
    uint32_t *parent_gen;   /* by orbit position: index into gen_ids of the generator used */
    uint32_t *schreier_done; /* construction bookkeeping, by orbit position: how many of the
                                generators (a prefix of gen_ids) have had their Schreier
                                generator checked (chain.c closure); not part of the chain's
                                meaning and not read by the verifier; may be NULL on levels
                                built by hand */
    uint32_t gen_count, gen_cap;
    uint32_t *gen_ids;      /* indices into the chain's generator table, insertion order */
} canon_bsgs_level;

/* Operation counts (detailed plan WP2.3, spec 9.2 ledgers; no timings). */
typedef struct canon_bsgs_stats {
    uint64_t candidates;   /* input generators and Schreier generators considered */
    uint64_t sifts;        /* sifts performed (a Schreier generator that is a tree edge is the
                              identity by construction and is not sifted) */
    uint64_t compositions; /* dense O(n) products, including one step of a transporter walk */
    uint64_t insertions;   /* strong generators inserted */
} canon_bsgs_stats;

typedef struct canon_bsgs {
    uint32_t n;
    uint32_t depth;              /* number of base points */
    uint32_t level_cap;          /* entries allocated in levels */
    canon_bsgs_level *levels;    /* depth + 1 used; levels[depth] is trivial */
    canon_perm_table gens;       /* strong generators, dense, row = generator id */
    canon_perm_table invs;       /* invs row k = gens row k inverse */
    uint32_t node_cap;           /* entries allocated in gen_node / inv_node */
    uint32_t *gen_node;          /* provenance node of generator k */
    uint32_t *inv_node;          /* provenance node of its inverse (INVERSE gen_node[k]) */
    canon_perm_table inputs;     /* the input generators the chain was built from, as given */
    canon_provenance prov;       /* INPUT entries index `inputs` */
    uint64_t order;              /* product of the orbit lengths of levels 0..depth-1 */
    bool verified;               /* set only by canon_bsgs_verify (src/bsgs/verify.c) */
    canon_bsgs_stats stats;      /* counts of the construction that produced this chain */
} canon_bsgs;

/* An empty chain of degree n (no allocation); free with canon_bsgs_free. */
void canon_bsgs_init(canon_bsgs *c, uint32_t n);
void canon_bsgs_free(canon_bsgs *c);

/* spec 9.1 second paragraph: the practical deterministic closure constructor with the fixed
 * policies of docs/slices/S3.md 2.2 (see chain.c).  `gens` is a flat array of `count` image
 * arrays of length n, each a bijection (the caller validates, spec 4.1).  The base starts with
 * the `prefix_len` distinct points of `prefix` (rebase, spec 9.2), then is extended by the
 * policy.  *out is initialised here; on failure it is freed and left empty.
 * Returns CANON_CAPACITY_LIMIT when |G| exceeds uint64 (detected as soon as the product of the
 * orbit lengths, a lower bound on |G|, overflows; the multi-limb canon_nat that would lift this is
 * deferred, docs/slices/S4-notes.md) or a size does not
 * fit, CANON_INVALID_INPUT for a prefix point >= n or a repeated prefix point,
 * CANON_RESOURCE_LIMIT on allocation failure.  The chain is NOT marked verified. */
canon_status canon_bsgs_build(canon_bsgs *out, uint32_t n, const uint32_t *gens, size_t count,
                              const uint32_t *prefix, uint32_t prefix_len);

/* Recompute the orbit, Schreier vector and orbit_pos of one level from its base point and
 * generator list (queue traversal in discovery order, generators in gen_ids order).  The
 * constructor extends orbits incrementally instead; this is for tests that build or edit
 * levels by hand (it does not touch schreier_done). */
void canon_bsgs_level_recompute(canon_bsgs *c, uint32_t level);

/* The transporter t_b of `level` (S3 brief 2.2): the element of K_level sending the base point
 * to b, reconstructed from the Schreier vector.  If the tree path from the base point is
 * b_0 -> b_1 -> ... -> b with b_{j+1} = b_j^{s_j}, then t_b = s_0 s_1 ... s_{k-1} (left to
 * right, spec 3).  CANON_INVALID_INPUT if b is not in the orbit. */
canon_status canon_bsgs_transporter(const canon_bsgs *c, uint32_t level, uint32_t b,
                                    uint32_t *out);

/* As canon_bsgs_transporter, with caller scratch and no allocation (slice S4: the coset
 * descent and the enumerator call it once per step).  Precondition: level < depth and b is in
 * the orbit of that level; out and tmp have n entries each and do not alias. */
void canon_bsgs_transporter_scratch(const canon_bsgs *c, uint32_t level, uint32_t b,
                                    uint32_t *out, uint32_t *tmp);

/* spec 9.2 "Membership sifts g by repeatedly mapping the base image back with the
 * corresponding transversal inverse": in place, from level `from` downwards, g <- g t_b^-1
 * where b = g[base point] (then g fixes the base point).  *stop receives the level at which b
 * was not in the orbit, or c->depth if every level was passed; g is then the residue. */
void canon_bsgs_sift(const canon_bsgs *c, uint32_t from, uint32_t *g, uint32_t *stop,
                     canon_bsgs_stats *stats);

/* spec 9.1/9.2 membership: *out = (p is in the group), by one sift of a copy of p in per-call
 * scratch.  p must be a bijection of {0..n-1}.  CANON_RESOURCE_LIMIT / CANON_CAPACITY_LIMIT
 * when the scratch cannot be allocated (*out = false). */
canon_status canon_bsgs_contains(const canon_bsgs *c, const uint32_t *p, bool *out);

/* spec 9.2: the order of the suffix from `level` (the pointwise stabiliser
 * G_(b_0..b_{level-1}), spec 9.2 "point stabilisers"), the product of its orbit lengths.  It
 * fits uint64 for every chain canon_bsgs_build returns. */
uint64_t canon_bsgs_suffix_order(const canon_bsgs *c, uint32_t level);

/* spec 9.2 "base change rebuilds/certifies for the requested ordered base (reuse is
 * optional)": a new chain for K_from (the suffix of `src` from level `from`) built from that
 * level's strong generators, with base starting with `prefix`.  With `verify`, the result is
 * then checked by canon_bsgs_verify against those generators (CANON_INTERNAL_ERROR if it is
 * rejected); internal transient rebuilds (the tuple minimum) pass false.  Otherwise as
 * canon_bsgs_build. */
canon_status canon_bsgs_rebase(const canon_bsgs *src, uint32_t from, const uint32_t *prefix,
                               uint32_t prefix_len, bool verify, canon_bsgs *out);

/* spec 9.1 "exact verification is mandatory": canon_bsgs_build with no prefix, then
 * canon_bsgs_verify against `gens` (CANON_INTERNAL_ERROR if the verifier rejects the chain).
 * Used for every chain that is an answer or certifies one (the group's own chain, the
 * stabiliser of slice S4, the subgroups K of the spec 9.4 rule 2 sequence); transient chains
 * of a descent are built by canon_bsgs_rebase without verification (S3 review item 2).  On
 * failure *out is freed and left empty. */
canon_status canon_bsgs_build_verified(canon_bsgs *out, uint32_t n, const uint32_t *gens,
                                       size_t count);

/* spec 7.1 ordering of the orbits of K_level (the pointwise stabiliser of b_0..b_{level-1}):
 * orbit_id[v] = rank of the orbit of v, orbits ranked by their least point. */
void canon_bsgs_orbit_ids(const canon_bsgs *c, uint32_t level, uint32_t *orbit_id);

/* spec 7.2 tuple minimum (and spec 7.1 G stage): see canon_group_ops.tuple_min in
 * src/bsgs/group.h for the contract; c must be a verified chain.  CANON_INVALID_INPUT for an
 * entry of L >= n, CANON_RESOURCE_LIMIT / CANON_CAPACITY_LIMIT on allocation failure.  The
 * transient rebased chains it builds are not verified (S3 review item 2). */
canon_status canon_bsgs_tuple_min(const canon_bsgs *c, const uint32_t *L, uint32_t len,
                                  uint32_t *t_out, uint32_t *orbit_id_out,
                                  canon_bsgs_stats *stats);

#endif /* CANON_SRC_BSGS_CHAIN_H */
