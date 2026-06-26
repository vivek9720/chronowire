#!/bin/bash -eu

ROOT="${SRC:-$(cd "$(dirname "$0")/.." && pwd)}"
OUT_DIR="${OUT:?OUT must be set by ClusterFuzzLite}"
CXX_BIN="${CXX:-clang++}"
FUZZ_ENGINE="${LIB_FUZZING_ENGINE:-}"

COMMON_FLAGS=(
  -std=c++17
  -I"${ROOT}/include"
  -Wall
  -Wextra
  -Wpedantic
)

SOURCES=(
  "${ROOT}/src/analyzer.cc"
  "${ROOT}/src/archive.cc"
  "${ROOT}/src/checksum.cc"
  "${ROOT}/src/config.cc"
  "${ROOT}/src/journal.cc"
  "${ROOT}/src/reader.cc"
  "${ROOT}/src/result.cc"
  "${ROOT}/src/schema.cc"
  "${ROOT}/src/stream.cc"
)

mkdir -p "${OUT_DIR}"

"${CXX_BIN}" ${CXXFLAGS:-} "${COMMON_FLAGS[@]}" "${SOURCES[@]}" \
  "${ROOT}/fuzz/chronowire_bundle_fuzzer.cc" ${FUZZ_ENGINE} \
  -o "${OUT_DIR}/chronowire_bundle_fuzzer"

"${CXX_BIN}" ${CXXFLAGS:-} "${COMMON_FLAGS[@]}" "${SOURCES[@]}" \
  "${ROOT}/fuzz/chronowire_config_fuzzer.cc" ${FUZZ_ENGINE} \
  -o "${OUT_DIR}/chronowire_config_fuzzer"
