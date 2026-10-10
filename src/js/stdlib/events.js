const core = globalThis[Symbol.for('tjs.internal.core')];
const nativeEvents = core.events;
const NativeEventEmitter = nativeEvents.EventEmitter;

function EventEmitter() {
    if (new.target === undefined) {
        return Reflect.construct(EventEmitter, [], EventEmitter);
    }
    return Reflect.construct(NativeEventEmitter, [], new.target);
}

EventEmitter.prototype = NativeEventEmitter.prototype;
Object.setPrototypeOf(EventEmitter, NativeEventEmitter);
Object.defineProperty(EventEmitter, 'defaultMaxListeners', {
    get() {
        return NativeEventEmitter.defaultMaxListeners;
    },
    set(value) {
        NativeEventEmitter.defaultMaxListeners = value;
    },
    enumerable: true,
    configurable: true
});
Object.defineProperty(EventEmitter, 'EventEmitter', {
    value: EventEmitter,
    writable: true,
    enumerable: true,
    configurable: true
});
Object.defineProperty(EventEmitter, 'default', {
    value: EventEmitter,
    writable: true,
    enumerable: true,
    configurable: true
});

function toAbortError() {
    const err = new Error('The operation was aborted');
    err.name = 'AbortError';
    return err;
}

function validateEmitter(emitter) {
    if (!emitter || typeof emitter.on !== 'function' || typeof emitter.off !== 'function') {
        throw new TypeError('The "emitter" argument must be an EventEmitter-like object');
    }
}

function once(emitter, eventName, options = undefined) {
    validateEmitter(emitter);

    return new Promise((resolve, reject) => {
        const { signal } = options || {};
        if (signal?.aborted) {
            reject(toAbortError());
            return;
        }

        const onEvent = (...args) => {
            cleanup();
            resolve(args);
        };
        const onError = (err) => {
            cleanup();
            reject(err);
        };
        const onAbort = () => {
            cleanup();
            reject(toAbortError());
        };
        const cleanup = () => {
            emitter.off(eventName, onEvent);
            if (eventName !== 'error') {
                emitter.off('error', onError);
            }
            signal?.removeEventListener('abort', onAbort);
        };

        emitter.on(eventName, onEvent);
        if (eventName !== 'error') {
            emitter.on('error', onError);
        }
        signal?.addEventListener('abort', onAbort, { once: true });
    });
}

function on(emitter, eventName, options = undefined) {
    validateEmitter(emitter);

    const { signal } = options || {};
    const queue = [];
    const pending = [];
    let finished = false;
    let error = null;

    const finish = () => {
        if (finished) return;
        finished = true;
        emitter.off(eventName, onEvent);
        if (eventName !== 'error') {
            emitter.off('error', onError);
        }
        signal?.removeEventListener('abort', onAbort);
        while (pending.length > 0) {
            const { resolve, reject } = pending.shift();
            if (error) reject(error);
            else resolve({ done: true, value: undefined });
        }
    };

    const onEvent = (...args) => {
        if (finished) return;
        if (pending.length > 0) {
            pending.shift().resolve({ done: false, value: args });
            return;
        }
        queue.push(args);
    };

    const onError = (err) => {
        if (finished) return;
        error = err;
        finish();
    };

    const onAbort = () => {
        if (finished) return;
        error = toAbortError();
        finish();
    };

    if (signal?.aborted) {
        error = toAbortError();
        finished = true;
    } else {
        emitter.on(eventName, onEvent);
        if (eventName !== 'error') {
            emitter.on('error', onError);
        }
        signal?.addEventListener('abort', onAbort, { once: true });
    }

    return {
        next() {
            if (queue.length > 0) {
                return Promise.resolve({ done: false, value: queue.shift() });
            }
            if (finished) {
                if (error) {
                    return Promise.reject(error);
                }
                return Promise.resolve({ done: true, value: undefined });
            }
            return new Promise((resolve, reject) => pending.push({ resolve, reject }));
        },
        return() {
            finish();
            return Promise.resolve({ done: true, value: undefined });
        },
        throw(err) {
            error = err;
            finish();
            return Promise.reject(err);
        },
        [Symbol.asyncIterator]() {
            return this;
        }
    };
}

function getEventListeners(emitter, eventName) {
    validateEmitter(emitter);
    if (typeof emitter.listeners === 'function') {
        return emitter.listeners(eventName);
    }
    return [];
}

function setMaxListeners(n, ...targets) {
    if (targets.length === 0) {
        EventEmitter.defaultMaxListeners = n;
        return;
    }
    for (const target of targets) {
        if (!target || typeof target.setMaxListeners !== 'function') {
            throw new TypeError('The "target" argument must support setMaxListeners');
        }
        target.setMaxListeners(n);
    }
}

function getMaxListeners(target) {
    if (!target || typeof target.getMaxListeners !== 'function') {
        throw new TypeError('The "target" argument must support getMaxListeners');
    }
    return target.getMaxListeners();
}

EventEmitter.once = once;
EventEmitter.on = on;
export {
    EventEmitter,
    once,
    on,
    getEventListeners,
    setMaxListeners,
    getMaxListeners
};

export const defaultMaxListeners = nativeEvents.defaultMaxListeners;
export default EventEmitter;
