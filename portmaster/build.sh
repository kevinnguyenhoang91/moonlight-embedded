#!/bin/bash
# Builds the PortMaster package: portmaster/dist/moonlightembedded.zip
# Requires Docker (or podman with a docker alias). No device toolchain needed.
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
SRC="$(dirname "$HERE")"
IMAGE=moonlight-portmaster-builder

git -C "$SRC" submodule update --init --recursive
docker build -t "$IMAGE" "$HERE"
docker run --rm --user "$(id -u):$(id -g)" -v "$SRC:/src" -w /src "$IMAGE" \
  bash /src/portmaster/build-in-container.sh

echo "Package: $HERE/dist/moonlightembedded.zip"
