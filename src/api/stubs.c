/* Stubs for the spec section 17 entry points not implemented yet (slices S1-S5).  Every function
 * reports CANON_UNSUPPORTED_ACTION (spec section 3.2: unknown or unimplemented actions are
 * unsupported, never reinterpreted) and stores NULL through any out-pointer it is given.
 * No argument is validated or dereferenced other than the out-pointers.
 * Replaced by: canon_solve_batch (S8), canon_checkpoint_write/read (M6).
 * canon_result_verify_witness was implemented in S4, canon_object_create in S5.
 * The implemented entry points live in src/api/api.c. */
#include "canon/canon.h"

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
