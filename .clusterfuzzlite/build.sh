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
    -DLOGSQUIRL_USE_VECTORSCAN=OFF \
    -DCMAKE_BUILD_WITH_INSTALL_RPATH=ON \
    -DCMAKE_INSTALL_RPATH='$ORIGIN/lib'
cmake --build "$WORK/build" --target logsquirl_fuzzers

for fuzzer in "$SRC"/logsquirl/tests/fuzz/*_fuzzer.cpp; do
    name=$(basename "$fuzzer" .cpp)
    cp "$WORK/build/output/$name" "$OUT/$name"
    # The image that checks and runs a fuzzer has no Qt: the shared libraries
    # it needs, beyond glibc's, travel next to it, and its RPATH (set above)
    # looks in lib/ first.
    mkdir -p "$OUT/lib"
    ldd "$OUT/$name" | awk '/=> \// { print $3 }' | while read -r lib; do
        case "$(basename "$lib")" in
            libc.so.*|libm.so.*|libdl.so.*|libpthread.so.*|librt.so.*|libresolv.so.*|libutil.so.*|ld-linux*) ;;
            *) cp -nL "$lib" "$OUT/lib/" ;;
        esac
    done
    corpus="$SRC/logsquirl/tests/fuzz/corpus/${name%_fuzzer}"
    if [ -d "$corpus" ]; then
        zip -j -q "$OUT/${name}_seed_corpus.zip" "$corpus"/*
    fi
done
