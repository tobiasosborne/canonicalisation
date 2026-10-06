/*
 * Internal header: the certificate v0 writer, CERT-0 (slice S7 step 2; docs/slices/S7.md 3.3,
 * 3.5; the byte grammar is docs/certificate-format.md).  PROVISIONAL until the independent
 * checker (S7 step 3) accepts it.
 *
 * The writer observes one P1 canonical-image run through the recorder hooks of
 * src/refine/p1.h (canon_p1_search_run_recorded) and writes, in DFS order, every node's
 * refinement sweeps (cell sizes after O; F, u, M = F^u, a chain of G with base prefix M and the
 * cell sizes after G), its leaf transporter or its branch with one entry per member of the
 * target cell in increasing atom id (EXPLORED, followed by the child's record, or PRUNED with
 * the explored representative and an automorphism).  The G-stage chains are rebuilt by the
 * writer (canon_bsgs_rebase of its own root chain with prefix M) and cross-checked against
 * the engine's orbit ids; every automorphism is verified by exact action before it is written
 * (spec 7.3, spec 14.3 R2).  Any mismatch is CANON_INTERNAL_ERROR: the writer never emits a
 * certificate that disagrees with the run.
 *
 * Convention (spec 3): permutations are dense image arrays p[v] = v^p; products act left to
 * right, (pq)[v] = q[p[v]].
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
#include "refine/p1.h"
#include "search/p1_tree.h"

/* Entry and tail tags of CERT-0 (docs/certificate-format.md). */
#define CANON_CERT_LEAF 0x00u
#define CANON_CERT_BRANCH 0x01u
#define CANON_CERT_EXPLORED 0x00u
#define CANON_CERT_PRUNED 0x01u

/* Writer state, owned by a workspace and reused across solves (buffers only grow).  Between
 * canon_cert_begin and canon_cert_end it borrows the group and the root of the run; after
 * canon_cert_end it borrows nothing. */
typedef struct canon_cert {
    canon_buf head;          /* magic .. root CHAIN */
    canon_buf nodes;         /* node records in DFS order */
    canon_perm_table autos;  /* the automorphisms named by PRUNED entries, first-use order */
    canon_buf out;           /* the assembled certificate (canon_cert_finish) */
    canon_bsgs root;         /* verified chain of G from the recorded input generators */
    bool have_root;
    const canon_group *g;    /* borrowed during a run */
    const canon_root *x;     /* borrowed during a run */
    canon_root_image img;    /* x^a for the automorphism checks (cleared after each use) */
    uint32_t n, cap;         /* degree of the run; words allocated per scratch array */
    uint32_t *path;          /* path[d - 1] = the atom individualised at depth d */
    uint32_t *last_u;        /* u of the last G stage */
    uint32_t *M;             /* M = F^u of the current G stage */
    uint32_t *ids;           /* orbit ids of the rebuilt chain */
    uint32_t *scratch;       /* membership scratch */
    uint32_t last_f;         /* |F| of the last G stage */
    size_t sweep_patch;      /* offset in `nodes` of the current node's U32(sweeps) */
    uint32_t sweeps;         /* sweeps of the current node so far */
    uint64_t node_records;   /* NODE records written */
    canon_p1_recorder hooks; /* ctx = this writer */
} canon_cert;

void canon_cert_init(canon_cert *c);
void canon_cert_free(canon_cert *c);

/* spec 7.1-7.3 (brief 3.3): start a certificate for the canonical image of x under g with
 * work policy `policy` (1 or 2): the header, the k recorded input generators of g
 * (canon_group_input_generators, as given) and the root chain (canon_bsgs_build_verified of
 * those generators; its input rows are checked equal to the header's).  Empties the node and
 * automorphism lists.  CANON_UNSUPPORTED_ACTION for a nested root or a group without recorded
 * generators; CANON_CAPACITY_LIMIT / CANON_RESOURCE_LIMIT as the writers.  Call canon_cert_end
 * on every outcome. */
canon_status canon_cert_begin(canon_cert *c, const canon_group *g, const canon_root *x,
                              canon_work_policy policy);

/* The recorder to pass to canon_p1_search_run_recorded between begin and end. */
const canon_p1_recorder *canon_cert_hooks(canon_cert *c);

/* After a COMPLETE recorded run `s`: assemble c->out = head || U32(a) || AUT[a] || nodes ||
 * FINAL (B(best trace) B(best bytes) best_t Nat(nodes)).  CANON_INTERNAL_ERROR if the number of
 * NODE records differs from s->nodes. */
canon_status canon_cert_finish(canon_cert *c, const canon_p1_search *s);

/* Drop the borrowed group and root and the root chain; idempotent; buffers are kept. */
void canon_cert_end(canon_cert *c);

/* brief 3.5 (docs/certificate-format.md CHAIN): append the serialised chain `ch`.  Its input
 * table is written by reference: input j is refs[j] (refs NULL: j), for j < ch->inputs.count. */
canon_status canon_cert_chain_write(canon_buf *out, const canon_bsgs *ch, const uint32_t *refs);

#endif /* CANON_SRC_SEARCH_CERTIFICATE_H */
