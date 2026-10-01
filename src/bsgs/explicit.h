/*
 * Internal header: explicit-enumeration group backend (slice S1, docs/slices/S1.md 4.2).
 * The whole group is held as a lexicographically sorted table of |G| image arrays.  Every query
 * is O(|G| * n); slice S3 replaces this backend by a verified stabiliser chain (spec 9.1).
 */
#ifndef CANON_SRC_BSGS_EXPLICIT_H
#define CANON_SRC_BSGS_EXPLICIT_H

#include <stddef.h>
#include <stdint.h>

#include "bsgs/group.h"

/* Build <generators> on {0..degree-1} by closure.  `generators` is a flat array of
 * generator_count * degree images (p[v] = v^p).  Identity and repeated generators are allowed.
 * Returns CANON_INVALID_INPUT if a generator is not a bijection of the domain,
 * CANON_CAPACITY_LIMIT if the closure would hold more than max_order elements (checked before
 * each growth of the table, so the outcome depends only on the generated group and max_order),
 * or CANON_RESOURCE_LIMIT on allocation failure.  On success *out has one reference. */
canon_status canon_group_explicit_create(uint32_t degree, const uint32_t *generators,
                                         size_t generator_count, uint64_t max_order,
                                         canon_group **out);

/* spec 8.4 (slice S6): as canon_group_explicit_create, for a signed group: signs[i] in
 * {-1, +1} is chi(generators[i]) (the caller checks the values; signs may be NULL only when
 * generator_count is 0).  The lifted group on degree + 2 points is closed into a second sorted
 * table, bounded by |G| rows (|lift| >= |G|, with equality iff the signs define a character), and
 * kept for `character`: CANON_INVALID_INPUT when the closure exceeds |G| rows (spec 8.4 "Reject
 * inconsistent signs"); CANON_CAPACITY_LIMIT when degree + 2 does not fit uint32 (spec 11.1,
 * checked first), when |G| exceeds max_order, or when the lift's table could not be sized.  The
 * handle keeps the generators and signs (canon_group_signs). */
canon_status canon_group_explicit_create_signed(uint32_t degree, const uint32_t *generators,
                                                size_t generator_count, const int8_t *signs,
                                                uint64_t max_order, canon_group **out);

#endif /* CANON_SRC_BSGS_EXPLICIT_H */
