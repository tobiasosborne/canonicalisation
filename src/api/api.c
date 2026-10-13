/* Slice S1-S6 public entry points (spec sections 3, 3.1, 3.2, 4.1, 4.2, 8, 11.1, 17): context,
 * capacity descriptor and options (S3: the group backend), retain/release handles, groups and
 * their order (S3), subset and graph objects (S2), objects imported from CDAG-2 streams and
 * the stream validator (S5), problems with targets and options (S4), workspaces, solve for the
 * canonical image and (S4) the enumeration objectives, results, result_encode and
 * result_verify_witness (S4), signed groups and the labeling-coset and signed objectives (S6).
 * Entry points of later slices remain in src/api/stubs.c. */
#include <stdlib.h>
#include <string.h>

#include "arena/alloc.h"
#include "arena/mem.h"
#include "arena/mem.h"
#include "arena/checked.h"
#include "arena/refcount.h"
#include "bsgs/chain.h"
#include "bsgs/chain_backend.h"
#include "bsgs/explicit.h"
#include "bsgs/group.h"
#include "canon/canon.h"
#include "object/graph.h"
#include "object/object.h"
#include "encoding/cdag_decode.h"
#include "encoding/group_stream.h"
#include "encoding/simple_upper.h"
#include "object/subset.h"
#include "perm/perm.h"
#include "search/certificate.h"
#include "search/objectives.h"
#include "search/p1_tree.h"

/* Built-in context defaults (docs/slices/S1.md section 3). */
#define DEFAULT_MAX_N 4096u
#define DEFAULT_MAX_GROUP_ORDER ((uint64_t)1 << 16)
#define DEFAULT_MAX_SEARCH_NODES ((uint64_t)1 << 20)
#define DEFAULT_MAX_OUTPUT_BYTES ((uint64_t)1 << 26)
/* docs/slices/S5.md 2 (detailed plan 2.1): limits of a normal form */
#define DEFAULT_MAX_NODES ((uint64_t)1 << 20)
#define DEFAULT_MAX_REFS ((uint64_t)1 << 22)
#define DEFAULT_MAX_LITERAL_BYTES ((uint64_t)1 << 26)
/* docs/slices/S7.md 6.2 (D14), S7b: the default work policy is the strongest sound prune */
#define DEFAULT_WORK_POLICY CANON_WORK_POLICY_PRUNE_2
/* docs/slices/S8.md 1.4: the live-memory budget of one solve (spec 11.1 admission) */
#define DEFAULT_MAX_LIVE_BYTES ((uint64_t)1 << 32)

/* Largest chunk handed to a sink in one call (spec 17: ordered borrowed chunks). */
#define SINK_CHUNK ((size_t)1 << 16)

struct canon_context {
    canon_capacity defaults; /* every field nonzero */
    canon_backend backend;   /* S3: group backend for canon_group_create, fixed at creation */
};

/* An object is a root (src/object/object.h): a top-level subset (S1), a top-level coloured
 * directed multigraph (S2) or (S5) any other EXT-DAG-1 root imported from a stream.  Like
 * canon_group, an object is immutable and shareable, so its reference count is reached through
 * a pointer (src/arena/refcount.h): retain/release work through a const handle without a
 * cast.  `block` is the allocation, used only to free it. */
struct canon_object {
    canon_refcount *refs; /* = &refs_storage */
    void *block;          /* = this handle's allocation */
    canon_refcount refs_storage;
    canon_root root;
};

/* Problems, workspaces and results are retained through non-const handles only, so their
 * counts are plain atomic members. */
struct canon_problem {
    canon_refcount refs;
    const canon_group *group;   /* shared, retained */
    const canon_object *object; /* shared, retained */
    const canon_object *target; /* S4: transporter target, shared, retained; else NULL */
    uint32_t *rho;              /* S6: owned copy of the labeling rho (labeling only); else NULL */
    canon_witness_mode witness_mode; /* S4 */
    bool certificate;           /* S7: emit a certificate with the result */
    uint64_t planned_bytes;     /* S8: the admission plan (spec 11.1), a function of the input */
    canon_objective objective;
    canon_profile profile;
    canon_encoding encoding;
    canon_order order;
    canon_capacity capacity; /* resolved: every field nonzero */
};

struct canon_workspace {
    canon_refcount refs;
    canon_p1_search search; /* spec 17: one active owner; storage reused across solves */
    canon_obj_search obj;   /* S4: enumeration objectives and the deterministic witness */
    canon_cert cert;        /* S7: certificate writer, storage reused across solves */
};

struct canon_result {
    canon_refcount refs;
    canon_status status;
    canon_result_flags flags;
    uint8_t *trace; /* owned copies, never aliasing the workspace */
    size_t trace_len;
    uint8_t *bytes;
    size_t bytes_len;
    uint32_t *witness; /* non-NULL iff a witness was produced (one spare entry for n = 0) */
    uint32_t degree;
    uint8_t *group_bytes; /* S4: Group(A) or Group(A) || Perm(r0); NULL if none */
    size_t group_len;
    uint8_t *key; /* S4: SIMPLE-UPPER-1 key of the minimum; NULL if none */
    size_t key_len;
    uint32_t *labeling; /* S6: lambda = rho t (one spare entry for n = 0); NULL if none */
    uint32_t *rho;      /* S6: the problem's rho, for canon_result_verify_witness; else NULL */
    int sign;           /* S6: +1/-1 nonzero, 0 certified zero (SIGNED_CANONICAL_IMAGE) */
    uint8_t *certificate; /* S7: the CERT-0 stream (owned copy); NULL if none */
    size_t certificate_len;
    canon_metrics metrics; /* S8: the solve's counters (spec 19) */
    canon_objective objective;
    /* S4, for canon_result_verify_witness: the problem's immutable inputs, retained (owned
     * references, not borrowed pointers); NULL in a status-only result. */
    const canon_group *group;
    const canon_object *object;
    const canon_object *target;
};

/* ---- capacity ---- */

/* spec 11.1: a zero field selects the fallback value. */
static canon_capacity resolve_capacity(const canon_capacity *given, const canon_capacity *fallback)
{
    canon_capacity c = *fallback;
    if (given != NULL) {
        if (given->max_n != 0) {
            c.max_n = given->max_n;
        }
        if (given->max_group_order != 0) {
            c.max_group_order = given->max_group_order;
        }
        if (given->max_search_nodes != 0) {
            c.max_search_nodes = given->max_search_nodes;
        }
        if (given->max_output_bytes != 0) {
            c.max_output_bytes = given->max_output_bytes;
        }
        if (given->max_nodes != 0) {
            c.max_nodes = given->max_nodes;
        }
        if (given->max_refs != 0) {
            c.max_refs = given->max_refs;
        }
        if (given->max_literal_bytes != 0) {
            c.max_literal_bytes = given->max_literal_bytes;
        }
        if (given->work_policy != 0) {
            c.work_policy = given->work_policy; /* S7, spec 11.1 */
        }
        if (given->max_live_bytes != 0) {
            c.max_live_bytes = given->max_live_bytes; /* S8, spec 11.1 */
        }
    }
    return c;
}

/* ---- context ---- */

canon_status canon_context_create(const canon_capacity *defaults, canon_context **out)
{
    return canon_context_create_with_options(defaults, NULL, out);
}

canon_status canon_context_create_with_options(const canon_capacity *defaults,
                                               const canon_context_options *options,
                                               canon_context **out)
{
    if (out == NULL) {
        return CANON_INVALID_INPUT;
    }
    *out = NULL;
    /* S3 brief 2.5: the chain is the default backend */
    const canon_backend backend = options != NULL ? options->backend : CANON_BACKEND_CHAIN;
    if (backend != CANON_BACKEND_CHAIN && backend != CANON_BACKEND_EXPLICIT) {
        return CANON_INVALID_INPUT;
    }
    const canon_capacity builtin = {DEFAULT_MAX_N,
                                    DEFAULT_MAX_GROUP_ORDER,
                                    DEFAULT_MAX_SEARCH_NODES,
                                    DEFAULT_MAX_OUTPUT_BYTES,
                                    DEFAULT_MAX_NODES,
                                    DEFAULT_MAX_REFS,
                                    DEFAULT_MAX_LITERAL_BYTES,
                                    DEFAULT_WORK_POLICY,
                                    DEFAULT_MAX_LIVE_BYTES};
    canon_context *ctx = canon_mem_malloc(sizeof *ctx);
    if (ctx == NULL) {
        return CANON_RESOURCE_LIMIT;
    }
    ctx->defaults = resolve_capacity(defaults, &builtin);
    ctx->backend = backend; /* fixed for the context's lifetime (spec 17: immutable) */
    *out = ctx;
    return CANON_COMPLETE;
}

void canon_context_release(canon_context *ctx)
{
    canon_mem_free(ctx);
}

/* ---- retain/release (spec 17).  Group retain/release live in src/bsgs/group.c. ---- */

/* Add/drop a reference through a const object handle (spec 17: shared immutable inputs). */
static void object_share(const canon_object *object)
{
    if (object != NULL) {
        canon_ref_retain(object->refs);
    }
}

static void object_unshare(const canon_object *object)
{
    if (object != NULL && canon_ref_release(object->refs)) {
        canon_object *owned = object->block; /* the allocation, now unreferenced */
        canon_root_free(&owned->root);
        canon_mem_free(owned);
    }
}

void canon_object_retain(canon_object *object)
{
    object_share(object);
}

void canon_object_release(canon_object *object)
{
    object_unshare(object);
}

void canon_problem_retain(canon_problem *problem)
{
    if (problem != NULL) {
        canon_ref_retain(&problem->refs);
    }
}

void canon_problem_release(canon_problem *problem)
{
    if (problem != NULL && canon_ref_release(&problem->refs)) {
        canon_group_unshare(problem->group);
        object_unshare(problem->object);
        object_unshare(problem->target);
        canon_mem_free(problem->rho);
        canon_mem_free(problem);
    }
}

void canon_workspace_retain(canon_workspace *workspace)
{
    if (workspace != NULL) {
        canon_ref_retain(&workspace->refs);
    }
}

void canon_workspace_release(canon_workspace *workspace)
{
    if (workspace != NULL && canon_ref_release(&workspace->refs)) {
        canon_p1_search_free(&workspace->search);
        canon_obj_search_free(&workspace->obj);
        canon_cert_free(&workspace->cert);
        canon_mem_free(workspace);
    }
}

void canon_result_retain(canon_result *result)
{
    if (result != NULL) {
        canon_ref_retain(&result->refs);
    }
}

void canon_result_release(canon_result *result)
{
    if (result != NULL && canon_ref_release(&result->refs)) {
        canon_mem_free(result->trace);
        canon_mem_free(result->bytes);
        canon_mem_free(result->witness);
        canon_mem_free(result->group_bytes);
        canon_mem_free(result->key);
        canon_mem_free(result->labeling);
        canon_mem_free(result->rho);
        canon_mem_free(result->certificate);
        canon_group_unshare(result->group);
        object_unshare(result->object);
        object_unshare(result->target);
        canon_mem_free(result);
    }
}

/* ---- builders ---- */

canon_status canon_group_create(canon_context *ctx, uint32_t degree, const uint32_t *generators,
                                size_t generator_count, canon_group **out)
{
    if (out == NULL) {
        return CANON_INVALID_INPUT;
    }
    *out = NULL;
    if (ctx == NULL || (generator_count > 0 && degree > 0 && generators == NULL)) {
        return CANON_INVALID_INPUT;
    }
    if (degree > ctx->defaults.max_n) {
        return CANON_CAPACITY_LIMIT; /* spec 11.1: degree limit of the descriptor */
    }
    /* spec 17: input builders copy data; each backend keeps only its own representation. */
    if (ctx->backend == CANON_BACKEND_EXPLICIT) {
        return canon_group_explicit_create(degree, generators, generator_count,
                                           ctx->defaults.max_group_order, out);
    }
    /* S3 brief 2.5: the chain is limited only by its uint64 order, not by max_group_order. */
    return canon_group_chain_create(degree, generators, generator_count, out);
}

canon_status canon_group_create_signed(canon_context *ctx, uint32_t degree,
                                       const uint32_t *generators, size_t generator_count,
                                       const int8_t *signs, canon_group **out)
{
    if (out == NULL) {
        return CANON_INVALID_INPUT;
    }
    *out = NULL;
    if (ctx == NULL ||
        (generator_count > 0 && ((degree > 0 && generators == NULL) || signs == NULL))) {
        return CANON_INVALID_INPUT;
    }
    if (degree > ctx->defaults.max_n) {
        return CANON_CAPACITY_LIMIT; /* spec 11.1: degree limit of the descriptor */
    }
    if (degree > UINT32_MAX - 2u) {
        /* spec 11.1: "The initial lifted-character validator additionally requires
         * n+2 <= 2^32-1, checked before constructing its two sign points" */
        return CANON_CAPACITY_LIMIT;
    }
    for (size_t i = 0; i < generator_count; ++i) {
        if (signs[i] != 1 && signs[i] != -1) {
            return CANON_INVALID_INPUT; /* spec 8.4: chi : G -> {+-1} */
        }
    }
    /* spec 8.4: the backend validates the signs by the lifted group and keeps it */
    if (ctx->backend == CANON_BACKEND_EXPLICIT) {
        return canon_group_explicit_create_signed(degree, generators, generator_count, signs,
                                                  ctx->defaults.max_group_order, out);
    }
    return canon_group_chain_create_signed(degree, generators, generator_count, signs, out);
}

canon_status canon_group_character(const canon_group *group, const uint32_t *g, int *sign_out)
{
    if (sign_out != NULL) {
        *sign_out = 0;
    }
    if (group == NULL || g == NULL || sign_out == NULL) {
        return CANON_INVALID_INPUT;
    }
    if (group->signs == NULL) {
        return CANON_UNSUPPORTED_ACTION; /* spec 8.4: an unsigned group has no character */
    }
    /* a member of G is a bijection of the domain; validate before any group operation */
    bool bijective = false;
    const canon_status st = canon_perm_check(g, group->degree, &bijective);
    if (st != CANON_COMPLETE || !bijective) {
        return st != CANON_COMPLETE ? st : CANON_INVALID_INPUT;
    }
    return group->ops->character(group, g, NULL, sign_out); /* INVALID_INPUT if g is not in G */
}

canon_status canon_group_order(const canon_group *group, uint64_t *out)
{
    if (group == NULL || out == NULL) {
        return CANON_INVALID_INPUT;
    }
    *out = group->ops->order(group); /* spec 9.2: exact; fits uint64 for every handle */
    return CANON_COMPLETE;
}

/* Allocate an object handle of the given kind with one reference; the root's storage is
 * zero (valid to free). */
static canon_object *new_object(canon_root_kind kind, uint32_t degree)
{
    canon_object *obj = canon_mem_calloc(1, sizeof *obj);
    if (obj != NULL) {
        obj->block = obj;
        obj->refs = &obj->refs_storage;
        canon_ref_init(obj->refs);
        obj->root.kind = kind;
        obj->root.n = degree;
    }
    return obj;
}

/* Finish a builder: on success hand out the object, otherwise free it. */
static canon_status finish_object(canon_object *obj, canon_status st, canon_object **out)
{
    if (st != CANON_COMPLETE) {
        canon_root_free(&obj->root);
        canon_mem_free(obj);
        return st;
    }
    *out = obj;
    return CANON_COMPLETE;
}

canon_status canon_object_create_subset(canon_context *ctx, uint32_t degree,
                                        const uint32_t *atoms, size_t count, canon_object **out)
{
    if (out == NULL) {
        return CANON_INVALID_INPUT;
    }
    *out = NULL;
    if (ctx == NULL || (count > 0 && atoms == NULL)) {
        return CANON_INVALID_INPUT;
    }
    /* spec 11.1: the admitted degree is checked first, as in canon_group_create, then the
     * data (spec 4.1: out-of-domain atom IDs are invalid, in canon_subset_init). */
    if (degree > ctx->defaults.max_n) {
        return CANON_CAPACITY_LIMIT;
    }
    canon_object *obj = new_object(CANON_ROOT_SUBSET, degree);
    if (obj == NULL) {
        return CANON_RESOURCE_LIMIT;
    }
    /* spec 17: copies data; spec 4.2: duplicates deduplicated. */
    return finish_object(obj, canon_subset_init(&obj->root.u.subset, degree, atoms, count), out);
}

canon_status canon_object_create_graph(canon_context *ctx, uint32_t degree,
                                       const uint8_t *const *colours, const size_t *colour_lengths,
                                       const canon_arc *arcs, size_t arc_count, canon_object **out)
{
    if (out == NULL) {
        return CANON_INVALID_INPUT;
    }
    *out = NULL;
    /* Required arrays: the arcs for arc_count > 0.  The colour arrays are both given or both
     * NULL (every colour empty); one without the other is invalid. */
    if (ctx == NULL || (colours == NULL) != (colour_lengths == NULL) ||
        (arc_count > 0 && arcs == NULL)) {
        return CANON_INVALID_INPUT;
    }
    if (degree > ctx->defaults.max_n) {
        return CANON_CAPACITY_LIMIT; /* spec 11.1: admitted degree first, then the data */
    }
    canon_object *obj = new_object(CANON_ROOT_GRAPH, degree);
    if (obj == NULL) {
        return CANON_RESOURCE_LIMIT;
    }
    /* spec 17: copies data; spec 4.1: duplicate arcs combined, zero multiplicities invalid. */
    canon_status st = canon_graph_init(&obj->root.u.graph, degree, colours, colour_lengths,
                                       arcs, arc_count);
    return finish_object(obj, st, out);
}

canon_status canon_object_create_simple_graph(canon_context *ctx, uint32_t degree,
                                              const uint32_t (*edges)[2], size_t edge_count,
                                              canon_object **out)
{
    if (out == NULL) {
        return CANON_INVALID_INPUT;
    }
    *out = NULL;
    if (ctx == NULL || (edge_count > 0 && edges == NULL)) {
        return CANON_INVALID_INPUT;
    }
    if (degree > ctx->defaults.max_n) {
        return CANON_CAPACITY_LIMIT;
    }
    canon_object *obj = new_object(CANON_ROOT_GRAPH, degree);
    if (obj == NULL) {
        return CANON_RESOURCE_LIMIT;
    }
    /* spec 4.1: the schema-specific wrapper rejects loops, coalesces duplicate edges and emits
     * two opposite unit arcs per edge; it is the only undirected-to-directed conversion. */
    return finish_object(obj, canon_graph_init_simple(&obj->root.u.graph, degree, edges,
                                                      edge_count),
                         out);
}

/* The limits of a normal form under a resolved capacity descriptor (spec 11.1). */
static canon_dag_limits dag_limits(const canon_capacity *cap)
{
    canon_dag_limits lim = {cap->max_n, cap->max_nodes, cap->max_refs, cap->max_literal_bytes};
    return lim;
}

canon_status canon_object_create(canon_context *ctx, canon_schema schema, canon_action action,
                                 uint32_t degree, const uint8_t *stream, size_t stream_length,
                                 canon_object **out)
{
    if (out == NULL) {
        return CANON_INVALID_INPUT;
    }
    *out = NULL;
    if (ctx == NULL || (stream == NULL && stream_length > 0)) {
        return CANON_INVALID_INPUT;
    }
    /* spec 4.1: "unknown schema/action/profile/encoding versions are unsupported, never
     * reinterpreted"; spec 2.1: EXT-DAG-1 under ATOM-TRANSPORT-1 is the built-in pair */
    if (schema != CANON_SCHEMA_EXT_DAG_1 || action != CANON_ACTION_ATOM_TRANSPORT_1) {
        return CANON_UNSUPPORTED_ACTION;
    }
    if (degree > ctx->defaults.max_n) {
        return CANON_CAPACITY_LIMIT; /* spec 11.1: admitted degree first, then the data */
    }
    canon_object *obj = new_object(CANON_ROOT_SUBSET, degree);
    if (obj == NULL) {
        return CANON_RESOURCE_LIMIT;
    }
    /* spec 17: copies data; spec 4.1 strict decode, spec 4.2 normalisation, spec 11.1 limits
     * of the normal form against the context defaults, spec 7.1 root kind */
    const canon_dag_limits lim = dag_limits(&ctx->defaults);
    canon_status st =
        canon_root_import_stream(&obj->root, degree, stream, stream_length, &lim, NULL);
    return finish_object(obj, st, out);
}

canon_status canon_stream_validate(canon_context *ctx, const uint8_t *stream, size_t length)
{
    if (ctx == NULL || (stream == NULL && length > 0)) {
        return CANON_INVALID_INPUT;
    }
    /* spec 4.2: reconstruct the normal form and require byte identity */
    const canon_dag_limits lim = dag_limits(&ctx->defaults);
    canon_cdag_reason reason = CANON_CDAG_OK;
    return canon_cdag_validate(stream, length, &lim, &reason);
}

/* spec 3, 4.3, 4.4: the objective/profile/encoding/order/witness combinations implemented
 * (slices S1, S4).  "unknown schema/action/profile/encoding versions are unsupported, never
 * reinterpreted" (spec 4.1). */
static bool combination_supported(canon_objective objective, canon_profile profile,
                                  canon_encoding encoding, canon_order order,
                                  canon_witness_mode mode)
{
    if (encoding != CANON_ENCODING_CDAG_2) {
        return false;
    }
    switch (objective) {
    case CANON_OBJECTIVE_CANONICAL_IMAGE:
        /* spec 4.3: "canonical and signed image objectives use P1" */
        return profile == CANON_PROFILE_P1 && order == CANON_ORDER_CDAG_BYTE_1;
    case CANON_OBJECTIVE_LEX_MIN_IMAGE:
        /* spec 3: "Least image in the named order (§§4.3-4.4)"; spec 4.3: NO_TREE */
        return profile == CANON_PROFILE_NO_TREE &&
               (order == CANON_ORDER_CDAG_BYTE_1 || order == CANON_ORDER_SIMPLE_UPPER_1);
    case CANON_OBJECTIVE_TRANSPORTER_COSET:
        return profile == CANON_PROFILE_NO_TREE && order == CANON_ORDER_CDAG_BYTE_1;
    case CANON_OBJECTIVE_TRANSPORTER_ONE:
    case CANON_OBJECTIVE_STABILISER:
        /* no deterministic witness for these (S4 brief 2; canon.h) */
        return profile == CANON_PROFILE_NO_TREE && order == CANON_ORDER_CDAG_BYTE_1 &&
               mode == CANON_WITNESS_ANY;
    case CANON_OBJECTIVE_CANONICAL_LABELING_COSET:
    case CANON_OBJECTIVE_SIGNED_CANONICAL_IMAGE:
        /* S6: spec 4.3 "canonical and signed image objectives use P1"; spec 8.2 "Canonical
         * labeling coset: Run §7 on target coordinates".  The complete coset A lambda and the
         * nonzero route already carry A; a deterministic witness is not offered (S6 notes). */
        return profile == CANON_PROFILE_P1 && order == CANON_ORDER_CDAG_BYTE_1 &&
               mode == CANON_WITNESS_ANY;
    default:
        return false; /* constraints: later */
    }
}

static bool needs_target(canon_objective objective)
{
    return objective == CANON_OBJECTIVE_TRANSPORTER_ONE ||
           objective == CANON_OBJECTIVE_TRANSPORTER_COSET;
}

/* u64 sum and product with the overflow flag folded into *ok (spec 11.1). */
static uint64_t add_ok(uint64_t a, uint64_t b, bool *ok)
{
    uint64_t r = 0;
    *ok = *ok && canon_u64_add(a, b, &r);
    return r;
}

static uint64_t mul_ok(uint64_t a, uint64_t b, bool *ok)
{
    uint64_t r = 0;
    *ok = *ok && canon_u64_mul(a, b, &r);
    return r;
}

/* The bytes a group's representation holds (spec 11.2 M_root_group): the chain backend's
 * measured chain, the explicit backend's element table (and its lift) by |G| rows. */
static uint64_t group_bytes(const canon_group *g, bool *ok)
{
    const canon_bsgs *chain = canon_group_chain_of(g);
    if (chain != NULL) {
        return canon_bsgs_bytes(chain);
    }
    return mul_ok(mul_ok(g->ops->order(g), g->degree, ok), 2u * sizeof(uint32_t), ok);
}

/* spec 11.1, 11.2 (slice S8, docs/slices/S8.md 1.4, D16): the admission plan, a conservative
 * bound on the bytes the serial solve allocates beyond the handles it is given, as a function
 * of the input (n, the arcs, the stream size, the group's measured representation and order,
 * the objective and the options) only.  Terms (W = 4 bytes per word):
 *   search state     partition (36 n + 8), snapshot stack (n + 1)(2 n + 2) W, decision stack
 *                    (n + 1) 2 n W, per-point blocks 12 n W, refinement scratch 32 n + 64 e;
 *   traces           the trace along a path has at most 2 n sweeps of at most 10 + 8 n bytes
 *                    and n + 1 NODE tokens: 2 (16 n^2 + 25 n + 8) for the current and the best;
 *   images           4 stream sizes (leaf stream, best stream, image storage, output copy)
 *                    plus 24 e + 16 n for an image's arcs and atoms;
 *   chains           4 times the group's measured bytes for A_known, a rebase, a transient
 *                    tuple-minimum chain and a descent chain, plus 64 (log2 |G| + 1) dense
 *                    rows of n words for inserted generators and their inverses (an estimate
 *                    calibrated on the tiers and checked by tests/c/test_metrics.c, not a
 *                    proof: the strong generator count of a subgroup chain has no useful
 *                    closed bound);
 *   enumeration      coset scratch 13 n W per recursion depth, at most n deep: 52 n (n + 1);
 *   payloads         the output bound (twice: the buffer and the result's copy) and, with a
 *                    certificate, the certificate buffer: a node record carries one rebased
 *                    chain per G stage, the node's stream-sized leaf data and dense rows, so
 *                    it is charged as 8 (group bytes + stream size + 64 n W) per sweep, with
 *                    at most n sweeps per node on a depth-n path: 8 n^2 (group bytes + stream
 *                    + 256 n).  The quota, not n, bounds the number of nodes, so a certificate
 *                    larger than this plan is caught by the ledger (RESOURCE_LIMIT), not here.
 * Returns false (CAPACITY_LIMIT) when a term does not fit uint64. */
static bool plan_bytes(const canon_group *group, const canon_object *object,
                       const canon_object *target, canon_objective objective, bool certificate,
                       uint64_t out_bytes, uint64_t *planned)
{
    bool ok = true;
    const uint64_t n = object->root.n, W = sizeof(uint32_t);
    const uint64_t e = object->root.kind == CANON_ROOT_GRAPH ? object->root.u.graph.e : 0u;
    uint64_t stream = 0;
    if (canon_root_stream_size(&object->root, &stream) != CANON_COMPLETE) {
        return false;
    }
    if (target != NULL) {
        uint64_t ts = 0;
        if (canon_root_stream_size(&target->root, &ts) != CANON_COMPLETE) {
            return false;
        }
        stream = add_ok(stream, ts, &ok);
    }
    const uint64_t n1 = add_ok(n, 1u, &ok);
    /* fixed overhead: the result handle and its copies, the buffers' minimum capacities and
     * the allocator's block headers (16 bytes each, at most a few hundred blocks plus a few
     * per point) */
    uint64_t total = add_ok(16384u, mul_ok(256u, n, &ok), &ok);
    total = add_ok(total, add_ok(mul_ok(36u, n, &ok), 8u, &ok), &ok);                /* partition */
    total = add_ok(total, mul_ok(mul_ok(n1, add_ok(mul_ok(2u, n, &ok), 2u, &ok), &ok), W, &ok), &ok);
    total = add_ok(total, mul_ok(mul_ok(n1, mul_ok(2u, n, &ok), &ok), W, &ok), &ok);  /* decisions */
    total = add_ok(total, mul_ok(12u * W, n, &ok), &ok);                           /* per-point */
    total = add_ok(total, add_ok(mul_ok(32u, n, &ok), mul_ok(64u, e, &ok), &ok), &ok); /* scratch */
    const uint64_t n2 = mul_ok(n, n, &ok);
    total = add_ok(total, mul_ok(2u, add_ok(add_ok(mul_ok(16u, n2, &ok), mul_ok(25u, n, &ok), &ok), 8u, &ok), &ok), &ok);
    total = add_ok(total, add_ok(mul_ok(4u, stream, &ok), add_ok(mul_ok(24u, e, &ok), mul_ok(16u, n, &ok), &ok), &ok), &ok);
    const uint64_t gbytes = group_bytes(group, &ok);
    uint64_t order = group->ops->order(group), log2g = 0;
    while (order > 1u) {
        order >>= 1;
        ++log2g;
    }
    total = add_ok(total, mul_ok(4u, gbytes, &ok), &ok);
    total = add_ok(total, mul_ok(mul_ok(64u, add_ok(log2g, 1u, &ok), &ok), mul_ok(n, W, &ok), &ok), &ok);
    if (objective != CANON_OBJECTIVE_CANONICAL_IMAGE || certificate) {
        total = add_ok(total, mul_ok(52u, mul_ok(n, n1, &ok), &ok), &ok); /* enumeration */
    }
    total = add_ok(total, mul_ok(2u, out_bytes, &ok), &ok);
    if (certificate) {
        const uint64_t per_stage = add_ok(add_ok(gbytes, stream, &ok), mul_ok(256u, n, &ok), &ok);
        total = add_ok(total, mul_ok(mul_ok(8u, n2, &ok), per_stage, &ok), &ok);
    }
    *planned = total;
    return ok;
}

canon_status canon_problem_create(canon_context *ctx, const canon_group *group,
                                  const canon_object *object, canon_objective objective,
                                  canon_profile profile, canon_encoding encoding, canon_order order,
                                  const canon_capacity *capacity, canon_problem **out)
{
    return canon_problem_create_with_options(ctx, group, object, NULL, objective, profile,
                                             encoding, order, capacity, NULL, out);
}

canon_status canon_problem_create_with_options(canon_context *ctx, const canon_group *group,
                                               const canon_object *object,
                                               const canon_object *target,
                                               canon_objective objective, canon_profile profile,
                                               canon_encoding encoding, canon_order order,
                                               const canon_capacity *capacity,
                                               const canon_problem_options *options,
                                               canon_problem **out)
{
    if (out == NULL) {
        return CANON_INVALID_INPUT;
    }
    *out = NULL;
    if (ctx == NULL || group == NULL || object == NULL) {
        return CANON_INVALID_INPUT;
    }
    const canon_witness_mode mode = options != NULL ? options->witness_mode : CANON_WITNESS_ANY;
    if (mode != CANON_WITNESS_ANY && mode != CANON_WITNESS_DETERMINISTIC) {
        return CANON_INVALID_INPUT;
    }
    /* spec 3.2 / 4.1: unsupported identifiers are refused, never reinterpreted */
    if (!combination_supported(objective, profile, encoding, order, mode)) {
        return CANON_UNSUPPORTED_ACTION;
    }
    /* spec 8.4: the signed objective needs a character; an unsigned group has none */
    if (objective == CANON_OBJECTIVE_SIGNED_CANONICAL_IMAGE && group->signs == NULL) {
        return CANON_UNSUPPORTED_ACTION;
    }
    canon_capacity cap = resolve_capacity(capacity, &ctx->defaults);
    /* S7, spec 11.1: the work policy names a traversal; an unknown ID is "unsupported, never
     * reinterpreted" (spec 4.1) */
    if (cap.work_policy != CANON_WORK_POLICY_UNPRUNED &&
        cap.work_policy != CANON_WORK_POLICY_PRUNE_1 && cap.work_policy != CANON_WORK_POLICY_PRUNE_2) {
        return CANON_UNSUPPORTED_ACTION;
    }
    /* S7: certificate v0 covers the canonical image of a subset or graph root (S7 brief 1) */
    const bool certificate = options != NULL && options->certificate;
    if (certificate && (objective != CANON_OBJECTIVE_CANONICAL_IMAGE ||
                        (object->root.kind != CANON_ROOT_SUBSET &&
                         object->root.kind != CANON_ROOT_GRAPH))) {
        return CANON_UNSUPPORTED_ACTION;
    }
    if (group->degree != object->root.n) {
        return CANON_INVALID_INPUT; /* group and object must act on the same domain */
    }
    /* spec 3.1: "rho : Omega -> D_n a bijection", required exactly for the labeling coset */
    const uint32_t *rho = options != NULL ? options->rho : NULL;
    if ((objective == CANON_OBJECTIVE_CANONICAL_LABELING_COSET) != (rho != NULL)) {
        return CANON_INVALID_INPUT;
    }
    if (rho != NULL) {
        bool bijective = false;
        const canon_status vs = canon_perm_check(rho, object->root.n, &bijective);
        if (vs != CANON_COMPLETE || !bijective) {
            return vs != CANON_COMPLETE ? vs : CANON_INVALID_INPUT;
        }
    }
    /* spec 3 table: the transporters relate x to a second object y on the same domain */
    if (needs_target(objective) != (target != NULL) ||
        (target != NULL &&
         (target->root.kind != object->root.kind || target->root.n != object->root.n))) {
        return CANON_INVALID_INPUT;
    }
    /* spec 4.4: "available only for uncoloured simple undirected graphs"; the class is
     * invariant under the action, so it is checked once, here */
    if (order == CANON_ORDER_SIMPLE_UPPER_1 &&
        (object->root.kind != CANON_ROOT_GRAPH ||
         !canon_simple_upper_in_class(&object->root.u.graph))) {
        return CANON_UNSUPPORTED_ACTION;
    }
    /* spec 11.1: "Validation is deterministic over the normalised input." */
    if (object->root.n > cap.max_n) {
        return CANON_CAPACITY_LIMIT;
    }
    /* spec 11.1 node/reference/literal limits of the normal form (S5; the target is the
     * object's kind and degree, and is checked the same way) */
    for (int which = 0; which < 2; ++which) {
        const canon_object *o = which == 0 ? object : target;
        uint64_t nodes = 0, refs = 0, literal = 0;
        if (o != NULL) {
            canon_root_counts(&o->root, &nodes, &refs, &literal);
            if (nodes > cap.max_nodes || refs > cap.max_refs || literal > cap.max_literal_bytes) {
                return CANON_CAPACITY_LIMIT;
            }
        }
    }
    /* spec 11.1: each backend states which descriptors admit it (S3 brief 2.5: the explicit
     * table is bounded by max_group_order, the chain by nothing beyond its uint64 order). */
    canon_status admitted = group->ops->admits(group, &cap);
    if (admitted != CANON_COMPLETE) {
        return admitted;
    }
    /* spec 11.1: data-dependent output size uses an exact input-derived bound where one
     * exists, else a conservative one ("An API promising this property may conservatively
     * reject an instance with a smaller actual output"). */
    uint64_t out_bytes = 0;
    canon_status st = CANON_COMPLETE;
    switch (objective) {
    case CANON_OBJECTIVE_CANONICAL_IMAGE:
    case CANON_OBJECTIVE_LEX_MIN_IMAGE:
    case CANON_OBJECTIVE_SIGNED_CANONICAL_IMAGE:
    case CANON_OBJECTIVE_CANONICAL_LABELING_COSET:
        /* The stream length of x^g equals that of x for every g: a subset image has as many
         * members; a graph image has the same colour multiset, arc count, label bytes and
         * multiplicities (hence Nat lengths), since the action only renumbers vertices.  The
         * argument holds for every permutation, not only for elements of G, so it also covers
         * the labeling's c = x^lambda (S6); a nested object's bound depends only on n and the
         * orders of its group leaves, which conjugation preserves.  A signed zero is the
         * single byte 00, shorter than any stream (spec 4.3). */
        st = canon_root_stream_size(&object->root, &out_bytes);
        if (st == CANON_COMPLETE && objective == CANON_OBJECTIVE_CANONICAL_LABELING_COSET) {
            /* spec 9.4: plus Group(A) || Perm(lambda0), A <= G (S6) */
            uint64_t coset = 0;
            if (!canon_group_bytes_bound(object->root.n, group->ops->order(group), true, &coset) ||
                !canon_u64_add(out_bytes, coset, &out_bytes)) {
                st = CANON_CAPACITY_LIMIT;
            }
        }
        /* spec 4.4: "return the selected graph in CDAG-2 plus its order key"; the key is
         * 4 + ceil(n(n-1)/16) bytes exactly (S4 review item 3) */
        if (st == CANON_COMPLETE && order == CANON_ORDER_SIMPLE_UPPER_1 &&
            !canon_u64_add(out_bytes, canon_simple_upper_key_size(object->root.n),
                           &out_bytes)) {
            st = CANON_CAPACITY_LIMIT;
        }
        break;
    case CANON_OBJECTIVE_STABILISER:
    case CANON_OBJECTIVE_TRANSPORTER_COSET:
        /* spec 9.4: Group(A) for A <= G, plus Perm(r0) for a coset */
        if (!canon_group_bytes_bound(object->root.n, group->ops->order(group),
                                     objective == CANON_OBJECTIVE_TRANSPORTER_COSET,
                                     &out_bytes)) {
            st = CANON_CAPACITY_LIMIT;
        }
        break;
    default:
        break; /* TRANSPORTER_ONE returns no canonical bytes */
    }
    if (st != CANON_COMPLETE) {
        return st;
    }
    if (out_bytes > cap.max_output_bytes) {
        return CANON_CAPACITY_LIMIT;
    }
    /* spec 11.1 (S8): "reserve a complete serial reference workspace plus bounded
     * output/verification buffers ... if that one-task requirement exceeds the descriptor,
     * return CAPACITY_LIMIT before search": the plan is a function of the input alone */
    uint64_t planned = 0;
    if (!plan_bytes(group, object, target, objective, certificate, out_bytes, &planned)) {
        return CANON_CAPACITY_LIMIT;
    }
    if (planned > cap.max_live_bytes) {
        return CANON_CAPACITY_LIMIT;
    }
    canon_problem *pr = canon_mem_malloc(sizeof *pr);
    if (pr == NULL) {
        return CANON_RESOURCE_LIMIT;
    }
    pr->rho = NULL;
    if (rho != NULL) {
        /* spec 17: builders copy data */
        pr->rho = canon_alloc_array(object->root.n, sizeof *pr->rho, &st);
        if (pr->rho == NULL) {
            canon_mem_free(pr);
            return st;
        }
        if (object->root.n > 0) {
            memcpy(pr->rho, rho, (size_t)object->root.n * sizeof *pr->rho);
        }
    }
    canon_ref_init(&pr->refs);
    /* spec 17: the problem retains its immutable inputs.  The handles are shared, not mutated:
     * only their reference counts (bookkeeping, src/arena/refcount.h) change. */
    pr->group = group;
    pr->object = object;
    pr->target = target;
    canon_group_share(group);
    object_share(object);
    object_share(target);
    pr->witness_mode = mode;
    pr->certificate = certificate;
    pr->planned_bytes = planned;
    pr->objective = objective;
    pr->profile = profile;
    pr->encoding = encoding;
    pr->order = order;
    pr->capacity = cap;
    *out = pr;
    return CANON_COMPLETE;
}

canon_status canon_workspace_create(canon_context *ctx, canon_workspace **out)
{
    if (out == NULL) {
        return CANON_INVALID_INPUT;
    }
    *out = NULL;
    if (ctx == NULL) {
        return CANON_INVALID_INPUT;
    }
    canon_workspace *ws = canon_mem_malloc(sizeof *ws);
    if (ws == NULL) {
        return CANON_RESOURCE_LIMIT;
    }
    canon_ref_init(&ws->refs);
    canon_p1_search_init(&ws->search); /* lazy: arrays are sized on the first solve */
    canon_obj_search_init(&ws->obj);
    canon_cert_init(&ws->cert);
    *out = ws;
    return CANON_COMPLETE;
}

/* ---- solve ---- */

static canon_result *new_result(canon_status status)
{
    canon_result *r = canon_mem_calloc(1, sizeof *r);
    if (r != NULL) {
        canon_ref_init(&r->refs);
        r->status = status; /* spec 3.2: all flags false = unproved */
    }
    return r;
}

static uint8_t *copy_bytes(const uint8_t *src, size_t len)
{
    uint8_t *dst = canon_mem_malloc(len > 0 ? len : 1);
    if (dst != NULL && len > 0) {
        memcpy(dst, src, len);
    }
    return dst;
}

/* What a completed solve hands to the result: views into the workspace, copied by
 * finish_result (spec 17: the result owns immutable copies and never aliases the workspace). */
typedef struct answer {
    canon_result_flags flags;
    const canon_buf *trace, *bytes, *group, *key; /* NULL: not produced */
    const uint32_t *witness;                      /* NULL: not produced */
    const uint32_t *labeling;                     /* S6: lambda; NULL: not produced */
    int sign;                                     /* S6: signed objective only */
    const uint8_t *certificate;                   /* S7: CERT-0 stream; NULL: none */
    size_t certificate_len;
    canon_metrics metrics;                        /* S8 */
} answer;

static const canon_buf *if_set(bool set, const canon_buf *b)
{
    return set ? b : NULL;
}

/* Build the result of a completed solve (status COMPLETE) or a status-only one. */
static canon_status finish_result(const canon_problem *problem, canon_status st, const answer *a,
                                  canon_result **out)
{
    if (st != CANON_COMPLETE) {
        /* spec 3.2: an incomplete result carries no trace, bytes or witness and all flags
         * false; spec 17: never a fabricated completion flag.  S8: the metrics are kept. */
        *out = new_result(st);
        if (*out != NULL) {
            (*out)->metrics = a->metrics;
            (*out)->objective = problem->objective;
        }
        return st;
    }
    const uint32_t n = problem->group->degree;
    canon_result *r = new_result(CANON_COMPLETE);
    canon_status alloc = CANON_COMPLETE;
    bool ok = r != NULL;
    if (ok && a->trace != NULL) {
        ok = (r->trace = copy_bytes(a->trace->data, a->trace->len)) != NULL;
        r->trace_len = a->trace->len;
    }
    if (ok && a->bytes != NULL) {
        ok = (r->bytes = copy_bytes(a->bytes->data, a->bytes->len)) != NULL;
        r->bytes_len = a->bytes->len;
    }
    if (ok && a->group != NULL) {
        ok = (r->group_bytes = copy_bytes(a->group->data, a->group->len)) != NULL;
        r->group_len = a->group->len;
    }
    if (ok && a->key != NULL) {
        ok = (r->key = copy_bytes(a->key->data, a->key->len)) != NULL;
        r->key_len = a->key->len;
    }
    if (ok && a->witness != NULL) {
        ok = (r->witness = canon_alloc_array(n, sizeof *r->witness, &alloc)) != NULL;
        if (ok && n > 0) {
            memcpy(r->witness, a->witness, (size_t)n * sizeof *r->witness);
        }
    }
    if (ok && a->certificate != NULL) {
        /* S7: the certificate (owned copy, spec 17) */
        ok = (r->certificate = copy_bytes(a->certificate, a->certificate_len)) != NULL;
        r->certificate_len = a->certificate_len;
    }
    if (ok && a->labeling != NULL) {
        /* S6: lambda, and rho for canon_result_verify_witness (owned copies) */
        ok = (r->labeling = canon_alloc_array(n, sizeof *r->labeling, &alloc)) != NULL &&
             (r->rho = canon_alloc_array(n, sizeof *r->rho, &alloc)) != NULL;
        if (ok && n > 0) {
            memcpy(r->labeling, a->labeling, (size_t)n * sizeof *r->labeling);
            memcpy(r->rho, problem->rho, (size_t)n * sizeof *r->rho);
        }
    }
    if (!ok) {
        canon_result_release(r);
        *out = new_result(CANON_RESOURCE_LIMIT);
        return CANON_RESOURCE_LIMIT;
    }
    r->degree = n;
    r->flags = a->flags;
    r->sign = a->sign;
    r->metrics = a->metrics;
    r->metrics.output_bytes = (uint64_t)r->bytes_len + r->group_len + r->key_len;
    r->metrics.certificate_bytes = r->certificate_len;
    r->objective = problem->objective;
    /* spec 17 verify_witness needs G, x and the target after the problem may be released:
     * the result retains them (owned references to immutable handles). */
    r->group = problem->group;
    r->object = problem->object;
    r->target = problem->target;
    canon_group_share(r->group);
    object_share(r->object);
    object_share(r->target);
    *out = r;
    return CANON_COMPLETE;
}

/* spec 7, 3: CANONICAL_IMAGE by the P1 tree under the descriptor's work policy (S7, S7b, spec
 * 11.1: 0x0001 the unpruned reference traversal, 0x0002 the orbit prune of
 * docs/pruning-rules.md by explicit automorphisms, 0x0003 with implicit ones too), with a
 * certificate when requested; with the deterministic witness
 * mode the least element of A t (spec 3), A the complete stabiliser from the spec 8.2
 * consumer. */
/* S8, spec 19: the counters of the workspace's P1 search and of the enumeration state. */
static void metrics_p1(canon_metrics *m, const canon_p1_search *s, const canon_group *g)
{
    m->p1_nodes = s->nodes;
    m->p1_leaves = s->leaves;
    m->p1_images = s->images;
    m->children_explored = s->explored;
    m->children_pruned = s->pruned;
    m->automorphism_candidates = s->sym.stats.candidates;
    m->automorphisms_inserted = s->sym.stats.inserted;
    m->implicit_candidates = s->implicit_candidates;
    m->implicit_inserted = s->implicit_inserted;
    m->stabiliser_rebases = s->sym.stats.rebases;
    m->root_discrete = s->nodes == 1u;   /* spec 7.3: the root was a leaf */
    m->singleton_orbit = s->fixing == g->gens.count; /* spec 7.3: every generator fixes x */
}

static void metrics_enumeration(canon_metrics *m, const canon_obj_stats *st)
{
    m->enumeration_visits = st->nodes;
    m->enumeration_leaves = st->leaves;
    m->stabiliser_hits = st->hits;
    m->stabiliser_rebuilds = st->stab_builds;
    m->coset_descents = st->coset.descents;
    m->coset_rebuilds = st->coset.rebuilds;
    m->character_evaluations = st->characters;
    m->signed_fast_path = st->fast_path != 0;
}

/* S8, spec 11.2: the ledger bracket of a solve (the plan and the live count before, the peak
 * after; exact when one solve runs at a time, decision D17). */
static void metrics_begin(canon_metrics *m, const canon_problem *problem)
{
    memset(m, 0, sizeof *m);
    m->planned_bytes = problem->planned_bytes;
    m->live_bytes_before = canon_mem_peak_reset();
}

static void metrics_end(canon_metrics *m)
{
    canon_memory_stats mem;
    canon_mem_stats(&mem);
    m->peak_live_bytes = mem.peak_bytes;
}

static canon_status solve_canonical(canon_workspace *ws, const canon_problem *problem,
                                    canon_result **out)
{
    canon_p1_search *s = &ws->search;
    const uint64_t quota = problem->capacity.max_search_nodes;
    answer a;
    memset(&a, 0, sizeof a);
    metrics_begin(&a.metrics, problem);
    memset(&ws->obj.stats, 0, sizeof ws->obj.stats);
    const uint16_t policy = problem->capacity.work_policy;
    canon_p1_run_options opt = {policy != CANON_WORK_POLICY_UNPRUNED,
                                policy == CANON_WORK_POLICY_PRUNE_2, NULL};
    canon_status st = CANON_COMPLETE;
    if (problem->certificate) {
        /* spec 20: the producer's certificate names coverage, group verification, the
         * symmetry rules and the final claim (src/search/certificate.h) */
        st = canon_cert_begin(&ws->cert, problem->group, &problem->object->root,
                              problem->capacity.work_policy);
        opt.cert = &ws->cert;
    } else {
        canon_cert_reset(&ws->cert); /* no stale certificate survives in the workspace */
    }
    if (st == CANON_COMPLETE) {
        st = canon_p1_search_run(s, problem->group, &problem->object->root, quota, &opt);
    }
    const uint32_t *witness = s->best_t;
    if (st == CANON_COMPLETE && problem->witness_mode == CANON_WITNESS_DETERMINISTIC) {
        /* spec 11.1: one quota for the solve; the enumeration gets what P1 left */
        st = canon_obj_deterministic_witness(&ws->obj, problem->group, &problem->object->root,
                                             s->best_t, quota - s->nodes);
        witness = ws->obj.best;
    }
    /* spec 3.2: COMPLETE for CANONICAL_IMAGE after the full traversal (TRUSTED_ENGINE
     * evidence, or a certificate for the checker): the witness is an element of G sending x
     * to the returned image, the image is the P1 canonical image, and the encoding is
     * complete. */
    metrics_p1(&a.metrics, s, problem->group);
    metrics_enumeration(&a.metrics, &ws->obj.stats); /* the deterministic witness's run */
    metrics_end(&a.metrics);
    a.flags.witness_valid = true;
    a.flags.image_canonical = true;
    a.flags.encoding_complete = true;
    a.trace = &s->best_trace;
    a.bytes = &s->best_bytes;
    a.witness = witness;
    if (problem->certificate) {
        a.certificate = canon_cert_bytes(&ws->cert, &a.certificate_len);
        if (st == CANON_COMPLETE && a.certificate == NULL) {
            st = CANON_INTERNAL_ERROR; /* a complete run with a certificate always finishes it */
        }
    }
    return finish_result(problem, st, &a, out);
}

/* spec 3.1, 8.2, 8.4 (S6): the labeling coset and the signed canonical image
 * (src/search/objectives.c), both running P1 in the workspace's P1 search. */
static canon_status solve_p1_objective(canon_workspace *ws, const canon_problem *problem,
                                       canon_result **out)
{
    canon_obj_search *s = &ws->obj;
    canon_obj_outcome o;
    const uint64_t quota = problem->capacity.max_search_nodes;
    answer a;
    memset(&a, 0, sizeof a);
    metrics_begin(&a.metrics, problem);
    canon_status st =
        problem->objective == CANON_OBJECTIVE_CANONICAL_LABELING_COSET
            ? canon_obj_labeling(s, &ws->search, problem->group, &problem->object->root,
                                 problem->rho, quota, &o)
            : canon_obj_signed(s, &ws->search, problem->group, &problem->object->root, quota, &o);
    if (o.p1 || st != CANON_COMPLETE) {
        metrics_p1(&a.metrics, &ws->search, problem->group);
    }
    metrics_enumeration(&a.metrics, &s->stats);
    metrics_end(&a.metrics);
    a.flags = o.flags;
    a.trace = if_set(o.p1, &ws->search.best_trace);
    a.bytes = if_set(o.p1, &ws->search.best_bytes);
    a.group = if_set(o.group, &s->group);
    a.witness = o.witness ? s->best : NULL;
    a.labeling = o.labeling ? s->lambda : NULL;
    a.sign = o.sign;
    return finish_result(problem, st, &a, out);
}

/* spec 8: the enumeration objectives (src/search/objectives.c). */
static canon_status solve_enumeration(canon_workspace *ws, const canon_problem *problem,
                                      canon_result **out)
{
    canon_obj_search *s = &ws->obj;
    canon_obj_outcome o;
    const canon_root *y = problem->target != NULL ? &problem->target->root : NULL;
    answer a;
    memset(&a, 0, sizeof a);
    metrics_begin(&a.metrics, problem);
    canon_status st = canon_obj_run(s, problem->group, &problem->object->root, y,
                                    problem->objective, problem->order,
                                    problem->witness_mode == CANON_WITNESS_DETERMINISTIC,
                                    problem->capacity.max_search_nodes, &o);
    metrics_enumeration(&a.metrics, &s->stats);
    metrics_end(&a.metrics);
    a.flags = o.flags;
    a.bytes = if_set(o.bytes, &s->bytes);
    a.group = if_set(o.group, &s->group);
    a.key = if_set(o.key, &s->best_key);
    a.witness = o.witness ? s->best : NULL;
    return finish_result(problem, st, &a, out);
}

canon_status canon_solve(canon_workspace *workspace, const canon_problem *problem,
                         canon_result **out)
{
    if (out == NULL) {
        return CANON_INVALID_INPUT;
    }
    *out = NULL;
    if (workspace == NULL || problem == NULL) {
        return CANON_INVALID_INPUT;
    }
    /* Objective, profile, encoding, order, target and degrees were validated once, in
     * canon_problem_create_with_options; a problem is immutable, so they are not re-checked. */
    if (problem->objective == CANON_OBJECTIVE_CANONICAL_IMAGE) {
        return solve_canonical(workspace, problem, out);
    }
    if (problem->objective == CANON_OBJECTIVE_CANONICAL_LABELING_COSET ||
        problem->objective == CANON_OBJECTIVE_SIGNED_CANONICAL_IMAGE) {
        return solve_p1_objective(workspace, problem, out);
    }
    return solve_enumeration(workspace, problem, out);
}

/* spec 17 v2.1 (slice S8, D12): every input attempted in caller order on the one workspace;
 * statuses[i]/results[i] exactly canon_solve's; call-level INVALID_INPUT only for NULL
 * arrays or workspace or a count whose byte size overflows; COMPLETE iff every input was
 * attempted. */
canon_status canon_solve_batch(canon_workspace *workspace, const canon_problem *const *problems,
                               size_t count, canon_result **results, canon_status *statuses)
{
    if (workspace == NULL) {
        return CANON_INVALID_INPUT;
    }
    if (count == 0) {
        return CANON_COMPLETE; /* a batch of 0 inputs: every (no) input was attempted */
    }
    if (problems == NULL || results == NULL || statuses == NULL) {
        return CANON_INVALID_INPUT;
    }
    size_t bytes = 0;
    if (!canon_size_mul(count, sizeof *results, &bytes) ||
        !canon_size_mul(count, sizeof *statuses, &bytes)) {
        return CANON_INVALID_INPUT; /* "a count whose byte size overflows" */
    }
    for (size_t i = 0; i < count; ++i) {
        /* spec 14.1 CALLER_THREADS: sequential, no hidden threads; spec 17: "a NULL problem
         * entry gives INVALID_INPUT for that entry only"; admission and quota are the
         * problem's own, so each entry is exactly a separate solve */
        results[i] = NULL;
        statuses[i] = canon_solve(workspace, problems[i], &results[i]);
    }
    return CANON_COMPLETE;
}

/* ---- result accessors (spec 3.2) ---- */

canon_status canon_result_status(const canon_result *result)
{
    return result != NULL ? result->status : CANON_INVALID_INPUT;
}

canon_result_flags canon_result_get_flags(const canon_result *result)
{
    canon_result_flags none = {false, false, false, false, false, false, false, false, false};
    return result != NULL ? result->flags : none;
}

const uint8_t *canon_result_trace(const canon_result *result, size_t *length)
{
    if (result == NULL || result->trace == NULL) {
        if (length != NULL) {
            *length = 0;
        }
        return NULL;
    }
    if (length != NULL) {
        *length = result->trace_len;
    }
    return result->trace;
}

const uint32_t *canon_result_witness(const canon_result *result, uint32_t *degree)
{
    if (result == NULL || result->witness == NULL || !result->flags.witness_valid) {
        if (degree != NULL) {
            *degree = 0;
        }
        return NULL;
    }
    if (degree != NULL) {
        *degree = result->degree;
    }
    return result->witness;
}

/* Hand out a result's owned byte array when `ok` and it exists, else NULL with length 0.
 * Callers pass ok = false for a NULL result and only then read nothing from it. */
static const uint8_t *bytes_if(bool ok, const uint8_t *data, size_t len, size_t *length)
{
    ok = ok && data != NULL;
    if (length != NULL) {
        *length = ok ? len : 0;
    }
    return ok ? data : NULL;
}

/* spec 3.2: "Incomplete bytes cannot be used as a canonical database key." */
const uint8_t *canon_result_bytes(const canon_result *r, size_t *length)
{
    return r == NULL ? bytes_if(false, NULL, 0, length)
                     : bytes_if(r->flags.encoding_complete &&
                                    (r->flags.image_canonical || r->flags.minimum_proved),
                                r->bytes, r->bytes_len, length);
}

/* spec 9.4; spec 3.2: a group answer needs the verified, complete subgroup (an empty coset has
 * neither flag and no payload). */
const uint8_t *canon_result_group_bytes(const canon_result *r, size_t *length)
{
    return r == NULL ? bytes_if(false, NULL, 0, length)
                     : bytes_if(r->flags.subgroup_verified && r->flags.stabiliser_complete,
                                r->group_bytes, r->group_len, length);
}

/* spec 4.4: "return the selected graph in CDAG-2 plus its order key" */
const uint8_t *canon_result_order_key(const canon_result *r, size_t *length)
{
    return r == NULL ? bytes_if(false, NULL, 0, length)
                     : bytes_if(r->flags.minimum_proved, r->key, r->key_len, length);
}

/* spec 8.2: "Transporter one: ... stop successfully on one hit." */
const uint32_t *canon_result_transporter(const canon_result *result, uint32_t *degree)
{
    if (result == NULL || (result->objective != CANON_OBJECTIVE_TRANSPORTER_ONE &&
                           result->objective != CANON_OBJECTIVE_TRANSPORTER_COSET)) {
        if (degree != NULL) {
            *degree = 0;
        }
        return NULL;
    }
    return canon_result_witness(result, degree);
}

/* spec 8.4, 4.3 (S6): the sign of a complete signed result. */
canon_status canon_result_sign(const canon_result *result, int *sign_out)
{
    if (sign_out != NULL) {
        *sign_out = 0;
    }
    if (result == NULL || sign_out == NULL ||
        result->objective != CANON_OBJECTIVE_SIGNED_CANONICAL_IMAGE ||
        result->status != CANON_COMPLETE ||
        !(result->flags.zero_certified || result->flags.nonzero_certified)) {
        return CANON_INVALID_INPUT;
    }
    *sign_out = result->flags.zero_certified ? 0 : result->sign;
    return CANON_COMPLETE;
}

/* S7, spec 3.2, 20: the certificate of a complete result. */
const uint8_t *canon_result_certificate(const canon_result *r, size_t *length)
{
    return r == NULL ? bytes_if(false, NULL, 0, length)
                     : bytes_if(r->status == CANON_COMPLETE, r->certificate, r->certificate_len,
                                length);
}

canon_evidence canon_result_evidence(const canon_result *result)
{
    size_t len = 0;
    /* spec 3.2: "A valid witness alone never upgrades one to the other": only an attached
     * certificate selects the checked mode, and only the checker's acceptance makes it so */
    return canon_result_certificate(result, &len) != NULL ? CANON_EVIDENCE_CHECKED_CERTIFICATE
                                                           : CANON_EVIDENCE_TRUSTED_ENGINE;
}

/* S8, spec 19: the counters of a result. */
canon_status canon_result_metrics(const canon_result *result, canon_metrics *out)
{
    if (result == NULL || out == NULL) {
        return CANON_INVALID_INPUT;
    }
    *out = result->metrics;
    return CANON_COMPLETE;
}

canon_status canon_problem_planned_bytes(const canon_problem *problem, uint64_t *out)
{
    if (problem == NULL || out == NULL) {
        return CANON_INVALID_INPUT;
    }
    *out = problem->planned_bytes;
    return CANON_COMPLETE;
}

/* S8, spec 11.2: the process-wide ledger. */
void canon_memory_get(canon_memory_stats *out)
{
    if (out != NULL) {
        canon_mem_stats(out);
    }
}

/* spec 3.1 (S6): lambda = rho t of a complete labeling-coset result. */
const uint32_t *canon_result_labeling(const canon_result *result, uint32_t *degree)
{
    const bool ok = result != NULL && result->labeling != NULL && result->flags.witness_valid;
    if (degree != NULL) {
        *degree = ok ? result->degree : 0;
    }
    return ok ? result->labeling : NULL;
}

/* spec 17: "result_verify_witness checks membership and exact action, not canonicity". */
canon_status canon_result_verify_witness(const canon_result *result, bool *valid)
{
    if (valid == NULL) {
        return CANON_INVALID_INPUT;
    }
    *valid = false;
    uint32_t degree = 0;
    const uint32_t *w = canon_result_witness(result, &degree);
    if (w == NULL || result->group == NULL || result->object == NULL) {
        return CANON_INVALID_INPUT; /* no witness to verify */
    }
    if (result->objective == CANON_OBJECTIVE_SIGNED_CANONICAL_IMAGE) {
        /* S6, spec 8.4: a zero certificate is "one checked membership/action/sign witness":
         * a in G, x^a = x, chi(a) = -1; a nonzero result also checks chi(t) = s */
        int sign = 0;
        canon_status st = canon_result_sign(result, &sign);
        if (st != CANON_COMPLETE) {
            return st;
        }
        size_t len = 0;
        const uint8_t *c = canon_result_bytes(result, &len); /* NULL for a zero: x itself */
        return canon_obj_check_signed(result->group, &result->object->root, w, sign, c, len, valid);
    }
    if (result->objective == CANON_OBJECTIVE_CANONICAL_LABELING_COSET) {
        /* S6, spec 3.1: t in G', lambda = rho t, x^lambda = c */
        size_t len = 0;
        const uint8_t *c = canon_result_bytes(result, &len);
        if (c == NULL || result->labeling == NULL || result->rho == NULL) {
            return CANON_INVALID_INPUT;
        }
        return canon_obj_check_labeling(result->group, &result->object->root, result->rho, w,
                                        result->labeling, c, len, valid);
    }
    if (result->target == NULL) {
        /* the image the witness is claimed to produce: the result's own bytes */
        size_t len = 0;
        const uint8_t *c = canon_result_bytes(result, &len);
        if (c == NULL) {
            return CANON_INVALID_INPUT;
        }
        return canon_obj_check_witness(result->group, &result->object->root, w, c, len, valid);
    }
    /* transporters: x^w must equal the target y, compared as CDAG-2 streams */
    canon_buf y;
    canon_buf_init(&y);
    canon_status st = canon_root_stream_write(&result->target->root, &y);
    if (st == CANON_COMPLETE) {
        st = canon_obj_check_witness(result->group, &result->object->root, w, y.data, y.len,
                                     valid);
    }
    canon_buf_free(&y);
    return st;
}

/* The bytes canon_result_encode streams: the canonical stream, or the 00 payload of a
 * certified signed zero (spec 17, 4.3). */
static const uint8_t *encode_bytes(const canon_result *result, size_t *total)
{
    static const uint8_t zero_payload[1] = {0x00};
    const uint8_t *bytes = canon_result_bytes(result, total);
    /* spec 17: "result_encode returns canonical bytes only when the corresponding image is
     * complete, or the signed 00 payload when zero is certified"; spec 4.3: "A certified zero
     * has distinguished payload byte 00 under the signed objective and no monomial stream;
     * ordinary CDAG streams begin 43" */
    if (result->objective == CANON_OBJECTIVE_SIGNED_CANONICAL_IMAGE &&
        result->status == CANON_COMPLETE && result->flags.zero_certified) {
        bytes = zero_payload;
        *total = sizeof zero_payload;
    }
    return bytes;
}

/* spec 17 (S8): "The sink receives ordered borrowed chunks valid only during its callback, with
 * accepted byte count; PAUSE retains the offset for resume, FAIL returns OUTPUT_ERROR.  No
 * bytes are skipped or duplicated on resume." */
canon_status canon_result_encode_resume(const canon_result *result, size_t *offset,
                                        canon_sink_fn sink, void *user)
{
    if (result == NULL || sink == NULL || offset == NULL) {
        return CANON_INVALID_INPUT;
    }
    size_t total = 0;
    const uint8_t *bytes = encode_bytes(result, &total);
    if (bytes == NULL || *offset > total) {
        return CANON_INVALID_INPUT;
    }
    while (*offset < total) {
        const size_t chunk = total - *offset < SINK_CHUNK ? total - *offset : SINK_CHUNK;
        size_t accepted = 0;
        const int rc = sink(user, bytes + *offset, chunk, &accepted);
        if (accepted > chunk) {
            return CANON_OUTPUT_ERROR; /* more than offered: the sink's contract is broken */
        }
        if (rc == 1) {
            *offset += accepted; /* PAUSE: whatever was taken is kept, the rest resumes here */
            return CANON_CANCELLED;
        }
        /* FAIL (any other nonzero return), or no progress on CONTINUE, which would loop: no
         * byte is skipped or duplicated either way */
        if (rc != 0 || accepted == 0) {
            return CANON_OUTPUT_ERROR;
        }
        *offset += accepted;
    }
    return CANON_COMPLETE;
}

canon_status canon_result_encode(const canon_result *result, canon_sink_fn sink, void *user)
{
    size_t offset = 0;
    return canon_result_encode_resume(result, &offset, sink, user);
}
