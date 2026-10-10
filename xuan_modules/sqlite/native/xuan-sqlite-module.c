#include <limits.h>
#include <sqlite3.h>

#include <quickjs.h>

#ifndef countof
#define countof(x) (sizeof(x) / sizeof((x)[0]))
#endif

static JSClassID xuan_sqlite3_class_id;
static JSClassID xuan_sqlite3_stmt_class_id;

typedef struct {
    sqlite3 *handle;
} XuanSqlite3Handle;

typedef struct {
    sqlite3_stmt *stmt;
} XuanSqlite3Stmt;

static JSValue xuan_throw_sqlite3_errno(JSContext *ctx, int err) {
    JSValue obj = JS_NewError(ctx);
    JS_DefinePropertyValueStr(
        ctx, obj, "message", JS_NewString(ctx, sqlite3_errstr(err)), JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE);
    JS_DefinePropertyValueStr(ctx, obj, "errno", JS_NewInt32(ctx, err), JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE);
    return JS_Throw(ctx, obj);
}

static void xuan_sqlite3_finalizer(JSRuntime *rt, JSValue val) {
    XuanSqlite3Handle *h = JS_GetOpaque(val, xuan_sqlite3_class_id);
    if (!h) {
        return;
    }
    if (h->handle) {
        sqlite3_close(h->handle);
    }
    js_free_rt(rt, h);
}

static void xuan_sqlite3_stmt_finalizer(JSRuntime *rt, JSValue val) {
    XuanSqlite3Stmt *h = JS_GetOpaque(val, xuan_sqlite3_stmt_class_id);
    if (!h) {
        return;
    }
    if (h->stmt) {
        sqlite3_reset(h->stmt);
        sqlite3_finalize(h->stmt);
    }
    js_free_rt(rt, h);
}

static JSClassDef xuan_sqlite3_class = {
    "XuanSqlite3Handle",
    .finalizer = xuan_sqlite3_finalizer,
};

static JSClassDef xuan_sqlite3_stmt_class = {
    "XuanSqlite3Statement",
    .finalizer = xuan_sqlite3_stmt_finalizer,
};

static XuanSqlite3Handle *xuan_sqlite3_get(JSContext *ctx, JSValue obj) {
    return JS_GetOpaque2(ctx, obj, xuan_sqlite3_class_id);
}

static XuanSqlite3Stmt *xuan_sqlite3_stmt_get(JSContext *ctx, JSValue obj) {
    return JS_GetOpaque2(ctx, obj, xuan_sqlite3_stmt_class_id);
}

static JSValue xuan_new_sqlite3(JSContext *ctx, sqlite3 *handle) {
    JSValue obj = JS_NewObjectClass(ctx, xuan_sqlite3_class_id);
    if (JS_IsException(obj)) {
        return obj;
    }
    XuanSqlite3Handle *h = js_mallocz(ctx, sizeof(*h));
    if (!h) {
        JS_FreeValue(ctx, obj);
        return JS_EXCEPTION;
    }
    h->handle = handle;
    JS_SetOpaque(obj, h);
    return obj;
}

static JSValue xuan_new_sqlite3_stmt(JSContext *ctx, sqlite3_stmt *stmt) {
    JSValue obj = JS_NewObjectClass(ctx, xuan_sqlite3_stmt_class_id);
    if (JS_IsException(obj)) {
        return obj;
    }
    XuanSqlite3Stmt *h = js_mallocz(ctx, sizeof(*h));
    if (!h) {
        JS_FreeValue(ctx, obj);
        return JS_EXCEPTION;
    }
    h->stmt = stmt;
    JS_SetOpaque(obj, h);
    return obj;
}

static JSValue xuan_sqlite3_open(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    const char *db_name = JS_ToCString(ctx, argv[0]);
    if (!db_name) {
        return JS_EXCEPTION;
    }

    int flags = 0;
    if (JS_ToInt32(ctx, &flags, argv[1])) {
        JS_FreeCString(ctx, db_name);
        return JS_EXCEPTION;
    }

    sqlite3 *handle = NULL;
    int r = sqlite3_open_v2(db_name, &handle, flags, NULL);
    JS_FreeCString(ctx, db_name);
    if (r != SQLITE_OK) {
        return xuan_throw_sqlite3_errno(ctx, r);
    }

    (void) sqlite3_db_config(handle, SQLITE_DBCONFIG_ENABLE_LOAD_EXTENSION, 1, NULL);
    JSValue obj = xuan_new_sqlite3(ctx, handle);
    if (JS_IsException(obj)) {
        sqlite3_close(handle);
    }
    return obj;
}

static JSValue xuan_sqlite3_close(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    XuanSqlite3Handle *h = xuan_sqlite3_get(ctx, argv[0]);
    if (!h) {
        return JS_EXCEPTION;
    }
    int r = sqlite3_close(h->handle);
    if (r != SQLITE_OK) {
        return xuan_throw_sqlite3_errno(ctx, r);
    }
    h->handle = NULL;
    return JS_UNDEFINED;
}

static JSValue xuan_sqlite3_exec(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    XuanSqlite3Handle *h = xuan_sqlite3_get(ctx, argv[0]);
    if (!h) {
        return JS_EXCEPTION;
    }
    const char *sql = JS_ToCString(ctx, argv[1]);
    if (!sql) {
        return JS_EXCEPTION;
    }
    int r = sqlite3_exec(h->handle, sql, NULL, NULL, NULL);
    JS_FreeCString(ctx, sql);
    if (r != SQLITE_OK) {
        return xuan_throw_sqlite3_errno(ctx, r);
    }
    return JS_UNDEFINED;
}

static JSValue xuan_sqlite3_prepare(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    XuanSqlite3Handle *h = xuan_sqlite3_get(ctx, argv[0]);
    if (!h) {
        return JS_EXCEPTION;
    }
    const char *sql = JS_ToCString(ctx, argv[1]);
    if (!sql) {
        return JS_EXCEPTION;
    }
    sqlite3_stmt *stmt = NULL;
    int r = sqlite3_prepare_v2(h->handle, sql, -1, &stmt, NULL);
    JS_FreeCString(ctx, sql);
    if (r != SQLITE_OK) {
        return xuan_throw_sqlite3_errno(ctx, r);
    }
    JSValue obj = xuan_new_sqlite3_stmt(ctx, stmt);
    if (JS_IsException(obj)) {
        sqlite3_finalize(stmt);
    }
    return obj;
}

static JSValue xuan_sqlite3_in_transaction(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    XuanSqlite3Handle *h = xuan_sqlite3_get(ctx, argv[0]);
    if (!h) {
        return JS_EXCEPTION;
    }
    return JS_NewBool(ctx, !sqlite3_get_autocommit(h->handle));
}

static JSValue xuan_sqlite3_stmt_finalize(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    XuanSqlite3Stmt *h = xuan_sqlite3_stmt_get(ctx, argv[0]);
    if (!h) {
        return JS_EXCEPTION;
    }
    if (!h->stmt) {
        return JS_UNDEFINED;
    }
    sqlite3_reset(h->stmt);
    int r = sqlite3_finalize(h->stmt);
    if (r != SQLITE_OK) {
        return xuan_throw_sqlite3_errno(ctx, r);
    }
    h->stmt = NULL;
    return JS_UNDEFINED;
}

static JSValue xuan_sqlite3_stmt_expand(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    XuanSqlite3Stmt *h = xuan_sqlite3_stmt_get(ctx, argv[0]);
    if (!h) {
        return JS_EXCEPTION;
    }
    if (!h->stmt) {
        return JS_NewString(ctx, "");
    }
    char *sql = sqlite3_expanded_sql(h->stmt);
    if (!sql) {
        return JS_ThrowOutOfMemory(ctx);
    }
    JSValue out = JS_NewString(ctx, sql);
    sqlite3_free(sql);
    return out;
}

static JSValue xuan_stmt_to_obj(JSContext *ctx, XuanSqlite3Stmt *h) {
    JSValue obj = JS_NewObjectProto(ctx, JS_NULL);
    int count = sqlite3_column_count(h->stmt);
    for (int i = 0; i < count; i++) {
        const char *name = sqlite3_column_name(h->stmt, i);
        JSValue value = JS_NULL;
        switch (sqlite3_column_type(h->stmt, i)) {
            case SQLITE_INTEGER:
                value = JS_NewInt64(ctx, sqlite3_column_int64(h->stmt, i));
                break;
            case SQLITE_FLOAT:
                value = JS_NewFloat64(ctx, sqlite3_column_double(h->stmt, i));
                break;
            case SQLITE3_TEXT:
                value = JS_NewString(ctx, (const char *) sqlite3_column_text(h->stmt, i));
                break;
            case SQLITE_BLOB:
                value = JS_NewUint8ArrayCopy(
                    ctx, (uint8_t *) sqlite3_column_blob(h->stmt, i), sqlite3_column_bytes(h->stmt, i));
                break;
            default:
                value = JS_NULL;
                break;
        }
        JS_DefinePropertyValueStr(ctx, obj, name, value, JS_PROP_C_W_E);
    }
    return obj;
}

static JSValue xuan_sqlite3_bind_param(JSContext *ctx, sqlite3_stmt *stmt, int idx, JSValue v) {
    int r;

    switch (JS_VALUE_GET_NORM_TAG(v)) {
        case JS_TAG_BIG_INT: {
            int64_t x;
            r = JS_ToBigInt64(ctx, &x, v);
            if (r == -1) {
                return JS_ThrowTypeError(ctx, "Failed to convert type at position %d", idx);
            }
            r = sqlite3_bind_int64(stmt, idx, x);
            if (r != SQLITE_OK) {
                return xuan_throw_sqlite3_errno(ctx, r);
            }
            break;
        }
        case JS_TAG_STRING: {
            size_t len;
            const char *x = JS_ToCStringLen(ctx, &len, v);
            if (!x) {
                return JS_EXCEPTION;
            }
            r = sqlite3_bind_text(stmt, idx, x, len, SQLITE_TRANSIENT);
            JS_FreeCString(ctx, x);
            if (r != SQLITE_OK) {
                return xuan_throw_sqlite3_errno(ctx, r);
            }
            break;
        }
        case JS_TAG_OBJECT: {
            size_t len = 0;
            const uint8_t *x = JS_GetUint8Array(ctx, &len, v);
            if (!x) {
                return JS_EXCEPTION;
            }
            r = sqlite3_bind_blob(stmt, idx, x, len, SQLITE_TRANSIENT);
            if (r != SQLITE_OK) {
                return xuan_throw_sqlite3_errno(ctx, r);
            }
            break;
        }
        case JS_TAG_INT: {
            int64_t x;
            r = JS_ToInt64(ctx, &x, v);
            if (r == -1) {
                return JS_ThrowTypeError(ctx, "Failed to convert type at position %d", idx);
            }
            if (x < INT_MIN || x > INT_MAX) {
                r = sqlite3_bind_int64(stmt, idx, x);
            } else {
                r = sqlite3_bind_int(stmt, idx, x);
            }
            if (r != SQLITE_OK) {
                return xuan_throw_sqlite3_errno(ctx, r);
            }
            break;
        }
        case JS_TAG_BOOL: {
            r = JS_ToBool(ctx, v);
            if (r == -1) {
                return JS_ThrowTypeError(ctx, "Failed to convert type at position %d", idx);
            }
            r = sqlite3_bind_int(stmt, idx, r);
            if (r != SQLITE_OK) {
                return xuan_throw_sqlite3_errno(ctx, r);
            }
            break;
        }
        case JS_TAG_NULL: {
            r = sqlite3_bind_null(stmt, idx);
            if (r != SQLITE_OK) {
                return xuan_throw_sqlite3_errno(ctx, r);
            }
            break;
        }
        case JS_TAG_FLOAT64: {
            double x;
            r = JS_ToFloat64(ctx, &x, v);
            if (r == -1) {
                return JS_ThrowTypeError(ctx, "Failed to convert type at position %d", idx);
            }
            r = sqlite3_bind_double(stmt, idx, x);
            if (r != SQLITE_OK) {
                return xuan_throw_sqlite3_errno(ctx, r);
            }
            break;
        }
        default:
            return JS_ThrowTypeError(ctx, "Invalid bound parameter type at position %d", idx);
    }

    return JS_UNDEFINED;
}

static JSValue xuan_sqlite3_bind_params(JSContext *ctx, sqlite3_stmt *stmt, JSValue params) {
    sqlite3_clear_bindings(stmt);

    if (JS_IsArray(params)) {
        JSValue js_length = JS_GetPropertyStr(ctx, params, "length");
        uint64_t len;
        if (JS_ToIndex(ctx, &len, js_length)) {
            JS_FreeValue(ctx, js_length);
            return JS_EXCEPTION;
        }
        JS_FreeValue(ctx, js_length);
        for (uint32_t i = 0; i < len; i++) {
            JSValue v = JS_GetPropertyUint32(ctx, params, i);
            if (JS_IsException(v)) {
                return JS_EXCEPTION;
            }
            bool is_exception = JS_IsException(xuan_sqlite3_bind_param(ctx, stmt, i + 1, v));
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
        for (uint32_t i = 0; i < plen; i++) {
            JSAtom patom = ptab[i].atom;
            JSValue prop = JS_GetProperty(ctx, params, patom);
            if (JS_IsException(prop)) {
                JS_FreePropertyEnum(ctx, ptab, plen);
                return JS_EXCEPTION;
            }
            const char *key = JS_AtomToCString(ctx, patom);
            int idx = sqlite3_bind_parameter_index(stmt, key);
            if (idx == 0 || JS_IsException(xuan_sqlite3_bind_param(ctx, stmt, idx, prop))) {
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

static JSValue xuan_sqlite3_stmt_all(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    XuanSqlite3Stmt *h = xuan_sqlite3_stmt_get(ctx, argv[0]);
    if (!h) {
        return JS_EXCEPTION;
    }
    if (!h->stmt) {
        return JS_ThrowInternalError(ctx, "Statement has been finalized");
    }
    int r = sqlite3_reset(h->stmt);
    if (r != SQLITE_OK) {
        return xuan_throw_sqlite3_errno(ctx, r);
    }
    if (argc == 2) {
        if (JS_IsException(xuan_sqlite3_bind_params(ctx, h->stmt, argv[1]))) {
            return JS_EXCEPTION;
        }
    }
    JSValue result = JS_NewArray(ctx);
    uint32_t i = 0;
    while ((r = sqlite3_step(h->stmt)) == SQLITE_ROW) {
        JS_DefinePropertyValueUint32(ctx, result, i++, xuan_stmt_to_obj(ctx, h), JS_PROP_C_W_E);
    }
    if (r != SQLITE_OK && r != SQLITE_DONE) {
        JS_FreeValue(ctx, result);
        return xuan_throw_sqlite3_errno(ctx, r);
    }
    return result;
}

static JSValue xuan_sqlite3_stmt_run(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    XuanSqlite3Stmt *h = xuan_sqlite3_stmt_get(ctx, argv[0]);
    if (!h) {
        return JS_EXCEPTION;
    }
    if (!h->stmt) {
        return JS_ThrowInternalError(ctx, "Statement has been finalized");
    }
    int r = sqlite3_reset(h->stmt);
    if (r != SQLITE_OK) {
        return xuan_throw_sqlite3_errno(ctx, r);
    }
    if (argc == 2) {
        if (JS_IsException(xuan_sqlite3_bind_params(ctx, h->stmt, argv[1]))) {
            return JS_EXCEPTION;
        }
    }
    r = sqlite3_step(h->stmt);
    if (r != SQLITE_OK && r != SQLITE_DONE && r != SQLITE_ROW) {
        return xuan_throw_sqlite3_errno(ctx, r);
    }
    return JS_UNDEFINED;
}

static JSValue xuan_sqlite3_load_extension(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    XuanSqlite3Handle *h = xuan_sqlite3_get(ctx, argv[0]);
    if (!h) {
        return JS_EXCEPTION;
    }
    const char *zfile = JS_ToCString(ctx, argv[1]);
    const char *zproc = JS_IsUndefined(argv[2]) ? NULL : JS_ToCString(ctx, argv[2]);
    if (!zfile) {
        return JS_EXCEPTION;
    }
    int r = sqlite3_load_extension(h->handle, zfile, zproc, NULL);
    JS_FreeCString(ctx, zfile);
    if (zproc) {
        JS_FreeCString(ctx, zproc);
    }
    if (r != SQLITE_OK) {
        return xuan_throw_sqlite3_errno(ctx, r);
    }
    return JS_UNDEFINED;
}

static const JSCFunctionListEntry xuan_sqlite3_exports[] = {
    JS_CFUNC_DEF("open", 2, xuan_sqlite3_open),
    JS_CFUNC_DEF("close", 1, xuan_sqlite3_close),
    JS_CFUNC_DEF("exec", 2, xuan_sqlite3_exec),
    JS_CFUNC_DEF("prepare", 2, xuan_sqlite3_prepare),
    JS_CFUNC_DEF("in_transaction", 1, xuan_sqlite3_in_transaction),
    JS_CFUNC_DEF("load_extension", 3, xuan_sqlite3_load_extension),
    JS_CFUNC_DEF("stmt_finalize", 1, xuan_sqlite3_stmt_finalize),
    JS_CFUNC_DEF("stmt_expand", 1, xuan_sqlite3_stmt_expand),
    JS_CFUNC_DEF("stmt_all", 2, xuan_sqlite3_stmt_all),
    JS_CFUNC_DEF("stmt_run", 2, xuan_sqlite3_stmt_run),
    JS_PROP_INT32_DEF("SQLITE_OPEN_CREATE", SQLITE_OPEN_CREATE, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("SQLITE_OPEN_READONLY", SQLITE_OPEN_READONLY, JS_PROP_ENUMERABLE),
    JS_PROP_INT32_DEF("SQLITE_OPEN_READWRITE", SQLITE_OPEN_READWRITE, JS_PROP_ENUMERABLE),
};

static int xuan_sqlite_module_init(JSContext *ctx, JSModuleDef *m) {
    JSRuntime *rt = JS_GetRuntime(ctx);

    JS_NewClassID(rt, &xuan_sqlite3_class_id);
    JS_NewClass(rt, xuan_sqlite3_class_id, &xuan_sqlite3_class);
    JS_SetClassProto(ctx, xuan_sqlite3_class_id, JS_NULL);

    JS_NewClassID(rt, &xuan_sqlite3_stmt_class_id);
    JS_NewClass(rt, xuan_sqlite3_stmt_class_id, &xuan_sqlite3_stmt_class);
    JS_SetClassProto(ctx, xuan_sqlite3_stmt_class_id, JS_NULL);

    return JS_SetModuleExportList(ctx, m, xuan_sqlite3_exports, countof(xuan_sqlite3_exports));
}

JSModuleDef *js_init_module(JSContext *ctx, const char *module_name) {
    JSModuleDef *m = JS_NewCModule(ctx, module_name, xuan_sqlite_module_init);
    if (!m) {
        return NULL;
    }
    JS_AddModuleExportList(ctx, m, xuan_sqlite3_exports, countof(xuan_sqlite3_exports));
    return m;
}
