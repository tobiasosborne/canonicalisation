/* Static checks on the public header against the specification numbers.
 * Spec section 3 (objective tags), 3.2 (status enum, nine flags), 4.1/4.3/4.4/7.1 (IDs). */
#include <stdbool.h>
#include <stdio.h>

#include "canon/canon.h"
#include "perm/perm.h" /* internal header: compile check only (declarations, no definitions) */

_Static_assert(sizeof(canon_objective) == 2, "objective tags are uint16 (spec section 3)");
_Static_assert(sizeof(canon_schema) == 2 && sizeof(canon_action) == 2, "U16 fields (spec 4.1)");
_Static_assert(sizeof(canon_profile) == 2 && sizeof(canon_encoding) == 2 &&
                   sizeof(canon_order) == 2,
               "uint16 IDs");
_Static_assert(sizeof(canon_status) <= sizeof(int), "status is a plain enum");

_Static_assert(CANON_OBJECTIVE_CANONICAL_IMAGE == 0x0001, "spec section 3");
_Static_assert(CANON_OBJECTIVE_LEX_MIN_IMAGE == 0x0002, "spec section 3");
_Static_assert(CANON_OBJECTIVE_TRANSPORTER_ONE == 0x0003, "spec section 3");
_Static_assert(CANON_OBJECTIVE_STABILISER == 0x0004, "spec section 3");
_Static_assert(CANON_OBJECTIVE_CANONICAL_LABELING_COSET == 0x0005, "spec section 3");
_Static_assert(CANON_OBJECTIVE_TRANSPORTER_COSET == 0x0006, "spec section 3");
_Static_assert(CANON_OBJECTIVE_SIGNED_CANONICAL_IMAGE == 0x0007, "spec section 3");
_Static_assert(CANON_OBJECTIVE_CONSTRAINT_ONE == 0x0008, "spec section 3");
_Static_assert(CANON_OBJECTIVE_CONSTRAINT_ENUM == 0x0009, "spec section 3");

/* Eight statuses (spec 3.2), contiguous from 0: the last member has value 7. */
_Static_assert(CANON_COMPLETE == 0, "first status");
_Static_assert(CANON_CANCELLED == 1, "spec 3.2 order");
_Static_assert(CANON_CAPACITY_LIMIT == 2, "spec 3.2 order");
_Static_assert(CANON_RESOURCE_LIMIT == 3, "spec 3.2 order");
_Static_assert(CANON_INVALID_INPUT == 4, "spec 3.2 order");
_Static_assert(CANON_UNSUPPORTED_ACTION == 5, "spec 3.2 order");
_Static_assert(CANON_OUTPUT_ERROR == 6, "spec 3.2 order");
_Static_assert(CANON_INTERNAL_ERROR == 7, "last of eight statuses");

_Static_assert(CANON_SCHEMA_EXT_DAG_1 == 1, "spec 4.1 schema");
_Static_assert(CANON_ACTION_ATOM_TRANSPORT_1 == 1, "spec 4.1 action");
_Static_assert(CANON_PROFILE_NO_TREE == 0, "spec 4.3 profile 0x0000");
_Static_assert(CANON_PROFILE_P1 == 1, "spec 7.1 profile 0x0001");
_Static_assert(CANON_ENCODING_CDAG_2 == 2, "spec 4.1 encoding 0x0002");
_Static_assert(CANON_ORDER_CDAG_BYTE_1 == 1, "spec 4.3 order 0x0001");
_Static_assert(CANON_ORDER_SIMPLE_UPPER_1 == 2, "spec 4.4 order 0x0002");

/* Nine boolean flags (spec 3.2) and nothing else. */
_Static_assert(sizeof(canon_result_flags) == 9 * sizeof(bool), "nine booleans");

int main(void)
{
    canon_result_flags f = {false, false, false, false, false, false, false, false, false};
    if (f.witness_valid || f.image_canonical || f.minimum_proved || f.subgroup_verified ||
        f.stabiliser_complete || f.transport_exhausted || f.zero_certified || f.nonzero_certified ||
        f.encoding_complete) {
        return 1;
    }
    puts("test_header_abi: ok");
    return 0;
}
