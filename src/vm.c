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

#include "mem.h"
#include "private.h"
#include "tjs.h"

#include <assert.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

#define TJS__DEFAULT_STACK_SIZE 1024 * 1024  // 1 MB

/* JS malloc functions */

static void *tjs__mf_calloc(void *opaque, size_t count, size_t size) {
    (void) opaque;
    return tjs__calloc(count, size);
}

static void *tjs__mf_malloc(void *opaque, size_t size) {
    (void) opaque;
    return tjs__malloc(size);
}

static void tjs__mf_free(void *opaque, void *ptr) {
    (void) opaque;
    tjs__free(ptr);
}

static void *tjs__mf_realloc(void *opaque, void *ptr, size_t size) {
    (void) opaque;
    return tjs__realloc(ptr, size);
}

static const JSMallocFunctions tjs_mf = {
    .js_calloc = tjs__mf_calloc,
    .js_malloc = tjs__mf_malloc,
    .js_free = tjs__mf_free,
    .js_realloc = tjs__mf_realloc,
    .js_malloc_usable_size = tjs__malloc_usable_size,
};

/* SharedArrayBuffer functions */

typedef struct {
    int ref_count;
    uint8_t buf[0];
} TJSSABHeader;

static int atomic_add_int(int *ptr, int v) {
    return atomic_fetch_add((_Atomic(uint32_t) *) ptr, v) + v;
}

static void *tjs__sab_alloc(void *opaque, size_t size) {
    TJSSABHeader *sab = tjs__malloc(sizeof(*sab) + size);
    if (!sab) {
        return NULL;
    }
    sab->ref_count = 1;
    return sab->buf;
}

void tjs__sab_free(void *opaque, void *ptr) {
    TJSSABHeader *sab = (TJSSABHeader *) ((uint8_t *) ptr - sizeof(TJSSABHeader));
    int ref_count = atomic_add_int(&sab->ref_count, -1);
    assert(ref_count >= 0);
    if (ref_count == 0) {
        tjs__free(sab);
    }
}

void tjs__sab_dup(void *opaque, void *ptr) {
    TJSSABHeader *sab = (TJSSABHeader *) ((uint8_t *) ptr - sizeof(TJSSABHeader));
    atomic_add_int(&sab->ref_count, 1);
}

static const JSSharedArrayBufferFunctions tjs_sf = {
    .sab_alloc = tjs__sab_alloc,
    .sab_dup = tjs__sab_dup,
    .sab_free = tjs__sab_free,
    .sab_opaque = NULL,
};

/* core */
extern const uint8_t tjs__core[];
extern const uint32_t tjs__core_size;
extern const uint8_t tjs__polyfills_compat[];
extern const uint32_t tjs__polyfills_compat_size;
extern const uint8_t tjs__polyfills_lite[];
extern const uint32_t tjs__polyfills_lite_size;
extern const uint8_t tjs__polyfills_heavy[];
extern const uint32_t tjs__polyfills_heavy_size;
extern const uint8_t tjs__run_main[];
extern const uint32_t tjs__run_main_size;


static int tjs__argc = 0;
static char **tjs__argv = NULL;

typedef struct {
    struct list_head link;
    JSValue promise;
    JSValue reason;
} TJSPendingPromiseRejection;

typedef struct {
    const char *name;
    uint64_t ns;
} TJSStartupProfileEntry;

typedef struct {
    bool enabled;
    uint64_t start_ns;
    uint64_t phase_start_ns;
    const char *phase_name;
    int count;
    TJSStartupProfileEntry entries[64];
} TJSStartupProfile;

static int tjs__startup_profile_enabled(void) {
    static int cached = -1;
    if (cached >= 0) {
        return cached;
    }

    const char *v = getenv("XUAN_STARTUP_PROFILE");
    cached = (v && *v && strcmp(v, "0") != 0) ? 1 : 0;
    return cached;
}

static int tjs__parse_bool_env(const char *v, bool *out) {
    if (!v || !*v) {
        return 0;
    }

    if (!strcmp(v, "1") || !strcmp(v, "true") || !strcmp(v, "TRUE") || !strcmp(v, "yes")
        || !strcmp(v, "YES") || !strcmp(v, "on") || !strcmp(v, "ON")) {
        *out = true;
        return 1;
    }

    if (!strcmp(v, "0") || !strcmp(v, "false") || !strcmp(v, "FALSE") || !strcmp(v, "no")
        || !strcmp(v, "NO") || !strcmp(v, "off") || !strcmp(v, "OFF")) {
        *out = false;
        return 1;
    }

    return 0;
}

static bool tjs__should_load_heavy_web_polyfills(void) {
    const char *forced = getenv("XUAN_HEAVY_WEB_POLYFILLS");
    bool forced_value = false;
    if (tjs__parse_bool_env(forced, &forced_value)) {
        return forced_value;
    }

    if (tjs__argc < 2 || !tjs__argv || !tjs__argv[1]) {
        return true;
    }

    const char *arg1 = tjs__argv[1];
    if (!strcmp(arg1, "-e") || !strcmp(arg1, "--eval") || !strcmp(arg1, "eval") || !strcmp(arg1, "-h")
        || !strcmp(arg1, "--help") || !strcmp(arg1, "-v") || !strcmp(arg1, "--version")) {
        return false;
    }

    return true;
}

static bool tjs__is_startup_light_command(void) {
    if (tjs__argc < 2 || !tjs__argv || !tjs__argv[1]) {
        return false;
    }

    const char *arg1 = tjs__argv[1];
    return !strcmp(arg1, "-e") || !strcmp(arg1, "--eval") || !strcmp(arg1, "-h") || !strcmp(arg1, "--help")
           || !strcmp(arg1, "-v") || !strcmp(arg1, "--version");
}

static bool tjs__should_use_lightweight_polyfills(void) {
    const char *forced = getenv("XUAN_LIGHTWEIGHT_POLYFILLS");
    bool forced_value = false;
    if (tjs__parse_bool_env(forced, &forced_value)) {
        return forced_value;
    }

    if (tjs__should_load_heavy_web_polyfills()) {
        return false;
    }

    return tjs__is_startup_light_command();
}

static void tjs__startup_profile_init(TJSStartupProfile *sp) {
    memset(sp, 0, sizeof(*sp));
    sp->enabled = tjs__startup_profile_enabled() == 1;
    if (sp->enabled) {
        sp->start_ns = uv_hrtime();
    }
}

static void tjs__startup_profile_begin(TJSStartupProfile *sp, const char *name) {
    if (!sp->enabled) {
        return;
    }
    sp->phase_name = name;
    sp->phase_start_ns = uv_hrtime();
}

static void tjs__startup_profile_end(TJSStartupProfile *sp) {
    if (!sp->enabled || !sp->phase_name) {
        return;
    }
    if (sp->count < (int) countof(sp->entries)) {
        sp->entries[sp->count].name = sp->phase_name;
        sp->entries[sp->count].ns = uv_hrtime() - sp->phase_start_ns;
        sp->count++;
    }
    sp->phase_name = NULL;
}

static int tjs__startup_profile_cmp_desc(const void *a, const void *b) {
    const TJSStartupProfileEntry *ea = a;
    const TJSStartupProfileEntry *eb = b;
    if (ea->ns < eb->ns) {
        return 1;
    }
    if (ea->ns > eb->ns) {
        return -1;
    }
    return 0;
}

static void tjs__startup_profile_print(TJSStartupProfile *sp) {
    if (!sp->enabled) {
        return;
    }

    TJSStartupProfileEntry sorted[64];
    int n = sp->count;
    memcpy(sorted, sp->entries, sizeof(TJSStartupProfileEntry) * n);
    qsort(sorted, n, sizeof(sorted[0]), tjs__startup_profile_cmp_desc);

    fprintf(stderr, "[startup-profile] total %.3f ms\n", (uv_hrtime() - sp->start_ns) / 1000000.0);
    for (int i = 0; i < n; i++) {
        fprintf(stderr, "[startup-profile] %-24s %.3f ms\n", sorted[i].name, sorted[i].ns / 1000000.0);
    }
}

static JSValue tjs__set_cookie_jar_path(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    TJSRuntime *qrt = TJS_GetRuntime(ctx);
    CHECK_NOT_NULL(qrt);

    CHECK_EQ(qrt->lws.cookie_jar_path, NULL);

    const char *path = JS_ToCString(ctx, argv[0]);
    if (!path) {
        return JS_EXCEPTION;
    }

    qrt->lws.cookie_jar_path = js_strdup(ctx, path);
    JS_FreeCString(ctx, path);

    return JS_UNDEFINED;
}

static void tjs__bootstrap_core(JSContext *ctx, JSValue ns, TJSStartupProfile *sp) {
    tjs__startup_profile_begin(sp, "mod_buffer_init");
    tjs__mod_buffer_init(ctx, ns);
    tjs__startup_profile_end(sp);
    tjs__startup_profile_begin(sp, "mod_crypto_init");
    tjs__mod_crypto_init(ctx, ns);
    tjs__startup_profile_end(sp);
    tjs__startup_profile_begin(sp, "mod_dns_init");
    tjs__mod_dns_init(ctx, ns);
    tjs__startup_profile_end(sp);
    tjs__startup_profile_begin(sp, "mod_engine_init");
    tjs__mod_engine_init(ctx, ns);
    tjs__startup_profile_end(sp);
    tjs__startup_profile_begin(sp, "mod_error_init");
    tjs__mod_error_init(ctx, ns);
    tjs__startup_profile_end(sp);
    tjs__startup_profile_begin(sp, "mod_events_init");
    tjs__mod_events_init(ctx, ns);
    tjs__startup_profile_end(sp);
    tjs__startup_profile_begin(sp, "mod_ffi_init");
    tjs__mod_ffi_init(ctx, ns);
    tjs__startup_profile_end(sp);
    tjs__startup_profile_begin(sp, "mod_fs_init");
    tjs__mod_fs_init(ctx, ns);
    tjs__startup_profile_end(sp);
    tjs__startup_profile_begin(sp, "mod_fswatch_init");
    tjs__mod_fswatch_init(ctx, ns);
    tjs__startup_profile_end(sp);
    tjs__startup_profile_begin(sp, "mod_os_init");
    tjs__mod_os_init(ctx, ns);
    tjs__startup_profile_end(sp);
    tjs__startup_profile_begin(sp, "mod_process_init");
    tjs__mod_process_init(ctx, ns);
    tjs__startup_profile_end(sp);
    tjs__startup_profile_begin(sp, "mod_signals_init");
    tjs__mod_signals_init(ctx, ns);
    tjs__startup_profile_end(sp);
#ifndef TJS_DISABLE_SQLITE3
    tjs__startup_profile_begin(sp, "mod_sqlite3_init");
    tjs__mod_sqlite3_init(ctx, ns);
    tjs__startup_profile_end(sp);
#endif
    tjs__startup_profile_begin(sp, "mod_streams_init");
    tjs__mod_streams_init(ctx, ns);
    tjs__startup_profile_end(sp);
    tjs__startup_profile_begin(sp, "mod_tls_init");
    tjs__mod_tls_init(ctx, ns);
    tjs__startup_profile_end(sp);
    tjs__startup_profile_begin(sp, "mod_sys_init");
    tjs__mod_sys_init(ctx, ns);
    tjs__startup_profile_end(sp);
    tjs__startup_profile_begin(sp, "mod_text_coding_init");
    tjs__mod_text_coding_init(ctx, ns);
    tjs__startup_profile_end(sp);
    tjs__startup_profile_begin(sp, "mod_timers_init");
    tjs__mod_timers_init(ctx, ns);
    tjs__startup_profile_end(sp);
    tjs__startup_profile_begin(sp, "mod_udp_init");
    tjs__mod_udp_init(ctx, ns);
    tjs__startup_profile_end(sp);
    tjs__startup_profile_begin(sp, "mod_util_init");
    tjs__mod_util_init(ctx, ns);
    tjs__startup_profile_end(sp);
    tjs__startup_profile_begin(sp, "mod_url_init");
    tjs__mod_url_init(ctx, ns);
    tjs__startup_profile_end(sp);
    tjs__startup_profile_begin(sp, "mod_node_url_init");
    tjs__mod_node_url_init(ctx, ns);
    tjs__startup_profile_end(sp);
#ifndef TJS_DISABLE_WASM
    tjs__startup_profile_begin(sp, "mod_wasm_init");
    tjs__mod_wasm_init(ctx, ns);
    tjs__startup_profile_end(sp);
#endif
    tjs__startup_profile_begin(sp, "mod_worker_init");
    tjs__mod_worker_init(ctx, ns);
    tjs__startup_profile_end(sp);
    tjs__startup_profile_begin(sp, "mod_httpclient_init");
    tjs__mod_httpclient_init(ctx, ns);
    tjs__startup_profile_end(sp);
#ifdef TJS__HAS_HTTP2
    tjs__startup_profile_begin(sp, "mod_http2_init");
    tjs__mod_http2_init(ctx, ns);
    tjs__startup_profile_end(sp);
#endif
    tjs__startup_profile_begin(sp, "mod_mqtt_init");
    tjs__mod_mqtt_init(ctx, ns);
    tjs__startup_profile_end(sp);
    tjs__startup_profile_begin(sp, "mod_ws_init");
    tjs__mod_ws_init(ctx, ns);
    tjs__startup_profile_end(sp);
    tjs__startup_profile_begin(sp, "mod_httpserver_init");
    tjs__mod_httpserver_init(ctx, ns);
    tjs__startup_profile_end(sp);
#ifndef _WIN32
    tjs__startup_profile_begin(sp, "mod_posix_socket_init");
    tjs__mod_posix_socket_init(ctx, ns);
    tjs__startup_profile_end(sp);
#endif
#ifdef TJS_HAS_LVGL
    tjs__startup_profile_begin(sp, "mod_lvgl_init");
    tjs__mod_lvgl_init(ctx, ns);
    tjs__startup_profile_end(sp);
#endif
    tjs__startup_profile_begin(sp, "init_require");
    tjs__init_require(ctx);
    tjs__startup_profile_end(sp);

    /* Internal helpers. */
    tjs__startup_profile_begin(sp, "set_cookie_jar_helper");
    JS_DefinePropertyValueStr(ctx,
                              ns,
                              "setCookieJarPath",
                              JS_NewCFunction(ctx, tjs__set_cookie_jar_path, "setCookieJarPath", 1),
                              JS_PROP_C_W_E);
    tjs__startup_profile_end(sp);
}

JSValue tjs__get_args(JSContext *ctx) {
    JSValue args = JS_NewArray(ctx);
    for (int i = 0; i < tjs__argc; i++) {
        JS_SetPropertyUint32(ctx, args, i, JS_NewString(ctx, tjs__argv[i]));
    }
    return args;
}

static JSValue tjs__dispatch_event(JSContext *ctx, JSValue *event) {
    TJSRuntime *qrt = TJS_GetRuntime(ctx);
    CHECK_NOT_NULL(qrt);

    if (qrt->freeing) {
        return JS_UNDEFINED;
    }

    JSValue global_obj = JS_GetGlobalObject(ctx);
    JSValue ret = JS_Call(ctx, qrt->builtins.dispatch_event_func, global_obj, 1, event);
    JS_FreeValue(ctx, global_obj);

    return ret;
}

static JSValue tjs__dispatch_promise_rejection_event(JSContext *ctx,
                                                     const char *event_type,
                                                     JSValueConst promise,
                                                     JSValueConst reason) {
    TJSRuntime *qrt = TJS_GetRuntime(ctx);
    CHECK_NOT_NULL(qrt);

    JSValue event_name = JS_NewString(ctx, event_type);
    JSValue args[3];
    args[0] = event_name;
    args[1] = JS_DupValue(ctx, promise);
    args[2] = JS_DupValue(ctx, reason);
    JSValue event = JS_CallConstructor(ctx, qrt->builtins.promise_event_ctor, countof(args), args);
    JS_FreeValue(ctx, args[2]);
    JS_FreeValue(ctx, args[1]);
    JS_FreeValue(ctx, args[0]);
    if (JS_IsException(event)) {
        return event;
    }

    JSValue ret = tjs__dispatch_event(ctx, &event);
    JS_FreeValue(ctx, event);
    return ret;
}

static TJSPendingPromiseRejection *tjs__find_pending_promise_rejection(JSContext *ctx,
                                                                        TJSRuntime *qrt,
                                                                        JSValueConst promise) {
    struct list_head *el;
    list_for_each(el, &qrt->promise_rejections.pending) {
        TJSPendingPromiseRejection *entry = list_entry(el, TJSPendingPromiseRejection, link);
        if (JS_IsSameValue(ctx, entry->promise, promise)) {
            return entry;
        }
    }

    return NULL;
}

static void tjs__free_pending_promise_rejection(JSContext *ctx, TJSPendingPromiseRejection *entry) {
    list_del(&entry->link);
    JS_FreeValue(ctx, entry->promise);
    JS_FreeValue(ctx, entry->reason);
    js_free(ctx, entry);
}

static void tjs__clear_pending_promise_rejections(JSContext *ctx) {
    TJSRuntime *qrt = TJS_GetRuntime(ctx);
    CHECK_NOT_NULL(qrt);

    struct list_head *el, *el1;
    list_for_each_safe(el, el1, &qrt->promise_rejections.pending) {
        TJSPendingPromiseRejection *entry = list_entry(el, TJSPendingPromiseRejection, link);
        tjs__free_pending_promise_rejection(ctx, entry);
    }
}

static void tjs__flush_pending_promise_rejections(JSContext *ctx) {
    TJSRuntime *qrt = TJS_GetRuntime(ctx);
    CHECK_NOT_NULL(qrt);

    if (qrt->freeing) {
        return;
    }

    struct list_head *el, *el1;
    list_for_each_safe(el, el1, &qrt->promise_rejections.pending) {
        TJSPendingPromiseRejection *entry = list_entry(el, TJSPendingPromiseRejection, link);
        JSValue ret =
            tjs__dispatch_promise_rejection_event(ctx, "unhandledrejection", entry->promise, entry->reason);
        tjs__free_pending_promise_rejection(ctx, entry);

        if (JS_IsException(ret)) {
            tjs_dump_error(ctx);
        }
        JS_FreeValue(ctx, ret);
    }
}

static void tjs__promise_rejection_tracker(JSContext *ctx,
                                           JSValue promise,
                                           JSValue reason,
                                           bool is_handled,
                                           void *opaque) {
    (void) opaque;

    TJSRuntime *qrt = TJS_GetRuntime(ctx);
    CHECK_NOT_NULL(qrt);

    if (qrt->freeing) {
        return;
    }

    TJSPendingPromiseRejection *entry = tjs__find_pending_promise_rejection(ctx, qrt, promise);
    if (is_handled) {
        if (entry) {
            tjs__free_pending_promise_rejection(ctx, entry);
        }
        return;
    }

    if (!entry) {
        entry = js_mallocz(ctx, sizeof(*entry));
        if (!entry) {
            return;
        }
        entry->promise = JS_DupValue(ctx, promise);
        entry->reason = JS_DupValue(ctx, reason);
        init_list_head(&entry->link);
        list_add_tail(&entry->link, &qrt->promise_rejections.pending);
    }
}

static void uv__stop(uv_async_t *handle) {
    TJSRuntime *qrt = handle->data;
    CHECK_NOT_NULL(qrt);

    uv_stop(&qrt->loop);
}

void TJS_DefaultOptions(TJSRunOptions *options) {
    static TJSRunOptions default_options = { .mem_limit = 0, .stack_size = TJS__DEFAULT_STACK_SIZE };

    memcpy(options, &default_options, sizeof(*options));
}

TJSRuntime *TJS_NewRuntime(void) {
    TJSRunOptions options;
    TJS_DefaultOptions(&options);
    return TJS_NewRuntimeInternal(false, &options);
}

TJSRuntime *TJS_NewRuntimeOptions(TJSRunOptions *options) {
    return TJS_NewRuntimeInternal(false, options);
}

TJSRuntime *TJS_NewRuntimeWorker(void) {
    TJSRunOptions options;
    TJS_DefaultOptions(&options);
    return TJS_NewRuntimeInternal(true, &options);
}

TJSRuntime *TJS_NewRuntimeInternal(bool is_worker, TJSRunOptions *options) {
    JSRuntime *rt = NULL;
    JSContext *ctx = NULL;
    TJSRuntime *qrt = tjs__mallocz(sizeof(*qrt));

    memcpy(&qrt->options, options, sizeof(*options));
    init_list_head(&qrt->promise_rejections.pending);
    TJSStartupProfile sp;
    tjs__startup_profile_init(&sp);

    rt = JS_NewRuntime2(&tjs_mf, NULL);
    CHECK_NOT_NULL(rt);
    qrt->rt = rt;

#ifdef DEBUG
    JS_SetDumpFlags(rt, JS_DUMP_LEAKS);
#endif

    ctx = JS_NewContext(rt);
    CHECK_NOT_NULL(ctx);
    qrt->ctx = ctx;

    JS_SetRuntimeOpaque(rt, qrt);
    JS_SetContextOpaque(ctx, qrt);

    /* Set memory limit */
    JS_SetMemoryLimit(rt, options->mem_limit);

    /* Set stack size */
    JS_SetMaxStackSize(rt, options->stack_size);

    /* SharedArrayBuffer functions */
    JS_SetSharedArrayBufferFunctions(rt, &tjs_sf);

    /* Worker support */
    qrt->is_worker = is_worker;
    JS_SetCanBlock(rt, is_worker);

    CHECK_EQ(uv_loop_init(&qrt->loop), 0);

    /* handle which runs the job queue */
    CHECK_EQ(uv_prepare_init(&qrt->loop, &qrt->jobs.prepare), 0);
    qrt->jobs.prepare.data = qrt;

    /* handle to prevent the loop from blocking for i/o when there are pending jobs. */
    CHECK_EQ(uv_idle_init(&qrt->loop, &qrt->jobs.idle), 0);
    qrt->jobs.idle.data = qrt;

    /* handle which runs the job queue */
    CHECK_EQ(uv_check_init(&qrt->loop, &qrt->jobs.check), 0);
    qrt->jobs.check.data = qrt;

    /* handle for stopping this runtime (also works from another thread) */
    CHECK_EQ(uv_async_init(&qrt->loop, &qrt->stop, uv__stop), 0);
    qrt->stop.data = qrt;

    /* loader for ES modules */
    JS_SetModuleLoaderFunc2(rt, tjs_module_normalizer, tjs_module_loader, tjs_module_attr_checker, qrt);

    /* unhandled promise rejection tracker */
    JS_SetHostPromiseRejectionTracker(rt, tjs__promise_rejection_tracker, NULL);

    /* start bootstrap */
    JSValue global_obj = JS_GetGlobalObject(ctx);
    JSValue core_sym = JS_NewSymbol(ctx, "tjs.internal.core", true);
    JSAtom core_atom = JS_ValueToAtom(ctx, core_sym);
    JSValue core = JS_NewObjectProto(ctx, JS_NULL);

    CHECK_EQ(JS_DefinePropertyValue(ctx, global_obj, core_atom, core, JS_PROP_C_W_E), true);
    CHECK_EQ(JS_DefinePropertyValueStr(ctx, core, "isWorker", JS_NewBool(ctx, is_worker), JS_PROP_C_W_E), true);

    tjs__bootstrap_core(ctx, core, &sp);

    bool use_lite_polyfills = tjs__should_use_lightweight_polyfills();
    tjs__startup_profile_begin(&sp, "eval_polyfills_lite_bytecode");
    CHECK_EQ(tjs__eval_bytecode(ctx, tjs__polyfills_lite, tjs__polyfills_lite_size, true), 0);
    tjs__startup_profile_end(&sp);

    if (!use_lite_polyfills) {
        tjs__startup_profile_begin(&sp, "eval_polyfills_compat_bytecode");
        CHECK_EQ(tjs__eval_bytecode(ctx, tjs__polyfills_compat, tjs__polyfills_compat_size, true), 0);
        tjs__startup_profile_end(&sp);
    }

    if (!use_lite_polyfills && tjs__should_load_heavy_web_polyfills()) {
        tjs__startup_profile_begin(&sp, "eval_polyfills_heavy_bytecode");
        CHECK_EQ(tjs__eval_bytecode(ctx, tjs__polyfills_heavy, tjs__polyfills_heavy_size, true), 0);
        tjs__startup_profile_end(&sp);
    }
    tjs__startup_profile_begin(&sp, "eval_core_bytecode");
    CHECK_EQ(tjs__eval_bytecode(ctx, tjs__core, tjs__core_size, true), 0);
    tjs__startup_profile_end(&sp);

    /* Load some builtin references for easy access */
    tjs__startup_profile_begin(&sp, "load_builtin_refs");
    qrt->builtins.dispatch_event_func = JS_GetPropertyStr(ctx, global_obj, "dispatchEvent");
    CHECK_EQ(JS_IsUndefined(qrt->builtins.dispatch_event_func), 0);
    qrt->builtins.promise_event_ctor = JS_GetPropertyStr(qrt->ctx, global_obj, "PromiseRejectionEvent");
    CHECK_EQ(JS_IsUndefined(qrt->builtins.promise_event_ctor), 0);
    tjs__startup_profile_end(&sp);

    /* end bootstrap */
    JS_FreeAtom(ctx, core_atom);
    JS_FreeValue(ctx, core_sym);
    JS_FreeValue(ctx, global_obj);

    /* Timers */
    qrt->timers.timers = NULL;
    qrt->timers.next_timer = 1;
    tjs__startup_profile_print(&sp);

    return qrt;
}

#ifndef TJS_DISABLE_WASM
int tjs__ensure_wasm_runtime_initialized(JSContext *ctx) {
    TJSRuntime *qrt = TJS_GetRuntime(ctx);
    CHECK_NOT_NULL(qrt);

    if (qrt->wasm_ctx.initialized) {
        return 0;
    }

    RuntimeInitArgs wasm_init_args;
    memset(&wasm_init_args, 0, sizeof(wasm_init_args));
    wasm_init_args.mem_alloc_type = Alloc_With_Allocator;
    wasm_init_args.mem_alloc_option.allocator.malloc_func = tjs__malloc;
    wasm_init_args.mem_alloc_option.allocator.realloc_func = tjs__realloc;
    wasm_init_args.mem_alloc_option.allocator.free_func = tjs__free;

    if (!wasm_runtime_full_init(&wasm_init_args)) {
        JS_ThrowInternalError(ctx, "failed to initialize wasm runtime");
        return -1;
    }

    qrt->wasm_ctx.initialized = true;
    return 0;
}
#endif

void TJS_FreeRuntime(TJSRuntime *qrt) {
    qrt->freeing = true;

    /* Close all core loop handles. */
    uv_close((uv_handle_t *) &qrt->jobs.prepare, NULL);
    uv_close((uv_handle_t *) &qrt->jobs.idle, NULL);
    uv_close((uv_handle_t *) &qrt->jobs.check, NULL);
    uv_close((uv_handle_t *) &qrt->stop, NULL);

    /* Destroy all timers */
    tjs__destroy_timers(qrt);

    /* Destroy lws context. Must happen before freeing the JS engine
     * so that any remaining WSI_DESTROY callbacks can release GC refs. */
    if (qrt->lws.ctx) {
        lws_context_destroy(qrt->lws.ctx);
        qrt->lws.ctx = NULL;
        uv_close((uv_handle_t *) &qrt->lws.keepalive, NULL);
    }
    js_free(qrt->ctx, qrt->lws.cookie_jar_path);
    qrt->lws.cookie_jar_path = NULL;
    js_free(qrt->ctx, qrt->lws.client_ca_pem);
    qrt->lws.client_ca_pem = NULL;
    qrt->lws.client_ca_pem_len = 0;
    js_free(qrt->ctx, qrt->lws.client_cert_der);
    qrt->lws.client_cert_der = NULL;
    qrt->lws.client_cert_der_len = 0;
    js_free(qrt->ctx, qrt->lws.client_key_der);
    qrt->lws.client_key_der = NULL;
    qrt->lws.client_key_der_len = 0;
    js_free(qrt->ctx, qrt->lws.last_tls_error_message);
    qrt->lws.last_tls_error_message = NULL;
    qrt->lws.last_tls_error_code = 0;

    /* Drain any pending lws close callbacks. */
    uv_run(&qrt->loop, UV_RUN_NOWAIT);

    /* Destroy the JS engine. */
    tjs__clear_pending_promise_rejections(qrt->ctx);
    JS_FreeValue(qrt->ctx, qrt->builtins.dispatch_event_func);
    qrt->builtins.dispatch_event_func = JS_UNDEFINED;
    JS_FreeValue(qrt->ctx, qrt->builtins.promise_event_ctor);
    qrt->builtins.promise_event_ctor = JS_UNDEFINED;
    JS_FreeContext(qrt->ctx);
    JS_FreeRuntime(qrt->rt);

    /* Destroy WASM runtime. */
#ifndef TJS_DISABLE_WASM
    if (qrt->wasm_ctx.initialized) {
        wasm_runtime_destroy();
        qrt->wasm_ctx.initialized = false;
    }
#endif

    /* Cleanup loop. All handles should be closed. */
    int closed = 0;
    for (int i = 0; i < 5; i++) {
        if (uv_loop_close(&qrt->loop) == 0) {
            closed = 1;
            break;
        }
        uv_run(&qrt->loop, UV_RUN_NOWAIT);
    }
#ifdef DEBUG
    if (!closed) {
        uv_print_all_handles(&qrt->loop, stderr);
    }
    CHECK_EQ(closed, 1);
#else
    (void) closed;
#endif

    tjs__free(qrt);
}

void TJS_Initialize(int argc, char **argv) {
    CHECK_EQ(0, uv_replace_allocator(tjs__malloc, tjs__realloc, tjs__calloc, tjs__free));

    tjs__argc = argc;
    tjs__argv = uv_setup_args(argc, argv);

    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

#ifdef _WIN32
    _setmode(_fileno(stdout), _O_BINARY);
    _setmode(_fileno(stderr), _O_BINARY);
#endif

#ifdef SIGPIPE
    signal(SIGPIPE, SIG_IGN);
#endif
}

JSContext *TJS_GetJSContext(TJSRuntime *qrt) {
    return qrt->ctx;
}

TJSRuntime *TJS_GetRuntime(JSContext *ctx) {
    return JS_GetContextOpaque(ctx);
}

static void uv__idle_cb(uv_idle_t *handle) {
    // Noop
}

static void uv__maybe_idle(TJSRuntime *qrt) {
    if (JS_IsJobPending(qrt->rt)) {
        CHECK_EQ(uv_idle_start(&qrt->jobs.idle, uv__idle_cb), 0);
    } else {
        CHECK_EQ(uv_idle_stop(&qrt->jobs.idle), 0);
    }
}

static void uv__prepare_cb(uv_prepare_t *handle) {
    TJSRuntime *qrt = handle->data;
    CHECK_NOT_NULL(qrt);

    uv__maybe_idle(qrt);
}

void tjs__execute_jobs(JSContext *ctx) {
    JSContext *ctx1;
    int err;

    /* execute the pending jobs */
    for (;;) {
        err = JS_ExecutePendingJob(JS_GetRuntime(ctx), &ctx1);
        if (err <= 0) {
            if (err < 0) {
                TJSRuntime *qrt = TJS_GetRuntime(ctx1);
                CHECK_NOT_NULL(qrt);
                JSValue exception_val = JS_GetException(ctx1);
                if (tjs__handle_uncaught_exception(ctx1, exception_val)) {
                    JS_FreeValue(ctx1, exception_val);
                    continue;
                }
                if (JS_HasException(ctx1)) {
                    tjs_dump_error(ctx1);
                } else {
                    tjs_dump_error1(ctx1, exception_val);
                }
                JS_FreeValue(ctx1, exception_val);
                if (qrt->exit_code == 0) {
                    qrt->exit_code = 1;
                }
                TJS_Stop(qrt);
            }

            break;
        }
    }

    if (!JS_IsJobPending(JS_GetRuntime(ctx))) {
        tjs__flush_pending_promise_rejections(ctx);
    }
}

static void uv__check_cb(uv_check_t *handle) {
    TJSRuntime *qrt = handle->data;
    CHECK_NOT_NULL(qrt);

    tjs__execute_jobs(qrt->ctx);

    uv__maybe_idle(qrt);
}

/* main loop which calls the user JS callbacks */
int TJS_Run(TJSRuntime *qrt) {
    int ret = qrt->exit_code;

    CHECK_EQ(uv_prepare_start(&qrt->jobs.prepare, uv__prepare_cb), 0);
    uv_unref((uv_handle_t *) &qrt->jobs.prepare);
    CHECK_EQ(uv_check_start(&qrt->jobs.check, uv__check_cb), 0);
    uv_unref((uv_handle_t *) &qrt->jobs.check);

    /* Use the async handle to keep the worker alive even when there is nothing to do. */
    if (!qrt->is_worker) {
        uv_unref((uv_handle_t *) &qrt->stop);

        /* If we are running the main interpreter, run the entrypoint. */
        ret = tjs__eval_bytecode(qrt->ctx, tjs__run_main, tjs__run_main_size, true);
    }

    if (ret != 0) {
        return ret;
    }

    int r;
    do {
        uv__maybe_idle(qrt);
        r = uv_run(&qrt->loop, UV_RUN_DEFAULT);
    } while (r == 0 && JS_IsJobPending(qrt->rt));

    if (JS_HasException(qrt->ctx)) {
        tjs_dump_error(qrt->ctx);
        ret = 1;
    }

    if (ret == 0 && qrt->exit_code != 0) {
        ret = qrt->exit_code;
    }

    return ret;
}

void TJS_Stop(TJSRuntime *qrt) {
    CHECK_NOT_NULL(qrt);
    uv_async_send(&qrt->stop);
}

uv_loop_t *TJS_GetLoop(TJSRuntime *qrt) {
    return &qrt->loop;
}

int tjs__load_file(JSContext *ctx, DynBuf *dbuf, const char *filename) {
    uv_fs_t req;
    uv_file fd;
    int r;

    r = uv_fs_open(NULL, &req, filename, O_RDONLY, 0, NULL);
    uv_fs_req_cleanup(&req);
    if (r < 0) {
        return r;
    }

    fd = r;
    char buf[64 * 1024];
    uv_buf_t b = uv_buf_init(buf, sizeof(buf));
    size_t offset = 0;

    do {
        r = uv_fs_read(NULL, &req, fd, &b, 1, offset, NULL);
        uv_fs_req_cleanup(&req);
        if (r <= 0) {
            break;
        }
        offset += r;
        r = dbuf_put(dbuf, (const uint8_t *) b.base, r);
        if (r != 0) {
            break;
        }
    } while (1);

    uv_fs_close(NULL, &req, fd, NULL);
    uv_fs_req_cleanup(&req);

    return r;
}

JSValue TJS_CompileModule(JSContext *ctx, const char *specifier, const char *content, size_t len) {
    static const char module_prefix[] = "const __filename = import.meta.path;const __dirname = import.meta.dirname;";
    DynBuf dbuf;

    tjs_dbuf_init(ctx, &dbuf);

    if (dbuf_put(&dbuf, (const uint8_t *) module_prefix, sizeof(module_prefix) - 1) != 0) {
        dbuf_free(&dbuf);
        return JS_EXCEPTION;
    }

    if (len >= 2 && content[0] == '#' && content[1] == '!') {
        size_t i = 2;
        while (i < len && content[i] != '\n' && content[i] != '\r') {
            i++;
        }
        while (i < len && (content[i] == '\n' || content[i] == '\r')) {
            i++;
        }
        content = content + i;
        len = len - i;
    }

    if (dbuf_put(&dbuf, (const uint8_t *) content, len) != 0 || dbuf_putc(&dbuf, '\0') != 0) {
        dbuf_free(&dbuf);
        return JS_EXCEPTION;
    }

    JSValue ret = JS_Eval(ctx,
                          (const char *) dbuf.buf,
                          dbuf.size - 1,
                          specifier,
                          JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
    dbuf_free(&dbuf);
    return ret;
}

JSValue TJS_EvalModuleContent(JSContext *ctx,
                              const char *specifier,
                              bool is_main,
                              bool use_real_path,
                              const char *content,
                              size_t len) {
    JSValue ret = TJS_CompileModule(ctx, specifier, content, len);
    if (!JS_IsException(ret)) {
        js_module_set_import_meta(ctx, ret, use_real_path, is_main);
        ret = JS_EvalFunction(ctx, ret);
    }

    /* Emit global 'load' event. */
    if (!JS_IsException(ret) && is_main) {
        static char emit_global_load[] = "globalThis.dispatchEvent(new Event('load'));";
        JSValue ret1 = JS_Eval(ctx, emit_global_load, strlen(emit_global_load), "<global>", JS_EVAL_TYPE_GLOBAL);
        if (JS_IsException(ret1)) {
            tjs_dump_error(ctx);
        }
    }

    return ret;
}

static void stripShebang(DynBuf *dbuf) {
    char *buf = (char *) dbuf->buf;
    size_t len = dbuf->size;

    if (len >= 2 && buf[0] == '#' && buf[1] == '!') {
        size_t i = 2;
        while (i < len && buf[i] != '\n' && buf[i] != '\r') {
            i++;
        }
        while (i < len && (buf[i] == '\n' || buf[i] == '\r')) {
            i++;
        }
        if (i < len) {
            memmove(buf, buf + i, len - i);
            dbuf->size = len - i;
        } else {
            dbuf->size = 0;
        }
    }
}

JSValue TJS_EvalScript(JSContext *ctx, const char *filename) {
    DynBuf dbuf;
    int r;
    JSValue ret;

    tjs_dbuf_init(ctx, &dbuf);
    r = tjs__load_file(ctx, &dbuf, filename);
    if (r != 0) {
        dbuf_free(&dbuf);
        JS_ThrowReferenceError(ctx, "could not load '%s' - %s: %s", filename, uv_err_name(r), uv_strerror(r));
        return JS_EXCEPTION;
    }

    stripShebang(&dbuf);

    /* Add null termination, required by JS_Eval. */
    dbuf_putc(&dbuf, '\0');

    ret = JS_Eval(ctx, (char *) dbuf.buf, dbuf.size - 1, filename, JS_EVAL_TYPE_GLOBAL);

    dbuf_free(&dbuf);
    return ret;
}

JSValue TJS_EvalModule(JSContext *ctx, const char *filename, bool is_main) {
    DynBuf dbuf;
    int r;
    JSValue ret;

    tjs_dbuf_init(ctx, &dbuf);
    r = tjs__load_file(ctx, &dbuf, filename);
    if (r != 0) {
        dbuf_free(&dbuf);
        JS_ThrowReferenceError(ctx, "could not load '%s' - %s: %s", filename, uv_err_name(r), uv_strerror(r));
        return JS_EXCEPTION;
    }

    stripShebang(&dbuf);

    /* Add null termination, required by JS_Eval. */
    dbuf_putc(&dbuf, '\0');

    ret = TJS_EvalModuleContent(ctx, filename, is_main, true, (char *) dbuf.buf, dbuf.size - 1);

    dbuf_free(&dbuf);
    return ret;
}
