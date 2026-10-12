#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

sha256_file() {
	local file_path="$1"
	if command -v sha256sum >/dev/null 2>&1; then
		sha256sum "${file_path}" | awk '{print $1}'
	elif command -v shasum >/dev/null 2>&1; then
		shasum -a 256 "${file_path}" | awk '{print $1}'
	else
		echo "Error: neither sha256sum nor shasum is available for SHA-256 verification."
		exit 1
	fi
}

download_file() {
	local url="$1"
	local out_path="$2"
	local tmp_path="${out_path}.tmp.$$"
	if command -v curl >/dev/null 2>&1; then
		if ! curl -fL --retry 3 --connect-timeout 15 -o "${tmp_path}" "${url}"; then
			rm -f "${tmp_path}"
			echo "Error: failed to download ${url}"
			exit 1
		fi
	elif command -v wget >/dev/null 2>&1; then
		if ! wget -O "${tmp_path}" "${url}"; then
			rm -f "${tmp_path}"
			echo "Error: failed to download ${url}"
			exit 1
		fi
	else
		echo "Error: neither curl nor wget is available to download ${url}"
		exit 1
	fi
	mv -f "${tmp_path}" "${out_path}"
}

ensure_libffi_source_dir() {
	local src_dir="$1"
	local tarball_path="$2"
	local source_url="$3"
	local expected_sha256="$4"
	local src_dir_name
	local src_parent_dir
	local actual_sha256

	if [[ -z "${src_dir}" || "${src_dir}" == "/" || "${src_dir}" == "." ]]; then
		echo "Error: invalid TJS_LIBFFI_SRC_DIR='${src_dir}'"
		exit 1
	fi
	if [[ -z "${tarball_path}" ]]; then
		echo "Error: invalid empty TJS_LIBFFI_TARBALL path."
		exit 1
	fi

	if [[ -x "${src_dir}/configure" ]]; then
		return 0
	fi

	src_dir_name="$(basename "${src_dir}")"
	src_parent_dir="$(dirname "${src_dir}")"
	mkdir -p "${src_parent_dir}"

	if [[ ! -f "${tarball_path}" ]]; then
		if [[ -z "${source_url}" ]]; then
			echo "Error: ${tarball_path} not found and TJS_LIBFFI_SOURCE_URL is empty."
			exit 1
		fi
		echo ""
		echo "=== Downloading ${src_dir_name}.tar.gz ==="
		echo "URL: ${source_url}"
		download_file "${source_url}" "${tarball_path}"
	fi

	if [[ -n "${expected_sha256}" ]]; then
		actual_sha256="$(sha256_file "${tarball_path}")"
		if [[ "${actual_sha256}" != "${expected_sha256}" ]]; then
			echo "Error: SHA-256 mismatch for ${tarball_path}"
			echo "Expected: ${expected_sha256}"
			echo "Actual:   ${actual_sha256}"
			exit 1
		fi
	fi

	echo ""
	echo "=== Extracting $(basename "${tarball_path}") ==="
	rm -rf "${src_dir}"
	tar -xzf "${tarball_path}" -C "${src_parent_dir}"

	if [[ ! -x "${src_dir}/configure" ]]; then
		echo "Error: ${src_dir}/configure not found or not executable after extraction"
		exit 1
	fi
}

print_help() {
	cat <<'EOF'
Usage:
  ./scripts/build-cross.sh [BUILD_DIR]

Description:
  Unified cross build entry for xuan (aarch64/armv7/armhf/mipsel/windows).
  Most options are controlled by environment variables.

Core selectors:
  TJS_TARGET_ARCH                 Target architecture: aarch64 | armv7 | armhf | mipsel | windows (default: aarch64)
  TJS_LINK_MODE                   Link mode: dynamic | static
                                  default: aarch64=dynamic, armv7=dynamic, armhf=dynamic, mipsel=static, windows=static

WASM/native module switches:
  TJS_WAMR_SIMD                   WAMR SIMD: ON | OFF | AUTO (default: OFF in this script)
  TJS_DISABLE_WASM                Disable built-in wasm runtime in xuan: ON | OFF
                                  default: aarch64=ON, armv7=OFF, armhf=ON, mipsel=OFF, windows=ON
  TJS_BUILD_WASM_NATIVE_MODULE    Build xuan-wasm native module: ON | OFF (default: ON for linux, OFF for windows)

Other frequently used toggles:
  BUILD_WITH_MIMALLOC             ON | OFF
  TJS_ENABLE_LVGL                 ON | OFF (default: OFF)
  TJS_DISABLE_ADA_URL             ON | OFF
  TJS_DISABLE_PROMISE_TRACKER     ON | OFF
  TJS_USE_SYSROOT_LIBUV           ON | OFF
  TJS_USE_DEPS_LIBUV_SHARED       ON | OFF

Sysroot/toolchain variables:
  TJS_SYSROOT                     Generic sysroot override (all arch, not needed for armhf)
  TJS_AARCH64_SYSROOT             aarch64 sysroot override
  TJS_ARM_SYSROOT                armv7 sysroot override
  TJS_MIPS_SYSROOT                mipsel sysroot override
  TJS_AARCH64_TARGET / TJS_AARCH64_HOST
  TJS_ARM_TARGET / TJS_ARM_HOST
  TJS_ARMHF_TARGET / TJS_ARMHF_HOST
  TJS_MIPS_TARGET / TJS_MIPS_HOST

libffi variables:
  TJS_BUILD_LIBFFI_FROM_SOURCE    ON | OFF
  TJS_LIBFFI_LINK_MODE            shared | static
  TJS_FFI_LIB                     Explicit libffi path
  TJS_LIBFFI_SRC_DIR              libffi source dir (default: deps_lib/libffi-3.5.2)
  TJS_LIBFFI_TARBALL              libffi source tarball path (default: deps_lib/libffi-3.5.2.tar.gz)
  TJS_LIBFFI_SOURCE_URL           libffi source URL
  TJS_LIBFFI_SOURCE_SHA256        expected SHA-256 for tarball
  TJS_AARCH64_BUILD_ONLY_LIBFFI   ON | OFF
  TJS_ARM_BUILD_ONLY_LIBFFI      ON | OFF
  TJS_MIPS_BUILD_ONLY_LIBFFI      ON | OFF
  TJS_WINDOWS_BUILD_ONLY_LIBFFI   ON | OFF

Examples:
  # aarch64 dynamic + mimalloc + SIMD ON + wasm externalized
  TJS_TARGET_ARCH=aarch64 BUILD_WITH_MIMALLOC=ON TJS_WAMR_SIMD=ON \
  TJS_DISABLE_WASM=ON TJS_BUILD_WASM_NATIVE_MODULE=ON ./scripts/build-cross.sh

  # mips static with default settings
  TJS_TARGET_ARCH=mipsel ./scripts/build-cross.sh

  # mips dynamic and custom output dir
  TJS_TARGET_ARCH=mipsel TJS_LINK_MODE=dynamic ./scripts/build-cross.sh /tmp/build-mips-dyn

  # windows static build
  TJS_TARGET_ARCH=windows ./scripts/build-cross.sh

  # windows dynamic build
  TJS_TARGET_ARCH=windows TJS_LINK_MODE=dynamic ./scripts/build-cross.sh
EOF
}

if [[ "${1:-}" == "--help" || "${1:-}" == "-h" || "${1:-}" == "help" ]]; then
	print_help
	exit 0
fi
if [[ "$#" -gt 1 ]]; then
	echo "Error: too many arguments."
	echo "Run './scripts/build-cross.sh --help' for usage."
	exit 1
fi

# Apply the in-tree dependency patches (libuv/quickjs/mimalloc/libwebsockets)
# before configuring. Native builds get this for free through the Makefile, but
# this script drives cmake directly, so do it explicitly — otherwise a fresh
# checkout configures unpatched deps (e.g. mimalloc's -Wdate-time breaks the
# Windows cross build).
echo ""
echo "=== Applying dependency patches ==="
make -C "${ROOT_DIR}" apply-deps-patches

# Always (re)generate JS->C bundles on host before cross configure/build.
# Cross toolchains cannot run target-built xuanc, so bundle generation must happen on host.
echo ""
echo "=== Regenerating JS bundles on host (make js) ==="
make -C "${ROOT_DIR}" js

TARGET_ARCH="${TJS_TARGET_ARCH:-aarch64}"

if [[ "${TARGET_ARCH}" != "aarch64" && "${TARGET_ARCH}" != "mipsel" && "${TARGET_ARCH}" != "armv7" && "${TARGET_ARCH}" != "armhf" && "${TARGET_ARCH}" != "windows" ]]; then
	echo "Error: unsupported TJS_TARGET_ARCH='${TARGET_ARCH}', expected 'aarch64', 'armv7', 'armhf', 'mipsel', or 'windows'"
	exit 1
fi

if [[ "${TARGET_ARCH}" == "aarch64" ]]; then
	SYSROOT_DEFAULT="${ROOT_DIR}/aarch64_root/target-aarch64-linux-gnu"
	LINK_MODE="${TJS_LINK_MODE:-dynamic}"
	SYSROOT="${TJS_AARCH64_SYSROOT:-${TJS_SYSROOT:-${SYSROOT_DEFAULT}}}"
	TOOLCHAIN_FILE="${ROOT_DIR}/cmake/toolchains/aarch64-linux-gnu-zig.cmake"
	ARCH_TARGET="${TJS_AARCH64_TARGET:-aarch64-linux-gnu}"
	ARCH_HOST="${TJS_AARCH64_HOST:-aarch64-linux-gnu}"
	BUILD_LIBFFI_FROM_SOURCE="${TJS_BUILD_LIBFFI_FROM_SOURCE:-ON}"
	LIBFFI_LINK_MODE="${TJS_LIBFFI_LINK_MODE:-shared}"
	if [[ "${LINK_MODE}" == "dynamic" ]]; then
		BUILD_DIR_DEFAULT="${ROOT_DIR}/build-aarch64-dyn"
	else
		BUILD_DIR_DEFAULT="${ROOT_DIR}/build-aarch64"
	fi
	BUILD_WITH_MIMALLOC="${BUILD_WITH_MIMALLOC:-OFF}"
	TJS_DISABLE_ADA_URL="${TJS_DISABLE_ADA_URL:-OFF}"
	TJS_DISABLE_PROMISE_TRACKER="${TJS_DISABLE_PROMISE_TRACKER:-OFF}"
	TJS_USE_SYSROOT_LIBUV="${TJS_USE_SYSROOT_LIBUV:-OFF}"
	TJS_USE_DEPS_LIBUV_SHARED="${TJS_USE_DEPS_LIBUV_SHARED:-OFF}"
	TJS_WAMR_SIMD="${TJS_WAMR_SIMD:-OFF}"
	TJS_DISABLE_WASM="${TJS_DISABLE_WASM:-ON}"
	TJS_BUILD_WASM_NATIVE_MODULE="${TJS_BUILD_WASM_NATIVE_MODULE:-ON}"
	TJS_ENABLE_LVGL="${TJS_ENABLE_LVGL:-OFF}"
	BUILD_ONLY_LIBFFI="${TJS_AARCH64_BUILD_ONLY_LIBFFI:-OFF}"
	FFI_LIB_DEFAULT=""
	STRIP_ARCH_CANDIDATE="aarch64-linux-gnu-strip"
elif [[ "${TARGET_ARCH}" == "armv7" ]]; then
	SYSROOT_DEFAULT="${ROOT_DIR}/armv7_root/target-arm-linux-musleabihf"
	LINK_MODE="${TJS_LINK_MODE:-dynamic}"
	LIBFFI_LINK_MODE="${TJS_LIBFFI_LINK_MODE:-static}"
	SYSROOT="${TJS_ARM_SYSROOT:-${TJS_SYSROOT:-${SYSROOT_DEFAULT}}}"
	TOOLCHAIN_FILE="${ROOT_DIR}/cmake/toolchains/arm-linux-musleabihf-zig.cmake"
	ARCH_TARGET="${TJS_ARM_TARGET:-arm-linux-musleabihf}"
	ARCH_HOST="${TJS_ARM_HOST:-arm-linux-musleabihf}"
	BUILD_LIBFFI_FROM_SOURCE="${TJS_BUILD_LIBFFI_FROM_SOURCE:-ON}"
	LIBFFI_LINK_MODE="${TJS_LIBFFI_LINK_MODE:-shared}"
	if [[ "${LINK_MODE}" == "dynamic" ]]; then
		BUILD_DIR_DEFAULT="${ROOT_DIR}/build-armv7-dyn"
	else
		BUILD_DIR_DEFAULT="${ROOT_DIR}/build-armv7"
	fi
	BUILD_WITH_MIMALLOC="${BUILD_WITH_MIMALLOC:-OFF}"
	TJS_DISABLE_ADA_URL="${TJS_DISABLE_ADA_URL:-OFF}"
	TJS_DISABLE_PROMISE_TRACKER="${TJS_DISABLE_PROMISE_TRACKER:-OFF}"
	TJS_USE_SYSROOT_LIBUV="${TJS_USE_SYSROOT_LIBUV:-OFF}"
	TJS_USE_DEPS_LIBUV_SHARED="${TJS_USE_DEPS_LIBUV_SHARED:-OFF}"
	TJS_WAMR_SIMD="${TJS_WAMR_SIMD:-OFF}"
	TJS_DISABLE_WASM="${TJS_DISABLE_WASM:-ON}"
	TJS_BUILD_WASM_NATIVE_MODULE="${TJS_BUILD_WASM_NATIVE_MODULE:-OFF}"
	TJS_ENABLE_LVGL="${TJS_ENABLE_LVGL:-OFF}"
	BUILD_ONLY_LIBFFI="${TJS_ARM_BUILD_ONLY_LIBFFI:-OFF}"
	FFI_LIB_DEFAULT=""
	STRIP_ARCH_CANDIDATE="arm-linux-gnueabihf-strip"
elif [[ "${TARGET_ARCH}" == "armhf" ]]; then
	SYSROOT_DEFAULT=""
	LINK_MODE="${TJS_LINK_MODE:-dynamic}"
	LIBFFI_LINK_MODE="${TJS_LIBFFI_LINK_MODE:-shared}"
	SYSROOT=""
	TOOLCHAIN_FILE="${ROOT_DIR}/cmake/toolchains/arm-linux-musleabihf-nosysroot-zig.cmake"
	ARCH_TARGET="${TJS_ARMHF_TARGET:-arm-linux-musleabihf}"
	ARCH_HOST="${TJS_ARMHF_HOST:-arm-linux-musleabihf}"
	BUILD_LIBFFI_FROM_SOURCE="${TJS_BUILD_LIBFFI_FROM_SOURCE:-ON}"
	LIBFFI_LINK_MODE="${TJS_LIBFFI_LINK_MODE:-shared}"
	if [[ "${LINK_MODE}" == "dynamic" ]]; then
		BUILD_DIR_DEFAULT="${ROOT_DIR}/build-armhf-dyn"
	else
		BUILD_DIR_DEFAULT="${ROOT_DIR}/build-armhf"
	fi
	BUILD_WITH_MIMALLOC="${BUILD_WITH_MIMALLOC:-OFF}"
	TJS_DISABLE_ADA_URL="${TJS_DISABLE_ADA_URL:-OFF}"
	TJS_DISABLE_PROMISE_TRACKER="${TJS_DISABLE_PROMISE_TRACKER:-OFF}"
	TJS_USE_SYSROOT_LIBUV="${TJS_USE_SYSROOT_LIBUV:-OFF}"
	TJS_USE_DEPS_LIBUV_SHARED="${TJS_USE_DEPS_LIBUV_SHARED:-ON}"
	TJS_WAMR_SIMD="${TJS_WAMR_SIMD:-OFF}"
	TJS_DISABLE_WASM="${TJS_DISABLE_WASM:-ON}"
	TJS_BUILD_WASM_NATIVE_MODULE="${TJS_BUILD_WASM_NATIVE_MODULE:-OFF}"
	TJS_ENABLE_LVGL="${TJS_ENABLE_LVGL:-OFF}"
	BUILD_ONLY_LIBFFI="${TJS_ARMHF_BUILD_ONLY_LIBFFI:-OFF}"
	FFI_LIB_DEFAULT=""
	STRIP_ARCH_CANDIDATE="arm-linux-gnueabihf-strip"
else
	SYSROOT_DEFAULT="${ROOT_DIR}/mips_root/target-mipsel_24kc_musl"
	LINK_MODE="${TJS_LINK_MODE:-static}"
	SYSROOT="${TJS_MIPS_SYSROOT:-${TJS_SYSROOT:-${SYSROOT_DEFAULT}}}"
	TOOLCHAIN_FILE="${ROOT_DIR}/cmake/toolchains/mipsel-linux-musleabi-zig.cmake"
	ARCH_TARGET="${TJS_MIPS_TARGET:-mipsel-linux-musleabi}"
	ARCH_HOST="${TJS_MIPS_HOST:-mipsel-linux-musleabi}"
	BUILD_LIBFFI_FROM_SOURCE="${TJS_BUILD_LIBFFI_FROM_SOURCE:-ON}"
	if [[ "${LINK_MODE}" == "dynamic" ]]; then
		LIBFFI_LINK_MODE="${TJS_LIBFFI_LINK_MODE:-shared}"
		BUILD_DIR_DEFAULT="${ROOT_DIR}/build-mipsel-dyn"
		BUILD_WITH_MIMALLOC="${BUILD_WITH_MIMALLOC:-OFF}"
		TJS_DISABLE_PROMISE_TRACKER="${TJS_DISABLE_PROMISE_TRACKER:-ON}"
	else
		LIBFFI_LINK_MODE="${TJS_LIBFFI_LINK_MODE:-static}"
		BUILD_DIR_DEFAULT="${ROOT_DIR}/build-mipsel"
		BUILD_WITH_MIMALLOC="${BUILD_WITH_MIMALLOC:-ON}"
		TJS_DISABLE_PROMISE_TRACKER="${TJS_DISABLE_PROMISE_TRACKER:-OFF}"
	fi
	TJS_DISABLE_ADA_URL="${TJS_DISABLE_ADA_URL:-OFF}"
	TJS_USE_SYSROOT_LIBUV="${TJS_USE_SYSROOT_LIBUV:-OFF}"
	TJS_USE_DEPS_LIBUV_SHARED="${TJS_USE_DEPS_LIBUV_SHARED:-OFF}"
	TJS_WAMR_SIMD="${TJS_WAMR_SIMD:-OFF}"
	TJS_DISABLE_WASM="${TJS_DISABLE_WASM:-OFF}"
	TJS_BUILD_WASM_NATIVE_MODULE="${TJS_BUILD_WASM_NATIVE_MODULE:-ON}"
	TJS_ENABLE_LVGL="${TJS_ENABLE_LVGL:-OFF}"
	BUILD_ONLY_LIBFFI="${TJS_MIPS_BUILD_ONLY_LIBFFI:-OFF}"
	if [[ "${LIBFFI_LINK_MODE}" == "static" ]]; then
		FFI_LIB_DEFAULT="${SYSROOT}/usr/lib/libffi.a"
	else
		FFI_LIB_DEFAULT=""
	fi
	STRIP_ARCH_CANDIDATE="mipsel-openwrt-linux-strip"
fi

if [[ "${TARGET_ARCH}" == "windows" ]]; then
	SYSROOT_DEFAULT=""
	LINK_MODE="${TJS_LINK_MODE:-static}"
	SYSROOT="${TJS_SYSROOT:-${SYSROOT_DEFAULT}}"
	TOOLCHAIN_FILE="${ROOT_DIR}/cmake/toolchains/x86_64-windows-gnu-zig.cmake"
	ARCH_TARGET="${TJS_WIN_TARGET:-x86_64-windows-gnu}"
	ARCH_HOST="${TJS_WIN_HOST:-x86_64-w64-mingw32}"
	BUILD_LIBFFI_FROM_SOURCE="${TJS_BUILD_LIBFFI_FROM_SOURCE:-ON}"
	LIBFFI_LINK_MODE="${TJS_LIBFFI_LINK_MODE:-static}"
	if [[ "${LIBFFI_LINK_MODE}" != "static" ]]; then
		echo "Warning: Windows cross-build on this script currently supports static libffi only."
		echo "         Overriding TJS_LIBFFI_LINK_MODE='${LIBFFI_LINK_MODE}' -> 'static'."
		LIBFFI_LINK_MODE="static"
	fi
	if [[ "${LINK_MODE}" == "dynamic" ]]; then
		BUILD_DIR_DEFAULT="${ROOT_DIR}/build-windows-dyn"
	else
		BUILD_DIR_DEFAULT="${ROOT_DIR}/build-windows"
	fi
	BUILD_WITH_MIMALLOC="${BUILD_WITH_MIMALLOC:-ON}"
	TJS_DISABLE_ADA_URL="${TJS_DISABLE_ADA_URL:-OFF}"
	TJS_DISABLE_PROMISE_TRACKER="${TJS_DISABLE_PROMISE_TRACKER:-OFF}"
	TJS_USE_SYSROOT_LIBUV="${TJS_USE_SYSROOT_LIBUV:-OFF}"
	TJS_USE_DEPS_LIBUV_SHARED="${TJS_USE_DEPS_LIBUV_SHARED:-OFF}"
	TJS_WAMR_SIMD="${TJS_WAMR_SIMD:-OFF}"
	TJS_DISABLE_WASM="${TJS_DISABLE_WASM:-ON}"
	TJS_BUILD_WASM_NATIVE_MODULE="${TJS_BUILD_WASM_NATIVE_MODULE:-OFF}"
	TJS_ENABLE_LVGL="${TJS_ENABLE_LVGL:-OFF}"
	TJS_DISABLE_NATIVE_MODULES="${TJS_DISABLE_NATIVE_MODULES:-ON}"
	BUILD_ONLY_LIBFFI="${TJS_WINDOWS_BUILD_ONLY_LIBFFI:-OFF}"
	FFI_LIB_DEFAULT=""
	STRIP_ARCH_CANDIDATE="x86_64-w64-mingw32-strip"
fi
if [[ "${TARGET_ARCH}" != "windows" ]]; then
	TJS_DISABLE_NATIVE_MODULES="${TJS_DISABLE_NATIVE_MODULES:-OFF}"
fi

BUILD_DIR="${1:-${BUILD_DIR_DEFAULT}}"
FFI_INCLUDE_DIR="${BUILD_DIR}/ffi-include"
FFI_LIB="${TJS_FFI_LIB:-${FFI_LIB_DEFAULT}}"
LIBFFI_DEFAULT_VERSION="3.5.2"
LIBFFI_DEFAULT_SRC_DIR="${ROOT_DIR}/deps_lib/libffi-${LIBFFI_DEFAULT_VERSION}"
LIBFFI_DEFAULT_TARBALL="${ROOT_DIR}/deps_lib/libffi-${LIBFFI_DEFAULT_VERSION}.tar.gz"
LIBFFI_DEFAULT_SOURCE_URL="https://github.com/libffi/libffi/releases/download/v${LIBFFI_DEFAULT_VERSION}/libffi-${LIBFFI_DEFAULT_VERSION}.tar.gz"
LIBFFI_DEFAULT_SOURCE_SHA256="f3a3082a23b37c293a4fcd1053147b371f2ff91fa7ea1b2a52e335676bac82dc"

mkdir -p "${ROOT_DIR}/.cache/zig-local" "${ROOT_DIR}/.cache/zig-global"
export ZIG_LOCAL_CACHE_DIR="${ROOT_DIR}/.cache/zig-local"
export ZIG_GLOBAL_CACHE_DIR="${ROOT_DIR}/.cache/zig-global"

if [[ -n "${SYSROOT}" && -f "${SYSROOT}/usr/include/ffi.h" && -f "${SYSROOT}/usr/include/ffitarget.h" ]]; then
	mkdir -p "${FFI_INCLUDE_DIR}"
	cp "${SYSROOT}/usr/include/ffi.h" "${FFI_INCLUDE_DIR}/ffi.h"
	cp "${SYSROOT}/usr/include/ffitarget.h" "${FFI_INCLUDE_DIR}/ffitarget.h"
fi

# For armhf without sysroot, use explicit TJS_ARMHF_FFI_INCLUDE or libffi-install
if [[ -z "${SYSROOT}" && ! -f "${FFI_INCLUDE_DIR}/ffi.h" ]]; then
	if [[ -n "${TJS_ARMHF_FFI_INCLUDE:-}" && -f "${TJS_ARMHF_FFI_INCLUDE}/ffi.h" ]]; then
		FFI_INCLUDE_DIR="${TJS_ARMHF_FFI_INCLUDE}"
	else
		LIBFFI_INSTALL_DIR="${BUILD_DIR}/libffi-install"
		if [[ -f "${LIBFFI_INSTALL_DIR}/include/ffi.h" && -f "${LIBFFI_INSTALL_DIR}/include/ffitarget.h" ]]; then
			mkdir -p "${FFI_INCLUDE_DIR}"
			cp "${LIBFFI_INSTALL_DIR}/include/ffi.h" "${FFI_INCLUDE_DIR}/ffi.h"
			cp "${LIBFFI_INSTALL_DIR}/include/ffitarget.h" "${FFI_INCLUDE_DIR}/ffitarget.h"
		fi
		if [[ -z "${FFI_LIB}" ]]; then
			for candidate in \
				"${LIBFFI_INSTALL_DIR}/lib/libffi.so" \
				"${LIBFFI_INSTALL_DIR}/lib/libffi.so.8" \
				"${LIBFFI_INSTALL_DIR}/lib/libffi.so.7" \
				"${LIBFFI_INSTALL_DIR}/lib/libffi.a"; do
				if [[ -f "${candidate}" ]]; then
					FFI_LIB="${candidate}"
					break
				fi
			done
		fi
	fi
fi

# For armhf without sysroot, use explicit TJS_ARMHF_FFI_INCLUDE or libffi-install
if [[ -z "${SYSROOT}" && ! -f "${FFI_INCLUDE_DIR}/ffi.h" ]]; then
	if [[ -n "${TJS_ARMHF_FFI_INCLUDE:-}" && -f "${TJS_ARMHF_FFI_INCLUDE}/ffi.h" ]]; then
		FFI_INCLUDE_DIR="${TJS_ARMHF_FFI_INCLUDE}"
	else
		LIBFFI_INSTALL_DIR="${BUILD_DIR}/libffi-install"
		if [[ -f "${LIBFFI_INSTALL_DIR}/include/ffi.h" && -f "${LIBFFI_INSTALL_DIR}/include/ffitarget.h" ]]; then
			mkdir -p "${FFI_INCLUDE_DIR}"
			cp "${LIBFFI_INSTALL_DIR}/include/ffi.h" "${FFI_INCLUDE_DIR}/ffi.h"
			cp "${LIBFFI_INSTALL_DIR}/include/ffitarget.h" "${FFI_INCLUDE_DIR}/ffitarget.h"
		fi
		if [[ -z "${FFI_LIB}" ]]; then
			for candidate in \
				"${LIBFFI_INSTALL_DIR}/lib/libffi.so" \
				"${LIBFFI_INSTALL_DIR}/lib/libffi.so.8" \
				"${LIBFFI_INSTALL_DIR}/lib/libffi.so.7" \
				"${LIBFFI_INSTALL_DIR}/lib/libffi.a"; do
				if [[ -f "${candidate}" ]]; then
					FFI_LIB="${candidate}"
					break
				fi
			done
		fi
	fi
fi

if [[ "${TARGET_ARCH}" == "aarch64" && -z "${FFI_LIB}" ]]; then
	for candidate in \
		"${SYSROOT}/usr/lib/libffi.so" \
		"${SYSROOT}/usr/lib/libffi.so.8" \
		"${SYSROOT}/usr/lib/libffi.so.7" \
		"${SYSROOT}/usr/lib/libffi.a"; do
		if [[ -f "${candidate}" ]]; then
			FFI_LIB="${candidate}"
			break
		fi
	done
elif [[ "${TARGET_ARCH}" == "windows" && -z "${FFI_LIB}" ]]; then
	for candidate in \
		"${BUILD_DIR}/libffi-install/lib/libffi.a" \
		"${BUILD_DIR}/libffi-install/lib64/libffi.a"; do
		if [[ -f "${candidate}" ]]; then
			FFI_LIB="${candidate}"
			break
		fi
	done
elif [[ "${TARGET_ARCH}" == "mipsel" ]]; then
	# mipsel sysroot doesn't ship libffi; verify the pre-set path actually exists
	if [[ ! -f "${FFI_LIB}" ]]; then
		FFI_LIB=""
	fi
fi

# Verify FFI_LIB points to a real file before skipping source build
if [[ -n "${FFI_LIB}" && ! -f "${FFI_LIB}" ]]; then
	FFI_LIB=""
fi

if [[ -z "${FFI_LIB}" && "${BUILD_LIBFFI_FROM_SOURCE}" == "ON" ]]; then
	LIBFFI_SRC_DIR="${TJS_LIBFFI_SRC_DIR:-${LIBFFI_DEFAULT_SRC_DIR}}"
	LIBFFI_TARBALL="${TJS_LIBFFI_TARBALL:-${LIBFFI_DEFAULT_TARBALL}}"
	LIBFFI_SOURCE_URL="${TJS_LIBFFI_SOURCE_URL-${LIBFFI_DEFAULT_SOURCE_URL}}"
	LIBFFI_SOURCE_SHA256="${TJS_LIBFFI_SOURCE_SHA256-${LIBFFI_DEFAULT_SOURCE_SHA256}}"
	LIBFFI_BUILD_DIR="${BUILD_DIR}/libffi-build"
	LIBFFI_INSTALL_DIR="${BUILD_DIR}/libffi-install"
	LIBFFI_STAMP="${LIBFFI_INSTALL_DIR}/.built"
	LIBFFI_BUILT_LIB=""
	if [[ "${LIBFFI_LINK_MODE}" == "static" ]]; then
		LIBFFI_CANDIDATES=(
			"${LIBFFI_INSTALL_DIR}/lib/libffi.a"
			"${LIBFFI_INSTALL_DIR}/lib64/libffi.a"
			"${LIBFFI_INSTALL_DIR}/lib/libffi.dll.a"
			"${LIBFFI_INSTALL_DIR}/lib64/libffi.dll.a"
		)
	else
		LIBFFI_CANDIDATES=(
			"${LIBFFI_INSTALL_DIR}/lib/libffi.so"
			"${LIBFFI_INSTALL_DIR}/lib/libffi.so.8"
			"${LIBFFI_INSTALL_DIR}/lib/libffi.so.7"
			"${LIBFFI_INSTALL_DIR}/lib64/libffi.so"
			"${LIBFFI_INSTALL_DIR}/lib64/libffi.so.8"
			"${LIBFFI_INSTALL_DIR}/lib64/libffi.so.7"
		)
	fi
	for candidate in "${LIBFFI_CANDIDATES[@]}"; do
		if [[ -f "${candidate}" ]]; then
			LIBFFI_BUILT_LIB="${candidate}"
			break
		fi
	done

	if [[ ! -f "${LIBFFI_STAMP}" || -z "${LIBFFI_BUILT_LIB}" || ! -f "${LIBFFI_INSTALL_DIR}/include/ffi.h" ]]; then
		ensure_libffi_source_dir "${LIBFFI_SRC_DIR}" "${LIBFFI_TARBALL}" "${LIBFFI_SOURCE_URL}" "${LIBFFI_SOURCE_SHA256}"

		echo ""
		echo "=== Building libffi from source (${LIBFFI_SRC_DIR}) ==="
		LIBFFI_CONFIGURE_FLAGS=(--enable-shared --disable-static)
		if [[ "${LIBFFI_LINK_MODE}" == "static" ]]; then
			LIBFFI_CONFIGURE_FLAGS=(--disable-shared --enable-static)
		fi
		rm -rf "${LIBFFI_BUILD_DIR}" "${LIBFFI_INSTALL_DIR}"
		mkdir -p "${LIBFFI_BUILD_DIR}" "${LIBFFI_INSTALL_DIR}"
		pushd "${LIBFFI_BUILD_DIR}" >/dev/null
		CC="zig cc -target ${ARCH_TARGET}" \
		CXX="zig c++ -target ${ARCH_TARGET}" \
		AR="zig ar" \
		RANLIB="zig ranlib" \
		CFLAGS="-O2" \
		CXXFLAGS="-O2" \
			"${LIBFFI_SRC_DIR}/configure" \
				--host="${ARCH_HOST}" \
				--prefix="${LIBFFI_INSTALL_DIR}" \
				"${LIBFFI_CONFIGURE_FLAGS[@]}"
			if [[ "${LIBFFI_LINK_MODE}" == "static" ]]; then
				if [[ "${TARGET_ARCH}" == "windows" ]]; then
					build_libffi_static_windows_fallback() {
						echo "libffi static build via libtool failed on Windows target, creating libffi.a from objects."
						make -j "$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)" -k >/dev/null 2>&1 || true
						LIBFFI_OBJS=()
						while IFS= read -r obj; do
							LIBFFI_OBJS+=("${obj}")
						done < <(find src -type f -name '*.obj' | sort)
						if [[ "${#LIBFFI_OBJS[@]}" -eq 0 ]]; then
							echo "Error: no libffi object files found for Windows static fallback."
							exit 1
						fi
						mkdir -p "${LIBFFI_INSTALL_DIR}/lib" "${LIBFFI_INSTALL_DIR}/include"
						zig ar rcs "${LIBFFI_INSTALL_DIR}/lib/libffi.a" "${LIBFFI_OBJS[@]}"
						zig ranlib "${LIBFFI_INSTALL_DIR}/lib/libffi.a"
						cp -f include/ffi.h "${LIBFFI_INSTALL_DIR}/include/ffi.h"
						cp -f include/ffitarget.h "${LIBFFI_INSTALL_DIR}/include/ffitarget.h"
					}

					set +e
					make -j "$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)"
					LIBFFI_MAKE_RC=$?
					set -e
					if [[ "${LIBFFI_MAKE_RC}" -eq 0 ]]; then
						set +e
						make install
						LIBFFI_INSTALL_RC=$?
						set -e
						if [[ "${LIBFFI_INSTALL_RC}" -ne 0 ]]; then
							build_libffi_static_windows_fallback
						fi
					else
						build_libffi_static_windows_fallback
					fi
				else
					make -j "$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)"
					make install
				fi
			else
				build_libffi_shared_fallback() {
					echo "libffi shared link/install via autotools failed, falling back to manual .so link."
				make -j "$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)" libffi_convenience.la
				LIBFFI_OBJS=(
					src/.libs/prep_cif.o
					src/.libs/types.o
					src/.libs/raw_api.o
					src/.libs/java_raw_api.o
					src/.libs/closures.o
					src/.libs/tramp.o
				)
			if [[ "${TARGET_ARCH}" == "aarch64" ]]; then
					LIBFFI_OBJS+=(src/aarch64/.libs/ffi.o src/aarch64/.libs/sysv.o)
					SONAME=libffi.so.8
					REALNAME=libffi.so.8.2.0
				elif [[ "${TARGET_ARCH}" == "armv7" || "${TARGET_ARCH}" == "armhf" ]]; then
					LIBFFI_OBJS+=(src/arm/.libs/ffi.o src/arm/.libs/sysv.o)
					SONAME=libffi.so.8
					REALNAME=libffi.so.8.2.0
				else
					LIBFFI_OBJS+=(src/mips/.libs/ffi.o src/mips/.libs/o32.o)
					SONAME=libffi.so.8
					REALNAME=libffi.so.8.2.0
				fi
				for obj in "${LIBFFI_OBJS[@]}"; do
					if [[ ! -f "${obj}" ]]; then
						echo "Error: missing libffi object ${obj}"
						exit 1
					fi
				done
				mkdir -p "${LIBFFI_INSTALL_DIR}/lib" "${LIBFFI_INSTALL_DIR}/include"
				LIBFFI_SHARED_LDFLAGS=()
				if [[ "${TARGET_ARCH}" == "mipsel" ]]; then
					LIBFFI_SHARED_LDFLAGS+=(-Wl,-z,notext)
				fi
				if [[ "${#LIBFFI_SHARED_LDFLAGS[@]}" -gt 0 ]]; then
					zig cc -target "${ARCH_TARGET}" -shared -nostdlib \
						"${LIBFFI_SHARED_LDFLAGS[@]}" \
						-Wl,-soname,"${SONAME}" \
						-o "${LIBFFI_INSTALL_DIR}/lib/${REALNAME}" \
						"${LIBFFI_OBJS[@]}"
				else
					zig cc -target "${ARCH_TARGET}" -shared -nostdlib \
						-Wl,-soname,"${SONAME}" \
						-o "${LIBFFI_INSTALL_DIR}/lib/${REALNAME}" \
						"${LIBFFI_OBJS[@]}"
				fi
				ln -sfn "${REALNAME}" "${LIBFFI_INSTALL_DIR}/lib/${SONAME}"
				ln -sfn "${SONAME}" "${LIBFFI_INSTALL_DIR}/lib/libffi.so"
				cp -f include/ffi.h "${LIBFFI_INSTALL_DIR}/include/ffi.h"
				cp -f include/ffitarget.h "${LIBFFI_INSTALL_DIR}/include/ffitarget.h"
			}

			set +e
			make -j "$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)"
			LIBFFI_MAKE_RC=$?
			set -e
			if [[ "${LIBFFI_MAKE_RC}" -eq 0 && ! -f ".libs/libffi.so.8.2.0" ]]; then
				echo "libffi autotools build completed without the shared object; using manual shared link fallback."
				LIBFFI_MAKE_RC=1
			fi
			if [[ "${LIBFFI_MAKE_RC}" -eq 0 ]]; then
				set +e
				make install
				LIBFFI_INSTALL_RC=$?
				set -e
				if [[ "${LIBFFI_INSTALL_RC}" -ne 0 ]]; then
					build_libffi_shared_fallback
				fi
			else
				build_libffi_shared_fallback
			fi
		fi
		popd >/dev/null
		date +%s > "${LIBFFI_STAMP}"
	fi

	FFI_INCLUDE_DIR="${LIBFFI_INSTALL_DIR}/include"
	for candidate in "${LIBFFI_CANDIDATES[@]}"; do
		if [[ -f "${candidate}" ]]; then
			FFI_LIB="${candidate}"
			break
		fi
	done

	for libdir in "${LIBFFI_INSTALL_DIR}/lib" "${LIBFFI_INSTALL_DIR}/lib64"; do
		if [[ -d "${libdir}" ]]; then
			for candidate in "${libdir}"/libffi.so*; do
				if [[ -e "${candidate}" ]]; then
					cp -fP "${candidate}" "${BUILD_DIR}/$(basename "${candidate}")"
				fi
			done
		fi
	done
fi

if [[ -z "${FFI_LIB}" ]]; then
	echo "Error: libffi not found."
	echo "Set TJS_FFI_LIB=/path/to/libffi.{a,so} or enable source build with TJS_BUILD_LIBFFI_FROM_SOURCE=ON."
	exit 1
fi
if [[ ! -f "${FFI_INCLUDE_DIR}/ffi.h" ]]; then
	echo "Error: ffi.h not found in ${FFI_INCLUDE_DIR}"
	exit 1
fi

if [[ "${BUILD_ONLY_LIBFFI}" == "ON" ]]; then
	echo ""
	echo "=== libffi build completed successfully ==="
	echo "FFI_INCLUDE_DIR=${FFI_INCLUDE_DIR}"
	echo "FFI_LIB=${FFI_LIB}"
	if [[ -f "${FFI_LIB}" ]]; then
		ls -lh "${FFI_LIB}"
		file "${FFI_LIB}" || true
	fi
	for candidate in "${BUILD_DIR}/libffi-install/lib/libffi.so"*; do
		if [[ -e "${candidate}" ]]; then
			ls -lh "${candidate}"
		fi
	done
	exit 0
fi

cmake_args=(
	-S "${ROOT_DIR}"
	-B "${BUILD_DIR}"
	-G Ninja
	-DCMAKE_BUILD_TYPE=Release
	-DCMAKE_TOOLCHAIN_FILE="${TOOLCHAIN_FILE}"
	-DTJS_LINK_MODE="${LINK_MODE}"
	-DTJS_DISABLE_SQLITE3=ON
	-DBUILD_WITH_MIMALLOC="${BUILD_WITH_MIMALLOC}"
	-DTJS_DISABLE_ADA_URL="${TJS_DISABLE_ADA_URL}"
	-DTJS_DISABLE_PROMISE_TRACKER="${TJS_DISABLE_PROMISE_TRACKER}"
	-DTJS_USE_SYSROOT_LIBUV="${TJS_USE_SYSROOT_LIBUV}"
	-DTJS_USE_DEPS_LIBUV_SHARED="${TJS_USE_DEPS_LIBUV_SHARED}"
	-DTJS_WAMR_SIMD="${TJS_WAMR_SIMD}"
	-DTJS_DISABLE_WASM="${TJS_DISABLE_WASM}"
	-DTJS_BUILD_WASM_NATIVE_MODULE="${TJS_BUILD_WASM_NATIVE_MODULE}"
	-DTJS_ENABLE_LVGL="${TJS_ENABLE_LVGL}"
	-DTJS_DISABLE_NATIVE_MODULES="${TJS_DISABLE_NATIVE_MODULES}"
)

if [[ "${TARGET_ARCH}" == "windows" ]]; then
	# Disable strict warning errors for Windows cross-builds
	cmake_args+=(-DDISABLE_WERROR=ON)
fi
if [[ "${TARGET_ARCH}" == "aarch64" ]]; then
	cmake_args+=(-DTJS_AARCH64_SYSROOT="${SYSROOT}")
elif [[ "${TARGET_ARCH}" == "armv7" ]]; then
	cmake_args+=(-DTJS_ARM_SYSROOT="${SYSROOT}")
elif [[ "${TARGET_ARCH}" == "armhf" ]]; then
	: # armhf uses no-sysroot toolchain
elif [[ "${TARGET_ARCH}" == "mipsel" ]]; then
	cmake_args+=(-DTJS_MIPS_SYSROOT="${SYSROOT}")
fi
if [[ -d "${FFI_INCLUDE_DIR}" ]]; then
	cmake_args+=(-DFFI_INCLUDE_DIR="${FFI_INCLUDE_DIR}")
fi
if [[ -n "${FFI_LIB}" ]]; then
	cmake_args+=(-DFFI_LIB="${FFI_LIB}")
fi
if [[ "${BUILD_WITH_MIMALLOC}" == "ON" ]]; then
	cmake_args+=(-DMI_OPT_ARCH=OFF -DMI_NO_OPT_ARCH=ON)
fi

# Build time64 stub library BEFORE cmake build for ARM targets (for old musl compatibility)
TIME64_STUB_LIB=""
if [[ "${TARGET_ARCH}" == "armv7" ]]; then
	echo ""
	echo "=== Building time64 stub library ==="
	mkdir -p "${BUILD_DIR}" "${ROOT_DIR}/.cache/zig-local" "${ROOT_DIR}/.cache/zig-global"
	TIME64_STUB_LIB="${BUILD_DIR}/libtime64_stub.so"
	ZIG_LOCAL_CACHE_DIR="${ROOT_DIR}/.cache/zig-local" \
	ZIG_GLOBAL_CACHE_DIR="${ROOT_DIR}/.cache/zig-global" \
	zig cc -target "${ARCH_TARGET}" \
		-fPIC -shared -w \
		-o "${TIME64_STUB_LIB}" \
		"${ROOT_DIR}/cmake/toolchains/time64_stub.c"
	cmake_args+=(-DTJS_TIME64_STUB_LIB="${TIME64_STUB_LIB}")
fi

cmake "${cmake_args[@]}"

if [[ "${TARGET_ARCH}" == "windows" ]]; then
	build_targets=(xuan-cli)
	# Windows cross-build doesn't support native modules or WAMR
else
	build_targets=(xuan-cli xuan-sqlite serial-shim)
	if [[ "${TJS_BUILD_WASM_NATIVE_MODULE}" == "ON" ]]; then
		build_targets+=(xuan-wasm)
	fi
	if [[ "${TJS_ENABLE_LVGL}" == "ON" ]]; then
		build_targets+=(xuan-lvgl)
	fi
fi
cmake --build "${BUILD_DIR}" -j "$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)" --target "${build_targets[@]}"

echo ""
echo "=== Stripping binaries ==="
if command -v llvm-strip &>/dev/null; then
	STRIP_CMD="llvm-strip -s"
elif command -v /opt/homebrew/opt/llvm/bin/llvm-strip &>/dev/null; then
	STRIP_CMD="/opt/homebrew/opt/llvm/bin/llvm-strip -s"
elif command -v "${STRIP_ARCH_CANDIDATE}" &>/dev/null; then
	STRIP_CMD="${STRIP_ARCH_CANDIDATE}"
elif command -v strip &>/dev/null; then
	STRIP_CMD="strip"
else
	echo "Warning: No strip command found, skipping strip step"
	STRIP_CMD=""
fi

if [[ -n "${STRIP_CMD}" ]]; then
	UNSTRIPPED_DIR="${BUILD_DIR}/unstripped"
	mkdir -p "${UNSTRIPPED_DIR}"

	if [[ "${TARGET_ARCH}" == "windows" ]]; then
		if [[ -f "${BUILD_DIR}/xuan.exe" ]]; then
			SIZE_BEFORE=$(stat -f%z "${BUILD_DIR}/xuan.exe" 2>/dev/null || stat -c%s "${BUILD_DIR}/xuan.exe" 2>/dev/null || echo "unknown")
			cp "${BUILD_DIR}/xuan.exe" "${UNSTRIPPED_DIR}/xuan.exe"
			echo "  Stripping ${BUILD_DIR}/xuan.exe (size before: ${SIZE_BEFORE} bytes)"
			${STRIP_CMD} "${BUILD_DIR}/xuan.exe" 2>/dev/null || echo "    Warning: Failed to strip xuan.exe"
			SIZE_AFTER=$(stat -f%z "${BUILD_DIR}/xuan.exe" 2>/dev/null || stat -c%s "${BUILD_DIR}/xuan.exe" 2>/dev/null || echo "unknown")
			echo "  Size after: ${SIZE_AFTER} bytes"
		fi
	else
		if [[ -f "${BUILD_DIR}/xuan" ]]; then
			SIZE_BEFORE=$(stat -f%z "${BUILD_DIR}/xuan" 2>/dev/null || stat -c%s "${BUILD_DIR}/xuan" 2>/dev/null || echo "unknown")
			cp "${BUILD_DIR}/xuan" "${UNSTRIPPED_DIR}/xuan"
			echo "  Stripping ${BUILD_DIR}/xuan (size before: ${SIZE_BEFORE} bytes)"
			${STRIP_CMD} "${BUILD_DIR}/xuan" 2>/dev/null || echo "    Warning: Failed to strip xuan"
			SIZE_AFTER=$(stat -f%z "${BUILD_DIR}/xuan" 2>/dev/null || stat -c%s "${BUILD_DIR}/xuan" 2>/dev/null || echo "unknown")
			echo "  Size after: ${SIZE_AFTER} bytes"
		fi
	fi

	if [[ "${TARGET_ARCH}" != "windows" ]]; then
		for lib in "${BUILD_DIR}/libada.so" "${BUILD_DIR}/deps/ada/libada.so" "${BUILD_DIR}/libserial-shim.so" "${BUILD_DIR}/xuan-sqlite.so" "${BUILD_DIR}/xuan-wasm.so" "${BUILD_DIR}/xuan-lvgl.so"; do
			if [[ -f "${lib}" ]]; then
				lib_name=$(basename "${lib}")
				SIZE_BEFORE=$(stat -f%z "${lib}" 2>/dev/null || stat -c%s "${lib}" 2>/dev/null || echo "unknown")
				cp "${lib}" "${UNSTRIPPED_DIR}/${lib_name}"
				echo "  Stripping ${lib} (size before: ${SIZE_BEFORE} bytes)"
				${STRIP_CMD} "${lib}" 2>/dev/null || echo "    Warning: Failed to strip ${lib}"
				SIZE_AFTER=$(stat -f%z "${lib}" 2>/dev/null || stat -c%s "${lib}" 2>/dev/null || echo "unknown")
				echo "  Size after: ${SIZE_AFTER} bytes"
			fi
		done
		# Strip libuv from deps/libuv build dir (may exist at multiple paths)
		for uv_lib in "${BUILD_DIR}/deps/libuv/libuv.so.1.0.0" "${BUILD_DIR}/libuv.so.1.0.0"; do
			if [[ -f "${uv_lib}" ]]; then
				SIZE_BEFORE=$(stat -f%z "${uv_lib}" 2>/dev/null || stat -c%s "${uv_lib}" 2>/dev/null || echo "unknown")
				cp "${uv_lib}" "${UNSTRIPPED_DIR}/libuv.so.1.0.0"
				echo "  Stripping ${uv_lib} (size before: ${SIZE_BEFORE} bytes)"
				${STRIP_CMD} "${uv_lib}" 2>/dev/null || echo "    Warning: Failed to strip ${uv_lib}"
				SIZE_AFTER=$(stat -f%z "${uv_lib}" 2>/dev/null || stat -c%s "${uv_lib}" 2>/dev/null || echo "unknown")
				echo "  Size after: ${SIZE_AFTER} bytes"
				break
			fi
		done
	fi
fi

echo ""
echo "=== Build completed successfully ==="
if [[ "${TARGET_ARCH}" == "windows" ]]; then
	echo "Output: ${BUILD_DIR}/xuan.exe"
	if [[ -f "${BUILD_DIR}/xuan.exe" ]]; then
		ls -lh "${BUILD_DIR}/xuan.exe"
		file "${BUILD_DIR}/xuan.exe"
	fi
else
	echo "Output: ${BUILD_DIR}/xuan"
	if [[ -f "${BUILD_DIR}/xuan" ]]; then
		ls -lh "${BUILD_DIR}/xuan"
		file "${BUILD_DIR}/xuan"
	fi
fi
if [[ -f "${BUILD_DIR}/xuan-sqlite.so" ]]; then
	echo "Output: ${BUILD_DIR}/xuan-sqlite.so"
	ls -lh "${BUILD_DIR}/xuan-sqlite.so"
	file "${BUILD_DIR}/xuan-sqlite.so"
fi
if [[ -f "${BUILD_DIR}/xuan-sqlite.dll" ]]; then
	echo "Output: ${BUILD_DIR}/xuan-sqlite.dll"
	ls -lh "${BUILD_DIR}/xuan-sqlite.dll"
	file "${BUILD_DIR}/xuan-sqlite.dll"
fi
if [[ -f "${BUILD_DIR}/xuan-wasm.so" ]]; then
	echo "Output: ${BUILD_DIR}/xuan-wasm.so"
	ls -lh "${BUILD_DIR}/xuan-wasm.so"
	file "${BUILD_DIR}/xuan-wasm.so"
fi

echo ""
echo "=== Assembling dist ==="
TJS_SYSROOT="${SYSROOT}" "${ROOT_DIR}/scripts/assemble-dist.sh" "${BUILD_DIR}"
