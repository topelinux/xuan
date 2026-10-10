/*
 * txiki.js
 *
 * Copyright (c) 2023-present Saúl Ibarra Corretgé <s@saghul.net>
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

#ifndef TJS_DISABLE_SQLITE3

#include "private.h"

#include <sqlite3.h>

#ifdef TJS_SQLITE3_DYNAMIC
typedef struct {
    int attempted;
    int loaded;
    uv_lib_t lib;
    int (*open_v2)(const char *, sqlite3 **, int, const char *);
    int (*close)(sqlite3 *);
    int (*db_config)(sqlite3 *, int, ...);
    int (*load_extension)(sqlite3 *, const char *, const char *, char **);
    int (*exec)(sqlite3 *, const char *, int (*)(void *, int, char **, char **), void *, char **);
    int (*prepare_v2)(sqlite3 *, const char *, int, sqlite3_stmt **, const char **);
    int (*get_autocommit)(sqlite3 *);
    int (*reset)(sqlite3_stmt *);
    int (*finalize)(sqlite3_stmt *);
    char *(*expanded_sql)(sqlite3_stmt *);
    void (*free)(void *);
    int (*column_count)(sqlite3_stmt *);
    const char *(*column_name)(sqlite3_stmt *, int);
    int (*column_type)(sqlite3_stmt *, int);
    sqlite3_int64 (*column_int64)(sqlite3_stmt *, int);
    double (*column_double)(sqlite3_stmt *, int);
    const unsigned char *(*column_text)(sqlite3_stmt *, int);
    const void *(*column_blob)(sqlite3_stmt *, int);
    int (*column_bytes)(sqlite3_stmt *, int);
    int (*bind_int64)(sqlite3_stmt *, int, sqlite3_int64);
    int (*bind_text)(sqlite3_stmt *, int, const char *, int, void (*)(void *));
    int (*bind_blob)(sqlite3_stmt *, int, const void *, int, void (*)(void *));
    int (*bind_int)(sqlite3_stmt *, int, int);
    int (*bind_null)(sqlite3_stmt *, int);
    int (*bind_double)(sqlite3_stmt *, int, double);
    int (*clear_bindings)(sqlite3_stmt *);
    int (*bind_parameter_index)(sqlite3_stmt *, const char *);
    int (*step)(sqlite3_stmt *);
    const char *(*errstr)(int);
} TJSSqlite3DynApi;

static TJSSqlite3DynApi tjs_sqlite3_api;

#define SQLITE3_SYM_LIST(V)                                                                                           \
    V(open_v2, "sqlite3_open_v2")                                                                                     \
    V(close, "sqlite3_close")                                                                                         \
    V(db_config, "sqlite3_db_config")                                                                                 \
    V(exec, "sqlite3_exec")                                                                                           \
    V(prepare_v2, "sqlite3_prepare_v2")                                                                               \
    V(get_autocommit, "sqlite3_get_autocommit")                                                                       \
    V(reset, "sqlite3_reset")                                                                                         \
    V(finalize, "sqlite3_finalize")                                                                                   \
    V(expanded_sql, "sqlite3_expanded_sql")                                                                           \
    V(free, "sqlite3_free")                                                                                           \
    V(column_count, "sqlite3_column_count")                                                                           \
    V(column_name, "sqlite3_column_name")                                                                             \
    V(column_type, "sqlite3_column_type")                                                                             \
    V(column_int64, "sqlite3_column_int64")                                                                           \
    V(column_double, "sqlite3_column_double")                                                                         \
    V(column_text, "sqlite3_column_text")                                                                             \
    V(column_blob, "sqlite3_column_blob")                                                                             \
    V(column_bytes, "sqlite3_column_bytes")                                                                           \
    V(bind_int64, "sqlite3_bind_int64")                                                                               \
    V(bind_text, "sqlite3_bind_text")                                                                                 \
    V(bind_blob, "sqlite3_bind_blob")                                                                                 \
    V(bind_int, "sqlite3_bind_int")                                                                                   \
    V(bind_null, "sqlite3_bind_null")                                                                                 \
    V(bind_double, "sqlite3_bind_double")                                                                             \
    V(clear_bindings, "sqlite3_clear_bindings")                                                                       \
    V(bind_parameter_index, "sqlite3_bind_parameter_index")                                                           \
    V(step, "sqlite3_step")                                                                                            \
    V(errstr, "sqlite3_errstr")

static int tjs__sqlite3_dynamic_load(JSContext *ctx) {
    if (tjs_sqlite3_api.loaded) {
        return 0;
    }
    if (tjs_sqlite3_api.attempted) {
        JS_ThrowInternalError(ctx, "sqlite3 dynamic loading failed in this process");
        return -1;
    }
    tjs_sqlite3_api.attempted = 1;

#ifdef _WIN32
    const char *candidates[] = {"sqlite3.dll", NULL};
#elif defined(__APPLE__)
    const char *candidates[] = {"libsqlite3.dylib", "/usr/lib/libsqlite3.dylib", NULL};
#else
    const char *candidates[] = {"libsqlite3.so.0", "libsqlite3.so", NULL};
#endif

    int loaded = -1;
    for (int i = 0; candidates[i] != NULL; i++) {
        if (uv_dlopen(candidates[i], &tjs_sqlite3_api.lib) == 0) {
            loaded = 0;
            break;
        }
    }
    if (loaded != 0) {
        JS_ThrowInternalError(ctx, "failed to load sqlite3 shared library");
        return -1;
    }

#define LOAD_SYM(field, symbol)                                                                                       \
    do {                                                                                                              \
        void *ptr = NULL;                                                                                             \
        if (uv_dlsym(&tjs_sqlite3_api.lib, symbol, &ptr) != 0 || ptr == NULL) {                                      \
            JS_ThrowInternalError(ctx, "failed to resolve sqlite3 symbol '%s'", symbol);                             \
            return -1;                                                                                                \
        }                                                                                                             \
        tjs_sqlite3_api.field = ptr;                                                                                  \
    } while (0);
    SQLITE3_SYM_LIST(LOAD_SYM)
#undef LOAD_SYM
    {
        void *ptr = NULL;
        if (uv_dlsym(&tjs_sqlite3_api.lib, "sqlite3_load_extension", &ptr) == 0 && ptr != NULL) {
            tjs_sqlite3_api.load_extension = ptr;
        }
    }

    tjs_sqlite3_api.loaded = 1;
    return 0;
}

#define ENSURE_SQLITE3_LOADED(ctx)                                                                                    \
    do {                                                                                                              \
        if (tjs__sqlite3_dynamic_load(ctx) != 0) {                                                                    \
            return JS_EXCEPTION;                                                                                      \
        }                                                                                                             \
    } while (0)

#define SQLITE3(name) (tjs_sqlite3_api.name)
#else
#define ENSURE_SQLITE3_LOADED(ctx)
#define SQLITE3(name) sqlite3_##name
#endif


static JSClassID tjs_sqlite3_class_id;

typedef struct {
    sqlite3 *handle;
} TJSSqlite3Handle;

static void tjs_sqlite3_finalizer(JSRuntime *rt, JSValue val) {
    TJSSqlite3Handle *h = JS_GetOpaque(val, tjs_sqlite3_class_id);
    if (!h) {
        return;
    }
    if (h->handle) {
        SQLITE3(close)(h->handle);
    }
    js_free_rt(rt, h);
}

static JSClassDef tjs_sqlite3_class = {
    "Handle",
    .finalizer = tjs_sqlite3_finalizer,
};

static JSValue tjs_new_sqlite3(JSContext *ctx, sqlite3 *handle) {
    TJSSqlite3Handle *h;
    JSValue obj;

    obj = JS_NewObjectClass(ctx, tjs_sqlite3_class_id);
    if (JS_IsException(obj)) {
        return obj;
    }

    h = js_mallocz(ctx, sizeof(*h));
    if (!h) {
        JS_FreeValue(ctx, obj);
        return JS_EXCEPTION;
    }

    h->handle = handle;

    JS_SetOpaque(obj, h);
    return obj;
}

static TJSSqlite3Handle *tjs_sqlite3_get(JSContext *ctx, JSValue obj) {
    return JS_GetOpaque2(ctx, obj, tjs_sqlite3_class_id);
}

static JSClassID tjs_sqlite3_stmt_class_id;

typedef struct {
    sqlite3_stmt *stmt;
} TJSSqlite3Stmt;

static void tjs_sqlite3_stmt_finalizer(JSRuntime *rt, JSValue val) {
    TJSSqlite3Stmt *h = JS_GetOpaque(val, tjs_sqlite3_stmt_class_id);
    if (!h) {
        return;
    }
    if (h->stmt) {
        SQLITE3(reset)(h->stmt);
        SQLITE3(finalize)(h->stmt);
    }
    js_free_rt(rt, h);
}

static JSClassDef tjs_sqlite3_stmt_class = {
    "Statement",
    .finalizer = tjs_sqlite3_stmt_finalizer,
};

static JSValue tjs_new_sqlite3_stmt(JSContext *ctx, sqlite3_stmt *stmt) {
    TJSSqlite3Stmt *h;
    JSValue obj;

    obj = JS_NewObjectClass(ctx, tjs_sqlite3_stmt_class_id);
    if (JS_IsException(obj)) {
        return obj;
    }

    h = js_mallocz(ctx, sizeof(*h));
    if (!h) {
        JS_FreeValue(ctx, obj);
        return JS_EXCEPTION;
    }

    h->stmt = stmt;

    JS_SetOpaque(obj, h);
    return obj;
}

static TJSSqlite3Stmt *tjs_sqlite3_stmt_get(JSContext *ctx, JSValue obj) {
    return JS_GetOpaque2(ctx, obj, tjs_sqlite3_stmt_class_id);
}

JSValue tjs_throw_sqlite3_errno(JSContext *ctx, int err) {
    JSValue obj;
    const char *errstr = "sqlite3 error";
#ifdef TJS_SQLITE3_DYNAMIC
    if (tjs_sqlite3_api.loaded && tjs_sqlite3_api.errstr) {
        errstr = SQLITE3(errstr)(err);
    }
#else
    errstr = SQLITE3(errstr)(err);
#endif
    obj = JS_NewError(ctx);
    JS_DefinePropertyValueStr(ctx,
                              obj,
                              "message",
                              JS_NewString(ctx, errstr),
                              JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE);
    JS_DefinePropertyValueStr(ctx, obj, "errno", JS_NewInt32(ctx, err), JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE);
    if (JS_IsException(obj)) {
        obj = JS_NULL;
    }
    return JS_Throw(ctx, obj);
}

static JSValue tjs_sqlite3_open(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    ENSURE_SQLITE3_LOADED(ctx);

    const char *db_name = JS_ToCString(ctx, argv[0]);

    if (!db_name) {
        return JS_EXCEPTION;
    }

    int flags;
    if (JS_ToInt32(ctx, &flags, argv[1])) {
        JS_FreeCString(ctx, db_name);
        return JS_EXCEPTION;
    }

    sqlite3 *handle = NULL;
    int r = SQLITE3(open_v2)(db_name, &handle, flags, NULL);

    JS_FreeCString(ctx, db_name);

    if (r != SQLITE_OK) {
        return tjs_throw_sqlite3_errno(ctx, r);
    }

    // Enable sqlite extensions (but only via C calls)
    if (SQLITE3(db_config)) {
        (void) SQLITE3(db_config)(handle, SQLITE_DBCONFIG_ENABLE_LOAD_EXTENSION, 1, NULL);
    }

    JSValue obj = tjs_new_sqlite3(ctx, handle);
    if (JS_IsException(obj)) {
        SQLITE3(close)(handle);
    }

    return obj;
}

static JSValue tjs_sqlite3_close(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    TJSSqlite3Handle *h = tjs_sqlite3_get(ctx, argv[0]);

    if (!h) {
        return JS_EXCEPTION;
    }

    int r = SQLITE3(close)(h->handle);
    if (r != SQLITE_OK) {
        return tjs_throw_sqlite3_errno(ctx, r);
    }

    h->handle = NULL;

    return JS_UNDEFINED;
}

static JSValue tjs_sqlite3_load_extension(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    TJSSqlite3Handle *h = tjs_sqlite3_get(ctx, argv[0]);

    if (!h) {
        return JS_EXCEPTION;
    }

    const char *zFile = JS_ToCString(ctx, argv[1]);
    const char *zProc = JS_IsUndefined(argv[2]) ? NULL : JS_ToCString(ctx, argv[2]);

    if (!zFile) {
        return JS_EXCEPTION;
    }
    if (!SQLITE3(load_extension)) {
        JS_FreeCString(ctx, zFile);
        if (zProc) {
            JS_FreeCString(ctx, zProc);
        }
        return JS_ThrowInternalError(ctx, "sqlite3_load_extension is not available in this sqlite3 shared library");
    }

    // zProc can be 0, it means "sqlite, do your best to quess it"

    int r = SQLITE3(load_extension)(h->handle, zFile, zProc, NULL);

    JS_FreeCString(ctx, zFile);
    if (zProc) {
        JS_FreeCString(ctx, zProc);
    }

    if (r != SQLITE_OK) {
        return tjs_throw_sqlite3_errno(ctx, r);
    }

    return JS_UNDEFINED;
}

static JSValue tjs_sqlite3_exec(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    TJSSqlite3Handle *h = tjs_sqlite3_get(ctx, argv[0]);

    if (!h) {
        return JS_EXCEPTION;
    }

    const char *sql = JS_ToCString(ctx, argv[1]);

    if (!sql) {
        return JS_EXCEPTION;
    }

    int r = SQLITE3(exec)(h->handle, sql, NULL, NULL, NULL);

    JS_FreeCString(ctx, sql);

    if (r != SQLITE_OK) {
        return tjs_throw_sqlite3_errno(ctx, r);
    }

    return JS_UNDEFINED;
}

static JSValue tjs_sqlite3_prepare(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    TJSSqlite3Handle *h = tjs_sqlite3_get(ctx, argv[0]);

    if (!h) {
        return JS_EXCEPTION;
    }

    const char *sql = JS_ToCString(ctx, argv[1]);

    if (!sql) {
        return JS_EXCEPTION;
    }

    sqlite3_stmt *stmt = NULL;
    int r = SQLITE3(prepare_v2)(h->handle, sql, -1, &stmt, NULL);

    JS_FreeCString(ctx, sql);

    if (r != SQLITE_OK) {
        return tjs_throw_sqlite3_errno(ctx, r);
    }

    JSValue obj = tjs_new_sqlite3_stmt(ctx, stmt);
    if (JS_IsException(obj)) {
        SQLITE3(finalize)(stmt);
    }

    return obj;
}

static JSValue tjs_sqlite3_in_transaction(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    TJSSqlite3Handle *h = tjs_sqlite3_get(ctx, argv[0]);

    if (!h) {
        return JS_EXCEPTION;
    }

    return JS_NewBool(ctx, !SQLITE3(get_autocommit)(h->handle));
}

static JSValue tjs_sqlite3_stmt_finalize(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    TJSSqlite3Stmt *h = tjs_sqlite3_stmt_get(ctx, argv[0]);

    if (!h) {
        return JS_EXCEPTION;
    }

    if (!h->stmt) {
        return JS_UNDEFINED;
    }

    SQLITE3(reset)(h->stmt);

    int r = SQLITE3(finalize)(h->stmt);
    if (r != SQLITE_OK) {
        return tjs_throw_sqlite3_errno(ctx, r);
    }

    h->stmt = NULL;

    return JS_UNDEFINED;
}

static JSValue tjs_sqlite3_stmt_expand(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    TJSSqlite3Stmt *h = tjs_sqlite3_stmt_get(ctx, argv[0]);

    if (!h) {
        return JS_EXCEPTION;
    }

    if (!h->stmt) {
        return JS_NewString(ctx, "");
    }

    char *sql = SQLITE3(expanded_sql)(h->stmt);
    if (sql == NULL) {
        return JS_ThrowOutOfMemory(ctx);
    }
    JSValue ret = JS_NewString(ctx, sql);
    SQLITE3(free)(sql);
    return ret;
}

static JSValue tjs__stmt2obj(JSContext *ctx, TJSSqlite3Stmt *h) {
    JSValue obj = JS_NewObjectProto(ctx, JS_NULL);
    int count = SQLITE3(column_count)(h->stmt);

    for (int i = 0; i < count; i++) {
        const char *name = SQLITE3(column_name)(h->stmt, i);
        JSValue value;

        switch (SQLITE3(column_type)(h->stmt, i)) {
            case SQLITE_INTEGER: {
                value = JS_NewInt64(ctx, SQLITE3(column_int64)(h->stmt, i));
                break;
            }
            case SQLITE_FLOAT: {
                value = JS_NewFloat64(ctx, SQLITE3(column_double)(h->stmt, i));
                break;
            }
            case SQLITE3_TEXT: {
                value = JS_NewString(ctx, (const char *) SQLITE3(column_text)(h->stmt, i));
                break;
            }
            case SQLITE_BLOB: {
                value = JS_NewUint8ArrayCopy(ctx,
                                             (uint8_t *) SQLITE3(column_blob)(h->stmt, i),
                                             SQLITE3(column_bytes)(h->stmt, i));
                break;
            }
            default: {
                value = JS_NULL;
                break;
            }
        }

        JS_DefinePropertyValueStr(ctx, obj, name, value, JS_PROP_C_W_E);
    }

    return obj;
}

static JSValue tjs__sqlite3_bind_param(JSContext *ctx, sqlite3_stmt *stmt, int idx, JSValue v) {
    int r;

#define CHECK_VALUE(ret, i)                                                                                            \
    if (ret == -1) {                                                                                                   \
        return JS_ThrowTypeError(ctx, "Failed to convert type at position %d", idx);                                   \
    }

#define CHECK_RET(ret)                                                                                                 \
    if (r != SQLITE_OK) {                                                                                              \
        return tjs_throw_sqlite3_errno(ctx, ret);                                                                      \
    }

    switch (JS_VALUE_GET_NORM_TAG(v)) {
        case JS_TAG_BIG_INT: {
            int64_t x;
            r = JS_ToBigInt64(ctx, &x, v);
            CHECK_VALUE(r, idx);
            r = SQLITE3(bind_int64)(stmt, idx, x);
            CHECK_RET(r);
            break;
        }
        case JS_TAG_STRING: {
            size_t len;
            const char *x = JS_ToCStringLen(ctx, &len, v);
            if (!x) {
                return JS_EXCEPTION;
            }
            r = SQLITE3(bind_text)(stmt, idx, x, len, SQLITE_TRANSIENT);
            JS_FreeCString(ctx, x);
            CHECK_RET(r);
            break;
        }
        case JS_TAG_OBJECT: {
            size_t len = 0;
            const uint8_t *x = JS_GetUint8Array(ctx, &len, v);
            if (!x) {
                return JS_EXCEPTION;
            }
            r = SQLITE3(bind_blob)(stmt, idx, x, len, SQLITE_TRANSIENT);
            CHECK_RET(r);
            break;
        }
        case JS_TAG_INT: {
            int64_t x;
            r = JS_ToInt64(ctx, &x, v);
            CHECK_VALUE(r, idx);
            if (x < INT_MIN || x > INT_MAX) {
                r = SQLITE3(bind_int64)(stmt, idx, x);
            } else {
                r = SQLITE3(bind_int)(stmt, idx, x);
            }
            CHECK_RET(r);
            break;
        }
        case JS_TAG_BOOL: {
            r = JS_ToBool(ctx, v);
            CHECK_VALUE(r, idx);
            r = SQLITE3(bind_int)(stmt, idx, r);
            CHECK_RET(r);
            break;
        }
        case JS_TAG_NULL: {
            r = SQLITE3(bind_null)(stmt, idx);
            CHECK_RET(r);
            break;
        }
        case JS_TAG_FLOAT64: {
            double x;
            r = JS_ToFloat64(ctx, &x, v);
            CHECK_VALUE(r, idx);
            r = SQLITE3(bind_double)(stmt, idx, x);
            CHECK_RET(r);
            break;
        }
        default:
            return JS_ThrowTypeError(ctx, "Invalid bound parameter type at position %d", idx);
    }

    return JS_UNDEFINED;

#undef CHECK_VALUE
#undef CHECK_RET
}

static JSValue tjs__sqlite3_bind_params(JSContext *ctx, sqlite3_stmt *stmt, JSValue params) {
    SQLITE3(clear_bindings)(stmt);

    if (JS_IsArray(params)) {
        JSValue js_length = JS_GetPropertyStr(ctx, params, "length");
        uint64_t len;
        if (JS_ToIndex(ctx, &len, js_length)) {
            JS_FreeValue(ctx, js_length);
            return JS_EXCEPTION;
        }
        JS_FreeValue(ctx, js_length);
        for (int i = 0; i < len; i++) {
            JSValue v = JS_GetPropertyUint32(ctx, params, i);
            if (JS_IsException(v)) {
                return v;
            }
            bool is_exception = JS_IsException(tjs__sqlite3_bind_param(ctx, stmt, i + 1, v));
            JS_FreeValue(ctx, v);
            if (is_exception) {
                return JS_EXCEPTION;
            }
        }
    } else if (JS_IsObject(params)) {
        JSPropertyEnum *ptab;
        uint32_t plen;
        if (JS_GetOwnPropertyNames(ctx, &ptab, &plen, params, JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY)) {
            return JS_EXCEPTION;
        }
        for (int i = 0; i < plen; i++) {
            JSAtom patom = ptab[i].atom;
            JSValue prop = JS_GetProperty(ctx, params, patom);
            if (JS_IsException(prop)) {
                JS_FreePropertyEnum(ctx, ptab, plen);
                return JS_EXCEPTION;
            }
            const char *key = JS_AtomToCString(ctx, patom);
            int idx = SQLITE3(bind_parameter_index)(stmt, key);
            if (idx == 0 || JS_IsException(tjs__sqlite3_bind_param(ctx, stmt, idx, prop))) {
                if (idx == 0) {
                    JS_ThrowReferenceError(ctx, "Could not find parameter '%s'", key);
                }
                JS_FreeValue(ctx, prop);
                JS_FreeCString(ctx, key);
                JS_FreePropertyEnum(ctx, ptab, plen);
                return JS_EXCEPTION;
            }
            JS_FreeValue(ctx, prop);
            JS_FreeCString(ctx, key);
        }
        JS_FreePropertyEnum(ctx, ptab, plen);
    } else {
        return JS_ThrowTypeError(ctx, "Invalid bind parameters type: expected object or array");
    }

    return JS_UNDEFINED;
}

static JSValue tjs_sqlite3_stmt_all(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    TJSSqlite3Stmt *h = tjs_sqlite3_stmt_get(ctx, argv[0]);

    if (!h) {
        return JS_EXCEPTION;
    }

    if (!h->stmt) {
        return JS_ThrowInternalError(ctx, "Statement has been finalized");
    }

    int r = SQLITE3(reset)(h->stmt);
    if (r != SQLITE_OK) {
        return tjs_throw_sqlite3_errno(ctx, r);
    }

    if (argc == 2) {
        JSValue params = argv[1];

        if (JS_IsException(tjs__sqlite3_bind_params(ctx, h->stmt, params))) {
            return JS_EXCEPTION;
        }
    }

    JSValue result = JS_NewArray(ctx);
    uint32_t i = 0;

    while ((r = SQLITE3(step)(h->stmt)) == SQLITE_ROW) {
        JS_DefinePropertyValueUint32(ctx, result, i, tjs__stmt2obj(ctx, h), JS_PROP_C_W_E);
        i++;
    }

    if (r != SQLITE_OK && r != SQLITE_DONE) {
        JS_FreeValue(ctx, result);
        return tjs_throw_sqlite3_errno(ctx, r);
    }

    return result;
}

static JSValue tjs_sqlite3_stmt_run(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    TJSSqlite3Stmt *h = tjs_sqlite3_stmt_get(ctx, argv[0]);

    if (!h) {
        return JS_EXCEPTION;
    }

    if (!h->stmt) {
        return JS_ThrowInternalError(ctx, "Statement has been finalized");
    }

    int r = SQLITE3(reset)(h->stmt);
    if (r != SQLITE_OK) {
        return tjs_throw_sqlite3_errno(ctx, r);
    }

    if (argc == 2) {
        JSValue params = argv[1];

        if (JS_IsException(tjs__sqlite3_bind_params(ctx, h->stmt, params))) {
            return JS_EXCEPTION;
        }
    }

    r = SQLITE3(step)(h->stmt);
    if (r != SQLITE_OK && r != SQLITE_DONE && r != SQLITE_ROW) {
        return tjs_throw_sqlite3_errno(ctx, r);
    }

    return JS_UNDEFINED;
}

static const JSCFunctionListEntry tjs_sqlite3_funcs[] = {
    TJS_CFUNC_DEF("open", 2, tjs_sqlite3_open),
    TJS_CFUNC_DEF("load_extension", 3, tjs_sqlite3_load_extension),
    TJS_CFUNC_DEF("close", 1, tjs_sqlite3_close),
    TJS_CFUNC_DEF("exec", 2, tjs_sqlite3_exec),
    TJS_CFUNC_DEF("prepare", 2, tjs_sqlite3_prepare),
    TJS_CFUNC_DEF("in_transaction", 1, tjs_sqlite3_in_transaction),
    TJS_CFUNC_DEF("stmt_finalize", 1, tjs_sqlite3_stmt_finalize),
    TJS_CFUNC_DEF("stmt_expand", 1, tjs_sqlite3_stmt_expand),
    TJS_CFUNC_DEF("stmt_all", 2, tjs_sqlite3_stmt_all),
    TJS_CFUNC_DEF("stmt_run", 2, tjs_sqlite3_stmt_run),
    TJS_CONST(SQLITE_OPEN_CREATE),
    TJS_CONST(SQLITE_OPEN_READONLY),
    TJS_CONST(SQLITE_OPEN_READWRITE),
};

void tjs__mod_sqlite3_init(JSContext *ctx, JSValue ns) {
    JSRuntime *rt = JS_GetRuntime(ctx);

    /* Handle object */
    JS_NewClassID(rt, &tjs_sqlite3_class_id);
    JS_NewClass(rt, tjs_sqlite3_class_id, &tjs_sqlite3_class);
    JS_SetClassProto(ctx, tjs_sqlite3_class_id, JS_NULL);

    /* Statement object */
    JS_NewClassID(rt, &tjs_sqlite3_stmt_class_id);
    JS_NewClass(rt, tjs_sqlite3_stmt_class_id, &tjs_sqlite3_stmt_class);
    JS_SetClassProto(ctx, tjs_sqlite3_stmt_class_id, JS_NULL);

    JSValue obj = JS_NewObjectProto(ctx, JS_NULL);
    JS_SetPropertyFunctionList(ctx, obj, tjs_sqlite3_funcs, countof(tjs_sqlite3_funcs));

    JS_DefinePropertyValueStr(ctx, ns, "sqlite3", obj, JS_PROP_C_W_E);
}

#endif /* TJS_DISABLE_SQLITE3 */
