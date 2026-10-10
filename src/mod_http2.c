/*
 * xuan - HTTP/2 module (node:http2 native support)
 *
 * Based on libwebsockets h2 client capability.
 * Provides native H2Session and H2Stream for Node.js-compatible HTTP/2 client API.
 */

#ifdef TJS__HAS_HTTP2

#include "private.h"
#include "mem.h"
#include "utils.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------
 * Forward declarations
 * ------------------------------------------------------------------ */

typedef struct TJSH2Stream TJSH2Stream;
typedef struct TJSH2Session TJSH2Session;

/* ------------------------------------------------------------------
 * lws internal type declarations (minimal subset needed for h2)
 * These mirror lws internal structures. Fragile across lws upgrades
 * but avoids pulling in the entire internal header dependency chain.
 * ------------------------------------------------------------------ */

/* h2 settings enum (matches lws internal) */
enum {
    TJS_H2SET_HEADER_TABLE_SIZE      = 1,
    TJS_H2SET_ENABLE_PUSH            = 2,
    TJS_H2SET_MAX_CONCURRENT_STREAMS = 3,
    TJS_H2SET_INITIAL_WINDOW_SIZE    = 4,
    TJS_H2SET_MAX_FRAME_SIZE         = 5,
    TJS_H2SET_MAX_HEADER_LIST_SIZE   = 6,
    TJS_H2SET_COUNT                  = 7,
};

/* Minimal mirror of lws http2_settings struct */
struct tjs_http2_settings {
    uint32_t s[TJS_H2SET_COUNT];
};

/* Minimal mirror of lws_h2_netconn struct (only fields we access) */
struct tjs_h2_netconn {
    struct tjs_http2_settings our_set;
    struct tjs_http2_settings peer_set;
    /* padding to match lws internal layout up to the fields we need */
    void     *_hpack_dummy;
    uint8_t   ping_payload[8];
    uint8_t   one_setting[6];
    char      goaway_str[32];
    uint32_t  goaway_err;
    uint32_t  goaway_sid;
};

/*
 * Helper to access h2 data from wsi.
 * NOTE: struct lws is opaque in public headers. We access h2 data
 * through a runtime-offset approach: the lws library exposes the
 * h2 netconn pointer through the wsi user data or via the protocol
 * callback. For now, we store settings locally and update them
 * from callback data where possible.
 */

/* ------------------------------------------------------------------
 * H2Stream structure
 * ------------------------------------------------------------------ */

/* H2Stream callback indices (must be before struct definition) */
#define H2STREAM_CB_RESPONSE    0
#define H2STREAM_CB_TRAILERS    1
#define H2STREAM_CB_DATA        2
#define H2STREAM_CB_END         3
#define H2STREAM_CB_ERROR       4
#define H2STREAM_CB_CLOSE       5
#define H2STREAM_CB_MAX         6

struct TJSH2Stream {
    JSContext       *ctx;
    JSValue          this_val;       /* self-reference to prevent GC */
    TJSH2Session    *session;        /* parent session */
    JSValue          session_val;    /* keep parent session alive until stream cleanup */
    uint64_t         gc_root_id;     /* rooted on globalThis while transport is alive */
    struct lws      *wsi;            /* the wsi (shares session's lws ctx) */

    /* Stream identification */
    int              stream_id;      /* HTTP/2 stream ID (assigned by lws) */

    /* Lifecycle flags */
    bool             closed;
    bool             destroyed;
    bool             pending;        /* still waiting for stream ID allocation */
    bool             half_closed_local;  /* we sent END_STREAM */
    bool             half_closed_remote; /* remote sent END_STREAM */
    bool             response_received;
    bool             trailers_received;

    /* RST_STREAM code */
    uint32_t         rst_code;       /* RST_STREAM error code, 0 if not received */

    /* Flow control */
    bool             paused;         /* flow control: paused by user */
    uint32_t         local_window_size;
    uint32_t         remote_window_size;

    /* Write state */
    bool             end_after_headers;  /* true if request() called with endStream */
    bool             headers_sent;
    bool             write_ended;    /* write() called with end flag */
    bool             body_pending;   /* lws should expect DATA frames */
    bool             completed;      /* terminal callback fired */
    bool             final_sent;     /* END_STREAM / final body marker sent */

    /* Headers/trailers tracking */
    JSValue          sent_headers;   /* headers object we sent */
    JSValue          sent_trailers;  /* trailers object we sent */
    char            *method;
    char            *path;
    char            *authority;
    char            *scheme;
    char            *connect_host;
    int              connect_port;
    int              ssl_flags;
    DynBuf           req_headers;    /* "name: value\r\n" lines */
    DynBuf           send_buf;       /* pending request DATA */

    /* Data buffering (for pause/resume) */
    uint8_t         *read_buf;
    size_t           read_buf_len;
    size_t           read_buf_pos;

    /* Event callbacks */
    JSValue          callbacks[H2STREAM_CB_MAX];

    /* Pending write callback (deferred until data actually sent) */
    JSValue          pending_write_cb;

    /* Ref count for event loop */
    int              ref_count;
};

/* lws internal functions (external linkage, declared for our use) */
enum lws_h2_protocol_send_type {
    LWS_PPS_NONE              = 0,
    LWS_H2_PPS_MY_SETTINGS    = 1,
    LWS_H2_PPS_ACK_SETTINGS   = 2,
    LWS_H2_PPS_PING           = 3,
    LWS_H2_PPS_PONG           = 4,
    LWS_H2_PPS_GOAWAY         = 5,
    LWS_H2_PPS_RST_STREAM     = 6,
    LWS_H2_PPS_UPDATE_WINDOW  = 7,
    LWS_H2_PPS_SETTINGS_INITIAL_UPDATE_WINDOW = 8,
};

struct lws_h2_protocol_send {
    struct lws_h2_protocol_send *next;
    enum lws_h2_protocol_send_type type;
    union {
        struct { char str[32]; uint32_t highest_sid; uint32_t err; } ga;
        struct { uint32_t sid; uint32_t err; } rs;
        struct { uint8_t ping_payload[8]; } ping;
        struct { uint32_t sid; uint32_t credit; } update_window;
    } u;
};

extern struct lws_h2_protocol_send *lws_h2_new_pps(enum lws_h2_protocol_send_type type);
extern void lws_pps_schedule(struct lws *wsi, struct lws_h2_protocol_send *pps);

/* ------------------------------------------------------------------
 * Constants and types
 * ------------------------------------------------------------------ */

#define H2SESSION_CB_CONNECT      0
#define H2SESSION_CB_REMOTE_SETTINGS  1
#define H2SESSION_CB_LOCAL_SETTINGS   2
#define H2SESSION_CB_GOAWAY       3
#define H2SESSION_CB_ERROR        4
#define H2SESSION_CB_CLOSE        5
#define H2SESSION_CB_PING         6
#define H2SESSION_CB_MAX          7

/* Default HTTP/2 settings (matching Node.js getDefaultSettings) */
#define H2_DEFAULT_HEADER_TABLE_SIZE      4096
#define H2_DEFAULT_ENABLE_PUSH            1
#define H2_DEFAULT_MAX_CONCURRENT_STREAMS 0xFFFFFFFF
#define H2_DEFAULT_INITIAL_WINDOW_SIZE    65535
#define H2_DEFAULT_MAX_FRAME_SIZE         16384
#define H2_DEFAULT_MAX_HEADER_LIST_SIZE   65536

typedef struct TJSH2Session {
    JSContext       *ctx;
    JSValue          this_val;       /* self-reference to prevent GC */
    uint64_t         gc_root_id;     /* rooted on globalThis while transport is alive */
    struct lws      *wsi;            /* H2 transport connection (anchor) */

    /* Lifecycle flags */
    bool             closed;
    bool             destroyed;
    bool             encrypted;
    bool             connecting;
    bool             connected;
    bool             remote_settings_received;
    bool             pending_settings_ack;
    bool             anchor_completed;  /* anchor HTTP txn done, conn ready for mux */

    /* Settings arrays (index matches lws enum lws_h2_settings: 1-6) */
    uint32_t         local_settings[7];
    uint32_t         remote_settings[7];

    /* Flow control window sizes */
    uint32_t         local_window_size;
    uint32_t         remote_window_size;

    /* Socket proxy (TLSSocket or net.Socket) */
    JSValue          socket_obj;
    char            *connect_host;
    char            *connect_path;
    int              connect_port;
    int              connect_ssl;

    /* Event callbacks */
    JSValue          callbacks[H2SESSION_CB_MAX];

    /* Pending ping state */
    struct {
        JSValue      callback;
        int64_t      send_time_ms;   /* uv_now() when ping was sent */
        uint8_t      payload[8];
        bool         pending;
    } ping;

    /* Stream tracking (for B2) */
    int              next_stream_id;
    int              last_processed_stream_id;
    JSValue          active_streams; /* Map: stream_id -> H2Stream JSValue */

    /* Event loop reference tracking */
    int              ref_count;      /* >0 means loop is kept alive */

    /* Tracked GOAWAY info (captured from close event) */
    uint32_t         goaway_err;
    uint32_t         goaway_sid;
    char             goaway_str_buf[32];
} TJSH2Session;

/* ------------------------------------------------------------------
 * Class registration
 * ------------------------------------------------------------------ */

static JSClassID tjs_h2session_class_id;
static JSClassID tjs_h2stream_class_id;

static bool tjs_h2_debug_enabled(void)
{
    static int enabled = -1;
    if (enabled < 0) {
        const char *v = getenv("TJS_H2_DEBUG");
        enabled = v && v[0] && strcmp(v, "0");
    }
    return enabled != 0;
}

static bool tjs_h2_skip_req_headers(void)
{
    static int enabled = -1;
    if (enabled < 0) {
        const char *v = getenv("TJS_H2_SKIP_REQ_HEADERS");
        enabled = v && v[0] && strcmp(v, "0");
    }
    return enabled != 0;
}

#define TJS_H2_DEBUGF(...) do { \
    if (tjs_h2_debug_enabled()) \
        fprintf(stderr, __VA_ARGS__); \
} while (0)

#define TJS_H2_ROOTS_PROP "__tjs_http2_roots__"

static uint64_t tjs_h2_next_root_id = 1;

static JSValue tjs_h2_get_roots_obj(JSContext *ctx)
{
    JSValue global = JS_GetGlobalObject(ctx);
    if (JS_IsException(global))
        return global;

    JSValue roots = JS_GetPropertyStr(ctx, global, TJS_H2_ROOTS_PROP);
    if (JS_IsException(roots)) {
        JS_FreeValue(ctx, global);
        return roots;
    }

    if (JS_IsUndefined(roots)) {
        JS_FreeValue(ctx, roots);
        roots = JS_NewObjectProto(ctx, JS_NULL);
        if (!JS_IsException(roots)) {
            JS_DefinePropertyValueStr(ctx, global, TJS_H2_ROOTS_PROP,
                                      JS_DupValue(ctx, roots),
                                      JS_PROP_C_W_E);
        }
    }

    JS_FreeValue(ctx, global);
    return roots;
}

static int tjs_h2_pin_value(JSContext *ctx, const char *prefix, uint64_t id, JSValue value)
{
    JSValue roots = tjs_h2_get_roots_obj(ctx);
    if (JS_IsException(roots))
        return -1;

    char keybuf[64];
    snprintf(keybuf, sizeof(keybuf), "%s_%llu", prefix, (unsigned long long) id);
    int ret = JS_DefinePropertyValueStr(ctx, roots, keybuf,
                                        JS_DupValue(ctx, value),
                                        JS_PROP_C_W_E);
    JS_FreeValue(ctx, roots);
    return ret;
}

static void tjs_h2_unpin_value(JSContext *ctx, const char *prefix, uint64_t *id_ptr)
{
    if (!ctx || !id_ptr || *id_ptr == 0)
        return;

    JSValue roots = tjs_h2_get_roots_obj(ctx);
    if (JS_IsException(roots))
        return;

    char keybuf[64];
    snprintf(keybuf, sizeof(keybuf), "%s_%llu", prefix, (unsigned long long) *id_ptr);
    JSAtom atom = JS_NewAtom(ctx, keybuf);
    if (atom != JS_ATOM_NULL)
        JS_DeleteProperty(ctx, roots, atom, 0);
    JS_FreeAtom(ctx, atom);
    JS_FreeValue(ctx, roots);
    *id_ptr = 0;
}

static JSValue tjs_h2session_push_event(TJSH2Session *s, int cb_idx,
                                        int argc, JSValue *argv);
static int tjs_lws_h2stream_callback(struct lws *wsi,
                                     enum lws_callback_reasons reason,
                                     void *user, void *in, size_t len);
static void tjs_h2stream_finish(TJSH2Stream *st, bool emit_end, bool emit_close);
static void tjs_h2stream_loop_unref(TJSH2Stream *st);
static void tjs_h2session_populate_socket_tls_info(TJSH2Session *s);
static void tjs_h2session_release_self(TJSH2Session *s);
static void tjs_h2stream_release_self(TJSH2Stream *st);
static void tjs_h2stream_detach_transport(TJSH2Stream *st, int kill_mode);
static void tjs_h2stream_adopt_callback_wsi(TJSH2Stream *st, struct lws *wsi,
                                            enum lws_callback_reasons reason);

/* ------------------------------------------------------------------
 * Helpers: loop ref/unref (internal, not JS methods)
 * ------------------------------------------------------------------ */

static void tjs_h2session_loop_ref(TJSH2Session *s)
{
    if (s->ref_count++ == 0) {
        TJSRuntime *qrt = TJS_GetRuntime(s->ctx);
        uv_ref((uv_handle_t *) &qrt->lws.keepalive);
    }
}

static void tjs_h2session_loop_unref(TJSH2Session *s)
{
    if (s->ref_count > 0 && --s->ref_count == 0) {
        TJSRuntime *qrt = TJS_GetRuntime(s->ctx);
        uv_unref((uv_handle_t *) &qrt->lws.keepalive);
    }
}

/* ------------------------------------------------------------------
 * Helpers: settings access
 * ------------------------------------------------------------------ */

static struct lws_context *tjs_h2session_get_lws_ctx(JSContext *ctx)
{
    return tjs__lws_get_context(ctx);
}

static void tjs_h2session_update_settings_from_wsi(TJSH2Session *s)
{
    /*
     * NOTE: struct lws is opaque in public headers, so we cannot
     * directly access wsi->h2.h2n->peer_set/our_set.
     *
     * For B1 MVP: settings remain at defaults. The remote settings
     * event fires with defaults, which is sufficient for grpc-js
     * to function (it uses initialWindowSize for flow control setup).
     *
     * TODO(B2): Extract actual settings from lws through one of:
     *   1. lws internal callback data if SETTINGS are passed through
     *   2. A small patch to lws to expose h2 settings via userdata
     *   3. Using the lws_settings lws_extension_callback
     */
    (void) s;
    s->remote_settings_received = true;
}

static JSValue tjs_h2session_make_settings_obj(JSContext *ctx, uint32_t settings[7])
{
    JSValue obj = JS_NewObjectProto(ctx, JS_NULL);
    if (JS_IsException(obj))
        return obj;

    JS_DefinePropertyValueStr(ctx, obj, "headerTableSize",
        JS_NewInt32(ctx, (int32_t) settings[1]), JS_PROP_C_W_E);
    JS_DefinePropertyValueStr(ctx, obj, "enablePush",
        JS_NewInt32(ctx, (int32_t) settings[2]), JS_PROP_C_W_E);
    JS_DefinePropertyValueStr(ctx, obj, "maxConcurrentStreams",
        JS_NewInt32(ctx, (int32_t) settings[3]), JS_PROP_C_W_E);
    JS_DefinePropertyValueStr(ctx, obj, "initialWindowSize",
        JS_NewInt32(ctx, (int32_t) settings[4]), JS_PROP_C_W_E);
    JS_DefinePropertyValueStr(ctx, obj, "maxFrameSize",
        JS_NewInt32(ctx, (int32_t) settings[5]), JS_PROP_C_W_E);
    JS_DefinePropertyValueStr(ctx, obj, "maxHeaderListSize",
        JS_NewInt32(ctx, (int32_t) settings[6]), JS_PROP_C_W_E);

    return obj;
}

/* ------------------------------------------------------------------
 * Helpers: stream tracking on session
 * ------------------------------------------------------------------ */

static void tjs_h2session_register_stream(TJSH2Session *s, TJSH2Stream *st)
{
    /*
     * The stream is tracked via its wsi userdata. When lws fires callbacks
     * on the stream's wsi, we can retrieve the stream via lws_wsi_user(wsi).
     * The session's active_streams object tracks stream_id -> JSValue for
     * reference management and cleanup.
     */
    char keybuf[32];
    snprintf(keybuf, sizeof(keybuf), "stream_%d", st->stream_id);
    JSValue js_key = JS_NewString(s->ctx, keybuf);
    if (JS_IsException(js_key))
        return;
    JSValue js_val = JS_DupValue(s->ctx, st->this_val);
    JSAtom atom = JS_ValueToAtom(s->ctx, js_key);
    if (atom != JS_ATOM_NULL)
        JS_SetProperty(s->ctx, s->active_streams, atom, js_val);
    else
        JS_FreeValue(s->ctx, js_val);
    JS_FreeAtom(s->ctx, atom);
    JS_FreeValue(s->ctx, js_key);
    lwsl_info("H2Session: registered stream %d\n", st->stream_id);
}

static void tjs_h2session_finish_all_streams(TJSH2Session *s)
{
    if (JS_IsUndefined(s->active_streams) || JS_IsNull(s->active_streams))
        return;

    JSPropertyEnum *tab = NULL;
    uint32_t len = 0;
    JSContext *ctx = s->ctx;

    if (JS_GetOwnPropertyNames(ctx, &tab, &len, s->active_streams,
                               JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY))
        return;

    for (uint32_t i = 0; i < len; i++) {
        JSValue val = JS_GetProperty(ctx, s->active_streams, tab[i].atom);
        if (JS_IsException(val)) {
            JS_FreeAtom(ctx, tab[i].atom);
            continue;
        }
        TJSH2Stream *st = JS_GetOpaque(val, tjs_h2stream_class_id);
        if (st && !st->completed) {
            lwsl_info("H2Session close: finishing stream %d\n", st->stream_id);
            tjs_h2stream_finish(st, false, true);
        }
        JS_FreeValue(ctx, val);
        JS_FreeAtom(ctx, tab[i].atom);
    }

    js_free(ctx, tab);
}

static void tjs_h2session_clear_stream_registry(TJSH2Session *s)
{
    if (JS_IsUndefined(s->active_streams) || JS_IsNull(s->active_streams))
        return;

    JSPropertyEnum *tab = NULL;
    uint32_t len = 0;
    JSContext *ctx = s->ctx;

    if (JS_GetOwnPropertyNames(ctx, &tab, &len, s->active_streams,
                               JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY))
        return;

    for (uint32_t i = 0; i < len; i++) {
        JS_DeleteProperty(ctx, s->active_streams, tab[i].atom, 0);
        JS_FreeAtom(ctx, tab[i].atom);
    }

    js_free(ctx, tab);
}

static void tjs_h2session_close_transport(TJSH2Session *s, int kill_mode)
{
    if (!s->wsi)
        return;

    struct lws *wsi = s->wsi;
    lws_set_wsi_user(wsi, NULL);
    s->wsi = NULL;

    tjs__lws_conn_unref(s->ctx);
    tjs_h2session_loop_unref(s);

    lws_set_timeout(wsi, PENDING_TIMEOUT_USER_OK, kill_mode);
}

static void tjs_h2session_release_self(TJSH2Session *s)
{
    if (JS_IsUndefined(s->this_val))
        return;

    tjs_h2_unpin_value(s->ctx, "session", &s->gc_root_id);
    JSValue val = s->this_val;
    s->this_val = JS_UNDEFINED;
    JS_FreeValue(s->ctx, val);
}

/* ------------------------------------------------------------------
 * R5: Populate socket_obj with TLS observability fields
 *
 * grpc-js reads these fields from session.socket:
 *   - alpnProtocol: 'h2' or null (h2c)
 *   - authorized: true/false
 *   - authorizationError: string or null
 *   - getCipher(): { name, standardName, version }
 *   - getPeerCertificate(): object (may be empty)
 *   - getCertificate(): object (may be empty)
 *   - getProtocol(): string or null
 *
 * Since lws handles TLS internally and doesn't expose ALPN/cipher details
 * through public APIs, we provide stable stub values that match Node.js
 * shape. For h2 (TLS), alpnProtocol is known to be 'h2' because that's
 * what we negotiate.
 * ------------------------------------------------------------------ */

static JSValue tjs_h2socket_get_cipher(JSContext *ctx, JSValue this_val,
                                        int argc, JSValue *argv)
{
    /* Stub: lws doesn't expose cipher details through public API.
     * Return Node.js-compatible shape with null values for h2c,
     * and generic values for TLS h2. */
    JSValue obj = JS_NewObject(ctx);
    if (JS_IsException(obj))
        return obj;
    JS_DefinePropertyValueStr(ctx, obj, "name", JS_NULL, JS_PROP_C_W_E);
    JS_DefinePropertyValueStr(ctx, obj, "standardName", JS_NULL, JS_PROP_C_W_E);
    JS_DefinePropertyValueStr(ctx, obj, "version", JS_NULL, JS_PROP_C_W_E);
    return obj;
}

static JSValue tjs_h2socket_get_peer_certificate(JSContext *ctx, JSValue this_val,
                                                  int argc, JSValue *argv)
{
    /* Stub: lws doesn't expose peer cert through public API.
     * Return empty object (same shape as Node's default behavior
     * when no cert data is available). */
    return JS_NewObject(ctx);
}

static JSValue tjs_h2socket_get_certificate(JSContext *ctx, JSValue this_val,
                                             int argc, JSValue *argv)
{
    /* Stub: lws doesn't expose local cert through public API. */
    return JS_NewObject(ctx);
}

static JSValue tjs_h2socket_get_protocol(JSContext *ctx, JSValue this_val,
                                          int argc, JSValue *argv)
{
    /* Return 'TLSv1.2' or 'TLSv1.3' for TLS connections, null for h2c.
     * We don't know the exact version from lws public API, so use
     * a conservative default for TLS. */
    bool encrypted = false;
    JSValue prop = JS_GetPropertyStr(ctx, this_val, "encrypted");
    if (!JS_IsException(prop) && JS_IsBool(prop))
        encrypted = JS_ToBool(ctx, prop);
    JS_FreeValue(ctx, prop);
    if (encrypted)
        return JS_NewString(ctx, "TLSv1.2");
    return JS_NULL;
}

static void tjs_h2session_populate_socket_tls_info(TJSH2Session *s)
{
    JSContext *ctx = s->ctx;
    JSValue sock = s->socket_obj;
    if (JS_IsUndefined(sock) || JS_IsNull(sock))
        return;

    bool encrypted = s->connect_ssl != 0;

    /* alpnProtocol: 'h2' for TLS h2, null for h2c */
    JS_DefinePropertyValueStr(ctx, sock, "alpnProtocol",
        encrypted ? JS_NewString(ctx, "h2") : JS_NULL,
        JS_PROP_C_W_E);

    /*
     * R5: Read rejectUnauthorized from the JS session object.
     * http2.js sets nativeSession._rejectUnauthorized before the
     * anchor connection completes (it's set right after connect()).
     *
     * Node.js semantics:
     * - rejectUnauthorized=true (default): authorized reflects real validation
     * - rejectUnauthorized=false: authorized=false (cert validation skipped)
     *
     * Since we don't have access to the actual mbedtls validation result,
     * we use rejectUnauthorized as a proxy:
     * - true: assume cert is valid (authorized=true, no error)
     * - false: authorized=false, authorizationError=null (skipped, not error)
     */
    bool reject_authz = true; /* default */
    JSValue this_obj = s->this_val;
    if (!JS_IsUndefined(this_obj)) {
        JSValue ru = JS_GetPropertyStr(ctx, this_obj, "_rejectUnauthorized");
        if (!JS_IsException(ru) && !JS_IsUndefined(ru))
            reject_authz = JS_ToBool(ctx, ru);
        JS_FreeValue(ctx, ru);
    }

    if (reject_authz) {
        /* Assume cert is valid; we can't verify without mbedtls access */
        JS_DefinePropertyValueStr(ctx, sock, "authorized",
            JS_NewBool(ctx, true), JS_PROP_C_W_E);
        JS_DefinePropertyValueStr(ctx, sock, "authorizationError",
            JS_NULL, JS_PROP_C_W_E);
    } else {
        /* Validation skipped; authorized=false, no error string */
        JS_DefinePropertyValueStr(ctx, sock, "authorized",
            JS_NewBool(ctx, false), JS_PROP_C_W_E);
        JS_DefinePropertyValueStr(ctx, sock, "authorizationError",
            JS_NULL, JS_PROP_C_W_E);
    }

    /* encrypted: true for TLS (h2), false for cleartext (h2c) */
    JS_DefinePropertyValueStr(ctx, sock, "encrypted",
        JS_NewBool(ctx, encrypted),
        JS_PROP_C_W_E);

    /* servername: the host we connected to (SNI) */
    JS_DefinePropertyValueStr(ctx, sock, "servername",
        s->connect_host ? JS_NewString(ctx, s->connect_host) : JS_NULL,
        JS_PROP_C_W_E);

    /* getCipher() method - returns { name, standardName, version } */
    JSValue cipher_func = JS_NewCFunction(ctx, tjs_h2socket_get_cipher,
                                          "getCipher", 0);
    JS_DefinePropertyValueStr(ctx, sock, "getCipher",
        cipher_func, JS_PROP_C_W_E);

    /* getPeerCertificate() method */
    JSValue peer_cert_func = JS_NewCFunction(ctx, tjs_h2socket_get_peer_certificate,
                                             "getPeerCertificate", 0);
    JS_DefinePropertyValueStr(ctx, sock, "getPeerCertificate",
        peer_cert_func, JS_PROP_C_W_E);

    /* getCertificate() method */
    JSValue cert_func = JS_NewCFunction(ctx, tjs_h2socket_get_certificate,
                                        "getCertificate", 0);
    JS_DefinePropertyValueStr(ctx, sock, "getCertificate",
        cert_func, JS_PROP_C_W_E);

    /* getProtocol() method */
    JSValue proto_func = JS_NewCFunction(ctx, tjs_h2socket_get_protocol,
                                         "getProtocol", 0);
    JS_DefinePropertyValueStr(ctx, sock, "getProtocol",
        proto_func, JS_PROP_C_W_E);

    lwsl_info("H2Session: socket TLS info populated (encrypted=%d, rejectAuthz=%d)\n",
              encrypted, reject_authz);
}

static void tjs_h2session_unregister_stream(TJSH2Session *s, int stream_id)
{
    if (JS_IsUndefined(s->active_streams) || JS_IsNull(s->active_streams))
        return;

    char keybuf[32];
    snprintf(keybuf, sizeof(keybuf), "stream_%d", stream_id);
    JSValue js_key = JS_NewString(s->ctx, keybuf);
    if (JS_IsException(js_key))
        return;
    JSAtom atom = JS_ValueToAtom(s->ctx, js_key);
    if (atom != JS_ATOM_NULL)
        JS_DeleteProperty(s->ctx, s->active_streams, atom, 0);
    JS_FreeAtom(s->ctx, atom);
    JS_FreeValue(s->ctx, js_key);
    lwsl_info("H2Session: unregistered stream %d\n", stream_id);
}

/* ------------------------------------------------------------------
 * Callback invocation
 * ------------------------------------------------------------------ */

static const char *tjs_h2session_cb_name(int cb_idx)
{
    switch (cb_idx) {
    case H2SESSION_CB_CONNECT:
        return "connect";
    case H2SESSION_CB_REMOTE_SETTINGS:
        return "remoteSettings";
    case H2SESSION_CB_LOCAL_SETTINGS:
        return "localSettings";
    case H2SESSION_CB_GOAWAY:
        return "goaway";
    case H2SESSION_CB_ERROR:
        return "error";
    case H2SESSION_CB_CLOSE:
        return "close";
    case H2SESSION_CB_PING:
        return "ping";
    default:
        return "unknown";
    }
}

static JSValue tjs_h2session_push_event(TJSH2Session *s, int cb_idx,
                                        int argc, JSValue *argv)
{
    JSContext *ctx = s->ctx;
    JSValue func = s->callbacks[cb_idx];

    if (JS_IsFunction(ctx, func)) {
        TJS_H2_DEBUGF("[h2session] push_event %s argc=%d closed=%d destroyed=%d connected=%d ref_count=%d\n",
                      tjs_h2session_cb_name(cb_idx), argc, s->closed, s->destroyed,
                      s->connected, s->ref_count);
        tjs_call_handler(ctx, func, argc, argv);
        TJS_H2_DEBUGF("[h2session] push_event %s returned\n",
                      tjs_h2session_cb_name(cb_idx));
    }

    /* Free argv */
    for (int i = 0; i < argc; i++) {
        JS_FreeValue(ctx, argv[i]);
    }

    return JS_UNDEFINED;
}

/* ------------------------------------------------------------------
 * lws protocol callback for h2 sessions
 * ------------------------------------------------------------------ */

static int tjs_lws_h2session_callback(struct lws *wsi,
                                       enum lws_callback_reasons reason,
                                       void *user, void *in, size_t len)
{
    (void) user;
    TJSH2Session *s = lws_wsi_user(wsi);

    if (!s || !s->ctx)
        return 0;

    JSContext *ctx = s->ctx;
    TJSRuntime *qrt = TJS_GetRuntime(ctx);

    switch (reason) {
    case LWS_CALLBACK_CLIENT_CONNECTION_ERROR: {
        if (qrt->freeing)
            break;
        const char *err_msg = in ? (const char *) in : "Connection error";
        lwsl_info("H2Session connection error: %s\n", err_msg);

        s->connecting = false;
        s->wsi = NULL;

        JSValue args[2];
        args[0] = JS_NewString(ctx, "CONNECTION_ERROR");
        args[1] = JS_NewString(ctx, err_msg);
        tjs_h2session_push_event(s, H2SESSION_CB_ERROR, 2, args);
        tjs_h2session_push_event(s, H2SESSION_CB_CLOSE, 0, NULL);

        tjs__lws_conn_unref(ctx);
        tjs_h2session_loop_unref(s);
        tjs_h2_unpin_value(ctx, "session", &s->gc_root_id);
        JS_FreeValue(ctx, s->this_val);
        s->this_val = JS_UNDEFINED;
        break;
    }

    case LWS_CALLBACK_ESTABLISHED_CLIENT_HTTP: {
        if (qrt->freeing)
            break;
        lwsl_info("H2Session established (anchor for mux)\n");

        s->connecting = false;
        s->connected = true;

        /* Extract initial settings from the wsi */
        tjs_h2session_update_settings_from_wsi(s);
        s->remote_settings_received = true;

        /*
         * R5: Populate socket_obj with TLS observability fields.
         * grpc-js reads session.socket.alpnProtocol, authorized, getCipher(), etc.
         */
        tjs_h2session_populate_socket_tls_info(s);

        /* Emit 'connect' event: (session, socket) */
        JSValue args[2];
        args[0] = JS_DupValue(ctx, s->this_val);
        args[1] = JS_DupValue(ctx, s->socket_obj);
        tjs_h2session_push_event(s, H2SESSION_CB_CONNECT, 2, args);

        /* Emit 'remoteSettings' event */
        JSValue settings = tjs_h2session_make_settings_obj(ctx, s->remote_settings);
        if (!JS_IsException(settings)) {
            JSValue args2[1];
            args2[0] = settings;
            tjs_h2session_push_event(s, H2SESSION_CB_REMOTE_SETTINGS, 1, args2);
        }
        break;
    }

    case LWS_CALLBACK_COMPLETED_CLIENT_HTTP: {
        /*
         * R4: The anchor HTTP transaction completed.
         *
         * The underlying H2 TCP/TLS connection remains active for multiplexing.
         * lws will route subsequent session.request() calls as substreams on
         * this existing H2 connection via internal mux routing.
         *
         * We mark the anchor as completed so we know the connection is
         * ready for mux. We do NOT close the WSI here.
         */
        if (qrt->freeing)
            break;
        lwsl_info("H2Session: anchor HTTP txn completed, conn ready for mux\n");
        s->anchor_completed = true;
        break;
    }

    case LWS_CALLBACK_CLOSED_CLIENT_HTTP: {
        if (qrt->freeing)
            break;
        lwsl_info("H2Session anchor HTTP txn closed; transport may remain for mux\n");

        /*
         * R4: Anchor HTTP transaction close is not necessarily transport close.
         * Do not tear down the whole session here.
         */
        s->anchor_completed = true;
        break;
    }

    case LWS_CALLBACK_CLOSED: {
        if (qrt->freeing)
            break;
        lwsl_info("H2Session transport closed\n");

        if (s->wsi == wsi) {
            /* Check for GOAWAY info we tracked */
            if (s->goaway_err != 0) {
                uint32_t err_code = s->goaway_err;
                uint32_t last_sid = s->goaway_sid;

                JSValue args[3];
                args[0] = JS_NewInt32(ctx, (int32_t) err_code);
                args[1] = JS_NewInt32(ctx, (int32_t) last_sid);
                args[2] = JS_NewArrayBufferCopy(ctx,
                    (const uint8_t *) s->goaway_str_buf,
                    strnlen(s->goaway_str_buf, sizeof(s->goaway_str_buf)));
                tjs_h2session_push_event(s, H2SESSION_CB_GOAWAY, 3, args);
            }

            lws_set_wsi_user(s->wsi, NULL);
            s->wsi = NULL;
        }

        s->closed = true;
        s->connected = false;
        s->connecting = false;
        s->anchor_completed = true;

        tjs_h2session_finish_all_streams(s);
        tjs_h2session_push_event(s, H2SESSION_CB_CLOSE, 0, NULL);

        tjs__lws_conn_unref(ctx);
        tjs_h2session_loop_unref(s);
        tjs_h2_unpin_value(ctx, "session", &s->gc_root_id);
        JS_FreeValue(ctx, s->this_val);
        s->this_val = JS_UNDEFINED;
        break;
    }

    case LWS_CALLBACK_CLIENT_HTTP_WRITEABLE: {
        /* Handle pending ping send */
        if (s->ping.pending && s->wsi) {
            struct lws_h2_protocol_send *pps = lws_h2_new_pps(LWS_H2_PPS_PING);
            if (pps) {
                memcpy(pps->u.ping.ping_payload, s->ping.payload, 8);
                lws_pps_schedule(s->wsi, pps);
                s->ping.send_time_ms = uv_now(tjs_get_loop(ctx));
                lwsl_info("H2Session ping sent\n");
            }
            s->ping.pending = false;
        }
        break;
    }

    case LWS_CALLBACK_CLIENT_RECEIVE: {
        /* This is for websocket data, not h2. Ignore. */
        break;
    }

    case LWS_CALLBACK_RECEIVE_CLIENT_HTTP_READ: {
        /* Data received on HTTP client connection (DATA frame payload) */
        if (qrt->freeing || !in || len == 0)
            break;

        lwsl_info("H2Session: received %zu bytes\n", len);

        /* For B2 MVP: we don't have per-stream wsi mapping from lws yet.
         * We'll emit data on all active streams for now.
         * TODO: Map to specific stream once we have proper stream tracking. */

        /* Create buffer from received data */
        JSValue data_buf = JS_NewArrayBufferCopy(ctx, (const uint8_t *) in, len);
        if (!JS_IsException(data_buf)) {
            /* Emit 'data' on active streams - for now we just store it
             * and let the JS layer handle routing to correct stream */
            JSValue args[1];
            args[0] = data_buf;
            /* We'd need to find the right stream. For B2 MVP, skip. */
            JS_FreeValue(ctx, data_buf);
        }
        break;
    }

    case LWS_CALLBACK_RECEIVE_CLIENT_HTTP: {
        /* HTTP client received data - need to call lws_http_client_read()
         * to actually drain it, which will trigger RECEIVE_CLIENT_HTTP_READ */
        if (qrt->freeing)
            break;

        /* Drain the data - this will trigger RECEIVE_CLIENT_HTTP_READ callbacks */
        int ilen = (int) len;
        lws_http_client_read(wsi, (char **) &in, &ilen);
        break;
    }

    case LWS_CALLBACK_CLIENT_RECEIVE_PONG: {
        /* PONG received - invoke pending ping callback */
        if (JS_IsFunction(ctx, s->ping.callback)) {
            int64_t now = uv_now(tjs_get_loop(ctx));
            int64_t duration = now - s->ping.send_time_ms;
            if (duration < 0)
                duration = 0;

            JSValue args[3];
            args[0] = JS_NULL;
            args[1] = JS_NewFloat64(ctx, (double) duration);
            args[2] = JS_NewArrayBufferCopy(ctx, s->ping.payload, 8);
            tjs_call_handler(ctx, s->ping.callback, 3, args);
            for (int i = 0; i < 3; i++)
                JS_FreeValue(ctx, args[i]);

            JS_FreeValue(ctx, s->ping.callback);
            s->ping.callback = JS_UNDEFINED;
            lwsl_info("H2Session pong received, RTT=%ld ms\n", (long) duration);
        }
        break;
    }

    case LWS_CALLBACK_TIMER: {
        if (qrt->freeing)
            break;
        lwsl_info("H2Session timeout\n");

        s->connecting = false;

        JSValue args[1];
        args[0] = JS_NewString(ctx, "TIMED_OUT");
        tjs_h2session_push_event(s, H2SESSION_CB_ERROR, 1, args);
        break;
    }

    default:
        break;
    }

    return lws_callback_http_dummy(wsi, reason, user, in, len);
}

/* ------------------------------------------------------------------
 * lws protocol registration
 * ------------------------------------------------------------------ */

const struct lws_protocols tjs_h2session_protocol = {
    .name            = "tjs-h2session",
    .callback        = tjs_lws_h2session_callback,
    .per_session_data_size = 0,
    .rx_buffer_size  = 0,
};

const struct lws_protocols tjs_h2stream_protocol = {
    .name            = "tjs-h2stream",
    .callback        = tjs_lws_h2stream_callback,
    .per_session_data_size = 0,
    .rx_buffer_size  = 0,
};

/* ------------------------------------------------------------------
 * Constructor
 * ------------------------------------------------------------------ */

static JSValue tjs_h2session_constructor(JSContext *ctx, JSValue new_target,
                                          int argc, JSValue *argv)
{
    (void) new_target;
    (void) argc;
    (void) argv;

    JSValue obj = JS_NewObjectClass(ctx, tjs_h2session_class_id);
    if (JS_IsException(obj))
        return obj;

    TJSH2Session *s = tjs__mallocz(sizeof(*s));
    if (!s) {
        JS_FreeValue(ctx, obj);
        return JS_ThrowOutOfMemory(ctx);
    }

    s->ctx = ctx;
    s->this_val = JS_DupValue(ctx, obj);
    s->gc_root_id = tjs_h2_next_root_id++;
    s->wsi = NULL;
    s->closed = false;
    s->destroyed = false;
    s->encrypted = false;
    s->connecting = false;
    s->connected = false;
    s->remote_settings_received = false;
    s->pending_settings_ack = false;
    s->socket_obj = JS_UNDEFINED;
    s->connect_host = NULL;
    s->connect_path = NULL;
    s->connect_port = 0;
    s->connect_ssl = 0;
    s->ping.callback = JS_UNDEFINED;
    s->ping.pending = false;
    s->next_stream_id = 1;
    s->last_processed_stream_id = 0;
    s->active_streams = JS_UNDEFINED;
    s->ref_count = 0;

    /* Initialize default settings */
    s->local_settings[1] = H2_DEFAULT_HEADER_TABLE_SIZE;
    s->local_settings[2] = H2_DEFAULT_ENABLE_PUSH;
    s->local_settings[3] = H2_DEFAULT_MAX_CONCURRENT_STREAMS;
    s->local_settings[4] = H2_DEFAULT_INITIAL_WINDOW_SIZE;
    s->local_settings[5] = H2_DEFAULT_MAX_FRAME_SIZE;
    s->local_settings[6] = H2_DEFAULT_MAX_HEADER_LIST_SIZE;
    s->remote_settings[1] = H2_DEFAULT_HEADER_TABLE_SIZE;
    s->remote_settings[2] = H2_DEFAULT_ENABLE_PUSH;
    s->remote_settings[3] = H2_DEFAULT_MAX_CONCURRENT_STREAMS;
    s->remote_settings[4] = H2_DEFAULT_INITIAL_WINDOW_SIZE;
    s->remote_settings[5] = H2_DEFAULT_MAX_FRAME_SIZE;
    s->remote_settings[6] = H2_DEFAULT_MAX_HEADER_LIST_SIZE;
    s->local_window_size = H2_DEFAULT_INITIAL_WINDOW_SIZE;
    s->remote_window_size = H2_DEFAULT_INITIAL_WINDOW_SIZE;

    for (int i = 0; i < H2SESSION_CB_MAX; i++)
        s->callbacks[i] = JS_UNDEFINED;

    JS_SetOpaque(obj, s);
    if (tjs_h2_pin_value(ctx, "session", s->gc_root_id, obj) < 0) {
        JS_SetOpaque(obj, NULL);
        JS_FreeValue(ctx, obj);
        tjs__free(s);
        return JS_EXCEPTION;
    }
    return obj;
}

/* ------------------------------------------------------------------
 * Finalizer / GC mark
 * ------------------------------------------------------------------ */

static void tjs_h2session_finalizer(JSRuntime *rt, JSValue val)
{
    TJSH2Session *s = JS_GetOpaque(val, tjs_h2session_class_id);
    if (!s)
        return;

    TJS_H2_DEBUGF("[h2session:%p] finalizer wsi=%p this=%d active_streams=%d\n",
                  (void *) s, (void *) s->wsi,
                  !JS_IsUndefined(s->this_val),
                  !JS_IsUndefined(s->active_streams));
    tjs_h2_unpin_value(s->ctx, "session", &s->gc_root_id);

    /* Kill the wsi if still alive */
    if (s->wsi) {
        lws_set_timeout(s->wsi, PENDING_TIMEOUT_USER_OK, LWS_TO_KILL_SYNC);
        lws_set_wsi_user(s->wsi, NULL);
        s->wsi = NULL;
    }

    /* Free all JS callback references */
    for (int i = 0; i < H2SESSION_CB_MAX; i++)
        JS_FreeValueRT(rt, s->callbacks[i]);

    JS_FreeValueRT(rt, s->this_val);
    JS_FreeValueRT(rt, s->socket_obj);
    JS_FreeValueRT(rt, s->ping.callback);
    JS_FreeValueRT(rt, s->active_streams);
    if (s->connect_host)
        js_free_rt(rt, s->connect_host);
    if (s->connect_path)
        js_free_rt(rt, s->connect_path);

    tjs__free(s);
}

static void tjs_h2session_mark(JSRuntime *rt, JSValue val, JS_MarkFunc *mark_func)
{
    TJSH2Session *s = JS_GetOpaque(val, tjs_h2session_class_id);
    if (!s)
        return;

    for (int i = 0; i < H2SESSION_CB_MAX; i++)
        JS_MarkValue(rt, s->callbacks[i], mark_func);

    JS_MarkValue(rt, s->this_val, mark_func);
    JS_MarkValue(rt, s->socket_obj, mark_func);
    JS_MarkValue(rt, s->ping.callback, mark_func);
    JS_MarkValue(rt, s->active_streams, mark_func);
}

/* ------------------------------------------------------------------
 * Methods
 * ------------------------------------------------------------------ */

/* Helper: initialize authority state and create H2 transport anchor connection */
static JSValue tjs_h2session_do_connect(TJSH2Session *s, const char *host, int port,
                                     const char *path, int use_ssl)
{
    JSContext *ctx = s->ctx;

    if (s->wsi || s->connecting || s->connect_host) {
        return JS_ThrowTypeError(ctx, "Session already connecting or connected");
    }

    s->encrypted = (use_ssl != 0);
    s->connecting = true;
    s->connect_port = port;
    s->connect_ssl = use_ssl;
    js_free(ctx, s->connect_host);
    js_free(ctx, s->connect_path);
    s->connect_host = js_strdup(ctx, host);
    /* For anchor: use the session path (typically "/" for authority-level connect) */
    s->connect_path = js_strdup(ctx, path ? path : "/");
    if (!s->connect_host || !s->connect_path) {
        s->connecting = false;
        return JS_ThrowOutOfMemory(ctx);
    }

    if (JS_IsUndefined(s->socket_obj)) {
        s->socket_obj = JS_NewObject(ctx);
        if (JS_IsException(s->socket_obj)) {
            s->connecting = false;
            return JS_EXCEPTION;
        }
    }

    if (JS_IsUndefined(s->active_streams)) {
        s->active_streams = JS_NewObject(ctx);
        if (JS_IsException(s->active_streams)) {
            s->connecting = false;
            return JS_EXCEPTION;
        }
    }

    /*
     * R4: Create a real lws H2 transport connection (anchor).
     *
     * This anchor connection is used as the underlying TCP/TLS connection
     * for HTTP/2 multiplexing. When session.request() creates new streams,
     * lws will route them as substreams on this existing H2 connection
     * via lws_vhost_active_conn_find() matching on host:port:alpn.
     *
     * The anchor uses a minimal HTTP transaction (GET to the authority path).
     * After the anchor's transaction completes, the H2 connection remains
     * available for multiplexing subsequent streams.
     */
    struct lws_context *lws_ctx = tjs_h2session_get_lws_ctx(ctx);
    if (!lws_ctx) {
        s->connecting = false;
        return JS_ThrowTypeError(ctx, "HTTP/2: lws context not available");
    }

    uint32_t ssl_flags = 0;
    if (use_ssl) {
        ssl_flags |= LCCSCF_USE_SSL;
    } else {
        /* h2c (cleartext HTTP/2) requires prior knowledge flag */
        ssl_flags |= LCCSCF_H2_PRIOR_KNOWLEDGE;
    }
    ssl_flags |= LCCSCF_HTTP_NO_FOLLOW_REDIRECT;
    ssl_flags |= LCCSCF_H2_QUIRK_OVERFLOWS_TXCR;
    ssl_flags |= LCCSCF_H2_QUIRK_NGHTTP2_END_STREAM;

    struct lws_client_connect_info cci;
    memset(&cci, 0, sizeof(cci));
    cci.context = lws_ctx;
    cci.address = s->connect_host;
    cci.port = s->connect_port;
    cci.path = s->connect_path;
    cci.host = s->connect_host;
    cci.origin = s->connect_host;
    cci.method = "GET";  /* minimal anchor transaction */
    cci.alpn = "h2";
    cci.protocol = "tjs-h2session";  /* use session protocol for anchor */
    cci.local_protocol_name = tjs_h2session_protocol.name;
    cci.userdata = s;
    cci.pwsi = &s->wsi;
    cci.ssl_connection = ssl_flags;

    tjs__lws_conn_ref(ctx);
    tjs_h2session_loop_ref(s);

    struct lws *wsi = lws_client_connect_via_info(&cci);
    if (!wsi) {
        lwsl_err("H2Session: lws_client_connect_via_info() failed for anchor\n");
        tjs__lws_conn_unref(ctx);
        tjs_h2session_loop_unref(s);
        s->connecting = false;
        return JS_ThrowInternalError(ctx, "Failed to create H2 session connection");
    }

    lwsl_info("H2Session: anchor wsi=%p created for %s:%d\n",
              (void *)wsi, s->connect_host, s->connect_port);
    lws_set_wsi_user(wsi, s);
    s->wsi = wsi;

    /* Kick the shared lws/libuv loop so the new anchor starts promptly. */
    lws_cancel_service(lws_ctx);

    return JS_UNDEFINED;
}

/* ------------------------------------------------------------------
 * session.connect(host, port, path, secure)
 * ------------------------------------------------------------------ */

static JSValue tjs_h2session_connect(JSContext *ctx, JSValue this_val,
                                      int argc, JSValue *argv)
{
    TJSH2Session *s = JS_GetOpaque2(ctx, this_val, tjs_h2session_class_id);
    if (!s)
        return JS_EXCEPTION;

    const char *host = JS_ToCString(ctx, argv[0]);
    if (!host)
        return JS_EXCEPTION;

    int32_t port = 443;
    if (argc > 1)
        JS_ToInt32(ctx, &port, argv[1]);

    const char *path = "/";
    bool has_path = argc > 2 && !JS_IsUndefined(argv[2]) && !JS_IsNull(argv[2]);
    if (has_path)
        path = JS_ToCString(ctx, argv[2]);

    int secure = 1;
    if (argc > 3)
        secure = JS_ToBool(ctx, argv[3]);

    JSValue ret = tjs_h2session_do_connect(s, host, (int) port, path, secure);

    JS_FreeCString(ctx, host);
    if (has_path)
        JS_FreeCString(ctx, path);

    return ret;
}

/* ------------------------------------------------------------------
 * session.ping(callback)
 * ------------------------------------------------------------------ */

static JSValue tjs_h2session_ping(JSContext *ctx, JSValue this_val,
                                   int argc, JSValue *argv)
{
    TJSH2Session *s = JS_GetOpaque2(ctx, this_val, tjs_h2session_class_id);
    if (!s)
        return JS_EXCEPTION;

    if (argc < 1 || !JS_IsFunction(ctx, argv[0]))
        return TJS_THROW_ARG_ERR(ctx, 0, "function");

    /* Store callback */
    JS_FreeValue(ctx, s->ping.callback);
    s->ping.callback = JS_DupValue(ctx, argv[0]);
    s->ping.pending = true;

    /* Generate unique payload */
    int64_t now = uv_now(tjs_get_loop(ctx));
    memcpy(s->ping.payload, &now, 8);

    /* Trigger writeable callback to send the ping */
    if (s->wsi) {
        lws_callback_on_writable(s->wsi);
    }

    return JS_UNDEFINED;
}

/* ------------------------------------------------------------------
 * session.setLocalWindowSize(size)
 * ------------------------------------------------------------------ */

static JSValue tjs_h2session_set_local_window_size(JSContext *ctx, JSValue this_val,
                                                    int argc, JSValue *argv)
{
    TJSH2Session *s = JS_GetOpaque2(ctx, this_val, tjs_h2session_class_id);
    if (!s)
        return JS_EXCEPTION;

    int32_t size;
    if (JS_ToInt32(ctx, &size, argv[0]) < 0)
        return JS_EXCEPTION;

    s->local_window_size = (uint32_t) size;
    s->local_settings[4] = (uint32_t) size;

    /* Emit localSettings event */
    JSValue settings = tjs_h2session_make_settings_obj(ctx, s->local_settings);
    if (!JS_IsException(settings)) {
        JSValue args[1];
        args[0] = settings;
        tjs_h2session_push_event(s, H2SESSION_CB_LOCAL_SETTINGS, 1, args);
    }

    /* TODO: send SETTINGS frame to peer via lws */

    return JS_UNDEFINED;
}

/* ------------------------------------------------------------------
 * session.incrementWindowSize(delta)
 * ------------------------------------------------------------------ */

static JSValue tjs_h2session_increment_window_size(JSContext *ctx, JSValue this_val,
                                                    int argc, JSValue *argv)
{
    TJSH2Session *s = JS_GetOpaque2(ctx, this_val, tjs_h2session_class_id);
    if (!s)
        return JS_EXCEPTION;

    int32_t delta;
    if (JS_ToInt32(ctx, &delta, argv[0]) < 0)
        return JS_EXCEPTION;

    s->local_window_size += (uint32_t) delta;
    s->local_settings[4] = s->local_window_size;

    /* Emit localSettings event */
    JSValue settings = tjs_h2session_make_settings_obj(ctx, s->local_settings);
    if (!JS_IsException(settings)) {
        JSValue args[1];
        args[0] = settings;
        tjs_h2session_push_event(s, H2SESSION_CB_LOCAL_SETTINGS, 1, args);
    }

    return JS_UNDEFINED;
}

/* ------------------------------------------------------------------
 * session.ref() / session.unref()
 * ------------------------------------------------------------------ */

static JSValue tjs_h2session_ref_method(JSContext *ctx, JSValue this_val,
                                  int argc, JSValue *argv)
{
    (void) argc; (void) argv;
    TJSH2Session *s = JS_GetOpaque2(ctx, this_val, tjs_h2session_class_id);
    if (!s)
        return JS_EXCEPTION;
    tjs_h2session_loop_ref(s);
    return JS_UNDEFINED;
}

static JSValue tjs_h2session_unref_method(JSContext *ctx, JSValue this_val,
                                    int argc, JSValue *argv)
{
    (void) argc; (void) argv;
    TJSH2Session *s = JS_GetOpaque2(ctx, this_val, tjs_h2session_class_id);
    if (!s)
        return JS_EXCEPTION;
    tjs_h2session_loop_unref(s);
    return JS_UNDEFINED;
}

/* ------------------------------------------------------------------
 * session.close()
 * ------------------------------------------------------------------ */

static JSValue tjs_h2session_close(JSContext *ctx, JSValue this_val,
                                    int argc, JSValue *argv)
{
    (void) argc; (void) argv;
    TJSH2Session *s = JS_GetOpaque2(ctx, this_val, tjs_h2session_class_id);
    if (!s)
        return JS_EXCEPTION;

    if (s->closed || s->destroyed || !s->wsi) {
        return JS_UNDEFINED;
    }

    /*
     * grpc-js uses session.close() when a transport is shutting down.
     * In our current client-only model there is no long-lived session reuse
     * requirement after close(), so release the transport immediately.
     * Relying on the h2 anchor close callback is not sufficient here because
     * the anchor stream close does not reliably map to full transport teardown.
     */
    s->closed = true;
    s->connected = false;
    s->connecting = false;
    s->anchor_completed = true;

    tjs_h2session_finish_all_streams(s);
    tjs_h2session_clear_stream_registry(s);
    tjs_h2session_close_transport(s, LWS_TO_KILL_SYNC);
    tjs_h2session_push_event(s, H2SESSION_CB_CLOSE, 0, NULL);
    tjs_h2session_release_self(s);

    return JS_UNDEFINED;
}

/* ------------------------------------------------------------------
 * session.destroy()
 * ------------------------------------------------------------------ */

static JSValue tjs_h2session_destroy(JSContext *ctx, JSValue this_val,
                                      int argc, JSValue *argv)
{
    TJSH2Session *s = JS_GetOpaque2(ctx, this_val, tjs_h2session_class_id);
    if (!s)
        return JS_EXCEPTION;

    if (s->destroyed)
        return JS_UNDEFINED;

    bool was_closed = s->closed;
    s->destroyed = true;
    s->closed = true;
    s->connected = false;
    s->connecting = false;
    s->anchor_completed = true;

    tjs_h2session_finish_all_streams(s);
    tjs_h2session_clear_stream_registry(s);
    tjs_h2session_close_transport(s, LWS_TO_KILL_SYNC);

    /* Emit error if an error object was passed */
    if (argc > 0 && !JS_IsUndefined(argv[0]) && !JS_IsNull(argv[0])) {
        JSValue args[1];
        args[0] = JS_DupValue(ctx, argv[0]);
        tjs_h2session_push_event(s, H2SESSION_CB_ERROR, 1, args);
    }

    /* Node-compatible behavior: destroy() should lead to session close event. */
    if (!was_closed)
        tjs_h2session_push_event(s, H2SESSION_CB_CLOSE, 0, NULL);

    tjs_h2session_release_self(s);

    return JS_UNDEFINED;
}

/* ------------------------------------------------------------------
 * H2Stream class registration
 * ------------------------------------------------------------------ */

static JSValue tjs_h2stream_push_event(TJSH2Stream *st, int cb_idx,
                                       int argc, JSValue *argv);

static const char *tjs_h2session_cb_name(int cb_idx);
static const char *tjs_h2stream_cb_name(int cb_idx);
static const char *tjs_h2stream_cb_prop_name(int cb_idx);

static void tjs_h2stream_loop_ref(TJSH2Stream *st)
{
    if (st->ref_count++ == 0) {
        TJSRuntime *qrt = TJS_GetRuntime(st->ctx);
        uv_ref((uv_handle_t *) &qrt->lws.keepalive);
    }
}

static void tjs_h2stream_loop_unref(TJSH2Stream *st)
{
    if (st->ref_count > 0 && --st->ref_count == 0) {
        TJSRuntime *qrt = TJS_GetRuntime(st->ctx);
        uv_unref((uv_handle_t *) &qrt->lws.keepalive);
    }
}

/* ------------------------------------------------------------------
 * H2Stream constructor (internal, called from session.request)
 * ------------------------------------------------------------------ */

static JSValue tjs_h2stream_new(JSContext *ctx, TJSH2Session *session, int stream_id)
{
    JSValue obj = JS_NewObjectClass(ctx, tjs_h2stream_class_id);
    if (JS_IsException(obj))
        return obj;

    TJSH2Stream *st = tjs__mallocz(sizeof(*st));
    if (!st) {
        JS_FreeValue(ctx, obj);
        return JS_ThrowOutOfMemory(ctx);
    }

    st->ctx = ctx;
    st->this_val = JS_DupValue(ctx, obj);
    st->session = session;
    st->session_val = JS_DupValue(ctx, session->this_val);
    st->gc_root_id = tjs_h2_next_root_id++;
    st->wsi = NULL;
    st->stream_id = stream_id;
    st->closed = false;
    st->destroyed = false;
    st->pending = (stream_id == 0);
    st->half_closed_local = false;
    st->half_closed_remote = false;
    st->response_received = false;
    st->trailers_received = false;
    st->rst_code = 0;
    st->paused = false;
    st->local_window_size = session->local_window_size;
    st->remote_window_size = session->remote_window_size;
    st->end_after_headers = false;
    st->headers_sent = false;
    st->write_ended = false;
    st->body_pending = false;
    st->completed = false;
    st->final_sent = false;
    st->sent_headers = JS_UNDEFINED;
    st->sent_trailers = JS_UNDEFINED;
    st->method = NULL;
    st->path = NULL;
    st->authority = NULL;
    st->scheme = NULL;
    st->connect_host = NULL;
    st->connect_port = 0;
    st->ssl_flags = 0;
    tjs_dbuf_init(ctx, &st->req_headers);
    tjs_dbuf_init(ctx, &st->send_buf);
    st->read_buf = NULL;
    st->read_buf_len = 0;
    st->read_buf_pos = 0;
    st->ref_count = 0;

    for (int i = 0; i < H2STREAM_CB_MAX; i++)
        st->callbacks[i] = JS_UNDEFINED;

    st->pending_write_cb = JS_UNDEFINED;

    JS_SetOpaque(obj, st);
    if (tjs_h2_pin_value(ctx, "stream", st->gc_root_id, obj) < 0) {
        JS_SetOpaque(obj, NULL);
        JS_FreeValue(ctx, obj);
        tjs__free(st);
        return JS_EXCEPTION;
    }

    /* Ref the loop for this stream */
    tjs_h2stream_loop_ref(st);

    return obj;
}

/* ------------------------------------------------------------------
 * H2Stream finalizer / GC mark
 * ------------------------------------------------------------------ */

static void tjs_h2stream_finalizer(JSRuntime *rt, JSValue val)
{
    TJSH2Stream *st = JS_GetOpaque(val, tjs_h2stream_class_id);
    if (!st)
        return;

    TJS_H2_DEBUGF("[h2stream:%d] finalizer st=%p session=%p wsi=%p completed=%d closed=%d destroyed=%d\n",
                  st->stream_id, (void *) st, (void *) st->session, (void *) st->wsi,
                  st->completed, st->closed, st->destroyed);
    tjs_h2_unpin_value(st->ctx, "stream", &st->gc_root_id);

    tjs_h2stream_loop_unref(st);

    /* Free all JS callback references */
    for (int i = 0; i < H2STREAM_CB_MAX; i++)
        JS_FreeValueRT(rt, st->callbacks[i]);

    JS_FreeValueRT(rt, st->pending_write_cb);
    JS_FreeValueRT(rt, st->this_val);
    JS_FreeValueRT(rt, st->session_val);
    JS_FreeValueRT(rt, st->sent_headers);
    JS_FreeValueRT(rt, st->sent_trailers);
    if (st->wsi)
        lws_set_wsi_user(st->wsi, NULL);
    if (st->method)
        js_free_rt(rt, st->method);
    if (st->path)
        js_free_rt(rt, st->path);
    if (st->authority)
        js_free_rt(rt, st->authority);
    if (st->scheme)
        js_free_rt(rt, st->scheme);
    if (st->connect_host)
        js_free_rt(rt, st->connect_host);
    dbuf_free(&st->req_headers);
    dbuf_free(&st->send_buf);

    if (st->read_buf)
        tjs__free(st->read_buf);

    tjs__free(st);
}

static void tjs_h2stream_mark(JSRuntime *rt, JSValue val, JS_MarkFunc *mark_func)
{
    TJSH2Stream *st = JS_GetOpaque(val, tjs_h2stream_class_id);
    if (!st)
        return;

    for (int i = 0; i < H2STREAM_CB_MAX; i++)
        JS_MarkValue(rt, st->callbacks[i], mark_func);

    JS_MarkValue(rt, st->pending_write_cb, mark_func);
    JS_MarkValue(rt, st->this_val, mark_func);
    JS_MarkValue(rt, st->session_val, mark_func);
    JS_MarkValue(rt, st->sent_headers, mark_func);
    JS_MarkValue(rt, st->sent_trailers, mark_func);
}

/* ------------------------------------------------------------------
 * H2Stream event push helper
 * ------------------------------------------------------------------ */

static const char *tjs_h2stream_cb_name(int cb_idx)
{
    switch (cb_idx) {
    case H2STREAM_CB_RESPONSE:
        return "response";
    case H2STREAM_CB_TRAILERS:
        return "trailers";
    case H2STREAM_CB_DATA:
        return "data";
    case H2STREAM_CB_END:
        return "end";
    case H2STREAM_CB_ERROR:
        return "error";
    case H2STREAM_CB_CLOSE:
        return "close";
    default:
        return "unknown";
    }
}

static const char *tjs_h2stream_cb_prop_name(int cb_idx)
{
    switch (cb_idx) {
    case H2STREAM_CB_RESPONSE:
        return "__tjs_h2_cb_response";
    case H2STREAM_CB_TRAILERS:
        return "__tjs_h2_cb_trailers";
    case H2STREAM_CB_DATA:
        return "__tjs_h2_cb_data";
    case H2STREAM_CB_END:
        return "__tjs_h2_cb_end";
    case H2STREAM_CB_ERROR:
        return "__tjs_h2_cb_error";
    case H2STREAM_CB_CLOSE:
        return "__tjs_h2_cb_close";
    default:
        return "__tjs_h2_cb_unknown";
    }
}

static JSValue tjs_h2stream_get_callback(TJSH2Stream *st, int cb_idx)
{
    if (JS_IsUndefined(st->this_val))
        return JS_UNDEFINED;

    return JS_GetPropertyStr(st->ctx, st->this_val, tjs_h2stream_cb_prop_name(cb_idx));
}

static int tjs_h2stream_set_callback_value(TJSH2Stream *st, int cb_idx, JSValue value)
{
    if (JS_IsUndefined(st->this_val))
        return -1;

    return JS_SetPropertyStr(st->ctx, st->this_val,
                             tjs_h2stream_cb_prop_name(cb_idx),
                             JS_DupValue(st->ctx, value));
}

static JSValue tjs_h2stream_take_pending_write_cb(TJSH2Stream *st)
{
    JSContext *ctx = st->ctx;

    if (JS_IsUndefined(st->this_val))
        return JS_UNDEFINED;

    JSValue cb = JS_GetPropertyStr(ctx, st->this_val, "__tjs_h2_pending_write_cb");
    JS_SetPropertyStr(ctx, st->this_val, "__tjs_h2_pending_write_cb", JS_UNDEFINED);
    return cb;
}

static int tjs_h2stream_set_pending_write_cb(TJSH2Stream *st, JSValue value)
{
    if (JS_IsUndefined(st->this_val))
        return -1;

    return JS_SetPropertyStr(st->ctx, st->this_val, "__tjs_h2_pending_write_cb",
                             JS_DupValue(st->ctx, value));
}

static void tjs_h2stream_clear_callback_refs(TJSH2Stream *st)
{
    if (JS_IsUndefined(st->this_val))
        return;

    JSContext *ctx = st->ctx;
    for (int i = 0; i < H2STREAM_CB_MAX; i++) {
        JS_SetPropertyStr(ctx, st->this_val, tjs_h2stream_cb_prop_name(i), JS_UNDEFINED);
    }
    JS_SetPropertyStr(ctx, st->this_val, "__tjs_h2_pending_write_cb", JS_UNDEFINED);
}

static JSValue tjs_h2stream_push_event(TJSH2Stream *st, int cb_idx,
                                        int argc, JSValue *argv)
{
    JSContext *ctx = st->ctx;
    JSValue func = tjs_h2stream_get_callback(st, cb_idx);

    if (JS_IsFunction(ctx, func)) {
        TJS_H2_DEBUGF("[h2stream:%d] push_event %s argc=%d completed=%d closed=%d destroyed=%d pending=%d\n",
                      st->stream_id, tjs_h2stream_cb_name(cb_idx), argc, st->completed,
                      st->closed, st->destroyed, st->pending);
        tjs_call_handler(ctx, func, argc, argv);
        TJS_H2_DEBUGF("[h2stream:%d] push_event %s returned\n",
                      st->stream_id, tjs_h2stream_cb_name(cb_idx));
    }
    JS_FreeValue(ctx, func);

    /* Free argv */
    for (int i = 0; i < argc; i++) {
        JS_FreeValue(ctx, argv[i]);
    }

    return JS_UNDEFINED;
}

static void tjs_h2stream_emit_end(TJSH2Stream *st)
{
    if (st->half_closed_remote)
        return;

    st->half_closed_remote = true;
    TJS_H2_DEBUGF("[h2stream:%d] emit end\n", st->stream_id);
    tjs_h2stream_push_event(st, H2STREAM_CB_END, 0, NULL);
}

static void tjs_h2stream_finish(TJSH2Stream *st, bool emit_end, bool emit_close)
{
    if (st->completed)
        return;

    TJS_H2_DEBUGF("[h2stream:%d] finish begin st=%p session=%p wsi=%p emit_end=%d emit_close=%d\n",
                  st->stream_id, (void *) st, (void *) st->session, (void *) st->wsi,
                  emit_end, emit_close);

    st->completed = true;
    st->closed = true;

    if (emit_end)
        tjs_h2stream_emit_end(st);
    if (emit_close)
        TJS_H2_DEBUGF("[h2stream:%d] emit close\n", st->stream_id);
    if (emit_close)
        tjs_h2stream_push_event(st, H2STREAM_CB_CLOSE, 0, NULL);
    if (emit_close)
        TJS_H2_DEBUGF("[h2stream:%d] close handler returned\n", st->stream_id);

    /*
     * No further user-visible callbacks should fire after finish(). Clearing
     * these refs breaks the native-stream <-> JS-wrapper callback cycle so
     * completed request objects can be reclaimed promptly instead of piling up
     * until the session itself dies.
     */
    tjs_h2stream_clear_callback_refs(st);

    if (st->session && st->stream_id > 0) {
        TJS_H2_DEBUGF("[h2stream:%d] unregister begin session=%p\n",
                      st->stream_id, (void *) st->session);
        tjs_h2session_unregister_stream(st->session, st->stream_id);
        TJS_H2_DEBUGF("[h2stream:%d] unregister done\n", st->stream_id);
    }

    tjs_h2stream_detach_transport(st, LWS_TO_KILL_SYNC);
    tjs_h2stream_loop_unref(st);
    tjs_h2stream_release_self(st);
    TJS_H2_DEBUGF("[h2stream:%d] finish done\n", st->stream_id);
}

static void tjs_h2stream_release_self(TJSH2Stream *st)
{
    if (JS_IsUndefined(st->this_val))
        return;

    TJS_H2_DEBUGF("[h2stream:%d] release self st=%p session=%p wsi=%p\n",
                  st->stream_id, (void *) st, (void *) st->session, (void *) st->wsi);
    tjs_h2_unpin_value(st->ctx, "stream", &st->gc_root_id);
    JSValue val = st->this_val;
    st->this_val = JS_UNDEFINED;
    JS_FreeValue(st->ctx, val);
    TJS_H2_DEBUGF("[h2stream:%d] release self done\n", st->stream_id);
}

static void tjs_h2stream_detach_transport(TJSH2Stream *st, int kill_mode)
{
    if (!st->wsi)
        return;

    /*
     * The first stream on a client session doubles as the current transport
     * anchor. Let session teardown own that wsi lifecycle.
     */
    if (st->session && st->session->wsi == st->wsi)
        return;

    struct lws *wsi = st->wsi;
    lws_set_wsi_user(wsi, NULL);
    st->wsi = NULL;
    tjs__lws_conn_unref(st->ctx);
    lws_set_timeout(wsi, PENDING_TIMEOUT_USER_OK, kill_mode);
}

static void tjs_h2stream_adopt_callback_wsi(TJSH2Stream *st, struct lws *wsi,
                                            enum lws_callback_reasons reason)
{
    if (!st || !wsi || st->wsi == wsi)
        return;

    switch (reason) {
    case LWS_CALLBACK_RECEIVE_CLIENT_HTTP:
    case LWS_CALLBACK_RECEIVE_CLIENT_HTTP_READ:
    case LWS_CALLBACK_ESTABLISHED_CLIENT_HTTP:
    case LWS_CALLBACK_COMPLETED_CLIENT_HTTP:
    case LWS_CALLBACK_CLOSED_CLIENT_HTTP:
    case LWS_CALLBACK_CLOSED:
        TJS_H2_DEBUGF("[h2stream:%d] adopt callback wsi old=%p new=%p reason=%d\n",
                      st->stream_id, (void *) st->wsi, (void *) wsi, (int) reason);
        if (st->wsi)
            lws_set_wsi_user(st->wsi, NULL);
        st->wsi = wsi;
        lws_set_wsi_user(wsi, st);
        break;
    default:
        break;
    }
}

/* Helper callback for lws_hdr_custom_name_foreach - captures custom headers into a JSValue */
typedef struct {
    JSContext   *ctx;
    JSValue      headers;
    struct lws  *wsi;
    bool         strip_trailing_colon;
    bool        *saw_header;
    int        (*copy)(struct lws *wsi, char *dst, int len, const char *name, int nlen);
} tjs_custom_hdr_ctx;

static void tjs_h2stream_custom_hdr_cb(const char *name, int nlen, void *opaque)
{
    tjs_custom_hdr_ctx *hctx = (tjs_custom_hdr_ctx *) opaque;
    JSContext *ctx = hctx->ctx;
    JSValue headers = hctx->headers;
    struct lws *wsi = hctx->wsi;

    char hdr_name[256];
    int real_len = nlen;
    if (hctx->strip_trailing_colon && real_len > 0 && name[real_len - 1] == ':')
        real_len--;
    if (real_len <= 0 || real_len >= (int) sizeof(hdr_name))
        return;
    memcpy(hdr_name, name, real_len);
    hdr_name[real_len] = '\0';

    char val[4096];
    int vlen = hctx->copy(wsi, val, sizeof(val), name, nlen);
    if (vlen > 0) {
        if (hctx->saw_header)
            *hctx->saw_header = true;
        TJS_H2_DEBUGF("[h2stream] %sheader %s: %s\n",
                      hctx->strip_trailing_colon ? "" : "trailer-",
                      hdr_name, val);
        JS_DefinePropertyValueStr(ctx, headers, hdr_name,
            JS_NewString(ctx, val), JS_PROP_C_W_E);
    }
}

static bool tjs_h2stream_fire_response(TJSH2Stream *st, struct lws *wsi)
{
    if (st->response_received)
        return false;

    JSContext *ctx = st->ctx;
    JSValue headers = JS_NewObject(ctx);
    if (JS_IsException(headers))
        return false;

    int status = (int) lws_http_client_http_response(wsi);
    JS_DefinePropertyValueStr(ctx, headers, ":status", JS_NewInt32(ctx, status), JS_PROP_C_W_E);

    /* Known token headers */
    for (int n = 0; n < WSI_TOKEN_COUNT; n++) {
        const unsigned char *tok_name = lws_token_to_string(n);
        if (!tok_name)
            continue;
        const char *tn = (const char *) tok_name;
        int tn_len = (int) strlen(tn);
        if (tn_len == 0 || tn[tn_len - 1] != ':')
            continue;
        if (lws_hdr_total_length(wsi, n) <= 0)
            continue;

        char val[4096];
        if (lws_hdr_copy(wsi, val, sizeof(val), n) < 0)
            continue;

        char header_name[128];
        int header_len = tn_len - 1;
        if (header_len <= 0)
            continue;
        if (header_len >= (int) sizeof(header_name))
            header_len = (int) sizeof(header_name) - 1;
        memcpy(header_name, tn, header_len);
        header_name[header_len] = '\0';
        JS_DefinePropertyValueStr(ctx, headers, header_name, JS_NewString(ctx, val), JS_PROP_C_W_E);
    }

    /* Custom/unknown headers not recognized by lws */
    tjs_custom_hdr_ctx hctx;
    hctx.ctx = ctx;
    hctx.headers = headers;
    hctx.wsi = wsi;
    hctx.strip_trailing_colon = true;
    hctx.saw_header = NULL;
    hctx.copy = lws_hdr_custom_copy;
    lws_hdr_custom_name_foreach(wsi, tjs_h2stream_custom_hdr_cb, &hctx);

    JSValue grpc_status = JS_GetPropertyStr(ctx, headers, "grpc-status");
    bool has_grpc_status = JS_IsString(grpc_status);
    JS_FreeValue(ctx, grpc_status);

    st->response_received = true;
    JSValue args[3];
    args[0] = headers;
    args[1] = JS_NewInt32(ctx, 0);
    args[2] = JS_UNDEFINED;
    tjs_h2stream_push_event(st, H2STREAM_CB_RESPONSE, 3, args);
    return has_grpc_status;
}

static bool tjs_h2stream_fire_trailers(TJSH2Stream *st, struct lws *wsi)
{
    if (st->trailers_received)
        return false;

    JSContext *ctx = st->ctx;
    JSValue headers = JS_NewObject(ctx);
    if (JS_IsException(headers))
        return false;

    bool saw_header = false;
    tjs_custom_hdr_ctx hctx;
    hctx.ctx = ctx;
    hctx.headers = headers;
    hctx.wsi = wsi;
    hctx.strip_trailing_colon = true;
    hctx.saw_header = &saw_header;
    hctx.copy = lws_hdr_h2_trailer_copy;
    lws_hdr_h2_trailer_name_foreach(wsi, tjs_h2stream_custom_hdr_cb, &hctx);

    /*
     * grpc-js error paths may arrive as headers-only END_STREAM, with
     * grpc-status / grpc-message present on the response header block
     * instead of a separate trailing header block. Preserve those as a
     * trailers event so JS sees the final gRPC status.
     */
    if (!saw_header) {
        hctx.copy = lws_hdr_custom_copy;
        lws_hdr_custom_name_foreach(wsi, tjs_h2stream_custom_hdr_cb, &hctx);

        JSValue grpc_status = JS_GetPropertyStr(ctx, headers, "grpc-status");
        bool has_grpc_status = JS_IsString(grpc_status);
        JS_FreeValue(ctx, grpc_status);
        if (!has_grpc_status) {
            JS_FreeValue(ctx, headers);
            return false;
        }
    }

    st->trailers_received = true;
    JSValue args[2];
    args[0] = headers;
    args[1] = JS_NewInt32(ctx, 0);
    tjs_h2stream_push_event(st, H2STREAM_CB_TRAILERS, 2, args);
    return true;
}

static int tjs_h2stream_add_header(JSContext *ctx, TJSH2Stream *st, const char *name, JSValue value)
{
    if (!name || name[0] == '\0' || name[0] == ':')
        return 0;
    if (!strcmp(name, "undefined"))
        return 0;

    const char *str = JS_ToCString(ctx, value);
    if (!str)
        return -1;

    int r = dbuf_printf(&st->req_headers, "%s: %s\r\n", name, str);
    JS_FreeCString(ctx, str);
    if (r) {
        JS_ThrowOutOfMemory(ctx);
        return -1;
    }
    return 0;
}

static char *tjs_h2stream_dup_js_string(JSContext *ctx, JSValue value)
{
    const char *str = JS_ToCString(ctx, value);
    if (!str)
        return NULL;
    char *dup = js_strdup(ctx, str);
    JS_FreeCString(ctx, str);
    return dup;
}

static char *tjs_h2stream_make_default_authority(JSContext *ctx, TJSH2Stream *st)
{
    const char *host = st->session->connect_host ? st->session->connect_host : "";
    int port = st->session->connect_port;
    bool secure = st->session->connect_ssl != 0;
    bool is_default_port = (secure && port == 443) || (!secure && port == 80) || port == 0;

    if (is_default_port)
        return js_strdup(ctx, host);

    size_t host_len = strlen(host);
    char port_buf[16];
    int port_len = snprintf(port_buf, sizeof(port_buf), ":%d", port);
    if (port_len <= 0)
        return js_strdup(ctx, host);

    char *authority = js_malloc(ctx, host_len + (size_t) port_len + 1);
    if (!authority)
        return NULL;

    memcpy(authority, host, host_len);
    memcpy(authority + host_len, port_buf, (size_t) port_len);
    authority[host_len + (size_t) port_len] = '\0';

    return authority;
}

static int tjs_h2stream_parse_headers(JSContext *ctx, TJSH2Stream *st, JSValue headers_obj)
{
    st->method = js_strdup(ctx, "POST");
    st->path = js_strdup(ctx, "/");
    st->scheme = js_strdup(ctx, st->session->connect_ssl ? "https" : "http");
    st->authority = tjs_h2stream_make_default_authority(ctx, st);
    if (!st->method || !st->path || !st->scheme || !st->authority)
        return -1;

    /* Build sent_headers object with pseudo-headers first */
    JSValue sent_hdrs = JS_NewObject(ctx);
    if (JS_IsException(sent_hdrs))
        return -1;
    JS_DefinePropertyValueStr(ctx, sent_hdrs, ":method", JS_NewString(ctx, st->method), JS_PROP_C_W_E);
    JS_DefinePropertyValueStr(ctx, sent_hdrs, ":path", JS_NewString(ctx, st->path), JS_PROP_C_W_E);
    JS_DefinePropertyValueStr(ctx, sent_hdrs, ":scheme", JS_NewString(ctx, st->scheme), JS_PROP_C_W_E);
    JS_DefinePropertyValueStr(ctx, sent_hdrs, ":authority", JS_NewString(ctx, st->authority), JS_PROP_C_W_E);

    if (JS_IsUndefined(headers_obj) || JS_IsNull(headers_obj)) {
        st->sent_headers = sent_hdrs;
        return 0;
    }

    JSPropertyEnum *tab = NULL;
    uint32_t len = 0;
    if (JS_GetOwnPropertyNames(ctx, &tab, &len, headers_obj, JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY)) {
        JS_FreeValue(ctx, sent_hdrs);
        return -1;
    }

    for (uint32_t i = 0; i < len; i++) {
        const char *name = JS_AtomToCString(ctx, tab[i].atom);
        if (!name)
            continue;

        JSValue value = JS_GetProperty(ctx, headers_obj, tab[i].atom);
        if (JS_IsException(value)) {
            JS_FreeCString(ctx, name);
            continue;
        }

        if (!strcmp(name, ":method")) {
            js_free(ctx, st->method);
            st->method = tjs_h2stream_dup_js_string(ctx, value);
            JS_DefinePropertyValueStr(ctx, sent_hdrs, ":method", JS_DupValue(ctx, value), JS_PROP_C_W_E);
        } else if (!strcmp(name, ":path")) {
            js_free(ctx, st->path);
            st->path = tjs_h2stream_dup_js_string(ctx, value);
            JS_DefinePropertyValueStr(ctx, sent_hdrs, ":path", JS_DupValue(ctx, value), JS_PROP_C_W_E);
        } else if (!strcmp(name, ":authority")) {
            js_free(ctx, st->authority);
            st->authority = tjs_h2stream_dup_js_string(ctx, value);
            JS_DefinePropertyValueStr(ctx, sent_hdrs, ":authority", JS_DupValue(ctx, value), JS_PROP_C_W_E);
        } else if (!strcmp(name, ":scheme")) {
            js_free(ctx, st->scheme);
            st->scheme = tjs_h2stream_dup_js_string(ctx, value);
            JS_DefinePropertyValueStr(ctx, sent_hdrs, ":scheme", JS_DupValue(ctx, value), JS_PROP_C_W_E);
        } else {
            if (tjs_h2stream_add_header(ctx, st, name, value)) {
                JS_FreeValue(ctx, value);
                JS_FreeCString(ctx, name);
                JS_FreeValue(ctx, sent_hdrs);
                js_free(ctx, tab);
                return -1;
            }
            JS_DefinePropertyValueStr(ctx, sent_hdrs, name, JS_DupValue(ctx, value), JS_PROP_C_W_E);
        }

        JS_FreeValue(ctx, value);
        JS_FreeCString(ctx, name);
    }

    js_free(ctx, tab);
    st->sent_headers = sent_hdrs;
    return (st->method && st->path && st->scheme && st->authority) ? 0 : -1;
}

static int tjs_h2stream_connect(TJSH2Stream *st)
{
    JSContext *ctx = st->ctx;
    TJSH2Session *s = st->session;
    struct lws_context *lws_ctx = tjs_h2session_get_lws_ctx(ctx);
    if (!lws_ctx)
        return -1;

    st->connect_host = js_strdup(ctx, s->connect_host ? s->connect_host : st->authority);
    st->connect_port = s->connect_port ? s->connect_port : (s->connect_ssl ? 443 : 80);
    if (!st->connect_host)
        return -1;

    /*
     * Build lws client connect info for HTTP/2.
     *
     * Critical fields (aligned with lws minimal-http-client-h2-rxflow):
     * - protocol: "tjs-http" matches the generic HTTP protocol registered in context
     * - local_protocol_name: "tjs-h2stream" binds to our custom callback handler
     * - alpn: "h2" requires HTTP/2 via TLS ALPN negotiation
     * - method: "POST" for gRPC unary requests
     *
     * SSL flags:
     * - LCCSCF_USE_SSL: enable TLS for https:// authority
     * - LCCSCF_H2_QUIRK_OVERFLOWS_TXCR: required for H2 tx credit handling
     * - LCCSCF_H2_QUIRK_NGHTTP2_END_STREAM: required for H2 END_STREAM quirk
     * - LCCSCF_HTTP_NO_FOLLOW_REDIRECT: don't follow redirects (gRPC expects exact endpoint)
     * - ssl_flags from options: self-signed, expired, insecure (for rejectUnauthorized:false)
     */
    struct lws_client_connect_info cci;
    memset(&cci, 0, sizeof(cci));
    cci.context = lws_ctx;
    cci.address = st->connect_host;
    cci.port = st->connect_port;
    cci.path = st->path ? st->path : "/";
    cci.host = st->authority && st->authority[0] ? st->authority : st->connect_host;
    cci.origin = cci.host;
    cci.method = st->method ? st->method : "POST";
    cci.alpn = "h2";
    cci.protocol = "tjs-http";  /* Must match registered HTTP protocol name */
    cci.local_protocol_name = tjs_h2stream_protocol.name;  /* Bind to our callback */
    cci.userdata = st;
    cci.pwsi = &st->wsi;

    /* Build SSL flags for H2 */
    uint32_t ssl_flags = 0;
    if (s->connect_ssl) {
        ssl_flags |= LCCSCF_USE_SSL;
    } else {
        /* h2c (cleartext HTTP/2) requires prior knowledge flag */
        ssl_flags |= LCCSCF_H2_PRIOR_KNOWLEDGE;
    }
    ssl_flags |= st->ssl_flags;
    /*
     * R4: LCCSCF_PIPELINE was for HTTP/1.1 pipelining. For H2, lws
     * automatically routes new requests to an existing active H2 connection
     * via lws_vhost_active_conn_find() when host:port:alpn match.
     * The session's anchor connection (created in session.connect())
     * provides the H2 transport that streams multiplex over.
     */
    ssl_flags |= LCCSCF_HTTP_NO_FOLLOW_REDIRECT;
    ssl_flags |= LCCSCF_H2_QUIRK_OVERFLOWS_TXCR;
    ssl_flags |= LCCSCF_H2_QUIRK_NGHTTP2_END_STREAM;
    cci.ssl_connection = ssl_flags;

    tjs__lws_conn_ref(ctx);
    struct lws *wsi = lws_client_connect_via_info(&cci);
    if (!wsi) {
        lwsl_err("H2Stream: lws_client_connect_via_info() failed\n");
        tjs__lws_conn_unref(ctx);
        st->wsi = NULL;
        return -1;
    }

    lwsl_info("H2Stream: wsi=%p created successfully\n", (void *) wsi);
    lws_set_wsi_user(wsi, st);
    st->wsi = wsi;
    if (!s->wsi) {
        s->wsi = wsi;
        s->connecting = true;
        s->closed = false;
    }

    /* Kick the shared lws/libuv loop so the new stream starts promptly. */
    lws_cancel_service(lws_ctx);

    return 0;
}

static int tjs_lws_h2stream_callback(struct lws *wsi,
                                      enum lws_callback_reasons reason,
                                      void *user, void *in, size_t len)
{
    TJSH2Stream *st = (TJSH2Stream *) user;
    if (!st)
        st = lws_wsi_user(wsi);
    if (!st || !st->ctx)
        return 0;

    JSContext *ctx = st->ctx;
    TJSRuntime *qrt = TJS_GetRuntime(ctx);

    tjs_h2stream_adopt_callback_wsi(st, wsi, reason);

    TJS_H2_DEBUGF("[h2stream:%d] cb=%d wsi=%p send=%zu ended=%d headers=%d body_pending=%d final=%d\n",
                  st->stream_id, (int) reason, (void *) wsi, st->send_buf.size,
                  st->write_ended, st->headers_sent, st->body_pending,
                  st->final_sent);

    switch (reason) {
    case LWS_CALLBACK_CLIENT_APPEND_HANDSHAKE_HEADER: {
        unsigned char **p = (unsigned char **) in;
        unsigned char *end = (*p) + len;

        if (!tjs_h2_skip_req_headers() && st->req_headers.size > 0) {
            /*
             * req_headers.buf is NOT NUL-terminated. Iterate using
             * st->req_headers.size to avoid reading past the buffer.
             */
            char *headers = (char *) st->req_headers.buf;
            size_t hdr_len = st->req_headers.size;
            size_t pos = 0;

            while (pos < hdr_len) {
                char *line = headers + pos;
                char *eol = memchr(line, '\r', hdr_len - pos);
                if (!eol || (size_t)(eol - line) < 2 || eol[1] != '\n')
                    break;

                int name_len = (int) (eol - line);
                char *colon = memchr(line, ':', (size_t) name_len);
                if (colon) {
                    *colon = '\0';
                    const char *name = line;
                    const char *value = colon + 1;
                    int value_len = (int) (eol - value);
                    while (value_len > 0 && *value == ' ') {
                        value++;
                        value_len--;
                    }

                    if (value_len > 0) {
                        TJS_H2_DEBUGF("[h2stream:%d] add-header %s: %.*s\n",
                                      st->stream_id, name, value_len, value);
                        if (lws_add_http_header_by_name(wsi,
                                                        (const unsigned char *) name,
                                                        (const unsigned char *) value,
                                                        value_len,
                                                        p,
                                                        end)) {
                            *colon = ':';
                            return -1;
                        }
                    }
                    *colon = ':';
                }

                pos = (size_t) (eol - headers) + 2;
            }
        }

        /*
         * When endStream is true with no body, we want lws to send
         * END_STREAM with the HEADERS frame via LCCSCF_H2_QUIRK_NGHTTP2_END_STREAM.
         * Do NOT set body_pending in this case — let lws finalize the
         * HEADERS with END_STREAM directly.
         *
         * When there is actual body data, set body_pending so lws waits
         * for the DATA frames before sending END_STREAM.
         */
        if (st->send_buf.size > 0) {
            st->body_pending = true;
            lws_client_http_body_pending(wsi, 1);
            lws_callback_on_writable(wsi);
            TJS_H2_DEBUGF("[h2stream:%d] append-header body pending with buffered data=%zu\n",
                          st->stream_id, st->send_buf.size);
        } else if (!st->end_after_headers && !st->write_ended) {
            /*
             * No body data yet, but the user hasn't ended the stream
             * either. We expect body to arrive later, so mark pending.
             */
            st->body_pending = true;
            lws_client_http_body_pending(wsi, 1);
            lws_callback_on_writable(wsi);
            TJS_H2_DEBUGF("[h2stream:%d] append-header body pending waiting for future data\n",
                          st->stream_id);
        }
        /*
         * When end_after_headers or write_ended is true with no body
         * data, we skip body_pending so lws sends END_STREAM with
         * the HEADERS frame (via LCCSCF_H2_QUIRK_NGHTTP2_END_STREAM).
         */
        st->headers_sent = true;
        st->pending = false;
        break;
    }

    case LWS_CALLBACK_ESTABLISHED_CLIENT_HTTP:
        if (!qrt->freeing) {
            if (st->response_received) {
                tjs_h2stream_fire_trailers(st, wsi);
                /*
                 * For normal grpc unary responses, libwebsockets delivers the
                 * trailing header block here after the body has already been
                 * read. If we only emit 'end' and wait for a later close-like
                 * callback, the stream may never fully finish from JS's point
                 * of view, so grpc-js keeps the session alive forever.
                 */
                tjs_h2stream_finish(st, true, true);
            } else {
                if (tjs_h2stream_fire_response(st, wsi)) {
                    tjs_h2stream_fire_trailers(st, wsi);
                    tjs_h2stream_finish(st, true, true);
                }
            }
        }
        break;

    case LWS_CALLBACK_RECEIVE_CLIENT_HTTP: {
        char buffer[8192 + LWS_PRE];
        char *px = buffer + LWS_PRE;
        int lenx = sizeof(buffer) - LWS_PRE;
        if (lws_http_client_read(wsi, &px, &lenx) < 0)
            return -1;
        break;
    }

    case LWS_CALLBACK_RECEIVE_CLIENT_HTTP_READ:
        if (!qrt->freeing && in && len > 0 && !st->paused) {
            TJS_H2_DEBUGF("[h2stream:%d] emit data len=%zu\n", st->stream_id, len);
            JSValue arg = JS_NewArrayBufferCopy(ctx, (const uint8_t *) in, len);
            if (!JS_IsException(arg))
                tjs_h2stream_push_event(st, H2STREAM_CB_DATA, 1, &arg);
        }
        break;

    case LWS_CALLBACK_CLIENT_HTTP_WRITEABLE: {
        bool sent_data = false;
        bool write_ok = false;

        if (st->send_buf.size > 0) {
            size_t to_send = st->send_buf.size;
            uint8_t *buf = js_malloc(ctx, LWS_PRE + to_send);
            if (!buf)
                return -1;
            memcpy(buf + LWS_PRE, st->send_buf.buf, to_send);
            enum lws_write_protocol wp = st->write_ended ? LWS_WRITE_HTTP_FINAL : LWS_WRITE_HTTP;
            int n = lws_write(wsi, buf + LWS_PRE, to_send, wp);
            js_free(ctx, buf);
            if (n < 0)
                return -1;
            TJS_H2_DEBUGF("[h2stream:%d] wrote DATA n=%d final=%d\n",
                          st->stream_id, n, wp == LWS_WRITE_HTTP_FINAL);
            st->send_buf.size = 0;
            sent_data = true;
            write_ok = true;
            if (st->write_ended)
                st->final_sent = true;
        }

        if (st->write_ended && !st->final_sent) {
            /*
             * end() was called but we haven't sent the final marker yet.
             * If body data was pending and we just sent it above, we need
             * to send END_STREAM via a zero-length final.
             *
             * If there was no body data at all, the HEADERS frame already
             * carried END_STREAM via LCCSCF_H2_QUIRK_NGHTTP2_END_STREAM,
             * so skip the double-send here.
             */
            if (sent_data) {
                uint8_t final_byte[LWS_PRE + 1];
                final_byte[LWS_PRE] = 0;
                (void) lws_write(wsi, final_byte + LWS_PRE, 0, LWS_WRITE_HTTP_FINAL);
                TJS_H2_DEBUGF("[h2stream:%d] wrote zero FINAL\n", st->stream_id);
                st->final_sent = true;
                write_ok = true;
            }
            lws_client_http_body_pending(wsi, 0);
        } else if (!st->write_ended) {
            /* User hasn't called end() yet — keep body pending for more data. */
            lws_client_http_body_pending(wsi, 1);
            st->body_pending = true;
        }

        /* Invoke deferred write callback if data was sent */
        JSValue cb = JS_UNDEFINED;
        if (write_ok)
            cb = tjs_h2stream_take_pending_write_cb(st);
        if (write_ok && !JS_IsUndefined(cb)) {
            JSValue cb_args[1];
            cb_args[0] = JS_NULL;
            TJS_H2_DEBUGF("[h2stream:%d] invoke deferred write cb final_sent=%d write_ended=%d send_buf=%zu\n",
                          st->stream_id, st->final_sent, st->write_ended, st->send_buf.size);
            tjs_call_handler(ctx, cb, 1, cb_args);
            TJS_H2_DEBUGF("[h2stream:%d] deferred write cb returned\n",
                          st->stream_id);
            JS_FreeValue(ctx, cb);
        }
        break;
    }

    case LWS_CALLBACK_COMPLETED_CLIENT_HTTP:
        if (!qrt->freeing) {
            if (st->response_received && !st->trailers_received)
                tjs_h2stream_fire_trailers(st, wsi);
            tjs_h2stream_finish(st, true, true);
        }
        break;

    case LWS_CALLBACK_CLIENT_CONNECTION_ERROR: {
        if (!qrt->freeing && !st->completed) {
            JSValue args[1];
            args[0] = JS_NewString(ctx, in ? (const char *) in : "Connection error");
            tjs_h2stream_push_event(st, H2STREAM_CB_ERROR, 1, args);
            tjs_h2stream_finish(st, false, true);
        }
        if (st->wsi == wsi) {
            tjs__lws_conn_unref(ctx);
            lws_set_wsi_user(st->wsi, NULL);
            st->wsi = NULL;
        }
        tjs_h2stream_release_self(st);
        break;
    }

    case LWS_CALLBACK_CLOSED_CLIENT_HTTP:
    case LWS_CALLBACK_CLOSED:
        if (st->wsi == wsi) {
            tjs__lws_conn_unref(ctx);
            lws_set_wsi_user(st->wsi, NULL);
            st->wsi = NULL;
        }
        if (!qrt->freeing)
            tjs_h2stream_finish(st, !st->closed, true);
        tjs_h2stream_release_self(st);
        break;

    default:
        break;
    }

    return lws_callback_http_dummy(wsi, reason, user, in, len);
}

/* ------------------------------------------------------------------
 * H2Stream stream.close([code])
 * ------------------------------------------------------------------ */

static JSValue tjs_h2stream_close(JSContext *ctx, JSValue this_val,
                                   int argc, JSValue *argv)
{
    TJSH2Stream *st = JS_GetOpaque2(ctx, this_val, tjs_h2stream_class_id);
    if (!st)
        return JS_EXCEPTION;

    if (st->closed || st->destroyed)
        return JS_UNDEFINED;

    uint32_t code = 0; /* NGHTTP2_NO_ERROR */
    if (argc > 0 && !JS_IsUndefined(argv[0]) && !JS_IsNull(argv[0])) {
        int32_t c;
        if (JS_ToInt32(ctx, &c, argv[0]) >= 0)
            code = (uint32_t) c;
    }

    st->rst_code = code;

    /* Send RST_STREAM frame */
    if (st->wsi && st->stream_id > 0) {
        struct lws_h2_protocol_send *pps = lws_h2_new_pps(LWS_H2_PPS_RST_STREAM);
        if (pps) {
            pps->u.rs.sid = (uint32_t) st->stream_id;
            pps->u.rs.err = code;
            lws_pps_schedule(st->wsi, pps);
            lwsl_info("H2Stream %d: sending RST_STREAM code=%u\n", st->stream_id, code);
        }
    }

    st->closed = true;

    /* Emit 'close' event */
    JSValue args[1];
    args[0] = JS_DupValue(ctx, st->this_val);
    tjs_h2stream_push_event(st, H2STREAM_CB_CLOSE, 1, args);

    if (st->session && st->stream_id > 0)
        tjs_h2session_unregister_stream(st->session, st->stream_id);

    tjs_h2stream_loop_unref(st);
    if (!st->wsi)
        tjs_h2stream_release_self(st);

    return JS_UNDEFINED;
}

/* ------------------------------------------------------------------
 * H2Stream stream.destroy([error])
 * ------------------------------------------------------------------ */

static JSValue tjs_h2stream_destroy(JSContext *ctx, JSValue this_val,
                                     int argc, JSValue *argv)
{
    TJSH2Stream *st = JS_GetOpaque2(ctx, this_val, tjs_h2stream_class_id);
    if (!st)
        return JS_EXCEPTION;

    if (st->destroyed)
        return JS_UNDEFINED;

    st->destroyed = true;
    st->closed = true;

    /* Emit 'error' if error was passed */
    if (argc > 0 && !JS_IsUndefined(argv[0]) && !JS_IsNull(argv[0])) {
        JSValue args[1];
        args[0] = JS_DupValue(ctx, argv[0]);
        tjs_h2stream_push_event(st, H2STREAM_CB_ERROR, 1, args);
    }

    /* Emit 'close' */
    JSValue args2[1];
    args2[0] = JS_DupValue(ctx, st->this_val);
    tjs_h2stream_push_event(st, H2STREAM_CB_CLOSE, 1, args2);

    if (st->session && st->stream_id > 0)
        tjs_h2session_unregister_stream(st->session, st->stream_id);

    tjs_h2stream_loop_unref(st);
    if (st->wsi) {
        lws_set_timeout(st->wsi, PENDING_TIMEOUT_USER_OK, LWS_TO_KILL_SYNC);
    } else {
        tjs_h2stream_release_self(st);
    }

    return JS_UNDEFINED;
}

/* ------------------------------------------------------------------
 * H2Stream stream.pause()
 * ------------------------------------------------------------------ */

static JSValue tjs_h2stream_pause(JSContext *ctx, JSValue this_val,
                                   int argc, JSValue *argv)
{
    (void) argc; (void) argv;
    TJSH2Stream *st = JS_GetOpaque2(ctx, this_val, tjs_h2stream_class_id);
    if (!st)
        return JS_EXCEPTION;

    st->paused = true;
    return JS_UNDEFINED;
}

/* ------------------------------------------------------------------
 * H2Stream stream.resume()
 * ------------------------------------------------------------------ */

static JSValue tjs_h2stream_resume(JSContext *ctx, JSValue this_val,
                                    int argc, JSValue *argv)
{
    (void) argc; (void) argv;
    TJSH2Stream *st = JS_GetOpaque2(ctx, this_val, tjs_h2stream_class_id);
    if (!st)
        return JS_EXCEPTION;

    st->paused = false;

    /* If we have buffered data, emit it now */
    if (st->read_buf && st->read_buf_pos < st->read_buf_len) {
        /* Will be handled when more data arrives or on next lws callback */
        lws_callback_on_writable(st->wsi);
    }

    return JS_UNDEFINED;
}

/* ------------------------------------------------------------------
 * H2Stream stream.write(chunk[, encoding][, callback])
 * ------------------------------------------------------------------ */

static JSValue tjs_h2stream_write(JSContext *ctx, JSValue this_val,
                                   int argc, JSValue *argv)
{
    TJSH2Stream *st = JS_GetOpaque2(ctx, this_val, tjs_h2stream_class_id);
    if (!st)
        return JS_EXCEPTION;

    if (st->write_ended || st->closed || st->destroyed) {
        /* Write after end - return error via callback if provided */
        if (argc >= 2 && JS_IsFunction(ctx, argv[argc - 1])) {
            JSValue err = JS_NewError(ctx);
            JS_DefinePropertyValueStr(ctx, err, "message",
                                      JS_NewString(ctx, "write after end"),
                                      JS_PROP_C_W_E);
            JS_DefinePropertyValueStr(ctx, err, "code",
                                      JS_NewString(ctx, "ERR_STREAM_WRITE_AFTER_END"),
                                      JS_PROP_C_W_E);
            JSValue cb_args[1];
            cb_args[0] = JS_DupValue(ctx, err);
            tjs_call_handler(ctx, argv[argc - 1], 1, cb_args);
            JS_FreeValue(ctx, err);
        } else {
            return JS_ThrowInternalError(ctx, "ERR_STREAM_WRITE_AFTER_END");
        }
        return JS_NewBool(ctx, 0);
    }

    /* Get the chunk data */
    const uint8_t *buf = NULL;
    size_t buf_len = 0;
    JSValue data_ref = JS_UNDEFINED;
    const char *str = NULL;

    if (argc > 0 && !JS_IsUndefined(argv[0]) && !JS_IsNull(argv[0])) {
        if (JS_IsString(argv[0])) {
            str = JS_ToCString(ctx, argv[0]);
            if (str) {
                buf = (const uint8_t *) str;
                buf_len = strlen(str);
            }
        } else if (JS_IsArrayBuffer(argv[0])) {
            size_t bsize;
            buf = JS_GetArrayBuffer(ctx, &bsize, argv[0]);
            buf_len = bsize;
        } else {
            size_t offset = 0;
            size_t bsize = 0;
            size_t elem_size = 0;
            data_ref = JS_GetTypedArrayBuffer(ctx, argv[0], &offset, &bsize, &elem_size);
            if (!JS_IsException(data_ref)) {
                size_t abuf_size = 0;
                uint8_t *abuf = JS_GetArrayBuffer(ctx, &abuf_size, data_ref);
                if (abuf && offset <= abuf_size && bsize <= abuf_size - offset) {
                    buf = abuf + offset;
                    buf_len = bsize;
                }
            } else {
                data_ref = JS_UNDEFINED;
            }
        }
    }

    if (!buf || buf_len == 0) {
        if (str)
            JS_FreeCString(ctx, str);
        JS_FreeValue(ctx, data_ref);
        /* Empty write - just invoke callback if provided */
        if (argc >= 2 && JS_IsFunction(ctx, argv[argc - 1])) {
            JSValue cb_args[1];
            cb_args[0] = JS_NULL;
            tjs_call_handler(ctx, argv[argc - 1], 1, cb_args);
        }
        return JS_NewBool(ctx, 1);
    }

    if (dbuf_put(&st->send_buf, buf, buf_len)) {
        if (str)
            JS_FreeCString(ctx, str);
        JS_FreeValue(ctx, data_ref);
        return JS_ThrowOutOfMemory(ctx);
    }
    if (str)
        JS_FreeCString(ctx, str);
    JS_FreeValue(ctx, data_ref);

    /* Trigger writeable callback */
    if (st->wsi) {
        lws_client_http_body_pending(st->wsi, 1);
        lws_callback_on_writable(st->wsi);
    }

    /*
     * Store the callback for deferred invocation. It will be called
     * after the data is actually sent in CLIENT_HTTP_WRITEABLE.
     * This matches Node's "data flushed" callback semantics.
     */
    if (argc >= 2 && JS_IsFunction(ctx, argv[argc - 1])) {
        if (tjs_h2stream_set_pending_write_cb(st, argv[argc - 1]) < 0)
            return JS_EXCEPTION;
    }

    return JS_NewBool(ctx, 1);
}

/* ------------------------------------------------------------------
 * H2Stream stream.end([chunk][, encoding][, callback])
 * ------------------------------------------------------------------ */

static JSValue tjs_h2stream_end(JSContext *ctx, JSValue this_val,
                                 int argc, JSValue *argv)
{
    TJSH2Stream *st = JS_GetOpaque2(ctx, this_val, tjs_h2stream_class_id);
    if (!st)
        return JS_EXCEPTION;

    if (st->write_ended || st->closed || st->destroyed)
        return JS_UNDEFINED;

    /*
     * Handle end() call patterns:
     *   end()                    - just end
     *   end(chunk)               - write chunk then end
     *   end(chunk, encoding)     - write chunk with encoding
     *   end(chunk, encoding, cb) - write chunk with callback
     *   end(chunk, cb)           - write chunk with callback
     *   end(cb)                  - just end with callback
     */
    int chunk_argc = 0;
    JSValue chunk_args[2] = { JS_UNDEFINED, JS_UNDEFINED };
    JSValue cb = JS_UNDEFINED;

    if (argc > 0 && !JS_IsUndefined(argv[0]) && !JS_IsNull(argv[0])) {
        if (JS_IsFunction(ctx, argv[0])) {
            /* end(callback) */
            cb = argv[0];
        } else {
            /* end(chunk) or end(chunk, encoding) or end(chunk, cb) */
            chunk_args[0] = argv[0];
            chunk_argc = 1;
            if (argc > 1) {
                if (JS_IsFunction(ctx, argv[1])) {
                    cb = argv[1];
                } else {
                    chunk_args[1] = argv[1];
                    chunk_argc = 2;
                    if (argc > 2 && JS_IsFunction(ctx, argv[2])) {
                        cb = argv[2];
                    }
                }
            }
        }
    }

    /* Write final chunk if present */
    if (chunk_argc > 0) {
        JSValue write_args[3];
        write_args[0] = chunk_args[0];
        write_args[1] = chunk_args[1];
        write_args[2] = JS_UNDEFINED;
        tjs_h2stream_write(ctx, this_val, chunk_argc, write_args);
    }

    st->write_ended = true;
    st->half_closed_local = true;

    /* Mark stream as writeable to send END_STREAM flag */
    if (st->wsi) {
        lws_callback_on_writable(st->wsi);
    }

    /* Store the callback for deferred invocation */
    if (JS_IsFunction(ctx, cb)) {
        if (tjs_h2stream_set_pending_write_cb(st, cb) < 0)
            return JS_EXCEPTION;
    }

    return JS_UNDEFINED;
}

/* ------------------------------------------------------------------
 * H2Stream getters/setters
 * ------------------------------------------------------------------ */

static JSValue tjs_h2stream_callback_get(JSContext *ctx, JSValue this_val, int magic)
{
    TJSH2Stream *st = JS_GetOpaque2(ctx, this_val, tjs_h2stream_class_id);
    if (!st)
        return JS_EXCEPTION;
    return tjs_h2stream_get_callback(st, magic);
}

static JSValue tjs_h2stream_callback_set(JSContext *ctx, JSValue this_val,
                                          JSValue value, int magic)
{
    TJSH2Stream *st = JS_GetOpaque2(ctx, this_val, tjs_h2stream_class_id);
    if (!st)
        return JS_EXCEPTION;
    if (JS_IsFunction(ctx, value) || JS_IsUndefined(value) || JS_IsNull(value)) {
        if (tjs_h2stream_set_callback_value(st, magic, value) < 0)
            return JS_EXCEPTION;
    }
    return JS_UNDEFINED;
}

/* Property: id */
static JSValue tjs_h2stream_get_id(JSContext *ctx, JSValue this_val)
{
    TJSH2Stream *st = JS_GetOpaque2(ctx, this_val, tjs_h2stream_class_id);
    if (!st)
        return JS_EXCEPTION;
    if (st->stream_id == 0)
        return JS_UNDEFINED;
    return JS_NewInt32(ctx, st->stream_id);
}

/* Property: rstCode */
static JSValue tjs_h2stream_get_rst_code(JSContext *ctx, JSValue this_val)
{
    TJSH2Stream *st = JS_GetOpaque2(ctx, this_val, tjs_h2stream_class_id);
    if (!st)
        return JS_EXCEPTION;
    return JS_NewInt32(ctx, (int32_t) st->rst_code);
}

/* Property: destroyed */
static JSValue tjs_h2stream_get_destroyed(JSContext *ctx, JSValue this_val)
{
    TJSH2Stream *st = JS_GetOpaque2(ctx, this_val, tjs_h2stream_class_id);
    if (!st)
        return JS_EXCEPTION;
    return JS_NewBool(ctx, st->destroyed);
}

/* Property: closed */
static JSValue tjs_h2stream_get_closed(JSContext *ctx, JSValue this_val)
{
    TJSH2Stream *st = JS_GetOpaque2(ctx, this_val, tjs_h2stream_class_id);
    if (!st)
        return JS_EXCEPTION;
    return JS_NewBool(ctx, st->closed);
}

/* Property: pending */
static JSValue tjs_h2stream_get_pending(JSContext *ctx, JSValue this_val)
{
    TJSH2Stream *st = JS_GetOpaque2(ctx, this_val, tjs_h2stream_class_id);
    if (!st)
        return JS_EXCEPTION;
    return JS_NewBool(ctx, st->pending);
}

/* Property: session */
static JSValue tjs_h2stream_get_session(JSContext *ctx, JSValue this_val)
{
    TJSH2Stream *st = JS_GetOpaque2(ctx, this_val, tjs_h2stream_class_id);
    if (!st)
        return JS_EXCEPTION;
    return JS_DupValue(ctx, st->session_val);
}

/* Property: state */
static JSValue tjs_h2stream_get_state(JSContext *ctx, JSValue this_val)
{
    TJSH2Stream *st = JS_GetOpaque2(ctx, this_val, tjs_h2stream_class_id);
    if (!st)
        return JS_EXCEPTION;

    JSValue state = JS_NewObject(ctx);
    if (JS_IsException(state))
        return state;

    JS_DefinePropertyValueStr(ctx, state, "localWindowSize",
        JS_NewInt32(ctx, (int32_t) st->local_window_size), JS_PROP_C_W_E);
    JS_DefinePropertyValueStr(ctx, state, "state",
        JS_NewInt32(ctx, 0), JS_PROP_C_W_E);
    JS_DefinePropertyValueStr(ctx, state, "localClose",
        JS_NewBool(ctx, st->half_closed_local), JS_PROP_C_W_E);
    JS_DefinePropertyValueStr(ctx, state, "remoteClose",
        JS_NewBool(ctx, st->half_closed_remote), JS_PROP_C_W_E);
    JS_DefinePropertyValueStr(ctx, state, "sumDependencyWeight",
        JS_NewInt32(ctx, 0), JS_PROP_C_W_E);
    JS_DefinePropertyValueStr(ctx, state, "weight",
        JS_NewInt32(ctx, 16), JS_PROP_C_W_E);

    return state;
}

/* Property: endAfterHeaders */
static JSValue tjs_h2stream_get_end_after_headers(JSContext *ctx, JSValue this_val)
{
    TJSH2Stream *st = JS_GetOpaque2(ctx, this_val, tjs_h2stream_class_id);
    if (!st)
        return JS_EXCEPTION;
    return JS_NewBool(ctx, st->end_after_headers);
}

/* Property: sentHeaders */
static JSValue tjs_h2stream_get_sent_headers(JSContext *ctx, JSValue this_val)
{
    TJSH2Stream *st = JS_GetOpaque2(ctx, this_val, tjs_h2stream_class_id);
    if (!st)
        return JS_EXCEPTION;
    return JS_DupValue(ctx, st->sent_headers);
}

/* Property: sentTrailers */
static JSValue tjs_h2stream_get_sent_trailers(JSContext *ctx, JSValue this_val)
{
    TJSH2Stream *st = JS_GetOpaque2(ctx, this_val, tjs_h2stream_class_id);
    if (!st)
        return JS_EXCEPTION;
    return JS_DupValue(ctx, st->sent_trailers);
}

/* ------------------------------------------------------------------
 * H2Stream method/property registration
 * ------------------------------------------------------------------ */

static const JSCFunctionListEntry tjs_h2stream_proto_funcs[] = {
    TJS_CFUNC_DEF("write", 3, tjs_h2stream_write),
    TJS_CFUNC_DEF("end", 3, tjs_h2stream_end),
    TJS_CFUNC_DEF("close", 1, tjs_h2stream_close),
    TJS_CFUNC_DEF("pause", 0, tjs_h2stream_pause),
    TJS_CFUNC_DEF("resume", 0, tjs_h2stream_resume),
    TJS_CFUNC_DEF("destroy", 1, tjs_h2stream_destroy),

    JS_CGETSET_MAGIC_DEF("onresponse", tjs_h2stream_callback_get,
                         tjs_h2stream_callback_set, H2STREAM_CB_RESPONSE),
    JS_CGETSET_MAGIC_DEF("ontrailers", tjs_h2stream_callback_get,
                         tjs_h2stream_callback_set, H2STREAM_CB_TRAILERS),
    JS_CGETSET_MAGIC_DEF("ondata", tjs_h2stream_callback_get,
                         tjs_h2stream_callback_set, H2STREAM_CB_DATA),
    JS_CGETSET_MAGIC_DEF("onend", tjs_h2stream_callback_get,
                         tjs_h2stream_callback_set, H2STREAM_CB_END),
    JS_CGETSET_MAGIC_DEF("onerror", tjs_h2stream_callback_get,
                         tjs_h2stream_callback_set, H2STREAM_CB_ERROR),
    JS_CGETSET_MAGIC_DEF("onclose", tjs_h2stream_callback_get,
                         tjs_h2stream_callback_set, H2STREAM_CB_CLOSE),

    JS_CGETSET_DEF("id", tjs_h2stream_get_id, NULL),
    JS_CGETSET_DEF("rstCode", tjs_h2stream_get_rst_code, NULL),
    JS_CGETSET_DEF("destroyed", tjs_h2stream_get_destroyed, NULL),
    JS_CGETSET_DEF("closed", tjs_h2stream_get_closed, NULL),
    JS_CGETSET_DEF("pending", tjs_h2stream_get_pending, NULL),
    JS_CGETSET_DEF("session", tjs_h2stream_get_session, NULL),
    JS_CGETSET_DEF("state", tjs_h2stream_get_state, NULL),
    JS_CGETSET_DEF("endAfterHeaders", tjs_h2stream_get_end_after_headers, NULL),
    JS_CGETSET_DEF("sentHeaders", tjs_h2stream_get_sent_headers, NULL),
    JS_CGETSET_DEF("sentTrailers", tjs_h2stream_get_sent_trailers, NULL),
};

/* ------------------------------------------------------------------
 * H2Stream class init (called from tjs__mod_http2_init)
 * ------------------------------------------------------------------ */

static void tjs_h2stream_class_init(JSContext *ctx, JSValue ns)
{
    JSRuntime *rt = JS_GetRuntime(ctx);

    JS_NewClassID(rt, &tjs_h2stream_class_id);

    static JSClassDef tjs_h2stream_class = {
        "H2Stream",
        .finalizer = tjs_h2stream_finalizer,
        .gc_mark   = tjs_h2stream_mark,
    };

    JS_NewClass(rt, tjs_h2stream_class_id, &tjs_h2stream_class);

    JSValue proto = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, proto, tjs_h2stream_proto_funcs,
                               countof(tjs_h2stream_proto_funcs));
    JS_SetClassProto(ctx, tjs_h2stream_class_id, proto);

    /* Export H2Stream as a class (not directly constructable) */
    JSValue obj = JS_NewObject(ctx);
    JS_DefinePropertyValueStr(ctx, obj, "prototype",
                              JS_DupValue(ctx, proto), JS_PROP_C_W_E);
    JS_DefinePropertyValueStr(ctx, ns, "H2Stream", obj, JS_PROP_C_W_E);
}

/* ------------------------------------------------------------------
 * session.request(headers) -> H2Stream
 * (B2: full implementation with headers sending)
 * ------------------------------------------------------------------ */

static JSValue tjs_h2session_request(JSContext *ctx, JSValue this_val,
                                      int argc, JSValue *argv)
{
    TJSH2Session *s = JS_GetOpaque2(ctx, this_val, tjs_h2session_class_id);
    if (!s)
        return JS_EXCEPTION;

    if (s->closed || s->destroyed || !s->connect_host) {
        return JS_ThrowTypeError(ctx, "Session is closed or destroyed");
    }

    /* Allocate stream ID */
    int stream_id = s->next_stream_id;
    s->next_stream_id += 2;  /* client streams use odd IDs */

    /* Create H2Stream object */
    JSValue stream_obj = tjs_h2stream_new(ctx, s, stream_id);
    if (JS_IsException(stream_obj))
        return stream_obj;

    TJSH2Stream *st = JS_GetOpaque(stream_obj, tjs_h2stream_class_id);
    if (!st) {
        JS_FreeValue(ctx, stream_obj);
        return JS_EXCEPTION;
    }

    /* Parse headers from argv[0] - sent_headers is built in tjs_h2stream_parse_headers */
    JSValue headers_obj = argc > 0 ? argv[0] : JS_UNDEFINED;

    /* Parse options from argv[1] */
    if (argc > 1 && !JS_IsUndefined(argv[1]) && !JS_IsNull(argv[1])) {
        JSValue options = argv[1];
        JSValue end_stream = JS_GetPropertyStr(ctx, options, "endStream");
        if (!JS_IsException(end_stream) && JS_IsBool(end_stream)) {
            st->end_after_headers = JS_ToBool(ctx, end_stream);
        }
        JS_FreeValue(ctx, end_stream);

        JSValue reject_unauthorized = JS_GetPropertyStr(ctx, options, "rejectUnauthorized");
        if (!JS_IsException(reject_unauthorized) && !JS_IsUndefined(reject_unauthorized) &&
            !JS_ToBool(ctx, reject_unauthorized)) {
            st->ssl_flags |= LCCSCF_ALLOW_SELFSIGNED;
            st->ssl_flags |= LCCSCF_SKIP_SERVER_CERT_HOSTNAME_CHECK;
            st->ssl_flags |= LCCSCF_ALLOW_EXPIRED;
            st->ssl_flags |= LCCSCF_ALLOW_INSECURE;
        }
        JS_FreeValue(ctx, reject_unauthorized);
    }

    if (tjs_h2stream_parse_headers(ctx, st, headers_obj)) {
        JS_FreeValue(ctx, stream_obj);
        return JS_EXCEPTION;
    }

    if (st->end_after_headers) {
        st->write_ended = true;
        st->half_closed_local = true;
    }

    /* Register stream with session for tracking */
    tjs_h2session_register_stream(s, st);

    if (tjs_h2stream_connect(st)) {
        tjs_h2session_unregister_stream(s, stream_id);
        JSValue args[1];
        args[0] = JS_NewString(ctx, "Failed to create HTTP/2 stream request");
        tjs_h2stream_push_event(st, H2STREAM_CB_ERROR, 1, args);
        JS_FreeValue(ctx, stream_obj);
        return JS_ThrowInternalError(ctx, "Failed to create HTTP/2 stream request");
    }

    return stream_obj;
}

/* ------------------------------------------------------------------
 * Getters / Setters
 * ------------------------------------------------------------------ */

/* onconnect, onremotesettings, etc. callback accessors */

static JSValue tjs_h2session_callback_get(JSContext *ctx, JSValue this_val, int magic)
{
    TJSH2Session *s = JS_GetOpaque2(ctx, this_val, tjs_h2session_class_id);
    if (!s)
        return JS_EXCEPTION;
    return JS_DupValue(ctx, s->callbacks[magic]);
}

static JSValue tjs_h2session_callback_set(JSContext *ctx, JSValue this_val,
                                           JSValue value, int magic)
{
    TJSH2Session *s = JS_GetOpaque2(ctx, this_val, tjs_h2session_class_id);
    if (!s)
        return JS_EXCEPTION;
    if (JS_IsFunction(ctx, value) || JS_IsUndefined(value) || JS_IsNull(value)) {
        JS_FreeValue(ctx, s->callbacks[magic]);
        s->callbacks[magic] = JS_DupValue(ctx, value);
    }
    return JS_UNDEFINED;
}

/* Property: closed */
static JSValue tjs_h2session_get_closed(JSContext *ctx, JSValue this_val)
{
    TJSH2Session *s = JS_GetOpaque2(ctx, this_val, tjs_h2session_class_id);
    if (!s)
        return JS_EXCEPTION;
    return JS_NewBool(ctx, s->closed);
}

/* Property: destroyed */
static JSValue tjs_h2session_get_destroyed(JSContext *ctx, JSValue this_val)
{
    TJSH2Session *s = JS_GetOpaque2(ctx, this_val, tjs_h2session_class_id);
    if (!s)
        return JS_EXCEPTION;
    return JS_NewBool(ctx, s->destroyed);
}

/* Property: connecting */
static JSValue tjs_h2session_get_connecting(JSContext *ctx, JSValue this_val)
{
    TJSH2Session *s = JS_GetOpaque2(ctx, this_val, tjs_h2session_class_id);
    if (!s)
        return JS_EXCEPTION;
    return JS_NewBool(ctx, s->connecting);
}

/* Property: encrypted */
static JSValue tjs_h2session_get_encrypted(JSContext *ctx, JSValue this_val)
{
    TJSH2Session *s = JS_GetOpaque2(ctx, this_val, tjs_h2session_class_id);
    if (!s)
        return JS_EXCEPTION;
    return JS_NewBool(ctx, s->encrypted);
}

/* Property: socket */
static JSValue tjs_h2session_get_socket(JSContext *ctx, JSValue this_val)
{
    TJSH2Session *s = JS_GetOpaque2(ctx, this_val, tjs_h2session_class_id);
    if (!s)
        return JS_EXCEPTION;
    return JS_DupValue(ctx, s->socket_obj);
}

static JSValue tjs_h2session_set_socket(JSContext *ctx, JSValue this_val, JSValue value)
{
    TJSH2Session *s = JS_GetOpaque2(ctx, this_val, tjs_h2session_class_id);
    if (!s)
        return JS_EXCEPTION;
    JS_FreeValue(ctx, s->socket_obj);
    s->socket_obj = JS_DupValue(ctx, value);
    return JS_UNDEFINED;
}

/* Property: type */
static JSValue tjs_h2session_get_type(JSContext *ctx, JSValue this_val)
{
    (void) this_val;
    return JS_NewInt32(ctx, 1);  /* NGHTTP2_SESSION_CLIENT */
}

/* Property: state */
static JSValue tjs_h2session_get_state(JSContext *ctx, JSValue this_val)
{
    TJSH2Session *s = JS_GetOpaque2(ctx, this_val, tjs_h2session_class_id);
    if (!s)
        return JS_EXCEPTION;

    JSValue state = JS_NewObject(ctx);
    if (JS_IsException(state))
        return state;

    JS_DefinePropertyValueStr(ctx, state, "effectiveLocalWindowSize",
        JS_NewInt32(ctx, (int32_t) s->local_window_size), JS_PROP_C_W_E);
    JS_DefinePropertyValueStr(ctx, state, "effectiveRecvDataLength",
        JS_NewInt32(ctx, 0), JS_PROP_C_W_E);
    JS_DefinePropertyValueStr(ctx, state, "nextStreamID",
        JS_NewInt32(ctx, s->next_stream_id), JS_PROP_C_W_E);
    JS_DefinePropertyValueStr(ctx, state, "localWindowSize",
        JS_NewInt32(ctx, (int32_t) s->local_window_size), JS_PROP_C_W_E);
    JS_DefinePropertyValueStr(ctx, state, "lastProcStreamID",
        JS_NewInt32(ctx, s->last_processed_stream_id), JS_PROP_C_W_E);
    JS_DefinePropertyValueStr(ctx, state, "remoteWindowSize",
        JS_NewInt32(ctx, (int32_t) s->remote_window_size), JS_PROP_C_W_E);
    JS_DefinePropertyValueStr(ctx, state, "outboundQueueSize",
        JS_NewInt32(ctx, 0), JS_PROP_C_W_E);
    JS_DefinePropertyValueStr(ctx, state, "deflateDynamicTableSize",
        JS_NewInt32(ctx, 4096), JS_PROP_C_W_E);
    JS_DefinePropertyValueStr(ctx, state, "inflateDynamicTableSize",
        JS_NewInt32(ctx, 4096), JS_PROP_C_W_E);

    return state;
}

/* Property: remoteSettings */
static JSValue tjs_h2session_get_remote_settings(JSContext *ctx, JSValue this_val)
{
    TJSH2Session *s = JS_GetOpaque2(ctx, this_val, tjs_h2session_class_id);
    if (!s)
        return JS_EXCEPTION;
    return tjs_h2session_make_settings_obj(ctx, s->remote_settings);
}

/* Property: localSettings */
static JSValue tjs_h2session_get_local_settings(JSContext *ctx, JSValue this_val)
{
    TJSH2Session *s = JS_GetOpaque2(ctx, this_val, tjs_h2session_class_id);
    if (!s)
        return JS_EXCEPTION;
    return tjs_h2session_make_settings_obj(ctx, s->local_settings);
}

/* Property: pendingSettingsAck */
static JSValue tjs_h2session_get_pending_settings_ack(JSContext *ctx, JSValue this_val)
{
    TJSH2Session *s = JS_GetOpaque2(ctx, this_val, tjs_h2session_class_id);
    if (!s)
        return JS_EXCEPTION;
    return JS_NewBool(ctx, s->pending_settings_ack);
}

/* ------------------------------------------------------------------
 * Method / property registration
 * ------------------------------------------------------------------ */

static const JSCFunctionListEntry tjs_h2session_proto_funcs[] = {
    TJS_CFUNC_DEF("connect", 4, tjs_h2session_connect),
    TJS_CFUNC_DEF("request", 1, tjs_h2session_request),
    TJS_CFUNC_DEF("ping", 1, tjs_h2session_ping),
    TJS_CFUNC_DEF("setLocalWindowSize", 1, tjs_h2session_set_local_window_size),
    TJS_CFUNC_DEF("incrementWindowSize", 1, tjs_h2session_increment_window_size),
    TJS_CFUNC_DEF("ref", 0, tjs_h2session_ref_method),
    TJS_CFUNC_DEF("unref", 0, tjs_h2session_unref_method),
    TJS_CFUNC_DEF("close", 0, tjs_h2session_close),
    TJS_CFUNC_DEF("destroy", 0, tjs_h2session_destroy),

    JS_CGETSET_MAGIC_DEF("onconnect", tjs_h2session_callback_get,
                         tjs_h2session_callback_set, H2SESSION_CB_CONNECT),
    JS_CGETSET_MAGIC_DEF("onremotesettings", tjs_h2session_callback_get,
                         tjs_h2session_callback_set, H2SESSION_CB_REMOTE_SETTINGS),
    JS_CGETSET_MAGIC_DEF("onlocalsettings", tjs_h2session_callback_get,
                         tjs_h2session_callback_set, H2SESSION_CB_LOCAL_SETTINGS),
    JS_CGETSET_MAGIC_DEF("ongoaway", tjs_h2session_callback_get,
                         tjs_h2session_callback_set, H2SESSION_CB_GOAWAY),
    JS_CGETSET_MAGIC_DEF("onerror", tjs_h2session_callback_get,
                         tjs_h2session_callback_set, H2SESSION_CB_ERROR),
    JS_CGETSET_MAGIC_DEF("onclose", tjs_h2session_callback_get,
                         tjs_h2session_callback_set, H2SESSION_CB_CLOSE),
    JS_CGETSET_MAGIC_DEF("onping", tjs_h2session_callback_get,
                         tjs_h2session_callback_set, H2SESSION_CB_PING),
    JS_CGETSET_DEF("closed", tjs_h2session_get_closed, NULL),
    JS_CGETSET_DEF("destroyed", tjs_h2session_get_destroyed, NULL),
    JS_CGETSET_DEF("connecting", tjs_h2session_get_connecting, NULL),
    JS_CGETSET_DEF("encrypted", tjs_h2session_get_encrypted, NULL),
    JS_CGETSET_DEF("socket", tjs_h2session_get_socket, tjs_h2session_set_socket),
    JS_CGETSET_DEF("type", tjs_h2session_get_type, NULL),
    JS_CGETSET_DEF("state", tjs_h2session_get_state, NULL),
    JS_CGETSET_DEF("remoteSettings", tjs_h2session_get_remote_settings, NULL),
    JS_CGETSET_DEF("localSettings", tjs_h2session_get_local_settings, NULL),
    JS_CGETSET_DEF("pendingSettingsAck", tjs_h2session_get_pending_settings_ack, NULL),
};

/* ------------------------------------------------------------------
 * Module init
 * ------------------------------------------------------------------ */

void tjs__mod_http2_init(JSContext *ctx, JSValue ns)
{
    JSRuntime *rt = JS_GetRuntime(ctx);

    /* Initialize H2Session class */
    JS_NewClassID(rt, &tjs_h2session_class_id);

    static JSClassDef tjs_h2session_class = {
        "H2Session",
        .finalizer = tjs_h2session_finalizer,
        .gc_mark   = tjs_h2session_mark,
    };

    JS_NewClass(rt, tjs_h2session_class_id, &tjs_h2session_class);

    JSValue proto = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, proto, tjs_h2session_proto_funcs,
                               countof(tjs_h2session_proto_funcs));
    JS_SetClassProto(ctx, tjs_h2session_class_id, proto);

    JSValue obj = JS_NewCFunction2(ctx, tjs_h2session_constructor,
                                    "H2Session", 0, JS_CFUNC_constructor, 0);
    JS_DefinePropertyValueStr(ctx, ns, "H2Session", obj, JS_PROP_C_W_E);

    /* Initialize H2Stream class */
    tjs_h2stream_class_init(ctx, ns);
}

#endif /* TJS__HAS_HTTP2 */
