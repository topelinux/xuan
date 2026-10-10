#!/usr/bin/env bash
set -euo pipefail

# Strip MIPS binaries with llvm-strip for maximum size reduction
# Usage: ./scripts/strip-mips.sh [build-directory]

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${1:-${ROOT_DIR}/build-mipsel-dyn}"

echo "=== MIPS Binary Stripper ==="
echo "Build directory: ${BUILD_DIR}"
echo ""

# Find llvm-strip
if command -v llvm-strip &>/dev/null; then
	STRIP_CMD="llvm-strip -s"
elif command -v /opt/homebrew/opt/llvm/bin/llvm-strip &>/dev/null; then
	STRIP_CMD="/opt/homebrew/opt/llvm/bin/llvm-strip -s"
else
	echo "Error: llvm-strip not found!"
	echo "Please install llvm: brew install llvm"
	exit 1
fi

echo "Using: ${STRIP_CMD}"
echo ""

TOTAL_SAVED=0
FILES_STRIPPED=0

# Strip function
strip_file() {
	local file="$1"
	local name="$2"

	if [[ -f "${file}" ]]; then
		local size_before=$(stat -f%z "${file}" 2>/dev/null || stat -c%s "${file}" 2>/dev/null)
		local size_before_mb=$(bc <<<"scale=2; ${size_before} / 1048576" 2>/dev/null || echo "N/A")

		echo "Stripping ${name}..."
		echo "  Before: ${size_before} bytes (${size_before_mb} MB)"

		${STRIP_CMD} "${file}"

		local size_after=$(stat -f%z "${file}" 2>/dev/null || stat -c%s "${file}" 2>/dev/null)
		local size_after_mb=$(bc <<<"scale=2; ${size_after} / 1048576" 2>/dev/null || echo "N/A")
		local saved=$((size_before - size_after))
		local saved_percent=$(bc <<<"scale=1; ${saved} * 100 / ${size_before}" 2>/dev/null || echo "N/A")

		echo "  After:  ${size_after} bytes (${size_after_mb} MB)"
		echo "  Saved:  ${saved} bytes (${saved_percent}%)"
		echo ""

		TOTAL_SAVED=$((TOTAL_SAVED + saved))
		FILES_STRIPPED=$((FILES_STRIPPED + 1))
	else
		echo "Skipping ${name} (not found: ${file})"
		echo ""
	fi
}

# Strip main executable
strip_file "${BUILD_DIR}/tjs" "tjs"

# Strip shared libraries (if they exist)
for lib in libada.so libserial-shim.so; do
	if [[ -f "${BUILD_DIR}/${lib}" ]]; then
		strip_file "${BUILD_DIR}/${lib}" "${lib}"
	fi
done

# Summary
echo "=== Summary ==="
echo "Files stripped: ${FILES_STRIPPED}"
if [[ ${TOTAL_SAVED} -gt 0 ]]; then
	local total_mb=$(bc <<<"scale=2; ${TOTAL_SAVED} / 1048576" 2>/dev/null || echo "N/A")
	echo "Total saved: ${TOTAL_SAVED} bytes (${total_mb} MB)"
else
	echo "Total saved: 0 bytes"
fi
echo ""
echo "Done!"
