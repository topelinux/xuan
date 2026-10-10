async function applyNodeBootstrap() {
    if (globalThis.__tjsNodeBootstrapApplied) {
        return;
    }
    Object.defineProperty(globalThis, '__tjsNodeBootstrapApplied', {
        value: true,
        configurable: true,
    });

    if (typeof globalThis.global === 'undefined') {
        Object.defineProperty(globalThis, 'global', {
            value: globalThis,
            writable: true,
            configurable: true,
            enumerable: true,
        });
    }

    if (typeof globalThis.process === 'undefined') {
        const mod = await import('node:process');
        globalThis.process = mod.default ?? mod.process ?? mod;
    }

    let BufferCtor = globalThis.Buffer;

    if (typeof BufferCtor === 'undefined') {
        const mod = await import('buffer');
        if (mod?.Buffer) {
            BufferCtor = mod.Buffer;
        }
    }

    if (typeof BufferCtor !== 'undefined') {
        Object.defineProperty(globalThis, 'Buffer', {
            value: BufferCtor,
            writable: true,
            configurable: true,
            enumerable: false,
        });
    }

    if (typeof globalThis.setImmediate === 'undefined' && typeof globalThis.setTimeout === 'function') {
        globalThis.setImmediate = (callback, ...args) => {
            if (typeof callback !== 'function') {
                throw new TypeError('callback must be a function');
            }
            return globalThis.setTimeout(() => callback(...args), 0);
        };

        globalThis.clearImmediate = (id) => globalThis.clearTimeout(id);
    }

    ensureCallSiteCompat();
    ensureUnhandledRejectionCompat();
}

function ensureCallSiteCompat() {
    if (typeof Error?.captureStackTrace !== 'function') {
        return;
    }

    const originalPrepare = Error.prepareStackTrace;
    const holder = {};

    try {
        Error.prepareStackTrace = (_err, stack) => stack;
        Error.captureStackTrace(holder);
    } catch {
        Error.prepareStackTrace = originalPrepare;
        return;
    }

    Error.prepareStackTrace = originalPrepare;

    const frame = holder.stack?.[0];
    if (!frame || typeof frame !== 'object') {
        return;
    }

    const proto = Object.getPrototypeOf(frame);
    if (!proto) {
        return;
    }

    defineMissingMethod(proto, 'isEval', () => false);
    defineMissingMethod(proto, 'getEvalOrigin', () => undefined);
    defineMissingMethod(proto, 'getThis', () => undefined);
    defineMissingMethod(proto, 'getTypeName', () => undefined);
    defineMissingMethod(proto, 'getMethodName', () => undefined);
}

function defineMissingMethod(proto, name, fn) {
    if (typeof proto[name] === 'function') {
        return;
    }

    try {
        Object.defineProperty(proto, name, {
            value: fn,
            configurable: true,
            writable: true,
        });
    } catch {
        // ignore
    }
}

function ensureUnhandledRejectionCompat() {
    if (typeof globalThis.addEventListener !== 'function') {
        return;
    }
    if (globalThis.__tjsNodeUnhandledRejectionCompat) {
        return;
    }

    Object.defineProperty(globalThis, '__tjsNodeUnhandledRejectionCompat', {
        value: true,
        configurable: true,
    });

    globalThis.addEventListener('unhandledrejection', (event) => {
        if (!hasUnhandledRejectionListeners()) {
            reportUnhandledRejection(event.reason);
            event.preventDefault();
            return;
        }

        try {
            if (typeof process?.emit === 'function') {
                process.emit('unhandledRejection', event.reason, event.promise);
            }
        } catch {
            // ignore
        }
        event.preventDefault();
    });
}

function hasUnhandledRejectionListeners() {
    if (!process) {
        return false;
    }

    if (typeof process.listenerCount === 'function') {
        return process.listenerCount('unhandledRejection') > 0;
    }

    if (typeof process.listeners === 'function') {
        return process.listeners('unhandledRejection').length > 0;
    }

    return false;
}

function reportUnhandledRejection(reason) {
    const stack = typeof reason?.stack === 'string' ? reason.stack : null;
    const message = typeof reason?.message === 'string' ? reason.message : null;
    const name = typeof reason?.name === 'string' ? reason.name : 'Error';
    let output = stack ?? '';

    if (message && (!stack || !stack.includes(message))) {
        output = stack ? `${name}: ${message}\n${stack}` : `${name}: ${message}`;
    } else if (!output) {
        output = String(reason);
    }

    console.error(output);

    if (typeof tjs?.exit === 'function') {
        tjs.exit(1);
    }
}

export default applyNodeBootstrap;
