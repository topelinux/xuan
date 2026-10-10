#include <quickjs.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wasm_export.h>

#ifndef countof
#define countof(x) (sizeof(x) / sizeof((x)[0]))
#endif

#define XUAN__WASM_MAX_ARGS       32
#define XUAN__WASM_ERROR_BUF_SIZE 256

static JSClassID xuan_wasm_module_class_id;
static JSClassID xuan_wasm_instance_class_id;

static bool xuan_wasm_runtime_initialized;
static bool xuan_wasm_runtime_atexit_registered;

typedef struct {
    wasm_module_t module;
    struct {
        uint8_t *bytes;
        size_t size;
    } data;
    struct {
        char **argv;
        uint32_t argc;
        char **env;
        uint32_t env_count;
        char **map_dir_list;
        uint32_t map_dir_count;
    } wasi;
} XuanWasmModule;

typedef struct {
    wasm_module_inst_t module_inst;
    wasm_exec_env_t exec_env;
} XuanWasmInstance;

static void *xuan_wasm_malloc(unsigned int size) {
    return malloc(size);
}

static void *xuan_wasm_realloc(void *ptr, unsigned int size) {
    return realloc(ptr, size);
}

static void xuan_wasm_free(void *ptr) {
    free(ptr);
}

static void xuan_wasm_runtime_destroy_once(void) {
    if (xuan_wasm_runtime_initialized) {
        wasm_runtime_destroy();
        xuan_wasm_runtime_initialized = false;
    }
}

static int xuan_wasm_ensure_runtime_initialized(JSContext *ctx) {
    if (xuan_wasm_runtime_initialized) {
        return 0;
    }

    RuntimeInitArgs wasm_init_args;
    memset(&wasm_init_args, 0, sizeof(wasm_init_args));
    wasm_init_args.mem_alloc_type = Alloc_With_Allocator;
    wasm_init_args.mem_alloc_option.allocator.malloc_func = xuan_wasm_malloc;
    wasm_init_args.mem_alloc_option.allocator.realloc_func = xuan_wasm_realloc;
    wasm_init_args.mem_alloc_option.allocator.free_func = xuan_wasm_free;

    if (!wasm_runtime_full_init(&wasm_init_args)) {
        JS_ThrowInternalError(ctx, "failed to initialize wasm runtime");
        return -1;
    }

    xuan_wasm_runtime_initialized = true;
    if (!xuan_wasm_runtime_atexit_registered) {
        atexit(xuan_wasm_runtime_destroy_once);
        xuan_wasm_runtime_atexit_registered = true;
    }
    return 0;
}

static void xuan_wasm_module_finalizer(JSRuntime *rt, JSValue val) {
    XuanWasmModule *m = JS_GetOpaque(val, xuan_wasm_module_class_id);
    if (m) {
        if (m->module) {
            wasm_runtime_unload(m->module);
        }
        js_free_rt(rt, m->data.bytes);
        if (m->wasi.argv) {
            for (uint32_t i = 0; i < m->wasi.argc; i++) {
                js_free_rt(rt, m->wasi.argv[i]);
            }
            js_free_rt(rt, m->wasi.argv);
        }
        if (m->wasi.env) {
            for (uint32_t i = 0; i < m->wasi.env_count; i++) {
                js_free_rt(rt, m->wasi.env[i]);
            }
            js_free_rt(rt, m->wasi.env);
        }
        if (m->wasi.map_dir_list) {
            for (uint32_t i = 0; i < m->wasi.map_dir_count; i++) {
                js_free_rt(rt, m->wasi.map_dir_list[i]);
            }
            js_free_rt(rt, m->wasi.map_dir_list);
        }
        js_free_rt(rt, m);
    }
}

static JSClassDef xuan_wasm_module_class = {
    "Module",
    .finalizer = xuan_wasm_module_finalizer,
};

static void xuan_wasm_instance_finalizer(JSRuntime *rt, JSValue val) {
    XuanWasmInstance *i = JS_GetOpaque(val, xuan_wasm_instance_class_id);
    if (i) {
        if (i->exec_env) {
            wasm_runtime_destroy_exec_env(i->exec_env);
        }
        if (i->module_inst) {
            wasm_runtime_deinstantiate(i->module_inst);
        }
        js_free_rt(rt, i);
    }
}

static JSClassDef xuan_wasm_instance_class = {
    "Instance",
    .finalizer = xuan_wasm_instance_finalizer,
};

static JSValue xuan_new_wasm_module(JSContext *ctx) {
    XuanWasmModule *m;
    JSValue obj;

    obj = JS_NewObjectClass(ctx, xuan_wasm_module_class_id);
    if (JS_IsException(obj)) {
        return obj;
    }

    m = js_mallocz(ctx, sizeof(*m));
    if (!m) {
        JS_FreeValue(ctx, obj);
        return JS_EXCEPTION;
    }

    JS_SetOpaque(obj, m);
    return obj;
}

static XuanWasmModule *xuan_wasm_module_get(JSContext *ctx, JSValue obj) {
    return JS_GetOpaque2(ctx, obj, xuan_wasm_module_class_id);
}

static JSValue xuan_new_wasm_instance(JSContext *ctx) {
    XuanWasmInstance *i;
    JSValue obj;

    obj = JS_NewObjectClass(ctx, xuan_wasm_instance_class_id);
    if (JS_IsException(obj)) {
        return obj;
    }

    i = js_mallocz(ctx, sizeof(*i));
    if (!i) {
        JS_FreeValue(ctx, obj);
        return JS_EXCEPTION;
    }

    JS_SetOpaque(obj, i);
    return obj;
}

static XuanWasmInstance *xuan_wasm_instance_get(JSContext *ctx, JSValue obj) {
    return JS_GetOpaque2(ctx, obj, xuan_wasm_instance_class_id);
}

static JSValue xuan_throw_wasm_error(JSContext *ctx, const char *name, const char *msg) {
    JSValue obj = JS_NewError(ctx);
    JS_DefinePropertyValueStr(
        ctx, obj, "message", JS_NewString(ctx, msg ? msg : "wasm error"), JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE);
    JS_DefinePropertyValueStr(ctx, obj, "wasmError", JS_NewString(ctx, name), JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE);
    if (JS_IsException(obj)) {
        obj = JS_NULL;
    }
    return JS_Throw(ctx, obj);
}

static JSValue xuan__wasm_val_to_js(JSContext *ctx, const wasm_val_t *val) {
    switch (val->kind) {
        case WASM_I32:
            return JS_NewInt32(ctx, val->of.i32);
        case WASM_I64:
            if (val->of.i64 == (int32_t) val->of.i64) {
                return JS_NewInt32(ctx, (int32_t) val->of.i64);
            }
            return JS_NewBigInt64(ctx, val->of.i64);
        case WASM_F32:
            return JS_NewFloat64(ctx, (double) val->of.f32);
        case WASM_F64:
            return JS_NewFloat64(ctx, val->of.f64);
        default:
            return JS_UNDEFINED;
    }
}

static bool xuan__js_to_wasm_val(JSContext *ctx, JSValue jsval, wasm_valkind_t type, wasm_val_t *val) {
    val->kind = type;
    switch (type) {
        case WASM_I32: {
            int32_t i32;
            if (JS_ToInt32(ctx, &i32, jsval)) {
                return false;
            }
            val->of.i32 = i32;
            return true;
        }
        case WASM_I64: {
            int64_t i64;
            if (!JS_ToBigInt64(ctx, &i64, jsval)) {
                val->of.i64 = i64;
                return true;
            }
            JS_FreeValue(ctx, JS_GetException(ctx));
            int32_t i32;
            if (JS_ToInt32(ctx, &i32, jsval)) {
                return false;
            }
            val->of.i64 = i32;
            return true;
        }
        case WASM_F32: {
            double f64;
            if (JS_ToFloat64(ctx, &f64, jsval)) {
                return false;
            }
            val->of.f32 = (float) f64;
            return true;
        }
        case WASM_F64: {
            double f64;
            if (JS_ToFloat64(ctx, &f64, jsval)) {
                return false;
            }
            val->of.f64 = f64;
            return true;
        }
        default:
            return false;
    }
}

static JSValue xuan_wasm_setwasioptions(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    XuanWasmModule *m = xuan_wasm_module_get(ctx, argv[0]);
    if (!m) {
        return JS_EXCEPTION;
    }

    JSValue js_args = argv[1];
    JSValue js_env = argv[2];
    JSValue js_preopens = argv[3];

    char **wasi_argv = NULL;
    uint32_t wasi_argc = 0;
    char **wasi_env = NULL;
    uint32_t wasi_env_count = 0;
    char **wasi_map_dir_list = NULL;
    uint32_t wasi_map_dir_count = 0;

    if (JS_IsArray(js_args)) {
        JSValue js_length = JS_GetPropertyStr(ctx, js_args, "length");
        uint64_t len;
        if (JS_ToIndex(ctx, &len, js_length)) {
            JS_FreeValue(ctx, js_length);
            goto fail;
        }
        JS_FreeValue(ctx, js_length);

        wasi_argv = js_mallocz(ctx, sizeof(*wasi_argv) * (len + 1));
        if (!wasi_argv) {
            goto fail;
        }
        wasi_argc = (uint32_t) len;

        for (uint32_t i = 0; i < len; i++) {
            JSValue v = JS_GetPropertyUint32(ctx, js_args, i);
            if (JS_IsException(v)) {
                goto fail;
            }
            const char *arg_str = JS_ToCString(ctx, v);
            JS_FreeValue(ctx, v);
            if (!arg_str) {
                goto fail;
            }
            wasi_argv[i] = js_strdup(ctx, arg_str);
            JS_FreeCString(ctx, arg_str);
            if (!wasi_argv[i]) {
                goto fail;
            }
        }
    }

    if (JS_IsObject(js_env) && !JS_IsNull(js_env)) {
        JSPropertyEnum *ptab;
        uint32_t plen;
        if (JS_GetOwnPropertyNames(ctx, &ptab, &plen, js_env, JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY)) {
            goto fail;
        }

        wasi_env = js_mallocz(ctx, sizeof(*wasi_env) * (plen + 1));
        if (!wasi_env) {
            JS_FreePropertyEnum(ctx, ptab, plen);
            goto fail;
        }
        wasi_env_count = plen;

        for (uint32_t i = 0; i < plen; i++) {
            JSValue prop = JS_GetProperty(ctx, js_env, ptab[i].atom);
            if (JS_IsException(prop)) {
                JS_FreePropertyEnum(ctx, ptab, plen);
                goto fail;
            }
            const char *key = JS_AtomToCString(ctx, ptab[i].atom);
            const char *value = JS_ToCString(ctx, prop);
            JS_FreeValue(ctx, prop);
            if (!key || !value) {
                JS_FreeCString(ctx, key);
                JS_FreeCString(ctx, value);
                JS_FreePropertyEnum(ctx, ptab, plen);
                goto fail;
            }
            size_t entry_len = strlen(key) + strlen(value) + 2;
            wasi_env[i] = js_malloc(ctx, entry_len);
            if (!wasi_env[i]) {
                JS_FreeCString(ctx, key);
                JS_FreeCString(ctx, value);
                JS_FreePropertyEnum(ctx, ptab, plen);
                goto fail;
            }
            snprintf(wasi_env[i], entry_len, "%s=%s", key, value);
            JS_FreeCString(ctx, key);
            JS_FreeCString(ctx, value);
        }
        JS_FreePropertyEnum(ctx, ptab, plen);
    }

    if (JS_IsObject(js_preopens) && !JS_IsNull(js_preopens)) {
        JSPropertyEnum *ptab;
        uint32_t plen;
        if (JS_GetOwnPropertyNames(ctx, &ptab, &plen, js_preopens, JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY)) {
            goto fail;
        }

        wasi_map_dir_list = js_mallocz(ctx, sizeof(*wasi_map_dir_list) * (plen + 1));
        if (!wasi_map_dir_list) {
            JS_FreePropertyEnum(ctx, ptab, plen);
            goto fail;
        }
        wasi_map_dir_count = plen;

        for (uint32_t i = 0; i < plen; i++) {
            JSValue prop = JS_GetProperty(ctx, js_preopens, ptab[i].atom);
            if (JS_IsException(prop)) {
                JS_FreePropertyEnum(ctx, ptab, plen);
                goto fail;
            }
            const char *guest_path = JS_AtomToCString(ctx, ptab[i].atom);
            const char *host_path = JS_ToCString(ctx, prop);
            JS_FreeValue(ctx, prop);
            if (!guest_path || !host_path) {
                JS_FreeCString(ctx, guest_path);
                JS_FreeCString(ctx, host_path);
                JS_FreePropertyEnum(ctx, ptab, plen);
                goto fail;
            }
            size_t entry_len = strlen(guest_path) + strlen(host_path) + 3;
            wasi_map_dir_list[i] = js_malloc(ctx, entry_len);
            if (!wasi_map_dir_list[i]) {
                JS_FreeCString(ctx, guest_path);
                JS_FreeCString(ctx, host_path);
                JS_FreePropertyEnum(ctx, ptab, plen);
                goto fail;
            }
            snprintf(wasi_map_dir_list[i], entry_len, "%s::%s", guest_path, host_path);
            JS_FreeCString(ctx, guest_path);
            JS_FreeCString(ctx, host_path);
        }
        JS_FreePropertyEnum(ctx, ptab, plen);
    }

    wasm_runtime_set_wasi_args(m->module,
                               NULL,
                               0,
                               (const char **) wasi_map_dir_list,
                               wasi_map_dir_count,
                               (const char **) wasi_env,
                               wasi_env_count,
                               wasi_argv,
                               (int) wasi_argc);

    m->wasi.argv = wasi_argv;
    m->wasi.argc = wasi_argc;
    m->wasi.env = wasi_env;
    m->wasi.env_count = wasi_env_count;
    m->wasi.map_dir_list = wasi_map_dir_list;
    m->wasi.map_dir_count = wasi_map_dir_count;

    return JS_UNDEFINED;

fail:
    if (wasi_argv) {
        for (uint32_t i = 0; i < wasi_argc; i++) {
            js_free(ctx, wasi_argv[i]);
        }
        js_free(ctx, wasi_argv);
    }
    if (wasi_env) {
        for (uint32_t i = 0; i < wasi_env_count; i++) {
            js_free(ctx, wasi_env[i]);
        }
        js_free(ctx, wasi_env);
    }
    if (wasi_map_dir_list) {
        for (uint32_t i = 0; i < wasi_map_dir_count; i++) {
            js_free(ctx, wasi_map_dir_list[i]);
        }
        js_free(ctx, wasi_map_dir_list);
    }
    return JS_EXCEPTION;
}

static JSValue xuan_wasm_callfunction(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    XuanWasmInstance *i = xuan_wasm_instance_get(ctx, this_val);
    if (!i) {
        return JS_EXCEPTION;
    }

    const char *fname = JS_ToCString(ctx, argv[0]);
    if (!fname) {
        return JS_EXCEPTION;
    }

    wasm_function_inst_t func = wasm_runtime_lookup_function(i->module_inst, fname);
    if (!func) {
        JS_FreeCString(ctx, fname);
        return xuan_throw_wasm_error(ctx, "RuntimeError", "function not found");
    }
    JS_FreeCString(ctx, fname);

    uint32_t param_count = wasm_func_get_param_count(func, i->module_inst);
    uint32_t result_count = wasm_func_get_result_count(func, i->module_inst);

    if (param_count > XUAN__WASM_MAX_ARGS || result_count > XUAN__WASM_MAX_ARGS) {
        return xuan_throw_wasm_error(ctx, "RuntimeError", "too many parameters or results");
    }

    wasm_valkind_t param_types[XUAN__WASM_MAX_ARGS];
    if (param_count > 0) {
        wasm_func_get_param_types(func, i->module_inst, param_types);
    }

    wasm_val_t params[XUAN__WASM_MAX_ARGS];
    int nargs = argc - 1;
    for (uint32_t j = 0; j < param_count; j++) {
        if ((int) j < nargs) {
            if (!xuan__js_to_wasm_val(ctx, argv[j + 1], param_types[j], &params[j])) {
                return JS_EXCEPTION;
            }
        } else {
            params[j].kind = param_types[j];
            params[j].of.i64 = 0;
        }
    }

    wasm_val_t results[XUAN__WASM_MAX_ARGS];
    if (!wasm_runtime_call_wasm_a(i->exec_env, func, result_count, results, param_count, params)) {
        const char *exception = wasm_runtime_get_exception(i->module_inst);
        wasm_runtime_clear_exception(i->module_inst);
        return xuan_throw_wasm_error(ctx, "RuntimeError", exception ? exception : "call failed");
    }

    if (result_count == 0) {
        return JS_UNDEFINED;
    }
    if (result_count == 1) {
        return xuan__wasm_val_to_js(ctx, &results[0]);
    }

    JSValue rets = JS_NewArray(ctx);
    for (uint32_t j = 0; j < result_count; j++) {
        JS_SetPropertyUint32(ctx, rets, j, xuan__wasm_val_to_js(ctx, &results[j]));
    }
    return rets;
}

static JSValue xuan_wasm_buildinstance(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    XuanWasmModule *m = xuan_wasm_module_get(ctx, argv[0]);
    if (!m) {
        return JS_EXCEPTION;
    }

    JSValue obj = xuan_new_wasm_instance(ctx);
    if (JS_IsException(obj)) {
        return obj;
    }

    XuanWasmInstance *i = xuan_wasm_instance_get(ctx, obj);
    char error_buf[XUAN__WASM_ERROR_BUF_SIZE];
    uint32_t stack_size = 64 * 1024;
    uint32_t heap_size = 512 * 1024;

    i->module_inst = wasm_runtime_instantiate(m->module, stack_size, heap_size, error_buf, sizeof(error_buf));
    if (!i->module_inst) {
        JS_FreeValue(ctx, obj);
        return xuan_throw_wasm_error(ctx, "LinkError", error_buf);
    }

    i->exec_env = wasm_runtime_create_exec_env(i->module_inst, stack_size);
    if (!i->exec_env) {
        wasm_runtime_deinstantiate(i->module_inst);
        i->module_inst = NULL;
        JS_FreeValue(ctx, obj);
        return xuan_throw_wasm_error(ctx, "RuntimeError", "failed to create exec env");
    }

    return obj;
}

static JSValue xuan_wasm_moduleexports(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    XuanWasmModule *m = xuan_wasm_module_get(ctx, argv[0]);
    if (!m) {
        return JS_EXCEPTION;
    }

    JSValue arr = JS_NewArray(ctx);
    int32_t export_count = wasm_runtime_get_export_count(m->module);
    uint32_t out_idx = 0;

    for (int32_t idx = 0; idx < export_count; idx++) {
        wasm_export_t export_type;
        wasm_runtime_get_export_type(m->module, idx, &export_type);

        if (export_type.kind == WASM_IMPORT_EXPORT_KIND_FUNC) {
            JSValue obj = JS_NewObject(ctx);
            JS_DefinePropertyValueStr(ctx, obj, "name", JS_NewString(ctx, export_type.name), JS_PROP_C_W_E);
            JS_DefinePropertyValueStr(ctx, obj, "kind", JS_NewString(ctx, "function"), JS_PROP_C_W_E);
            JS_SetPropertyUint32(ctx, arr, out_idx++, obj);
        }
    }

    return arr;
}

static JSValue xuan_wasm_parsemodule(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    if (xuan_wasm_ensure_runtime_initialized(ctx) != 0) {
        return JS_EXCEPTION;
    }

    size_t size;
    uint8_t *data = NULL;

    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "expected wasm bytes");
    }

    data = JS_GetArrayBuffer(ctx, &size, argv[0]);
    if (!data) {
        JS_FreeValue(ctx, JS_GetException(ctx));
        size_t aoffset, asize;
        JSValue abuf = JS_GetTypedArrayBuffer(ctx, argv[0], &aoffset, &asize, NULL);
        if (JS_IsException(abuf)) {
            return abuf;
        }
        data = JS_GetArrayBuffer(ctx, &size, abuf);
        JS_FreeValue(ctx, abuf);
        if (data) {
            data += aoffset;
            size = asize;
        }
    }
    if (!data || size == 0) {
        return JS_ThrowTypeError(ctx, "invalid wasm bytes");
    }

    JSValue obj = xuan_new_wasm_module(ctx);
    if (JS_IsException(obj)) {
        return obj;
    }

    XuanWasmModule *m = xuan_wasm_module_get(ctx, obj);
    m->data.bytes = js_malloc(ctx, size);
    if (!m->data.bytes) {
        JS_FreeValue(ctx, obj);
        return JS_EXCEPTION;
    }
    memcpy(m->data.bytes, data, size);
    m->data.size = size;

    char error_buf[XUAN__WASM_ERROR_BUF_SIZE];
    m->module = wasm_runtime_load(m->data.bytes, (uint32_t) size, error_buf, sizeof(error_buf));
    if (!m->module) {
        JS_FreeValue(ctx, obj);
        return xuan_throw_wasm_error(ctx, "CompileError", error_buf);
    }

    return obj;
}

static JSValue xuan_wasm_issimdenabled(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    (void) this_val;
    (void) argc;
    (void) argv;
#if defined(WASM_ENABLE_SIMD) && (WASM_ENABLE_SIMD != 0)
    return JS_NewBool(ctx, 1);
#else
    return JS_NewBool(ctx, 0);
#endif
}

static const JSCFunctionListEntry xuan_wasm_funcs[] = {
    JS_CFUNC_DEF("buildInstance", 1, xuan_wasm_buildinstance),
    JS_CFUNC_DEF("moduleExports", 1, xuan_wasm_moduleexports),
    JS_CFUNC_DEF("parseModule", 1, xuan_wasm_parsemodule),
    JS_CFUNC_DEF("isSimdEnabled", 0, xuan_wasm_issimdenabled),
    JS_CFUNC_DEF("setWasiOptions", 4, xuan_wasm_setwasioptions),
};

static const JSCFunctionListEntry xuan_wasm_instance_funcs[] = {
    JS_CFUNC_DEF("callFunction", 1, xuan_wasm_callfunction),
};

static int xuan_wasm_module_init(JSContext *ctx, JSModuleDef *m) {
    JSRuntime *rt = JS_GetRuntime(ctx);

    JS_NewClassID(rt, &xuan_wasm_module_class_id);
    JS_NewClass(rt, xuan_wasm_module_class_id, &xuan_wasm_module_class);
    JS_SetClassProto(ctx, xuan_wasm_module_class_id, JS_NULL);

    JS_NewClassID(rt, &xuan_wasm_instance_class_id);
    JS_NewClass(rt, xuan_wasm_instance_class_id, &xuan_wasm_instance_class);
    JSValue proto = JS_NewObjectProto(ctx, JS_NULL);
    JS_SetPropertyFunctionList(ctx, proto, xuan_wasm_instance_funcs, countof(xuan_wasm_instance_funcs));
    JS_SetClassProto(ctx, xuan_wasm_instance_class_id, proto);

    return JS_SetModuleExportList(ctx, m, xuan_wasm_funcs, countof(xuan_wasm_funcs));
}

JSModuleDef *js_init_module(JSContext *ctx, const char *module_name) {
    JSModuleDef *m = JS_NewCModule(ctx, module_name, xuan_wasm_module_init);
    if (!m) {
        return NULL;
    }
    JS_AddModuleExportList(ctx, m, xuan_wasm_funcs, countof(xuan_wasm_funcs));
    return m;
}
