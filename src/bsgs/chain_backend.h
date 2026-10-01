/*
 * Internal header: the stabiliser-chain group backend (slice S3, docs/slices/S3.md 1, 2.5),
 * the default implementation of canon_group_ops (src/bsgs/group.h).
 */
#ifndef CANON_SRC_BSGS_CHAIN_BACKEND_H
#define CANON_SRC_BSGS_CHAIN_BACKEND_H

#include <stddef.h>
#include <stdint.h>

#include "bsgs/chain.h"
#include "bsgs/group.h"

/* Build <generators> on {0..degree-1} as a verified stabiliser chain.  `generators` is a flat
 * array of generator_count * degree images (p[v] = v^p); identities and repeats are allowed
 * (degree 0: not read, may be NULL).  Returns CANON_INVALID_INPUT if a generator is not a
 * bijection of the domain, CANON_CAPACITY_LIMIT if |G| exceeds uint64 (spec 11.1 count-bit
 * limit 64, detailed plan 2.1; the multi-limb canon_nat that would lift this is deferred,
 * docs/slices/S4-notes.md) or a size does not fit,
 * CANON_RESOURCE_LIMIT on allocation failure, CANON_INTERNAL_ERROR if the independent verifier
 * rejects the constructed chain.  There is no limit on the order other than uint64: the
 * capacity descriptor's max_group_order applies to the explicit backend only (S3 brief 2.5).
 * On success *out has one reference. */
canon_status canon_group_chain_create(uint32_t degree, const uint32_t *generators,
                                      size_t generator_count, canon_group **out);

/* spec 8.4 (slice S6): as canon_group_chain_create, for a signed group: signs[i] in {-1, +1} is
 * chi(generators[i]) (the caller checks the values; signs may be NULL only when generator_count
 * is 0).  The signs are validated by the lifted group on degree + 2 points, kept as a second
 * verified chain for `character`: CANON_INVALID_INPUT unless |lift| = |G| (spec 8.4 "Reject
 * inconsistent signs"); CANON_CAPACITY_LIMIT when degree + 2 does not fit uint32 (spec 11.1,
 * checked before anything else is built) or the lift's order exceeds uint64 (which, |G| fitting,
 * happens only for inconsistent signs with |G| >= 2^63: the count-bit limit decides first).
 * The handle keeps the generators and signs (canon_group_signs). */
canon_status canon_group_chain_create_signed(uint32_t degree, const uint32_t *generators,
                                             size_t generator_count, const int8_t *signs,
                                             canon_group **out);

/* The verified chain behind a group created by canon_group_chain_create, or NULL for a group
 * of another backend (tests and diagnostics). */
const canon_bsgs *canon_group_chain_of(const canon_group *group);

#endif /* CANON_SRC_BSGS_CHAIN_BACKEND_H */
