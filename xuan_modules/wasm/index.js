const platform = globalThis.navigator?.userAgentData?.platform ?? '';
const isWindows = platform === 'windows' || platform === 'Windows';
const bindingExts = isWindows ? [ 'dll', 'so', 'dylib' ] : [ 'so', 'dylib', 'dll' ];
const bindingRoots = [ './native', '../../build' ];
const bindingPaths = [];
for (const root of bindingRoots) {
    for (const ext of bindingExts) {
        bindingPaths.push(`${root}/xuan-wasm.${ext}`);
    }
}

let wasm;
let lastErr;
for (const p of bindingPaths) {
    try {
        wasm = await import(p);
        break;
    } catch (err) {
        lastErr = err;
    }
}
if (!wasm) {
    throw lastErr;
}
globalThis[Symbol.for('xuan.internal.wasm.native')] = wasm;

const kWasmModule = Symbol('kWasmModule');
const kWasmModuleRef = Symbol('kWasmModuleRef');
const kWasmExports = Symbol('kWasmExports');
const kWasmInstance = Symbol('kWasmInstance');

class CompileError extends Error {
    constructor(...args) {
        super(...args);
        this.name = 'CompileError';
    }
}

class LinkError extends Error {
    constructor(...args) {
        super(...args);
        this.name = 'LinkError';
    }
}

class RuntimeError extends Error {
    constructor(...args) {
        super(...args);
        this.name = 'RuntimeError';
    }
}

function getWasmError(e) {
    switch (e.wasmError) {
        case 'CompileError':
            return new CompileError(e.message);
        case 'LinkError':
            return new LinkError(e.message);
        case 'RuntimeError':
            return new RuntimeError(e.message);
        default:
            return new TypeError(`Invalid WASM error: ${e.wasmError}`);
    }
}

function callWasmFunction(name, ...args) {
    const instance = this;
    try {
        return instance.callFunction(name, ...args);
    } catch (e) {
        if (e.wasmError) {
            throw getWasmError(e);
        }
        throw e;
    }
}

function buildInstance(mod) {
    try {
        return wasm.buildInstance(mod);
    } catch (e) {
        if (e.wasmError) {
            throw getWasmError(e);
        }
        throw e;
    }
}

function parseModule(buf) {
    try {
        return wasm.parseModule(buf);
    } catch (e) {
        if (e.wasmError) {
            throw getWasmError(e);
        }
        throw e;
    }
}

class Module {
    constructor(buf) {
        this[kWasmModule] = parseModule(buf);
    }

    static exports(module) {
        return wasm.moduleExports(module[kWasmModule]);
    }

    static imports(module) {
        void module;
        return {};
    }
}

class Instance {
    constructor(module, importObject = {}) {
        for (const ns of Object.values(importObject)) {
            if (ns && typeof ns === 'object' && typeof ns._configure === 'function') {
                try {
                    ns._configure(module[kWasmModule]);
                } catch (e) {
                    if (e && e.wasmError) {
                        throw getWasmError(e);
                    }
                    throw new LinkError(e?.message ?? String(e));
                }
                break;
            }
        }

        const instance = buildInstance(module[kWasmModule]);
        const _exports = Module.exports(module);
        const exports = Object.create(null);

        for (const item of _exports) {
            if (item.kind === 'function') {
                exports[item.name] = callWasmFunction.bind(instance, item.name);
            }
        }

        this[kWasmInstance] = instance;
        this[kWasmExports] = Object.freeze(exports);
        this[kWasmModuleRef] = module;
    }

    get exports() {
        return this[kWasmExports];
    }
}

class XuanWebAssembly {
    Module = Module;
    Instance = Instance;
    CompileError = CompileError;
    LinkError = LinkError;
    RuntimeError = RuntimeError;

    async compile(src) {
        return new Module(src);
    }

    async instantiate(src, importObject) {
        const module = await this.compile(src);
        const instance = new Instance(module, importObject);
        return { module, instance };
    }
}

const webAssembly = new XuanWebAssembly();
Object.defineProperty(globalThis, 'WebAssembly', {
    enumerable: true,
    configurable: true,
    writable: true,
    value: webAssembly
});

export {
    Module,
    Instance,
    CompileError,
    LinkError,
    RuntimeError,
    webAssembly as WebAssembly,
    wasm as native
};
export default webAssembly;
