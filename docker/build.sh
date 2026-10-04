#!/usr/bin/env bash
# Build the artifact image from the repository root.  IMAGE=name overrides the tag.
# The image is labelled with docker/IMAGE_VERSION (checked by docker/run.sh) and the checked-out commit.
set -eu
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
IMAGE="${IMAGE:-ptracer-artifact}"
REV=$(git -C "$ROOT" rev-parse HEAD 2>/dev/null || echo unknown)
exec docker build --build-arg IMAGE_VERSION="$(cat "$ROOT/docker/IMAGE_VERSION")" --build-arg REVISION="$REV" \
    -t "$IMAGE" -f "$ROOT/docker/Dockerfile" "$ROOT"
