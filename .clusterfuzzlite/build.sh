#!/bin/bash -eu

ROOT="${SRC:-$(cd "$(dirname "$0")/.." && pwd)}"
OUT_DIR="${OUT:?OUT must be set by ClusterFuzzLite}"
CXX_BIN="${CXX:-clang++}"
FUZZ_ENGINE="${LIB_FUZZING_ENGINE:-}"

CXXFLAGS_ARRAY=()
if [[ -n "${CXXFLAGS:-}" ]]; then
  read -r -a CXXFLAGS_ARRAY <<< "${CXXFLAGS}"
fi

LINK_FLAGS=()
for flag in "${CXXFLAGS_ARRAY[@]}"; do
  if [[ "${flag}" == -fsanitize=* ]]; then
    spec="${flag#-fsanitize=}"
    kept=()
    IFS=',' read -r -a sanitizers <<< "${spec}"
    for sanitizer in "${sanitizers[@]}"; do
      if [[ "${sanitizer}" != "fuzzer-no-link" && "${sanitizer}" != "fuzzer" ]]; then
        kept+=("${sanitizer}")
      fi
    done
    if (( ${#kept[@]} > 0 )); then
      joined="$(IFS=,; echo "${kept[*]}")"
      LINK_FLAGS+=("-fsanitize=${joined}")
    fi
  else
    LINK_FLAGS+=("${flag}")
  fi
done

FUZZ_ENGINE_ARRAY=()
if [[ -n "${FUZZ_ENGINE}" ]]; then
  read -r -a FUZZ_ENGINE_ARRAY <<< "${FUZZ_ENGINE}"
fi

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
  "${ROOT}/src/notes.cc"
  "${ROOT}/src/reader.cc"
  "${ROOT}/src/result.cc"
  "${ROOT}/src/schema.cc"
  "${ROOT}/src/stream.cc"
)

mkdir -p "${OUT_DIR}"
mkdir -p "${OUT_DIR}/obj"

build_fuzzer() {
  local target="$1"
  local fuzzer_source="$2"
  local objects=()
  local source
  local object

  for source in "${SOURCES[@]}" "${fuzzer_source}"; do
    object="${OUT_DIR}/obj/${target}_$(basename "${source%.*}").o"
    "${CXX_BIN}" "${CXXFLAGS_ARRAY[@]}" "${COMMON_FLAGS[@]}" -c "${source}" -o "${object}"
    objects+=("${object}")
  done

  "${CXX_BIN}" "${LINK_FLAGS[@]}" "${objects[@]}" "${FUZZ_ENGINE_ARRAY[@]}" \
    -o "${OUT_DIR}/${target}"
}

build_fuzzer chronowire_bundle_fuzzer "${ROOT}/fuzz/chronowire_bundle_fuzzer.cc"
build_fuzzer chronowire_config_fuzzer "${ROOT}/fuzz/chronowire_config_fuzzer.cc"
