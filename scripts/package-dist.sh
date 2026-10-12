#!/usr/bin/env bash
#
# Package the assembled dist tree (build/dist) into a relocatable archive that
# can be attached to a GitHub release.
#
# Usage:
#   ./scripts/package-dist.sh [BUILD_DIR] [OUT_DIR]
#
# Environment:
#   TJS_VERSION            Version string used in the archive name (default: 1.3.0)
#   TJS_RELEASE_PLATFORM   OS slug, e.g. linux | macos | windows (default: host)
#   TJS_RELEASE_ARCH       Arch slug, e.g. x86_64 | arm64 (default: host)
#
# Produces:
#   <OUT_DIR>/xuan-<version>-<platform>-<arch>.tar.gz   (Unix)
#   <OUT_DIR>/xuan-<version>-<platform>-<arch>.zip      (Windows dist)
#   <OUT_DIR>/xuan-<version>-<platform>-<arch>.sha256   (checksum)
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${1:-${ROOT_DIR}/build}"
OUT_DIR="${2:-${ROOT_DIR}/dist-archives}"
DIST_DIR="${BUILD_DIR}/dist"

TJS_VERSION="${TJS_VERSION:-1.3.0}"

detect_platform() {
    case "$(uname -s)" in
        Linux*)  echo "linux" ;;
        Darwin*) echo "macos" ;;
        MINGW*|MSYS*|CYGWIN*) echo "windows" ;;
        *) echo "unknown" ;;
    esac
}

detect_arch() {
    case "$(uname -m)" in
        x86_64|amd64) echo "x86_64" ;;
        arm64|aarch64) echo "arm64" ;;
        *) uname -m ;;
    esac
}

TJS_RELEASE_PLATFORM="${TJS_RELEASE_PLATFORM:-$(detect_platform)}"
TJS_RELEASE_ARCH="${TJS_RELEASE_ARCH:-$(detect_arch)}"

if [[ ! -d "${DIST_DIR}" ]]; then
    echo "Error: dist directory '${DIST_DIR}' not found; run the build first." >&2
    exit 1
fi

NAME="xuan-${TJS_VERSION}-${TJS_RELEASE_PLATFORM}-${TJS_RELEASE_ARCH}"
mkdir -p "${OUT_DIR}"

# Include user-facing docs in the archive for convenience.
for doc in README.md README.zh-CN.md LICENSE; do
    if [[ -f "${ROOT_DIR}/${doc}" ]]; then
        cp -f "${ROOT_DIR}/${doc}" "${DIST_DIR}/${doc}"
    fi
done

IS_WINDOWS_DIST=0
if [[ -f "${DIST_DIR}/bin/xuan.exe" ]]; then
    IS_WINDOWS_DIST=1
fi

ARCHIVE=""
if [[ "${IS_WINDOWS_DIST}" -eq 1 ]]; then
    ARCHIVE="${OUT_DIR}/${NAME}.zip"
    rm -f "${ARCHIVE}"
    if command -v zip >/dev/null 2>&1; then
        (cd "${DIST_DIR}" && zip -qr "${ARCHIVE}" .)
    else
        echo "Error: 'zip' is required to package the Windows dist." >&2
        exit 1
    fi
else
    ARCHIVE="${OUT_DIR}/${NAME}.tar.gz"
    rm -f "${ARCHIVE}"
    tar -C "${DIST_DIR}" -czf "${ARCHIVE}" .
fi

# Checksum
if command -v sha256sum >/dev/null 2>&1; then
    (cd "${OUT_DIR}" && sha256sum "$(basename "${ARCHIVE}")" > "$(basename "${ARCHIVE}").sha256")
elif command -v shasum >/dev/null 2>&1; then
    (cd "${OUT_DIR}" && shasum -a 256 "$(basename "${ARCHIVE}")" > "$(basename "${ARCHIVE}").sha256")
fi

echo "Created ${ARCHIVE}"
ls -lh "${ARCHIVE}"
