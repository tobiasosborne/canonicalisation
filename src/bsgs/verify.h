/*
 * Internal header: the independent chain verifier (spec 9.1 third paragraph; slice S3,
 * docs/slices/S3.md 2.3; detailed plan WP2.4).
 *
 * src/bsgs/verify.c reads only the chain data layout of src/bsgs/chain.h and uses only
 * src/perm/perm.h and src/bsgs/provenance.h: it calls none of the constructor's functions
 * (no sift, transporter or orbit routine of chain.c), so it does not inherit the
 * constructor's assumptions.  It was written before chain.c (docs/slices/S3-notes.md).
 */
#ifndef CANON_SRC_BSGS_VERIFY_H
#define CANON_SRC_BSGS_VERIFY_H

#include <stdint.h>

#include "bsgs/chain.h"
#include "canon/canon.h"

/* The first violated condition, in the order the verifier checks them.  The numbers refer to
 * the list of docs/slices/S3.md 2.3 (spec 9.1 third paragraph). */
typedef enum canon_bsgs_reason {
    CANON_BSGS_VALID = 0,
    CANON_BSGS_UNCHECKED,            /* no verdict: scratch could not be allocated */
    CANON_BSGS_BAD_STRUCTURE,        /* sizes, ranges or ids that cannot be read safely */
    CANON_BSGS_LAST_NOT_TRIVIAL,     /* 9: levels[depth] has generators */
    CANON_BSGS_NOT_BIJECTION,        /* 1: an input, stored generator or inverse */
    CANON_BSGS_INPUT_MISMATCH,       /* 2: recorded inputs differ from the original inputs */
    CANON_BSGS_PROVENANCE_MISMATCH,  /* 2: a record is malformed or re-derives another array */
    CANON_BSGS_INVERSE_MISMATCH,     /* 2: a stored inverse is not the generator's inverse */
    CANON_BSGS_PREFIX_NOT_FIXED,     /* 3: a level-i generator moves some b_j, j < i */
    CANON_BSGS_NOT_NESTED,           /* S_{i+1} is not contained in S_i (see verify.c) */
    CANON_BSGS_ORBIT_ROOT,           /* 4: orbit[0] is not the base point, or the root has a
                                        parent */
    CANON_BSGS_ORBIT_DUPLICATE,      /* 4: a point occurs twice in an orbit */
    CANON_BSGS_ORBIT_POS_MISMATCH,   /* 4: orbit_pos does not invert orbit */
    CANON_BSGS_ORBIT_EDGE,           /* 4: a stored edge is not parent^s = point for a level
                                        generator s and a parent in the orbit */
    CANON_BSGS_ORBIT_UNREACHABLE,    /* 4: following parents does not reach the base point */
    CANON_BSGS_ORBIT_NOT_CLOSED,     /* 5 */
    CANON_BSGS_TRANSVERSAL_IMAGE,    /* 6: reconstructed t_b does not send the base to b */
    CANON_BSGS_SCHREIER_RESIDUE,     /* 7: t_b s t_(b^s)^-1 does not sift to the identity */
    CANON_BSGS_INPUT_NOT_MEMBER,     /* 8: an original input does not sift to the identity */
    CANON_BSGS_ORDER_MISMATCH        /* 10: order is not the product of the orbit lengths */
} canon_bsgs_reason;

/* spec 9.1: verify c against the original validated input generators (`inputs`, a flat array
 * of input_count image arrays of length c->n).  Sets c->verified = true only when every
 * condition holds (*reason = CANON_BSGS_VALID); clears it first.  Returns CANON_COMPLETE when
 * a verdict was reached (valid or not), or CANON_CAPACITY_LIMIT / CANON_RESOURCE_LIMIT when
 * its scratch could not be allocated (*reason = CANON_BSGS_UNCHECKED, c->verified false). */
canon_status canon_bsgs_verify(canon_bsgs *c, const uint32_t *inputs, uint32_t input_count,
                               canon_bsgs_reason *reason);

/* A short name for a reason (tests and diagnostics). */
const char *canon_bsgs_reason_name(canon_bsgs_reason reason);

#endif /* CANON_SRC_BSGS_VERIFY_H */
