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

/* Test-only reference for the pre-R1 split algorithm (the old
 * src/partition/partition.c split): per old cell, gather (key, member)
 * pairs in lab order, stable sort by key, cut a boundary where the key
 * changes, then rebuild pos/cell_of from the final (lab, start).  The
 * stable sort here is a plain stable insertion sort: the ordering by
 * (key, input position) is unique, so it agrees with the old stable
 * merge sort (canon_stable_sort over pair_cmp) element for element.
 * The production counting split must reproduce this reference exactly
 * (bit-identical lab, start, pos, cell_of).  Fixed-size buffers (no
 * VLAs); the differential driver below keeps n within REF_MAX. */
enum { REF_MAX = 96 };

typedef struct ref_pair {
    uint32_t key;
    uint32_t member;
} ref_pair;

static uint32_t ref_split(uint32_t n, uint32_t cells, const uint32_t *start, const uint32_t *lab,
                          const uint32_t *sig, uint32_t *out_lab, uint32_t *out_start,
                          uint32_t *out_pos, uint32_t *out_cell_of)
{
    (void)n;
    ref_pair pairs[REF_MAX];
    uint32_t out = 0;
    for (uint32_t i = 0; i < cells; ++i) {
        uint32_t lo = start[i], hi = start[i + 1];
        for (uint32_t j = lo; j < hi; ++j) {
            pairs[j].key = sig[lab[j]];
            pairs[j].member = lab[j];
        }
        /* stable insertion sort by key: shift only strictly greater keys */
        for (uint32_t j = lo + 1; j < hi; ++j) {
            ref_pair x = pairs[j];
            uint32_t k = j;
            while (k > lo && pairs[k - 1].key > x.key) {
                pairs[k] = pairs[k - 1];
                --k;
            }
            pairs[k] = x;
        }
        out_start[out++] = lo;
        for (uint32_t j = lo; j < hi; ++j) {
            out_lab[j] = pairs[j].member;
            if (j > lo && pairs[j].key != pairs[j - 1].key) {
                out_start[out++] = j;
            }
        }
    }
    out_start[out] = start[cells];
    for (uint32_t i = 0; i < out; ++i) {
        for (uint32_t j = out_start[i]; j < out_start[i + 1]; ++j) {
            out_pos[out_lab[j]] = j;
            out_cell_of[out_lab[j]] = i;
        }
    }
    return out;
}

/* Install (lab, start, cells) on p and rebuild pos/cell_of in the test, so
 * the differential driver can start from arbitrary ordered partitions. */
static void set_partition(canon_partition *p, const uint32_t *lab, const uint32_t *start,
                          uint32_t cells)
{
    memcpy(p->lab, lab, (size_t)p->n * sizeof *lab);
    memcpy(p->start, start, ((size_t)cells + 1u) * sizeof *start);
    p->cells = cells;
    for (uint32_t i = 0; i < cells; ++i) {
        for (uint32_t j = start[i]; j < start[i + 1]; ++j) {
            p->pos[lab[j]] = j;
            p->cell_of[lab[j]] = i;
        }
    }
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
    const uint32_t sig6[6] = {0, 9, 7, 1, 7, 0};
    CHECK(canon_partition_split(&p, sig6));
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

    /* Differential test (R1, E-perf-refine-02): the production split
     * (stable counting split, merge-sort fallback for sparse key ranges)
     * against the test-only reference of the old algorithm above.
     * Random ordered partitions (shuffled lab; a single cell, all
     * singletons, or random boundaries) x key shapes (ties on 0..2,
     * dense 0..n-1, constant, maximal uint32 ranges, a large base with
     * a small range, sorted/reverse along lab).  Dense small-range keys
     * in one big cell exercise the counting path; the maximal-range
     * keys exercise the fallback.  lab, start, pos, cell_of, the cell
     * count and the return value must all match exactly. */
    CHECK(canon_partition_init(&p, REF_MAX) == CANON_COMPLETE);
    {
        static const uint32_t sizes[] = {0, 1, 2, 3, 5, 17, 64, REF_MAX};
        static const uint32_t extreme[] = {0u, 1u, 0x80000000u, 0xfffffffeu, 0xffffffffu};
        for (uint32_t trial = 0; trial < 420; ++trial) {
            uint32_t n = sizes[trial % (sizeof sizes / sizeof *sizes)];
            canon_partition_set_degree(&p, n);
            uint32_t lab0[REF_MAX], start0[REF_MAX + 1], sig[REF_MAX];
            for (uint32_t v = 0; v < n; ++v) {
                lab0[v] = v;
            }
            for (uint32_t v = n; v > 1; --v) { /* Fisher-Yates with the test rng */
                uint32_t j = (uint32_t)(check_rng() % v);
                uint32_t t = lab0[v - 1];
                lab0[v - 1] = lab0[j];
                lab0[j] = t;
            }
            uint32_t cells0 = 0;
            uint32_t shape = (trial / 8u) % 3u;
            if (n == 0) {
                start0[0] = 0;
            } else if (shape == 0) { /* a single cell */
                cells0 = 1;
                start0[0] = 0;
                start0[1] = n;
            } else if (shape == 1) { /* all singletons */
                cells0 = n;
                for (uint32_t i = 0; i <= n; ++i) {
                    start0[i] = i;
                }
            } else { /* random boundaries */
                uint32_t at = 0;
                while (at < n) {
                    start0[cells0++] = at;
                    uint32_t room = n - at;
                    at += 1u + (uint32_t)(check_rng() % (room < 7u ? room : 7u));
                }
                start0[cells0] = n;
            }
            set_partition(&p, lab0, start0, cells0);
            invariants(&p);
            switch (trial % 7u) {
            case 0: /* ties: three keys */
                for (uint32_t v = 0; v < n; ++v) {
                    sig[v] = (uint32_t)(check_rng() % 3u);
                }
                break;
            case 1: /* dense: keys in 0..n-1 */
                for (uint32_t v = 0; v < n; ++v) {
                    sig[v] = (uint32_t)(check_rng() % (n > 0 ? n : 1u));
                }
                break;
            case 2: /* constant: the identity split */
                for (uint32_t v = 0; v < n; ++v) {
                    sig[v] = 7u;
                }
                break;
            case 3: /* maximal uint32 key range (fallback territory) */
                for (uint32_t v = 0; v < n; ++v) {
                    sig[v] = extreme[check_rng() % (sizeof extreme / sizeof *extreme)];
                }
                break;
            case 4: /* large base, small range: min-offset arithmetic */
                for (uint32_t v = 0; v < n; ++v) {
                    sig[v] = 0xffffff00u + (uint32_t)(check_rng() % 3u);
                }
                break;
            case 5: /* already sorted along lab */
                for (uint32_t j = 0; j < n; ++j) {
                    sig[lab0[j]] = j;
                }
                break;
            default: /* reverse along lab */
                for (uint32_t j = 0; j < n; ++j) {
                    sig[lab0[j]] = n - 1u - j;
                }
                break;
            }
            uint32_t want_lab[REF_MAX], want_start[REF_MAX + 1];
            uint32_t want_pos[REF_MAX], want_cell_of[REF_MAX];
            uint32_t want_cells = ref_split(n, cells0, start0, lab0, sig, want_lab, want_start,
                                            want_pos, want_cell_of);
            int grew = canon_partition_split(&p, sig) ? 1 : 0;
            invariants(&p);
            CHECK(grew == (want_cells > cells0 ? 1 : 0));
            CHECK(p.cells == want_cells);
            CHECK(memcmp(p.lab, want_lab, (size_t)n * sizeof *want_lab) == 0);
            CHECK(memcmp(p.start, want_start, ((size_t)want_cells + 1u) * sizeof *want_start) ==
                  0);
            CHECK(memcmp(p.pos, want_pos, (size_t)n * sizeof *want_pos) == 0);
            CHECK(memcmp(p.cell_of, want_cell_of, (size_t)n * sizeof *want_cell_of) == 0);
        }
    }
    canon_partition_free(&p);
    return check_finish("test_partition");
}
