#include "../deps/quickjs/list.h"
#include "private.h"

#include <libwebsockets/lws-mqtt.h>
#include <string.h>

enum {
    MQTT_CALLBACK_ACK = 0,
    MQTT_CALLBACK_CLOSE,
    MQTT_CALLBACK_CONNECT,
    MQTT_CALLBACK_ERROR,
    MQTT_CALLBACK_MESSAGE,
    MQTT_CALLBACK_SUBSCRIBED,
    MQTT_CALLBACK_UNSUBSCRIBED,
    MQTT_CALLBACK_MAX
};

typedef enum { MQTT_OP_PUBLISH = 1, MQTT_OP_SUBSCRIBE, MQTT_OP_UNSUBSCRIBE } TJSMqttOpType;

typedef enum { TJS_MQTT_TRANSPORT_TCP = 0, TJS_MQTT_TRANSPORT_WS } TJSMqttTransport;

typedef struct {
    struct list_head link;
    TJSMqttOpType type;
    char *topic;
    uint8_t *payload;
    size_t payload_len;
    size_t payload_sent;
    int qos;
    bool retain;
    uint16_t packet_id;
    size_t topics_len;
    size_t topics_sent;
    size_t topics_batch_len;
    char **topics;
    int *topic_qos;
} TJSMqttOp;

typedef struct {
    JSContext *ctx;
    JSValue callbacks[MQTT_CALLBACK_MAX];
    JSValue this_val;
    struct lws *wsi;
    struct lws_vhost *vhost;
    struct list_head pending_ops;
    TJSMqttOp *inflight;
    DynBuf recv_buf;
    DynBuf ws_rx_buf;
    DynBuf ca_pem;
    DynBuf cert_pem;
    DynBuf key_pem;
    char *recv_topic;
    int recv_qos;
    bool recv_retain;
    bool connected;
    bool closing;
    bool closed;
    bool use_ssl;
    TJSMqttTransport transport;
    bool ws_established;
    bool ws_connect_sent;
    bool ws_ping_pending;
    bool ws_ping_send_pending;
    bool tcp_ping_send_pending;
    int ssl_flags;
    int port;
    char *host;
    char *servername;
    char *path;
    char *client_id;
    char *username;
    char *password;
    char *will_topic;
    char *will_message;
    lws_mqtt_client_connect_param_t connect_param;
} TJSMqtt;

static JSClassID tjs_mqtt_class_id;

#define TJS_MQTT_MAX_TOPICS_PER_OP       7
#define TJS_MQTT_PUBLISH_CHUNK_SIZE      1024
#define TJS_MQTT_WS_MAX_REMAINING_LENGTH 268435455u

extern const struct lws_protocols tjs_mqtt_protocol;
extern const struct lws_protocols tjs_mqtt_ws_protocol;

static JSValue tjs_mqtt_tls_error(JSContext *ctx, const char *fallback) {
    const char *message = tjs__lws_last_tls_error(ctx);
    int code = tjs__lws_last_tls_error_code(ctx);
    JSValue err = JS_NewError(ctx);

    JS_DefinePropertyValueStr(ctx, err, "message", JS_NewString(ctx, message ? message : fallback), JS_PROP_C_W_E);
    if (code != 0) {
        JS_DefinePropertyValueStr(ctx, err, "code", JS_NewInt32(ctx, code), JS_PROP_C_W_E);
    }

    return JS_Throw(ctx, err);
}

static bool tjs_mqtt_is_uint8_array(JSValueConst val) {
    int type = JS_GetTypedArrayType(val);
    return type == JS_TYPED_ARRAY_UINT8 || type == JS_TYPED_ARRAY_UINT8C;
}

static int tjs_mqtt_append_bytes(JSContext *ctx, DynBuf *dbuf, const uint8_t *buf, size_t len) {
    if (dbuf->size > 0) {
        uint8_t last = dbuf->buf[dbuf->size - 1];
        if (last != '\n' && last != '\r' && dbuf_putc(dbuf, '\n')) {
            return -1;
        }
    }
    if (dbuf_put(dbuf, buf, len)) {
        return -1;
    }
    if (len > 0) {
        uint8_t last = buf[len - 1];
        if (last != '\n' && last != '\r' && dbuf_putc(dbuf, '\n')) {
            return -1;
        }
    }
    return 0;
}

static int tjs_mqtt_append_pem_value(JSContext *ctx, DynBuf *dbuf, JSValueConst val) {
    if (JS_IsUndefined(val) || JS_IsNull(val)) {
        return 0;
    }

    if (tjs_mqtt_is_uint8_array(val)) {
        size_t len = 0;
        const uint8_t *buf = JS_GetUint8Array(ctx, &len, val);
        if (!buf) {
            return -1;
        }
        return tjs_mqtt_append_bytes(ctx, dbuf, buf, len);
    }

    size_t len = 0;
    const char *str = JS_ToCStringLen(ctx, &len, val);
    if (!str) {
        return -1;
    }
    int rc = tjs_mqtt_append_bytes(ctx, dbuf, (const uint8_t *) str, len);
    JS_FreeCString(ctx, str);
    return rc;
}

static int tjs_mqtt_append_pem_option(JSContext *ctx, DynBuf *dbuf, JSValueConst val) {
    if (JS_IsArray(val)) {
        JSValue js_length = JS_GetPropertyStr(ctx, val, "length");
        uint64_t len = 0;

        if (JS_ToIndex(ctx, &len, js_length)) {
            JS_FreeValue(ctx, js_length);
            return -1;
        }
        JS_FreeValue(ctx, js_length);

        for (uint64_t i = 0; i < len; i++) {
            JSValue item = JS_GetPropertyUint32(ctx, val, (uint32_t) i);
            if (JS_IsException(item)) {
                return -1;
            }
            int rc = tjs_mqtt_append_pem_value(ctx, dbuf, item);
            JS_FreeValue(ctx, item);
            if (rc) {
                return -1;
            }
        }
        return 0;
    }

    return tjs_mqtt_append_pem_value(ctx, dbuf, val);
}

static int tjs_mqtt_read_tls_options(JSContext *ctx, JSValueConst opts, TJSMqtt *m) {
    JSValue ca = JS_GetPropertyStr(ctx, opts, "ca");
    if (JS_IsException(ca)) {
        return -1;
    }
    if (!JS_IsUndefined(ca) && !JS_IsNull(ca)) {
        if (tjs_mqtt_append_pem_option(ctx, &m->ca_pem, ca)) {
            JS_FreeValue(ctx, ca);
            return -1;
        }
    }
    JS_FreeValue(ctx, ca);

    JSValue cert_val = JS_GetPropertyStr(ctx, opts, "cert");
    JSValue key_val = JS_GetPropertyStr(ctx, opts, "key");
    bool has_cert = !JS_IsUndefined(cert_val) && !JS_IsNull(cert_val);
    bool has_key = !JS_IsUndefined(key_val) && !JS_IsNull(key_val);

    if (JS_IsException(cert_val) || JS_IsException(key_val)) {
        JS_FreeValue(ctx, key_val);
        JS_FreeValue(ctx, cert_val);
        return -1;
    }

    if (has_cert && has_key) {
        if (tjs_mqtt_append_pem_option(ctx, &m->cert_pem, cert_val) ||
            tjs_mqtt_append_pem_option(ctx, &m->key_pem, key_val)) {
            JS_FreeValue(ctx, key_val);
            JS_FreeValue(ctx, cert_val);
            return -1;
        }
    }

    JS_FreeValue(ctx, key_val);
    JS_FreeValue(ctx, cert_val);
    return 0;
}

static void tjs_mqtt_op_free_rt(JSRuntime *rt, TJSMqttOp *op) {
    if (!op) {
        return;
    }

    for (size_t i = 0; i < op->topics_len; i++) {
        js_free_rt(rt, op->topics[i]);
    }

    js_free_rt(rt, op->topics);
    js_free_rt(rt, op->topic_qos);
    js_free_rt(rt, op->topic);
    js_free_rt(rt, op->payload);
    js_free_rt(rt, op);
}

static void tjs_mqtt_cleanup_rt(JSRuntime *rt, TJSMqtt *m) {
    if (!m) {
        return;
    }

    for (int i = 0; i < MQTT_CALLBACK_MAX; i++) {
        JS_FreeValueRT(rt, m->callbacks[i]);
    }

    struct list_head *el, *el1;
    list_for_each_safe(el, el1, &m->pending_ops) {
        TJSMqttOp *op = list_entry(el, TJSMqttOp, link);
        list_del(&op->link);
        tjs_mqtt_op_free_rt(rt, op);
    }

    tjs_mqtt_op_free_rt(rt, m->inflight);
    if (m->vhost) {
        lws_vhost_destroy(m->vhost);
        m->vhost = NULL;
    }
    dbuf_free(&m->recv_buf);
    dbuf_free(&m->ws_rx_buf);
    dbuf_free(&m->ca_pem);
    dbuf_free(&m->cert_pem);
    dbuf_free(&m->key_pem);
    js_free_rt(rt, m->recv_topic);
    js_free_rt(rt, m->host);
    js_free_rt(rt, m->servername);
    js_free_rt(rt, m->path);
    js_free_rt(rt, m->client_id);
    js_free_rt(rt, m->username);
    js_free_rt(rt, m->password);
    js_free_rt(rt, m->will_topic);
    js_free_rt(rt, m->will_message);
    js_free_rt(rt, m);
}

static void tjs_mqtt_finalizer(JSRuntime *rt, JSValue val) {
    TJSMqtt *m = JS_GetOpaque(val, tjs_mqtt_class_id);
    tjs_mqtt_cleanup_rt(rt, m);
}

static void tjs_mqtt_mark(JSRuntime *rt, JSValue val, JS_MarkFunc *mark_func) {
    TJSMqtt *m = JS_GetOpaque(val, tjs_mqtt_class_id);
    if (!m) {
        return;
    }

    for (int i = 0; i < MQTT_CALLBACK_MAX; i++) {
        JS_MarkValue(rt, m->callbacks[i], mark_func);
    }
}

static JSClassDef tjs_mqtt_class = {
    "MQTTClient",
    .finalizer = tjs_mqtt_finalizer,
    .gc_mark = tjs_mqtt_mark,
};

static TJSMqtt *tjs_mqtt_get(JSContext *ctx, JSValue obj) {
    return JS_GetOpaque2(ctx, obj, tjs_mqtt_class_id);
}

static uint32_t tjs_mqtt_pending_op_count(TJSMqtt *m) {
    uint32_t count = 0;
    struct list_head *el;

    list_for_each(el, &m->pending_ops) {
        count++;
    }

    return count;
}

static void tjs_mqtt_call_callback(TJSMqtt *m, int event, int argc, JSValue *argv) {
    JSContext *ctx = m->ctx;
    TJSRuntime *qrt = TJS_GetRuntime(ctx);
    JSValue cb = m->callbacks[event];

    if (qrt->freeing || !JS_IsFunction(ctx, cb)) {
        for (int i = 0; i < argc; i++) {
            JS_FreeValue(ctx, argv[i]);
        }
        return;
    }

    tjs_call_handler(ctx, cb, argc, argv);

    for (int i = 0; i < argc; i++) {
        JS_FreeValue(ctx, argv[i]);
    }
}

static void tjs_mqtt_maybe_request_writable(TJSMqtt *m) {
    struct lws_context *lws_ctx;

    if (!m->wsi) {
        return;
    }

    lws_callback_on_writable(m->wsi);
    lws_ctx = tjs__lws_get_context(m->ctx);
    if (lws_ctx) {
        lws_cancel_service(lws_ctx);
    }
}

static int tjs_mqtt_dbuf_put_u8(DynBuf *dbuf, uint8_t value) {
    return dbuf_put(dbuf, &value, 1);
}

static int tjs_mqtt_dbuf_put_u16(DynBuf *dbuf, uint16_t value) {
    uint8_t buf[2] = {
        (uint8_t) (value >> 8),
        (uint8_t) value,
    };
    return dbuf_put(dbuf, buf, sizeof(buf));
}

static int tjs_mqtt_dbuf_put_string(DynBuf *dbuf, const char *str) {
    size_t len = str ? strlen(str) : 0;

    if (len > UINT16_MAX) {
        return -1;
    }

    if (tjs_mqtt_dbuf_put_u16(dbuf, (uint16_t) len)) {
        return -1;
    }

    return len ? dbuf_put(dbuf, (const uint8_t *) str, len) : 0;
}

static int tjs_mqtt_dbuf_put_remaining_length(DynBuf *dbuf, uint32_t value) {
    do {
        uint8_t encoded = value % 128;
        value /= 128;
        if (value) {
            encoded |= 0x80;
        }
        if (tjs_mqtt_dbuf_put_u8(dbuf, encoded)) {
            return -1;
        }
    } while (value);

    return 0;
}

static int tjs_mqtt_dbuf_put_fixed_header(DynBuf *dbuf, uint8_t packet_type_flags, uint32_t remaining_len) {
    if (remaining_len > TJS_MQTT_WS_MAX_REMAINING_LENGTH) {
        return -1;
    }

    if (tjs_mqtt_dbuf_put_u8(dbuf, packet_type_flags)) {
        return -1;
    }

    return tjs_mqtt_dbuf_put_remaining_length(dbuf, remaining_len);
}

static int tjs_mqtt_ws_encode_connect(TJSMqtt *m, DynBuf *out) {
    DynBuf var;
    DynBuf payload;
    uint8_t flags = 0;
    int rc = -1;

    tjs_dbuf_init(m->ctx, &var);
    tjs_dbuf_init(m->ctx, &payload);

    if (tjs_mqtt_dbuf_put_string(&var, "MQTT") || tjs_mqtt_dbuf_put_u8(&var, MQTT_VER_3_1_1)) {
        goto done;
    }

    if (m->connect_param.clean_start) {
        flags |= 0x02;
    }
    if (m->will_topic) {
        flags |= 0x04;
        flags |= ((uint8_t) m->connect_param.will_param.qos & 0x03) << 3;
        if (m->connect_param.will_param.retain) {
            flags |= 0x20;
        }
    }
    if (m->password) {
        flags |= 0x40;
    }
    if (m->username) {
        flags |= 0x80;
    }

    if (tjs_mqtt_dbuf_put_u8(&var, flags) || tjs_mqtt_dbuf_put_u16(&var, m->connect_param.keep_alive)) {
        goto done;
    }

    if (tjs_mqtt_dbuf_put_string(&payload, m->client_id ? m->client_id : "")) {
        goto done;
    }
    if (m->will_topic) {
        if (tjs_mqtt_dbuf_put_string(&payload, m->will_topic) ||
            tjs_mqtt_dbuf_put_string(&payload, m->will_message ? m->will_message : "")) {
            goto done;
        }
    }
    if (m->username && tjs_mqtt_dbuf_put_string(&payload, m->username)) {
        goto done;
    }
    if (m->password && tjs_mqtt_dbuf_put_string(&payload, m->password)) {
        goto done;
    }

    if (tjs_mqtt_dbuf_put_fixed_header(out, LMQCP_CTOS_CONNECT << 4, (uint32_t) (var.size + payload.size)) ||
        dbuf_put(out, var.buf, var.size) || dbuf_put(out, payload.buf, payload.size)) {
        goto done;
    }

    rc = 0;

done:
    dbuf_free(&payload);
    dbuf_free(&var);
    return rc;
}

static int tjs_mqtt_ws_encode_publish(TJSMqtt *m, TJSMqttOp *op, DynBuf *out) {
    uint32_t remaining_len;
    uint8_t flags = (uint8_t) (LMQCP_PUBLISH << 4);

    if (op->qos < 0 || op->qos > 1 || strlen(op->topic) > UINT16_MAX) {
        return -1;
    }

    flags |= (uint8_t) ((op->qos & 0x03) << 1);
    if (op->retain) {
        flags |= 0x01;
    }

    remaining_len = (uint32_t) (2 + strlen(op->topic) + op->payload_len + (op->qos > 0 ? 2 : 0));
    if (tjs_mqtt_dbuf_put_fixed_header(out, flags, remaining_len) || tjs_mqtt_dbuf_put_string(out, op->topic)) {
        return -1;
    }

    if (op->qos > 0 && tjs_mqtt_dbuf_put_u16(out, op->packet_id)) {
        return -1;
    }

    return op->payload_len ? dbuf_put(out, op->payload, op->payload_len) : 0;
}

static int tjs_mqtt_ws_encode_topic_op(TJSMqtt *m, TJSMqttOp *op, DynBuf *out) {
    size_t remaining = op->topics_len - op->topics_sent;
    size_t batch_len = remaining > TJS_MQTT_MAX_TOPICS_PER_OP ? TJS_MQTT_MAX_TOPICS_PER_OP : remaining;
    uint8_t type_flags = op->type == MQTT_OP_SUBSCRIBE ? ((uint8_t) LMQCP_CTOS_SUBSCRIBE << 4) | 0x02
                                                       : ((uint8_t) LMQCP_CTOS_UNSUBSCRIBE << 4) | 0x02;
    uint32_t remaining_len = 2;

    if (!batch_len) {
        return -1;
    }

    for (size_t i = 0; i < batch_len; i++) {
        size_t index = op->topics_sent + i;
        size_t topic_len = strlen(op->topics[index]);
        if (topic_len > UINT16_MAX) {
            return -1;
        }
        remaining_len += (uint32_t) (2 + topic_len + (op->type == MQTT_OP_SUBSCRIBE ? 1 : 0));
    }

    if (tjs_mqtt_dbuf_put_fixed_header(out, type_flags, remaining_len) || tjs_mqtt_dbuf_put_u16(out, op->packet_id)) {
        return -1;
    }

    for (size_t i = 0; i < batch_len; i++) {
        size_t index = op->topics_sent + i;
        if (tjs_mqtt_dbuf_put_string(out, op->topics[index])) {
            return -1;
        }
        if (op->type == MQTT_OP_SUBSCRIBE && tjs_mqtt_dbuf_put_u8(out, (uint8_t) op->topic_qos[index])) {
            return -1;
        }
    }

    op->topics_batch_len = batch_len;
    return 0;
}

static int tjs_mqtt_ws_write_packet(TJSMqtt *m, const uint8_t *data, size_t len) {
    uint8_t *buf = js_malloc(m->ctx, LWS_PRE + len);
    int n;

    if (!buf) {
        return -1;
    }

    memcpy(buf + LWS_PRE, data, len);
    n = lws_write(m->wsi, buf + LWS_PRE, len, LWS_WRITE_BINARY);
    js_free(m->ctx, buf);

    return n == (int) len ? 0 : -1;
}

static int tjs_mqtt_ws_send_inflight(TJSMqtt *m, TJSMqttOp *op) {
    DynBuf packet;
    int rc = -1;

    tjs_dbuf_init(m->ctx, &packet);

    switch (op->type) {
        case MQTT_OP_PUBLISH:
            rc = tjs_mqtt_ws_encode_publish(m, op, &packet);
            break;
        case MQTT_OP_SUBSCRIBE:
        case MQTT_OP_UNSUBSCRIBE:
            rc = tjs_mqtt_ws_encode_topic_op(m, op, &packet);
            break;
        default:
            break;
    }

    if (!rc) {
        rc = tjs_mqtt_ws_write_packet(m, packet.buf, packet.size);
    }

    if (!rc && op->type == MQTT_OP_PUBLISH) {
        op->payload_sent = op->payload_len;
    }

    dbuf_free(&packet);
    return rc;
}

static void tjs_mqtt_ws_schedule_keepalive(TJSMqtt *m) {
    if (!m->wsi || !m->connect_param.keep_alive) {
        return;
    }

    lws_set_timer_usecs(m->wsi, (lws_usec_t) m->connect_param.keep_alive * LWS_USEC_PER_SEC);
}

static void tjs_mqtt_tcp_schedule_keepalive(TJSMqtt *m) {
    if (!m->wsi || !m->connect_param.keep_alive) {
        return;
    }

    lws_set_timer_usecs(m->wsi, (lws_usec_t) m->connect_param.keep_alive * LWS_USEC_PER_SEC);
}

static void tjs_mqtt_tcp_note_activity(TJSMqtt *m) {
    m->tcp_ping_send_pending = false;
    tjs_mqtt_tcp_schedule_keepalive(m);
}

static int tjs_mqtt_tcp_send_pingreq(TJSMqtt *m) {
    uint8_t buf[LWS_PRE + 2];
    struct lws *nwsi;

    if (!m->wsi) {
        return -1;
    }

    nwsi = lws_get_network_wsi(m->wsi);
    buf[LWS_PRE] = LMQCP_CTOS_PINGREQ << 4;
    buf[LWS_PRE + 1] = 0;

    return lws_write(nwsi, buf + LWS_PRE, 2, LWS_WRITE_BINARY) == 2 ? 0 : -1;
}

static int tjs_mqtt_tcp_send_pending_ping(TJSMqtt *m) {
    if (!m->tcp_ping_send_pending) {
        return 0;
    }

    m->tcp_ping_send_pending = false;
    if (tjs_mqtt_tcp_send_pingreq(m)) {
        return -1;
    }
    tjs_mqtt_tcp_schedule_keepalive(m);
    return 0;
}

static int tjs_mqtt_send_topic_op_batch(TJSMqtt *m, TJSMqttOp *op) {
    size_t remaining = op->topics_len - op->topics_sent;
    size_t batch_len = remaining > TJS_MQTT_MAX_TOPICS_PER_OP ? TJS_MQTT_MAX_TOPICS_PER_OP : remaining;
    lws_mqtt_topic_elem_t *topics;
    lws_mqtt_subscribe_param_t sub;
    int rc;

    if (!batch_len) {
        return 0;
    }

    topics = js_mallocz(m->ctx, sizeof(*topics) * batch_len);
    if (!topics) {
        return -1;
    }

    for (size_t i = 0; i < batch_len; i++) {
        size_t index = op->topics_sent + i;
        topics[i].name = op->topics[index];
        topics[i].qos = (lws_mqtt_qos_levels_t) op->topic_qos[index];
    }

    memset(&sub, 0, sizeof(sub));
    sub.packet_id = op->packet_id;
    sub.num_topics = (uint32_t) batch_len;
    sub.topic = topics;
    op->topics_batch_len = batch_len;

    rc = op->type == MQTT_OP_SUBSCRIBE ? lws_mqtt_client_send_subcribe(m->wsi, &sub)
                                       : lws_mqtt_client_send_unsubcribe(m->wsi, &sub);
    js_free(m->ctx, topics);

    if (rc) {
        op->topics_batch_len = 0;
    }

    return rc;
}

static int tjs_mqtt_send_publish_chunk(TJSMqtt *m, TJSMqttOp *op) {
    lws_mqtt_publish_param_t pub;
    size_t remaining;
    size_t chunk_size;
    int is_complete;

    remaining = op->payload_len - op->payload_sent;
    if (!remaining && op->payload_sent) {
        return 0;
    }

    if (!remaining) {
        chunk_size = 0;
        is_complete = 1;
    } else {
        chunk_size = TJS_MQTT_PUBLISH_CHUNK_SIZE;
        if (chunk_size > remaining) {
            chunk_size = remaining;
        }

        is_complete = chunk_size == remaining;
    }

    memset(&pub, 0, sizeof(pub));
    pub.topic = op->topic;
    pub.topic_len = (uint16_t) strlen(op->topic);
    pub.payload_len = (uint32_t) op->payload_len;
    pub.qos = (lws_mqtt_qos_levels_t) op->qos;
    pub.retain = op->retain;
    pub.packet_id = op->packet_id;

    op->payload_sent += chunk_size;

    if (lws_mqtt_client_send_publish(m->wsi,
                                     &pub,
                                     op->payload + (op->payload_sent - chunk_size),
                                     (uint32_t) chunk_size,
                                     is_complete)) {
        return -1;
    }

    return 0;
}

static void tjs_mqtt_finish_inflight(TJSMqtt *m, int event) {
    TJSMqttOp *op = m->inflight;

    if (!op) {
        return;
    }

    if ((event == MQTT_CALLBACK_SUBSCRIBED || event == MQTT_CALLBACK_UNSUBSCRIBED) &&
        (op->type == MQTT_OP_SUBSCRIBE || op->type == MQTT_OP_UNSUBSCRIBE) && op->topics_batch_len) {
        op->topics_sent += op->topics_batch_len;
        op->topics_batch_len = 0;

        if (op->topics_sent < op->topics_len) {
            tjs_mqtt_maybe_request_writable(m);
            return;
        }
    }

    m->inflight = NULL;
    tjs_mqtt_op_free_rt(JS_GetRuntime(m->ctx), op);
    tjs_mqtt_call_callback(m, event, 0, NULL);
    tjs_mqtt_maybe_request_writable(m);
}

static void tjs_mqtt_finish_inflight_error(TJSMqtt *m, int event, const char *message) {
    TJSMqttOp *op = m->inflight;
    JSValue err;

    if (!op) {
        return;
    }

    m->inflight = NULL;
    tjs_mqtt_op_free_rt(JS_GetRuntime(m->ctx), op);

    err = JS_NewError(m->ctx);
    JS_DefinePropertyValueStr(m->ctx, err, "message", JS_NewString(m->ctx, message), JS_PROP_C_W_E);
    tjs_mqtt_call_callback(m, event, 1, &err);
    tjs_mqtt_maybe_request_writable(m);
}

static void tjs_mqtt_finish_inflight_no_callback(TJSMqtt *m) {
    TJSMqttOp *op = m->inflight;

    if (!op) {
        return;
    }

    m->inflight = NULL;
    tjs_mqtt_op_free_rt(JS_GetRuntime(m->ctx), op);
    tjs_mqtt_maybe_request_writable(m);
}

static int tjs_mqtt_ws_decode_remaining_length(const uint8_t *data, size_t len, size_t *offset, uint32_t *out) {
    uint32_t multiplier = 1;
    uint32_t value = 0;
    size_t pos = *offset;

    for (int i = 0; i < 4; i++) {
        uint8_t encoded;

        if (pos >= len) {
            return 0;
        }

        encoded = data[pos++];
        value += (uint32_t) (encoded & 127) * multiplier;
        if (!(encoded & 128)) {
            *offset = pos;
            *out = value;
            return 1;
        }
        multiplier *= 128;
    }

    return -1;
}

static int tjs_mqtt_ws_read_u16(const uint8_t *data, size_t len, size_t *offset, uint16_t *out) {
    if (*offset + 2 > len) {
        return -1;
    }

    *out = (uint16_t) ((data[*offset] << 8) | data[*offset + 1]);
    *offset += 2;
    return 0;
}

static int tjs_mqtt_ws_send_simple_packet(TJSMqtt *m, uint8_t packet_type_flags, uint16_t packet_id) {
    uint8_t buf[LWS_PRE + 4];
    size_t len = 0;

    buf[LWS_PRE + len++] = packet_type_flags;
    if (packet_id) {
        buf[LWS_PRE + len++] = 2;
        buf[LWS_PRE + len++] = (uint8_t) (packet_id >> 8);
        buf[LWS_PRE + len++] = (uint8_t) packet_id;
    } else {
        buf[LWS_PRE + len++] = 0;
    }

    return lws_write(m->wsi, buf + LWS_PRE, len, LWS_WRITE_BINARY) == (int) len ? 0 : -1;
}

static void tjs_mqtt_ws_note_activity(TJSMqtt *m) {
    m->ws_ping_pending = false;
    m->ws_ping_send_pending = false;
    tjs_mqtt_ws_schedule_keepalive(m);
}

static int tjs_mqtt_ws_send_pending_ping(TJSMqtt *m) {
    if (!m->ws_ping_send_pending) {
        return 0;
    }
    if (m->ws_ping_pending) {
        return -1;
    }

    m->ws_ping_send_pending = false;
    if (tjs_mqtt_ws_send_simple_packet(m, LMQCP_CTOS_PINGREQ << 4, 0)) {
        return -1;
    }
    m->ws_ping_pending = true;
    tjs_mqtt_ws_schedule_keepalive(m);
    return 0;
}

static int tjs_mqtt_ws_handle_packet(TJSMqtt *m, const uint8_t *data, size_t len) {
    JSContext *ctx = m->ctx;
    size_t offset = 1;
    uint8_t packet_type;
    uint8_t flags;
    uint32_t remaining_len;
    size_t end;

    if (!len) {
        return 0;
    }

    packet_type = data[0] >> 4;
    flags = data[0] & 0x0f;

    int rl_rc = tjs_mqtt_ws_decode_remaining_length(data, len, &offset, &remaining_len);
    if (rl_rc <= 0 || offset + remaining_len > len) {
        return -1;
    }

    end = offset + remaining_len;

    switch (packet_type) {
        case LMQCP_STOC_CONNACK: {
            uint8_t session_present;
            uint8_t return_code;
            JSValue packet;

            if (remaining_len < 2) {
                return -1;
            }

            session_present = data[offset] & 0x01;
            return_code = data[offset + 1];
            if (return_code != 0) {
                char message[64];
                snprintf(message, sizeof(message), "MQTT CONNACK rejected (%u)", return_code);
                JSValue err = JS_NewString(ctx, message);
                tjs_mqtt_call_callback(m, MQTT_CALLBACK_ERROR, 1, &err);
                return -1;
            }

            m->connected = true;
            tjs_mqtt_ws_note_activity(m);
            packet = JS_NewObjectProto(ctx, JS_NULL);
            JS_DefinePropertyValueStr(ctx, packet, "returnCode", JS_NewInt32(ctx, return_code), JS_PROP_C_W_E);
            JS_DefinePropertyValueStr(ctx, packet, "sessionPresent", JS_NewBool(ctx, session_present), JS_PROP_C_W_E);
            tjs_mqtt_call_callback(m, MQTT_CALLBACK_CONNECT, 1, &packet);
            tjs_mqtt_maybe_request_writable(m);
            break;
        }

        case LMQCP_PUBLISH: {
            uint16_t topic_len;
            char *topic;
            int qos = (flags >> 1) & 0x03;
            bool retain = flags & 0x01;
            uint16_t packet_id = 0;

            if (tjs_mqtt_ws_read_u16(data, end, &offset, &topic_len) || offset + topic_len > end) {
                return -1;
            }

            topic = js_malloc(ctx, topic_len + 1);
            if (!topic) {
                return -1;
            }
            memcpy(topic, data + offset, topic_len);
            topic[topic_len] = '\0';
            offset += topic_len;

            if (qos > 0 && tjs_mqtt_ws_read_u16(data, end, &offset, &packet_id)) {
                js_free(ctx, topic);
                return -1;
            }

            JSValue args[4] = {
                JS_NewString(ctx, topic),
                JS_NewArrayBufferCopy(ctx, data + offset, end - offset),
                JS_NewInt32(ctx, qos),
                JS_NewBool(ctx, retain),
            };
            js_free(ctx, topic);

            tjs_mqtt_call_callback(m, MQTT_CALLBACK_MESSAGE, 4, args);
            tjs_mqtt_ws_note_activity(m);
            if (qos == 1) {
                return tjs_mqtt_ws_send_simple_packet(m, LMQCP_PUBACK << 4, packet_id);
            }
            break;
        }

        case LMQCP_PUBACK:
            tjs_mqtt_ws_note_activity(m);
            tjs_mqtt_finish_inflight(m, MQTT_CALLBACK_ACK);
            break;

        case LMQCP_STOC_SUBACK:
            tjs_mqtt_ws_note_activity(m);
            if (remaining_len < 3) {
                return -1;
            }
            for (size_t i = offset + 2; i < end; i++) {
                if (data[i] == FAILURE_QOS_LEVEL || data[i] > QOS2) {
                    tjs_mqtt_finish_inflight_error(m, MQTT_CALLBACK_SUBSCRIBED, "MQTT subscribe failed");
                    break;
                }
            }
            if (!m->inflight) {
                break;
            }
            tjs_mqtt_finish_inflight(m, MQTT_CALLBACK_SUBSCRIBED);
            break;

        case LMQCP_STOC_UNSUBACK:
            tjs_mqtt_ws_note_activity(m);
            tjs_mqtt_finish_inflight(m, MQTT_CALLBACK_UNSUBSCRIBED);
            break;

        case LMQCP_STOC_PINGRESP:
            tjs_mqtt_ws_note_activity(m);
            break;

        default:
            break;
    }

    return 0;
}

static int tjs_mqtt_ws_handle_receive(TJSMqtt *m, struct lws *wsi, const void *in, size_t len) {
    bool is_first = (bool) lws_is_first_fragment(wsi);
    bool is_final = (bool) lws_is_final_fragment(wsi);

    if (is_first) {
        m->ws_rx_buf.size = 0;
    }

    if (dbuf_put(&m->ws_rx_buf, in, len)) {
        return -1;
    }

    if (!is_final || lws_remaining_packet_payload(wsi) != 0) {
        return 0;
    }

    if (tjs_mqtt_ws_handle_packet(m, m->ws_rx_buf.buf, m->ws_rx_buf.size)) {
        return -1;
    }
    m->ws_rx_buf.size = 0;
    return 0;
}

static void tjs_mqtt_release_handle(TJSMqtt *m, struct lws *wsi) {
    if (JS_IsUndefined(m->this_val)) {
        return;
    }

    tjs__lws_conn_unref(m->ctx);

    JSValue this_val = m->this_val;
    m->this_val = JS_UNDEFINED;
    lws_set_wsi_user(wsi, NULL);
    JS_FreeValue(m->ctx, this_val);
}

static void tjs_mqtt_emit_close(TJSMqtt *m, struct lws *wsi, bool user_initiated) {
    if (m->closed) {
        return;
    }

    m->closed = true;
    m->connected = false;
    m->wsi = NULL;
    m->ws_ping_pending = false;
    m->ws_ping_send_pending = false;
    m->tcp_ping_send_pending = false;

    JSValue arg = JS_NewBool(m->ctx, user_initiated);
    tjs_mqtt_call_callback(m, MQTT_CALLBACK_CLOSE, 1, &arg);
    tjs_mqtt_release_handle(m, wsi);
}

static int tjs_mqtt_callback(struct lws *wsi, enum lws_callback_reasons reason, void *user, void *in, size_t len) {
    TJSMqtt *m = (TJSMqtt *) user;

    if (!m) {
        return 0;
    }

    switch (reason) {
        case LWS_CALLBACK_MQTT_CLIENT_ESTABLISHED: {
            JSContext *ctx = m->ctx;
            JSValue packet = JS_NewObjectProto(ctx, JS_NULL);

            m->wsi = wsi;
            m->connected = true;
            m->tcp_ping_send_pending = false;
            JS_DefinePropertyValueStr(ctx, packet, "returnCode", JS_NewInt32(ctx, 0), JS_PROP_C_W_E);
            JS_DefinePropertyValueStr(ctx, packet, "sessionPresent", JS_FALSE, JS_PROP_C_W_E);
            tjs_mqtt_call_callback(m, MQTT_CALLBACK_CONNECT, 1, &packet);
            tjs_mqtt_tcp_note_activity(m);
            tjs_mqtt_maybe_request_writable(m);
            break;
        }

        case LWS_CALLBACK_MQTT_CLIENT_RX: {
            lws_mqtt_publish_param_t *pub = (lws_mqtt_publish_param_t *) in;

            if (pub->payload_pos == 0) {
                m->recv_buf.size = 0;
                js_free(m->ctx, m->recv_topic);
                m->recv_topic = js_strdup(m->ctx, pub->topic ? pub->topic : "");
                m->recv_qos = pub->qos;
                m->recv_retain = pub->retain;
            }

            if (dbuf_put(&m->recv_buf, pub->payload, len)) {
                return -1;
            }

            if (pub->payload_pos + len == pub->payload_len) {
                JSContext *ctx = m->ctx;
                JSValue args[4] = {
                    JS_NewString(ctx, m->recv_topic ? m->recv_topic : ""),
                    JS_NewArrayBufferCopy(ctx, m->recv_buf.buf, m->recv_buf.size),
                    JS_NewInt32(ctx, m->recv_qos),
                    JS_NewBool(ctx, m->recv_retain),
                };

                tjs_mqtt_call_callback(m, MQTT_CALLBACK_MESSAGE, 4, args);
                m->recv_buf.size = 0;
                tjs_mqtt_tcp_note_activity(m);
            }
            break;
        }

        case LWS_CALLBACK_MQTT_ACK:
            tjs_mqtt_tcp_note_activity(m);
            tjs_mqtt_finish_inflight(m, MQTT_CALLBACK_ACK);
            break;

        case LWS_CALLBACK_MQTT_SUBSCRIBED:
            tjs_mqtt_tcp_note_activity(m);
            if (in && len) {
                tjs_mqtt_finish_inflight_error(m, MQTT_CALLBACK_SUBSCRIBED, "MQTT subscribe failed");
            } else {
                tjs_mqtt_finish_inflight(m, MQTT_CALLBACK_SUBSCRIBED);
            }
            break;

        case LWS_CALLBACK_MQTT_UNSUBSCRIBED:
            tjs_mqtt_tcp_note_activity(m);
            tjs_mqtt_finish_inflight(m, MQTT_CALLBACK_UNSUBSCRIBED);
            break;

        case LWS_CALLBACK_MQTT_CLIENT_WRITEABLE: {
            if (m->closing) {
                lws_set_timeout(wsi, PENDING_TIMEOUT_USER_OK, 1);
                return 0;
            }

            if (m->inflight) {
                TJSMqttOp *op = m->inflight;

                if (op->type == MQTT_OP_PUBLISH && op->payload_sent < op->payload_len) {
                    if (tjs_mqtt_send_publish_chunk(m, op)) {
                        return -1;
                    }
                    tjs_mqtt_tcp_note_activity(m);
                    if (op->qos == 0 && op->payload_sent >= op->payload_len) {
                        tjs_mqtt_finish_inflight_no_callback(m);
                    }
                } else if ((op->type == MQTT_OP_SUBSCRIBE || op->type == MQTT_OP_UNSUBSCRIBE) &&
                           !op->topics_batch_len && op->topics_sent < op->topics_len) {
                    if (tjs_mqtt_send_topic_op_batch(m, op)) {
                        return -1;
                    }
                    tjs_mqtt_tcp_note_activity(m);
                } else if (tjs_mqtt_tcp_send_pending_ping(m)) {
                    return -1;
                }
                break;
            }

            if (list_empty(&m->pending_ops)) {
                if (tjs_mqtt_tcp_send_pending_ping(m)) {
                    return -1;
                }
                break;
            }

            TJSMqttOp *op = list_entry(m->pending_ops.next, TJSMqttOp, link);
            list_del(&op->link);
            m->inflight = op;

            switch (op->type) {
                case MQTT_OP_PUBLISH: {
                    if (tjs_mqtt_send_publish_chunk(m, op)) {
                        return -1;
                    }
                    tjs_mqtt_tcp_note_activity(m);
                    if (op->qos == 0 && op->payload_sent >= op->payload_len) {
                        tjs_mqtt_finish_inflight_no_callback(m);
                    }
                    break;
                }

                case MQTT_OP_SUBSCRIBE:
                case MQTT_OP_UNSUBSCRIBE: {
                    if (tjs_mqtt_send_topic_op_batch(m, op)) {
                        return -1;
                    }
                    tjs_mqtt_tcp_note_activity(m);
                    break;
                }
            }
            break;
        }

        case LWS_CALLBACK_TIMER:
            if (m->connected && !m->closing) {
                m->tcp_ping_send_pending = true;
                tjs_mqtt_maybe_request_writable(m);
            }
            break;

        case LWS_CALLBACK_CLIENT_CONNECTION_ERROR: {
            JSValue err = JS_NewString(m->ctx, in ? (const char *) in : "");

            tjs_mqtt_call_callback(m, MQTT_CALLBACK_ERROR, 1, &err);
            tjs_mqtt_emit_close(m, wsi, false);
            break;
        }

        case LWS_CALLBACK_MQTT_CLIENT_CLOSED:
            tjs_mqtt_emit_close(m, wsi, m->closing);
            break;

        default:
            break;
    }

    return 0;
}

const struct lws_protocols tjs_mqtt_protocol = {
    .name = "mqtt",
    .callback = tjs_mqtt_callback,
    .per_session_data_size = 0,
    .rx_buffer_size = 0,
};

static int tjs_mqtt_ws_callback(struct lws *wsi, enum lws_callback_reasons reason, void *user, void *in, size_t len) {
    TJSMqtt *m = (TJSMqtt *) user;

    if (!m) {
        return 0;
    }

    switch (reason) {
        case LWS_CALLBACK_CLIENT_ESTABLISHED: {
            DynBuf packet;
            int rc;

            m->wsi = wsi;
            m->ws_established = true;
            tjs_dbuf_init(m->ctx, &packet);
            rc = tjs_mqtt_ws_encode_connect(m, &packet);
            if (!rc) {
                rc = tjs_mqtt_ws_write_packet(m, packet.buf, packet.size);
            }
            dbuf_free(&packet);
            if (rc) {
                return -1;
            }
            m->ws_connect_sent = true;
            tjs_mqtt_ws_schedule_keepalive(m);
            break;
        }

        case LWS_CALLBACK_CLIENT_RECEIVE:
            return tjs_mqtt_ws_handle_receive(m, wsi, in, len);

        case LWS_CALLBACK_CLIENT_WRITEABLE: {
            if (m->closing) {
                if (m->connected) {
                    (void) tjs_mqtt_ws_send_simple_packet(m, LMQCP_DISCONNECT << 4, 0);
                }
                lws_set_timeout(wsi, PENDING_TIMEOUT_USER_OK, 1);
                return 0;
            }

            if (m->inflight) {
                TJSMqttOp *op = m->inflight;

                if (op->type == MQTT_OP_PUBLISH && op->payload_sent < op->payload_len) {
                    if (tjs_mqtt_ws_send_inflight(m, op)) {
                        return -1;
                    }
                    m->ws_ping_send_pending = false;
                    tjs_mqtt_ws_schedule_keepalive(m);
                    if (op->qos == 0) {
                        tjs_mqtt_finish_inflight_no_callback(m);
                    }
                } else if ((op->type == MQTT_OP_SUBSCRIBE || op->type == MQTT_OP_UNSUBSCRIBE) &&
                           !op->topics_batch_len && op->topics_sent < op->topics_len) {
                    if (tjs_mqtt_ws_send_inflight(m, op)) {
                        return -1;
                    }
                    m->ws_ping_send_pending = false;
                    tjs_mqtt_ws_schedule_keepalive(m);
                } else if (tjs_mqtt_ws_send_pending_ping(m)) {
                    return -1;
                }
                break;
            }

            if (list_empty(&m->pending_ops)) {
                if (tjs_mqtt_ws_send_pending_ping(m)) {
                    return -1;
                }
                break;
            }

            TJSMqttOp *op = list_entry(m->pending_ops.next, TJSMqttOp, link);
            list_del(&op->link);
            m->inflight = op;

            if (tjs_mqtt_ws_send_inflight(m, op)) {
                return -1;
            }
            m->ws_ping_send_pending = false;
            tjs_mqtt_ws_schedule_keepalive(m);

            if (op->type == MQTT_OP_PUBLISH && op->qos == 0) {
                tjs_mqtt_finish_inflight_no_callback(m);
            }
            break;
        }

        case LWS_CALLBACK_TIMER:
            if (!m->connected || m->closing) {
                return 0;
            }
            if (m->ws_ping_pending || m->ws_ping_send_pending) {
                return -1;
            }
            m->ws_ping_send_pending = true;
            tjs_mqtt_maybe_request_writable(m);
            break;

        case LWS_CALLBACK_CLIENT_CONNECTION_ERROR: {
            JSValue err = JS_NewString(m->ctx, in ? (const char *) in : "");

            tjs_mqtt_call_callback(m, MQTT_CALLBACK_ERROR, 1, &err);
            tjs_mqtt_emit_close(m, wsi, false);
            break;
        }

        case LWS_CALLBACK_CLIENT_CLOSED:
            tjs_mqtt_emit_close(m, wsi, m->closing);
            break;

        default:
            break;
    }

    return 0;
}

const struct lws_protocols tjs_mqtt_ws_protocol = {
    .name = "tjs-mqtt-ws",
    .callback = tjs_mqtt_ws_callback,
    .per_session_data_size = 0,
    .rx_buffer_size = 0,
};

static bool tjs_mqtt_get_bool(JSContext *ctx, JSValue obj, const char *name, bool default_value) {
    JSValue val = JS_GetPropertyStr(ctx, obj, name);
    bool out = default_value;

    if (!JS_IsUndefined(val)) {
        out = JS_ToBool(ctx, val);
    }

    JS_FreeValue(ctx, val);
    return out;
}

static int32_t tjs_mqtt_get_int32(JSContext *ctx, JSValue obj, const char *name, int32_t default_value) {
    JSValue val = JS_GetPropertyStr(ctx, obj, name);
    int32_t out = default_value;

    if (!JS_IsUndefined(val)) {
        JS_ToInt32(ctx, &out, val);
    }

    JS_FreeValue(ctx, val);
    return out;
}

static char *tjs_mqtt_dup_prop(JSContext *ctx, JSValue obj, const char *name) {
    JSValue val = JS_GetPropertyStr(ctx, obj, name);
    char *out = NULL;

    if (!JS_IsUndefined(val) && !JS_IsNull(val)) {
        const char *str = JS_ToCString(ctx, val);
        if (str) {
            out = js_strdup(ctx, str);
            JS_FreeCString(ctx, str);
        }
    }

    JS_FreeValue(ctx, val);
    return out;
}

static JSValue tjs_mqtt_constructor(JSContext *ctx, JSValue new_target, int argc, JSValue *argv) {
    JSValue obj = JS_NewObjectClass(ctx, tjs_mqtt_class_id);
    JSValue opts = argc > 0 ? argv[0] : JS_UNDEFINED;
    TJSMqtt *m;
    struct lws_context *lws_ctx;
    struct lws_client_connect_info cci;
    char *protocol;

    if (JS_IsException(obj)) {
        return obj;
    }

    if (!JS_IsObject(opts)) {
        JS_FreeValue(ctx, obj);
        return JS_ThrowTypeError(ctx, "MQTT options object required");
    }

    m = js_mallocz(ctx, sizeof(*m));
    if (!m) {
        JS_FreeValue(ctx, obj);
        return JS_EXCEPTION;
    }

    m->ctx = ctx;
    init_list_head(&m->pending_ops);
    tjs_dbuf_init(ctx, &m->recv_buf);
    tjs_dbuf_init(ctx, &m->ws_rx_buf);
    tjs_dbuf_init(ctx, &m->ca_pem);
    tjs_dbuf_init(ctx, &m->cert_pem);
    tjs_dbuf_init(ctx, &m->key_pem);
    for (int i = 0; i < MQTT_CALLBACK_MAX; i++) {
        m->callbacks[i] = JS_UNDEFINED;
    }

    m->host = tjs_mqtt_dup_prop(ctx, opts, "host");
    if (!m->host) {
        m->host = tjs_mqtt_dup_prop(ctx, opts, "hostname");
    }
    if (!m->host) {
        m->host = js_strdup(ctx, "localhost");
    }

    protocol = tjs_mqtt_dup_prop(ctx, opts, "protocol");
    if (protocol && (!strcmp(protocol, "wss") || !strcmp(protocol, "mqtt+wss"))) {
        m->transport = TJS_MQTT_TRANSPORT_WS;
        m->use_ssl = true;
    } else if (protocol && (!strcmp(protocol, "ws") || !strcmp(protocol, "mqtt+ws"))) {
        m->transport = TJS_MQTT_TRANSPORT_WS;
        m->use_ssl = false;
    } else {
        m->transport = TJS_MQTT_TRANSPORT_TCP;
        m->use_ssl = protocol && !strcmp(protocol, "mqtts");
    }
    js_free(ctx, protocol);

    if (m->use_ssl) {
        JSValue reject_unauthorized = JS_GetPropertyStr(ctx, opts, "rejectUnauthorized");
        if (!JS_IsUndefined(reject_unauthorized) && !JS_ToBool(ctx, reject_unauthorized)) {
            m->ssl_flags |= LCCSCF_ALLOW_SELFSIGNED;
            m->ssl_flags |= LCCSCF_SKIP_SERVER_CERT_HOSTNAME_CHECK;
            m->ssl_flags |= LCCSCF_ALLOW_EXPIRED;
            m->ssl_flags |= LCCSCF_ALLOW_INSECURE;
        }
        JS_FreeValue(ctx, reject_unauthorized);
    }

    m->port = tjs_mqtt_get_int32(ctx,
                                 opts,
                                 "port",
                                 m->transport == TJS_MQTT_TRANSPORT_WS ? (m->use_ssl ? 443 : 80)
                                                                       : (m->use_ssl ? 8883 : 1883));
    m->servername = tjs_mqtt_dup_prop(ctx, opts, "servername");
    m->path = tjs_mqtt_dup_prop(ctx, opts, "path");
    if (!m->path) {
        m->path = tjs_mqtt_dup_prop(ctx, opts, "pathname");
    }
    if (m->transport == TJS_MQTT_TRANSPORT_WS && (!m->path || !m->path[0])) {
        js_free(ctx, m->path);
        m->path = js_strdup(ctx, "/mqtt");
    }
    m->client_id = tjs_mqtt_dup_prop(ctx, opts, "clientId");
    m->username = tjs_mqtt_dup_prop(ctx, opts, "username");
    m->password = tjs_mqtt_dup_prop(ctx, opts, "password");

    if (m->use_ssl) {
        if (tjs_mqtt_read_tls_options(ctx, opts, m)) {
            tjs_mqtt_cleanup_rt(JS_GetRuntime(ctx), m);
            JS_FreeValue(ctx, obj);
            return JS_EXCEPTION;
        }
    }

    memset(&m->connect_param, 0, sizeof(m->connect_param));

    {
        JSValue will = JS_GetPropertyStr(ctx, opts, "will");
        if (JS_IsObject(will)) {
            m->will_topic = tjs_mqtt_dup_prop(ctx, will, "topic");
            m->will_message = tjs_mqtt_dup_prop(ctx, will, "payload");
            m->connect_param.will_param.qos = (lws_mqtt_qos_levels_t) tjs_mqtt_get_int32(ctx, will, "qos", 0);
            m->connect_param.will_param.retain = tjs_mqtt_get_bool(ctx, will, "retain", false);
        }
        JS_FreeValue(ctx, will);
    }
    m->connect_param.keep_alive = (uint16_t) tjs_mqtt_get_int32(ctx, opts, "keepalive", 60);
    m->connect_param.clean_start = tjs_mqtt_get_bool(ctx, opts, "clean", true);
    m->connect_param.client_id = m->client_id;
    m->connect_param.client_id_nofree = 1;
    m->connect_param.username = m->username;
    m->connect_param.password = m->password;
    m->connect_param.username_nofree = 1;
    m->connect_param.password_nofree = 1;
    if (m->will_topic) {
        m->connect_param.will_param.topic = m->will_topic;
        m->connect_param.will_param.message = m->will_message ? m->will_message : "";
    }

    lws_ctx = tjs__lws_get_context(ctx);
    if (!lws_ctx) {
        tjs_mqtt_cleanup_rt(JS_GetRuntime(ctx), m);
        JS_FreeValue(ctx, obj);
        return JS_ThrowInternalError(ctx, "failed to create lws context");
    }

    if (m->use_ssl && (m->ca_pem.size > 0 || (m->cert_pem.size > 0 && m->key_pem.size > 0))) {
        const struct lws_protocols protocols[] = {
            tjs_mqtt_protocol,
            tjs_mqtt_ws_protocol,
            LWS_PROTOCOL_LIST_TERM,
        };
        m->vhost = tjs__lws_create_client_tls_vhost(ctx,
                                                    protocols,
                                                    m->ca_pem.buf,
                                                    m->ca_pem.size,
                                                    m->cert_pem.buf,
                                                    m->cert_pem.size,
                                                    m->key_pem.buf,
                                                    m->key_pem.size);
        if (!m->vhost) {
            JSValue err = tjs_mqtt_tls_error(ctx, "failed to configure MQTT TLS");
            tjs_mqtt_cleanup_rt(JS_GetRuntime(ctx), m);
            JS_FreeValue(ctx, obj);
            return err;
        }
    }

    memset(&cci, 0, sizeof(cci));
    cci.context = lws_ctx;
    cci.address = m->host;
    cci.host = m->host;
    if (m->servername) {
        cci.host = m->servername;
        cci.origin = m->servername;
    } else {
        cci.origin = m->host;
    }
    cci.port = m->port;
    cci.ssl_connection = (m->use_ssl ? LCCSCF_USE_SSL : 0) | m->ssl_flags;
    cci.userdata = m;
    cci.pwsi = &m->wsi;
    cci.vhost = m->vhost;

    if (m->transport == TJS_MQTT_TRANSPORT_WS) {
        cci.path = m->path ? m->path : "/mqtt";
        cci.protocol = "mqtt";
        cci.local_protocol_name = "tjs-mqtt-ws";
        cci.alpn = "http/1.1";
    } else {
        cci.protocol = "mqtt";
        cci.local_protocol_name = "mqtt";
        cci.method = "MQTT";
        cci.alpn = "mqtt";
        cci.mqtt_cp = &m->connect_param;
    }

    if (!lws_client_connect_via_info(&cci)) {
        tjs_mqtt_cleanup_rt(JS_GetRuntime(ctx), m);
        JS_FreeValue(ctx, obj);
        return JS_ThrowInternalError(ctx, "MQTT connection failed");
    }

    JS_SetOpaque(obj, m);
    m->this_val = JS_DupValue(ctx, obj);
    tjs__lws_conn_ref(ctx);
    lws_cancel_service(lws_ctx);

    return obj;
}

static JSValue tjs_mqtt_callback_get(JSContext *ctx, JSValue this_val, int magic) {
    TJSMqtt *m = tjs_mqtt_get(ctx, this_val);
    if (!m) {
        return JS_EXCEPTION;
    }

    return JS_DupValue(ctx, m->callbacks[magic]);
}

static JSValue tjs_mqtt_callback_set(JSContext *ctx, JSValue this_val, JSValue value, int magic) {
    TJSMqtt *m = tjs_mqtt_get(ctx, this_val);
    if (!m) {
        return JS_EXCEPTION;
    }

    if (JS_IsFunction(ctx, value) || JS_IsUndefined(value) || JS_IsNull(value)) {
        JS_FreeValue(ctx, m->callbacks[magic]);
        m->callbacks[magic] = JS_DupValue(ctx, value);
    }

    return JS_UNDEFINED;
}

static JSValue tjs_mqtt_get_debug_info(JSContext *ctx, JSValue this_val) {
    TJSMqtt *m = tjs_mqtt_get(ctx, this_val);
    JSValue obj;

    if (!m) {
        return JS_EXCEPTION;
    }

    obj = JS_NewObjectProto(ctx, JS_NULL);
    JS_DefinePropertyValueStr(ctx, obj, "pendingOps", JS_NewUint32(ctx, tjs_mqtt_pending_op_count(m)), JS_PROP_C_W_E);
    JS_DefinePropertyValueStr(ctx, obj, "hasInflight", JS_NewBool(ctx, m->inflight != NULL), JS_PROP_C_W_E);
    JS_DefinePropertyValueStr(ctx, obj, "connected", JS_NewBool(ctx, m->connected), JS_PROP_C_W_E);
    JS_DefinePropertyValueStr(ctx, obj, "closing", JS_NewBool(ctx, m->closing), JS_PROP_C_W_E);
    JS_DefinePropertyValueStr(ctx, obj, "closed", JS_NewBool(ctx, m->closed), JS_PROP_C_W_E);
    JS_DefinePropertyValueStr(ctx, obj, "tcpPingSendPending", JS_NewBool(ctx, m->tcp_ping_send_pending), JS_PROP_C_W_E);
    JS_DefinePropertyValueStr(ctx, obj, "wsPingPending", JS_NewBool(ctx, m->ws_ping_pending), JS_PROP_C_W_E);
    JS_DefinePropertyValueStr(ctx, obj, "wsPingSendPending", JS_NewBool(ctx, m->ws_ping_send_pending), JS_PROP_C_W_E);
    JS_DefinePropertyValueStr(ctx, obj, "recvBufSize", JS_NewUint32(ctx, (uint32_t) m->recv_buf.size), JS_PROP_C_W_E);
    JS_DefinePropertyValueStr(ctx,
                              obj,
                              "recvBufCapacity",
                              JS_NewUint32(ctx, (uint32_t) m->recv_buf.allocated_size),
                              JS_PROP_C_W_E);

    if (m->inflight) {
        JS_DefinePropertyValueStr(ctx, obj, "inflightType", JS_NewInt32(ctx, m->inflight->type), JS_PROP_C_W_E);
        JS_DefinePropertyValueStr(ctx, obj, "inflightQos", JS_NewInt32(ctx, m->inflight->qos), JS_PROP_C_W_E);
        JS_DefinePropertyValueStr(ctx,
                                  obj,
                                  "inflightPayloadLen",
                                  JS_NewUint32(ctx, (uint32_t) m->inflight->payload_len),
                                  JS_PROP_C_W_E);
        JS_DefinePropertyValueStr(ctx,
                                  obj,
                                  "inflightPayloadSent",
                                  JS_NewUint32(ctx, (uint32_t) m->inflight->payload_sent),
                                  JS_PROP_C_W_E);
        JS_DefinePropertyValueStr(ctx,
                                  obj,
                                  "inflightTopicsLen",
                                  JS_NewUint32(ctx, (uint32_t) m->inflight->topics_len),
                                  JS_PROP_C_W_E);
        JS_DefinePropertyValueStr(ctx,
                                  obj,
                                  "inflightTopicsSent",
                                  JS_NewUint32(ctx, (uint32_t) m->inflight->topics_sent),
                                  JS_PROP_C_W_E);
    }

    return obj;
}

static void tjs_mqtt_queue_op(TJSMqtt *m, TJSMqttOp *op) {
    list_add_tail(&op->link, &m->pending_ops);
    tjs_mqtt_maybe_request_writable(m);
}

static JSValue tjs_mqtt_publish(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    TJSMqtt *m = tjs_mqtt_get(ctx, this_val);
    TJSMqttOp *op;
    const char *topic;
    size_t payload_len = 0;
    uint8_t *payload;
    uint32_t packet_id = 0;

    if (!m) {
        return JS_EXCEPTION;
    }

    op = js_mallocz(ctx, sizeof(*op));
    if (!op) {
        return JS_EXCEPTION;
    }

    op->type = MQTT_OP_PUBLISH;
    topic = JS_ToCString(ctx, argv[0]);
    if (!topic) {
        tjs_mqtt_op_free_rt(JS_GetRuntime(ctx), op);
        return JS_EXCEPTION;
    }

    op->topic = js_strdup(ctx, topic);
    JS_FreeCString(ctx, topic);
    if (!op->topic) {
        tjs_mqtt_op_free_rt(JS_GetRuntime(ctx), op);
        return JS_EXCEPTION;
    }

    payload = JS_GetUint8Array(ctx, &payload_len, argv[1]);
    if (!payload) {
        tjs_mqtt_op_free_rt(JS_GetRuntime(ctx), op);
        return JS_EXCEPTION;
    }

    op->payload = js_malloc(ctx, payload_len ? payload_len : 1);
    if (!op->payload) {
        tjs_mqtt_op_free_rt(JS_GetRuntime(ctx), op);
        return JS_EXCEPTION;
    }

    if (payload_len) {
        memcpy(op->payload, payload, payload_len);
    }
    op->payload_len = payload_len;
    JS_ToInt32(ctx, &op->qos, argv[2]);
    op->retain = JS_ToBool(ctx, argv[3]);
    JS_ToUint32(ctx, &packet_id, argv[4]);
    op->packet_id = (uint16_t) packet_id;

    tjs_mqtt_queue_op(m, op);
    return JS_UNDEFINED;
}

static JSValue tjs_mqtt_subscribe(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    TJSMqtt *m = tjs_mqtt_get(ctx, this_val);
    JSValue topics = argv[0];
    JSValue js_length;
    uint64_t len = 0;
    uint32_t packet_id = 0;
    TJSMqttOp *op;

    if (!m) {
        return JS_EXCEPTION;
    }

    js_length = JS_GetPropertyStr(ctx, topics, "length");
    if (JS_ToIndex(ctx, &len, js_length)) {
        JS_FreeValue(ctx, js_length);
        return JS_EXCEPTION;
    }
    JS_FreeValue(ctx, js_length);

    op = js_mallocz(ctx, sizeof(*op));
    if (!op) {
        return JS_EXCEPTION;
    }

    op->type = MQTT_OP_SUBSCRIBE;
    op->topics_len = (size_t) len;
    op->topics = js_mallocz(ctx, sizeof(char *) * op->topics_len);
    op->topic_qos = js_mallocz(ctx, sizeof(int) * op->topics_len);
    if (!op->topics || !op->topic_qos) {
        tjs_mqtt_op_free_rt(JS_GetRuntime(ctx), op);
        return JS_EXCEPTION;
    }

    for (uint32_t i = 0; i < len; i++) {
        JSValue item = JS_GetPropertyUint32(ctx, topics, i);
        JSValue name = JS_GetPropertyStr(ctx, item, "topic");
        JSValue qos = JS_GetPropertyStr(ctx, item, "qos");
        const char *topic = JS_ToCString(ctx, name);

        if (!topic) {
            JS_FreeValue(ctx, qos);
            JS_FreeValue(ctx, name);
            JS_FreeValue(ctx, item);
            tjs_mqtt_op_free_rt(JS_GetRuntime(ctx), op);
            return JS_EXCEPTION;
        }

        op->topics[i] = js_strdup(ctx, topic);
        JS_FreeCString(ctx, topic);
        JS_ToInt32(ctx, &op->topic_qos[i], qos);
        JS_FreeValue(ctx, qos);
        JS_FreeValue(ctx, name);
        JS_FreeValue(ctx, item);
    }

    JS_ToUint32(ctx, &packet_id, argv[1]);
    op->packet_id = (uint16_t) packet_id;
    tjs_mqtt_queue_op(m, op);
    return JS_UNDEFINED;
}

static JSValue tjs_mqtt_unsubscribe(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    TJSMqtt *m = tjs_mqtt_get(ctx, this_val);
    JSValue topics = argv[0];
    JSValue js_length;
    uint64_t len = 0;
    uint32_t packet_id = 0;
    TJSMqttOp *op;

    if (!m) {
        return JS_EXCEPTION;
    }

    js_length = JS_GetPropertyStr(ctx, topics, "length");
    if (JS_ToIndex(ctx, &len, js_length)) {
        JS_FreeValue(ctx, js_length);
        return JS_EXCEPTION;
    }
    JS_FreeValue(ctx, js_length);

    op = js_mallocz(ctx, sizeof(*op));
    if (!op) {
        return JS_EXCEPTION;
    }

    op->type = MQTT_OP_UNSUBSCRIBE;
    op->topics_len = (size_t) len;
    op->topics = js_mallocz(ctx, sizeof(char *) * op->topics_len);
    op->topic_qos = js_mallocz(ctx, sizeof(int) * op->topics_len);
    if (!op->topics || !op->topic_qos) {
        tjs_mqtt_op_free_rt(JS_GetRuntime(ctx), op);
        return JS_EXCEPTION;
    }

    for (uint32_t i = 0; i < len; i++) {
        JSValue item = JS_GetPropertyUint32(ctx, topics, i);
        const char *topic = JS_ToCString(ctx, item);

        if (!topic) {
            JS_FreeValue(ctx, item);
            tjs_mqtt_op_free_rt(JS_GetRuntime(ctx), op);
            return JS_EXCEPTION;
        }

        op->topics[i] = js_strdup(ctx, topic);
        op->topic_qos[i] = 0;
        JS_FreeCString(ctx, topic);
        JS_FreeValue(ctx, item);
    }

    JS_ToUint32(ctx, &packet_id, argv[1]);
    op->packet_id = (uint16_t) packet_id;
    tjs_mqtt_queue_op(m, op);
    return JS_UNDEFINED;
}

static JSValue tjs_mqtt_close(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    TJSMqtt *m = tjs_mqtt_get(ctx, this_val);

    if (!m) {
        return JS_EXCEPTION;
    }

    m->closing = true;
    if (m->wsi) {
        lws_set_timeout(m->wsi, PENDING_TIMEOUT_USER_OK, 1);
        tjs_mqtt_maybe_request_writable(m);
    }

    return JS_UNDEFINED;
}

static const JSCFunctionListEntry tjs_mqtt_proto_funcs[] = {
    JS_CGETSET_MAGIC_DEF("onack", tjs_mqtt_callback_get, tjs_mqtt_callback_set, MQTT_CALLBACK_ACK),
    JS_CGETSET_MAGIC_DEF("onclose", tjs_mqtt_callback_get, tjs_mqtt_callback_set, MQTT_CALLBACK_CLOSE),
    JS_CGETSET_MAGIC_DEF("onconnect", tjs_mqtt_callback_get, tjs_mqtt_callback_set, MQTT_CALLBACK_CONNECT),
    JS_CGETSET_MAGIC_DEF("onerror", tjs_mqtt_callback_get, tjs_mqtt_callback_set, MQTT_CALLBACK_ERROR),
    JS_CGETSET_MAGIC_DEF("onmessage", tjs_mqtt_callback_get, tjs_mqtt_callback_set, MQTT_CALLBACK_MESSAGE),
    JS_CGETSET_MAGIC_DEF("onsubscribed", tjs_mqtt_callback_get, tjs_mqtt_callback_set, MQTT_CALLBACK_SUBSCRIBED),
    JS_CGETSET_MAGIC_DEF("onunsubscribed", tjs_mqtt_callback_get, tjs_mqtt_callback_set, MQTT_CALLBACK_UNSUBSCRIBED),
    JS_CGETSET_DEF("debugInfo", tjs_mqtt_get_debug_info, NULL),
    TJS_CFUNC_DEF("publish", 5, tjs_mqtt_publish),
    TJS_CFUNC_DEF("subscribe", 2, tjs_mqtt_subscribe),
    TJS_CFUNC_DEF("unsubscribe", 2, tjs_mqtt_unsubscribe),
    TJS_CFUNC_DEF("close", 0, tjs_mqtt_close),
};

void tjs__mod_mqtt_init(JSContext *ctx, JSValue ns) {
    JSRuntime *rt = JS_GetRuntime(ctx);
    JSValue proto = JS_NewObject(ctx);
    JSValue ctor;

    JS_NewClassID(rt, &tjs_mqtt_class_id);
    JS_NewClass(rt, tjs_mqtt_class_id, &tjs_mqtt_class);
    JS_SetPropertyFunctionList(ctx, proto, tjs_mqtt_proto_funcs, countof(tjs_mqtt_proto_funcs));
    JS_SetClassProto(ctx, tjs_mqtt_class_id, proto);

    ctor = JS_NewCFunction2(ctx, tjs_mqtt_constructor, "MQTTClient", 1, JS_CFUNC_constructor, 0);
    JS_DefinePropertyValueStr(ctx, ns, "MQTTClient", ctor, JS_PROP_C_W_E);
}
