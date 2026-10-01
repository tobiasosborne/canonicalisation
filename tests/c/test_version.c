/* ctest: canon_version() matches the header constants; remaining stubs report
 * UNSUPPORTED_ACTION; the slice S1 entry points validate NULL arguments.
 *
 * Documented stub behaviour (src/api/stubs.c): the section 17 entry points not implemented in
 * slice S1 (canon_object_create from a stream, canon_solve_batch, canon_result_verify_witness,
 * canon_checkpoint_write/read) return CANON_UNSUPPORTED_ACTION unconditionally, even for NULL
 * arguments, and store NULL through out-pointers.  The S1 entry points (src/api/api.c) return
 * CANON_INVALID_INPUT for NULL required arguments and store NULL through out-pointers. */
#include <stdio.h>
#include <string.h>

#include "canon/canon.h"

static int failures;

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond);               \
            ++failures;                                                                            \
        }                                                                                          \
    } while (0)

int main(void)
{
    CHECK(CANON_VERSION_MAJOR == 0 && CANON_VERSION_MINOR == 0 && CANON_VERSION_PATCH == 1);
    CHECK(strcmp(CANON_SPEC_VERSION, "2.0") == 0);
    CHECK(canon_version() == (uint32_t)CANON_VERSION_PACK(0, 0, 1));
    CHECK(strcmp(canon_version_string(), "0.0.1") == 0);

    char expect[32];
    (void)snprintf(expect, sizeof expect, "%d.%d.%d", CANON_VERSION_MAJOR, CANON_VERSION_MINOR,
                   CANON_VERSION_PATCH);
    CHECK(strcmp(canon_version_string(), expect) == 0);

    /* S1 entry points: NULL arguments are INVALID_INPUT and out-pointers receive NULL. */
    canon_result *result = (canon_result *)&failures; /* sentinel: must be overwritten with NULL */
    CHECK(canon_solve(NULL, NULL, &result) == CANON_INVALID_INPUT);
    CHECK(result == NULL);
    CHECK(canon_solve(NULL, NULL, NULL) == CANON_INVALID_INPUT);

    canon_group *group = (canon_group *)&failures;
    CHECK(canon_group_create(NULL, 0, NULL, 0, &group) == CANON_INVALID_INPUT);
    CHECK(group == NULL);
    CHECK(canon_problem_create(NULL, NULL, NULL, CANON_OBJECTIVE_CANONICAL_IMAGE, CANON_PROFILE_P1,
                               CANON_ENCODING_CDAG_2, CANON_ORDER_CDAG_BYTE_1, NULL,
                               NULL) == CANON_INVALID_INPUT);
    CHECK(canon_workspace_create(NULL, NULL) == CANON_INVALID_INPUT);
    CHECK(canon_result_encode(NULL, NULL, NULL) == CANON_INVALID_INPUT);

    /* Remaining stubs. */
    CHECK(canon_object_create(NULL, CANON_SCHEMA_EXT_DAG_1, CANON_ACTION_ATOM_TRANSPORT_1, 0, NULL,
                              0, NULL) == CANON_UNSUPPORTED_ACTION);

    canon_status st[2] = {CANON_COMPLETE, CANON_COMPLETE};
    canon_result *rs[2] = {NULL, NULL};
    CHECK(canon_solve_batch(NULL, NULL, 2, rs, st) == CANON_UNSUPPORTED_ACTION);
    CHECK(st[0] == CANON_UNSUPPORTED_ACTION && st[1] == CANON_UNSUPPORTED_ACTION);

    bool valid = true;
    CHECK(canon_result_verify_witness(NULL, &valid) == CANON_UNSUPPORTED_ACTION);
    CHECK(!valid);
    CHECK(canon_checkpoint_write(NULL, NULL, NULL) == CANON_UNSUPPORTED_ACTION);
    CHECK(canon_checkpoint_read(NULL, NULL, 0, NULL) == CANON_UNSUPPORTED_ACTION);

    if (failures != 0) {
        return 1;
    }
    puts("test_version: ok");
    return 0;
}
