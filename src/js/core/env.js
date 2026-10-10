const core = globalThis[Symbol.for('tjs.internal.core')];

const env = new Proxy({}, {
    ownKeys() {
        return core.envKeys();
    },
    getOwnPropertyDescriptor(_, prop) {
        if (typeof prop !== 'string') {
            return undefined;
        }

        try {
            return {
                value: core.getenv(prop),
                writable: true,
                enumerable: true,
                configurable: true,
            };
        } catch (_) {
            return undefined;
        }
    },
    get(_, prop) {
        if (prop === Symbol.toStringTag) {
            return JSON.stringify(core.environ(), null, 2);
        } else if (prop === 'toJSON') {
            return () => core.environ();
        } else if (typeof prop === 'string') {
            try {
                return core.getenv(prop);
            } catch (_) { /* Ignored. */ }
        }

        return undefined;
    },
    set(_, prop, val) {
        core.setenv(prop, val);

        return true;
    },
    deleteProperty(_, prop) {
        core.unsetenv(prop);

        return true;
    },
    has(_, key) {
        if (typeof key !== 'string') {
            return false;
        }

        try {
            core.getenv(key);
            return true;
        } catch (_) {
            return false;
        }
    }
});

export default env;
