/*
 * canon.h: public C17 API of the canon library (opaque handles only).
 *
 * Normative text: docs/specification.md v2.0.  Sections cited below refer to it.
 * Conventions (spec section 3): permutation arrays store p[v] = v^p and products act left to
 * right, (pq)[v] = q[p[v]].
 *
 * Status of this header: M-scaffold.  Only canon_version() and canon_version_string() are
 * implemented; every other entry point is a stub returning CANON_UNSUPPORTED_ACTION until
 * milestone M4 (docs/implementation-plan.md).  The argument lists of the stubbed functions
 * are PROVISIONAL and will be frozen at M4 together with the section 17 client vectors.
 */
#ifndef CANON_CANON_H
#define CANON_CANON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "canon/canon_version.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Status (spec section 3.2): never encoded as a sign; separate from validity flags. ---- */
typedef enum canon_status {
    CANON_COMPLETE = 0,
    CANON_CANCELLED = 1,
    CANON_CAPACITY_LIMIT = 2,
    CANON_RESOURCE_LIMIT = 3,
    CANON_INVALID_INPUT = 4,
    CANON_UNSUPPORTED_ACTION = 5,
    CANON_OUTPUT_ERROR = 6,
    CANON_INTERNAL_ERROR = 7
} canon_status; /* spec section 3.2: the eight statuses, in the order listed there */

/* ---- Objective tags (spec section 3 table): uint16 on the wire. ---- */
typedef uint16_t canon_objective; /* spec section 3: objective tags are uint16 */
enum {
    CANON_OBJECTIVE_CANONICAL_IMAGE = 0x0001,
    CANON_OBJECTIVE_LEX_MIN_IMAGE = 0x0002,
    CANON_OBJECTIVE_TRANSPORTER_ONE = 0x0003,
    CANON_OBJECTIVE_STABILISER = 0x0004,
    CANON_OBJECTIVE_CANONICAL_LABELING_COSET = 0x0005,
    CANON_OBJECTIVE_TRANSPORTER_COSET = 0x0006,
    CANON_OBJECTIVE_SIGNED_CANONICAL_IMAGE = 0x0007,
    CANON_OBJECTIVE_CONSTRAINT_ONE = 0x0008,
    CANON_OBJECTIVE_CONSTRAINT_ENUM = 0x0009
};

/* ---- Frozen identifiers (spec sections 3, 4.1, 4.3, 4.4, 7.1). ---- */
typedef uint16_t canon_schema;   /* spec section 4.1: U16(schema) in the stream header */
typedef uint16_t canon_action;   /* spec section 4.1: U16(action) in the stream header */
typedef uint16_t canon_profile;  /* spec section 4.3: profile tag */
typedef uint16_t canon_encoding; /* spec section 4.1: encoding ID */
typedef uint16_t canon_order;    /* spec sections 4.3, 4.4: comparison/minimum order ID */
enum {
    CANON_SCHEMA_EXT_DAG_1 = 1,        /* spec section 4.1: schema=1 */
    CANON_ACTION_ATOM_TRANSPORT_1 = 1, /* spec section 4.1: action=1 */
    CANON_PROFILE_NO_TREE = 0,         /* spec section 4.3: 0x0000 for coset enumeration */
    CANON_PROFILE_P1 = 1,              /* spec section 7.1: BASE-ORBIT-GRAPH-1 */
    CANON_ENCODING_CDAG_2 = 2,         /* spec section 4.1: CDAG-2 */
    CANON_ORDER_CDAG_BYTE_1 = 1,       /* spec section 4.3: CDAG-BYTE-1 */
    CANON_ORDER_SIMPLE_UPPER_1 = 2     /* spec section 4.4: SIMPLE-UPPER-1 */
};

/* ---- Per-result evidence flags (spec section 3.2). False means unproved, not false. ---- */
typedef struct canon_result_flags {
    bool witness_valid;
    bool image_canonical;
    bool minimum_proved;
    bool subgroup_verified;
    bool stabiliser_complete;
    bool transport_exhausted;
    bool zero_certified;
    bool nonzero_certified;
    bool encoding_complete;
} canon_result_flags; /* spec section 3.2: nine independent booleans */

/* ---- Opaque retain/release handles (spec section 17). ---- */
typedef struct canon_context canon_context;       /* spec section 17 */
typedef struct canon_registry canon_registry;     /* spec section 17 */
typedef struct canon_group canon_group;           /* spec section 17 */
typedef struct canon_object canon_object;         /* spec section 17 */
typedef struct canon_problem canon_problem;       /* spec section 17 */
typedef struct canon_workspace canon_workspace;   /* spec section 17 */
typedef struct canon_result canon_result;         /* spec section 17 */
typedef struct canon_checkpoint canon_checkpoint; /* spec sections 17, 18 */

/* Output sink (spec section 17): ordered borrowed chunks valid only during the callback.
 * Provisional: returns 0 = continue, 1 = pause (offset retained), other = fail (OUTPUT_ERROR).
 * *accepted receives the number of bytes taken. */
typedef int (*canon_sink_fn)(void *user, const uint8_t *chunk, size_t length, size_t *accepted);

/* ---- Version (implemented, src/api/version.c). ---- */
/* Build metadata (spec section 4.1 versioned IDs): packed (major << 16) | (minor << 8) | patch,
 * equal to CANON_VERSION_NUMBER. */
uint32_t canon_version(void);
/* Build metadata (spec section 4.1 versioned IDs): static "major.minor.patch" string. */
const char *canon_version_string(void);

/* ---- Section 17 entry points: declared, stubbed (src/api/stubs.c). ----
 * Every function below currently returns CANON_UNSUPPORTED_ACTION and, when an out-pointer is
 * supplied, stores NULL there.  TODO(M4): spec section 17 */

/* spec section 17, 9.4: build an immutable group from `generator_count` generators, each
 * `degree` uint32 images (flat array, p[v] = v^p). */
canon_status canon_group_create(canon_context *ctx, uint32_t degree, const uint32_t *generators,
                                size_t generator_count, canon_group **out);

/* spec section 17, 4.1: build an object from a CDAG-2 stream (copied by default). */
canon_status canon_object_create(canon_context *ctx, canon_schema schema, canon_action action,
                                 uint32_t degree, const uint8_t *stream, size_t stream_length,
                                 canon_object **out);

/* spec section 17, 3: bind group, object, objective, profile, encoding and order explicitly. */
canon_status canon_problem_create(canon_context *ctx, const canon_group *group,
                                  const canon_object *object, canon_objective objective,
                                  canon_profile profile, canon_encoding encoding, canon_order order,
                                  canon_problem **out);

/* spec section 17: a worker-owned mutable workspace (one active owner). */
canon_status canon_workspace_create(canon_context *ctx, canon_workspace **out);

/* spec section 17, 3, 7: solve one problem; the result owns its immutable evidence. */
canon_status canon_solve(canon_workspace *workspace, const canon_problem *problem,
                         canon_result **out);

/* spec section 17: per-input statuses, input order preserved regardless of scheduling. */
canon_status canon_solve_batch(canon_workspace *workspace, const canon_problem *const *problems,
                               size_t count, canon_result **results, canon_status *statuses);

/* spec section 17: checks membership and exact action of the witness, not canonicity. */
canon_status canon_result_verify_witness(const canon_result *result, bool *valid);

/* spec sections 17, 4.3: canonical bytes only when the image is complete (or signed zero `00`). */
canon_status canon_result_encode(const canon_result *result, canon_sink_fn sink, void *user);

/* spec sections 17, 18: write a checkpoint of an interrupted search. */
canon_status canon_checkpoint_write(const canon_workspace *workspace, canon_sink_fn sink,
                                    void *user);

/* spec sections 17, 18: read a checkpoint (trusted resume only; see spec section 18). */
canon_status canon_checkpoint_read(canon_context *ctx, const uint8_t *bytes, size_t length,
                                   canon_checkpoint **out);

#ifdef __cplusplus
}
#endif

#endif /* CANON_CANON_H */
