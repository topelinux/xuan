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

#include <string.h>


#if defined(_WIN32)
#define TJS__PATHSEP '\\'
#else
#define TJS__PATHSEP '/'
#endif

static bool tjs__urlmod_path_is_absolute(const char *path) {
#if defined(_WIN32)
    if (((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z')) && path[1] == ':' &&
        (path[2] == '\\' || path[2] == '/')) {
        return true;
    }
    if (path[0] == '\\' && path[1] == '\\') {
        return true;
    }
#endif
    return path[0] == '/';
}

static char *tjs__urlmod_join_path(JSContext *ctx, const char *a, const char *b) {
    size_t len_a = strlen(a);
    size_t len_b = strlen(b);
    bool need_sep = len_a > 0 && a[len_a - 1] != '/' && a[len_a - 1] != '\\';
    char *out = js_malloc(ctx, len_a + (need_sep ? 1 : 0) + len_b + 1);
    if (!out) {
        return NULL;
    }
    memcpy(out, a, len_a);
    size_t off = len_a;
    if (need_sep) {
        out[off++] = TJS__PATHSEP;
    }
    memcpy(out + off, b, len_b);
    out[off + len_b] = '\0';
    return out;
}

static JSValue tjs__urlmod_to_url(JSContext *ctx, JSValueConst input) {
    JSValue global_obj = JS_GetGlobalObject(ctx);
    JSValue url_ctor = JS_GetPropertyStr(ctx, global_obj, "URL");
    JS_FreeValue(ctx, global_obj);
    if (JS_IsException(url_ctor)) {
        return JS_EXCEPTION;
    }

    JSValue url_obj;
    if (JS_IsObject(input)) {
        JSValue protocol = JS_GetPropertyStr(ctx, input, "protocol");
        if (JS_IsException(protocol)) {
            JS_FreeValue(ctx, url_ctor);
            return JS_EXCEPTION;
        }
        bool is_url_like = !JS_IsUndefined(protocol);
        JS_FreeValue(ctx, protocol);
        if (is_url_like) {
            url_obj = JS_DupValue(ctx, input);
        } else {
            JSValue arg = JS_ToString(ctx, input);
            if (JS_IsException(arg)) {
                JS_FreeValue(ctx, url_ctor);
                return JS_EXCEPTION;
            }
            url_obj = JS_CallConstructor(ctx, url_ctor, 1, &arg);
            JS_FreeValue(ctx, arg);
        }
    } else {
        JSValue arg = JS_ToString(ctx, input);
        if (JS_IsException(arg)) {
            JS_FreeValue(ctx, url_ctor);
            return JS_EXCEPTION;
        }
        url_obj = JS_CallConstructor(ctx, url_ctor, 1, &arg);
        JS_FreeValue(ctx, arg);
    }
    JS_FreeValue(ctx, url_ctor);
    return url_obj;
}

static JSValue tjs_url_file_url_to_path(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    JSValue url = tjs__urlmod_to_url(ctx, argv[0]);
    if (JS_IsException(url)) {
        return JS_EXCEPTION;
    }

    JSValue protocol = JS_GetPropertyStr(ctx, url, "protocol");
    if (JS_IsException(protocol)) {
        JS_FreeValue(ctx, url);
        return JS_EXCEPTION;
    }
    const char *protocol_str = JS_ToCString(ctx, protocol);
    JS_FreeValue(ctx, protocol);
    if (!protocol_str) {
        JS_FreeValue(ctx, url);
        return JS_EXCEPTION;
    }
    bool is_file = strcmp(protocol_str, "file:") == 0;
    JS_FreeCString(ctx, protocol_str);
    if (!is_file) {
        JS_FreeValue(ctx, url);
        return JS_ThrowTypeError(ctx, "The URL must be of scheme file");
    }

    JSValue pathname = JS_GetPropertyStr(ctx, url, "pathname");
    JSValue host = JS_GetPropertyStr(ctx, url, "host");
    JS_FreeValue(ctx, url);
    if (JS_IsException(pathname) || JS_IsException(host)) {
        JS_FreeValue(ctx, pathname);
        JS_FreeValue(ctx, host);
        return JS_EXCEPTION;
    }

    JSValue global_obj = JS_GetGlobalObject(ctx);
    JSValue decode_uri = JS_GetPropertyStr(ctx, global_obj, "decodeURIComponent");
    JS_FreeValue(ctx, global_obj);
    if (JS_IsException(decode_uri)) {
        JS_FreeValue(ctx, pathname);
        JS_FreeValue(ctx, host);
        return JS_EXCEPTION;
    }
    JSValue decoded = JS_Call(ctx, decode_uri, JS_UNDEFINED, 1, (JSValueConst *) &pathname);
    JS_FreeValue(ctx, decode_uri);
    JS_FreeValue(ctx, pathname);
    if (JS_IsException(decoded)) {
        JS_FreeValue(ctx, host);
        return JS_EXCEPTION;
    }

    const char *decoded_str = JS_ToCString(ctx, decoded);
    const char *host_str = JS_ToCString(ctx, host);
    JS_FreeValue(ctx, decoded);
    JS_FreeValue(ctx, host);
    if (!decoded_str || !host_str) {
        if (decoded_str) JS_FreeCString(ctx, decoded_str);
        if (host_str) JS_FreeCString(ctx, host_str);
        return JS_EXCEPTION;
    }

#if defined(_WIN32)
    JSValue ret;
    if (host_str[0] != '\0') {
        size_t len = strlen(host_str) + strlen(decoded_str) + 3;
        char *buf = js_malloc(ctx, len);
        if (!buf) {
            JS_FreeCString(ctx, decoded_str);
            JS_FreeCString(ctx, host_str);
            return JS_EXCEPTION;
        }
        snprintf(buf, len, "\\\\%s%s", host_str, decoded_str);
        for (char *p = buf; *p; p++) {
            if (*p == '/') {
                *p = '\\';
            }
        }
        ret = JS_NewString(ctx, buf);
        js_free(ctx, buf);
    } else {
        const char *start = decoded_str;
        if (start[0] == '/' && ((start[1] >= 'A' && start[1] <= 'Z') || (start[1] >= 'a' && start[1] <= 'z')) &&
            start[2] == ':') {
            start++;
        }
        size_t len = strlen(start) + 1;
        char *buf = js_malloc(ctx, len);
        if (!buf) {
            JS_FreeCString(ctx, decoded_str);
            JS_FreeCString(ctx, host_str);
            return JS_EXCEPTION;
        }
        memcpy(buf, start, len);
        for (char *p = buf; *p; p++) {
            if (*p == '/') {
                *p = '\\';
            }
        }
        ret = JS_NewString(ctx, buf);
        js_free(ctx, buf);
    }
#else
    JSValue ret = JS_NewString(ctx, decoded_str);
#endif

    JS_FreeCString(ctx, decoded_str);
    JS_FreeCString(ctx, host_str);
    return ret;
}

static JSValue tjs_url_path_to_file_url(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    const char *path = JS_ToCString(ctx, argv[0]);
    if (!path) {
        return JS_EXCEPTION;
    }

    char cwd[1024];
    size_t cwd_size = sizeof(cwd);
    char *absolute = NULL;
    if (tjs__urlmod_path_is_absolute(path)) {
        absolute = js_strdup(ctx, path);
    } else {
        if (uv_cwd(cwd, &cwd_size) != 0) {
            JS_FreeCString(ctx, path);
            return JS_EXCEPTION;
        }
        absolute = tjs__urlmod_join_path(ctx, cwd, path);
    }
    JS_FreeCString(ctx, path);
    if (!absolute) {
        return JS_EXCEPTION;
    }

#if defined(_WIN32)
    for (char *p = absolute; *p; p++) {
        if (*p == '\\') {
            *p = '/';
        }
    }
    char *tmp = absolute;
    if (tmp[0] != '/') {
        size_t len = strlen(tmp) + 2;
        absolute = js_malloc(ctx, len);
        if (!absolute) {
            js_free(ctx, tmp);
            return JS_EXCEPTION;
        }
        absolute[0] = '/';
        strcpy(absolute + 1, tmp);
        js_free(ctx, tmp);
    }
#endif

    size_t url_len = strlen(absolute) + 8;
    char *url_str = js_malloc(ctx, url_len);
    if (!url_str) {
        js_free(ctx, absolute);
        return JS_EXCEPTION;
    }
    snprintf(url_str, url_len, "file://%s", absolute);
    js_free(ctx, absolute);

    JSValue global_obj = JS_GetGlobalObject(ctx);
    JSValue url_ctor = JS_GetPropertyStr(ctx, global_obj, "URL");
    JS_FreeValue(ctx, global_obj);
    if (JS_IsException(url_ctor)) {
        js_free(ctx, url_str);
        return JS_EXCEPTION;
    }
    JSValue arg = JS_NewString(ctx, url_str);
    js_free(ctx, url_str);
    JSValue ret = JS_CallConstructor(ctx, url_ctor, 1, (JSValueConst *) &arg);
    JS_FreeValue(ctx, arg);
    JS_FreeValue(ctx, url_ctor);
    return ret;
}

static JSValue tjs_url_format(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    JSValue url = tjs__urlmod_to_url(ctx, argv[0]);
    if (JS_IsException(url)) {
        return JS_EXCEPTION;
    }
    JSValue href = JS_GetPropertyStr(ctx, url, "href");
    JS_FreeValue(ctx, url);
    return href;
}

static JSValue tjs_url_domain_to_ascii(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    const char *domain = JS_ToCString(ctx, argv[0]);
    if (!domain) {
        return JS_EXCEPTION;
    }
    size_t len = strlen(domain) + 8;
    char *tmp = js_malloc(ctx, len);
    if (!tmp) {
        JS_FreeCString(ctx, domain);
        return JS_EXCEPTION;
    }
    snprintf(tmp, len, "http://%s", domain);
    JS_FreeCString(ctx, domain);

    JSValue global_obj = JS_GetGlobalObject(ctx);
    JSValue url_ctor = JS_GetPropertyStr(ctx, global_obj, "URL");
    JS_FreeValue(ctx, global_obj);
    if (JS_IsException(url_ctor)) {
        js_free(ctx, tmp);
        return JS_EXCEPTION;
    }
    JSValue arg = JS_NewString(ctx, tmp);
    js_free(ctx, tmp);
    JSValue url = JS_CallConstructor(ctx, url_ctor, 1, (JSValueConst *) &arg);
    JS_FreeValue(ctx, arg);
    JS_FreeValue(ctx, url_ctor);
    if (JS_IsException(url)) {
        return JS_EXCEPTION;
    }
    JSValue hostname = JS_GetPropertyStr(ctx, url, "hostname");
    JS_FreeValue(ctx, url);
    return hostname;
}

static JSValue tjs_url_domain_to_unicode(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    return JS_ToString(ctx, argv[0]);
}

static const JSCFunctionListEntry tjs_urlmod_funcs[] = {
    TJS_CFUNC_DEF("fileURLToPath", 1, tjs_url_file_url_to_path),
    TJS_CFUNC_DEF("pathToFileURL", 1, tjs_url_path_to_file_url),
    TJS_CFUNC_DEF("format", 1, tjs_url_format),
    TJS_CFUNC_DEF("domainToASCII", 1, tjs_url_domain_to_ascii),
    TJS_CFUNC_DEF("domainToUnicode", 1, tjs_url_domain_to_unicode),
};

void tjs__mod_node_url_init(JSContext *ctx, JSValue ns) {
    JSValue global_obj = JS_GetGlobalObject(ctx);
    JSValue urlmod = JS_NewObjectProto(ctx, JS_NULL);
    JS_SetPropertyFunctionList(ctx, urlmod, tjs_urlmod_funcs, countof(tjs_urlmod_funcs));

    JSValue url_ctor = JS_GetPropertyStr(ctx, global_obj, "URL");
    JSValue usp_ctor = JS_GetPropertyStr(ctx, global_obj, "URLSearchParams");
    JSValue pattern_ctor = JS_GetPropertyStr(ctx, global_obj, "URLPattern");

    if (!JS_IsUndefined(url_ctor)) {
        JS_DefinePropertyValueStr(ctx, urlmod, "URL", url_ctor, JS_PROP_C_W_E);
    } else {
        JS_FreeValue(ctx, url_ctor);
    }
    if (!JS_IsUndefined(usp_ctor)) {
        JS_DefinePropertyValueStr(ctx, urlmod, "URLSearchParams", usp_ctor, JS_PROP_C_W_E);
    } else {
        JS_FreeValue(ctx, usp_ctor);
    }
    if (!JS_IsUndefined(pattern_ctor)) {
        JS_DefinePropertyValueStr(ctx, urlmod, "URLPattern", pattern_ctor, JS_PROP_C_W_E);
    } else {
        JS_FreeValue(ctx, pattern_ctor);
    }

    JS_DefinePropertyValueStr(ctx, ns, "nodeUrl", urlmod, JS_PROP_C_W_E);
    JS_FreeValue(ctx, global_obj);
}
