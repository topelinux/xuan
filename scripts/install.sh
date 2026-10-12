#!/bin/sh
# xuan installer — downloads a prebuilt release and installs it locally.
#
# Usage:
#   curl -fsSL https://raw.githubusercontent.com/topelinux/xuan/main/scripts/install.sh | sh
#   sh install.sh --version 1.3.0
#
# Environment:
#   XUAN_VERSION    Install a specific version (default: latest release)
#   XUAN_HOME       Install directory           (default: $HOME/.xuan)
#   XUAN_BIN_DIR    Directory for the `xuan` symlink (default: $HOME/.local/bin)
#   XUAN_REPO       owner/repo on GitHub        (default: topelinux/xuan)
#   XUAN_BASE_URL   Override the release download base URL (advanced)
set -eu

REPO="${XUAN_REPO:-topelinux/xuan}"
XUAN_HOME="${XUAN_HOME:-$HOME/.xuan}"
XUAN_BIN_DIR="${XUAN_BIN_DIR:-$HOME/.local/bin}"
VERSION="${XUAN_VERSION:-}"

usage() {
    cat <<EOF
xuan installer

Usage: sh install.sh [options]

Options:
  --version <ver>   Install a specific version, e.g. 1.3.0 (default: latest)
  --dir <path>      Install directory (default: \$HOME/.xuan)
  --bin-dir <path>  Where to symlink \`xuan\` (default: \$HOME/.local/bin)
  -h, --help        Show this help

Supported prebuilt targets: linux-x86_64, macos-arm64.
Windows users: download the .zip from https://github.com/${REPO}/releases
EOF
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --version) VERSION="${2:-}"; shift 2 ;;
        --version=*) VERSION="${1#*=}"; shift ;;
        --dir) XUAN_HOME="${2:-}"; shift 2 ;;
        --dir=*) XUAN_HOME="${1#*=}"; shift ;;
        --bin-dir) XUAN_BIN_DIR="${2:-}"; shift 2 ;;
        --bin-dir=*) XUAN_BIN_DIR="${1#*=}"; shift ;;
        -h|--help) usage; exit 0 ;;
        *) echo "error: unknown option '$1'" >&2; usage >&2; exit 1 ;;
    esac
done

err() { echo "error: $*" >&2; exit 1; }
note() { echo "$*" >&2; }

download() {
    url="$1"
    out="$2"
    if command -v curl >/dev/null 2>&1; then
        curl -fsSL "$url" -o "$out"
    elif command -v wget >/dev/null 2>&1; then
        wget -qO "$out" "$url"
    else
        err "neither curl nor wget is available"
    fi
}

download_stdout() {
    url="$1"
    if command -v curl >/dev/null 2>&1; then
        curl -fsSL "$url"
    elif command -v wget >/dev/null 2>&1; then
        wget -qO- "$url"
    else
        err "neither curl nor wget is available"
    fi
}

detect_platform() {
    case "$(uname -s)" in
        Linux) echo "linux" ;;
        Darwin) echo "macos" ;;
        MINGW*|MSYS*|CYGWIN*)
            err "on Windows, download the .zip from https://github.com/${REPO}/releases and unzip it" ;;
        *) err "unsupported OS: $(uname -s)" ;;
    esac
}

detect_arch() {
    case "$(uname -m)" in
        x86_64|amd64) echo "x86_64" ;;
        arm64|aarch64) echo "arm64" ;;
        *) err "unsupported CPU architecture: $(uname -m)" ;;
    esac
}

PLATFORM="$(detect_platform)"
ARCH="$(detect_arch)"

case "${PLATFORM}-${ARCH}" in
    linux-x86_64|macos-arm64) : ;;
    *)
        note "error: no prebuilt binary for ${PLATFORM}-${ARCH} yet."
        note "Prebuilt targets: linux-x86_64, macos-arm64, windows-x86_64 (zip)."
        note "Build from source: https://github.com/${REPO}#building-from-source"
        exit 1
        ;;
esac

if [ -z "$VERSION" ]; then
    note "Resolving latest xuan release..."
    # Preferred: follow the /releases/latest redirect (no API rate limits).
    if command -v curl >/dev/null 2>&1; then
        VERSION="$(curl -fsSL -o /dev/null -w '%{url_effective}' \
            "https://github.com/${REPO}/releases/latest" 2>/dev/null \
            | sed -n 's#.*/tag/v\{0,1\}##p')"
    fi
    # Fallback: the GitHub API.
    if [ -z "$VERSION" ]; then
        VERSION="$(download_stdout "https://api.github.com/repos/${REPO}/releases/latest" 2>/dev/null \
            | sed -n 's/.*"tag_name"[[:space:]]*:[[:space:]]*"v\{0,1\}\([^"]*\)".*/\1/p' \
            | head -n 1)"
    fi
    [ -n "$VERSION" ] || err "could not resolve the latest version; set XUAN_VERSION=<ver>"
fi
VERSION="${VERSION#v}"

ASSET="xuan-${VERSION}-${PLATFORM}-${ARCH}.tar.gz"
BASE_URL="${XUAN_BASE_URL:-https://github.com/${REPO}/releases/download/v${VERSION}}"

TMP_DIR="$(mktemp -d 2>/dev/null || mktemp -d -t xuan-install)"
trap 'rm -rf "$TMP_DIR"' EXIT INT TERM

note "Downloading ${ASSET} (xuan ${VERSION})..."
download "${BASE_URL}/${ASSET}" "${TMP_DIR}/${ASSET}"

if download "${BASE_URL}/${ASSET}.sha256" "${TMP_DIR}/${ASSET}.sha256" 2>/dev/null; then
    note "Verifying checksum..."
    if command -v sha256sum >/dev/null 2>&1; then
        ( cd "$TMP_DIR" && sha256sum -c "${ASSET}.sha256" ) || err "checksum verification failed"
    elif command -v shasum >/dev/null 2>&1; then
        ( cd "$TMP_DIR" && shasum -a 256 -c "${ASSET}.sha256" ) || err "checksum verification failed"
    else
        note "warning: no sha256 tool found, skipping checksum verification"
    fi
else
    note "warning: checksum file not available, skipping verification"
fi

note "Installing to ${XUAN_HOME}..."
rm -rf "$XUAN_HOME"
mkdir -p "$XUAN_HOME"
tar -xzf "${TMP_DIR}/${ASSET}" -C "$XUAN_HOME"

# The macOS binaries are not notarized; clear the quarantine flag so Gatekeeper
# does not block first run.
if [ "$PLATFORM" = "macos" ] && command -v xattr >/dev/null 2>&1; then
    xattr -dr com.apple.quarantine "$XUAN_HOME" 2>/dev/null || true
fi

mkdir -p "$XUAN_BIN_DIR"
ln -sf "${XUAN_HOME}/bin/xuan" "${XUAN_BIN_DIR}/xuan"

echo ""
echo "xuan ${VERSION} installed."
echo "  install dir: ${XUAN_HOME}"
echo "  binary:      ${XUAN_BIN_DIR}/xuan"

case ":${PATH}:" in
    *":${XUAN_BIN_DIR}:"*) ;;
    *)
        echo ""
        echo "Add it to your PATH:"
        echo "  export PATH=\"${XUAN_BIN_DIR}:\$PATH\""
        ;;
esac

echo ""
"${XUAN_HOME}/bin/xuan" --version 2>/dev/null || true
