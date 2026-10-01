# Plain-make build of the canon library; works with gcc alone (no cmake needed).
# CMakeLists.txt is a parallel definition of the same targets.
#   make            build library, checker and C tests
#   make test       run the C tests
#   make check      review_checks.py plus the Python vector/hexdump tests
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

LIB_SRC  = src/api/version.c src/api/stubs.c \
           src/perm/perm.c src/encoding/wire.c src/encoding/subset_stream.c \
           src/object/subset.c src/bsgs/explicit.c src/partition/partition.c
LIB_OBJ  = $(LIB_SRC:%.c=$(BUILD)/%.o)
LIB      = $(BUILD)/libcanon.a
# Unit tests that may include internal headers from src/ (tests/c/README.md).
UNIT_TESTS = test_perm test_wire test_group_explicit test_partition
TESTS    = $(BUILD)/test_version $(BUILD)/test_header_abi $(UNIT_TESTS:%=$(BUILD)/%)
CHECKER  = $(BUILD)/canon-check
FORMAT_FILES = $(shell find include src checker tests/c -name '*.c' -o -name '*.h' 2>/dev/null)

.PHONY: all test check format clean
all: $(LIB) $(CHECKER) $(TESTS)

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -Iinclude -Isrc -MMD -MP -c $< -o $@

$(LIB): $(LIB_OBJ)
	ar rcs $@ $^

# The checker is deliberately independent: no libcanon, no include paths into src/ or include/.
$(CHECKER): checker/main.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

$(BUILD)/test_version: tests/c/test_version.c $(LIB)
	$(CC) $(CFLAGS) -Iinclude $< $(LIB) -o $@ $(LDFLAGS)

$(BUILD)/test_header_abi: tests/c/test_header_abi.c $(LIB)
	$(CC) $(CFLAGS) -Iinclude -Isrc $< $(LIB) -o $@ $(LDFLAGS)

$(UNIT_TESTS:%=$(BUILD)/%): $(BUILD)/%: tests/c/%.c tests/c/check.h $(LIB)
	$(CC) $(CFLAGS) -Iinclude -Isrc $< $(LIB) -o $@ $(LDFLAGS)

test: all
	@set -e; for t in $(TESTS); do echo "run $$t"; $$t; done

check:
	nice -n 19 python3 checks/review_checks.py
	nice -n 19 python3 -m unittest discover -s tests/python -q

format:
	@if command -v clang-format >/dev/null 2>&1; then \
	  clang-format -i $(FORMAT_FILES); \
	else echo "clang-format not found: format is a no-op"; fi

clean:
	rm -rf $(BUILD)

-include $(LIB_OBJ:.o=.d)
