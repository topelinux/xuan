/*
 * txiki.js
 *
 * Copyright (c) 2026-present txiki.js contributors
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

#include <uv.h>


static JSValue tjs__util_get_core(JSContext *ctx) {
    JSValue global_obj = JS_GetGlobalObject(ctx);
    JSValue core_sym = JS_NewSymbol(ctx, "tjs.internal.core", true);
    JSAtom core_atom = JS_ValueToAtom(ctx, core_sym);
    JSValue core = JS_GetProperty(ctx, global_obj, core_atom);

    JS_FreeAtom(ctx, core_atom);
    JS_FreeValue(ctx, core_sym);
    JS_FreeValue(ctx, global_obj);
    return core;
}

static JSValue tjs__util_call_core(JSContext *ctx, const char *name, int argc, JSValueConst *argv) {
    JSValue core = tjs__util_get_core(ctx);
    if (JS_IsException(core)) {
        return JS_EXCEPTION;
    }
    JSValue func = JS_GetPropertyStr(ctx, core, name);
    if (JS_IsException(func)) {
        JS_FreeValue(ctx, core);
        return JS_EXCEPTION;
    }
    if (!JS_IsFunction(ctx, func)) {
        JS_FreeValue(ctx, func);
        JS_FreeValue(ctx, core);
        return JS_ThrowTypeError(ctx, "core.%s is not available", name);
    }
    JSValue ret = JS_Call(ctx, func, core, argc, argv);
    JS_FreeValue(ctx, func);
    JS_FreeValue(ctx, core);
    return ret;
}

static int tjs__util_is_instance_of_ctor(JSContext *ctx, JSValueConst value, const char *ctor_name, bool *result) {
    JSValue global_obj = JS_GetGlobalObject(ctx);
    JSValue ctor = JS_GetPropertyStr(ctx, global_obj, ctor_name);
    JS_FreeValue(ctx, global_obj);
    if (JS_IsException(ctor)) {
        return -1;
    }
    if (JS_IsUndefined(ctor) || JS_IsNull(ctor)) {
        JS_FreeValue(ctx, ctor);
        *result = false;
        return 0;
    }
    int r = JS_IsInstanceOf(ctx, value, ctor);
    JS_FreeValue(ctx, ctor);
    if (r < 0) {
        return -1;
    }
    *result = r != 0;
    return 0;
}

static JSValue tjs_util_format(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    return tjs__util_call_core(ctx, "format", argc, argv);
}

static JSValue tjs_util_format_with_options(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    if (argc <= 1) {
        JSValue empty = JS_NewString(ctx, "");
        return empty;
    }
    return tjs__util_call_core(ctx, "format", argc - 1, argv + 1);
}

static JSValue tjs_util_inspect(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    return tjs__util_call_core(ctx, "inspect", argc, argv);
}

static JSValue tjs_util_deprecate(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    if (argc < 1 || !JS_IsFunction(ctx, argv[0])) {
        return JS_ThrowTypeError(ctx, "The \"fn\" argument must be of type function");
    }
    return JS_DupValue(ctx, argv[0]);
}

static JSValue tjs_util_debuglog(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    JSValue noop = JS_Eval(ctx, "(function(){})", 13, "<util:debuglog>", JS_EVAL_TYPE_GLOBAL);
    return noop;
}

static JSValue tjs_util_inherits(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    if (argc < 2) {
        return JS_ThrowTypeError(ctx, "inherits expects ctor and superCtor");
    }
    JSValue ctor = argv[0];
    JSValue super_ctor = argv[1];
    if (!JS_IsFunction(ctx, ctor)) {
        return JS_ThrowTypeError(ctx, "The \"ctor\" argument must be of type function");
    }
    if (!JS_IsFunction(ctx, super_ctor)) {
        JSValue maybe_default = JS_GetPropertyStr(ctx, super_ctor, "default");
        if (!JS_IsFunction(ctx, maybe_default)) {
            JS_FreeValue(ctx, maybe_default);
            maybe_default = JS_GetPropertyStr(ctx, super_ctor, "Stream");
        }
        if (JS_IsFunction(ctx, maybe_default)) {
            super_ctor = maybe_default;
        } else {
            JS_FreeValue(ctx, maybe_default);
            return JS_ThrowTypeError(ctx, "The \"superCtor\" argument must be of type function");
        }
    }

    JSValue super_proto = JS_GetPropertyStr(ctx, super_ctor, "prototype");
    if (JS_IsException(super_proto)) {
        return JS_EXCEPTION;
    }
    if (!JS_IsObject(super_proto) && !JS_IsNull(super_proto)) {
        JS_FreeValue(ctx, super_proto);
        return JS_ThrowTypeError(ctx, "The \"superCtor.prototype\" property must be of type object");
    }

    JSValue ctor_proto = JS_GetPropertyStr(ctx, ctor, "prototype");
    if (JS_IsException(ctor_proto)) {
        JS_FreeValue(ctx, super_proto);
        return JS_EXCEPTION;
    }
    if (!JS_IsObject(ctor_proto)) {
        JS_FreeValue(ctx, ctor_proto);
        JS_FreeValue(ctx, super_proto);
        return JS_ThrowTypeError(ctx, "The \"ctor.prototype\" property must be of type object");
    }

    if (JS_SetPrototype(ctx, ctor_proto, super_proto) < 0) {
        JS_FreeValue(ctx, ctor_proto);
        JS_FreeValue(ctx, super_proto);
        return JS_EXCEPTION;
    }
    JS_FreeValue(ctx, ctor_proto);
    JS_FreeValue(ctx, super_proto);

    if (JS_SetPropertyStr(ctx, ctor, "super_", JS_DupValue(ctx, super_ctor)) < 0) {
        return JS_EXCEPTION;
    }
    return JS_UNDEFINED;
}

static JSValue tjs_util_isArray(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    return JS_NewBool(ctx, JS_IsArray(argv[0]));
}

static JSValue tjs_util_isBoolean(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    return JS_NewBool(ctx, JS_IsBool(argv[0]));
}

static JSValue tjs_util_isNull(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    return JS_NewBool(ctx, JS_IsNull(argv[0]));
}

static JSValue tjs_util_isNullOrUndefined(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    return JS_NewBool(ctx, JS_IsNull(argv[0]) || JS_IsUndefined(argv[0]));
}

static JSValue tjs_util_isNumber(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    return JS_NewBool(ctx, JS_IsNumber(argv[0]));
}

static JSValue tjs_util_isString(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    return JS_NewBool(ctx, JS_IsString(argv[0]));
}

static JSValue tjs_util_isSymbol(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    return JS_NewBool(ctx, JS_IsSymbol(argv[0]));
}

static JSValue tjs_util_isUndefined(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    return JS_NewBool(ctx, JS_IsUndefined(argv[0]));
}

static JSValue tjs_util_isObject(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    return JS_NewBool(ctx, JS_IsObject(argv[0]) && !JS_IsNull(argv[0]) && !JS_IsFunction(ctx, argv[0]));
}

static JSValue tjs_util_isFunction(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    return JS_NewBool(ctx, JS_IsFunction(ctx, argv[0]));
}

static JSValue tjs_util_isRegExp(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    return JS_NewBool(ctx, JS_IsRegExp(argv[0]));
}

static JSValue tjs_util_isDate(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    return JS_NewBool(ctx, JS_IsDate(argv[0]));
}

static JSValue tjs_util_isError(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    return JS_NewBool(ctx, JS_IsError(argv[0]));
}

static JSValue tjs_util_isPrimitive(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    bool is_primitive = JS_IsNull(argv[0]) || JS_IsUndefined(argv[0]) || JS_IsBool(argv[0]) || JS_IsNumber(argv[0]) ||
                        JS_IsString(argv[0]) || JS_IsSymbol(argv[0]) || JS_IsBigInt(argv[0]);
    return JS_NewBool(ctx, is_primitive);
}

static JSValue tjs_util_getSystemErrorName(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    int32_t err;
    if (JS_ToInt32(ctx, &err, argv[0])) {
        return JS_EXCEPTION;
    }
    if (err >= 0) {
        return JS_ThrowRangeError(ctx, "The \"err\" argument is out of range");
    }
    const char *name = uv_err_name(err);
    if (!name) {
        return JS_ThrowRangeError(ctx, "Unknown system error");
    }
    return JS_NewString(ctx, name);
}

static JSValue tjs_util_types_isAnyArrayBuffer(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    bool is_sab = false;
    if (tjs__util_is_instance_of_ctor(ctx, argv[0], "SharedArrayBuffer", &is_sab) < 0) {
        return JS_EXCEPTION;
    }
    return JS_NewBool(ctx, JS_IsArrayBuffer(argv[0]) || is_sab);
}

static JSValue tjs_util_types_isArrayBuffer(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    return JS_NewBool(ctx, JS_IsArrayBuffer(argv[0]));
}

static JSValue tjs_util_types_isArrayBufferView(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    bool is_view = JS_IsDataView(argv[0]) || JS_GetTypedArrayType(argv[0]) >= 0;
    return JS_NewBool(ctx, is_view);
}

static JSValue tjs_util_types_isDataView(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    return JS_NewBool(ctx, JS_IsDataView(argv[0]));
}

static JSValue tjs_util_types_isTypedArray(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    return JS_NewBool(ctx, JS_GetTypedArrayType(argv[0]) >= 0);
}

static JSValue tjs_util_types_isMap(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    return JS_NewBool(ctx, JS_IsMap(argv[0]));
}

static JSValue tjs_util_types_isSet(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    return JS_NewBool(ctx, JS_IsSet(argv[0]));
}

static JSValue tjs_util_types_isWeakMap(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    return JS_NewBool(ctx, JS_IsWeakMap(argv[0]));
}

static JSValue tjs_util_types_isWeakSet(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    return JS_NewBool(ctx, JS_IsWeakSet(argv[0]));
}

static JSValue tjs_util_types_isDate(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    return JS_NewBool(ctx, JS_IsDate(argv[0]));
}

static JSValue tjs_util_types_isRegExp(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    return JS_NewBool(ctx, JS_IsRegExp(argv[0]));
}

static JSValue tjs_util_types_isNativeError(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    return JS_NewBool(ctx, JS_IsError(argv[0]));
}

static JSValue tjs_util_types_isPromise(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    return JS_NewBool(ctx, JS_IsPromise(argv[0]));
}

static const JSCFunctionListEntry tjs_util_funcs[] = {
    TJS_CFUNC_DEF("format", 1, tjs_util_format),
    TJS_CFUNC_DEF("formatWithOptions", 2, tjs_util_format_with_options),
    TJS_CFUNC_DEF("inspect", 2, tjs_util_inspect),
    TJS_CFUNC_DEF("deprecate", 3, tjs_util_deprecate),
    TJS_CFUNC_DEF("debuglog", 1, tjs_util_debuglog),
    TJS_CFUNC_DEF("inherits", 2, tjs_util_inherits),
    TJS_CFUNC_DEF("isArray", 1, tjs_util_isArray),
    TJS_CFUNC_DEF("isBoolean", 1, tjs_util_isBoolean),
    TJS_CFUNC_DEF("isNull", 1, tjs_util_isNull),
    TJS_CFUNC_DEF("isNullOrUndefined", 1, tjs_util_isNullOrUndefined),
    TJS_CFUNC_DEF("isNumber", 1, tjs_util_isNumber),
    TJS_CFUNC_DEF("isString", 1, tjs_util_isString),
    TJS_CFUNC_DEF("isSymbol", 1, tjs_util_isSymbol),
    TJS_CFUNC_DEF("isUndefined", 1, tjs_util_isUndefined),
    TJS_CFUNC_DEF("isObject", 1, tjs_util_isObject),
    TJS_CFUNC_DEF("isFunction", 1, tjs_util_isFunction),
    TJS_CFUNC_DEF("isRegExp", 1, tjs_util_isRegExp),
    TJS_CFUNC_DEF("isDate", 1, tjs_util_isDate),
    TJS_CFUNC_DEF("isError", 1, tjs_util_isError),
    TJS_CFUNC_DEF("isPrimitive", 1, tjs_util_isPrimitive),
    TJS_CFUNC_DEF("getSystemErrorName", 1, tjs_util_getSystemErrorName),
};

static const JSCFunctionListEntry tjs_util_types_funcs[] = {
    TJS_CFUNC_DEF("isAnyArrayBuffer", 1, tjs_util_types_isAnyArrayBuffer),
    TJS_CFUNC_DEF("isArrayBuffer", 1, tjs_util_types_isArrayBuffer),
    TJS_CFUNC_DEF("isArrayBufferView", 1, tjs_util_types_isArrayBufferView),
    TJS_CFUNC_DEF("isDataView", 1, tjs_util_types_isDataView),
    TJS_CFUNC_DEF("isTypedArray", 1, tjs_util_types_isTypedArray),
    TJS_CFUNC_DEF("isMap", 1, tjs_util_types_isMap),
    TJS_CFUNC_DEF("isSet", 1, tjs_util_types_isSet),
    TJS_CFUNC_DEF("isWeakMap", 1, tjs_util_types_isWeakMap),
    TJS_CFUNC_DEF("isWeakSet", 1, tjs_util_types_isWeakSet),
    TJS_CFUNC_DEF("isDate", 1, tjs_util_types_isDate),
    TJS_CFUNC_DEF("isRegExp", 1, tjs_util_types_isRegExp),
    TJS_CFUNC_DEF("isNativeError", 1, tjs_util_types_isNativeError),
    TJS_CFUNC_DEF("isPromise", 1, tjs_util_types_isPromise),
};

void tjs__mod_util_init(JSContext *ctx, JSValue ns) {
    JSValue util = JS_NewObjectProto(ctx, JS_NULL);
    JSValue types = JS_NewObjectProto(ctx, JS_NULL);

    JS_SetPropertyFunctionList(ctx, util, tjs_util_funcs, countof(tjs_util_funcs));
    JS_SetPropertyFunctionList(ctx, types, tjs_util_types_funcs, countof(tjs_util_types_funcs));
    JS_DefinePropertyValueStr(ctx, util, "types", types, JS_PROP_C_W_E);
    JS_DefinePropertyValueStr(ctx, ns, "util", util, JS_PROP_C_W_E);
}
