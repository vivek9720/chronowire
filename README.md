# ChronoWire

ChronoWire is a small C++17 library and tool for inspecting offline journal bundles. A bundle can contain metadata, configuration records, record-store journal pages, and fragmented stream frames. The code is intentionally dependency-free and uses deterministic parsing and replay logic suitable for local testing and ClusterFuzzLite fuzzing.

This repository is intended to be reviewed, expanded, and submitted as a private, original Fenrir candidate. Do not publish it as a public repository before submission.

## Project Overview

ChronoWire models a compact data pipeline used by offline services that exchange state snapshots:

- A bundle parser reads a sectioned `CWJB` binary container or a line-oriented `CWJ1-TEXT` envelope used for fixtures and seed corpora.
- A configuration parser supports assignments, nested blocks, quoted strings, escaped characters, include records represented only as data, and arithmetic/string expression trees.
- A journal decoder replays transaction-like record-store operations into a deterministic in-memory state.
- A stream decoder reconstructs fragmented messages while carrying compression flags as metadata only.
- A note index decoder reads offline segment annotations and escaped payload summaries from notes sections.
- A schema profiler cross-checks section inventory, configuration keys, journal keyspaces, transaction records, and stream fragment coverage.
- An analyzer connects those parts and produces a summary from a full bundle.

No component performs network access, reads include paths from disk, prompts interactively, or depends on credentials.

## Layout

```text
chronowire/
  CMakeLists.txt
  README.md
  include/chronowire/
  src/
  tests/
  tools/
  fuzz/
    chronowire_bundle_fuzzer.cc
    chronowire_config_fuzzer.cc
    corpus/
    dictionary.txt
  .clusterfuzzlite/
    build.sh
    project.yaml
```

## Build

From the repository root:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build --config RelWithDebInfo
```

The build uses only local source files and the C++ standard library.

## Tests

```bash
ctest --test-dir build --output-on-failure
```

The tests parse a complete bundle, exercise config expressions and include records, replay journal transactions, and reconstruct stream fragments.

## CLI

```bash
./build/cwj_dump fuzz/corpus/chronowire_bundle_fuzzer/complete_bundle.txt
```

On Windows with a multi-config generator, the executable may be under `build/RelWithDebInfo/`.

## Fuzzing

ClusterFuzzLite uses `.clusterfuzzlite/build.sh` and writes these binaries into `$OUT`:

- `chronowire_bundle_fuzzer`
- `chronowire_config_fuzzer`

Local Clang/libFuzzer example:

```bash
cmake -S . -B build-fuzz -DCHRONOWIRE_BUILD_FUZZERS=ON -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build-fuzz --config RelWithDebInfo
./build-fuzz/chronowire_bundle_fuzzer fuzz/corpus/chronowire_bundle_fuzzer -dict=fuzz/dictionary.txt -runs=1000
./build-fuzz/chronowire_config_fuzzer fuzz/corpus/chronowire_config_fuzzer -dict=fuzz/dictionary.txt -runs=1000
```

ClusterFuzzLite-compatible local build example:

```bash
mkdir -p out
SRC="$PWD" OUT="$PWD/out" CXX=clang++ CXXFLAGS="-O1 -g -fsanitize=fuzzer-no-link,address,undefined" LIB_FUZZING_ENGINE="-fsanitize=fuzzer" ./.clusterfuzzlite/build.sh
./out/chronowire_bundle_fuzzer fuzz/corpus/chronowire_bundle_fuzzer -dict=fuzz/dictionary.txt -runs=1000
```

## Seed Corpus

The recognized seed corpus lives under `fuzz/corpus/`.

- `fuzz/corpus/chronowire_bundle_fuzzer/` contains complete bundle envelopes with config, journal, and stream sections.
- `fuzz/corpus/chronowire_config_fuzzer/` contains standalone configuration/script inputs.

The dictionary includes magic strings, section names, config keywords, delimiters, journal tokens, stream frame tokens, and common metadata keys.

## Fenrir Readiness Checklist

- [x] Private-repo-ready source layout with `.clusterfuzzlite/` at the repository root.
- [x] Connected fuzzing harnesses call real project code.
- [x] Harnesses exercise meaningful parser, replay, and reconstruction paths.
- [x] ClusterFuzzLite build writes all harness executables to `$OUT`.
- [x] Build uses `$SRC` or repository-relative paths only.
- [x] Build is deterministic and non-interactive.
- [x] No network access, credentials, local absolute paths, or runtime prompts.
- [x] Recognized seed corpus location is present.
- [x] Fuzz dictionary is present.
- [x] Tests and sample CLI are included.

## Manual Review Before Submission

- Confirm the final GitHub repository is private and not forked from any public repository.
- Review every file for originality and remove or rewrite anything that does not reflect your own design intent.
- Build in a clean checkout on the same platform expected by Fenrir.
- Run tests and both fuzz targets locally with sanitizers.
- Inspect the corpus and dictionary to ensure they match the intended input formats.
- Confirm `.clusterfuzzlite/build.sh` remains executable after committing.
- Verify that no accidental credentials, absolute local paths, generated logs, or private notes were committed.
- Keep the final repository primarily human-written and manually reviewed before submission.
