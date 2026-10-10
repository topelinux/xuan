#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${1:-${ROOT_DIR}/build}"
DIST_DIR="${BUILD_DIR}/dist"
SYSROOT="${TJS_SYSROOT:-${TJS_MIPS_SYSROOT:-}}"

mkdir -p "${DIST_DIR}/bin" "${DIST_DIR}/lib" "${DIST_DIR}/node_modules/sqlite/native" "${DIST_DIR}/node_modules/wasm/native"

copy_if_exists() {
    local src="$1"
    local dst="$2"
    if [[ -f "${src}" ]]; then
        mkdir -p "$(dirname "${dst}")"
        cp -f "${src}" "${dst}"
    fi
}

copy_path_preserve_link() {
    local src="$1"
    local dst_dir="$2"
    local name
    name="$(basename "${src}")"
    mkdir -p "${dst_dir}"
    # Dereference symlinks so dist works on any filesystem
    # (e.g. target devices pushed via adb that may not support symlinks)
    cp -fL "${src}" "${dst_dir}/${name}"
}

copy_soname_family() {
    local src="$1"
    local src_dir
    local src_base
    src_dir="$(dirname "${src}")"
    src_base="$(basename "${src}")"

    # Copy the real file if it's not a symlink, or the target if it is
    local real_src
    real_src="$(readlink -f "${src}")"
    copy_if_exists "${real_src}" "${DIST_DIR}/lib/$(basename "${real_src}")"

    # Create symlinks for the .so chain (e.g. libuv.so -> libuv.so.1 -> libuv.so.1.0.0)
    if [[ "${src_base}" == *.so* ]]; then
        local soname_root="${src_base%%.so*}.so"
        local real_name
        real_name="$(basename "${real_src}")"
        for candidate in "${src_dir}/${soname_root}"*; do
            if [[ -e "${candidate}" ]]; then
                local link_name
                link_name="$(basename "${candidate}")"
                if [[ "${link_name}" != "${real_name}" && -L "${candidate}" ]]; then
                    # Skip dev symlinks (e.g. libuv.so) — only needed at link time
                    if [[ "${link_name}" == *.so && "${link_name}" != *.so.* ]]; then
                        continue
                    fi
                    # It's a symlink - recreate it pointing to the real file
                    ln -sf "${real_name}" "${DIST_DIR}/lib/${link_name}"
                fi
            fi
        done
    fi
}

resolve_readelf() {
    if command -v llvm-readelf >/dev/null 2>&1; then
        echo "llvm-readelf"
        return
    fi
    if command -v readelf >/dev/null 2>&1; then
        echo "readelf"
        return
    fi
    echo ""
}

collect_needed_lib() {
    local soname="$1"
    local found=""
    if [[ -f "${BUILD_DIR}/${soname}" ]]; then
        found="${BUILD_DIR}/${soname}"
    elif [[ -f "${BUILD_DIR}/deps/libuv/${soname}" ]]; then
        found="${BUILD_DIR}/deps/libuv/${soname}"
    elif [[ -f "${BUILD_DIR}/deps/ada/${soname}" ]]; then
        found="${BUILD_DIR}/deps/ada/${soname}"
    elif [[ -n "${SYSROOT}" && -f "${SYSROOT}/usr/lib/${soname}" ]]; then
        found="${SYSROOT}/usr/lib/${soname}"
    elif [[ -f "/usr/lib/${soname}" ]]; then
        found="/usr/lib/${soname}"
    elif [[ -f "/usr/local/lib/${soname}" ]]; then
        found="/usr/local/lib/${soname}"
    fi
    if [[ -n "${found}" ]]; then
        copy_soname_family "${found}"
    fi
}

rm -rf "${DIST_DIR}"
mkdir -p "${DIST_DIR}/bin" "${DIST_DIR}/lib" "${DIST_DIR}/node_modules/sqlite/native" "${DIST_DIR}/node_modules/wasm/native" "${DIST_DIR}/node_modules/lvgl/native" "${DIST_DIR}/node_modules/lvgljs"

# Detect Windows cross-build (xuan.exe exists)
TJS_IS_WINDOWS_BUILD=0
if [[ -f "${BUILD_DIR}/xuan.exe" ]]; then
    TJS_IS_WINDOWS_BUILD=1
fi

# Runtime binary.
if [[ "${TJS_IS_WINDOWS_BUILD}" -eq 1 ]]; then
    # Windows: copy xuan.exe and required DLLs
    copy_if_exists "${BUILD_DIR}/xuan.exe" "${DIST_DIR}/bin/xuan.exe"
else
    if [[ -f "${BUILD_DIR}/xuan" ]]; then
        copy_if_exists "${BUILD_DIR}/xuan" "${DIST_DIR}/bin/xuan"
    elif [[ -f "${BUILD_DIR}/tjs" ]]; then
        copy_if_exists "${BUILD_DIR}/tjs" "${DIST_DIR}/bin/xuan"
    fi
fi

# Native sqlite module package.
copy_if_exists "${ROOT_DIR}/xuan_modules/sqlite/package.json" "${DIST_DIR}/node_modules/sqlite/package.json"
copy_if_exists "${ROOT_DIR}/xuan_modules/sqlite/index.js" "${DIST_DIR}/node_modules/sqlite/index.js"

for ext in so dylib dll; do
    if [[ -f "${BUILD_DIR}/xuan-sqlite.${ext}" ]]; then
        copy_if_exists "${BUILD_DIR}/xuan-sqlite.${ext}" "${DIST_DIR}/node_modules/sqlite/native/xuan-sqlite.${ext}"
    fi
done

# Native wasm module package.
copy_if_exists "${ROOT_DIR}/xuan_modules/wasm/package.json" "${DIST_DIR}/node_modules/wasm/package.json"
copy_if_exists "${ROOT_DIR}/xuan_modules/wasm/index.js" "${DIST_DIR}/node_modules/wasm/index.js"

for ext in so dylib dll; do
    if [[ -f "${BUILD_DIR}/xuan-wasm.${ext}" ]]; then
        copy_if_exists "${BUILD_DIR}/xuan-wasm.${ext}" "${DIST_DIR}/node_modules/wasm/native/xuan-wasm.${ext}"
    fi
done

# Native lvgl module package.
copy_if_exists "${ROOT_DIR}/xuan_modules/lvgl/package.json" "${DIST_DIR}/node_modules/lvgl/package.json"
copy_if_exists "${ROOT_DIR}/xuan_modules/lvgl/index.js" "${DIST_DIR}/node_modules/lvgl/index.js"

for ext in so dylib dll; do
    if [[ -f "${BUILD_DIR}/xuan-lvgl.${ext}" ]]; then
        copy_if_exists "${BUILD_DIR}/xuan-lvgl.${ext}" "${DIST_DIR}/node_modules/lvgl/native/xuan-lvgl.${ext}"
    fi
done

# lvgljs bridge module package.
copy_if_exists "${ROOT_DIR}/xuan_modules/lvgljs/package.json" "${DIST_DIR}/node_modules/lvgljs/package.json"
copy_if_exists "${ROOT_DIR}/xuan_modules/lvgljs/index.js" "${DIST_DIR}/node_modules/lvgljs/index.js"

# Other runtime shared libraries we already produce.
for lib in libserial-shim.so libserial-shim.dylib libserial-shim.dll libada.so libada.dylib libada.dll; do
    copy_if_exists "${BUILD_DIR}/${lib}" "${DIST_DIR}/lib/${lib}"
done
copy_if_exists "${BUILD_DIR}/deps/ada/libada.so" "${DIST_DIR}/lib/libada.so"
copy_if_exists "${BUILD_DIR}/deps/ada/libada.dylib" "${DIST_DIR}/lib/libada.dylib"
copy_if_exists "${BUILD_DIR}/deps/ada/ada.dll" "${DIST_DIR}/lib/ada.dll"
collect_needed_lib "libuv.so.1"
collect_needed_lib "libada.so"
collect_needed_lib "libffi.so.8"

# For ARM target (armv7 only, armhf uses time64-capable libc), collect time64 stub if available
if [[ -f "${BUILD_DIR}/libtime64_stub.so" ]]; then
    copy_if_exists "${BUILD_DIR}/libtime64_stub.so" "${DIST_DIR}/lib/libtime64_stub.so"
fi

# For armhf/armv7 without sysroot, collect libffi from build dir or armhf_root
if [[ -f "${BUILD_DIR}/libffi.so.8" ]]; then
    copy_soname_family "${BUILD_DIR}/libffi.so.8"
elif [[ -f "${BUILD_DIR}/libffi.so" ]]; then
    copy_soname_family "${BUILD_DIR}/libffi.so"
elif [[ -f "${ROOT_DIR}/armhf_root/usr/lib/libffi.so.8" ]]; then
    copy_soname_family "${ROOT_DIR}/armhf_root/usr/lib/libffi.so.8"
elif [[ -f "${ROOT_DIR}/build-armv7-dyn/libffi-install/lib/libffi.so.8" ]]; then
    copy_soname_family "${ROOT_DIR}/build-armv7-dyn/libffi-install/lib/libffi.so.8"
fi

collect_needed_from_binary() {
    local bin="$1"
    if [[ -z "${READELF_BIN}" || ! -f "${bin}" ]]; then
        return
    fi
    while IFS= read -r line; do
        soname="$(echo "${line}" | sed -n 's/.*Shared library: \[\(.*\)\].*/\1/p')"
        if [[ -z "${soname}" ]]; then
            continue
        fi
        case "${soname}" in
            libuv.so*|libffi.so*|libserial-shim.so*|libada.so*)
                collect_needed_lib "${soname}"
                ;;
        esac
    done < <("${READELF_BIN}" -d "${bin}" 2>/dev/null || true)
}

collect_needed_dlls() {
    local bin="$1"
    if [[ -z "${OBJDUMP_BIN}" || ! -f "${bin}" ]]; then
        return
    fi
    while IFS= read -r line; do
        dll_name="$(echo "${line}" | sed -n 's/.*DLL Name: \(.*\)/\1/p')"
        if [[ -z "${dll_name}" ]]; then
            continue
        fi
        dll_lower="$(echo "${dll_name}" | tr '[:upper:]' '[:lower:]')"
        case "${dll_lower}" in
            libuv*|libffi*|libserial-shim*|libada*|ada.dll|xuan-sqlite.dll|xuan-wasm.dll)
                if [[ -f "${BUILD_DIR}/${dll_name}" ]]; then
                    copy_if_exists "${BUILD_DIR}/${dll_name}" "${DIST_DIR}/bin/${dll_name}"
                elif [[ -f "${BUILD_DIR}/deps/libuv/${dll_name}" ]]; then
                    copy_if_exists "${BUILD_DIR}/deps/libuv/${dll_name}" "${DIST_DIR}/bin/${dll_name}"
                elif [[ -f "${BUILD_DIR}/deps/ada/${dll_name}" ]]; then
                    copy_if_exists "${BUILD_DIR}/deps/ada/${dll_name}" "${DIST_DIR}/bin/${dll_name}"
                fi
                ;;
        esac
    done < <("${OBJDUMP_BIN}" -p "${bin}" 2>/dev/null || true)
}

# Collect selected DT_NEEDED deps (best effort).
READELF_BIN="$(resolve_readelf)"
if [[ "${TJS_IS_WINDOWS_BUILD}" -eq 1 ]]; then
    # For Windows, use objdump to find DLL dependencies
    if command -v llvm-objdump >/dev/null 2>&1; then
        OBJDUMP_BIN="llvm-objdump"
    elif command -v objdump >/dev/null 2>&1; then
        OBJDUMP_BIN="objdump"
    else
        OBJDUMP_BIN=""
    fi
    collect_needed_dlls "${DIST_DIR}/bin/xuan.exe"
    for ext in dll; do
        collect_needed_dlls "${DIST_DIR}/node_modules/sqlite/native/xuan-sqlite.${ext}"
        collect_needed_dlls "${DIST_DIR}/node_modules/wasm/native/xuan-wasm.${ext}"
    done
else
    collect_needed_from_binary "${DIST_DIR}/bin/xuan"
    for ext in so dylib dll; do
        collect_needed_from_binary "${DIST_DIR}/node_modules/sqlite/native/xuan-sqlite.${ext}"
        collect_needed_from_binary "${DIST_DIR}/node_modules/wasm/native/xuan-wasm.${ext}"
        collect_needed_from_binary "${DIST_DIR}/node_modules/lvgl/native/xuan-lvgl.${ext}"
    done
fi

echo "dist prepared at ${DIST_DIR}"
find "${DIST_DIR}" -maxdepth 4 \( -type f -o -type l \) | sort
