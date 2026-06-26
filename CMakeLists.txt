cmake_minimum_required(VERSION 3.16)

project(chronowire VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(CHRONOWIRE_BUILD_FUZZERS "Build local libFuzzer targets with Clang" OFF)

add_library(chronowire STATIC
  src/analyzer.cc
  src/archive.cc
  src/checksum.cc
  src/config.cc
  src/journal.cc
  src/notes.cc
  src/reader.cc
  src/result.cc
  src/schema.cc
  src/stream.cc
)

target_include_directories(chronowire PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(chronowire PUBLIC cxx_std_17)

if(MSVC)
  target_compile_options(chronowire PRIVATE /W4 /permissive-)
else()
  target_compile_options(chronowire PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(cwj_dump tools/cwj_dump.cc)
target_link_libraries(cwj_dump PRIVATE chronowire)

add_executable(chronowire_tests tests/unit_tests.cc)
target_link_libraries(chronowire_tests PRIVATE chronowire)

enable_testing()
add_test(NAME chronowire_tests COMMAND chronowire_tests)

if(CHRONOWIRE_BUILD_FUZZERS)
  if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "CHRONOWIRE_BUILD_FUZZERS expects Clang or clang-cl")
  endif()

  target_compile_options(chronowire PRIVATE -fsanitize=fuzzer-no-link,address,undefined)

  add_executable(chronowire_bundle_fuzzer fuzz/chronowire_bundle_fuzzer.cc)
  target_link_libraries(chronowire_bundle_fuzzer PRIVATE chronowire)
  target_compile_options(chronowire_bundle_fuzzer PRIVATE -fsanitize=fuzzer,address,undefined)
  target_link_options(chronowire_bundle_fuzzer PRIVATE -fsanitize=fuzzer,address,undefined)

  add_executable(chronowire_config_fuzzer fuzz/chronowire_config_fuzzer.cc)
  target_link_libraries(chronowire_config_fuzzer PRIVATE chronowire)
  target_compile_options(chronowire_config_fuzzer PRIVATE -fsanitize=fuzzer,address,undefined)
  target_link_options(chronowire_config_fuzzer PRIVATE -fsanitize=fuzzer,address,undefined)
endif()
