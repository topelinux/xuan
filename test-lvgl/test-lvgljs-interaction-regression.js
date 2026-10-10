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
    console.log('skip test-lvgljs-interaction-regression:', String(err?.message || err));
    tjs.exit(0);
}

const NC = lvgljs.default.NativeRender.NativeComponents;

lvgljs.init();

const root = new NC.View({ uid: 'interaction_root' });
root._ensureNative();
root.nativeSetStyle({ width: 320, height: 240 }, ['width', 'height'], 2, 0, true);

/* Explicit z-order APIs should map to native LVGL child order. */
const zBack = new NC.View({ uid: 'interaction_z_back' });
const zFront = new NC.Mask({ uid: 'interaction_z_front_mask' });
root.appendChild(zBack);
root.appendChild(zFront);
assert.eq(typeof lvgl.__debugGetIndex, 'function', 'native child-index debug helper should be exposed');
assert.eq(lvgl.__debugGetIndex(zBack._native), 0, 'first appended child should start in the background');
assert.eq(lvgl.__debugGetIndex(zFront._native), 1, 'later appended Mask should start above older sibling');
zBack.moveToFront();
assert.eq(lvgl.__debugGetIndex(zBack._native), 1, 'moveToFront should move object above siblings natively');
zBack.moveToBackground();
assert.eq(lvgl.__debugGetIndex(zBack._native), 0, 'moveToBackground should move object behind siblings natively');

const fired = [];
globalThis.FIRE_QEVENT_CALLBACK = (targetUid, currentTargetUid, code, payload) => {
    fired.push({ targetUid, currentTargetUid, code, payload });
};

/* Dropdown value events should carry native selected index and selected text. */
const dropdown = new NC.Dropdownlist({ uid: 'interaction_dropdown' });
root.appendChild(dropdown);
dropdown.setItems(['Alpha', 'Beta', 'Gamma'], 3);
dropdown.setselectIndex(2);
dropdown.addEventListener(28);

lvgl.__debugEmitEvent(dropdown._native, dropdown._native, 28);
lvgljs.__debugFlushNativeEvents();

assert.eq(fired.length, 1, 'dropdown value-change should dispatch once');
assert.eq(fired[0].targetUid, 'interaction_dropdown', 'dropdown target uid should dispatch');
assert.eq(fired[0].currentTargetUid, 'interaction_dropdown', 'dropdown currentTarget uid should dispatch');
assert.eq(fired[0].code, 28, 'dropdown event code should be VALUE_CHANGED');
assert.eq(fired[0].payload.value, 2, 'dropdown value should be native selected index');
assert.eq(lvgl.debugGetState(dropdown._native)?.selectedText, 'Gamma', 'dropdown selected text should update natively');

assert.eq(typeof lvgl.dropdownOpen, 'function', 'dropdown open helper should be exposed');
assert.eq(typeof lvgl.dropdownClose, 'function', 'dropdown close helper should be exposed');
lvgl.dropdownOpen(dropdown._native);
let dropdownState = lvgl.debugGetState(dropdown._native);
assert.eq(dropdownState?.dropdownOpen, true, 'dropdown popup should be observable after open');
assert.eq(dropdownState?.dropdownListWidth >= 1, true, 'dropdown popup should have native width after open');
assert.eq(dropdownState?.dropdownListHeight >= 1, true, 'dropdown popup should have native height after open');
lvgl.dropdownClose(dropdown._native);
dropdownState = lvgl.debugGetState(dropdown._native);
assert.eq(dropdownState?.dropdownOpen, false, 'dropdown popup should close and clear native list');

/* Calendar inside Mask should produce date-string value and bubble to Mask. */
const mask = new NC.Mask({ uid: 'interaction_mask' });
root.appendChild(mask);
mask.addEventListener(7);
mask.addEventListener(28);

const calendar = new NC.Calendar({ uid: 'interaction_calendar' });
mask.appendChild(calendar);
calendar.setToday(2026, 5, 12);
calendar.setShownMonth(2026, 5);
calendar.addEventListener(28);

assert.eq(typeof lvgl.__debugCalendarPressDay, 'function', 'calendar press helper should be exposed');
lvgl.__debugCalendarPressDay(calendar._native, 12);
lvgljs.__debugFlushNativeEvents();

const calendarEvent = fired[fired.length - 1];
assert.eq(calendarEvent.targetUid, 'interaction_calendar', 'calendar date event target should be calendar');
assert.eq(calendarEvent.currentTargetUid, 'interaction_calendar', 'calendar listener should be currentTarget first');
assert.eq(calendarEvent.code, 28, 'calendar event code should be VALUE_CHANGED');
assert.eq(calendarEvent.payload.value, '2026-05-12', 'calendar event value should be selected date string');
assert.eq(calendarEvent.payload.textValue, '2026-05-12', 'calendar textValue should retain selected date string');

calendar.removeEventListener(28);
lvgl.__debugCalendarPressDay(calendar._native, 13);
lvgljs.__debugFlushNativeEvents();

const maskCalendarEvent = fired[fired.length - 1];
assert.eq(maskCalendarEvent.targetUid, 'interaction_calendar', 'bubbled calendar event should preserve calendar target');
assert.eq(maskCalendarEvent.currentTargetUid, 'interaction_mask', 'mask should receive bubbled calendar event');
assert.eq(maskCalendarEvent.payload.value, '2026-05-13', 'bubbled calendar event should keep date string payload');

lvgl.__debugEmitEvent(calendar._native, calendar._native, 7);
lvgljs.__debugFlushNativeEvents();
const maskClickEvent = fired[fired.length - 1];
assert.eq(maskClickEvent.targetUid, 'interaction_mask', 'calendar click without dataset should normalize to mask listener target');
assert.eq(maskClickEvent.currentTargetUid, 'interaction_mask', 'mask click listener should be currentTarget');
assert.eq(maskClickEvent.code, 7, 'mask click event should stay CLICKED');

/* Textarea/Input native focus path should work with autoKeyboard disabled. */
const textarea = new NC.Textarea({ uid: 'interaction_textarea' });
root.appendChild(textarea);
textarea.setOneLine(true);
textarea.setAutoKeyboard(false);
textarea.addEventListener(14);
textarea.addEventListener(15);

const textareaState = lvgl.debugGetState(textarea._native);
assert.eq(textareaState?.autoKeyboardSet, true, 'textarea autoKeyboard setter should be observable');
assert.eq(textareaState?.autoKeyboard, false, 'textarea autoKeyboard=false should be retained as a desktop no-op');

lvgl.__debugEmitEvent(textarea._native, textarea._native, 14);
lvgl.__debugEmitEvent(textarea._native, textarea._native, 15);
lvgljs.__debugFlushNativeEvents();

const focusEvent = fired[fired.length - 2];
const blurEvent = fired[fired.length - 1];
assert.eq(focusEvent.targetUid, 'interaction_textarea', 'textarea focus target should dispatch');
assert.eq(focusEvent.currentTargetUid, 'interaction_textarea', 'textarea focus currentTarget should dispatch');
assert.eq(focusEvent.code, 14, 'textarea focus event code should be FOCUSED');
assert.eq(blurEvent.targetUid, 'interaction_textarea', 'textarea blur target should dispatch');
assert.eq(blurEvent.currentTargetUid, 'interaction_textarea', 'textarea blur currentTarget should dispatch');
assert.eq(blurEvent.code, 15, 'textarea blur event code should be DEFOCUSED');

delete globalThis.FIRE_QEVENT_CALLBACK;
root.close();

tjs.exit(0);
