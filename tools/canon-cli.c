/* canon-cli: command-line driver for the canon library (slices S1, S2 and S3,
 * docs/slices/S1.md 4.9, S2.md 3.6, S3.md 3).
 *
 *   canon-cli p1-subset --n N --gens "a0,a1,...;b0,b1,..." --atoms "x,y,z"
 *                       [--max-nodes K] [--id CASE]
 *   canon-cli p1-graph  --n N --gens "..." [--colours "hex;hex;..."]
 *                       [--arcs "s,t,labelhex,m;..."] [--max-nodes K] [--id CASE]
 *   both also accept [--backend chain|explicit] (slice S3, docs/slices/S3.md 3): the group
 *   backend, default chain; the explicit backend is the test oracle.
 *
 * Prints one refs/compare/FORMAT.md record:
 *   CASE \t 0001 \t STATUS \t trace_hex \t bytes_hex \t witness
 * `--gens ""` (the default) is the trivial group and `--atoms ""` (the default) the empty
 * subset.  Generators are image arrays p[v] = v^p (spec section 3), separated by ';'; for
 * N = 0 a generator is the empty string.  For p1-graph, `--colours ""` (the default) makes every
 * vertex colour empty; otherwise it lists exactly N hex strings separated by ';' (a hex string
 * may be empty).  `--arcs` (default: no arcs) lists arcs "source,target,labelhex,multiplicity"
 * separated by ';' (labelhex may be empty; the multiplicity is passed to the library as given,
 * so 0 yields INVALID_INPUT).  `--max-nodes K` sets the spec 11.1 logical work quota
 * (0 = the context default).  Exit status: 0 on COMPLETE, 3 on any other status, 2 on a usage
 * error.  Uses only the public header. */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "canon/canon.h"

static int usage(const char *msg)
{
    fprintf(stderr,
            "canon-cli: %s\n"
            "usage: canon-cli p1-subset --n N [--gens \"a0,a1,...;b0,...\"] [--atoms \"x,y,...\"]\n"
            "                 [--max-nodes K] [--id CASE]\n"
            "       canon-cli p1-graph --n N [--gens \"...\"] [--colours \"hex;hex;...\"]\n"
            "                 [--arcs \"s,t,labelhex,m;...\"] [--max-nodes K] [--id CASE]\n"
            "       both: [--backend chain|explicit]\n",
            msg);
    return 2;
}

/* Strict unsigned decimal of the whole span [s, s+len): digits only, no sign, no overflow. */
static int parse_u64(const char *s, size_t len, uint64_t max, uint64_t *out)
{
    if (len == 0) {
        return 0;
    }
    uint64_t v = 0;
    for (size_t i = 0; i < len; ++i) {
        if (s[i] < '0' || s[i] > '9') {
            return 0;
        }
        uint64_t d = (uint64_t)(s[i] - '0');
        if (v > (max - d) / 10u) {
            return 0;
        }
        v = v * 10u + d;
    }
    *out = v;
    return 1;
}

/* Parse a comma-separated list of uint32 values from [s, s+len) into out (capacity cap);
 * an empty span is the empty list.  Returns the count, or SIZE_MAX on a syntax error or when
 * more than cap values are present. */
static size_t parse_list(const char *s, size_t len, uint32_t *out, size_t cap)
{
    if (len == 0) {
        return 0;
    }
    size_t count = 0, begin = 0;
    for (size_t i = 0; i <= len; ++i) {
        if (i == len || s[i] == ',') {
            uint64_t v = 0;
            if (count >= cap || !parse_u64(s + begin, i - begin, UINT32_MAX, &v)) {
                return SIZE_MAX;
            }
            out[count++] = (uint32_t)v;
            begin = i + 1;
        }
    }
    return count;
}

static size_t count_char(const char *s, char c)
{
    size_t k = 0;
    for (; *s != '\0'; ++s) {
        k += *s == c;
    }
    return k;
}

static int hex_digit(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

/* Decode the hex span [s, s+len) into out (room for len / 2 bytes); 0 on odd length or a
 * non-hex character. */
static int parse_hex(const char *s, size_t len, uint8_t *out)
{
    if (len % 2 != 0) {
        return 0;
    }
    for (size_t i = 0; i < len; i += 2) {
        int hi = hex_digit(s[i]), lo = hex_digit(s[i + 1]);
        if (hi < 0 || lo < 0) {
            return 0;
        }
        out[i / 2] = (uint8_t)(hi * 16 + lo);
    }
    return 1;
}

/* Length of the span starting at s up to (not including) the next `sep` or the end. */
static size_t span_len(const char *s, char sep)
{
    const char *end = strchr(s, sep);
    return end != NULL ? (size_t)(end - s) : strlen(s);
}

/* A parsed graph: colour pointers/lengths into `pool`, arcs with labels into `pool`. */
typedef struct graph_input {
    const uint8_t **colours;
    size_t *colour_lengths;
    canon_arc *arcs;
    size_t arc_count;
    uint8_t *pool; /* decoded colour and label bytes */
} graph_input;

static void graph_input_free(graph_input *in)
{
    free(in->colours);
    free(in->colour_lengths);
    free(in->arcs);
    free(in->pool);
}

/* Parse --colours and --arcs for degree n.  Returns 0 on success, 2 on a usage error (message
 * printed), 3 on allocation failure. */
static int parse_graph(uint32_t n, const char *colours_arg, const char *arcs_arg, graph_input *in)
{
    memset(in, 0, sizeof *in);
    size_t pool_bytes = strlen(colours_arg) / 2 + strlen(arcs_arg) / 2 + 1;
    size_t arc_slots = *arcs_arg == '\0' ? 0 : count_char(arcs_arg, ';') + 1;
    in->arcs = calloc(arc_slots > 0 ? arc_slots : 1u, sizeof *in->arcs);
    in->pool = malloc(pool_bytes);
    if (in->arcs == NULL || in->pool == NULL) {
        return 3;
    }
    size_t used = 0;
    /* Colours: "" = all empty, passed to the library as NULL arrays (no per-vertex storage, so
     * a huge --n reaches the library's degree check); otherwise exactly n hex strings
     * separated by ';', so n is bounded by the argument's length before anything is
     * allocated. */
    if (*colours_arg != '\0') {
        if (count_char(colours_arg, ';') + 1 != (size_t)n) {
            return usage("--colours must list exactly N hex strings separated by ';'");
        }
        in->colours = calloc(n, sizeof *in->colours);
        in->colour_lengths = calloc(n, sizeof *in->colour_lengths);
        if (in->colours == NULL || in->colour_lengths == NULL) {
            return 3;
        }
        const char *p = colours_arg;
        for (uint32_t v = 0; v < n; ++v) {
            size_t len = span_len(p, ';');
            if (!parse_hex(p, len, in->pool + used)) {
                return usage("--colours expects hex strings");
            }
            in->colours[v] = in->pool + used;
            in->colour_lengths[v] = len / 2;
            used += len / 2;
            p += len + 1;
        }
    }
    /* Arcs: "s,t,labelhex,m" separated by ';'. */
    const char *p = arcs_arg;
    for (size_t i = 0; i < arc_slots; ++i) {
        size_t len = span_len(p, ';');
        const char *f[4];
        size_t flen[4];
        const char *q = p;
        for (int k = 0; k < 4; ++k) {
            const char *comma = memchr(q, ',', (size_t)(p + len - q));
            f[k] = q;
            flen[k] = comma != NULL ? (size_t)(comma - q) : (size_t)(p + len - q);
            if ((k < 3) != (comma != NULL)) {
                return usage("each arc must be \"source,target,labelhex,multiplicity\"");
            }
            q = comma != NULL ? comma + 1 : q;
        }
        uint64_t src = 0, dst = 0, mult = 0;
        if (!parse_u64(f[0], flen[0], UINT32_MAX, &src) ||
            !parse_u64(f[1], flen[1], UINT32_MAX, &dst) ||
            !parse_u64(f[3], flen[3], UINT64_MAX, &mult) ||
            !parse_hex(f[2], flen[2], in->pool + used)) {
            return usage("arc fields: decimal source, target, multiplicity; hex label");
        }
        in->arcs[i].source = (uint32_t)src;
        in->arcs[i].target = (uint32_t)dst;
        in->arcs[i].label = in->pool + used;
        in->arcs[i].label_length = flen[2] / 2;
        in->arcs[i].multiplicity = mult;
        used += flen[2] / 2;
        p += len + 1;
    }
    in->arc_count = arc_slots;
    return 0;
}

static const char *status_name(canon_status st)
{
    switch (st) { /* spec 3.2 status names, as in refs/compare/FORMAT.md */
    case CANON_COMPLETE:
        return "COMPLETE";
    case CANON_CANCELLED:
        return "CANCELLED";
    case CANON_CAPACITY_LIMIT:
        return "CAPACITY_LIMIT";
    case CANON_RESOURCE_LIMIT:
        return "RESOURCE_LIMIT";
    case CANON_INVALID_INPUT:
        return "INVALID_INPUT";
    case CANON_UNSUPPORTED_ACTION:
        return "UNSUPPORTED_ACTION";
    case CANON_OUTPUT_ERROR:
        return "OUTPUT_ERROR";
    case CANON_INTERNAL_ERROR:
        return "INTERNAL_ERROR";
    }
    return "INTERNAL_ERROR";
}

static void put_hex(const uint8_t *bytes, size_t len)
{
    static const char digits[] = "0123456789abcdef";
    for (size_t i = 0; i < len; ++i) {
        putchar(digits[bytes[i] >> 4]);
        putchar(digits[bytes[i] & 15u]);
    }
}

/* canon_sink_fn printing hex: the canonical bytes reach stdout through canon_result_encode. */
static int hex_sink(void *user, const uint8_t *chunk, size_t length, size_t *accepted)
{
    (void)user;
    put_hex(chunk, length);
    *accepted = length;
    return 0;
}

/* Print a record with the given status and no evidence (non-COMPLETE outcomes). */
static int print_failure(const char *id, canon_status st)
{
    printf("%s\t0001\t%s\t\t\t-\n", id, status_name(st));
    return st == CANON_COMPLETE ? 0 : 3;
}

int main(int argc, char **argv)
{
    if (argc < 2 || (strcmp(argv[1], "p1-subset") != 0 && strcmp(argv[1], "p1-graph") != 0)) {
        return usage("expected the subcommand p1-subset or p1-graph");
    }
    const bool graph = strcmp(argv[1], "p1-graph") == 0;
    const char *n_arg = NULL, *gens = "", *atoms_arg = "", *id = argv[1];
    const char *colours_arg = "", *arcs_arg = "";
    uint64_t max_nodes = 0;
    canon_backend backend = CANON_BACKEND_CHAIN;
    for (int i = 2; i < argc; i += 2) {
        if (i + 1 >= argc) {
            return usage("option without a value");
        }
        const char *opt = argv[i], *val = argv[i + 1];
        if (strcmp(opt, "--n") == 0) {
            n_arg = val;
        } else if (strcmp(opt, "--gens") == 0) {
            gens = val;
        } else if (!graph && strcmp(opt, "--atoms") == 0) {
            atoms_arg = val;
        } else if (graph && strcmp(opt, "--colours") == 0) {
            colours_arg = val;
        } else if (graph && strcmp(opt, "--arcs") == 0) {
            arcs_arg = val;
        } else if (strcmp(opt, "--max-nodes") == 0) {
            if (!parse_u64(val, strlen(val), UINT64_MAX, &max_nodes)) {
                return usage("--max-nodes expects an unsigned decimal");
            }
        } else if (strcmp(opt, "--backend") == 0) {
            if (strcmp(val, "chain") == 0) {
                backend = CANON_BACKEND_CHAIN;
            } else if (strcmp(val, "explicit") == 0) {
                backend = CANON_BACKEND_EXPLICIT;
            } else {
                return usage("--backend expects chain or explicit");
            }
        } else if (strcmp(opt, "--id") == 0) {
            id = val;
            /* refs/compare/FORMAT.md: one record per line, TAB-separated, and lines beginning
             * with '#' are comments, so an id must be nonempty, must not start with '#' and
             * must not contain TAB, CR or LF. */
            if (*id == '\0' || *id == '#' || strpbrk(id, "\t\n\r") != NULL) {
                return usage("--id must be nonempty, not start with '#', and have no tabs or "
                             "newlines");
            }
        } else {
            return usage("unknown option");
        }
    }
    uint64_t n64 = 0;
    if (n_arg == NULL || !parse_u64(n_arg, strlen(n_arg), UINT32_MAX, &n64)) {
        return usage("--n N is required (unsigned 32-bit decimal)");
    }
    const uint32_t n = (uint32_t)n64;

    /* Generators: "" = none; otherwise ';'-separated lists of exactly n images. */
    size_t gen_count = *gens == '\0' ? 0 : count_char(gens, ';') + 1;
    size_t gen_words = 0;
    if (n > 0 && gen_count > SIZE_MAX / n / sizeof(uint32_t)) {
        return usage("too many generators");
    }
    gen_words = gen_count * (size_t)n;
    uint32_t *gen = malloc(gen_words > 0 ? gen_words * sizeof *gen : 1);
    size_t atom_cap = count_char(atoms_arg, ',') + 1;
    uint32_t *atoms = malloc(atom_cap * sizeof *atoms);
    if (gen == NULL || atoms == NULL) {
        free(gen);
        free(atoms);
        fprintf(stderr, "canon-cli: out of memory\n");
        return print_failure(id, CANON_RESOURCE_LIMIT);
    }
    const char *p = gens;
    for (size_t g = 0; g < gen_count; ++g) {
        const char *end = strchr(p, ';');
        size_t len = end != NULL ? (size_t)(end - p) : strlen(p);
        size_t got = parse_list(p, len, gen + g * (size_t)n, n);
        if (got != n) {
            free(gen);
            free(atoms);
            return usage("each generator must list exactly N comma-separated images");
        }
        p += len + (end != NULL ? 1 : 0);
    }
    size_t atom_count = parse_list(atoms_arg, strlen(atoms_arg), atoms, atom_cap);
    if (atom_count == SIZE_MAX) {
        free(gen);
        free(atoms);
        return usage("--atoms expects comma-separated unsigned decimals");
    }
    graph_input gin;
    memset(&gin, 0, sizeof gin);
    if (graph) {
        int prc = parse_graph(n, colours_arg, arcs_arg, &gin);
        if (prc != 0) {
            graph_input_free(&gin);
            free(gen);
            free(atoms);
            if (prc == 3) {
                fprintf(stderr, "canon-cli: out of memory\n");
                return print_failure(id, CANON_RESOURCE_LIMIT);
            }
            return prc;
        }
    }

    canon_context *ctx = NULL;
    canon_group *group = NULL;
    canon_object *object = NULL;
    canon_problem *problem = NULL;
    canon_workspace *ws = NULL;
    canon_result *result = NULL;
    canon_capacity cap = {0, 0, max_nodes, 0};
    canon_status st = canon_context_create(NULL, &ctx);
    if (st == CANON_COMPLETE) {
        st = canon_context_set_group_backend(ctx, backend);
    }
    if (st == CANON_COMPLETE) {
        st = canon_group_create(ctx, n, gen, gen_count, &group);
    }
    if (st == CANON_COMPLETE && graph) {
        st = canon_object_create_graph(ctx, n, gin.colours,
                                       gin.colour_lengths, gin.arcs, gin.arc_count, &object);
    } else if (st == CANON_COMPLETE) {
        st = canon_object_create_subset(ctx, n, atoms, atom_count, &object);
    }
    if (st == CANON_COMPLETE) {
        st = canon_problem_create(ctx, group, object, CANON_OBJECTIVE_CANONICAL_IMAGE,
                                  CANON_PROFILE_P1, CANON_ENCODING_CDAG_2, CANON_ORDER_CDAG_BYTE_1,
                                  &cap, &problem);
    }
    if (st == CANON_COMPLETE) {
        st = canon_workspace_create(ctx, &ws);
    }
    if (st == CANON_COMPLETE) {
        st = canon_solve(ws, problem, &result);
    }
    int rc = 0;
    if (st != CANON_COMPLETE) {
        rc = print_failure(id, st);
    } else {
        size_t trace_len = 0;
        const uint8_t *trace = canon_result_trace(result, &trace_len);
        printf("%s\t0001\t%s\t", id, status_name(canon_result_status(result)));
        put_hex(trace, trace_len);
        putchar('\t');
        canon_status enc = canon_result_encode(result, hex_sink, NULL);
        putchar('\t');
        uint32_t degree = 0;
        const uint32_t *w = canon_result_witness(result, &degree);
        if (w == NULL || degree == 0) {
            putchar('-'); /* FORMAT.md: n = 0 witness is written '-' */
        } else {
            for (uint32_t v = 0; v < degree; ++v) {
                printf(v == 0 ? "%lu" : ",%lu", (unsigned long)w[v]);
            }
        }
        putchar('\n');
        rc = enc == CANON_COMPLETE ? 0 : 3;
    }
    canon_result_release(result);
    canon_workspace_release(ws);
    canon_problem_release(problem);
    canon_object_release(object);
    canon_group_release(group);
    canon_context_release(ctx);
    graph_input_free(&gin);
    free(gen);
    free(atoms);
    if (fflush(stdout) != 0 || ferror(stdout)) {
        return 3;
    }
    return rc;
}
