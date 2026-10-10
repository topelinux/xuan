/*
 * xuan native crypto helpers
 */

#include "private.h"
#include "utils.h"

#include <mbedtls/md.h>
#include <string.h>

static JSClassID tjs_hmac_class_id;

typedef struct {
    mbedtls_md_context_t ctx;
    const mbedtls_md_info_t *info;
    bool finalized;
} TJSHmac;

static const mbedtls_md_info_t *tjs_md_info_from_name(const char *name) {
    if (!name) {
        return NULL;
    }

    if (strcmp(name, "md5") == 0) {
        return mbedtls_md_info_from_type(MBEDTLS_MD_MD5);
    }
    if (strcmp(name, "sha1") == 0) {
        return mbedtls_md_info_from_type(MBEDTLS_MD_SHA1);
    }
    if (strcmp(name, "sha224") == 0) {
        return mbedtls_md_info_from_type(MBEDTLS_MD_SHA224);
    }
    if (strcmp(name, "sha256") == 0) {
        return mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    }
    if (strcmp(name, "sha384") == 0) {
        return mbedtls_md_info_from_type(MBEDTLS_MD_SHA384);
    }
    if (strcmp(name, "sha512") == 0) {
        return mbedtls_md_info_from_type(MBEDTLS_MD_SHA512);
    }
    if (strcmp(name, "sha3_224") == 0) {
        return mbedtls_md_info_from_type(MBEDTLS_MD_SHA3_224);
    }
    if (strcmp(name, "sha3_256") == 0) {
        return mbedtls_md_info_from_type(MBEDTLS_MD_SHA3_256);
    }
    if (strcmp(name, "sha3_384") == 0) {
        return mbedtls_md_info_from_type(MBEDTLS_MD_SHA3_384);
    }
    if (strcmp(name, "sha3_512") == 0) {
        return mbedtls_md_info_from_type(MBEDTLS_MD_SHA3_512);
    }

    return NULL;
}

static uint8_t *tjs_get_bytes(JSContext *ctx, JSValueConst val, size_t *size, JSValue *ref) {
    size_t offset;
    size_t elem_size;
    size_t ab_size;

    JSValue ab = JS_GetTypedArrayBuffer(ctx, val, &offset, size, &elem_size);
    if (!JS_IsException(ab)) {
        uint8_t *data = JS_GetArrayBuffer(ctx, &ab_size, ab);
        if (!data || offset + *size > ab_size) {
            JS_FreeValue(ctx, ab);
            return NULL;
        }

        *ref = ab;
        return data + offset;
    }

    JS_ResetUncatchableError(ctx);
    JS_FreeValue(ctx, JS_GetException(ctx));

    uint8_t *data = JS_GetArrayBuffer(ctx, size, val);
    if (data) {
        *ref = JS_UNDEFINED;
        return data;
    }

    JS_ResetUncatchableError(ctx);
    JS_FreeValue(ctx, JS_GetException(ctx));
    return NULL;
}

static JSValue tjs_hmac_constructor(JSContext *ctx, JSValueConst new_target, int argc, JSValueConst *argv) {
    const char *algorithm = JS_ToCString(ctx, argv[0]);
    if (!algorithm) {
        return JS_EXCEPTION;
    }

    const mbedtls_md_info_t *info = tjs_md_info_from_name(algorithm);
    JS_FreeCString(ctx, algorithm);
    if (!info) {
        return JS_ThrowTypeError(ctx, "unsupported HMAC algorithm");
    }

    size_t key_len = 0;
    JSValue key_ref = JS_UNDEFINED;
    uint8_t *key = tjs_get_bytes(ctx, argv[1], &key_len, &key_ref);
    if (!key) {
        return JS_ThrowTypeError(ctx, "expected key to be ArrayBuffer or ArrayBufferView");
    }

    TJSHmac *h = js_mallocz(ctx, sizeof(*h));
    if (!h) {
        JS_FreeValue(ctx, key_ref);
        return JS_EXCEPTION;
    }

    mbedtls_md_init(&h->ctx);
    h->info = info;

    int ret = mbedtls_md_setup(&h->ctx, info, 1);
    if (ret == 0) {
        ret = mbedtls_md_hmac_starts(&h->ctx, key, key_len);
    }
    JS_FreeValue(ctx, key_ref);

    if (ret != 0) {
        mbedtls_md_free(&h->ctx);
        js_free(ctx, h);
        return JS_ThrowInternalError(ctx, "mbedtls hmac setup failed: %d", ret);
    }

    JSValue obj = JS_NewObjectClass(ctx, tjs_hmac_class_id);
    if (JS_IsException(obj)) {
        mbedtls_md_free(&h->ctx);
        js_free(ctx, h);
        return JS_EXCEPTION;
    }

    JS_SetOpaque(obj, h);
    return obj;
}

static JSValue tjs_hmac_update(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    TJSHmac *h = JS_GetOpaque2(ctx, this_val, tjs_hmac_class_id);
    if (!h) {
        return JS_EXCEPTION;
    }
    if (h->finalized) {
        return JS_ThrowTypeError(ctx, "Digest already called");
    }

    size_t data_len = 0;
    JSValue data_ref = JS_UNDEFINED;
    uint8_t *data = tjs_get_bytes(ctx, argv[0], &data_len, &data_ref);
    if (!data) {
        return JS_ThrowTypeError(ctx, "expected data to be ArrayBuffer or ArrayBufferView");
    }

    int ret = mbedtls_md_hmac_update(&h->ctx, data, data_len);
    JS_FreeValue(ctx, data_ref);
    if (ret != 0) {
        return JS_ThrowInternalError(ctx, "mbedtls hmac update failed: %d", ret);
    }

    return JS_DupValue(ctx, this_val);
}

static JSValue tjs_hmac_digest(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    TJSHmac *h = JS_GetOpaque2(ctx, this_val, tjs_hmac_class_id);
    if (!h) {
        return JS_EXCEPTION;
    }
    if (h->finalized) {
        return JS_ThrowTypeError(ctx, "Digest already called");
    }

    uint8_t out[MBEDTLS_MD_MAX_SIZE];
    int ret = mbedtls_md_hmac_finish(&h->ctx, out);
    if (ret != 0) {
        return JS_ThrowInternalError(ctx, "mbedtls hmac finish failed: %d", ret);
    }

    h->finalized = true;
    return JS_NewArrayBufferCopy(ctx, out, mbedtls_md_get_size(h->info));
}

static void tjs_hmac_finalizer(JSRuntime *rt, JSValue val) {
    TJSHmac *h = JS_GetOpaque(val, tjs_hmac_class_id);
    if (!h) {
        return;
    }

    mbedtls_md_free(&h->ctx);
    js_free_rt(rt, h);
}

static JSClassDef tjs_hmac_class = {
    "Hmac",
    .finalizer = tjs_hmac_finalizer,
};

static const JSCFunctionListEntry tjs_hmac_proto_funcs[] = {
    TJS_CFUNC_DEF("update", 1, tjs_hmac_update),
    TJS_CFUNC_DEF("digest", 0, tjs_hmac_digest),
};

void tjs__mod_crypto_init(JSContext *ctx, JSValue ns) {
    JSRuntime *rt = JS_GetRuntime(ctx);

    JS_NewClassID(rt, &tjs_hmac_class_id);
    JS_NewClass(rt, tjs_hmac_class_id, &tjs_hmac_class);

    JSValue proto = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, proto, tjs_hmac_proto_funcs, countof(tjs_hmac_proto_funcs));

    JSValue crypto = JS_NewObject(ctx);
    JSValue ctor = JS_NewCFunction2(ctx, tjs_hmac_constructor, "Hmac", 2, JS_CFUNC_constructor, 0);
    JS_SetConstructor(ctx, ctor, proto);
    JS_SetClassProto(ctx, tjs_hmac_class_id, proto);
    JS_DefinePropertyValueStr(ctx, crypto, "Hmac", ctor, JS_PROP_C_W_E);
    JS_DefinePropertyValueStr(ctx, ns, "crypto", crypto, JS_PROP_C_W_E);
}
