#include "mem.h"
#include "private.h"
#include "utils.h"

#include "cacert.h"

#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/error.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>

#ifndef _WIN32
#include <netdb.h>
#else
#include <winsock2.h>
#include <ws2tcpip.h>
#endif
#include <stdbool.h>
#include <string.h>

enum {
    TLS_CB_READ = 0,
    TLS_CB_WRITE,
    TLS_CB_CONNECT,
    TLS_CB_MAX,
};

typedef struct {
    JSContext *ctx;
    int closed;
    int finalized;
    int poll_inited;
    int poll_closed;
    int connecting;
    int connected;
    int read_active;
    int resources_freed;
    int reject_unauthorized;
    uint8_t *write_buf;
    size_t write_len;
    size_t write_off;
    int write_pending;
    uv_poll_t poll;
    JSValue callbacks[TLS_CB_MAX];
    char *servername;
    mbedtls_net_context net;
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config conf;
    mbedtls_ctr_drbg_context ctr_drbg;
    mbedtls_entropy_context entropy;
    mbedtls_x509_crt cacert;
} TJSTls;

static JSClassID tjs_tls_class_id;
static void uv__tls_poll_cb(uv_poll_t *handle, int status, int events);

static JSValue tjs_tls_new_error(JSContext *ctx, const char *where, int code) {
    char errbuf[128];
    mbedtls_strerror(code, errbuf, sizeof(errbuf));

    char msg[256];
    snprintf(msg, sizeof(msg), "%s: %s (%d)", where, errbuf, code);

    JSValue err = JS_NewError(ctx);
    JS_DefinePropertyValueStr(ctx, err, "message", JS_NewString(ctx, msg), JS_PROP_C_W_E);
    JS_DefinePropertyValueStr(ctx, err, "code", JS_NewInt32(ctx, code), JS_PROP_C_W_E);
    return err;
}

static void maybe_invoke_callback(TJSTls *t, int callback, int argc, JSValue *argv) {
    JSContext *ctx = t->ctx;
    JSValue func = t->callbacks[callback];
    if (!JS_IsFunction(ctx, func)) {
        for (int i = 0; i < argc; i++) {
            JS_FreeValue(ctx, argv[i]);
        }
        return;
    }

    tjs_call_handler(ctx, func, argc, argv);

    for (int i = 0; i < argc; i++) {
        JS_FreeValue(ctx, argv[i]);
    }
}

static void tjs_tls_free_resources(TJSTls *t) {
    if (t->resources_freed) {
        return;
    }
    t->resources_freed = 1;

    js_free(t->ctx, t->write_buf);
    t->write_buf = NULL;
    t->write_len = 0;
    t->write_off = 0;
    t->write_pending = 0;

    if (t->connected) {
        mbedtls_ssl_close_notify(&t->ssl);
        t->connected = 0;
    }
    mbedtls_net_free(&t->net);
    mbedtls_x509_crt_free(&t->cacert);
    mbedtls_ssl_free(&t->ssl);
    mbedtls_ssl_config_free(&t->conf);
    mbedtls_ctr_drbg_free(&t->ctr_drbg);
    mbedtls_entropy_free(&t->entropy);
}

static void uv__tls_close_cb(uv_handle_t *handle) {
    TJSTls *t = handle->data;
    CHECK_NOT_NULL(t);
    t->poll_closed = 1;
    if (t->finalized) {
        tjs__free(t);
    }
}

static void tjs_tls_mark_closed(TJSTls *t) {
    if (t->closed) {
        return;
    }
    t->closed = 1;
    t->connecting = 0;
    t->read_active = 0;

    tjs_tls_free_resources(t);

    if (t->poll_inited) {
        uv_poll_stop(&t->poll);
        if (!uv_is_closing((uv_handle_t *) &t->poll)) {
            uv_close((uv_handle_t *) &t->poll, uv__tls_close_cb);
        }
    } else {
        t->poll_closed = 1;
    }
}

static int tjs_tls_update_poll(TJSTls *t) {
    if (t->closed || t->net.fd < 0) {
        return 0;
    }

    int events = 0;
    if (t->connecting) {
        events |= UV_READABLE | UV_WRITABLE | UV_DISCONNECT;
    }
    if (t->read_active) {
        events |= UV_READABLE | UV_DISCONNECT;
    }
    if (t->write_pending) {
        /* TLS writes can require reads during renegotiation / alerts. */
        events |= UV_READABLE | UV_WRITABLE | UV_DISCONNECT;
    }

    if (!events) {
        if (t->poll_inited && !uv_is_closing((uv_handle_t *) &t->poll)) {
            uv_poll_stop(&t->poll);
        }
        return 0;
    }

    if (!t->poll_inited) {
        int r = uv_poll_init_socket(tjs_get_loop(t->ctx), &t->poll, t->net.fd);
        if (r != 0) {
            return r;
        }
        t->poll_inited = 1;
        t->poll_closed = 0;
        t->poll.data = t;
    }

    return uv_poll_start(&t->poll, events, uv__tls_poll_cb);
}

static void tjs_tls_finish_connect(TJSTls *t, JSValue arg) {
    t->connecting = 0;
    tjs_tls_update_poll(t);
    maybe_invoke_callback(t, TLS_CB_CONNECT, 1, &arg);
}

static void tjs_tls_continue_handshake(TJSTls *t) {
    if (!t->connecting || t->closed) {
        return;
    }

    int r = mbedtls_ssl_handshake(&t->ssl);
    if (r == 0) {
        t->connected = 1;
        JSValue arg = JS_UNDEFINED;
        tjs_tls_finish_connect(t, arg);
        return;
    }

    if (r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_WANT_WRITE) {
        tjs_tls_update_poll(t);
        return;
    }

    JSValue arg = tjs_tls_new_error(t->ctx, "tls.handshake", r);
    tjs_tls_finish_connect(t, arg);
}

static int tjs_tls_flush_write(TJSTls *t, int *error_code) {
    *error_code = 0;

    while (t->write_off < t->write_len) {
        int r = mbedtls_ssl_write(&t->ssl, t->write_buf + t->write_off, t->write_len - t->write_off);
        if (r > 0) {
            t->write_off += (size_t) r;
            continue;
        }

        if (r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_WANT_WRITE) {
            return 0;
        }

        *error_code = r;
        return -1;
    }

    js_free(t->ctx, t->write_buf);
    t->write_buf = NULL;
    t->write_len = 0;
    t->write_off = 0;
    t->write_pending = 0;
    return 1;
}

static void tjs_tls_continue_write(TJSTls *t) {
    if (!t->write_pending || t->closed) {
        return;
    }

    int error_code = 0;
    int r = tjs_tls_flush_write(t, &error_code);
    if (r == 0) {
        tjs_tls_update_poll(t);
        return;
    }

    JSValue arg = r > 0 ? JS_UNDEFINED : tjs_tls_new_error(t->ctx, "tls.write", error_code);
    if (r < 0) {
        js_free(t->ctx, t->write_buf);
        t->write_buf = NULL;
        t->write_len = 0;
        t->write_off = 0;
        t->write_pending = 0;
    }
    tjs_tls_update_poll(t);
    maybe_invoke_callback(t, TLS_CB_WRITE, 1, &arg);
}

static void uv__tls_poll_cb(uv_poll_t *handle, int status, int events) {
    TJSTls *t = handle->data;
    CHECK_NOT_NULL(t);

    if (t->closed) {
        return;
    }

    JSContext *ctx = t->ctx;

    if (status < 0) {
        if (t->connecting) {
            JSValue arg = tjs_new_error(ctx, status);
            tjs_tls_finish_connect(t, arg);
            return;
        }
        if (t->write_pending) {
            JSValue arg = tjs_new_error(ctx, status);
            js_free(ctx, t->write_buf);
            t->write_buf = NULL;
            t->write_len = 0;
            t->write_off = 0;
            t->write_pending = 0;
            tjs_tls_update_poll(t);
            maybe_invoke_callback(t, TLS_CB_WRITE, 1, &arg);
            return;
        }
        if (t->read_active) {
            t->read_active = 0;
            JSValue args[2] = { JS_UNDEFINED, tjs_new_error(ctx, status) };
            tjs_tls_update_poll(t);
            maybe_invoke_callback(t, TLS_CB_READ, 2, args);
        }
        return;
    }

    if (t->connecting) {
        tjs_tls_continue_handshake(t);
        if (t->closed || !t->connected) {
            return;
        }
    }

    if (t->write_pending && (events & (UV_READABLE | UV_WRITABLE | UV_DISCONNECT))) {
        tjs_tls_continue_write(t);
        if (t->closed) {
            return;
        }
    }

    if (!t->read_active || !(events & (UV_READABLE | UV_DISCONNECT))) {
        tjs_tls_update_poll(t);
        return;
    }

    for (;;) {
        uint8_t *buf = js_malloc(ctx, 16384);
        if (!buf) {
            JSValue args[2] = { JS_UNDEFINED, JS_ThrowOutOfMemory(ctx) };
            maybe_invoke_callback(t, TLS_CB_READ, 2, args);
            return;
        }

        int r = mbedtls_ssl_read(&t->ssl, buf, 16384);
        if (r > 0) {
            JSValue args[2] = { TJS_NewUint8Array(ctx, buf, r), JS_UNDEFINED };
            maybe_invoke_callback(t, TLS_CB_READ, 2, args);
            if (t->closed || !t->read_active) {
                return;
            }
            continue;
        }

        js_free(ctx, buf);

        if (r == 0 || r == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) {
            t->read_active = 0;
            JSValue args[2] = { JS_NULL, JS_UNDEFINED };
            tjs_tls_update_poll(t);
            maybe_invoke_callback(t, TLS_CB_READ, 2, args);
            return;
        }

        if (r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_WANT_WRITE) {
            tjs_tls_update_poll(t);
            return;
        }

        t->read_active = 0;
        JSValue args[2] = { JS_UNDEFINED, tjs_tls_new_error(ctx, "tls.read", r) };
        tjs_tls_update_poll(t);
        maybe_invoke_callback(t, TLS_CB_READ, 2, args);
        return;
    }
}

static JSValue tjs_tls_callback_get(JSContext *ctx, JSValue this_val, int magic) {
    TJSTls *t = JS_GetOpaque2(ctx, this_val, tjs_tls_class_id);
    if (!t) {
        return JS_EXCEPTION;
    }
    return JS_DupValue(ctx, t->callbacks[magic]);
}

static JSValue tjs_tls_callback_set(JSContext *ctx, JSValue this_val, JSValue value, int magic) {
    TJSTls *t = JS_GetOpaque2(ctx, this_val, tjs_tls_class_id);
    if (!t) {
        return JS_EXCEPTION;
    }
    if (JS_IsFunction(ctx, value) || JS_IsUndefined(value) || JS_IsNull(value)) {
        JS_FreeValue(ctx, t->callbacks[magic]);
        t->callbacks[magic] = JS_DupValue(ctx, value);
    }
    return JS_UNDEFINED;
}

static JSValue tjs_tls_constructor(JSContext *ctx, JSValue new_target, int argc, JSValue *argv) {
    JSValue obj = JS_NewObjectClass(ctx, tjs_tls_class_id);
    if (JS_IsException(obj)) {
        return obj;
    }

    TJSTls *t = tjs__mallocz(sizeof(*t));
    if (!t) {
        JS_FreeValue(ctx, obj);
        return JS_ThrowOutOfMemory(ctx);
    }

    t->ctx = ctx;
    t->reject_unauthorized = 1;
    t->poll_closed = 1;

    for (int i = 0; i < TLS_CB_MAX; i++) {
        t->callbacks[i] = JS_UNDEFINED;
    }

    mbedtls_net_init(&t->net);
    mbedtls_ssl_init(&t->ssl);
    mbedtls_ssl_config_init(&t->conf);
    mbedtls_ctr_drbg_init(&t->ctr_drbg);
    mbedtls_entropy_init(&t->entropy);
    mbedtls_x509_crt_init(&t->cacert);

    const unsigned char *pers = (const unsigned char *) "tjs_tls";
    int r = mbedtls_ctr_drbg_seed(&t->ctr_drbg, mbedtls_entropy_func, &t->entropy, pers, strlen((const char *) pers));
    if (r != 0) {
        JS_FreeValue(ctx, obj);
        tjs_tls_free_resources(t);
        tjs__free(t);
        return JS_ThrowInternalError(ctx, "mbedtls_ctr_drbg_seed failed: %d", r);
    }

    r = mbedtls_ssl_config_defaults(&t->conf, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT);
    if (r != 0) {
        JS_FreeValue(ctx, obj);
        tjs_tls_free_resources(t);
        tjs__free(t);
        return JS_ThrowInternalError(ctx, "mbedtls_ssl_config_defaults failed: %d", r);
    }

    mbedtls_ssl_conf_rng(&t->conf, mbedtls_ctr_drbg_random, &t->ctr_drbg);
    mbedtls_ssl_conf_authmode(&t->conf, MBEDTLS_SSL_VERIFY_REQUIRED);

    r = mbedtls_x509_crt_parse(&t->cacert, (const unsigned char *) tjs_cacert_pem, TJS_CACERT_PEM_LEN + 1);
    if (r == 0) {
        mbedtls_ssl_conf_ca_chain(&t->conf, &t->cacert, NULL);
    }

    r = mbedtls_ssl_setup(&t->ssl, &t->conf);
    if (r != 0) {
        JS_FreeValue(ctx, obj);
        tjs_tls_free_resources(t);
        tjs__free(t);
        return JS_ThrowInternalError(ctx, "mbedtls_ssl_setup failed: %d", r);
    }

    JS_SetOpaque(obj, t);
    return obj;
}

static JSValue tjs_tls_configure(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    TJSTls *t = JS_GetOpaque2(ctx, this_val, tjs_tls_class_id);
    if (!t) {
        return JS_EXCEPTION;
    }
    if (argc < 1 || !JS_IsObject(argv[0])) {
        return JS_UNDEFINED;
    }

    JSValue servername = JS_GetPropertyStr(ctx, argv[0], "servername");
    if (JS_IsString(servername)) {
        const char *s = JS_ToCString(ctx, servername);
        if (s) {
            js_free(ctx, t->servername);
            t->servername = js_strdup(ctx, s);
            JS_FreeCString(ctx, s);
        }
    }
    JS_FreeValue(ctx, servername);

    JSValue rejectUnauthorized = JS_GetPropertyStr(ctx, argv[0], "rejectUnauthorized");
    if (!JS_IsUndefined(rejectUnauthorized)) {
        int v = JS_ToBool(ctx, rejectUnauthorized);
        if (v >= 0) {
            t->reject_unauthorized = v;
            mbedtls_ssl_conf_authmode(&t->conf, v ? MBEDTLS_SSL_VERIFY_REQUIRED : MBEDTLS_SSL_VERIFY_OPTIONAL);
        }
    }
    JS_FreeValue(ctx, rejectUnauthorized);

    JSValue ca = JS_GetPropertyStr(ctx, argv[0], "ca");
    if (JS_IsString(ca)) {
        const char *pem = JS_ToCString(ctx, ca);
        if (pem) {
            mbedtls_x509_crt_free(&t->cacert);
            mbedtls_x509_crt_init(&t->cacert);
            int r = mbedtls_x509_crt_parse(&t->cacert, (const unsigned char *) pem, strlen(pem) + 1);
            if (r == 0) {
                mbedtls_ssl_conf_ca_chain(&t->conf, &t->cacert, NULL);
            }
            JS_FreeCString(ctx, pem);
        }
    }
    JS_FreeValue(ctx, ca);

    return JS_UNDEFINED;
}

static JSValue tjs_tls_connect(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    TJSTls *t = JS_GetOpaque2(ctx, this_val, tjs_tls_class_id);
    if (!t) {
        return JS_EXCEPTION;
    }
    if (t->closed) {
        return JS_ThrowInternalError(ctx, "tls socket is closed");
    }
    if (t->connected) {
        return JS_ThrowInternalError(ctx, "tls socket is already connected");
    }

    struct sockaddr_storage ss;
    int r = tjs_obj2addr(ctx, argv[0], &ss);
    if (r != 0) {
        return JS_EXCEPTION;
    }

    char host[NI_MAXHOST];
    char serv[NI_MAXSERV];
    r = getnameinfo((struct sockaddr *) &ss, sizeof(ss), host, sizeof(host), serv, sizeof(serv), NI_NUMERICHOST | NI_NUMERICSERV);
    if (r != 0) {
        JSValue arg = JS_ThrowInternalError(ctx, "getnameinfo failed");
        maybe_invoke_callback(t, TLS_CB_CONNECT, 1, &arg);
        return JS_UNDEFINED;
    }

    r = mbedtls_net_connect(&t->net, host, serv, MBEDTLS_NET_PROTO_TCP);
    if (r != 0) {
        JSValue arg = tjs_tls_new_error(ctx, "tls.connect", r);
        maybe_invoke_callback(t, TLS_CB_CONNECT, 1, &arg);
        return JS_UNDEFINED;
    }

    r = mbedtls_net_set_nonblock(&t->net);
    if (r != 0) {
        JSValue arg = tjs_tls_new_error(ctx, "tls.nonblock", r);
        maybe_invoke_callback(t, TLS_CB_CONNECT, 1, &arg);
        return JS_UNDEFINED;
    }

    if (t->servername) {
        r = mbedtls_ssl_set_hostname(&t->ssl, t->servername);
        if (r != 0) {
            JSValue arg = tjs_tls_new_error(ctx, "tls.set_hostname", r);
            maybe_invoke_callback(t, TLS_CB_CONNECT, 1, &arg);
            return JS_UNDEFINED;
        }
    }

    mbedtls_ssl_set_bio(&t->ssl, &t->net, mbedtls_net_send, mbedtls_net_recv, NULL);

    t->connecting = 1;
    tjs_tls_continue_handshake(t);
    return JS_UNDEFINED;
}

static JSValue tjs_tls_start_read(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    TJSTls *t = JS_GetOpaque2(ctx, this_val, tjs_tls_class_id);
    if (!t) {
        return JS_EXCEPTION;
    }
    if (t->closed) {
        return JS_ThrowInternalError(ctx, "tls socket is closed");
    }
    if (!t->connected) {
        return JS_ThrowInternalError(ctx, "tls socket is not connected");
    }

    t->read_active = 1;
    int rr = tjs_tls_update_poll(t);
    if (rr != 0) {
        return tjs_throw_errno(ctx, rr);
    }
    return JS_UNDEFINED;
}

static JSValue tjs_tls_stop_read(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    TJSTls *t = JS_GetOpaque2(ctx, this_val, tjs_tls_class_id);
    if (!t) {
        return JS_EXCEPTION;
    }
    t->read_active = 0;
    tjs_tls_update_poll(t);
    return JS_UNDEFINED;
}

static JSValue tjs_tls_write(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    TJSTls *t = JS_GetOpaque2(ctx, this_val, tjs_tls_class_id);
    if (!t) {
        return JS_EXCEPTION;
    }
    if (!t->connected || t->closed) {
        return JS_ThrowInternalError(ctx, "tls socket is not connected");
    }

    size_t size;
    uint8_t *buf = JS_GetUint8Array(ctx, &size, argv[0]);
    if (!buf) {
        return JS_EXCEPTION;
    }

    if (t->write_pending) {
        return JS_ThrowInternalError(ctx, "tls write already pending");
    }
    if (size == 0) {
        return JS_NewInt64(ctx, 0);
    }

    t->write_buf = js_malloc(ctx, size);
    if (!t->write_buf) {
        return JS_ThrowOutOfMemory(ctx);
    }
    memcpy(t->write_buf, buf, size);
    t->write_len = size;
    t->write_off = 0;
    t->write_pending = 1;

    int error_code = 0;
    int wr = tjs_tls_flush_write(t, &error_code);
    if (wr > 0) {
        tjs_tls_update_poll(t);
        return JS_NewInt64(ctx, size);
    }
    if (wr == 0) {
        int rr = tjs_tls_update_poll(t);
        if (rr != 0) {
            return tjs_throw_errno(ctx, rr);
        }
        return JS_UNDEFINED;
    }

    js_free(ctx, t->write_buf);
    t->write_buf = NULL;
    t->write_len = 0;
    t->write_off = 0;
    t->write_pending = 0;
    return JS_Throw(ctx, tjs_tls_new_error(ctx, "tls.write", error_code));
}

static JSValue tjs_tls_shutdown(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    TJSTls *t = JS_GetOpaque2(ctx, this_val, tjs_tls_class_id);
    if (!t) {
        return JS_EXCEPTION;
    }
    if (t->closed || !t->connected) {
        return JS_UNDEFINED;
    }
    int r = mbedtls_ssl_close_notify(&t->ssl);
    if (r < 0 && r != MBEDTLS_ERR_SSL_WANT_READ && r != MBEDTLS_ERR_SSL_WANT_WRITE) {
        return JS_ThrowInternalError(ctx, "tls.shutdown failed: %d", r);
    }
    return JS_UNDEFINED;
}

static JSValue tjs_tls_close(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    TJSTls *t = JS_GetOpaque2(ctx, this_val, tjs_tls_class_id);
    if (!t) {
        return JS_EXCEPTION;
    }
    tjs_tls_mark_closed(t);
    return JS_UNDEFINED;
}

static JSValue tjs_tls_fileno(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    TJSTls *t = JS_GetOpaque2(ctx, this_val, tjs_tls_class_id);
    if (!t) {
        return JS_EXCEPTION;
    }
    return JS_NewInt32(ctx, t->net.fd);
}

static JSValue tjs_tls_nodelay(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    TJSTls *t = JS_GetOpaque2(ctx, this_val, tjs_tls_class_id);
    if (!t) {
        return JS_EXCEPTION;
    }
    if (t->net.fd < 0) {
        return JS_UNDEFINED;
    }
    int enable;
    if ((enable = JS_ToBool(ctx, argv[0])) == -1) {
        return JS_EXCEPTION;
    }
#ifdef TCP_NODELAY
    setsockopt(t->net.fd, IPPROTO_TCP, TCP_NODELAY, &enable, sizeof(enable));
#else
    (void) enable;
#endif
    return JS_UNDEFINED;
}

static JSValue tjs_tls_keepalive(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    TJSTls *t = JS_GetOpaque2(ctx, this_val, tjs_tls_class_id);
    if (!t) {
        return JS_EXCEPTION;
    }
    if (t->net.fd < 0) {
        return JS_UNDEFINED;
    }
    int enable;
    if ((enable = JS_ToBool(ctx, argv[0])) == -1) {
        return JS_EXCEPTION;
    }
#ifdef SO_KEEPALIVE
    setsockopt(t->net.fd, SOL_SOCKET, SO_KEEPALIVE, &enable, sizeof(enable));
#else
    (void) enable;
#endif
    return JS_UNDEFINED;
}

static void tjs_tls_finalizer(JSRuntime *rt, JSValue val) {
    TJSTls *t = JS_GetOpaque(val, tjs_tls_class_id);
    if (!t) {
        return;
    }

    for (int i = 0; i < TLS_CB_MAX; i++) {
        JS_FreeValueRT(rt, t->callbacks[i]);
    }

    js_free_rt(rt, t->servername);
    t->servername = NULL;

    t->finalized = 1;
    tjs_tls_mark_closed(t);
    /* NOTE: keep object memory until process teardown; there is still a
       lifecycle race in current TLS MVP around finalization order. */
}

static void tjs_tls_mark(JSRuntime *rt, JSValue val, JS_MarkFunc *mark_func) {
    TJSTls *t = JS_GetOpaque(val, tjs_tls_class_id);
    if (!t) {
        return;
    }
    for (int i = 0; i < TLS_CB_MAX; i++) {
        JS_MarkValue(rt, t->callbacks[i], mark_func);
    }
}

static JSClassDef tjs_tls_class = {
    "TLS",
    .finalizer = tjs_tls_finalizer,
    .gc_mark = tjs_tls_mark,
};

static const JSCFunctionListEntry tjs_tls_proto_funcs[] = {
    JS_CGETSET_MAGIC_DEF("onread", tjs_tls_callback_get, tjs_tls_callback_set, TLS_CB_READ),
    JS_CGETSET_MAGIC_DEF("onwrite", tjs_tls_callback_get, tjs_tls_callback_set, TLS_CB_WRITE),
    JS_CGETSET_MAGIC_DEF("onconnect", tjs_tls_callback_get, tjs_tls_callback_set, TLS_CB_CONNECT),
    TJS_CFUNC_DEF("configure", 1, tjs_tls_configure),
    TJS_CFUNC_DEF("connect", 1, tjs_tls_connect),
    TJS_CFUNC_DEF("startRead", 0, tjs_tls_start_read),
    TJS_CFUNC_DEF("stopRead", 0, tjs_tls_stop_read),
    TJS_CFUNC_DEF("write", 1, tjs_tls_write),
    TJS_CFUNC_DEF("shutdown", 0, tjs_tls_shutdown),
    TJS_CFUNC_DEF("setNoDelay", 1, tjs_tls_nodelay),
    TJS_CFUNC_DEF("setKeepAlive", 2, tjs_tls_keepalive),
    TJS_CFUNC_DEF("close", 0, tjs_tls_close),
    TJS_CFUNC_DEF("fileno", 0, tjs_tls_fileno),
};

void tjs__mod_tls_init(JSContext *ctx, JSValue ns) {
    JSRuntime *rt = JS_GetRuntime(ctx);

    JS_NewClassID(rt, &tjs_tls_class_id);
    JS_NewClass(rt, tjs_tls_class_id, &tjs_tls_class);

    JSValue proto = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, proto, tjs_tls_proto_funcs, countof(tjs_tls_proto_funcs));
    JS_SetClassProto(ctx, tjs_tls_class_id, proto);

    JSValue obj = JS_NewCFunction2(ctx, tjs_tls_constructor, "TLS", 1, JS_CFUNC_constructor, 0);
    JS_DefinePropertyValueStr(ctx, ns, "TLS", obj, JS_PROP_C_W_E);
}
