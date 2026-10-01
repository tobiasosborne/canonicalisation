/*
 * canon.h: public C17 API of the canon library (opaque handles only).
 *
 * Normative text: docs/specification.md v2.0.  Sections cited below refer to it.
 * Conventions (spec section 3): permutation arrays store p[v] = v^p and products act left to
 * right, (pq)[v] = q[p[v]].
 *
 * Status of this header: slice S1 (docs/slices/S1.md).  Implemented: the version functions, the
 * context and capacity descriptor, retain/release for every handle below, groups (explicit
 * enumeration backend), subset objects, problems for CANONICAL_IMAGE under profile P1 with
 * encoding CDAG-2 and order CDAG-BYTE-1, workspaces, canon_solve, the result accessors and
 * canon_result_encode.  Every other entry point is a stub returning CANON_UNSUPPORTED_ACTION
 * until its slice lands (canon_object_create from a stream: S5; canon_solve_batch: S8;
 * canon_result_verify_witness: S4; checkpoints: M6).  ALL argument lists are PROVISIONAL
 * until M4 and will be frozen there together with the section 17 client vectors.
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
 * *accepted receives the number of bytes taken.
 * Slice S1: pause/resume is DEFERRED to slice S8; a return of 1 is treated as failure
 * (CANON_OUTPUT_ERROR).  On return 0, *accepted must be in 1..length; the remaining bytes of
 * the chunk are offered again in a following call.  *accepted == 0 with return 0, or
 * *accepted > length, is a failure (CANON_OUTPUT_ERROR). */
typedef int (*canon_sink_fn)(void *user, const uint8_t *chunk, size_t length, size_t *accepted);

/* ---- Version (implemented, src/api/version.c). ---- */
/* Build metadata (spec section 4.1 versioned IDs): packed (major << 16) | (minor << 8) | patch,
 * equal to CANON_VERSION_NUMBER. */
uint32_t canon_version(void);
/* Build metadata (spec section 4.1 versioned IDs): static "major.minor.patch" string. */
const char *canon_version_string(void);

/* ---- Capacity descriptor and context (spec sections 11.1, 17).  PROVISIONAL until M4. ---- */

/* spec 11.1: problem-time capacity descriptor.  Zero means "use the context default". */
typedef struct canon_capacity {
    uint32_t max_n;            /* degree admitted */
    uint64_t max_group_order;  /* S1 explicit backend: largest |G| that may be enumerated */
    uint64_t max_search_nodes; /* spec 11.1 logical work quota: NODE tokens in the reference
                                  traversal */
    uint64_t max_output_bytes; /* canonical stream bytes */
} canon_capacity;

/* spec 17: create a context holding the capacity defaults.  `defaults` may be NULL; a NULL
 * descriptor or a zero field selects the built-in default for that field: max_n = 4096,
 * max_group_order = 1 << 16, max_search_nodes = 1 << 20, max_output_bytes = 1 << 26.
 * Handles created from a context copy what they need and do not keep it alive. */
canon_status canon_context_create(const canon_capacity *defaults, canon_context **out);
/* spec 17: release the context; NULL is a no-op. */
void canon_context_release(canon_context *ctx);

/* ---- Retain/release (spec 17: opaque handles; releasing a failed or partial handle is always
 * valid; release of NULL is a no-op; retain of NULL is a no-op).
 * Sharing (spec 17: "immutable contexts/registries/groups can be shared"): reference counts
 * are atomic, so groups, objects, problems and results may be shared between threads for
 * retain/release and for read-only use (accessors, as arguments to builders and canon_solve).
 * A count is bookkeeping, not logical state, which is why canon_problem_create may retain
 * through its const group and object arguments.  A workspace has one active owner at a time;
 * a context is not reference counted and must outlive only the calls that receive it. ---- */
void canon_group_retain(canon_group *group);
void canon_group_release(canon_group *group);
void canon_object_retain(canon_object *object);
void canon_object_release(canon_object *object);
void canon_problem_retain(canon_problem *problem);
void canon_problem_release(canon_problem *problem);
void canon_workspace_retain(canon_workspace *workspace);
void canon_workspace_release(canon_workspace *workspace);
void canon_result_retain(canon_result *result);
void canon_result_release(canon_result *result);

/* ---- Section 17 entry points.  On any failure an out-pointer that was supplied receives NULL.
 * NULL required arguments give CANON_INVALID_INPUT. ---- */

/* spec section 17, 9: build an immutable group from `generator_count` generators, each
 * `degree` uint32 images (flat array, p[v] = v^p).  Identity and repeated generators are
 * allowed; zero generators give the trivial group.  S1 builds the explicit-enumeration backend
 * and returns CANON_CAPACITY_LIMIT when degree exceeds the context's max_n or the closure would
 * exceed the context's max_group_order (checked before each growth of the element table, never
 * after); CANON_INVALID_INPUT when a generator is not a bijection of {0..degree-1}. */
canon_status canon_group_create(canon_context *ctx, uint32_t degree, const uint32_t *generators,
                                size_t generator_count, canon_group **out);

/* spec 17 input builder (copies data): a subset of atoms of {0..degree-1}; duplicates permitted
 * and deduplicated (spec 4.2: sets deduplicate equal children); degree above the context's
 * max_n gives CANON_CAPACITY_LIMIT (checked first); an atom >= degree gives
 * CANON_INVALID_INPUT.  `atoms` may be NULL when count is 0. */
canon_status canon_object_create_subset(canon_context *ctx, uint32_t degree,
                                        const uint32_t *atoms, size_t count, canon_object **out);

/* spec section 17, 4.1: build an object from a CDAG-2 stream (copied by default).
 * STUB until slice S5: returns CANON_UNSUPPORTED_ACTION. */
canon_status canon_object_create(canon_context *ctx, canon_schema schema, canon_action action,
                                 uint32_t degree, const uint8_t *stream, size_t stream_length,
                                 canon_object **out);

/* spec section 17, 3: bind group, object, objective, profile, encoding and order explicitly.
 * `capacity` may be NULL (all context defaults); a zero field selects the context default.
 * The problem retains the group and the object.  S1 validation, in this order:
 * CANON_UNSUPPORTED_ACTION for an objective other than CANONICAL_IMAGE (0x0001), a profile
 * other than P1, an encoding other than CDAG-2 or an order other than CDAG-BYTE-1;
 * CANON_INVALID_INPUT for a degree mismatch between group and object; CANON_CAPACITY_LIMIT
 * when the degree exceeds max_n, the group order exceeds max_group_order, or the exact
 * canonical stream length (determined by the subset's size, spec 11.1) exceeds
 * max_output_bytes. */
canon_status canon_problem_create(canon_context *ctx, const canon_group *group,
                                  const canon_object *object, canon_objective objective,
                                  canon_profile profile, canon_encoding encoding, canon_order order,
                                  const canon_capacity *capacity, canon_problem **out);

/* spec section 17: a worker-owned mutable workspace (one active owner).  Storage is allocated
 * lazily on the first solve, sized to the problem's degree, and reused across solves. */
canon_status canon_workspace_create(canon_context *ctx, canon_workspace **out);

/* spec section 17, 3, 7: solve one problem; the result owns its immutable evidence and never
 * aliases workspace memory.  Returns the result's status (spec 3.2):
 * CANON_COMPLETE with witness_valid, image_canonical and encoding_complete true and every other
 * flag false; CANON_CAPACITY_LIMIT when the P1 reference traversal has more NODE tokens than
 * max_search_nodes (spec 11.1), with a result that has no trace, no bytes, no witness and all
 * flags false; CANON_RESOURCE_LIMIT on allocation failure (a result is returned when it could
 * itself be allocated).  CANON_INVALID_INPUT for NULL arguments (no result). */
canon_status canon_solve(canon_workspace *workspace, const canon_problem *problem,
                         canon_result **out);

/* ---- Result accessors (spec section 3.2).  A NULL result reads as no data. ---- */

/* spec 3.2: the result's status (CANON_INVALID_INPUT for NULL). */
canon_status canon_result_status(const canon_result *result);
/* spec 3.2: the nine evidence flags (all false for NULL). */
canon_result_flags canon_result_get_flags(const canon_result *result);
/* spec 7.2: the complete P1 trace of the selected leaf; NULL and *length = 0 if not produced. */
const uint8_t *canon_result_trace(const canon_result *result, size_t *length);
/* spec 3: the witness t as an image array of length *degree (x^t = canonical image); NULL and
 * *degree = 0 if not produced.  For degree 0 a produced witness is a non-NULL pointer with
 * *degree = 0.  S1 reports the least LEAF witness attaining the minimal (trace, bytes) key, as
 * spec 7.4's preamble prescribes for the golden cases ("minimise among the witnesses that
 * actually attain that key").  S1 does not compute Aut_G(x), which the spec 3 deterministic
 * witness (least image array over all g with x^g = c, i.e. over Aut_G(x) t) is defined by.
 * For the UNPRUNED tree the two coincide: by tree equivariance (spec 7.2, t_(L^a) = a^-1 t_L)
 * the leaves attaining the least key carry exactly the coset Aut_G(x) t, and this is checked
 * over the whole T1 tier by tests/python/test_e2e.py.  Once pruning lands (S7) the leaf set
 * shrinks and the deterministic witness needs the complete stabiliser (S4). */
const uint32_t *canon_result_witness(const canon_result *result, uint32_t *degree);
/* spec 4.3: the canonical CDAG-2 bytes, only when image_canonical && encoding_complete; else
 * NULL and *length = 0. */
const uint8_t *canon_result_bytes(const canon_result *result, size_t *length);

/* spec section 17: per-input statuses, input order preserved regardless of scheduling.
 * STUB until slice S8: returns CANON_UNSUPPORTED_ACTION. */
canon_status canon_solve_batch(canon_workspace *workspace, const canon_problem *const *problems,
                               size_t count, canon_result **results, canon_status *statuses);

/* spec section 17: checks membership and exact action of the witness, not canonicity.
 * STUB until slice S4: returns CANON_UNSUPPORTED_ACTION and *valid = false. */
canon_status canon_result_verify_witness(const canon_result *result, bool *valid);

/* spec sections 17, 4.3: stream the canonical bytes to `sink` in one or more ordered chunks,
 * only when image_canonical && encoding_complete (signed zero `00` arrives with S6).
 * CANON_COMPLETE after the last byte is accepted; CANON_INVALID_INPUT for NULL arguments or a
 * result without canonical bytes; CANON_OUTPUT_ERROR when the sink fails (see canon_sink_fn;
 * pause is deferred to S8).  The result is never modified. */
canon_status canon_result_encode(const canon_result *result, canon_sink_fn sink, void *user);

/* spec sections 17, 18: write a checkpoint of an interrupted search.  STUB until M6. */
canon_status canon_checkpoint_write(const canon_workspace *workspace, canon_sink_fn sink,
                                    void *user);

/* spec sections 17, 18: read a checkpoint (trusted resume only; see spec section 18).
 * STUB until M6. */
canon_status canon_checkpoint_read(canon_context *ctx, const uint8_t *bytes, size_t length,
                                   canon_checkpoint **out);

#ifdef __cplusplus
}
#endif

#endif /* CANON_CANON_H */
