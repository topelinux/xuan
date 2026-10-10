import assert from 'tjs:assert';

if (!tjs.env.SDL_VIDEODRIVER) {
    tjs.env.SDL_VIDEODRIVER = 'dummy';
}

const lvgl = await import('../xuan_modules/lvgl/index.js');
const lvgljs = await import('../xuan_modules/lvgljs/index.js');
lvgljs.init();

const NC = lvgljs.default.NativeRender.NativeComponents;

const parent = new NC.View({ uid: 'life_parent' });
parent._ensureNative();

for (let i = 0; i < 30; i++) {
    const child = new NC.View({ uid: `life_child_${i}` });
    child._ensureNative();
    parent.appendChild(child);
    parent.removeChild(child);
}

// Drive LVGL timers so async deletes are actually executed.
for (let i = 0; i < 20; i++) {
    lvgl.tickHandler();
}

assert.ok(true, 'removeChild lifecycle should not crash with async native deletes');

parent.close();
tjs.exit(0);
