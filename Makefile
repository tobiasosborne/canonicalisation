# Plain-make build of the canon library; works with gcc alone (no cmake needed).
# CMakeLists.txt is a parallel definition of the same targets.
#   make            build library, checker, canon-cli and C tests
#   make test       run the C tests
#   make check      review_checks.py plus the Python tests (vectors, hexdump, CLI end to end
#                   with the chain backend, then again with the explicit backend) and the
#                   checker harness (certificates through the CLI, canon-check, mutations)
#   make format     clang-format (no-op with a message if unavailable)
#   make clean
# Options: CC=clang  SANITIZE=1 (address,undefined)  LTO=1  BUILD=dir

ifeq ($(origin CC),default)
CC       = gcc
endif
BUILD    ?= build/make
WARN      = -std=c17 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Wvla -Werror
OPT      ?= -O2 -g
CFLAGS   += $(WARN) $(OPT)
ifeq ($(SANITIZE),1)
CFLAGS   += -fsanitize=address,undefined -fno-omit-frame-pointer
LDFLAGS  += -fsanitize=address,undefined
endif
ifeq ($(LTO),1)
CFLAGS   += -flto
LDFLAGS  += -flto
endif

LIB_SRC  = src/api/version.c src/api/stubs.c src/api/api.c \
           src/perm/perm.c src/encoding/wire.c src/encoding/subset_stream.c \
           src/encoding/graph_stream.c src/encoding/simple_upper.c src/encoding/group_stream.c \
           src/encoding/cdag_encode.c src/encoding/cdag_decode.c \
           src/object/subset.c src/object/graph.c src/object/dag.c src/object/object.c \
           src/bsgs/group.c src/bsgs/explicit.c src/bsgs/provenance.c src/bsgs/chain.c \
           src/bsgs/verify.c src/bsgs/chain_backend.c src/bsgs/reference.c \
           src/coset/least.c src/coset/enumerate.c src/partition/partition.c \
           src/util/sort.c \
           src/refine/p1.c src/search/p1_tree.c src/search/objectives.c \
           src/search/certificate.c src/symmetry/symmetry.c src/arena/mem.c
LIB_OBJ  = $(LIB_SRC:%.c=$(BUILD)/%.o)
LIB      = $(BUILD)/libcanon.a
# Unit tests that may include internal headers from src/ (tests/c/README.md).
UNIT_TESTS = test_perm test_sort test_wire test_group_explicit test_partition test_search_subset \
             test_nat test_graph test_graph_stream test_signature test_simple_upper \
             test_search_graph test_chain test_reference_schreier test_verify test_provenance \
             test_coset_least test_enumerate test_group_stream test_objectives \
             test_dag test_cdag_decode test_dag_action test_search_dag \
             test_signed_group test_labeling test_signed \
             test_symmetry test_prune test_certificate \
             test_sink test_batch test_metrics test_faults
TESTS    = $(BUILD)/test_version $(BUILD)/test_header_abi $(UNIT_TESTS:%=$(BUILD)/%)
CHECKER  = $(BUILD)/canon-check
CLI      = $(BUILD)/canon-cli
FORMAT_FILES = $(shell find include src checker tests/c tools -name '*.c' -o -name '*.h' 2>/dev/null)

# Slice S8 fuzz targets: libFuzzer entry points with a deterministic replay driver (fuzz/run.sh);
# the checker target compiles the checker's own sources and nothing from src/.
FUZZ     = $(BUILD)/fuzz_cdag_decode $(BUILD)/fuzz_checker
FUZZ_ITER ?= 2000

.PHONY: all test check format clean fuzz
all: $(LIB) $(CHECKER) $(CLI) $(TESTS) $(FUZZ)

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -Iinclude -Isrc -MMD -MP -c $< -o $@

$(LIB): $(LIB_OBJ)
	ar rcs $@ $^

# The checker is deliberately independent: no libcanon, no include paths into src/ or include/.
CHECKER_SRC = checker/main.c checker/cc_check.c checker/cc_chain.c checker/cc_object.c \
              checker/cc_reader.c checker/cc_refine.c
$(CHECKER): $(CHECKER_SRC) checker/cc.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(CHECKER_SRC) -o $@ $(LDFLAGS)

# The CLI uses only the public header (no -Isrc).
$(CLI): tools/canon-cli.c $(LIB)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -Iinclude $< $(LIB) -o $@ $(LDFLAGS)

$(BUILD)/test_version: tests/c/test_version.c $(LIB)
	$(CC) $(CFLAGS) -Iinclude $< $(LIB) -o $@ $(LDFLAGS)

$(BUILD)/test_header_abi: tests/c/test_header_abi.c $(LIB)
	$(CC) $(CFLAGS) -Iinclude -Isrc $< $(LIB) -o $@ $(LDFLAGS)

$(UNIT_TESTS:%=$(BUILD)/%): $(BUILD)/%: tests/c/%.c tests/c/check.h tests/c/t1_groups.h $(LIB)
	$(CC) $(CFLAGS) -Iinclude -Isrc $< $(LIB) -o $@ $(LDFLAGS)

$(BUILD)/fuzz_cdag_decode: fuzz/fuzz_cdag_decode.c fuzz/fuzz_driver.h $(LIB)
	$(CC) $(CFLAGS) -DCANON_FUZZ_MAIN -Iinclude $< $(LIB) -o $@ $(LDFLAGS)

$(BUILD)/fuzz_checker: fuzz/fuzz_checker.c fuzz/fuzz_driver.h $(filter-out checker/main.c,$(CHECKER_SRC)) checker/cc.h
	$(CC) $(CFLAGS) -DCANON_FUZZ_MAIN $< $(filter-out checker/main.c,$(CHECKER_SRC)) -o $@ $(LDFLAGS)

test: all
	@set -e; for t in $(TESTS); do echo "run $$t"; $$t; done
	nice -n 19 sh fuzz/run.sh $(BUILD) $(FUZZ_ITER)

fuzz: $(FUZZ) $(CLI)
	nice -n 19 sh fuzz/run.sh $(BUILD) $(FUZZ_ITER)

# make check builds the CLI first: tests/python/test_e2e.py drives it through CANON_CLI.
# The discover run uses the default (chain) group backend; the second run repeats the CLI end
# to end test with the explicit backend (slice S3: both must agree with the model).
# Slice S7: the checker harness (checker/tests/test_checker.py) produces certificates through
# the CLI and runs canon-check on them, including the mutation tests.
check: $(CLI) $(CHECKER)
	nice -n 19 python3 checks/review_checks.py
	CANON_CLI=$(abspath $(CLI)) CANON_CHECK=$(abspath $(CHECKER)) CANON_REQUIRE_CLI=1 nice -n 19 python3 -m unittest discover -s tests/python -q
	CANON_CLI=$(abspath $(CLI)) CANON_CHECK=$(abspath $(CHECKER)) CANON_REQUIRE_CLI=1 CANON_BACKEND=explicit nice -n 19 python3 tests/python/test_e2e.py
	CANON_CLI=$(abspath $(CLI)) CANON_CHECK=$(abspath $(CHECKER)) CANON_REQUIRE_CLI=1 nice -n 19 python3 checker/tests/test_checker.py

format:
	@if command -v clang-format >/dev/null 2>&1; then \
	  clang-format -i $(FORMAT_FILES); \
	else echo "clang-format not found: format is a no-op"; fi

clean:
	rm -rf $(BUILD)

-include $(LIB_OBJ:.o=.d)
