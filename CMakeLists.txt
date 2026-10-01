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

# The library.  No external dependencies.
add_library(canon STATIC src/api/version.c src/api/stubs.c)
target_include_directories(canon PUBLIC include)
target_link_libraries(canon PRIVATE canon_options)

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

# Exit status 2 is the intended M0-M3 behaviour of the checker stub.
add_test(NAME canon_check_stub COMMAND canon-check)
set_tests_properties(canon_check_stub PROPERTIES PASS_REGULAR_EXPRESSION
  "no certificate rules implemented")
