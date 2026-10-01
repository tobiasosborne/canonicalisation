cmake_minimum_required(VERSION 3.16)
project(canon VERSION 0.0.1 LANGUAGES C)

option(CANON_SANITIZE "Build with -fsanitize=address,undefined" OFF)
option(CANON_LTO "Enable link-time optimisation" OFF)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

# Strict warnings, applied to every target through one interface library.
add_library(canon_options INTERFACE)
target_compile_options(canon_options INTERFACE
  -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Wvla -Werror)
if(CANON_SANITIZE)
  target_compile_options(canon_options INTERFACE -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(canon_options INTERFACE -fsanitize=address,undefined)
endif()
if(CANON_LTO)
  include(CheckIPOSupported)
  check_ipo_supported(RESULT canon_ipo_ok OUTPUT canon_ipo_msg LANGUAGES C)
  if(canon_ipo_ok)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
  else()
    message(WARNING "CANON_LTO requested but unsupported: ${canon_ipo_msg}")
  endif()
endif()

# The library.  No external dependencies.  Keep this list in step with LIB_SRC in the Makefile.
add_library(canon STATIC
  src/api/version.c src/api/stubs.c src/api/api.c
  src/perm/perm.c src/encoding/wire.c src/encoding/subset_stream.c
  src/encoding/graph_stream.c src/encoding/simple_upper.c src/encoding/group_stream.c
  src/encoding/cdag_encode.c src/encoding/cdag_decode.c
  src/object/subset.c src/object/graph.c src/object/dag.c src/object/object.c
  src/bsgs/group.c src/bsgs/explicit.c src/bsgs/provenance.c src/bsgs/chain.c
  src/bsgs/verify.c src/bsgs/chain_backend.c src/bsgs/reference.c
  src/coset/least.c src/coset/enumerate.c src/partition/partition.c
  src/util/sort.c src/refine/p1.c src/search/p1_tree.c src/search/objectives.c)
target_include_directories(canon PUBLIC include PRIVATE src)
target_link_libraries(canon PRIVATE canon_options)

# Command-line driver (refs/compare/FORMAT.md records); public header only.
add_executable(canon-cli tools/canon-cli.c)
target_link_libraries(canon-cli PRIVATE canon canon_options)

# Independent certificate checker: must NOT link canon and must not see src/ or include/.
add_executable(canon-check checker/main.c)
target_link_libraries(canon-check PRIVATE canon_options)

enable_testing()
add_executable(test_version tests/c/test_version.c)
target_link_libraries(test_version PRIVATE canon canon_options)
add_test(NAME test_version COMMAND test_version)

add_executable(test_header_abi tests/c/test_header_abi.c)
target_include_directories(test_header_abi PRIVATE src)
target_link_libraries(test_header_abi PRIVATE canon canon_options)
add_test(NAME test_header_abi COMMAND test_header_abi)

# Unit and end-to-end C tests; they may include internal headers from src/.
foreach(t test_perm test_sort test_wire test_group_explicit test_partition test_search_subset
          test_nat test_graph test_graph_stream test_signature test_simple_upper
          test_search_graph test_chain test_reference_schreier test_verify test_provenance
          test_coset_least test_enumerate test_group_stream test_objectives
          test_dag test_cdag_decode test_dag_action test_search_dag
          test_signed_group test_labeling test_signed)
  add_executable(${t} tests/c/${t}.c)
  target_include_directories(${t} PRIVATE src)
  target_link_libraries(${t} PRIVATE canon canon_options)
  add_test(NAME ${t} COMMAND ${t})
endforeach()

# Python end-to-end test of canon-cli against checks/review_checks.py and golden.json.
find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
  # Slice S6 review item 8: under the sanitizers the e2e tiers run on seeded subsets
  # (CANON_E2E_FAST=1, tests/python/README.md); without them, in full.
  if(CANON_SANITIZE)
    set(CANON_E2E_ENV "CANON_CLI=$<TARGET_FILE:canon-cli>;CANON_REQUIRE_CLI=1;CANON_E2E_FAST=1")
  else()
    set(CANON_E2E_ENV "CANON_CLI=$<TARGET_FILE:canon-cli>;CANON_REQUIRE_CLI=1")
  endif()
  add_test(NAME test_e2e
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/python/test_e2e.py)
  set_tests_properties(test_e2e PROPERTIES ENVIRONMENT "${CANON_E2E_ENV}")
  # Slice S3: the same end-to-end test with the explicit group backend (the test oracle).
  add_test(NAME test_e2e_explicit
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/python/test_e2e.py)
  set_tests_properties(test_e2e_explicit PROPERTIES
    ENVIRONMENT "${CANON_E2E_ENV};CANON_BACKEND=explicit")
else()
  message(WARNING "python3 not found: test_e2e is not registered")
endif()

# Exit status 2 is the intended M0-M3 behaviour of the checker stub.
add_test(NAME canon_check_stub COMMAND canon-check)
set_tests_properties(canon_check_stub PROPERTIES PASS_REGULAR_EXPRESSION
  "no certificate rules implemented")
