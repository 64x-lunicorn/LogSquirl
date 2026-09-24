#!/bin/bash -eu
# Builds the libFuzzer targets for ClusterFuzzLite (#477). CC, CXX, CFLAGS,
# CXXFLAGS (sanitizer and coverage instrumentation), LIB_FUZZING_ENGINE, OUT
# and WORK come from the OSS-Fuzz builder image.

# Vectorscan is left out: the fuzz targets do not use it, and building it
# with the builder's newest Clang trips its own -Werror.
cmake -S "$SRC/logsquirl" -B "$WORK/build" -G Ninja \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DCMAKE_C_COMPILER="$CC" -DCMAKE_CXX_COMPILER="$CXX" \
    -DCMAKE_C_FLAGS="$CFLAGS" -DCMAKE_CXX_FLAGS="$CXXFLAGS" \
    -DLOGSQUIRL_FUZZ_ENGINE="$LIB_FUZZING_ENGINE" \
    -DLOGSQUIRL_BUILD_FUZZERS=ON \
    -DLOGSQUIRL_BUILD_TESTS=OFF \
    -DLOGSQUIRL_USE_LTO=OFF \
    -DLOGSQUIRL_USE_VECTORSCAN=OFF
cmake --build "$WORK/build" --target logsquirl_fuzzers

for fuzzer in "$SRC"/logsquirl/tests/fuzz/*_fuzzer.cpp; do
    name=$(basename "$fuzzer" .cpp)
    cp "$WORK/build/output/$name" "$OUT/$name"
    corpus="$SRC/logsquirl/tests/fuzz/corpus/${name%_fuzzer}"
    if [ -d "$corpus" ]; then
        zip -j -q "$OUT/${name}_seed_corpus.zip" "$corpus"/*
    fi
done
