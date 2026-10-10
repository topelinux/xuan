#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
export TJS_TARGET_ARCH="windows"
: "${TJS_LINK_MODE:=static}"

exec "${ROOT_DIR}/scripts/build-cross.sh" "$@"
