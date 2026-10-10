const lvgl = await import('../lvgl/index.js');

const BRIDGE_SYMBOL = Symbol.for('lvgljs');

let initialized = false;
let refreshInterval = null;
let rootComp = null;
let internalUidCounter = 0;
const ptrToUid = new Map();
const uidToComp = new Map();
const ENABLE_LEGACY_JS_LAYOUT_FALLBACK = false;

class NativeAnimateCompat {
    constructor() {
        this._timer = null;
    }

    start(config = {}) {
        const duration = Math.max(0, Number(config.duration) || 0);
        const startValue = Number(config.startValue ?? 0);
        const endValue = Number(config.endValue ?? 0);
        const delay = Math.max(0, Number(config.delay) || 0);
        const repeatCount = Math.max(0, Number(config.repeatCount) || 0);
        const execUid = Number(config.execUid ?? 0);
        const startCbUid = Number(config.startCbUid ?? 0);
        const readyCbUid = Number(config.readyCbUid ?? 0);

        const fire = (uid, value) => {
            const cb = globalThis.ANIMIATE_CALLBACK;
            if (typeof cb === 'function' && Number(uid) > 0) {
                try {
                    cb(uid, value);
                } catch (_) {}
            }
        };

        const runOnce = () => {
            fire(startCbUid);
            const t0 = Date.now();
            const tick = () => {
                const elapsed = Date.now() - t0;
                const ratio = duration <= 0 ? 1 : Math.max(0, Math.min(1, elapsed / duration));
                const value = startValue + (endValue - startValue) * ratio;
                fire(execUid, value);
                if (ratio >= 1) {
                    fire(readyCbUid);
                    return true;
                }
                return false;
            };
            if (tick()) {
                return;
            }
            this._timer = setInterval(() => {
                if (tick()) {
                    clearInterval(this._timer);
                    this._timer = null;
                }
            }, 16);
        };

        const totalRuns = repeatCount > 0 ? (repeatCount + 1) : 1;
        let runIndex = 0;
        const runNext = () => {
            runIndex++;
            runOnce();
            if (runIndex >= totalRuns) {
                return;
            }
            const waitMs = delay + duration + 16;
            setTimeout(runNext, waitMs);
        };
        setTimeout(runNext, delay);
    }
}

function requestRefresh() {
    if (typeof lvgl.refreshWindow === 'function') {
        try {
            lvgl.refreshWindow();
            return;
        } catch (_) {}
    }
    try {
        lvgl.tickHandler();
    } catch (_) {}
}

function ptrKey(ptr) {
    if (typeof ptr === 'bigint') {
        return ptr.toString();
    }
    if (ptr == null) {
        return '';
    }
    return String(ptr);
}

function makeInternalUid(prefix = 'internal') {
    internalUidCounter++;
    return `__${prefix}_${internalUidCounter}`;
}

function hasDataset(comp) {
    return !!(comp && comp.dataset && Object.keys(comp.dataset).length > 0);
}

function findEventReceiver(uid, code) {
    let comp = uidToComp.get(uid);
    while (comp) {
        if (comp._eventTypes && comp._eventTypes.has(code)) {
            return comp;
        }
        comp = comp._parent || null;
    }
    return null;
}

function findDatasetOwner(uid) {
    let comp = uidToComp.get(uid);
    while (comp) {
        if (hasDataset(comp)) {
            return comp;
        }
        comp = comp._parent || null;
    }
    return null;
}

function dispatchNativeEvents() {
    if (typeof lvgl.pollEvents !== 'function') {
        return;
    }
    const fire = globalThis.FIRE_QEVENT_CALLBACK;
    if (typeof fire !== 'function') {
        return;
    }
    const events = lvgl.pollEvents();
    if (!Array.isArray(events) || events.length === 0) {
        return;
    }
    for (const ev of events) {
        let currentUid = ptrToUid.get(ptrKey(ev?.currentTarget));
        let targetUid = ptrToUid.get(ptrKey(ev?.target));
        if ((currentUid == null || currentUid === '') && (targetUid == null || targetUid === '')) {
            continue;
        }
        if (currentUid == null || currentUid === '') {
            currentUid = targetUid;
        }
        if (targetUid == null || targetUid === '') {
            targetUid = currentUid;
        }
        const code = Number(ev?.code ?? 0);
        const receiver =
            findEventReceiver(currentUid, code) ||
            findEventReceiver(targetUid, code);
        if (!receiver || !receiver.uid) {
            continue;
        }
        if (typeof receiver._handleNativeEvent === 'function') {
            try {
                const consumed = receiver._handleNativeEvent(code, ev, {
                    targetUid,
                    currentUid,
                });
                if (consumed === true) {
                    continue;
                }
            } catch (_) {}
        }
        const dispatchCurrentUid = receiver.uid;
        if (code === 7 && !hasDataset(uidToComp.get(targetUid))) {
            const datasetOwner = findDatasetOwner(targetUid) || findDatasetOwner(dispatchCurrentUid);
            if (datasetOwner && datasetOwner.uid) {
                targetUid = datasetOwner.uid;
            } else {
                targetUid = dispatchCurrentUid;
            }
        }
        const payload = {
            value: ev?.textValue ?? Number(ev?.value ?? 0),
            textValue: ev?.textValue,
            stopPropagation() {},
        };
        try {
            fire(targetUid, dispatchCurrentUid, code, payload);
        } catch (_) {}
    }
}

function ensureRuntime() {
    if (initialized) {
        return;
    }
    lvgl.init();
    refreshInterval = setInterval(() => {
        try {
            const alive = lvgl.tickHandler();
            dispatchNativeEvents();
            if (alive === false) {
                clearInterval(refreshInterval);
                refreshInterval = null;
                initialized = false;
            }
        } catch (_) {
            // Ignore refresh errors in bootstrap phase.
        }
    }, 30);
    initialized = true;
}

/* Upstream style-pipe encodings for auto/fr (from grid.ts / util.ts).
 * Keep this formula-based to avoid scattering magic numbers like 10193. */
const LV_COORD_FRAC_BITS = 13;
const LV_COORD_TYPE_SPEC = 2001;
const UPSTREAM_AUTO_SIZE_MARKER = LV_COORD_TYPE_SPEC | (1 << LV_COORD_FRAC_BITS);

function isUpstreamAutoSize(v) {
    return typeof v === 'number' && v === UPSTREAM_AUTO_SIZE_MARKER;
}

function isAutoSizeValue(v) {
    return isUpstreamAutoSize(v) || (typeof v === 'string' && v.trim() === 'auto');
}

function parseSize(value, fallback = 0) {
    if (typeof value === 'number') {
        return value;
    }
    if (typeof value === 'string') {
        const trimmed = value.trim();
        if (trimmed.endsWith('px')) {
            const n = Number.parseFloat(trimmed.slice(0, -2));
            return Number.isFinite(n) ? n : fallback;
        }
        const n = Number.parseFloat(trimmed);
        return Number.isFinite(n) ? n : fallback;
    }
    return fallback;
}

function getDimensions() {
    ensureRuntime();
    if (typeof lvgl.getDisplaySize === 'function') {
        const size = lvgl.getDisplaySize();
        const width = Number(size?.width ?? 0);
        const height = Number(size?.height ?? 0);
        if (width > 0 && height > 0) {
            return { width, height };
        }
    }
    return { width: 1024, height: 600 };
}

class BaseComponent {
    constructor({ uid } = {}) {
        this.uid = uid ?? '';
        this.dataset = {};
        this._parent = null;
        this._children = [];
        this._pendingText = '';
        this._styleCache = Object.create(null);
        this._nativeStyleCache = new Map();
        this._lastBounds = { left: 0, top: 0, width: 0, height: 0 };
        this._native = null;
        this._disposed = false;
        this._isScreenRoot = false;
        this._eventTypes = new Set();
        this._pendingAlign = null;
        this._pendingAlignTo = null;
        this._pendingBackgroundImages = new Map();
        this._pendingArcImages = new Map();

        const self = this;
        this._style = {
            left() { return self._lastBounds.left; },
            top() { return self._lastBounds.top; },
            width() { return self._lastBounds.width; },
            height() { return self._lastBounds.height; },
        };
    }

    get style() {
        return this._style;
    }

    set style(value) {
        this._style = value;
    }

    _createNative(_parentNative) {
        throw new Error('_createNative must be implemented');
    }

    _onNativeCreated() {}

    _flushNativeStyles() {
        if (!this._native) {
            return;
        }
        const applyStyle = typeof this._native.setStyle === 'function'
            ? (styleObj, keys, styleType) => this._native.setStyle(styleObj, keys, keys.length, styleType, true)
            : (typeof lvgl.setStyle === 'function'
                ? (styleObj, keys, styleType) => lvgl.setStyle(this._native, styleObj, keys, keys.length, styleType, true)
                : null);
        if (!applyStyle) {
            return;
        }
        for (const [styleType, styleObj] of this._nativeStyleCache.entries()) {
            const keys = Object.keys(styleObj);
            if (keys.length === 0) {
                continue;
            }
            try {
                applyStyle(styleObj, keys, styleType);
            } catch (_) {}
        }
    }

    _bindEventListeners() {
        if (!this._native || !this._native._ptr) {
            return;
        }
        if (typeof lvgl.addEventListener !== 'function') {
            return;
        }
        for (const type of this._eventTypes) {
            lvgl.addEventListener(this._native._ptr, type);
        }
    }

    _ensureNative(parentNative = null) {
        if (this._disposed) {
            return null;
        }
        if (this._native) {
            return this._native;
        }
        ensureRuntime();
        this._native = this._createNative(parentNative);
        if (this.uid) {
            uidToComp.set(this.uid, this);
        }
        if (this._native && this._native._ptr !== undefined) {
            ptrToUid.set(ptrKey(this._native._ptr), this.uid);
        }
        this._flushNativeStyles();
        this._replayPendingImages();
        this._bindEventListeners();
        this._onNativeCreated();
        this._applyLayout();
        this._replayPendingAlign();
        return this._native;
    }

    _syncText() {}

    _hasNativeStyleBridge() {
        return !!(
            (this._native && typeof this._native.setStyle === 'function') ||
            typeof lvgl.setStyle === 'function'
        );
    }

    _replayPendingAlign() {
        if (this._pendingAlignTo) {
            const { type, pos, parent } = this._pendingAlignTo;
            if (parent?._native && this._native && typeof this._native.alignTo === 'function') {
                try {
                    this._native.alignTo(parent._native, type, Number(pos?.[0] ?? 0), Number(pos?.[1] ?? 0));
                    requestRefresh();
                    return;
                } catch (_) {}
            }
        }
        if (this._pendingAlign && this._native && typeof this._native.align === 'function') {
            const { type, pos } = this._pendingAlign;
            try {
                this._native.align(type, Number(pos?.[0] ?? 0), Number(pos?.[1] ?? 0));
                requestRefresh();
            } catch (_) {}
        }
    }

    _replayPendingImages() {
        if (!this._native || this._disposed) {
            return;
        }
        if (typeof lvgl.setBackgroundSymbol === 'function') {
            for (const [styleType, image] of this._pendingBackgroundImages.entries()) {
                try {
                    lvgl.setBackgroundSymbol(this._native, image?.symbol ?? null, styleType);
                } catch (_) {}
            }
        }
        if (typeof lvgl.setArcSymbol === 'function') {
            for (const [styleType, image] of this._pendingArcImages.entries()) {
                try {
                    lvgl.setArcSymbol(this._native, image?.symbol ?? null, styleType);
                } catch (_) {}
            }
        }
    }

    _usesNativeLayout() {
        const display = this._styleCache.display;
        if (!this._hasNativeStyleBridge()) {
            return false;
        }
        return display === 'grid' || display === 'flex';
    }

    _needsJsChildLayout() {
        if (!ENABLE_LEGACY_JS_LAYOUT_FALLBACK) {
            return false;
        }
        const display = this._styleCache.display;
        if (this._usesNativeLayout()) {
            return false;
        }
        return display === 'grid' || (display === 'flex' && this._styleCache['flex-direction'] === 'column');
    }

    _applyLayout() {
        const hasWidth = this._styleCache.width !== undefined && this._styleCache.width !== null;
        const hasHeight = this._styleCache.height !== undefined && this._styleCache.height !== null;
        const hasLeft = this._styleCache.left !== undefined && this._styleCache.left !== null;
        const hasTop = this._styleCache.top !== undefined && this._styleCache.top !== null;
        const width = parseSize(this._styleCache.width, this._lastBounds.width || 0);
        const height = parseSize(this._styleCache.height, this._lastBounds.height || 0);
        const left = parseSize(this._styleCache.left, this._lastBounds.left || 0);
        const top = parseSize(this._styleCache.top, this._lastBounds.top || 0);

        /* Detect auto-size: when upstream passes the LV_SIZE_CONTENT marker,
         * native width_content/height_content is already set, so skip setSize. */
        const widthIsAuto = isUpstreamAutoSize(this._styleCache.width);
        const heightIsAuto = isUpstreamAutoSize(this._styleCache.height);

        this._lastBounds = { left, top, width, height };

        if (!this._native) {
            return;
        }
        if (hasWidth && hasHeight && !widthIsAuto && !heightIsAuto &&
            Number.isFinite(width) && Number.isFinite(height) && width >= 0 && height >= 0) {
            this._native.setSize(width, height);
        }
        if (hasLeft && hasTop && Number.isFinite(left) && Number.isFinite(top)) {
            this._native.setPos(left, top);
        }
        requestRefresh();
    }

    nativeSetStyle(styleObj = {}, keys = [], len = 0, styleType = 0, isInit = true) {
        if (this._disposed) {
            return;
        }
        const count = Number.isFinite(len) ? Math.min(len, keys.length) : keys.length;
        const hadNativeStyleBridge = this._hasNativeStyleBridge();
        let styleBucket = this._nativeStyleCache.get(styleType);
        if (!styleBucket) {
            styleBucket = Object.create(null);
            this._nativeStyleCache.set(styleType, styleBucket);
        }
        for (let i = 0; i < count; i++) {
            const key = keys[i];
            this._styleCache[key] = styleObj[key];
            styleBucket[key] = styleObj[key];
        }

        /* Build a native-compatible style object by converting upstream
         * auto/fr encodings into keys the native module understands. */
        const nativeObj = Object.create(null);
        const nativeKeys = [];
        for (let i = 0; i < count; i++) {
            const key = keys[i];
            let val = styleObj[key];

            if (key === 'width' && isUpstreamAutoSize(val)) {
                nativeKeys.push('width_content');
                nativeObj['width_content'] = 1;
                styleBucket['width_content'] = 1;
            } else if (key === 'height' && isUpstreamAutoSize(val)) {
                nativeKeys.push('height_content');
                nativeObj['height_content'] = 1;
                styleBucket['height_content'] = 1;
            } else {
                nativeKeys.push(key);
                nativeObj[key] = val;
            }
        }
        if (!hadNativeStyleBridge) {
            /* Legacy fallback: preserve just enough derived state for the old JS layout shim. */
            if (Array.isArray(styleObj['grid-template'])) {
                const tpl = styleObj['grid-template'];
                if (Array.isArray(tpl[0])) {
                    this._styleCache.__gridCols = tpl[0].length;
                }
                if (Array.isArray(tpl[1])) {
                    this._styleCache.__gridRows = tpl[1].length;
                }
            }
            if (Array.isArray(styleObj['grid-child'])) {
                const g = styleObj['grid-child'];
                this._styleCache['grid-column-pos'] = Number(g[1] ?? 0);
                this._styleCache['grid-row-pos'] = Number(g[4] ?? 0);
                this._styleCache['grid-column-span'] = Number(g[2] ?? 1);
                this._styleCache['grid-row-span'] = Number(g[5] ?? 1);
            }
            if (this._styleCache.display === 'flex' && this._styleCache['flex-direction'] == null) {
                const flow = Number(this._styleCache['flex-flow']);
                /* In upstream mapping bit0 means column in common paths. */
                if ((flow & 0x01) === 0x01) {
                    this._styleCache['flex-direction'] = 'column';
                } else {
                    this._styleCache['flex-direction'] = 'row';
                }
            }
        }
        let usedNativeStyle = false;
        if (this._native && typeof this._native.setStyle === 'function') {
            try {
                this._native.setStyle(nativeObj, nativeKeys, nativeKeys.length, styleType, isInit);
                usedNativeStyle = true;
            } catch (_) {}
        } else if (this._native && typeof lvgl.setStyle === 'function') {
            try {
                lvgl.setStyle(this._native, nativeObj, nativeKeys, nativeKeys.length, styleType, isInit);
                usedNativeStyle = true;
            } catch (_) {}
        }
        this._applyLayout();
        this._replayPendingAlign();
        if (!usedNativeStyle) {
            this._layoutChildren();
            if (this._parent) {
                this._parent._layoutChildren();
            }
        }
        requestRefresh();
    }

    _layoutChildren() {
        if (!this._needsJsChildLayout()) {
            return;
        }
        const display = this._styleCache.display;
        if (display === 'grid') {
            this._layoutGridChildren();
        } else if (display === 'flex' && this._styleCache['flex-direction'] === 'column') {
            this._layoutFlexColumnChildren();
        }
    }

    _layoutFlexColumnChildren() {
        /* Legacy fallback only. Native flex layout is the default path in xuan. */
        if (this._children.length === 0) {
            return;
        }
        let top = 0;
        for (const child of this._children) {
            child._styleCache.top = top;
            child._styleCache.left = 0;
            child._applyLayout();
            top += child._lastBounds.height;
        }
    }

    _layoutGridChildren() {
        /* Legacy fallback only. Native grid layout is the default path in xuan. */
        const cols = Number(this._styleCache.__gridCols) || String(this._styleCache['grid-template-columns'] || '').trim().split(/\s+/).filter(Boolean).length || 1;
        const rows = Number(this._styleCache.__gridRows) || String(this._styleCache['grid-template-rows'] || '').trim().split(/\s+/).filter(Boolean).length || 1;
        const gapX = parseSize(this._styleCache['column-spacing'], 0);
        const gapY = parseSize(this._styleCache['row-spacing'], 0);
        const padding = parseSize(this._styleCache.padding, 0);
        const totalWidth = this._lastBounds.width || 0;
        const totalHeight = this._lastBounds.height || 0;
        if (totalWidth <= 0 || totalHeight <= 0) {
            return;
        }

        const cellW = Math.floor((totalWidth - padding * 2 - gapX * (cols - 1)) / cols);
        const cellH = Math.floor((totalHeight - padding * 2 - gapY * (rows - 1)) / rows);

        for (const child of this._children) {
            const col = Number(child._styleCache['grid-column-pos'] ?? 0);
            const row = Number(child._styleCache['grid-row-pos'] ?? 0);
            const colSpan = Math.max(1, Number(child._styleCache['grid-column-span'] ?? 1));
            const rowSpan = Math.max(1, Number(child._styleCache['grid-row-span'] ?? 1));
            if (!Number.isFinite(col) || !Number.isFinite(row)) {
                continue;
            }
            child._styleCache.left = padding + col * (cellW + gapX);
            child._styleCache.top = padding + row * (cellH + gapY);
            child._styleCache.width = cellW * colSpan + gapX * (colSpan - 1);
            child._styleCache.height = cellH * rowSpan + gapY * (rowSpan - 1);
            child._applyLayout();
        }
    }

    appendChild(child) {
        if (!child || child === this || this._disposed || child._disposed) {
            return;
        }
        if (child._parent === this) {
            return;
        }
        const parentNative = this._ensureNative(this._parent ? this._parent._native : null);
        child._parent = this;
        child._ensureNative(parentNative);
        if (this._native && typeof this._native.appendChild === 'function' && child._native) {
            try {
                this._native.appendChild(child._native);
            } catch (_) {}
        }
        child._replayPendingAlign();
        if (typeof lvgl.setEventBubble === 'function' && child._native && child._native._ptr !== undefined) {
            try {
                lvgl.setEventBubble(child._native._ptr, true);
            } catch (_) {}
        }
        this._children.push(child);
        if (this._needsJsChildLayout()) {
            this._layoutChildren();
        }
        requestRefresh();
    }

    insertChildBefore(child, _beforeChild) {
        if (!child || child === this || this._disposed || child._disposed) {
            return;
        }
        const beforeChild = _beforeChild;
        const parentNative = this._ensureNative(this._parent ? this._parent._native : null);
        child._parent = this;
        child._ensureNative(parentNative);
        if (this._native && typeof this._native.insertBefore === 'function' && child._native && beforeChild?._native) {
            try {
                this._native.insertBefore(child._native, beforeChild._native);
            } catch (_) {}
        } else {
            this.appendChild(child);
            return;
        }
        if (!this._children.includes(child)) {
            const beforeIndex = this._children.indexOf(beforeChild);
            if (beforeIndex >= 0) {
                this._children.splice(beforeIndex, 0, child);
            } else {
                this._children.push(child);
            }
        }
        child._replayPendingAlign();
        requestRefresh();
    }

    removeChild(child) {
        if (!child) {
            return;
        }
        this._children = this._children.filter((it) => it !== child);
        let removedByNative = false;
        if (this._native && typeof this._native.removeChild === 'function' && child._native) {
            try {
                this._native.removeChild(child._native);
                removedByNative = true;
            } catch (_) {}
        }
        child._finalizeDispose(!removedByNative);
        requestRefresh();
    }

    addEventListener(type) {
        if (this._eventTypes.has(type)) {
            return;
        }
        this._eventTypes.add(type);
        if (!this._native || !this._native._ptr) {
            return;
        }
        if (typeof lvgl.addEventListener === 'function') {
            lvgl.addEventListener(this._native._ptr, type);
        }
    }

    removeEventListener(type) {
        if (type === undefined || type === null) {
            this._eventTypes.clear();
        } else {
            this._eventTypes.delete(type);
        }
        if (!this._native || !this._native._ptr) {
            return;
        }
        if (typeof lvgl.removeEventListener === 'function') {
            lvgl.removeEventListener(this._native._ptr);
            this._bindEventListeners();
        }
    }

    align(_type, pos = [0, 0]) {
        this._pendingAlign = { type: _type, pos: [Number(pos?.[0] ?? 0), Number(pos?.[1] ?? 0)] };
        this._pendingAlignTo = null;
        if (this._native && typeof this._native.align === 'function') {
            try {
                this._native.align(this._pendingAlign.type, this._pendingAlign.pos[0], this._pendingAlign.pos[1]);
                requestRefresh();
                return;
            } catch (_) {}
        }
        this._styleCache.left = this._pendingAlign.pos[0];
        this._styleCache.top = this._pendingAlign.pos[1];
        this._applyLayout();
    }

    alignTo(_type, pos = [0, 0], parent) {
        if (parent) {
            this._parent = parent;
        }
        this._pendingAlignTo = { type: _type, pos: [Number(pos?.[0] ?? 0), Number(pos?.[1] ?? 0)], parent };
        this._pendingAlign = null;
        if (this._native && typeof this._native.alignTo === 'function' && parent?._native) {
            try {
                this._native.alignTo(parent._native, this._pendingAlignTo.type, this._pendingAlignTo.pos[0], this._pendingAlignTo.pos[1]);
                requestRefresh();
                return;
            } catch (_) {}
        }
        this._styleCache.left = this._pendingAlignTo.pos[0];
        this._styleCache.top = this._pendingAlignTo.pos[1];
        this._applyLayout();
    }

    moveToFront() {
        if (this._disposed || !this._native || typeof lvgl.moveToFront !== 'function') {
            return;
        }
        lvgl.moveToFront(this._native);
        requestRefresh();
    }

    moveToBackground() {
        if (this._disposed || !this._native || typeof lvgl.moveToBackground !== 'function') {
            return;
        }
        lvgl.moveToBackground(this._native);
        requestRefresh();
    }

    scrollIntoView() {}

    setBackgroundImage(_data, styleType = 0, symbol = null) {
        if (this._disposed) {
            return;
        }
        const nativeStyleType = Number(styleType) || 0;
        this._pendingBackgroundImages.set(nativeStyleType, { data: _data ?? null, symbol: symbol ?? null });
        if (!this._native || typeof lvgl.setBackgroundSymbol !== 'function') {
            return;
        }
        lvgl.setBackgroundSymbol(this._native, symbol ?? null, nativeStyleType);
        requestRefresh();
    }

    setArcImage(_data, styleType = 0, symbol = null) {
        if (this._disposed) {
            return;
        }
        const nativeStyleType = Number(styleType) || 0;
        this._pendingArcImages.set(nativeStyleType, { data: _data ?? null, symbol: symbol ?? null });
        if (!this._native || typeof lvgl.setArcSymbol !== 'function') {
            return;
        }
        lvgl.setArcSymbol(this._native, symbol ?? null, nativeStyleType);
        requestRefresh();
    }

    setChecked(val) {
        if (this._disposed || !this._native || typeof lvgl.setChecked !== 'function') {
            return;
        }
        lvgl.setChecked(this._native, !!val);
        requestRefresh();
    }

    setDisabled(val) {
        if (this._disposed || !this._native || typeof lvgl.setDisabled !== 'function') {
            return;
        }
        lvgl.setDisabled(this._native, !!val);
        requestRefresh();
    }

    setRange(_arr) {}

    setValue(_val) {}

    setItems(_items, _len) {}

    setArrowDir(_dir) {}

    setselectIndex(_idx) {}

    setDir(_dir) {}

    setHighLightSelect(_val) {}

    setType(_val) {}

    setDivLineCount(_val) {}

    setPointNum(_val) {}

    setScatterData(_val) {}

    setLeftAxisOption(_val) {}

    setLeftAxisData(_val) {}

    setBottomAxisOption(_val) {}

    setBottomAxisData(_val) {}

    setRightAxisOption(_val) {}

    setRightAxisData(_val) {}

    setTopAxisOption(_val) {}

    setTopAxisData(_val) {}

    setLeftAxisLabels(_val) {}

    setRightAxisLabels(_val) {}

    setTopAxisLabels(_val) {}

    setBottomAxisLabels(_val) {}

    setLeftAxisRange(_val) {}

    setRightAxisRange(_val) {}

    setTopAxisRange(_val) {}

    setBottomAxisRange(_val) {}

    setToday(_y, _m, _d) {}

    setShownMonth(_y, _m) {}

    setHighlightDates(_dates, _len) {}

    setImageBinary(_data) {}

    setSymbol(_symbol) {}

    setTab(_name, _child) {}

    close() {
        this._finalizeDispose(true);
    }

    _finalizeDispose(deleteNative) {
        if (this._disposed) {
            return;
        }
        this._disposed = true;
        /* Clear chart/layer references first to prevent stale native calls. */
        this._plot = null;
        this._gridLayer = null;
        this._seriesLayer = null;
        this._bars = [];
        this._gridLines = [];
        for (const child of [...this._children]) {
            if (!child._disposed) {
                child._finalizeDispose(false);
            }
        }
        this._children = [];
        this._parent = null;
        this._eventTypes.clear();
        if (deleteNative && this._native && typeof this._native.delete === 'function') {
            if (!this._isScreenRoot) {
                try {
                    this._native.delete();
                } catch (_) {}
            }
        }
        if (this.uid) {
            uidToComp.delete(this.uid);
        }
        if (this._native && this._native._ptr !== undefined) {
            ptrToUid.delete(ptrKey(this._native._ptr));
        }
        this._native = null;
        if (rootComp === this) {
            rootComp = null;
        }
        requestRefresh();
    }
}

class View extends BaseComponent {
    _createNative(parentNative) {
        return lvgl.objCreate(parentNative || lvgl.screenActive());
    }
}

class Text extends BaseComponent {
    _createNative(parentNative) {
        return lvgl.labelCreate(parentNative || lvgl.screenActive());
    }

    _onNativeCreated() {
        this._syncText();
    }

    _syncText() {
        if (this._native && typeof this._native.setText === 'function') {
            this._native.setText(String(this._pendingText ?? ''));
            /* Keep textarea size stable after text updates. */
            this._applyLayout();
        }
    }

    setText(text) {
        if (this._disposed) {
            return;
        }
        this._pendingText = String(text ?? '');
        this._syncText();
        if (this._parent && this._parent._needsJsChildLayout()) {
            this._parent._layoutChildren();
        }
        requestRefresh();
    }
}

class Button extends BaseComponent {
    _createNative(parentNative) {
        return lvgl.buttonCreate(parentNative || lvgl.screenActive());
    }
}

class Textarea extends BaseComponent {
    _createNative(parentNative) {
        if (typeof lvgl.textareaCreate === 'function') {
            return lvgl.textareaCreate(parentNative || lvgl.screenActive());
        }
        return lvgl.labelCreate(parentNative || lvgl.screenActive());
    }

    _onNativeCreated() {
        this._syncText();
        if (this._oneLine !== undefined) {
            this.setOneLine(this._oneLine);
        }
        if (this._placeholder !== undefined) {
            this.setPlaceHolder(this._placeholder);
        }
        if (this._passwordMode !== undefined) {
            this.setPasswordMode(this._passwordMode);
        }
        if (this._maxLength !== undefined) {
            this.setMaxLength(this._maxLength);
        }
        if (this._autoKeyboard !== undefined) {
            this.setAutoKeyboard(this._autoKeyboard);
        }
    }

    _syncText() {
        if (this._native && typeof this._native.setText === 'function') {
            this._native.setText(String(this._pendingText ?? ''));
            /* LVGL textarea can resize after content changes; re-apply bounds. */
            this._applyLayout();
        }
    }

    setOneLine(oneLine) {
        if (this._disposed) {
            return;
        }
        this._oneLine = !!oneLine;
        if (this._native && typeof this._native.setOneLine === 'function') {
            this._native.setOneLine(this._oneLine);
        }
    }

    setText(text) {
        if (this._disposed) {
            return;
        }
        this._pendingText = String(text ?? '');
        this._syncText();
        if (this._parent && this._parent._needsJsChildLayout()) {
            this._parent._layoutChildren();
        }
        requestRefresh();
    }

    setPlaceHolder(text) {
        if (this._disposed) {
            return;
        }
        this._placeholder = String(text ?? '');
        if (this._native && typeof this._native.setPlaceHolder === 'function') {
            this._native.setPlaceHolder(this._placeholder);
        }
    }

    setPasswordMode(enabled) {
        if (this._disposed) {
            return;
        }
        this._passwordMode = !!enabled;
        if (this._native && typeof this._native.setPasswordMode === 'function') {
            this._native.setPasswordMode(this._passwordMode);
        }
    }

    setMaxLength(maxLen) {
        if (this._disposed) {
            return;
        }
        this._maxLength = Number(maxLen) || 0;
        if (this._native && typeof this._native.setMaxLength === 'function') {
            this._native.setMaxLength(this._maxLength);
        }
    }

    setAutoKeyboard(enabled) {
        if (this._disposed) {
            return;
        }
        this._autoKeyboard = !!enabled;
        if (this._native && typeof this._native.setAutoKeyboard === 'function') {
            this._native.setAutoKeyboard(this._autoKeyboard);
        }
    }
}

class Image extends BaseComponent {
    _createNative(parentNative) {
        if (typeof lvgl.imgCreate === 'function') {
            return lvgl.imgCreate(parentNative || lvgl.screenActive());
        }
        return lvgl.labelCreate(parentNative || lvgl.screenActive());
    }

    _onNativeCreated() {
        if (this._pendingImageSymbol != null) {
            this.setSymbol(this._pendingImageSymbol);
        } else if (this._pendingImageBinary) {
            this.setImageBinary(this._pendingImageBinary);
        }
    }

    setSymbol(symbol) {
        if (this._disposed) {
            return;
        }
        this._pendingImageSymbol = String(symbol ?? '');
        this._pendingImageBinary = null;
        if (this._native && typeof this._native.setSymbol === 'function') {
            this._native.setSymbol(this._pendingImageSymbol);
        } else if (this._native && typeof this._native.setText === 'function') {
            this._native.setText(this._pendingImageSymbol);
        }
        requestRefresh();
    }

    setImageBinary(data) {
        if (this._disposed) {
            return;
        }
        this._pendingImageBinary = data;
        this._pendingImageSymbol = null;
        if (this._native && typeof this._native.setImageBinary === 'function') {
            this._native.setImageBinary(data);
        } else if (this._native && typeof this._native.setText === 'function') {
            this._native.setText('[img]');
        }
        requestRefresh();
    }
}

class Switch extends View {
    _createNative(parentNative) {
        if (typeof lvgl.switchCreate === 'function') {
            return lvgl.switchCreate(parentNative || lvgl.screenActive());
        }
        return super._createNative(parentNative);
    }

    _onNativeCreated() {
        if (this._checked !== undefined) {
            super.setChecked(this._checked);
        }
        if (this._disabled !== undefined) {
            super.setDisabled(this._disabled);
        }
    }

    setChecked(val) {
        if (this._disposed) {
            return;
        }
        this._checked = !!val;
        super.setChecked(val);
    }

    setDisabled(val) {
        if (this._disposed) {
            return;
        }
        this._disabled = !!val;
        super.setDisabled(val);
    }
}

class Slider extends View {
    _createNative(parentNative) {
        if (typeof lvgl.sliderCreate === 'function') {
            return lvgl.sliderCreate(parentNative || lvgl.screenActive());
        }
        return super._createNative(parentNative);
    }

    _onNativeCreated() {
        if (Array.isArray(this._range)) {
            this.setRange(this._range);
        }
        if (this._value !== undefined) {
            this.setValue(this._value);
        }
    }

    setRange(arr) {
        if (this._disposed) {
            return;
        }
        if (Array.isArray(arr) && arr.length >= 2) {
            this._range = [Number(arr[0]) || 0, Number(arr[1]) || 100];
            if (this._native && typeof this._native.setRange === 'function') {
                this._native.setRange(this._range[0], this._range[1]);
            } else if (this._native && typeof lvgl.sliderSetRange === 'function') {
                lvgl.sliderSetRange(this._native, this._range[0], this._range[1]);
            }
            requestRefresh();
        }
    }

    setValue(val) {
        if (this._disposed) {
            return;
        }
        this._value = Number(val) || 0;
        if (this._native && typeof this._native.setValue === 'function') {
            this._native.setValue(this._value);
        } else if (this._native && typeof lvgl.sliderSetValue === 'function') {
            lvgl.sliderSetValue(this._native, this._value);
        }
        requestRefresh();
    }
}

class Checkbox extends View {
    _createNative(parentNative) {
        if (typeof lvgl.checkboxCreate === 'function') {
            return lvgl.checkboxCreate(parentNative || lvgl.screenActive());
        }
        return lvgl.buttonCreate(parentNative || lvgl.screenActive());
    }

    _onNativeCreated() {
        if (this._pendingText != null) {
            this.setText(this._pendingText);
        }
        if (this._checked !== undefined) {
            super.setChecked(this._checked);
        }
        if (this._disabled !== undefined) {
            super.setDisabled(this._disabled);
        }
    }

    setChecked(val) {
        if (this._disposed) {
            return;
        }
        this._checked = !!val;
        super.setChecked(val);
    }

    setDisabled(val) {
        if (this._disposed) {
            return;
        }
        this._disabled = !!val;
        super.setDisabled(val);
    }

    setText(text) {
        if (this._disposed) {
            return;
        }
        this._pendingText = String(text ?? '');
        if (this._native && typeof this._native.setText === 'function') {
            this._native.setText(this._pendingText);
        } else if (this._native && typeof this._native.setLabel === 'function') {
            this._native.setLabel(this._pendingText);
        } else if (this._native && typeof lvgl.checkboxSetText === 'function') {
            lvgl.checkboxSetText(this._native, this._pendingText);
        }
        requestRefresh();
    }
}

class Dropdownlist extends View {
    _createNative(parentNative) {
        if (typeof lvgl.dropdownCreate === 'function') {
            return lvgl.dropdownCreate(parentNative || lvgl.screenActive());
        }
        return lvgl.textareaCreate(parentNative || lvgl.screenActive());
    }

    _onNativeCreated() {
        if (Array.isArray(this._items)) {
            this._applyDropdownItems();
        }
        if (this._selectIndex !== undefined) {
            this.setselectIndex(this._selectIndex);
        }
        if (this._dropdownTextSet) {
            this.setText(this._pendingText);
        } else if (!this._hasNativeDropdown() && this._pendingText != null) {
            this.setText(this._pendingText);
        }
        if (this._arrowDir !== undefined) {
            this.setArrowDir(this._arrowDir);
        }
        if (this._dir !== undefined) {
            this.setDir(this._dir);
        }
        if (this._highlightSelect !== undefined) {
            this.setHighLightSelect(this._highlightSelect);
        }
    }

    _hasNativeDropdown() {
        return !!(
            this._native &&
            (typeof this._native.setItems === 'function' || typeof lvgl.dropdownSetOptions === 'function')
        );
    }

    _applyDropdownItems() {
        if (this._native && typeof this._native.setItems === 'function') {
            this._native.setItems(this._items);
        } else if (this._native && typeof lvgl.dropdownSetOptions === 'function') {
            lvgl.dropdownSetOptions(this._native, this._items);
        }
    }

    setItems(items) {
        if (this._disposed) {
            return;
        }
        if (!Array.isArray(items)) {
            return;
        }
        this._items = items.slice();
        if (!this._hasNativeDropdown() && !this._dropdownTextSet && (this._pendingText == null || this._pendingText === '') && items.length > 0) {
            this._pendingText = String(items[0]);
        }
        this._applyDropdownItems();
        requestRefresh();
    }

    setArrowDir(dir) {
        if (this._disposed) {
            return;
        }
        this._arrowDir = Number(dir) || 0;
        if (this._native && typeof this._native.setSymbolDir === 'function') {
            this._native.setSymbolDir(this._arrowDir);
        } else if (this._native && typeof lvgl.dropdownSetSymbolDir === 'function') {
            lvgl.dropdownSetSymbolDir(this._native, this._arrowDir);
        }
        requestRefresh();
    }

    setselectIndex(idx) {
        if (this._disposed) {
            return;
        }
        this._selectIndex = Number(idx) || 0;
        if (!this._hasNativeDropdown() && Array.isArray(this._items) && this._items[this._selectIndex] != null) {
            if (this._native) {
                this.setText(String(this._items[this._selectIndex]));
            } else {
                this._pendingText = String(this._items[this._selectIndex]);
            }
        }
        if (this._native && typeof this._native.setSelected === 'function') {
            this._native.setSelected(this._selectIndex);
        } else if (this._native && typeof lvgl.dropdownSetSelected === 'function') {
            lvgl.dropdownSetSelected(this._native, this._selectIndex);
        }
        requestRefresh();
    }

    setText(text) {
        if (this._disposed) {
            return;
        }
        this._pendingText = text == null ? null : String(text);
        this._dropdownTextSet = true;
        if (this._native && typeof this._native.setText === 'function') {
            this._native.setText(this._pendingText);
        } else if (this._native && typeof lvgl.dropdownSetText === 'function') {
            lvgl.dropdownSetText(this._native, this._pendingText);
        }
        requestRefresh();
    }

    setDir(dir) {
        if (this._disposed) {
            return;
        }
        this._dir = Number(dir) || 0;
        if (this._native && typeof this._native.setDir === 'function') {
            this._native.setDir(this._dir);
        } else if (this._native && typeof lvgl.dropdownSetDir === 'function') {
            lvgl.dropdownSetDir(this._native, this._dir);
        }
        requestRefresh();
    }

    setHighLightSelect(val) {
        if (this._disposed) {
            return;
        }
        this._highlightSelect = !!val;
        if (this._native && typeof this._native.setSelectedHighlight === 'function') {
            this._native.setSelectedHighlight(this._highlightSelect);
        } else if (this._native && typeof lvgl.dropdownSetSelectedHighlight === 'function') {
            lvgl.dropdownSetSelectedHighlight(this._native, this._highlightSelect);
        }
        requestRefresh();
    }
}

class Calendar extends View {
    _createNative(parentNative) {
        if (typeof lvgl.calendarCreate === 'function') {
            return lvgl.calendarCreate(parentNative || lvgl.screenActive());
        }
        return super._createNative(parentNative);
    }

    _onNativeCreated() {
        if (Array.isArray(this._today)) {
            this.setToday(this._today[0], this._today[1], this._today[2]);
        }
        if (Array.isArray(this._shownMonth)) {
            this.setShownMonth(this._shownMonth[0], this._shownMonth[1]);
        }
        if (Array.isArray(this._highlightDates)) {
            this.setHighlightDates(this._highlightDates, this._highlightDates.length);
        }
    }

    setToday(y, m, d) {
        if (this._disposed) {
            return;
        }
        this._today = [Number(y) || 0, Number(m) || 0, Number(d) || 0];
        if (this._native && typeof this._native.setToday === 'function') {
            this._native.setToday(this._today[0], this._today[1], this._today[2]);
        } else if (this._native && typeof lvgl.calendarSetToday === 'function') {
            lvgl.calendarSetToday(this._native, this._today[0], this._today[1], this._today[2]);
        }
        requestRefresh();
    }

    setShownMonth(y, m) {
        if (this._disposed) {
            return;
        }
        this._shownMonth = [Number(y) || 0, Number(m) || 0];
        if (this._native && typeof this._native.setShownMonth === 'function') {
            this._native.setShownMonth(this._shownMonth[0], this._shownMonth[1]);
        } else if (this._native && typeof lvgl.calendarSetShownMonth === 'function') {
            lvgl.calendarSetShownMonth(this._native, this._shownMonth[0], this._shownMonth[1]);
        }
        requestRefresh();
    }

    setHighlightDates(dates, len) {
        if (this._disposed) {
            return;
        }
        if (Array.isArray(dates)) {
            const requestedLen = len == null ? dates.length : Number(len);
            const count = Number.isFinite(requestedLen) ? Math.max(0, requestedLen) : dates.length;
            this._highlightDates = dates.slice(0, count);
            if (this._native && typeof this._native.setHighlightDates === 'function') {
                this._native.setHighlightDates(this._highlightDates);
            } else if (this._native && typeof lvgl.calendarSetHighlightedDates === 'function') {
                lvgl.calendarSetHighlightedDates(this._native, this._highlightDates);
            }
            requestRefresh();
        }
    }
}

class Chart extends View {
    constructor(opts = {}) {
        super(opts);
        this._chartType = 1;
        this._pointNum = 0;
        this._divLineCount = [3, 3];
        this._leftAxisRange = null;
        this._leftAxisOption = null;
        this._leftAxisData = [];
        this._bottomAxisOption = null;
        this._bottomAxisData = [];
        this._rightAxisOption = null;
        this._rightAxisData = [];
        this._topAxisOption = null;
        this._topAxisData = [];
        this._leftAxisLabels = [];
        this._rightAxisLabels = [];
        this._topAxisLabels = [];
        this._bottomAxisLabels = [];
        this._rightAxisRange = null;
        this._topAxisRange = null;
        this._bottomAxisRange = null;
        this._scatterData = [];
        this._nativeChart = false;
        this._plot = null;
        this._gridLayer = null;
        this._seriesLayer = null;
        this._gridLines = [];
        this._bars = [];
    }

    _createNative(parentNative) {
        if (typeof lvgl.chartCreate === 'function') {
            return lvgl.chartCreate(parentNative || lvgl.screenActive());
        }
        return lvgl.objCreate(parentNative || lvgl.screenActive());
    }

    _onNativeCreated() {
        this._nativeChart = typeof lvgl.chartSetType === 'function';
        this._clearFallbackPlot();
        this._rebuildBars();
    }

    _hasNativeChartBridge() {
        return typeof lvgl.chartCreate === 'function' && typeof lvgl.chartSetType === 'function';
    }

    _nativeChartReady() {
        return !!(this._nativeChart && this._native && typeof lvgl.chartSetType === 'function');
    }

    _applyAxisTickNative(axis, option) {
        if (this._disposed || !this._native) {
            return;
        }
        if (!option || typeof lvgl.chartSetAxisTick !== 'function') {
            return;
        }
        const majorLen = Number(option.majorLen);
        const minorLen = Number(option.minorLen);
        const majorNum = Number(option.majorNum);
        const minorNum = Number(option.minorNum);
        const drawSize = Number(option.drawSize);
        if (!Number.isFinite(majorLen) || !Number.isFinite(minorLen) ||
            !Number.isFinite(majorNum) || !Number.isFinite(minorNum) || !Number.isFinite(drawSize)) {
            return;
        }
        lvgl.chartSetAxisTick(
            this._native,
            axis,
            Math.max(0, Math.round(majorLen)),
            Math.max(0, Math.round(minorLen)),
            Math.max(0, Math.round(majorNum)),
            Math.max(0, Math.round(minorNum)),
            option.labelEn !== false,
            Math.max(0, Math.round(drawSize))
        );
    }

    _applyAxisLabelsNative(axis, labels) {
        if (this._disposed || !this._native || typeof lvgl.chartSetAxisLabels !== 'function') {
            return;
        }
        lvgl.chartSetAxisLabels(this._native, axis, Array.isArray(labels) ? labels : []);
    }

    _ensurePlot() {
        if (this._disposed) {
            return null;
        }
        if (this._hasNativeChartBridge()) {
            this._ensureNative(this._parent ? this._parent._native : null);
            return this._nativeChartReady() ? this : null;
        }
        if (this._nativeChartReady()) {
            return this;
        }
        if (this._plot) {
            return this._plot;
        }
        const plot = new View({ uid: makeInternalUid('chart_plot') });
        plot.nativeSetStyle({
            display: 'flex',
            'flex-flow': 1,
            width_pct: 100,
            height_pct: 100,
        }, [ 'display', 'flex-flow', 'width_pct', 'height_pct' ], 4, 0, true);

        const gridLayer = new View({ uid: makeInternalUid('chart_grid') });
        gridLayer.nativeSetStyle({
            display: 'flex',
            'flex-flow': 1,
            width_pct: 100,
            height_pct: 100,
            'padding-left': 6,
            'padding-right': 6,
            'padding-top': 6,
            'padding-bottom': 4,
            'row-spacing': 2,
        }, [ 'display', 'flex-flow', 'width_pct', 'height_pct', 'padding-left', 'padding-right', 'padding-top', 'padding-bottom', 'row-spacing' ], 9, 0, true);

        const seriesLayer = new View({ uid: makeInternalUid('chart_series') });
        seriesLayer.nativeSetStyle({
            display: 'flex',
            'flex-flow': 2,
            'justify-content': 2,
            'align-items': 1,
            width_pct: 100,
            'flex-grow': 1,
            'column-spacing': 4,
            'padding-left': 6,
            'padding-right': 6,
            'padding-bottom': 6,
        }, [ 'display', 'flex-flow', 'justify-content', 'align-items', 'width_pct', 'flex-grow', 'column-spacing', 'padding-left', 'padding-right', 'padding-bottom' ], 10, 0, true);

        plot.appendChild(gridLayer);
        plot.appendChild(seriesLayer);
        this.appendChild(plot);
        this._plot = plot;
        this._gridLayer = gridLayer;
        this._seriesLayer = seriesLayer;
        return plot;
    }

    _clearFallbackPlot() {
        if (this._plot && !this._plot._disposed) {
            this._children = this._children.filter((child) => child !== this._plot);
            try {
                this._plot._finalizeDispose(true);
            } catch (_) {}
        }
        this._plot = null;
        this._gridLayer = null;
        this._seriesLayer = null;
        this._gridLines = [];
        this._bars = [];
    }

    _updateDebugSnapshots(series, pointNum) {
        const bars = [];
        const type = Number(this._chartType) || 0;
        for (let pointIndex = 0; pointIndex < pointNum; pointIndex++) {
            for (let seriesIndex = 0; seriesIndex < series.length; seriesIndex++) {
                const color = Number(series[seriesIndex]?.color);
                const fallbackColor = type === 2 ? 0x2196f3 : (type === 3 ? 0x9c27b0 : 0x607d8b);
                const bar = {
                    _nativeStyleCache: new Map([[0, { 'background-color': Number.isFinite(color) ? color : fallbackColor }]])
                };
                bars.push(bar);
            }
        }
        this._bars = bars;
        const rawY = Array.isArray(this._divLineCount) ? Number(this._divLineCount[1]) : Number(this._divLineCount);
        const lineCount = Math.max(0, Number.isFinite(rawY) ? rawY : 0);
        this._gridLines = Array.from({ length: lineCount }, () => ({}));
    }

    _extractSeries() {
        const series = [];
        if (Number(this._chartType) === 3) {
            const scatterInput = this._resolveScatterInput();
            for (const item of scatterInput) {
                const points = [];
                const raw = Array.isArray(item?.data) ? item.data : [];
                for (let i = 1; i < raw.length; i += 2) {
                    points.push(Number(raw[i]) || 0);
                }
                series.push({
                    color: Number(item?.color) || 0x9c27b0,
                    points,
                });
            }
        } else {
            const src = Array.isArray(this._leftAxisData) ? this._leftAxisData : [];
            for (const item of src) {
                const raw = Array.isArray(item?.data) ? item.data : [];
                const points = [];
                for (let i = 0; i < raw.length; i++) {
                    points.push(Number(raw[i]) || 0);
                }
                const color = Number(item?.color);
                series.push({
                    color: Number.isFinite(color) ? color : 0x607d8b,
                    points,
                });
            }
        }
        if (series.length === 0) {
            series.push({ color: 0x607d8b, points: [] });
        }
        const targetPointNum = Math.max(0, Number(this._pointNum) || 0);
        const labelPointNum = Array.isArray(this._bottomAxisLabels) ? this._bottomAxisLabels.length : 0;
        let inferredPointNum = 0;
        for (const s of series) {
            if (s.points.length > inferredPointNum) {
                inferredPointNum = s.points.length;
            }
        }
        const normalizedPointNum = Math.max(targetPointNum, inferredPointNum, labelPointNum);
        for (const s of series) {
            while (s.points.length < normalizedPointNum) {
                s.points.push(0);
            }
        }
        return { series, pointNum: normalizedPointNum };
    }

    _resolveScatterInput() {
        if (Array.isArray(this._scatterData) && this._scatterData.length > 0) {
            return this._scatterData;
        }
        const xAxis = Array.isArray(this._bottomAxisData) && this._bottomAxisData.length > 0
            ? this._bottomAxisData
            : (Array.isArray(this._topAxisData) ? this._topAxisData : []);
        const yAxis = Array.isArray(this._leftAxisData) && this._leftAxisData.length > 0
            ? this._leftAxisData
            : (Array.isArray(this._rightAxisData) ? this._rightAxisData : []);
        if (xAxis.length === 0 || yAxis.length === 0) {
            return [];
        }
        const count = Math.max(xAxis.length, yAxis.length);
        const out = [];
        for (let i = 0; i < count; i++) {
            const xs = xAxis[i] || xAxis[xAxis.length - 1] || {};
            const ys = yAxis[i] || yAxis[yAxis.length - 1] || {};
            const xVals = Array.isArray(xs.data) ? xs.data : [];
            const yVals = Array.isArray(ys.data) ? ys.data : [];
            const pointCount = Math.max(xVals.length, yVals.length, Number(this._pointNum) || 0);
            const data = [];
            for (let j = 0; j < pointCount; j++) {
                data.push(Number(xVals[j]) || 0, Number(yVals[j]) || 0);
            }
            const color = Number(ys.color);
            out.push({
                color: Number.isFinite(color) ? color : 0x9c27b0,
                data,
            });
        }
        return out;
    }

    _resolveRange(series) {
        let minVal = 0;
        let maxVal = 1;
        if (Array.isArray(this._leftAxisRange) && this._leftAxisRange.length >= 2) {
            minVal = Number(this._leftAxisRange[0]);
            maxVal = Number(this._leftAxisRange[1]);
        } else {
            for (const item of series) {
                const points = Array.isArray(item?.points) ? item.points : [];
                for (const n of points) {
                    if (n > maxVal) {
                        maxVal = n;
                    }
                    if (n < minVal) {
                        minVal = n;
                    }
                }
            }
        }
        if (!Number.isFinite(minVal)) minVal = 0;
        if (!Number.isFinite(maxVal)) maxVal = 1;
        if (maxVal <= minVal) {
            maxVal = minVal + 1;
        }
        return { minVal, maxVal };
    }

    _clearLayerChildren(layer, arr) {
        if (!layer) {
            return;
        }
        for (const child of arr) {
            if (!child || child._disposed) {
                continue;
            }
            try {
                layer.removeChild(child);
            } catch (_) {}
        }
        arr.length = 0;
    }

    _rebuildGridLines() {
        const plot = this._ensurePlot();
        if (!plot || !this._gridLayer) {
            return;
        }
        this._clearLayerChildren(this._gridLayer, this._gridLines);
        const rawY = Array.isArray(this._divLineCount) ? Number(this._divLineCount[1]) : Number(this._divLineCount);
        const lineCount = Math.max(0, Number.isFinite(rawY) ? rawY : 0);
        for (let i = 0; i < lineCount; i++) {
            const line = new View({ uid: makeInternalUid('chart_grid_line') });
            line.nativeSetStyle({
                height: 1,
                width_pct: 100,
                'background-color': 0xd0d4da,
                opacity: 140,
            }, [ 'height', 'width_pct', 'background-color', 'opacity' ], 4, 0, true);
            this._gridLayer.appendChild(line);
            this._gridLines.push(line);
        }
    }

    _rebuildBars() {
        if (this._disposed) {
            return;
        }
        const { series, pointNum } = this._extractSeries();
        if (this._hasNativeChartBridge() && !this._nativeChartReady()) {
            this._updateDebugSnapshots(series, pointNum);
            this._ensureNative(this._parent ? this._parent._native : null);
            if (!this._nativeChartReady()) {
                return;
            }
        }
        if (this._nativeChartReady()) {
            this._clearFallbackPlot();
            this._updateDebugSnapshots(series, pointNum);
            try {
                lvgl.chartSetType(this._native, this._chartType);
                lvgl.chartSetPointNum(this._native, pointNum);
                const hdiv = Array.isArray(this._divLineCount) ? Number(this._divLineCount[0]) || 0 : 0;
                const vdiv = Array.isArray(this._divLineCount) ? Number(this._divLineCount[1]) || 0 : (Number(this._divLineCount) || 0);
                lvgl.chartSetDivLineCount(this._native, hdiv, vdiv);
                if (Array.isArray(this._leftAxisRange) && this._leftAxisRange.length >= 2) {
                    lvgl.chartSetRange(this._native, 0, Number(this._leftAxisRange[0]) || 0, Number(this._leftAxisRange[1]) || 1);
                }
                if (Array.isArray(this._rightAxisRange) && this._rightAxisRange.length >= 2) {
                    lvgl.chartSetRange(this._native, 1, Number(this._rightAxisRange[0]) || 0, Number(this._rightAxisRange[1]) || 1);
                }
                if (Array.isArray(this._bottomAxisRange) && this._bottomAxisRange.length >= 2) {
                    lvgl.chartSetRange(this._native, 2, Number(this._bottomAxisRange[0]) || 0, Number(this._bottomAxisRange[1]) || 1);
                }
                if (Array.isArray(this._topAxisRange) && this._topAxisRange.length >= 2) {
                    lvgl.chartSetRange(this._native, 4, Number(this._topAxisRange[0]) || 0, Number(this._topAxisRange[1]) || 1);
                }
                this._applyAxisTickNative(0, this._leftAxisOption);
                this._applyAxisTickNative(1, this._rightAxisOption);
                this._applyAxisTickNative(2, this._bottomAxisOption);
                this._applyAxisTickNative(4, this._topAxisOption);
                this._applyAxisLabelsNative(0, this._leftAxisLabels);
                this._applyAxisLabelsNative(1, this._rightAxisLabels);
                this._applyAxisLabelsNative(2, this._bottomAxisLabels);
                this._applyAxisLabelsNative(4, this._topAxisLabels);
                if (Number(this._chartType) === 3) {
                    lvgl.chartSetScatterData(this._native, this._resolveScatterInput());
                } else {
                    const merged = [];
                    if (Array.isArray(this._leftAxisData)) {
                        for (const item of this._leftAxisData) {
                            merged.push({
                                ...(item || {}),
                                axis: 0,
                            });
                        }
                    }
                    if (Array.isArray(this._rightAxisData)) {
                        for (const item of this._rightAxisData) {
                            merged.push({
                                ...(item || {}),
                                axis: 1,
                            });
                        }
                    }
                    if (typeof lvgl.chartSetAllSeries === 'function') {
                        lvgl.chartSetAllSeries(this._native, merged);
                    } else if (merged.length > 0) {
                        lvgl.chartSetLeftAxisData(this._native, merged);
                    } else {
                        lvgl.chartSetLeftAxisData(this._native, []);
                    }
                }
            } catch (_) {}
            return;
        }
        const plot = this._ensurePlot();
        if (!plot || !this._seriesLayer) {
            return;
        }
        this._rebuildGridLines();
        this._clearLayerChildren(this._seriesLayer, this._bars);
        if (pointNum === 0) {
            return;
        }
        const { minVal, maxVal } = this._resolveRange(series);
        const type = Number(this._chartType) || 0;
        const drawSize = Number(this._leftAxisOption?.drawSize);
        const maxHeight = Number.isFinite(drawSize) && drawSize > 0
            ? Math.max(40, Math.min(220, Math.round(drawSize)))
            : 120;
        const seriesCount = Math.max(1, series.length);
        const grouped = type === 2; // bar chart renders grouped bars per point
        for (let pointIndex = 0; pointIndex < pointNum; pointIndex++) {
            for (let seriesIndex = 0; seriesIndex < series.length; seriesIndex++) {
                const item = series[seriesIndex];
                const val = Number(item.points[pointIndex]) || 0;
                const bar = new View({ uid: makeInternalUid('chart_bar') });
                const ratio = Math.max(0, Math.min(1, (val - minVal) / (maxVal - minVal)));
                const height = Math.max(4, Math.min(maxHeight, Math.round(ratio * maxHeight)));
                const baseWidth = type === 3 ? 4 : (type === 1 ? 3 : 10);
                const width = grouped ? Math.max(3, Math.floor(baseWidth / seriesCount) + 3) : baseWidth;
                const radius = type === 3 ? 0x7FFF : 2;
                const fallbackColor = type === 2 ? 0x2196f3 : (type === 3 ? 0x9c27b0 : 0x607d8b);
                const color = Number.isFinite(item.color) ? Number(item.color) : fallbackColor;
                bar.nativeSetStyle({
                    width,
                    height,
                    'background-color': color,
                    'border-radius': radius,
                }, [ 'width', 'height', 'background-color', 'border-radius' ], 4, 0, true);
                this._seriesLayer.appendChild(bar);
                this._bars.push(bar);
            }
        }
    }

    setType(val) {
        if (this._disposed) {
            return;
        }
        this._chartType = Number(val) || 0;
        this._rebuildBars();
    }

    setDivLineCount(val) {
        if (this._disposed) {
            return;
        }
        this._divLineCount = Array.isArray(val) ? val.slice(0, 2) : [0, Number(val) || 0];
        this._rebuildBars();
    }

    setPointNum(val) {
        if (this._disposed) {
            return;
        }
        this._pointNum = Number(val) || 0;
        this._rebuildBars();
    }

    setLeftAxisData(val) {
        if (this._disposed) {
            return;
        }
        this._leftAxisData = Array.isArray(val) ? val : [];
        this._rebuildBars();
    }

    setScatterData(val) {
        if (this._disposed) {
            return;
        }
        this._scatterData = Array.isArray(val) ? val : [];
        this._rebuildBars();
    }

    setLeftAxisOption(val) {
        if (this._disposed) {
            return;
        }
        this._leftAxisOption = val || null;
        this._rebuildBars();
    }

    setBottomAxisOption(val) {
        if (this._disposed) {
            return;
        }
        this._bottomAxisOption = val || null;
        this._rebuildBars();
    }

    setBottomAxisData(val) {
        if (this._disposed) {
            return;
        }
        this._bottomAxisData = Array.isArray(val) ? val : [];
        this._rebuildBars();
    }

    setRightAxisOption(val) {
        if (this._disposed) {
            return;
        }
        this._rightAxisOption = val || null;
        this._rebuildBars();
    }

    setRightAxisData(val) {
        if (this._disposed) {
            return;
        }
        this._rightAxisData = Array.isArray(val) ? val : [];
        this._rebuildBars();
    }

    setTopAxisOption(val) {
        if (this._disposed) {
            return;
        }
        this._topAxisOption = val || null;
        this._rebuildBars();
    }

    setTopAxisData(val) {
        if (this._disposed) {
            return;
        }
        this._topAxisData = Array.isArray(val) ? val : [];
        this._rebuildBars();
    }

    setLeftAxisLabels(val) {
        if (this._disposed) {
            return;
        }
        this._leftAxisLabels = Array.isArray(val) ? val : [];
        this._rebuildBars();
    }

    setRightAxisLabels(val) {
        if (this._disposed) {
            return;
        }
        this._rightAxisLabels = Array.isArray(val) ? val : [];
        this._rebuildBars();
    }

    setTopAxisLabels(val) {
        if (this._disposed) {
            return;
        }
        this._topAxisLabels = Array.isArray(val) ? val : [];
        this._rebuildBars();
    }

    setBottomAxisLabels(val) {
        if (this._disposed) {
            return;
        }
        this._bottomAxisLabels = Array.isArray(val) ? val : [];
        this._rebuildBars();
    }

    setLeftAxisRange(val) {
        if (this._disposed) {
            return;
        }
        this._leftAxisRange = Array.isArray(val) ? val.slice(0, 2) : null;
        this._rebuildBars();
    }

    setRightAxisRange(val) {
        if (this._disposed) {
            return;
        }
        this._rightAxisRange = Array.isArray(val) ? val.slice(0, 2) : null;
        this._rebuildBars();
    }

    setTopAxisRange(val) {
        if (this._disposed) {
            return;
        }
        this._topAxisRange = Array.isArray(val) ? val.slice(0, 2) : null;
        this._rebuildBars();
    }

    setBottomAxisRange(val) {
        if (this._disposed) {
            return;
        }
        this._bottomAxisRange = Array.isArray(val) ? val.slice(0, 2) : null;
        this._rebuildBars();
    }
}

class Mask extends View {}

class TabView extends View {
    constructor(opts = {}) {
        super(opts);
        this._tabPosition = Number(opts.tabPosition) || 0;
        this._tabSize = Number(opts.tabSize) || 56;
        this._tabs = [];
        this._pages = [];
        this._tabButtons = [];
        this._header = null;
        this._content = null;
        this._activeIndex = 0;
    }

    _hasNativeTabView() {
        return typeof lvgl.tabviewCreate === 'function' && typeof lvgl.tabviewAddTab === 'function';
    }

    _nativeTabPosition() {
        return this._tabPosition || (1 << 2);
    }

    _createNative(parentNative) {
        if (this._hasNativeTabView()) {
            return lvgl.tabviewCreate(parentNative || lvgl.screenActive(), this._nativeTabPosition(), this._tabSize);
        }
        return super._createNative(parentNative);
    }

    nativeSetStyle(styleObj = {}, keys = [], len = 0, styleType = 0, isInit = true) {
        if (!this._hasNativeTabView()) {
            return super.nativeSetStyle(styleObj, keys, len, styleType, isInit);
        }
        const count = Number.isFinite(len) ? Math.min(len, keys.length) : keys.length;
        const nativeObj = Object.create(null);
        const nativeKeys = [];
        for (let i = 0; i < count; i++) {
            const key = keys[i];
            const val = styleObj[key];
            if (key === 'width' && isAutoSizeValue(val)) {
                nativeObj.width_pct = 100;
                nativeKeys.push('width_pct');
            } else if (key === 'height' && isAutoSizeValue(val)) {
                nativeObj.height_pct = 100;
                nativeKeys.push('height_pct');
            } else {
                nativeObj[key] = val;
                nativeKeys.push(key);
            }
        }
        return super.nativeSetStyle(nativeObj, nativeKeys, nativeKeys.length, styleType, isInit);
    }

    _isLeft() {
        return (this._tabPosition & (1 << 0)) !== 0;
    }

    _isRight() {
        return (this._tabPosition & (1 << 1)) !== 0;
    }

    _isTop() {
        return (this._tabPosition & (1 << 2)) !== 0 || this._tabPosition === 0;
    }

    _isBottom() {
        return (this._tabPosition & (1 << 3)) !== 0;
    }

    _isVerticalTabs() {
        return this._isLeft() || this._isRight();
    }

    _ensureStructure() {
        if (this._header && this._content) {
            return;
        }
        const verticalTabs = this._isVerticalTabs();
        if (this._hasNativeTabView()) {
            this._ensureNative(this._parent ? this._parent._native : null);
            const rootStyle = {
                display: 'flex',
                'flex-flow': verticalTabs ? 0 : 1,
            };
            const rootKeys = ['display', 'flex-flow'];
            if (this._styleCache.width == null && this._styleCache.width_pct == null) {
                rootStyle.width_pct = 100;
                rootKeys.push('width_pct');
            }
            if (this._styleCache.height == null && this._styleCache.height_pct == null) {
                rootStyle.height_pct = 100;
                rootKeys.push('height_pct');
            }
            super.nativeSetStyle(rootStyle, rootKeys, rootKeys.length, 0, true);

            const header = new View({ uid: makeInternalUid('tab_header') });
            const content = new View({ uid: makeInternalUid('tab_content') });
            header._parent = this;
            content._parent = this;
            Object.assign(header._styleCache, {
                display: 'flex',
                'flex-flow': verticalTabs ? 1 : 0,
                'justify-content': 0,
                'align-items': 0,
                ...(verticalTabs ? { width: this._tabSize, height_pct: 100 } : { width_pct: 100, height: this._tabSize }),
                'padding-left': 4,
                'padding-right': 4,
                'padding-top': 4,
                'padding-bottom': 4,
                'row-spacing': verticalTabs ? 4 : 0,
                'column-spacing': 4,
            });
            Object.assign(content._styleCache, {
                display: 'flex',
                'flex-flow': 3,
                'flex-grow': 1,
                ...(verticalTabs ? { height_pct: 100 } : { width_pct: 100 }),
            });
            const headerAfterContent = this._isBottom() || this._isRight();
            this._children = headerAfterContent ? [content, header] : [header, content];
            this._header = header;
            this._content = content;
            return;
        }
        const rootFlow = verticalTabs ? 0 : 1;
        this.nativeSetStyle({
            display: 'flex',
            'flex-flow': rootFlow,
            width_pct: 100,
            height_pct: 100,
        }, [ 'display', 'flex-flow', 'width_pct', 'height_pct' ], 4, 0, true);

        const header = new View({ uid: makeInternalUid('tab_header') });
        const content = new View({ uid: makeInternalUid('tab_content') });
        header.nativeSetStyle({
            display: 'flex',
            'flex-flow': verticalTabs ? 1 : 0,
            'justify-content': 0,
            'align-items': 0,
            ...(verticalTabs ? { width: this._tabSize, height_pct: 100 } : { width_pct: 100, height: this._tabSize }),
            'padding-left': 4,
            'padding-right': 4,
            'padding-top': 4,
            'padding-bottom': 4,
            'row-spacing': verticalTabs ? 4 : 0,
            'column-spacing': 4,
        }, [ 'display', 'flex-flow', 'justify-content', 'align-items', 'width', 'height', 'width_pct', 'height_pct', 'padding-left', 'padding-right', 'padding-top', 'padding-bottom', 'row-spacing', 'column-spacing' ], 14, 0, true);
        content.nativeSetStyle({
            display: 'flex',
            'flex-flow': 3,
            'flex-grow': 1,
            ...(verticalTabs ? { height_pct: 100 } : { width_pct: 100 }),
        }, [ 'display', 'flex-flow', 'flex-grow', 'width_pct', 'height_pct' ], 5, 0, true);
        const headerAfterContent = this._isBottom() || this._isRight();
        if (headerAfterContent) {
            super.appendChild(content);
            super.appendChild(header);
        } else {
            super.appendChild(header);
            super.appendChild(content);
        }
        this._header = header;
        this._content = content;
    }

    _setActiveIndex(index) {
        const next = Math.max(0, Math.min(index, this._pages.length - 1));
        this._activeIndex = next;
        if (this._hasNativeTabView()) {
            if (this._native && typeof lvgl.tabviewSetActive === 'function' && this._pages.length > 0) {
                try {
                    lvgl.tabviewSetActive(this._native, next);
                } catch (_) {}
            }
            for (let i = 0; i < this._pages.length; i++) {
                const page = this._pages[i];
                if (page && !page._disposed) {
                    page._styleCache.display = i === next ? 'flex' : 'none';
                }
            }
            for (let i = 0; i < this._tabButtons.length; i++) {
                const btn = this._tabButtons[i];
                if (btn && !btn._disposed) {
                    btn._styleCache.opacity = i === next ? 255 : 180;
                }
            }
            requestRefresh();
            return;
        }
        for (let i = 0; i < this._pages.length; i++) {
            const page = this._pages[i];
            if (!page || page._disposed) {
                continue;
            }
            const hidden = i !== next;
            page.nativeSetStyle({
                display: hidden ? 'none' : 'flex',
            }, [ 'display' ], 1, 0, false);
        }
        for (let i = 0; i < this._tabButtons.length; i++) {
            const btn = this._tabButtons[i];
            if (!btn || btn._disposed) {
                continue;
            }
            btn.nativeSetStyle({
                opacity: i === next ? 255 : 180,
            }, [ 'opacity' ], 1, 0, false);
        }
        requestRefresh();
    }

    setTab(name, child) {
        if (this._disposed) {
            return;
        }
        this._ensureStructure();
        this._tabs.push(String(name ?? ''));
        const btn = new Button({ uid: makeInternalUid('tab_btn') });
        btn._handleNativeEvent = (code) => {
            if (Number(code) === 7) {
                const idx = this._tabButtons.indexOf(btn);
                this._setActiveIndex(idx);
                return true;
            }
            return false;
        };
        const verticalTabs = this._isVerticalTabs();
        if (this._hasNativeTabView()) {
            Object.assign(btn._styleCache, {
                ...(verticalTabs ? { width_pct: 100, height: 44 } : { height_pct: 100, 'flex-grow': 1 }),
                'border-radius': 8,
                opacity: 180,
                'padding-left': 8,
                'padding-right': 8,
            });
            btn._parent = this._header;
            this._header._children.push(btn);
        } else {
            btn.addEventListener(7);
            btn.nativeSetStyle({
                ...(verticalTabs ? { width_pct: 100, height: 44 } : { height_pct: 100, 'flex-grow': 1 }),
                'border-radius': 8,
                opacity: 180,
                'padding-left': 8,
                'padding-right': 8,
            }, [ 'height', 'width', 'width_pct', 'height_pct', 'flex-grow', 'border-radius', 'opacity', 'padding-left', 'padding-right' ], 9, 0, true);
            const label = new Text({ uid: makeInternalUid('tab_btn_label') });
            label.setText(String(name ?? ''));
            btn.appendChild(label);
            this._header.appendChild(btn);
        }
        this._tabButtons.push(btn);
        if (child) {
            if (this._hasNativeTabView()) {
                child._parent = this._content;
                child._ensureNative(null);
                if (this._native && child._native && typeof lvgl.tabviewAddTab === 'function') {
                    try {
                        lvgl.tabviewAddTab(this._native, name, child._native);
                    } catch (_) {}
                }
                this._content._children.push(child);
            } else {
                this._content.appendChild(child);
            }
            this._pages.push(child);
            child.nativeSetStyle({
                width_pct: 100,
                height_pct: 100,
            }, [ 'width_pct', 'height_pct' ], 2, 0, false);
            if (this._pages.length === 1) {
                this._setActiveIndex(0);
            } else {
                if (this._hasNativeTabView()) {
                    child._styleCache.display = 'none';
                } else {
                    child.nativeSetStyle({ display: 'none' }, [ 'display' ], 1, 0, false);
                }
            }
        }
    }

    removeChild(child) {
        if (this._disposed || !child) {
            return;
        }
        const idx = this._pages.indexOf(child);
        if (idx >= 0) {
            const btn = this._tabButtons[idx];
            if (this._hasNativeTabView()) {
                if (btn && this._header) {
                    this._header._children = this._header._children.filter((it) => it !== btn);
                    btn._finalizeDispose(false);
                }
                if (this._content) {
                    this._content._children = this._content._children.filter((it) => it !== child);
                }
                child._finalizeDispose(true);
            } else {
                if (btn && this._header) {
                    try {
                        this._header.removeChild(btn);
                    } catch (_) {}
                }
                if (this._content) {
                    try {
                        this._content.removeChild(child);
                    } catch (_) {}
                }
            }
            this._pages.splice(idx, 1);
            this._tabButtons.splice(idx, 1);
            this._tabs.splice(idx, 1);
            if (this._pages.length === 0) {
                this._activeIndex = 0;
                return;
            }
            const nextIndex = Math.min(this._activeIndex, this._pages.length - 1);
            this._setActiveIndex(nextIndex);
            return;
        }
        super.removeChild(child);
    }

    _finalizeDispose(deleteNative) {
        if (this._hasNativeTabView()) {
            for (const page of [...this._pages]) {
                if (page && !page._disposed) {
                    page._finalizeDispose(false);
                }
            }
            for (const btn of [...this._tabButtons]) {
                if (btn && !btn._disposed) {
                    btn._finalizeDispose(false);
                }
            }
        }
        super._finalizeDispose(deleteNative);
        this._tabs = [];
        this._pages = [];
        this._tabButtons = [];
        this._header = null;
        this._content = null;
        this._activeIndex = 0;
    }
}

const bridge = {
    NativeRender: {
        NativeComponents: {
            View,
            Text,
            Button,
            Textarea,
            Image,
            Switch,
            Slider,
            Checkbox,
            Dropdownlist,
            Calendar,
            Chart,
            Mask,
            TabView,
        },
        get dimensions() {
            return {
                get window() {
                    return getDimensions();
                }
            };
        },
        RenderUtil: {
            refreshWindow() {
                requestRefresh();
            }
        },
        Animate: NativeAnimateCompat,
        theme: {
            setTheme(options = {}) {
                if (typeof lvgl.setTheme === 'function') {
                    lvgl.setTheme(options ?? {});
                    requestRefresh();
                }
            },
            resetTheme() {
                if (typeof lvgl.resetTheme === 'function') {
                    lvgl.resetTheme();
                    requestRefresh();
                }
            },
        },
    },
};

if (!globalThis[BRIDGE_SYMBOL]) {
    globalThis[BRIDGE_SYMBOL] = bridge;
}

export function init() {
    ensureRuntime();
}

export function setRootComponent(comp) {
    rootComp = comp || null;
}

export function __debugFlushNativeEvents() {
    dispatchNativeEvents();
}

export default globalThis[BRIDGE_SYMBOL];
