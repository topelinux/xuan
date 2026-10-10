import assert from 'tjs:assert';

if (!tjs.env.SDL_VIDEODRIVER) {
    tjs.env.SDL_VIDEODRIVER = 'dummy';
}

const lvgl = await import('../xuan_modules/lvgl/index.js');
const lvgljs = await import('../xuan_modules/lvgljs/index.js');
lvgljs.init();

const NC = lvgljs.default.NativeRender.NativeComponents;

const view = new NC.View({ uid: 'position_style_reg_view' });
view._ensureNative();

assert.eq(lvgl.__debugIsFloating(view._native), false, 'default view should not be floating');

view.nativeSetStyle({ position: 'absolute' }, ['position'], 1, 0, false);
assert.eq(lvgl.__debugIsFloating(view._native), true, 'position:absolute should map to LV_OBJ_FLAG_FLOATING');

view.nativeSetStyle({ position: 'fixed' }, ['position'], 1, 0, false);
assert.eq(lvgl.__debugIsFloating(view._native), true, 'position:fixed should map to LV_OBJ_FLAG_FLOATING');

view.close();

if (typeof lvgl.debugGetGeometry === 'function') {
    const root = new (lvgljs.default.NativeRender.NativeComponents.View)({ uid: 'position_align_root' });
    root._ensureNative();
    root.nativeSetStyle({
        width: 200,
        height: 100,
        'padding-left': 0,
        'padding-right': 0,
        'padding-top': 0,
        'padding-bottom': 0,
        'border-width': 0,
    }, [ 'width', 'height', 'padding-left', 'padding-right', 'padding-top', 'padding-bottom', 'border-width' ], 7, 0, true);

    const floating = new (lvgljs.default.NativeRender.NativeComponents.View)({ uid: 'position_align_child' });
    floating.nativeSetStyle({ width: 20, height: 10 }, ['width', 'height'], 2, 0, true);
    floating.align(6, [-5, -7]); /* ALIGN_BOTTOM_RIGHT */
    root.appendChild(floating);
    lvgl.refreshWindow();

    const geom = lvgl.debugGetGeometry(floating._native);
    assert.eq(Number(geom?.x), 175, 'pending ALIGN_BOTTOM_RIGHT should replay after native mount/reparent');
    assert.eq(Number(geom?.y), 83, 'pending ALIGN_BOTTOM_RIGHT y should replay after native mount/reparent');

    floating.nativeSetStyle({ width: 40, height: 10 }, ['width', 'height'], 2, 0, false);
    lvgl.refreshWindow();
    const resized = lvgl.debugGetGeometry(floating._native);
    assert.eq(Number(resized?.x), 155, 'ALIGN_BOTTOM_RIGHT should replay after aligned object size changes');
    assert.eq(Number(resized?.y), 83, 'ALIGN_BOTTOM_RIGHT y should remain anchored after size changes');
    root.close();
}

tjs.exit(0);
