#!/usr/bin/env bash
# Build the artifact image from the repository root.  IMAGE=name overrides the tag.
set -eu
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
IMAGE="${IMAGE:-ptracer-artifact}"
exec docker build -t "$IMAGE" -f "$ROOT/docker/Dockerfile" "$ROOT"
