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

#ifndef TJS_PRIVATE_H
#define TJS_PRIVATE_H

#include "../deps/quickjs/cutils.h"
#include "../deps/quickjs/list.h"
#include "tjs.h"
#include "utils.h"

#include <libwebsockets.h>
#include <quickjs.h>
#ifndef TJS_DISABLE_SQLITE3
#include <sqlite3.h>
#endif
#include <stdbool.h>
#ifndef _WIN32
#include <unistd.h>
#endif
#include <uv.h>
#ifndef TJS_DISABLE_WASM
#include <wasm_export.h>
#endif

#ifndef STDIN_FILENO
#define STDIN_FILENO 0
#endif
#ifndef STDOUT_FILENO
#define STDOUT_FILENO 1
#endif
#ifndef STDERR_FILENO
#define STDERR_FILENO 2
#endif

#ifdef _MSC_VER
#define strncasecmp _strnicmp
#define strcasecmp  _stricmp
#endif

typedef struct TJSTimer TJSTimer;

struct TJSRuntime {
    TJSRunOptions options;
    int exit_code;
    JSRuntime *rt;
    JSContext *ctx;
    uv_loop_t loop;
    struct {
        uv_check_t check;
        uv_idle_t idle;
        uv_prepare_t prepare;
    } jobs;
    uv_async_t stop;
    bool is_worker;
    bool freeing;
#ifndef TJS_DISABLE_WASM
    struct {
        bool initialized;
    } wasm_ctx;
#endif
    struct {
        struct lws_context *ctx;
        char *cookie_jar_path;
        char *client_ca_pem;
        unsigned int client_ca_pem_len;
        uint8_t *client_cert_der;
        unsigned int client_cert_der_len;
        uint8_t *client_key_der;
        unsigned int client_key_der_len;
        int last_tls_error_code;
        char *last_tls_error_message;
        uv_async_t keepalive;
        int active_conns;
    } lws;
    struct {
        TJSTimer *timers;
        int64_t next_timer;
    } timers;
    struct {
        JSValue promise_event_ctor;
        JSValue dispatch_event_func;
    } builtins;
    struct {
        struct list_head pending;
    } promise_rejections;
};

void tjs__mod_dns_init(JSContext *ctx, JSValue ns);
void tjs__mod_engine_init(JSContext *ctx, JSValue ns);
void tjs__mod_error_init(JSContext *ctx, JSValue ns);
void tjs__mod_events_init(JSContext *ctx, JSValue ns);
void tjs__mod_buffer_init(JSContext *ctx, JSValue ns);
void tjs__mod_crypto_init(JSContext *ctx, JSValue ns);
int tjs__buffer_decode_base64(JSContext *ctx, const uint8_t *in, size_t in_len, uint8_t **out, size_t *out_len);
void tjs__mod_ffi_init(JSContext *ctx, JSValue ns);
void tjs__mod_fs_init(JSContext *ctx, JSValue ns);
void tjs__mod_fswatch_init(JSContext *ctx, JSValue ns);
void tjs__mod_httpclient_init(JSContext *ctx, JSValue ns);
#ifdef TJS__HAS_HTTP2
void tjs__mod_http2_init(JSContext *ctx, JSValue ns);
#endif
void tjs__mod_mqtt_init(JSContext *ctx, JSValue ns);
void tjs__mod_os_init(JSContext *ctx, JSValue ns);
void tjs__mod_process_init(JSContext *ctx, JSValue ns);
void tjs__mod_signals_init(JSContext *ctx, JSValue ns);
#ifndef TJS_DISABLE_SQLITE3
void tjs__mod_sqlite3_init(JSContext *ctx, JSValue ns);
#endif
void tjs__mod_streams_init(JSContext *ctx, JSValue ns);
void tjs__mod_tls_init(JSContext *ctx, JSValue ns);
void tjs__mod_sys_init(JSContext *ctx, JSValue ns);
void tjs__mod_text_coding_init(JSContext *ctx, JSValue ns);
void tjs__mod_timers_init(JSContext *ctx, JSValue ns);
void tjs__mod_udp_init(JSContext *ctx, JSValue ns);
void tjs__mod_node_url_init(JSContext *ctx, JSValue ns);
void tjs__mod_util_init(JSContext *ctx, JSValue ns);
#ifndef TJS_DISABLE_WASM
void tjs__mod_wasm_init(JSContext *ctx, JSValue ns);
#endif
void tjs__mod_worker_init(JSContext *ctx, JSValue ns);
void tjs__mod_ws_init(JSContext *ctx, JSValue ns);
void tjs__mod_httpserver_init(JSContext *ctx, JSValue ns);
void tjs__mod_url_init(JSContext *ctx, JSValue ns);
#ifndef _WIN32
void tjs__mod_posix_socket_init(JSContext *ctx, JSValue ns);
#endif
#ifdef TJS_HAS_LVGL
void tjs__mod_lvgl_init(JSContext *ctx, JSValue ns);
#endif

JSValue tjs_new_error(JSContext *ctx, int err);
JSValue tjs_throw_errno(JSContext *ctx, int err);

JSValue tjs_new_pipe(JSContext *ctx);
uv_stream_t *tjs_pipe_get_stream(JSContext *ctx, JSValue obj);

void tjs__execute_jobs(JSContext *ctx);
bool tjs__handle_uncaught_exception(JSContext *ctx, JSValue exception_val);
const char *tjs__canonical_builtin_specifier(const char *name);
JSModuleDef *tjs__load_builtin(JSContext *ctx, const char *name);
JSValue tjs__load_builtin_namespace(JSContext *ctx, const char *name);
int tjs__load_file(JSContext *ctx, DynBuf *dbuf, const char *filename);
int tjs_module_attr_checker(JSContext *ctx, void *opaque, JSValueConst attributes);
JSModuleDef *tjs_module_loader(JSContext *ctx, const char *module_name, void *opaque, JSValueConst attributes);
char *tjs_module_normalizer(JSContext *ctx, const char *base_name, const char *name, void *opaque);
void tjs__init_require(JSContext *ctx);

int js_module_set_import_meta(JSContext *ctx, JSValue func_val, bool use_realpath, bool is_main);

JSValue tjs__get_args(JSContext *ctx);

int tjs__eval_bytecode(JSContext *ctx, const uint8_t *buf, size_t buf_len, bool check_promise);
#ifndef TJS_DISABLE_WASM
int tjs__ensure_wasm_runtime_initialized(JSContext *ctx);
#endif

void tjs__destroy_timers(TJSRuntime *qrt);

void tjs__sab_free(void *opaque, void *ptr);
void tjs__sab_dup(void *opaque, void *ptr);

struct lws_context *tjs__lws_get_context(JSContext *ctx);
void tjs__lws_init(TJSRuntime *qrt);
void tjs__lws_conn_ref(JSContext *ctx);
void tjs__lws_conn_unref(JSContext *ctx);
int tjs__lws_load_http(TJSRuntime *qrt, DynBuf *dbuf, const char *url);
int tjs__lws_set_client_ca(JSContext *ctx, const uint8_t *pem, size_t len);
int tjs__lws_set_client_cert(JSContext *ctx, const uint8_t *cert, size_t cert_len, const uint8_t *key, size_t key_len);
struct lws_vhost *tjs__lws_create_client_tls_vhost(JSContext *ctx,
                                                   const struct lws_protocols *protocols,
                                                   const uint8_t *ca_pem,
                                                   size_t ca_len,
                                                   const uint8_t *cert_pem,
                                                   size_t cert_len,
                                                   const uint8_t *key_pem,
                                                   size_t key_len);
const char *tjs__lws_last_tls_error(JSContext *ctx);
int tjs__lws_last_tls_error_code(JSContext *ctx);

extern const struct lws_protocols tjs_mqtt_protocol;

uv_loop_t *TJS_GetLoop(TJSRuntime *qrt);
TJSRuntime *TJS_NewRuntimeWorker(void);
TJSRuntime *TJS_NewRuntimeInternal(bool is_worker, TJSRunOptions *options);
JSValue TJS_CompileModule(JSContext *ctx, const char *specifier, const char *content, size_t len);
JSValue TJS_EvalScript(JSContext *ctx, const char *filename);
JSValue TJS_EvalModule(JSContext *ctx, const char *filename, bool is_main);
JSValue TJS_EvalModuleContent(JSContext *ctx,
                              const char *filename,
                              bool is_main,
                              bool use_realpath,
                              const char *content,
                              size_t len);

#endif
