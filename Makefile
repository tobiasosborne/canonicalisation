# Plain-make build of the canon library; works with gcc alone (no cmake needed).
# CMakeLists.txt is a parallel definition of the same targets.
#   make            build library, checker, canon-cli and C tests
#   make test       run the C tests
#   make check      review_checks.py plus the Python tests (vectors, hexdump, CLI end to end)
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
           src/encoding/graph_stream.c src/encoding/simple_upper.c \
           src/object/subset.c src/object/graph.c src/object/object.c \
           src/bsgs/group.c src/bsgs/explicit.c src/partition/partition.c \
           src/util/sort.c \
           src/refine/p1.c src/search/p1_tree.c
LIB_OBJ  = $(LIB_SRC:%.c=$(BUILD)/%.o)
LIB      = $(BUILD)/libcanon.a
# Unit tests that may include internal headers from src/ (tests/c/README.md).
UNIT_TESTS = test_perm test_sort test_wire test_group_explicit test_partition test_search_subset \
             test_nat test_graph test_graph_stream test_signature test_simple_upper \
             test_search_graph
TESTS    = $(BUILD)/test_version $(BUILD)/test_header_abi $(UNIT_TESTS:%=$(BUILD)/%)
CHECKER  = $(BUILD)/canon-check
CLI      = $(BUILD)/canon-cli
FORMAT_FILES = $(shell find include src checker tests/c tools -name '*.c' -o -name '*.h' 2>/dev/null)

.PHONY: all test check format clean
all: $(LIB) $(CHECKER) $(CLI) $(TESTS)

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -Iinclude -Isrc -MMD -MP -c $< -o $@

$(LIB): $(LIB_OBJ)
	ar rcs $@ $^

# The checker is deliberately independent: no libcanon, no include paths into src/ or include/.
$(CHECKER): checker/main.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

# The CLI uses only the public header (no -Isrc).
$(CLI): tools/canon-cli.c $(LIB)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -Iinclude $< $(LIB) -o $@ $(LDFLAGS)

$(BUILD)/test_version: tests/c/test_version.c $(LIB)
	$(CC) $(CFLAGS) -Iinclude $< $(LIB) -o $@ $(LDFLAGS)

$(BUILD)/test_header_abi: tests/c/test_header_abi.c $(LIB)
	$(CC) $(CFLAGS) -Iinclude -Isrc $< $(LIB) -o $@ $(LDFLAGS)

$(UNIT_TESTS:%=$(BUILD)/%): $(BUILD)/%: tests/c/%.c tests/c/check.h $(LIB)
	$(CC) $(CFLAGS) -Iinclude -Isrc $< $(LIB) -o $@ $(LDFLAGS)

test: all
	@set -e; for t in $(TESTS); do echo "run $$t"; $$t; done

# make check builds the CLI first: tests/python/test_e2e.py drives it through CANON_CLI.
check: $(CLI)
	nice -n 19 python3 checks/review_checks.py
	CANON_CLI=$(abspath $(CLI)) CANON_REQUIRE_CLI=1 nice -n 19 python3 -m unittest discover -s tests/python -q

format:
	@if command -v clang-format >/dev/null 2>&1; then \
	  clang-format -i $(FORMAT_FILES); \
	else echo "clang-format not found: format is a no-op"; fi

clean:
	rm -rf $(BUILD)

-include $(LIB_OBJ:.o=.d)
