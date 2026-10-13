/*
 * Internal header: the verified known-automorphism subgroup A_known and the prefix-stabiliser
 * orbits used for pruning (spec 7.3; slice S7, docs/slices/S7.md 3.1-3.2; detailed plan WP4.5).
 *
 * spec 7.3: "Known automorphisms form a verified subgroup A_known <= A.  At each node intersect
 * with the stabiliser of its constraints before sibling pruning.  Root orbits alone cannot
 * justify deeper pruning."  A_known is kept as a verified stabiliser chain (src/bsgs/chain.h)
 * whose generators are automorphisms checked by exact action (x^a = x); in S7 the only source
 * is the input generators of G that fix x (S7 brief 3.1 source i).  At a node with individualised
 * prefix (a_1, ..., a_d) the pointwise stabiliser A_S of the prefix in A_known is the suffix of
 * A_known rebased to that prefix (spec 9.2 base change, without verification: the rebased chain
 * is transient and the certificate checker re-verifies every automorphism it is handed).  The
 * orbits of A_S on the target cell are computed by breadth-first search over the level-d strong
 * generators, keeping the Schreier tree so that for every point b of an orbit with
 * representative r an explicit element a of A_S with r^a = b can be reconstructed as a product
 * of those generators (the automorphism the certificate names for a pruned child, S7 brief
 * 3.3).  Slice S7b (docs/slices/S7b.md) adds the second source of A_known, implicit
 * automorphisms t_L t_best^-1 from equal leaf keys (spec 7.3 "Verify implicit automorphisms by
 * exact membership and object equality before publication"; the membership test is the
 * caller's, the action check is here), and the epoch counter the search uses to notice that
 * A_known grew.
 *
 * Convention (spec 3): p[v] = v^p; (pq)[v] = q[p[v]].
 */
#ifndef CANON_SRC_SYMMETRY_SYMMETRY_H
#define CANON_SRC_SYMMETRY_SYMMETRY_H

#include <stdbool.h>
#include <stdint.h>

#include "bsgs/chain.h"
#include "canon/canon.h"
#include "object/object.h"
#include "perm/perm.h"

/* Counters of one run (docs/slices/S7-notes.md; no timings). */
typedef struct canon_symmetry_stats {
    uint64_t candidates; /* automorphism candidates offered to canon_symmetry_insert */
    uint64_t fixing;     /* candidates that fix x (members or not) */
    uint64_t inserted;   /* candidates that enlarged A_known (verified rebuilds) */
    uint64_t rebases;    /* transient rebases of A_known to a node prefix */
    uint64_t orbits;     /* orbits of A_S computed on target cells */
} canon_symmetry_stats;

typedef struct canon_symmetry {
    uint32_t n;              /* degree of the current solve */
    uint32_t cap;            /* degree the per-point arrays are allocated for (grow-only) */
    bool ready;              /* arrays allocated and A_known reset for degree n */
    canon_bsgs a;            /* A_known: a verified chain of <gens> (trivial while gens is empty) */
    canon_perm_table gens;   /* the automorphisms inserted into A_known, in insertion order */
    canon_bsgs stab;         /* transient: A_known rebased so that its base starts with the
                                current prefix; levels[stab_depth] generates A_S */
    uint32_t stab_depth;     /* prefix length the rebase was made for (0: stab is unused and A_S
                                is A_known itself) */
    uint32_t *scratch;       /* cap: sift residue */
    uint32_t *mark;          /* cap: BFS stamp per point */
    uint32_t stamp;
    uint32_t *parent_point;  /* cap: Schreier tree of the last cell-orbit computation */
    uint32_t *parent_gen;    /* cap: generator id (into the stabiliser chain's table) of the edge */
    uint32_t *rep;           /* cap: orbit representative of every marked point */
    uint32_t *queue;         /* cap: BFS queue, then the edge path of a transporter */
    uint64_t epoch;          /* number of insertions so far (S7b: a change means A_known grew) */
    canon_symmetry_stats stats;
} canon_symmetry;

/* Zero state, no allocation. */
void canon_symmetry_init(canon_symmetry *s);
/* Release all storage; valid on an initialised or failed state. */
void canon_symmetry_free(canon_symmetry *s);

/* Start a solve of degree n: A_known becomes the trivial group, the per-point arrays are
 * (re)allocated when n exceeds the capacity, the counters are zeroed.  CANON_RESOURCE_LIMIT /
 * CANON_CAPACITY_LIMIT on allocation failure (the state is then not ready). */
canon_status canon_symmetry_reset(canon_symmetry *s, uint32_t n);

/* spec 7.3 "Known automorphisms form a verified subgroup A_known <= A": offer the permutation
 * g (an element of G, a bijection of {0..n-1}: the caller's contract) as an automorphism of x.
 * The action is checked exactly (x^g = x through canon_root_act_into into `img` and
 * canon_root_equal); if g does not fix x nothing changes and *inserted is false with
 * CANON_INVALID_INPUT.  Otherwise g is inserted into the verified chain of A_known unless it
 * is already a member (canon_bsgs_insert_verified: membership by one sift, a non-member
 * appended to `gens` and the chain rebuilt and verified), and *inserted says whether A_known
 * grew.  CANON_RESOURCE_LIMIT / CANON_CAPACITY_LIMIT / CANON_INTERNAL_ERROR as
 * canon_bsgs_insert_verified. */
canon_status canon_symmetry_insert(canon_symmetry *s, const canon_root *x, canon_root_image *img,
                                   const uint32_t *g, bool *inserted);

/* true iff A_known is the trivial group (no automorphism was inserted). */
bool canon_symmetry_trivial(const canon_symmetry *s);

/* spec 7.3 "At each node intersect with the stabiliser of its constraints before sibling
 * pruning": make the pointwise stabiliser A_S of the prefix (prefix[0..d-1], distinct points)
 * in A_known available for canon_symmetry_cell_orbits.  For d = 0 that is A_known itself;
 * otherwise A_known is rebased (spec 9.2, without verification) so that its base starts with
 * the prefix, and A_S is generated by the strong generators of level d.  Statuses as
 * canon_bsgs_rebase. */
canon_status canon_symmetry_stabiliser(canon_symmetry *s, const uint32_t *prefix, uint32_t d);

/* The orbits of A_S (the last canon_symmetry_stabiliser) on the `len` points of `cell`.
 * Without `root` (NULL), in cell order: a point is a representative iff no earlier point of the
 * cell lies in its orbit.  With `root` (len flags, by cell position), the representatives are
 * exactly the flagged points (S7b: the children already explored and the one about to be), and
 * only points reached from a representative are assigned to it.  Afterwards
 * canon_symmetry_rep(v) is the representative of every cell point v: itself for a
 * representative, and itself also for a point no representative reaches (with `root` only).
 * A_S preserves the target cell as a set (S7 brief 3.1 lemma), so every orbit met lies inside
 * the cell; the BFS from each representative over the level generators records the Schreier
 * tree for canon_symmetry_transporter.  Requires a ready state and distinct cell points. */
void canon_symmetry_cell_orbits(canon_symmetry *s, const uint32_t *cell, uint32_t len,
                                const uint8_t *root);

/* After canon_symmetry_cell_orbits: the representative of the cell point v (see above). */
uint32_t canon_symmetry_rep(const canon_symmetry *s, uint32_t v);

/* After canon_symmetry_cell_orbits: out (n entries) receives an element a of A_S with
 * rep(b)^a = b, the product s_1 s_2 ... s_k of the level generators along the Schreier tree
 * path rep(b) -> ... -> b (spec 3: s_1 acts first, so a[rep] = s_k[...s_1[rep]] = b).  Every
 * s_i fixes the prefix pointwise, hence so does a; a lies in A_known <= A. */
void canon_symmetry_transporter(canon_symmetry *s, uint32_t b, uint32_t *out);

#endif /* CANON_SRC_SYMMETRY_SYMMETRY_H */
