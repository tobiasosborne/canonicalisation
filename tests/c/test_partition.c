/* Unit tests for src/partition (spec sections 7.1, 10, 11.2). */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "partition/partition.h"

/* spec 10 invariants: lab[pos[v]] = v, cell_of consistent with start, exact coverage. */
static void invariants(const canon_partition *p)
{
    CHECK(p->start[0] == 0);
    CHECK(p->start[p->cells] == p->n);
    for (uint32_t i = 0; i < p->cells; ++i) {
        CHECK(p->start[i] < p->start[i + 1]); /* nonempty cells */
        for (uint32_t j = p->start[i]; j < p->start[i + 1]; ++j) {
            CHECK(p->pos[p->lab[j]] == j);
            CHECK(p->cell_of[p->lab[j]] == i);
        }
    }
}

/* Cell i as a sorted set equals want[0..len). */
static int cell_is(const canon_partition *p, uint32_t i, const uint32_t *want, uint32_t len)
{
    if (canon_partition_cell_size(p, i) != len) {
        return 0;
    }
    for (uint32_t w = 0; w < len; ++w) {
        if (p->cell_of[want[w]] != i) {
            return 0;
        }
    }
    return 1;
}

int main(void)
{
    canon_partition p;
    /* n = 0: empty ordered partition, discrete, splits are no-ops. */
    CHECK(canon_partition_init(&p, 0) == CANON_COMPLETE);
    CHECK(p.cells == 0 && canon_partition_is_discrete(&p));
    CHECK(!canon_partition_split(&p, NULL));
    invariants(&p);
    size_t words = 0;
    CHECK(canon_partition_snapshot_words(0, &words) == CANON_COMPLETE && words == 2);
    uint32_t snap0[2];
    canon_partition_save(&p, snap0);
    canon_partition_restore(&p, snap0);
    CHECK(p.cells == 0);
    canon_partition_free(&p);
    canon_partition_free(&p); /* idempotent */

    CHECK(canon_partition_init(&p, 6) == CANON_COMPLETE);
    CHECK(p.cells == 1 && canon_partition_cell_size(&p, 0) == 6);
    invariants(&p);
    /* spec 7.1 initial key: membership; cells ordered by increasing key: [{non-members},
     * {members}]. */
    const uint32_t member[6] = {1, 0, 0, 1, 0, 1};
    CHECK(canon_partition_split(&p, member));
    invariants(&p);
    const uint32_t c0[3] = {1, 2, 4}, c1[3] = {0, 3, 5};
    CHECK(p.cells == 2 && cell_is(&p, 0, c0, 3) && cell_is(&p, 1, c1, 3));
    /* A split never reorders old cells: keys that would sort cell 1's points before cell 0's
     * still leave them in position 1 and later. */
    const uint32_t sig[6] = {0, 9, 7, 1, 7, 0};
    CHECK(canon_partition_split(&p, sig));
    invariants(&p);
    const uint32_t d0[2] = {2, 4}, d1[1] = {1}, d2[2] = {0, 5}, d3[1] = {3};
    CHECK(p.cells == 4 && cell_is(&p, 0, d0, 2) && cell_is(&p, 1, d1, 1) && cell_is(&p, 2, d2, 2) &&
          cell_is(&p, 3, d3, 1));
    /* A constant signature is the identity split. */
    const uint32_t flat[6] = {5, 5, 5, 5, 5, 5};
    CHECK(!canon_partition_split(&p, flat));
    CHECK(p.cells == 4);
    invariants(&p);

    /* Snapshot, individualise, restore exactly. */
    CHECK(canon_partition_snapshot_words(6, &words) == CANON_COMPLETE && words == 14);
    uint32_t *snap = malloc(words * sizeof *snap);
    CHECK(snap != NULL);
    if (snap != NULL) {
        uint32_t lab_before[6], start_before[7];
        memcpy(lab_before, p.lab, sizeof lab_before);
        memcpy(start_before, p.start, sizeof start_before);
        canon_partition_save(&p, snap);
        CHECK(memcmp(canon_partition_snapshot_lab(snap, 6), lab_before, sizeof lab_before) == 0);
        /* spec 7.1: cell C replaced in place by [{a}, C minus {a}]. */
        canon_partition_individualise(&p, 2, 5);
        invariants(&p);
        const uint32_t e2[1] = {5}, e3[1] = {0};
        CHECK(p.cells == 5 && cell_is(&p, 0, d0, 2) && cell_is(&p, 1, d1, 1) &&
              cell_is(&p, 2, e2, 1) && cell_is(&p, 3, e3, 1) && cell_is(&p, 4, d3, 1));
        canon_partition_individualise(&p, 0, 4);
        invariants(&p);
        CHECK(canon_partition_is_discrete(&p));
        CHECK(p.lab[0] == 4 && p.lab[1] == 2);
        canon_partition_restore(&p, snap);
        invariants(&p);
        CHECK(p.cells == 4);
        CHECK(memcmp(lab_before, p.lab, sizeof lab_before) == 0);
        CHECK(memcmp(start_before, p.start, 5 * sizeof *p.start) == 0);
        free(snap);
    }
    canon_partition_reset(&p);
    CHECK(p.cells == 1);
    invariants(&p);
    /* Capacity: a smaller degree reuses the arrays; degree 0 and back. */
    canon_partition_set_degree(&p, 3);
    CHECK(p.n == 3 && p.cap == 6 && p.cells == 1 && canon_partition_cell_size(&p, 0) == 3);
    invariants(&p);
    canon_partition_set_degree(&p, 0);
    CHECK(p.n == 0 && p.cells == 0);
    canon_partition_set_degree(&p, 6);
    CHECK(p.n == 6 && p.cells == 1);
    invariants(&p);
    canon_partition_free(&p);

    /* Random splits keep the invariants and never merge distinctions. */
    CHECK(canon_partition_init(&p, 40) == CANON_COMPLETE);
    uint32_t keys[40];
    for (int round = 0; round < 50; ++round) {
        uint32_t before = p.cells;
        uint32_t old_cell[40];
        memcpy(old_cell, p.cell_of, sizeof old_cell);
        for (uint32_t v = 0; v < 40; ++v) {
            keys[v] = (uint32_t)(check_rng() % 3);
        }
        (void)canon_partition_split(&p, keys);
        invariants(&p);
        CHECK(p.cells >= before);
        for (uint32_t v = 0; v < 40; ++v) {
            for (uint32_t w = 0; w < 40; ++w) {
                if (old_cell[v] < old_cell[w]) {
                    CHECK(p.cell_of[v] < p.cell_of[w]); /* old order preserved */
                }
                if (old_cell[v] == old_cell[w] && keys[v] < keys[w]) {
                    CHECK(p.cell_of[v] < p.cell_of[w]); /* increasing signature order */
                }
            }
        }
        if (canon_partition_is_discrete(&p)) {
            canon_partition_reset(&p);
        }
    }
    canon_partition_free(&p);
    return check_finish("test_partition");
}
