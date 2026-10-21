#!/usr/bin/env bash
# Build a musl libc/loader for armhf (arm-linux-musleabihf) that can be
# installed on targets whose system musl is too old for xuan's armhf build.
#
# Why this exists
# ---------------
# Zig links xuan against musl 1.2.5.  Old targets (e.g. Alpine 3.12 /
# musl 1.1.24) do not export the time64 symbols (__clock_gettime64,
# __stat_time64, ...) nor pthread_getname_np, so the binary fails at
# relocation time.  Instead of shipping a time64 stub, install a newer
# musl on the target and point /lib/ld-musl-armhf.so.1 at it.
#
# This script produces that loader with two extra fixes:
#
#   1. statx fallback: some vendor kernels (e.g. 4.9 BSP kernels) stub the
#      statx syscall with EINVAL instead of ENOSYS.  musl only falls back to
#      fstatat64 on ENOSYS, which breaks *every* stat() call (including
#      busybox).  We make the fallback also trigger on EINVAL.
#
#   2. compiler-rt visibility: zig's compiler-rt defines weak HIDDEN
#      memcpy/memmove/memset/memcmp/...  When it is linked into libc.so the
#      ELF visibility merge hides musl's strong definitions, so the built
#      libc.so would not export them.  We rename the compiler-rt duplicates
#      before linking.
#
# Usage:
#   ./scripts/build-musl-armhf.sh [OUTPUT_DIR]
#
#   OUTPUT_DIR defaults to build-armhf-musl
#   Result: OUTPUT_DIR/ld-musl-armhf.so.1 (+ libc.musl-armhf.so.1 symlink)
#
# Install on the target (keep a backup!):
#   scp build-armhf-musl/ld-musl-armhf.so.1 root@192.168.1.1:/tmp/
#   ssh root@192.168.1.1
#     cp -p /lib/ld-musl-armhf.so.1 /lib/ld-musl-armhf.so.1.orig
#     cp /tmp/ld-musl-armhf.so.1 /lib/ld-musl-armhf.so.1
#     sync
#
# Verify in /tmp without touching the system first (LD_LIBRARY_PATH alone
# cannot replace musl's loader -- libc.so *is* the loader -- so invoke it
# explicitly):
#   /tmp/ld-musl-armhf.so.1 --library-path /tmp/lib ./bin/xuan --version

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

MUSL_VERSION="${MUSL_VERSION:-1.2.5}"
MUSL_SOURCE_URL="${MUSL_SOURCE_URL:-https://musl.libc.org/releases/musl-${MUSL_VERSION}.tar.gz}"
MUSL_SOURCE_SHA256="${MUSL_SOURCE_SHA256:-a9a118bbe84d8764da0ea0d28b3ab3fae8477fc7e4085d90102b8596fc7c75e4}"
MUSL_TARGET="${TJS_ARMHF_MUSL_TARGET:-arm-linux-musleabihf}"
JOBS="${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)}"

CACHE_DIR="${ROOT_DIR}/.cache/musl-${MUSL_TARGET}-${MUSL_VERSION}"
TARBALL="${CACHE_DIR}/musl-${MUSL_VERSION}.tar.gz"
SRC_DIR="${CACHE_DIR}/musl-${MUSL_VERSION}"
CRT_DIR="${CACHE_DIR}/compiler-rt"
CRT_ARCHIVE="${CACHE_DIR}/libcompiler_rt_fixed.a"

OUT_DIR="${1:-${ROOT_DIR}/build-armhf-musl}"
OUT_FILE="${OUT_DIR}/ld-musl-armhf.so.1"

ZIG="${ZIG:-zig}"
if ! command -v "${ZIG}" >/dev/null 2>&1; then
	echo "Error: '${ZIG}' not found." >&2
	exit 1
fi

# Prefer llvm tools (they handle cross-arch ELF fine on macOS).
find_tool() {
	local name="$1"
	local candidate
	for candidate in \
		"${LLVM_TOOL_DIR:-}/bin/${name}" \
		/opt/homebrew/opt/llvm/bin/"${name}" \
		/usr/local/opt/llvm/bin/"${name}"; do
		if [[ -x "${candidate}" ]]; then
			echo "${candidate}"
			return 0
		fi
	done
	if command -v "${name}" >/dev/null 2>&1; then
		command -v "${name}"
		return 0
	fi
	echo "Error: '${name}' not found. Install LLVM (brew install llvm)." >&2
	exit 1
}

LLVM_AR="$(find_tool llvm-ar)"
LLVM_NM="$(find_tool llvm-nm)"
LLVM_OBJCOPY="$(find_tool llvm-objcopy)"
LLVM_STRIP="$(find_tool llvm-strip)"

sha256_file() {
	local file_path="$1"
	if command -v sha256sum >/dev/null 2>&1; then
		sha256sum "${file_path}" | awk '{print $1}'
	else
		shasum -a 256 "${file_path}" | awk '{print $1}'
	fi
}

fetch_source() {
	mkdir -p "${CACHE_DIR}"
	if [[ ! -f "${TARBALL}" ]]; then
		echo "=== Downloading musl ${MUSL_VERSION} ==="
		local tmp="${TARBALL}.tmp.$$"
		if command -v curl >/dev/null 2>&1; then
			curl -fL --retry 3 --connect-timeout 15 -o "${tmp}" "${MUSL_SOURCE_URL}"
		elif command -v wget >/dev/null 2>&1; then
			wget -O "${tmp}" "${MUSL_SOURCE_URL}"
		else
			echo "Error: neither curl nor wget is available." >&2
			exit 1
		fi
		mv -f "${tmp}" "${TARBALL}"
	fi

	local actual
	actual="$(sha256_file "${TARBALL}")"
	if [[ "${actual}" != "${MUSL_SOURCE_SHA256}" ]]; then
		echo "Error: SHA-256 mismatch for ${TARBALL}" >&2
		echo "  expected: ${MUSL_SOURCE_SHA256}" >&2
		echo "  actual:   ${actual}" >&2
		exit 1
	fi
}

patch_statx_fallback() {
	local fstatat="${SRC_DIR}/src/stat/fstatat.c"
	local old='if (ret!=-ENOSYS) return __syscall_ret(ret);'
	local new='if (ret!=-ENOSYS && ret!=-EINVAL) return __syscall_ret(ret);'

	if grep -qF "${new}" "${fstatat}"; then
		echo "statx EINVAL fallback: already applied"
		return 0
	fi
	if ! grep -qF "${old}" "${fstatat}"; then
		echo "Error: could not find the statx fallback check in ${fstatat}" >&2
		exit 1
	fi
	# Plain sed replacement, avoids needing GNU sed on macOS.
	perl -0pi -e 's/\Q'"${old}"'\E/'"${new}"'/' "${fstatat}"
	echo "statx EINVAL fallback: patched"
}

build_musl() {
	rm -rf "${SRC_DIR}"
	echo "=== Extracting musl ${MUSL_VERSION} ==="
	tar -xzf "${TARBALL}" -C "${CACHE_DIR}"

	patch_statx_fallback

	echo ""
	echo "=== Configuring musl for ${MUSL_TARGET} ==="
	pushd "${SRC_DIR}" >/dev/null
	make distclean >/dev/null 2>&1 || true
	CC="${ZIG} cc -target ${MUSL_TARGET}" \
		./configure \
			--target="${MUSL_TARGET}" \
			--prefix="${SRC_DIR}/install" \
			--syslibdir="${SRC_DIR}/install/lib"

	echo ""
	echo "=== Building musl ==="
	if [[ "${VERBOSE:-0}" == "1" ]]; then
		make -j "${JOBS}" AR="${LLVM_AR}" RANLIB="${LLVM_AR} s"
	else
		make -j "${JOBS}" AR="${LLVM_AR}" RANLIB="${LLVM_AR} s" \
			> "${CACHE_DIR}/musl-build.log" 2>&1 ||
			{ tail -40 "${CACHE_DIR}/musl-build.log"; exit 1; }
	fi
	popd >/dev/null
}

prepare_compiler_rt() {
	echo ""
	echo "=== Patching compiler-rt duplicate symbols ==="

	local crt_archive
	local probe_src="${CACHE_DIR}/probe.c"
	local probe_obj="${CACHE_DIR}/probe.o"
	printf 'int x;\n' > "${probe_src}"
	"${ZIG}" cc -target "${MUSL_TARGET}" -c -o "${probe_obj}" "${probe_src}"
	crt_archive="$("${ZIG}" cc -target "${MUSL_TARGET}" -### \
		-o "${CACHE_DIR}/probe.out" "${probe_obj}" 2>&1 |
		grep -o '[^ ]*libcompiler_rt\.a' | head -1 || true)"
	if [[ -z "${crt_archive}" || ! -f "${crt_archive}" ]]; then
		echo "Error: could not locate zig's libcompiler_rt.a." >&2
		echo "Run 'zig cc -target ${MUSL_TARGET} -### -o /tmp/probe.out /tmp/probe.o' to debug." >&2
		exit 1
	fi

	rm -rf "${CRT_DIR}" "${CRT_ARCHIVE}"
	mkdir -p "${CRT_DIR}"
	pushd "${CRT_DIR}" >/dev/null
	"${LLVM_AR}" x "${crt_archive}"

	local crt_obj
	crt_obj="$(find . -maxdepth 1 -name '*.o' -print -quit)"

	# Symbols defined both by musl's objects and compiler-rt.  compiler-rt
	# marks these weak+HIDDEN; if left alone the final libc.so hides musl's
	# own definitions (memcpy, ceil, __stack_chk_guard, ...).
	pushd "${SRC_DIR}" >/dev/null
	find obj -name '*.lo' -print0 |
		xargs -0 "${LLVM_NM}" --defined-only -A |
		awk -F'[ :]+' 'NF >= 4 { print $NF }' | sort -u > "${CRT_DIR}/musl.syms"
	popd >/dev/null

	"${LLVM_NM}" --defined-only "${crt_obj}" |
		awk '{ print $NF }' | sort -u > "${CRT_DIR}/crt.syms"

	comm -12 "${CRT_DIR}/musl.syms" "${CRT_DIR}/crt.syms" > "${CRT_DIR}/rename.txt"

	local objcopy_args=()
	local sym
	while IFS= read -r sym; do
		[[ -n "${sym}" ]] || continue
		objcopy_args+=(--redefine-sym "${sym}=__zig_crt_${sym}")
	done < "${CRT_DIR}/rename.txt"

	"${LLVM_OBJCOPY}" "${objcopy_args[@]}" "${crt_obj}" "${CRT_DIR}/compiler_rt_fixed.o"
	"${LLVM_AR}" rcs "${CRT_ARCHIVE}" "${CRT_DIR}/compiler_rt_fixed.o"
	echo "Renamed $(wc -l < "${CRT_DIR}/rename.txt" | tr -d ' ') compiler-rt symbols"
	popd >/dev/null
}

link_libc() {
	echo ""
	echo "=== Linking libc.so ==="
	pushd "${SRC_DIR}" >/dev/null
	rm -f lib/libc.so
	"${ZIG}" ld.lld \
		-O3 --entry _dlstart --build-id=none --image-base=0 \
		--sort-section=alignment --gc-sections --eh-frame-hdr \
		-znow -z defs -m armelf_linux_eabi -shared \
		-o lib/libc.so \
		$(find obj -name '*.lo' | sort) \
		"${CRT_ARCHIVE}"
	popd >/dev/null
}

install_result() {
	echo ""
	echo "=== Verifying and installing result ==="
	local libc="${SRC_DIR}/lib/libc.so"
	local required=(__clock_gettime64 __stat_time64 __time64 pthread_getname_np memcpy memmove memset memcmp __stack_chk_guard)
	local exported sym
	exported="$("${LLVM_NM}" -D --defined-only "${libc}" | awk '{print $NF}')"
	for sym in "${required[@]}"; do
		if ! grep -qx "${sym}" <<< "${exported}"; then
			echo "Error: ${sym} is not exported by the built libc.so" >&2
			exit 1
		fi
	done

	mkdir -p "${OUT_DIR}"
	"${LLVM_STRIP}" --strip-all "${libc}" -o "${OUT_FILE}.tmp"
	mv -f "${OUT_FILE}.tmp" "${OUT_FILE}"
	ln -sfn ld-musl-armhf.so.1 "${OUT_DIR}/libc.musl-armhf.so.1"

	echo ""
	echo "Built: ${OUT_FILE}"
	ls -lh "${OUT_FILE}"
	file "${OUT_FILE}" || true
	echo ""
	echo "Install on the target (example):"
	echo "  scp ${OUT_FILE} root@192.168.1.1:/tmp/"
	echo "  ssh root@192.168.1.1 'cp -p /lib/ld-musl-armhf.so.1 /lib/ld-musl-armhf.so.1.orig && \\"
	echo "    cp /tmp/ld-musl-armhf.so.1 /lib/ld-musl-armhf.so.1 && sync'"
}

fetch_source
build_musl
prepare_compiler_rt
link_libc
install_result
