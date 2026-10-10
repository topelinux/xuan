#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
OUT_WASM="${1:-$ROOT_DIR/tests/wasm/simd_zig.wasm}"

mkdir -p "$ROOT_DIR/.zig-cache-local" "$ROOT_DIR/.zig-cache-global"

ZIG_LOCAL_CACHE_DIR="$ROOT_DIR/.zig-cache-local" \
ZIG_GLOBAL_CACHE_DIR="$ROOT_DIR/.zig-cache-global" \
zig build-exe "$ROOT_DIR/tests/wasm/simd.zig" \
  -target wasm32-freestanding \
  -mcpu baseline+simd128 \
  -O ReleaseSmall \
  -fno-entry \
  -rdynamic \
  -femit-bin="$OUT_WASM"

echo "built: $OUT_WASM"
