#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
export TJS_TARGET_ARCH="armhf"
: "${TJS_LINK_MODE:=dynamic}"

# Build libffi from source for armhf
export TJS_BUILD_LIBFFI_FROM_SOURCE=ON

exec "${ROOT_DIR}/scripts/build-cross.sh" "$@"
