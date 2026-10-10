/*
 * txiki.js
 *
 * Copyright (c) 2026-present
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
#include "utils.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

/* Forward declarations for QuickJS BigInt functions */
extern JSValue JS_NewBigUint64(JSContext *ctx, uint64_t v);
extern int JS_ToBigUint64(JSContext *ctx, uint64_t *pres, JSValueConst val);

#define TJS__BUFFER_MAX_LENGTH      UINT32_MAX
#define TJS__BUFFER_MAX_STR_LENGTH  ((1 << 30) - 1)
#define TJS__BUFFER_EVAL_NAME       "<buffer-native>"

typedef enum {
    TJS__ENC_UTF8 = 0,
    TJS__ENC_LATIN1,
    TJS__ENC_ASCII,
    TJS__ENC_HEX,
    TJS__ENC_BASE64,
    TJS__ENC_BASE64URL,
    TJS__ENC_UTF16LE,
    TJS__ENC_UCS2,
} TJSBufferEncoding;

typedef enum {
    TJS__NUM_I8 = 1,
    TJS__NUM_U8,
    TJS__NUM_I16,
    TJS__NUM_U16,
    TJS__NUM_I32,
    TJS__NUM_U32,
    TJS__NUM_F32,
    TJS__NUM_F64,
    TJS__NUM_BIGI64,
    TJS__NUM_BIGU64,
} TJSBufferNumKind;

#define TJS__NUM_F_LE    (1 << 8)
#define TJS__NUM_F_WRITE (1 << 9)

static JSValue tjs__buffer_get_ctor(JSContext *ctx) {
    JSValue global_obj = JS_GetGlobalObject(ctx);
    JSValue ctor = JS_GetPropertyStr(ctx, global_obj, "Buffer");
    JS_FreeValue(ctx, global_obj);
    return ctor;
}

static int tjs__buffer_parse_encoding(JSContext *ctx, JSValueConst v, TJSBufferEncoding *enc) {
    if (JS_IsUndefined(v) || JS_IsNull(v)) {
        *enc = TJS__ENC_UTF8;
        return 0;
    }

    const char *s = JS_ToCString(ctx, v);
    if (!s) {
        return -1;
    }

    if (!strcasecmp(s, "utf8") || !strcasecmp(s, "utf-8") || !strcasecmp(s, "unicode-1-1-utf8") ||
        !strcasecmp(s, "unicode-1-1-utf-8")) {
        *enc = TJS__ENC_UTF8;
    } else if (!strcasecmp(s, "latin1") || !strcasecmp(s, "binary")) {
        *enc = TJS__ENC_LATIN1;
    } else if (!strcasecmp(s, "ascii")) {
        *enc = TJS__ENC_ASCII;
    } else if (!strcasecmp(s, "hex")) {
        *enc = TJS__ENC_HEX;
    } else if (!strcasecmp(s, "base64")) {
        *enc = TJS__ENC_BASE64;
    } else if (!strcasecmp(s, "base64url")) {
        *enc = TJS__ENC_BASE64URL;
    } else if (!strcasecmp(s, "utf16le") || !strcasecmp(s, "ucs2") || !strcasecmp(s, "ucs-2")) {
        *enc = TJS__ENC_UTF16LE;
    } else {
        JS_FreeCString(ctx, s);
        JS_ThrowTypeError(ctx, "Unknown encoding");
        return -1;
    }

    JS_FreeCString(ctx, s);
    return 0;
}

static bool tjs__buffer_is_encoding_name(const char *s) {
    return !strcasecmp(s, "utf8") || !strcasecmp(s, "utf-8") || !strcasecmp(s, "unicode-1-1-utf8") ||
           !strcasecmp(s, "unicode-1-1-utf-8") || !strcasecmp(s, "ascii") || !strcasecmp(s, "latin1") ||
           !strcasecmp(s, "binary") || !strcasecmp(s, "hex") || !strcasecmp(s, "base64") ||
           !strcasecmp(s, "base64url") || !strcasecmp(s, "utf16le") || !strcasecmp(s, "ucs2") ||
           !strcasecmp(s, "ucs-2");
}

static int tjs__buffer_arg_index(JSContext *ctx, JSValueConst v, size_t *out, size_t def) {
    if (JS_IsUndefined(v)) {
        *out = def;
        return 0;
    }
    uint64_t tmp;
    if (JS_ToIndex(ctx, &tmp, v)) {
        return -1;
    }
    *out = (size_t) tmp;
    return 0;
}

static int tjs__buffer_get_view(JSContext *ctx, JSValueConst v, uint8_t **ptr, size_t *len, JSValue *ab_out) {
    size_t off, view_len, bpe, ab_len;
    JSValue ab = JS_UNDEFINED;

    if (JS_IsObject(v)) {
        ab = JS_GetTypedArrayBuffer(ctx, v, &off, &view_len, &bpe);
        if (!JS_IsException(ab) && JS_IsObject(ab)) {
            uint8_t *p = JS_GetArrayBuffer(ctx, &ab_len, ab);
            if (!p) {
                JS_FreeValue(ctx, ab);
                return -1;
            }
            if (off + view_len > ab_len) {
                JS_FreeValue(ctx, ab);
                JS_ThrowRangeError(ctx, "array buffer overflow");
                return -1;
            }
            *ptr = p + off;
            *len = view_len;
            if (ab_out) {
                *ab_out = ab;
            } else {
                JS_FreeValue(ctx, ab);
            }
            (void) bpe;
            return 0;
        }
        JS_FreeValue(ctx, ab);
        JS_ResetUncatchableError(ctx);
        JSValue ex = JS_GetException(ctx);
        JS_FreeValue(ctx, ex);

        uint8_t *p = JS_GetArrayBuffer(ctx, &ab_len, v);
        if (p) {
            *ptr = p;
            *len = ab_len;
            if (ab_out) {
                *ab_out = JS_DupValue(ctx, v);
            }
            return 0;
        }
    }

    JS_ThrowTypeError(ctx, "expected ArrayBufferView or ArrayBuffer");
    return -1;
}

static int tjs__buffer_get_uint8_view(JSContext *ctx, JSValueConst v, uint8_t **ptr, size_t *len) {
    int type = JS_GetTypedArrayType(v);
    if (type != JS_TYPED_ARRAY_UINT8) {
        JS_ThrowTypeError(ctx, "argument must be a Buffer or Uint8Array");
        return -1;
    }
    return tjs__buffer_get_view(ctx, v, ptr, len, NULL);
}

static int tjs__buffer_compare_bytes(const uint8_t *a, size_t a_len, const uint8_t *b, size_t b_len) {
    size_t min_len = a_len < b_len ? a_len : b_len;
    int cmp = memcmp(a, b, min_len);
    if (cmp != 0) {
        return cmp < 0 ? -1 : 1;
    }
    if (a_len < b_len) {
        return -1;
    }
    if (a_len > b_len) {
        return 1;
    }
    return 0;
}

static JSValue tjs__buffer_new_with_length(JSContext *ctx, size_t len) {
    JSValue ctor = tjs__buffer_get_ctor(ctx);
    if (JS_IsException(ctor)) {
        return JS_EXCEPTION;
    }

    JSValue argv[1] = { JS_NewUint32(ctx, (uint32_t) len) };
    JSValue out = JS_CallConstructor(ctx, ctor, 1, argv);
    JS_FreeValue(ctx, argv[0]);
    JS_FreeValue(ctx, ctor);
    return out;
}

static JSValue tjs__buffer_new_with_ab(JSContext *ctx, JSValue ab, size_t off, size_t len) {
    JSValue ctor = tjs__buffer_get_ctor(ctx);
    if (JS_IsException(ctor)) {
        return JS_EXCEPTION;
    }

    JSValue argv[3] = { JS_DupValue(ctx, ab), JS_NewUint32(ctx, (uint32_t) off), JS_NewUint32(ctx, (uint32_t) len) };
    JSValue out = JS_CallConstructor(ctx, ctor, 3, argv);
    JS_FreeValue(ctx, argv[0]);
    JS_FreeValue(ctx, argv[1]);
    JS_FreeValue(ctx, argv[2]);
    JS_FreeValue(ctx, ctor);
    return out;
}

static int tjs__hex_nibble(int c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return 10 + c - 'a';
    }
    if (c >= 'A' && c <= 'F') {
        return 10 + c - 'A';
    }
    return -1;
}

static int tjs__base64_val(int c) {
    if (c >= 'A' && c <= 'Z') {
        return c - 'A';
    }
    if (c >= 'a' && c <= 'z') {
        return 26 + c - 'a';
    }
    if (c >= '0' && c <= '9') {
        return 52 + c - '0';
    }
    if (c == '+' || c == '-') {
        return 62;
    }
    if (c == '/' || c == '_') {
        return 63;
    }
    return -1;
}

int tjs__buffer_decode_base64(JSContext *ctx, const uint8_t *in, size_t in_len, uint8_t **out, size_t *out_len) {
    uint8_t *tmp = js_malloc(ctx, in_len + 4);
    if (!tmp) {
        return -1;
    }

    size_t n = 0;
    for (size_t i = 0; i < in_len; i++) {
        uint8_t c = in[i];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f') {
            continue;
        }
        tmp[n++] = c;
    }
    while (n > 0 && tmp[n - 1] == '=') {
        n--;
    }
    while (n % 4 != 0) {
        tmp[n++] = '=';
    }

    uint8_t *buf = js_malloc(ctx, (n / 4) * 3 + 1);
    if (!buf) {
        js_free(ctx, tmp);
        return -1;
    }

    size_t outp = 0;
    for (size_t i = 0; i < n; i += 4) {
        int a = tjs__base64_val(tmp[i]);
        int b = tjs__base64_val(tmp[i + 1]);
        if (a < 0 || b < 0) {
            js_free(ctx, tmp);
            js_free(ctx, buf);
            JS_ThrowTypeError(ctx, "Invalid base64 string");
            return -1;
        }

        int c = tmp[i + 2] == '=' ? -2 : tjs__base64_val(tmp[i + 2]);
        int d = tmp[i + 3] == '=' ? -2 : tjs__base64_val(tmp[i + 3]);
        if (c < -2 || d < -2 || (c == -2 && d != -2)) {
            js_free(ctx, tmp);
            js_free(ctx, buf);
            JS_ThrowTypeError(ctx, "Invalid base64 string");
            return -1;
        }

        buf[outp++] = (uint8_t) ((a << 2) | (b >> 4));
        if (c >= 0) {
            buf[outp++] = (uint8_t) (((b & 0x0f) << 4) | (c >> 2));
            if (d >= 0) {
                buf[outp++] = (uint8_t) (((c & 0x03) << 6) | d);
            }
        }
    }

    js_free(ctx, tmp);
    *out = buf;
    *out_len = outp;
    return 0;
}

static int tjs__buffer_encode_base64(JSContext *ctx, const uint8_t *in, size_t in_len, char **out, size_t *out_len) {
    static const char tbl[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    size_t len = ((in_len + 2) / 3) * 4;
    char *buf = js_malloc(ctx, len + 1);
    if (!buf) {
        return -1;
    }

    size_t i = 0;
    size_t p = 0;
    while (i + 2 < in_len) {
        uint32_t x = ((uint32_t) in[i] << 16) | ((uint32_t) in[i + 1] << 8) | in[i + 2];
        buf[p++] = tbl[(x >> 18) & 0x3f];
        buf[p++] = tbl[(x >> 12) & 0x3f];
        buf[p++] = tbl[(x >> 6) & 0x3f];
        buf[p++] = tbl[x & 0x3f];
        i += 3;
    }

    if (i < in_len) {
        uint32_t x = (uint32_t) in[i] << 16;
        buf[p++] = tbl[(x >> 18) & 0x3f];
        if (i + 1 < in_len) {
            x |= (uint32_t) in[i + 1] << 8;
            buf[p++] = tbl[(x >> 12) & 0x3f];
            buf[p++] = tbl[(x >> 6) & 0x3f];
            buf[p++] = '=';
        } else {
            buf[p++] = tbl[(x >> 12) & 0x3f];
            buf[p++] = '=';
            buf[p++] = '=';
        }
    }

    buf[p] = '\0';
    *out = buf;
    *out_len = p;
    return 0;
}

static int tjs__buffer_encode_latin1_string(JSContext *ctx, JSValueConst str_val, uint8_t **out, size_t *out_len) {
    size_t len = 0;
    const uint16_t *s = JS_ToCStringLenUTF16(ctx, &len, str_val);
    if (!s) {
        return -1;
    }

    uint8_t *buf = js_malloc(ctx, len + 1);
    if (!buf) {
        JS_FreeCStringUTF16(ctx, s);
        return -1;
    }

    for (size_t i = 0; i < len; i++) {
        buf[i] = (uint8_t) (s[i] & 0xff);
    }
    JS_FreeCStringUTF16(ctx, s);

    *out = buf;
    *out_len = len;
    return 0;
}

static JSValue tjs__buffer_decode_single_byte(JSContext *ctx, const uint8_t *data, size_t len, bool ascii) {
    if (len == 0) {
        return JS_NewString(ctx, "");
    }

    uint16_t *buf = js_malloc(ctx, len * sizeof(*buf));
    if (!buf) {
        return JS_EXCEPTION;
    }

    for (size_t i = 0; i < len; i++) {
        buf[i] = ascii ? (data[i] & 0x7f) : data[i];
    }

    JSValue out = JS_NewStringUTF16(ctx, buf, len);
    js_free(ctx, buf);
    return out;
}

static int tjs__buffer_encode_string(JSContext *ctx,
                                     JSValueConst str_val,
                                     TJSBufferEncoding enc,
                                     uint8_t **out,
                                     size_t *out_len) {
    if (enc == TJS__ENC_LATIN1 || enc == TJS__ENC_ASCII) {
        return tjs__buffer_encode_latin1_string(ctx, str_val, out, out_len);
    }

    if (enc == TJS__ENC_UTF16LE || enc == TJS__ENC_UCS2) {
        size_t len = 0;
        const uint16_t *s = JS_ToCStringLenUTF16(ctx, &len, str_val);
        if (!s) {
            return -1;
        }
        uint8_t *buf = js_malloc(ctx, len * 2);
        if (!buf) {
            JS_FreeCStringUTF16(ctx, s);
            return -1;
        }
        for (size_t i = 0; i < len; i++) {
            buf[i * 2] = (uint8_t) (s[i] & 0xff);
            buf[i * 2 + 1] = (uint8_t) ((s[i] >> 8) & 0xff);
        }
        JS_FreeCStringUTF16(ctx, s);
        *out = buf;
        *out_len = len * 2;
        return 0;
    }

    size_t len = 0;
    const char *s = JS_ToCStringLen(ctx, &len, str_val);
    if (!s) {
        return -1;
    }

    int ret = 0;
    if (enc == TJS__ENC_UTF8) {
        uint8_t *buf = js_malloc(ctx, len + 1);
        if (!buf) {
            ret = -1;
            goto end;
        }
        memcpy(buf, s, len);
        *out = buf;
        *out_len = len;
    } else if (enc == TJS__ENC_HEX) {
        size_t n = len / 2;
        uint8_t *buf = js_malloc(ctx, n + 1);
        if (!buf) {
            ret = -1;
            goto end;
        }
        for (size_t i = 0; i < n; i++) {
            int a = tjs__hex_nibble(s[i * 2]);
            int b = tjs__hex_nibble(s[i * 2 + 1]);
            if (a < 0 || b < 0) {
                js_free(ctx, buf);
                JS_ThrowTypeError(ctx, "Invalid hex string");
                ret = -1;
                goto end;
            }
            buf[i] = (uint8_t) ((a << 4) | b);
        }
        *out = buf;
        *out_len = n;
    } else {
        ret = tjs__buffer_decode_base64(ctx, (const uint8_t *) s, len, out, out_len);
    }

end:
    JS_FreeCString(ctx, s);
    return ret;
}

static JSValue tjs__buffer_decode_bytes(JSContext *ctx, const uint8_t *data, size_t len, TJSBufferEncoding enc) {
    if (enc == TJS__ENC_UTF8) {
        return JS_NewStringLen(ctx, (const char *) data, len);
    }

    if (enc == TJS__ENC_LATIN1 || enc == TJS__ENC_ASCII) {
        return tjs__buffer_decode_single_byte(ctx, data, len, enc == TJS__ENC_ASCII);
    }

    if (enc == TJS__ENC_UTF16LE || enc == TJS__ENC_UCS2) {
        size_t char_count = len / 2;
        uint16_t *buf = js_malloc(ctx, char_count * sizeof(*buf));
        if (!buf) {
            return JS_EXCEPTION;
        }
        for (size_t i = 0; i < char_count; i++) {
            buf[i] = (uint16_t) data[i * 2] | ((uint16_t) data[i * 2 + 1] << 8);
        }
        JSValue out = JS_NewStringUTF16(ctx, buf, char_count);
        js_free(ctx, buf);
        return out;
    }

    if (enc == TJS__ENC_HEX) {
        static const char hex[] = "0123456789abcdef";
        char *buf = js_malloc(ctx, len * 2 + 1);
        if (!buf) {
            return JS_EXCEPTION;
        }
        for (size_t i = 0; i < len; i++) {
            buf[i * 2] = hex[data[i] >> 4];
            buf[i * 2 + 1] = hex[data[i] & 0x0f];
        }
        buf[len * 2] = '\0';
        JSValue out = JS_NewStringLen(ctx, buf, len * 2);
        js_free(ctx, buf);
        return out;
    }

    char *b64 = NULL;
    size_t b64_len = 0;
    if (tjs__buffer_encode_base64(ctx, data, len, &b64, &b64_len)) {
        return JS_EXCEPTION;
    }

    if (enc == TJS__ENC_BASE64URL) {
        while (b64_len > 0 && b64[b64_len - 1] == '=') {
            b64_len--;
        }
        for (size_t i = 0; i < b64_len; i++) {
            if (b64[i] == '+') {
                b64[i] = '-';
            } else if (b64[i] == '/') {
                b64[i] = '_';
            }
        }
    }

    JSValue out = JS_NewStringLen(ctx, b64, b64_len);
    js_free(ctx, b64);
    return out;
}

static size_t tjs__buffer_clamp_index(int64_t idx, size_t max) {
    if (idx <= 0) {
        return 0;
    }
    if ((uint64_t) idx >= max) {
        return max;
    }
    return (size_t) idx;
}

static size_t tjs__buffer_normalize_slice_start(int64_t idx, size_t max) {
    if (idx < 0) {
        int64_t n = (int64_t) max + idx;
        if (n < 0) {
            return 0;
        }
        return (size_t) n;
    }
    return tjs__buffer_clamp_index(idx, max);
}

static size_t tjs__buffer_normalize_slice_end(int64_t idx, size_t max) {
    if (idx < 0) {
        int64_t n = (int64_t) max + idx;
        if (n < 0) {
            return 0;
        }
        return (size_t) n;
    }
    return tjs__buffer_clamp_index(idx, max);
}

static size_t tjs__buffer_trim_utf8(const uint8_t *buf, size_t len, size_t max_len) {
    if (max_len >= len) {
        return len;
    }
    size_t end = max_len;
    while (end > 0 && (buf[end] & 0xc0) == 0x80) {
        end--;
    }
    if (end == 0) {
        return 0;
    }
    size_t lead = end - 1;
    uint8_t c = buf[lead];
    size_t expect = 1;
    if ((c & 0xf8) == 0xf0) {
        expect = 4;
    } else if ((c & 0xf0) == 0xe0) {
        expect = 3;
    } else if ((c & 0xe0) == 0xc0) {
        expect = 2;
    }
    if (lead + expect > max_len) {
        end = lead;
    }
    return end;
}

static int tjs__buffer_get_len_arg(JSContext *ctx, JSValueConst arg, size_t *out) {
    uint64_t n;
    if (JS_ToIndex(ctx, &n, arg)) {
        return -1;
    }
    if (n > TJS__BUFFER_MAX_LENGTH) {
        JS_ThrowRangeError(ctx, "size is out of range");
        return -1;
    }
    *out = (size_t) n;
    return 0;
}

typedef struct {
    const uint8_t *ptr;
    size_t len;
    bool owned;
} TJSBufferNeedle;

static void tjs__buffer_free_needle(JSContext *ctx, TJSBufferNeedle *needle) {
    if (needle->owned) {
        js_free(ctx, (void *) needle->ptr);
    }
    needle->ptr = NULL;
    needle->len = 0;
    needle->owned = false;
}

static int tjs__buffer_parse_needle(JSContext *ctx, JSValueConst value, TJSBufferEncoding enc, TJSBufferNeedle *needle) {
    memset(needle, 0, sizeof(*needle));

    if (JS_IsString(value)) {
        uint8_t *tmp = NULL;
        size_t tmp_len = 0;
        if (tjs__buffer_encode_string(ctx, value, enc, &tmp, &tmp_len)) {
            return -1;
        }
        needle->ptr = tmp;
        needle->len = tmp_len;
        needle->owned = true;
        return 0;
    }

    if (JS_IsNumber(value)) {
        int32_t num;
        if (JS_ToInt32(ctx, &num, value)) {
            return -1;
        }
        uint8_t *tmp = js_malloc(ctx, 1);
        if (!tmp) {
            return -1;
        }
        tmp[0] = (uint8_t) (num & 0xff);
        needle->ptr = tmp;
        needle->len = 1;
        needle->owned = true;
        return 0;
    }

    uint8_t *ptr = NULL;
    size_t len = 0;
    if (!tjs__buffer_get_view(ctx, value, &ptr, &len, NULL)) {
        needle->ptr = ptr;
        needle->len = len;
        needle->owned = false;
        return 0;
    }

    JS_ResetUncatchableError(ctx);
    JS_FreeValue(ctx, JS_GetException(ctx));
    JS_ThrowTypeError(ctx, "value must be string, number, Buffer, TypedArray, DataView or ArrayBuffer");
    return -1;
}

static int tjs__buffer_normalize_search_offset(JSContext *ctx,
                                               size_t hay_len,
                                               int argc,
                                               JSValue *argv,
                                               bool is_last,
                                               bool needle_is_empty,
                                               size_t *out_offset,
                                               TJSBufferEncoding *out_enc) {
    *out_enc = TJS__ENC_UTF8;

    int64_t offset = is_last ? (needle_is_empty ? (int64_t) hay_len : (int64_t) hay_len - 1) : 0;
    bool has_offset = false;

    if (argc >= 2 && !JS_IsUndefined(argv[1])) {
        if (JS_IsString(argv[1])) {
            if (tjs__buffer_parse_encoding(ctx, argv[1], out_enc)) {
                return -1;
            }
        } else {
            if (JS_ToInt64(ctx, &offset, argv[1])) {
                return -1;
            }
            has_offset = true;
        }
    }

    if (argc >= 3 && !JS_IsUndefined(argv[2])) {
        if (tjs__buffer_parse_encoding(ctx, argv[2], out_enc)) {
            return -1;
        }
    }

    if (!is_last) {
        if (offset < 0) {
            offset += (int64_t) hay_len;
            if (offset < 0) {
                offset = 0;
            }
        }
        if ((uint64_t) offset > (uint64_t) hay_len) {
            offset = (int64_t) hay_len;
        }
        *out_offset = (size_t) offset;
        return 0;
    }

    if (needle_is_empty) {
        if (has_offset && offset < 0) {
            offset += (int64_t) hay_len;
        }
        if (offset < 0) {
            offset = 0;
        }
        if ((uint64_t) offset > (uint64_t) hay_len) {
            offset = (int64_t) hay_len;
        }
        *out_offset = (size_t) offset;
        return 0;
    }

    if (hay_len == 0) {
        *out_offset = 0;
        return 0;
    }

    if (has_offset && offset < 0) {
        offset += (int64_t) hay_len;
    }
    if (offset < 0) {
        *out_offset = SIZE_MAX; /* sentinel: definitely not found */
        return 0;
    }
    if ((uint64_t) offset >= (uint64_t) hay_len) {
        offset = (int64_t) hay_len - 1;
    }
    *out_offset = (size_t) offset;
    return 0;
}

static int64_t tjs__buffer_find_forward(const uint8_t *hay, size_t hay_len, const uint8_t *needle, size_t needle_len, size_t start) {
    if (needle_len == 0) {
        return (int64_t) (start > hay_len ? hay_len : start);
    }
    if (start > hay_len || needle_len > hay_len || start > hay_len - needle_len) {
        return -1;
    }

    size_t last = hay_len - needle_len;
    for (size_t i = start; i <= last; i++) {
        if (memcmp(hay + i, needle, needle_len) == 0) {
            return (int64_t) i;
        }
    }
    return -1;
}

static int64_t tjs__buffer_find_backward(const uint8_t *hay, size_t hay_len, const uint8_t *needle, size_t needle_len, size_t start) {
    if (needle_len == 0) {
        return (int64_t) (start > hay_len ? hay_len : start);
    }
    if (needle_len > hay_len || start == SIZE_MAX) {
        return -1;
    }

    if (start > hay_len - needle_len) {
        start = hay_len - needle_len;
    }

    for (size_t i = start + 1; i > 0; i--) {
        size_t pos = i - 1;
        if (memcmp(hay + pos, needle, needle_len) == 0) {
            return (int64_t) pos;
        }
    }
    return -1;
}

static JSValue tjs_buffer_find_index(JSContext *ctx, JSValue this_val, int argc, JSValue *argv, int magic) {
    uint8_t *hay = NULL;
    size_t hay_len = 0;
    if (tjs__buffer_get_view(ctx, this_val, &hay, &hay_len, NULL)) {
        return JS_EXCEPTION;
    }

    bool is_last = (magic & 0x1) != 0;
    bool return_bool = (magic & 0x2) != 0;

    TJSBufferEncoding enc = TJS__ENC_UTF8;
    TJSBufferNeedle needle = { 0 };

    /* Parse offset/encoding from argv[1]/argv[2]. */
    size_t offset = 0;
    if (tjs__buffer_normalize_search_offset(ctx, hay_len, argc, argv, is_last, false, &offset, &enc)) {
        return JS_EXCEPTION;
    }

    if (tjs__buffer_parse_needle(ctx, argv[0], enc, &needle)) {
        return JS_EXCEPTION;
    }

    /* Re-normalize for empty needle because lastIndexOf has different default/clamp. */
    if (is_last && needle.len == 0) {
        if (tjs__buffer_normalize_search_offset(ctx, hay_len, argc, argv, true, true, &offset, &enc)) {
            tjs__buffer_free_needle(ctx, &needle);
            return JS_EXCEPTION;
        }
    }

    int64_t idx = is_last ? tjs__buffer_find_backward(hay, hay_len, needle.ptr, needle.len, offset)
                          : tjs__buffer_find_forward(hay, hay_len, needle.ptr, needle.len, offset);

    tjs__buffer_free_needle(ctx, &needle);

    if (return_bool) {
        return JS_NewBool(ctx, idx >= 0);
    }
    return JS_NewInt64(ctx, idx);
}

static JSValue tjs_buffer_alloc(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    size_t size;
    if (tjs__buffer_get_len_arg(ctx, argv[0], &size)) {
        return JS_EXCEPTION;
    }

    JSValue out = tjs__buffer_new_with_length(ctx, size);
    if (JS_IsException(out)) {
        return out;
    }

    if (argc < 2 || JS_IsUndefined(argv[1])) {
        return out;
    }

    uint8_t *ptr;
    size_t len;
    if (tjs__buffer_get_view(ctx, out, &ptr, &len, NULL)) {
        JS_FreeValue(ctx, out);
        return JS_EXCEPTION;
    }

    if (JS_IsNumber(argv[1])) {
        int32_t v;
        if (JS_ToInt32(ctx, &v, argv[1])) {
            JS_FreeValue(ctx, out);
            return JS_EXCEPTION;
        }
        memset(ptr, v & 0xff, len);
        return out;
    }

    TJSBufferEncoding enc = TJS__ENC_UTF8;
    if (argc >= 3 && tjs__buffer_parse_encoding(ctx, argv[2], &enc)) {
        JS_FreeValue(ctx, out);
        return JS_EXCEPTION;
    }

    if (JS_IsString(argv[1])) {
        uint8_t *fill = NULL;
        size_t fill_len = 0;
        if (tjs__buffer_encode_string(ctx, argv[1], enc, &fill, &fill_len)) {
            JS_FreeValue(ctx, out);
            return JS_EXCEPTION;
        }
        if (fill_len > 0) {
            for (size_t i = 0; i < len; i++) {
                ptr[i] = fill[i % fill_len];
            }
        }
        js_free(ctx, fill);
        return out;
    }

    uint8_t *fill_ptr;
    size_t fill_len;
    if (!tjs__buffer_get_view(ctx, argv[1], &fill_ptr, &fill_len, NULL) && fill_len > 0) {
        for (size_t i = 0; i < len; i++) {
            ptr[i] = fill_ptr[i % fill_len];
        }
    } else {
        JS_ResetUncatchableError(ctx);
        JS_FreeValue(ctx, JS_GetException(ctx));
    }

    return out;
}

static JSValue tjs_buffer_alloc_unsafe(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    size_t size;
    if (tjs__buffer_get_len_arg(ctx, argv[0], &size)) {
        return JS_EXCEPTION;
    }
    return tjs__buffer_new_with_length(ctx, size);
}

static JSValue tjs_buffer_byte_length(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    if (JS_IsString(argv[0])) {
        TJSBufferEncoding enc = TJS__ENC_UTF8;
        if (argc >= 2 && tjs__buffer_parse_encoding(ctx, argv[1], &enc)) {
            return JS_EXCEPTION;
        }
        uint8_t *tmp = NULL;
        size_t tmp_len = 0;
        if (tjs__buffer_encode_string(ctx, argv[0], enc, &tmp, &tmp_len)) {
            return JS_EXCEPTION;
        }
        js_free(ctx, tmp);
        return JS_NewUint32(ctx, (uint32_t) tmp_len);
    }

    uint8_t *ptr;
    size_t len;
    if (!tjs__buffer_get_view(ctx, argv[0], &ptr, &len, NULL)) {
        return JS_NewUint32(ctx, (uint32_t) len);
    }
    JS_ResetUncatchableError(ctx);
    JS_FreeValue(ctx, JS_GetException(ctx));

    if (JS_IsArray(argv[0])) {
        int64_t len64 = 0;
        if (JS_GetLength(ctx, argv[0], &len64)) {
            return JS_EXCEPTION;
        }
        if (len64 < 0) {
            len64 = 0;
        }
        return JS_NewUint32(ctx, (uint32_t) len64);
    }

    return JS_ThrowTypeError(ctx, "value must be a string, Buffer, TypedArray, DataView or ArrayBuffer");
}

static JSValue tjs_buffer_is_buffer(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    JSValue ctor = tjs__buffer_get_ctor(ctx);
    if (JS_IsException(ctor)) {
        return JS_EXCEPTION;
    }
    int r = JS_IsInstanceOf(ctx, argv[0], ctor);
    JS_FreeValue(ctx, ctor);
    if (r < 0) {
        JS_ResetUncatchableError(ctx);
        JS_FreeValue(ctx, JS_GetException(ctx));
        return JS_NewBool(ctx, false);
    }
    return JS_NewBool(ctx, r);
}

static JSValue tjs_buffer_is_encoding(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    if (!JS_IsString(argv[0])) {
        return JS_NewBool(ctx, false);
    }
    const char *s = JS_ToCString(ctx, argv[0]);
    if (!s) {
        return JS_EXCEPTION;
    }
    bool ok = tjs__buffer_is_encoding_name(s);
    JS_FreeCString(ctx, s);
    return JS_NewBool(ctx, ok);
}

static JSValue tjs_buffer_concat(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    if (!JS_IsArray(argv[0])) {
        return JS_ThrowTypeError(ctx, "list must be an array of Uint8Array");
    }

    int64_t count64 = 0;
    if (JS_GetLength(ctx, argv[0], &count64)) {
        return JS_EXCEPTION;
    }
    if (count64 < 0) {
        count64 = 0;
    }
    uint32_t count = (uint32_t) count64;

    size_t total = 0;
    for (uint32_t i = 0; i < count; i++) {
        JSValue item = JS_GetPropertyUint32(ctx, argv[0], i);
        if (JS_IsException(item)) {
            return JS_EXCEPTION;
        }
        uint8_t *p;
        size_t len;
        if (tjs__buffer_get_view(ctx, item, &p, &len, NULL)) {
            JS_FreeValue(ctx, item);
            return JS_ThrowTypeError(ctx, "list items must be Uint8Array");
        }
        total += len;
        JS_FreeValue(ctx, item);
    }

    size_t out_len = total;
    if (argc >= 2 && !JS_IsUndefined(argv[1])) {
        size_t n;
        if (tjs__buffer_get_len_arg(ctx, argv[1], &n)) {
            return JS_EXCEPTION;
        }
        if (n < out_len) {
            out_len = n;
        }
    }

    JSValue out = tjs__buffer_new_with_length(ctx, out_len);
    if (JS_IsException(out)) {
        return out;
    }
    uint8_t *dst;
    size_t dst_len;
    if (tjs__buffer_get_view(ctx, out, &dst, &dst_len, NULL)) {
        JS_FreeValue(ctx, out);
        return JS_EXCEPTION;
    }

    size_t off = 0;
    for (uint32_t i = 0; i < count && off < dst_len; i++) {
        JSValue item = JS_GetPropertyUint32(ctx, argv[0], i);
        uint8_t *src;
        size_t src_len;
        if (tjs__buffer_get_view(ctx, item, &src, &src_len, NULL)) {
            JS_FreeValue(ctx, item);
            JS_FreeValue(ctx, out);
            return JS_ThrowTypeError(ctx, "list items must be Uint8Array");
        }
        size_t n = src_len;
        if (n > dst_len - off) {
            n = dst_len - off;
        }
        memcpy(dst + off, src, n);
        off += n;
        JS_FreeValue(ctx, item);
    }

    return out;
}

static JSValue tjs_buffer_fill(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    uint8_t *ptr;
    size_t len;
    if (tjs__buffer_get_view(ctx, this_val, &ptr, &len, NULL)) {
        return JS_EXCEPTION;
    }

    size_t start = 0;
    size_t end = len;

    if (argc >= 2 && !JS_IsUndefined(argv[1])) {
        uint64_t n;
        if (JS_ToIndex(ctx, &n, argv[1])) {
            return JS_EXCEPTION;
        }
        start = n > len ? len : (size_t) n;
    }
    if (argc >= 3 && !JS_IsUndefined(argv[2])) {
        uint64_t n;
        if (JS_ToIndex(ctx, &n, argv[2])) {
            return JS_EXCEPTION;
        }
        end = n > len ? len : (size_t) n;
    }

    if (end <= start) {
        return JS_DupValue(ctx, this_val);
    }

    size_t fill_len = end - start;

    if (JS_IsNumber(argv[0])) {
        int32_t v;
        if (JS_ToInt32(ctx, &v, argv[0])) {
            return JS_EXCEPTION;
        }
        memset(ptr + start, v & 0xff, fill_len);
        return JS_DupValue(ctx, this_val);
    }

    if (JS_IsString(argv[0])) {
        TJSBufferEncoding enc = TJS__ENC_UTF8;
        if (argc >= 4 && !JS_IsUndefined(argv[3])) {
            if (tjs__buffer_parse_encoding(ctx, argv[3], &enc)) {
                return JS_EXCEPTION;
            }
        }
        uint8_t *fill_data = NULL;
        size_t fill_data_len = 0;
        if (tjs__buffer_encode_string(ctx, argv[0], enc, &fill_data, &fill_data_len)) {
            return JS_EXCEPTION;
        }
        if (fill_data_len > 0) {
            for (size_t i = 0; i < fill_len; i++) {
                ptr[start + i] = fill_data[i % fill_data_len];
            }
        }
        js_free(ctx, fill_data);
        return JS_DupValue(ctx, this_val);
    }

    uint8_t *fill_ptr;
    size_t fill_data_len;
    if (!tjs__buffer_get_view(ctx, argv[0], &fill_ptr, &fill_data_len, NULL) && fill_data_len > 0) {
        for (size_t i = 0; i < fill_len; i++) {
            ptr[start + i] = fill_ptr[i % fill_data_len];
        }
    }

    return JS_DupValue(ctx, this_val);
}

static JSValue tjs_buffer_to_json(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    uint8_t *ptr;
    size_t len;
    if (tjs__buffer_get_view(ctx, this_val, &ptr, &len, NULL)) {
        return JS_EXCEPTION;
    }

    JSValue obj = JS_NewObject(ctx);
    if (JS_IsException(obj)) {
        return JS_EXCEPTION;
    }

    JS_DefinePropertyValueStr(ctx, obj, "type", JS_NewString(ctx, "Buffer"), JS_PROP_C_W_E);

    JSValue data = JS_NewArray(ctx);
    if (JS_IsException(data)) {
        JS_FreeValue(ctx, obj);
        return JS_EXCEPTION;
    }
    for (size_t i = 0; i < len; i++) {
        JS_DefinePropertyValueUint32(ctx, data, (uint32_t) i, JS_NewInt32(ctx, ptr[i]), JS_PROP_C_W_E);
    }
    JS_DefinePropertyValueStr(ctx, obj, "data", data, JS_PROP_C_W_E);

    return obj;
}

static JSValue tjs_buffer_inspect(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    uint8_t *ptr;
    size_t len;
    if (tjs__buffer_get_view(ctx, this_val, &ptr, &len, NULL)) {
        return JS_EXCEPTION;
    }

    static const char hex[] = "0123456789abcdef";
    size_t shown = len > 50 ? 50 : len;
    size_t cap = 8 + shown * 3 + 64;
    char *buf = js_malloc(ctx, cap);
    if (!buf) {
        return JS_EXCEPTION;
    }

    size_t pos = 0;
    memcpy(buf + pos, "<Buffer", 7);
    pos += 7;
    for (size_t i = 0; i < shown; i++) {
        buf[pos++] = ' ';
        buf[pos++] = hex[ptr[i] >> 4];
        buf[pos++] = hex[ptr[i] & 0x0f];
    }
    if (len > shown) {
        pos += snprintf(buf + pos, cap - pos, " ... %zu more byte%s", len - shown, len - shown == 1 ? "" : "s");
    }
    buf[pos++] = '>';

    JSValue out = JS_NewStringLen(ctx, buf, pos);
    js_free(ctx, buf);
    return out;
}

static JSValue tjs_buffer_equals(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    uint8_t *a;
    size_t a_len;
    if (tjs__buffer_get_view(ctx, this_val, &a, &a_len, NULL)) {
        return JS_EXCEPTION;
    }

    uint8_t *b;
    size_t b_len;
    if (tjs__buffer_get_uint8_view(ctx, argv[0], &b, &b_len)) {
        return JS_ThrowTypeError(ctx, "The \"otherBuffer\" argument must be an instance of Buffer or Uint8Array");
    }

    if (a_len != b_len) {
        return JS_NewBool(ctx, false);
    }

    return JS_NewBool(ctx, memcmp(a, b, a_len) == 0);
}

static JSValue tjs_buffer_compare(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    uint8_t *a;
    size_t a_len;
    if (tjs__buffer_get_uint8_view(ctx, argv[0], &a, &a_len)) {
        return JS_ThrowTypeError(ctx, "The \"buf1\" argument must be an instance of Buffer or Uint8Array");
    }

    uint8_t *b;
    size_t b_len;
    if (tjs__buffer_get_uint8_view(ctx, argv[1], &b, &b_len)) {
        return JS_ThrowTypeError(ctx, "The \"buf2\" argument must be an instance of Buffer or Uint8Array");
    }

    return JS_NewInt32(ctx, tjs__buffer_compare_bytes(a, a_len, b, b_len));
}

static JSValue tjs_buffer_proto_compare(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    uint8_t *src;
    size_t src_len;
    if (tjs__buffer_get_view(ctx, this_val, &src, &src_len, NULL)) {
        return JS_EXCEPTION;
    }

    uint8_t *target;
    size_t target_len;
    if (tjs__buffer_get_uint8_view(ctx, argv[0], &target, &target_len)) {
        return JS_ThrowTypeError(ctx, "The \"target\" argument must be an instance of Buffer or Uint8Array");
    }

    size_t target_start = 0;
    size_t target_end = target_len;
    size_t source_start = 0;
    size_t source_end = src_len;

    if (argc >= 2 && tjs__buffer_arg_index(ctx, argv[1], &target_start, 0)) {
        return JS_EXCEPTION;
    }
    if (argc >= 3 && tjs__buffer_arg_index(ctx, argv[2], &target_end, target_len)) {
        return JS_EXCEPTION;
    }
    if (argc >= 4 && tjs__buffer_arg_index(ctx, argv[3], &source_start, 0)) {
        return JS_EXCEPTION;
    }
    if (argc >= 5 && tjs__buffer_arg_index(ctx, argv[4], &source_end, src_len)) {
        return JS_EXCEPTION;
    }

    if (target_start > target_len || target_end > target_len || target_end < target_start) {
        return JS_ThrowRangeError(ctx, "target range out of bounds");
    }
    if (source_start > src_len || source_end > src_len || source_end < source_start) {
        return JS_ThrowRangeError(ctx, "source range out of bounds");
    }

    return JS_NewInt32(ctx,
                       tjs__buffer_compare_bytes(src + source_start, source_end - source_start,
                                                 target + target_start, target_end - target_start));
}

static JSValue tjs_buffer_swap(JSContext *ctx, JSValue this_val, int argc, JSValue *argv, int magic) {
    uint8_t *ptr;
    size_t len;
    if (tjs__buffer_get_view(ctx, this_val, &ptr, &len, NULL)) {
        return JS_EXCEPTION;
    }

    int group_size = magic; /* 2, 4, or 8 */
    if (len % group_size != 0) {
        return JS_ThrowRangeError(ctx, "Buffer size must be a multiple of %d", group_size);
    }

    uint8_t tmp[8];
    for (size_t i = 0; i < len; i += group_size) {
        memcpy(tmp, ptr + i, group_size);
        for (int j = 0; j < group_size; j++) {
            ptr[i + j] = tmp[group_size - 1 - j];
        }
    }

    return JS_DupValue(ctx, this_val);
}

static JSValue tjs_buffer_from(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    JSValueConst value = argv[0];

    if (JS_IsString(value)) {
        TJSBufferEncoding enc = TJS__ENC_UTF8;
        if (argc >= 2 && tjs__buffer_parse_encoding(ctx, argv[1], &enc)) {
            return JS_EXCEPTION;
        }
        uint8_t *tmp = NULL;
        size_t tmp_len = 0;
        if (tjs__buffer_encode_string(ctx, value, enc, &tmp, &tmp_len)) {
            return JS_EXCEPTION;
        }
        JSValue out = tjs__buffer_new_with_length(ctx, tmp_len);
        if (JS_IsException(out)) {
            js_free(ctx, tmp);
            return out;
        }
        uint8_t *dst;
        size_t dst_len;
        if (tjs__buffer_get_view(ctx, out, &dst, &dst_len, NULL)) {
            JS_FreeValue(ctx, out);
            js_free(ctx, tmp);
            return JS_EXCEPTION;
        }
        memcpy(dst, tmp, tmp_len);
        js_free(ctx, tmp);
        return out;
    }

    if (JS_IsArray(value)) {
        int64_t len64 = 0;
        if (JS_GetLength(ctx, value, &len64)) {
            return JS_EXCEPTION;
        }
        if (len64 < 0) {
            len64 = 0;
        }
        uint32_t len = (uint32_t) len64;
        size_t off = 0;
        size_t take = len;

        JSValue out = tjs__buffer_new_with_length(ctx, take);
        if (JS_IsException(out)) {
            return out;
        }
        uint8_t *dst;
        size_t dst_len;
        if (tjs__buffer_get_view(ctx, out, &dst, &dst_len, NULL)) {
            JS_FreeValue(ctx, out);
            return JS_EXCEPTION;
        }

        for (size_t i = 0; i < take; i++) {
            JSValue item = JS_GetPropertyUint32(ctx, value, (uint32_t) (off + i));
            uint32_t v = 0;
            if (!JS_IsUndefined(item)) {
                JS_ToUint32(ctx, &v, item);
            }
            dst[i] = (uint8_t) (v & 0xff);
            JS_FreeValue(ctx, item);
        }
        return out;
    }

    JSValue ctor = tjs__buffer_get_ctor(ctx);
    if (JS_IsException(ctor)) {
        return JS_EXCEPTION;
    }
    int is_buf = JS_IsInstanceOf(ctx, value, ctor);
    JS_FreeValue(ctx, ctor);
    if (is_buf < 0) {
        is_buf = 0;
        JS_ResetUncatchableError(ctx);
        JS_FreeValue(ctx, JS_GetException(ctx));
    }

    uint8_t *src;
    size_t src_len;
    JSValue ab = JS_UNDEFINED;
    if (!tjs__buffer_get_view(ctx, value, &src, &src_len, &ab)) {
        if (is_buf) {
            JSValue out = tjs__buffer_new_with_length(ctx, src_len);
            if (JS_IsException(out)) {
                JS_FreeValue(ctx, ab);
                return out;
            }
            uint8_t *dst;
            size_t dst_len;
            if (tjs__buffer_get_view(ctx, out, &dst, &dst_len, NULL)) {
                JS_FreeValue(ctx, ab);
                JS_FreeValue(ctx, out);
                return JS_EXCEPTION;
            }
            memcpy(dst, src, src_len);
            JS_FreeValue(ctx, ab);
            return out;
        }

        if (JS_IsArrayBuffer(value)) {
            size_t off = 0;
            size_t take = src_len;
            if (argc >= 2 && !JS_IsUndefined(argv[1]) && !JS_IsString(argv[1])) {
                uint64_t n;
                if (JS_ToIndex(ctx, &n, argv[1])) {
                    JS_FreeValue(ctx, ab);
                    return JS_EXCEPTION;
                }
                off = n > src_len ? src_len : (size_t) n;
            }
            if (argc >= 3 && !JS_IsUndefined(argv[2])) {
                uint64_t n;
                if (JS_ToIndex(ctx, &n, argv[2])) {
                    JS_FreeValue(ctx, ab);
                    return JS_EXCEPTION;
                }
                take = n;
            } else {
                take = src_len - off;
            }
            if (off + take > src_len) {
                take = src_len - off;
            }

            JSValue out = tjs__buffer_new_with_ab(ctx, ab, off, take);
            JS_FreeValue(ctx, ab);
            return out;
        }

        int64_t len64 = 0;
        if (JS_GetLength(ctx, value, &len64) || len64 < 0) {
            len64 = 0;
            JS_ResetUncatchableError(ctx);
            JS_FreeValue(ctx, JS_GetException(ctx));
        }
        JSValue out = tjs__buffer_new_with_length(ctx, (size_t) len64);
        if (JS_IsException(out)) {
            JS_FreeValue(ctx, ab);
            return out;
        }
        uint8_t *dst;
        size_t dst_len;
        if (tjs__buffer_get_view(ctx, out, &dst, &dst_len, NULL)) {
            JS_FreeValue(ctx, ab);
            JS_FreeValue(ctx, out);
            return JS_EXCEPTION;
        }
        for (size_t i = 0; i < dst_len; i++) {
            JSValue item = JS_GetPropertyUint32(ctx, value, (uint32_t) i);
            if (JS_IsException(item)) {
                JS_FreeValue(ctx, ab);
                JS_FreeValue(ctx, out);
                return JS_EXCEPTION;
            }
            uint32_t v = 0;
            if (!JS_IsUndefined(item)) {
                JS_ToUint32(ctx, &v, item);
            }
            dst[i] = (uint8_t) (v & 0xff);
            JS_FreeValue(ctx, item);
        }
        JS_FreeValue(ctx, ab);
        return out;
    }

    JS_ResetUncatchableError(ctx);
    JS_FreeValue(ctx, JS_GetException(ctx));

    JSValue s = JS_ToString(ctx, value);
    if (JS_IsException(s)) {
        return JS_EXCEPTION;
    }
    JSValue out = tjs_buffer_from(ctx, this_val, 1, &s);
    JS_FreeValue(ctx, s);
    return out;
}

static JSValue tjs_buffer_copy(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    uint8_t *src;
    size_t src_len;
    if (tjs__buffer_get_view(ctx, this_val, &src, &src_len, NULL)) {
        return JS_EXCEPTION;
    }

    uint8_t *dst;
    size_t dst_len;
    if (tjs__buffer_get_view(ctx, argv[0], &dst, &dst_len, NULL)) {
        return JS_ThrowTypeError(ctx, "target must be a Buffer or Uint8Array");
    }

    size_t target_start = 0;
    size_t source_start = 0;
    size_t source_end = src_len;

    if (argc >= 2 && tjs__buffer_arg_index(ctx, argv[1], &target_start, 0)) {
        return JS_EXCEPTION;
    }
    if (argc >= 3 && tjs__buffer_arg_index(ctx, argv[2], &source_start, 0)) {
        return JS_EXCEPTION;
    }
    if (argc >= 4 && tjs__buffer_arg_index(ctx, argv[3], &source_end, src_len)) {
        return JS_EXCEPTION;
    }

    if (target_start > dst_len) {
        target_start = dst_len;
    }
    if (source_start > src_len) {
        source_start = src_len;
    }
    if (source_end > src_len) {
        source_end = src_len;
    }
    if (source_end <= source_start || target_start >= dst_len) {
        return JS_NewInt32(ctx, 0);
    }

    size_t n = source_end - source_start;
    if (n > dst_len - target_start) {
        n = dst_len - target_start;
    }
    memmove(dst + target_start, src + source_start, n);
    return JS_NewInt32(ctx, (int32_t) n);
}

static JSValue tjs_buffer_subarray(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    size_t off, len, bpe;
    JSValue ab = JS_GetTypedArrayBuffer(ctx, this_val, &off, &len, &bpe);
    if (JS_IsException(ab)) {
        return JS_EXCEPTION;
    }

    int64_t start_in = 0;
    int64_t end_in = (int64_t) len;
    if (argc >= 1 && !JS_IsUndefined(argv[0])) {
        if (JS_ToInt64(ctx, &start_in, argv[0])) {
            JS_FreeValue(ctx, ab);
            return JS_EXCEPTION;
        }
    }
    if (argc >= 2 && !JS_IsUndefined(argv[1])) {
        if (JS_ToInt64(ctx, &end_in, argv[1])) {
            JS_FreeValue(ctx, ab);
            return JS_EXCEPTION;
        }
    }

    size_t start = tjs__buffer_normalize_slice_start(start_in, len);
    size_t end = tjs__buffer_normalize_slice_end(end_in, len);
    if (end < start) {
        end = start;
    }
    JSValue out = tjs__buffer_new_with_ab(ctx, ab, off + start, end - start);
    JS_FreeValue(ctx, ab);
    (void) bpe;
    return out;
}

static JSValue tjs_buffer_latin1_slice(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    uint8_t *src;
    size_t src_len;
    if (tjs__buffer_get_view(ctx, this_val, &src, &src_len, NULL)) {
        return JS_EXCEPTION;
    }

    int64_t start_in = 0;
    int64_t end_in = (int64_t) src_len;
    if (argc >= 1 && !JS_IsUndefined(argv[0])) {
        if (JS_ToInt64(ctx, &start_in, argv[0])) {
            return JS_EXCEPTION;
        }
    }
    if (argc >= 2 && !JS_IsUndefined(argv[1])) {
        if (JS_ToInt64(ctx, &end_in, argv[1])) {
            return JS_EXCEPTION;
        }
    }

    size_t start = tjs__buffer_normalize_slice_start(start_in, src_len);
    size_t end = tjs__buffer_normalize_slice_end(end_in, src_len);
    if (end < start) {
        end = start;
    }

    return tjs__buffer_decode_single_byte(ctx, src + start, end - start, false);
}

static JSValue tjs_buffer_utf8_slice(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    uint8_t *src;
    size_t src_len;
    if (tjs__buffer_get_view(ctx, this_val, &src, &src_len, NULL)) {
        return JS_EXCEPTION;
    }

    int64_t start_in = 0;
    int64_t end_in = (int64_t) src_len;
    if (argc >= 1 && !JS_IsUndefined(argv[0])) {
        if (JS_ToInt64(ctx, &start_in, argv[0])) {
            return JS_EXCEPTION;
        }
    }
    if (argc >= 2 && !JS_IsUndefined(argv[1])) {
        if (JS_ToInt64(ctx, &end_in, argv[1])) {
            return JS_EXCEPTION;
        }
    }

    size_t start = tjs__buffer_normalize_slice_start(start_in, src_len);
    size_t end = tjs__buffer_normalize_slice_end(end_in, src_len);
    if (end < start) {
        end = start;
    }

    // UTF-8: interpret bytes as UTF-8 encoded
    JSValue str = JS_NewStringLen(ctx, (const char *)(src + start), end - start);
    return str;
}

static JSValue tjs_buffer_ucs2_slice(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    uint8_t *src;
    size_t src_len;
    if (tjs__buffer_get_view(ctx, this_val, &src, &src_len, NULL)) {
        return JS_EXCEPTION;
    }

    int64_t start_in = 0;
    int64_t end_in = (int64_t) src_len;
    if (argc >= 1 && !JS_IsUndefined(argv[0])) {
        if (JS_ToInt64(ctx, &start_in, argv[0])) {
            return JS_EXCEPTION;
        }
    }
    if (argc >= 2 && !JS_IsUndefined(argv[1])) {
        if (JS_ToInt64(ctx, &end_in, argv[1])) {
            return JS_EXCEPTION;
        }
    }

    size_t start = tjs__buffer_normalize_slice_start(start_in, src_len);
    size_t end = tjs__buffer_normalize_slice_end(end_in, src_len);
    if (end < start) {
        end = start;
    }

    // UCS-2/UTF-16 LE: each 2 bytes (little-endian) form a UTF-16 code unit.
    // For odd-length ranges, Node.js behavior truncates the dangling byte.
    size_t byte_len = end - start;
    size_t char_count = byte_len / 2;
    if (char_count == 0) {
        return JS_NewString(ctx, "");
    }

    // Worst case per UTF-16 code unit is 3 UTF-8 bytes for BMP characters.
    size_t utf8_cap = (char_count * 3) + 1;
    char *utf8_buf = malloc(utf8_cap);
    if (!utf8_buf) {
        return JS_ThrowOutOfMemory(ctx);
    }

    const uint8_t *p = src + start;
    size_t utf8_len = 0;
    for (size_t i = 0; i < char_count; i++) {
        uint16_t ch = p[i * 2] | (p[i * 2 + 1] << 8);
        if (ch < 0x80) {
            utf8_buf[utf8_len++] = (char)ch;
        } else if (ch < 0x800) {
            utf8_buf[utf8_len++] = 0xC0 | (ch >> 6);
            utf8_buf[utf8_len++] = 0x80 | (ch & 0x3F);
        } else {
            utf8_buf[utf8_len++] = 0xE0 | (ch >> 12);
            utf8_buf[utf8_len++] = 0x80 | ((ch >> 6) & 0x3F);
            utf8_buf[utf8_len++] = 0x80 | (ch & 0x3F);
        }
    }
    utf8_buf[utf8_len] = '\0';
    
    JSValue str = JS_NewString(ctx, utf8_buf);
    free(utf8_buf);
    return str;
}

static JSValue tjs_buffer_base64_slice(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    uint8_t *src;
    size_t src_len;
    if (tjs__buffer_get_view(ctx, this_val, &src, &src_len, NULL)) {
        return JS_EXCEPTION;
    }

    int64_t start_in = 0;
    int64_t end_in = (int64_t) src_len;
    if (argc >= 1 && !JS_IsUndefined(argv[0])) {
        if (JS_ToInt64(ctx, &start_in, argv[0])) {
            return JS_EXCEPTION;
        }
    }
    if (argc >= 2 && !JS_IsUndefined(argv[1])) {
        if (JS_ToInt64(ctx, &end_in, argv[1])) {
            return JS_EXCEPTION;
        }
    }

    size_t start = tjs__buffer_normalize_slice_start(start_in, src_len);
    size_t end = tjs__buffer_normalize_slice_end(end_in, src_len);
    if (end < start) {
        end = start;
    }

    // Base64: encode raw bytes directly, avoiding UTF-8 string roundtrips.
    size_t slice_len = end - start;
    char *b64 = NULL;
    size_t b64_len = 0;
    if (tjs__buffer_encode_base64(ctx, src + start, slice_len, &b64, &b64_len)) {
        return JS_EXCEPTION;
    }

    JSValue out = JS_NewStringLen(ctx, b64, b64_len);
    js_free(ctx, b64);
    return out;
}

static JSValue tjs_buffer_to_string(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    uint8_t *src;
    size_t src_len;
    if (tjs__buffer_get_view(ctx, this_val, &src, &src_len, NULL)) {
        return JS_EXCEPTION;
    }

    TJSBufferEncoding enc = TJS__ENC_UTF8;
    if (argc >= 1 && tjs__buffer_parse_encoding(ctx, argv[0], &enc)) {
        return JS_EXCEPTION;
    }

    size_t start = 0;
    size_t end = src_len;
    if (argc >= 2 && !JS_IsUndefined(argv[1])) {
        uint64_t n;
        if (JS_ToIndex(ctx, &n, argv[1])) {
            return JS_EXCEPTION;
        }
        start = n > src_len ? src_len : (size_t) n;
    }
    if (argc >= 3 && !JS_IsUndefined(argv[2])) {
        uint64_t n;
        if (JS_ToIndex(ctx, &n, argv[2])) {
            return JS_EXCEPTION;
        }
        end = n > src_len ? src_len : (size_t) n;
    }
    if (end <= start) {
        return JS_NewString(ctx, "");
    }
    return tjs__buffer_decode_bytes(ctx, src + start, end - start, enc);
}

static JSValue tjs_buffer_write(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    uint8_t *dst;
    size_t dst_len;
    if (tjs__buffer_get_view(ctx, this_val, &dst, &dst_len, NULL)) {
        return JS_EXCEPTION;
    }

    size_t off = 0;
    size_t max_len = dst_len;
    TJSBufferEncoding enc = TJS__ENC_UTF8;

    if (argc >= 2) {
        if (JS_IsString(argv[1])) {
            if (tjs__buffer_parse_encoding(ctx, argv[1], &enc)) {
                return JS_EXCEPTION;
            }
        } else {
            uint64_t n;
            if (JS_ToIndex(ctx, &n, argv[1])) {
                return JS_EXCEPTION;
            }
            off = n > dst_len ? dst_len : (size_t) n;
        }
    }
    if (argc >= 3) {
        if (JS_IsString(argv[2])) {
            if (tjs__buffer_parse_encoding(ctx, argv[2], &enc)) {
                return JS_EXCEPTION;
            }
            max_len = dst_len - off;
        } else {
            uint64_t n;
            if (JS_ToIndex(ctx, &n, argv[2])) {
                return JS_EXCEPTION;
            }
            max_len = n > (dst_len - off) ? (dst_len - off) : (size_t) n;
        }
    } else {
        max_len = dst_len - off;
    }
    if (argc >= 4) {
        if (tjs__buffer_parse_encoding(ctx, argv[3], &enc)) {
            return JS_EXCEPTION;
        }
    }

    if (off >= dst_len || max_len == 0) {
        return JS_NewInt32(ctx, 0);
    }

    uint8_t *src = NULL;
    size_t src_len = 0;
    if (tjs__buffer_encode_string(ctx, argv[0], enc, &src, &src_len)) {
        return JS_EXCEPTION;
    }

    if (enc == TJS__ENC_UTF8) {
        src_len = tjs__buffer_trim_utf8(src, src_len, max_len);
    } else if (src_len > max_len) {
        src_len = max_len;
    }

    if (src_len > dst_len - off) {
        src_len = dst_len - off;
    }
    memcpy(dst + off, src, src_len);
    js_free(ctx, src);
    return JS_NewInt32(ctx, (int32_t) src_len);
}

static uint16_t tjs__read_u16_be(const uint8_t *p) {
    return ((uint16_t) p[0] << 8) | p[1];
}
static uint16_t tjs__read_u16_le(const uint8_t *p) {
    return ((uint16_t) p[1] << 8) | p[0];
}
static uint32_t tjs__read_u32_be(const uint8_t *p) {
    return ((uint32_t) p[0] << 24) | ((uint32_t) p[1] << 16) | ((uint32_t) p[2] << 8) | p[3];
}
static uint32_t tjs__read_u32_le(const uint8_t *p) {
    return ((uint32_t) p[3] << 24) | ((uint32_t) p[2] << 16) | ((uint32_t) p[1] << 8) | p[0];
}
static uint64_t tjs__read_u64_be(const uint8_t *p) {
    return ((uint64_t) p[0] << 56) | ((uint64_t) p[1] << 48) | ((uint64_t) p[2] << 40) | ((uint64_t) p[3] << 32) |
           ((uint64_t) p[4] << 24) | ((uint64_t) p[5] << 16) | ((uint64_t) p[6] << 8) | p[7];
}
static uint64_t tjs__read_u64_le(const uint8_t *p) {
    return ((uint64_t) p[7] << 56) | ((uint64_t) p[6] << 48) | ((uint64_t) p[5] << 40) | ((uint64_t) p[4] << 32) |
           ((uint64_t) p[3] << 24) | ((uint64_t) p[2] << 16) | ((uint64_t) p[1] << 8) | p[0];
}
static void tjs__write_u16_be(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t) (v >> 8);
    p[1] = (uint8_t) v;
}
static void tjs__write_u16_le(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t) v;
    p[1] = (uint8_t) (v >> 8);
}
static void tjs__write_u32_be(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t) (v >> 24);
    p[1] = (uint8_t) (v >> 16);
    p[2] = (uint8_t) (v >> 8);
    p[3] = (uint8_t) v;
}
static void tjs__write_u32_le(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t) v;
    p[1] = (uint8_t) (v >> 8);
    p[2] = (uint8_t) (v >> 16);
    p[3] = (uint8_t) (v >> 24);
}
static void tjs__write_u64_be(uint8_t *p, uint64_t v) {
    p[0] = (uint8_t) (v >> 56);
    p[1] = (uint8_t) (v >> 48);
    p[2] = (uint8_t) (v >> 40);
    p[3] = (uint8_t) (v >> 32);
    p[4] = (uint8_t) (v >> 24);
    p[5] = (uint8_t) (v >> 16);
    p[6] = (uint8_t) (v >> 8);
    p[7] = (uint8_t) v;
}
static void tjs__write_u64_le(uint8_t *p, uint64_t v) {
    p[0] = (uint8_t) v;
    p[1] = (uint8_t) (v >> 8);
    p[2] = (uint8_t) (v >> 16);
    p[3] = (uint8_t) (v >> 24);
    p[4] = (uint8_t) (v >> 32);
    p[5] = (uint8_t) (v >> 40);
    p[6] = (uint8_t) (v >> 48);
    p[7] = (uint8_t) (v >> 56);
}

static JSValue tjs_buffer_num(JSContext *ctx, JSValue this_val, int argc, JSValue *argv, int magic) {
    int kind = magic & 0xff;
    bool is_le = (magic & TJS__NUM_F_LE) != 0;
    bool is_write = (magic & TJS__NUM_F_WRITE) != 0;

    uint8_t *ptr;
    size_t len;
    if (tjs__buffer_get_view(ctx, this_val, &ptr, &len, NULL)) {
        return JS_EXCEPTION;
    }

    size_t off = 0;
    if (tjs__buffer_arg_index(ctx, is_write ? argv[1] : argv[0], &off, 0)) {
        return JS_EXCEPTION;
    }

    size_t size = 1;
    switch (kind) {
        case TJS__NUM_I8:
        case TJS__NUM_U8:
            size = 1;
            break;
        case TJS__NUM_I16:
        case TJS__NUM_U16:
            size = 2;
            break;
        case TJS__NUM_I32:
        case TJS__NUM_U32:
        case TJS__NUM_F32:
            size = 4;
            break;
        default:
            size = 8;
            break;
    }

    if (off + size > len) {
        if (is_write) {
            return JS_ThrowRangeError(ctx, "The specified offset is out of range");
        }
        return JS_ThrowRangeError(ctx, "The value of \"offset\" is out of range");
    }

    uint8_t *p = ptr + off;

    if (!is_write) {
        switch (kind) {
            case TJS__NUM_I8:
                return JS_NewInt32(ctx, (int8_t) p[0]);
            case TJS__NUM_U8:
                return JS_NewInt32(ctx, p[0]);
            case TJS__NUM_I16:
                return JS_NewInt32(ctx, (int16_t) (is_le ? tjs__read_u16_le(p) : tjs__read_u16_be(p)));
            case TJS__NUM_U16:
                return JS_NewInt32(ctx, is_le ? tjs__read_u16_le(p) : tjs__read_u16_be(p));
            case TJS__NUM_I32:
                return JS_NewInt32(ctx, (int32_t) (is_le ? tjs__read_u32_le(p) : tjs__read_u32_be(p)));
            case TJS__NUM_U32:
                return JS_NewFloat64(ctx, (double) (is_le ? tjs__read_u32_le(p) : tjs__read_u32_be(p)));
            case TJS__NUM_F32: {
                uint32_t v = is_le ? tjs__read_u32_le(p) : tjs__read_u32_be(p);
                float f;
                memcpy(&f, &v, sizeof(f));
                return JS_NewFloat64(ctx, f);
            }
            case TJS__NUM_F64: {
                uint64_t v = is_le ? tjs__read_u64_le(p) : tjs__read_u64_be(p);
                double d;
                memcpy(&d, &v, sizeof(d));
                return JS_NewFloat64(ctx, d);
            }
            case TJS__NUM_BIGI64: {
                int64_t v = (int64_t) (is_le ? tjs__read_u64_le(p) : tjs__read_u64_be(p));
                return JS_NewBigInt64(ctx, v);
            }
            case TJS__NUM_BIGU64: {
                uint64_t v = is_le ? tjs__read_u64_le(p) : tjs__read_u64_be(p);
                return JS_NewBigUint64(ctx, v);
            }
        }
        return JS_UNDEFINED;
    }

    switch (kind) {
        case TJS__NUM_I8: {
            int32_t v;
            if (JS_ToInt32(ctx, &v, argv[0])) {
                return JS_EXCEPTION;
            }
            if (v < INT8_MIN || v > INT8_MAX) {
                return JS_ThrowRangeError(ctx, "Value out of range");
            }
            p[0] = (uint8_t) ((int8_t) v);
            break;
        }
        case TJS__NUM_U8: {
            uint32_t v;
            if (JS_ToUint32(ctx, &v, argv[0])) {
                return JS_EXCEPTION;
            }
            if (v > UINT8_MAX) {
                return JS_ThrowRangeError(ctx, "Value out of range");
            }
            p[0] = (uint8_t) v;
            break;
        }
        case TJS__NUM_I16: {
            int32_t v;
            if (JS_ToInt32(ctx, &v, argv[0])) {
                return JS_EXCEPTION;
            }
            if (v < INT16_MIN || v > INT16_MAX) {
                return JS_ThrowRangeError(ctx, "Value out of range");
            }
            if (is_le) {
                tjs__write_u16_le(p, (uint16_t) (int16_t) v);
            } else {
                tjs__write_u16_be(p, (uint16_t) (int16_t) v);
            }
            break;
        }
        case TJS__NUM_U16: {
            uint32_t v;
            if (JS_ToUint32(ctx, &v, argv[0])) {
                return JS_EXCEPTION;
            }
            if (v > UINT16_MAX) {
                return JS_ThrowRangeError(ctx, "Value out of range");
            }
            if (is_le) {
                tjs__write_u16_le(p, (uint16_t) v);
            } else {
                tjs__write_u16_be(p, (uint16_t) v);
            }
            break;
        }
        case TJS__NUM_I32: {
            int32_t v;
            if (JS_ToInt32(ctx, &v, argv[0])) {
                return JS_EXCEPTION;
            }
            if (is_le) {
                tjs__write_u32_le(p, (uint32_t) v);
            } else {
                tjs__write_u32_be(p, (uint32_t) v);
            }
            break;
        }
        case TJS__NUM_U32: {
            uint32_t v;
            if (JS_ToUint32(ctx, &v, argv[0])) {
                return JS_EXCEPTION;
            }
            if (is_le) {
                tjs__write_u32_le(p, v);
            } else {
                tjs__write_u32_be(p, v);
            }
            break;
        }
        case TJS__NUM_F32: {
            double d;
            if (JS_ToFloat64(ctx, &d, argv[0])) {
                return JS_EXCEPTION;
            }
            float f = (float) d;
            uint32_t v;
            memcpy(&v, &f, sizeof(v));
            if (is_le) {
                tjs__write_u32_le(p, v);
            } else {
                tjs__write_u32_be(p, v);
            }
            break;
        }
        case TJS__NUM_F64: {
            double d;
            if (JS_ToFloat64(ctx, &d, argv[0])) {
                return JS_EXCEPTION;
            }
            uint64_t v;
            memcpy(&v, &d, sizeof(v));
            if (is_le) {
                tjs__write_u64_le(p, v);
            } else {
                tjs__write_u64_be(p, v);
            }
            break;
        }
        case TJS__NUM_BIGI64: {
            int64_t v;
            if (JS_ToBigInt64(ctx, &v, argv[0])) {
                return JS_EXCEPTION;
            }
            if (is_le) {
                tjs__write_u64_le(p, (uint64_t) v);
            } else {
                tjs__write_u64_be(p, (uint64_t) v);
            }
            break;
        }
        case TJS__NUM_BIGU64: {
            uint64_t v;
            if (JS_ToBigUint64(ctx, &v, argv[0])) {
                return JS_EXCEPTION;
            }
            if (is_le) {
                tjs__write_u64_le(p, v);
            } else {
                tjs__write_u64_be(p, v);
            }
            break;
        }
    }

    return JS_NewInt32(ctx, (int32_t) (off + size));
}

static const JSCFunctionListEntry tjs_buffer_ctor_funcs[] = {
    TJS_CFUNC_DEF("alloc", 3, tjs_buffer_alloc),
    TJS_CFUNC_DEF("allocUnsafe", 1, tjs_buffer_alloc_unsafe),
    TJS_CFUNC_DEF("allocUnsafeSlow", 1, tjs_buffer_alloc_unsafe),
    TJS_CFUNC_DEF("byteLength", 2, tjs_buffer_byte_length),
    TJS_CFUNC_DEF("compare", 2, tjs_buffer_compare),
    TJS_CFUNC_DEF("concat", 2, tjs_buffer_concat),
    TJS_CFUNC_DEF("from", 3, tjs_buffer_from),
    TJS_CFUNC_DEF("isBuffer", 1, tjs_buffer_is_buffer),
    TJS_CFUNC_DEF("isEncoding", 1, tjs_buffer_is_encoding),
};

static const JSCFunctionListEntry tjs_buffer_proto_funcs[] = {
    TJS_CFUNC_DEF("compare", 5, tjs_buffer_proto_compare),
    TJS_CFUNC_DEF("copy", 4, tjs_buffer_copy),
    TJS_CFUNC_DEF("equals", 1, tjs_buffer_equals),
    TJS_CFUNC_DEF("fill", 4, tjs_buffer_fill),
    TJS_CFUNC_DEF("inspect", 0, tjs_buffer_inspect),
    TJS_CFUNC_DEF("subarray", 2, tjs_buffer_subarray),
    TJS_CFUNC_DEF("latin1Slice", 2, tjs_buffer_latin1_slice),
    TJS_CFUNC_DEF("utf8Slice", 2, tjs_buffer_utf8_slice),
    TJS_CFUNC_DEF("ucs2Slice", 2, tjs_buffer_ucs2_slice),
    TJS_CFUNC_DEF("base64Slice", 2, tjs_buffer_base64_slice),
    TJS_CFUNC_DEF("toJSON", 0, tjs_buffer_to_json),
    TJS_CFUNC_DEF("toString", 3, tjs_buffer_to_string),
    TJS_CFUNC_DEF("write", 4, tjs_buffer_write),
    TJS_CFUNC_MAGIC_DEF("swap16", 0, tjs_buffer_swap, 2),
    TJS_CFUNC_MAGIC_DEF("swap32", 0, tjs_buffer_swap, 4),
    TJS_CFUNC_MAGIC_DEF("swap64", 0, tjs_buffer_swap, 8),
    TJS_CFUNC_MAGIC_DEF("indexOf", 3, tjs_buffer_find_index, 0),
    TJS_CFUNC_MAGIC_DEF("lastIndexOf", 3, tjs_buffer_find_index, 1),
    TJS_CFUNC_MAGIC_DEF("includes", 3, tjs_buffer_find_index, 2),

    TJS_CFUNC_MAGIC_DEF("readInt8", 1, tjs_buffer_num, TJS__NUM_I8),
    TJS_CFUNC_MAGIC_DEF("readUInt8", 1, tjs_buffer_num, TJS__NUM_U8),
    TJS_CFUNC_MAGIC_DEF("readUint8", 1, tjs_buffer_num, TJS__NUM_U8),
    TJS_CFUNC_MAGIC_DEF("readInt16LE", 1, tjs_buffer_num, TJS__NUM_I16 | TJS__NUM_F_LE),
    TJS_CFUNC_MAGIC_DEF("readInt16BE", 1, tjs_buffer_num, TJS__NUM_I16),
    TJS_CFUNC_MAGIC_DEF("readUInt16LE", 1, tjs_buffer_num, TJS__NUM_U16 | TJS__NUM_F_LE),
    TJS_CFUNC_MAGIC_DEF("readUint16LE", 1, tjs_buffer_num, TJS__NUM_U16 | TJS__NUM_F_LE),
    TJS_CFUNC_MAGIC_DEF("readUInt16BE", 1, tjs_buffer_num, TJS__NUM_U16),
    TJS_CFUNC_MAGIC_DEF("readUint16BE", 1, tjs_buffer_num, TJS__NUM_U16),
    TJS_CFUNC_MAGIC_DEF("readInt32LE", 1, tjs_buffer_num, TJS__NUM_I32 | TJS__NUM_F_LE),
    TJS_CFUNC_MAGIC_DEF("readInt32BE", 1, tjs_buffer_num, TJS__NUM_I32),
    TJS_CFUNC_MAGIC_DEF("readUInt32LE", 1, tjs_buffer_num, TJS__NUM_U32 | TJS__NUM_F_LE),
    TJS_CFUNC_MAGIC_DEF("readUint32LE", 1, tjs_buffer_num, TJS__NUM_U32 | TJS__NUM_F_LE),
    TJS_CFUNC_MAGIC_DEF("readUInt32BE", 1, tjs_buffer_num, TJS__NUM_U32),
    TJS_CFUNC_MAGIC_DEF("readUint32BE", 1, tjs_buffer_num, TJS__NUM_U32),
    TJS_CFUNC_MAGIC_DEF("readFloatLE", 1, tjs_buffer_num, TJS__NUM_F32 | TJS__NUM_F_LE),
    TJS_CFUNC_MAGIC_DEF("readFloatBE", 1, tjs_buffer_num, TJS__NUM_F32),
    TJS_CFUNC_MAGIC_DEF("readDoubleLE", 1, tjs_buffer_num, TJS__NUM_F64 | TJS__NUM_F_LE),
    TJS_CFUNC_MAGIC_DEF("readDoubleBE", 1, tjs_buffer_num, TJS__NUM_F64),
    TJS_CFUNC_MAGIC_DEF("readBigInt64LE", 1, tjs_buffer_num, TJS__NUM_BIGI64 | TJS__NUM_F_LE),
    TJS_CFUNC_MAGIC_DEF("readBigInt64BE", 1, tjs_buffer_num, TJS__NUM_BIGI64),
    TJS_CFUNC_MAGIC_DEF("readBigUInt64LE", 1, tjs_buffer_num, TJS__NUM_BIGU64 | TJS__NUM_F_LE),
    TJS_CFUNC_MAGIC_DEF("readBigUint64LE", 1, tjs_buffer_num, TJS__NUM_BIGU64 | TJS__NUM_F_LE),
    TJS_CFUNC_MAGIC_DEF("readBigUInt64BE", 1, tjs_buffer_num, TJS__NUM_BIGU64),
    TJS_CFUNC_MAGIC_DEF("readBigUint64BE", 1, tjs_buffer_num, TJS__NUM_BIGU64),

    TJS_CFUNC_MAGIC_DEF("writeInt8", 2, tjs_buffer_num, TJS__NUM_I8 | TJS__NUM_F_WRITE),
    TJS_CFUNC_MAGIC_DEF("writeUInt8", 2, tjs_buffer_num, TJS__NUM_U8 | TJS__NUM_F_WRITE),
    TJS_CFUNC_MAGIC_DEF("writeUint8", 2, tjs_buffer_num, TJS__NUM_U8 | TJS__NUM_F_WRITE),
    TJS_CFUNC_MAGIC_DEF("writeInt16LE", 2, tjs_buffer_num, TJS__NUM_I16 | TJS__NUM_F_LE | TJS__NUM_F_WRITE),
    TJS_CFUNC_MAGIC_DEF("writeInt16BE", 2, tjs_buffer_num, TJS__NUM_I16 | TJS__NUM_F_WRITE),
    TJS_CFUNC_MAGIC_DEF("writeUInt16LE", 2, tjs_buffer_num, TJS__NUM_U16 | TJS__NUM_F_LE | TJS__NUM_F_WRITE),
    TJS_CFUNC_MAGIC_DEF("writeUint16LE", 2, tjs_buffer_num, TJS__NUM_U16 | TJS__NUM_F_LE | TJS__NUM_F_WRITE),
    TJS_CFUNC_MAGIC_DEF("writeUInt16BE", 2, tjs_buffer_num, TJS__NUM_U16 | TJS__NUM_F_WRITE),
    TJS_CFUNC_MAGIC_DEF("writeUint16BE", 2, tjs_buffer_num, TJS__NUM_U16 | TJS__NUM_F_WRITE),
    TJS_CFUNC_MAGIC_DEF("writeInt32LE", 2, tjs_buffer_num, TJS__NUM_I32 | TJS__NUM_F_LE | TJS__NUM_F_WRITE),
    TJS_CFUNC_MAGIC_DEF("writeInt32BE", 2, tjs_buffer_num, TJS__NUM_I32 | TJS__NUM_F_WRITE),
    TJS_CFUNC_MAGIC_DEF("writeUInt32LE", 2, tjs_buffer_num, TJS__NUM_U32 | TJS__NUM_F_LE | TJS__NUM_F_WRITE),
    TJS_CFUNC_MAGIC_DEF("writeUint32LE", 2, tjs_buffer_num, TJS__NUM_U32 | TJS__NUM_F_LE | TJS__NUM_F_WRITE),
    TJS_CFUNC_MAGIC_DEF("writeUInt32BE", 2, tjs_buffer_num, TJS__NUM_U32 | TJS__NUM_F_WRITE),
    TJS_CFUNC_MAGIC_DEF("writeUint32BE", 2, tjs_buffer_num, TJS__NUM_U32 | TJS__NUM_F_WRITE),
    TJS_CFUNC_MAGIC_DEF("writeFloatLE", 2, tjs_buffer_num, TJS__NUM_F32 | TJS__NUM_F_LE | TJS__NUM_F_WRITE),
    TJS_CFUNC_MAGIC_DEF("writeFloatBE", 2, tjs_buffer_num, TJS__NUM_F32 | TJS__NUM_F_WRITE),
    TJS_CFUNC_MAGIC_DEF("writeDoubleLE", 2, tjs_buffer_num, TJS__NUM_F64 | TJS__NUM_F_LE | TJS__NUM_F_WRITE),
    TJS_CFUNC_MAGIC_DEF("writeDoubleBE", 2, tjs_buffer_num, TJS__NUM_F64 | TJS__NUM_F_WRITE),
    TJS_CFUNC_MAGIC_DEF("writeBigInt64LE", 2, tjs_buffer_num, TJS__NUM_BIGI64 | TJS__NUM_F_LE | TJS__NUM_F_WRITE),
    TJS_CFUNC_MAGIC_DEF("writeBigInt64BE", 2, tjs_buffer_num, TJS__NUM_BIGI64 | TJS__NUM_F_WRITE),
    TJS_CFUNC_MAGIC_DEF("writeBigUInt64LE", 2, tjs_buffer_num, TJS__NUM_BIGU64 | TJS__NUM_F_LE | TJS__NUM_F_WRITE),
    TJS_CFUNC_MAGIC_DEF("writeBigUint64LE", 2, tjs_buffer_num, TJS__NUM_BIGU64 | TJS__NUM_F_LE | TJS__NUM_F_WRITE),
    TJS_CFUNC_MAGIC_DEF("writeBigUInt64BE", 2, tjs_buffer_num, TJS__NUM_BIGU64 | TJS__NUM_F_WRITE),
    TJS_CFUNC_MAGIC_DEF("writeBigUint64BE", 2, tjs_buffer_num, TJS__NUM_BIGU64 | TJS__NUM_F_WRITE),
};

void tjs__mod_buffer_init(JSContext *ctx, JSValue ns) {
    static const char src[] = "(class Buffer extends Uint8Array {})";

    JSValue ctor = JS_Eval(ctx, src, sizeof(src) - 1, TJS__BUFFER_EVAL_NAME, JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(ctor)) {
        return;
    }

    JS_SetPropertyFunctionList(ctx, ctor, tjs_buffer_ctor_funcs, countof(tjs_buffer_ctor_funcs));

    JSValue proto = JS_GetPropertyStr(ctx, ctor, "prototype");
    JS_SetPropertyFunctionList(ctx, proto, tjs_buffer_proto_funcs, countof(tjs_buffer_proto_funcs));
    JS_FreeValue(ctx, proto);

    JSValue constants = JS_NewObject(ctx);
    JS_DefinePropertyValueStr(ctx, constants, "MAX_LENGTH", JS_NewUint32(ctx, TJS__BUFFER_MAX_LENGTH), JS_PROP_C_W_E);
    JS_DefinePropertyValueStr(ctx,
                              constants,
                              "MAX_STRING_LENGTH",
                              JS_NewInt32(ctx, TJS__BUFFER_MAX_STR_LENGTH),
                              JS_PROP_C_W_E);
    JS_DefinePropertyValueStr(ctx, ctor, "constants", constants, JS_PROP_C_W_E);

    JS_DefinePropertyValueStr(ctx, ctor, "poolSize", JS_NewInt32(ctx, 8192), JS_PROP_C_W_E);

    JSValue global_obj = JS_GetGlobalObject(ctx);
    JS_DefinePropertyValueStr(ctx, global_obj, "Buffer", JS_DupValue(ctx, ctor), JS_PROP_C_W_E);
    JS_FreeValue(ctx, global_obj);

    JS_DefinePropertyValueStr(ctx, ns, "Buffer", ctor, JS_PROP_C_W_E);
}
