cmake_minimum_required(VERSION 3.14)
project(char_classify LANGUAGES CXX)

# --- Library target ---
# char-classify is header-only and dependency-free: one constexpr character
# classification header (chars.hh) with nothing but the C++ standard library
# behind it. So the target is a pure INTERFACE library that just puts the source
# dir on the consumer's include path.
add_library(char_classify INTERFACE)
target_include_directories(char_classify INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(char_classify INTERFACE cxx_std_20)
# Alias so consumers can link `char_classify::char_classify` whether they add
# this via add_subdirectory or FetchContent.
add_library(char_classify::char_classify ALIAS char_classify)

# Only build the test when this repo is the top-level project, so consumers
# (e.g. via FetchContent) don't build our tests.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  enable_testing()
  add_executable(char_classify_test test/test.cc)
  target_link_libraries(char_classify_test PRIVATE char_classify)
  add_test(NAME char_classify COMMAND char_classify_test)

  # chars_simd.hh (byte-class scanning, case fold) and chars_escape.hh (the escaping kernel),
  # each against a scalar reference, once with the backend of the target (NEON or SSE2) and
  # once with the portable 64-bit backend forced by CHARS_NO_SIMD.
  foreach(name simd escape_kernel)
    add_executable(chars_${name}_test test/${name}_test.cc)
    target_link_libraries(chars_${name}_test PRIVATE char_classify)
    add_test(NAME chars_${name} COMMAND chars_${name}_test)

    add_executable(chars_${name}_test_swar test/${name}_test.cc)
    target_link_libraries(chars_${name}_test_swar PRIVATE char_classify)
    target_compile_definitions(chars_${name}_test_swar PRIVATE CHARS_NO_SIMD=1)
    add_test(NAME chars_${name}_swar COMMAND chars_${name}_test_swar)
  endforeach()

  # Comparison benchmark (not run by ctest): classification, hex, case folding, scanning and the
  # escaping kernel against arithmetic loops, <cctype>, memchr and, when abseil is installed,
  # abseil. The comparators are optional and never a dependency of the library target.
  # char_classify_compare_swar is the same program with the portable backend forced, for a paired
  # run. With the option on, both also run in `--check` mode under ctest (results, no timing).
  option(CHAR_CLASSIFY_BUILD_BENCH "Build bench/compare.cc" OFF)
  if(CHAR_CLASSIFY_BUILD_BENCH)
    find_package(absl QUIET)
    foreach(variant "" _swar)
      add_executable(char_classify_compare${variant} bench/compare.cc)
      target_link_libraries(char_classify_compare${variant} PRIVATE char_classify)
      if(absl_FOUND)
        target_compile_definitions(char_classify_compare${variant} PRIVATE HAVE_ABSL=1)
        target_link_libraries(char_classify_compare${variant} PRIVATE absl::strings)
      endif()
      add_test(NAME char_classify_compare${variant} COMMAND char_classify_compare${variant} --check)
    endforeach()
    target_compile_definitions(char_classify_compare_swar PRIVATE CHARS_NO_SIMD=1)
  endif()

  # A runnable tour of the library (not a test): build & run ./build/char_classify_demo
  add_executable(char_classify_demo examples/demo.cc)
  target_link_libraries(char_classify_demo PRIVATE char_classify)
endif()
