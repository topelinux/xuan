#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT_DIR="${1:-${ROOT_DIR}/build-mips-repro}"
mkdir -p "${OUT_DIR}" "${ROOT_DIR}/.cache/zig-local" "${ROOT_DIR}/.cache/zig-global"

export ZIG_LOCAL_CACHE_DIR="${ROOT_DIR}/.cache/zig-local"
export ZIG_GLOBAL_CACHE_DIR="${ROOT_DIR}/.cache/zig-global"

cat > "${OUT_DIR}/hello.c" <<'SRC'
#include <stdio.h>
int main(void) {
    puts("hello-mips-dyn");
    return 0;
}
SRC

cat > "${OUT_DIR}/probe_libc_getenv.c" <<'SRC'
#include <stdio.h>
#include <stdlib.h>
int main(void) {
    const char *v = getenv("TMPDIR");
    printf("libc getenv TMPDIR=%s\n", v ? v : "(null)");
    return 0;
}
SRC

cat > "${OUT_DIR}/probe_uv_getenv.c" <<'SRC'
#include <stdio.h>
#include <uv.h>
int main(void) {
    char buf[256];
    size_t n = sizeof(buf);
    int r = uv_os_getenv("TMPDIR", buf, &n);
    printf("uv_os_getenv r=%d n=%zu val=%s\n", r, n, (r == 0) ? buf : "(null)");
    return 0;
}
SRC

cat > "${OUT_DIR}/probe_uv_tmpdir.c" <<'SRC'
#include <stdio.h>
#include <uv.h>
int main(void) {
    char buf[256];
    size_t n = sizeof(buf);
    int r = uv_os_tmpdir(buf, &n);
    printf("uv_os_tmpdir r=%d n=%zu val=%s\n", r, n, (r == 0) ? buf : "(null)");
    return 0;
}
SRC

cat > "${OUT_DIR}/probe_uv_setup_args_tmpdir.c" <<'SRC'
#include <stdio.h>
#include <uv.h>
int main(int argc, char **argv) {
    uv_setup_args(argc, argv);
    char buf[256];
    size_t n = sizeof(buf);
    int r = uv_os_tmpdir(buf, &n);
    printf("after uv_setup_args uv_os_tmpdir r=%d n=%zu val=%s\n", r, n, (r == 0) ? buf : "(null)");
    return 0;
}
SRC

ZIG="${ZIG:-zig}"
TARGET="${TARGET:-mipsel-linux-musleabi}"
CPU="${CPU:-mips32r2}"
COMMON=(-target "${TARGET}" -msoft-float -mcpu="${CPU}" -D_TIME_BITS=32)

"${ZIG}" cc "${COMMON[@]}" -dynamic "${OUT_DIR}/hello.c" -o "${OUT_DIR}/hello_dyn_musl"
"${ZIG}" cc "${COMMON[@]}" -static "${OUT_DIR}/hello.c" -o "${OUT_DIR}/hello_static_musl"
"${ZIG}" cc "${COMMON[@]}" -dynamic "${OUT_DIR}/probe_libc_getenv.c" -o "${OUT_DIR}/probe_libc_getenv"

if [ -f "${ROOT_DIR}/build-mipsel-dyn/deps/libuv/libuv.a" ]; then
    "${ZIG}" cc "${COMMON[@]}" -dynamic \
        "${OUT_DIR}/probe_uv_getenv.c" \
        -I "${ROOT_DIR}/deps/libuv/include" \
        "${ROOT_DIR}/build-mipsel-dyn/deps/libuv/libuv.a" \
        -o "${OUT_DIR}/probe_uv_getenv"
    "${ZIG}" cc "${COMMON[@]}" -dynamic \
        "${OUT_DIR}/probe_uv_tmpdir.c" \
        -I "${ROOT_DIR}/deps/libuv/include" \
        "${ROOT_DIR}/build-mipsel-dyn/deps/libuv/libuv.a" \
        -o "${OUT_DIR}/probe_uv_tmpdir"
    "${ZIG}" cc "${COMMON[@]}" -dynamic \
        "${OUT_DIR}/probe_uv_setup_args_tmpdir.c" \
        -I "${ROOT_DIR}/deps/libuv/include" \
        "${ROOT_DIR}/build-mipsel-dyn/deps/libuv/libuv.a" \
        -o "${OUT_DIR}/probe_uv_setup_args_tmpdir"
fi

echo "=== file ==="
file "${OUT_DIR}/hello_dyn_musl" "${OUT_DIR}/hello_static_musl" "${OUT_DIR}/probe_libc_getenv" \
    ${OUT_DIR}/probe_uv_getenv ${OUT_DIR}/probe_uv_tmpdir ${OUT_DIR}/probe_uv_setup_args_tmpdir 2>/dev/null || true

echo "=== dyn needed ==="
if command -v llvm-readelf >/dev/null 2>&1; then
    llvm-readelf -d "${OUT_DIR}/hello_dyn_musl" | rg "NEEDED|INTERP" || true
else
    echo "llvm-readelf not found"
fi

echo
cat <<'USAGE'
# Upload and reproduce on device:
# scp build-mips-repro/hello_dyn_musl root@<device>:/tmp/
# scp build-mips-repro/hello_static_musl root@<device>:/tmp/
# scp build-mips-repro/probe_libc_getenv root@<device>:/tmp/
# scp build-mips-repro/probe_uv_getenv root@<device>:/tmp/   # optional, if built
# scp build-mips-repro/probe_uv_tmpdir root@<device>:/tmp/   # optional, if built
# scp build-mips-repro/probe_uv_setup_args_tmpdir root@<device>:/tmp/   # optional, if built
#
# Device side:
# chmod +x /tmp/hello_dyn_musl /tmp/hello_static_musl
# /tmp/hello_static_musl
# /tmp/hello_dyn_musl
# /tmp/probe_libc_getenv
# /tmp/probe_uv_getenv
# /tmp/probe_uv_tmpdir
# /tmp/probe_uv_setup_args_tmpdir
# strace -f -o /tmp/hello_dyn.strace /tmp/hello_dyn_musl
# dmesg | tail -n 20
USAGE
