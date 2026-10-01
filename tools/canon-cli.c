/* canon-cli: command-line driver for the canon library (slices S1 to S4,
 * docs/slices/S1.md 4.9, S2.md 3.6, S3.md 3, S4.md 3.6).
 *
 *   canon-cli p1-subset --n N --gens "a0,a1,...;b0,b1,..." --atoms "x,y,z"
 *   canon-cli p1-graph  --n N --gens "..." [--colours "hex;hex;..."] [--arcs "s,t,labelhex,m;..."]
 *       CANONICAL_IMAGE (0001) under profile P1;
 *   canon-cli min [--order cdag|simple-upper] OBJECT
 *       LEX_MIN_IMAGE (0002) under profile NO_TREE, order CDAG-BYTE-1 (default) or
 *       SIMPLE-UPPER-1;
 *   canon-cli transporter OBJECT TARGET        TRANSPORTER_ONE (0003);
 *   canon-cli stabiliser OBJECT                STABILISER (0004);
 *   canon-cli transporter-coset OBJECT TARGET  TRANSPORTER_COSET (0006);
 * where for the S4 subcommands OBJECT is [--kind subset|graph] with --atoms (subset) or
 * --colours/--arcs (graph), and TARGET is --target-atoms (subset) or --target-colours/
 * --target-arcs (graph), each defaulting to the empty subset or the arc-free, uncoloured graph.
 * The kind defaults to graph when any graph option is given, else subset.  Every subcommand
 * also accepts [--max-nodes K] [--id CASE] [--backend chain|explicit] (slice S3: the group
 * backend, default chain; the explicit backend is the test oracle) and
 * [--witness any|deterministic] (slice S4: the spec 3 deterministic witness).
 *
 * Prints one refs/compare/FORMAT.md record of seven fields:
 *   CASE \t OBJECTIVE \t STATUS \t trace_hex \t bytes_hex \t witness \t group_hex
 * trace_hex: the P1 trace (canonical image only); bytes_hex: the canonical image or the
 * minimum image's CDAG-2 stream; witness: comma-separated images of the witness (canonical
 * image, minimum, transporter hit), '-' if none or n = 0; group_hex: Group(A) (stabiliser),
 * Group(A) || Perm(r0) (nonempty transporter coset), the SIMPLE-UPPER-1 key (minimum under
 * that order), '-' otherwise.  `--gens ""` (the default) is the trivial group and `--atoms ""`
 * (the default) the empty subset.  Generators are image arrays p[v] = v^p (spec section 3),
 * separated by ';'; for N = 0 a generator is the empty string.  `--colours ""` (the default)
 * makes every vertex colour empty; otherwise it lists exactly N hex strings separated by ';'
 * (a hex string may be empty).  `--arcs` (default: no arcs) lists arcs
 * "source,target,labelhex,multiplicity" separated by ';' (labelhex may be empty; the
 * multiplicity is passed to the library as given, so 0 yields INVALID_INPUT).
 * `--max-nodes K` sets the spec 11.1 logical work quota (0 = the context default).  Exit
 * status: 0 on COMPLETE, 3 on any other status, 2 on a usage error.  Uses only the public
 * header. */
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
            "       canon-cli p1-graph --n N [--gens \"...\"] [--colours \"hex;hex;...\"]\n"
            "                 [--arcs \"s,t,labelhex,m;...\"]\n"
            "       canon-cli min [--order cdag|simple-upper] OBJECT\n"
            "       canon-cli transporter OBJECT TARGET\n"
            "       canon-cli stabiliser OBJECT\n"
            "       canon-cli transporter-coset OBJECT TARGET\n"
            "       OBJECT: --n N [--gens ...] [--kind subset|graph] [--atoms ...]\n"
            "               [--colours ...] [--arcs ...]\n"
            "       TARGET: [--target-atoms ...] [--target-colours ...] [--target-arcs ...]\n"
            "       all: [--max-nodes K] [--id CASE] [--backend chain|explicit]\n"
            "            [--witness any|deterministic]\n",
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
static int print_failure(const char *id, canon_objective objective, canon_status st)
{
    printf("%s\t%04x\t%s\t\t\t-\t-\n", id, (unsigned)objective, status_name(st));
    return st == CANON_COMPLETE ? 0 : 3;
}

/* The subcommands and the problem each one builds. */
typedef struct subcommand {
    const char *name;
    canon_objective objective;
    canon_profile profile;
    int kind;        /* 0 subset, 1 graph, -1 chosen by --kind or the options given */
    bool target;     /* takes a target object */
} subcommand;

static const subcommand SUBCOMMANDS[] = {
    {"p1-subset", CANON_OBJECTIVE_CANONICAL_IMAGE, CANON_PROFILE_P1, 0, false},
    {"p1-graph", CANON_OBJECTIVE_CANONICAL_IMAGE, CANON_PROFILE_P1, 1, false},
    {"min", CANON_OBJECTIVE_LEX_MIN_IMAGE, CANON_PROFILE_NO_TREE, -1, false},
    {"transporter", CANON_OBJECTIVE_TRANSPORTER_ONE, CANON_PROFILE_NO_TREE, -1, true},
    {"stabiliser", CANON_OBJECTIVE_STABILISER, CANON_PROFILE_NO_TREE, -1, false},
    {"transporter-coset", CANON_OBJECTIVE_TRANSPORTER_COSET, CANON_PROFILE_NO_TREE, -1, true},
};

/* Parsed command line. */
typedef struct options {
    const subcommand *cmd;
    const char *n_arg, *gens, *id;
    const char *atoms, *colours, *arcs;                      /* the object */
    const char *target_atoms, *target_colours, *target_arcs; /* the target */
    bool graph;                                              /* object kind */
    uint64_t max_nodes;
    canon_backend backend;
    canon_order order;
    canon_witness_mode witness;
} options;

/* Parse argv into *o; returns 0 or a usage exit status (message printed). */
static int parse_options(int argc, char **argv, options *o)
{
    memset(o, 0, sizeof *o);
    if (argc >= 2) {
        for (size_t i = 0; i < sizeof SUBCOMMANDS / sizeof SUBCOMMANDS[0]; ++i) {
            if (strcmp(argv[1], SUBCOMMANDS[i].name) == 0) {
                o->cmd = &SUBCOMMANDS[i];
            }
        }
    }
    if (o->cmd == NULL) {
        return usage("expected a subcommand: p1-subset, p1-graph, min, transporter, "
                     "stabiliser or transporter-coset");
    }
    const bool s4 = o->cmd->kind < 0;
    o->gens = o->atoms = o->colours = o->arcs = "";
    o->target_atoms = o->target_colours = o->target_arcs = "";
    o->id = argv[1];
    o->backend = CANON_BACKEND_CHAIN;
    o->order = CANON_ORDER_CDAG_BYTE_1;
    o->witness = CANON_WITNESS_ANY;
    int kind = o->cmd->kind; /* -1 until --kind or a kind-specific option decides */
    bool subset_opt = false, graph_opt = false;
    for (int i = 2; i < argc; i += 2) {
        if (i + 1 >= argc) {
            return usage("option without a value");
        }
        const char *opt = argv[i], *val = argv[i + 1];
        const bool takes_subset = o->cmd->kind != 1, takes_graph = o->cmd->kind != 0;
        if (strcmp(opt, "--n") == 0) {
            o->n_arg = val;
        } else if (strcmp(opt, "--gens") == 0) {
            o->gens = val;
        } else if (takes_subset && strcmp(opt, "--atoms") == 0) {
            o->atoms = val;
            subset_opt = true;
        } else if (takes_graph && strcmp(opt, "--colours") == 0) {
            o->colours = val;
            graph_opt = true;
        } else if (takes_graph && strcmp(opt, "--arcs") == 0) {
            o->arcs = val;
            graph_opt = true;
        } else if (o->cmd->target && strcmp(opt, "--target-atoms") == 0) {
            o->target_atoms = val;
            subset_opt = true;
        } else if (o->cmd->target && strcmp(opt, "--target-colours") == 0) {
            o->target_colours = val;
            graph_opt = true;
        } else if (o->cmd->target && strcmp(opt, "--target-arcs") == 0) {
            o->target_arcs = val;
            graph_opt = true;
        } else if (s4 && strcmp(opt, "--kind") == 0) {
            if (strcmp(val, "subset") != 0 && strcmp(val, "graph") != 0) {
                return usage("--kind expects subset or graph");
            }
            kind = strcmp(val, "graph") == 0;
        } else if (o->cmd->objective == CANON_OBJECTIVE_LEX_MIN_IMAGE &&
                   strcmp(opt, "--order") == 0) {
            if (strcmp(val, "cdag") == 0) {
                o->order = CANON_ORDER_CDAG_BYTE_1;
            } else if (strcmp(val, "simple-upper") == 0) {
                o->order = CANON_ORDER_SIMPLE_UPPER_1;
            } else {
                return usage("--order expects cdag or simple-upper");
            }
        } else if (strcmp(opt, "--witness") == 0) {
            if (strcmp(val, "any") == 0) {
                o->witness = CANON_WITNESS_ANY;
            } else if (strcmp(val, "deterministic") == 0) {
                o->witness = CANON_WITNESS_DETERMINISTIC;
            } else {
                return usage("--witness expects any or deterministic");
            }
        } else if (strcmp(opt, "--max-nodes") == 0) {
            if (!parse_u64(val, strlen(val), UINT64_MAX, &o->max_nodes)) {
                return usage("--max-nodes expects an unsigned decimal");
            }
        } else if (strcmp(opt, "--backend") == 0) {
            if (strcmp(val, "chain") == 0) {
                o->backend = CANON_BACKEND_CHAIN;
            } else if (strcmp(val, "explicit") == 0) {
                o->backend = CANON_BACKEND_EXPLICIT;
            } else {
                return usage("--backend expects chain or explicit");
            }
        } else if (strcmp(opt, "--id") == 0) {
            o->id = val;
            /* refs/compare/FORMAT.md: one record per line, TAB-separated, and lines beginning
             * with '#' are comments, so an id must be nonempty, must not start with '#' and
             * must not contain TAB, CR or LF. */
            if (*val == '\0' || *val == '#' || strpbrk(val, "\t\n\r") != NULL) {
                return usage("--id must be nonempty, not start with '#', and have no tabs or "
                             "newlines");
            }
        } else {
            return usage("unknown option");
        }
    }
    if (kind < 0) {
        kind = graph_opt ? 1 : 0;
    }
    if ((kind == 1 && subset_opt) || (kind == 0 && graph_opt)) {
        return usage("subset options (--atoms, --target-atoms) and graph options (--colours, "
                     "--arcs, --target-colours, --target-arcs) do not mix");
    }
    o->graph = kind == 1;
    return 0;
}

/* Build a subset or graph object of degree n from the CLI strings.  Returns 0 with *st the
 * library status (and *out on success), 2 on a usage error, 3 on allocation failure. */
static int build_object(canon_context *ctx, uint32_t n, bool graph, const char *atoms_arg,
                        const char *colours_arg, const char *arcs_arg, canon_status *st,
                        canon_object **out)
{
    *out = NULL;
    if (graph) {
        graph_input gin;
        int prc = parse_graph(n, colours_arg, arcs_arg, &gin);
        if (prc == 0) {
            *st = canon_object_create_graph(ctx, n, gin.colours, gin.colour_lengths, gin.arcs,
                                            gin.arc_count, out);
        }
        graph_input_free(&gin);
        return prc;
    }
    size_t atom_cap = count_char(atoms_arg, ',') + 1;
    uint32_t *atoms = malloc(atom_cap * sizeof *atoms);
    if (atoms == NULL) {
        return 3;
    }
    size_t atom_count = parse_list(atoms_arg, strlen(atoms_arg), atoms, atom_cap);
    if (atom_count == SIZE_MAX) {
        free(atoms);
        return usage("--atoms expects comma-separated unsigned decimals");
    }
    *st = canon_object_create_subset(ctx, n, atoms, atom_count, out);
    free(atoms);
    return 0;
}

/* Print the seven-field record of a COMPLETE result; returns the exit status. */
static int print_result(const char *id, canon_objective objective, const canon_result *result)
{
    size_t trace_len = 0;
    const uint8_t *trace = canon_result_trace(result, &trace_len);
    printf("%s\t%04x\t%s\t", id, (unsigned)objective, status_name(canon_result_status(result)));
    put_hex(trace, trace_len);
    putchar('\t');
    canon_status enc = CANON_COMPLETE;
    size_t bytes_len = 0;
    if (canon_result_bytes(result, &bytes_len) != NULL) {
        enc = canon_result_encode(result, hex_sink, NULL);
    }
    putchar('\t');
    uint32_t degree = 0;
    const uint32_t *w = canon_result_witness(result, &degree);
    if (w == NULL || degree == 0) {
        putchar('-'); /* FORMAT.md: no witness, or the n = 0 witness, is written '-' */
    } else {
        for (uint32_t v = 0; v < degree; ++v) {
            printf(v == 0 ? "%lu" : ",%lu", (unsigned long)w[v]);
        }
    }
    putchar('\t');
    size_t group_len = 0, key_len = 0;
    const uint8_t *group = canon_result_group_bytes(result, &group_len);
    const uint8_t *key = canon_result_order_key(result, &key_len);
    if (group != NULL) {
        put_hex(group, group_len); /* Group(A) or Group(A) || Perm(r0) (spec 9.4) */
    } else if (key != NULL) {
        put_hex(key, key_len); /* the SIMPLE-UPPER-1 key of the minimum (spec 4.4) */
    } else {
        putchar('-');
    }
    putchar('\n');
    return enc == CANON_COMPLETE ? 0 : 3;
}

int main(int argc, char **argv)
{
    options o;
    int prc = parse_options(argc, argv, &o);
    if (prc != 0) {
        return prc;
    }
    const canon_objective objective = o.cmd->objective;
    uint64_t n64 = 0;
    if (o.n_arg == NULL || !parse_u64(o.n_arg, strlen(o.n_arg), UINT32_MAX, &n64)) {
        return usage("--n N is required (unsigned 32-bit decimal)");
    }
    const uint32_t n = (uint32_t)n64;

    /* Generators: "" = none; otherwise ';'-separated lists of exactly n images. */
    size_t gen_count = *o.gens == '\0' ? 0 : count_char(o.gens, ';') + 1;
    if (n > 0 && gen_count > SIZE_MAX / n / sizeof(uint32_t)) {
        return usage("too many generators");
    }
    const size_t gen_words = gen_count * (size_t)n;
    uint32_t *gen = malloc(gen_words > 0 ? gen_words * sizeof *gen : 1);
    if (gen == NULL) {
        fprintf(stderr, "canon-cli: out of memory\n");
        return print_failure(o.id, objective, CANON_RESOURCE_LIMIT);
    }
    const char *p = o.gens;
    for (size_t g = 0; g < gen_count; ++g) {
        const char *end = strchr(p, ';');
        size_t len = end != NULL ? (size_t)(end - p) : strlen(p);
        size_t got = parse_list(p, len, gen + g * (size_t)n, n);
        if (got != n) {
            free(gen);
            return usage("each generator must list exactly N comma-separated images");
        }
        p += len + (end != NULL ? 1 : 0);
    }

    canon_context *ctx = NULL;
    canon_group *group = NULL;
    canon_object *object = NULL, *target = NULL;
    canon_problem *problem = NULL;
    canon_workspace *ws = NULL;
    canon_result *result = NULL;
    canon_capacity cap = {0, 0, o.max_nodes, 0, 0, 0, 0};
    const canon_context_options copts = {o.backend};
    const canon_problem_options popts = {o.witness};
    canon_status st = canon_context_create_with_options(NULL, &copts, &ctx);
    if (st == CANON_COMPLETE) {
        st = canon_group_create(ctx, n, gen, gen_count, &group);
    }
    int rc = 0;
    if (st == CANON_COMPLETE) {
        rc = build_object(ctx, n, o.graph, o.atoms, o.colours, o.arcs, &st, &object);
    }
    if (rc == 0 && st == CANON_COMPLETE && o.cmd->target) {
        rc = build_object(ctx, n, o.graph, o.target_atoms, o.target_colours, o.target_arcs, &st,
                          &target);
    }
    if (rc == 0 && st == CANON_COMPLETE) {
        st = canon_problem_create_with_options(ctx, group, object, target, objective,
                                               o.cmd->profile, CANON_ENCODING_CDAG_2, o.order,
                                               &cap, &popts, &problem);
    }
    if (rc == 0 && st == CANON_COMPLETE) {
        st = canon_workspace_create(ctx, &ws);
    }
    if (rc == 0 && st == CANON_COMPLETE) {
        st = canon_solve(ws, problem, &result);
    }
    if (rc == 3) {
        fprintf(stderr, "canon-cli: out of memory\n");
        rc = print_failure(o.id, objective, CANON_RESOURCE_LIMIT);
    } else if (rc == 0) {
        rc = st != CANON_COMPLETE ? print_failure(o.id, objective, st)
                                  : print_result(o.id, objective, result);
    }
    canon_result_release(result);
    canon_workspace_release(ws);
    canon_problem_release(problem);
    canon_object_release(object);
    canon_object_release(target);
    canon_group_release(group);
    canon_context_release(ctx);
    free(gen);
    if (fflush(stdout) != 0 || ferror(stdout)) {
        return 3;
    }
    return rc;
}
