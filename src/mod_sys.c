/*
 * txiki.js
 *
 * Copyright (c) 2022-present Saúl Ibarra Corretgé <s@saghul.net>
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
#include "version.h"

#include <miniz.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <uv.h>

extern const uint8_t tjs__run_repl[];
extern const uint32_t tjs__run_repl_size;

#define TJS__PATHSEP_POSIX '/'
#if defined(_WIN32)
#define TJS__PATHSEP '\\'
#else
#define TJS__PATHSEP '/'
#endif

typedef enum {
    GZIP_HDR_BASE = 0,
    GZIP_HDR_EXTRA_LEN,
    GZIP_HDR_EXTRA,
    GZIP_HDR_NAME,
    GZIP_HDR_COMMENT,
    GZIP_HDR_HCRC,
    GZIP_HDR_DONE,
} TJSGzipHeaderState;

typedef struct {
    TJSGzipHeaderState header_state;
    uint8_t header[10];
    size_t header_len;
    uint8_t flg;
    uint8_t extra_len_buf[2];
    size_t extra_len_pos;
    size_t extra_len_rem;
    int hcrc_rem;
    bool deflate_done;
    bool footer_done;
    uint8_t footer[8];
    size_t footer_len;
    uint32_t crc32;
    uint32_t isize;
    mz_stream stream;
    bool stream_inited;
} TJSGunzipStream;

static JSClassID tjs_gunzip_stream_class_id;


static bool tjs_path_dirname_inplace(char *path) {
    size_t len = strlen(path);
    if (len == 0) {
        return false;
    }
    char *p = path + len;
    while (p > path && (p[-1] == TJS__PATHSEP || p[-1] == TJS__PATHSEP_POSIX)) {
        p--;
    }
    while (p > path && p[-1] != TJS__PATHSEP && p[-1] != TJS__PATHSEP_POSIX) {
        p--;
    }
    if (p == path) {
        path[0] = '.';
        path[1] = '\0';
        return true;
    }
    if (p == path + 1 && (path[0] == TJS__PATHSEP || path[0] == TJS__PATHSEP_POSIX)) {
        path[1] = '\0';
        return true;
    }
    p--;
    while (p > path && (p[0] == TJS__PATHSEP || p[0] == TJS__PATHSEP_POSIX)) {
        p--;
    }
    p[1] = '\0';
    return true;
}

static JSValue tjs_set_entrypoint_globals(JSContext *ctx, const char *filename) {
    JSValue global_obj = JS_GetGlobalObject(ctx);
    if (JS_IsException(global_obj)) {
        return JS_EXCEPTION;
    }

    uv_fs_t req;
    memset(&req, 0, sizeof(req));
    int r = uv_fs_realpath(NULL, &req, filename, NULL);
    const char *resolved = r == 0 && req.ptr ? req.ptr : filename;
    int ret;

    ret = JS_DefinePropertyValueStr(ctx, global_obj, "__filename", JS_NewString(ctx, resolved), JS_PROP_C_W_E);
    if (ret < 0) {
        uv_fs_req_cleanup(&req);
        JS_FreeValue(ctx, global_obj);
        return JS_EXCEPTION;
    }

    char *dirname = js_strdup(ctx, resolved);
    if (!dirname) {
        uv_fs_req_cleanup(&req);
        JS_FreeValue(ctx, global_obj);
        return JS_EXCEPTION;
    }
    tjs_path_dirname_inplace(dirname);
    ret = JS_DefinePropertyValueStr(ctx, global_obj, "__dirname", JS_NewString(ctx, dirname), JS_PROP_C_W_E);
    js_free(ctx, dirname);
    if (ret < 0) {
        uv_fs_req_cleanup(&req);
        JS_FreeValue(ctx, global_obj);
        return JS_EXCEPTION;
    }

    uv_fs_req_cleanup(&req);
    JS_FreeValue(ctx, global_obj);
    return JS_UNDEFINED;
}

static JSValue tjs_evalFile(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    const char *filename;
    size_t len;
    JSValue ret;
    filename = JS_ToCStringLen(ctx, &len, argv[0]);
    if (!filename) {
        return JS_EXCEPTION;
    }
    ret = tjs_set_entrypoint_globals(ctx, filename);
    if (JS_IsException(ret)) {
        JS_FreeCString(ctx, filename);
        return ret;
    }
    JS_FreeValue(ctx, ret);
    ret = TJS_EvalModule(ctx, filename, true);
    JS_FreeCString(ctx, filename);
    return ret;
}

static JSValue tjs_loadScript(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    const char *filename;
    size_t len;
    JSValue ret;
    filename = JS_ToCStringLen(ctx, &len, argv[0]);
    if (!filename) {
        return JS_EXCEPTION;
    }
    ret = TJS_EvalScript(ctx, filename);
    JS_FreeCString(ctx, filename);
    return ret;
}

static JSValue tjs_evalScript(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    const char *str;
    size_t len;
    JSValue ret;
    str = JS_ToCStringLen(ctx, &len, argv[0]);
    if (!str) {
        return JS_EXCEPTION;
    }
    ret = JS_Eval(ctx, str, len, "<evalScript>", JS_EVAL_TYPE_GLOBAL | JS_EVAL_FLAG_ASYNC);
    JS_FreeCString(ctx, str);
    return ret;
}

static JSValue tjs_print(JSContext *ctx, JSValue this_val, int argc, JSValue *argv, int magic) {
    const char *str;
    size_t len;
    str = JS_ToCStringLen(ctx, &len, argv[0]);
    if (!str) {
        return JS_EXCEPTION;
    }
    int fd = magic == 0 ? STDOUT_FILENO : STDERR_FILENO;
    tjs_write_fd(fd, str, len);
    JS_FreeCString(ctx, str);
    return JS_UNDEFINED;
}

static JSValue tjs_runRepl(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    tjs__eval_bytecode(ctx, tjs__run_repl, tjs__run_repl_size, false);

    return JS_UNDEFINED;
}

static JSValue tjs_isArrayBuffer(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    return JS_NewBool(ctx, JS_IsArrayBuffer(argv[0]));
}

static JSValue tjs_detachArrayBuffer(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    JS_DetachArrayBuffer(ctx, argv[0]);

    return JS_UNDEFINED;
}

static uint16_t tjs__read_u16_le(const uint8_t *p) {
    return (uint16_t) p[0] | ((uint16_t) p[1] << 8);
}

static uint32_t tjs__read_u32_le(const uint8_t *p) {
    return (uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) | ((uint32_t) p[3] << 24);
}

static void tjs_gunzip_stream_finalizer(JSRuntime *rt, JSValue val) {
    TJSGunzipStream *s = JS_GetOpaque(val, tjs_gunzip_stream_class_id);
    if (s) {
        if (s->stream_inited) {
            mz_inflateEnd(&s->stream);
            s->stream_inited = false;
        }
        js_free_rt(rt, s);
    }
}

static JSClassDef tjs_gunzip_stream_class = {
    "GunzipStream",
    .finalizer = tjs_gunzip_stream_finalizer,
};

static TJSGunzipStream *tjs_gunzip_stream_get(JSContext *ctx, JSValueConst obj) {
    return JS_GetOpaque2(ctx, obj, tjs_gunzip_stream_class_id);
}

static void tjs__gzip_header_advance(TJSGunzipStream *s) {
    if (s->flg & 0x04) {
        s->header_state = GZIP_HDR_EXTRA_LEN;
        return;
    }
    if (s->flg & 0x08) {
        s->header_state = GZIP_HDR_NAME;
        return;
    }
    if (s->flg & 0x10) {
        s->header_state = GZIP_HDR_COMMENT;
        return;
    }
    if (s->flg & 0x02) {
        s->header_state = GZIP_HDR_HCRC;
        s->hcrc_rem = 2;
        return;
    }
    s->header_state = GZIP_HDR_DONE;
}

static int tjs__gzip_consume_header(TJSGunzipStream *s,
                                    const uint8_t *input,
                                    size_t input_len,
                                    size_t *consumed,
                                    const char **err) {
    size_t i = 0;

    while (i < input_len && s->header_state != GZIP_HDR_DONE) {
        switch (s->header_state) {
            case GZIP_HDR_BASE: {
                size_t need = 10 - s->header_len;
                size_t avail = input_len - i;
                size_t n = need < avail ? need : avail;
                memcpy(s->header + s->header_len, input + i, n);
                s->header_len += n;
                i += n;
                if (s->header_len < 10) {
                    break;
                }
                if (s->header[0] != 0x1f || s->header[1] != 0x8b || s->header[2] != 8) {
                    *err = "invalid gzip header";
                    return -1;
                }
                s->flg = s->header[3];
                if (s->flg & 0xE0) {
                    *err = "invalid gzip flags";
                    return -1;
                }
                tjs__gzip_header_advance(s);
                break;
            }
            case GZIP_HDR_EXTRA_LEN: {
                while (i < input_len && s->extra_len_pos < 2) {
                    s->extra_len_buf[s->extra_len_pos++] = input[i++];
                }
                if (s->extra_len_pos < 2) {
                    break;
                }
                s->extra_len_rem = tjs__read_u16_le(s->extra_len_buf);
                s->extra_len_pos = 0;
                if (s->extra_len_rem == 0) {
                    s->flg &= (uint8_t) ~0x04;
                    tjs__gzip_header_advance(s);
                } else {
                    s->header_state = GZIP_HDR_EXTRA;
                }
                break;
            }
            case GZIP_HDR_EXTRA: {
                size_t avail = input_len - i;
                size_t n = s->extra_len_rem < avail ? s->extra_len_rem : avail;
                i += n;
                s->extra_len_rem -= n;
                if (s->extra_len_rem == 0) {
                    s->flg &= (uint8_t) ~0x04;
                    tjs__gzip_header_advance(s);
                }
                break;
            }
            case GZIP_HDR_NAME:
            case GZIP_HDR_COMMENT: {
                while (i < input_len && input[i] != 0) {
                    i++;
                }
                if (i >= input_len) {
                    break;
                }
                i++;
                if (s->header_state == GZIP_HDR_NAME) {
                    s->flg &= (uint8_t) ~0x08;
                } else {
                    s->flg &= (uint8_t) ~0x10;
                }
                tjs__gzip_header_advance(s);
                break;
            }
            case GZIP_HDR_HCRC: {
                size_t avail = input_len - i;
                size_t n = (size_t) s->hcrc_rem < avail ? (size_t) s->hcrc_rem : avail;
                i += n;
                s->hcrc_rem -= (int) n;
                if (s->hcrc_rem == 0) {
                    s->flg &= (uint8_t) ~0x02;
                    tjs__gzip_header_advance(s);
                }
                break;
            }
            case GZIP_HDR_DONE:
            default:
                break;
        }
    }

    *consumed = i;
    return 0;
}

static int tjs__gunzip_inflate(TJSGunzipStream *s,
                               const uint8_t *input,
                               size_t input_len,
                               size_t *consumed,
                               bool no_more_input,
                               DynBuf *out,
                               const char **err) {
    mz_stream *stream = &s->stream;
    uint8_t out_buf[32 * 1024];
    static const uint8_t empty_input[1] = { 0 };
    stream->next_in = (const unsigned char *) (input ? input : empty_input);
    stream->avail_in = (mz_uint) input_len;
    *consumed = 0;

    for (;;) {
        stream->next_out = out_buf;
        stream->avail_out = (mz_uint) sizeof(out_buf);
        int flush = no_more_input ? MZ_FINISH : MZ_NO_FLUSH;
        int st = mz_inflate(stream, flush);

        size_t out_size = sizeof(out_buf) - stream->avail_out;
        *consumed = input_len - stream->avail_in;

        if (out_size > 0) {
            if (dbuf_put(out, out_buf, out_size) != 0) {
                *err = "out of memory";
                return -1;
            }
            s->crc32 = mz_crc32(s->crc32, out_buf, out_size);
            s->isize += (uint32_t) out_size;
        }

        if (st == MZ_STREAM_END) {
            s->deflate_done = true;
            break;
        }
        if (st == MZ_OK) {
            if (stream->avail_in == 0 && out_size == 0) {
                break;
            }
            continue;
        }
        if (st == MZ_BUF_ERROR) {
            if (no_more_input && stream->avail_in == 0) {
                *err = "unexpected end of gzip stream";
                return -1;
            }
            break;
        }
        *err = "invalid gzip deflate stream";
        return -1;
    }
    return 0;
}

static int tjs__gunzip_consume_footer(TJSGunzipStream *s,
                                      const uint8_t *input,
                                      size_t input_len,
                                      size_t *consumed,
                                      const char **err) {
    if (!s->footer_done && s->footer_len < 8) {
        size_t need = 8 - s->footer_len;
        size_t n = need < input_len ? need : input_len;
        memcpy(s->footer + s->footer_len, input, n);
        s->footer_len += n;
    }

    if (!s->footer_done && s->footer_len == 8) {
        uint32_t expected_crc32 = tjs__read_u32_le(s->footer);
        uint32_t expected_isize = tjs__read_u32_le(s->footer + 4);
        if (expected_crc32 != s->crc32 || expected_isize != s->isize) {
            *err = "gzip checksum mismatch";
            return -1;
        }
        s->footer_done = true;
    }

    *consumed = input_len;
    return 0;
}

static JSValue tjs_gunzip_stream_create(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    JSValue obj = JS_NewObjectClass(ctx, tjs_gunzip_stream_class_id);
    if (JS_IsException(obj)) {
        return obj;
    }

    TJSGunzipStream *s = js_mallocz(ctx, sizeof(*s));
    if (!s) {
        JS_FreeValue(ctx, obj);
        return JS_EXCEPTION;
    }

    s->header_state = GZIP_HDR_BASE;
    s->crc32 = mz_crc32(0, NULL, 0);
    JS_SetOpaque(obj, s);
    return obj;
}

static JSValue tjs_gunzip_stream_push(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    TJSGunzipStream *s = tjs_gunzip_stream_get(ctx, argv[0]);
    if (!s) {
        return JS_EXCEPTION;
    }
    if (s->footer_done) {
        static const uint8_t empty[1] = { 0 };
        return JS_NewUint8ArrayCopy(ctx, empty, 0);
    }

    size_t input_len = 0;
    const uint8_t *input = JS_GetUint8Array(ctx, &input_len, argv[1]);
    TJS_CHECK_ARG_RET(ctx, input != NULL, 1, "Uint8Array");

    DynBuf out;
    tjs_dbuf_init(ctx, &out);
    const char *err = NULL;
    size_t in_pos = 0;

    if (s->header_state != GZIP_HDR_DONE) {
        size_t consumed = 0;
        if (tjs__gzip_consume_header(s, input + in_pos, input_len - in_pos, &consumed, &err) < 0) {
            dbuf_free(&out);
            return JS_ThrowTypeError(ctx, "%s", err);
        }
        in_pos += consumed;
    }

    if (s->header_state == GZIP_HDR_DONE && !s->deflate_done && in_pos < input_len) {
        if (!s->stream_inited) {
            memset(&s->stream, 0, sizeof(s->stream));
            if (mz_inflateInit2(&s->stream, -MZ_DEFAULT_WINDOW_BITS) != MZ_OK) {
                dbuf_free(&out);
                return JS_ThrowTypeError(ctx, "failed to initialize gunzip stream");
            }
            s->stream_inited = true;
        }
        size_t consumed = 0;
        if (tjs__gunzip_inflate(s, input + in_pos, input_len - in_pos, &consumed, false, &out, &err) < 0) {
            dbuf_free(&out);
            return JS_ThrowTypeError(ctx, "%s", err);
        }
        in_pos += consumed;
    }

    if (s->deflate_done && in_pos < input_len) {
        size_t consumed = 0;
        if (tjs__gunzip_consume_footer(s, input + in_pos, input_len - in_pos, &consumed, &err) < 0) {
            dbuf_free(&out);
            return JS_ThrowTypeError(ctx, "%s", err);
        }
        in_pos += consumed;
        if (s->footer_done && s->stream_inited) {
            mz_inflateEnd(&s->stream);
            s->stream_inited = false;
        }
    }

    if (out.size == 0) {
        static const uint8_t empty[1] = { 0 };
        dbuf_free(&out);
        return JS_NewUint8ArrayCopy(ctx, empty, 0);
    }
    JSValue ret = TJS_NewUint8Array(ctx, out.buf, out.size);
    if (JS_IsException(ret)) {
        dbuf_free(&out);
    }
    return ret;
}

static JSValue tjs_gunzip_stream_finish(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    TJSGunzipStream *s = tjs_gunzip_stream_get(ctx, argv[0]);
    if (!s) {
        return JS_EXCEPTION;
    }
    if (s->footer_done) {
        if (s->stream_inited) {
            mz_inflateEnd(&s->stream);
            s->stream_inited = false;
        }
        static const uint8_t empty[1] = { 0 };
        return JS_NewUint8ArrayCopy(ctx, empty, 0);
    }

    DynBuf out;
    tjs_dbuf_init(ctx, &out);
    const char *err = NULL;

    if (s->header_state != GZIP_HDR_DONE) {
        dbuf_free(&out);
        return JS_ThrowTypeError(ctx, "unexpected end of gzip header");
    }

    if (!s->deflate_done) {
        if (!s->stream_inited) {
            memset(&s->stream, 0, sizeof(s->stream));
            if (mz_inflateInit2(&s->stream, -MZ_DEFAULT_WINDOW_BITS) != MZ_OK) {
                dbuf_free(&out);
                return JS_ThrowTypeError(ctx, "failed to initialize gunzip stream");
            }
            s->stream_inited = true;
        }
        size_t consumed = 0;
        if (tjs__gunzip_inflate(s, NULL, 0, &consumed, true, &out, &err) < 0) {
            dbuf_free(&out);
            return JS_ThrowTypeError(ctx, "%s", err);
        }
        if (!s->deflate_done) {
            dbuf_free(&out);
            return JS_ThrowTypeError(ctx, "unexpected end of gzip stream");
        }
    }

    if (!s->footer_done) {
        dbuf_free(&out);
        return JS_ThrowTypeError(ctx, "unexpected end of gzip footer");
    }

    if (s->stream_inited) {
        mz_inflateEnd(&s->stream);
        s->stream_inited = false;
    }

    if (out.size == 0) {
        static const uint8_t empty[1] = { 0 };
        dbuf_free(&out);
        return JS_NewUint8ArrayCopy(ctx, empty, 0);
    }
    JSValue ret = TJS_NewUint8Array(ctx, out.buf, out.size);
    if (JS_IsException(ret)) {
        dbuf_free(&out);
    }
    return ret;
}

static int tjs__gunzip_write_cb(const void *buf, int len, void *user) {
    DynBuf *dbuf = user;
    return dbuf_put(dbuf, buf, (size_t) len) == 0;
}

static JSValue tjs_gunzipSync(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    size_t input_len = 0;
    const uint8_t *input = JS_GetUint8Array(ctx, &input_len, argv[0]);
    TJS_CHECK_ARG_RET(ctx, input != NULL, 0, "Uint8Array");

    if (input_len < 18) {
        return JS_ThrowTypeError(ctx, "invalid gzip data");
    }
    if (input[0] != 0x1f || input[1] != 0x8b || input[2] != 8) {
        return JS_ThrowTypeError(ctx, "invalid gzip header");
    }

    uint8_t flg = input[3];
    if (flg & 0xE0) {
        return JS_ThrowTypeError(ctx, "invalid gzip flags");
    }

    size_t pos = 10;
    if (flg & 0x04) {  // FEXTRA
        if (pos + 2 > input_len) {
            return JS_ThrowTypeError(ctx, "invalid gzip extra field");
        }
        size_t xlen = tjs__read_u16_le(input + pos);
        pos += 2;
        if (pos + xlen > input_len) {
            return JS_ThrowTypeError(ctx, "invalid gzip extra field");
        }
        pos += xlen;
    }
    if (flg & 0x08) {  // FNAME
        while (pos < input_len && input[pos] != 0) {
            pos++;
        }
        if (pos >= input_len) {
            return JS_ThrowTypeError(ctx, "invalid gzip file name");
        }
        pos++;
    }
    if (flg & 0x10) {  // FCOMMENT
        while (pos < input_len && input[pos] != 0) {
            pos++;
        }
        if (pos >= input_len) {
            return JS_ThrowTypeError(ctx, "invalid gzip comment");
        }
        pos++;
    }
    if (flg & 0x02) {  // FHCRC
        if (pos + 2 > input_len) {
            return JS_ThrowTypeError(ctx, "invalid gzip header crc");
        }
        pos += 2;
    }

    if (pos + 8 > input_len) {
        return JS_ThrowTypeError(ctx, "invalid gzip payload");
    }

    const uint8_t *stream = input + pos;
    size_t stream_len = input_len - pos;
    size_t consumed = stream_len;

    DynBuf output;
    tjs_dbuf_init(ctx, &output);
    if (!tinfl_decompress_mem_to_callback(stream, &consumed, tjs__gunzip_write_cb, &output, 0)) {
        dbuf_free(&output);
        return JS_ThrowTypeError(ctx, "invalid gzip deflate stream");
    }

    if (consumed + 8 > stream_len) {
        dbuf_free(&output);
        return JS_ThrowTypeError(ctx, "invalid gzip footer");
    }

    const uint8_t *footer = stream + consumed;
    uint32_t expected_crc32 = tjs__read_u32_le(footer);
    uint32_t expected_isize = tjs__read_u32_le(footer + 4);
    uint32_t actual_crc32 = mz_crc32(0, output.buf, output.size);
    if (actual_crc32 != expected_crc32 || ((uint32_t) output.size) != expected_isize) {
        dbuf_free(&output);
        return JS_ThrowTypeError(ctx, "gzip checksum mismatch");
    }

    JSValue ret = JS_NewUint8ArrayCopy(ctx, output.buf, output.size);
    dbuf_free(&output);
    return ret;
}

static JSValue tjs_exepath(JSContext *ctx, JSValue this_val) {
    char buf[1024];
    size_t size = sizeof(buf);
    char *dbuf = buf;
    int r;

    r = uv_exepath(dbuf, &size);
    if (r != 0) {
        if (r != UV_ENOBUFS) {
            return tjs_throw_errno(ctx, r);
        }
        dbuf = js_malloc(ctx, size);
        if (!dbuf) {
            return JS_EXCEPTION;
        }
        r = uv_exepath(dbuf, &size);
        if (r != 0) {
            js_free(ctx, dbuf);
            return tjs_throw_errno(ctx, r);
        }
    }

    JSValue ret = JS_NewStringLen(ctx, dbuf, size);

    if (dbuf != buf) {
        js_free(ctx, dbuf);
    }

    return ret;
}

static JSValue tjs_randomUUID(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    char v[37];
    unsigned char u[16];

    int r = uv_random(NULL, NULL, u, sizeof(u), 0, NULL);
    if (r != 0) {
        return tjs_throw_errno(ctx, r);
    }

    u[6] &= 15;
    u[6] |= 64;  // '4x'

    u[8] &= 63;
    u[8] |= 128;  // 0b10xxxxxx

    snprintf(v,
             sizeof(v),
             "%02x%02x%02x%02x-%02x%02x-%02x%02x-"
             "%02x%02x-%02x%02x%02x%02x%02x%02x",
             u[0],
             u[1],
             u[2],
             u[3],
             u[4],
             u[5],
             u[6],
             u[7],
             u[8],
             u[9],
             u[10],
             u[11],
             u[12],
             u[13],
             u[14],
             u[15]);

    return JS_NewString(ctx, v);
}

/* clang-format off */
static const JSCFunctionListEntry tjs_sys_funcs[] = {
    TJS_CFUNC_DEF("evalFile", 1, tjs_evalFile),
    TJS_CFUNC_DEF("evalScript", 1, tjs_evalScript),
    TJS_CFUNC_DEF("loadScript", 1, tjs_loadScript),
    TJS_CFUNC_DEF("randomUUID", 0, tjs_randomUUID),
    TJS_CFUNC_DEF("runRepl", 0, tjs_runRepl),
    TJS_CFUNC_DEF("isArrayBuffer", 1, tjs_isArrayBuffer),
    TJS_CFUNC_DEF("detachArrayBuffer", 1, tjs_detachArrayBuffer),
    TJS_CFUNC_DEF("gunzipSync", 1, tjs_gunzipSync),
    TJS_CFUNC_DEF("gunzipStreamCreate", 0, tjs_gunzip_stream_create),
    TJS_CFUNC_DEF("gunzipStreamPush", 2, tjs_gunzip_stream_push),
    TJS_CFUNC_DEF("gunzipStreamFinish", 1, tjs_gunzip_stream_finish),
    TJS_CFUNC_MAGIC_DEF("stdoutPrint", 1, tjs_print, 0),
    TJS_CFUNC_MAGIC_DEF("stderrPrint", 1, tjs_print, 1),
    TJS_CGETSET_DEF("exePath", tjs_exepath, NULL),
};
/* clang-format on */

void tjs__mod_sys_init(JSContext *ctx, JSValue ns) {
    JSRuntime *rt = JS_GetRuntime(ctx);
    static bool tjs_gunzip_stream_class_inited = false;
    if (!tjs_gunzip_stream_class_inited) {
        JS_NewClassID(rt, &tjs_gunzip_stream_class_id);
        JS_NewClass(rt, tjs_gunzip_stream_class_id, &tjs_gunzip_stream_class);
        tjs_gunzip_stream_class_inited = true;
    }

    JS_SetPropertyFunctionList(ctx, ns, tjs_sys_funcs, countof(tjs_sys_funcs));
    JS_DefinePropertyValueStr(ctx, ns, "args", tjs__get_args(ctx), JS_PROP_C_W_E);
    JS_DefinePropertyValueStr(ctx, ns, "version", JS_NewString(ctx, tjs_version()), JS_PROP_C_W_E);
    JS_DefinePropertyValueStr(ctx, ns, "platform", JS_NewString(ctx, TJS__PLATFORM), JS_PROP_C_W_E);
}
