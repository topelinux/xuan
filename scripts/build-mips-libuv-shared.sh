#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SYSROOT_DEFAULT="${ROOT_DIR}/mips_root/target-mipsel_24kc_musl"
SYSROOT="${TJS_MIPS_SYSROOT:-${SYSROOT_DEFAULT}}"
BUILD_DIR="${1:-${ROOT_DIR}/build-mipsel-libuv-shared}"

mkdir -p "${ROOT_DIR}/.cache/zig-local" "${ROOT_DIR}/.cache/zig-global"
export ZIG_LOCAL_CACHE_DIR="${ROOT_DIR}/.cache/zig-local"
export ZIG_GLOBAL_CACHE_DIR="${ROOT_DIR}/.cache/zig-global"

cmake -S "${ROOT_DIR}/deps/libuv" -B "${BUILD_DIR}" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE="${ROOT_DIR}/cmake/toolchains/mipsel-linux-musleabi-zig.cmake" \
    -DTJS_MIPS_SYSROOT="${SYSROOT}" \
    -DTJS_LINK_MODE=dynamic \
    -DBUILD_SHARED_LIBS=ON \
    -DLIBUV_BUILD_SHARED=ON \
    -DLIBUV_BUILD_TESTS=OFF \
    -DLIBUV_BUILD_BENCH=OFF

cmake --build "${BUILD_DIR}" -j "$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)" --target uv

echo ""
echo "=== Build completed successfully ==="
for lib in "${BUILD_DIR}/libuv.so.1.0.0" "${BUILD_DIR}/libuv.so.1" "${BUILD_DIR}/libuv.so"; do
    if [[ -f "${lib}" ]]; then
        ls -lh "${lib}"
        file "${lib}"
    fi
done

