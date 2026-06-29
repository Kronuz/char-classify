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

  # A runnable tour of the library (not a test): build & run ./build/char_classify_demo
  add_executable(char_classify_demo examples/demo.cc)
  target_link_libraries(char_classify_demo PRIVATE char_classify)
endif()
