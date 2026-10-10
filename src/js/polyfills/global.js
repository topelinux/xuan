Object.defineProperty(globalThis, 'global', {
    enumerable: true,
    configurable: true,
    writable: true,
    value: globalThis,
});

Object.defineProperty(globalThis, 'self', {
    enumerable: true,
    get() {
        return globalThis;
    },
    set() {}
});
