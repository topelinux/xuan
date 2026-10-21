# Cross Build README

This document explains how to use `scripts/build-cross.sh` for cross-compiling **xuan** for `aarch64` and `mipsel`.

## Quick Start

```bash
# default target is aarch64
./scripts/build-cross.sh

# explicit arch selection
TJS_TARGET_ARCH=aarch64 ./scripts/build-cross.sh
TJS_TARGET_ARCH=mipsel  ./scripts/build-cross.sh
```

Optional positional argument:

```bash
./scripts/build-cross.sh /path/to/custom-build-dir
```

## Help

```bash
./scripts/build-cross.sh --help
```

The help output lists all supported environment variables and examples.

## Core Selectors

- `TJS_TARGET_ARCH`: `aarch64` or `mipsel` (default: `aarch64`)
- `TJS_LINK_MODE`: `dynamic` or `static`
  - default: `aarch64=dynamic`, `mipsel=static`

## WASM / Native Module Toggles

- `TJS_WAMR_SIMD`: `ON | OFF | AUTO` (script default is `OFF`)
- `TJS_DISABLE_WASM`: disable built-in wasm in `xuan`
  - default: `aarch64=ON`, `mipsel=OFF`
- `TJS_BUILD_WASM_NATIVE_MODULE`: build `xuan-wasm` native module (`ON | OFF`, default `ON`)

## Common Build Toggles

- `BUILD_WITH_MIMALLOC`: `ON | OFF`
- `TJS_DISABLE_ADA_URL`: `ON | OFF`
- `TJS_DISABLE_PROMISE_TRACKER`: `ON | OFF`
- `TJS_USE_SYSROOT_LIBUV`: `ON | OFF`
- `TJS_USE_DEPS_LIBUV_SHARED`: `ON | OFF`

## Sysroot / Toolchain Variables

- `TJS_SYSROOT`: generic sysroot override (works for both targets)
- `TJS_AARCH64_SYSROOT`: aarch64 sysroot override
- `TJS_MIPS_SYSROOT`: mipsel sysroot override
- `TJS_AARCH64_TARGET`, `TJS_AARCH64_HOST`
- `TJS_MIPS_TARGET`, `TJS_MIPS_HOST`

## libffi Variables

- `TJS_BUILD_LIBFFI_FROM_SOURCE`: `ON | OFF`
- `TJS_LIBFFI_LINK_MODE`: `shared | static`
- `TJS_FFI_LIB`: explicit libffi path
- `TJS_LIBFFI_SRC_DIR`: libffi source directory
- `TJS_LIBFFI_TARBALL`: libffi source tarball path
- `TJS_LIBFFI_SOURCE_URL`: download URL when tarball is missing
- `TJS_LIBFFI_SOURCE_SHA256`: expected SHA-256 for tarball verification
- `TJS_AARCH64_BUILD_ONLY_LIBFFI`: `ON | OFF`
- `TJS_MIPS_BUILD_ONLY_LIBFFI`: `ON | OFF`

### libffi Source Provisioning

When `TJS_BUILD_LIBFFI_FROM_SOURCE=ON` and no usable `FFI_LIB` is found, `build-cross.sh` now ensures libffi source is present before building:

1. Reuse `TJS_LIBFFI_SRC_DIR` if `configure` exists.
2. Otherwise reuse `TJS_LIBFFI_TARBALL` if present.
3. If tarball is missing, download from `TJS_LIBFFI_SOURCE_URL`.
4. Verify tarball SHA-256 with `TJS_LIBFFI_SOURCE_SHA256`.
5. Extract to the source directory and continue the existing cross-build flow.

Default source settings:

- Source dir: `deps_lib/libffi-3.5.2`
- Tarball: `deps_lib/libffi-3.5.2.tar.gz`
- URL: `https://github.com/libffi/libffi/releases/download/v3.5.2/libffi-3.5.2.tar.gz`

Offline-only mode: set `TJS_LIBFFI_SOURCE_URL=""` to disable download attempts (build will fail fast if source dir/tarball are absent).

## Examples

```bash
# aarch64: dynamic + mimalloc + SIMD ON + wasm externalized
TJS_TARGET_ARCH=aarch64 \
BUILD_WITH_MIMALLOC=ON \
TJS_WAMR_SIMD=ON \
TJS_DISABLE_WASM=ON \
TJS_BUILD_WASM_NATIVE_MODULE=ON \
./scripts/build-cross.sh
```

```bash
# mipsel: static build with defaults
TJS_TARGET_ARCH=mipsel ./scripts/build-cross.sh
```

```bash
# mipsel: dynamic build + custom output dir
TJS_TARGET_ARCH=mipsel \
TJS_LINK_MODE=dynamic \
./scripts/build-cross.sh /tmp/build-mips-dyn
```

## Compatibility Wrappers

These existing commands still work, and now call `build-cross.sh` internally:

- `./scripts/build-aarch64.sh`
- `./scripts/build-mips-static.sh`

## armhf targets with an older musl

armhf builds are linked against musl 1.2.5, which uses the time64 ABI and
exports `pthread_getname_np`. On a target with an older musl (for example
Alpine 3.12 / musl 1.1.24) the binary fails at load time:

```
Error relocating .../libuv.so.1: __clock_gettime64: symbol not found
Error relocating .../libuv.so.1: __stat_time64: symbol not found
...
```

The fix is to install a newer musl on the target rather than adding time64
stubs to the build:

```bash
./scripts/build-musl-armhf.sh     # -> build-armhf-musl/ld-musl-armhf.so.1

scp build-armhf-musl/ld-musl-armhf.so.1 root@192.168.1.1:/tmp/
ssh root@192.168.1.1
    cp -p /lib/ld-musl-armhf.so.1 /lib/ld-musl-armhf.so.1.orig
    cp /tmp/ld-musl-armhf.so.1 /lib/ld-musl-armhf.so.1
    sync
```

To try it in `/tmp` before touching the system, invoke the new loader
explicitly. musl's `libc.so` *is* the dynamic loader, so `LD_LIBRARY_PATH`
alone cannot replace it:

```bash
scp build-armhf-musl/ld-musl-armhf.so.1 root@192.168.1.1:/tmp/
ssh root@192.168.1.1
    /tmp/ld-musl-armhf.so.1 --library-path /tmp/lib ./bin/xuan --version
```

The helper script also works around two build/runtime quirks:

- vendor 4.9 kernels that stub the `statx` syscall with `EINVAL` (musl only
  falls back to `fstatat64` on `ENOSYS`, so `stat()` would fail on the whole
  system otherwise)
- zig's compiler-rt weak hidden `memcpy`/`memmove`/`memset`/`memcmp`,
  math functions and `__stack_chk_guard`, which would otherwise hide musl's
  own definitions in the linked `libc.so`

armhf no longer links `libtime64_stub.so`. `TJS_TIME64_STUB_LIB` remains
available for armv7 builds.

