/* Slice S1/S2/S3 public entry points (spec sections 3, 3.2, 11.1, 17): context, capacity
 * descriptor, group backend selection (S3), retain/release handles, groups and their order
 * (S3), subset and graph objects (S2), problems, workspaces, solve, results and
 * result_encode.  Entry points of later slices remain in
 * src/api/stubs.c. */
#include <stdlib.h>
#include <string.h>

#include "arena/checked.h"
#include "arena/refcount.h"
#include "bsgs/chain_backend.h"
#include "bsgs/explicit.h"
#include "bsgs/group.h"
#include "canon/canon.h"
#include "object/graph.h"
#include "object/object.h"
#include "object/subset.h"
#include "search/p1_tree.h"

/* Built-in context defaults (docs/slices/S1.md section 3). */
#define DEFAULT_MAX_N 4096u
#define DEFAULT_MAX_GROUP_ORDER ((uint64_t)1 << 16)
#define DEFAULT_MAX_SEARCH_NODES ((uint64_t)1 << 20)
#define DEFAULT_MAX_OUTPUT_BYTES ((uint64_t)1 << 26)

/* Largest chunk handed to a sink in one call (spec 17: ordered borrowed chunks). */
#define SINK_CHUNK ((size_t)1 << 16)

struct canon_context {
    canon_capacity defaults; /* every field nonzero */
    canon_backend backend;   /* S3: group backend for canon_group_create */
};

/* An object is a root (src/object/object.h): a top-level subset (S1) or a top-level coloured
 * directed multigraph (S2); nested DAGs arrive in S5.  Like
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
    canon_objective objective;
    canon_profile profile;
    canon_encoding encoding;
    canon_order order;
    canon_capacity capacity; /* resolved: every field nonzero */
};

struct canon_workspace {
    canon_refcount refs;
    canon_p1_search search; /* spec 17: one active owner; storage reused across solves */
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
    }
    return c;
}

/* ---- context ---- */

canon_status canon_context_create(const canon_capacity *defaults, canon_context **out)
{
    if (out == NULL) {
        return CANON_INVALID_INPUT;
    }
    *out = NULL;
    const canon_capacity builtin = {DEFAULT_MAX_N, DEFAULT_MAX_GROUP_ORDER,
                                    DEFAULT_MAX_SEARCH_NODES, DEFAULT_MAX_OUTPUT_BYTES};
    canon_context *ctx = malloc(sizeof *ctx);
    if (ctx == NULL) {
        return CANON_RESOURCE_LIMIT;
    }
    ctx->defaults = resolve_capacity(defaults, &builtin);
    ctx->backend = CANON_BACKEND_CHAIN; /* S3 brief 2.5: the chain is the default */
    *out = ctx;
    return CANON_COMPLETE;
}

void canon_context_release(canon_context *ctx)
{
    free(ctx);
}

canon_status canon_context_set_group_backend(canon_context *ctx, canon_backend backend)
{
    if (ctx == NULL || (backend != CANON_BACKEND_CHAIN && backend != CANON_BACKEND_EXPLICIT)) {
        return CANON_INVALID_INPUT;
    }
    ctx->backend = backend;
    return CANON_COMPLETE;
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
        free(owned);
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
        free(problem);
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
        free(workspace);
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
        free(result->trace);
        free(result->bytes);
        free(result->witness);
        free(result);
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
    canon_object *obj = calloc(1, sizeof *obj);
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
        free(obj);
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

canon_status canon_problem_create(canon_context *ctx, const canon_group *group,
                                  const canon_object *object, canon_objective objective,
                                  canon_profile profile, canon_encoding encoding, canon_order order,
                                  const canon_capacity *capacity, canon_problem **out)
{
    if (out == NULL) {
        return CANON_INVALID_INPUT;
    }
    *out = NULL;
    if (ctx == NULL || group == NULL || object == NULL) {
        return CANON_INVALID_INPUT;
    }
    /* spec 3.2 / 4.1: "unknown schema/action/profile/encoding versions are unsupported, never
     * reinterpreted"; S1 implements only CANONICAL_IMAGE under P1, CDAG-2, CDAG-BYTE-1. */
    if (objective != CANON_OBJECTIVE_CANONICAL_IMAGE || profile != CANON_PROFILE_P1 ||
        encoding != CANON_ENCODING_CDAG_2 || order != CANON_ORDER_CDAG_BYTE_1) {
        return CANON_UNSUPPORTED_ACTION;
    }
    if (group->degree != object->root.n) {
        return CANON_INVALID_INPUT; /* group and object must act on the same domain */
    }
    canon_capacity cap = resolve_capacity(capacity, &ctx->defaults);
    /* spec 11.1: "Validation is deterministic over the normalised input." */
    if (object->root.n > cap.max_n) {
        return CANON_CAPACITY_LIMIT;
    }
    /* S3 brief 2.5: max_group_order bounds the explicit backend's element table only. */
    if (canon_group_is_explicit(group) && group->ops->order(group) > cap.max_group_order) {
        return CANON_CAPACITY_LIMIT;
    }
    /* spec 11.1: data-dependent output size uses an exact input-derived bound.  The stream
     * length of x^g equals that of x for every g: a subset image has as many members; a graph
     * image has the same colour multiset, arc count, label bytes and multiplicities (hence Nat
     * lengths), since the action only renumbers vertices.  So the length is exact here. */
    uint64_t out_bytes = 0;
    canon_status st = canon_root_stream_size(&object->root, &out_bytes);
    if (st != CANON_COMPLETE) {
        return st;
    }
    if (out_bytes > cap.max_output_bytes) {
        return CANON_CAPACITY_LIMIT;
    }
    canon_problem *pr = malloc(sizeof *pr);
    if (pr == NULL) {
        return CANON_RESOURCE_LIMIT;
    }
    canon_ref_init(&pr->refs);
    /* spec 17: the problem retains its immutable inputs.  The handles are shared, not mutated:
     * only their reference counts (bookkeeping, src/arena/refcount.h) change. */
    pr->group = group;
    pr->object = object;
    canon_group_share(group);
    object_share(object);
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
    canon_workspace *ws = malloc(sizeof *ws);
    if (ws == NULL) {
        return CANON_RESOURCE_LIMIT;
    }
    canon_ref_init(&ws->refs);
    canon_p1_search_init(&ws->search); /* lazy: arrays are sized on the first solve */
    *out = ws;
    return CANON_COMPLETE;
}

/* ---- solve ---- */

static canon_result *new_result(canon_status status)
{
    canon_result *r = calloc(1, sizeof *r);
    if (r != NULL) {
        canon_ref_init(&r->refs);
        r->status = status; /* spec 3.2: all flags false = unproved */
    }
    return r;
}

static uint8_t *copy_bytes(const uint8_t *src, size_t len)
{
    uint8_t *dst = malloc(len > 0 ? len : 1);
    if (dst != NULL && len > 0) {
        memcpy(dst, src, len);
    }
    return dst;
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
    /* Objective, profile, encoding, order and degrees were validated once, in
     * canon_problem_create; a problem is immutable, so they are not re-checked here. */
    canon_p1_search *s = &workspace->search;
    canon_status st = canon_p1_search_run(s, problem->group, &problem->object->root,
                                          problem->capacity.max_search_nodes);
    if (st != CANON_COMPLETE) {
        /* spec 3.2: an incomplete result carries no trace, bytes or witness and all flags
         * false; spec 17: never a fabricated completion flag. */
        *out = new_result(st);
        return st;
    }
    const uint32_t n = problem->group->degree;
    canon_result *r = new_result(CANON_COMPLETE);
    size_t witness_bytes = 0;
    if (r == NULL || !canon_size_mul(n > 0 ? (size_t)n : 1u, sizeof(uint32_t), &witness_bytes)) {
        canon_result_release(r);
        *out = new_result(CANON_RESOURCE_LIMIT);
        return CANON_RESOURCE_LIMIT;
    }
    /* spec 17: the result owns immutable copies; it never aliases the workspace. */
    r->trace = copy_bytes(s->best_trace.data, s->best_trace.len);
    r->bytes = copy_bytes(s->best_bytes.data, s->best_bytes.len);
    r->witness = malloc(witness_bytes);
    if (r->trace == NULL || r->bytes == NULL || r->witness == NULL) {
        canon_result_release(r);
        *out = new_result(CANON_RESOURCE_LIMIT);
        return CANON_RESOURCE_LIMIT;
    }
    r->trace_len = s->best_trace.len;
    r->bytes_len = s->best_bytes.len;
    if (n > 0) {
        memcpy(r->witness, s->best_t, (size_t)n * sizeof *r->witness);
    }
    r->degree = n;
    /* spec 3.2: COMPLETE for CANONICAL_IMAGE after the full unpruned traversal (TRUSTED_ENGINE
     * evidence): the witness is an element of G sending x to the returned image, the image is
     * the P1 canonical image, and the encoding is complete.  Every other flag stays false. */
    r->flags.witness_valid = true;
    r->flags.image_canonical = true;
    r->flags.encoding_complete = true;
    *out = r;
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

/* spec 3.2: "Incomplete bytes cannot be used as a canonical database key." */
const uint8_t *canon_result_bytes(const canon_result *result, size_t *length)
{
    if (result == NULL || result->bytes == NULL || !result->flags.image_canonical ||
        !result->flags.encoding_complete) {
        if (length != NULL) {
            *length = 0;
        }
        return NULL;
    }
    if (length != NULL) {
        *length = result->bytes_len;
    }
    return result->bytes;
}

/* spec 17: "result_encode returns canonical bytes only when the corresponding image is
 * complete"; the sink receives ordered borrowed chunks valid only during its callback. */
canon_status canon_result_encode(const canon_result *result, canon_sink_fn sink, void *user)
{
    if (result == NULL || sink == NULL) {
        return CANON_INVALID_INPUT;
    }
    size_t total = 0;
    const uint8_t *bytes = canon_result_bytes(result, &total);
    if (bytes == NULL) {
        return CANON_INVALID_INPUT;
    }
    size_t offset = 0;
    while (offset < total) {
        size_t chunk = total - offset < SINK_CHUNK ? total - offset : SINK_CHUNK;
        size_t accepted = 0;
        int rc = sink(user, bytes + offset, chunk, &accepted);
        /* S1: 0 continues; PAUSE (1) is deferred to S8 and treated as failure; anything else
         * is FAIL (spec 17: "FAIL returns OUTPUT_ERROR").  No progress is also a failure, so
         * no byte is ever skipped or duplicated. */
        if (rc != 0 || accepted == 0 || accepted > chunk) {
            return CANON_OUTPUT_ERROR;
        }
        offset += accepted;
    }
    return CANON_COMPLETE;
}
