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
    console.log('skip test-lvgljs-root-mount-order-regression:', String(err?.message || err));
    tjs.exit(0);
}

const { View, Text } = lvgljs.default.NativeRender.NativeComponents;

lvgljs.init();

const screen = lvgl.screenActive();
const root = new View({ uid: 'root_view' });
const panel = new View({ uid: 'panel_view' });
const leaf = new Text({ uid: 'leaf_text' });
leaf.setText('MOUNT_ORDER_OK');

// Simulate deep-first mount: create child subtree before root is attached.
leaf._ensureNative();
panel._ensureNative();

const leafParentBefore = lvgl.__debugGetParent(leaf._native);
const panelParentBefore = lvgl.__debugGetParent(panel._native);
assert.eq(String(leafParentBefore?._ptr), String(screen._ptr), 'leaf should start on screen');
assert.eq(String(panelParentBefore?._ptr), String(screen._ptr), 'panel should start on screen');

panel.appendChild(leaf);
root._ensureNative();
root.appendChild(panel);
lvgl.refreshWindow();

const rootParentAfter = lvgl.__debugGetParent(root._native);
const panelParentAfter = lvgl.__debugGetParent(panel._native);
const leafParentAfter = lvgl.__debugGetParent(leaf._native);

assert.ok(root._native, 'root native should be created');
assert.ok(root._native._ptr !== screen._ptr, 'root should be an object under screen, not screen itself');
assert.eq(String(rootParentAfter?._ptr), String(screen._ptr), 'root should be attached to screen');
assert.eq(String(panelParentAfter?._ptr), String(root._native._ptr), 'panel should reparent to root');
assert.eq(String(leafParentAfter?._ptr), String(panel._native._ptr), 'leaf should stay under panel');
assert.eq(leaf._native.getText(), 'MOUNT_ORDER_OK');

root.close();

tjs.exit(0);
