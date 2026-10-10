/*
 * txiki.js
 *
 * Copyright (c) 2019-present Saúl Ibarra Corretgé <s@saghul.net>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "private.h"
#include "tjs.h"
#include "utils.h"

#include <string.h>
#include <sys/stat.h>

#define TJS__PATHSEP_POSIX '/'
#if defined(_WIN32)
#define TJS__PATHSEP     '\\'
#define TJS__PATHSEP_STR "\\"
#else
#define TJS__PATHSEP     '/'
#define TJS__PATHSEP_STR "/"
#endif

#if defined(_WIN32)
#define TJS_NATIVE_SUFFIX_PRIMARY ".dll"
#elif defined(__APPLE__)
#define TJS_NATIVE_SUFFIX_PRIMARY ".dylib"
#else
#define TJS_NATIVE_SUFFIX_PRIMARY ".so"
#endif

static bool tjs__path_contains_segment(const char *path, const char *segment);

#ifndef TJS_DISABLE_NATIVE_MODULES
typedef JSModuleDef *(TJSNativeInitModuleFunc)(JSContext *ctx, const char *module_name);

typedef struct TJSNativeModuleHandle {
    uv_lib_t lib;
    struct TJSNativeModuleHandle *next;
} TJSNativeModuleHandle;

static TJSNativeModuleHandle *tjs_native_module_handles;
#endif

static bool tjs__is_native_module(const char *path) {
    return js__has_suffix(path, ".so") || js__has_suffix(path, ".dylib") || js__has_suffix(path, ".dll");
}

#ifndef TJS_DISABLE_NATIVE_MODULES
static JSModuleDef *tjs__load_native_module(JSContext *ctx, const char *module_name) {
    uv_lib_t lib = {0};
    int ret = uv_dlopen(module_name, &lib);
    if (ret != 0) {
        JS_ThrowReferenceError(ctx, "could not load native module '%s': %s", module_name, uv_dlerror(&lib));
        return NULL;
    }

    void *sym = NULL;
    ret = uv_dlsym(&lib, "js_init_module", &sym);
    if (ret != 0 || sym == NULL) {
        JS_ThrowReferenceError(ctx, "native module '%s': js_init_module not found", module_name);
        uv_dlclose(&lib);
        return NULL;
    }

    TJSNativeInitModuleFunc *init = sym;
    JSModuleDef *m = init(ctx, module_name);
    if (!m) {
        JS_ThrowReferenceError(ctx, "native module '%s': initialization error", module_name);
        uv_dlclose(&lib);
        return NULL;
    }

    TJSNativeModuleHandle *node = js_mallocz(ctx, sizeof(*node));
    if (!node) {
        uv_dlclose(&lib);
        return NULL;
    }
    node->lib = lib;
    node->next = tjs_native_module_handles;
    tjs_native_module_handles = node;

    return m;
}
#endif

static bool tjs__path_is_absolute(const char *path) {
#if defined(_WIN32)
    if (((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z')) && path[1] == ':' &&
        (path[2] == '\\' || path[2] == '/')) {
        return true;
    }
#endif
    return path[0] == TJS__PATHSEP;
}

static bool tjs__path_is_root(const char *path) {
#if defined(_WIN32)
    if (((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z')) && path[1] == ':' &&
        (path[2] == '\\' || path[2] == '/') && path[3] == '\0') {
        return true;
    }
#endif
    return path[0] == TJS__PATHSEP && path[1] == '\0';
}

static bool tjs__path_dirname_inplace(char *path) {
    size_t len = strlen(path);
    if (len == 0) {
        return false;
    }
    if (len == 1 && path[0] == TJS__PATHSEP) {
        return false;
    }
    char *p = strrchr(path, TJS__PATHSEP);
#if defined(_WIN32)
    if (!p) {
        p = strrchr(path, TJS__PATHSEP_POSIX);
    }
#endif
    if (!p) {
        path[0] = '.';
        path[1] = '\0';
        return false;
    }
    if (p == path) {
        path[1] = '\0';
        return true;
    }
    *p = '\0';
    return true;
}

static char *tjs__path_join2(JSContext *ctx, const char *a, const char *b) {
    size_t len_a = strlen(a);
    size_t len_b = strlen(b);
    bool need_sep = len_a > 0 && a[len_a - 1] != TJS__PATHSEP;
    size_t len = len_a + (need_sep ? 1 : 0) + len_b + 1;
    char *out = js_malloc(ctx, len);
    if (!out) {
        return NULL;
    }
    memcpy(out, a, len_a);
    size_t offset = len_a;
    if (need_sep) {
        out[offset++] = TJS__PATHSEP;
    }
    memcpy(out + offset, b, len_b);
    out[offset + len_b] = '\0';
    return out;
}

static bool tjs__path_has_extension(const char *path) {
    const char *slash = strrchr(path, TJS__PATHSEP);
#if defined(_WIN32)
    const char *slash2 = strrchr(path, TJS__PATHSEP_POSIX);
    if (!slash || (slash2 && slash2 > slash)) {
        slash = slash2;
    }
#endif
    const char *base = slash ? slash + 1 : path;
    if (*base == '\0') {
        return false;
    }
    if ((base[0] == '.' && base[1] == '\0') || (base[0] == '.' && base[1] == '.' && base[2] == '\0')) {
        return false;
    }
    const char *dot = strrchr(base, '.');
    if (!dot) {
        return false;
    }
    if (dot == base) {
        return false;
    }
    return true;
}

static bool tjs__is_file(const char *path) {
    uv_fs_t req;
    int r = uv_fs_stat(NULL, &req, path, NULL);
    if (r < 0) {
        uv_fs_req_cleanup(&req);
        return false;
    }
    bool ok = S_ISREG(req.statbuf.st_mode);
    uv_fs_req_cleanup(&req);
    return ok;
}

static char *tjs__path_realpath_dup(JSContext *ctx, const char *path) {
    uv_fs_t req;
    int r = uv_fs_realpath(NULL, &req, path, NULL);
    if (r < 0) {
        uv_fs_req_cleanup(&req);
        return js_strdup(ctx, path);
    }
    const char *resolved = req.ptr ? req.ptr : path;
    char *out = js_strdup(ctx, resolved);
    uv_fs_req_cleanup(&req);
    return out;
}

static bool tjs__is_dir(const char *path) {
    uv_fs_t req;
    int r = uv_fs_stat(NULL, &req, path, NULL);
    if (r < 0) {
        uv_fs_req_cleanup(&req);
        return false;
    }
    bool ok = S_ISDIR(req.statbuf.st_mode);
    uv_fs_req_cleanup(&req);
    return ok;
}

static char *tjs__get_cwd(JSContext *ctx) {
    char buf[1024];
    size_t size = sizeof(buf);
    char *dbuf = buf;
    int r = uv_cwd(dbuf, &size);
    if (r != 0) {
        if (r != UV_ENOBUFS) {
            return NULL;
        }
        dbuf = js_malloc(ctx, size);
        if (!dbuf) {
            return NULL;
        }
        r = uv_cwd(dbuf, &size);
        if (r != 0) {
            js_free(ctx, dbuf);
            return NULL;
        }
    }
    char *out = js_strdup(ctx, dbuf);
    if (dbuf != buf) {
        js_free(ctx, dbuf);
    }
    return out;
}

static bool tjs__path_has_valid_extension(const char *path) {
    static const char *valid_exts[] = { ".js", ".mjs", ".cjs", ".json", ".so", ".dylib", ".dll" };
    if (!tjs__path_has_extension(path)) {
        return false;
    }
    const char *slash = strrchr(path, TJS__PATHSEP);
#if defined(_WIN32)
    const char *slash2 = strrchr(path, TJS__PATHSEP_POSIX);
    if (!slash || (slash2 && slash2 > slash)) {
        slash = slash2;
    }
#endif
    const char *base = slash ? slash + 1 : path;
    const char *dot = strrchr(base, '.');
    if (!dot || dot == base) {
        return false;
    }
    for (size_t i = 0; i < countof(valid_exts); i++) {
        if (strcmp(dot, valid_exts[i]) == 0) {
            return true;
        }
    }
    return false;
}

static char *tjs__resolve_as_file(JSContext *ctx, const char *path) {
    static const char *exts[] = { ".js", ".mjs", ".cjs", ".json", TJS_NATIVE_SUFFIX_PRIMARY, ".so", ".dylib", ".dll" };
    if (tjs__is_file(path)) {
        return tjs__path_realpath_dup(ctx, path);
    }
    if (tjs__path_has_valid_extension(path)) {
        return NULL;
    }
    for (size_t i = 0; i < countof(exts); i++) {
        size_t len = strlen(path) + strlen(exts[i]) + 1;
        char *candidate = js_malloc(ctx, len);
        if (!candidate) {
            return NULL;
        }
        strcpy(candidate, path);
        strcat(candidate, exts[i]);
        if (tjs__is_file(candidate)) {
            char *resolved = tjs__path_realpath_dup(ctx, candidate);
            js_free(ctx, candidate);
            return resolved;
        }
        js_free(ctx, candidate);
    }
    return NULL;
}

static char *tjs__resolve_as_directory(JSContext *ctx, const char *path, bool is_esm);

static JSValue tjs__load_json_value(JSContext *ctx, const char *filename) {
    DynBuf dbuf;
    int r;
    tjs_dbuf_init(ctx, &dbuf);
    r = tjs__load_file(ctx, &dbuf, filename);
    if (r != 0) {
        dbuf_free(&dbuf);
        return JS_UNDEFINED;
    }
    dbuf_putc(&dbuf, '\0');
    JSValue val = JS_ParseJSON(ctx, (char *) dbuf.buf, dbuf.size - 1, filename);
    dbuf_free(&dbuf);
    return val;
}

static char *tjs__get_string_prop(JSContext *ctx, JSValueConst obj, const char *key) {
    JSValue val = JS_GetPropertyStr(ctx, obj, key);
    if (JS_IsException(val)) {
        return NULL;
    }
    if (!JS_IsString(val)) {
        JS_FreeValue(ctx, val);
        return NULL;
    }
    const char *cstr = JS_ToCString(ctx, val);
    JS_FreeValue(ctx, val);
    if (!cstr) {
        return NULL;
    }
    char *out = js_strdup(ctx, cstr);
    JS_FreeCString(ctx, cstr);
    return out;
}

static char *tjs__resolve_conditional_export(JSContext *ctx, JSValueConst target, bool is_esm) {
    if (JS_IsString(target)) {
        const char *cstr = JS_ToCString(ctx, target);
        if (!cstr) {
            return NULL;
        }
        char *out = js_strdup(ctx, cstr);
        JS_FreeCString(ctx, cstr);
        return out;
    }
    if (!JS_IsObject(target)) {
        return NULL;
    }
    if (is_esm) {
        char *import_path = tjs__get_string_prop(ctx, target, "import");
        if (import_path) {
            return import_path;
        }
    } else {
        char *require_path = tjs__get_string_prop(ctx, target, "require");
        if (require_path) {
            return require_path;
        }
    }
    char *default_path = tjs__get_string_prop(ctx, target, "default");
    if (default_path) {
        return default_path;
    }
    return NULL;
}

static char *tjs__resolve_exports(JSContext *ctx, JSValueConst pkg, const char *subpath, bool is_esm) {
    if (!JS_IsObject(pkg)) {
        return NULL;
    }
    JSValue exports = JS_GetPropertyStr(ctx, pkg, "exports");
    if (JS_IsException(exports)) {
        return NULL;
    }
    if (JS_IsUndefined(exports)) {
        JS_FreeValue(ctx, exports);
        return NULL;
    }
    char *resolved = NULL;
    if (JS_IsString(exports)) {
        if (subpath && strcmp(subpath, ".") == 0) {
            resolved = tjs__resolve_conditional_export(ctx, exports, is_esm);
        }
    } else if (JS_IsObject(exports)) {
        if (subpath) {
            char *key = NULL;
            if (strcmp(subpath, ".") == 0) {
                key = js_strdup(ctx, ".");
            } else {
                size_t key_len = strlen(subpath) + 3;
                key = js_malloc(ctx, key_len);
                if (key) {
                    key[0] = '.';
                    key[1] = '/';
                    strcpy(key + 2, subpath);
                }
            }
            if (key) {
                JSValue target = JS_GetPropertyStr(ctx, exports, key);
                if (!JS_IsException(target)) {
                    resolved = tjs__resolve_conditional_export(ctx, target, is_esm);
                }
                JS_FreeValue(ctx, target);
                js_free(ctx, key);
            }
        }
        if (!resolved && subpath && strcmp(subpath, ".") == 0) {
            resolved = tjs__resolve_conditional_export(ctx, exports, is_esm);
        }
    }
    JS_FreeValue(ctx, exports);
    return resolved;
}

static char *tjs__resolve_package_entry(JSContext *ctx, const char *package_dir, JSValueConst pkg, bool is_esm) {
    char *target = NULL;
    if (JS_IsObject(pkg)) {
        target = tjs__resolve_exports(ctx, pkg, ".", is_esm);
        if (target) {
            char *joined = tjs__path_join2(ctx, package_dir, target);
            js_free(ctx, target);
            if (!joined) {
                return NULL;
            }
            char *resolved = tjs__resolve_as_file(ctx, joined);
            if (!resolved) {
                resolved = tjs__resolve_as_directory(ctx, joined, is_esm);
            }
            js_free(ctx, joined);
            if (resolved) {
                return resolved;
            }
        }
        if (is_esm) {
            target = tjs__get_string_prop(ctx, pkg, "module");
            if (target) {
                char *joined = tjs__path_join2(ctx, package_dir, target);
                js_free(ctx, target);
                if (joined) {
                    char *resolved = tjs__resolve_as_file(ctx, joined);
                    if (!resolved) {
                        resolved = tjs__resolve_as_directory(ctx, joined, is_esm);
                    }
                    js_free(ctx, joined);
                    if (resolved) {
                        return resolved;
                    }
                }
            }
        }
        target = tjs__get_string_prop(ctx, pkg, "main");
        if (target) {
            char *joined = tjs__path_join2(ctx, package_dir, target);
            js_free(ctx, target);
            if (joined) {
                char *resolved = tjs__resolve_as_file(ctx, joined);
                if (!resolved) {
                    resolved = tjs__resolve_as_directory(ctx, joined, is_esm);
                }
                js_free(ctx, joined);
                if (resolved) {
                    return resolved;
                }
            }
        }
    }
    char *index_path = tjs__path_join2(ctx, package_dir, "index");
    if (!index_path) {
        return NULL;
    }
    char *resolved = tjs__resolve_as_file(ctx, index_path);
    if (!resolved) {
        resolved = tjs__resolve_as_directory(ctx, index_path, is_esm);
    }
    js_free(ctx, index_path);
    return resolved;
}

static char *tjs__resolve_package_subpath(JSContext *ctx,
                                          const char *package_dir,
                                          JSValueConst pkg,
                                          const char *subpath,
                                          bool is_esm) {
    char *target = tjs__resolve_exports(ctx, pkg, subpath, is_esm);
    if (target) {
        char *joined = tjs__path_join2(ctx, package_dir, target);
        js_free(ctx, target);
        if (!joined) {
            return NULL;
        }
        char *resolved = tjs__resolve_as_file(ctx, joined);
        if (!resolved) {
            resolved = tjs__resolve_as_directory(ctx, joined, is_esm);
        }
        js_free(ctx, joined);
        if (resolved) {
            return resolved;
        }
    }
    char *direct = tjs__path_join2(ctx, package_dir, subpath);
    if (!direct) {
        return NULL;
    }
    char *resolved = tjs__resolve_as_file(ctx, direct);
    if (!resolved) {
        resolved = tjs__resolve_as_directory(ctx, direct, is_esm);
    }
    js_free(ctx, direct);
    return resolved;
}

static void tjs__split_package(const char *name, size_t *pkg_len, const char **subpath) {
    const char *slash = strchr(name, '/');
    if (name[0] == '@') {
        if (slash) {
            const char *slash2 = strchr(slash + 1, '/');
            if (slash2) {
                *pkg_len = (size_t) (slash2 - name);
                *subpath = slash2 + 1;
                return;
            }
        }
        *pkg_len = strlen(name);
        *subpath = NULL;
        return;
    }
    if (slash) {
        *pkg_len = (size_t) (slash - name);
        *subpath = slash + 1;
        return;
    }
    *pkg_len = strlen(name);
    *subpath = NULL;
}

static char *tjs__resolve_in_modules_dir(JSContext *ctx, const char *modules_dir, const char *name, bool is_esm) {
    /* Node resolution first tries modules_dir/name as file or directory. */
    char *direct = tjs__path_join2(ctx, modules_dir, name);
    if (!direct) {
        return NULL;
    }
    char *direct_resolved = tjs__resolve_as_file(ctx, direct);
    if (!direct_resolved) {
        direct_resolved = tjs__resolve_as_directory(ctx, direct, is_esm);
    }
    js_free(ctx, direct);
    if (direct_resolved) {
        return direct_resolved;
    }

    size_t pkg_len;
    const char *subpath;
    tjs__split_package(name, &pkg_len, &subpath);
    char *pkg_name = js_malloc(ctx, pkg_len + 1);
    if (!pkg_name) {
        return NULL;
    }
    memcpy(pkg_name, name, pkg_len);
    pkg_name[pkg_len] = '\0';
    char *package_dir = tjs__path_join2(ctx, modules_dir, pkg_name);
    js_free(ctx, pkg_name);
    if (!package_dir) {
        return NULL;
    }
    if (!tjs__is_dir(package_dir)) {
        js_free(ctx, package_dir);
        return NULL;
    }
    JSValue pkg = JS_UNDEFINED;
    char *package_json_path = tjs__path_join2(ctx, package_dir, "package.json");
    if (package_json_path) {
        pkg = tjs__load_json_value(ctx, package_json_path);
        js_free(ctx, package_json_path);
    }
    char *resolved = NULL;
    if (subpath) {
        resolved = tjs__resolve_package_subpath(ctx, package_dir, pkg, subpath, is_esm);
    } else {
        resolved = tjs__resolve_package_entry(ctx, package_dir, pkg, is_esm);
    }
    JS_FreeValue(ctx, pkg);
    js_free(ctx, package_dir);
    return resolved;
}

static char *tjs__resolve_node_modules(JSContext *ctx, const char *base_dir, const char *name, bool is_esm) {
    char *current = js_strdup(ctx, base_dir);
    if (!current) {
        return NULL;
    }
    bool checked_dot = false;
    while (1) {
        char *xuan_dir = tjs__path_join2(ctx, current, "xuan_modules");
        if (xuan_dir) {
            if (tjs__is_dir(xuan_dir)) {
                char *resolved = tjs__resolve_in_modules_dir(ctx, xuan_dir, name, is_esm);
                if (resolved) {
                    js_free(ctx, xuan_dir);
                    js_free(ctx, current);
                    return resolved;
                }
            }
            js_free(ctx, xuan_dir);
        }
        char *native_dir = tjs__path_join2(ctx, current, "native_modules");
        if (native_dir) {
            if (tjs__is_dir(native_dir)) {
                char *resolved = tjs__resolve_in_modules_dir(ctx, native_dir, name, is_esm);
                if (resolved) {
                    js_free(ctx, native_dir);
                    js_free(ctx, current);
                    return resolved;
                }
            }
            js_free(ctx, native_dir);
        }
        char *node_dir = tjs__path_join2(ctx, current, "node_modules");
        if (node_dir) {
            if (tjs__is_dir(node_dir)) {
                char *resolved = tjs__resolve_in_modules_dir(ctx, node_dir, name, is_esm);
                if (resolved) {
                    js_free(ctx, node_dir);
                    js_free(ctx, current);
                    return resolved;
                }
            }
            js_free(ctx, node_dir);
        }
        if (tjs__path_is_root(current)) {
            break;
        }
        if (!tjs__path_dirname_inplace(current)) {
            /* For bare relative names like "tests", dirname sets current to
             * "." but returns false.  We must still check "." before giving
             * up.  Use checked_dot to avoid an infinite loop at ".". */
            if (checked_dot) {
                break;
            }
            checked_dot = true;
        } else {
            checked_dot = false;
        }
    }
    js_free(ctx, current);

    const char *node_path = getenv("NODE_PATH");
    if (node_path && *node_path) {
        char *paths = js_strdup(ctx, node_path);
        if (!paths) {
            return NULL;
        }
        char *cwd = tjs__get_cwd(ctx);
        char *saveptr = NULL;
#if defined(_WIN32)
        const char *delim = ";";
#else
        const char *delim = ":";
#endif
        for (char *entry = strtok_r(paths, delim, &saveptr); entry; entry = strtok_r(NULL, delim, &saveptr)) {
            if (*entry == '\0') {
                continue;
            }

            char *modules_dir = NULL;
            if (tjs__path_is_absolute(entry)) {
                modules_dir = js_strdup(ctx, entry);
            } else if (cwd) {
                modules_dir = tjs__path_join2(ctx, cwd, entry);
            }
            if (!modules_dir) {
                continue;
            }

            if (tjs__is_dir(modules_dir)) {
                char *resolved = tjs__resolve_in_modules_dir(ctx, modules_dir, name, is_esm);
                js_free(ctx, modules_dir);
                if (resolved) {
                    js_free(ctx, paths);
                    if (cwd) {
                        js_free(ctx, cwd);
                    }
                    return resolved;
                }
            } else {
                js_free(ctx, modules_dir);
            }
        }

        js_free(ctx, paths);
        if (cwd) {
            js_free(ctx, cwd);
        }
    }

    return NULL;
}

static bool tjs__is_url_specifier(const char *name) {
    static const char http[] = "http://";
    static const char https[] = "https://";
    static const char tjs_prefix[] = "tjs:";
    static const char node_prefix[] = "node:";
    return strncmp(http, name, strlen(http)) == 0 || strncmp(https, name, strlen(https)) == 0 ||
           strncmp(tjs_prefix, name, strlen(tjs_prefix)) == 0 || strncmp(node_prefix, name, strlen(node_prefix)) == 0;
}

static char *tjs__resolve_as_directory(JSContext *ctx, const char *path, bool is_esm) {
    if (!tjs__is_dir(path)) {
        return NULL;
    }
    char *package_json_path = tjs__path_join2(ctx, path, "package.json");
    if (package_json_path) {
        JSValue pkg = tjs__load_json_value(ctx, package_json_path);
        js_free(ctx, package_json_path);
        if (JS_IsObject(pkg)) {
            char *resolved = tjs__resolve_package_entry(ctx, path, pkg, is_esm);
            JS_FreeValue(ctx, pkg);
            if (resolved) {
                return resolved;
            }
        } else {
            JS_FreeValue(ctx, pkg);
        }
    }
    char *index_path = tjs__path_join2(ctx, path, "index");
    if (!index_path) {
        return NULL;
    }
    char *resolved = tjs__resolve_as_file(ctx, index_path);
    js_free(ctx, index_path);
    return resolved;
}

static char *tjs__resolve_bare_specifier(JSContext *ctx, const char *base_name, const char *name, bool is_esm) {
    const char *base_dir = base_name;
    char *tmp_base = NULL;
    if (!base_dir || tjs__is_url_specifier(base_dir)) {
        tmp_base = tjs__get_cwd(ctx);
        base_dir = tmp_base ? tmp_base : ".";
    } else {
        tmp_base = js_strdup(ctx, base_dir);
        if (!tmp_base) {
            return NULL;
        }
        if (!tjs__path_dirname_inplace(tmp_base)) {
            js_free(ctx, tmp_base);
            tmp_base = tjs__get_cwd(ctx);
        }
        base_dir = tmp_base ? tmp_base : ".";
    }
    char *resolved = tjs__resolve_node_modules(ctx, base_dir, name, is_esm);
    if (tmp_base) {
        js_free(ctx, tmp_base);
    }
    return resolved;
}

static char *tjs__resolve_cjs_specifier(JSContext *ctx, const char *base_dir, const char *name) {
    const char *builtin = tjs__canonical_builtin_specifier(name);
    if (builtin) {
        return js_strdup(ctx, builtin);
    }
    if (tjs__is_url_specifier(name)) {
        return NULL;
    }
    if (tjs__path_is_absolute(name)) {
        char *resolved = tjs__resolve_as_file(ctx, name);
        if (!resolved) {
            resolved = tjs__resolve_as_directory(ctx, name, false);
        }
        return resolved;
    }
    if (name[0] == '.' && (name[1] == TJS__PATHSEP_POSIX || name[1] == TJS__PATHSEP ||
                           (name[1] == '.' && (name[2] == TJS__PATHSEP_POSIX || name[2] == TJS__PATHSEP)))) {
        char *joined = tjs__path_join2(ctx, base_dir, name);
        if (!joined) {
            return NULL;
        }
        char *resolved = tjs__resolve_as_file(ctx, joined);
        if (!resolved) {
            resolved = tjs__resolve_as_directory(ctx, joined, false);
        }
        js_free(ctx, joined);
        return resolved;
    }
    return tjs__resolve_node_modules(ctx, base_dir, name, false);
}

static JSValue tjs__build_require_stack(JSContext *ctx, const char *filename, JSValueConst parent_stack) {
    JSValue stack = JS_NewArray(ctx);
    if (JS_IsException(stack)) {
        return stack;
    }
    uint32_t idx = 0;
    if (JS_IsArray(parent_stack)) {
        JSValue len_val = JS_GetPropertyStr(ctx, parent_stack, "length");
        uint32_t len = 0;
        if (JS_IsException(len_val) || JS_ToUint32(ctx, &len, len_val)) {
            JS_FreeValue(ctx, len_val);
            JS_FreeValue(ctx, stack);
            return JS_EXCEPTION;
        }
        JS_FreeValue(ctx, len_val);
        for (uint32_t i = 0; i < len; i++) {
            JSValue item = JS_GetPropertyUint32(ctx, parent_stack, i);
            if (JS_IsException(item)) {
                JS_FreeValue(ctx, stack);
                return JS_EXCEPTION;
            }
            if (JS_SetPropertyUint32(ctx, stack, idx++, item) < 0) {
                JS_FreeValue(ctx, item);
                JS_FreeValue(ctx, stack);
                return JS_EXCEPTION;
            }
        }
    }
    if (filename) {
        if (JS_SetPropertyUint32(ctx, stack, idx, JS_NewString(ctx, filename)) < 0) {
            JS_FreeValue(ctx, stack);
            return JS_EXCEPTION;
        }
    }
    return stack;
}

static JSValue tjs__throw_module_not_found(JSContext *ctx, const char *specifier, JSValueConst require_stack) {
    JSValue err = JS_NewError(ctx);
    if (JS_IsException(err)) {
        return err;
    }
    size_t spec_len = strlen(specifier);
    size_t msg_len = spec_len + sizeof("Cannot find module ''") - 1;
    char *msg = js_malloc(ctx, msg_len + 1);
    if (!msg) {
        JS_FreeValue(ctx, err);
        return JS_EXCEPTION;
    }
    snprintf(msg, msg_len + 1, "Cannot find module '%s'", specifier);
    JS_DefinePropertyValueStr(ctx, err, "message", JS_NewString(ctx, msg), JS_PROP_C_W_E);
    js_free(ctx, msg);
    JS_DefinePropertyValueStr(ctx, err, "code", JS_NewString(ctx, "MODULE_NOT_FOUND"), JS_PROP_C_W_E);
    if (JS_IsArray(require_stack)) {
        JS_DefinePropertyValueStr(ctx, err, "requireStack", JS_DupValue(ctx, require_stack), JS_PROP_C_W_E);
    }
    return JS_Throw(ctx, err);
}

static JSValue tjs__get_cjs_cache(JSContext *ctx) {
    JSValue global_obj = JS_GetGlobalObject(ctx);
    JSValue cache = JS_GetPropertyStr(ctx, global_obj, "__tjs_cjs_cache");
    if (JS_IsUndefined(cache)) {
        JS_FreeValue(ctx, cache);
        cache = JS_NewObject(ctx);
        JS_DefinePropertyValueStr(ctx, global_obj, "__tjs_cjs_cache", JS_DupValue(ctx, cache), JS_PROP_C_W_E);
    }
    JS_FreeValue(ctx, global_obj);
    return cache;
}

static JSValue tjs__load_cjs(JSContext *ctx, const char *filename, JSValueConst parent_stack);
static bool tjs__package_type_is_module(JSContext *ctx, const char *filename);

static JSValue tjs__await_promise(JSContext *ctx, JSValue promise) {
    JSRuntime *rt = JS_GetRuntime(ctx);
    JSContext *job_ctx = NULL;

    for (;;) {
        JSPromiseStateEnum state = JS_PromiseState(ctx, promise);
        if (state == JS_PROMISE_FULFILLED) {
            return JS_PromiseResult(ctx, promise);
        }
        if (state == JS_PROMISE_REJECTED) {
            JSValue reason = JS_PromiseResult(ctx, promise);
            JS_Throw(ctx, reason);
            return JS_EXCEPTION;
        }
        int r = JS_ExecutePendingJob(rt, &job_ctx);
        if (r < 0) {
            return JS_EXCEPTION;
        }
        if (r == 0) {
            return JS_EXCEPTION;
        }
    }
}

static JSValue tjs__load_esm_namespace(JSContext *ctx, const char *filename) {
    DynBuf dbuf;
    tjs_dbuf_init(ctx, &dbuf);
    int r = tjs__load_file(ctx, &dbuf, filename);
    if (r != 0) {
        dbuf_free(&dbuf);
        return JS_ThrowReferenceError(ctx, "could not load '%s'", filename);
    }
    dbuf_putc(&dbuf, '\0');

    JSValue module_obj =
        JS_Eval(ctx, (char *) dbuf.buf, dbuf.size - 1, filename, JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
    dbuf_free(&dbuf);
    if (JS_IsException(module_obj)) {
        return module_obj;
    }

    JSValue module_obj_dup = JS_DupValue(ctx, module_obj);

    if (JS_ResolveModule(ctx, module_obj) < 0) {
        JS_FreeValue(ctx, module_obj_dup);
        return JS_EXCEPTION;
    }
    if (js_module_set_import_meta(ctx, module_obj, true, false) != 0) {
        JS_FreeValue(ctx, module_obj_dup);
        return JS_EXCEPTION;
    }

    JSValue exec_res = JS_EvalFunction(ctx, module_obj);
    if (JS_IsException(exec_res)) {
        JS_FreeValue(ctx, module_obj_dup);
        return exec_res;
    }
    if (JS_IsPromise(exec_res)) {
        JSValue awaited = tjs__await_promise(ctx, exec_res);
        JS_FreeValue(ctx, exec_res);
        if (JS_IsException(awaited)) {
            JS_FreeValue(ctx, module_obj_dup);
            return awaited;
        }
        JS_FreeValue(ctx, awaited);
    } else {
        JS_FreeValue(ctx, exec_res);
    }

    JSModuleDef *m = JS_VALUE_GET_PTR(module_obj_dup);
    JSValue ns = JS_GetModuleNamespace(ctx, m);
    JS_FreeValue(ctx, module_obj_dup);
    return ns;
}

static bool tjs__is_esm_path(JSContext *ctx, const char *filename) {
    if (js__has_suffix(filename, ".mjs")) {
        return true;
    }
    if (js__has_suffix(filename, ".cjs")) {
        return false;
    }
    if (js__has_suffix(filename, ".js")) {
        return tjs__package_type_is_module(ctx, filename);
    }
    return false;
}

static JSValue tjs__namespace_to_cjs(JSContext *ctx, JSValue ns) {
    JSValue def = JS_GetPropertyStr(ctx, ns, "default");
    if (!JS_IsObject(def) && !JS_IsFunction(ctx, def)) {
        JS_FreeValue(ctx, def);
        return ns;
    }

    JSPropertyEnum *tab;
    uint32_t len;
    if (JS_GetOwnPropertyNames(ctx, &tab, &len, ns, JS_GPN_ENUM_ONLY | JS_GPN_STRING_MASK) == 0) {
        for (uint32_t i = 0; i < len; i++) {
            const char *name = JS_AtomToCString(ctx, tab[i].atom);
            if (!name) {
                continue;
            }
            if (strcmp(name, "default") != 0) {
                JSValue val = JS_GetProperty(ctx, ns, tab[i].atom);
                if (!JS_IsException(val)) {
                    JS_SetPropertyStr(ctx, def, name, val);
                } else {
                    JS_FreeValue(ctx, val);
                }
            }
            JS_FreeCString(ctx, name);
        }
        JS_FreePropertyEnum(ctx, tab, len);
    }
    JS_FreeValue(ctx, ns);
    return def;
}

static JSValue tjs__cjs_require(JSContext *ctx,
                                JSValueConst this_val,
                                int argc,
                                JSValueConst *argv,
                                int magic,
                                JSValue *data) {
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "require expects a specifier");
    }
    const char *specifier = JS_ToCString(ctx, argv[0]);
    if (!specifier) {
        return JS_EXCEPTION;
    }
    const char *base_dir = NULL;
    JSValueConst require_stack = JS_UNDEFINED;
    if (data && !JS_IsUndefined(data[0])) {
        base_dir = JS_ToCString(ctx, data[0]);
    }
    if (data && !JS_IsUndefined(data[1])) {
        require_stack = data[1];
    }
    if (!base_dir) {
        base_dir = specifier;
    }
    char *resolved = tjs__resolve_cjs_specifier(ctx, base_dir, specifier);
    if (data && !JS_IsUndefined(data[0]) && base_dir) {
        JS_FreeCString(ctx, base_dir);
    }
    if (!resolved) {
        JS_FreeCString(ctx, specifier);
        return tjs__throw_module_not_found(ctx, specifier, require_stack);
    }
    JS_FreeCString(ctx, specifier);
    JSValue result;
    JSValue cache = tjs__get_cjs_cache(ctx);
    // Try CJS file module cache first (with prefix)
    size_t cjs_key_len = strlen(resolved) + 12;
    char *cjs_key = js_malloc(ctx, cjs_key_len);
    if (!cjs_key) {
        JS_FreeValue(ctx, cache);
        js_free(ctx, resolved);
        return JS_EXCEPTION;
    }
    snprintf(cjs_key, cjs_key_len, "__cjs__%s", resolved);
    JSValue cjs_cached = JS_GetPropertyStr(ctx, cache, cjs_key);
    js_free(ctx, cjs_key);
    if (!JS_IsUndefined(cjs_cached)) {
        JSValue exports = JS_GetPropertyStr(ctx, cjs_cached, "exports");
        JS_FreeValue(ctx, cjs_cached);
        JS_FreeValue(ctx, cache);
        js_free(ctx, resolved);
        return exports;
    }
    JS_FreeValue(ctx, cjs_cached);
    // Try regular cache (for built-ins, JSON, ESM)
    JSValue cached = JS_GetPropertyStr(ctx, cache, resolved);
    if (!JS_IsUndefined(cached)) {
        JS_FreeValue(ctx, cache);
        js_free(ctx, resolved);
        return cached;
    }
    JS_FreeValue(ctx, cached);
    if (js__has_suffix(resolved, ".json")) {
        JSValue json = tjs__load_json_value(ctx, resolved);
        if (JS_IsException(json)) {
            JS_FreeValue(ctx, cache);
            js_free(ctx, resolved);
            return json;
        }
        JS_SetPropertyStr(ctx, cache, resolved, JS_DupValue(ctx, json));
        JS_FreeValue(ctx, cache);
        js_free(ctx, resolved);
        return json;
    }
    if (tjs__canonical_builtin_specifier(resolved)) {
        JSValue ns = tjs__load_builtin_namespace(ctx, resolved);
        if (JS_IsException(ns)) {
            JS_FreeValue(ctx, cache);
            js_free(ctx, resolved);
            return ns;
        }
        JSValue cjs = tjs__namespace_to_cjs(ctx, ns);
        JS_SetPropertyStr(ctx, cache, resolved, JS_DupValue(ctx, cjs));
        JS_FreeValue(ctx, cache);
        js_free(ctx, resolved);
        return cjs;
    }
    if (tjs__is_native_module(resolved)) {
        JS_FreeValue(ctx, cache);
        js_free(ctx, resolved);
        return JS_ThrowTypeError(ctx, "require() does not support native modules, use import()");
    }
    if (tjs__is_esm_path(ctx, resolved)) {
        JSValue ns = tjs__load_esm_namespace(ctx, resolved);
        if (JS_IsException(ns)) {
            JS_FreeValue(ctx, cache);
            js_free(ctx, resolved);
            return ns;
        }
        JS_SetPropertyStr(ctx, cache, resolved, JS_DupValue(ctx, ns));
        JS_FreeValue(ctx, cache);
        js_free(ctx, resolved);
        return ns;
    }
    JS_FreeValue(ctx, cache);
    result = tjs__load_cjs(ctx, resolved, require_stack);
    js_free(ctx, resolved);
    return result;
}

static JSValue tjs__execute_cjs(JSContext *ctx,
                                const char *filename,
                                const char *dirname,
                                JSValue exports_obj,
                                JSValue module_obj,
                                JSValueConst require_stack) {
    DynBuf dbuf;
    tjs_dbuf_init(ctx, &dbuf);
    int r = tjs__load_file(ctx, &dbuf, filename);
    if (r != 0) {
        dbuf_free(&dbuf);
        return JS_ThrowReferenceError(ctx, "could not load '%s'", filename);
    }
    const char *prefix = "(function (exports, require, module, __filename, __dirname) {";
    const char *suffix = "\n})";
    size_t total = strlen(prefix) + dbuf.size + strlen(suffix) + 1;
    char *wrapped = js_malloc(ctx, total);
    if (!wrapped) {
        dbuf_free(&dbuf);
        return JS_EXCEPTION;
    }
    strcpy(wrapped, prefix);
    memcpy(wrapped + strlen(prefix), dbuf.buf, dbuf.size);
    strcpy(wrapped + strlen(prefix) + dbuf.size, suffix);
    JSValue func = JS_Eval(ctx, wrapped, strlen(prefix) + dbuf.size + strlen(suffix), filename, JS_EVAL_TYPE_GLOBAL);
    js_free(ctx, wrapped);
    dbuf_free(&dbuf);
    if (JS_IsException(func)) {
        return func;
    }
    JSValue base_dir = JS_NewString(ctx, dirname);
    JSValue require_stack_val = JS_DupValue(ctx, require_stack);
    JSValue require_data[2] = { base_dir, require_stack_val };
    JSValue require_func = JS_NewCFunctionData(ctx, tjs__cjs_require, 1, 0, 2, require_data);
    JSValue argv[5] = { exports_obj,
                        require_func,
                        module_obj,
                        JS_NewString(ctx, filename),
                        JS_NewString(ctx, dirname) };
    JSValue res = JS_Call(ctx, func, JS_UNDEFINED, 5, argv);
    JS_FreeValue(ctx, func);
    JS_FreeValue(ctx, require_func);
    JS_FreeValue(ctx, argv[3]);
    JS_FreeValue(ctx, argv[4]);
    JS_FreeValue(ctx, require_stack_val);
    JS_FreeValue(ctx, base_dir);
    if (JS_IsException(res)) {
        return res;
    }
    JS_FreeValue(ctx, res);
    return JS_UNDEFINED;
}

static JSValue tjs__load_cjs(JSContext *ctx, const char *filename, JSValueConst parent_stack) {
    JSValue cache = tjs__get_cjs_cache(ctx);
    // Use prefixed key for CJS file modules
    size_t cjs_key_len = strlen(filename) + 12;
    char *cjs_key = js_malloc(ctx, cjs_key_len);
    if (!cjs_key) {
        JS_FreeValue(ctx, cache);
        return JS_EXCEPTION;
    }
    snprintf(cjs_key, cjs_key_len, "__cjs__%s", filename);
    JSValue cached = JS_GetPropertyStr(ctx, cache, cjs_key);
    if (!JS_IsUndefined(cached)) {
        JSValue exports = JS_GetPropertyStr(ctx, cached, "exports");
        JS_FreeValue(ctx, cached);
        JS_FreeValue(ctx, cache);
        js_free(ctx, cjs_key);
        return exports;
    }
    JS_FreeValue(ctx, cached);
    JSValue exports_obj = JS_NewObject(ctx);
    JSValue module_obj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, module_obj, "exports", JS_DupValue(ctx, exports_obj));
    // Store module_obj directly - circular requires will see live module.exports
    JS_SetPropertyStr(ctx, cache, cjs_key, JS_DupValue(ctx, module_obj));
    char *dir = js_strdup(ctx, filename);
    if (!dir) {
        JS_FreeValue(ctx, cache);
        JS_FreeValue(ctx, module_obj);
        JS_FreeValue(ctx, exports_obj);
        js_free(ctx, cjs_key);
        return JS_EXCEPTION;
    }
    tjs__path_dirname_inplace(dir);
    JSValue require_stack = tjs__build_require_stack(ctx, filename, parent_stack);
    if (JS_IsException(require_stack)) {
        js_free(ctx, dir);
        JS_FreeValue(ctx, cache);
        JS_FreeValue(ctx, module_obj);
        JS_FreeValue(ctx, exports_obj);
        js_free(ctx, cjs_key);
        return JS_EXCEPTION;
    }
    JSValue exec_res = tjs__execute_cjs(ctx, filename, dir, exports_obj, module_obj, require_stack);
    JS_FreeValue(ctx, require_stack);
    js_free(ctx, dir);
    if (JS_IsException(exec_res)) {
        JS_FreeValue(ctx, cache);
        JS_FreeValue(ctx, module_obj);
        JS_FreeValue(ctx, exports_obj);
        js_free(ctx, cjs_key);
        return exec_res;
    }
    JS_FreeValue(ctx, exec_res);
    JSValue final_exports = JS_GetPropertyStr(ctx, module_obj, "exports");
    JS_FreeValue(ctx, cache);
    JS_FreeValue(ctx, module_obj);
    JS_FreeValue(ctx, exports_obj);
    js_free(ctx, cjs_key);
    JSValue ret = JS_DupValue(ctx, final_exports);
    JS_FreeValue(ctx, final_exports);
    return ret;
}

static int cjs_module_init(JSContext *ctx, JSModuleDef *m) {
    JSValue val = JS_GetModulePrivateValue(ctx, m);
    JS_SetModuleExport(ctx, m, "default", val);
    if (JS_IsObject(val)) {
        JSPropertyEnum *tab;
        uint32_t len;
        if (JS_GetOwnPropertyNames(ctx, &tab, &len, val, JS_GPN_ENUM_ONLY | JS_GPN_STRING_MASK) == 0) {
            for (uint32_t i = 0; i < len; i++) {
                JSValue prop = JS_GetProperty(ctx, val, tab[i].atom);
                const char *name = JS_AtomToCString(ctx, tab[i].atom);
                if (name) {
                    JS_SetModuleExport(ctx, m, name, prop);
                    JS_FreeCString(ctx, name);
                } else {
                    JS_FreeValue(ctx, prop);
                }
            }
            JS_FreePropertyEnum(ctx, tab, len);
        }
    }
    return 0;
}

static JSModuleDef *create_cjs_module(JSContext *ctx, const char *module_name, JSValue exports) {
    JSModuleDef *m = JS_NewCModule(ctx, module_name, cjs_module_init);
    if (!m) {
        JS_FreeValue(ctx, exports);
        return NULL;
    }
    JS_AddModuleExport(ctx, m, "default");
    if (JS_IsObject(exports)) {
        JSPropertyEnum *tab;
        uint32_t len;
        if (JS_GetOwnPropertyNames(ctx, &tab, &len, exports, JS_GPN_ENUM_ONLY | JS_GPN_STRING_MASK) == 0) {
            for (uint32_t i = 0; i < len; i++) {
                const char *name = JS_AtomToCString(ctx, tab[i].atom);
                if (name) {
                    JS_AddModuleExport(ctx, m, name);
                    JS_FreeCString(ctx, name);
                }
            }
            JS_FreePropertyEnum(ctx, tab, len);
        }
    }
    JS_SetModulePrivateValue(ctx, m, exports);
    return m;
}

static bool tjs__path_contains_segment(const char *path, const char *segment) {
    size_t seg_len = strlen(segment);
    const char *p = path;
    while ((p = strstr(p, segment)) != NULL) {
        if ((p == path || p[-1] == TJS__PATHSEP || p[-1] == TJS__PATHSEP_POSIX) &&
            (p[seg_len] == '\0' || p[seg_len] == TJS__PATHSEP || p[seg_len] == TJS__PATHSEP_POSIX)) {
            return true;
        }
        p += seg_len;
    }
    return false;
}

static bool tjs__package_type_is_module(JSContext *ctx, const char *filename) {
    char *dir = js_strdup(ctx, filename);
    if (!dir) {
        return false;
    }
    tjs__path_dirname_inplace(dir);
    while (1) {
        char *package_json_path = tjs__path_join2(ctx, dir, "package.json");
        if (!package_json_path) {
            js_free(ctx, dir);
            return false;
        }
        if (tjs__is_file(package_json_path)) {
            JSValue pkg = tjs__load_json_value(ctx, package_json_path);
            js_free(ctx, package_json_path);
            if (JS_IsObject(pkg)) {
                char *type = tjs__get_string_prop(ctx, pkg, "type");
                JS_FreeValue(ctx, pkg);
                if (type) {
                    bool is_module = strcmp(type, "module") == 0;
                    js_free(ctx, type);
                    js_free(ctx, dir);
                    return is_module;
                }
                /* Nearest package.json exists but has no type: default to CommonJS. */
                js_free(ctx, dir);
                return false;
            } else {
                JS_FreeValue(ctx, pkg);
            }
        } else {
            js_free(ctx, package_json_path);
        }
        if (tjs__path_is_root(dir)) {
            break;
        }
        if (!tjs__path_dirname_inplace(dir)) {
            break;
        }
    }
    js_free(ctx, dir);
    return false;
}

static bool tjs__is_cjs_module(JSContext *ctx, const char *module_name) {
    if (tjs__is_url_specifier(module_name)) {
        return false;
    }
    if (tjs__is_native_module(module_name)) {
        return false;
    }
    if (js__has_suffix(module_name, ".cjs")) {
        return true;
    }
    if (js__has_suffix(module_name, ".mjs")) {
        return false;
    }
    if (js__has_suffix(module_name, ".js")) {
        if (tjs__path_contains_segment(module_name, "node_modules") ||
            tjs__path_contains_segment(module_name, "native_modules") ||
            tjs__path_contains_segment(module_name, "xuan_modules")) {
            if (tjs__package_type_is_module(ctx, module_name)) {
                return false;
            }
            return true;
        }
    }
    return false;
}

void tjs__init_require(JSContext *ctx) {
    JSValue global_obj = JS_GetGlobalObject(ctx);
    char *cwd = tjs__get_cwd(ctx);
    if (!cwd) {
        cwd = js_strdup(ctx, ".");
    }
    JSValue base_dir = JS_NewString(ctx, cwd ? cwd : ".");
    if (cwd) {
        js_free(ctx, cwd);
    }
    JSValue require_data[2] = { base_dir, JS_UNDEFINED };
    JSValue require_func = JS_NewCFunctionData(ctx, tjs__cjs_require, 1, 0, 2, require_data);
    JS_DefinePropertyValueStr(ctx, global_obj, "require", require_func, JS_PROP_C_W_E);
    JS_FreeValue(ctx, base_dir);
    JS_FreeValue(ctx, global_obj);
}


static int json_module_init(JSContext *ctx, JSModuleDef *m) {
    JSValue val;
    val = JS_GetModulePrivateValue(ctx, m);
    JS_SetModuleExport(ctx, m, "default", val);
    return 0;
}

static JSModuleDef *create_json_module(JSContext *ctx, const char *module_name, JSValue val) {
    JSModuleDef *m;
    m = JS_NewCModule(ctx, module_name, json_module_init);
    if (!m) {
        JS_FreeValue(ctx, val);
        return NULL;
    }
    /* only export the "default" symbol which will contain the JSON object */
    JS_AddModuleExport(ctx, m, "default");
    JS_SetModulePrivateValue(ctx, m, val);
    return m;
}

/* return > 0 if the attributes indicate a JSON module, 0 otherwise, -1 on error */
static int js_module_test_json(JSContext *ctx, JSValueConst attributes) {
    JSValue str;
    const char *cstr;
    size_t len;
    int res;

    if (JS_IsUndefined(attributes)) {
        return 0;
    }
    str = JS_GetPropertyStr(ctx, attributes, "type");
    if (JS_IsException(str)) {
        return -1;
    }
    if (!JS_IsString(str)) {
        JS_FreeValue(ctx, str);
        return 0;
    }
    cstr = JS_ToCStringLen(ctx, &len, str);
    JS_FreeValue(ctx, str);
    if (!cstr) {
        return -1;
    }
    if (len == 4 && !memcmp(cstr, "json", len)) {
        res = 1;
    } else {
        res = 0;
    }
    JS_FreeCString(ctx, cstr);
    return res;
}

/* in order to conform with the specification, only the keys should be
   tested and not the associated values. */
int tjs_module_attr_checker(JSContext *ctx, void *opaque, JSValueConst attributes) {
    JSPropertyEnum *tab;
    uint32_t i, len;
    int ret;
    const char *cstr;
    size_t cstr_len;

    if (JS_GetOwnPropertyNames(ctx, &tab, &len, attributes, JS_GPN_ENUM_ONLY | JS_GPN_STRING_MASK)) {
        return -1;
    }
    ret = 0;
    for (i = 0; i < len; i++) {
        cstr = JS_AtomToCStringLen(ctx, &cstr_len, tab[i].atom);
        if (!cstr) {
            ret = -1;
            break;
        }
        if (!(cstr_len == 4 && !memcmp(cstr, "type", cstr_len))) {
            JS_ThrowTypeError(ctx, "import attribute '%s' is not supported", cstr);
            ret = -1;
        }
        JS_FreeCString(ctx, cstr);
        if (ret) {
            break;
        }
    }
    JS_FreePropertyEnum(ctx, tab, len);
    return ret;
}

JSModuleDef *tjs_module_loader(JSContext *ctx, const char *module_name, void *opaque, JSValueConst attributes) {
    static const char http[] = "http://";
    static const char https[] = "https://";

    JSModuleDef *m = NULL;
    int r;
    bool is_json, use_realpath;
    DynBuf dbuf;

    if (tjs__canonical_builtin_specifier(module_name)) {
        return tjs__load_builtin(ctx, module_name);
    }

#ifndef TJS_DISABLE_NATIVE_MODULES
    if (tjs__is_native_module(module_name)) {
        return tjs__load_native_module(ctx, module_name);
    }
#endif

    r = js_module_test_json(ctx, attributes);
    if (r < 0) {
        return NULL;
    }
    is_json = js__has_suffix(module_name, ".json") || r > 0;
    if (!is_json && tjs__is_cjs_module(ctx, module_name)) {
        JSValue exports = tjs__load_cjs(ctx, module_name, JS_UNDEFINED);
        if (JS_IsException(exports)) {
            return NULL;
        }
        m = create_cjs_module(ctx, module_name, exports);
        return m;
    }

    tjs_dbuf_init(ctx, &dbuf);

    if (strncmp(http, module_name, strlen(http)) == 0 || strncmp(https, module_name, strlen(https)) == 0) {
        TJSRuntime *qrt = TJS_GetRuntime(ctx);
        r = tjs__lws_load_http(qrt, &dbuf, module_name);
        if (r != 200) {
            if (r == -2) {
                JS_ThrowReferenceError(ctx, "could not load '%s': request timed out", module_name);
            } else if (r < 0) {
                JS_ThrowReferenceError(ctx, "could not load '%s': network error", module_name);
            } else {
                /* http error */
                JS_ThrowReferenceError(ctx, "could not load '%s': %d", module_name, r);
            }
            goto end;
        }
        use_realpath = false;
    } else {
        r = tjs__load_file(ctx, &dbuf, module_name);
        if (r != 0) {
            JS_ThrowReferenceError(ctx, "could not load '%s'", module_name);
            goto end;
        }
        use_realpath = true;
    }

    /* Add null termination, required by JS_Eval / JS_ParseJSON. */
    dbuf_putc(&dbuf, '\0');

    /* Now load the module for real. */
    if (is_json) {
        JSValue val;
        val = JS_ParseJSON(ctx, (char *) dbuf.buf, dbuf.size - 1, module_name);
        if (JS_IsException(val)) {
            goto end;
        }
        m = create_json_module(ctx, module_name, val);
    } else {
        JSValue func_val = TJS_CompileModule(ctx, module_name, (const char *) dbuf.buf, dbuf.size - 1);
        if (JS_IsException(func_val)) {
            goto end;
        }

        r = js_module_set_import_meta(ctx, func_val, use_realpath, false);
        if (r != 0) {
            JS_FreeValue(ctx, func_val);
            goto end;
        }

        /* the module is already referenced, so we must free it */
        m = JS_VALUE_GET_PTR(func_val);
        JS_FreeValue(ctx, func_val);
    }

end:
    dbuf_free(&dbuf);

    return m;
}

int js_module_set_import_meta(JSContext *ctx, JSValue func_val, bool use_realpath, bool is_main) {
    JSModuleDef *m;
    char buf[JS__PATH_MAX + 16] = { 0 };
    int r;
    JSValue meta_obj;
    JSAtom module_name_atom;
    const char *module_name;
    char module_dirname[JS__PATH_MAX] = { 0 };
    char module_basename[JS__PATH_MAX] = { 0 };

    CHECK_EQ(JS_VALUE_GET_TAG(func_val), JS_TAG_MODULE);
    m = JS_VALUE_GET_PTR(func_val);

    module_name_atom = JS_GetModuleName(ctx, m);
    module_name = JS_AtomToCString(ctx, module_name_atom);
#if 0
    fprintf(stdout, "XXX loaded module: %s\n", module_name);
#endif
    JS_FreeAtom(ctx, module_name_atom);
    if (!module_name) {
        return -1;
    }

    /* realpath() cannot be used with builtin modules
        because the corresponding module source code is not
        necessarily present */
    if (use_realpath) {
        uv_fs_t req;
        r = uv_fs_realpath(NULL, &req, module_name, NULL);
        if (r != 0) {
            uv_fs_req_cleanup(&req);
            JS_ThrowTypeError(ctx, "realpath failure");
            JS_FreeCString(ctx, module_name);
            return -1;
        }
        js__pstrcpy(buf, sizeof(buf), "file://");
        js__pstrcat(buf, sizeof(buf), req.ptr);
        uv_fs_req_cleanup(&req);

        // When using realpath we have the opportunity to extract the dirname
        // and basename and add them to the meta. Since the path is now absolute
        // all we need to do is split on the last path separator.
        const char *start = buf + 7; /* skip file:// */
        char *p = strrchr(start, TJS__PATHSEP);
        strncpy(module_dirname, start, p - start);
        strcpy(module_basename, p + 1);
    } else {
        js__pstrcat(buf, sizeof(buf), module_name);
    }

    JS_FreeCString(ctx, module_name);

    meta_obj = JS_GetImportMeta(ctx, m);
    if (JS_IsException(meta_obj)) {
        return -1;
    }
    JS_DefinePropertyValueStr(ctx, meta_obj, "url", JS_NewString(ctx, buf), JS_PROP_C_W_E);
    JS_DefinePropertyValueStr(ctx, meta_obj, "main", JS_NewBool(ctx, is_main), JS_PROP_C_W_E);
    if (use_realpath) {
        JS_DefinePropertyValueStr(ctx, meta_obj, "dirname", JS_NewString(ctx, module_dirname), JS_PROP_C_W_E);
        JS_DefinePropertyValueStr(ctx, meta_obj, "basename", JS_NewString(ctx, module_basename), JS_PROP_C_W_E);
        JS_DefinePropertyValueStr(ctx, meta_obj, "path", JS_NewString(ctx, buf + 7), JS_PROP_C_W_E);
    }
    JS_FreeValue(ctx, meta_obj);
    return 0;
}

static inline void tjs__normalize_pathsep(const char *name) {
#if defined(_WIN32)
    char *p;

    for (p = name; *p; p++) {
        if (p[0] == TJS__PATHSEP_POSIX) {
            p[0] = TJS__PATHSEP;
        }
    }
#else
    (void) name;
#endif
}

char *tjs_module_normalizer(JSContext *ctx, const char *base_name, const char *name, void *opaque) {
    char *filename, *p;
    const char *r;
    int len;

    const char *builtin = tjs__canonical_builtin_specifier(name);
    if (builtin) {
        return js_strdup(ctx, builtin);
    }

    if (name[0] != '.') {
        if (tjs__is_url_specifier(name) || tjs__path_is_absolute(name)) {
            return js_strdup(ctx, name);
        }
        char *resolved = tjs__resolve_bare_specifier(ctx, base_name, name, true);
        if (resolved) {
            return resolved;
        }
        return js_strdup(ctx, name);
    }

    /* Normalize base_name. This is the path to the importing module, and
     * it should have the platform native path separator.
     */
    tjs__normalize_pathsep(name);

    p = strrchr(base_name, TJS__PATHSEP);
    if (p) {
        len = p - base_name;
    } else {
        len = 0;
    }

    filename = js_malloc(ctx, len + strlen(name) + 1 + 1);
    if (!filename) {
        return NULL;
    }
    memcpy(filename, base_name, len);
    filename[len] = '\0';

    /* we only normalize the leading '..' or '.' */
    r = name;
    for (;;) {
        if (r[0] == '.' && r[1] == TJS__PATHSEP_POSIX) {
            r += 2;
        } else if (r[0] == '.' && r[1] == '.' && r[2] == TJS__PATHSEP_POSIX) {
            /* remove the last path element of filename, except if "."
               or ".." */
            if (filename[0] == '\0') {
                break;
            }
            p = strrchr(filename, TJS__PATHSEP);
            if (!p) {
                p = filename;
            } else {
                p++;
            }
            if (!strcmp(p, ".") || !strcmp(p, "..")) {
                break;
            }
            if (p > filename) {
                p--;
            }
            *p = '\0';
            r += 3;
        } else {
            break;
        }
    }
    if (filename[0] != '\0') {
        strcat(filename, TJS__PATHSEP_STR);
    }
    strcat(filename, r);

    /* Re-normalize the path. The name part will have posix style paths, so
     * normalize it to the platform native separator.
     */
    tjs__normalize_pathsep(filename);

    return filename;
}

#undef TJS__PATHSEP
#undef TJS__PATHSEP_STR
#undef TJS__PATHSEP_POSIX
