#!/bin/sh
# Prints the inputs hash of a build image: the tag CI Build pulls it by and the
# Docker Images workflow pushes it under (#215), also stamped on the image as
# its dockerfile.sha256 label.
#
# The build context is docker/, so an image is made from its own directory plus
# docker/shared; the hash covers both, and a change to the shared sccache
# install (or to shared/refresh-stamp, the scheduled rebuild marker) gives all
# four images a new tag, not just a change to their Dockerfile.
#
# Usage: sh docker/image-hash.sh docker/ubuntu24.04
set -eu

image=$(basename "${1:?usage: image-hash.sh <image directory, e.g. docker/ubuntu24.04>}")
cd "$(dirname "$0")"
find "$image" shared -type f | LC_ALL=C sort | xargs sha256sum | sha256sum | cut -d' ' -f1
