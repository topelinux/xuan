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

#include "bundles/c/stdlib/assert.c"
#include "bundles/c/stdlib/buffer.c"
#include "bundles/c/stdlib/child_process.c"
#include "bundles/c/stdlib/crypto.c"
#include "bundles/c/stdlib/dgram.c"
#include "bundles/c/stdlib/dns.c"
#include "bundles/c/stdlib/events.c"
#include "bundles/c/stdlib/ffi.c"
#include "bundles/c/stdlib/fs.c"
#include "bundles/c/stdlib/fs-promises.c"
#include "bundles/c/stdlib/getopts.c"
#include "bundles/c/stdlib/hashing.c"
#include "bundles/c/stdlib/http.c"
#ifdef TJS__HAS_HTTP2
#include "bundles/c/stdlib/http2.c"
#endif
#include "bundles/c/stdlib/https.c"
#include "bundles/c/stdlib/ipaddr.c"
#include "bundles/c/stdlib/mqtt.c"
#include "bundles/c/stdlib/net.c"
#include "bundles/c/stdlib/npm.c"
#include "bundles/c/stdlib/os.c"
#include "bundles/c/stdlib/path.c"
#include "bundles/c/stdlib/posix-socket.c"
#include "bundles/c/stdlib/process.c"
#include "bundles/c/stdlib/querystring.c"
#include "bundles/c/stdlib/readline.c"
#include "bundles/c/stdlib/serial.c"
#include "bundles/c/stdlib/sqlite.c"
#include "bundles/c/stdlib/node-sqlite.c"
#include "bundles/c/stdlib/stream.c"
#include "bundles/c/stdlib/string_decoder.c"
#include "bundles/c/stdlib/tls.c"
#include "bundles/c/stdlib/tty.c"
#include "bundles/c/stdlib/url.c"
#include "bundles/c/stdlib/util.c"
#include "bundles/c/stdlib/uuid.c"
#include "bundles/c/stdlib/wasi.c"
#include "bundles/c/stdlib/ws.c"
#include "bundles/c/stdlib/zlib.c"
#include "private.h"

#include <string.h>


typedef struct {
    const char *name;
    const uint8_t *data;
    uint32_t data_size;
} tjs_builtin_t;

typedef struct {
    const char *name;
    const char *builtin;
} tjs_builtin_alias_t;

static tjs_builtin_t builtins[] = {
    { "tjs:assert", tjs__assert, sizeof(tjs__assert) },
    { "tjs:buffer", tjs__buffer, sizeof(tjs__buffer) },
    { "tjs:child_process", tjs__child_process, sizeof(tjs__child_process) },
    { "tjs:crypto", tjs__crypto, sizeof(tjs__crypto) },
    { "tjs:dgram", tjs__dgram, sizeof(tjs__dgram) },
    { "tjs:dns", tjs__dns, sizeof(tjs__dns) },
    { "tjs:events", tjs__events, sizeof(tjs__events) },
    { "tjs:ffi", tjs__ffi, sizeof(tjs__ffi) },
    { "tjs:fs", tjs__fs, sizeof(tjs__fs) },
    { "tjs:fs-promises", tjs__fs_promises, sizeof(tjs__fs_promises) },
    { "tjs:getopts", tjs__getopts, sizeof(tjs__getopts) },
    { "tjs:hashing", tjs__hashing, sizeof(tjs__hashing) },
    { "tjs:http", tjs__http, sizeof(tjs__http) },
#ifdef TJS__HAS_HTTP2
    { "tjs:http2", tjs__http2, sizeof(tjs__http2) },
#endif
    { "tjs:https", tjs__https, sizeof(tjs__https) },
    { "tjs:ipaddr", tjs__ipaddr, sizeof(tjs__ipaddr) },
    { "tjs:mqtt", tjs__mqtt, sizeof(tjs__mqtt) },
    { "tjs:npm", tjs__npm, sizeof(tjs__npm) },
    { "tjs:path", tjs__path, sizeof(tjs__path) },
    { "tjs:os", tjs__os, sizeof(tjs__os) },
    { "tjs:posix-socket", tjs__posix_socket, sizeof(tjs__posix_socket) },
    { "tjs:process", tjs__process, sizeof(tjs__process) },
    { "tjs:querystring", tjs__querystring, sizeof(tjs__querystring) },
    { "tjs:readline", tjs__readline, sizeof(tjs__readline) },
    { "tjs:serial", tjs__serial, sizeof(tjs__serial) },
    { "tjs:zlib", tjs__zlib, sizeof(tjs__zlib) },
    { "tjs:sqlite", tjs__sqlite, sizeof(tjs__sqlite) },
    { "tjs:node-sqlite", tjs__node_sqlite, sizeof(tjs__node_sqlite) },
    { "tjs:net", tjs__net, sizeof(tjs__net) },
    { "tjs:stream", tjs__stream, sizeof(tjs__stream) },
    { "tjs:string_decoder", tjs__string_decoder, sizeof(tjs__string_decoder) },
    { "tjs:tty", tjs__tty, sizeof(tjs__tty) },
    { "tjs:tls", tjs__tls, sizeof(tjs__tls) },
    { "tjs:url", tjs__url, sizeof(tjs__url) },
    { "tjs:util", tjs__util, sizeof(tjs__util) },
    { "tjs:uuid", tjs__uuid, sizeof(tjs__uuid) },
    { "tjs:wasi", tjs__wasi, sizeof(tjs__wasi) },
    { "tjs:ws", tjs__ws, sizeof(tjs__ws) },
    { NULL, NULL, 0 },
};

static const tjs_builtin_alias_t builtin_aliases[] = {
    { "assert", "tjs:assert" },
    { "buffer", "tjs:buffer" },
    { "child_process", "tjs:child_process" },
    { "crypto", "tjs:crypto" },
    { "dgram", "tjs:dgram" },
    { "dns", "tjs:dns" },
    { "events", "tjs:events" },
    { "node:assert", "tjs:assert" },
    { "node:buffer", "tjs:buffer" },
    { "node:child_process", "tjs:child_process" },
    { "node:crypto", "tjs:crypto" },
    { "node:sqlite", "tjs:node-sqlite" },
    { "node:dgram", "tjs:dgram" },
    { "node:dns", "tjs:dns" },
    { "node:events", "tjs:events" },
    { "node:fs", "tjs:fs" },
    { "node:fs/promises", "tjs:fs-promises" },
    { "node:mqtt", "tjs:mqtt" },
    { "node:path", "tjs:path" },
    { "node:process", "tjs:process" },
    { "node:querystring", "tjs:querystring" },
    { "node:readline", "tjs:readline" },
    { "node:url", "tjs:url" },
    { "node:util", "tjs:util" },
    { "node:wasi", "tjs:wasi" },
    { "node:stream", "tjs:stream" },
    { "node:string_decoder", "tjs:string_decoder" },
    { "node:tty", "tjs:tty" },
    { "node:ws", "tjs:ws" },
    { "node:net", "tjs:net" },
    { "node:os", "tjs:os" },
    { "node:tls", "tjs:tls" },
    { "node:http", "tjs:http" },
#ifdef TJS__HAS_HTTP2
    { "node:http2", "tjs:http2" },
#endif
    { "node:https", "tjs:https" },
    { "node:zlib", "tjs:zlib" },
    { "fs", "tjs:fs" },
    { "fs/promises", "tjs:fs-promises" },
    { "readline", "tjs:readline" },
    { "http", "tjs:http" },
#ifdef TJS__HAS_HTTP2
    { "http2", "tjs:http2" },
#endif
    { "https", "tjs:https" },
    { "mini-mqtt-client", "tjs:mqtt" },
    { "mqtt", "tjs:mqtt" },
    { "net", "tjs:net" },
    { "os", "tjs:os" },
    { "zlib", "tjs:zlib" },
    { "tls", "tjs:tls" },
    { "path", "tjs:path" },
    { "process", "tjs:process" },
    { "querystring", "tjs:querystring" },
    { "serialport", "tjs:serial" },
    { "stream", "tjs:stream" },
    { "string_decoder", "tjs:string_decoder" },
    { "tty", "tjs:tty" },
    { "url", "tjs:url" },
    { "util", "tjs:util" },
    { "wasi", "tjs:wasi" },
    { "ws", "tjs:ws" },
    { NULL, NULL },
};

static tjs_builtin_t *tjs__find_builtin(const char *name) {
    for (tjs_builtin_t *p = builtins; p->name != NULL; ++p) {
        if (strcmp(p->name, name) == 0) {
            return p;
        }
    }
    return NULL;
}

const char *tjs__canonical_builtin_specifier(const char *name) {
    if (!name) {
        return NULL;
    }
    for (const tjs_builtin_alias_t *p = builtin_aliases; p->name != NULL; ++p) {
        if (strcmp(p->name, name) == 0) {
            return p->builtin;
        }
    }
    return tjs__find_builtin(name) ? name : NULL;
}

JSModuleDef *tjs__load_builtin(JSContext *ctx, const char *name) {
    const char *canonical_name = tjs__canonical_builtin_specifier(name);
    tjs_builtin_t *item = canonical_name ? tjs__find_builtin(canonical_name) : NULL;
    if (!item) {
        return NULL;
    }

    JSValue obj = JS_ReadObject(ctx, item->data, item->data_size, JS_READ_OBJ_BYTECODE);

    CHECK_EQ(JS_IsException(obj), 0);
    CHECK_EQ(JS_VALUE_GET_TAG(obj), JS_TAG_MODULE);

    JSModuleDef *m = JS_VALUE_GET_PTR(obj);
    JS_FreeValue(ctx, obj);

    return m;
}

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
        if (r <= 0) {
            return JS_EXCEPTION;
        }
    }
}

JSValue tjs__load_builtin_namespace(JSContext *ctx, const char *name) {
    const char *canonical_name = tjs__canonical_builtin_specifier(name);
    tjs_builtin_t *item = canonical_name ? tjs__find_builtin(canonical_name) : NULL;
    if (!item) {
        return JS_ThrowReferenceError(ctx, "Cannot find module '%s'", name);
    }

    JSValue module_obj = JS_ReadObject(ctx, item->data, item->data_size, JS_READ_OBJ_BYTECODE);
    if (JS_IsException(module_obj)) {
        return module_obj;
    }

    JSValue module_obj_dup = JS_DupValue(ctx, module_obj);
    if (JS_ResolveModule(ctx, module_obj) < 0) {
        JS_FreeValue(ctx, module_obj_dup);
        return JS_EXCEPTION;
    }
    if (js_module_set_import_meta(ctx, module_obj, false, false) != 0) {
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
