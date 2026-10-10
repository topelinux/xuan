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


#define TJS__EE_EVENTS_KEY "__tjs_events"
#define TJS__EE_MAX_KEY    "__tjs_max_listeners"
#define TJS__EE_ONCE_KEY   "__tjs_once_listener"
#define TJS__EE_DEFAULT_MAX_LISTENERS 10

enum {
    TJS__EE_ADD_ON = 0,
    TJS__EE_ADD_ONCE,
    TJS__EE_ADD_PREPEND,
    TJS__EE_ADD_PREPEND_ONCE,
};

enum {
    TJS__EE_LISTENERS = 0,
    TJS__EE_RAW_LISTENERS,
};

static bool tjs__ee_is_error_event(JSContext *ctx, JSAtom event_atom) {
    JSAtom error_atom = JS_NewAtom(ctx, "error");
    if (error_atom == JS_ATOM_NULL) {
        return false;
    }
    bool ret = event_atom == error_atom;
    JS_FreeAtom(ctx, error_atom);
    return ret;
}

static JSValue tjs__ee_throw_unhandled_error(JSContext *ctx, int argc, JSValue *argv) {
    if (argc >= 2 && !JS_IsUndefined(argv[1])) {
        if (JS_IsError(argv[1])) {
            return JS_Throw(ctx, JS_DupValue(ctx, argv[1]));
        }

        JSValue err = JS_NewError(ctx);
        if (JS_IsException(err)) {
            return err;
        }
        const char *reason = JS_ToCString(ctx, argv[1]);
        JS_DefinePropertyValueStr(ctx,
                                  err,
                                  "message",
                                  JS_NewString(ctx, reason ? reason : "Unhandled error."),
                                  JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE);
        if (reason) {
            JS_FreeCString(ctx, reason);
        }
        JS_DefinePropertyValueStr(ctx,
                                  err,
                                  "code",
                                  JS_NewString(ctx, "ERR_UNHANDLED_ERROR"),
                                  JS_PROP_C_W_E);
        return JS_Throw(ctx, err);
    }

    JSValue err = JS_NewError(ctx);
    if (JS_IsException(err)) {
        return err;
    }
    JS_DefinePropertyValueStr(ctx,
                              err,
                              "message",
                              JS_NewString(ctx, "Unhandled error."),
                              JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE);
    JS_DefinePropertyValueStr(ctx, err, "code", JS_NewString(ctx, "ERR_UNHANDLED_ERROR"), JS_PROP_C_W_E);
    return JS_Throw(ctx, err);
}

static JSValue tjs__ee_ensure_events(JSContext *ctx, JSValueConst emitter, bool create) {
    JSValue events = JS_GetPropertyStr(ctx, emitter, TJS__EE_EVENTS_KEY);
    if (JS_IsException(events)) {
        return JS_EXCEPTION;
    }
    if (JS_IsUndefined(events)) {
        JS_FreeValue(ctx, events);
        if (!create) {
            return JS_UNDEFINED;
        }
        events = JS_NewObjectProto(ctx, JS_NULL);
        if (JS_IsException(events)) {
            return JS_EXCEPTION;
        }
        if (JS_SetPropertyStr(ctx, (JSValue) emitter, TJS__EE_EVENTS_KEY, JS_DupValue(ctx, events)) < 0) {
            JS_FreeValue(ctx, events);
            return JS_EXCEPTION;
        }
        return events;
    }
    if (!JS_IsObject(events)) {
        JS_FreeValue(ctx, events);
        return JS_ThrowTypeError(ctx, "Invalid EventEmitter state");
    }
    return events;
}

static JSValue tjs__ee_ensure_listener_array(JSContext *ctx, JSValueConst events, JSAtom event_atom, bool create) {
    JSValue list = JS_GetProperty(ctx, events, event_atom);
    if (JS_IsException(list)) {
        return JS_EXCEPTION;
    }
    if (JS_IsUndefined(list)) {
        JS_FreeValue(ctx, list);
        if (!create) {
            return JS_UNDEFINED;
        }
        list = JS_NewArray(ctx);
        if (JS_IsException(list)) {
            return JS_EXCEPTION;
        }
        if (JS_SetProperty(ctx, (JSValue) events, event_atom, JS_DupValue(ctx, list)) < 0) {
            JS_FreeValue(ctx, list);
            return JS_EXCEPTION;
        }
        return list;
    }
    if (JS_IsArray(list)) {
        return list;
    }

    JSValue arr = JS_NewArray(ctx);
    if (JS_IsException(arr)) {
        JS_FreeValue(ctx, list);
        return JS_EXCEPTION;
    }
    if (JS_SetPropertyUint32(ctx, arr, 0, JS_DupValue(ctx, list)) < 0) {
        JS_FreeValue(ctx, arr);
        JS_FreeValue(ctx, list);
        return JS_EXCEPTION;
    }
    JS_FreeValue(ctx, list);
    if (JS_SetProperty(ctx, (JSValue) events, event_atom, JS_DupValue(ctx, arr)) < 0) {
        JS_FreeValue(ctx, arr);
        return JS_EXCEPTION;
    }
    return arr;
}

static int tjs__ee_array_insert(JSContext *ctx, JSValue arr, JSValueConst listener, bool prepend) {
    int64_t len = 0;
    if (JS_GetLength(ctx, arr, &len) < 0) {
        return -1;
    }
    if (prepend) {
        for (int64_t i = len; i > 0; i--) {
            JSValue item = JS_GetPropertyInt64(ctx, arr, i - 1);
            if (JS_IsException(item)) {
                return -1;
            }
            if (JS_SetPropertyInt64(ctx, arr, i, item) < 0) {
                return -1;
            }
        }
        return JS_SetPropertyUint32(ctx, arr, 0, JS_DupValue(ctx, listener));
    }
    return JS_SetPropertyInt64(ctx, arr, len, JS_DupValue(ctx, listener));
}

static bool tjs__ee_is_same_listener(JSContext *ctx, JSValueConst stored, JSValueConst listener) {
    if (JS_IsStrictEqual(ctx, stored, listener)) {
        return true;
    }
    JSValue original = JS_GetPropertyStr(ctx, stored, TJS__EE_ONCE_KEY);
    if (JS_IsException(original)) {
        return false;
    }
    bool is_same = JS_IsStrictEqual(ctx, original, listener);
    JS_FreeValue(ctx, original);
    return is_same;
}

static int tjs__ee_remove_index(JSContext *ctx, JSValue arr, int64_t idx, int64_t len) {
    for (int64_t i = idx + 1; i < len; i++) {
        JSValue item = JS_GetPropertyInt64(ctx, arr, i);
        if (JS_IsException(item)) {
            return -1;
        }
        if (JS_SetPropertyInt64(ctx, arr, i - 1, item) < 0) {
            return -1;
        }
    }
    if (JS_SetLength(ctx, arr, len - 1) < 0) {
        return -1;
    }
    return 0;
}

static int tjs__ee_remove_listener(JSContext *ctx,
                                   JSValueConst emitter,
                                   JSValueConst event_name,
                                   JSValueConst listener,
                                   bool exact_match) {
    JSValue events = tjs__ee_ensure_events(ctx, emitter, false);
    if (JS_IsException(events)) {
        return -1;
    }
    if (JS_IsUndefined(events)) {
        return 0;
    }

    JSAtom event_atom = JS_ValueToAtom(ctx, event_name);
    if (event_atom == JS_ATOM_NULL) {
        JS_FreeValue(ctx, events);
        return -1;
    }
    JSValue list = tjs__ee_ensure_listener_array(ctx, events, event_atom, false);
    if (JS_IsException(list)) {
        JS_FreeAtom(ctx, event_atom);
        JS_FreeValue(ctx, events);
        return -1;
    }
    if (JS_IsUndefined(list)) {
        JS_FreeAtom(ctx, event_atom);
        JS_FreeValue(ctx, events);
        return 0;
    }

    int64_t len = 0;
    if (JS_GetLength(ctx, list, &len) < 0) {
        JS_FreeValue(ctx, list);
        JS_FreeAtom(ctx, event_atom);
        JS_FreeValue(ctx, events);
        return -1;
    }
    for (int64_t i = 0; i < len; i++) {
        JSValue current = JS_GetPropertyInt64(ctx, list, i);
        if (JS_IsException(current)) {
            JS_FreeValue(ctx, list);
            JS_FreeAtom(ctx, event_atom);
            JS_FreeValue(ctx, events);
            return -1;
        }
        bool match = exact_match ? JS_IsStrictEqual(ctx, current, listener) : tjs__ee_is_same_listener(ctx, current, listener);
        JS_FreeValue(ctx, current);
        if (match) {
            if (tjs__ee_remove_index(ctx, list, i, len) < 0) {
                JS_FreeValue(ctx, list);
                JS_FreeAtom(ctx, event_atom);
                JS_FreeValue(ctx, events);
                return -1;
            }
            len--;
            if (len == 0) {
                JS_DeleteProperty(ctx, events, event_atom, 0);
            }
            break;
        }
    }

    JS_FreeValue(ctx, list);
    JS_FreeAtom(ctx, event_atom);
    JS_FreeValue(ctx, events);
    return 0;
}

static JSValue tjs__ee_once_wrapper(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv, int magic,
                                    JSValue *data) {
    (void) magic;
    (void) data;
    return JS_Call(ctx, data[2], this_val, argc, argv);
}

static int32_t tjs__ee_get_default_max_listeners(JSContext *ctx, JSValueConst ctor) {
    int32_t n = TJS__EE_DEFAULT_MAX_LISTENERS;
    if (JS_IsUndefined(ctor) || JS_IsNull(ctor)) {
        return n;
    }
    JSValue val = JS_GetPropertyStr(ctx, ctor, "defaultMaxListeners");
    if (JS_IsException(val)) {
        return n;
    }
    int32_t tmp = 0;
    if (!JS_ToInt32(ctx, &tmp, val) && tmp >= 0) {
        n = tmp;
    }
    JS_FreeValue(ctx, val);
    return n;
}

static JSValue tjs_event_emitter_constructor(JSContext *ctx, JSValueConst new_target, int argc, JSValueConst *argv) {
    JSValue proto = JS_UNDEFINED;
    if (!JS_IsUndefined(new_target)) {
        proto = JS_GetPropertyStr(ctx, new_target, "prototype");
        if (JS_IsException(proto)) {
            return JS_EXCEPTION;
        }
        if (!JS_IsObject(proto)) {
            JS_FreeValue(ctx, proto);
            proto = JS_NULL;
        }
    } else {
        proto = JS_NULL;
    }
    JSValue this_obj = JS_NewObjectProto(ctx, proto);
    JS_FreeValue(ctx, proto);
    if (JS_IsException(this_obj)) {
        return JS_EXCEPTION;
    }

    JSValue events = JS_NewObjectProto(ctx, JS_NULL);
    if (JS_IsException(events)) {
        JS_FreeValue(ctx, this_obj);
        return JS_EXCEPTION;
    }
    if (JS_SetPropertyStr(ctx, this_obj, TJS__EE_EVENTS_KEY, events) < 0) {
        JS_FreeValue(ctx, this_obj);
        return JS_EXCEPTION;
    }
    int32_t default_max = tjs__ee_get_default_max_listeners(ctx, new_target);
    if (JS_SetPropertyStr(ctx, this_obj, TJS__EE_MAX_KEY, JS_NewInt32(ctx, default_max)) < 0) {
        JS_FreeValue(ctx, this_obj);
        return JS_EXCEPTION;
    }
    return this_obj;
}

static JSValue tjs_event_emitter_add(JSContext *ctx, JSValue this_val, int argc, JSValue *argv, int magic) {
    if (!JS_IsObject(this_val)) {
        return JS_ThrowTypeError(ctx, "EventEmitter method called on non-object");
    }
    if (argc < 2) {
        return JS_ThrowTypeError(ctx, "The \"listener\" argument must be of type function");
    }
    if (!JS_IsFunction(ctx, argv[1])) {
        return JS_ThrowTypeError(ctx, "The \"listener\" argument must be of type function");
    }

    JSValue events = tjs__ee_ensure_events(ctx, this_val, true);
    if (JS_IsException(events)) {
        return JS_EXCEPTION;
    }
    JSAtom event_atom = JS_ValueToAtom(ctx, argv[0]);
    if (event_atom == JS_ATOM_NULL) {
        JS_FreeValue(ctx, events);
        return JS_EXCEPTION;
    }
    JSValue list = tjs__ee_ensure_listener_array(ctx, events, event_atom, true);
    if (JS_IsException(list)) {
        JS_FreeAtom(ctx, event_atom);
        JS_FreeValue(ctx, events);
        return JS_EXCEPTION;
    }

    bool prepend = magic == TJS__EE_ADD_PREPEND || magic == TJS__EE_ADD_PREPEND_ONCE;
    bool once = magic == TJS__EE_ADD_ONCE || magic == TJS__EE_ADD_PREPEND_ONCE;

    JSValue listener = JS_DupValue(ctx, argv[1]);
    if (once) {
        JSValue data[3] = { JS_DupValue(ctx, this_val), JS_DupValue(ctx, argv[0]), JS_DupValue(ctx, argv[1]) };
        JSValue wrapper = JS_NewCFunctionData(ctx, tjs__ee_once_wrapper, 0, 0, 3, data);
        JS_FreeValue(ctx, data[0]);
        JS_FreeValue(ctx, data[1]);
        JS_FreeValue(ctx, data[2]);
        if (JS_IsException(wrapper)) {
            JS_FreeValue(ctx, listener);
            JS_FreeValue(ctx, list);
            JS_FreeAtom(ctx, event_atom);
            JS_FreeValue(ctx, events);
            return JS_EXCEPTION;
        }
        if (JS_SetPropertyStr(ctx, wrapper, TJS__EE_ONCE_KEY, JS_DupValue(ctx, argv[1])) < 0) {
            JS_FreeValue(ctx, wrapper);
            JS_FreeValue(ctx, listener);
            JS_FreeValue(ctx, list);
            JS_FreeAtom(ctx, event_atom);
            JS_FreeValue(ctx, events);
            return JS_EXCEPTION;
        }
        JS_FreeValue(ctx, listener);
        listener = wrapper;
    }

    if (tjs__ee_array_insert(ctx, list, listener, prepend) < 0) {
        JS_FreeValue(ctx, listener);
        JS_FreeValue(ctx, list);
        JS_FreeAtom(ctx, event_atom);
        JS_FreeValue(ctx, events);
        return JS_EXCEPTION;
    }

    JS_FreeValue(ctx, listener);
    JS_FreeValue(ctx, list);
    JS_FreeAtom(ctx, event_atom);
    JS_FreeValue(ctx, events);
    return JS_DupValue(ctx, this_val);
}

static JSValue tjs_event_emitter_remove_listener(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    if (!JS_IsObject(this_val)) {
        return JS_ThrowTypeError(ctx, "EventEmitter method called on non-object");
    }
    if (argc < 2 || !JS_IsFunction(ctx, argv[1])) {
        return JS_DupValue(ctx, this_val);
    }
    if (tjs__ee_remove_listener(ctx, this_val, argv[0], argv[1], false) < 0) {
        return JS_EXCEPTION;
    }
    return JS_DupValue(ctx, this_val);
}

static JSValue tjs_event_emitter_emit(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    if (!JS_IsObject(this_val)) {
        return JS_ThrowTypeError(ctx, "EventEmitter method called on non-object");
    }
    if (argc < 1) {
        return JS_NewBool(ctx, false);
    }

    JSAtom event_atom = JS_ValueToAtom(ctx, argv[0]);
    if (event_atom == JS_ATOM_NULL) {
        return JS_EXCEPTION;
    }
    bool is_error_event = tjs__ee_is_error_event(ctx, event_atom);

    JSValue events = tjs__ee_ensure_events(ctx, this_val, false);
    if (JS_IsException(events)) {
        JS_FreeAtom(ctx, event_atom);
        return JS_EXCEPTION;
    }
    if (JS_IsUndefined(events)) {
        JS_FreeAtom(ctx, event_atom);
        if (is_error_event) {
            return tjs__ee_throw_unhandled_error(ctx, argc, argv);
        }
        return JS_NewBool(ctx, false);
    }

    JSValue list = tjs__ee_ensure_listener_array(ctx, events, event_atom, false);
    if (JS_IsException(list)) {
        JS_FreeAtom(ctx, event_atom);
        JS_FreeValue(ctx, events);
        return JS_EXCEPTION;
    }
    if (JS_IsUndefined(list)) {
        JS_FreeAtom(ctx, event_atom);
        JS_FreeValue(ctx, events);
        if (is_error_event) {
            return tjs__ee_throw_unhandled_error(ctx, argc, argv);
        }
        return JS_NewBool(ctx, false);
    }

    int64_t len = 0;
    if (JS_GetLength(ctx, list, &len) < 0) {
        JS_FreeValue(ctx, list);
        JS_FreeAtom(ctx, event_atom);
        JS_FreeValue(ctx, events);
        return JS_EXCEPTION;
    }
    if (len == 0) {
        JS_FreeValue(ctx, list);
        JS_FreeAtom(ctx, event_atom);
        JS_FreeValue(ctx, events);
        if (is_error_event) {
            return tjs__ee_throw_unhandled_error(ctx, argc, argv);
        }
        return JS_NewBool(ctx, false);
    }

    JSValue snapshot = JS_NewArray(ctx);
    if (JS_IsException(snapshot)) {
        JS_FreeValue(ctx, list);
        JS_FreeAtom(ctx, event_atom);
        JS_FreeValue(ctx, events);
        return JS_EXCEPTION;
    }
    for (int64_t i = 0; i < len; i++) {
        JSValue listener = JS_GetPropertyInt64(ctx, list, i);
        if (JS_IsException(listener) || JS_SetPropertyInt64(ctx, snapshot, i, listener) < 0) {
            JS_FreeValue(ctx, snapshot);
            JS_FreeValue(ctx, list);
            JS_FreeAtom(ctx, event_atom);
            JS_FreeValue(ctx, events);
            return JS_EXCEPTION;
        }
    }

    for (int64_t i = 0; i < len; i++) {
        JSValue listener = JS_GetPropertyInt64(ctx, snapshot, i);
        if (JS_IsException(listener)) {
            JS_FreeValue(ctx, snapshot);
            JS_FreeValue(ctx, list);
            JS_FreeAtom(ctx, event_atom);
            JS_FreeValue(ctx, events);
            return JS_EXCEPTION;
        }
        JSValue orig = JS_GetPropertyStr(ctx, listener, TJS__EE_ONCE_KEY);
        if (JS_IsException(orig)) {
            JS_FreeValue(ctx, listener);
            JS_FreeValue(ctx, snapshot);
            JS_FreeValue(ctx, list);
            JS_FreeAtom(ctx, event_atom);
            JS_FreeValue(ctx, events);
            return JS_EXCEPTION;
        }
        bool is_once_listener = !JS_IsUndefined(orig);
        if (is_once_listener) {
            if (tjs__ee_remove_listener(ctx, this_val, argv[0], listener, true) < 0) {
                JS_FreeValue(ctx, orig);
                JS_FreeValue(ctx, listener);
                JS_FreeValue(ctx, snapshot);
                JS_FreeValue(ctx, list);
                JS_FreeAtom(ctx, event_atom);
                JS_FreeValue(ctx, events);
                return JS_EXCEPTION;
            }
        }
        JS_FreeValue(ctx, orig);
        JSValue ret = JS_Call(ctx, listener, this_val, argc - 1, argv + 1);
        JS_FreeValue(ctx, listener);
        if (JS_IsException(ret)) {
            JS_FreeValue(ctx, snapshot);
            JS_FreeValue(ctx, list);
            JS_FreeAtom(ctx, event_atom);
            JS_FreeValue(ctx, events);
            return JS_EXCEPTION;
        }
        JS_FreeValue(ctx, ret);
    }

    JS_FreeValue(ctx, snapshot);
    JS_FreeValue(ctx, list);
    JS_FreeAtom(ctx, event_atom);
    JS_FreeValue(ctx, events);
    return JS_NewBool(ctx, true);
}

static JSValue tjs_event_emitter_remove_all_listeners(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    if (!JS_IsObject(this_val)) {
        return JS_ThrowTypeError(ctx, "EventEmitter method called on non-object");
    }
    if (argc < 1 || JS_IsUndefined(argv[0])) {
        JSValue events = JS_NewObjectProto(ctx, JS_NULL);
        if (JS_IsException(events)) {
            return JS_EXCEPTION;
        }
        if (JS_SetPropertyStr(ctx, this_val, TJS__EE_EVENTS_KEY, events) < 0) {
            return JS_EXCEPTION;
        }
        return JS_DupValue(ctx, this_val);
    }

    JSValue events = tjs__ee_ensure_events(ctx, this_val, false);
    if (JS_IsException(events)) {
        return JS_EXCEPTION;
    }
    if (JS_IsUndefined(events)) {
        return JS_DupValue(ctx, this_val);
    }
    JSAtom event_atom = JS_ValueToAtom(ctx, argv[0]);
    if (event_atom == JS_ATOM_NULL) {
        JS_FreeValue(ctx, events);
        return JS_EXCEPTION;
    }
    JS_DeleteProperty(ctx, events, event_atom, 0);
    JS_FreeAtom(ctx, event_atom);
    JS_FreeValue(ctx, events);
    return JS_DupValue(ctx, this_val);
}

static JSValue tjs_event_emitter_get_listeners(JSContext *ctx, JSValue this_val, int argc, JSValue *argv, int magic) {
    if (!JS_IsObject(this_val)) {
        return JS_ThrowTypeError(ctx, "EventEmitter method called on non-object");
    }
    if (argc < 1) {
        return JS_NewArray(ctx);
    }
    JSValue events = tjs__ee_ensure_events(ctx, this_val, false);
    if (JS_IsException(events)) {
        return JS_EXCEPTION;
    }
    if (JS_IsUndefined(events)) {
        return JS_NewArray(ctx);
    }
    JSAtom event_atom = JS_ValueToAtom(ctx, argv[0]);
    if (event_atom == JS_ATOM_NULL) {
        JS_FreeValue(ctx, events);
        return JS_EXCEPTION;
    }
    JSValue list = tjs__ee_ensure_listener_array(ctx, events, event_atom, false);
    JS_FreeAtom(ctx, event_atom);
    JS_FreeValue(ctx, events);
    if (JS_IsException(list)) {
        return JS_EXCEPTION;
    }
    if (JS_IsUndefined(list)) {
        return JS_NewArray(ctx);
    }

    int64_t len = 0;
    if (JS_GetLength(ctx, list, &len) < 0) {
        JS_FreeValue(ctx, list);
        return JS_EXCEPTION;
    }
    JSValue out = JS_NewArray(ctx);
    if (JS_IsException(out)) {
        JS_FreeValue(ctx, list);
        return JS_EXCEPTION;
    }
    for (int64_t i = 0; i < len; i++) {
        JSValue listener = JS_GetPropertyInt64(ctx, list, i);
        if (JS_IsException(listener)) {
            JS_FreeValue(ctx, out);
            JS_FreeValue(ctx, list);
            return JS_EXCEPTION;
        }
        if (magic == TJS__EE_LISTENERS) {
            JSValue orig = JS_GetPropertyStr(ctx, listener, TJS__EE_ONCE_KEY);
            if (JS_IsException(orig)) {
                JS_FreeValue(ctx, listener);
                JS_FreeValue(ctx, out);
                JS_FreeValue(ctx, list);
                return JS_EXCEPTION;
            }
            if (!JS_IsUndefined(orig)) {
                JS_FreeValue(ctx, listener);
                listener = orig;
            } else {
                JS_FreeValue(ctx, orig);
            }
        }
        if (JS_SetPropertyInt64(ctx, out, i, listener) < 0) {
            JS_FreeValue(ctx, out);
            JS_FreeValue(ctx, list);
            return JS_EXCEPTION;
        }
    }
    JS_FreeValue(ctx, list);
    return out;
}

static JSValue tjs_event_emitter_listener_count(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    if (!JS_IsObject(this_val)) {
        return JS_ThrowTypeError(ctx, "EventEmitter method called on non-object");
    }
    if (argc < 1) {
        return JS_NewInt32(ctx, 0);
    }
    JSValue events = tjs__ee_ensure_events(ctx, this_val, false);
    if (JS_IsException(events)) {
        return JS_EXCEPTION;
    }
    if (JS_IsUndefined(events)) {
        return JS_NewInt32(ctx, 0);
    }
    JSAtom event_atom = JS_ValueToAtom(ctx, argv[0]);
    if (event_atom == JS_ATOM_NULL) {
        JS_FreeValue(ctx, events);
        return JS_EXCEPTION;
    }
    JSValue list = tjs__ee_ensure_listener_array(ctx, events, event_atom, false);
    JS_FreeAtom(ctx, event_atom);
    JS_FreeValue(ctx, events);
    if (JS_IsException(list)) {
        return JS_EXCEPTION;
    }
    if (JS_IsUndefined(list)) {
        return JS_NewInt32(ctx, 0);
    }
    int64_t len = 0;
    if (JS_GetLength(ctx, list, &len) < 0) {
        JS_FreeValue(ctx, list);
        return JS_EXCEPTION;
    }
    JS_FreeValue(ctx, list);
    return JS_NewInt64(ctx, len);
}

static JSValue tjs_event_emitter_event_names(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    if (!JS_IsObject(this_val)) {
        return JS_ThrowTypeError(ctx, "EventEmitter method called on non-object");
    }
    JSValue events = tjs__ee_ensure_events(ctx, this_val, false);
    if (JS_IsException(events)) {
        return JS_EXCEPTION;
    }
    if (JS_IsUndefined(events)) {
        return JS_NewArray(ctx);
    }

    JSPropertyEnum *tab = NULL;
    uint32_t len = 0;
    if (JS_GetOwnPropertyNames(ctx, &tab, &len, events, JS_GPN_STRING_MASK | JS_GPN_SYMBOL_MASK | JS_GPN_ENUM_ONLY)) {
        JS_FreeValue(ctx, events);
        return JS_EXCEPTION;
    }
    JSValue out = JS_NewArray(ctx);
    if (JS_IsException(out)) {
        JS_FreePropertyEnum(ctx, tab, len);
        JS_FreeValue(ctx, events);
        return JS_EXCEPTION;
    }
    for (uint32_t i = 0; i < len; i++) {
        JSValue key = JS_AtomToValue(ctx, tab[i].atom);
        if (JS_IsException(key) || JS_SetPropertyUint32(ctx, out, i, key) < 0) {
            JS_FreePropertyEnum(ctx, tab, len);
            JS_FreeValue(ctx, events);
            JS_FreeValue(ctx, out);
            return JS_EXCEPTION;
        }
    }
    JS_FreePropertyEnum(ctx, tab, len);
    JS_FreeValue(ctx, events);
    return out;
}

static JSValue tjs_event_emitter_set_max_listeners(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    if (!JS_IsObject(this_val)) {
        return JS_ThrowTypeError(ctx, "EventEmitter method called on non-object");
    }
    int32_t n;
    if (JS_ToInt32(ctx, &n, argv[0])) {
        return JS_EXCEPTION;
    }
    if (n < 0) {
        return JS_ThrowRangeError(ctx, "The value of \"n\" is out of range");
    }
    if (JS_SetPropertyStr(ctx, this_val, TJS__EE_MAX_KEY, JS_NewInt32(ctx, n)) < 0) {
        return JS_EXCEPTION;
    }
    return JS_DupValue(ctx, this_val);
}

static JSValue tjs_event_emitter_get_max_listeners(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    if (!JS_IsObject(this_val)) {
        return JS_ThrowTypeError(ctx, "EventEmitter method called on non-object");
    }
    JSValue n = JS_GetPropertyStr(ctx, this_val, TJS__EE_MAX_KEY);
    if (JS_IsException(n)) {
        return JS_EXCEPTION;
    }
    if (JS_IsUndefined(n)) {
        JS_FreeValue(ctx, n);
        JSValue ctor = JS_GetPropertyStr(ctx, this_val, "constructor");
        if (JS_IsException(ctor)) {
            return JS_EXCEPTION;
        }
        int32_t default_max = tjs__ee_get_default_max_listeners(ctx, ctor);
        JS_FreeValue(ctx, ctor);
        return JS_NewInt32(ctx, default_max);
    }
    return n;
}

static const JSCFunctionListEntry tjs_events_emitter_proto_funcs[] = {
    TJS_CFUNC_MAGIC_DEF("addListener", 2, tjs_event_emitter_add, TJS__EE_ADD_ON),
    TJS_CFUNC_MAGIC_DEF("on", 2, tjs_event_emitter_add, TJS__EE_ADD_ON),
    TJS_CFUNC_MAGIC_DEF("once", 2, tjs_event_emitter_add, TJS__EE_ADD_ONCE),
    TJS_CFUNC_MAGIC_DEF("prependListener", 2, tjs_event_emitter_add, TJS__EE_ADD_PREPEND),
    TJS_CFUNC_MAGIC_DEF("prependOnceListener", 2, tjs_event_emitter_add, TJS__EE_ADD_PREPEND_ONCE),
    TJS_CFUNC_DEF("emit", 1, tjs_event_emitter_emit),
    TJS_CFUNC_DEF("removeListener", 2, tjs_event_emitter_remove_listener),
    TJS_CFUNC_DEF("off", 2, tjs_event_emitter_remove_listener),
    TJS_CFUNC_DEF("removeAllListeners", 1, tjs_event_emitter_remove_all_listeners),
    TJS_CFUNC_MAGIC_DEF("listeners", 1, tjs_event_emitter_get_listeners, TJS__EE_LISTENERS),
    TJS_CFUNC_MAGIC_DEF("rawListeners", 1, tjs_event_emitter_get_listeners, TJS__EE_RAW_LISTENERS),
    TJS_CFUNC_DEF("listenerCount", 1, tjs_event_emitter_listener_count),
    TJS_CFUNC_DEF("eventNames", 0, tjs_event_emitter_event_names),
    TJS_CFUNC_DEF("setMaxListeners", 1, tjs_event_emitter_set_max_listeners),
    TJS_CFUNC_DEF("getMaxListeners", 0, tjs_event_emitter_get_max_listeners),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "EventEmitter", JS_PROP_CONFIGURABLE),
};

void tjs__mod_events_init(JSContext *ctx, JSValue ns) {
    JSValue events = JS_NewObjectProto(ctx, JS_NULL);
    JSValue emitter_proto = JS_NewObject(ctx);
    JSValue emitter_ctor = JS_NewCFunction2(ctx, tjs_event_emitter_constructor, "EventEmitter", 0, JS_CFUNC_constructor, 0);

    JS_SetPropertyFunctionList(ctx, emitter_proto, tjs_events_emitter_proto_funcs, countof(tjs_events_emitter_proto_funcs));
    JS_SetConstructor(ctx, emitter_ctor, emitter_proto);
    JS_DefinePropertyValueStr(ctx,
                              emitter_ctor,
                              "defaultMaxListeners",
                              JS_NewInt32(ctx, TJS__EE_DEFAULT_MAX_LISTENERS),
                              JS_PROP_C_W_E);

    JS_DefinePropertyValueStr(ctx, events, "defaultMaxListeners", JS_NewInt32(ctx, TJS__EE_DEFAULT_MAX_LISTENERS), JS_PROP_C_W_E);
    JS_DefinePropertyValueStr(ctx, events, "EventEmitter", emitter_ctor, JS_PROP_C_W_E);
    JS_DefinePropertyValueStr(ctx, ns, "events", events, JS_PROP_C_W_E);
}
