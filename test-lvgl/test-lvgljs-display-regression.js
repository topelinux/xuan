import assert from 'tjs:assert';

if (!tjs.env.SDL_VIDEODRIVER) {
    tjs.env.SDL_VIDEODRIVER = 'dummy';
}

let lvgl;
let lvgljs;
try {
    lvgl = await import('../xuan_modules/lvgl/index.js');
    lvgljs = await import('../xuan_modules/lvgljs/index.js');
} catch (err) {
    console.log('skip test-lvgljs-display-regression:', String(err?.message || err));
    tjs.exit(0);
}

const { View, Text } = lvgljs.default.NativeRender.NativeComponents;

lvgljs.init();

const dims = lvgljs.default.NativeRender.dimensions.window;
assert.eq(dims.width, 1024, 'xuan SDL simulator width should match lv_binding_js widgets baseline');
assert.eq(dims.height, 600, 'xuan SDL simulator height should match lv_binding_js widgets baseline');

const screen = lvgl.screenActive();

const directText = new Text({ uid: 'display_direct_text' });
directText.setText('DIRECT_OK');
directText._ensureNative();
assert.eq(directText._native.getText(), 'DIRECT_OK');

const directParent = lvgl.__debugGetParent(directText._native);
assert.ok(directParent, 'direct text should have a parent');
assert.eq(String(directParent._ptr), String(screen._ptr), 'direct text should stay on active screen');

const panel = new View({ uid: 'display_panel' });
const childText = new Text({ uid: 'display_child_text' });
childText.setText('CHILD_OK');
childText._ensureNative();

const beforeReparent = lvgl.__debugGetParent(childText._native);
assert.ok(beforeReparent, 'child text should have parent before reparent');
assert.eq(String(beforeReparent._ptr), String(screen._ptr), 'child pre-attach parent should be screen');

panel._ensureNative();
panel.appendChild(childText);
lvgl.refreshWindow();

const afterReparent = lvgl.__debugGetParent(childText._native);
assert.ok(afterReparent, 'child text should have parent after reparent');
assert.eq(String(afterReparent._ptr), String(panel._native._ptr), 'child should be reparented to container');
assert.eq(childText._native.getText(), 'CHILD_OK');

panel.close();
directText.close();

tjs.exit(0);
