/*
 * txiki.js
 *
 * Copyright (c) 2026-present Saúl Ibarra Corretgé <s@saghul.net>
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

#include "cacert.h"
#include "mem.h"
#include "private.h"
#include "version.h"

#include <mbedtls/error.h>
#include <mbedtls/pk.h>
#include <mbedtls/x509_crt.h>
#include <stdio.h>
#include <string.h>

/* Portable memmem implementation for Windows */
#if defined(_WIN32) && !defined(__GLIBC__)
static void *tjs__memmem(const void *haystack, size_t haystacklen,
                         const void *needle, size_t needlelen) {
    const char *h = (const char *) haystack;
    const char *n = (const char *) needle;

    if (needlelen == 0)
        return (void *) haystack;
    if (needlelen > haystacklen)
        return NULL;

    for (size_t i = 0; i <= haystacklen - needlelen; i++) {
        if (memcmp(h + i, n, needlelen) == 0)
            return (void *) (h + i);
    }
    return NULL;
}
#define memmem(h, hl, n, nl) tjs__memmem((h), (hl), (n), (nl))
#endif

#define TJS__UA_STRING                                                                                                 \
    "xuan/" STRINGIFY(TJS_VERSION_MAJOR) "." STRINGIFY(TJS_VERSION_MINOR) "." STRINGIFY(TJS_VERSION_PATCH)         \
        TJS_VERSION_SUFFIX

extern const struct lws_protocols tjs_ws_protocol;
extern const struct lws_protocols tjs_http_protocol;
extern const struct lws_protocols tjs_mqtt_protocol;
extern const struct lws_protocols tjs_mqtt_ws_protocol;
#ifdef TJS__HAS_HTTP2
extern const struct lws_protocols tjs_h2session_protocol;
extern const struct lws_protocols tjs_h2stream_protocol;
#endif

#define TJS_LWS_HTTP_LOAD_PROTOCOL_NAME "tjs-http-load"

typedef struct {
    DynBuf *dbuf;
    int status;
    bool done;
    char redirect_url[2048];
} TJSHttpLoadCtx;

static void tjs__lws_clear_tls_error(TJSRuntime *qrt, JSContext *ctx) {
    qrt->lws.last_tls_error_code = 0;
    js_free(ctx, qrt->lws.last_tls_error_message);
    qrt->lws.last_tls_error_message = NULL;
}

static int tjs__lws_set_tls_error(JSContext *ctx, int code, const char *where) {
    TJSRuntime *qrt = TJS_GetRuntime(ctx);
    char errbuf[128];
    char msg[256];

    CHECK_NOT_NULL(qrt);

    if (code > 0) {
        snprintf(msg, sizeof(msg), "%s: %d certificate(s) failed to parse", where, code);
    } else {
        mbedtls_strerror(code, errbuf, sizeof(errbuf));
        snprintf(msg, sizeof(msg), "%s: %s (%d / -0x%x)", where, errbuf, code, (unsigned int) -code);
    }

    js_free(ctx, qrt->lws.last_tls_error_message);
    qrt->lws.last_tls_error_message = js_strdup(ctx, msg);
    if (!qrt->lws.last_tls_error_message) {
        qrt->lws.last_tls_error_code = code;
        return -1;
    }

    qrt->lws.last_tls_error_code = code;
    return 0;
}

static int tjs__lws_validate_client_ca(JSContext *ctx, const char *pem, size_t len) {
    mbedtls_x509_crt cert;
    int ret;

    mbedtls_x509_crt_init(&cert);
    ret = mbedtls_x509_crt_parse(&cert, (const unsigned char *) pem, len + 1);
    mbedtls_x509_crt_free(&cert);
    if (ret != 0) {
        tjs__lws_set_tls_error(ctx, ret, "MQTT CA parse failed");
        return -1;
    }

    return 0;
}

/* DER-path validator used by non-mbedtls backends. */
#if !defined(LWS_WITH_MBEDTLS)
static int tjs__lws_validate_client_cert(JSContext *ctx, const uint8_t *cert_der, size_t cert_der_len) {
    mbedtls_x509_crt cert;
    int ret;

    mbedtls_x509_crt_init(&cert);
    ret = mbedtls_x509_crt_parse_der(&cert, cert_der, cert_der_len);
    mbedtls_x509_crt_free(&cert);
    if (ret != 0) {
        tjs__lws_set_tls_error(ctx, ret, "MQTT client certificate parse failed");
        return -1;
    }

    return 0;
}
#endif

static int tjs__lws_validate_client_cert_pem(JSContext *ctx, const uint8_t *cert_pem, size_t cert_pem_len) {
    mbedtls_x509_crt cert;
    int ret;

    mbedtls_x509_crt_init(&cert);
    ret = mbedtls_x509_crt_parse(&cert, cert_pem, cert_pem_len + 1);
    mbedtls_x509_crt_free(&cert);
    if (ret != 0) {
        tjs__lws_set_tls_error(ctx, ret, "MQTT client certificate parse failed");
        return -1;
    }

    return 0;
}

static int tjs__lws_validate_client_key_pem(JSContext *ctx, const uint8_t *key_pem, size_t key_pem_len) {
    mbedtls_pk_context pk;
    int ret;

    mbedtls_pk_init(&pk);
#if defined(MBEDTLS_VERSION_NUMBER) && MBEDTLS_VERSION_NUMBER >= 0x03000000
#if defined(MBEDTLS_VERSION_NUMBER) && MBEDTLS_VERSION_NUMBER >= 0x03050000
    ret = mbedtls_pk_parse_key(&pk, key_pem, key_pem_len + 1, NULL, 0, NULL, NULL);
#else
    ret = mbedtls_pk_parse_key(&pk, key_pem, key_pem_len + 1, NULL, 0, NULL, NULL);
#endif
#else
    ret = mbedtls_pk_parse_key(&pk, key_pem, key_pem_len + 1, NULL, 0);
#endif
    mbedtls_pk_free(&pk);
    if (ret != 0) {
        tjs__lws_set_tls_error(ctx, ret, "MQTT client key parse failed");
        return -1;
    }

    return 0;
}

static int tjs_lws_http_load_callback(struct lws *wsi,
                                      enum lws_callback_reasons reason,
                                      void *user,
                                      void *in,
                                      size_t len) {
    TJSHttpLoadCtx *ctx = (TJSHttpLoadCtx *) user;

    if (!ctx) {
        return 0;
    }

    switch (reason) {
        case LWS_CALLBACK_ESTABLISHED_CLIENT_HTTP:
            ctx->status = (int) lws_http_client_http_response(wsi);
            break;

        case LWS_CALLBACK_RECEIVE_CLIENT_HTTP: {
            char buffer[4096 + LWS_PRE];
            char *px = buffer + LWS_PRE;
            int lenx = sizeof(buffer) - LWS_PRE;

            if (lws_http_client_read(wsi, &px, &lenx) < 0) {
                return -1;
            }
            break;
        }

        case LWS_CALLBACK_RECEIVE_CLIENT_HTTP_READ:
            if (dbuf_put(ctx->dbuf, (const uint8_t *) in, len)) {
                return -1;
            }
            break;

        case LWS_CALLBACK_COMPLETED_CLIENT_HTTP:
            ctx->done = true;
            break;

        case LWS_CALLBACK_CLOSED_CLIENT_HTTP:
            if (!ctx->done) {
                ctx->status = -1;
                ctx->done = true;
            }
            break;

        case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
            ctx->status = -1;
            ctx->done = true;
            break;

        case LWS_CALLBACK_TIMER:
            ctx->status = -2;
            ctx->done = true;
            return -1;

        case LWS_CALLBACK_CLIENT_HTTP_REDIRECT:
            /* Capture the redirect target so we can retry on cross-host
             * failures.  Return 0 to let lws try the built-in redirect. */
            if (in) {
                lws_strncpy(ctx->redirect_url, (const char *) in, sizeof(ctx->redirect_url));
            }
            return 0;

        case LWS_CALLBACK_CLIENT_APPEND_HANDSHAKE_HEADER: {
            unsigned char **p = (unsigned char **) in, *end = (*p) + len;

            if (lws_add_http_header_by_name(wsi,
                                            (const unsigned char *) "user-agent:",
                                            (const unsigned char *) TJS__UA_STRING,
                                            (int) strlen(TJS__UA_STRING),
                                            p,
                                            end)) {
                return -1;
            }
            break;
        }

        default:
            break;
    }

    return 0;
}

static const struct lws_protocols tjs_http_load_protocol = {
    .name = TJS_LWS_HTTP_LOAD_PROTOCOL_NAME,
    .callback = tjs_lws_http_load_callback,
    .per_session_data_size = 0,
    .rx_buffer_size = 0,
};

static void tjs__lws_keepalive_cb(uv_async_t *handle) {
    /* The handle only exists to keep the foreign libuv loop alive while
     * lws client work is in flight.  Driving lws from here would recurse
     * into uv_run() because libwebsockets already uses the same loop. */
    (void) handle;
}

static void *tjs__lws_realloc(void *ptr, size_t size, const char *reason) {
    (void) reason;
    if (size == 0) {
        tjs__free(ptr);
        return NULL;
    }
    if (ptr == NULL) {
        return tjs__malloc(size);
    }
    return tjs__realloc(ptr, size);
}

static const char *tjs__path_last_separator(const char *path) {
    const char *last = strrchr(path, '/');
#ifdef _WIN32
    const char *windows_last = strrchr(path, '\\');
    if (!last || (windows_last && windows_last > last)) {
        last = windows_last;
    }
#endif
    return last;
}

static bool tjs__lws_cookie_jar_path_writable(TJSRuntime *qrt) {
    static const char probe_template_name[] = ".xuan-cookiejar-probe-XXXXXX";
    const char *cookie_path = qrt->lws.cookie_jar_path;
    const char *separator;
    const char *parent;
    char separator_char;
    char *probe_template;
    size_t parent_len;
    size_t probe_len;
    bool is_root_parent;
    bool writable = false;
    int mkstemp_result;
    uv_fs_t mkstemp_req;

    if (!cookie_path || !cookie_path[0]) {
        return false;
    }

    separator = tjs__path_last_separator(cookie_path);
    if (!separator) {
        parent = ".";
        parent_len = 1;
        separator_char = '/';
    } else {
        parent = cookie_path;
        parent_len = separator == cookie_path ? 1 : (size_t) (separator - cookie_path);
        separator_char = *separator;
    }

    is_root_parent = parent_len == 1 && (parent[0] == '/' || parent[0] == '\\');
    probe_len = parent_len + (is_root_parent ? 0 : 1) + sizeof(probe_template_name);
    probe_template = js_malloc(qrt->ctx, probe_len);
    if (!probe_template) {
        return false;
    }

    if (is_root_parent) {
        snprintf(probe_template, probe_len, "%.*s%s", (int) parent_len, parent, probe_template_name);
    } else {
        snprintf(probe_template, probe_len, "%.*s%c%s", (int) parent_len, parent, separator_char, probe_template_name);
    }

    memset(&mkstemp_req, 0, sizeof(mkstemp_req));
    mkstemp_result = uv_fs_mkstemp(NULL, &mkstemp_req, probe_template, NULL);
    if (mkstemp_result >= 0) {
        uv_fs_t close_req;
        uv_fs_t unlink_req;
        int fd = mkstemp_result;

        memset(&close_req, 0, sizeof(close_req));
        (void) uv_fs_close(NULL, &close_req, fd, NULL);
        uv_fs_req_cleanup(&close_req);

        memset(&unlink_req, 0, sizeof(unlink_req));
        (void) uv_fs_unlink(NULL, &unlink_req, mkstemp_req.path, NULL);
        uv_fs_req_cleanup(&unlink_req);

        writable = true;
    }

    uv_fs_req_cleanup(&mkstemp_req);
    js_free(qrt->ctx, probe_template);
    return writable;
}

void tjs__lws_init(TJSRuntime *qrt) {
    const struct lws_protocols protocols[] = {
        tjs_http_protocol,
        tjs_mqtt_protocol,
        tjs_mqtt_ws_protocol,
        tjs_ws_protocol,
#ifdef TJS__HAS_HTTP2
        tjs_h2session_protocol,
        tjs_h2stream_protocol,
#endif
        LWS_PROTOCOL_LIST_TERM,
    };

    struct lws_context_creation_info info;
    memset(&info, 0, sizeof(info));

    info.port = CONTEXT_PORT_NO_LISTEN;
    info.protocols = protocols;
    info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT | LWS_SERVER_OPTION_LIBUV;
    info.count_threads = 1;

    /* Use the existing libuv event loop. */
    void *foreign_loops[1] = { &qrt->loop };
    info.foreign_loops = foreign_loops;

    /* Embedded CA certificates for TLS, optionally extended with runtime CA PEM. */
    info.client_ssl_ca_mem = qrt->lws.client_ca_pem ? qrt->lws.client_ca_pem : tjs_cacert_pem;
    info.client_ssl_ca_mem_len = qrt->lws.client_ca_pem ? qrt->lws.client_ca_pem_len : TJS_CACERT_PEM_LEN;

    /* Client certificate and key for mTLS, if configured. */
    if (qrt->lws.client_cert_der && qrt->lws.client_key_der) {
        info.client_ssl_cert_mem = qrt->lws.client_cert_der;
        info.client_ssl_cert_mem_len = qrt->lws.client_cert_der_len;
        info.client_ssl_key_mem = qrt->lws.client_key_der;
        info.client_ssl_key_mem_len = qrt->lws.client_key_der_len;
    }

    if (tjs__lws_cookie_jar_path_writable(qrt)) {
        info.http_nsc_filepath = qrt->lws.cookie_jar_path;
    }

    lws_set_log_level(0, NULL);
    lws_set_allocator(tjs__lws_realloc);

    qrt->lws.ctx = lws_create_context(&info);

    /* Keepalive handle: stays unrefed when idle so the loop can exit,
     * gets refed while client connections are in flight to prevent
     * the loop from exiting during async DNS resolution. */
    uv_async_init(&qrt->loop, &qrt->lws.keepalive, tjs__lws_keepalive_cb);
    qrt->lws.keepalive.data = qrt;
    uv_unref((uv_handle_t *) &qrt->lws.keepalive);
    qrt->lws.active_conns = 0;
}

void tjs__lws_conn_ref(JSContext *ctx) {
    TJSRuntime *qrt = TJS_GetRuntime(ctx);
    CHECK_NOT_NULL(qrt);
    if (qrt->lws.active_conns++ == 0) {
        uv_ref((uv_handle_t *) &qrt->lws.keepalive);
    }
}

void tjs__lws_conn_unref(JSContext *ctx) {
    TJSRuntime *qrt = TJS_GetRuntime(ctx);
    CHECK_NOT_NULL(qrt);
    if (--qrt->lws.active_conns == 0) {
        uv_unref((uv_handle_t *) &qrt->lws.keepalive);
    }
}

struct lws_context *tjs__lws_get_context(JSContext *ctx) {
    TJSRuntime *qrt = TJS_GetRuntime(ctx);
    CHECK_NOT_NULL(qrt);

    if (!qrt->lws.ctx) {
        tjs__lws_init(qrt);
    }

    return qrt->lws.ctx;
}

struct lws_vhost *tjs__lws_create_client_tls_vhost(JSContext *ctx,
                                                   const struct lws_protocols *protocols,
                                                   const uint8_t *ca_pem,
                                                   size_t ca_len,
                                                   const uint8_t *cert_pem,
                                                   size_t cert_len,
                                                   const uint8_t *key_pem,
                                                   size_t key_len) {
    struct lws_context *lws_ctx = tjs__lws_get_context(ctx);
    struct lws_context_creation_info info;
    DynBuf combined_ca;
    uint8_t *cert_pem_nt = NULL;
    uint8_t *key_pem_nt = NULL;
    size_t combined_ca_len = 0;
    size_t key_pem_store_len = 0;
    struct lws_vhost *vhost = NULL;

    tjs__lws_clear_tls_error(TJS_GetRuntime(ctx), ctx);
    if (!lws_ctx) {
        return NULL;
    }

    tjs_dbuf_init(ctx, &combined_ca);
    if (dbuf_put(&combined_ca, (const uint8_t *) tjs_cacert_pem, TJS_CACERT_PEM_LEN) ||
        dbuf_putc(&combined_ca, '\n') ||
        dbuf_putc(&combined_ca, '\n')) {
        goto done;
    }

    if (ca_pem && ca_len) {
        if (dbuf_put(&combined_ca, ca_pem, ca_len)) {
            goto done;
        }
    }

    combined_ca_len = combined_ca.size;
    if (dbuf_putc(&combined_ca, '\0')) {
        goto done;
    }

    if (tjs__lws_validate_client_ca(ctx, (const char *) combined_ca.buf, combined_ca_len)) {
        goto done;
    }

    if (cert_pem && cert_len) {
        cert_pem_nt = js_malloc(ctx, cert_len + 1);
        if (!cert_pem_nt) {
            goto done;
        }
        memcpy(cert_pem_nt, cert_pem, cert_len);
        cert_pem_nt[cert_len] = '\0';

        if (tjs__lws_validate_client_cert_pem(ctx, cert_pem_nt, cert_len)) {
            goto done;
        }
    }

    if (key_pem && key_len) {
        key_pem_store_len = key_len + 2;
        key_pem_nt = js_malloc(ctx, key_pem_store_len);
        if (!key_pem_nt) {
            goto done;
        }
        memcpy(key_pem_nt, key_pem, key_len);
        key_pem_nt[key_len] = '\0';
        key_pem_nt[key_len + 1] = '\0';

        if (tjs__lws_validate_client_key_pem(ctx, key_pem_nt, key_len)) {
            goto done;
        }
    }

    memset(&info, 0, sizeof(info));
    info.port = CONTEXT_PORT_NO_LISTEN;
    info.protocols = protocols;
    info.vhost_name = "tjs-mqtt-client";
    info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT | LWS_SERVER_OPTION_VH_INSTANTIATE_ALL_PROTOCOLS;
    info.client_ssl_ca_mem = combined_ca.buf;
    info.client_ssl_ca_mem_len = (unsigned int) combined_ca_len;
    if (cert_pem_nt && cert_len && key_pem_nt && key_pem_store_len) {
        info.client_ssl_cert_mem = cert_pem_nt;
        info.client_ssl_cert_mem_len = (unsigned int) cert_len;
        info.client_ssl_key_mem = key_pem_nt;
        info.client_ssl_key_mem_len = (unsigned int) key_pem_store_len;
    }

    vhost = lws_create_vhost(lws_ctx, &info);

done:
    js_free(ctx, cert_pem_nt);
    js_free(ctx, key_pem_nt);
    dbuf_free(&combined_ca);
    return vhost;
}

int tjs__lws_set_client_ca(JSContext *ctx, const uint8_t *pem, size_t len) {
    TJSRuntime *qrt = TJS_GetRuntime(ctx);
    char *combined;
    size_t combined_len;

    CHECK_NOT_NULL(qrt);
    tjs__lws_clear_tls_error(qrt, ctx);

    combined_len = TJS_CACERT_PEM_LEN + 2 + len;
    combined = js_malloc(ctx, combined_len + 1);
    if (!combined) {
        return -1;
    }

    memcpy(combined, tjs_cacert_pem, TJS_CACERT_PEM_LEN);
    combined[TJS_CACERT_PEM_LEN] = '\n';
    combined[TJS_CACERT_PEM_LEN + 1] = '\n';
    memcpy(combined + TJS_CACERT_PEM_LEN + 2, pem, len);
    combined[combined_len] = '\0';

    if (tjs__lws_validate_client_ca(ctx, combined, combined_len)) {
        js_free(ctx, combined);
        return -1;
    }

    if (qrt->lws.ctx) {
        /* LWS context already created; only allow if CA matches existing. */
        if (qrt->lws.client_ca_pem && qrt->lws.client_ca_pem_len == combined_len &&
            !memcmp(qrt->lws.client_ca_pem, combined, combined_len)) {
            js_free(ctx, combined);
            return 0;
        }
        js_free(ctx, combined);
        return -1;
    }

    js_free(ctx, qrt->lws.client_ca_pem);
    qrt->lws.client_ca_pem = combined;
    qrt->lws.client_ca_pem_len = (unsigned int) combined_len;

    return 0;
}

/* Extract the base64 content between PEM markers. Returns the base64 portion
 * (still encoded) as a newly allocated buffer, or NULL on failure.
 * Used by non-mbedtls backends. */
#if !defined(LWS_WITH_MBEDTLS)
static char *tjs__pem_extract_b64(JSContext *ctx, const uint8_t *pem, size_t pem_len, size_t *out_len) {
    const char *begin, *end;
    size_t b64_len;
    char *out;

    begin = (const char *) memmem(pem, pem_len, "-----\n", 6);
    if (!begin) {
        return NULL;
    }
    begin += 6;

    end = memmem(begin, pem_len - (begin - (const char *) pem), "-----END ", 9);
    if (!end) {
        return NULL;
    }

    b64_len = end - begin;
    out = js_malloc(ctx, b64_len + 1);
    if (!out) {
        return NULL;
    }
    memcpy(out, begin, b64_len);
    out[b64_len] = '\0';
    *out_len = b64_len;
    return out;
}
#endif

int tjs__lws_set_client_cert(JSContext *ctx, const uint8_t *cert, size_t cert_len, const uint8_t *key, size_t key_len) {
    TJSRuntime *qrt = TJS_GetRuntime(ctx);

    CHECK_NOT_NULL(qrt);
    tjs__lws_clear_tls_error(qrt, ctx);

#if defined(LWS_WITH_MBEDTLS)
    uint8_t *cert_pem_nt = NULL, *key_pem_nt = NULL;
    size_t key_pem_store_len;

    cert_pem_nt = js_malloc(ctx, cert_len + 1);
    if (!cert_pem_nt) {
        return -1;
    }
    memcpy(cert_pem_nt, cert, cert_len);
    cert_pem_nt[cert_len] = '\0';

    /* lws mbedtls path passes key_mem_len - 1 downstream, so store
     * one extra trailing NUL to preserve a NUL-terminated PEM length. */
    key_pem_store_len = key_len + 2;
    key_pem_nt = js_malloc(ctx, key_pem_store_len);
    if (!key_pem_nt) {
        js_free(ctx, cert_pem_nt);
        return -1;
    }
    memcpy(key_pem_nt, key, key_len);
    key_pem_nt[key_len] = '\0';
    key_pem_nt[key_len + 1] = '\0';

    if (tjs__lws_validate_client_cert_pem(ctx, cert_pem_nt, cert_len) ||
        tjs__lws_validate_client_key_pem(ctx, key_pem_nt, key_len)) {
        js_free(ctx, cert_pem_nt);
        js_free(ctx, key_pem_nt);
        return -1;
    }

    if (qrt->lws.ctx) {
        if (qrt->lws.client_cert_der && qrt->lws.client_key_der &&
            qrt->lws.client_cert_der_len == cert_len + 1 &&
            qrt->lws.client_key_der_len == key_pem_store_len &&
            !memcmp(qrt->lws.client_cert_der, cert_pem_nt, cert_len + 1) &&
            !memcmp(qrt->lws.client_key_der, key_pem_nt, key_pem_store_len)) {
            js_free(ctx, cert_pem_nt);
            js_free(ctx, key_pem_nt);
            return 0;
        }

        js_free(ctx, cert_pem_nt);
        js_free(ctx, key_pem_nt);
        return -1;
    }

    js_free(ctx, qrt->lws.client_cert_der);
    js_free(ctx, qrt->lws.client_key_der);
    qrt->lws.client_cert_der = cert_pem_nt;
    qrt->lws.client_cert_der_len = (unsigned int) cert_len + 1;
    qrt->lws.client_key_der = key_pem_nt;
    qrt->lws.client_key_der_len = (unsigned int) key_pem_store_len;

    return 0;
#else
    char *cert_b64 = NULL, *key_b64 = NULL;
    uint8_t *cert_der = NULL, *key_der = NULL;
    uint8_t *key_der_nt = NULL;
    size_t cert_b64_len = 0, key_b64_len = 0;
    size_t cert_der_len = 0, key_der_len = 0;


    /* Extract base64 content from PEM. */
    cert_b64 = tjs__pem_extract_b64(ctx, cert, cert_len, &cert_b64_len);
    if (!cert_b64) {
        return -1;
    }

    key_b64 = tjs__pem_extract_b64(ctx, key, key_len, &key_b64_len);
    if (!key_b64) {
        js_free(ctx, cert_b64);
        return -1;
    }

    /* Decode base64 to DER. */
    if (tjs__buffer_decode_base64(ctx, (const uint8_t *) cert_b64, cert_b64_len, &cert_der, &cert_der_len)) {
        js_free(ctx, cert_b64);
        js_free(ctx, key_b64);
        return -1;
    }

    if (tjs__buffer_decode_base64(ctx, (const uint8_t *) key_b64, key_b64_len, &key_der, &key_der_len)) {
        js_free(ctx, cert_b64);
        js_free(ctx, key_b64);
        js_free(ctx, cert_der);
        return -1;
    }

    js_free(ctx, cert_b64);
    js_free(ctx, key_b64);

    if (tjs__lws_validate_client_cert(ctx, cert_der, cert_der_len)) {
        js_free(ctx, cert_der);
        js_free(ctx, key_der);
        return -1;
    }

    /* lws passes key_mem_len - 1 to SSL_CTX_use_PrivateKey_ASN1, expecting
     * a null-terminated PEM string. Since we pass DER, append a null byte
     * and increment the length so lws uses the correct DER size. */
    key_der_nt = js_malloc(ctx, key_der_len + 1);
    if (!key_der_nt) {
        js_free(ctx, cert_der);
        js_free(ctx, key_der);
        return -1;
    }
    memcpy(key_der_nt, key_der, key_der_len);
    key_der_nt[key_der_len] = '\0';
    js_free(ctx, key_der);

    if (qrt->lws.ctx) {
        if (qrt->lws.client_cert_der && qrt->lws.client_key_der &&
            qrt->lws.client_cert_der_len == cert_der_len &&
            qrt->lws.client_key_der_len == key_der_len + 1 &&
            !memcmp(qrt->lws.client_cert_der, cert_der, cert_der_len) &&
            !memcmp(qrt->lws.client_key_der, key_der_nt, key_der_len + 1)) {
            js_free(ctx, cert_der);
            js_free(ctx, key_der_nt);
            return 0;
        }

        js_free(ctx, cert_der);
        js_free(ctx, key_der_nt);
        return -1;
    }

    js_free(ctx, qrt->lws.client_cert_der);
    js_free(ctx, qrt->lws.client_key_der);
    qrt->lws.client_cert_der = cert_der;
    qrt->lws.client_cert_der_len = (unsigned int) cert_der_len;
    qrt->lws.client_key_der = key_der_nt;
    qrt->lws.client_key_der_len = (unsigned int) key_der_len + 1;

    return 0;
#endif
}

const char *tjs__lws_last_tls_error(JSContext *ctx) {
    TJSRuntime *qrt = TJS_GetRuntime(ctx);
    CHECK_NOT_NULL(qrt);
    return qrt->lws.last_tls_error_message;
}

int tjs__lws_last_tls_error_code(JSContext *ctx) {
    TJSRuntime *qrt = TJS_GetRuntime(ctx);
    CHECK_NOT_NULL(qrt);
    return qrt->lws.last_tls_error_code;
}

static int tjs__lws_load_http_once(TJSRuntime *qrt, TJSHttpLoadCtx *load_ctx, const char *url) {
    JSContext *ctx = qrt->ctx;

    load_ctx->status = -1;
    load_ctx->done = false;
    load_ctx->redirect_url[0] = '\0';

    /* Parse URL. lws_parse_uri modifies the string in-place. */
    char *url_copy = js_strdup(ctx, url);
    if (!url_copy) {
        return -1;
    }

    const char *prot_str, *ads, *path;
    int port;
    if (lws_parse_uri(url_copy, &prot_str, &ads, &port, &path)) {
        js_free(ctx, url_copy);
        return -1;
    }

    bool use_ssl = !strcmp(prot_str, "https");

    /* lws_parse_uri strips the leading '/' from path, restore it. */
    char full_path[JS__PATH_MAX];
    snprintf(full_path, sizeof(full_path), "/%s", path);

    /* Resolve DNS synchronously.  The lws async DNS resolver is
     * unreliable in a temporary poll-based context. */
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    uv_getaddrinfo_t dns_req;
    if (uv_getaddrinfo(&qrt->loop, &dns_req, NULL, ads, NULL, &hints)) {
        js_free(ctx, url_copy);
        return -1;
    }

    char ip_str[INET6_ADDRSTRLEN];
    if (dns_req.addrinfo->ai_family == AF_INET6) {
        struct sockaddr_in6 *sa6 = (struct sockaddr_in6 *) dns_req.addrinfo->ai_addr;
        uv_inet_ntop(AF_INET6, &sa6->sin6_addr, ip_str, sizeof(ip_str));
    } else {
        struct sockaddr_in *sa = (struct sockaddr_in *) dns_req.addrinfo->ai_addr;
        uv_inet_ntop(AF_INET, &sa->sin_addr, ip_str, sizeof(ip_str));
    }
    uv_freeaddrinfo(dns_req.addrinfo);

    const struct lws_protocols protocols[] = {
        tjs_http_load_protocol,
        LWS_PROTOCOL_LIST_TERM,
    };

    struct lws_context_creation_info info;
    memset(&info, 0, sizeof(info));

    info.port = CONTEXT_PORT_NO_LISTEN;
    info.protocols = protocols;
    info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
    info.client_ssl_ca_mem = tjs_cacert_pem;
    info.client_ssl_ca_mem_len = TJS_CACERT_PEM_LEN;

    lws_set_log_level(0, NULL);

    struct lws_context *lws_ctx = lws_create_context(&info);
    if (!lws_ctx) {
        js_free(ctx, url_copy);
        return -1;
    }

    struct lws_client_connect_info cci;
    memset(&cci, 0, sizeof(cci));

    cci.context = lws_ctx;
    cci.address = ip_str;
    cci.port = port;
    cci.path = full_path;
    cci.host = ads;
    cci.origin = ads;
    cci.ssl_connection = (use_ssl ? LCCSCF_USE_SSL : 0);
    cci.method = "GET";
    cci.alpn = "http/1.1"; /* HTTP loader is HTTP/1 only */
    cci.local_protocol_name = TJS_LWS_HTTP_LOAD_PROTOCOL_NAME;
    cci.userdata = load_ctx;

    struct lws *wsi = lws_client_connect_via_info(&cci);

    if (!wsi) {
        lws_context_destroy(lws_ctx);
        js_free(ctx, url_copy);
        return -1;
    }

    uint64_t deadline = uv_hrtime() + (uint64_t) 10 * 1000000000;

    while (!load_ctx->done) {
        lws_service(lws_ctx, 250);
        if (uv_hrtime() >= deadline) {
            load_ctx->status = -2;
            load_ctx->done = true;
        }
    }

    lws_context_destroy(lws_ctx);

    /* Free after the context is destroyed. lws may reference strings
     * from url_copy during the connection handshake. */
    js_free(ctx, url_copy);

    return load_ctx->status;
}

#define TJS__MAX_REDIRECTS 20

int tjs__lws_load_http(TJSRuntime *qrt, DynBuf *dbuf, const char *url) {
    TJSHttpLoadCtx load_ctx = {
        .dbuf = dbuf,
    };

    const char *current_url = url;
    char retry_url[2048];

    for (int i = 0; i <= TJS__MAX_REDIRECTS; i++) {
        int status = tjs__lws_load_http_once(qrt, &load_ctx, current_url);

        /* Success or no redirect captured — we're done. */
        if (status == 200 || load_ctx.redirect_url[0] == '\0') {
            return status;
        }

        /* lws's built-in redirect failed (cross-host). Retry with
         * the captured redirect URL and a fresh lws context. */
        lws_strncpy(retry_url, load_ctx.redirect_url, sizeof(retry_url));
        current_url = retry_url;
        dbuf->size = 0;
    }

    return -1;
}
