/*
 * canon.h: public C17 API of the canon library (opaque handles only).
 *
 * Normative text: docs/specification.md v2.0.  Sections cited below refer to it.
 * Conventions (spec section 3): permutation arrays store p[v] = v^p and products act left to
 * right, (pq)[v] = q[p[v]].
 *
 * Status of this header: slices S1 to S4 (docs/slices/S1.md, S2.md, S3.md, S4.md).
 * Implemented: the version functions, the context and capacity descriptor, retain/release for
 * every handle below, groups (S3: a verified stabiliser chain by default; the S1 explicit
 * enumeration backend stays selectable through canon_context_options), canon_group_order,
 * subset objects, coloured directed multigraph objects and the simple undirected graph wrapper
 * (S2), problems for CANONICAL_IMAGE under profile P1 and (S4) for the enumeration objectives
 * LEX_MIN_IMAGE (orders CDAG-BYTE-1 and SIMPLE-UPPER-1), TRANSPORTER_ONE, STABILISER and
 * TRANSPORTER_COSET under profile NO_TREE, all with encoding CDAG-2, workspaces, canon_solve,
 * the result accessors, canon_result_encode and canon_result_verify_witness (S4).  Every other
 * entry point is a stub returning CANON_UNSUPPORTED_ACTION until its slice lands
 * (canon_object_create from a stream: S5; canon_solve_batch: S8; checkpoints: M6).  ALL
 * argument lists are PROVISIONAL until M4 and will be frozen there together with the section
 * 17 client vectors.
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
    uint64_t max_group_order;  /* explicit backend only (S3): largest |G| that may be
                                  enumerated; ignored by the default chain backend */
    uint64_t max_search_nodes; /* spec 11.1 logical work quota of one solve: NODE tokens of the
                                  P1 reference traversal, plus (S4) the visit calls of every
                                  spec 8.1 coset enumeration the solve runs */
    uint64_t max_output_bytes; /* canonical stream bytes */
} canon_capacity;

/* spec 17: create an immutable context holding the capacity defaults and the default options
 * (canon_context_create_with_options).  `defaults` may be NULL; a NULL
 * descriptor or a zero field selects the built-in default for that field: max_n = 4096,
 * max_group_order = 1 << 16, max_search_nodes = 1 << 20, max_output_bytes = 1 << 26.
 * Handles created from a context copy what they need and do not keep it alive. */
canon_status canon_context_create(const canon_capacity *defaults, canon_context **out);
/* spec 17: release the context; NULL is a no-op. */
void canon_context_release(canon_context *ctx);

/* Group backend used by canon_group_create (slice S3; PROVISIONAL until M4).
 * CANON_BACKEND_CHAIN (the default): a deterministic Schreier-Sims stabiliser chain checked by
 * an independent verifier (spec 9.1); any group whose order fits uint64 is admitted.
 * CANON_BACKEND_EXPLICIT: the S1 sorted element table, bounded by max_group_order; kept as a
 * test oracle.  Both give identical results for every operation. */
typedef enum canon_backend {
    CANON_BACKEND_CHAIN = 0,
    CANON_BACKEND_EXPLICIT = 1
} canon_backend;

/* Creation-time context options (slice S3; PROVISIONAL until M4).  A context is immutable
 * after creation (spec 17: "immutable contexts/registries/groups can be shared"), so every
 * choice it carries is fixed here. */
typedef struct canon_context_options {
    canon_backend backend; /* group backend for canon_group_create; default CANON_BACKEND_CHAIN */
} canon_context_options;

/* spec 17: as canon_context_create, with options.  `options` may be NULL (every option at its
 * default).  CANON_INVALID_INPUT for NULL `out` or an unknown backend value. */
canon_status canon_context_create_with_options(const canon_capacity *defaults,
                                               const canon_context_options *options,
                                               canon_context **out);

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
 * allowed; zero generators give the trivial group.  CANON_CAPACITY_LIMIT when degree exceeds
 * the context's max_n; CANON_INVALID_INPUT when a generator is not a bijection of
 * {0..degree-1}.  With the context's backend (canon_context_options):
 * CANON_BACKEND_CHAIN (default, S3) builds a verified stabiliser chain (spec 9.1) and returns
 * CANON_CAPACITY_LIMIT only when |G| exceeds 2^64 - 1 (the order is exact in uint64 in this
 * release; slice S4's multi-limb orders lift this), or CANON_INTERNAL_ERROR if the independent
 * verifier rejected the constructed chain; CANON_BACKEND_EXPLICIT (S1) enumerates the group
 * and returns CANON_CAPACITY_LIMIT when the closure would exceed the context's max_group_order
 * (checked before each growth of the element table, never after). */
canon_status canon_group_create(canon_context *ctx, uint32_t degree, const uint32_t *generators,
                                size_t generator_count, canon_group **out);

/* spec 9.2: the exact order |G| in *out.  Capacity: every group handle has an order that fits
 * uint64 (canon_group_create refuses larger groups with CANON_CAPACITY_LIMIT).
 * CANON_INVALID_INPUT for NULL arguments. */
canon_status canon_group_order(const canon_group *group, uint64_t *out);

/* spec 17 input builder (copies data): a subset of atoms of {0..degree-1}; duplicates permitted
 * and deduplicated (spec 4.2: sets deduplicate equal children); degree above the context's
 * max_n gives CANON_CAPACITY_LIMIT (checked first); an atom >= degree gives
 * CANON_INVALID_INPUT.  `atoms` may be NULL when count is 0. */
canon_status canon_object_create_subset(canon_context *ctx, uint32_t degree,
                                        const uint32_t *atoms, size_t count, canon_object **out);

/* spec 4.1 arc record as input (slice S2): source, target, label bytes, positive multiplicity.
 * `label` may be NULL when label_length == 0. */
typedef struct canon_arc {
    uint32_t source, target;
    const uint8_t *label;
    size_t label_length;
    uint64_t multiplicity; /* must be > 0 (spec 4.1) */
} canon_arc;

/* spec 17 builder (copies data), spec 4.1 graph semantics (slice S2): a coloured directed
 * multigraph with `degree` vertices; vertex v has colour colours[v] of colour_lengths[v] bytes
 * (colours and colour_lengths both NULL means every colour is empty, for any degree; a NULL
 * colours[v] with length 0 is the empty colour), and `arc_count` arcs (`arcs` may be NULL when
 * arc_count == 0).  Duplicate (source, target, label) arcs are combined by exact addition;
 * loops are allowed.  Status, in this order: CANON_INVALID_INPUT for NULL ctx/out, exactly one
 * of colours/colour_lengths NULL, or NULL arcs with arc_count > 0;
 * CANON_CAPACITY_LIMIT for degree above the context's max_n; CANON_INVALID_INPUT for a vertex
 * >= degree, a zero multiplicity, or a NULL colour/label pointer with a nonzero length;
 * CANON_CAPACITY_LIMIT for a colour or label longer than 2^32 - 1 bytes (spec 4.1 U32
 * lengths), more than 2^32 - 1 distinct arcs, or a combined multiplicity or total positive
 * multiplicity that does not fit uint64 (the count-bit limit is 64 in this release, detailed
 * plan 2.1; spec 11.1). */
canon_status canon_object_create_graph(canon_context *ctx, uint32_t degree,
                                       const uint8_t *const *colours, const size_t *colour_lengths,
                                       const canon_arc *arcs, size_t arc_count, canon_object **out);

/* spec 4.1 simple-undirected wrapper (slice S2): edges are unordered pairs {a, b}; loops
 * (a == b) and vertices >= degree are CANON_INVALID_INPUT; duplicate edges (in either
 * orientation) are coalesced; each edge becomes two opposite unit arcs with empty labels; all
 * vertex colours are empty.  The core never silently makes a directed graph undirected: this
 * is the only place the conversion happens.  `edges` may be NULL when edge_count == 0.  Degree
 * above the context's max_n is CANON_CAPACITY_LIMIT (checked before the edges). */
canon_status canon_object_create_simple_graph(canon_context *ctx, uint32_t degree,
                                              const uint32_t (*edges)[2], size_t edge_count,
                                              canon_object **out);

/* spec section 17, 4.1: build an object from a CDAG-2 stream (copied by default).
 * STUB until slice S5: returns CANON_UNSUPPORTED_ACTION. */
canon_status canon_object_create(canon_context *ctx, canon_schema schema, canon_action action,
                                 uint32_t degree, const uint8_t *stream, size_t stream_length,
                                 canon_object **out);

/* spec section 17, 3: bind group, object, objective, profile, encoding and order explicitly.
 * `capacity` may be NULL (all context defaults); a zero field selects the context default.
 * The problem retains the group and the object, which may be a subset or a graph (S2).
 * Equivalent to canon_problem_create_with_options (S4) with no target and default options.
 * Validation, in this order (for CANONICAL_IMAGE; see canon_problem_create_with_options for
 * the other objectives):
 * CANON_UNSUPPORTED_ACTION for an unsupported objective, a profile other than P1, an encoding
 * other than CDAG-2 or an order other than CDAG-BYTE-1;
 * CANON_INVALID_INPUT for a degree mismatch between group and object; CANON_CAPACITY_LIMIT
 * when the degree exceeds max_n, the group was built by the explicit backend and its order
 * exceeds max_group_order (S3: the chain backend has no such limit), or the exact
 * canonical stream length exceeds max_output_bytes (spec 11.1).  That length is a function of
 * the input alone: for a subset it is determined by its size; for a graph by n, the colour
 * multiset, the arc count, the label bytes and the multiplicities, none of which the action
 * changes. */
canon_status canon_problem_create(canon_context *ctx, const canon_group *group,
                                  const canon_object *object, canon_objective objective,
                                  canon_profile profile, canon_encoding encoding, canon_order order,
                                  const canon_capacity *capacity, canon_problem **out);

/* Witness choice (spec 3, slice S4; PROVISIONAL until M4).  CANON_WITNESS_ANY: the witness the
 * reference traversal finds first (for CANONICAL_IMAGE the least attaining leaf witness of the
 * unpruned P1 tree, as before).  CANON_WITNESS_DETERMINISTIC: spec 3 "minimise the image array
 * among all solutions sending x to the selected c": for CANONICAL_IMAGE by completing the
 * stabiliser A and taking the least element of A t; for LEX_MIN_IMAGE the least g among all
 * attaining the minimum; for TRANSPORTER_COSET the least element r0 of A g.  Not available for
 * TRANSPORTER_ONE and STABILISER (CANON_UNSUPPORTED_ACTION). */
typedef enum canon_witness_mode {
    CANON_WITNESS_ANY = 0,
    CANON_WITNESS_DETERMINISTIC = 1
} canon_witness_mode;

/* Problem options (slice S4; PROVISIONAL until M4). */
typedef struct canon_problem_options {
    canon_witness_mode witness_mode;
} canon_problem_options;

/* spec 17, 3, 8 (slice S4): as canon_problem_create, plus an optional second object `target`
 * and options (NULL = defaults: CANON_WITNESS_ANY).  canon_problem_create calls this with
 * target and options NULL.  Supported combinations (encoding CDAG-2 throughout):
 *   CANONICAL_IMAGE (0x0001)    profile P1,      order CDAG-BYTE-1;
 *   LEX_MIN_IMAGE (0x0002)      profile NO_TREE, order CDAG-BYTE-1 or SIMPLE-UPPER-1 (the
 *                               latter only for a graph in the spec 4.4 class: empty colours
 *                               and labels, no loops, one unit arc each way per edge);
 *   TRANSPORTER_ONE (0x0003), STABILISER (0x0004), TRANSPORTER_COSET (0x0006)
 *                               profile NO_TREE (spec 4.3: "Profile tag 0x0000 means NO_TREE
 *                               for coset-enumeration objectives"), order CDAG-BYTE-1.
 * The target is required for TRANSPORTER_ONE and TRANSPORTER_COSET and must be NULL
 * otherwise; it must have the object's kind and degree.  The problem retains it.
 * Validation, in this order: CANON_INVALID_INPUT for NULL ctx/group/object/out or an unknown
 * witness mode; CANON_UNSUPPORTED_ACTION for a combination not listed above (including a
 * deterministic witness for TRANSPORTER_ONE or STABILISER); CANON_INVALID_INPUT for a degree
 * mismatch, a missing, superfluous or mismatched target; CANON_UNSUPPORTED_ACTION for
 * SIMPLE-UPPER-1 on an object outside the spec 4.4 class; CANON_CAPACITY_LIMIT as for
 * canon_problem_create, where the output size is the exact stream length for CANONICAL_IMAGE
 * and LEX_MIN_IMAGE and a conservative bound derived from n and |G| for the Group payloads of
 * STABILISER and TRANSPORTER_COSET (spec 11.1: "a conservative input-derived bound"). */
canon_status canon_problem_create_with_options(canon_context *ctx, const canon_group *group,
                                               const canon_object *object,
                                               const canon_object *target,
                                               canon_objective objective, canon_profile profile,
                                               canon_encoding encoding, canon_order order,
                                               const canon_capacity *capacity,
                                               const canon_problem_options *options,
                                               canon_problem **out);

/* spec section 17: a worker-owned mutable workspace (one active owner).  Storage is allocated
 * lazily on the first solve, sized to the problem's degree, and reused across solves. */
canon_status canon_workspace_create(canon_context *ctx, canon_workspace **out);

/* spec section 17, 3, 7, 8: solve one problem; the result owns its immutable evidence and
 * never aliases workspace memory (it retains the problem's group and objects, for
 * canon_result_verify_witness).  Returns the result's status (spec 3.2):
 * CANON_COMPLETE with the flags of the objective and every other flag false:
 *   CANONICAL_IMAGE: witness_valid, image_canonical, encoding_complete;
 *   LEX_MIN_IMAGE: minimum_proved, witness_valid, encoding_complete;
 *   TRANSPORTER_ONE: witness_valid (a hit) or transport_exhausted (proved empty);
 *   STABILISER: subgroup_verified, stabiliser_complete;
 *   TRANSPORTER_COSET: witness_valid, subgroup_verified, stabiliser_complete (nonempty), or
 *   transport_exhausted (proved empty);
 * CANON_CAPACITY_LIMIT when the solve's reference traversals exceed max_search_nodes (spec
 * 11.1: P1 NODE tokens plus coset-enumeration visits), with a result that has no trace, no
 * bytes, no witness and all flags false; CANON_RESOURCE_LIMIT on allocation failure (a result
 * is returned when it could itself be allocated).  CANON_INVALID_INPUT for NULL arguments (no
 * result).  Evidence mode: TRUSTED_ENGINE (spec 3.2), exhaustion asserted by this engine. */
canon_status canon_solve(canon_workspace *workspace, const canon_problem *problem,
                         canon_result **out);

/* ---- Result accessors (spec section 3.2).  A NULL result reads as no data. ---- */

/* spec 3.2: the result's status (CANON_INVALID_INPUT for NULL). */
canon_status canon_result_status(const canon_result *result);
/* spec 3.2: the nine evidence flags (all false for NULL). */
canon_result_flags canon_result_get_flags(const canon_result *result);
/* spec 7.2: the complete P1 trace of the selected leaf; NULL and *length = 0 if not produced. */
const uint8_t *canon_result_trace(const canon_result *result, size_t *length);
/* spec 3: the witness t as an image array of length *degree, only when witness_valid; NULL and
 * *degree = 0 otherwise.  For degree 0 a produced witness is a non-NULL pointer with
 * *degree = 0.  CANONICAL_IMAGE: x^t = the canonical image.  With CANON_WITNESS_ANY this is
 * the least LEAF witness attaining the minimal (trace, bytes) key, as spec 7.4's preamble
 * prescribes ("minimise among the witnesses that actually attain that key"); with
 * CANON_WITNESS_DETERMINISTIC (S4) it is the spec 3 deterministic witness, the least element
 * of Aut_G(x) t, computed from the complete stabiliser.  For the UNPRUNED tree the two
 * coincide (tree equivariance, spec 7.2: the attaining leaves carry exactly Aut_G(x) t); S4's
 * tests check this, and once pruning lands (S7) only the deterministic mode keeps the
 * guarantee.  LEX_MIN_IMAGE: a g in G attaining the minimum (the least such g in
 * deterministic mode, else the first found by the spec 8.1 reference traversal).
 * TRANSPORTER_ONE and TRANSPORTER_COSET: a g with x^g = target (the first hit of the traversal;
 * for the coset in deterministic mode the least element r0 of A g). */
const uint32_t *canon_result_witness(const canon_result *result, uint32_t *degree);
/* spec 4.3: the canonical CDAG-2 bytes, only when encoding_complete and either image_canonical
 * (CANONICAL_IMAGE) or minimum_proved (LEX_MIN_IMAGE: the stream of the minimum image under
 * either order, spec 4.4 "return the selected graph in CDAG-2 plus its order key"); else NULL
 * and *length = 0. */
const uint8_t *canon_result_bytes(const canon_result *result, size_t *length);
/* spec 9.4: the canonical Group(A) bytes (STABILISER) or Group(A) || Perm(r0)
 * (TRANSPORTER_COSET, A g the complete solution set, r0 its least element); NULL and
 * *length = 0 unless subgroup_verified && stabiliser_complete (and, for a coset, witness_valid:
 * an empty coset has no payload; see docs/slices/S4-notes.md). */
const uint8_t *canon_result_group_bytes(const canon_result *result, size_t *length);
/* spec 4.4: the SIMPLE-UPPER-1 key of the minimum when LEX_MIN_IMAGE was solved under that
 * order (and minimum_proved); else NULL and *length = 0. */
const uint8_t *canon_result_order_key(const canon_result *result, size_t *length);
/* spec 8.2: for TRANSPORTER_ONE and TRANSPORTER_COSET, one g with x^g = target (the same array
 * as canon_result_witness), of length *degree; NULL and *degree = 0 when there is none (proved
 * empty: transport_exhausted) or the solve did not complete. */
const uint32_t *canon_result_transporter(const canon_result *result, uint32_t *degree);

/* spec section 17: per-input statuses, input order preserved regardless of scheduling.
 * STUB until slice S8: returns CANON_UNSUPPORTED_ACTION. */
canon_status canon_solve_batch(canon_workspace *workspace, const canon_problem *const *problems,
                               size_t count, canon_result **results, canon_status *statuses);

/* spec section 17 (slice S4): "result_verify_witness checks membership and exact action, not
 * canonicity".  *valid = true iff the witness is a permutation of the domain, lies in G (the
 * group backend's membership test) and sends the problem's object x to c, compared as CDAG-2
 * streams by re-acting: c is the result's bytes (CANONICAL_IMAGE, LEX_MIN_IMAGE) or the target
 * (TRANSPORTER_ONE, TRANSPORTER_COSET).  CANON_COMPLETE when a verdict was reached;
 * CANON_INVALID_INPUT (*valid = false) for NULL arguments or a result without a witness;
 * CANON_RESOURCE_LIMIT / CANON_CAPACITY_LIMIT when scratch cannot be allocated. */
canon_status canon_result_verify_witness(const canon_result *result, bool *valid);

/* spec sections 17, 4.3: stream the canonical bytes to `sink` in one or more ordered chunks,
 * only when canon_result_bytes has them (signed zero `00` arrives with S6).
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
