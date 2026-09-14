#!/bin/sh
# Prints the hash CI stamps on a build image (its dockerfile.sha256 label) and
# compares a pulled image against to decide whether it is stale.
#
# The build context is docker/, so an image is made from its own directory plus
# docker/shared; the hash covers both, and a change to the shared sccache
# install marks all four images stale, not just a change to their Dockerfile.
#
# Usage: sh docker/image-hash.sh docker/ubuntu24.04
set -eu

image=$(basename "${1:?usage: image-hash.sh <image directory, e.g. docker/ubuntu24.04>}")
cd "$(dirname "$0")"
find "$image" shared -type f | LC_ALL=C sort | xargs sha256sum | sha256sum | cut -d' ' -f1
