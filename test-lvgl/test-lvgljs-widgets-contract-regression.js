import assert from 'tjs:assert';

if (!tjs.env.SDL_VIDEODRIVER) {
    tjs.env.SDL_VIDEODRIVER = 'dummy';
}

const lvgljs = await import('../xuan_modules/lvgljs/index.js');
const lvgl = await import('../xuan_modules/lvgl/index.js');
lvgljs.init();

const NC = lvgljs.default.NativeRender.NativeComponents;
const rgb24 = (value) => (Number(value) >>> 0) & 0x00ffffff;

for (const name of [
    'View',
    'Text',
    'Button',
    'Textarea',
    'Image',
    'Switch',
    'Slider',
    'Checkbox',
    'Dropdownlist',
    'Calendar',
    'Chart',
    'Mask',
    'TabView',
]) {
    assert.eq(typeof NC[name], 'function', `${name} should be exposed in NativeComponents`);
}

assert.eq(typeof lvgl.tabviewCreate, 'function', 'native TabView create should be available for widgets parity');
assert.eq(typeof lvgl.tabviewAddTab, 'function', 'native TabView addTab should be available for widgets parity');
assert.eq(typeof lvgl.chartSetAxisLabels, 'function', 'native Chart axis label bridge should be available for widgets parity');

const root = new NC.View({ uid: 'widgets_contract_root' });
root._ensureNative();
root.nativeSetStyle({ width: 320, height: 240 }, ['width', 'height'], 2, 0, true);

const view = new NC.View({ uid: 'widgets_contract_view' });
root.appendChild(view);
view.nativeSetStyle({ width: 120, height: 80 }, ['width', 'height'], 2, 0, true);

const text = new NC.Text({ uid: 'widgets_contract_text' });
root.appendChild(text);
text.setText('text');

const button = new NC.Button({ uid: 'widgets_contract_button' });
root.appendChild(button);
const preMountBackgroundSymbolButton = new NC.Button({ uid: 'widgets_contract_pre_mount_bg_symbol_button' });
preMountBackgroundSymbolButton.setBackgroundImage(null, 0, '\uF043');
root.appendChild(preMountBackgroundSymbolButton);
assert.eq(
    lvgl.debugGetStyle(preMountBackgroundSymbolButton._native)?.['background-image-symbol'],
    '\uF043',
    'pre-mount background-image symbols should replay after native creation'
);

const textarea = new NC.Textarea({ uid: 'widgets_contract_textarea' });
root.appendChild(textarea);
textarea.setText('hello');
textarea.setPlaceHolder('placeholder');
textarea.setOneLine(true);
textarea.setPasswordMode(false);
textarea.setMaxLength(32);
textarea.setAutoKeyboard(false);
const preMountTextarea = new NC.Textarea({ uid: 'widgets_contract_pre_mount_textarea' });
preMountTextarea.setText('pre');
preMountTextarea.setPlaceHolder('pre-placeholder');
preMountTextarea.setOneLine(true);
preMountTextarea.setPasswordMode(true);
preMountTextarea.setMaxLength(4);
preMountTextarea.setAutoKeyboard(false);
root.appendChild(preMountTextarea);

const image = new NC.Image({ uid: 'widgets_contract_image' });
root.appendChild(image);

const sw = new NC.Switch({ uid: 'widgets_contract_switch' });
root.appendChild(sw);
sw.setChecked(true);
sw.setDisabled(false);

const slider = new NC.Slider({ uid: 'widgets_contract_slider' });
root.appendChild(slider);
slider.setRange([0, 100]);
slider.setValue(12);

const checkbox = new NC.Checkbox({ uid: 'widgets_contract_checkbox' });
root.appendChild(checkbox);
checkbox.setText('check');
checkbox.setChecked(true);
checkbox.setDisabled(false);

const dd = new NC.Dropdownlist({ uid: 'widgets_contract_dropdownlist' });
root.appendChild(dd);
dd.setItems(['A', 'B', 'C'], 3);
dd.setselectIndex(1);
dd.setArrowDir(0);
dd.setDir(0);
dd.setHighLightSelect(true);

const calendar = new NC.Calendar({ uid: 'widgets_contract_calendar' });
root.appendChild(calendar);
calendar.setToday(2026, 4, 12);
calendar.setShownMonth(2026, 4);
calendar.setHighlightDates([[2026, 4, 12]], 1);

assert.eq(typeof lvgl.debugGetState, 'function', 'native debugGetState should be exposed');
const png1x1 = new Uint8Array([
    137, 80, 78, 71, 13, 10, 26, 10, 0, 0, 0, 13, 73, 72, 68, 82,
    0, 0, 0, 1, 0, 0, 0, 1, 8, 6, 0, 0, 0, 31, 21, 196,
    137, 0, 0, 0, 10, 73, 68, 65, 84, 120, 156, 99, 0, 1, 0,
    0, 5, 0, 1, 13, 10, 45, 180, 0, 0, 0, 0, 73, 69, 78, 68,
    174, 66, 96, 130,
]);
image.setImageBinary(png1x1.buffer);
const imagePngState = lvgl.debugGetState(image._native);
assert.eq(imagePngState?.hasImageSrc, true, 'Image should keep a native LVGL image source');
assert.eq(imagePngState?.imageWidth, 1, 'Image PNG binary width should be parsed from header');
assert.eq(imagePngState?.imageHeight, 1, 'Image PNG binary height should be parsed from header');
image.nativeSetStyle({
    width: 40,
    height: 30,
    'img-scale': 512,
    'img-rotate': 90,
    'img-origin': [5, 6],
    'img-recolor': 0x336699,
    'img-recolor-opacity': 127,
    'img-opacity': 128,
}, [
    'width',
    'height',
    'img-scale',
    'img-rotate',
    'img-origin',
    'img-recolor',
    'img-recolor-opacity',
    'img-opacity',
], 8, 0, false);
const imageSizedState = lvgl.debugGetState(image._native);
const imageGeometry = lvgl.debugGetGeometry(image._native);
assert.eq(imageSizedState?.imageWidth, 1, 'Image PNG natural width should stay independent from explicit style width');
assert.eq(imageSizedState?.imageHeight, 1, 'Image PNG natural height should stay independent from explicit style height');
assert.eq(imageGeometry?.width, 40, 'Image explicit style width should reach native geometry');
assert.eq(imageGeometry?.height, 30, 'Image explicit style height should reach native geometry');
assert.eq(imageSizedState?.imageZoom, 512, 'Image img-scale should reach native zoom');
assert.eq(imageSizedState?.imageAngle, 90, 'Image img-rotate should match upstream native angle units');
assert.eq(imageSizedState?.imagePivotX, 5, 'Image transform-origin x should reach native pivot');
assert.eq(imageSizedState?.imagePivotY, 6, 'Image transform-origin y should reach native pivot');
assert.eq(rgb24(imageSizedState?.imageRecolor), 0x336699, 'Image recolor should reach native style');
assert.eq(imageSizedState?.imageRecolorOpacity, 127, 'Image recolor opacity should reach native style');
assert.eq(imageSizedState?.imageOpacity, 128, 'Image opacity should reach native img style');
const switchState = lvgl.debugGetState(sw._native);
assert.eq(switchState?.checked, true, 'Switch checked state should reach native LVGL');
assert.eq(switchState?.disabled, false, 'Switch disabled state should reach native LVGL');
const sliderState = lvgl.debugGetState(slider._native);
assert.eq(sliderState?.value, 12, 'Slider value should reach native LVGL');
const textareaState = lvgl.debugGetState(textarea._native);
assert.eq(textareaState?.textareaText, 'hello', 'Textarea text should reach native LVGL');
assert.eq(textareaState?.placeholder, 'placeholder', 'Textarea placeholder should reach native LVGL');
assert.eq(textareaState?.oneLine, true, 'Textarea one-line mode should reach native LVGL');
assert.eq(textareaState?.passwordMode, false, 'Textarea password mode should reach native LVGL');
assert.eq(textareaState?.maxLength, 32, 'Textarea maxLength should reach native LVGL');
const preMountTextareaState = lvgl.debugGetState(preMountTextarea._native);
assert.eq(preMountTextareaState?.textareaText, 'pre', 'pre-mount Textarea text should replay to native LVGL');
assert.eq(preMountTextareaState?.placeholder, 'pre-placeholder', 'pre-mount Textarea placeholder should replay to native LVGL');
assert.eq(preMountTextareaState?.oneLine, true, 'pre-mount Textarea oneLine should replay to native LVGL');
assert.eq(preMountTextareaState?.passwordMode, true, 'pre-mount Textarea passwordMode should replay to native LVGL');
assert.eq(preMountTextareaState?.maxLength, 4, 'pre-mount Textarea maxLength should replay to native LVGL');
const checkboxState = lvgl.debugGetState(checkbox._native);
assert.eq(checkboxState?.text, 'check', 'Checkbox text should reach native LVGL');
assert.eq(checkboxState?.checked, true, 'Checkbox checked state should reach native LVGL');
assert.eq(checkboxState?.disabled, false, 'Checkbox disabled state should reach native LVGL');
const dropdownState = lvgl.debugGetState(dd._native);
assert.eq(dropdownState?.selected, 1, 'Dropdown selected index should reach native LVGL');
assert.eq(dropdownState?.selectedText, 'B', 'Dropdown selected text should come from native LVGL options');
assert.eq(dropdownState?.selectedHighlight, true, 'Dropdown highlight setting should reach native LVGL');
assert.eq(dropdownState?.optionCount, 3, 'Dropdown option count should reach native LVGL');
assert.eq(dropdownState?.options, 'A\nB\nC', 'Dropdown option text should be stored natively');
dd.setArrowDir(1);
dd.setDir(8);
let dropdownDetailState = lvgl.debugGetState(dd._native);
assert.eq(dropdownDetailState?.dropdownDir, 8, 'Dropdown popup direction should reach native LVGL');
assert.eq(dropdownDetailState?.dropdownSymbol !== '', true, 'Dropdown arrow symbol should reach native LVGL');
dd.setText('custom');
assert.eq(lvgl.debugGetState(dd._native)?.dropdownText, 'custom', 'Dropdown custom text should be owned by native state');
assert.eq(lvgl.debugGetState(dd._native)?.dropdownButtonText, 'custom', 'Dropdown button custom text should reach native LVGL');
dd.setText(null);
assert.eq(lvgl.debugGetState(dd._native)?.dropdownText, '', 'Dropdown custom text should clear safely');
assert.eq(lvgl.debugGetState(dd._native)?.dropdownButtonText, '', 'Dropdown native custom text should clear safely');
const calendarState = lvgl.debugGetState(calendar._native);
assert.eq(calendarState?.todayYear, 2026, 'Calendar today year should reach native LVGL');
assert.eq(calendarState?.todayMonth, 4, 'Calendar today month should reach native LVGL');
assert.eq(calendarState?.todayDay, 12, 'Calendar today day should reach native LVGL');
assert.eq(calendarState?.shownYear, 2026, 'Calendar shown year should reach native LVGL');
assert.eq(calendarState?.shownMonth, 4, 'Calendar shown month should reach native LVGL');
assert.eq(calendarState?.highlightCount, 1, 'Calendar highlighted dates should reach native LVGL');
calendar.setHighlightDates([], 0);
assert.eq(lvgl.debugGetState(calendar._native)?.highlightCount, 0, 'Calendar highlighted dates should clear without leaving stale native pointers');
calendar.setHighlightDates([[2026, 4, 12]], 1);

const chart = new NC.Chart({ uid: 'widgets_contract_chart' });
root.appendChild(chart);
chart.setType(1);
chart.setDivLineCount([2, 3]);
chart.setPointNum(8);
chart.setLeftAxisData([{ data: [1, 2, 3] }]);
chart.setLeftAxisOption({ majorLen: 4, minorLen: 2, majorNum: 3, minorNum: 1, labelEn: true, drawSize: 40 });
chart.setRightAxisData([{ data: [4, 5, 6] }]);
chart.setBottomAxisLabels(['A', 'B', 'C']);

const mask = new NC.Mask({ uid: 'widgets_contract_mask' });
root.appendChild(mask);
mask.appendChild(new NC.View({ uid: 'widgets_contract_mask_child' }));
const maskCalendar = new NC.Calendar({ uid: 'widgets_contract_mask_calendar' });
mask.appendChild(maskCalendar);
maskCalendar.setToday(2026, 5, 7);
assert.eq(lvgl.debugGetState(maskCalendar._native)?.todayDay, 7, 'Calendar in Mask should be native before disposal');
mask.removeChild(maskCalendar);
assert.eq(maskCalendar._disposed, true, 'Mask removeChild should dispose child Calendar');
assert.eq(maskCalendar._native, null, 'Mask child Calendar should clear native pointer after removeChild');

const tab = new NC.TabView({ uid: 'widgets_contract_tabview', tabPosition: 0, tabSize: 60 });
root.appendChild(tab);
tab.setTab('one', new NC.View({ uid: 'widgets_contract_tab_1' }));
tab.setTab('two', new NC.View({ uid: 'widgets_contract_tab_2' }));
assert.eq(tab._pages.length, 2, 'TabView should store pages');
assert.eq(tab._tabButtons.length, 2, 'TabView should store tab buttons');

/* Removed or closed components must not be able to recreate native objects. */
const disposed = new NC.Button({ uid: 'widgets_contract_disposed_button' });
root.appendChild(disposed);
root.removeChild(disposed);
assert.eq(disposed._disposed, true, 'removeChild should dispose child');
assert.eq(disposed._native, null, 'disposed child should clear native object');
const childCount = root._children.length;
root.appendChild(disposed);
assert.eq(root._children.length, childCount, 'disposed child should not be re-appended');
assert.eq(disposed._ensureNative(), null, 'disposed child should not recreate native object');
assert.eq(disposed._native, null, 'disposed child native should stay null');

/* Widget-specific post-dispose calls should be safe no-ops. */
text.close();
text.setText('after-close');
textarea.close();
textarea.setText('after-close');
textarea.setPlaceHolder('after-close');
textarea.setOneLine(false);
textarea.setPasswordMode(true);
textarea.setMaxLength(1);
textarea.setAutoKeyboard(true);
image.close();
image.setSymbol('after-close');
image.setImageBinary(new ArrayBuffer(0));
sw.close();
sw.setChecked(false);
sw.setDisabled(true);
slider.close();
slider.setRange([1, 2]);
slider.setValue(2);
checkbox.close();
checkbox.setText('after-close');
checkbox.setChecked(false);
checkbox.setDisabled(true);
dd.close();
dd.setItems(['Z'], 1);
dd.setselectIndex(0);
dd.setArrowDir(1);
dd.setDir(1);
dd.setHighLightSelect(false);
calendar.close();
calendar.setToday(2000, 1, 1);
calendar.setShownMonth(2000, 1);
calendar.setHighlightDates([[2000, 1, 1]], 1);
chart.close();
chart.setType(2);
chart.setDivLineCount([9, 9]);
chart.setPointNum(9);
chart.setLeftAxisData([{ data: [9] }]);
chart.setRightAxisData([{ data: [9] }]);
chart.setScatterData([{ data: [1, 2] }]);
mask.close();
tab.close();
tab.setTab('ghost', new NC.View({ uid: 'widgets_contract_ghost_tab' }));
assert.eq(tab._pages.length, 0, 'closed TabView should clear and reject pages');

root.close();

tjs.exit(0);
