/*
 * Internal header: certificate v0 (`CERT-0`) of a CANONICAL_IMAGE solve (spec 3.2
 * CHECKED_CERTIFICATE, 7.3, 20; slice S7, docs/slices/S7.md 3.3, 3.5; detailed plan WP4.7).
 *
 * A certificate is a byte stream that lets the independent checker (checker/, no shared code)
 * re-derive the P1 search of (x, G) without trusting the engine: every refinement step is
 * stated with checkable group data (a chain of G rebased to the G stage's M), every
 * automorphism used for pruning is listed, every child of every node is recorded as explored
 * or pruned with its justification, and the final claim is stated.  Layout (all integers big
 * endian, spec 4.1 U16/U32/B; `dense(p)` is n x U32 images, p[v] = v^p):
 *
 *   magic "CNC0" | U16(objective = 0x0001) | U16(profile = 0x0001) | U16(work_policy)
 *   | U16(encoding = 0x0002) | U16(order = 0x0001) | U32(n) | B(CDAG-2 stream of x)
 *   | U32(k) | k x dense(generator of G, input order)
 *   | CHAIN(root: a verified chain of G whose inputs are those k generators)
 *   | U32(m) | m x dense(automorphism)         (the automorphisms referenced below, plus the
 *                                               input generators inserted into A_known)
 *   | NODE(root)                                (node records in depth-first order)
 *   | B(best trace) | B(best bytes) | dense(witness t) | U64(nodes explored)
 *
 *   NODE = U32(depth) | U32(individualised atom, 0xffffffff at the root) | U32(sweeps)
 *          | sweeps x SWEEP | U8(kind) | LEAF or BRANCH
 *   SWEEP = U32(k_O) | k_O x U32(cell size after O) | U32(f) | f x U32(F, singletons in
 *           partition order) | dense(u) | f x U32(M = F^u) | CHAIN(G rebased: base prefix M)
 *           | U32(k_G) | k_G x U32(cell size after G)
 *   kind 0x00 LEAF   = dense(t_L)
 *   kind 0x01 BRANCH = U32(target cell position) | U32(cell size) | cell size x CHILD
 *   CHILD = U8(0x01) | U32(b) | NODE                        (explored)
 *         | U8(0x00) | U32(b) | U32(b') | U32(automorphism index)   (pruned: b'^a = b, a fixes
 *                                                                    the node's prefix pointwise,
 *                                                                    b' an explored child)
 *   CHAIN = U32(n) | U32(depth) | depth x LEVEL | U32(g) | g x (U32(provenance node) | dense)
 *           | U32(p) | p x (U32(kind) U32(a) U32(b)) | U32(i) | i x dense(input)
 *   LEVEL = U32(base point) | U32(orbit length r) | r x U32(orbit, discovery order)
 *           | r x U32(parent point) | r x U32(parent generator id) | U32(s) | s x U32(generator id)
 *           (parent entries of the root position are 0xffffffff; the chain's generator table is
 *            indexed by generator id, its provenance records by node, src/bsgs/provenance.h:
 *            kind 1 INPUT a, 2 INVERSE a, 3 PRODUCT a b with a acting first)
 *
 * Deviations from the drafted format of docs/slices/S7.md 3.3, recorded in S7-notes.md: a
 * sweep count per node, explicit child points b (so the checker never depends on the engine's
 * internal cell member order), one generator id per generator (no separate id list) and the
 * provenance node of every generator in the chain.  The header carries U16(work_policy)
 * (decision D14).
 *
 * The writer buffers the automorphism table and the node records separately (the table grows
 * while the search runs) and assembles the stream in canon_cert_finish.  All sizes are
 * checked (spec 11.1); a failure (CANON_RESOURCE_LIMIT, CANON_CAPACITY_LIMIT) deactivates the
 * writer and is returned to the search, which fails the run with it: a requested certificate
 * is never silently dropped (spec 17: no fabricated completion).
 */
#ifndef CANON_SRC_SEARCH_CERTIFICATE_H
#define CANON_SRC_SEARCH_CERTIFICATE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "bsgs/chain.h"
#include "bsgs/group.h"
#include "canon/canon.h"
#include "encoding/wire.h"
#include "object/object.h"
#include "partition/partition.h"
#include "refine/p1.h"

#define CANON_CERT_NONE UINT32_MAX

typedef struct canon_cert {
    canon_buf head;     /* header through the root chain */
    canon_buf auts;     /* automorphism arrays */
    uint32_t aut_count;
    canon_buf nodes;    /* node records */
    canon_buf out;      /* the assembled certificate (canon_cert_finish) */
    canon_bsgs root;    /* the verified chain of G the certificate embeds */
    canon_bsgs rebased; /* transient: G rebased to a G stage's M */
    uint32_t n;
    uint32_t *m;        /* m_cap entries: M of the current G stage (grow-only scratch) */
    uint32_t m_cap;
    bool active;        /* begun and not yet finished or failed */
    bool complete;      /* out holds a finished certificate */
    size_t sweeps_at;   /* offset in nodes of the current node's sweep count */
    uint32_t sweeps;    /* sweeps recorded for the current node */
    uint64_t chains;    /* rebased chains written (counter for the notes) */
} canon_cert;

/* Zero state, no allocation. */
void canon_cert_init(canon_cert *c);
/* Release all storage; valid on an initialised or failed writer. */
void canon_cert_free(canon_cert *c);
/* Forget the previous certificate; keep the buffers' storage (spec 17 reusable workspace). */
void canon_cert_reset(canon_cert *c);

/* Start a certificate for the P1 canonical image of x under g (same degree): the header with
 * `work_policy`, the stream of x, the generators kept by the handle (canon_group_set_generators)
 * and a verified chain of G built here from them (spec 9.1 "exact verification is mandatory").
 * CANON_RESOURCE_LIMIT / CANON_CAPACITY_LIMIT on allocation failure, CANON_INTERNAL_ERROR if
 * the verifier rejects the chain. */
canon_status canon_cert_begin(canon_cert *c, const canon_group *g, const canon_root *x,
                              uint16_t work_policy);

/* Append the automorphism a (n images) to the table; *index receives its index. */
canon_status canon_cert_automorphism(canon_cert *c, const uint32_t *a, uint32_t *index);

/* The observer handed to canon_p1_refine_node, recording each sweep of the current node. */
canon_p1_observer canon_cert_observer(canon_cert *c);

/* Node record: open it before refinement (atom CANON_CERT_NONE at the root), close the sweep
 * list after refinement, then exactly one of leaf or branch, and for a branch one child entry
 * per cell member. */
canon_status canon_cert_node_begin(canon_cert *c, uint32_t depth, uint32_t atom);
canon_status canon_cert_node_refined(canon_cert *c);
canon_status canon_cert_leaf(canon_cert *c, const uint32_t *t);
canon_status canon_cert_branch(canon_cert *c, uint32_t cell, uint32_t size);
canon_status canon_cert_child_explored(canon_cert *c, uint32_t b);
canon_status canon_cert_child_pruned(canon_cert *c, uint32_t b, uint32_t rep, uint32_t aut);

/* The final claim: the least key (trace, bytes), the least explored-leaf witness t attaining
 * it and the number of nodes explored (the spec 11.1 count under the header's work policy;
 * the checker compares it with the node records and, under policy 0x0001, requires that no
 * child was pruned); assembles c->out.  Afterwards canon_cert_bytes hands the stream out. */
canon_status canon_cert_finish(canon_cert *c, const uint8_t *trace, size_t trace_len,
                               const uint8_t *bytes, size_t bytes_len, const uint32_t *t,
                               uint64_t nodes);

/* The finished certificate, or NULL with *length = 0. */
const uint8_t *canon_cert_bytes(const canon_cert *c, size_t *length);

/* Serialise a chain in the CHAIN layout above (shared with the tests). */
canon_status canon_cert_write_chain(canon_buf *out, const canon_bsgs *chain);

#endif /* CANON_SRC_SEARCH_CERTIFICATE_H */
