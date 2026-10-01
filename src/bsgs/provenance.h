/*
 * Internal header: straight-line provenance for strong generators (spec 9.1, 9.2; slice S3,
 * docs/slices/S3.md 2.2 item 6).
 *
 * spec 9.2: "Store shared straight-line derivations (input, inverse, product) with child
 * indices and exact verification; do not expand long words repeatedly."  A provenance is a DAG
 * of nodes numbered 0, 1, ...; every node refers only to EARLIER nodes, so node order is a
 * topological order and a single forward pass evaluates all of them.
 *
 *   INPUT i        the i-th original input generator (an index into the input table)
 *   INVERSE j      the inverse of node j
 *   PRODUCT j k    the product (node j)(node k): node j acts first, (pq)[v] = q[p[v]] (spec 3)
 *
 * The identity is never a node (spec 9.3 tagged identity): a word of length zero has no node
 * and callers use CANON_PROV_NONE for it.
 */
#ifndef CANON_SRC_BSGS_PROVENANCE_H
#define CANON_SRC_BSGS_PROVENANCE_H

#include <stdbool.h>
#include <stdint.h>

#include "canon/canon.h"
#include "perm/perm.h"

#define CANON_PROV_NONE UINT32_MAX /* the empty word (identity): no node */

typedef enum canon_prov_kind {
    CANON_PROV_INPUT = 1,
    CANON_PROV_INVERSE = 2,
    CANON_PROV_PRODUCT = 3
} canon_prov_kind;

typedef struct canon_prov_node {
    uint32_t kind; /* canon_prov_kind */
    uint32_t a;    /* INPUT: input index; INVERSE: node; PRODUCT: first factor (acts first) */
    uint32_t b;    /* PRODUCT: second factor; otherwise 0 */
} canon_prov_node;

typedef struct canon_provenance {
    uint32_t count, cap;
    canon_prov_node *nodes;
} canon_provenance;

void canon_prov_init(canon_provenance *p);
void canon_prov_free(canon_provenance *p);

/* Append a node and return its id in *id.  The operands must be existing nodes (< count);
 * otherwise CANON_INTERNAL_ERROR.  CANON_CAPACITY_LIMIT when the node count would exceed
 * uint32 ids (spec 11.1), CANON_RESOURCE_LIMIT on allocation failure (p unchanged). */
canon_status canon_prov_input(canon_provenance *p, uint32_t input, uint32_t *id);
canon_status canon_prov_inverse(canon_provenance *p, uint32_t node, uint32_t *id);
/* PRODUCT j k with the identity rule: if j or k is CANON_PROV_NONE the other is returned and no
 * node is added (both NONE gives NONE). */
canon_status canon_prov_product(canon_provenance *p, uint32_t j, uint32_t k, uint32_t *id);

/* spec 9.1 "each generator's derivation from the original input": re-derive the arrays of
 * the `count` nodes targets[k] and compare each with expected[k] (inputs->n entries);
 * *match = all equal.  Only the nodes reachable from the targets are evaluated, in node order
 * (a topological order), reading INPUT entries from `inputs`, and each row is released after
 * its last use (S3 review item 4); *peak_rows receives the largest number of rows live at once.
 * Returns CANON_INVALID_INPUT if any record is malformed (unknown kind, an operand that is not
 * an earlier node, an input index out of range, a target that is not a node, or a non-bijective
 * input or operand), so a corrupted record is reported and never read out of bounds;
 * CANON_CAPACITY_LIMIT / CANON_RESOURCE_LIMIT when scratch cannot be allocated. */
canon_status canon_prov_check(const canon_provenance *p, const canon_perm_table *inputs,
                              const uint32_t *targets, const uint32_t *const *expected,
                              uint32_t count, bool *match, uint32_t *peak_rows);

#endif /* CANON_SRC_BSGS_PROVENANCE_H */
