#include <quickjs.h>

static JSValue native_answer(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    return JS_NewInt32(ctx, 42);
}

static const JSCFunctionListEntry native_exports[] = {
    JS_CFUNC_DEF("answer", 0, native_answer),
    JS_PROP_INT32_DEF("version", 1, JS_PROP_C_W_E),
};

static int native_test_init(JSContext *ctx, JSModuleDef *m) {
    return JS_SetModuleExportList(ctx, m, native_exports, sizeof(native_exports) / sizeof(native_exports[0]));
}

JSModuleDef *js_init_module(JSContext *ctx, const char *module_name) {
    JSModuleDef *m = JS_NewCModule(ctx, module_name, native_test_init);
    if (!m) {
        return NULL;
    }
    JS_AddModuleExportList(ctx, m, native_exports, sizeof(native_exports) / sizeof(native_exports[0]));
    return m;
}
