/*
 * xuan LVGL native module - dynamic loadable version
 *
 * This module can be loaded at runtime via:
 *   const lvgl = await import('lvgl');
 *
 * Build as: xuan-lvgl.so
 */

#include <quickjs.h>
#include <uv.h>

#include <lvgl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifndef countof
#define countof(x) (sizeof(x) / sizeof((x)[0]))
#endif

#ifdef TJS_LVGL_USE_SDL
/* Match lv_binding_js' simulator defaults so React layouts based on
 * Dimensions.window compute the same geometry in xuan. */
#define TJS_LVGL_SDL_HOR_RES 1024
#define TJS_LVGL_SDL_VER_RES 600
#endif

static char *tjs_lvgl_strdup(const char *src) {
    if (!src) {
        return NULL;
    }
    size_t len = strlen(src);
    char *out = (char *)malloc(len + 1);
    if (!out) {
        return NULL;
    }
    memcpy(out, src, len + 1);
    return out;
}

/* Display and input driver headers */
#ifdef TJS_LVGL_USE_SDL
#include <SDL2/SDL.h>
#else
#include <lv_drivers/display/fbdev.h>
#include <lv_drivers/indev/evdev.h>

#ifdef __linux__
#include <fcntl.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#endif
#endif

/* All buffers heap-allocated for PIC/shared-library safety */
#define DISP_BUF_SIZE (128 * 1024)  /* 128KB draw buffer */
static lv_color_t *draw_buf = NULL;
static lv_disp_draw_buf_t *disp_buf_ptr = NULL;

typedef struct {
    bool initialized;
    lv_disp_t *disp;
    lv_indev_t *indev;
    JSContext *ctx;
} TjsLVGLState;

static TjsLVGLState lvgl_state = {
    .initialized = false,
    .disp = NULL,
    .indev = NULL,
    .ctx = NULL,
};

static lv_obj_t *tjs_lvgl_get_obj(JSContext *ctx, JSValue val);

typedef struct TjsLVGLStyleState {
    lv_obj_t *obj;
    lv_coord_t *grid_cols;
    lv_coord_t *grid_rows;
    lv_img_dsc_t *img_dsc;
    uint8_t *img_data;
    lv_calendar_date_t *calendar_highlighted;
    char *bg_img_symbol;
    char *arc_img_symbol;
    char *dropdown_text;
    bool textarea_auto_keyboard;
    bool textarea_auto_keyboard_set;
#if LV_USE_CHART != 0
    lv_chart_series_t *chart_series[16];
#else
    void *chart_series[16];
#endif
    uint32_t chart_series_count;
    char **chart_axis_labels[5];
    uint32_t chart_axis_label_counts[5];
    struct TjsLVGLStyleState *next;
} TjsLVGLStyleState;

static TjsLVGLStyleState *tjs_lvgl_style_states = NULL;

static TjsLVGLStyleState *tjs_lvgl_style_state_get(lv_obj_t *obj, bool create) {
    TjsLVGLStyleState *cur = tjs_lvgl_style_states;
    while (cur) {
        if (cur->obj == obj) {
            return cur;
        }
        cur = cur->next;
    }
    if (!create) {
        return NULL;
    }
    TjsLVGLStyleState *state = (TjsLVGLStyleState *)calloc(1, sizeof(TjsLVGLStyleState));
    if (!state) {
        return NULL;
    }
    state->obj = obj;
    state->next = tjs_lvgl_style_states;
    tjs_lvgl_style_states = state;
    return state;
}

static void tjs_lvgl_style_state_drop(lv_obj_t *obj) {
    TjsLVGLStyleState **cur = &tjs_lvgl_style_states;
    while (*cur) {
        if ((*cur)->obj == obj) {
            TjsLVGLStyleState *victim = *cur;
            *cur = victim->next;
            free(victim->grid_cols);
            free(victim->grid_rows);
            free(victim->img_dsc);
            free(victim->img_data);
            free(victim->calendar_highlighted);
            free(victim->bg_img_symbol);
            free(victim->arc_img_symbol);
            free(victim->dropdown_text);
            for (uint32_t axis = 0; axis < countof(victim->chart_axis_labels); axis++) {
                if (victim->chart_axis_labels[axis]) {
                    for (uint32_t i = 0; i < victim->chart_axis_label_counts[axis]; i++) {
                        free(victim->chart_axis_labels[axis][i]);
                    }
                    free(victim->chart_axis_labels[axis]);
                }
                victim->chart_axis_labels[axis] = NULL;
                victim->chart_axis_label_counts[axis] = 0;
            }
            victim->chart_series_count = 0;
            free(victim);
            return;
        }
        cur = &(*cur)->next;
    }
}

static void tjs_lvgl_style_state_drop_tree(lv_obj_t *obj) {
    if (!obj) {
        return;
    }
    uint32_t child_cnt = lv_obj_get_child_cnt(obj);
    while (child_cnt > 0) {
        lv_obj_t *child = lv_obj_get_child(obj, (int32_t)(child_cnt - 1));
        tjs_lvgl_style_state_drop_tree(child);
        child_cnt--;
    }
    tjs_lvgl_style_state_drop(obj);
}

static bool tjs_lvgl_js_to_i32(JSContext *ctx, JSValueConst value, int32_t *out) {
    if (!out) {
        return false;
    }
    if (JS_IsNumber(value)) {
        return JS_ToInt32(ctx, out, value) >= 0;
    }
    if (JS_IsString(value)) {
        const char *str = JS_ToCString(ctx, value);
        if (!str) {
            return false;
        }
        char *end = NULL;
        long n = strtol(str, &end, 10);
        JS_FreeCString(ctx, str);
        if (end == str) {
            return false;
        }
        *out = (int32_t)n;
        return true;
    }
    return false;
}

static bool tjs_lvgl_js_to_color(JSContext *ctx, JSValueConst value, lv_color_t *out) {
    int32_t color = 0;
    if (!tjs_lvgl_js_to_i32(ctx, value, &color)) {
        return false;
    }
    if (out) {
        *out = lv_color_hex((uint32_t)color);
    }
    return true;
}

static bool tjs_lvgl_js_to_opa(JSContext *ctx, JSValueConst value, lv_opa_t *out) {
    int32_t v = 255;
    if (!tjs_lvgl_js_to_i32(ctx, value, &v)) {
        return false;
    }
    if (v < 0) v = 0;
    if (v > 255) v = 255;
    if (out) {
        *out = (lv_opa_t)v;
    }
    return true;
}

static uint8_t *tjs_lvgl_js_to_bytes_copy(JSContext *ctx, JSValueConst value, size_t *out_size) {
    if (out_size) {
        *out_size = 0;
    }
    size_t size = 0;
    uint8_t *data = JS_GetArrayBuffer(ctx, &size, value);
    if (!data) {
        JS_FreeValue(ctx, JS_GetException(ctx));
        size_t offset = 0;
        size_t view_size = 0;
        JSValue buffer = JS_GetTypedArrayBuffer(ctx, value, &offset, &view_size, NULL);
        if (JS_IsException(buffer)) {
            return NULL;
        }
        data = JS_GetArrayBuffer(ctx, &size, buffer);
        if (data) {
            data += offset;
            size = view_size;
        }
        JS_FreeValue(ctx, buffer);
    }
    if (!data || size == 0) {
        return NULL;
    }
    uint8_t *copy = (uint8_t *)malloc(size);
    if (!copy) {
        return NULL;
    }
    memcpy(copy, data, size);
    if (out_size) {
        *out_size = size;
    }
    return copy;
}

static bool tjs_lvgl_png_dimensions(const uint8_t *data, size_t size, uint32_t *width, uint32_t *height) {
    static const uint8_t png_sig[8] = { 0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a };
    if (!data || size < 24 || memcmp(data, png_sig, sizeof(png_sig)) != 0) {
        return false;
    }
    uint32_t w = ((uint32_t)data[16] << 24) | ((uint32_t)data[17] << 16) | ((uint32_t)data[18] << 8) | (uint32_t)data[19];
    uint32_t h = ((uint32_t)data[20] << 24) | ((uint32_t)data[21] << 16) | ((uint32_t)data[22] << 8) | (uint32_t)data[23];
    if (w == 0 || h == 0 || w > 2047 || h > 2047) {
        return false;
    }
    if (width) {
        *width = w;
    }
    if (height) {
        *height = h;
    }
    return true;
}

static bool tjs_lvgl_js_to_border_side(JSContext *ctx, JSValueConst value, lv_border_side_t *out) {
    if (!out) {
        return false;
    }
    if (JS_IsNumber(value)) {
        int32_t v = 0;
        if (JS_ToInt32(ctx, &v, value) < 0) {
            return false;
        }
        *out = (lv_border_side_t)v;
        return true;
    }
    if (!JS_IsString(value)) {
        return false;
    }
    const char *str = JS_ToCString(ctx, value);
    if (!str) {
        return false;
    }
    lv_border_side_t side = LV_BORDER_SIDE_NONE;
    if (strcmp(str, "none") == 0) {
        side = LV_BORDER_SIDE_NONE;
    } else if (strcmp(str, "full") == 0) {
        side = LV_BORDER_SIDE_FULL;
    } else {
        if (strstr(str, "left")) side |= LV_BORDER_SIDE_LEFT;
        if (strstr(str, "right")) side |= LV_BORDER_SIDE_RIGHT;
        if (strstr(str, "top")) side |= LV_BORDER_SIDE_TOP;
        if (strstr(str, "bottom")) side |= LV_BORDER_SIDE_BOTTOM;
        if (strstr(str, "internal")) side |= LV_BORDER_SIDE_INTERNAL;
    }
    JS_FreeCString(ctx, str);
    *out = side;
    return true;
}

static bool tjs_lvgl_js_to_overflow_mode(JSContext *ctx, JSValueConst value, bool *scrollable) {
    if (!scrollable) {
        return false;
    }
    int32_t v = 0;
    if (tjs_lvgl_js_to_i32(ctx, value, &v)) {
        /* keep compatibility with upstream enum mapping:
         * 0 = scroll/auto, 1 = hidden */
        *scrollable = (v != 1);
        return true;
    }
    if (JS_IsString(value)) {
        const char *str = JS_ToCString(ctx, value);
        if (!str) {
            return false;
        }
        if (strcmp(str, "hidden") == 0) {
            *scrollable = false;
        } else if (strcmp(str, "scroll") == 0 || strcmp(str, "auto") == 0 || strcmp(str, "visible") == 0) {
            *scrollable = true;
        } else {
            JS_FreeCString(ctx, str);
            return false;
        }
        JS_FreeCString(ctx, str);
        return true;
    }
    return false;
}

static const lv_font_t *tjs_lvgl_font_by_size(int size) {
    const lv_font_t *best_font = LV_FONT_DEFAULT;
    int best_distance = 1 << 30;
#define TJS_LVGL_FONT_CASE(px) \
    do { \
        const int dist = abs(size - (px)); \
        if (dist < best_distance) { \
            best_distance = dist; \
            best_font = &lv_font_montserrat_##px; \
        } \
    } while (0)
#if LV_FONT_MONTSERRAT_8
    TJS_LVGL_FONT_CASE(8);
#endif
#if LV_FONT_MONTSERRAT_10
    TJS_LVGL_FONT_CASE(10);
#endif
#if LV_FONT_MONTSERRAT_12
    TJS_LVGL_FONT_CASE(12);
#endif
#if LV_FONT_MONTSERRAT_14
    TJS_LVGL_FONT_CASE(14);
#endif
#if LV_FONT_MONTSERRAT_16
    TJS_LVGL_FONT_CASE(16);
#endif
#if LV_FONT_MONTSERRAT_18
    TJS_LVGL_FONT_CASE(18);
#endif
#if LV_FONT_MONTSERRAT_20
    TJS_LVGL_FONT_CASE(20);
#endif
#if LV_FONT_MONTSERRAT_22
    TJS_LVGL_FONT_CASE(22);
#endif
#if LV_FONT_MONTSERRAT_24
    TJS_LVGL_FONT_CASE(24);
#endif
#if LV_FONT_MONTSERRAT_26
    TJS_LVGL_FONT_CASE(26);
#endif
#if LV_FONT_MONTSERRAT_28
    TJS_LVGL_FONT_CASE(28);
#endif
#if LV_FONT_MONTSERRAT_30
    TJS_LVGL_FONT_CASE(30);
#endif
#if LV_FONT_MONTSERRAT_32
    TJS_LVGL_FONT_CASE(32);
#endif
#if LV_FONT_MONTSERRAT_34
    TJS_LVGL_FONT_CASE(34);
#endif
#if LV_FONT_MONTSERRAT_36
    TJS_LVGL_FONT_CASE(36);
#endif
#if LV_FONT_MONTSERRAT_38
    TJS_LVGL_FONT_CASE(38);
#endif
#if LV_FONT_MONTSERRAT_40
    TJS_LVGL_FONT_CASE(40);
#endif
#if LV_FONT_MONTSERRAT_42
    TJS_LVGL_FONT_CASE(42);
#endif
#if LV_FONT_MONTSERRAT_44
    TJS_LVGL_FONT_CASE(44);
#endif
#if LV_FONT_MONTSERRAT_46
    TJS_LVGL_FONT_CASE(46);
#endif
#if LV_FONT_MONTSERRAT_48
    TJS_LVGL_FONT_CASE(48);
#endif
#undef TJS_LVGL_FONT_CASE
    return best_font;
}

static const lv_font_t *tjs_lvgl_font_by_index(int index) {
    static const int full_sizes[] = {
        8, 10, 12, 14, 16, 18, 20, 22, 24, 26, 28,
        30, 32, 34, 36, 38, 40, 42, 44, 46, 48
    };
    if (index < 0) {
        index = 0;
    }
    if (index >= (int)countof(full_sizes)) {
        index = (int)countof(full_sizes) - 1;
    }
    return tjs_lvgl_font_by_size(full_sizes[index]);
}

static bool tjs_lvgl_copy_coord_array(JSContext *ctx, JSValueConst arr, lv_coord_t **out_ptr) {
    if (!JS_IsArray(arr) || !out_ptr) {
        return false;
    }
    JSValue len_val = JS_GetPropertyStr(ctx, arr, "length");
    int32_t len = 0;
    bool ok = JS_ToInt32(ctx, &len, len_val) >= 0 && len >= 0;
    JS_FreeValue(ctx, len_val);
    if (!ok) {
        return false;
    }
    lv_coord_t *ptr = (lv_coord_t *)malloc((size_t)(len + 1) * sizeof(lv_coord_t));
    if (!ptr) {
        return false;
    }
    for (int32_t i = 0; i < len; i++) {
        JSValue item = JS_GetPropertyUint32(ctx, arr, (uint32_t)i);
        int32_t num = 0;
        ok = tjs_lvgl_js_to_i32(ctx, item, &num);
        JS_FreeValue(ctx, item);
        if (!ok) {
            free(ptr);
            return false;
        }
        ptr[i] = (lv_coord_t)num;
    }
    ptr[len] = LV_GRID_TEMPLATE_LAST;
    *out_ptr = ptr;
    return true;
}

static lv_style_selector_t tjs_lvgl_style_selector_from_type(int32_t style_type) {
    lv_part_t part = (lv_part_t)(style_type & 0xFF0000);
    lv_state_t state = (lv_state_t)(style_type & 0x00FFFF);
    if (part == 0) {
        part = LV_PART_MAIN;
    }
    return (lv_style_selector_t)(part | state);
}

static lv_part_t tjs_lvgl_style_part_from_type(int32_t style_type) {
    lv_part_t part = (lv_part_t)(style_type & 0xFF0000);
    return part == 0 ? LV_PART_MAIN : part;
}

static lv_state_t tjs_lvgl_style_state_from_type(int32_t style_type) {
    return (lv_state_t)(style_type & 0x00FFFF);
}

static JSValue tjs_lvgl_obj_set_style(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    (void)this_val;
    if (argc < 3) {
        return JS_ThrowTypeError(ctx, "Expected at least 3 arguments: ptr, styleObj, keys");
    }

    lv_obj_t *obj = tjs_lvgl_get_obj(ctx, argv[0]);
    if (!obj) {
        return JS_EXCEPTION;
    }
    JSValueConst style_obj = argv[1];
    JSValueConst keys = argv[2];
    int32_t key_len = 0;
    if (argc > 3 && !JS_IsUndefined(argv[3])) {
        if (JS_ToInt32(ctx, &key_len, argv[3]) < 0) {
            return JS_EXCEPTION;
        }
    } else {
        JSValue len_val = JS_GetPropertyStr(ctx, keys, "length");
        if (JS_ToInt32(ctx, &key_len, len_val) < 0) {
            JS_FreeValue(ctx, len_val);
            return JS_EXCEPTION;
        }
        JS_FreeValue(ctx, len_val);
    }
    if (key_len < 0) {
        key_len = 0;
    }

    int32_t style_type = 0;
    if (argc > 4 && !JS_IsUndefined(argv[4])) {
        if (JS_ToInt32(ctx, &style_type, argv[4]) < 0) {
            return JS_EXCEPTION;
        }
    }
    lv_style_selector_t selector = tjs_lvgl_style_selector_from_type(style_type);
    lv_part_t part = tjs_lvgl_style_part_from_type(style_type);
    lv_state_t state = tjs_lvgl_style_state_from_type(style_type);
    bool is_main_part = part == LV_PART_MAIN;
    bool is_default_state = state == LV_STATE_DEFAULT;
    bool has_background_opacity = false;
    for (int32_t i = 0; i < key_len; i++) {
        JSValue key_val = JS_GetPropertyUint32(ctx, keys, (uint32_t)i);
        const char *key = JS_ToCString(ctx, key_val);
        if (key && strcmp(key, "background-opacity") == 0) {
            has_background_opacity = true;
        }
        if (key) {
            JS_FreeCString(ctx, key);
        }
        JS_FreeValue(ctx, key_val);
    }

    for (int32_t i = 0; i < key_len; i++) {
        JSValue key_val = JS_GetPropertyUint32(ctx, keys, (uint32_t)i);
        const char *key = JS_ToCString(ctx, key_val);
        if (!key) {
            JS_FreeValue(ctx, key_val);
            continue;
        }
        JSValue value = JS_GetPropertyStr(ctx, style_obj, key);

        if (strcmp(key, "width") == 0) {
            int32_t v = 0;
            if (tjs_lvgl_js_to_i32(ctx, value, &v)) lv_obj_set_style_width(obj, v, selector);
        } else if (strcmp(key, "height") == 0) {
            int32_t v = 0;
            if (tjs_lvgl_js_to_i32(ctx, value, &v)) lv_obj_set_style_height(obj, v, selector);
        } else if (strcmp(key, "width_pct") == 0) {
            int32_t v = 0;
            if (tjs_lvgl_js_to_i32(ctx, value, &v)) lv_obj_set_style_width(obj, lv_pct(v), selector);
        } else if (strcmp(key, "height_pct") == 0) {
            int32_t v = 0;
            if (tjs_lvgl_js_to_i32(ctx, value, &v)) lv_obj_set_style_height(obj, lv_pct(v), selector);
        } else if (strcmp(key, "left") == 0) {
            int32_t v = 0;
            if (tjs_lvgl_js_to_i32(ctx, value, &v)) lv_obj_set_style_x(obj, v, selector);
        } else if (strcmp(key, "top") == 0) {
            int32_t v = 0;
            if (tjs_lvgl_js_to_i32(ctx, value, &v)) lv_obj_set_style_y(obj, v, selector);
        } else if (strcmp(key, "left_pct") == 0) {
            int32_t v = 0;
            if (tjs_lvgl_js_to_i32(ctx, value, &v)) lv_obj_set_style_x(obj, lv_pct(v), selector);
        } else if (strcmp(key, "top_pct") == 0) {
            int32_t v = 0;
            if (tjs_lvgl_js_to_i32(ctx, value, &v)) lv_obj_set_style_y(obj, lv_pct(v), selector);
        } else if (strcmp(key, "min-width") == 0) {
            int32_t v = 0;
            if (tjs_lvgl_js_to_i32(ctx, value, &v)) lv_obj_set_style_min_width(obj, v, selector);
        } else if (strcmp(key, "max-width") == 0) {
            int32_t v = 0;
            if (tjs_lvgl_js_to_i32(ctx, value, &v)) lv_obj_set_style_max_width(obj, v, selector);
        } else if (strcmp(key, "min-height") == 0) {
            int32_t v = 0;
            if (tjs_lvgl_js_to_i32(ctx, value, &v)) lv_obj_set_style_min_height(obj, v, selector);
        } else if (strcmp(key, "max-height") == 0) {
            int32_t v = 0;
            if (tjs_lvgl_js_to_i32(ctx, value, &v)) lv_obj_set_style_max_height(obj, v, selector);
        } else if (strcmp(key, "min-width_pct") == 0) {
            int32_t v = 0;
            if (tjs_lvgl_js_to_i32(ctx, value, &v)) lv_obj_set_style_min_width(obj, lv_pct(v), selector);
        } else if (strcmp(key, "max-width_pct") == 0) {
            int32_t v = 0;
            if (tjs_lvgl_js_to_i32(ctx, value, &v)) lv_obj_set_style_max_width(obj, lv_pct(v), selector);
        } else if (strcmp(key, "min-height_pct") == 0) {
            int32_t v = 0;
            if (tjs_lvgl_js_to_i32(ctx, value, &v)) lv_obj_set_style_min_height(obj, lv_pct(v), selector);
        } else if (strcmp(key, "max-height_pct") == 0) {
            int32_t v = 0;
            if (tjs_lvgl_js_to_i32(ctx, value, &v)) lv_obj_set_style_max_height(obj, lv_pct(v), selector);
        } else if (strcmp(key, "position") == 0) {
            if (is_main_part && is_default_state) {
                const char *str = JS_ToCString(ctx, value);
                if (str) {
                    if (strcmp(str, "absolute") == 0 || strcmp(str, "fixed") == 0) {
                        lv_obj_add_flag(obj, LV_OBJ_FLAG_FLOATING);
                    } else {
                        lv_obj_clear_flag(obj, LV_OBJ_FLAG_FLOATING);
                    }
                    JS_FreeCString(ctx, str);
                }
            }
        } else if (strcmp(key, "display") == 0) {
            if (is_main_part && is_default_state) {
                const char *str = JS_ToCString(ctx, value);
                if (str) {
                    lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
                    if (strcmp(str, "flex") == 0) {
                        lv_obj_set_layout(obj, LV_LAYOUT_FLEX);
                    } else if (strcmp(str, "grid") == 0) {
                        lv_obj_set_layout(obj, LV_LAYOUT_GRID);
                    } else if (strcmp(str, "none") == 0) {
                        lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
                    }
                    JS_FreeCString(ctx, str);
                }
            }
        } else if (strcmp(key, "flex-flow") == 0) {
            int32_t v = 0;
            if (is_main_part && tjs_lvgl_js_to_i32(ctx, value, &v)) lv_obj_set_flex_flow(obj, (lv_flex_flow_t)v);
        } else if (strcmp(key, "justify-content") == 0) {
            int32_t v = 0;
            if (tjs_lvgl_js_to_i32(ctx, value, &v)) lv_obj_set_style_flex_main_place(obj, (lv_flex_align_t)v, selector);
        } else if (strcmp(key, "align-items") == 0) {
            int32_t v = 0;
            if (tjs_lvgl_js_to_i32(ctx, value, &v)) lv_obj_set_style_flex_cross_place(obj, (lv_flex_align_t)v, selector);
        } else if (strcmp(key, "align-content") == 0) {
            int32_t v = 0;
            if (tjs_lvgl_js_to_i32(ctx, value, &v)) lv_obj_set_style_flex_track_place(obj, (lv_flex_align_t)v, selector);
        } else if (strcmp(key, "flex-grow") == 0) {
            int32_t v = 0;
            if (tjs_lvgl_js_to_i32(ctx, value, &v)) lv_obj_set_style_flex_grow(obj, v, selector);
        } else if (strcmp(key, "padding") == 0) {
            int32_t v = 0;
            if (tjs_lvgl_js_to_i32(ctx, value, &v)) lv_obj_set_style_pad_all(obj, v, selector);
        } else if (strcmp(key, "padding-left") == 0) {
            int32_t v = 0;
            if (tjs_lvgl_js_to_i32(ctx, value, &v)) lv_obj_set_style_pad_left(obj, v, selector);
        } else if (strcmp(key, "padding-right") == 0) {
            int32_t v = 0;
            if (tjs_lvgl_js_to_i32(ctx, value, &v)) lv_obj_set_style_pad_right(obj, v, selector);
        } else if (strcmp(key, "padding-top") == 0) {
            int32_t v = 0;
            if (tjs_lvgl_js_to_i32(ctx, value, &v)) lv_obj_set_style_pad_top(obj, v, selector);
        } else if (strcmp(key, "padding-bottom") == 0) {
            int32_t v = 0;
            if (tjs_lvgl_js_to_i32(ctx, value, &v)) lv_obj_set_style_pad_bottom(obj, v, selector);
        } else if (strcmp(key, "row-spacing") == 0) {
            int32_t v = 0;
            if (tjs_lvgl_js_to_i32(ctx, value, &v)) lv_obj_set_style_pad_row(obj, v, selector);
        } else if (strcmp(key, "column-spacing") == 0) {
            int32_t v = 0;
            if (tjs_lvgl_js_to_i32(ctx, value, &v)) lv_obj_set_style_pad_column(obj, v, selector);
        } else if (strcmp(key, "background-color") == 0) {
            lv_color_t color;
            if (tjs_lvgl_js_to_color(ctx, value, &color)) {
                lv_obj_set_style_bg_color(obj, color, selector);
                if (!has_background_opacity) {
                    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, selector);
                }
            }
        } else if (strcmp(key, "background-opacity") == 0) {
            lv_opa_t opa = LV_OPA_COVER;
            if (tjs_lvgl_js_to_opa(ctx, value, &opa)) lv_obj_set_style_bg_opa(obj, opa, selector);
        } else if (strcmp(key, "background-grad-color") == 0) {
            lv_color_t color;
            if (tjs_lvgl_js_to_color(ctx, value, &color)) lv_obj_set_style_bg_grad_color(obj, color, selector);
        } else if (strcmp(key, "background-grad-color-dir") == 0) {
            int32_t v = 0;
            if (tjs_lvgl_js_to_i32(ctx, value, &v)) lv_obj_set_style_bg_grad_dir(obj, (lv_grad_dir_t)v, selector);
        } else if (strcmp(key, "background-image-opacity") == 0) {
            lv_opa_t opa = LV_OPA_COVER;
            if (tjs_lvgl_js_to_opa(ctx, value, &opa)) lv_obj_set_style_bg_img_opa(obj, opa, selector);
        } else if (strcmp(key, "background-image-recolor") == 0) {
            lv_color_t color;
            if (tjs_lvgl_js_to_color(ctx, value, &color)) lv_obj_set_style_bg_img_recolor(obj, color, selector);
        } else if (strcmp(key, "background-image-recolor-opacity") == 0) {
            lv_opa_t opa = LV_OPA_COVER;
            if (tjs_lvgl_js_to_opa(ctx, value, &opa)) lv_obj_set_style_bg_img_recolor_opa(obj, opa, selector);
        } else if (strcmp(key, "background-image-tiled") == 0) {
            int enabled = JS_ToBool(ctx, value);
            if (enabled >= 0) lv_obj_set_style_bg_img_tiled(obj, enabled ? true : false, selector);
        } else if (strcmp(key, "text-color") == 0) {
            lv_color_t color;
            if (tjs_lvgl_js_to_color(ctx, value, &color)) lv_obj_set_style_text_color(obj, color, selector);
        } else if (strcmp(key, "border-radius") == 0) {
            int32_t v = 0;
            if (tjs_lvgl_js_to_i32(ctx, value, &v)) lv_obj_set_style_radius(obj, v, selector);
        } else if (strcmp(key, "border-width") == 0) {
            int32_t v = 0;
            if (tjs_lvgl_js_to_i32(ctx, value, &v)) lv_obj_set_style_border_width(obj, v, selector);
        } else if (strcmp(key, "border-side") == 0) {
            lv_border_side_t side = LV_BORDER_SIDE_FULL;
            if (tjs_lvgl_js_to_border_side(ctx, value, &side)) lv_obj_set_style_border_side(obj, side, selector);
        } else if (strcmp(key, "border-color") == 0) {
            lv_color_t color;
            if (tjs_lvgl_js_to_color(ctx, value, &color)) lv_obj_set_style_border_color(obj, color, selector);
        } else if (strcmp(key, "border-opacity") == 0) {
            lv_opa_t opa = LV_OPA_COVER;
            if (tjs_lvgl_js_to_opa(ctx, value, &opa)) lv_obj_set_style_border_opa(obj, opa, selector);
        } else if (strcmp(key, "opacity") == 0) {
            lv_opa_t opa = LV_OPA_COVER;
            if (tjs_lvgl_js_to_opa(ctx, value, &opa)) lv_obj_set_style_opa(obj, opa, selector);
        } else if (strcmp(key, "outline-width") == 0) {
            int32_t v = 0;
            if (tjs_lvgl_js_to_i32(ctx, value, &v)) lv_obj_set_style_outline_width(obj, v, selector);
        } else if (strcmp(key, "outline-color") == 0) {
            lv_color_t color;
            if (tjs_lvgl_js_to_color(ctx, value, &color)) lv_obj_set_style_outline_color(obj, color, selector);
        } else if (strcmp(key, "outline-opacity") == 0) {
            lv_opa_t opa = LV_OPA_COVER;
            if (tjs_lvgl_js_to_opa(ctx, value, &opa)) lv_obj_set_style_outline_opa(obj, opa, selector);
        } else if (strcmp(key, "shadow-width") == 0) {
            int32_t v = 0;
            if (tjs_lvgl_js_to_i32(ctx, value, &v)) lv_obj_set_style_shadow_width(obj, v, selector);
        } else if (strcmp(key, "shadow-color") == 0) {
            lv_color_t color;
            if (tjs_lvgl_js_to_color(ctx, value, &color)) lv_obj_set_style_shadow_color(obj, color, selector);
        } else if (strcmp(key, "shadow-opacity") == 0) {
            lv_opa_t opa = LV_OPA_COVER;
            if (tjs_lvgl_js_to_opa(ctx, value, &opa)) lv_obj_set_style_shadow_opa(obj, opa, selector);
        } else if (strcmp(key, "line-width") == 0) {
            int32_t v = 0;
            if (tjs_lvgl_js_to_i32(ctx, value, &v)) lv_obj_set_style_line_width(obj, v, selector);
        } else if (strcmp(key, "line-color") == 0) {
            lv_color_t color;
            if (tjs_lvgl_js_to_color(ctx, value, &color)) lv_obj_set_style_line_color(obj, color, selector);
        } else if (strcmp(key, "line-rounded") == 0) {
            int enabled = JS_ToBool(ctx, value);
            if (enabled >= 0) lv_obj_set_style_line_rounded(obj, enabled ? true : false, selector);
        } else if (strcmp(key, "line-opacity") == 0) {
            lv_opa_t opa = LV_OPA_COVER;
            if (tjs_lvgl_js_to_opa(ctx, value, &opa)) lv_obj_set_style_line_opa(obj, opa, selector);
        } else if (strcmp(key, "line-dash-width") == 0) {
            int32_t v = 0;
            if (tjs_lvgl_js_to_i32(ctx, value, &v)) lv_obj_set_style_line_dash_width(obj, v, selector);
        } else if (strcmp(key, "line-dash-gap") == 0) {
            int32_t v = 0;
            if (tjs_lvgl_js_to_i32(ctx, value, &v)) lv_obj_set_style_line_dash_gap(obj, v, selector);
        } else if (strcmp(key, "font-size") == 0 || strcmp(key, "font-size-1") == 0) {
            int32_t v = 0;
            if (tjs_lvgl_js_to_i32(ctx, value, &v)) lv_obj_set_style_text_font(obj, tjs_lvgl_font_by_index(v), selector);
        } else if (strcmp(key, "img-opacity") == 0) {
            lv_opa_t opa = LV_OPA_COVER;
            if (tjs_lvgl_js_to_opa(ctx, value, &opa)) lv_obj_set_style_img_opa(obj, opa, selector);
        } else if (strcmp(key, "recolor") == 0 || strcmp(key, "img-recolor") == 0) {
            lv_color_t color;
            if (tjs_lvgl_js_to_color(ctx, value, &color)) lv_obj_set_style_img_recolor(obj, color, selector);
        } else if (strcmp(key, "img-recolor-opacity") == 0) {
            lv_opa_t opa = LV_OPA_COVER;
            if (tjs_lvgl_js_to_opa(ctx, value, &opa)) lv_obj_set_style_img_recolor_opa(obj, opa, selector);
        } else if (strcmp(key, "img-scale") == 0) {
            int32_t v = 256;
            if (tjs_lvgl_js_to_i32(ctx, value, &v) && lv_obj_check_type(obj, &lv_img_class)) lv_img_set_zoom(obj, (uint16_t)v);
        } else if (strcmp(key, "img-rotate") == 0) {
            int32_t v = 0;
            if (tjs_lvgl_js_to_i32(ctx, value, &v) && lv_obj_check_type(obj, &lv_img_class)) lv_img_set_angle(obj, (int16_t)v);
        } else if (strcmp(key, "img-origin") == 0) {
            if (JS_IsArray(value) && lv_obj_check_type(obj, &lv_img_class)) {
                int32_t x = 0;
                int32_t y = 0;
                JSValue xv = JS_GetPropertyUint32(ctx, value, 0);
                JSValue yv = JS_GetPropertyUint32(ctx, value, 1);
                bool ok = tjs_lvgl_js_to_i32(ctx, xv, &x) && tjs_lvgl_js_to_i32(ctx, yv, &y);
                JS_FreeValue(ctx, xv);
                JS_FreeValue(ctx, yv);
                if (ok) lv_img_set_pivot(obj, (lv_coord_t)x, (lv_coord_t)y);
            }
        } else if (strcmp(key, "grid-template") == 0) {
            if (is_main_part && is_default_state && JS_IsArray(value)) {
                JSValue cols_val = JS_GetPropertyUint32(ctx, value, 0);
                JSValue rows_val = JS_GetPropertyUint32(ctx, value, 1);
                lv_coord_t *cols = NULL;
                lv_coord_t *rows = NULL;
                bool ok_cols = tjs_lvgl_copy_coord_array(ctx, cols_val, &cols);
                bool ok_rows = tjs_lvgl_copy_coord_array(ctx, rows_val, &rows);
                if (ok_cols && ok_rows) {
                    TjsLVGLStyleState *state = tjs_lvgl_style_state_get(obj, true);
                    if (state) {
                        free(state->grid_cols);
                        free(state->grid_rows);
                        state->grid_cols = cols;
                        state->grid_rows = rows;
                        lv_obj_set_grid_dsc_array(obj, state->grid_cols, state->grid_rows);
                    } else {
                        free(cols);
                        free(rows);
                    }
                } else {
                    free(cols);
                    free(rows);
                }
                JS_FreeValue(ctx, cols_val);
                JS_FreeValue(ctx, rows_val);
            }
        } else if (strcmp(key, "grid-child") == 0) {
            if (is_main_part && is_default_state && JS_IsArray(value)) {
                int32_t nums[6] = {0, 0, 1, 0, 0, 1};
                bool ok = true;
                for (uint32_t n = 0; n < 6; n++) {
                    JSValue item = JS_GetPropertyUint32(ctx, value, n);
                    ok = tjs_lvgl_js_to_i32(ctx, item, &nums[n]) && ok;
                    JS_FreeValue(ctx, item);
                }
                if (ok) {
                    lv_obj_set_grid_cell(
                        obj,
                        (lv_grid_align_t)nums[0], nums[1], nums[2],
                        (lv_grid_align_t)nums[3], nums[4], nums[5]
                    );
                }
            }
        } else if (strcmp(key, "grid-align") == 0) {
            if (is_main_part && is_default_state && JS_IsArray(value)) {
                int32_t col = 0;
                int32_t row = 0;
                JSValue col_val = JS_GetPropertyUint32(ctx, value, 0);
                JSValue row_val = JS_GetPropertyUint32(ctx, value, 1);
                bool ok = tjs_lvgl_js_to_i32(ctx, col_val, &col) && tjs_lvgl_js_to_i32(ctx, row_val, &row);
                JS_FreeValue(ctx, col_val);
                JS_FreeValue(ctx, row_val);
                if (ok) {
                    lv_obj_set_grid_align(obj, (lv_grid_align_t)col, (lv_grid_align_t)row);
                }
            }
        } else if (strcmp(key, "overflow") == 0) {
            if (is_main_part && is_default_state) {
                bool scrollable = true;
                if (tjs_lvgl_js_to_overflow_mode(ctx, value, &scrollable)) {
                    if (scrollable) {
                        lv_obj_add_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
                    } else {
                        lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
                    }
                }
            }
        } else if (strcmp(key, "width_content") == 0) {
            /* width: auto → size to content (LV_SIZE_CONTENT) */
            int32_t v = 0;
            if (tjs_lvgl_js_to_i32(ctx, value, &v) && v) {
                lv_obj_set_style_width(obj, LV_SIZE_CONTENT, selector);
            }
        } else if (strcmp(key, "height_content") == 0) {
            /* height: auto → size to content (LV_SIZE_CONTENT) */
            int32_t v = 0;
            if (tjs_lvgl_js_to_i32(ctx, value, &v) && v) {
                lv_obj_set_style_height(obj, LV_SIZE_CONTENT, selector);
            }
        }

        JS_FreeValue(ctx, value);
        JS_FreeCString(ctx, key);
        JS_FreeValue(ctx, key_val);
    }

    lv_obj_mark_layout_as_dirty(obj);
    lv_obj_update_layout(obj);
    lv_obj_refresh_style(obj, LV_PART_ANY, LV_STYLE_PROP_ANY);
    return JS_UNDEFINED;
}

typedef struct {
    lv_obj_t *target;
    lv_obj_t *current_target;
    uint32_t code;
    int32_t value;
    char *text_value;
} TjsLVGLEvent;

#define TJS_LVGL_EVENT_QUEUE_CAP 256
static TjsLVGLEvent tjs_lvgl_event_queue[TJS_LVGL_EVENT_QUEUE_CAP];
static uint32_t tjs_lvgl_event_head = 0;
static uint32_t tjs_lvgl_event_tail = 0;

static void tjs_lvgl_event_clear(TjsLVGLEvent *ev) {
    if (!ev) {
        return;
    }
    free(ev->text_value);
    ev->text_value = NULL;
}

static int32_t tjs_lvgl_get_event_value(lv_obj_t *target) {
    if (!target) {
        return 0;
    }
#if LV_USE_SLIDER != 0
    if (lv_obj_check_type(target, &lv_slider_class)) {
        return (int32_t)lv_slider_get_value(target);
    }
#endif
#if LV_USE_DROPDOWN != 0
    if (lv_obj_check_type(target, &lv_dropdown_class)) {
        return (int32_t)lv_dropdown_get_selected(target);
    }
#endif
    return 0;
}

static char *tjs_lvgl_get_event_text_value(lv_obj_t *target, uint32_t code) {
    if (!target || code != LV_EVENT_VALUE_CHANGED) {
        return NULL;
    }
#if LV_USE_CALENDAR != 0
    lv_obj_t *calendar = NULL;
    if (lv_obj_check_type(target, &lv_calendar_class)) {
        calendar = target;
    } else {
        lv_obj_t *parent = lv_obj_get_parent(target);
        if (parent && lv_obj_check_type(parent, &lv_calendar_class)) {
            calendar = parent;
        }
    }
    if (calendar) {
        lv_calendar_date_t date;
        if (lv_calendar_get_pressed_date(calendar, &date) == LV_RES_OK) {
            char buf[16];
            snprintf(buf, sizeof(buf), "%04u-%02d-%02d", (unsigned)date.year, (int)date.month, (int)date.day);
            return tjs_lvgl_strdup(buf);
        }
    }
#endif
    return NULL;
}

static void tjs_lvgl_event_queue_push(lv_obj_t *target, lv_obj_t *current_target, uint32_t code) {
    uint32_t next = (tjs_lvgl_event_tail + 1) % TJS_LVGL_EVENT_QUEUE_CAP;
    if (next == tjs_lvgl_event_head) {
        /* Drop oldest event if queue is full to keep UI responsive. */
        tjs_lvgl_event_clear(&tjs_lvgl_event_queue[tjs_lvgl_event_head]);
        tjs_lvgl_event_head = (tjs_lvgl_event_head + 1) % TJS_LVGL_EVENT_QUEUE_CAP;
    }
    tjs_lvgl_event_queue[tjs_lvgl_event_tail].target = target;
    tjs_lvgl_event_queue[tjs_lvgl_event_tail].current_target = current_target;
    tjs_lvgl_event_queue[tjs_lvgl_event_tail].code = code;
    tjs_lvgl_event_queue[tjs_lvgl_event_tail].value = tjs_lvgl_get_event_value(target);
    tjs_lvgl_event_queue[tjs_lvgl_event_tail].text_value = tjs_lvgl_get_event_text_value(target, code);
    tjs_lvgl_event_tail = next;
}

static bool tjs_lvgl_event_queue_pop(TjsLVGLEvent *ev) {
    if (tjs_lvgl_event_head == tjs_lvgl_event_tail) {
        return false;
    }
    uint32_t idx = tjs_lvgl_event_head;
    *ev = tjs_lvgl_event_queue[idx];
    memset(&tjs_lvgl_event_queue[idx], 0, sizeof(tjs_lvgl_event_queue[idx]));
    tjs_lvgl_event_head = (tjs_lvgl_event_head + 1) % TJS_LVGL_EVENT_QUEUE_CAP;
    return true;
}

static void tjs_lvgl_event_cb(lv_event_t *e) {
    if (!e) {
        return;
    }
    lv_obj_t *target = e->target;
    lv_obj_t *current_target = e->current_target;
#if LV_USE_CALENDAR != 0
    if (target && lv_obj_get_parent(target) && lv_obj_check_type(lv_obj_get_parent(target), &lv_calendar_class)) {
        target = lv_obj_get_parent(target);
    }
#endif
    tjs_lvgl_event_queue_push(target, current_target, (uint32_t)e->code);
}

/* ============================================================
 * Display and Input Driver Initialization
 * ============================================================ */

#ifdef TJS_LVGL_USE_SDL
/* SDL state */
static SDL_Window *sdl_window = NULL;
static SDL_Renderer *sdl_renderer = NULL;
static SDL_Texture *sdl_texture = NULL;
static bool sdl_window_closed = false;
static uint32_t *sdl_pixels = NULL;
static size_t sdl_pixels_cap = 0;
static int32_t sdl_mouse_x = 0;
static int32_t sdl_mouse_y = 0;
static bool sdl_mouse_pressed = false;
#endif

#ifdef TJS_LVGL_USE_SDL
static void sdl_display_flush(lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *color_p);
static void sdl_mouse_read(lv_indev_drv_t *drv, lv_indev_data_t *data);

static void tjs_lvgl_sdl_shutdown(void) {
    if (sdl_texture) {
        SDL_DestroyTexture(sdl_texture);
        sdl_texture = NULL;
    }
    if (sdl_renderer) {
        SDL_DestroyRenderer(sdl_renderer);
        sdl_renderer = NULL;
    }
    if (sdl_window) {
        SDL_DestroyWindow(sdl_window);
        sdl_window = NULL;
    }
    SDL_QuitSubSystem(SDL_INIT_VIDEO);

    free(sdl_pixels);
    sdl_pixels = NULL;
    sdl_pixels_cap = 0;
}
#endif

#ifndef TJS_LVGL_USE_SDL
#ifdef __linux__
#define TJS_LVGL_BITS_PER_LONG (sizeof(unsigned long) * 8)
#define TJS_LVGL_BITS_TO_LONGS(nr) (((nr) + TJS_LVGL_BITS_PER_LONG - 1) / TJS_LVGL_BITS_PER_LONG)

static bool tjs_lvgl_test_bit(size_t bit, const unsigned long *bits) {
    return (bits[bit / TJS_LVGL_BITS_PER_LONG] & (1UL << (bit % TJS_LVGL_BITS_PER_LONG))) != 0;
}

static bool tjs_lvgl_evdev_has_bitset(int fd, unsigned int ev, size_t max_bit, unsigned long *bits, size_t bytes) {
    memset(bits, 0, bytes);
    if (ioctl(fd, EVIOCGBIT(ev, bytes), bits) < 0) {
        return false;
    }
    (void)max_bit;
    return true;
}

static int tjs_lvgl_evdev_pointer_score(const char *path) {
    int fd = open(path, O_RDONLY | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        return 0;
    }

    unsigned long ev_bits[TJS_LVGL_BITS_TO_LONGS(EV_MAX + 1)];
    unsigned long abs_bits[TJS_LVGL_BITS_TO_LONGS(ABS_MAX + 1)];
    unsigned long rel_bits[TJS_LVGL_BITS_TO_LONGS(REL_MAX + 1)];
    unsigned long key_bits[TJS_LVGL_BITS_TO_LONGS(KEY_MAX + 1)];

    int score = 0;
    if (tjs_lvgl_evdev_has_bitset(fd, 0, EV_MAX, ev_bits, sizeof(ev_bits))) {
        bool has_touch_button = false;
        bool has_mouse_button = false;
        if (tjs_lvgl_test_bit(EV_KEY, ev_bits) &&
            tjs_lvgl_evdev_has_bitset(fd, EV_KEY, KEY_MAX, key_bits, sizeof(key_bits))) {
            has_touch_button = tjs_lvgl_test_bit(BTN_TOUCH, key_bits);
            has_mouse_button = tjs_lvgl_test_bit(BTN_MOUSE, key_bits);
        }

        if (tjs_lvgl_test_bit(EV_ABS, ev_bits) &&
            tjs_lvgl_evdev_has_bitset(fd, EV_ABS, ABS_MAX, abs_bits, sizeof(abs_bits))) {
            bool has_abs_xy = tjs_lvgl_test_bit(ABS_X, abs_bits) && tjs_lvgl_test_bit(ABS_Y, abs_bits);
            bool has_mt_xy = tjs_lvgl_test_bit(ABS_MT_POSITION_X, abs_bits) && tjs_lvgl_test_bit(ABS_MT_POSITION_Y, abs_bits);
            if (has_touch_button && has_mt_xy) {
                score = 400;
            } else if (has_touch_button && has_abs_xy) {
                score = 350;
            } else if (has_mt_xy) {
                score = 300;
            } else if (has_abs_xy) {
                score = 250;
            }
        }

        if (score == 0 && tjs_lvgl_test_bit(EV_REL, ev_bits) &&
            tjs_lvgl_evdev_has_bitset(fd, EV_REL, REL_MAX, rel_bits, sizeof(rel_bits))) {
            bool has_rel_xy = tjs_lvgl_test_bit(REL_X, rel_bits) && tjs_lvgl_test_bit(REL_Y, rel_bits);
            if (has_rel_xy && has_mouse_button) {
                score = 100;
            }
        }
    }

    close(fd);
    return score;
}

static bool tjs_lvgl_find_evdev_path(char *out, size_t out_len) {
    if (!out || out_len == 0) {
        return false;
    }
    int best_score = 0;
    char best_path[64] = { 0 };
    for (int i = 0; i < 32; i++) {
        char path[64];
        snprintf(path, sizeof(path), "/dev/input/event%d", i);
        int score = tjs_lvgl_evdev_pointer_score(path);
        if (score > best_score) {
            best_score = score;
            snprintf(best_path, sizeof(best_path), "%s", path);
        }
    }
    if (best_score > 0) {
        snprintf(out, out_len, "%s", best_path);
        return true;
    }
    return false;
}
#endif

static int tjs_lvgl_register_evdev_input(void) {
    const char *env_path = getenv("XUAN_LVGL_EVDEV");
    if (env_path && (!strcmp(env_path, "0") || !strcmp(env_path, "off") || !strcmp(env_path, "none"))) {
        printf("[lvgl] evdev input disabled by XUAN_LVGL_EVDEV=%s\n", env_path);
        return 0;
    }

    char auto_path[64];
    const char *dev_path = env_path;
#ifdef __linux__
    if (!dev_path || !dev_path[0]) {
        if (tjs_lvgl_find_evdev_path(auto_path, sizeof(auto_path))) {
            dev_path = auto_path;
        }
    }
#endif

    if (!dev_path || !dev_path[0]) {
        dev_path = EVDEV_NAME;
    }

    bool opened = evdev_set_file((char *)dev_path);
    if (!opened) {
        printf("[lvgl] evdev input unavailable");
        if (dev_path && dev_path[0]) {
            printf(": %s", dev_path);
        }
        printf("; continuing without touch input\n");
        return 0;
    }

    lv_indev_drv_t *indev_drv = (lv_indev_drv_t *)calloc(1, sizeof(lv_indev_drv_t));
    if (!indev_drv) {
        return -1;
    }
    lv_indev_drv_init(indev_drv);
    indev_drv->type = LV_INDEV_TYPE_POINTER;
    indev_drv->read_cb = evdev_read;
    indev_drv->disp = lvgl_state.disp;
    lvgl_state.indev = lv_indev_drv_register(indev_drv);
    if (!lvgl_state.indev) {
        free(indev_drv);
        printf("[lvgl] failed to register evdev input; continuing without touch input\n");
        return 0;
    }

    printf("[lvgl] evdev input registered: %s\n", dev_path ? dev_path : EVDEV_NAME);
    return 0;
}
#endif

static int tjs__lvgl_drivers_init(void) {
#ifdef TJS_LVGL_USE_SDL
    sdl_window_closed = false;
    sdl_mouse_x = 0;
    sdl_mouse_y = 0;
    sdl_mouse_pressed = false;
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        printf("[lvgl] SDL_Init failed: %s\n", SDL_GetError());
        return -1;
    }

    sdl_window = SDL_CreateWindow("LVGL xuan",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        TJS_LVGL_SDL_HOR_RES, TJS_LVGL_SDL_VER_RES, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if (!sdl_window) {
        printf("[lvgl] SDL_CreateWindow failed: %s\n", SDL_GetError());
        return -1;
    }

    sdl_renderer = SDL_CreateRenderer(sdl_window, -1, SDL_RENDERER_ACCELERATED);
    if (!sdl_renderer) {
        sdl_renderer = SDL_CreateRenderer(sdl_window, -1, SDL_RENDERER_SOFTWARE);
    }
    if (!sdl_renderer) {
        printf("[lvgl] SDL_CreateRenderer failed: %s\n", SDL_GetError());
        return -1;
    }

    /* Allocate draw buffer on heap */
    draw_buf = (lv_color_t *)malloc(DISP_BUF_SIZE * sizeof(lv_color_t));
    if (!draw_buf) return -1;
    disp_buf_ptr = (lv_disp_draw_buf_t *)calloc(1, sizeof(lv_disp_draw_buf_t));
    lv_disp_draw_buf_init(disp_buf_ptr, draw_buf, NULL, DISP_BUF_SIZE);

    sdl_texture = SDL_CreateTexture(sdl_renderer, SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STATIC, TJS_LVGL_SDL_HOR_RES, TJS_LVGL_SDL_VER_RES);
    if (!sdl_texture) {
        printf("[lvgl] SDL_CreateTexture failed: %s\n", SDL_GetError());
        return -1;
    }

    printf("[lvgl] SDL window created successfully (%dx%d)\n", TJS_LVGL_SDL_HOR_RES, TJS_LVGL_SDL_VER_RES);

    /* Allocate display driver on heap */
    lv_disp_drv_t *disp_drv = (lv_disp_drv_t *)calloc(1, sizeof(lv_disp_drv_t));
    lv_disp_drv_init(disp_drv);
    disp_drv->draw_buf = disp_buf_ptr;
    disp_drv->flush_cb = sdl_display_flush;
    disp_drv->hor_res = TJS_LVGL_SDL_HOR_RES;
    disp_drv->ver_res = TJS_LVGL_SDL_VER_RES;

    lvgl_state.disp = lv_disp_drv_register(disp_drv);
    if (!lvgl_state.disp) {
        return -1;
    }

    lv_theme_t *theme = lv_theme_default_init(
        lvgl_state.disp,
        lv_palette_main(LV_PALETTE_BLUE),
        lv_palette_main(LV_PALETTE_RED),
        LV_THEME_DEFAULT_DARK,
        LV_FONT_DEFAULT
    );
    if (theme) {
        lv_disp_set_theme(lvgl_state.disp, theme);
    }

    lv_group_t *group = lv_group_create();
    if (group) {
        lv_group_set_default(group);
    }

    /* Register SDL mouse input device for click events. */
    lv_indev_drv_t *indev_drv = (lv_indev_drv_t *)calloc(1, sizeof(lv_indev_drv_t));
    if (!indev_drv) {
        return -1;
    }
    lv_indev_drv_init(indev_drv);
    indev_drv->type = LV_INDEV_TYPE_POINTER;
    indev_drv->read_cb = sdl_mouse_read;
    lvgl_state.indev = lv_indev_drv_register(indev_drv);
    if (!lvgl_state.indev) {
        return -1;
    }
#else
    fbdev_init();

    /* Allocate draw buffer on heap */
    draw_buf = (lv_color_t *)malloc(DISP_BUF_SIZE * sizeof(lv_color_t));
    if (!draw_buf) {
        return -1;
    }
    disp_buf_ptr = (lv_disp_draw_buf_t *)calloc(1, sizeof(lv_disp_draw_buf_t));
    lv_disp_draw_buf_init(disp_buf_ptr, draw_buf, NULL, DISP_BUF_SIZE);

    /* Auto-detect framebuffer resolution */
    uint32_t fb_width = 800, fb_height = 480;
    fbdev_get_sizes(&fb_width, &fb_height);

    /* Allocate display driver on heap - must persist after init! */
    lv_disp_drv_t *disp_drv = (lv_disp_drv_t *)calloc(1, sizeof(lv_disp_drv_t));
    lv_disp_drv_init(disp_drv);
    disp_drv->draw_buf = disp_buf_ptr;
    disp_drv->flush_cb = fbdev_flush;
    disp_drv->hor_res = fb_width;
    disp_drv->ver_res = fb_height;

    lvgl_state.disp = lv_disp_drv_register(disp_drv);
    if (!lvgl_state.disp) {
        return -1;
    }

    lv_theme_t *theme = lv_theme_default_init(
        lvgl_state.disp,
        lv_palette_main(LV_PALETTE_BLUE),
        lv_palette_main(LV_PALETTE_RED),
        LV_THEME_DEFAULT_DARK,
        LV_FONT_DEFAULT
    );
    if (theme) {
        lv_disp_set_theme(lvgl_state.disp, theme);
    }

    lv_group_t *group = lv_group_create();
    if (group) {
        lv_group_set_default(group);
    }

    if (tjs_lvgl_register_evdev_input() != 0) {
        return -1;
    }
#endif

    return 0;
}

/* ============================================================
 * JavaScript Bindings - Helper Functions
 * ============================================================ */

static JSValue tjs_lvgl_new_obj(JSContext *ctx, lv_obj_t *obj) {
    return JS_NewBigInt64(ctx, (int64_t)(intptr_t)obj);
}

static lv_obj_t *tjs_lvgl_get_obj(JSContext *ctx, JSValue val) {
    int64_t ptr;
    if (JS_ToBigInt64(ctx, &ptr, val) < 0) {
        return NULL;
    }
    return (lv_obj_t *)(intptr_t)ptr;
}

/* ============================================================
 * Core Functions
 * ============================================================ */

static JSValue tjs_lvgl_init(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    if (lvgl_state.initialized) {
        return JS_ThrowTypeError(ctx, "LVGL already initialized");
    }

    lv_init();
    lv_extra_init();

    int ret = tjs__lvgl_drivers_init();
    if (ret != 0) {
        return JS_ThrowInternalError(ctx, "Failed to initialize LVGL drivers");
    }

    lvgl_state.initialized = true;
    lvgl_state.ctx = ctx;

    return JS_UNDEFINED;
}

static JSValue tjs_lvgl_tick(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    return JS_NewInt32(ctx, lv_tick_get());
}

static JSValue tjs_lvgl_get_display_size(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    (void)this_val;
    (void)argc;
    (void)argv;

    lv_disp_t *disp = lvgl_state.disp ? lvgl_state.disp : lv_disp_get_default();
    JSValue out = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, out, "width", JS_NewInt32(ctx, disp ? (int32_t)lv_disp_get_hor_res(disp) : 0));
    JS_SetPropertyStr(ctx, out, "height", JS_NewInt32(ctx, disp ? (int32_t)lv_disp_get_ver_res(disp) : 0));
    return out;
}

static JSValue tjs_lvgl_refresh_window(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    (void)this_val;
    (void)argc;
    (void)argv;

    if (!lvgl_state.initialized) {
        return JS_UNDEFINED;
    }

    lv_obj_t *screen = lv_scr_act();
    if (screen) {
        lv_obj_mark_layout_as_dirty(screen);
        lv_obj_update_layout(screen);
    }
    lv_refr_now(NULL);
    return JS_UNDEFINED;
}

/* Called from JS periodically to process LVGL tasks */
static JSValue tjs_lvgl_tickHandler(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
#ifdef TJS_LVGL_USE_SDL
    if (sdl_window_closed) {
        return JS_NewBool(ctx, 0);
    }

    /* Process SDL events (window close, keyboard, mouse) */
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT ||
            (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE)) {
            sdl_window_closed = true;
            tjs_lvgl_sdl_shutdown();
            break;
        } else if (event.type == SDL_MOUSEMOTION) {
            sdl_mouse_x = event.motion.x;
            sdl_mouse_y = event.motion.y;
        } else if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) {
            sdl_mouse_pressed = true;
            sdl_mouse_x = event.button.x;
            sdl_mouse_y = event.button.y;
        } else if (event.type == SDL_MOUSEBUTTONUP && event.button.button == SDL_BUTTON_LEFT) {
            sdl_mouse_pressed = false;
            sdl_mouse_x = event.button.x;
            sdl_mouse_y = event.button.y;
        }
    }
    if (sdl_window_closed) {
        return JS_NewBool(ctx, 0);
    }
#endif
    if (lvgl_state.initialized) {
        lv_timer_handler();
    }
    return JS_NewBool(ctx, 1);
}

#ifdef TJS_LVGL_USE_SDL
/* Direct SDL2 display flush callback */
static void sdl_display_flush(lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *color_p) {
    if (!sdl_renderer || !sdl_texture) {
        lv_disp_flush_ready(disp_drv);
        return;
    }

    lv_coord_t hres = disp_drv->hor_res;
    lv_coord_t vres = disp_drv->ver_res;

    if (area->x2 < 0 || area->y2 < 0 || area->x1 > hres - 1 || area->y1 > vres - 1) {
        lv_disp_flush_ready(disp_drv);
        return;
    }

    /* Convert LVGL colors to ARGB8888 and update texture */
    int w = area->x2 - area->x1 + 1;
    int h = area->y2 - area->y1 + 1;
    size_t pixels_needed = (size_t)w * (size_t)h;
    if (pixels_needed > sdl_pixels_cap) {
        uint32_t *new_pixels = (uint32_t *)realloc(sdl_pixels, pixels_needed * sizeof(uint32_t));
        if (!new_pixels) {
            lv_disp_flush_ready(disp_drv);
            return;
        }
        sdl_pixels = new_pixels;
        sdl_pixels_cap = pixels_needed;
    }
    if (!sdl_pixels) {
        lv_disp_flush_ready(disp_drv);
        return;
    }

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            lv_color_t c = color_p[y * w + x];
            sdl_pixels[y * w + x] = (0xFF << 24) | (c.ch.red << 16) | (c.ch.green << 8) | c.ch.blue;
        }
    }

    SDL_Rect rect = { area->x1, area->y1, w, h };
    SDL_UpdateTexture(sdl_texture, &rect, sdl_pixels, w * sizeof(uint32_t));

    if (lv_disp_flush_is_last(disp_drv)) {
        SDL_RenderClear(sdl_renderer);
        SDL_RenderCopy(sdl_renderer, sdl_texture, NULL, NULL);
        SDL_RenderPresent(sdl_renderer);
    }

    lv_disp_flush_ready(disp_drv);
}

static void sdl_mouse_read(lv_indev_drv_t *drv, lv_indev_data_t *data) {
    (void)drv;
    int32_t x = sdl_mouse_x;
    int32_t y = sdl_mouse_y;

    /* SDL window can be resized while LVGL keeps a fixed logical resolution.
     * Scale mouse coordinates to LVGL logical space to avoid out-of-range input.
     */
    int32_t win_w = 0;
    int32_t win_h = 0;
    if (sdl_window) {
        SDL_GetWindowSize(sdl_window, &win_w, &win_h);
    }

    lv_disp_t *disp = lv_disp_get_default();
    if (disp && win_w > 0 && win_h > 0) {
        const lv_coord_t hor_res = lv_disp_get_hor_res(disp);
        const lv_coord_t ver_res = lv_disp_get_ver_res(disp);
        if (hor_res > 0 && ver_res > 0) {
            x = (int32_t)(((int64_t)x * (int64_t)hor_res) / (int64_t)win_w);
            y = (int32_t)(((int64_t)y * (int64_t)ver_res) / (int64_t)win_h);
        }
    }

    if (disp) {
        const lv_coord_t max_x = lv_disp_get_hor_res(disp) - 1;
        const lv_coord_t max_y = lv_disp_get_ver_res(disp) - 1;
        if (x < 0) x = 0;
        if (y < 0) y = 0;
        if (x > max_x) x = max_x;
        if (y > max_y) y = max_y;
    }

    data->point.x = x;
    data->point.y = y;
    data->state = sdl_mouse_pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}
#endif

static JSValue tjs_lvgl_screen_active(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    lv_obj_t *screen = lv_scr_act();
    return tjs_lvgl_new_obj(ctx, screen);
}

/* ============================================================
 * Object Creation
 * ============================================================ */

static JSValue tjs_lvgl_obj_create(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    lv_obj_t *parent = NULL;
    if (argc > 0 && !JS_IsUndefined(argv[0]) && !JS_IsNull(argv[0])) {
        parent = tjs_lvgl_get_obj(ctx, argv[0]);
        if (!parent)
            return JS_EXCEPTION;
    }

    lv_obj_t *obj = lv_obj_create(parent);
    if (!obj) {
        return JS_ThrowInternalError(ctx, "Failed to create LVGL object");
    }

    return tjs_lvgl_new_obj(ctx, obj);
}

static JSValue tjs_lvgl_label_create(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    lv_obj_t *parent = NULL;
    if (argc > 0 && !JS_IsUndefined(argv[0]) && !JS_IsNull(argv[0])) {
        parent = tjs_lvgl_get_obj(ctx, argv[0]);
        if (!parent)
            return JS_EXCEPTION;
    }

    lv_obj_t *label = lv_label_create(parent);
    if (!label) {
        return JS_ThrowInternalError(ctx, "Failed to create label");
    }

    return tjs_lvgl_new_obj(ctx, label);
}

static JSValue tjs_lvgl_button_create(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    lv_obj_t *parent = NULL;
    if (argc > 0 && !JS_IsUndefined(argv[0]) && !JS_IsNull(argv[0])) {
        parent = tjs_lvgl_get_obj(ctx, argv[0]);
        if (!parent)
            return JS_EXCEPTION;
    }

    lv_obj_t *btn = lv_btn_create(parent);
    if (!btn) {
        return JS_ThrowInternalError(ctx, "Failed to create button");
    }

    return tjs_lvgl_new_obj(ctx, btn);
}

static JSValue tjs_lvgl_textarea_create(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    lv_obj_t *parent = NULL;
    if (argc > 0 && !JS_IsUndefined(argv[0]) && !JS_IsNull(argv[0])) {
        parent = tjs_lvgl_get_obj(ctx, argv[0]);
        if (!parent)
            return JS_EXCEPTION;
    }

    lv_obj_t *ta = lv_textarea_create(parent);
    if (!ta) {
        return JS_ThrowInternalError(ctx, "Failed to create textarea");
    }

    return tjs_lvgl_new_obj(ctx, ta);
}

static JSValue tjs_lvgl_img_create(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    lv_obj_t *parent = NULL;
    if (argc > 0 && !JS_IsUndefined(argv[0]) && !JS_IsNull(argv[0])) {
        parent = tjs_lvgl_get_obj(ctx, argv[0]);
        if (!parent)
            return JS_EXCEPTION;
    }

    lv_obj_t *img = lv_img_create(parent);
    if (!img) {
        return JS_ThrowInternalError(ctx, "Failed to create image");
    }

    return tjs_lvgl_new_obj(ctx, img);
}

static JSValue tjs_lvgl_slider_create(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    lv_obj_t *parent = NULL;
    if (argc > 0 && !JS_IsUndefined(argv[0]) && !JS_IsNull(argv[0])) {
        parent = tjs_lvgl_get_obj(ctx, argv[0]);
        if (!parent)
            return JS_EXCEPTION;
    }

    lv_obj_t *slider = lv_slider_create(parent);
    if (!slider) {
        return JS_ThrowInternalError(ctx, "Failed to create slider");
    }

    return tjs_lvgl_new_obj(ctx, slider);
}

static JSValue tjs_lvgl_switch_create(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    lv_obj_t *parent = NULL;
    if (argc > 0 && !JS_IsUndefined(argv[0]) && !JS_IsNull(argv[0])) {
        parent = tjs_lvgl_get_obj(ctx, argv[0]);
        if (!parent)
            return JS_EXCEPTION;
    }

    lv_obj_t *sw = lv_switch_create(parent);
    if (!sw) {
        return JS_ThrowInternalError(ctx, "Failed to create switch");
    }

    return tjs_lvgl_new_obj(ctx, sw);
}

static JSValue tjs_lvgl_checkbox_create(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    lv_obj_t *parent = NULL;
    if (argc > 0 && !JS_IsUndefined(argv[0]) && !JS_IsNull(argv[0])) {
        parent = tjs_lvgl_get_obj(ctx, argv[0]);
        if (!parent)
            return JS_EXCEPTION;
    }

    lv_obj_t *checkbox = lv_checkbox_create(parent);
    if (!checkbox) {
        return JS_ThrowInternalError(ctx, "Failed to create checkbox");
    }

    return tjs_lvgl_new_obj(ctx, checkbox);
}

static JSValue tjs_lvgl_dropdown_create(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    lv_obj_t *parent = NULL;
    if (argc > 0 && !JS_IsUndefined(argv[0]) && !JS_IsNull(argv[0])) {
        parent = tjs_lvgl_get_obj(ctx, argv[0]);
        if (!parent)
            return JS_EXCEPTION;
    }

    lv_obj_t *dropdown = lv_dropdown_create(parent);
    if (!dropdown) {
        return JS_ThrowInternalError(ctx, "Failed to create dropdown");
    }

    return tjs_lvgl_new_obj(ctx, dropdown);
}

static JSValue tjs_lvgl_calendar_create(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    lv_obj_t *parent = NULL;
    if (argc > 0 && !JS_IsUndefined(argv[0]) && !JS_IsNull(argv[0])) {
        parent = tjs_lvgl_get_obj(ctx, argv[0]);
        if (!parent)
            return JS_EXCEPTION;
    }

    lv_obj_t *calendar = lv_calendar_create(parent);
    if (!calendar) {
        return JS_ThrowInternalError(ctx, "Failed to create calendar");
    }

    return tjs_lvgl_new_obj(ctx, calendar);
}

static JSValue tjs_lvgl_tabview_create(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    (void)this_val;
#if LV_USE_TABVIEW != 0
    lv_obj_t *parent = NULL;
    if (argc > 0 && !JS_IsUndefined(argv[0]) && !JS_IsNull(argv[0])) {
        parent = tjs_lvgl_get_obj(ctx, argv[0]);
        if (!parent)
            return JS_EXCEPTION;
    }

    int32_t tab_pos = LV_DIR_TOP;
    int32_t tab_size = 56;
    if (argc > 1 && !JS_IsUndefined(argv[1]) && JS_ToInt32(ctx, &tab_pos, argv[1]) < 0) {
        return JS_EXCEPTION;
    }
    if (argc > 2 && !JS_IsUndefined(argv[2]) && JS_ToInt32(ctx, &tab_size, argv[2]) < 0) {
        return JS_EXCEPTION;
    }
    if (tab_pos == LV_DIR_NONE) {
        tab_pos = LV_DIR_TOP;
    }
    if (tab_size <= 0) {
        tab_size = 56;
    }

    lv_obj_t *tabview = lv_tabview_create(parent, (lv_dir_t)tab_pos, (lv_coord_t)tab_size);
    if (!tabview) {
        return JS_ThrowInternalError(ctx, "Failed to create tabview");
    }

    lv_group_t *group = lv_group_get_default();
    if (group) {
        lv_group_add_obj(group, tabview);
        lv_obj_t *btns = lv_tabview_get_tab_btns(tabview);
        if (btns) {
            lv_group_add_obj(group, btns);
        }
    }
    lv_obj_clear_flag(tabview, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_add_flag(tabview, LV_OBJ_FLAG_EVENT_BUBBLE | LV_OBJ_FLAG_CLICK_FOCUSABLE);

    return tjs_lvgl_new_obj(ctx, tabview);
#else
    (void)argc;
    (void)argv;
    return JS_ThrowInternalError(ctx, "LV_USE_TABVIEW is disabled");
#endif
}

static JSValue tjs_lvgl_tabview_add_tab(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    (void)this_val;
#if LV_USE_TABVIEW != 0
    if (argc < 3) {
        return JS_ThrowTypeError(ctx, "Expected 3 arguments: tabview, name, child");
    }
    lv_obj_t *tabview = tjs_lvgl_get_obj(ctx, argv[0]);
    lv_obj_t *child = tjs_lvgl_get_obj(ctx, argv[2]);
    if (!tabview || !child) {
        return JS_EXCEPTION;
    }

    const char *name = JS_ToCString(ctx, argv[1]);
    if (!name) {
        return JS_EXCEPTION;
    }
    lv_obj_t *page = lv_tabview_add_tab(tabview, name);
    JS_FreeCString(ctx, name);
    if (!page) {
        return JS_ThrowInternalError(ctx, "Failed to add tabview tab");
    }

    lv_obj_set_parent(child, page);
    lv_obj_set_size(child, LV_PCT(100), LV_PCT(100));
    return tjs_lvgl_new_obj(ctx, page);
#else
    (void)argc;
    (void)argv;
    return JS_ThrowInternalError(ctx, "LV_USE_TABVIEW is disabled");
#endif
}

static JSValue tjs_lvgl_tabview_set_active(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    (void)this_val;
#if LV_USE_TABVIEW != 0
    if (argc < 2) {
        return JS_ThrowTypeError(ctx, "Expected 2 arguments: tabview, index");
    }
    lv_obj_t *tabview = tjs_lvgl_get_obj(ctx, argv[0]);
    if (!tabview) {
        return JS_EXCEPTION;
    }
    int32_t index = 0;
    if (JS_ToInt32(ctx, &index, argv[1]) < 0) {
        return JS_EXCEPTION;
    }
    if (index < 0) {
        index = 0;
    }
    lv_tabview_set_act(tabview, (uint32_t)index, LV_ANIM_OFF);
    return JS_UNDEFINED;
#else
    (void)argc;
    (void)argv;
    return JS_ThrowInternalError(ctx, "LV_USE_TABVIEW is disabled");
#endif
}

#if LV_USE_CHART != 0
static void tjs_lvgl_chart_clear_axis_labels(TjsLVGLStyleState *state, uint32_t axis) {
    if (!state || axis >= countof(state->chart_axis_labels)) {
        return;
    }
    if (state->chart_axis_labels[axis]) {
        for (uint32_t i = 0; i < state->chart_axis_label_counts[axis]; i++) {
            free(state->chart_axis_labels[axis][i]);
        }
        free(state->chart_axis_labels[axis]);
    }
    state->chart_axis_labels[axis] = NULL;
    state->chart_axis_label_counts[axis] = 0;
}

static void tjs_lvgl_chart_draw_event_cb(lv_event_t *e) {
    lv_obj_draw_part_dsc_t *dsc = lv_event_get_draw_part_dsc(e);
    if (!dsc || !lv_obj_draw_part_check_type(dsc, &lv_chart_class, LV_CHART_DRAW_PART_TICK_LABEL)) {
        return;
    }
    if (!dsc->text || dsc->text_length <= 0 || dsc->value < 0) {
        return;
    }

    lv_obj_t *chart = lv_event_get_target(e);
    TjsLVGLStyleState *state = tjs_lvgl_style_state_get(chart, false);
    uint32_t axis = (uint32_t)dsc->id;
    uint32_t value = (uint32_t)dsc->value;
    if (!state || axis >= countof(state->chart_axis_labels) ||
        value >= state->chart_axis_label_counts[axis]) {
        return;
    }

    const char *label = state->chart_axis_labels[axis][value];
    if (label) {
        lv_snprintf(dsc->text, dsc->text_length, "%s", label);
    }
}

static JSValue tjs_lvgl_chart_create(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    lv_obj_t *parent = NULL;
    if (argc > 0 && !JS_IsUndefined(argv[0]) && !JS_IsNull(argv[0])) {
        parent = tjs_lvgl_get_obj(ctx, argv[0]);
        if (!parent)
            return JS_EXCEPTION;
    }

    lv_obj_t *chart = lv_chart_create(parent);
    if (!chart) {
        return JS_ThrowInternalError(ctx, "Failed to create chart");
    }
    lv_obj_add_event_cb(chart, tjs_lvgl_chart_draw_event_cb, LV_EVENT_DRAW_PART_BEGIN, NULL);

    return tjs_lvgl_new_obj(ctx, chart);
}

static lv_color_t tjs_lvgl_chart_color_from_js(JSContext *ctx, lv_obj_t *chart, JSValueConst value) {
    int32_t color = -1;
    if (!tjs_lvgl_js_to_i32(ctx, value, &color) || color == -1) {
        return lv_theme_get_color_primary(chart);
    }
    return lv_color_hex((uint32_t)color);
}

static bool tjs_lvgl_chart_sync_series(JSContext *ctx, lv_obj_t *chart, JSValueConst series_arr, bool scatter_mode, lv_chart_axis_t default_axis) {
    if (!chart || !JS_IsArray(series_arr)) {
        return false;
    }
    TjsLVGLStyleState *state = tjs_lvgl_style_state_get(chart, true);
    if (!state) {
        return false;
    }

    JSValue len_val = JS_GetPropertyStr(ctx, series_arr, "length");
    int32_t target_count_i32 = 0;
    if (JS_ToInt32(ctx, &target_count_i32, len_val) < 0) {
        JS_FreeValue(ctx, len_val);
        return false;
    }
    JS_FreeValue(ctx, len_val);
    if (target_count_i32 < 0) {
        target_count_i32 = 0;
    }
    if (target_count_i32 > 16) {
        target_count_i32 = 16;
    }
    uint32_t target_count = (uint32_t)target_count_i32;

    while (state->chart_series_count > 0) {
        uint32_t idx = state->chart_series_count - 1;
        lv_chart_series_t *ser = state->chart_series[idx];
        if (ser) {
            lv_chart_remove_series(chart, ser);
        }
        state->chart_series[idx] = NULL;
        state->chart_series_count--;
    }

    uint16_t point_count = lv_chart_get_point_count(chart);
    for (uint32_t s = 0; s < target_count; s++) {
        JSValue item = JS_GetPropertyUint32(ctx, series_arr, s);
        lv_chart_axis_t axis = default_axis;
        lv_color_t color = lv_theme_get_color_primary(chart);
        if (JS_IsObject(item)) {
            JSValue axis_val = JS_GetPropertyStr(ctx, item, "axis");
            int32_t axis_i32 = (int32_t)default_axis;
            if (tjs_lvgl_js_to_i32(ctx, axis_val, &axis_i32)) {
                axis = (lv_chart_axis_t)axis_i32;
            }
            JS_FreeValue(ctx, axis_val);
            JSValue color_val = JS_GetPropertyStr(ctx, item, "color");
            color = tjs_lvgl_chart_color_from_js(ctx, chart, color_val);
            JS_FreeValue(ctx, color_val);
        }
        lv_chart_series_t *ser = lv_chart_add_series(chart, color, axis);
        if (!ser) {
            JS_FreeValue(ctx, item);
            return false;
        }
        state->chart_series[state->chart_series_count++] = ser;

        JSValue data_val = JS_GetPropertyStr(ctx, item, "data");
        if (JS_IsArray(data_val)) {
            JSValue data_len_val = JS_GetPropertyStr(ctx, data_val, "length");
            int32_t data_len = 0;
            if (JS_ToInt32(ctx, &data_len, data_len_val) < 0) {
                data_len = 0;
            }
            JS_FreeValue(ctx, data_len_val);
            if (data_len < 0) {
                data_len = 0;
            }
            if (scatter_mode) {
                for (uint16_t i = 0; i < point_count; i++) {
                    int32_t x = 0;
                    int32_t y = 0;
                    uint32_t x_idx = (uint32_t)i * 2;
                    uint32_t y_idx = x_idx + 1;
                    if ((int32_t)y_idx < data_len) {
                        JSValue x_val = JS_GetPropertyUint32(ctx, data_val, x_idx);
                        JSValue y_val = JS_GetPropertyUint32(ctx, data_val, y_idx);
                        tjs_lvgl_js_to_i32(ctx, x_val, &x);
                        tjs_lvgl_js_to_i32(ctx, y_val, &y);
                        JS_FreeValue(ctx, x_val);
                        JS_FreeValue(ctx, y_val);
                    }
                    if (ser->x_points) ser->x_points[i] = (lv_coord_t)x;
                    if (ser->y_points) ser->y_points[i] = (lv_coord_t)y;
                }
            } else {
                for (uint16_t i = 0; i < point_count; i++) {
                    int32_t y = 0;
                    if ((int32_t)i < data_len) {
                        JSValue y_val = JS_GetPropertyUint32(ctx, data_val, i);
                        tjs_lvgl_js_to_i32(ctx, y_val, &y);
                        JS_FreeValue(ctx, y_val);
                    }
                    if (ser->y_points) ser->y_points[i] = (lv_coord_t)y;
                }
            }
        } else {
            for (uint16_t i = 0; i < point_count; i++) {
                if (ser->x_points) ser->x_points[i] = 0;
                if (ser->y_points) ser->y_points[i] = 0;
            }
        }
        JS_FreeValue(ctx, data_val);
        JS_FreeValue(ctx, item);
    }
    lv_chart_refresh(chart);
    return true;
}

static JSValue tjs_lvgl_chart_set_type(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    if (argc < 2) return JS_ThrowTypeError(ctx, "Expected 2 arguments: ptr, type");
    lv_obj_t *chart = tjs_lvgl_get_obj(ctx, argv[0]);
    if (!chart) return JS_EXCEPTION;
    int32_t type = 0;
    if (JS_ToInt32(ctx, &type, argv[1]) < 0) return JS_EXCEPTION;
    lv_chart_set_type(chart, (lv_chart_type_t)type);
    return JS_UNDEFINED;
}

static JSValue tjs_lvgl_chart_set_point_num(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    if (argc < 2) return JS_ThrowTypeError(ctx, "Expected 2 arguments: ptr, pointNum");
    lv_obj_t *chart = tjs_lvgl_get_obj(ctx, argv[0]);
    if (!chart) return JS_EXCEPTION;
    int32_t point_num = 0;
    if (JS_ToInt32(ctx, &point_num, argv[1]) < 0) return JS_EXCEPTION;
    if (point_num < 0) point_num = 0;
    lv_chart_set_point_count(chart, (uint16_t)point_num);
    return JS_UNDEFINED;
}

static JSValue tjs_lvgl_chart_set_div_line_count(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    if (argc < 3) return JS_ThrowTypeError(ctx, "Expected 3 arguments: ptr, hdiv, vdiv");
    lv_obj_t *chart = tjs_lvgl_get_obj(ctx, argv[0]);
    if (!chart) return JS_EXCEPTION;
    int32_t hdiv = 0;
    int32_t vdiv = 0;
    if (JS_ToInt32(ctx, &hdiv, argv[1]) < 0 || JS_ToInt32(ctx, &vdiv, argv[2]) < 0) return JS_EXCEPTION;
    if (hdiv < 0) hdiv = 0;
    if (vdiv < 0) vdiv = 0;
    lv_chart_set_div_line_count(chart, (uint8_t)hdiv, (uint8_t)vdiv);
    return JS_UNDEFINED;
}

static JSValue tjs_lvgl_chart_set_range(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    if (argc < 4) return JS_ThrowTypeError(ctx, "Expected 4 arguments: ptr, axis, min, max");
    lv_obj_t *chart = tjs_lvgl_get_obj(ctx, argv[0]);
    if (!chart) return JS_EXCEPTION;
    int32_t axis = 0, min = 0, max = 0;
    if (JS_ToInt32(ctx, &axis, argv[1]) < 0 || JS_ToInt32(ctx, &min, argv[2]) < 0 || JS_ToInt32(ctx, &max, argv[3]) < 0) {
        return JS_EXCEPTION;
    }
    lv_chart_set_range(chart, (lv_chart_axis_t)axis, (lv_coord_t)min, (lv_coord_t)max);
    return JS_UNDEFINED;
}

static JSValue tjs_lvgl_chart_set_axis_tick(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    if (argc < 8) return JS_ThrowTypeError(ctx, "Expected 8 arguments: ptr, axis, majorLen, minorLen, majorCnt, minorCnt, labelEn, drawSize");
    lv_obj_t *chart = tjs_lvgl_get_obj(ctx, argv[0]);
    if (!chart) return JS_EXCEPTION;
    int32_t axis = 0, major_len = 0, minor_len = 0, major_cnt = 0, minor_cnt = 0, draw_size = 0;
    if (JS_ToInt32(ctx, &axis, argv[1]) < 0 ||
        JS_ToInt32(ctx, &major_len, argv[2]) < 0 ||
        JS_ToInt32(ctx, &minor_len, argv[3]) < 0 ||
        JS_ToInt32(ctx, &major_cnt, argv[4]) < 0 ||
        JS_ToInt32(ctx, &minor_cnt, argv[5]) < 0 ||
        JS_ToInt32(ctx, &draw_size, argv[7]) < 0) {
        return JS_EXCEPTION;
    }
    int label_en = JS_ToBool(ctx, argv[6]);
    if (label_en < 0) return JS_EXCEPTION;
    if (major_len < 0) major_len = 0;
    if (minor_len < 0) minor_len = 0;
    if (major_cnt < 0) major_cnt = 0;
    if (minor_cnt < 0) minor_cnt = 0;
    if (draw_size < 0) draw_size = 0;
    lv_chart_set_axis_tick(
        chart,
        (lv_chart_axis_t)axis,
        (lv_coord_t)major_len,
        (lv_coord_t)minor_len,
        (lv_coord_t)major_cnt,
        (lv_coord_t)minor_cnt,
        label_en ? true : false,
        (lv_coord_t)draw_size
    );
    return JS_UNDEFINED;
}

static JSValue tjs_lvgl_chart_set_axis_labels(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    if (argc < 3) return JS_ThrowTypeError(ctx, "Expected 3 arguments: ptr, axis, labels");
    lv_obj_t *chart = tjs_lvgl_get_obj(ctx, argv[0]);
    if (!chart) return JS_EXCEPTION;
    int32_t axis_i32 = 0;
    if (JS_ToInt32(ctx, &axis_i32, argv[1]) < 0) return JS_EXCEPTION;
    if (axis_i32 < 0 || axis_i32 >= 5) {
        return JS_ThrowRangeError(ctx, "Invalid chart axis");
    }
    uint32_t axis = (uint32_t)axis_i32;
    TjsLVGLStyleState *state = tjs_lvgl_style_state_get(chart, true);
    if (!state) {
        return JS_ThrowInternalError(ctx, "Failed to allocate chart state");
    }
    tjs_lvgl_chart_clear_axis_labels(state, axis);

    if (!JS_IsArray(argv[2])) {
        lv_obj_invalidate(chart);
        return JS_UNDEFINED;
    }

    JSValue len_val = JS_GetPropertyStr(ctx, argv[2], "length");
    int32_t len_i32 = 0;
    if (JS_ToInt32(ctx, &len_i32, len_val) < 0) {
        JS_FreeValue(ctx, len_val);
        return JS_EXCEPTION;
    }
    JS_FreeValue(ctx, len_val);
    if (len_i32 < 0) {
        len_i32 = 0;
    }
    if (len_i32 > 256) {
        len_i32 = 256;
    }
    uint32_t count = (uint32_t)len_i32;
    if (count == 0) {
        lv_obj_invalidate(chart);
        return JS_UNDEFINED;
    }

    char **labels = (char **)calloc(count, sizeof(char *));
    if (!labels) {
        return JS_ThrowOutOfMemory(ctx);
    }
    for (uint32_t i = 0; i < count; i++) {
        JSValue item = JS_GetPropertyUint32(ctx, argv[2], i);
        const char *str = JS_ToCString(ctx, item);
        if (str) {
            labels[i] = tjs_lvgl_strdup(str);
            JS_FreeCString(ctx, str);
            if (!labels[i]) {
                JS_FreeValue(ctx, item);
                for (uint32_t j = 0; j < i; j++) {
                    free(labels[j]);
                }
                free(labels);
                return JS_ThrowOutOfMemory(ctx);
            }
        }
        JS_FreeValue(ctx, item);
    }
    state->chart_axis_labels[axis] = labels;
    state->chart_axis_label_counts[axis] = count;
    lv_obj_invalidate(chart);
    return JS_UNDEFINED;
}

static JSValue tjs_lvgl_chart_set_left_axis_data(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    if (argc < 2) return JS_ThrowTypeError(ctx, "Expected 2 arguments: ptr, series");
    lv_obj_t *chart = tjs_lvgl_get_obj(ctx, argv[0]);
    if (!chart) return JS_EXCEPTION;
    if (!tjs_lvgl_chart_sync_series(ctx, chart, argv[1], false, LV_CHART_AXIS_PRIMARY_Y)) {
        return JS_EXCEPTION;
    }
    return JS_UNDEFINED;
}

static JSValue tjs_lvgl_chart_set_right_axis_data(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    if (argc < 2) return JS_ThrowTypeError(ctx, "Expected 2 arguments: ptr, series");
    lv_obj_t *chart = tjs_lvgl_get_obj(ctx, argv[0]);
    if (!chart) return JS_EXCEPTION;
    if (!tjs_lvgl_chart_sync_series(ctx, chart, argv[1], false, LV_CHART_AXIS_SECONDARY_Y)) {
        return JS_EXCEPTION;
    }
    return JS_UNDEFINED;
}

static JSValue tjs_lvgl_chart_set_all_series(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    if (argc < 2) return JS_ThrowTypeError(ctx, "Expected 2 arguments: ptr, series");
    lv_obj_t *chart = tjs_lvgl_get_obj(ctx, argv[0]);
    if (!chart) return JS_EXCEPTION;
    if (!tjs_lvgl_chart_sync_series(ctx, chart, argv[1], false, LV_CHART_AXIS_PRIMARY_Y)) {
        return JS_EXCEPTION;
    }
    return JS_UNDEFINED;
}

static JSValue tjs_lvgl_chart_set_scatter_data(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    if (argc < 2) return JS_ThrowTypeError(ctx, "Expected 2 arguments: ptr, series");
    lv_obj_t *chart = tjs_lvgl_get_obj(ctx, argv[0]);
    if (!chart) return JS_EXCEPTION;
    if (!tjs_lvgl_chart_sync_series(ctx, chart, argv[1], true, LV_CHART_AXIS_PRIMARY_Y)) {
        return JS_EXCEPTION;
    }
    return JS_UNDEFINED;
}

static JSValue tjs_lvgl_chart_get_type(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    if (argc < 1) return JS_ThrowTypeError(ctx, "Expected 1 argument: ptr");
    lv_obj_t *chart = tjs_lvgl_get_obj(ctx, argv[0]);
    if (!chart) return JS_EXCEPTION;
    return JS_NewInt32(ctx, (int32_t)lv_chart_get_type(chart));
}

static JSValue tjs_lvgl_chart_get_point_num(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    if (argc < 1) return JS_ThrowTypeError(ctx, "Expected 1 argument: ptr");
    lv_obj_t *chart = tjs_lvgl_get_obj(ctx, argv[0]);
    if (!chart) return JS_EXCEPTION;
    return JS_NewInt32(ctx, (int32_t)lv_chart_get_point_count(chart));
}

static JSValue tjs_lvgl_chart_get_series_count(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    if (argc < 1) return JS_ThrowTypeError(ctx, "Expected 1 argument: ptr");
    lv_obj_t *chart = tjs_lvgl_get_obj(ctx, argv[0]);
    if (!chart) return JS_EXCEPTION;
    int32_t count = 0;
    lv_chart_series_t *ser = NULL;
    while ((ser = lv_chart_get_series_next(chart, ser)) != NULL) {
        count++;
    }
    return JS_NewInt32(ctx, count);
}

static JSValue tjs_lvgl_chart_debug_get_state(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    if (argc < 1) return JS_ThrowTypeError(ctx, "Expected 1 argument: ptr");
    lv_obj_t *chart_obj = tjs_lvgl_get_obj(ctx, argv[0]);
    if (!chart_obj) return JS_EXCEPTION;
    lv_chart_t *chart = (lv_chart_t *)chart_obj;
    JSValue out = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, out, "type", JS_NewInt32(ctx, (int32_t)chart->type));
    JS_SetPropertyStr(ctx, out, "pointCount", JS_NewInt32(ctx, (int32_t)chart->point_cnt));
    JS_SetPropertyStr(ctx, out, "hdiv", JS_NewInt32(ctx, (int32_t)chart->hdiv_cnt));
    JS_SetPropertyStr(ctx, out, "vdiv", JS_NewInt32(ctx, (int32_t)chart->vdiv_cnt));

    JSValue ranges = JS_NewArray(ctx);
    for (uint32_t i = 0; i < 4; i++) {
        JSValue range = JS_NewArray(ctx);
        switch (i) {
            case 0:
                JS_SetPropertyUint32(ctx, range, 0, JS_NewInt32(ctx, (int32_t)chart->ymin[0]));
                JS_SetPropertyUint32(ctx, range, 1, JS_NewInt32(ctx, (int32_t)chart->ymax[0]));
                break;
            case 1:
                JS_SetPropertyUint32(ctx, range, 0, JS_NewInt32(ctx, (int32_t)chart->ymin[1]));
                JS_SetPropertyUint32(ctx, range, 1, JS_NewInt32(ctx, (int32_t)chart->ymax[1]));
                break;
            case 2:
                JS_SetPropertyUint32(ctx, range, 0, JS_NewInt32(ctx, (int32_t)chart->xmin[0]));
                JS_SetPropertyUint32(ctx, range, 1, JS_NewInt32(ctx, (int32_t)chart->xmax[0]));
                break;
            case 3:
                JS_SetPropertyUint32(ctx, range, 0, JS_NewInt32(ctx, (int32_t)chart->xmin[1]));
                JS_SetPropertyUint32(ctx, range, 1, JS_NewInt32(ctx, (int32_t)chart->xmax[1]));
                break;
        }
        JS_SetPropertyUint32(ctx, ranges, i, range);
    }
    JS_SetPropertyStr(ctx, out, "ranges", ranges);

    JSValue ticks = JS_NewArray(ctx);
    for (uint32_t i = 0; i < 4; i++) {
        JSValue t = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, t, "majorLen", JS_NewInt32(ctx, (int32_t)chart->tick[i].major_len));
        JS_SetPropertyStr(ctx, t, "minorLen", JS_NewInt32(ctx, (int32_t)chart->tick[i].minor_len));
        JS_SetPropertyStr(ctx, t, "majorCnt", JS_NewInt32(ctx, (int32_t)chart->tick[i].major_cnt));
        JS_SetPropertyStr(ctx, t, "minorCnt", JS_NewInt32(ctx, (int32_t)chart->tick[i].minor_cnt));
        JS_SetPropertyStr(ctx, t, "drawSize", JS_NewInt32(ctx, (int32_t)chart->tick[i].draw_size));
        JS_SetPropertyStr(ctx, t, "labelEn", JS_NewBool(ctx, chart->tick[i].label_en ? 1 : 0));
        JS_SetPropertyUint32(ctx, ticks, i, t);
    }
    JS_SetPropertyStr(ctx, out, "ticks", ticks);

    TjsLVGLStyleState *state = tjs_lvgl_style_state_get(chart_obj, false);
    JSValue labels = JS_NewArray(ctx);
    for (uint32_t axis = 0; axis < 5; axis++) {
        JSValue axis_labels = JS_NewArray(ctx);
        if (state && state->chart_axis_labels[axis]) {
            for (uint32_t i = 0; i < state->chart_axis_label_counts[axis]; i++) {
                JS_SetPropertyUint32(ctx, axis_labels, i,
                    JS_NewString(ctx, state->chart_axis_labels[axis][i] ? state->chart_axis_labels[axis][i] : ""));
            }
        }
        JS_SetPropertyUint32(ctx, labels, axis, axis_labels);
    }
    JS_SetPropertyStr(ctx, out, "axisLabels", labels);
    return out;
}

static JSValue tjs_lvgl_chart_debug_get_series(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    if (argc < 1) return JS_ThrowTypeError(ctx, "Expected 1 argument: ptr");
    lv_obj_t *chart_obj = tjs_lvgl_get_obj(ctx, argv[0]);
    if (!chart_obj) return JS_EXCEPTION;
    lv_chart_t *chart = (lv_chart_t *)chart_obj;
    uint16_t point_count = chart->point_cnt;
    JSValue arr = JS_NewArray(ctx);
    uint32_t series_idx = 0;
    lv_chart_series_t *ser = NULL;
    while ((ser = lv_chart_get_series_next(chart_obj, ser)) != NULL) {
        JSValue one = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, one, "color", JS_NewUint32(ctx, (uint32_t)lv_color_to32(ser->color)));
        JS_SetPropertyStr(ctx, one, "xAxisSec", JS_NewBool(ctx, ser->x_axis_sec ? 1 : 0));
        JS_SetPropertyStr(ctx, one, "yAxisSec", JS_NewBool(ctx, ser->y_axis_sec ? 1 : 0));
        JSValue data = JS_NewArray(ctx);
        for (uint16_t i = 0; i < point_count; i++) {
            JS_SetPropertyUint32(ctx, data, i, JS_NewInt32(ctx, (int32_t)(ser->y_points ? ser->y_points[i] : 0)));
        }
        JS_SetPropertyStr(ctx, one, "data", data);

        JSValue data2 = JS_NewArray(ctx);
        for (uint16_t i = 0; i < point_count; i++) {
            uint32_t base = (uint32_t)i * 2;
            JS_SetPropertyUint32(ctx, data2, base, JS_NewInt32(ctx, (int32_t)(ser->x_points ? ser->x_points[i] : 0)));
            JS_SetPropertyUint32(ctx, data2, base + 1, JS_NewInt32(ctx, (int32_t)(ser->y_points ? ser->y_points[i] : 0)));
        }
        JS_SetPropertyStr(ctx, one, "data2", data2);

        JS_SetPropertyUint32(ctx, arr, series_idx++, one);
    }
    return arr;
}
#else
static JSValue tjs_lvgl_chart_create(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    (void)this_val; (void)argc; (void)argv;
    return JS_ThrowTypeError(ctx, "LVGL chart is disabled");
}
static JSValue tjs_lvgl_chart_set_type(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    (void)this_val; (void)argc; (void)argv;
    return JS_ThrowTypeError(ctx, "LVGL chart is disabled");
}
static JSValue tjs_lvgl_chart_set_point_num(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    (void)this_val; (void)argc; (void)argv;
    return JS_ThrowTypeError(ctx, "LVGL chart is disabled");
}
static JSValue tjs_lvgl_chart_set_div_line_count(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    (void)this_val; (void)argc; (void)argv;
    return JS_ThrowTypeError(ctx, "LVGL chart is disabled");
}
static JSValue tjs_lvgl_chart_set_range(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    (void)this_val; (void)argc; (void)argv;
    return JS_ThrowTypeError(ctx, "LVGL chart is disabled");
}
static JSValue tjs_lvgl_chart_set_left_axis_data(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    (void)this_val; (void)argc; (void)argv;
    return JS_ThrowTypeError(ctx, "LVGL chart is disabled");
}
static JSValue tjs_lvgl_chart_set_right_axis_data(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    (void)this_val; (void)argc; (void)argv;
    return JS_ThrowTypeError(ctx, "LVGL chart is disabled");
}
static JSValue tjs_lvgl_chart_set_all_series(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    (void)this_val; (void)argc; (void)argv;
    return JS_ThrowTypeError(ctx, "LVGL chart is disabled");
}
static JSValue tjs_lvgl_chart_set_axis_tick(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    (void)this_val; (void)argc; (void)argv;
    return JS_ThrowTypeError(ctx, "LVGL chart is disabled");
}
static JSValue tjs_lvgl_chart_set_axis_labels(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    (void)this_val; (void)argc; (void)argv;
    return JS_ThrowTypeError(ctx, "LVGL chart is disabled");
}
static JSValue tjs_lvgl_chart_set_scatter_data(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    (void)this_val; (void)argc; (void)argv;
    return JS_ThrowTypeError(ctx, "LVGL chart is disabled");
}
static JSValue tjs_lvgl_chart_get_type(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    (void)this_val; (void)argc; (void)argv;
    return JS_NewInt32(ctx, 0);
}
static JSValue tjs_lvgl_chart_get_point_num(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    (void)this_val; (void)argc; (void)argv;
    return JS_NewInt32(ctx, 0);
}
static JSValue tjs_lvgl_chart_get_series_count(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    (void)this_val; (void)argc; (void)argv;
    return JS_NewInt32(ctx, 0);
}
static JSValue tjs_lvgl_chart_debug_get_state(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    (void)this_val; (void)argc; (void)argv;
    return JS_NULL;
}
static JSValue tjs_lvgl_chart_debug_get_series(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    (void)this_val; (void)argc; (void)argv;
    return JS_NULL;
}
#endif

/* ============================================================
 * Object Methods (take ptr as argv[0])
 * ============================================================ */

static JSValue tjs_lvgl_obj_set_pos(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    if (argc < 3)
        return JS_ThrowTypeError(ctx, "Expected 3 arguments: ptr, x, y");

    lv_obj_t *obj = tjs_lvgl_get_obj(ctx, argv[0]);
    if (!obj)
        return JS_EXCEPTION;

    int32_t x, y;
    if (JS_ToInt32(ctx, &x, argv[1]) < 0 || JS_ToInt32(ctx, &y, argv[2]) < 0)
        return JS_EXCEPTION;

    lv_obj_set_pos(obj, x, y);
    return JS_UNDEFINED;
}

static JSValue tjs_lvgl_obj_set_size(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    if (argc < 3)
        return JS_ThrowTypeError(ctx, "Expected 3 arguments: ptr, w, h");

    lv_obj_t *obj = tjs_lvgl_get_obj(ctx, argv[0]);
    if (!obj)
        return JS_EXCEPTION;

    int32_t w, h;
    if (JS_ToInt32(ctx, &w, argv[1]) < 0 || JS_ToInt32(ctx, &h, argv[2]) < 0)
        return JS_EXCEPTION;

    lv_obj_set_size(obj, w, h);
    return JS_UNDEFINED;
}

static JSValue tjs_lvgl_obj_delete(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    if (argc < 1)
        return JS_ThrowTypeError(ctx, "Expected 1 argument: ptr");

    lv_obj_t *obj = tjs_lvgl_get_obj(ctx, argv[0]);
    if (!obj)
        return JS_EXCEPTION;

    tjs_lvgl_style_state_drop_tree(obj);
    lv_obj_del(obj);
    return JS_UNDEFINED;
}

static JSValue tjs_lvgl_obj_center(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    if (argc < 1)
        return JS_ThrowTypeError(ctx, "Expected 1 argument: ptr");

    lv_obj_t *obj = tjs_lvgl_get_obj(ctx, argv[0]);
    if (!obj)
        return JS_EXCEPTION;

    lv_obj_center(obj);
    return JS_UNDEFINED;
}

static JSValue tjs_lvgl_obj_append_child(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    if (argc < 2)
        return JS_ThrowTypeError(ctx, "Expected 2 arguments: parent, child");

    lv_obj_t *parent = tjs_lvgl_get_obj(ctx, argv[0]);
    lv_obj_t *child = tjs_lvgl_get_obj(ctx, argv[1]);
    if (!parent || !child)
        return JS_EXCEPTION;

    if (lv_obj_get_parent(child) == NULL) {
        return JS_UNDEFINED;
    }
    lv_obj_set_parent(child, parent);
    return JS_UNDEFINED;
}

static JSValue tjs_lvgl_obj_insert_before(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    if (argc < 3)
        return JS_ThrowTypeError(ctx, "Expected 3 arguments: parent, child, beforeChild");

    lv_obj_t *parent = tjs_lvgl_get_obj(ctx, argv[0]);
    lv_obj_t *child = tjs_lvgl_get_obj(ctx, argv[1]);
    lv_obj_t *before = tjs_lvgl_get_obj(ctx, argv[2]);
    if (!parent || !child || !before)
        return JS_EXCEPTION;

    if (lv_obj_get_parent(child) == NULL) {
        return JS_UNDEFINED;
    }
    lv_obj_set_parent(child, parent);
    lv_obj_move_to_index(child, lv_obj_get_index(before));
    return JS_UNDEFINED;
}

static JSValue tjs_lvgl_obj_move_to_front(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    (void)this_val;
    if (argc < 1)
        return JS_ThrowTypeError(ctx, "Expected 1 argument: ptr");

    lv_obj_t *obj = tjs_lvgl_get_obj(ctx, argv[0]);
    if (!obj)
        return JS_EXCEPTION;

    lv_obj_t *parent = lv_obj_get_parent(obj);
    if (parent) {
        lv_obj_move_to_index(obj, (int32_t)lv_obj_get_child_cnt(parent) - 1);
    }
    return JS_UNDEFINED;
}

static JSValue tjs_lvgl_obj_move_to_background(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    (void)this_val;
    if (argc < 1)
        return JS_ThrowTypeError(ctx, "Expected 1 argument: ptr");

    lv_obj_t *obj = tjs_lvgl_get_obj(ctx, argv[0]);
    if (!obj)
        return JS_EXCEPTION;

    if (lv_obj_get_parent(obj)) {
        lv_obj_move_to_index(obj, 0);
    }
    return JS_UNDEFINED;
}

static JSValue tjs_lvgl_obj_remove_child(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    if (argc < 2)
        return JS_ThrowTypeError(ctx, "Expected 2 arguments: parent, child");

    lv_obj_t *parent = tjs_lvgl_get_obj(ctx, argv[0]);
    lv_obj_t *child = tjs_lvgl_get_obj(ctx, argv[1]);
    if (!parent || !child)
        return JS_EXCEPTION;

    if (lv_obj_get_parent(child) == parent) {
        lv_obj_del_async(child);
    }
    return JS_UNDEFINED;
}

static JSValue tjs_lvgl_obj_align(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    if (argc < 4)
        return JS_ThrowTypeError(ctx, "Expected 4 arguments: ptr, align, x, y");

    lv_obj_t *obj = tjs_lvgl_get_obj(ctx, argv[0]);
    if (!obj)
        return JS_EXCEPTION;

    int32_t align_type = 0;
    int32_t x = 0;
    int32_t y = 0;
    if (JS_ToInt32(ctx, &align_type, argv[1]) < 0 ||
        JS_ToInt32(ctx, &x, argv[2]) < 0 ||
        JS_ToInt32(ctx, &y, argv[3]) < 0) {
        return JS_EXCEPTION;
    }

    lv_obj_align(obj, (lv_align_t)align_type, x, y);
    return JS_UNDEFINED;
}

static JSValue tjs_lvgl_obj_align_to(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    if (argc < 5)
        return JS_ThrowTypeError(ctx, "Expected 5 arguments: ptr, base, align, x, y");

    lv_obj_t *obj = tjs_lvgl_get_obj(ctx, argv[0]);
    lv_obj_t *base = tjs_lvgl_get_obj(ctx, argv[1]);
    if (!obj || !base)
        return JS_EXCEPTION;

    int32_t align_type = 0;
    int32_t x = 0;
    int32_t y = 0;
    if (JS_ToInt32(ctx, &align_type, argv[2]) < 0 ||
        JS_ToInt32(ctx, &x, argv[3]) < 0 ||
        JS_ToInt32(ctx, &y, argv[4]) < 0) {
        return JS_EXCEPTION;
    }

    lv_obj_align_to(obj, base, (lv_align_t)align_type, x, y);
    return JS_UNDEFINED;
}

static JSValue tjs_lvgl_set_event_bubble(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    if (argc < 2) {
        return JS_ThrowTypeError(ctx, "Expected 2 arguments: ptr, enabled");
    }

    lv_obj_t *obj = tjs_lvgl_get_obj(ctx, argv[0]);
    if (!obj) {
        return JS_EXCEPTION;
    }

    int enabled = JS_ToBool(ctx, argv[1]);
    if (enabled < 0) {
        return JS_EXCEPTION;
    }

    if (enabled) {
        lv_obj_add_flag(obj, LV_OBJ_FLAG_EVENT_BUBBLE);
    } else {
        lv_obj_clear_flag(obj, LV_OBJ_FLAG_EVENT_BUBBLE);
    }

    return JS_UNDEFINED;
}

/* ============================================================
 * Label Methods
 * ============================================================ */

static JSValue tjs_lvgl_label_set_text(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    if (argc < 2)
        return JS_ThrowTypeError(ctx, "Expected 2 arguments: ptr, text");

    lv_obj_t *label = tjs_lvgl_get_obj(ctx, argv[0]);
    if (!label)
        return JS_EXCEPTION;

    const char *text = JS_ToCString(ctx, argv[1]);
    if (!text)
        return JS_EXCEPTION;

    lv_label_set_text(label, text);
    JS_FreeCString(ctx, text);

    return JS_UNDEFINED;
}

static JSValue tjs_lvgl_label_get_text(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    if (argc < 1)
        return JS_ThrowTypeError(ctx, "Expected 1 argument: ptr");

    lv_obj_t *label = tjs_lvgl_get_obj(ctx, argv[0]);
    if (!label)
        return JS_EXCEPTION;

    const char *text = lv_label_get_text(label);
    return JS_NewString(ctx, text ? text : "");
}

/* ============================================================
 * Button Methods
 * ============================================================ */

static JSValue tjs_lvgl_button_set_label(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    if (argc < 2)
        return JS_ThrowTypeError(ctx, "Expected 2 arguments: ptr, text");

    lv_obj_t *btn = tjs_lvgl_get_obj(ctx, argv[0]);
    if (!btn)
        return JS_EXCEPTION;

    const char *text = JS_ToCString(ctx, argv[1]);
    if (!text)
        return JS_EXCEPTION;

    lv_obj_t *label = NULL;
    uint32_t child_cnt = lv_obj_get_child_cnt(btn);
    for (uint32_t i = 0; i < child_cnt; i++) {
        lv_obj_t *child = lv_obj_get_child(btn, (int32_t)i);
        if (child && lv_obj_check_type(child, &lv_label_class)) {
            label = child;
            break;
        }
    }
    if (!label) {
        label = lv_label_create(btn);
    }
    lv_label_set_text(label, text);
    lv_obj_center(label);

    JS_FreeCString(ctx, text);

    return JS_UNDEFINED;
}

/* ============================================================
 * Image Methods
 * ============================================================ */

static JSValue tjs_lvgl_img_set_symbol(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    if (argc < 2)
        return JS_ThrowTypeError(ctx, "Expected 2 arguments: ptr, symbol");

    lv_obj_t *img = tjs_lvgl_get_obj(ctx, argv[0]);
    if (!img)
        return JS_EXCEPTION;

    const char *symbol = JS_ToCString(ctx, argv[1]);
    if (!symbol)
        return JS_EXCEPTION;

    lv_img_set_src(img, symbol);
    JS_FreeCString(ctx, symbol);

    return JS_UNDEFINED;
}

static JSValue tjs_lvgl_img_set_binary(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    if (argc < 2)
        return JS_ThrowTypeError(ctx, "Expected 2 arguments: ptr, ArrayBuffer|TypedArray");

    lv_obj_t *img = tjs_lvgl_get_obj(ctx, argv[0]);
    if (!img)
        return JS_EXCEPTION;

    size_t size = 0;
    uint8_t *data = tjs_lvgl_js_to_bytes_copy(ctx, argv[1], &size);
    if (!data || size == 0) {
        free(data);
        return JS_UNDEFINED;
    }

    lv_img_dsc_t *dsc = (lv_img_dsc_t *)calloc(1, sizeof(lv_img_dsc_t));
    if (!dsc) {
        free(data);
        return JS_ThrowOutOfMemory(ctx);
    }
    dsc->header.always_zero = 0;
    uint32_t png_w = 1;
    uint32_t png_h = 1;
    lv_img_cf_t cf = LV_IMG_CF_RAW;
    if (tjs_lvgl_png_dimensions(data, size, &png_w, &png_h)) {
        cf = LV_IMG_CF_RAW_ALPHA;
    }
    dsc->header.cf = cf;
    dsc->header.w = (lv_coord_t)png_w;
    dsc->header.h = (lv_coord_t)png_h;
    dsc->data_size = (uint32_t)size;
    dsc->data = data;

    TjsLVGLStyleState *state = tjs_lvgl_style_state_get(img, true);
    if (!state) {
        free(dsc);
        free(data);
        return JS_ThrowOutOfMemory(ctx);
    }
    free(state->img_dsc);
    free(state->img_data);
    state->img_dsc = dsc;
    state->img_data = data;

    lv_img_set_src(img, dsc);
    return JS_UNDEFINED;
}

static JSValue tjs_lvgl_img_set_zoom(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    if (argc < 2)
        return JS_ThrowTypeError(ctx, "Expected 2 arguments: ptr, zoom");

    lv_obj_t *img = tjs_lvgl_get_obj(ctx, argv[0]);
    if (!img)
        return JS_EXCEPTION;

    int32_t zoom = 256;
    if (JS_ToInt32(ctx, &zoom, argv[1]) < 0)
        return JS_EXCEPTION;
    if (zoom < 0) zoom = 0;
    if (zoom > 4096) zoom = 4096;
    lv_img_set_zoom(img, (uint16_t)zoom);
    return JS_UNDEFINED;
}

static JSValue tjs_lvgl_img_set_angle(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    if (argc < 2)
        return JS_ThrowTypeError(ctx, "Expected 2 arguments: ptr, angle");

    lv_obj_t *img = tjs_lvgl_get_obj(ctx, argv[0]);
    if (!img)
        return JS_EXCEPTION;

    int32_t angle = 0;
    if (JS_ToInt32(ctx, &angle, argv[1]) < 0)
        return JS_EXCEPTION;
    lv_img_set_angle(img, (int16_t)angle);
    return JS_UNDEFINED;
}

static JSValue tjs_lvgl_img_set_pivot(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    if (argc < 3)
        return JS_ThrowTypeError(ctx, "Expected 3 arguments: ptr, x, y");

    lv_obj_t *img = tjs_lvgl_get_obj(ctx, argv[0]);
    if (!img)
        return JS_EXCEPTION;

    int32_t x = 0;
    int32_t y = 0;
    if (JS_ToInt32(ctx, &x, argv[1]) < 0 || JS_ToInt32(ctx, &y, argv[2]) < 0)
        return JS_EXCEPTION;
    lv_img_set_pivot(img, (lv_coord_t)x, (lv_coord_t)y);
    return JS_UNDEFINED;
}

static JSValue tjs_lvgl_obj_set_background_symbol(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    if (argc < 3)
        return JS_ThrowTypeError(ctx, "Expected 3 arguments: ptr, symbol|null, styleType");

    lv_obj_t *obj = tjs_lvgl_get_obj(ctx, argv[0]);
    if (!obj)
        return JS_EXCEPTION;

    int32_t style_type = 0;
    if (JS_ToInt32(ctx, &style_type, argv[2]) < 0)
        return JS_EXCEPTION;
    lv_style_selector_t selector = tjs_lvgl_style_selector_from_type(style_type);

    TjsLVGLStyleState *state = tjs_lvgl_style_state_get(obj, true);
    if (!state) {
        return JS_ThrowOutOfMemory(ctx);
    }

    if (JS_IsNull(argv[1]) || JS_IsUndefined(argv[1])) {
        lv_obj_set_style_bg_img_src(obj, NULL, selector);
        free(state->bg_img_symbol);
        state->bg_img_symbol = NULL;
        return JS_UNDEFINED;
    }

    const char *symbol = JS_ToCString(ctx, argv[1]);
    if (!symbol)
        return JS_EXCEPTION;
    char *owned_symbol = tjs_lvgl_strdup(symbol);
    JS_FreeCString(ctx, symbol);
    if (!owned_symbol) {
        return JS_ThrowOutOfMemory(ctx);
    }
    free(state->bg_img_symbol);
    state->bg_img_symbol = owned_symbol;
    lv_obj_set_style_bg_img_src(obj, state->bg_img_symbol, selector);
    lv_obj_set_style_bg_img_opa(obj, LV_OPA_COVER, selector);
    return JS_UNDEFINED;
}

static JSValue tjs_lvgl_obj_set_arc_symbol(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    if (argc < 3)
        return JS_ThrowTypeError(ctx, "Expected 3 arguments: ptr, symbol|null, styleType");

    lv_obj_t *obj = tjs_lvgl_get_obj(ctx, argv[0]);
    if (!obj)
        return JS_EXCEPTION;

    int32_t style_type = 0;
    if (JS_ToInt32(ctx, &style_type, argv[2]) < 0)
        return JS_EXCEPTION;
    lv_style_selector_t selector = tjs_lvgl_style_selector_from_type(style_type);

    TjsLVGLStyleState *state = tjs_lvgl_style_state_get(obj, true);
    if (!state) {
        return JS_ThrowOutOfMemory(ctx);
    }

    if (JS_IsNull(argv[1]) || JS_IsUndefined(argv[1])) {
        lv_obj_set_style_arc_img_src(obj, NULL, selector);
        free(state->arc_img_symbol);
        state->arc_img_symbol = NULL;
        return JS_UNDEFINED;
    }

    const char *symbol = JS_ToCString(ctx, argv[1]);
    if (!symbol)
        return JS_EXCEPTION;
    char *owned_symbol = tjs_lvgl_strdup(symbol);
    JS_FreeCString(ctx, symbol);
    if (!owned_symbol) {
        return JS_ThrowOutOfMemory(ctx);
    }
    free(state->arc_img_symbol);
    state->arc_img_symbol = owned_symbol;
    lv_obj_set_style_arc_img_src(obj, state->arc_img_symbol, selector);
    return JS_UNDEFINED;
}

/* ============================================================
 * State and Widget Methods
 * ============================================================ */

static JSValue tjs_lvgl_obj_set_checked(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    if (argc < 2)
        return JS_ThrowTypeError(ctx, "Expected 2 arguments: ptr, checked");

    lv_obj_t *obj = tjs_lvgl_get_obj(ctx, argv[0]);
    if (!obj)
        return JS_EXCEPTION;

    int checked = JS_ToBool(ctx, argv[1]);
    if (checked < 0)
        return JS_EXCEPTION;
    if (checked) {
        lv_obj_add_state(obj, LV_STATE_CHECKED);
    } else {
        lv_obj_clear_state(obj, LV_STATE_CHECKED);
    }
    return JS_UNDEFINED;
}

static JSValue tjs_lvgl_obj_set_disabled(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    if (argc < 2)
        return JS_ThrowTypeError(ctx, "Expected 2 arguments: ptr, disabled");

    lv_obj_t *obj = tjs_lvgl_get_obj(ctx, argv[0]);
    if (!obj)
        return JS_EXCEPTION;

    int disabled = JS_ToBool(ctx, argv[1]);
    if (disabled < 0)
        return JS_EXCEPTION;
    if (disabled) {
        lv_obj_add_state(obj, LV_STATE_DISABLED);
    } else {
        lv_obj_clear_state(obj, LV_STATE_DISABLED);
    }
    return JS_UNDEFINED;
}

static JSValue tjs_lvgl_slider_set_range(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    if (argc < 3)
        return JS_ThrowTypeError(ctx, "Expected 3 arguments: ptr, min, max");

    lv_obj_t *slider = tjs_lvgl_get_obj(ctx, argv[0]);
    if (!slider)
        return JS_EXCEPTION;

    int32_t min = 0;
    int32_t max = 100;
    if (JS_ToInt32(ctx, &min, argv[1]) < 0 || JS_ToInt32(ctx, &max, argv[2]) < 0)
        return JS_EXCEPTION;
    lv_slider_set_range(slider, min, max);
    return JS_UNDEFINED;
}

static JSValue tjs_lvgl_slider_set_value(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    if (argc < 2)
        return JS_ThrowTypeError(ctx, "Expected 2 arguments: ptr, value");

    lv_obj_t *slider = tjs_lvgl_get_obj(ctx, argv[0]);
    if (!slider)
        return JS_EXCEPTION;

    int32_t value = 0;
    if (JS_ToInt32(ctx, &value, argv[1]) < 0)
        return JS_EXCEPTION;
    lv_slider_set_value(slider, value, LV_ANIM_OFF);
    return JS_UNDEFINED;
}

static JSValue tjs_lvgl_slider_get_value(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    if (argc < 1)
        return JS_ThrowTypeError(ctx, "Expected 1 argument: ptr");

    lv_obj_t *slider = tjs_lvgl_get_obj(ctx, argv[0]);
    if (!slider)
        return JS_EXCEPTION;
    return JS_NewInt32(ctx, (int32_t)lv_slider_get_value(slider));
}

static JSValue tjs_lvgl_checkbox_set_text(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    if (argc < 2)
        return JS_ThrowTypeError(ctx, "Expected 2 arguments: ptr, text");

    lv_obj_t *checkbox = tjs_lvgl_get_obj(ctx, argv[0]);
    if (!checkbox)
        return JS_EXCEPTION;

    const char *text = JS_ToCString(ctx, argv[1]);
    if (!text)
        return JS_EXCEPTION;
    lv_checkbox_set_text(checkbox, text);
    JS_FreeCString(ctx, text);
    return JS_UNDEFINED;
}

static JSValue tjs_lvgl_dropdown_set_options(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    if (argc < 2)
        return JS_ThrowTypeError(ctx, "Expected 2 arguments: ptr, items");

    lv_obj_t *dropdown = tjs_lvgl_get_obj(ctx, argv[0]);
    if (!dropdown)
        return JS_EXCEPTION;

    if (!JS_IsArray(argv[1])) {
        return JS_UNDEFINED;
    }
    JSValue len_val = JS_GetPropertyStr(ctx, argv[1], "length");
    int32_t len = 0;
    if (JS_ToInt32(ctx, &len, len_val) < 0) {
        JS_FreeValue(ctx, len_val);
        return JS_EXCEPTION;
    }
    JS_FreeValue(ctx, len_val);
    if (len < 0) {
        len = 0;
    }

    size_t total = 1;
    for (int32_t i = 0; i < len; i++) {
        JSValue item = JS_GetPropertyUint32(ctx, argv[1], (uint32_t)i);
        const char *str = JS_ToCString(ctx, item);
        if (str) {
            total += strlen(str) + 1;
            JS_FreeCString(ctx, str);
        }
        JS_FreeValue(ctx, item);
    }

    char *joined = (char *)malloc(total);
    if (!joined) {
        return JS_ThrowOutOfMemory(ctx);
    }
    joined[0] = '\0';
    for (int32_t i = 0; i < len; i++) {
        JSValue item = JS_GetPropertyUint32(ctx, argv[1], (uint32_t)i);
        const char *str = JS_ToCString(ctx, item);
        if (str) {
            if (joined[0] != '\0') {
                strcat(joined, "\n");
            }
            strcat(joined, str);
            JS_FreeCString(ctx, str);
        }
        JS_FreeValue(ctx, item);
    }

    lv_dropdown_set_options(dropdown, joined);
    free(joined);
    return JS_UNDEFINED;
}

static JSValue tjs_lvgl_dropdown_set_selected(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    if (argc < 2)
        return JS_ThrowTypeError(ctx, "Expected 2 arguments: ptr, selected");

    lv_obj_t *dropdown = tjs_lvgl_get_obj(ctx, argv[0]);
    if (!dropdown)
        return JS_EXCEPTION;

    int32_t selected = 0;
    if (JS_ToInt32(ctx, &selected, argv[1]) < 0)
        return JS_EXCEPTION;
    if (selected < 0) {
        selected = 0;
    }
    lv_dropdown_set_selected(dropdown, (uint16_t)selected);
    return JS_UNDEFINED;
}

static JSValue tjs_lvgl_dropdown_set_text(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    if (argc < 2)
        return JS_ThrowTypeError(ctx, "Expected 2 arguments: ptr, text|null");

    lv_obj_t *dropdown = tjs_lvgl_get_obj(ctx, argv[0]);
    if (!dropdown)
        return JS_EXCEPTION;

    TjsLVGLStyleState *state = tjs_lvgl_style_state_get(dropdown, true);
    if (!state) {
        return JS_ThrowOutOfMemory(ctx);
    }

    if (JS_IsNull(argv[1]) || JS_IsUndefined(argv[1])) {
        lv_dropdown_set_text(dropdown, NULL);
        free(state->dropdown_text);
        state->dropdown_text = NULL;
        return JS_UNDEFINED;
    }
    const char *text = JS_ToCString(ctx, argv[1]);
    if (!text)
        return JS_EXCEPTION;
    char *owned_text = tjs_lvgl_strdup(text);
    JS_FreeCString(ctx, text);
    if (!owned_text) {
        return JS_ThrowOutOfMemory(ctx);
    }
    free(state->dropdown_text);
    state->dropdown_text = owned_text;
    lv_dropdown_set_text(dropdown, state->dropdown_text);
    return JS_UNDEFINED;
}

static const char *tjs_lvgl_dropdown_symbol_from_dir(int32_t dir) {
    switch (dir) {
        case 0: return LV_SYMBOL_UP;
        case 1: return LV_SYMBOL_RIGHT;
        case 2: return LV_SYMBOL_DOWN;
        case 3: return LV_SYMBOL_LEFT;
        default: return LV_SYMBOL_DOWN;
    }
}

static JSValue tjs_lvgl_dropdown_set_symbol_dir(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    if (argc < 2)
        return JS_ThrowTypeError(ctx, "Expected 2 arguments: ptr, dir");

    lv_obj_t *dropdown = tjs_lvgl_get_obj(ctx, argv[0]);
    if (!dropdown)
        return JS_EXCEPTION;

    int32_t dir = 2;
    if (JS_ToInt32(ctx, &dir, argv[1]) < 0)
        return JS_EXCEPTION;
    lv_dropdown_set_symbol(dropdown, tjs_lvgl_dropdown_symbol_from_dir(dir));
    return JS_UNDEFINED;
}

static JSValue tjs_lvgl_dropdown_set_dir(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    if (argc < 2)
        return JS_ThrowTypeError(ctx, "Expected 2 arguments: ptr, dir");

    lv_obj_t *dropdown = tjs_lvgl_get_obj(ctx, argv[0]);
    if (!dropdown)
        return JS_EXCEPTION;

    int32_t dir = 0;
    if (JS_ToInt32(ctx, &dir, argv[1]) < 0)
        return JS_EXCEPTION;
    lv_dropdown_set_dir(dropdown, (lv_dir_t)dir);
    return JS_UNDEFINED;
}

static JSValue tjs_lvgl_dropdown_set_selected_highlight(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    if (argc < 2)
        return JS_ThrowTypeError(ctx, "Expected 2 arguments: ptr, enabled");

    lv_obj_t *dropdown = tjs_lvgl_get_obj(ctx, argv[0]);
    if (!dropdown)
        return JS_EXCEPTION;

    int enabled = JS_ToBool(ctx, argv[1]);
    if (enabled < 0)
        return JS_EXCEPTION;
    lv_dropdown_set_selected_highlight(dropdown, enabled ? true : false);
    return JS_UNDEFINED;
}

static JSValue tjs_lvgl_dropdown_get_selected(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    (void)this_val;
    if (argc < 1)
        return JS_ThrowTypeError(ctx, "Expected 1 argument: ptr");

    lv_obj_t *dropdown = tjs_lvgl_get_obj(ctx, argv[0]);
    if (!dropdown)
        return JS_EXCEPTION;
    return JS_NewInt32(ctx, (int32_t)lv_dropdown_get_selected(dropdown));
}

static JSValue tjs_lvgl_dropdown_open(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    (void)this_val;
    if (argc < 1)
        return JS_ThrowTypeError(ctx, "Expected 1 argument: ptr");

    lv_obj_t *dropdown = tjs_lvgl_get_obj(ctx, argv[0]);
    if (!dropdown)
        return JS_EXCEPTION;
#if LV_USE_DROPDOWN != 0
    lv_dropdown_open(dropdown);
#endif
    return JS_UNDEFINED;
}

static JSValue tjs_lvgl_dropdown_close(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    (void)this_val;
    if (argc < 1)
        return JS_ThrowTypeError(ctx, "Expected 1 argument: ptr");

    lv_obj_t *dropdown = tjs_lvgl_get_obj(ctx, argv[0]);
    if (!dropdown)
        return JS_EXCEPTION;
#if LV_USE_DROPDOWN != 0
    lv_dropdown_close(dropdown);
#endif
    return JS_UNDEFINED;
}

static JSValue tjs_lvgl_calendar_set_today(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    (void)this_val;
    if (argc < 4)
        return JS_ThrowTypeError(ctx, "Expected 4 arguments: ptr, year, month, day");

    lv_obj_t *calendar = tjs_lvgl_get_obj(ctx, argv[0]);
    if (!calendar)
        return JS_EXCEPTION;

    int32_t y = 0, m = 0, d = 0;
    if (JS_ToInt32(ctx, &y, argv[1]) < 0 || JS_ToInt32(ctx, &m, argv[2]) < 0 || JS_ToInt32(ctx, &d, argv[3]) < 0)
        return JS_EXCEPTION;
    lv_calendar_set_today_date(calendar, (uint32_t)y, (uint32_t)m, (uint32_t)d);
    return JS_UNDEFINED;
}

static JSValue tjs_lvgl_calendar_set_shown_month(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    (void)this_val;
    if (argc < 3)
        return JS_ThrowTypeError(ctx, "Expected 3 arguments: ptr, year, month");

    lv_obj_t *calendar = tjs_lvgl_get_obj(ctx, argv[0]);
    if (!calendar)
        return JS_EXCEPTION;

    int32_t y = 0, m = 0;
    if (JS_ToInt32(ctx, &y, argv[1]) < 0 || JS_ToInt32(ctx, &m, argv[2]) < 0)
        return JS_EXCEPTION;
    lv_calendar_set_showed_date(calendar, (uint32_t)y, (uint32_t)m);
    return JS_UNDEFINED;
}

static JSValue tjs_lvgl_calendar_set_highlighted_dates(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    (void)this_val;
    if (argc < 2)
        return JS_ThrowTypeError(ctx, "Expected 2 arguments: ptr, dates");

    lv_obj_t *calendar = tjs_lvgl_get_obj(ctx, argv[0]);
    if (!calendar)
        return JS_EXCEPTION;

    static lv_calendar_date_t empty_highlight = { 0, 0, 0 };

    if (!JS_IsArray(argv[1])) {
        TjsLVGLStyleState *state = tjs_lvgl_style_state_get(calendar, false);
        if (state) {
            free(state->calendar_highlighted);
            state->calendar_highlighted = NULL;
        }
        lv_calendar_set_highlighted_dates(calendar, &empty_highlight, 0);
        return JS_UNDEFINED;
    }
    JSValue len_val = JS_GetPropertyStr(ctx, argv[1], "length");
    int32_t len = 0;
    if (JS_ToInt32(ctx, &len, len_val) < 0) {
        JS_FreeValue(ctx, len_val);
        return JS_EXCEPTION;
    }
    JS_FreeValue(ctx, len_val);
    if (len < 0) {
        len = 0;
    }
    if (len == 0) {
        TjsLVGLStyleState *state = tjs_lvgl_style_state_get(calendar, false);
        if (state) {
            free(state->calendar_highlighted);
            state->calendar_highlighted = NULL;
        }
        lv_calendar_set_highlighted_dates(calendar, &empty_highlight, 0);
        return JS_UNDEFINED;
    }

    lv_calendar_date_t *dates = NULL;
    dates = (lv_calendar_date_t *)calloc((size_t)len, sizeof(lv_calendar_date_t));
    if (!dates) {
        return JS_ThrowOutOfMemory(ctx);
    }
    for (int32_t i = 0; i < len; i++) {
        JSValue item = JS_GetPropertyUint32(ctx, argv[1], (uint32_t)i);
        if (JS_IsArray(item)) {
            int32_t y = 0, m = 0, d = 0;
            JSValue yv = JS_GetPropertyUint32(ctx, item, 0);
            JSValue mv = JS_GetPropertyUint32(ctx, item, 1);
            JSValue dv = JS_GetPropertyUint32(ctx, item, 2);
            JS_ToInt32(ctx, &y, yv);
            JS_ToInt32(ctx, &m, mv);
            JS_ToInt32(ctx, &d, dv);
            dates[i].year = (uint16_t)y;
            dates[i].month = (int8_t)m;
            dates[i].day = (int8_t)d;
            JS_FreeValue(ctx, yv);
            JS_FreeValue(ctx, mv);
            JS_FreeValue(ctx, dv);
        }
        JS_FreeValue(ctx, item);
    }

    TjsLVGLStyleState *state = tjs_lvgl_style_state_get(calendar, true);
    if (!state) {
        free(dates);
        return JS_ThrowOutOfMemory(ctx);
    }
    free(state->calendar_highlighted);
    state->calendar_highlighted = dates;
    lv_calendar_set_highlighted_dates(calendar, state->calendar_highlighted, (uint16_t)len);
    return JS_UNDEFINED;
}

static JSValue tjs_lvgl_debug_calendar_press_day(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    (void)this_val;
    if (argc < 2)
        return JS_ThrowTypeError(ctx, "Expected 2 arguments: ptr, day");

    lv_obj_t *calendar = tjs_lvgl_get_obj(ctx, argv[0]);
    if (!calendar)
        return JS_EXCEPTION;

    int32_t day = 0;
    if (JS_ToInt32(ctx, &day, argv[1]) < 0)
        return JS_EXCEPTION;

#if LV_USE_CALENDAR != 0
    if (day < 1 || day > 31) {
        return JS_ThrowRangeError(ctx, "Calendar day is out of range");
    }

    const lv_calendar_date_t *shown = lv_calendar_get_showed_date(calendar);
    if (!shown) {
        return JS_EXCEPTION;
    }
    lv_calendar_set_showed_date(calendar, shown->year, shown->month);

    lv_obj_t *btnm = lv_calendar_get_btnmatrix(calendar);
    if (!btnm) {
        return JS_EXCEPTION;
    }

    uint16_t found = LV_BTNMATRIX_BTN_NONE;
    for (uint16_t id = 7; id < 49; id++) {
        if (lv_btnmatrix_has_btn_ctrl(btnm, id, LV_BTNMATRIX_CTRL_DISABLED)) {
            continue;
        }
        const char *txt = lv_btnmatrix_get_btn_text(btnm, id);
        if (!txt || !txt[0]) {
            continue;
        }
        char *end = NULL;
        long value = strtol(txt, &end, 10);
        if (end != txt && value == day) {
            found = id;
            break;
        }
    }
    if (found == LV_BTNMATRIX_BTN_NONE) {
        return JS_ThrowRangeError(ctx, "Calendar day is not visible in the shown month");
    }

    lv_btnmatrix_set_selected_btn(btnm, found);
    tjs_lvgl_event_queue_push(calendar, calendar, LV_EVENT_VALUE_CHANGED);
#endif
    return JS_UNDEFINED;
}

/* ============================================================
 * Textarea Methods
 * ============================================================ */

static JSValue tjs_lvgl_textarea_set_text(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    if (argc < 2)
        return JS_ThrowTypeError(ctx, "Expected 2 arguments: ptr, text");

    lv_obj_t *ta = tjs_lvgl_get_obj(ctx, argv[0]);
    if (!ta)
        return JS_EXCEPTION;

    const char *text = JS_ToCString(ctx, argv[1]);
    if (!text)
        return JS_EXCEPTION;

    lv_textarea_set_text(ta, text);
    JS_FreeCString(ctx, text);

    return JS_UNDEFINED;
}

static JSValue tjs_lvgl_textarea_set_one_line(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    if (argc < 2)
        return JS_ThrowTypeError(ctx, "Expected 2 arguments: ptr, oneLine");

    lv_obj_t *ta = tjs_lvgl_get_obj(ctx, argv[0]);
    if (!ta)
        return JS_EXCEPTION;

    int one_line = JS_ToBool(ctx, argv[1]);
    if (one_line < 0)
        return JS_EXCEPTION;

    lv_textarea_set_one_line(ta, one_line ? true : false);
    return JS_UNDEFINED;
}

static JSValue tjs_lvgl_textarea_set_placeholder(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    if (argc < 2)
        return JS_ThrowTypeError(ctx, "Expected 2 arguments: ptr, placeholder");

    lv_obj_t *ta = tjs_lvgl_get_obj(ctx, argv[0]);
    if (!ta)
        return JS_EXCEPTION;

    const char *text = JS_ToCString(ctx, argv[1]);
    if (!text)
        return JS_EXCEPTION;

    lv_textarea_set_placeholder_text(ta, text);
    JS_FreeCString(ctx, text);

    return JS_UNDEFINED;
}

static JSValue tjs_lvgl_textarea_set_password_mode(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    if (argc < 2)
        return JS_ThrowTypeError(ctx, "Expected 2 arguments: ptr, enabled");

    lv_obj_t *ta = tjs_lvgl_get_obj(ctx, argv[0]);
    if (!ta)
        return JS_EXCEPTION;

    int enabled = JS_ToBool(ctx, argv[1]);
    if (enabled < 0)
        return JS_EXCEPTION;

    lv_textarea_set_password_mode(ta, enabled ? true : false);
    return JS_UNDEFINED;
}

static JSValue tjs_lvgl_textarea_set_max_length(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    if (argc < 2)
        return JS_ThrowTypeError(ctx, "Expected 2 arguments: ptr, maxLen");

    lv_obj_t *ta = tjs_lvgl_get_obj(ctx, argv[0]);
    if (!ta)
        return JS_EXCEPTION;

    int32_t max_len = 0;
    if (JS_ToInt32(ctx, &max_len, argv[1]) < 0)
        return JS_EXCEPTION;

    if (max_len < 0)
        max_len = 0;

    lv_textarea_set_max_length(ta, (uint32_t)max_len);
    return JS_UNDEFINED;
}

static JSValue tjs_lvgl_textarea_set_auto_keyboard(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    (void)this_val;
    if (argc < 2)
        return JS_ThrowTypeError(ctx, "Expected 2 arguments: ptr, enabled");

    lv_obj_t *ta = tjs_lvgl_get_obj(ctx, argv[0]);
    if (!ta)
        return JS_EXCEPTION;

    int enabled = JS_ToBool(ctx, argv[1]);
    if (enabled < 0)
        return JS_EXCEPTION;

    TjsLVGLStyleState *state = tjs_lvgl_style_state_get(ta, true);
    if (!state)
        return JS_ThrowOutOfMemory(ctx);
    state->textarea_auto_keyboard = enabled ? true : false;
    state->textarea_auto_keyboard_set = true;
    /* Placeholder for API compatibility in desktop SDL stage. */
    return JS_UNDEFINED;
}

/* ============================================================
 * Theme Methods
 * ============================================================ */

static bool tjs_lvgl_get_optional_i32_prop(JSContext *ctx, JSValueConst obj, const char *name, int32_t *out) {
    JSValue val = JS_GetPropertyStr(ctx, obj, name);
    bool ok = false;
    if (!JS_IsUndefined(val) && !JS_IsNull(val)) {
        ok = tjs_lvgl_js_to_i32(ctx, val, out);
    }
    JS_FreeValue(ctx, val);
    return ok;
}

static JSValue tjs_lvgl_set_theme(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    (void)this_val;
    if (!lvgl_state.initialized || !lvgl_state.disp) {
        return JS_UNDEFINED;
    }

    int32_t primary = 0x2196f3;
    int32_t secondary = primary;
    int32_t font_size = 14;

    if (argc > 0 && JS_IsObject(argv[0]) && !JS_IsArray(argv[0])) {
        tjs_lvgl_get_optional_i32_prop(ctx, argv[0], "primaryColor", &primary);
        if (!tjs_lvgl_get_optional_i32_prop(ctx, argv[0], "secondPrimaryValue", &secondary) &&
            !tjs_lvgl_get_optional_i32_prop(ctx, argv[0], "secondaryColor", &secondary)) {
            secondary = primary;
        }
        tjs_lvgl_get_optional_i32_prop(ctx, argv[0], "fontSize", &font_size);
    } else {
        if (argc > 0) {
            tjs_lvgl_js_to_i32(ctx, argv[0], &primary);
        }
        secondary = primary;
        if (argc > 1) {
            tjs_lvgl_js_to_i32(ctx, argv[1], &secondary);
        }
        if (argc > 2) {
            tjs_lvgl_js_to_i32(ctx, argv[2], &font_size);
        }
    }

#if LV_USE_THEME_DEFAULT
    lv_theme_t *theme = lv_theme_default_init(
        lvgl_state.disp,
        lv_color_hex((uint32_t)primary),
        lv_color_hex((uint32_t)secondary),
        LV_THEME_DEFAULT_DARK,
        tjs_lvgl_font_by_size(font_size)
    );
    if (theme) {
        lv_disp_set_theme(lvgl_state.disp, theme);
        lv_obj_report_style_change(NULL);
    }
#endif
    return JS_UNDEFINED;
}

static JSValue tjs_lvgl_reset_theme(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    (void)argc;
    (void)argv;
    JSValue args[1];
    args[0] = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, args[0], "primaryColor", JS_NewInt32(ctx, 0x2196f3));
    JS_SetPropertyStr(ctx, args[0], "secondPrimaryValue", JS_NewInt32(ctx, 0xf44336));
    JS_SetPropertyStr(ctx, args[0], "fontSize", JS_NewInt32(ctx, 14));
    JSValue ret = tjs_lvgl_set_theme(ctx, this_val, 1, args);
    JS_FreeValue(ctx, args[0]);
    return ret;
}

/* ============================================================
 * Event Methods
 * ============================================================ */

static JSValue tjs_lvgl_add_event_listener(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    if (argc < 2) {
        return JS_ThrowTypeError(ctx, "Expected 2 arguments: ptr, eventType");
    }
    lv_obj_t *obj = tjs_lvgl_get_obj(ctx, argv[0]);
    if (!obj) {
        return JS_EXCEPTION;
    }
    int32_t event_type = 0;
    if (JS_ToInt32(ctx, &event_type, argv[1]) < 0) {
        return JS_EXCEPTION;
    }
    lv_obj_add_event_cb(obj, tjs_lvgl_event_cb, (lv_event_code_t)event_type, NULL);
    return JS_UNDEFINED;
}

static JSValue tjs_lvgl_remove_event_listener(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "Expected 1 argument: ptr");
    }
    lv_obj_t *obj = tjs_lvgl_get_obj(ctx, argv[0]);
    if (!obj) {
        return JS_EXCEPTION;
    }
    lv_obj_remove_event_cb(obj, tjs_lvgl_event_cb);
    return JS_UNDEFINED;
}

static JSValue tjs_lvgl_poll_events(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    JSValue arr = JS_NewArray(ctx);
    uint32_t idx = 0;
    TjsLVGLEvent ev;
    while (tjs_lvgl_event_queue_pop(&ev)) {
        JSValue item = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, item, "target", tjs_lvgl_new_obj(ctx, ev.target));
        JS_SetPropertyStr(ctx, item, "currentTarget", tjs_lvgl_new_obj(ctx, ev.current_target));
        JS_SetPropertyStr(ctx, item, "code", JS_NewInt32(ctx, (int32_t)ev.code));
        JS_SetPropertyStr(ctx, item, "value", JS_NewInt32(ctx, ev.value));
        if (ev.text_value) {
            JS_SetPropertyStr(ctx, item, "textValue", JS_NewString(ctx, ev.text_value));
        }
        JS_SetPropertyUint32(ctx, arr, idx++, item);
        tjs_lvgl_event_clear(&ev);
    }
    return arr;
}

static JSValue tjs_lvgl_debug_get_parent(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "Expected 1 argument: ptr");
    }
    lv_obj_t *obj = tjs_lvgl_get_obj(ctx, argv[0]);
    if (!obj) {
        return JS_EXCEPTION;
    }
    lv_obj_t *parent = lv_obj_get_parent(obj);
    if (!parent) {
        return JS_NULL;
    }
    return tjs_lvgl_new_obj(ctx, parent);
}

static JSValue tjs_lvgl_debug_get_index(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    (void)this_val;
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "Expected 1 argument: ptr");
    }
    lv_obj_t *obj = tjs_lvgl_get_obj(ctx, argv[0]);
    if (!obj) {
        return JS_EXCEPTION;
    }
    return JS_NewInt32(ctx, (int32_t)lv_obj_get_index(obj));
}

static JSValue tjs_lvgl_debug_is_floating(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    (void)this_val;
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "Expected 1 argument: ptr");
    }
    lv_obj_t *obj = tjs_lvgl_get_obj(ctx, argv[0]);
    if (!obj) {
        return JS_EXCEPTION;
    }
    return JS_NewBool(ctx, lv_obj_has_flag(obj, LV_OBJ_FLAG_FLOATING));
}

static JSValue tjs_lvgl_debug_get_grid_template(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    (void)this_val;
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "Expected 1 argument: ptr");
    }
    lv_obj_t *obj = tjs_lvgl_get_obj(ctx, argv[0]);
    if (!obj) {
        return JS_EXCEPTION;
    }

    JSValue out = JS_NewObject(ctx);
    JSValue cols = JS_NewArray(ctx);
    JSValue rows = JS_NewArray(ctx);
    JS_SetPropertyStr(ctx, out, "cols", cols);
    JS_SetPropertyStr(ctx, out, "rows", rows);

    TjsLVGLStyleState *state = tjs_lvgl_style_state_get(obj, false);
    if (!state) {
        return out;
    }

    if (state->grid_cols) {
        uint32_t idx = 0;
        for (; idx < 128 && state->grid_cols[idx] != LV_GRID_TEMPLATE_LAST; idx++) {
            JS_SetPropertyUint32(ctx, cols, idx, JS_NewInt32(ctx, (int32_t)state->grid_cols[idx]));
        }
    }
    if (state->grid_rows) {
        uint32_t idx = 0;
        for (; idx < 128 && state->grid_rows[idx] != LV_GRID_TEMPLATE_LAST; idx++) {
            JS_SetPropertyUint32(ctx, rows, idx, JS_NewInt32(ctx, (int32_t)state->grid_rows[idx]));
        }
    }
    return out;
}

static JSValue tjs_lvgl_debug_get_grid_cell(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    (void)this_val;
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "Expected 1 argument: ptr");
    }
    lv_obj_t *obj = tjs_lvgl_get_obj(ctx, argv[0]);
    if (!obj) {
        return JS_EXCEPTION;
    }

    JSValue out = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, out, "colPos",   JS_NewInt32(ctx, (int32_t)lv_obj_get_style_grid_cell_column_pos(obj, LV_PART_MAIN)));
    JS_SetPropertyStr(ctx, out, "colSpan",  JS_NewInt32(ctx, (int32_t)lv_obj_get_style_grid_cell_column_span(obj, LV_PART_MAIN)));
    JS_SetPropertyStr(ctx, out, "rowPos",   JS_NewInt32(ctx, (int32_t)lv_obj_get_style_grid_cell_row_pos(obj, LV_PART_MAIN)));
    JS_SetPropertyStr(ctx, out, "rowSpan",  JS_NewInt32(ctx, (int32_t)lv_obj_get_style_grid_cell_row_span(obj, LV_PART_MAIN)));
    JS_SetPropertyStr(ctx, out, "xAlign",   JS_NewInt32(ctx, (int32_t)lv_obj_get_style_grid_cell_x_align(obj, LV_PART_MAIN)));
    JS_SetPropertyStr(ctx, out, "yAlign",   JS_NewInt32(ctx, (int32_t)lv_obj_get_style_grid_cell_y_align(obj, LV_PART_MAIN)));
    return out;
}

static JSValue tjs_lvgl_debug_get_scrollable(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    (void)this_val;
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "Expected 1 argument: ptr");
    }
    lv_obj_t *obj = tjs_lvgl_get_obj(ctx, argv[0]);
    if (!obj) {
        return JS_EXCEPTION;
    }
    return JS_NewBool(ctx, lv_obj_has_flag(obj, LV_OBJ_FLAG_SCROLLABLE));
}

static JSValue tjs_lvgl_debug_get_geometry(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    (void)this_val;
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "Expected 1 argument: ptr");
    }
    lv_obj_t *obj = tjs_lvgl_get_obj(ctx, argv[0]);
    if (!obj) {
        return JS_EXCEPTION;
    }

    lv_obj_update_layout(obj);
    JSValue out = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, out, "x", JS_NewInt32(ctx, (int32_t)lv_obj_get_x(obj)));
    JS_SetPropertyStr(ctx, out, "y", JS_NewInt32(ctx, (int32_t)lv_obj_get_y(obj)));
    JS_SetPropertyStr(ctx, out, "width", JS_NewInt32(ctx, (int32_t)lv_obj_get_width(obj)));
    JS_SetPropertyStr(ctx, out, "height", JS_NewInt32(ctx, (int32_t)lv_obj_get_height(obj)));
    return out;
}

static JSValue tjs_lvgl_debug_get_style(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    (void)this_val;
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "Expected 1 argument: ptr");
    }
    lv_obj_t *obj = tjs_lvgl_get_obj(ctx, argv[0]);
    if (!obj) {
        return JS_EXCEPTION;
    }

    int32_t style_type = 0;
    if (argc > 1 && !JS_IsUndefined(argv[1])) {
        if (JS_ToInt32(ctx, &style_type, argv[1]) < 0) {
            return JS_EXCEPTION;
        }
    }
    lv_part_t part = tjs_lvgl_style_part_from_type(style_type);

    JSValue out = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, out, "part", JS_NewInt32(ctx, (int32_t)part));
    JS_SetPropertyStr(ctx, out, "background-color", JS_NewUint32(ctx, (uint32_t)lv_color_to32(lv_obj_get_style_bg_color(obj, part))));
    JS_SetPropertyStr(ctx, out, "background-opacity", JS_NewInt32(ctx, (int32_t)lv_obj_get_style_bg_opa(obj, part)));
    JS_SetPropertyStr(ctx, out, "border-color", JS_NewUint32(ctx, (uint32_t)lv_color_to32(lv_obj_get_style_border_color(obj, part))));
    JS_SetPropertyStr(ctx, out, "border-opacity", JS_NewInt32(ctx, (int32_t)lv_obj_get_style_border_opa(obj, part)));
    JS_SetPropertyStr(ctx, out, "outline-color", JS_NewUint32(ctx, (uint32_t)lv_color_to32(lv_obj_get_style_outline_color(obj, part))));
    JS_SetPropertyStr(ctx, out, "outline-opacity", JS_NewInt32(ctx, (int32_t)lv_obj_get_style_outline_opa(obj, part)));
    JS_SetPropertyStr(ctx, out, "shadow-color", JS_NewUint32(ctx, (uint32_t)lv_color_to32(lv_obj_get_style_shadow_color(obj, part))));
    JS_SetPropertyStr(ctx, out, "shadow-opacity", JS_NewInt32(ctx, (int32_t)lv_obj_get_style_shadow_opa(obj, part)));
    JS_SetPropertyStr(ctx, out, "line-width", JS_NewInt32(ctx, (int32_t)lv_obj_get_style_line_width(obj, part)));
    JS_SetPropertyStr(ctx, out, "line-color", JS_NewUint32(ctx, (uint32_t)lv_color_to32(lv_obj_get_style_line_color(obj, part))));
    JS_SetPropertyStr(ctx, out, "line-rounded", JS_NewBool(ctx, lv_obj_get_style_line_rounded(obj, part)));
    JS_SetPropertyStr(ctx, out, "line-opacity", JS_NewInt32(ctx, (int32_t)lv_obj_get_style_line_opa(obj, part)));
    JS_SetPropertyStr(ctx, out, "line-dash-width", JS_NewInt32(ctx, (int32_t)lv_obj_get_style_line_dash_width(obj, part)));
    JS_SetPropertyStr(ctx, out, "line-dash-gap", JS_NewInt32(ctx, (int32_t)lv_obj_get_style_line_dash_gap(obj, part)));
    TjsLVGLStyleState *state = tjs_lvgl_style_state_get(obj, false);
    JS_SetPropertyStr(ctx, out, "background-image-symbol", JS_NewString(ctx, state && state->bg_img_symbol ? state->bg_img_symbol : ""));
    JS_SetPropertyStr(ctx, out, "arc-image-symbol", JS_NewString(ctx, state && state->arc_img_symbol ? state->arc_img_symbol : ""));
    return out;
}

static JSValue tjs_lvgl_debug_get_state(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    (void)this_val;
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "Expected 1 argument: ptr");
    }
    lv_obj_t *obj = tjs_lvgl_get_obj(ctx, argv[0]);
    if (!obj) {
        return JS_EXCEPTION;
    }

    JSValue out = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, out, "state", JS_NewInt32(ctx, (int32_t)lv_obj_get_state(obj)));
    JS_SetPropertyStr(ctx, out, "checked", JS_NewBool(ctx, (lv_obj_get_state(obj) & LV_STATE_CHECKED) != 0));
    JS_SetPropertyStr(ctx, out, "disabled", JS_NewBool(ctx, (lv_obj_get_state(obj) & LV_STATE_DISABLED) != 0));
#if LV_USE_SLIDER != 0
    if (lv_obj_check_type(obj, &lv_slider_class)) {
        JS_SetPropertyStr(ctx, out, "value", JS_NewInt32(ctx, (int32_t)lv_slider_get_value(obj)));
    }
#endif
#if LV_USE_DROPDOWN != 0
    if (lv_obj_check_type(obj, &lv_dropdown_class)) {
        char selected[128];
        selected[0] = '\0';
        lv_dropdown_get_selected_str(obj, selected, sizeof(selected));
        const char *text = lv_dropdown_get_text(obj);
        const char *options = lv_dropdown_get_options(obj);
        const char *symbol = lv_dropdown_get_symbol(obj);
        lv_obj_t *list = lv_dropdown_get_list(obj);
        JS_SetPropertyStr(ctx, out, "selected", JS_NewInt32(ctx, (int32_t)lv_dropdown_get_selected(obj)));
        JS_SetPropertyStr(ctx, out, "selectedText", JS_NewString(ctx, selected));
        JS_SetPropertyStr(ctx, out, "selectedHighlight", JS_NewBool(ctx, lv_dropdown_get_selected_highlight(obj)));
        JS_SetPropertyStr(ctx, out, "dropdownDir", JS_NewInt32(ctx, (int32_t)lv_dropdown_get_dir(obj)));
        JS_SetPropertyStr(ctx, out, "optionCount", JS_NewInt32(ctx, (int32_t)lv_dropdown_get_option_cnt(obj)));
        JS_SetPropertyStr(ctx, out, "options", JS_NewString(ctx, options ? options : ""));
        JS_SetPropertyStr(ctx, out, "dropdownButtonText", JS_NewString(ctx, text ? text : ""));
        JS_SetPropertyStr(ctx, out, "dropdownSymbol", JS_NewString(ctx, symbol ? symbol : ""));
        JS_SetPropertyStr(ctx, out, "dropdownOpen", JS_NewBool(ctx, list != NULL));
        if (list) {
            lv_obj_update_layout(list);
            JS_SetPropertyStr(ctx, out, "dropdownListX", JS_NewInt32(ctx, (int32_t)lv_obj_get_x(list)));
            JS_SetPropertyStr(ctx, out, "dropdownListY", JS_NewInt32(ctx, (int32_t)lv_obj_get_y(list)));
            JS_SetPropertyStr(ctx, out, "dropdownListWidth", JS_NewInt32(ctx, (int32_t)lv_obj_get_width(list)));
            JS_SetPropertyStr(ctx, out, "dropdownListHeight", JS_NewInt32(ctx, (int32_t)lv_obj_get_height(list)));
        }
        TjsLVGLStyleState *state = tjs_lvgl_style_state_get(obj, false);
        JS_SetPropertyStr(ctx, out, "dropdownText", JS_NewString(ctx, state && state->dropdown_text ? state->dropdown_text : ""));
    }
#endif
#if LV_USE_TEXTAREA != 0
    if (lv_obj_check_type(obj, &lv_textarea_class)) {
        const char *text = lv_textarea_get_text(obj);
        const char *placeholder = lv_textarea_get_placeholder_text(obj);
        JS_SetPropertyStr(ctx, out, "textareaText", JS_NewString(ctx, text ? text : ""));
        JS_SetPropertyStr(ctx, out, "placeholder", JS_NewString(ctx, placeholder ? placeholder : ""));
        JS_SetPropertyStr(ctx, out, "passwordMode", JS_NewBool(ctx, lv_textarea_get_password_mode(obj)));
        JS_SetPropertyStr(ctx, out, "oneLine", JS_NewBool(ctx, lv_textarea_get_one_line(obj)));
        JS_SetPropertyStr(ctx, out, "maxLength", JS_NewInt32(ctx, (int32_t)lv_textarea_get_max_length(obj)));
        TjsLVGLStyleState *state = tjs_lvgl_style_state_get(obj, false);
        JS_SetPropertyStr(ctx, out, "autoKeyboard", JS_NewBool(ctx, state && state->textarea_auto_keyboard_set ? state->textarea_auto_keyboard : false));
        JS_SetPropertyStr(ctx, out, "autoKeyboardSet", JS_NewBool(ctx, state && state->textarea_auto_keyboard_set));
    }
#endif
#if LV_USE_CHECKBOX != 0
    if (lv_obj_check_type(obj, &lv_checkbox_class)) {
        const char *text = lv_checkbox_get_text(obj);
        JS_SetPropertyStr(ctx, out, "text", JS_NewString(ctx, text ? text : ""));
    }
#endif
#if LV_USE_IMG != 0
    if (lv_obj_check_type(obj, &lv_img_class)) {
        const void *src = lv_img_get_src(obj);
        lv_point_t pivot = {0, 0};
        lv_img_get_pivot(obj, &pivot);
        JS_SetPropertyStr(ctx, out, "hasImageSrc", JS_NewBool(ctx, src != NULL));
        const lv_img_t *img = (const lv_img_t *)obj;
        JS_SetPropertyStr(ctx, out, "imageWidth", JS_NewInt32(ctx, (int32_t)img->w));
        JS_SetPropertyStr(ctx, out, "imageHeight", JS_NewInt32(ctx, (int32_t)img->h));
        JS_SetPropertyStr(ctx, out, "imageZoom", JS_NewInt32(ctx, (int32_t)lv_img_get_zoom(obj)));
        JS_SetPropertyStr(ctx, out, "imageAngle", JS_NewInt32(ctx, (int32_t)lv_img_get_angle(obj)));
        JS_SetPropertyStr(ctx, out, "imagePivotX", JS_NewInt32(ctx, (int32_t)pivot.x));
        JS_SetPropertyStr(ctx, out, "imagePivotY", JS_NewInt32(ctx, (int32_t)pivot.y));
        JS_SetPropertyStr(ctx, out, "imageOpacity", JS_NewInt32(ctx, (int32_t)lv_obj_get_style_img_opa(obj, LV_PART_MAIN)));
        JS_SetPropertyStr(ctx, out, "imageRecolor", JS_NewUint32(ctx, (uint32_t)lv_color_to32(lv_obj_get_style_img_recolor(obj, LV_PART_MAIN))));
        JS_SetPropertyStr(ctx, out, "imageRecolorOpacity", JS_NewInt32(ctx, (int32_t)lv_obj_get_style_img_recolor_opa(obj, LV_PART_MAIN)));
    }
#endif
#if LV_USE_CALENDAR != 0
    if (lv_obj_check_type(obj, &lv_calendar_class)) {
        const lv_calendar_date_t *today = lv_calendar_get_today_date(obj);
        const lv_calendar_date_t *shown = lv_calendar_get_showed_date(obj);
        JS_SetPropertyStr(ctx, out, "todayYear", JS_NewInt32(ctx, today ? today->year : 0));
        JS_SetPropertyStr(ctx, out, "todayMonth", JS_NewInt32(ctx, today ? today->month : 0));
        JS_SetPropertyStr(ctx, out, "todayDay", JS_NewInt32(ctx, today ? today->day : 0));
        JS_SetPropertyStr(ctx, out, "shownYear", JS_NewInt32(ctx, shown ? shown->year : 0));
        JS_SetPropertyStr(ctx, out, "shownMonth", JS_NewInt32(ctx, shown ? shown->month : 0));
        JS_SetPropertyStr(ctx, out, "highlightCount", JS_NewInt32(ctx, (int32_t)lv_calendar_get_highlighted_dates_num(obj)));
    }
#endif
    return out;
}

static JSValue tjs_lvgl_debug_emit_event(JSContext *ctx, JSValue this_val, int argc, JSValue *argv) {
    if (argc < 3) {
        return JS_ThrowTypeError(ctx, "Expected 3 arguments: target, currentTarget|null, eventCode");
    }

    lv_obj_t *target = tjs_lvgl_get_obj(ctx, argv[0]);
    if (!target) {
        return JS_EXCEPTION;
    }

    lv_obj_t *current_target = NULL;
    if (!JS_IsUndefined(argv[1]) && !JS_IsNull(argv[1])) {
        current_target = tjs_lvgl_get_obj(ctx, argv[1]);
        if (!current_target) {
            return JS_EXCEPTION;
        }
    }

    int32_t event_code = 0;
    if (JS_ToInt32(ctx, &event_code, argv[2]) < 0) {
        return JS_EXCEPTION;
    }

    tjs_lvgl_event_queue_push(target, current_target, (uint32_t)event_code);
    return JS_UNDEFINED;
}

/* ============================================================
 * Module Initialization (for dynamic loading)
 * ============================================================ */

static const JSCFunctionListEntry tjs_lvgl_funcs[] = {
    JS_CFUNC_DEF("init", 0, tjs_lvgl_init),
    JS_CFUNC_DEF("tick", 0, tjs_lvgl_tick),
    JS_CFUNC_DEF("getDisplaySize", 0, tjs_lvgl_get_display_size),
    JS_CFUNC_DEF("refreshWindow", 0, tjs_lvgl_refresh_window),
    JS_CFUNC_DEF("screenActive", 0, tjs_lvgl_screen_active),
    JS_CFUNC_DEF("objCreate", 1, tjs_lvgl_obj_create),
    JS_CFUNC_DEF("labelCreate", 1, tjs_lvgl_label_create),
    JS_CFUNC_DEF("buttonCreate", 1, tjs_lvgl_button_create),
    JS_CFUNC_DEF("textareaCreate", 1, tjs_lvgl_textarea_create),
    JS_CFUNC_DEF("imgCreate", 1, tjs_lvgl_img_create),
    JS_CFUNC_DEF("sliderCreate", 1, tjs_lvgl_slider_create),
    JS_CFUNC_DEF("switchCreate", 1, tjs_lvgl_switch_create),
    JS_CFUNC_DEF("checkboxCreate", 1, tjs_lvgl_checkbox_create),
    JS_CFUNC_DEF("dropdownCreate", 1, tjs_lvgl_dropdown_create),
    JS_CFUNC_DEF("calendarCreate", 1, tjs_lvgl_calendar_create),
    JS_CFUNC_DEF("tabviewCreate", 3, tjs_lvgl_tabview_create),
    JS_CFUNC_DEF("tabviewAddTab", 3, tjs_lvgl_tabview_add_tab),
    JS_CFUNC_DEF("tabviewSetActive", 2, tjs_lvgl_tabview_set_active),
    JS_CFUNC_DEF("chartCreate", 1, tjs_lvgl_chart_create),
    JS_CFUNC_DEF("setPos", 3, tjs_lvgl_obj_set_pos),
    JS_CFUNC_DEF("setSize", 3, tjs_lvgl_obj_set_size),
    JS_CFUNC_DEF("setStyle", 6, tjs_lvgl_obj_set_style),
    JS_CFUNC_DEF("delete", 1, tjs_lvgl_obj_delete),
    JS_CFUNC_DEF("center", 1, tjs_lvgl_obj_center),
    JS_CFUNC_DEF("appendChild", 2, tjs_lvgl_obj_append_child),
    JS_CFUNC_DEF("insertBefore", 3, tjs_lvgl_obj_insert_before),
    JS_CFUNC_DEF("moveToFront", 1, tjs_lvgl_obj_move_to_front),
    JS_CFUNC_DEF("moveToBackground", 1, tjs_lvgl_obj_move_to_background),
    JS_CFUNC_DEF("removeChild", 2, tjs_lvgl_obj_remove_child),
    JS_CFUNC_DEF("align", 4, tjs_lvgl_obj_align),
    JS_CFUNC_DEF("alignTo", 5, tjs_lvgl_obj_align_to),
    JS_CFUNC_DEF("setEventBubble", 2, tjs_lvgl_set_event_bubble),
    JS_CFUNC_DEF("setText", 2, tjs_lvgl_label_set_text),
    JS_CFUNC_DEF("getText", 1, tjs_lvgl_label_get_text),
    JS_CFUNC_DEF("setLabel", 2, tjs_lvgl_button_set_label),
    JS_CFUNC_DEF("setTextareaText", 2, tjs_lvgl_textarea_set_text),
    JS_CFUNC_DEF("setOneLine", 2, tjs_lvgl_textarea_set_one_line),
    JS_CFUNC_DEF("setPlaceHolder", 2, tjs_lvgl_textarea_set_placeholder),
    JS_CFUNC_DEF("setPasswordMode", 2, tjs_lvgl_textarea_set_password_mode),
    JS_CFUNC_DEF("setMaxLength", 2, tjs_lvgl_textarea_set_max_length),
    JS_CFUNC_DEF("setAutoKeyboard", 2, tjs_lvgl_textarea_set_auto_keyboard),
    JS_CFUNC_DEF("setImageSymbol", 2, tjs_lvgl_img_set_symbol),
    JS_CFUNC_DEF("setImageBinary", 2, tjs_lvgl_img_set_binary),
    JS_CFUNC_DEF("setImageZoom", 2, tjs_lvgl_img_set_zoom),
    JS_CFUNC_DEF("setImageAngle", 2, tjs_lvgl_img_set_angle),
    JS_CFUNC_DEF("setImagePivot", 3, tjs_lvgl_img_set_pivot),
    JS_CFUNC_DEF("setBackgroundSymbol", 3, tjs_lvgl_obj_set_background_symbol),
    JS_CFUNC_DEF("setArcSymbol", 3, tjs_lvgl_obj_set_arc_symbol),
    JS_CFUNC_DEF("setChecked", 2, tjs_lvgl_obj_set_checked),
    JS_CFUNC_DEF("setDisabled", 2, tjs_lvgl_obj_set_disabled),
    JS_CFUNC_DEF("sliderSetRange", 3, tjs_lvgl_slider_set_range),
    JS_CFUNC_DEF("sliderSetValue", 2, tjs_lvgl_slider_set_value),
    JS_CFUNC_DEF("sliderGetValue", 1, tjs_lvgl_slider_get_value),
    JS_CFUNC_DEF("checkboxSetText", 2, tjs_lvgl_checkbox_set_text),
    JS_CFUNC_DEF("dropdownSetOptions", 2, tjs_lvgl_dropdown_set_options),
    JS_CFUNC_DEF("dropdownSetSelected", 2, tjs_lvgl_dropdown_set_selected),
    JS_CFUNC_DEF("dropdownSetText", 2, tjs_lvgl_dropdown_set_text),
    JS_CFUNC_DEF("dropdownSetSymbolDir", 2, tjs_lvgl_dropdown_set_symbol_dir),
    JS_CFUNC_DEF("dropdownSetDir", 2, tjs_lvgl_dropdown_set_dir),
    JS_CFUNC_DEF("dropdownSetSelectedHighlight", 2, tjs_lvgl_dropdown_set_selected_highlight),
    JS_CFUNC_DEF("dropdownGetSelected", 1, tjs_lvgl_dropdown_get_selected),
    JS_CFUNC_DEF("dropdownOpen", 1, tjs_lvgl_dropdown_open),
    JS_CFUNC_DEF("dropdownClose", 1, tjs_lvgl_dropdown_close),
    JS_CFUNC_DEF("calendarSetToday", 4, tjs_lvgl_calendar_set_today),
    JS_CFUNC_DEF("calendarSetShownMonth", 3, tjs_lvgl_calendar_set_shown_month),
    JS_CFUNC_DEF("calendarSetHighlightedDates", 2, tjs_lvgl_calendar_set_highlighted_dates),
    JS_CFUNC_DEF("setTheme", 1, tjs_lvgl_set_theme),
    JS_CFUNC_DEF("resetTheme", 0, tjs_lvgl_reset_theme),
    JS_CFUNC_DEF("chartSetType", 2, tjs_lvgl_chart_set_type),
    JS_CFUNC_DEF("chartSetPointNum", 2, tjs_lvgl_chart_set_point_num),
    JS_CFUNC_DEF("chartSetDivLineCount", 3, tjs_lvgl_chart_set_div_line_count),
    JS_CFUNC_DEF("chartSetRange", 4, tjs_lvgl_chart_set_range),
    JS_CFUNC_DEF("chartSetAxisTick", 8, tjs_lvgl_chart_set_axis_tick),
    JS_CFUNC_DEF("chartSetAxisLabels", 3, tjs_lvgl_chart_set_axis_labels),
    JS_CFUNC_DEF("chartSetLeftAxisData", 2, tjs_lvgl_chart_set_left_axis_data),
    JS_CFUNC_DEF("chartSetRightAxisData", 2, tjs_lvgl_chart_set_right_axis_data),
    JS_CFUNC_DEF("chartSetAllSeries", 2, tjs_lvgl_chart_set_all_series),
    JS_CFUNC_DEF("chartSetScatterData", 2, tjs_lvgl_chart_set_scatter_data),
    JS_CFUNC_DEF("chartGetType", 1, tjs_lvgl_chart_get_type),
    JS_CFUNC_DEF("chartGetPointNum", 1, tjs_lvgl_chart_get_point_num),
    JS_CFUNC_DEF("chartGetSeriesCount", 1, tjs_lvgl_chart_get_series_count),
    JS_CFUNC_DEF("chartDebugGetState", 1, tjs_lvgl_chart_debug_get_state),
    JS_CFUNC_DEF("chartDebugGetSeries", 1, tjs_lvgl_chart_debug_get_series),
    JS_CFUNC_DEF("addEventListener", 2, tjs_lvgl_add_event_listener),
    JS_CFUNC_DEF("removeEventListener", 1, tjs_lvgl_remove_event_listener),
    JS_CFUNC_DEF("pollEvents", 0, tjs_lvgl_poll_events),
    JS_CFUNC_DEF("debugGetParent", 1, tjs_lvgl_debug_get_parent),
    JS_CFUNC_DEF("debugGetIndex", 1, tjs_lvgl_debug_get_index),
    JS_CFUNC_DEF("debugIsFloating", 1, tjs_lvgl_debug_is_floating),
    JS_CFUNC_DEF("debugGetGridTemplate", 1, tjs_lvgl_debug_get_grid_template),
    JS_CFUNC_DEF("debugGetGridCell", 1, tjs_lvgl_debug_get_grid_cell),
    JS_CFUNC_DEF("debugGetScrollable", 1, tjs_lvgl_debug_get_scrollable),
    JS_CFUNC_DEF("debugGetGeometry", 1, tjs_lvgl_debug_get_geometry),
    JS_CFUNC_DEF("debugGetStyle", 2, tjs_lvgl_debug_get_style),
    JS_CFUNC_DEF("debugGetState", 1, tjs_lvgl_debug_get_state),
    JS_CFUNC_DEF("debugEmitEvent", 3, tjs_lvgl_debug_emit_event),
    JS_CFUNC_DEF("debugCalendarPressDay", 2, tjs_lvgl_debug_calendar_press_day),
    JS_CFUNC_DEF("tickHandler", 0, tjs_lvgl_tickHandler),
};

static int xuan_lvgl_module_init(JSContext *ctx, JSModuleDef *m) {
    JSValue ns = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, ns, tjs_lvgl_funcs, countof(tjs_lvgl_funcs));
    return JS_SetModuleExportList(ctx, m, tjs_lvgl_funcs, countof(tjs_lvgl_funcs));
}

JSModuleDef *js_init_module(JSContext *ctx, const char *module_name) {
    JSModuleDef *m = JS_NewCModule(ctx, module_name, xuan_lvgl_module_init);
    if (!m) {
        return NULL;
    }
    JS_AddModuleExportList(ctx, m, tjs_lvgl_funcs, countof(tjs_lvgl_funcs));
    return m;
}
