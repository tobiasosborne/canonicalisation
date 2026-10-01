/*
 * Internal header: the subset object (spec sections 2.1, 4.2, 7.1).  Implemented in slice S1
 * (docs/slices/S1.md section 4.3).
 *
 * A subset of the atom domain {0..n-1} is stored as a bitset plus the strictly increasing list
 * of its members.  It is immutable after canon_subset_init.
 */
#ifndef CANON_SRC_OBJECT_SUBSET_H
#define CANON_SRC_OBJECT_SUBSET_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "canon/canon.h"

typedef struct canon_subset {
    uint32_t n;      /* degree: size of the atom domain */
    uint32_t k;      /* number of distinct members */
    uint32_t *atoms; /* k strictly increasing members; NULL when k == 0 */
    uint64_t *bits;  /* ceil(n / 64) words; NULL when n == 0 */
} canon_subset;

/* Number of uint64_t words of an n-bit bitset. */
size_t canon_bitset_words(uint32_t n);

/* Build a subset of {0..n-1} from `count` atoms (copied).  spec 4.2: sets deduplicate equal
 * children, so duplicates are accepted and merged.  spec 4.1: an atom >= n is invalid
 * (CANON_INVALID_INPUT).  Allocation failure is CANON_RESOURCE_LIMIT.  On failure *s is left
 * empty and canon_subset_free(s) is still valid. */
canon_status canon_subset_init(canon_subset *s, uint32_t n, const uint32_t *atoms, size_t count);

/* Release storage; valid on a zeroed or failed subset. */
void canon_subset_free(canon_subset *s);

/* Membership test; a must be < n. */
bool canon_subset_contains(const canon_subset *s, uint32_t a);

/* spec 7.1: on the top-level subset the P1 initial key of atom a is membership 0/1. */
uint32_t canon_subset_initial_key(const canon_subset *s, uint32_t a);

/* spec 2.1 action ATOM-TRANSPORT-1: x^g = {g[a] : a in x}.  Writes the image's k members in
 * increasing order to out_atoms.  `scratch` has canon_bitset_words(n) words; its contents are
 * overwritten.  g must be a permutation of {0..n-1}. */
void canon_subset_act_sorted(const canon_subset *s, const uint32_t *g, uint64_t *scratch,
                             uint32_t *out_atoms);

/* Extensional equality (same degree, same members). */
bool canon_subset_equal(const canon_subset *a, const canon_subset *b);

#endif /* CANON_SRC_OBJECT_SUBSET_H */
