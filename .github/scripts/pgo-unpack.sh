#!/usr/bin/env bash
# Unpacks the variants the build jobs of the PGO workflow (#682) uploaded for
# one platform into pgo-run/<variant>/ and names them as pgo.py measure's
# sides, plain first: `sides=--side plain=pgo-run/plain/bin --side ...` in
# GITHUB_OUTPUT. A variant whose build failed has no artifact and is left
# out; without the plain build or without any other there is nothing to
# compare.
#
# Usage: pgo-unpack.sh linux|macos|windows   (from the workspace root, after
#        download-artifact into pgo-run/artifacts)
set -euo pipefail

platform=${1:?usage: pgo-unpack.sh linux|macos|windows}
sides=()
for variant in plain pgo bolt; do
    tarball="pgo-run/artifacts/pgo-$platform-$variant/pgo-$platform-$variant.tar"
    if [ ! -f "$tarball" ]; then
        continue
    fi
    mkdir -p "pgo-run/$variant"
    tar -xf "$tarball" -C "pgo-run/$variant"
    sides+=(--side "$variant=pgo-run/$variant/bin")
    echo "Unpacked $variant"
done
if [ ! -d pgo-run/plain ]; then
    echo "::error::The plain build of $platform did not finish: nothing to compare with"
    exit 1
fi
if [ "${#sides[@]}" -lt 4 ]; then
    echo "::error::No optimized build of $platform finished: nothing to compare"
    exit 1
fi
echo "sides=${sides[*]}" >> "$GITHUB_OUTPUT"
