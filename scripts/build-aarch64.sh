#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
export TJS_TARGET_ARCH="aarch64"
: "${TJS_LINK_MODE:=dynamic}"

exec "${ROOT_DIR}/scripts/build-cross.sh" "$@"
