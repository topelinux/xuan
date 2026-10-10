/**
 * LVGL module for xuan runtime
 * 
 * Usage:
 *   const { init, screenActive, labelCreate, buttonCreate } = await import('lvgl');
 *   
 *   // or with default import:
 *   const lvgl = await import('lvgl');
 *   lvgl.init();
 */

const platform = globalThis.navigator?.userAgentData?.platform ?? '';
const isWindows = platform === 'windows' || platform === 'Windows';
const bindingExts = isWindows ? [ 'dll', 'so', 'dylib' ] : [ 'so', 'dylib', 'dll' ];
const bindingRoots = [ './native', '../../build' ];
const bindingPaths = [];
for (const root of bindingRoots) {
    for (const ext of bindingExts) {
        bindingPaths.push(`${root}/xuan-lvgl.${ext}`);
    }
}

let native;
let lastErr;
for (const p of bindingPaths) {
    try {
        native = await import(p);
        break;
    } catch (err) {
        lastErr = err;
    }
}
if (!native) {
    throw lastErr;
}

/* ============================================================
 * Wrapper Classes
 * ============================================================ */

const kPtr = Symbol('kLvglPtr');

class LvObject {
    constructor(ptr) {
        this[kPtr] = ptr;
    }

    get _ptr() { return this[kPtr]; }

    setPos(x, y) { return native.setPos(this[kPtr], x, y); }
    setSize(w, h) { return native.setSize(this[kPtr], w, h); }
    setStyle(styleObj = {}, keys = [], len = keys.length, styleType = 0, isInit = true) {
        return native.setStyle(this[kPtr], styleObj, keys, len, styleType, isInit);
    }
    center() { return native.center(this[kPtr]); }
    appendChild(child) { return native.appendChild(this[kPtr], child?.[kPtr] ?? child?._ptr ?? child); }
    insertBefore(child, beforeChild) { return native.insertBefore(this[kPtr], child?.[kPtr] ?? child?._ptr ?? child, beforeChild?.[kPtr] ?? beforeChild?._ptr ?? beforeChild); }
    removeChild(child) { return native.removeChild(this[kPtr], child?.[kPtr] ?? child?._ptr ?? child); }
    moveToFront() { return native.moveToFront(this[kPtr]); }
    moveToBackground() { return native.moveToBackground(this[kPtr]); }
    align(type, x = 0, y = 0) { return native.align(this[kPtr], type, x, y); }
    alignTo(base, type, x = 0, y = 0) { return native.alignTo(this[kPtr], base?.[kPtr] ?? base?._ptr ?? base, type, x, y); }
    addEventListener(type) { return native.addEventListener(this[kPtr], type); }
    removeEventListener() { return native.removeEventListener(this[kPtr]); }
    setEventBubble(enabled = true) { return native.setEventBubble(this[kPtr], !!enabled); }
    delete() { return native.delete(this[kPtr]); }
}

class Label extends LvObject {
    setText(text) { return native.setText(this[kPtr], text); }
    getText() { return native.getText(this[kPtr]); }
}

class Button extends LvObject {
    setLabel(text) { return native.setLabel(this[kPtr], text); }
}

class Textarea extends LvObject {
    setText(text) { return native.setTextareaText(this[kPtr], text); }
    setOneLine(oneLine) { return native.setOneLine(this[kPtr], !!oneLine); }
    setPlaceHolder(text) { return native.setPlaceHolder(this[kPtr], text ?? ''); }
    setPasswordMode(enabled) { return native.setPasswordMode(this[kPtr], !!enabled); }
    setMaxLength(maxLen) { return native.setMaxLength(this[kPtr], maxLen | 0); }
    setAutoKeyboard(enabled) { return native.setAutoKeyboard(this[kPtr], !!enabled); }
}

class Image extends LvObject {
    setSymbol(symbol) { return native.setImageSymbol(this[kPtr], symbol ?? ''); }
    setImageBinary(data) { return native.setImageBinary(this[kPtr], data); }
    setZoom(zoom) { return native.setImageZoom(this[kPtr], zoom | 0); }
    setAngle(angle) { return native.setImageAngle(this[kPtr], angle | 0); }
    setPivot(x, y) { return native.setImagePivot(this[kPtr], x | 0, y | 0); }
}

class Slider extends LvObject {
    setRange(min, max) { return native.sliderSetRange(this[kPtr], min | 0, max | 0); }
    setValue(value) { return native.sliderSetValue(this[kPtr], value | 0); }
    getValue() { return native.sliderGetValue(this[kPtr]); }
}

class Checkbox extends LvObject {
    setText(text) { return native.checkboxSetText(this[kPtr], text ?? ''); }
}

class Dropdown extends LvObject {
    setItems(items) { return native.dropdownSetOptions(this[kPtr], Array.isArray(items) ? items : []); }
    setSelected(index) { return native.dropdownSetSelected(this[kPtr], index | 0); }
    setText(text) { return native.dropdownSetText(this[kPtr], text ?? null); }
    setSymbolDir(dir) { return native.dropdownSetSymbolDir(this[kPtr], dir | 0); }
    setDir(dir) { return native.dropdownSetDir(this[kPtr], dir | 0); }
    setSelectedHighlight(enabled) { return native.dropdownSetSelectedHighlight(this[kPtr], !!enabled); }
    getSelected() { return native.dropdownGetSelected(this[kPtr]); }
    open() { return native.dropdownOpen(this[kPtr]); }
    closePopup() { return native.dropdownClose(this[kPtr]); }
}

class Calendar extends LvObject {
    setToday(y, m, d) { return native.calendarSetToday(this[kPtr], y | 0, m | 0, d | 0); }
    setShownMonth(y, m) { return native.calendarSetShownMonth(this[kPtr], y | 0, m | 0); }
    setHighlightDates(dates) { return native.calendarSetHighlightedDates(this[kPtr], Array.isArray(dates) ? dates : []); }
}

/* ============================================================
 * Exported Functions
 * ============================================================ */

export function init(...args) { return native.init(...args); }
export function tick() { return native.tick(); }
export function getDisplaySize() { return native.getDisplaySize(); }
export function refreshWindow() { return native.refreshWindow(); }
export function setStyle(target, styleObj = {}, keys = [], len = keys.length, styleType = 0, isInit = true) {
    const ptr = target && typeof target === 'object' ? (target[kPtr] ?? target._ptr) : target;
    return native.setStyle(ptr, styleObj, keys, len, styleType, isInit);
}
export function tickHandler() { return native.tickHandler(); }
export function addEventListener(target, type) {
    const ptr = target && typeof target === 'object' ? (target[kPtr] ?? target._ptr) : target;
    return native.addEventListener(ptr, type);
}
export function removeEventListener(target) {
    const ptr = target && typeof target === 'object' ? (target[kPtr] ?? target._ptr) : target;
    return native.removeEventListener(ptr);
}
export function setEventBubble(target, enabled = true) {
    const ptr = target && typeof target === 'object' ? (target[kPtr] ?? target._ptr) : target;
    return native.setEventBubble(ptr, !!enabled);
}
export function moveToFront(target) {
    if (typeof native.moveToFront !== 'function') return;
    const ptr = target && typeof target === 'object' ? (target[kPtr] ?? target._ptr) : target;
    return native.moveToFront(ptr);
}
export function moveToBackground(target) {
    if (typeof native.moveToBackground !== 'function') return;
    const ptr = target && typeof target === 'object' ? (target[kPtr] ?? target._ptr) : target;
    return native.moveToBackground(ptr);
}
export function pollEvents() {
    return native.pollEvents();
}

export function __debugGetParent(target) {
    if (typeof native.debugGetParent !== 'function') {
        return null;
    }
    const ptr = target && typeof target === 'object' ? (target[kPtr] ?? target._ptr) : target;
    const parentPtr = native.debugGetParent(ptr);
    if (parentPtr == null) {
        return null;
    }
    return new LvObject(parentPtr);
}

export function __debugGetIndex(target) {
    if (typeof native.debugGetIndex !== 'function') {
        return -1;
    }
    const ptr = target && typeof target === 'object' ? (target[kPtr] ?? target._ptr) : target;
    return native.debugGetIndex(ptr);
}

export function __debugIsFloating(target) {
    if (typeof native.debugIsFloating !== 'function') {
        return false;
    }
    const ptr = target && typeof target === 'object' ? (target[kPtr] ?? target._ptr) : target;
    return !!native.debugIsFloating(ptr);
}

export function __debugEmitEvent(target, currentTarget, code) {
    if (typeof native.debugEmitEvent !== 'function') {
        return;
    }
    const targetPtr = target && typeof target === 'object' ? (target[kPtr] ?? target._ptr) : target;
    const currentPtr = currentTarget && typeof currentTarget === 'object'
        ? (currentTarget[kPtr] ?? currentTarget._ptr)
        : currentTarget;
    return native.debugEmitEvent(targetPtr, currentPtr ?? null, code);
}

export function __debugCalendarPressDay(target, day) {
    if (typeof native.debugCalendarPressDay !== 'function') {
        return;
    }
    const ptr = target && typeof target === 'object' ? (target[kPtr] ?? target._ptr) : target;
    return native.debugCalendarPressDay(ptr, day | 0);
}

export function __debugGetGridTemplate(target) {
    if (typeof native.debugGetGridTemplate !== 'function') {
        return null;
    }
    const ptr = target && typeof target === 'object' ? (target[kPtr] ?? target._ptr) : target;
    return native.debugGetGridTemplate(ptr);
}

export function debugGetGridCell(target) {
    if (typeof native.debugGetGridCell !== 'function') {
        return null;
    }
    const ptr = target && typeof target === 'object' ? (target[kPtr] ?? target._ptr) : target;
    return native.debugGetGridCell(ptr);
}

export function debugGetScrollable(target) {
    if (typeof native.debugGetScrollable !== 'function') {
        return false;
    }
    const ptr = target && typeof target === 'object' ? (target[kPtr] ?? target._ptr) : target;
    return !!native.debugGetScrollable(ptr);
}

export function debugGetGeometry(target) {
    if (typeof native.debugGetGeometry !== 'function') {
        return null;
    }
    const ptr = target && typeof target === 'object' ? (target[kPtr] ?? target._ptr) : target;
    return native.debugGetGeometry(ptr);
}

export function debugGetStyle(target, styleType = 0) {
    if (typeof native.debugGetStyle !== 'function') {
        return null;
    }
    const ptr = target && typeof target === 'object' ? (target[kPtr] ?? target._ptr) : target;
    return native.debugGetStyle(ptr, styleType | 0);
}

export function debugGetState(target) {
    if (typeof native.debugGetState !== 'function') {
        return null;
    }
    const ptr = target && typeof target === 'object' ? (target[kPtr] ?? target._ptr) : target;
    return native.debugGetState(ptr);
}

/* Auto-start refresh loop when init is called */
let refreshInterval = null;
export function initAuto(...args) {
    const result = native.init(...args);
    /* Start refresh loop at ~33 FPS */
    if (!refreshInterval) {
        refreshInterval = setInterval(() => {
            const alive = native.tickHandler();
            if (alive === false) {
                clearInterval(refreshInterval);
                refreshInterval = null;
            }
        }, 30);
    }
    return result;
}

export function screenActive() {
    const ptr = native.screenActive();
    return new LvObject(ptr);
}

export function objCreate(parent) {
    const ptr = native.objCreate(parent ? parent[kPtr] ?? parent : null);
    return new LvObject(ptr);
}

export function labelCreate(parent) {
    const ptr = native.labelCreate(parent ? parent[kPtr] ?? parent : null);
    return new Label(ptr);
}

export function buttonCreate(parent) {
    const ptr = native.buttonCreate(parent ? parent[kPtr] ?? parent : null);
    return new Button(ptr);
}

export function textareaCreate(parent) {
    const ptr = native.textareaCreate(parent ? parent[kPtr] ?? parent : null);
    return new Textarea(ptr);
}

export function imgCreate(parent) {
    const ptr = native.imgCreate(parent ? parent[kPtr] ?? parent : null);
    return new Image(ptr);
}

export function sliderCreate(parent) {
    const ptr = native.sliderCreate(parent ? parent[kPtr] ?? parent : null);
    return new Slider(ptr);
}

export function switchCreate(parent) {
    const ptr = native.switchCreate(parent ? parent[kPtr] ?? parent : null);
    return new LvObject(ptr);
}

export function checkboxCreate(parent) {
    const ptr = native.checkboxCreate(parent ? parent[kPtr] ?? parent : null);
    return new Checkbox(ptr);
}

export function dropdownCreate(parent) {
    const ptr = native.dropdownCreate(parent ? parent[kPtr] ?? parent : null);
    return new Dropdown(ptr);
}

export function calendarCreate(parent) {
    const ptr = native.calendarCreate(parent ? parent[kPtr] ?? parent : null);
    return new Calendar(ptr);
}

export function tabviewCreate(parent, tabPosition = 4, tabSize = 56) {
    const ptr = native.tabviewCreate(parent ? parent[kPtr] ?? parent : null, tabPosition | 0, tabSize | 0);
    return new LvObject(ptr);
}

export function tabviewAddTab(target, name, child) {
    const targetPtr = target && typeof target === 'object' ? (target[kPtr] ?? target._ptr) : target;
    const childPtr = child && typeof child === 'object' ? (child[kPtr] ?? child._ptr) : child;
    const ptr = native.tabviewAddTab(targetPtr, String(name ?? ''), childPtr);
    return new LvObject(ptr);
}

export function tabviewSetActive(target, index) {
    const ptr = target && typeof target === 'object' ? (target[kPtr] ?? target._ptr) : target;
    return native.tabviewSetActive(ptr, index | 0);
}

export function chartCreate(parent) {
    const ptr = native.chartCreate(parent ? parent[kPtr] ?? parent : null);
    return new LvObject(ptr);
}

export function setImageSymbol(target, symbol) {
    const ptr = target && typeof target === 'object' ? (target[kPtr] ?? target._ptr) : target;
    return native.setImageSymbol(ptr, symbol ?? '');
}

export function setImageBinary(target, data) {
    const ptr = target && typeof target === 'object' ? (target[kPtr] ?? target._ptr) : target;
    return native.setImageBinary(ptr, data);
}

export function setImageZoom(target, zoom) {
    const ptr = target && typeof target === 'object' ? (target[kPtr] ?? target._ptr) : target;
    return native.setImageZoom(ptr, zoom | 0);
}

export function setImageAngle(target, angle) {
    const ptr = target && typeof target === 'object' ? (target[kPtr] ?? target._ptr) : target;
    return native.setImageAngle(ptr, angle | 0);
}

export function setImagePivot(target, x, y) {
    const ptr = target && typeof target === 'object' ? (target[kPtr] ?? target._ptr) : target;
    return native.setImagePivot(ptr, x | 0, y | 0);
}

export function setBackgroundSymbol(target, symbol, styleType = 0) {
    const ptr = target && typeof target === 'object' ? (target[kPtr] ?? target._ptr) : target;
    return native.setBackgroundSymbol(ptr, symbol ?? null, styleType | 0);
}

export function setArcSymbol(target, symbol, styleType = 0) {
    const ptr = target && typeof target === 'object' ? (target[kPtr] ?? target._ptr) : target;
    return native.setArcSymbol(ptr, symbol ?? null, styleType | 0);
}

export function setChecked(target, checked) {
    const ptr = target && typeof target === 'object' ? (target[kPtr] ?? target._ptr) : target;
    return native.setChecked(ptr, !!checked);
}

export function setDisabled(target, disabled) {
    const ptr = target && typeof target === 'object' ? (target[kPtr] ?? target._ptr) : target;
    return native.setDisabled(ptr, !!disabled);
}

export function sliderSetRange(target, min, max) {
    const ptr = target && typeof target === 'object' ? (target[kPtr] ?? target._ptr) : target;
    return native.sliderSetRange(ptr, min | 0, max | 0);
}

export function sliderSetValue(target, value) {
    const ptr = target && typeof target === 'object' ? (target[kPtr] ?? target._ptr) : target;
    return native.sliderSetValue(ptr, value | 0);
}

export function sliderGetValue(target) {
    const ptr = target && typeof target === 'object' ? (target[kPtr] ?? target._ptr) : target;
    return native.sliderGetValue(ptr);
}

export function checkboxSetText(target, text) {
    const ptr = target && typeof target === 'object' ? (target[kPtr] ?? target._ptr) : target;
    return native.checkboxSetText(ptr, text ?? '');
}

export function dropdownSetOptions(target, items) {
    const ptr = target && typeof target === 'object' ? (target[kPtr] ?? target._ptr) : target;
    return native.dropdownSetOptions(ptr, Array.isArray(items) ? items : []);
}

export function dropdownSetSelected(target, index) {
    const ptr = target && typeof target === 'object' ? (target[kPtr] ?? target._ptr) : target;
    return native.dropdownSetSelected(ptr, index | 0);
}

export function dropdownSetText(target, text) {
    const ptr = target && typeof target === 'object' ? (target[kPtr] ?? target._ptr) : target;
    return native.dropdownSetText(ptr, text ?? null);
}

export function dropdownSetSymbolDir(target, dir) {
    const ptr = target && typeof target === 'object' ? (target[kPtr] ?? target._ptr) : target;
    return native.dropdownSetSymbolDir(ptr, dir | 0);
}

export function dropdownSetDir(target, dir) {
    const ptr = target && typeof target === 'object' ? (target[kPtr] ?? target._ptr) : target;
    return native.dropdownSetDir(ptr, dir | 0);
}

export function dropdownSetSelectedHighlight(target, enabled) {
    const ptr = target && typeof target === 'object' ? (target[kPtr] ?? target._ptr) : target;
    return native.dropdownSetSelectedHighlight(ptr, !!enabled);
}

export function dropdownGetSelected(target) {
    const ptr = target && typeof target === 'object' ? (target[kPtr] ?? target._ptr) : target;
    return native.dropdownGetSelected(ptr);
}

export function dropdownOpen(target) {
    if (typeof native.dropdownOpen !== 'function') return;
    const ptr = target && typeof target === 'object' ? (target[kPtr] ?? target._ptr) : target;
    return native.dropdownOpen(ptr);
}

export function dropdownClose(target) {
    if (typeof native.dropdownClose !== 'function') return;
    const ptr = target && typeof target === 'object' ? (target[kPtr] ?? target._ptr) : target;
    return native.dropdownClose(ptr);
}

export function calendarSetToday(target, y, m, d) {
    const ptr = target && typeof target === 'object' ? (target[kPtr] ?? target._ptr) : target;
    return native.calendarSetToday(ptr, y | 0, m | 0, d | 0);
}

export function calendarSetShownMonth(target, y, m) {
    const ptr = target && typeof target === 'object' ? (target[kPtr] ?? target._ptr) : target;
    return native.calendarSetShownMonth(ptr, y | 0, m | 0);
}

export function calendarSetHighlightedDates(target, dates) {
    const ptr = target && typeof target === 'object' ? (target[kPtr] ?? target._ptr) : target;
    return native.calendarSetHighlightedDates(ptr, Array.isArray(dates) ? dates : []);
}

export function setTheme(options = {}) {
    if (typeof native.setTheme !== 'function') {
        return;
    }
    return native.setTheme(options ?? {});
}

export function resetTheme() {
    if (typeof native.resetTheme !== 'function') {
        return;
    }
    return native.resetTheme();
}

export function chartSetType(target, type) {
    const ptr = target && typeof target === 'object' ? (target[kPtr] ?? target._ptr) : target;
    return native.chartSetType(ptr, type);
}

export function chartSetPointNum(target, pointNum) {
    const ptr = target && typeof target === 'object' ? (target[kPtr] ?? target._ptr) : target;
    return native.chartSetPointNum(ptr, pointNum);
}

export function chartSetDivLineCount(target, hdiv, vdiv) {
    const ptr = target && typeof target === 'object' ? (target[kPtr] ?? target._ptr) : target;
    return native.chartSetDivLineCount(ptr, hdiv, vdiv);
}

export function chartSetRange(target, axis, min, max) {
    const ptr = target && typeof target === 'object' ? (target[kPtr] ?? target._ptr) : target;
    return native.chartSetRange(ptr, axis, min, max);
}

export function chartSetAxisTick(target, axis, majorLen, minorLen, majorCnt, minorCnt, labelEn, drawSize) {
    const ptr = target && typeof target === 'object' ? (target[kPtr] ?? target._ptr) : target;
    return native.chartSetAxisTick(ptr, axis, majorLen, minorLen, majorCnt, minorCnt, !!labelEn, drawSize);
}

export function chartSetAxisLabels(target, axis, labels) {
    if (typeof native.chartSetAxisLabels !== 'function') return;
    const ptr = target && typeof target === 'object' ? (target[kPtr] ?? target._ptr) : target;
    return native.chartSetAxisLabels(ptr, axis, Array.isArray(labels) ? labels.map((it) => String(it)) : []);
}

export function chartSetLeftAxisData(target, data) {
    const ptr = target && typeof target === 'object' ? (target[kPtr] ?? target._ptr) : target;
    return native.chartSetLeftAxisData(ptr, data);
}

export function chartSetRightAxisData(target, data) {
    const ptr = target && typeof target === 'object' ? (target[kPtr] ?? target._ptr) : target;
    return native.chartSetRightAxisData(ptr, data);
}

export function chartSetAllSeries(target, data) {
    const ptr = target && typeof target === 'object' ? (target[kPtr] ?? target._ptr) : target;
    return native.chartSetAllSeries(ptr, data);
}

export function chartSetScatterData(target, data) {
    const ptr = target && typeof target === 'object' ? (target[kPtr] ?? target._ptr) : target;
    return native.chartSetScatterData(ptr, data);
}

export function chartGetType(target) {
    if (typeof native.chartGetType !== 'function') return 0;
    const ptr = target && typeof target === 'object' ? (target[kPtr] ?? target._ptr) : target;
    return native.chartGetType(ptr);
}

export function chartGetPointNum(target) {
    if (typeof native.chartGetPointNum !== 'function') return 0;
    const ptr = target && typeof target === 'object' ? (target[kPtr] ?? target._ptr) : target;
    return native.chartGetPointNum(ptr);
}

export function chartGetSeriesCount(target) {
    if (typeof native.chartGetSeriesCount !== 'function') return 0;
    const ptr = target && typeof target === 'object' ? (target[kPtr] ?? target._ptr) : target;
    return native.chartGetSeriesCount(ptr);
}

export function chartDebugGetState(target) {
    if (typeof native.chartDebugGetState !== 'function') return null;
    const ptr = target && typeof target === 'object' ? (target[kPtr] ?? target._ptr) : target;
    return native.chartDebugGetState(ptr);
}

export function chartDebugGetSeries(target) {
    if (typeof native.chartDebugGetSeries !== 'function') return null;
    const ptr = target && typeof target === 'object' ? (target[kPtr] ?? target._ptr) : target;
    return native.chartDebugGetSeries(ptr);
}

/* ============================================================
 * Default export for convenience
 * ============================================================ */

export default {
    init,
    initAuto,
    tick,
    getDisplaySize,
    refreshWindow,
    setStyle,
    tickHandler,
    addEventListener,
    removeEventListener,
    setEventBubble,
    moveToFront,
    moveToBackground,
    pollEvents,
    __debugGetParent,
    __debugGetIndex,
    __debugIsFloating,
    __debugGetGridTemplate,
    debugGetGridCell,
    debugGetScrollable,
    debugGetGeometry,
    debugGetStyle,
    debugGetState,
    __debugEmitEvent,
    __debugCalendarPressDay,
    screenActive,
    objCreate,
    labelCreate,
    buttonCreate,
    textareaCreate,
    imgCreate,
    sliderCreate,
    switchCreate,
    checkboxCreate,
    dropdownCreate,
    calendarCreate,
    tabviewCreate,
    tabviewAddTab,
    tabviewSetActive,
    setImageSymbol,
    setImageBinary,
    setImageZoom,
    setImageAngle,
    setImagePivot,
    setBackgroundSymbol,
    setArcSymbol,
    setChecked,
    setDisabled,
    sliderSetRange,
    sliderSetValue,
    sliderGetValue,
    checkboxSetText,
    dropdownSetOptions,
    dropdownSetSelected,
    dropdownSetText,
    dropdownSetSymbolDir,
    dropdownSetDir,
    dropdownSetSelectedHighlight,
    dropdownGetSelected,
    dropdownOpen,
    dropdownClose,
    calendarSetToday,
    calendarSetShownMonth,
    calendarSetHighlightedDates,
    setTheme,
    resetTheme,
    chartCreate,
    chartSetType,
    chartSetPointNum,
    chartSetDivLineCount,
    chartSetRange,
    chartSetAxisTick,
    chartSetAxisLabels,
    chartSetLeftAxisData,
    chartSetRightAxisData,
    chartSetAllSeries,
    chartSetScatterData,
    chartGetType,
    chartGetPointNum,
    chartGetSeriesCount,
    chartDebugGetState,
    chartDebugGetSeries,
};
