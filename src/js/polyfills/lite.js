// Startup-critical minimal polyfills set.
// Keep this small for eval/help/version fast path.

/*
 * Polyfill: TypedArray.prototype.copy()
 * grpc-js calls message.copy() on Uint8Array, which is available on Node's Buffer
 * but not on standard TypedArrays.
 */
let needsUint8ArrayCopyPatch = false;
if (typeof Uint8Array.prototype.copy !== 'function') {
    needsUint8ArrayCopyPatch = true;
} else {
    try {
        const src = new Uint8Array([1, 2, 3]);
        const dst = new Uint8Array(3);
        // Node-compatible behavior should clamp out-of-range targetStart to return 0.
        if (src.copy(dst, 10, 0, 2) !== 0) {
            needsUint8ArrayCopyPatch = true;
        }
    } catch (_) {
        needsUint8ArrayCopyPatch = true;
    }
}

if (needsUint8ArrayCopyPatch) {
    Object.defineProperty(Uint8Array.prototype, 'copy', {
        configurable: true,
        writable: true,
        enumerable: false,
        value: function(target, targetStart, sourceStart, sourceEnd) {
            if (!target || typeof target.length !== 'number')
                throw new TypeError('copy target must be a Uint8Array-like object');

            const srcLen = this.length >>> 0;
            const dstLen = target.length >>> 0;

            let to = targetStart == null ? 0 : Number(targetStart);
            let from = sourceStart == null ? 0 : Number(sourceStart);
            let end = sourceEnd == null ? srcLen : Number(sourceEnd);

            to = Number.isFinite(to) ? Math.trunc(to) : 0;
            from = Number.isFinite(from) ? Math.trunc(from) : 0;
            end = Number.isFinite(end) ? Math.trunc(end) : srcLen;

            if (to < 0) to = 0;
            if (from < 0) from = 0;
            if (to >= dstLen || from >= srcLen || end <= from)
                return 0;
            if (end > srcLen) end = srcLen;

            const len = Math.min(end - from, dstLen - to);
            for (let i = 0; i < len; i++)
                target[to + i] = this[from + i];
            return len;
        },
    });
}

import './global.js';
import './timers.js';
import './event-target-polyfill.js';
import './structured-clone.js';

import './abba.js';
import './text-encoding.js';
import './text-encode-transform.js';
import './url.js';

import './console.js';
import './crypto.js';
import './performance.js';

import 'web-streams-polyfill/polyfill';
import './abort-controller.js';
