/* Stubs for the spec section 17 entry points.  Every function reports
 * CANON_UNSUPPORTED_ACTION (spec section 3.2: unknown or unimplemented actions are
 * unsupported, never reinterpreted) and stores NULL through any out-pointer it is given.
 * No argument is validated or dereferenced other than the out-pointers.
 * TODO(M4): spec section 17 -- replace each stub with the real implementation. */
#include "canon/canon.h"

/* spec section 17 */
canon_status canon_group_create(canon_context *ctx, uint32_t degree, const uint32_t *generators,
                                size_t generator_count, canon_group **out)
{
    (void)ctx;
    (void)degree;
    (void)generators;
    (void)generator_count;
    if (out != NULL) {
        *out = NULL;
    }
    return CANON_UNSUPPORTED_ACTION;
}

/* spec section 17 */
canon_status canon_object_create(canon_context *ctx, canon_schema schema, canon_action action,
                                 uint32_t degree, const uint8_t *stream, size_t stream_length,
                                 canon_object **out)
{
    (void)ctx;
    (void)schema;
    (void)action;
    (void)degree;
    (void)stream;
    (void)stream_length;
    if (out != NULL) {
        *out = NULL;
    }
    return CANON_UNSUPPORTED_ACTION;
}

/* spec section 17 */
canon_status canon_problem_create(canon_context *ctx, const canon_group *group,
                                  const canon_object *object, canon_objective objective,
                                  canon_profile profile, canon_encoding encoding, canon_order order,
                                  canon_problem **out)
{
    (void)ctx;
    (void)group;
    (void)object;
    (void)objective;
    (void)profile;
    (void)encoding;
    (void)order;
    if (out != NULL) {
        *out = NULL;
    }
    return CANON_UNSUPPORTED_ACTION;
}

/* spec section 17 */
canon_status canon_workspace_create(canon_context *ctx, canon_workspace **out)
{
    (void)ctx;
    if (out != NULL) {
        *out = NULL;
    }
    return CANON_UNSUPPORTED_ACTION;
}

/* spec section 17 */
canon_status canon_solve(canon_workspace *workspace, const canon_problem *problem,
                         canon_result **out)
{
    (void)workspace;
    (void)problem;
    if (out != NULL) {
        *out = NULL;
    }
    return CANON_UNSUPPORTED_ACTION;
}

/* spec section 17 */
canon_status canon_solve_batch(canon_workspace *workspace, const canon_problem *const *problems,
                               size_t count, canon_result **results, canon_status *statuses)
{
    (void)workspace;
    (void)problems;
    if (results != NULL && statuses != NULL) {
        for (size_t i = 0; i < count; ++i) {
            results[i] = NULL;
            statuses[i] = CANON_UNSUPPORTED_ACTION;
        }
    }
    return CANON_UNSUPPORTED_ACTION;
}

/* spec section 17 */
canon_status canon_result_verify_witness(const canon_result *result, bool *valid)
{
    (void)result;
    if (valid != NULL) {
        *valid = false; /* spec section 3.2: false means unproved */
    }
    return CANON_UNSUPPORTED_ACTION;
}

/* spec section 17 */
canon_status canon_result_encode(const canon_result *result, canon_sink_fn sink, void *user)
{
    (void)result;
    (void)sink;
    (void)user;
    return CANON_UNSUPPORTED_ACTION;
}

/* spec sections 17, 18 */
canon_status canon_checkpoint_write(const canon_workspace *workspace, canon_sink_fn sink,
                                    void *user)
{
    (void)workspace;
    (void)sink;
    (void)user;
    return CANON_UNSUPPORTED_ACTION;
}

/* spec sections 17, 18 */
canon_status canon_checkpoint_read(canon_context *ctx, const uint8_t *bytes, size_t length,
                                   canon_checkpoint **out)
{
    (void)ctx;
    (void)bytes;
    (void)length;
    if (out != NULL) {
        *out = NULL;
    }
    return CANON_UNSUPPORTED_ACTION;
}
