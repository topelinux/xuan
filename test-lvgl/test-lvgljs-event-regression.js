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
    console.log('skip test-lvgljs-event-regression:', String(err?.message || err));
    tjs.exit(0);
}

const { Button, Text, View } = lvgljs.default.NativeRender.NativeComponents;

lvgljs.init();

const fired = [];
globalThis.FIRE_QEVENT_CALLBACK = (targetUid, currentTargetUid, code, payload) => {
    fired.push({ targetUid, currentTargetUid, code, payload });
};

const btn = new Button({ uid: 'key_equal' });
btn.dataset = { num: '=' };
btn.addEventListener(7);

const label = new Text({ uid: 'key_equal_label' });
label.setText('=');
btn.appendChild(label);

lvgl.__debugEmitEvent(label._native, null, 7);
lvgljs.__debugFlushNativeEvents();

assert.eq(fired.length, 1, 'click event should be dispatched once');
assert.eq(fired[0].targetUid, 'key_equal', 'target should be normalized to dataset owner');
assert.eq(fired[0].currentTargetUid, 'key_equal', 'currentTarget should resolve to listener owner');
assert.eq(fired[0].code, 7, 'event code should stay clicked');
assert.eq(typeof fired[0].payload?.stopPropagation, 'function');

btn.close();
delete globalThis.FIRE_QEVENT_CALLBACK;

/* ── bubbling to listener owner with dataset target preserved ─ */

const bubbled = [];
globalThis.FIRE_QEVENT_CALLBACK = (targetUid, currentTargetUid, code, payload) => {
    bubbled.push({ targetUid, currentTargetUid, code, payload });
};

const parent = new View({ uid: 'event_parent' });
parent._ensureNative();
parent.addEventListener(7);

const child = new Button({ uid: 'event_child' });
child.dataset = { id: 'child' };
parent.appendChild(child);

lvgl.__debugEmitEvent(child._native, child._native, 7);
lvgljs.__debugFlushNativeEvents();

assert.eq(bubbled.length, 1, 'bubbled click should dispatch once');
assert.eq(bubbled[0].targetUid, 'event_child', 'dataset child should remain event target');
assert.eq(bubbled[0].currentTargetUid, 'event_parent', 'listener owner should be currentTarget');
assert.eq(bubbled[0].code, 7, 'bubbled event code should stay clicked');

parent.removeEventListener(7);
lvgl.__debugEmitEvent(child._native, child._native, 7);
lvgljs.__debugFlushNativeEvents();
assert.eq(bubbled.length, 1, 'removed listener should not receive later events');

/* Re-adding the same listener should bind once, not duplicate dispatch. */
parent.addEventListener(7);
parent.addEventListener(7);
lvgl.__debugEmitEvent(child._native, child._native, 7);
lvgljs.__debugFlushNativeEvents();
assert.eq(bubbled.length, 2, 'duplicate addEventListener should still dispatch once');

/* A component-specific handler may consume the native event before React dispatch. */
child.addEventListener(7);
child._handleNativeEvent = () => true;
lvgl.__debugEmitEvent(child._native, child._native, 7);
lvgljs.__debugFlushNativeEvents();
assert.eq(bubbled.length, 2, 'consumed event should not reach FIRE_QEVENT_CALLBACK');

parent.close();
delete globalThis.FIRE_QEVENT_CALLBACK;

tjs.exit(0);
